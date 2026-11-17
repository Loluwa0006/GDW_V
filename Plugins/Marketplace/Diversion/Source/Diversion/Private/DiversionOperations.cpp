// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionOperations.h"
#include "IDiversionWorker.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionProvider.h"
#include "DiversionUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"
#include "CustomWidgets/DiversionLockContextMenu.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif


#define LOCTEXT_NAMESPACE "Diversion"

// Wrapper for the commands validity checks
bool ExecuteValidityCheck(FDiversionCommand& InCommand, const FName& WorkerName)
{
	if (!DiversionUtils::DiversionValidityCheck(InCommand.Operation->GetName() == WorkerName,
	InCommand.Operation->GetName().ToString() + " != " + WorkerName.ToString(), InCommand.WsInfo.AccountID))
	{
		InCommand.MarkOperationCompleted(false);
		return false;
	}
	return true;
}

FText FAgentHealthCheck::ProgressString = FText::GetEmpty();

FText FGetWsInfo::ProgressString = FText::GetEmpty();

#pragma region Diversion API Workers

//

FName FDiversionCheckForRepoWithSameNameWorker::GetName() const
{
	return "CheckForRepoWithSameName";
}

bool FDiversionCheckForRepoWithSameNameWorker::Execute(FDiversionCommand& InCommand)
{
	if (!ExecuteValidityCheck(InCommand, GetName())) { return false; }

	bool Success = true;
	bool OwnedOnly = true;

	auto Operation = StaticCastSharedRef<FCheckForRepoWithSameName>(InCommand.Operation);
	auto DesiredRepoName = Operation->GetRepoName();
	TArray<FString> RemoteRepositories;
	Success &= DiversionUtils::GetRemoteRepos(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, OwnedOnly, RemoteRepositories);

	bRepoWithSameNameExists = RemoteRepositories.Contains(DesiredRepoName);

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionCheckForRepoWithSameNameWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.SetRepoWithSameNameExists(bRepoWithSameNameExists);
	return true;
}

//

FName FDiversionCheckIfWorkspaceExistsInPathWorker::GetName() const
{
	return "CheckIfWorkspaceExistsInPath";
}

