// Copyright 2024 Diversion Company, Inc. All Rights Reserved.


#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryMergeManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::RunGetMerges(const FDiversionCommand& InCommand,
	TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, TArray<Diversion::CoreAPI::Model::Merge>& OutMerges) {

	auto ErrorResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_listOpenMergesDelegate::Bind([&](){
		return false;
	});

	auto VariantResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_listOpenMergesDelegate::Bind(
		[&](const TVariant<TSharedPtr<Src_handlersv2_merge_list_open_merges_200_response>>& Variant) {
			if (!Variant.IsType<TSharedPtr<Src_handlersv2_merge_list_open_merges_200_response>>())
			{
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}
			auto Value = Variant.Get<TSharedPtr<Src_handlersv2_merge_list_open_merges_200_response>>();

			OutMerges.Append(Value->mItems);
			return true;
		}
	);

	if (!FDiversionAPIAccess::RepositoryMergeManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunGetMerges: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryMergeManipulationAPI->SrcHandlersv2MergeListOpenMerges(InCommand.WsInfo.RepoID,
		TOptional<FString>(), TOptional<FString>(), FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}


bool DiversionUtils::GetConflictedFiles(const FDiversionCommand& InCommand,  TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, TMap<FString, FDiversionResolveInfo>& OutConflicts,
	TArray<Diversion::CoreAPI::Model::Merge>& OutWorkspaceMergesList, TArray<Diversion::CoreAPI::Model::Merge>& OutBranchMergesList) {

	// Get the list of open merges for current branch
	TArray<Diversion::CoreAPI::Model::Merge> AllRepoMerges;
	if (!RunGetMerges(InCommand, OutInfoMessages, OutErrorMessages, AllRepoMerges))
	{
		return false;
	}

	TArray<Diversion::CoreAPI::Model::Merge> Merges;
	for (auto& Merge : AllRepoMerges)
	{
		// Filter out the merges based on either the workspace or branch(both for source and for target)
		if(Merge.mBase_ref == InCommand.WsInfo.BranchID ||
			Merge.mOther_ref == InCommand.WsInfo.BranchID)
		{
			Merges.Add(Merge);
			OutBranchMergesList.Add(Merge);
		}
		else if (Merge.mBase_ref == InCommand.WsInfo.WorkspaceID ||
			Merge.mOther_ref == InCommand.WsInfo.WorkspaceID)
		{
			Merges.Add(Merge);
			OutWorkspaceMergesList.Add(Merge);
		}
	}

	// Only the first merge is handled for now. Supporting multiple merges
	// requires extending the merge UI to show more than one.
	if (Merges.Num() > 0)
	{
		const auto& Merge = Merges[0];

		auto ErrorResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_getOpenMergeDelegate::Bind(
			[&]() {
				return false;
			}
		);
		auto VariantResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_getOpenMergeDelegate::Bind(
			[&](const TVariant<TSharedPtr<DetailedMerge>, TSharedPtr<Diversion::CoreAPI::Error>>& Variant) {

				if (!Variant.IsType<TSharedPtr<DetailedMerge>>())
				{
					// Unexpected response type
					OutErrorMessages.Add("Unexpected response type");
					return false;
				}
				auto Value = Variant.Get<TSharedPtr<DetailedMerge>>();


				FDiversionResolveInfo ConflictInfo;
				for (const auto& Conflict : Value->mConflicts)
				{
					ConflictInfo.BaseFile = DiversionUtils::ConvertRelativePathToDiversionFull(Conflict.mBase.mPath, InCommand.WsInfo.GetPath());
					ConflictInfo.BaseRevision = Merge.mAncestor_commit;
					ConflictInfo.RemoteRevision = Merge.mOther_ref;
					ConflictInfo.RemoteFile = DiversionUtils::ConvertRelativePathToDiversionFull(Conflict.mOther.mPath, InCommand.WsInfo.GetPath());
					ConflictInfo.MergeId = Merge.mId;
					ConflictInfo.ConflictId = Conflict.mConflict_id;
					if (Conflict.mResolved_side.IsSet())
					{
						ConflictInfo.ResolutionSide = Conflict.mResolved_side.GetValue();
					}
					OutConflicts.Add(ConflictInfo.BaseFile, ConflictInfo);
				}

				OutInfoMessages.Add(FString::Printf(TEXT("Found %d conflicted files"), OutConflicts.Num()));
				return true;
			}
		);

		if (!FDiversionAPIAccess::RepositoryMergeManipulationAPI)
		{
			UE_LOG(LogSourceControl, Error, TEXT("GetConflictedFiles: API not initialized"));
			return false;
		}

		if (!FDiversionAPIAccess::RepositoryMergeManipulationAPI->SrcHandlersv2MergeGetOpenMerge(
			InCommand.WsInfo.RepoID, Merge.mId, FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).
			HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages))
		{
			OutErrorMessages.Add(FString::Printf(TEXT("Failed to get conflicted files for merge %s"), *Merge.mId));
			return false;
		}
	}
	return true;
}
