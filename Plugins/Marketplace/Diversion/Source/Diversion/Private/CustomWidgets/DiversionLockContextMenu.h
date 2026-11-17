// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiversionUtils.h"

class UToolMenu;

/** Per-file lock facts a context-menu decision needs (built from FDiversionState) */
struct FDiversionLockMenuFileInfo
{
	FString FilePath;
	bool bIsLocked = false;
	bool bLockedByOther = false;
	/** Holder display name (or id) when locked by another user */
	FString HolderDisplayName;
};

/** Which files each lock action would apply to, for the current selection */
struct FDiversionLockMenuState
{
	/** Unlocked files - targets for Lock */
	TArray<FString> LockTargets;
	/** Files locked by the current user - targets for Unlock */
	TArray<FString> UnlockTargets;
	/** Files locked by other users - targets for Force Unlock (keeps holder for the dialog) */
	TArray<FDiversionLockMenuFileInfo> ForceUnlockTargets;
};

/** Buckets every selected file into the lock action it is eligible for. Pure - unit tested. */
FDiversionLockMenuState ComputeLockMenuState(const TArray<FDiversionLockMenuFileInfo>& InFiles);

/** Confirmation text for Force Unlock, listing holder + file per target (capped). Pure - unit tested. */
FText BuildForceUnlockMessage(const TArray<FDiversionLockMenuFileInfo>& InTargets, int32 MaxListed = 10);

/**
 * Result-toast text for a completed lock mutation; empty when there is nothing to report.
 * bOutIsFailure selects the notification's completion state. Pure - unit tested.
 */
/** Toast for a lock mutation whose RPC never got an answer (transport-level failure):
 * names the interrupted operation and accounts for every file - processed-before-failure
 * and not-attempted counts - so a dead connection is never a silent no-op. */
FText BuildLockTransportFailureNotification(DiversionUtils::EDiversionLockAction InAction,
	int32 SuccessCount, int32 NotProcessedCount);

FText BuildLockResultNotification(DiversionUtils::EDiversionLockAction InAction, int32 SuccessCount,
	const TArray<DiversionUtils::FDiversionLockOpFailure>& InFailures, bool& bOutIsFailure);

/**
 * Adds the Diversion hard-lock actions (Lock / Unlock / Force Unlock) to the Content
 * Browser's engine-native "Revision Control" asset submenu. Entries appear only for
 * lock-eligible repos (ShouldCheckHardLocks) with the Diversion provider active.
 */
class FDiversionLockContextMenu
{
public:
	/** Extend the Revision Control submenu; safe to call repeatedly (Init can rerun without Close) */
	static void Register();
	static void Unregister();

private:
	static void PopulateSection(UToolMenu* InMenu);
	static void ExecuteLockAction(DiversionUtils::EDiversionLockAction InAction, TArray<FString> InFiles);
	static void ShowForceUnlockConfirmation(TArray<FDiversionLockMenuFileInfo> InTargets);
};
