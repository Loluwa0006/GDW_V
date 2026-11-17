// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once
#include "DiversionState.h"
#include "SaveDecision/DiversionSaveDecisionGate.h"
#include "DiversionWorkspaceInfo.h"
#include "ISourceControlProvider.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CoreDelegates.h"
#include "Containers/Ticker.h"
#include "IDiversionWorker.h"
#include "DiversionVersion.h"
#include "CachedState.h"
#include "DiversionChangelistState.h"
#include "DiversionTimedDelegate.h"
#include "DiversionNotificationManager.h"
#include "DiversionVersionCompat.h"
#include "IDirectoryWatcher.h"

class FDiversionState;

class FDiversionCommand;

DECLARE_DELEGATE_RetVal(FDiversionWorkerRef, FGetDiversionWorker)


class FDiversionProvider : public ISourceControlProvider
{
public:
	FDiversionProvider() : SaveDecisionGate(*this),
						   DvVersion(FDiversionVersion(""), FTimespan::FromSeconds(3)),
						   WsInfo(WorkspaceInfo(), FTimespan::FromSeconds(3)),
						   SyncStatus(DiversionUtils::EDiversionWsSyncStatus::Paused, FTimespan::FromSeconds(1)),
						   bRepoWithSameNameExists(false, FTimespan::FromSeconds(15)),  // backend call - slow interval
						   bWorkspaceExistsInPath(false, FTimespan::FromSeconds(2)),
						   ChangelistState(MakeShared<FDiversionChangelistState>())
	{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
		// UE 5.8 exposes pre-submit validation through the "Ex" delegates; the non-Ex getters
		// are deprecated no-ops that return empty delegates (see RegisterPreSubmitValidationDelegates).
		OriginalPreChangelistValidationDelegateEx = ISourceControlModule::Get().GetRegisteredPreSubmitChangelistValidationEx();
		OriginalPreFilesValidationDelegateEx = ISourceControlModule::Get().GetRegisteredPreSubmitFilesValidationEx();
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		OriginalPreChangelistValidationDelegate = ISourceControlModule::Get().GetRegisteredPreSubmitChangelistValidation();
		OriginalPreFilesValidationDelegate = ISourceControlModule::Get().GetRegisteredPreSubmitFilesValidation();
#else
		OriginalPreChangelistValidationDelegate = ISourceControlModule::Get().GetRegisteredPreSubmitDataValidation();
#endif
	}

	/* ISourceControlProvider implementation */
	virtual void Init(bool bForceConnection = true) override;
	virtual void Close() override;
	virtual FText GetStatusText() const override;
	virtual TMap<EStatus, FString> GetStatus() const override;
	virtual bool IsEnabled() const override;
	virtual bool IsAvailable() const override;
	virtual const FName& GetName(void) const override;
	virtual bool QueryStateBranchConfig(const FString& ConfigSrc, const FString& ConfigDest) override { return false; }
	virtual void RegisterStateBranches(const TArray<FString>& BranchNames, const FString& ContentRoot) override {}
	virtual int32 GetStateBranchIndex(const FString& InBranchName) const override { return INDEX_NONE; }
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	virtual bool GetStateBranchAtIndex(int32 BranchIndex, FString& OutBranchName) const override { return false; }
#endif
	virtual ECommandResult::Type GetState(const TArray<FString>& InFiles, TArray<FSourceControlStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual ECommandResult::Type GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual TArray<FSourceControlStateRef> GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const override;
	virtual FDelegateHandle RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged) override;
	virtual void UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle) override;
	virtual ECommandResult::Type Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency = EConcurrency::Synchronous, const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete()) override;
	virtual bool CanExecuteOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual bool CanCancelOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual void CancelOperation(const FSourceControlOperationRef& InOperation) override;
	virtual bool UsesLocalReadOnlyState() const override;
	virtual bool UsesChangelists() const override;
	virtual bool UsesUncontrolledChangelists() const override;
	virtual bool UsesCheckout() const override;
	virtual bool UsesFileRevisions() const override;
	virtual bool UsesSnapshots() const override;
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	virtual bool UsesSoftRevertOnDelete() const override;
#endif
	virtual bool AllowsDiffAgainstDepot() const override;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	virtual TOptional<bool> IsAtLatestRevision() const override;
	virtual TOptional<int> GetNumLocalChanges() const override;
