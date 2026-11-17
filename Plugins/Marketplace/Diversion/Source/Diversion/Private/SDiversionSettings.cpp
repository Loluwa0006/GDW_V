// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "SDiversionSettings.h"

// Widgets
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Input/SDirectoryPicker.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

// Misc
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

// Diversion
#include "DiversionUtils.h"
#include "DiversionModule.h"
#include "DiversionProvider.h"
#include "DiversionOperations.h"
#include "SourceControlOperations.h"
#include "DiversionNotificationManager.h"

#define LOCTEXT_NAMESPACE "SDiversionSettings"

TSharedRef<SWidget> SDiversionSettings::DiversionNotInstalledWidget()
{
	const FText DiversionNotInstalled = LOCTEXT("DiversionNotInstalled", "Diversion is not installed. Please install Diversion and restart Unreal Engine.");
	const FText DiversionQuickstart = LOCTEXT("DiversionQuickstart", "Diversion Quickstart Guide");
	const FText DiversionQuickstart_Tooltip = LOCTEXT("DiversionQuickstart_Tooltip", "Open Diversion quickstart guide in your browser");

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(DiversionNotInstalled)
		]
		+ SVerticalBox::Slot()
		.FillHeight(2.5f)
		.Padding(4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
						.Text(DiversionQuickstart)
						.ToolTipText(DiversionQuickstart_Tooltip)
						.OnClicked(this, &SDiversionSettings::OnClickedOpenDiversionQuickstart)
						.HAlign(HAlign_Center)
						.ContentPadding(6.0f)
				]
		];
}

TSharedRef<SWidget> SDiversionSettings::RepoWithSameNameExistsWidget()
{
	const FText RepoWithSameNameExists = LOCTEXT("DiversionRepoWithSameNameExists", "You already have a repository with the same name.\n The Unreal plugin cannot initialize.\n Please refer to the documentation for available options");
	const FText DiversionLearnMore = LOCTEXT("DiversionRepoWithSameNameExists_LearnMore", "Learn More");
	const FText DiversionLearnMore_Tooltip = LOCTEXT("DiversionRepoWithSameNameExists_LearnMore_Tooltip", "View the documentation to understand and resolve this issue.");

	return SNew(SVerticalBox)
		.Visibility_Lambda(
			[this] {return SettingCurrentScreen() == SettingsState::RepoWithSameNameAlreadyExists ? EVisibility::Visible : EVisibility::Collapsed; }
		)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(1.0f, 0.72f, 0.0f, 1.0f))
			.Padding(10.0f)
			[
				SNew(STextBlock)
					.Text(RepoWithSameNameExists)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(2.5f)
		.Padding(4.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
						.Text(DiversionLearnMore)
						.ToolTipText(DiversionLearnMore_Tooltip)
						.OnClicked(this, &SDiversionSettings::OnClickedOpenDiversionRepoWithSameNameExists)
						.HAlign(HAlign_Center)
						.ContentPadding(6.0f)
				]
		];
}

TSharedRef<SWidget> SDiversionSettings::DiversionRunAgentWidget()
{
	const FText DiversionRunAgent = LOCTEXT("DiversionRunAgent", "Diversion agent is not running. Please start the agent to use this plugin.");
	const FText DiversionRunAgentButton = LOCTEXT("DiversionRunAgentButton", "Start Diversion Agent");

	return SNew(SVerticalBox)
		.Visibility_Lambda(
			[this] {return SettingCurrentScreen() == SettingsState::StartAgent ? EVisibility::Visible : EVisibility::Collapsed; }
		)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(DiversionRunAgent)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
				.Text(DiversionRunAgentButton)
				.OnClicked(this, &SDiversionSettings::OnClickedStartDiversionAgent)
				.HAlign(HAlign_Center)
				.ContentPadding(6.0f)
		];
}

