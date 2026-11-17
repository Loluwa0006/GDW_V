// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiversionState.h"

/**
 * The rules for saving a file: how its lock state is classified, what the user may choose,
 * and what a failed lock request does to the save. Pure functions - unit-testable without
 * the engine or a backend.
 */
namespace DiversionUtils
{
/** Why a lock request failed: someone holds the lock (a fact about the file), or the
 * request itself failed (says nothing about the file). */
enum class ELockAcquisitionFailure : uint8
{
	HeldByKnownUser,
	HeldByUnknownUser,
	Unexpected
};

/** Turns a failed lock request (HTTP status + cached lock state) into a reason. The cache
 * names the holder when it can; the status code covers a cache that is behind. */
ELockAcquisitionFailure ClassifyLockAcquisitionFailure(int32 InStatusCode,
	const FDiversionLockInfo& InCachedLockInfo);

// Decision rules only - no UI, no network, no state. The gate (DiversionSaveDecisionGate)
// performs the actions these rules choose.

/** The lock situation of a file being saved */
enum class EDiversionSaveCategory : uint8
{
	/** You hold the lock - nothing to ask */
	LockedByMe,
	/** On the repo's lockable-extension list: the server locks it for you right after the
	 * save syncs, so any answer here would be overridden - nothing to ask */
	AutoLockedByRule,
	/** Someone else holds the lock. Saving still works (a save always syncs to your own
	 * workspace); the commit is rejected until they release it */
	HardLockedByOther,
	/** Another workspace has unpublished edits to this file (only reported while the
	 * soft-lock setting is on) */
	SoftLocked,
	/** No lock, no clash. The only category the "Prompt to Lock Assets on Save" setting
	 * controls */
	UnlockedLockable,
	/** The file on disk changed after this editor loaded or last saved it - most likely a
	 * teammate's commit the sync brought in. Saving would overwrite it. */
	ChangedOnDisk
};

/** The user's answer for one file in the save-decision dialog */
enum class EDiversionSaveChoice : uint8
{
	/** Take the lock, then save */
	LockAndSave,
	/** Save with no lock (shown as "Save Without Lock", "Save Anyway" or "Save Mine" per category) */
	SaveUnlocked,
	/** Do not save - the file stays dirty in the editor */
	Skip,
	/** Do not save - drop the unsaved changes and load the file from disk (ChangedOnDisk only) */
	AcceptIncoming
};

/** What the gate should do with one file */
enum class EDiversionSaveVetoAction : uint8
{
	/** Save it */
	Allow,
	/** Ask the user first */
	Prompt,
	/** Do not save: nobody can answer, and saving would overwrite a change on disk */
	Refuse
};

/**
 * The one decision function: Allow or Prompt, plus the file's category. Hard-lock state
 * outranks a soft clash, which outranks the auto-lock rule.
 *
 * InLockInfo is the lock state verified for this save (unset = no lock, or tier not
 * eligible). bInAutoLockedByRule = the file is on the repo's lockable-extension list
 * (false while the list is unresolved). bInInteractiveSession = a person performed this
 * save; when false (script, commandlet, editor exit) the answer is always Allow, even over
 * someone else's lock - nobody can answer a dialog, and the server rejects the commit of a
 * locked file anyway.
 *
 * Saves that must never be held (new packages, merges, autosave, PIE...) are let through by
 * the gate before this is called - see FDiversionSaveDecisionGate::ShouldNeverHoldSave.
 */
EDiversionSaveVetoAction RouteSaveDecision(const FDiversionLockInfo& InLockInfo,
	bool bInIsSoftClashed, bool bInAutoLockedByRule, bool bInHardLocksEligible,
	bool bInPromptOnSaveEnabled, bool bInInteractiveSession, EDiversionSaveCategory& OutCategory);

/**
 * The decision for a file whose disk copy changed after the editor loaded or last saved it,
 * taken before RouteSaveDecision: an overwrite outranks every lock state. Prompt when a person
 * can answer; Refuse otherwise - not saving loses nothing (the edit stays in the editor, the
 * file on disk stays as it is), and the next save someone makes asks.
 */
EDiversionSaveVetoAction RouteChangedOnDiskSave(bool bInInteractiveSession, EDiversionSaveCategory& OutCategory);

/**
 * The answer a dialog row starts on. Someone else's lock or a change on disk -> Skip; no lock
 * on offer -> Save without lock; otherwise the "Lock by Default on Save" setting picks between
 * Lock & Save and Save without lock.
 */
EDiversionSaveChoice DefaultSaveChoice(EDiversionSaveCategory InCategory, bool bInLockOfferAvailable,
	bool bInLockByDefault);

/** Fixed string for a choice, used in analytics and logs */
const TCHAR* SaveChoiceToString(EDiversionSaveChoice InChoice);

/** What a failed lock request does to the save: held by someone -> Skip (respect their
 * lock); anything else -> save without the lock (a broken lock service must not cost the
 * user their save). */
EDiversionSaveChoice ResolveLockFailure(ELockAcquisitionFailure InFailure);

}