bool FDiversionCheckIfWorkspaceExistsInPathWorker::Execute(FDiversionCommand& InCommand)
{
	if (!ExecuteValidityCheck(InCommand, GetName())) { return false; }

	bool Success = true;
	bool OwnedOnly = true;
	TArray<WorkspaceInfo> LocalWorkspaces;
	auto Operation = StaticCastSharedRef<FCheckIfWorkspaceExistsInPath>(InCommand.Operation);

	auto UnrealProjectDirectoryPath = Operation->GetPath();
	FPaths::NormalizeDirectoryName(UnrealProjectDirectoryPath);

	auto DesiredRepoName = Operation->GetRepoName();

	TArray<FString> RemoteRepositories;
	Success &= DiversionUtils::GetRemoteRepos(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, OwnedOnly, RemoteRepositories);
	bRepoWithSameNameExists = RemoteRepositories.Contains(DesiredRepoName);

	Success &= DiversionUtils::GetAllLocalWorkspaces(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, LocalWorkspaces);


	LocalWorkspaces = LocalWorkspaces.FilterByPredicate([&RemoteRepositories](const WorkspaceInfo& WsInfo) {
		return RemoteRepositories.Contains(WsInfo.RepoName);
	});

	// We're supporting UE projects not in the main repo folder too, so we need to find a workspace which contains
	// The UE project root path. We need to ensure proper path matching to handle edge cases like:
	// - Workspaces with similar prefixes (e.g., /workspace and /workspace2)
	// - Nested workspaces (e.g., project at /ws1/proj inside workspace /ws1)
	// - Case sensitivity on different platforms

	// Normalize paths and add trailing separator to ensure proper prefix matching
	FString ProjectPathWithSeparator = DiversionUtils::NormalizePathWithTrailingSeparator(UnrealProjectDirectoryPath);

	bWorkspaceExistsInPath = LocalWorkspaces.ContainsByPredicate([&ProjectPathWithSeparator](const WorkspaceInfo& WsInfo) {
		// Normalize workspace path with trailing separator
		FString WsPathWithSeparator = DiversionUtils::NormalizePathWithTrailingSeparator(WsInfo.GetPath());

		// Check if the project path starts with the workspace path
		// The separator ensures we don't match partial directory names
		// e.g., "/workspace2/project" won't match "/workspace/"
		return ProjectPathWithSeparator.StartsWith(WsPathWithSeparator);
	});

	bFetchSuccess = Success;
	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionCheckIfWorkspaceExistsInPathWorker::UpdateStates() const
{
	// Only update states if data fetch was successful, otherwise keep the last known state
	if (!bFetchSuccess)
	{
		return false;
	}
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.SetWorkspaceExistsInPath(bWorkspaceExistsInPath);
	// We already fetch this info so lets set it as well - this saves cached calls to the backend
	Provider.SetRepoWithSameNameExists(bRepoWithSameNameExists);
	return true;
}

//

FName FDiversionUpdateWorkspaceWorker::GetName() const
{
	return "UpdateWorkspace";
}

bool FDiversionUpdateWorkspaceWorker::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;
	UE_LOG(LogSourceControl, Display, TEXT("Diversion: Updating workspace"));
	Success &= DiversionUtils::UpdateWorkspace(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionUpdateWorkspaceWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	// Call get conflicted synchronously files to update the conflicted files
	auto operation = ISourceControlOperation::Create<FGetConflictedFiles>();
	Provider.Execute(operation, nullptr, { }, EConcurrency::Synchronous);
	// Reset the conflicted files timer
	Provider.BackgroundConflictedFilesSkipToNextInterval();
	return true;
}

FName FDiversionInitRepoWorker::GetName() const
{
	return "InitRepo";
}

bool FDiversionInitRepoWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;
	TSharedRef<FInitRepo, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FInitRepo>(InCommand.Operation);

	Success &= DiversionUtils::RunRepoInit(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages,
		Operation->GetRepoPath(), Operation->GetRepoName());

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

//

FName FDiversionAnalyticsEventWorker::GetName() const
{
	return "SendAnalytics";
}

bool FDiversionAnalyticsEventWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	const TSharedRef<FSendAnalytics, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FSendAnalytics>(InCommand.Operation);

	bool Success = true;
	// TODO: Error message and info like in the connect
	Success &= DiversionUtils::SendAnalyticsEvent(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, Operation->GetEventProperties());

	InCommand.MarkOperationCompleted(Success);
	return Success;
}


//

FName FDiversionGetPotentialClashes::GetName() const
{
	return "GetPotentialClashes";
}

