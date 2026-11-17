// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::NotifyAgentSyncRequired(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages)
{
	// Triggers an immediate sync on the agent (Pull updates etc. )
	// Should be used for commands that changes the backend state and requires immediate sync
	auto ErrorResponse = DefaultApi::FnotifySyncRequiredDelegate::Bind([&]() {
		return false;
	});

	auto VariantResponse = DefaultApi::FnotifySyncRequiredDelegate::Bind([&]() {
		return true;
	});

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("NotifyAgentSyncRequired: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AgentAPI->NotifySyncRequired(InCommand.WsInfo.RepoID,
		InCommand.WsInfo.WorkspaceID, FString(), {}, 5, 5).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}

