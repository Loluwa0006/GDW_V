// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionSaveDecision.h"

namespace DiversionUtils
{
ELockAcquisitionFailure ClassifyLockAcquisitionFailure(int32 InStatusCode,
	const FDiversionLockInfo& InCachedLockInfo)
{
	if (InCachedLockInfo.IsSet() && InCachedLockInfo.bLockedByOther)
	{
		return ELockAcquisitionFailure::HeldByKnownUser;
	}
	// 409 is the lock endpoint's conflict answer; 423 (Locked) is the same fact from any
	// other path. Both mean someone holds it, even when the cache does not know who yet.
	if (InStatusCode == 409 || InStatusCode == 423)
	{
		return ELockAcquisitionFailure::HeldByUnknownUser;
	}
	// There is no "not lockable" answer to classify: the backend's lock handler
	// (core file_locks.lock_file) checks tier and path validity only, never the repo's
	// lockable patterns, so a 400 is a malformed request like any other - not a verdict on
	// the file, and not something to stop asking about.
	return ELockAcquisitionFailure::Unexpected;
}

EDiversionSaveChoice ResolveLockFailure(ELockAcquisitionFailure InFailure)
{
	switch (InFailure)
	{
	case ELockAcquisitionFailure::HeldByKnownUser:
	case ELockAcquisitionFailure::HeldByUnknownUser:
		// Honor the holder's lock
		return EDiversionSaveChoice::Skip;

	case ELockAcquisitionFailure::Unexpected:
	default:
		// Never leave the user stuck
		return EDiversionSaveChoice::SaveUnlocked;
	}
}

EDiversionSaveVetoAction RouteSaveDecision(const FDiversionLockInfo& InLockInfo,
	bool bInIsSoftClashed, bool bInAutoLockedByRule, bool bInHardLocksEligible,
	bool bInPromptOnSaveEnabled, bool bInInteractiveSession, EDiversionSaveCategory& OutCategory)
{
	OutCategory = EDiversionSaveCategory::UnlockedLockable;

	// Hard-lock state outranks soft state (only meaningful on lock-eligible tiers)
	if (bInHardLocksEligible && InLockInfo.IsSet())
	{
		if (!InLockInfo.bLockedByOther)
		{
			OutCategory = EDiversionSaveCategory::LockedByMe;
			return EDiversionSaveVetoAction::Allow;
		}
		OutCategory = EDiversionSaveCategory::HardLockedByOther;
		// A save the user never asked for is not refused, not even for someone else's lock:
		// nobody is watching to see it fail, and a script treating a failed save as fatal
		// would break on a file it cannot do anything about. The server rejects the commit
		// either way, so enforcement is not lost - it moves to where someone will read it.
		return bInInteractiveSession ? EDiversionSaveVetoAction::Prompt : EDiversionSaveVetoAction::Allow;
	}

	if (bInIsSoftClashed)
	{
		OutCategory = EDiversionSaveCategory::SoftLocked;
		// Fail open when nobody can answer a dialog - matches pre-dialog behavior where
		// non-interactive saves of clashed files went through
		return bInInteractiveSession ? EDiversionSaveVetoAction::Prompt : EDiversionSaveVetoAction::Allow;
	}

	if (bInHardLocksEligible && bInAutoLockedByRule)
	{
		// The repo rule locks this file on sync whatever the user would answer - a "save
		// without lock" here would be silently reversed seconds later. Nothing to ask.
		OutCategory = EDiversionSaveCategory::AutoLockedByRule;
		return EDiversionSaveVetoAction::Allow;
	}

	OutCategory = EDiversionSaveCategory::UnlockedLockable;
	// The opt-in prompt only exists where a lock could actually be offered
	if (!bInHardLocksEligible || !bInPromptOnSaveEnabled)
	{
		return EDiversionSaveVetoAction::Allow;
	}
	return bInInteractiveSession ? EDiversionSaveVetoAction::Prompt : EDiversionSaveVetoAction::Allow;
}

EDiversionSaveVetoAction RouteChangedOnDiskSave(bool bInInteractiveSession, EDiversionSaveCategory& OutCategory)
{
	OutCategory = EDiversionSaveCategory::ChangedOnDisk;
	return bInInteractiveSession ? EDiversionSaveVetoAction::Prompt : EDiversionSaveVetoAction::Refuse;
}

EDiversionSaveChoice DefaultSaveChoice(EDiversionSaveCategory InCategory, bool bInLockOfferAvailable,
	bool bInLockByDefault)
{
	if (InCategory == EDiversionSaveCategory::HardLockedByOther)
	{
		// Honoring someone else's lock is the least surprising default
		return EDiversionSaveChoice::Skip;
	}
	if (InCategory == EDiversionSaveCategory::ChangedOnDisk)
	{
		// The only answer that changes nothing: both the edit and the file on disk survive
		return EDiversionSaveChoice::Skip;
	}
	return bInLockOfferAvailable && bInLockByDefault
		? EDiversionSaveChoice::LockAndSave
		: EDiversionSaveChoice::SaveUnlocked;
}

const TCHAR* SaveChoiceToString(EDiversionSaveChoice InChoice)
{
	switch (InChoice)
	{
	case EDiversionSaveChoice::LockAndSave:    return TEXT("lock_and_save");
	case EDiversionSaveChoice::SaveUnlocked:   return TEXT("save_without_lock");
	case EDiversionSaveChoice::Skip:           return TEXT("skip");
	case EDiversionSaveChoice::AcceptIncoming: return TEXT("accept_incoming");
	default:                                   return TEXT("unknown");
	}
}

}