#else
	virtual TOptional<bool> HasChangesToSync() const override;
	virtual TOptional<bool> HasChangesToCheckIn() const override;
#endif
	virtual void Tick() override;
	virtual TArray< TSharedRef<class ISourceControlLabel> > GetLabels(const FString& InMatchingSpec) const override;
	virtual TArray<FSourceControlChangelistRef> GetChangelists(EStateCacheUsage::Type InStateCacheUsage) override;
#if SOURCE_CONTROL_WITH_SLATE
	virtual TSharedRef<class SWidget> MakeSettingsWidget() const override;
	FString GetBranchId();
#endif

	using ISourceControlProvider::Execute;

	/**
	 * Check configuration, else standard paths, and run a Diversion "version" command to check the availability of the binary.
	 */
	bool CheckDiversionAvailability();

	/** Diversion version for feature checking */
	const FDiversionVersion& GetDiversionVersion(EConcurrency::Type Concurrency, bool InForceUpdate);
	const FDiversionVersion& GetDiversionVersion() const;

	bool IsAgentAlive(EConcurrency::Type Concurrency, bool InForceUpdate)
	{
		return GetDiversionVersion(Concurrency, InForceUpdate).IsValid();
	}
	bool IsAgentAlive() const { return GetDiversionVersion().IsValid(); }


	WorkspaceInfo GetWsInfo(EConcurrency::Type Concurrency, bool InForceUpdate);
	WorkspaceInfo GetWsInfo() const;

	void SetDvVersion(const FDiversionVersion& InVersion);

	void SetWorkspaceInfo(const WorkspaceInfo& InWsInfo);

	DiversionUtils::EDiversionWsSyncStatus GetSyncStatus() const;
	void SetSyncStatus(const DiversionUtils::EDiversionWsSyncStatus& InSyncStatus);

	void SetReloadStatus()
	{
		ReloadStatusRequired = true;
	}

	const FString& GetPluginVersion() const
	{
		return PluginVersion;
	}

	bool IsRepoFound() const;

	/** Get the path to the root of the Diversion repository: can be the ProjectDir itself, or any parent directory */
	FString GetPathToWorkspaceRoot() const
	{
		return WsInfo.Get().GetPath();
	}

	/** Diversion config user.name */
	FString GetUserEmail() const
	{
		return WsInfo.Get().AccountID;
	}

	FString GetWorkspaceName() const
	{
		return WsInfo.Get().WorkspaceID;
	}

	FString GetRepositoryName() const
	{
		return WsInfo.Get().RepoName;
	}

	/**
	 * Register a worker with the provider.
	 * This is used internally so the provider can maintain a map of all available operations.
	 */
	void RegisterWorker(const FName& InName, const FGetDiversionWorker& InDelegate);

	/** Remove a named file from the state cache */
	bool RemoveFileFromCache(const FString& Filename);

	FString GetRepositoryId() const
	{
		return WsInfo.Get().RepoID;
	}

	FString GetWorkspacId() const
	{
		return WsInfo.Get().WorkspaceID;
	}

	void SetWorkspaceMergesList(const TArray<Diversion::CoreAPI::Model::Merge>& InList)
	{
		WorkspaceMergeList = InList;
	}

	const TArray<Diversion::CoreAPI::Model::Merge>& GetWorkspaceMergesList() const
	{
		return WorkspaceMergeList;
	}

	void SetBranchMergesList(const TArray<Diversion::CoreAPI::Model::Merge>& InList)
	{
		BranchMergeList = InList;
	}

	const TArray<Diversion::CoreAPI::Model::Merge>& GetBranchMergesList() const
	{
		return BranchMergeList;
	}

	void ShowNotification(const FDiversionNotification& Notification)
	{
		NotificationManager.ShowNotification(Notification);
	}

	/** Status panel status bar functionality */
	void RegisterStatusBarWidget();
	void RegisterStatusBarMenus();
	void UnregisterStatusBarWidget();

private:

