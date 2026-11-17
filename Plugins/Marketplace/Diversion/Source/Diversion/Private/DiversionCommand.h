// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "ISourceControlProvider.h"
#include "Misc/IQueuedWork.h"
#include "DiversionWorkspaceInfo.h"
#include "DiversionState.h"
#include "DiversionUtils.h"
#include "DiversionNotificationManager.h"

/**
 * Used to execute Diversion commands multi-threaded.
 */
class FDiversionCommand : public IQueuedWork
{
public:

	FDiversionCommand(const TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>& InOperation,
		const TSharedRef<class IDiversionWorker, ESPMode::ThreadSafe>& InWorker,
		EConcurrency::Type InConcurrency,
		const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete());

	/**
	 * Tells the queued work that it is being abandoned so that it can do
	 * per object clean up as needed. This will only be called if it is being
	 * abandoned before completion. NOTE: This requires the object to delete
	 * itself using whatever heap it was allocated in.
	 */
	virtual void Abandon() override;

	/**
	 * This method is also used to tell the object to cleanup but not before
	 * the object has finished it's work.
	 */
	virtual void DoThreadedWork() override;

	/** Save any results and call any registered callbacks. */
	ECommandResult::Type ReturnResults();


	/** Worker must call this to mark command completion */
	void MarkOperationCompleted(bool InCommandSuccessful);

	EConcurrency::Type GetConcurrency() const { return Concurrency; }

public:

	WorkspaceInfo WsInfo;

	bool IsAgentAlive;

	/** The sync status in time of calling the command.
	 * optimization to avoid calling the agent from the worker again.
	 * Should be used for workers that need to return fast and run periodically.
	 */
	DiversionUtils::EDiversionWsSyncStatus SyncStatus;

	TArray<FString> ConflictedFiles;

	/** Operation we want to perform - contains outward-facing parameters & results */
	TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe> Operation;

	/** The object that will actually do the work */
	TSharedRef<class IDiversionWorker, ESPMode::ThreadSafe> Worker;

	/** Delegate to notify when this operation completes */
	FSourceControlOperationComplete OperationCompleteDelegate;

	/** If true, this command has been processed by the version control thread*/
	volatile int32 bExecuteProcessed;

	/** If true, the version control command succeeded*/
	bool bCommandSuccessful;

	/** If true, this command will be automatically cleaned up in Tick() */
	bool bAutoDelete;

	/** Files to perform this operation on */
	TArray<FString> Files;

	/** Info and/or warning message storage*/
	TArray<FString> InfoMessages;

	/** Potential error message storage*/
	TArray<FString> ErrorMessages;

	/** Potential conflicted file paths to query again - used to know promptly if their state was resolved */
	TArray<FString> ExistingPotentialConflicts;

	/** Snapshot of files pending deletion at command creation time — used by Revert to skip RunReset */
	TSet<FString> FilesPendingDelete;

	/** Notification to show to the user as an Editor popup */
	TUniquePtr<FDiversionNotification> PopupNotification;

private:
	/** Perform the actual work of the command */
	void DoWork();

	/** All commands are running in a BG worker, this indicates if the caller wanted this to block game thread or not */
	EConcurrency::Type Concurrency;
};