TSharedRef<SWidget> SDiversionSettings::ShowVersionWidget()
{
	const FText DiversionVersions = LOCTEXT("DiversionVersions", "Diversion version");
	const FText DiversionVersions_Tooltip = LOCTEXT("DiversionVersions_Tooltip", "Diversion and Plugin versions");

	return SNew(SHorizontalBox)
		.Visibility_Lambda(
			[this] {return SettingCurrentScreen() != SettingsState::StartAgent ? EVisibility::Visible : EVisibility::Collapsed; }
		)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
				.Text(DiversionVersions)
				.ToolTipText(DiversionVersions_Tooltip)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.80f)
		[
			SNew(STextBlock)
				.Text(this, &SDiversionSettings::GetVersions)
				.ToolTipText(DiversionVersions_Tooltip)
		];
}

TSharedRef<SWidget> SDiversionSettings::DiversionExistingRepoWidget(const FText& RepositoryRootLabel, const FText& RepositoryRootLabel_Tooltip)
{
	return SNew(SVerticalBox)
		.Visibility_Lambda(
				[this] {return SettingCurrentScreen() == SettingsState::RepoInitialized ? EVisibility::Visible : EVisibility::Collapsed; }
			)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
						.Text(RepositoryRootLabel)
						.ToolTipText(RepositoryRootLabel_Tooltip)
				]
			+ SHorizontalBox::Slot()
				.FillWidth(1.85f)
				[
					RootOfLocalDirectoryTextbox(RepositoryRootLabel_Tooltip)
				]
		];
}

TSharedRef<SWidget> SDiversionSettings::RootOfLocalDirectoryTextbox(const FText& RepositoryRootLabel_Tooltip)
{
	return SNew(SMultiLineEditableTextBox)
		.Text(this, &SDiversionSettings::GetPathToWorkspaceRoot)
		.IsReadOnly(true)
		.ToolTipText(RepositoryRootLabel_Tooltip)
		.HScrollBar(
			SNew(SScrollBar)
			.Orientation(Orient_Horizontal)
			.Thickness(FVector2D(1.0f, 1.0f))
		);
}

TSharedRef<SWidget> SDiversionSettings::DiversionInitializeRepoWidget(const FText& RepositoryRootLabel, const FText& RepositoryRootLabel_Tooltip)
{
	return SNew(SVerticalBox)
		.Visibility_Lambda(
			[this] {return SettingCurrentScreen() == SettingsState::RepoCreation ? EVisibility::Visible : EVisibility::Collapsed; }
		)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)[
			SNew(SSeparator)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock)
					.Text(RepositoryRootLabel)
					.ToolTipText(RepositoryRootLabel_Tooltip)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.85f)
			[
				SNew(SDirectoryPicker)
					.OnDirectoryChanged(this, &SDiversionSettings::OnProjectPathPicked)
					.Directory(RepoPath)
					.ToolTipText(RepositoryRootLabel_Tooltip)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)[
			AddInitializeButton()
		];
}

TSharedRef<SWidget> SDiversionSettings::AddInitializeButton()
{
	const FText DiversionInitRepository = LOCTEXT("DiversionInitRepository", "Initialize project with Diversion");
	const FText DiversionInitRepository_Tooltip = LOCTEXT("DiversionInitRepository_Tooltip", "Initialize current project as a new Diversion repository");

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SButton)
				.Text(DiversionInitRepository)
				.ToolTipText(DiversionInitRepository_Tooltip)
				.OnClicked(this, &SDiversionSettings::OnClickedInitializeDiversionRepository)
				.HAlign(HAlign_Center)
				.ContentPadding(6.0f)
		];
}

SDiversionSettings::SettingsState SDiversionSettings::SettingCurrentScreen()
{
	// Avoid any computational heavy/API Calls here!! this is called
	if (!SourceControlProvider->IsAgentAlive(EConcurrency::Asynchronous, false))
	{
		return SettingsState::StartAgent;
	}
	if (!SourceControlProvider->IsWorkspaceExistsInPath(RepoName, RepoPath, EConcurrency::Asynchronous, false))
	{
		// Check this only if the repo is not initialized
		if (SourceControlProvider->IsRepoWithSameNameExists(RepoName, EConcurrency::Asynchronous, false))
		{
			return SettingsState::RepoWithSameNameAlreadyExists;
		}
		return SettingsState::RepoCreation;
	}
	return SettingsState::RepoInitialized;
}

