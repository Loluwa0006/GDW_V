// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionCommitHelper.h"

#include "DiversionProvider.h"
#include "DiversionModule.h"

#include "ISourceControlModule.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "SourceControlWindows.h"
#include "FileHelpers.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/PackageName.h"
#include "Misc/MessageDialog.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "DiversionCommitHelper"

TWeakPtr<SNotificationItem> FDiversionCommitHelper::CommitNotification;

void FDiversionCommitHelper::ExecuteCommit()
{
	if (!ISourceControlModule::Get().IsEnabled())
	{
		return;
	}

	if (!ISourceControlModule::Get().GetProvider().IsAvailable())
	{
		FText Message = LOCTEXT("NoSCCConnection", "No connection to revision control available!");
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.Warning(Message);
		EditorErrors.Notify();
		return;
	}

	// Get source control locations to update status for
	TArray<FString> Filenames = SourceControlHelpers::GetSourceControlLocations();

	// Trigger async status update (same as Epic's ChoosePackagesToCheckIn)
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	FSourceControlOperationRef Operation = ISourceControlOperation::Create<FUpdateStatus>();
	SourceControlProvider.Execute(
		Operation,
		Filenames,
		EConcurrency::Asynchronous,
		FSourceControlOperationComplete::CreateStatic(&FDiversionCommitHelper::OnUpdateStatusComplete));

	// Show notification while updating (same pattern as Epic)
	if (CommitNotification.IsValid())
	{
		CommitNotification.Pin()->ExpireAndFadeout();
	}

	FNotificationInfo Info(LOCTEXT("CommitAssetsIndicator", "Looking for assets to commit..."));
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.0f;
	Info.FadeOutDuration = 1.0f;

	if (SourceControlProvider.CanCancelOperation(Operation))
	{
		Info.ButtonDetails.Add(FNotificationButtonInfo(
			LOCTEXT("Commit_CancelButton", "Cancel"),
			LOCTEXT("Commit_CancelButtonTooltip", "Cancel the commit operation."),
			FSimpleDelegate::CreateStatic(&FDiversionCommitHelper::OnCommitCancelled, Operation)
		));
	}

	CommitNotification = FSlateNotificationManager::Get().AddNotification(Info);

	if (CommitNotification.IsValid())
	{
		CommitNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

bool FDiversionCommitHelper::CanExecuteCommit()
{
	// Prevent concurrent commit operations
	if (CommitNotification.IsValid())
	{
		return false;
	}

	// Reuse Epic's logic - checks if source control is enabled and available
	return FSourceControlWindows::CanChoosePackagesToCheckIn();
}

void FDiversionCommitHelper::OnCommitCancelled(FSourceControlOperationRef InOperation)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.CancelOperation(InOperation);

	if (CommitNotification.IsValid())
	{
		CommitNotification.Pin()->ExpireAndFadeout();
	}
	CommitNotification.Reset();
}

void FDiversionCommitHelper::OnUpdateStatusComplete(
	const FSourceControlOperationRef& InOperation,
	ECommandResult::Type InResult)
{
	// Dismiss notification
	if (CommitNotification.IsValid())
	{
		CommitNotification.Pin()->ExpireAndFadeout();
	}
	CommitNotification.Reset();

	if (InResult != ECommandResult::Succeeded)
	{
		if (InResult == ECommandResult::Failed)
		{
			FText Message = LOCTEXT("CommitOperationFailed", "Failed checking revision control status!");
			FMessageLog EditorErrors("EditorErrors");
			EditorErrors.Warning(Message);
			EditorErrors.Notify();
		}
		return;
	}

	// Gather files from our cache (this is where we differ from Epic)
	TArray<FString> PackageNames;
	TArray<UPackage*> LoadedPackages;
	TArray<FString> ConfigFiles;

	GatherCommittableFiles(PackageNames, LoadedPackages, ConfigFiles);

	// Prompt user to save dirty packages before committing
	// This replicates what ChoosePackagesToCheckInCompleted does
	const FEditorFileUtils::EPromptReturnCode UserResponse =
		FEditorFileUtils::PromptForCheckoutAndSave(LoadedPackages, true, true);

	const bool bShouldProceed = (UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Success ||
								  UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Declined);
	if (!bShouldProceed)
	{
		if (UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Failure)
		{
			FText Message = NSLOCTEXT("UnrealEd", "SCC_Checkin_Aborted", "Check-in aborted as a result of save failure.");
			FMessageDialog::Open(EAppMsgType::Ok, Message);
		}
		return;
	}

	// Temporarily override UsesChangelists() to return false.
	//
	// In UE 5.6+, Epic's PromptForCheckin uses this to determine:
	//   const bool bAllowUncheckFiles = !bUsesSnapshots && !bUsesChangelists;
	//
	// When UsesChangelists() returns true (Diversion's default), file checkboxes
	// are hidden in the submit dialog, forcing users to commit all files.
	// By temporarily returning false, we enable file selection checkboxes.
	//
	// This also affects bValidationRequired which becomes false, making
	// validation results non-blocking (but validation may still run).
	ISourceControlProvider& ActiveProvider = ISourceControlModule::Get().GetProvider();
	if (ActiveProvider.GetName() != FName("Diversion"))
	{
		// Not the Diversion provider; cannot safely override changelist behavior.
		return;
	}
	FDiversionProvider& DiversionProvider = static_cast<FDiversionProvider&>(ActiveProvider);

	// Use RAII guard to ensure override is restored even if PromptForCheckin fails
	FDiversionProvider::FUsesChangelistsOverrideGuard Guard(DiversionProvider);

	// Use Epic's PromptForCheckin directly - this is a public API.
	// Pass source control locations as PendingDeletePaths to trigger deleted files search.
	// When InPendingDeletePaths.Num() > 0, UE searches for deleted files via GetCachedStateByPredicate.
	TArray<FString> PendingDeletePaths = SourceControlHelpers::GetSourceControlLocations();
	FSourceControlWindows::PromptForCheckin(
		/*bUseSourceControlStateCache=*/ true,
		PackageNames,
		PendingDeletePaths,
		ConfigFiles);
}

void FDiversionCommitHelper::GatherCommittableFiles(
	TArray<FString>& OutPackageNames,
	TArray<UPackage*>& OutLoadedPackages,
	TArray<FString>& OutConfigFiles)
{
	ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();

	// Get all committable files from the cache using the provider's method
	// In 5.6+ GetCachedStateByPredicate returns all files without filtering
	TArray<FSourceControlStateRef> AllStates = Provider.GetCachedStateByPredicate(
		[](const FSourceControlStateRef& State) -> bool
		{
			return IsCommittable(State);
		});

	// Use a set to track filenames and avoid duplicates
	TSet<FString> ProcessedFiles;

	for (const FSourceControlStateRef& State : AllStates)
	{
		const FString& Filename = State->GetFilename();

		// Skip if already processed
		if (ProcessedFiles.Contains(Filename))
		{
			continue;
		}
		ProcessedFiles.Add(Filename);

		// Check if this is actually a package file by extension
		// (TryConvertFilenameToLongPackageName can succeed for non-package files)
		const FString Extension = FPaths::GetExtension(Filename);
		const bool bIsPackageFile = FPackageName::IsPackageExtension(*Extension) ||
									FPackageName::IsTextPackageExtension(*Extension);

		FString PackageName;
		if (bIsPackageFile && FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName))
		{
			// It's a package file (asset, map, etc.)
			OutPackageNames.Add(PackageName);

			// Check if package is currently loaded (for save prompts)
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (Package != nullptr)
			{
				OutLoadedPackages.Add(Package);
			}
		}
		else
		{
			// Non-package file (config, code, etc.)
			OutConfigFiles.Add(Filename);
		}
	}
}

bool FDiversionCommitHelper::IsCommittable(const FSourceControlStateRef& State)
{
	// A file is committable if it can be checked in, or if it's not source controlled but can be added
	// This matches the logic in FindAllSubmittablePackageFiles and FindAllSubmittableConfigFiles
	if (State->CanCheckIn())
	{
		return true;
	}

	if (!State->IsSourceControlled() && State->CanAdd())
	{
		return true;
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
