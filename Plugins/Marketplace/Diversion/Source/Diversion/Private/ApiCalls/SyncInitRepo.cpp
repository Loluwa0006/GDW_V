// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::RunRepoInit(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InRepoRootPath, const FString& InRepoName)
{
	auto ErrorResponse = DefaultApi::FrepoInitDelegate::Bind([&]() {
		OutErrorMessages.Add("Failed initializing repo in the provided path.");
		return false;
	});

	auto VariantResponse = DefaultApi::FrepoInitDelegate::Bind([&]() {
		OutInfoMessages.Add("Repo initialized successfully");
		return true;
	});

	auto initRepoRequestData = MakeShared<InitRepo>();
	initRepoRequestData->mName = InRepoName;
	initRepoRequestData->mPath = InRepoRootPath;

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunRepoInit: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AgentAPI->RepoInit(initRepoRequestData, FString(), {}, 5, 120).
		HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