TSharedRef<SWidget> SDiversionSettings::MakeSettingsPanel()
{
	if(!DiversionUtils::IsDiversionInstalled())
	{
		// This is the only screen that won't dynamically change based on the status.
		// It's expected that the user will restart UE after installing Diversion.
		return DiversionNotInstalledWidget();
	}

	TSharedRef<SVerticalBox> MainSettingsBox =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f)
		.VAlign(VAlign_Center)
		[
			ShowVersionWidget()
		];

	auto RepoWithSameNameScreen = RepoWithSameNameExistsWidget();
	MainSettingsBox->AddSlot()[RepoWithSameNameScreen];

	auto RunAgentScreen = DiversionRunAgentWidget();
	MainSettingsBox->AddSlot()[RunAgentScreen];

	const FText RepositoryRootLabel = LOCTEXT("RepositoryRootLabel", "Root of the repository");
	const FText RepositoryRootLabel_Tooltip = LOCTEXT("RepositoryRootLabel_Tooltip", "Path to the root of the Diversion repository");

	auto ExistingRepoScreen = DiversionExistingRepoWidget(RepositoryRootLabel, RepositoryRootLabel_Tooltip);
	MainSettingsBox->AddSlot()[ExistingRepoScreen];

	auto InitializeRepoScreen = DiversionInitializeRepoWidget(RepositoryRootLabel, RepositoryRootLabel_Tooltip);
	MainSettingsBox->AddSlot()[InitializeRepoScreen];

	return MainSettingsBox;
}

