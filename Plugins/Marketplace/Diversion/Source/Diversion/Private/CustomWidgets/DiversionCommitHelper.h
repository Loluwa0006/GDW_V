// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"

class FDiversionProvider;

/**
 * Helper class for Diversion's custom commit flow (UE 5.6+).
 *
 * This replaces UE's ChoosePackagesToCheckIn flow to avoid duplicate files
 * that occur when using custom projects with GetCachedStateByPredicate.
 * In 5.6+ the Submit Content button was hidden, so we provide our own.
 *
 * We reuse as much of Epic's code as possible:
 * - FSourceControlWindows::ChoosePackagesToCheckInCompleted (save prompts)
 * - FSourceControlWindows::PromptForCheckin (commit dialog)
 *
 * Only the file gathering logic is customized to pull directly from our cache.
 */
class FDiversionCommitHelper
{
public:
	/**
	 * Initiates the commit flow - triggers status update and shows commit dialog.
	 * This is the entry point that replaces FSourceControlWindows::ChoosePackagesToCheckIn().
	 */
	static void ExecuteCommit();

	/**
	 * Returns true if the commit action can be executed.
	 * Mirrors FSourceControlWindows::CanChoosePackagesToCheckIn().
	 */
	static bool CanExecuteCommit();

private:
	/**
	 * Callback when commit operation is cancelled by user.
	 */
	static void OnCommitCancelled(FSourceControlOperationRef InOperation);

	/**
	 * Callback after status update completes.
	 * Gathers files from Diversion's cache and triggers the commit dialog.
	 */
	static void OnUpdateStatusComplete(
		const FSourceControlOperationRef& InOperation,
		ECommandResult::Type InResult);

	/**
	 * Gathers all committable files from Diversion's state cache.
	 * This replaces the FindAllSubmittable* functions that cause duplicates.
	 *
	 * @param OutPackageNames     Package names (assets, maps)
	 * @param OutLoadedPackages   Currently loaded packages (for save prompts)
	 * @param OutConfigFiles      Config files and other non-package files
	 */
	static void GatherCommittableFiles(
		TArray<FString>& OutPackageNames,
		TArray<UPackage*>& OutLoadedPackages,
		TArray<FString>& OutConfigFiles);

	/**
	 * Checks if a file state represents a committable file.
	 */
	static bool IsCommittable(const FSourceControlStateRef& State);

	/** Notification widget shown during status update */
	static TWeakPtr<class SNotificationItem> CommitNotification;
};
