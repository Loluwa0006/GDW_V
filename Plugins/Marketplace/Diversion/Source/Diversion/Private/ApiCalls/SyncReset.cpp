// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryWorkspaceManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::RunReset(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages)
{
	auto ErrorResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_resetDelegate::Bind([&]() {
		return false;
	});
	auto VariantResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_resetDelegate::Bind([&](const TVariant<TSharedPtr<ResetStatus>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
		if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
		{
			auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
			OutErrorMessages.Add(FString::Printf(TEXT("Received error for reset call: %s"), *Value->mDetail));
			return false;
		}

		if (!Variant.IsType<TSharedPtr<ResetStatus>>())
		{
			// Unexpected response type
			OutErrorMessages.Add("Unexpected response type");
			return false;
		}

		OutInfoMessages.Add("Successfully reset path(s)");
		return true;
	});

	TSharedPtr<Src_handlersv2_workspace_reset_request> Request = MakeShared<Src_handlersv2_workspace_reset_request>();
	Request->mDelete_added = true;

	TArray<FString> RelativeFilesPaths;
	Algo::Transform(InCommand.Files, RelativeFilesPaths, [&InCommand](const FString& Path) {
		return ConvertFullPathToRelative(Path, InCommand.WsInfo.GetPath());
	});
	Request->mPaths = RelativeFilesPaths;

	if (!FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunReset: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI->SrcHandlersv2WorkspaceReset(InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID,
		Request, FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