bool FDiversionGetPotentialClashes::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = DiversionUtils::GetPotentialFileClashes(InCommand, InCommand.InfoMessages,
		InCommand.ErrorMessages, PotentialClashes, bRecursiveRequest);
	if(Success)
	{
		QueriedPaths = InCommand.Files;
	}

	// Piggyback the hard-locks refresh on the repo-wide poll pass (no separate timer).
	// A lock refresh failure must not fail the clashes poll, so its messages go to a
	// local sink instead of InCommand's - errors are logged, not surfaced.
	TSharedRef<FGetPotentialClashes> Operation = StaticCastSharedRef<FGetPotentialClashes>(InCommand.Operation);
	if (Success && Operation->ShouldAlsoFetchLocks())
	{
		TArray<FString> LockInfoMessages;
		TArray<FString> LockErrorMessages;
		bool bFullRepoLocksQuery = false;
		bLocksFetched = DiversionUtils::GetFileLocks(InCommand, LockInfoMessages,
			LockErrorMessages, Locks, bFullRepoLocksQuery) && bFullRepoLocksQuery;
		for (const FString& LockError : LockErrorMessages)
		{
			UE_LOG(LogSourceControl, Warning, TEXT("Diversion: Hard-locks refresh during clashes poll failed: %s"), *LockError);
		}
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionGetPotentialClashes::UpdateStates() const
{
	const bool bBaseUpdated = IDiversionWorker::UpdateStates();
	auto& Provider = FDiversionModule::Get().GetProvider();
	bool FullRepoUpdateRequested = IsFullRepoStatusQuery(Provider.GetWsInfo().GetPath());
	const bool bClashesUpdated = Provider.UpdatePotentialClashedStates(PotentialClashes,
		FullRepoUpdateRequested);
	// The locks refresh rides along on this poll, so either half moving is a real change
	const bool bLocksUpdated = bLocksFetched &&
		Provider.UpdateLockedStates(Locks, /*IsFullStatusUpdate*/ true);
	return (bBaseUpdated && bClashesUpdated) || bLocksUpdated;
}

bool FDiversionGetPotentialClashes::IsFullRepoStatusQuery(const FString& RepoPath) const
{
	// Search for the repo path in the queried files, if found and the query
	// is recursive - it means we have a repo-wide status info to update
	return QueriedPaths.Contains(RepoPath) && bRecursiveRequest;
}


//

FName FDiversionGetFileLocksWorker::GetName() const
{
	return "GetFileLocks";
}

bool FDiversionGetFileLocksWorker::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool bFullRepoQuery = false;
	bool Success = DiversionUtils::GetFileLocks(InCommand, InCommand.InfoMessages,
		InCommand.ErrorMessages, Locks, bFullRepoQuery);
	// Only mark the update as repo-wide on success: a failed/partial full-repo refresh
	// must not be applied as a full update, or UpdateLockedStates would wipe every
	// cached lock missing from the incomplete result (partial data is still applied
	// additively as a non-full update).
	if (Success)
	{
		bFullRepoRequest = bFullRepoQuery;
	}
	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionGetFileLocksWorker::UpdateStates() const
{
	const bool bBaseUpdated = IDiversionWorker::UpdateStates();
	auto& Provider = FDiversionModule::Get().GetProvider();
	const bool bLocksUpdated = Provider.UpdateLockedStates(Locks, bFullRepoRequest);
	// This worker exists only to refresh locks, so it reports a state change only when the
	// lock cache actually moved - an unchanged refresh should not trigger a UI broadcast.
	return bBaseUpdated && bLocksUpdated;
}


//

FName FDiversionUpdateFileLockWorker::GetName() const
{
	return "UpdateFileLock";
}

bool FDiversionUpdateFileLockWorker::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	TSharedRef<FUpdateFileLock> Operation = StaticCastSharedRef<FUpdateFileLock>(InCommand.Operation);
	Action = Operation->GetLockAction();
	bSuppressResultNotification = Operation->ShouldSuppressResultNotification();
	RequestedFileCount = InCommand.Files.Num();
	bool Success = DiversionUtils::UpdateFileLocks(InCommand, Action, InCommand.InfoMessages,
		InCommand.ErrorMessages, UpdatedLocks, Failures);
	bTransportError = !Success;
	// Copy per-file outcomes back onto the operation so a synchronous caller can act on them
	Operation->SetResults(UpdatedLocks, Failures);
	InCommand.MarkOperationCompleted(Success && Failures.Num() == 0);
	return Success;
}

bool FDiversionUpdateFileLockWorker::UpdateStates() const
{
	const bool bBaseUpdated = IDiversionWorker::UpdateStates();
	auto& Provider = FDiversionModule::Get().GetProvider();
	// Apply the mutation results directly (unset entries clear released locks); never a
	// full update - only the touched files may change
	const bool bLocksUpdated = Provider.UpdateLockedStates(UpdatedLocks, /*IsFullStatusUpdate*/ false);
	// A user-initiated lock/unlock always reports a change: even when no cache entry moved
	// (an already-held lock, a rejected mutation), the notifications below and the icon
	// refresh they imply need the broadcast.
	bool bUpdated = bBaseUpdated || bLocksUpdated;

	if (!bSuppressResultNotification)
	{
		if (bTransportError)
		{
			// Both result maps may be empty here - a dead connection must not look like a no-op
			const int32 NotProcessedCount = FMath::Max(0,
				RequestedFileCount - UpdatedLocks.Num() - Failures.Num());
			Provider.ShowNotification(FDiversionNotification(
				BuildLockTransportFailureNotification(Action, UpdatedLocks.Num(), NotProcessedCount),
				TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail));
		}
		else
		{
			bool bIsFailure = false;
			const FText Message = BuildLockResultNotification(Action, UpdatedLocks.Num(), Failures, bIsFailure);
			if (!Message.IsEmpty())
			{
				Provider.ShowNotification(FDiversionNotification(Message, TArray<FNotificationButtonInfo>(),
					bIsFailure ? SNotificationItem::CS_Fail : SNotificationItem::CS_Success));
			}
		}
	}

	// A rejected mutation means our cached lock state was stale - reconcile the failed
	// paths (refreshes holder info and the Content Browser icons)
	if (Failures.Num() > 0)
	{
		TArray<FString> FailedPaths;
		for (const auto& Failure : Failures)
		{
			FailedPaths.Add(Failure.Path);
		}
		Provider.Execute(ISourceControlOperation::Create<FGetFileLocks>(), nullptr,
			FailedPaths, EConcurrency::Asynchronous);
	}
	return bUpdated;
}


