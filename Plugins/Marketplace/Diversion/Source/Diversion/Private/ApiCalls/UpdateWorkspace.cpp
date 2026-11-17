// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionConstants.h"
#include "DiversionAPIAccess.h"
#include "RepositoryWorkspaceManipulationApi.h"


using namespace Diversion::CoreAPI;


#define LOCTEXT_NAMESPACE "Diversion"


bool DiversionUtils::UpdateWorkspace(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages)
{
	auto ErrorResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_forwardWorkspaceDelegate::Bind(
		[&]() {
			return false;
		}
	);

	auto VariantResponse = RepositoryWorkspaceManipulationApi::Fsrc_handlersv2_workspace_forwardWorkspaceDelegate::Bind(
		[&](const TVariant<void*, TSharedPtr<MergeId>, TSharedPtr<Diversion::CoreAPI::Error>>& Variant, int StatusCode) {
			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				OutErrorMessages.Add(FString::Printf(TEXT("Received error for forward workspace call: %s"), *Value->mDetail));
				return false;
			}

			if (!Variant.IsType<TSharedPtr<MergeId>>() && !Variant.IsType<void*>())
			{
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}

			if (StatusCode == 202)
			{
				FNotificationButtonInfo UpdateButton(LOCTEXT("DiversionPopup_OpenMergesInDiversion", "Show in Diversion"),
					LOCTEXT("DiversionPopup_OpenMergesInDiversion_Tooltip", "Open Merge view in Diversion"),
					FSimpleDelegate::CreateLambda([]() {
						auto& Provider = FDiversionModule::Get().GetProvider();
						FFormatNamedArguments Args;
						Args.Add(TEXT("RepoID"), FText::FromString(Provider.GetWsInfo().RepoID));

						FString UrlTemplate = DIVERSION_WEB_URL "repo/{RepoID}/merges";
						if (Provider.GetWorkspaceMergesList().Num() > 0)
						{
							FString MergeID = Provider.GetWorkspaceMergesList()[0].mId;
							Args.Add(TEXT("MergeID"), FText::FromString(MergeID));
							UrlTemplate += "/{MergeID}";
						}

						// Can't simply know if the DesktopApp is installed and
						// connected to DIVERSION_APP_URL, so opening the web URL
						FText Url = FText::Format(FTextFormat::FromString(UrlTemplate), Args);
						if (FPlatformProcess::CanLaunchURL(*Url.ToString()))
						{
							FPlatformProcess::LaunchURL(*Url.ToString(), nullptr, nullptr);
						}
						}), SNotificationItem::CS_Pending);

				InCommand.PopupNotification = MakeUnique<FDiversionNotification>(
					LOCTEXT("Diversion_ConflictedFilesInWorkspace",
						"Workspace contains conflicted files. Resolve conflicts first to continue with the workspace update."),
					TArray<FNotificationButtonInfo>({ UpdateButton }),
					SNotificationItem::CS_Pending);
			}

			return true;
		}
	);

	if (!FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("UpdateWorkspace: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI->SrcHandlersv2WorkspaceForwardWorkspace(
		InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID,
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}

#undef LOCTEXT_NAMESPACE