#pragma region PotentialClashesUIExtension

	/** Delegate handle for the potential clash UI icon that was added to the AssetViewExtraStateGenerator */
	FDelegateHandle PotentialClashUIIconDelegateHandle;
	TSharedRef<SWidget> OnGenerateAssetViewPotentialClashIcon(const FAssetData& AssetData);
	TSharedRef<SWidget> OnGenerateAssetViewPotentialClashTooltip(const FAssetData& AssetData);

	FDelegateHandle HardLockUIIconDelegateHandle;
	TSharedRef<SWidget> OnGenerateAssetViewHardLockIcon(const FAssetData& AssetData);
	TSharedRef<SWidget> OnGenerateAssetViewHardLockTooltip(const FAssetData& AssetData);

#pragma endregion

	/** Is dv binary found and working. */
	bool bDiversionAvailable = false;
	bool bDiversionStopped = false;

	/** Owns the save-time lock decision flow; reaches back into this provider for the state
	 * cache, command execution and notifications (hence the friendship below) */
	FDiversionSaveDecisionGate SaveDecisionGate;
	friend class FDiversionSaveDecisionGate;

	/** Helper function for Execute() */
	TSharedPtr<class IDiversionWorker, ESPMode::ThreadSafe> CreateWorker(const FName& InOperationName) const;

	/** Helper function for running command synchronously. */
	ECommandResult::Type ExecuteSynchronousCommand(class FDiversionCommand& InCommand, const FText& Task);
	/** Issue a command asynchronously if possible. */
	ECommandResult::Type IssueCommand(class FDiversionCommand& InCommand);

	/** Output any messages this command holds */
	void OutputCommandMessages(const class FDiversionCommand& InCommand);

	/** Version of the Diversion plugin */
	FString PluginVersion;

	/** Diversion user email */
	FString UserEmail;

	bool ReloadStatusRequired = false;

	/** The currently registered version control operations */
	TMap<FName, FGetDiversionWorker> WorkersMap;

	/** Queue for commands given by the main thread */
	TArray<FDiversionCommand*> CommandQueue;

	/** For notifying when the version control states in the cache have changed */
	FSourceControlStateChanged OnSourceControlStateChanged;

	TCached<FDiversionVersion> DvVersion;
	TCached<WorkspaceInfo> WsInfo;
	TCached<DiversionUtils::EDiversionWsSyncStatus> SyncStatus;

	const TArray<FString> CanExecuteBeforeEnabled = {
		"Connect",
		"AgentHealthCheck",
		"GetWsInfo",
		"InitRepo",
		"SendAnalytics",
		"CheckIfWorkspaceExistsInPath",
		"CheckForRepoWithSameName"
	};

	/** Saves undismissed Diversion generated notifications */
	FDiversionNotificationManager NotificationManager;
public:
	TMap<FString, FDiversionResolveInfo> ConflictedFiles;
#pragma region MergeConflictsMembers
	TArray<Diversion::CoreAPI::Model::Merge> WorkspaceMergeList;
	TArray<Diversion::CoreAPI::Model::Merge> BranchMergeList;
	TMap<FString, FDiversionResolveInfo> FilesToResolve;
	TArray<FString> FilesToRevert;
	void OnPackageSave(const FString& PackageFileName, UObject* Outer);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	void OnPackageSaveWithContext(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext ObjectSaveContext);
#endif
	void OnPrePackageSave(UPackage*, FObjectPreSaveContext);

	/** Save-time lock decision flow: the chained engine save gate, the per-gesture record of
	 * vetoed saves and the save-decision dialog. Bound in Init(), unbound in Close(). */
	FDiversionSaveDecisionGate& GetSaveDecisionGate() { return SaveDecisionGate; }

	/** Called when a package is deleted - marks files as pending deletion to skip RunReset in Revert */
	void OnPackageDeletedHandler(UPackage* DeletedPackage);
#pragma endregion

#pragma region UnrealOpenedFiles
	/**
	 * Functions and members for handling files opened(and FS-locked) by UE
	 * that diversion tries to sync incoming changes for.
	 */

public:
	bool TryUnloadingOpenedPackage(const FString& InOpenedPackagePath);

	/** Shows one notification per read-only path blocking the sync, and dismisses the
	 * notifications of paths that are no longer reported. Paths map to the message to show. */
	void UpdateReadOnlyPathNotifications(const TMap<FString, FString>& InReadOnlyPaths);
	bool HasReadOnlyPathNotification(const FString& Path) const { return ReadOnlyPathNotifications.Contains(Path); }

	// Functions to interact with the restoring files after sync delegates
	int AddRestoreAfterSyncDelegateHandle();
	FDelegateHandle& GetRestoreAfterSyncDelegateHandle(int DelegateHandleIndex);
	void RemoveRestoreAfterSyncDelegateHandle(int DelegateHandleIndex);