//

FName FDiversionGetHardLocksEligibilityWorker::GetName() const
{
	return "GetHardLocksEligibility";
}

bool FDiversionGetHardLocksEligibilityWorker::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bFetchSucceeded = DiversionUtils::GetHardLocksEligibility(InCommand, InCommand.InfoMessages,
		InCommand.ErrorMessages, bEligible, LockablePatterns);
	InCommand.MarkOperationCompleted(bFetchSucceeded);
	return bFetchSucceeded;
}

bool FDiversionGetHardLocksEligibilityWorker::UpdateStates() const
{
	bool bUpdated = IDiversionWorker::UpdateStates();
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.OnHardLocksEligibilityRequestFinished();
	// A failed fetch leaves whatever was resolved before in place - only an answer can change
	// the value, and the poll will ask again
	if (bFetchSucceeded)
	{
		Provider.UpdateHardLocksEligibility(bEligible);
		Provider.SetLockablePatterns(LockablePatterns);
	}
	return bUpdated;
}


//

FName FDiversionAgentHealthCheckWorker::GetName() const
{
	return "AgentHealthCheck";
}

bool FDiversionAgentHealthCheckWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;

	Success &= DiversionUtils::RunAgentHealthCheck(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
	// Use the worker's freshly-set flag, not InCommand.IsAgentAlive (stale snapshot from cold start).
	if(Success && IsAgentAlive && InCommand.WsInfo.IsValid())
	{
		Success &= DiversionUtils::AgentInSync(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionAgentHealthCheckWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	auto Version = FDiversionVersion(AgentVersion);
	Version.SetAgentAlive(IsAgentAlive);
	Provider.SetDvVersion(Version);
	// SyncStatus is propagated by the base UpdateSyncStatus() (called via the chain
	// below). It writes only when AgentInSync set the base TOptional, which already
	// handles the cold-start race where this command snapshotted empty WsInfo.
	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionWsInfoWorker::GetName() const
{
	return "GetWsInfo";
}

bool FDiversionWsInfoWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;
	FString RootPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	TArray<WorkspaceInfo> LocalWorkspaces;
	Success &= DiversionUtils::GetAllLocalWorkspaces(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, LocalWorkspaces);

	// We're looking for a nested repo, so we need to find the one that contains the root path

	// We're supporting UE projects not in the main repo folder too, so we need to find a workspace which contains
	// The UE project root path - We're using StartsWith here since contains catches middle strings as well
	FString RootPathWithSlash = DiversionUtils::NormalizePathWithTrailingSeparator(RootPath);
	auto WsInfoPtr = LocalWorkspaces.FindByPredicate([&RootPathWithSlash](const WorkspaceInfo& WsInfoArg) {
		FString NormalizedWsPathWithSlash = DiversionUtils::NormalizePathWithTrailingSeparator(WsInfoArg.GetPath());
		return RootPathWithSlash.StartsWith(NormalizedWsPathWithSlash);
	});
	WsInfo = WsInfoPtr ? *WsInfoPtr : WorkspaceInfo();

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionWsInfoWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.SetWorkspaceInfo(WsInfo);

	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionResolveFileWorker::GetName() const
{
	return "ResolveFile";
}

bool FDiversionResolveFileWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;

	TSharedRef<FResolveFile, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FResolveFile>(InCommand.Operation);
	Success &= DiversionUtils::RunResolvePath(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, Operation->GetMergeID(), Operation->GetConflictID());

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionResolveFileWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.RemoveConflictedState(FileEntry.mPath);
	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionFinalizeMergeWorker::GetName() const
{
	return "FinalizeMerge";
}

bool FDiversionFinalizeMergeWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;

	TSharedRef<FFinalizeMerge, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FFinalizeMerge>(InCommand.Operation);
	Success &= DiversionUtils::RunFinalizeMerge(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, Operation->GetMergeID());
	if (Success)
	{
		Success &= DiversionUtils::NotifyAgentSyncRequired(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

#pragma endregion

#pragma region Source Control Workers

FName FDiversionGetConflictedFiles::GetName() const
{
	return "GetConflictedFiles";
}

bool FDiversionGetConflictedFiles::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = DiversionUtils::GetConflictedFiles(InCommand, InCommand.InfoMessages,
													  InCommand.ErrorMessages, ConflictedFilesData,
													  WorkspaceMergesList, BranchMergesList);
	bRequireUpdate = Success;
	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionGetConflictedFiles::UpdateStates() const
{
	if(!bRequireUpdate)
	{
		return false;
	}

	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.SetWorkspaceMergesList(WorkspaceMergesList);
	Provider.SetBranchMergesList(BranchMergesList);
	return Provider.UpdateConflictedStates(ConflictedFilesData);
}

FName FDiversionConnectWorker::GetName() const
{
	return "Connect";
}

bool FDiversionConnectWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;

	if (!DiversionUtils::IsDiversionInstalled())
	{
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("DiversionNotInstalled",
			"Diversion is not installed. Documentation available at https://docs.diversion.dev/ue-quickstart?utm_source=ue-plugin&utm_medium=plugin")
		);
		Success = false;
		InCommand.MarkOperationCompleted(Success);
		return Success;
	}

	if(!InCommand.IsAgentAlive)
	{
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("AgentIsDown",
			"Diversion agent is not running. Start it by clicking the start button or by running 'dv' in a cmd.")
		);
		Success = false;
		InCommand.MarkOperationCompleted(Success);
		return Success;
	}

	if (!InCommand.WsInfo.IsValid())
	{
		StaticCastSharedRef<FConnect>(InCommand.Operation)->SetErrorText(LOCTEXT("NotADiversionRepository",
			"Failed to enable Diversion revision control. You need to initialize the project as a Diversion repository first.")
		);
		Success = false;
		InCommand.MarkOperationCompleted(Success);
		return Success;
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionConnectWorker::UpdateStates() const
{
	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionCheckInWorker::GetName() const
{
	return "CheckIn";
}

static FText MakeCommitSuccessMessage(const FString& InCommitId)
{
	if (!InCommitId.IsEmpty())
	{
		return FText::Format(LOCTEXT("CommitMessage", "Commit {0} was added successfully."), FText::AsCultureInvariant(InCommitId));
	}
	return LOCTEXT("CommitMessageUnknown", "Submitted revision.");
}

bool FDiversionCheckInWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	bool Success = true;
	const TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);

	FString CommitId;
	Success &= DiversionUtils::RunCommit(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, Operation->GetDescription().ToString(), true, &CommitId);
	if(Success)
	{
		Operation->SetSuccessMessage(MakeCommitSuccessMessage(CommitId));
		CommittedFilesPaths = InCommand.Files;
		Success &= DiversionUtils::NotifyAgentSyncRequired(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionCheckInWorker::UpdateStates() const
{
	// Remove deleted files from status cache
	auto& Provider = FDiversionModule::Get().GetProvider();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(CommittedFilesPaths, LocalStates, EStateCacheUsage::Use);
	for (const auto& State : LocalStates)
	{
		if (State->IsDeleted())
		{
			Provider.RemoveFileFromCache(State->GetFilename());
		}
	}

	Provider.SetReloadStatus();
	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FDiversionMarkForAddWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	InCommand.MarkOperationCompleted(true);
	return true;
}

//

FName FDiversionDeleteWorker::GetName() const
{
	return "Delete";
}

bool FDiversionDeleteWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	DeletedFiles = InCommand.Files;
	bool bSuccess = true;
	for(auto& File : InCommand.Files)
	{
		if (FPaths::FileExists(File))
		{
			// Diversion sync agent will pick up the changes and update automatically
			// UE status will be synced in the background by the provider
			if (!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*File))
			{
				UE_LOG(LogSourceControl, Error, TEXT("Diversion: Failed to delete file '%s'"), *File);
				bSuccess = false;
			}
		}
	}
	InCommand.MarkOperationCompleted(bSuccess);
	return bSuccess;
}

bool FDiversionDeleteWorker::UpdateStates() const
{
	// Clear pending delete entries for files processed by this Delete operation.
	// This is the safety-net cleanup - the Revert worker also clears for files it skipped.
	auto& Provider = FDiversionModule::Get().GetProvider();
	for (const FString& File : DeletedFiles)
	{
		Provider.ClearPendingDelete(File);
	}

	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionRevertWorker::GetName() const
{
	return "Revert";
}

bool FDiversionRevertWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	// Filter out files that are pending deletion - RunReset is unnecessary for them.
	// UE calls Revert before Delete (a P4 pattern), but Diversion doesn't need to reset files
	// that are about to be deleted. The pending delete set is snapshotted in FDiversionCommand.
	TArray<FString> FilesNeedingReset;
	SkippedPendingDeleteFiles.Reset();

	for (const FString& File : InCommand.Files)
	{
		if (InCommand.FilesPendingDelete.Contains(File))
		{
			SkippedPendingDeleteFiles.Add(File);
		}
		else
		{
			FilesNeedingReset.Add(File);
		}
	}

	if (SkippedPendingDeleteFiles.Num() > 0)
	{
		UE_LOG(LogSourceControl, Log, TEXT("Diversion: Skipping RunReset for %d file(s) pending deletion"), SkippedPendingDeleteFiles.Num());
	}

	bool Success = true;
	bool bRanStatusCalls = false;

	if (FilesNeedingReset.Num() > 0)
	{
		// Swap in the filtered file list for RunReset, then restore the original
		TArray<FString> OriginalFiles = MoveTemp(InCommand.Files);
		InCommand.Files = MoveTemp(FilesNeedingReset);

		// Run reset on the given paths. Note! this reset is a hard reset, it will remove all changes to the files even erasing new additions.
		// Status will be updated in the BG by the provider
		Success &= DiversionUtils::RunReset(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
		Success &= DiversionUtils::NotifyAgentSyncRequired(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
		Success &= DiversionUtils::RunUpdateStatus(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);
		bool temptyReturnContainer;
		Success &= DiversionUtils::GetPotentialFileClashes(InCommand, InCommand.InfoMessages,
			InCommand.ErrorMessages, PotentialClashes, temptyReturnContainer);

		InCommand.Files = MoveTemp(OriginalFiles);
		bRanStatusCalls = true;
	}

	// Only update cached states if status-producing calls actually ran
	bShouldUpdateStates = Success && bRanStatusCalls;

	InCommand.MarkOperationCompleted(Success);
	return Success;
}

bool FDiversionRevertWorker::UpdateStates() const
{
	FDiversionProvider& Provider = FDiversionModule::Get().GetProvider();

	// Clear pending delete entries for files that were processed by this Revert
	for (const FString& File : SkippedPendingDeleteFiles)
	{
		Provider.ClearPendingDelete(File);
	}

	if (!bShouldUpdateStates)
	{
		// Don't update states if we got an error from the backend
		// No need to log, the error is in the API call
		return false;
	}

	bool bUpdated = IDiversionWorker::UpdateStates();

	// This is only fine since it's a synchronous blocking call!
	bool bIsFullUpdateStatus = false;
	bUpdated &= Provider.UpdateCachedStates(States, bIsFullUpdateStatus);
	bUpdated &= Provider.UpdatePotentialClashedStates(PotentialClashes, bIsFullUpdateStatus);

	return bUpdated;
}

//

FName FDiversionUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FDiversionUpdateStatusWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	const TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FUpdateStatus>(InCommand.Operation);
	bool Success = true;

	if (InCommand.Files.Num() == 0)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Diversion: No files to update status for."));
		InCommand.MarkOperationCompleted(true);
		return true;
	}

	if(InCommand.GetConcurrency() == EConcurrency::Synchronous)
	{
		// Synchronous call should only block when updating states history
		if (Operation->ShouldUpdateHistory())
		{
			Success = UpdateHistory(InCommand);
		}

		if(!Operation->ShouldUpdateModifiedState())
		{
			// UE uses this flag during commit operation call flow.
			// We want to avoid setting the files as waiting for sync in this case.
			// So it won't think they don't contain changes.
			QueriedPaths = InCommand.Files;
		}
		// Replace the sync call by a background async one -
		// this will trigger it (Avoid waiting for the next check interval)
		bTriggerStatusReload = true;
	}
	else
	{
		if (Operation->ShouldUpdateHistory())
		{
			UpdateHistory(InCommand);
		}
		// Call update status normally
		QueriedPaths = InCommand.Files;
		Success &= DiversionUtils::RunUpdateStatus(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, false);
		bShouldUpdateStates = Success;
	}

	InCommand.MarkOperationCompleted(Success);
	return Success;
}


void FDiversionUpdateStatusWorker::MarkSyncingFiles() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	// Mark the files as being synced to give the user immediate feedback
	for(auto& Path : QueriedPaths)
	{
		const TSharedRef<FDiversionState> State = Provider.GetStateInternal(Path);
		if (State->WorkingCopyState != EWorkingCopyState::Unknown &&
			// Avoid tagging conflicted files as syncing since it prevents calling resolve
			!State->IsConflicted())
		{
			State->IsSyncing = true;
			Provider.AddSyncingState(Path, State);
		}
	}
}

bool FDiversionUpdateStatusWorker::IsFullRepoStatusQuery(const FString& RepoPath) const
{
	// Search for the repo path in the queried files, if found and the query
	// is recursive - it means we have a repo-wide status info to update
	return QueriedPaths.Contains(RepoPath) && bRecursiveRequest;
}

bool FDiversionUpdateStatusWorker::UpdateStates() const
{
	bool bUpdated = true;
	FDiversionProvider& Provider = FDiversionModule::Get().GetProvider();

	// update states history, if any
	for (const auto& History : Histories)
	{
		const TSharedRef<FDiversionState> State = Provider.GetStateInternal(History.Key);
		State->History = History.Value;
	}

	// If we ran get history, we should update the conflicted files as we fetched them implicitly
	UpdateConflicts();

	bool FullRepoUpdateRequested = IsFullRepoStatusQuery(Provider.GetWsInfo().GetPath());

	// This is a sync call - return here and trigger a BG status call
	if(bTriggerStatusReload)
	{
		if(!FullRepoUpdateRequested)
		{
			MarkSyncingFiles();
		}
		// Force a reload of the status to get the latest changes
		Provider.BackgroundStatusTriggerInstantCall();
		return true;
	}

	if(Provider.GetSyncStatus() != DiversionUtils::EDiversionWsSyncStatus::Completed)
	{
		// Handle syncing status - mark the queried files as being synced
		if(!FullRepoUpdateRequested)
		{
			MarkSyncingFiles();
		}
	}

	// Async status call
	if(bShouldUpdateStates)
	{
		Provider.UpdateCachedStates(States, FullRepoUpdateRequested);
	}

	return true;
}

bool FDiversionUpdateStatusWorker::UpdateHistory(FDiversionCommand& InCommand)
{
	// When fetching history, we need to have the updated states data. so running update status first
	bool Success = DiversionUtils::RunUpdateStatus(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages);

	bShouldUpdateConflicts = DiversionUtils::GetConflictedFiles(InCommand, InCommand.InfoMessages,
		InCommand.ErrorMessages, ConflictedFilesData,
		WorkspaceMergesList, BranchMergesList);
	bShouldUpdateConflicts &= (ConflictedFilesData.Num() > 0);

	for (auto& State : States)
	{
		// Get the history of the file in the current branch
		Success &= DiversionUtils::RunGetHistory(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages, State.Key, nullptr);
	}

	if (bShouldUpdateConflicts)
	{
		// Fetch conflict "remote revision" data
		for (auto& [FilePath, ConflictData] : ConflictedFilesData)
		{
			// Get the revision data for the conflicted with file on the other branch
			DiversionUtils::RunGetHistory(InCommand, InCommand.InfoMessages, InCommand.ErrorMessages,
				FilePath, &ConflictData.RemoteRevision);
		}
	}

	return Success;
}

bool FDiversionUpdateStatusWorker::UpdateConflicts() const
{
	if (!bShouldUpdateConflicts)
	{
		// It's not a failure, just no conflicts to update
		return true;
	}

	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.SetWorkspaceMergesList(WorkspaceMergesList);
	Provider.SetBranchMergesList(BranchMergesList);
	return Provider.UpdateConflictedStates(ConflictedFilesData);
}

//

FName FDiversionCopyWorker::GetName() const
{
	return "Copy";
}

bool FDiversionCopyWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	// Copies need no Diversion-specific handling; the new file is picked up by the next status update.
	InCommand.bCommandSuccessful = true;

	InCommand.MarkOperationCompleted(InCommand.bCommandSuccessful);
	return InCommand.bCommandSuccessful;
}

bool FDiversionCopyWorker::UpdateStates() const
{
	return IDiversionWorker::UpdateStates();
}

//

FName FDiversionResolveWorker::GetName() const
{
	return "Resolve";
}

bool FDiversionResolveWorker::Execute(class FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}

	for (const auto& File : InCommand.Files)
	{
		const auto FileName = DiversionUtils::ConvertRelativePathToDiversionFull(File, InCommand.WsInfo.GetPath());
		FilesToResolve.Add(FileName);
	}

	InCommand.MarkOperationCompleted(InCommand.bCommandSuccessful);
	return InCommand.bCommandSuccessful;
}

bool FDiversionResolveWorker::UpdateStates() const
{
	auto& Provider = FDiversionModule::Get().GetProvider();

	TMap<FString, FDiversionResolveInfo> ResolveFilesWithInfo;
	for(const auto& File: FilesToResolve)
	{
		if(auto ConflictedData = Provider.GetFileResolveInfo(File))
		{
			ResolveFilesWithInfo.Add(File, *ConflictedData);
		}
		else
		{
			UE_LOG(LogSourceControl, Warning, TEXT("Diversion: Trying to resolve a file %s which isn't in the conflicted files list"), *File);
		}
	}

	Provider.SetFilesToResolve(ResolveFilesWithInfo);
	// Remove the resolved files from the provider conflicted files
	for(const auto& File : FilesToResolve)
	{
		Provider.RemoveConflictedState(File);
	}

	return IDiversionWorker::UpdateStates();
}

FName FDiversionSyncWorker::GetName() const
{
	return "Sync";
}

bool FDiversionSyncWorker::Execute(FDiversionCommand& InCommand)
{
	if(!ExecuteValidityCheck(InCommand, GetName())) { return false;}
	// In Diversion files are automatically synced by the agent

	InCommand.MarkOperationCompleted(InCommand.bCommandSuccessful);
	return InCommand.bCommandSuccessful;
}

bool FDiversionSyncWorker::UpdateStates() const
{
	return true;
}


FName FDiversionGetPendingChangelistsWorker::GetName() const
{
	return "UpdateChangelistsStatus";
}

bool FDiversionGetPendingChangelistsWorker::Execute(FDiversionCommand& InCommand)
{
	if (!ExecuteValidityCheck(InCommand, GetName())) { return false; }


	TSharedRef<FUpdatePendingChangelistsStatus> Operation =
		StaticCastSharedRef<FUpdatePendingChangelistsStatus>(InCommand.Operation);
	InCommand.bCommandSuccessful = true;

	InCommand.MarkOperationCompleted(InCommand.bCommandSuccessful);
	return InCommand.bCommandSuccessful;
}

bool FDiversionGetPendingChangelistsWorker::UpdateStates() const
{
	bool bUpdated = false;
	const FDateTime Now = FDateTime::Now();

	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.AddCurrentChangesToChangelistState();
	bUpdated = true;
	return bUpdated;
}


// IDiversionWorker interface
FName FDiversionOperationNotSupportedWorker::GetName() const
{
	return FName();
}

bool FDiversionOperationNotSupportedWorker::Execute(FDiversionCommand& InCommand)
{
	// Put a notification that the operation is not supported by Diversion
	const FName OperationName = InCommand.Operation->GetName();
	const FText OperationUnsupported = FText::Format(
		LOCTEXT("OperationNotSupported", "Operation: {0} is inapplicable for Diversion"), FText::FromName(OperationName));
	InCommand.InfoMessages.Add(OperationUnsupported.ToString());
	InCommand.PopupNotification = MakeUnique<FDiversionNotification>(OperationUnsupported, TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Pending);

	InCommand.MarkOperationCompleted(false);
	return InCommand.bCommandSuccessful;
}

bool FDiversionOperationNotSupportedWorker::UpdateStates() const
{
	return true;
}

#pragma endregion

#undef LOCTEXT_NAMESPACE
