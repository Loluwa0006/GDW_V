// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "IDiversionStatusWorker.h"
#include "DiversionAPIAccess.h"
#include "RepositoryWorkspaceManipulationApi.h"
#include "Types.h" // DiversionHttp::HTTP_STATUS_CUSTOM_REQUEST_NOT_SENT

using namespace Diversion::CoreAPI;

constexpr int StatusItemsLimit = 1500;

bool IsPathContained(const FString& InPath, const TArray<FString>& InCommandPaths)
{
	for (auto& CommandPath : InCommandPaths)
	{
		if (FPaths::IsUnderDirectory(InPath, CommandPath))
		{
			return true;
		}
	}
	return false;
}

void ParseStateFromList(const TArray<FileEntry>& InItems,
	const FString& InWsPath, const TArray<FString>& InCommandPaths,
	const EWorkingCopyState::Type& InState, const FDateTime& InDefaultMtime,
	const int InLocalRevNumber, TMap<FString, FDiversionState>& OutStates, const TArray<FString>& ConflictedFiles) {
	for (const auto& File : InItems)
	{
		FString FullItemPath = DiversionUtils::ConvertRelativePathToDiversionFull(File.mPath, InWsPath);
		// (Optimization): Only add state if FullItemPath is a subpath(or equal) of at least one of the InCommandPaths
		if (!IsPathContained(FullItemPath, InCommandPaths))
		{
			continue;
		}

		// Don't override conflicted files states if we have ongoing merge
		if (ConflictedFiles.Contains(FullItemPath))
		{
			continue;
		}

		FDiversionState FileState(FullItemPath);
		FileState.WorkingCopyState = InState;
		FileState.TimeStamp = File.mMtime.IsSet() ? File.mMtime.GetValue() : InDefaultMtime;
		FileState.LocalRevNumber = InLocalRevNumber;
		FileState.Hash = File.mHash.IsSet() ? File.mHash.GetValue() : TEXT("");
		OutStates.Add(FullItemPath, FileState);
	}
}

bool AllPathsAreFiles(const TArray<FString>& InPaths)
{
	for (auto& Path : InPaths)
	{
		if (FPaths::DirectoryExists(Path))
		{
			return false;
		}
	}
	return true;
}



bool DiversionUtils::RunUpdateStatus(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, bool WaitForSync){

	IDiversionStatusWorker& Worker = static_cast<IDiversionStatusWorker&>(InCommand.Worker.Get());
	auto ErrorResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_getStatusDelegate::Bind(
		[&]() {
			return false;
		});


	auto VariantResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_getStatusDelegate::Bind(
		[&](const TVariant<TSharedPtr<WorkspaceStatus>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				OutErrorMessages.Add(FString::Printf(TEXT("Received error for status call: %s"), *Value->mDetail));
				return false;
			}

			if (!Variant.IsType<TSharedPtr<WorkspaceStatus>>())
			{
				Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::Unknown;
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}

			auto Value = Variant.Get<TSharedPtr<WorkspaceStatus>>();

			if (!Value->mItems.IsSet())
			{
				OutErrorMessages.Add("Failed parsing status response");
				return false;
			}

			// Since the command paths might be a superset of the paths in the response, we need to
			// traverse the response and update the states accordingly
			auto& Items = Value->mItems.GetValue();

			const FDateTime Now = FDateTime::Now();
			const int LocalRevNumber = DiversionUtils::GetWorkspaceRevisionByCommit(InCommand.WsInfo.CommitID);
			const FString WsPath = InCommand.WsInfo.GetPath();

			// Handle controlled files
			ParseStateFromList(Items.mr_new, WsPath, InCommand.Files, EWorkingCopyState::Added, Now, LocalRevNumber, Worker.States, InCommand.ConflictedFiles);
			ParseStateFromList(Items.mModified, WsPath, InCommand.Files, EWorkingCopyState::Modified, Now, LocalRevNumber, Worker.States, InCommand.ConflictedFiles);
			ParseStateFromList(Items.mDeleted, WsPath, InCommand.Files, EWorkingCopyState::Deleted, Now, LocalRevNumber, Worker.States, InCommand.ConflictedFiles);

			// Handle non-controlled files
			for (auto& Path : InCommand.Files)
			{
				// The assumption is UE provides us absolute path
				if (!FPaths::IsUnderDirectory(Path, WsPath))
				{
					UE_LOG(LogSourceControl, Log, TEXT("Path: %s is not contained in the repo, skipping."), *Path);
					continue;
				}

				// A path the backend already reported keeps that state, even if the file was
				// re-created locally since. The next status poll after the agent syncs corrects it.
				if (Worker.States.Contains(Path))
				{
					continue;
				}

				FDiversionState FileState(Path);

				FileState.SetStateFromDiskPresence();
				Worker.States.Add(Path, FileState);
			}

			// This is only relevant to know if an update operation is needed or not
			// in the context of UE plugin
			Worker.WorkspaceUpdateRequired = Value->mConflicts.IsSet() &&
				Value->mConflicts.GetValue().Num() > 0;

			// Handle pagination
			Worker.ItemsFetchedNum = Items.mr_new.Num() + Items.mModified.Num() + Items.mDeleted.Num();
			Worker.bIncompleteResult = Value->mIncomplete_result.IsSet() && Value->mIncomplete_result.GetValue();

			return true;
		});



	TArray<FString> CommonPrefixes = GetPathsCommonPrefixes(InCommand.Files, InCommand.WsInfo.GetPath()).Array();

	// Avoid recursive status requests if all paths are files and on the same directory
	bool bRequestStatusOfOnlyOneDirectory = AllPathsAreFiles(InCommand.Files) && CommonPrefixes.Num() == 1;
	Worker.bRecursiveRequest = !bRequestStatusOfOnlyOneDirectory;

	FString AncestorPrefix = DiversionUtils::ConvertFullPathToRelative(FindCommonAncestorDirectory(InCommand.Files),
		InCommand.WsInfo.GetPath());

	if (!FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunUpdateStatus: API not initialized"));
		return false;
	}

	TMap<FString, FString> Headers;
	Headers.Add(TEXT("If-None-Match"), TEXT(""));

	int RequestOffset = 0;
	bool Success = true;
	while (true)
	{
		auto Result = FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI->SrcHandlersv2WorkspaceGetStatus(
			InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID, true, StatusItemsLimit, RequestOffset, !bRequestStatusOfOnlyOneDirectory,
			AncestorPrefix, false, FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), Headers, 5, 120);

		Success &= Result.HandleApiResponse(ErrorResponse, VariantResponse, OutInfoMessages);

		// The request was not sent (editor unfocused, or shutdown in progress); bIncompleteResult is
		// stale from the previous iteration, so stop paginating rather than spinning on requests that
		// won't be sent.
		if (Result.StatusCode == DiversionHttp::HTTP_STATUS_CUSTOM_REQUEST_NOT_SENT)
		{
			break;
		}

		if (!Worker.bIncompleteResult)
		{
			break;
		}
		RequestOffset += Worker.ItemsFetchedNum;
	}

	return Success;
}