private:

	bool ReleaseLockedPackage(UPackage* Package, const FString& PackagePath);
	// Functions to handle unsaved packages
	void HandleUnsavedPackage(UPackage* Package, const FString& PackagePath, const FString& PackageName);
	TArray<FDelegateHandle> DirectoryWatcherHandles;

	// Function to handle opened in editor packages
	void HandleOpenedInEditorPackage(UPackage* Package, const FString& PackagePath, const FString& PackageName);

	// Adds a package to the list of packages that are locked by UE and are being synched by Diversion
	void AddAndNotifySyncInaccessiblePackage(const FString& Path, const FDiversionNotification& InNotificationInfo);
	void RemoveSyncInaccessiblePackage(const FString& Path);
	bool GetSyncInaccessiblePackages(const FString& PackagePath);

	struct FSyncInaccessiblePackages
	{
		FDiversionNotificationManager::FNotificationId NotificationId;
		FDateTime LockTime;
	};
	TMap<FString, FSyncInaccessiblePackages> SyncInaccessiblePackages;
	// Time to invalidate entries in SyncInaccessiblePackages
	const FTimespan SyncInaccessiblePackageRefreshTime = FTimespan::FromSeconds(30);
	bool IsPackageExpired(const FSyncInaccessiblePackages& SyncInaccessiblePackage) const;

	// Read-only paths the user was notified about, and their notification
	TMap<FString, FDiversionNotificationManager::FNotificationId> ReadOnlyPathNotifications;
#pragma endregion

