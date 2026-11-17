// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionOperations.h"
#include "DiversionAPIAccess.h"
#include "AnalyticsApi.h"

#include "DiversionCommonUtils.h"
#include "HAL/PlatformMisc.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"


using namespace Diversion::CoreAPI;

bool DiversionUtils::SendAnalyticsEvent(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const TMap<
										FString, FString>& InProperties)
{
	auto ErrorResponse = AnalyticsApi::Fsrc_handlers_analytics_ingestDelegate::Bind([]() {return false; });
	auto VariantResponse = AnalyticsApi::Fsrc_handlers_analytics_ingestDelegate::Bind([&]() {
		OutInfoMessages.Add("Successfully sent analytics event");
		return true;
	});

	TSharedRef<FSendAnalytics, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FSendAnalytics>(InCommand.Operation);
	AnalyticsEvent Event;
	Event.mEvent = Operation->GetEventName().ToString();
	Event.mSource = "UE";
	for (auto& Elem : InProperties)
	{
		Event.mProperties.Add(Elem.Key, Elem.Value);
	}
	Event.mProperties.Add("$os", DiversionUtils::GetOperatingSystemName());
	Event.mProperties.Add("$os_version", FPlatformMisc::GetOSVersion());
	Event.mProperties.Add("ue_ver_major", FString::Printf(TEXT("%d"), ENGINE_MAJOR_VERSION));
	Event.mProperties.Add("ue_ver_minor", FString::Printf(TEXT("%d"), ENGINE_MINOR_VERSION));
	Event.mProperties.Add("ue_version", FString::Printf(TEXT("%d.%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION, ENGINE_PATCH_VERSION));
	Event.mProperties.Add("ue_language", FInternationalization::Get().GetCurrentCulture()->GetName());
	// Workspace context — add each field independently when meaningfully set
	auto IsMeaningfullySet = [](const FString& Value)
	{
		return !Value.IsEmpty() && Value != TEXT("N/a");
	};

	if (IsMeaningfullySet(InCommand.WsInfo.RepoID))
	{
		Event.mProperties.Add("repo_id", InCommand.WsInfo.RepoID);
	}
	if (IsMeaningfullySet(InCommand.WsInfo.WorkspaceID))
	{
		Event.mProperties.Add("workspace_id", InCommand.WsInfo.WorkspaceID);
	}
	if (IsMeaningfullySet(InCommand.WsInfo.BranchID))
	{
		Event.mProperties.Add("branch_id", InCommand.WsInfo.BranchID);
	}
	if (IsMeaningfullySet(InCommand.WsInfo.BranchName))
	{
		Event.mProperties.Add("branch_name", InCommand.WsInfo.BranchName);
	}

	auto AnalyticEvents = MakeShared<AnalyticsEvents>();
	AnalyticEvents->mEvents.Add(Event);

	if (!FDiversionAPIAccess::AnalyticsAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("SendAnalyticsEvent: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AnalyticsAPI->SrcHandlersAnalyticsIngest(AnalyticEvents, FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).
		HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
