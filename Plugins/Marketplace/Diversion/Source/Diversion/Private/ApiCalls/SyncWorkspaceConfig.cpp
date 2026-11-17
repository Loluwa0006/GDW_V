// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionOperations.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::GetWorkspaceConfigByPath(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InPath)
{
	auto ErrorResponse = DefaultApi::FgetWorkspaceByPathDelegate::Bind([&]() {
		return false;
	});

	auto& Worker = static_cast<FDiversionWsInfoWorker&>(InCommand.Worker.Get());

	auto VariantResponse = DefaultApi::FgetWorkspaceByPathDelegate::Bind([&](const TVariant<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>& Variant) {
		if (!Variant.IsType<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>())
		{
			OutErrorMessages.Add("Unexpected response type");
			return false;
		}

		auto Value = Variant.Get<TMap<FString, TSharedPtr<WorkspaceConfiguration>>>();

		if (Value.Num() <= 0)
		{
			if (InCommand.WsInfo.IsValid())
			{
				FString ErrorMessage = FString::Printf(TEXT("No workspaces found in the given path: %s"), *InPath);
				OutErrorMessages.Add(ErrorMessage);
			}
			return false;
		}

		// We care only about the first workspace (The return shouldn't have more than one)
		auto& ResultWsConfig = Value.begin()->Value;
		auto& ResultWsId = Value.begin()->Key;

		Worker.WsInfo.WorkspaceID = ResultWsId;
		Worker.WsInfo.WorkspaceName = "";
		Worker.WsInfo.RepoID = ResultWsConfig->mRepoID;
		Worker.WsInfo.RepoName = ResultWsConfig->mRepoName.IsSet() ? ResultWsConfig->mRepoName.GetValue() : "";
		Worker.WsInfo.SetPath(ResultWsConfig->mPath);
		Worker.WsInfo.AccountID = ResultWsConfig->mAccountID;
		Worker.WsInfo.BranchID = ResultWsConfig->mBranchID.IsSet() ? ResultWsConfig->mBranchID.GetValue() : "";
		Worker.WsInfo.BranchName = ResultWsConfig->mBranchName.IsSet() ? ResultWsConfig->mBranchName.GetValue() : "";
		Worker.WsInfo.CommitID = ResultWsConfig->mCommitID;

		return true;
	});

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("GetWorkspaceConfigByPath: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AgentAPI->GetWorkspaceByPath(InPath, FString(), {}, 5, 5)
		.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
