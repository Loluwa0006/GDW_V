// Copyright 2024 Diversion Company, Inc. All Rights Reserved.


#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryMergeManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::RunFinalizeMerge(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InMergeId)
{

	auto ErrorResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_finalizeDelegate::Bind(
		[&]() {
			return false;
		}
	);
	auto VariantResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_finalizeDelegate::Bind(
		[&]() {
			return true;
		}
	);

	TSharedPtr<CommitMessage> CommitMessageRequest = MakeShared<CommitMessage>();
	CommitMessageRequest->mCommit_message = FString("Merged " + InMergeId);

	if (!FDiversionAPIAccess::RepositoryMergeManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunFinalizeMerge: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryMergeManipulationAPI->SrcHandlersv2MergeFinalize(InCommand.WsInfo.RepoID, InMergeId, CommitMessageRequest,
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}

