// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::AgentInSync(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages)
{
	IDiversionWorker& Worker = static_cast<IDiversionWorker&>(InCommand.Worker.Get());

	auto ErrorResponse = DefaultApi::FgetWorkspaceSyncStatusDelegate::Bind([&]() {
		Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::Unknown;
		return false;
	});

	auto VariantResponse = DefaultApi::FgetWorkspaceSyncStatusDelegate::Bind([&](const TVariant<TSharedPtr<WorkspaceSyncStatus>>& Variant) {
		if (!Variant.IsType<TSharedPtr<WorkspaceSyncStatus>>())
		{
			Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::Unknown;
			// Unexpected response type
			OutErrorMessages.Add("Unexpected response type");
			return false;
		}
		auto Value = Variant.Get<TSharedPtr<WorkspaceSyncStatus>>();

		if (Value->mIsPaused)
		{
			Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::Paused;
		}
		else if (Value->mIsSyncComplete)
		{
			Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::Completed;
		}
		else
		{
			Worker.SyncStatus = DiversionUtils::EDiversionWsSyncStatus::InProgress;
		}

		return true;
	});

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("AgentInSync: API not initialized"));
		return false;
	}

	bool Success = FDiversionAPIAccess::AgentAPI->GetWorkspaceSyncStatus(InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID, FString(), {}, 5, 5).
		HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
	if (Success)
	{
		Success &= GetWorkspaceSyncProgress(InCommand, OutInfoMessages, OutErrorMessages);
	}

	return Success;
}