#pragma region StatesCache
public:

	/**
	 * Helper function for various commands to update cached states.
	 * @param InNewStates - new states to update the cache with
	 * @param IsFullStatusUpdate - if true, this will clear the modified states cache
	 * @param InConflictedFiles - Conflicted paths to update the states of
	 * @returns true if any states were updated
	 */
	bool UpdateCachedStates(const TMap<FString, FDiversionState>& InNewStates, bool IsFullStatusUpdate);

	/**
	 * Updates the states with the conflicted data of current open merges.
	 * @param InConflictedFiles
	 * @param IsFullStatusUpdate
	 * @return True if any state was updated
	 */
	bool UpdateConflictedStates(const TMap<FString, FDiversionResolveInfo>& ConflictedFilesData);

	/**
	 * Helper function to remove a conflict data from a state in the cache.
	 * @param Path - Path of the file to remove the conflict data from
	 * @returns true if any states were updated
	 */
	bool RemoveConflictedState(const FString& Path);

	/**
	 * Retrieves the resolve information for a given file path.
	 * @param Path - The path of the file to get the resolve information for.
	 * @return The resolve information for the specified file.
	 */
	TUniquePtr<FDiversionResolveInfo> GetFileResolveInfo(const FString& Path) const;
	/**
	 * Helper function to get the paths values of conflicted files.
	 * @param OutArray - Array to fill with the conflicted files paths
	 */
	void GetConflictedFilesPaths(TArray<FString>& OutArray) const;

	void SetFilesToResolve(const TMap<FString, FDiversionResolveInfo>& InFilesToResolve);
	/**
	 * Helper function for various commands to update states with their potential clashes status.
	 * @param InPotentialClashes - Potential clashes to update the states with
	 *                             Will remove the potential clash info from local cache if its empty (Num==0).
	 * @returns true if any states were updated
	 */
	bool UpdatePotentialClashedStates(const TMap<FString, TArray<EDiversionPotentialClashInfo>>& InPotentialClashes,
		bool IsFullStatusUpdate);

	void GetCurrentPotentialClashes(TArray<FString>& OutPotentialClashedPaths) const;

	/** Snapshot of the cached locks. The clashes poll fills this with the complete repo lock
	 *  set, so it is the whole repo's locks and not just this workspace's. */
	void GetCurrentLockedStates(TArray<TSharedRef<FDiversionState>>& OutLockedStates) const
	{
		LockedStates.GenerateValueArray(OutLockedStates);
	}

	uint32 GetLockedStatesRevision() const { return LockedStatesRevision; }

	/** Whether a full repo lock set has been fetched at least once. Until it has, an empty
	 *  lock cache means "not looked yet", not "nothing is locked". */
	bool HasFetchedFullLockSet() const { return bFetchedFullLockSet; }

	/**
	 * Update the hard-lock state of files in the state cache.
	 * @param InLocks Map of file paths to their lock info. An unset entry (empty LockedBy)
	 *                clears the cached lock state for that file.
	 * @param IsFullStatusUpdate If true, files absent from InLocks have their cached lock state cleared.
	 * @returns true if any states were updated
	 */
	bool UpdateLockedStates(const TMap<FString, FDiversionLockInfo>& InLocks, bool IsFullStatusUpdate);

	/**
	 * Re-verify the hard-lock state of the given paths against the backend, synchronously,
	 * and apply the result to the state cache.
	 *
	 * Every point that GATES on a lock refreshes through here first: the cache alone is never
	 * the authority, because a stale entry would let a locked file slip past the check it
	 * exists to make. Callers must gate on ShouldCheckHardLocks() themselves - this issues the
	 * query unconditionally.
	 *
	 * @returns false if the backend could not be reached. What that means is the caller's
	 *          call, and the two callers deliberately differ: the save gate lets the save
	 *          through (a blocked save would strand the user, and commit stays server-enforced),
	 *          while the asset-open dialog falls back to the cached state (a cached
	 *          "locked by other" should still raise the dialog).
	 *
	 * Not the only path that fills the lock cache - the clashes poll piggybacks a full-repo
	 * refresh, and a rejected lock mutation queues an async reconcile of the failed paths.
	 * This is the synchronous, decision-time one.
	 */
	bool RefreshFileLocks(const TArray<FString>& InPaths);

	/**
	 * Whether lock API calls should run for this repo (effective tier is Studio+).
	 * Resolved once per session at Init; FAIL-CLOSED while unresolved - the server-side
	 * 423 commit rejection is the safety net. All lock-check consumers must gate on this.
	 */
	bool ShouldCheckHardLocks() const
	{
		return bHardLocksEligible.IsSet() && bHardLocksEligible.GetValue();
	}

	/** Game-thread setter, applied by the GetHardLocksEligibility worker */
	void UpdateHardLocksEligibility(const TOptional<bool>& bInEligible);

	/**
	 * Queues a hard-locks eligibility (+ lockable patterns) fetch unless one is already in
	 * flight. Called at Init and, when IsHardLocksEligibilityRefreshDue(), from the
	 * potential-clashes poll: an unresolved value is retried every poll, a resolved one is
	 * re-fetched every SECONDS_TO_REFRESH_HARD_LOCKS_ELIGIBILITY, so a startup hiccup, a tier
	 * change or a pattern edit all heal without a restart. A failed fetch never touches the
	 * current value - only a successful answer can change it (see the worker's UpdateStates).
	 */
	void RequestHardLocksEligibility();
	bool IsHardLocksEligibilityRefreshDue() const;
	/** Worker callback, success or not - releases the in-flight guard */
	void OnHardLocksEligibilityRequestFinished() { bHardLocksEligibilityInFlight = false; }

	/** Game-thread setter, applied by the GetHardLocksEligibility worker alongside eligibility */
	void SetLockablePatterns(const TOptional<TArray<FString>>& InPatterns);

	/**
	 * Whether the repo locks this file on sync no matter what the user answers at save time -
	 * the file matches one of the repo's lockable patterns. False while the list is still
	 * unresolved, so the save gate falls back to asking as before.
	 */
	bool IsAutoLockedByRepoRule(const FString& InFullPath) const;

	/**
	 * Whether the repo is known to have no lockable patterns - a lock taken at save time then
	 * stays until the user releases it. False while the list is still unresolved.
	 */
	bool HasNoAutoLockRules() const
	{
		return ShouldCheckHardLocks() && LockablePatterns.IsSet() && LockablePatterns.GetValue().IsEmpty();
	}

	/** Helper function used to update state cache */
	TSharedRef<FDiversionState, ESPMode::ThreadSafe> GetStateInternal(const FString& Filename);

	/** Adds a state to the syncing states cache */
	void AddSyncingState(const FString& Path, const TSharedRef<class FDiversionState>& InState);

	/** Check if a file is pending deletion (thread-safe) */
	bool IsFilePendingDelete(const FString& FilePath) const;
	/** Clear a file from the pending deletion set (thread-safe) */
	void ClearPendingDelete(const FString& FilePath);
	/** Get a snapshot of the pending deletion set (thread-safe) */
	TSet<FString> GetFilesPendingDeleteSnapshot() const;