void SDiversionSettings::Construct(const FArguments& InArgs)
{
	UE_LOG(LogTemp, Log, TEXT("SDiversionSettings::Construct"));


	// Set default repo path and name
	RepoPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	OnProjectPathPicked(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

	// This function assume that the Diversion Module was already loaded
	SourceControlProvider = &FDiversionModule::Get().GetProvider();
	// Needed for state data to decide which connect screen to show the user
	SourceControlProvider->IsWorkspaceExistsInPath(RepoName, RepoPath, EConcurrency::Synchronous, true);
	SourceControlProvider->IsRepoWithSameNameExists(RepoName, EConcurrency::Synchronous, true);

	ChildSlot [ MakeSettingsPanel() ];
}

SDiversionSettings::~SDiversionSettings()
{
	RemoveInProgressNotification();
}

FText SDiversionSettings::GetVersions() const
{
	return FText::Format(LOCTEXT("VersionsLabel", "{0} (plugin v{1})"),
		FText::AsCultureInvariant(SourceControlProvider->GetDiversionVersion().ToString()),
		FText::AsCultureInvariant(SourceControlProvider->GetPluginVersion()));
}

FText SDiversionSettings::GetPathToWorkspaceRoot() const
{
	return FText::FromString(SourceControlProvider->GetPathToWorkspaceRoot());
}

FReply SDiversionSettings::OnClickedInitializeDiversionRepository()
{
	auto SendAnalyticsOperation = ISourceControlOperation::Create<FSendAnalytics>();
	SendAnalyticsOperation->SetEventName(FText::FromString("UE Plugin Init"));
	SourceControlProvider->Execute(SendAnalyticsOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);

	const FString PathToProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	// Make sure the provided RepoPath from options contains(is parent of) the project directory
	if (!PathToProjectDir.StartsWith(RepoPath))
	{
		const FText NotificationText = FText::Format(LOCTEXT("InvalidRepoPath", "Provided repository path is not a parent of the current project: {0}. "
			"Please choose a dir that is a parent of the project dir (or the project dir itself)"), FText::FromString(RepoPath));
		DiversionUtils::ShowErrorNotification(NotificationText);
		return FReply::Handled();
	}

	auto InitRepoOperation = ISourceControlOperation::Create<FInitRepo>();
	InitRepoOperation->SetRepoName(RepoName);
	InitRepoOperation->SetRepoPath(RepoPath);

	SourceControlProvider->Execute(InitRepoOperation, nullptr, TArray<FString>(), EConcurrency::Synchronous,
		FSourceControlOperationComplete::CreateSP(this, &SDiversionSettings::OnSourceControlOperationComplete));

	SourceControlProvider->GetDiversionVersion(EConcurrency::Synchronous, true);
	SourceControlProvider->GetWsInfo(EConcurrency::Synchronous, true);

	bool bInitializedSuccessfully = SourceControlProvider->IsWorkspaceExistsInPath(RepoName, RepoPath, EConcurrency::Synchronous, true);
	// Check the new repository status to enable connection (branch, user e-mail)
	if(!bInitializedSuccessfully)
	{
		FString DiversionDirectoryPath = RepoPath / TEXT(".diversion");
		if (FPaths::DirectoryExists(DiversionDirectoryPath))
		{
			auto SendFailedInitOperation = ISourceControlOperation::Create<FSendAnalytics>();
			SendFailedInitOperation->SetEventName(FText::FromString(".diversion folder - UE Plugin Init Failure "));
			SourceControlProvider->Execute(SendFailedInitOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);


			FNotificationButtonInfo LearnMoreButton(LOCTEXT("DiversionRepoInitFailed_LearnMore", "Learn More"),
				LOCTEXT("DiversionRepoInitFailed_LearnMore_Tooltip", "View the documentation to understand and resolve this issue."),
				FSimpleDelegate::CreateLambda([]()
					{
						const FString FailedInitFolderExistLink = "https://docs.diversion.dev/unreal/common-plugin-issues/existing-config-in-path?utm_source=ue-plugin&utm_medium=plugin";
						if (FPlatformProcess::CanLaunchURL(*FailedInitFolderExistLink))
						{
							FPlatformProcess::LaunchURL(*FailedInitFolderExistLink, nullptr, nullptr);
						}
					}),
				SNotificationItem::CS_Fail);

			auto notification = FDiversionNotification(
				LOCTEXT("DiversionConfigExists", "Plugin initialization failed due to an existing '.diversion' config in this project path"),
				TArray<FNotificationButtonInfo>({ LearnMoreButton }),
				SNotificationItem::CS_Fail);
			SourceControlProvider->ShowNotification(notification);

			return FReply::Handled();
		}

		FNotificationButtonInfo LearnMoreButton(LOCTEXT("DiversionRepoInitFailed_GetSupport", "Get Help"),
			LOCTEXT("DiversionRepoInitFailed_GetSupport_Tooltip", "Access support and get assistance for this issue"),
			FSimpleDelegate::CreateLambda([]()
				{
					const FString GetHelpLink = "https://www.diversion.dev/support?utm_source=ue-plugin&utm_medium=plugin";
					if (FPlatformProcess::CanLaunchURL(*GetHelpLink))
					{
						FPlatformProcess::LaunchURL(*GetHelpLink, nullptr, nullptr);
					}
				}),
			SNotificationItem::CS_Fail);

		auto notification = FDiversionNotification(
			LOCTEXT("Initialization_Failure", "Repository initialization failed"),
			TArray<FNotificationButtonInfo>({ LearnMoreButton }),
			SNotificationItem::CS_Fail);
		SourceControlProvider->ShowNotification(notification);

		// Add analytics for failed repo initialization
		auto SendFailedInitOperation = ISourceControlOperation::Create<FSendAnalytics>();
		SendFailedInitOperation->SetEventName(FText::FromString("UE Plugin Init Failed"));
		SourceControlProvider->Execute(SendFailedInitOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);
	}
	else
	{
		DisplayInfoNotification(LOCTEXT("InitialSyncStarted", "Diversion is now syncing your files in the background. This might take a while. Source control operations will be available once the initial sync has finished."));
	}

	// Retrieve agent status to update sync status as well
	SourceControlProvider->IsAgentAlive(EConcurrency::Synchronous, true);

	// Update the provider
	return FReply::Handled();
}

FString SDiversionSettings::NormalizeRepoPath() const
{
#if PLATFORM_WINDOWS
	FString FullRepoPath = FPaths::ConvertRelativePathToFull(RepoPath).Replace(TEXT("/"), TEXT("\\"));
#elif PLATFORM_LINUX || PLATFORM_MAC
	FString FullRepoPath = FPaths::ConvertRelativePathToFull(RepoPath);
#endif
	if (FullRepoPath.EndsWith(TEXT("\\")) || FullRepoPath.EndsWith(TEXT("/")))
	{
		FullRepoPath = FullRepoPath.LeftChop(1);
	}
	return FullRepoPath;
}

FReply SDiversionSettings::OnClickedOpenDiversionQuickstart() const
{
	FPlatformProcess::LaunchURL(TEXT("https://docs.diversion.dev/quickstart?utm_source=ue-plugin&utm_medium=plugin"), nullptr, nullptr);
	return FReply::Handled();
}

FReply SDiversionSettings::OnClickedOpenDiversionRepoWithSameNameExists() const
{
	FPlatformProcess::LaunchURL(TEXT("https://docs.diversion.dev/unreal/common-plugin-issues/repo-already-exists?utm_source=ue-plugin&utm_medium=plugin"), nullptr, nullptr);
	return FReply::Handled();
}

FReply SDiversionSettings::OnClickedStartDiversionAgent() const
{
	DiversionUtils::StartDiversionAgent(FDiversionModule::Get().AccessSettings().GetBinaryPath());
	// Update the provider
	if (!SourceControlProvider->IsAgentAlive(EConcurrency::Synchronous, true))
	{
		FText Message = LOCTEXT("StartAgentFailed", "Failed starting Diversion Sync Agent");
		DiversionUtils::ShowErrorNotification(Message);
	}

	SourceControlProvider->GetWsInfo(EConcurrency::Synchronous, true);
	SourceControlProvider->IsWorkspaceExistsInPath(RepoName, RepoPath, EConcurrency::Synchronous, true);

	return FReply::Handled();
}

/// Delegate called when a version control operation has completed: launch the next one and manage notifications
void SDiversionSettings::OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	RemoveInProgressNotification();

	// Report result with a notification
	if (InResult == ECommandResult::Succeeded)
	{
		DisplaySuccessNotification(InOperation);
	}
}

