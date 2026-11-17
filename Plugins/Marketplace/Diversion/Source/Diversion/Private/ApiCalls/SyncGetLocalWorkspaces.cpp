// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::GetAllLocalWorkspaces(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	TArray<WorkspaceInfo>& OutLocalWorkspaces)
{
	auto ErrorResponse = DefaultApi::FgetAllWorkspacesDelegate::Bind([&]() {
		OutErrorMessages.Add("Failed fetching all cloned workspaces");
		return false;
	});

	auto VariantResponse = DefaultApi::FgetAllWorkspacesDelegate::Bind(
		[&](const TVariant<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>& Variant) {
			if (!Variant.IsType<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>())
			{
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}
			auto Value = Variant.Get<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>();

			for (auto& [_, wsItem] : Value)
			{
				WorkspaceInfo WsInfo;
				WsInfo.WorkspaceName = "";
				WsInfo.WorkspaceID = wsItem->mWorkspaceID;
				WsInfo.RepoID = wsItem->mRepoID;
				WsInfo.RepoName = wsItem->mRepoName.IsSet() ? wsItem->mRepoName.GetValue() : "";
				WsInfo.SetPath(wsItem->mPath);
				WsInfo.AccountID = wsItem->mAccountID;
				WsInfo.BranchID = wsItem->mBranchID.IsSet() ? wsItem->mBranchID.GetValue() : "";
				WsInfo.BranchName = wsItem->mBranchName.IsSet() ? wsItem->mBranchName.GetValue() : "";
				WsInfo.CommitID = wsItem->mCommitID;

				OutLocalWorkspaces.Add(WsInfo);
			}
			return true;
		}
	);

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("GetAllLocalWorkspaces: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AgentAPI->GetAllWorkspaces(FString(), {},
		5, 5)
		.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
