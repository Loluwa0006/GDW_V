// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "DiversionUtils.h"

class IDiversionWorker
{
public:
	virtual ~IDiversionWorker() = default;
	/**
	 * Name describing the work that this worker does. Used for factory method hookup.
	 */
	virtual FName GetName() const = 0;

	/**
	 * Function that actually does the work. Can be executed on another thread.
	 */
	virtual bool Execute(class FDiversionCommand& InCommand) = 0;

	/**
	 * Updates the state of any items after completion (if necessary). This is always executed on the main thread.
	 * @returns true if states were updated. The provider ORs this across the commands it
	 *          drains and broadcasts a state change when any of them reports true.
	 *
	 * NOTE for overrides: this base implementation currently returns true unconditionally -
	 * it applies sync status, which has no "nothing changed" answer. So `bUpdated &= MyUpdate()`
	 * means "report what MY update did" and `bUpdated |= MyUpdate()` means "always report a
	 * change"; both read as if they combine with the base result, but neither does. Prefer
	 * naming the parts (see FDiversionGetFileLocksWorker::UpdateStates) over the compound
	 * operators, so the intent survives if this base ever gains a real answer.
	 */
	virtual bool UpdateStates() const;

public:
	/** Storing the state of agent sync
	* Relevant for most operations, since we need the FS state to be stable
	*/
	TOptional<DiversionUtils::EDiversionWsSyncStatus> SyncStatus;

	TArray<FString> OpenedPackagePaths = TArray<FString>();
	bool ReloadStatusReuired = false;

	/** Read-only paths blocking the sync, mapped to the agent's message for the user.
	* Unset when this worker didn't read the sync progress, so it leaves the shown notifications as they are.
	*/
	TOptional<TMap<FString, FString>> ReadOnlyPaths;

private:
	bool UpdateSyncStatus() const;

	void ReleaseLockedPackages() const;

	void UpdateReadOnlyPathNotifications() const;
};

typedef TSharedRef<IDiversionWorker, ESPMode::ThreadSafe> FDiversionWorkerRef;