// Display an ongoing notification during the whole operation
void SDiversionSettings::DisplayInProgressNotification(const FSourceControlOperationRef& InOperation)
{
	FNotificationInfo Info(InOperation->GetInProgressString());
	Info.bFireAndForget = false;
	Info.ExpireDuration = 0.0f;
	Info.FadeOutDuration = 1.0f;
	OperationInProgressNotification = FSlateNotificationManager::Get().AddNotification(Info);
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

// Remove the ongoing notification at the end of the operation
void SDiversionSettings::RemoveInProgressNotification()
{
	if (OperationInProgressNotification.IsValid())
	{
		OperationInProgressNotification.Pin()->ExpireAndFadeout();
		OperationInProgressNotification.Reset();
	}
}

// Display a temporary success notification at the end of the operation
void SDiversionSettings::DisplayInfoNotification(const FText& NotificationText)
{
	FNotificationInfo Info(NotificationText);
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush(TEXT("NotificationList.DefaultMessage"));
	Info.ExpireDuration = 10.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

// Display a temporary success notification at the end of the operation
void SDiversionSettings::DisplaySuccessNotification(const FSourceControlOperationRef& InOperation)
{
	const FText NotificationText = FText::Format(LOCTEXT("InitialCommit_Success", "{0} operation completed successfully"), FText::FromName(InOperation->GetName()));
	FNotificationInfo Info(NotificationText);
	Info.bUseSuccessFailIcons = true;
	Info.Image = FAppStyle::GetBrush(TEXT("NotificationList.SuccessImage"));
	FSlateNotificationManager::Get().AddNotification(Info);
}

#undef LOCTEXT_NAMESPACE