// Background status triggering functions
	// Timers are not created in commandlet/unattended mode, so these may be null.
	// Use when want to mark BG status as called and wait for next interval
	void BackgroundStatusSkipToNextInterval()
	{
		if (BackgroundStatus) { BackgroundStatus->SkipToNextInterval(); }
	}
	// Force trigger calling BG status on the next tick
	void BackgroundStatusTriggerInstantCall()
	{
		if (BackgroundStatus) { BackgroundStatus->TriggerInstantCallAndReset(); }
	}
//
	void BackgroundConflictedFilesSkipToNextInterval()
	{
		if (BackgroundConflictedFiles) { BackgroundConflictedFiles->SkipToNextInterval(); }
	}

private:
	/** Helper function for UpdateCachedStates. Updates the modified states cache.
	 * @param InNewModifiedStates - new states to update the cache with
	 */
	void UpdateModifiedStates(const TMap<FString, TSharedRef<FDiversionState>>& InNewModifiedStates);

	/** Adds a state to the modified states cache */
	void AddModifiedState(const TSharedRef<class FDiversionState>& InState);

	/** Called when the editor gains or loses focus */
	void OnApplicationFocusChanged(const bool bIsFocused);

	/** State cache */
	TMap<FString, TSharedRef<FDiversionState>> StateCache;

	/** Files pending deletion - used to skip RunReset in Revert when UE calls Revert-before-Delete.
	 *  Populated by OnPackageDeleted, checked by Revert worker, cleared by Revert/Delete UpdateStates.
	 *  Stale entries are cleared automatically when a new batch arrives after PendingDeleteStaleSeconds. */
	mutable FCriticalSection PendingDeleteLock;
	TSet<FString> FilesPendingDelete;
	double PendingDeleteLastAddTime = 0.0;
	static constexpr double PendingDeleteStaleSeconds = 30.0;
	/** Tracking modified states - enables resetting the changes to apply external to UE changes too */
	TMap<FString, TSharedRef<FDiversionState>> ModifiedStates;
	/** Tracking potential clashes - this used to keep track of resolved potential clashes*/
	TMap<FString, TSharedRef<FDiversionState>> PotentiallyClashedStates;
	/** Tracking hard-locked files - used to clear lock state once locks are released */
	TMap<FString, TSharedRef<FDiversionState>> LockedStates;
	uint32 LockedStatesRevision = 0;
	bool bFetchedFullLockSet = false;

	/** Whether the repo's effective tier supports hard locks; unset until resolved (see ShouldCheckHardLocks) */
	TOptional<bool> bHardLocksEligible;
	/** The repo's lockable patterns - files the server auto-locks on sync; unset until resolved with eligibility */
	TOptional<TArray<FString>> LockablePatterns;
	/** FPlatformTime::Seconds() of the last successful eligibility resolution; drives the periodic refresh */
	double HardLocksEligibilityResolvedAt = 0.0;
	/** Whether an eligibility fetch is queued or running - the poll never stacks a second one */
	bool bHardLocksEligibilityInFlight = false;
	/** Tracking syncing states - enables resetting them once finished syncing */
	TMap<FString, TSharedRef<FDiversionState>> SynchingStates;
	/** Tracking conflicted states - enables resetting them once finished resolving/Updating conflicts data */
	TMap<FString, TSharedRef<FDiversionState>> ConflictedStates;

// Background status triggering variables
	TUniquePtr<FTimedDelegateWrapper> BackgroundStatus = nullptr;
	TUniquePtr<FTimedDelegateWrapper> BackgroundPotentialClashes = nullptr;
	TUniquePtr<FTimedDelegateWrapper> BackgroundConflictedFiles = nullptr;
	FDelegateHandle FocusChangedHandle;
//

#pragma endregion

