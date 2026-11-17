// Copyright 2024 Diversion Company, Inc. All Rights Reserved.


#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryWorkspaceManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::GetPotentialFileClashes(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
TArray<FString>& OutErrorMessages, TMap<FString, TArray<EDiversionPotentialClashInfo>>& OutPotentialClashes, bool& OutRecurseCall)
{

	auto ErrorResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_getOtherStatusesDelegate::Bind(
		[&]() {
			return false;
		}
	);
	auto VariantResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_getOtherStatusesDelegate::Bind(
		[&](const TVariant<TSharedPtr<RefsFilesStatus>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {

			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				OutErrorMessages.Add(FString::Printf(TEXT("Received error for get other statuses call: %s"), *Value->mDetail));
				return false;
			}

			if (!Variant.IsType<TSharedPtr<RefsFilesStatus>>())
			{
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}
			auto Value = Variant.Get<TSharedPtr<RefsFilesStatus>>();

			const FString WsPath = InCommand.WsInfo.GetPath();

			for (auto& status : Value->mStatuses)
			{
				auto FullStatusFilePath = DiversionUtils::ConvertRelativePathToDiversionFull(status.mPath, WsPath);
				TArray<EDiversionPotentialClashInfo> PotentialClashes;
				for (auto& FileStatus : status.mFile_statuses)
				{
					PotentialClashes.Add(EDiversionPotentialClashInfo(
						FileStatus.mCommit_id,
						FileStatus.mWorkspace_id.GetPtrOrNull() ? *FileStatus.mWorkspace_id : "",
						FileStatus.mBranch_name.GetPtrOrNull() ? *FileStatus.mBranch_name : "N/a",
						FileStatus.mAuthor.mEmail.GetPtrOrNull() ? *FileStatus.mAuthor.mEmail : "",
						FileStatus.mAuthor.mFull_name.GetPtrOrNull() ? *FileStatus.mAuthor.mFull_name : "",
						FileStatus.mMtime.GetPtrOrNull() ? *FileStatus.mMtime : -1
					));
				}
				OutPotentialClashes.Add(FullStatusFilePath, PotentialClashes);
			}

			// Remove outdated potential clash data
			for (auto& Path : InCommand.Files)
			{
				if (!FPaths::IsUnderDirectory(Path, WsPath))
				{
					UE_LOG(LogSourceControl, Log, TEXT("Path: %s is not contained in the repo, skipping."), *Path);
					continue;
				}
				auto FullStatusFilePath = DiversionUtils::ConvertRelativePathToDiversionFull(Path, WsPath);
				if (OutPotentialClashes.Contains(FullStatusFilePath))
				{
					continue;
				}
				// Indicate that there are no potential clashes for the file we queried
				OutPotentialClashes.Add(FullStatusFilePath, TArray<EDiversionPotentialClashInfo>());
			}

			return true;
		}
	);


	const int PrefixesLimit = 20;
	bool Success = true;

	// Recurse request if the full repo is being updated
	bool FullRepoUpdateRequested = (InCommand.Files.Num() == 1) && (InCommand.Files[0] == InCommand.WsInfo.GetPath());
	bool Recurse = FullRepoUpdateRequested;
	OutRecurseCall = FullRepoUpdateRequested;

	TArray<FString> FullPrefixesArray = GetPathsCommonPrefixes(InCommand.Files, InCommand.WsInfo.GetPath()).Array();
	int PrefixesRequestOffset = 0;

	while (PrefixesRequestOffset < FullPrefixesArray.Num())
	{
		// Get the relevant prefixes to send to the backend in the current request

		if(!DiversionUtils::DiversionValidityCheck(PrefixesRequestOffset < FullPrefixesArray.Num(),
			FString::Printf(TEXT("Prefix request offset %d exceeds the number of prefixes %d"), PrefixesRequestOffset, FullPrefixesArray.Num()), InCommand.WsInfo.AccountID))
		{
			// Should never happen!
			break;
		}

		// Try taking as much elements as possible left in the FullPrefixesArray, but not passing the limit
		int ElementsToTake = FullPrefixesArray.Num() - PrefixesRequestOffset < PrefixesLimit ? (FullPrefixesArray.Num() - PrefixesRequestOffset) : PrefixesLimit;
		if (!DiversionUtils::DiversionValidityCheck(PrefixesRequestOffset + ElementsToTake <= FullPrefixesArray.Num(),
			FString::Printf(TEXT("Prefix batch of %d at offset %d exceeds the number of prefixes %d"), ElementsToTake, PrefixesRequestOffset, FullPrefixesArray.Num()), InCommand.WsInfo.AccountID))
		{
			// Should never happen!
			break;
		}

		auto PartialPrefixesView = TArrayView<FString>(&FullPrefixesArray[PrefixesRequestOffset], ElementsToTake);

		// Convert view to array with relative paths
		TArray<FString> RelativePartialPrefixesArray;
		for (auto& Prefix : PartialPrefixesView)
		{
			RelativePartialPrefixesArray.Add(DiversionUtils::ConvertFullPathToRelative(Prefix, InCommand.WsInfo.GetPath()));
		}

		if (!FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI)
		{
			UE_LOG(LogSourceControl, Error, TEXT("GetPotentialFileClashes: API not initialized"));
			return false;
		}

		// Send request of the current offset
		Success &= FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI->SrcHandlersv2WorkspaceGetOtherStatuses(
			InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID, TOptional<FString>(), RelativePartialPrefixesArray, TOptional<int32>(), TOptional<int32>(), Recurse,
			FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);

		if (!Success)
		{
			break;
		}

		PrefixesRequestOffset += ElementsToTake;
	}

	return Success;
}
