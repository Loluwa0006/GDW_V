// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionOperations.h"
#include "DiversionAPIAccess.h"
#include "RepositoryCommitManipulationApi.h"


#define LOCTEXT_NAMESPACE "Diversion"


using namespace Diversion::CoreAPI;


bool DiversionUtils::RunCommit(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InDescription, bool WaitForSync, FString* OutCommitId)
{
	const auto ErrorResponse = RepositoryCommitManipulationApi::Fsrc_handlersv2_workspace_commitWorkspaceDelegate::Bind(
		[&](const TVariant<TSharedPtr<NewCommit>, void*, TSharedPtr<Src_handlersv2_workspace_commit_workspace_400_response>, TSharedPtr<Diversion::CoreAPI::Error>, TSharedPtr<CommitPaymentResponse>>& Variant, int StatusCode) {
			const FText COMMIT_CONFLICT_ERROR_NOTIFICATION_MESSAGE = LOCTEXT("DiversionPopup_CommitConflicts",
				"Unable to commit due to existing conflicts, "
				"please run update using the button below or the Diversion "
				"desktop app and resolve the conflicts to continue.");

			FNotificationButtonInfo UpdateButton(LOCTEXT("DiversionPopup_UpdateButton", "Update"),
				LOCTEXT("DiversionPopup_UpdateButton_Tooltip", "Update the workspace to pull the latest changes and show any conflicts"),
				FSimpleDelegate::CreateLambda([]()
					{
						FDiversionModule::Get().GetProvider().Execute(ISourceControlOperation::Create<FUpdateWorkspaceOperation>(),
							nullptr, {}, EConcurrency::Asynchronous);
					}),
				SNotificationItem::CS_Fail);

			if (StatusCode == 409)
			{
				InCommand.PopupNotification = MakeUnique<FDiversionNotification>(
					COMMIT_CONFLICT_ERROR_NOTIFICATION_MESSAGE,
					TArray<FNotificationButtonInfo>({ UpdateButton }),SNotificationItem::CS_Fail);
			}
			else if (StatusCode == 423 && Variant.IsType<TSharedPtr<Diversion::CoreAPI::Error>>())
			{
				// File(s) are hard-locked by other users - the server rejected the commit.
				const auto& LockedError = Variant.Get<TSharedPtr<Diversion::CoreAPI::Error>>();
				if (LockedError.IsValid())
				{
					// The raw server detail (incl. the HTTP code) is only for the log
					OutErrorMessages.Add(FString::Printf(TEXT("Commit failed (HTTP %d): %s"), StatusCode, *LockedError->mDetail));

					// The popup shows a structured, code-free message. The server detail format is
					// "Cannot commit: file(s) are locked by other users: <p1>, <p2>" - extract the
					// path list; fall back to the raw detail if the format ever changes.
					FText LockedFilesList;
					const FString LockedFilesPrefix = TEXT("locked by other users:");
					const int32 PrefixIdx = LockedError->mDetail.Find(LockedFilesPrefix, ESearchCase::IgnoreCase);
					if (PrefixIdx != INDEX_NONE)
					{
						TArray<FString> LockedPaths;
						LockedError->mDetail.Mid(PrefixIdx + LockedFilesPrefix.Len()).ParseIntoArray(LockedPaths, TEXT(","));
						constexpr int MaxShownPaths = 5;
						TArray<FString> ShownPaths;
						for (int i = 0; i < LockedPaths.Num() && i < MaxShownPaths; ++i)
						{
							ShownPaths.Add(LockedPaths[i].TrimStartAndEnd());
						}
						LockedFilesList = FText::FromString(FString::Join(ShownPaths, TEXT("\n")));
						if (LockedPaths.Num() > MaxShownPaths)
						{
							LockedFilesList = FText::Format(LOCTEXT("DiversionPopup_CommitLockedFiles_More", "{0}\n... and {1} more"),
								LockedFilesList, FText::AsNumber(LockedPaths.Num() - MaxShownPaths));
						}
					}
					else
					{
						LockedFilesList = FText::FromString(LockedError->mDetail);
					}

					const FText LockedPopupMessage = FText::Format(LOCTEXT("DiversionPopup_CommitLockedFiles",
						"Commit blocked - file(s) locked by another user:\n\n{0}\n\nSaving these changes requires the files to be unlocked by their holder or a repository admin."),
						LockedFilesList);
					InCommand.PopupNotification = MakeUnique<FDiversionNotification>(
						LockedPopupMessage, TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail);
				}
			}
			else if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Error>>())
			{
				const auto& ParsedError = Variant.Get<TSharedPtr<Diversion::CoreAPI::Error>>();
				if (ParsedError.IsValid())
				{
					OutErrorMessages.Add(FString::Printf(TEXT("Commit failed (HTTP %d): %s"), StatusCode, *ParsedError->mDetail));
				}
			}
			else if (Variant.IsType<TSharedPtr<CommitPaymentResponse>>())
			{
				const auto& PaymentError = Variant.Get<TSharedPtr<CommitPaymentResponse>>();
				if (PaymentError.IsValid())
				{
					OutErrorMessages.Add(FString::Printf(TEXT("Commit failed (HTTP %d): %s"), StatusCode, *PaymentError->mDetail));
				}
			}
			return false;
		}
	);

	const auto VariantResponse = RepositoryCommitManipulationApi::Fsrc_handlersv2_workspace_commitWorkspaceDelegate::Bind(
		[&](const TVariant<TSharedPtr<NewCommit>, void*, TSharedPtr<Src_handlersv2_workspace_commit_workspace_400_response>, TSharedPtr<Diversion::CoreAPI::Error>, TSharedPtr<CommitPaymentResponse>>& Variant) {
			if (Variant.IsType<TSharedPtr<NewCommit>>())
			{
				const auto Response = Variant.Get<TSharedPtr<NewCommit>>();
				InCommand.InfoMessages.Add(FString::Printf(TEXT("Commit %s was added successfully"), *Response->mId));
				if (OutCommitId)
				{
					*OutCommitId = Response->mId;
				}
				return true;
			}

			return true;
		}
	);

	if (WaitForSync && !WaitForAgentSync(InCommand, OutInfoMessages, OutErrorMessages))
	{
		return false;
	}

	TOptional<TArray<FString>> CommitPaths;
	if (InCommand.Files.Num() > 0)
	{
		// Note! zero files means commit all!
		// This is for internal use as using UE UI its not possible to send 0 files
		CommitPaths = TArray<FString>();
	}
	for (auto& FilePath : InCommand.Files)
	{
		// TODO: Convert all file paths in the command to relative paths upon command creation
		auto RelativeFilePath = FilePath.Replace(*InCommand.WsInfo.GetPath(), TEXT(""));
		CommitPaths->Add(RelativeFilePath);
	}

	const TSharedPtr<CommitRequest> Request = MakeShared<CommitRequest>();
	Request->mCommit_message = InDescription;
	Request->mInclude_paths = CommitPaths;

	if (!FDiversionAPIAccess::RepositoryCommitManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunCommit: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryCommitManipulationAPI->SrcHandlersv2WorkspaceCommitWorkspace(InCommand.WsInfo.RepoID, InCommand.WsInfo.WorkspaceID, Request,
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}

#undef LOCTEXT_NAMESPACE
