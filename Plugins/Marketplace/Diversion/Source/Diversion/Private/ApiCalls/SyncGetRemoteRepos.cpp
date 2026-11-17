// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryManagementApi.h"

using namespace Diversion::CoreAPI;

bool DiversionUtils::GetRemoteRepos(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, bool InOwnedOnly, TArray<FString>& OutReposList)
{

	const auto ErrorResponse = RepositoryManagementApi::Fsrc_handlersv2_repo_listAllDelegate::Bind(
		[&]() {
			return false;
		}
	);
	const auto VariantResponse = RepositoryManagementApi::Fsrc_handlersv2_repo_listAllDelegate::Bind(
		[&](const TVariant<TSharedPtr<Src_handlersv2_repo_list_all_200_response>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {

			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				const auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				OutErrorMessages.Add(FString::Printf(TEXT("Received error for list repos api call: %s"), *Value->mDetail));
				return false;
			}

			if (!Variant.IsType<TSharedPtr<Src_handlersv2_repo_list_all_200_response>>())
			{
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}
			auto Value = Variant.Get<TSharedPtr<Src_handlersv2_repo_list_all_200_response>>();

			for (auto& RepoItem : Value->mItems)
			{
				OutReposList.Add(RepoItem.mRepo_name);
			}
			return true;
		}
	);

	if (!FDiversionAPIAccess::RepositoryManagementAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("GetRemoteRepos: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryManagementAPI->SrcHandlersv2RepoListAll(
		InOwnedOnly, FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID),
		{}, 5, 120).
	HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