#pragma region InitializationStatusCache
public:
	bool IsRepoWithSameNameExists(const FString& RepoName, EConcurrency::Type Concurrency, bool InForceUpdate);
	bool IsWorkspaceExistsInPath(const FString& RepoName, const FString& Path, EConcurrency::Type Concurrency, bool InForceUpdate);

	// To be used by the API calls
	void SetRepoWithSameNameExists(bool InValue)
	{
		bRepoWithSameNameExists.Set(InValue);
	}
	void SetWorkspaceExistsInPath(bool InValue)
	{
		bWorkspaceExistsInPath.Set(InValue);
	}

private:
	TCached<bool> bRepoWithSameNameExists;
	TCached<bool> bWorkspaceExistsInPath;
#pragma endregion

#pragma region changelists
public:
	void AddCurrentChangesToChangelistState();

private:
	TSharedRef<FDiversionChangelistState> ChangelistState;
#pragma endregion

// Misc
private:
	void RegisterPreSubmitValidationDelegates();
	void UnregisterAndRestorePreSubmitValidationDelegates();

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	// UE 5.8 replaced the pre-submit validation delegates with "Ex" variants that report results
	// through FSourceControlValidationDelegateResult. The non-Ex Register/Get methods became
	// deprecated no-ops, so the submit dialog only invokes the "Ex" wrappers below.
	FSourceControlPreSubmitChangelistValidationExDelegate OriginalPreChangelistValidationDelegateEx;
	FSourceControlPreSubmitFilesValidationExDelegate OriginalPreFilesValidationDelegateEx;

	void PreChangelistValidationDelegateWrapperEx(FSourceControlChangelistPtr Changelist,
		FSourceControlValidationDelegateResult& OutResult);
	void PreFilesValidationDelegateWrapperEx(TArray<FSourceControlStateRef>& InStates,
		FSourceControlValidationDelegateResult& OutResult);
#else
	FSourceControlPreSubmitDataValidationDelegate OriginalPreChangelistValidationDelegate;

	void PreChangelistValidationDelegateWrapper(FSourceControlChangelistPtr Changelist,
		EDataValidationResult& Result, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	FSourceControlPreSubmitFilesValidationDelegate OriginalPreFilesValidationDelegate;
	void PreFilesValidationDelegateWrapper(TArray<FSourceControlStateRef>& InStates,
		EDataValidationResult& Result, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings);
#endif
#endif

	// Helper function to perform validation logic common to both changelist and files validation
	// Returns array of states that need to be cleaned up after validation delegate execution
	TArray<TSharedRef<FDiversionState>> PerformPreSubmitValidation(const TArray<FString>& FilesInChangeList,
		EDataValidationResult& Result, TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings);

	// Set by the pre-submit validation wrappers; consumed by Execute() to block CheckIn when
	// validation returned Invalid (Epic's dialog ignores it because we force UsesChangelists()=false).
	// Only meaningful while bOverrideUsesChangelists is true. Game-thread only.
	bool bLastPreSubmitValidationFailed = false;

	// UI Commands for custom submit functionality
	FName MenuSectionName;

	void InitializeUICommands();
	void ShutdownUICommands();
	void ExecuteCommitChanges();
	bool CanExecuteCommitChanges() const;

	// Status panel widget
	TSharedPtr<class SDiversionStatusPanel> StatusPanelWidget;

	// Override for UsesChangelists() - used by custom commit flow to enable file checkboxes
	// When true, UsesChangelists() returns false to allow unchecking files in the commit dialog
	// This is due to UE not allowing using Git style commits while changelists enabled
	TAtomic<bool> bOverrideUsesChangelists{false};

public:
	void SetUsesChangelistsOverride(bool bOverride)
	{
		bOverrideUsesChangelists = bOverride;
	}

	/** RAII guard for temporarily overriding UsesChangelists() to return false */
	struct FUsesChangelistsOverrideGuard
	{
		FDiversionProvider& Provider;

		FUsesChangelistsOverrideGuard(FDiversionProvider& InProvider)
			: Provider(InProvider)
		{
			// Clear stale state from prior non-custom flows.
			Provider.bLastPreSubmitValidationFailed = false;
			Provider.SetUsesChangelistsOverride(true);
		}

		~FUsesChangelistsOverrideGuard()
		{
			Provider.SetUsesChangelistsOverride(false);
		}

		// Non-copyable
		FUsesChangelistsOverrideGuard(const FUsesChangelistsOverrideGuard&) = delete;
		FUsesChangelistsOverrideGuard& operator=(const FUsesChangelistsOverrideGuard&) = delete;
	};

};
