// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "ISourceControlProvider.h"
#include "DiversionTimedDelegate.h"

class FDiversionProvider;

class SNotificationItem;
namespace ETextCommit { enum Type : int; }

enum class ECheckBoxState : uint8;

class SDiversionSettings : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SDiversionSettings) {}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDiversionSettings() override;

private:
	FString GetRepoPathString() const { return RepoPath; }
	FString RepoPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	FString GetRepoNameString() const { return RepoName; }
	FString RepoName = TEXT("");

	FText GetVersions() const;
	/** Delegate to get repository root and email from provider */
	FText GetPathToWorkspaceRoot() const;

	/** Delegate called when a version control operation has completed */
	void OnSourceControlOperationComplete(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);

	/** Asynchronous operation progress notifications */
	TWeakPtr<SNotificationItem> OperationInProgressNotification;

	void DisplayInProgressNotification(const FSourceControlOperationRef& InOperation);
	void RemoveInProgressNotification();
	void DisplayInfoNotification(const FText& NotificationText);
	void DisplaySuccessNotification(const FSourceControlOperationRef& InOperation);


private:
	// Widget functions
	TSharedRef<SWidget> MakeSettingsPanel();
	TSharedRef<SWidget> DiversionNotInstalledWidget();
	TSharedRef<SWidget> RepoWithSameNameExistsWidget();
	TSharedRef<SWidget> DiversionRunAgentWidget();
	TSharedRef<SWidget> ShowVersionWidget();
	TSharedRef<SWidget> DiversionExistingRepoWidget(const FText& RepositoryRootLabel, const FText& RepositoryRootLabel_Tooltip);
	TSharedRef<SWidget> RootOfLocalDirectoryTextbox(const FText& RepositoryRootLabel_Tooltip);
	TSharedRef<SWidget> DiversionInitializeRepoWidget(const FText& RepositoryRootLabel, const FText& RepositoryRootLabel_Tooltip);
	TSharedRef<SWidget> AddInitializeButton();

	enum class SettingsState
	{
		StartAgent,
		RepoWithSameNameAlreadyExists,
		RepoCreation,
		RepoInitialized
	};

	SettingsState SettingCurrentScreen();

private:
	// Click event handlers
	FReply OnClickedOpenDiversionQuickstart() const;
	FReply OnClickedStartDiversionAgent() const;
	FReply OnClickedInitializeDiversionRepository();
	FReply OnClickedOpenDiversionRepoWithSameNameExists() const;
	void OnProjectPathPicked(const FString& InRepoPath)
	{
		RepoPath = InRepoPath;
		NormalizeRepoPath();
		RepoName = FPaths::GetPathLeaf(RepoPath);
	}


	// We don't use the Utils absolute path since the repo isn't considered initialized yet
	FString NormalizeRepoPath() const;

private:
	FDiversionProvider* SourceControlProvider = nullptr;
};
