// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionProvider.h"
#include "DiversionState.h"
#include "DiversionEngineTextOverrides.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/QueuedThreadPool.h"
#include "DiversionCommand.h"
#include "DiversionConfig.h"
#include "ISourceControlModule.h"
#include "SourceControlHelpers.h"
#include "DiversionModule.h"
#include "DiversionUtils.h"
#include "DiversionFilePattern.h"
#include "DiversionAPIAccess.h"
#include "SDiversionSettings.h"
#include "SourceControlOperations.h"
#include "Logging/MessageLog.h"
#include "ScopedSourceControlProgress.h"
#include "Interfaces/IPluginManager.h"
#include "DiversionOperations.h"
#include "DiversionConstants.h"
#include "DiversionCommonUtils.h"
#include "CustomWidgets/DiversionPotentialClashUI.h"
#include "CustomWidgets/DiversionHardLockUI.h"
#include "CustomWidgets/DiversionAssetEditorLockIndicator.h"
#include "CustomWidgets/DiversionLockContextMenu.h"
#include "CustomWidgets/DiversionStatusPanel.h"
#include "CustomWidgets/DiversionCommitHelper.h"
#include "CustomWidgets/ConfirmationDialog.h"
#include "CustomWidgets/SDiversionLockedFilesDialog.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformProcess.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ContentBrowserModule.h"
#include "PackageTools.h"
#include "DirectoryWatcherModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectSaveContext.h"
#include "Misc/EngineVersionComparison.h"
#include "Framework/Application/SlateApplication.h"
#include "SourceControlOperations.h"
#include "ToolMenuEntry.h"
#include "SourceControlWindows.h"
#include "RevisionControlStyle/RevisionControlStyle.h"

// For status bar registration
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"


#include "Editor.h"
#include "DataValidationChangelist.h"
#include "EditorValidatorSubsystem.h"
#include <Algo/AnyOf.h>


#define LOCTEXT_NAMESPACE "Diversion"

namespace
{
	/**
	 * Writes the settings-panel mirror flag without ever creating the config object.
	 * Editor shutdown destroys default objects before modules unload, so Close() runs after
	 * the object may be gone - GetMutableDefault would create one mid-shutdown (crash).
	 * GetDefaultObject(false) returns null instead of creating; then there is nothing to clear.
	 */
	void SetHardLocksAvailableFlag(bool bAvailable)
	{
		if (!UObjectInitialized())
		{
			return;
		}
		if (UObject* ConfigCDO = UDiversionConfig::StaticClass()->GetDefaultObject(false))
		{
			CastChecked<UDiversionConfig>(ConfigCDO)->bHardLocksAvailable = bAvailable;
		}
	}
}


static FName ProviderName("Diversion");



void FDiversionProvider::Init(bool bForceConnection)
{
	bDiversionStopped = false;
	// Init() is called multiple times at startup: do not check dv each time
	if (!bDiversionAvailable)
	{
		DiversionEngineTextOverrides::Enable();
		// Populate WsInfo before the agent health check so its AgentInSync step
		// (which sets SyncStatus) runs on first startup. Short-lived commandlets
		// like GatherText check IsAvailable() before queuing operations and would
		// otherwise see SyncStatus unset before Tick has a chance to refresh.
		GetWsInfo(EConcurrency::Synchronous, true);
		GetDiversionVersion(EConcurrency::Synchronous, true);
		CheckDiversionAvailability();

		// Resolve hard-locks eligibility (async). Fail-closed: lock checks stay skipped until
		// it resolves; the potential-clashes poll retries and refreshes it from here on. Not
		// needed in commandlet/unattended runs.
		if (!IsRunningCommandlet() && !FApp::IsUnattended())
		{
			RequestHardLocksEligibility();
		}

		// Handle "Resolve" on package save and "Finalize Merge"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
		UPackage::PackageSavedWithContextEvent.AddRaw(this, &FDiversionProvider::OnPackageSaveWithContext);
#else
		UPackage::PackageSavedEvent.AddRaw(this, &FDiversionProvider::OnPackageSave);
#endif
		UPackage::PreSavePackageWithContextEvent.AddRaw(this, &FDiversionProvider::OnPrePackageSave);

		// Save-time lock enforcement: the gate chains the engine's save gate and modal-dialog
		// routing. Restored symmetrically in Close().
		SaveDecisionGate.Bind();

		// Skip unnecessary Revert (RunReset) when UE deletes packages (Revert-before-Delete is a P4 pattern)
		FEditorDelegates::OnPackageDeleted.AddRaw(this, &FDiversionProvider::OnPackageDeletedHandler);

		auto& Module = FDiversionModule::Get();
		if (!DiversionUtils::DiversionValidityCheck(&Module != nullptr,
				"Diversion Module is not valid!", GetWsInfo().AccountID))
		{
			return;
		}
		// Set the original account ID with which the plugin was loaded
		Module.SetOriginalAccountID(GetWsInfo().AccountID);

		auto SendAnalyticsOperation = ISourceControlOperation::Create<FSendAnalytics>();
		SendAnalyticsOperation->SetEventName(FText::FromString("UE Plugin Load"));
		SendAnalyticsOperation->SetEventProperties({{"ue_softlock_enabled", IsDiversionSoftLockEnabled() ? "true" : "false"}});
		Execute(SendAnalyticsOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);

		FAgentHealthCheck::SetProgressString();
		FGetWsInfo::SetProgressString();

		// Set the project path delegate
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 4
		ISourceControlModule::Get().RegisterSourceControlProjectDirDelegate(
			FSourceControlProjectDirDelegate::CreateLambda([this]() {
				return WsInfo.Get().GetPath();
			}));
#else
		ISourceControlModule::Get().RegisterCustomProjectsDelegate(
			FSourceControlCustomProjectsDelegate::CreateLambda([this]() {
				FSourceControlProjectInfo ProjectInfo;
				ProjectInfo.ProjectDirectory = WsInfo.Get().GetPath();
				ProjectInfo.ContentDirectories = { FPaths::ProjectContentDir() };
				return TArray<FSourceControlProjectInfo>{ProjectInfo};
			}));
#endif
		RegisterPreSubmitValidationDelegates();

		// Start background checks. These poll for state that is only consumed by
		// the editor UI (status bar, content browser overlays, scene outliner). In
		// commandlet/unattended runs nobody reads the polled state, so skip the
		// timers entirely to avoid wasted agent traffic for the commandlet's lifetime.
		if (!IsRunningCommandlet() && !FApp::IsUnattended())
		{
			auto BackgroundStatusDelegate = DiversionTimerDelegate::CreateLambda([this]() {
				if (!bDiversionAvailable)
				{
					return;
				}

				if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"Call Update Status called outside of main thread",
						FDiversionModule::Get().GetOriginalAccountID()))
				{
					return;
				}

				const auto Operation = ISourceControlOperation::Create<FUpdateStatus>();
				Execute(Operation, nullptr, { WsInfo.Get().GetPath() }, EConcurrency::Asynchronous);
			});

			BackgroundStatus = MakeUnique<FTimedDelegateWrapper>(
				BackgroundStatusDelegate, SECONDS_TO_POLL_STATUS);
			BackgroundStatus->Start();

			auto BackgroundPotentialClashesDelegate = DiversionTimerDelegate::CreateLambda([this]() {
				if (!bDiversionAvailable)
				{
					return;
				}

				if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
				"Get Potential Clashes called outside of main thread",
						FDiversionModule::Get().GetOriginalAccountID()))
				{
					return;
				}

				// Heal a failed startup resolution, and pick up tier / lockable-pattern changes
				if (IsHardLocksEligibilityRefreshDue())
				{
					RequestHardLocksEligibility();
				}

				auto operation = ISourceControlOperation::Create<FGetPotentialClashes>();
				// Refresh hard-lock states (for icons/tooltips) in the same poll pass - gated
				// by repo tier eligibility so ineligible repos issue no lock calls
				operation->SetAlsoFetchLocks(ShouldCheckHardLocks());
				Execute(operation, nullptr, { WsInfo.Get().GetPath() }, EConcurrency::Asynchronous);
			});

			BackgroundPotentialClashes = MakeUnique<FTimedDelegateWrapper>(
				BackgroundPotentialClashesDelegate, SECONDS_TO_POLL_POTENTIAL_CLASHES);
			BackgroundPotentialClashes->Start();


			auto BackgroundConflictedFilesDelegate = DiversionTimerDelegate::CreateLambda([this]() {
				if (!bDiversionAvailable)
				{
					return;
				}

				if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
				"Get Conflicts called outside of main thread",
						FDiversionModule::Get().GetOriginalAccountID()))
				{
					return;
				}

				auto operation = ISourceControlOperation::Create<FGetConflictedFiles>();
				Execute(operation, nullptr, { }, EConcurrency::Asynchronous);
			});

			BackgroundConflictedFiles = MakeUnique<FTimedDelegateWrapper>(
				BackgroundConflictedFilesDelegate, SECONDS_TO_POLL_CONFLICTED_FILES);
			BackgroundConflictedFiles->Start();
			BackgroundConflictedFiles->TriggerInstantCallAndReset();
		}

		if (FSlateApplication::IsInitialized())
		{
			FocusChangedHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddRaw(
				this, &FDiversionProvider::OnApplicationFocusChanged);
		}

		// Add the Diversion potential clash and hard-lock indicators to the asset view
		SPotentialClashIndicator::CacheIndicatorBrush();
		SHardLockIndicator::CacheIndicatorBrush();
		if (FContentBrowserModule* ContentBrowserModule = FModuleManager::Get().GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
		{
			if (!PotentialClashUIIconDelegateHandle.IsValid())
			{
				PotentialClashUIIconDelegateHandle = ContentBrowserModule->AddAssetViewExtraStateGenerator(FAssetViewExtraStateGenerator(
					FOnGenerateAssetViewExtraStateIndicators::CreateRaw(this, &FDiversionProvider::OnGenerateAssetViewPotentialClashIcon),
					FOnGenerateAssetViewExtraStateIndicators::CreateRaw(this, &FDiversionProvider::OnGenerateAssetViewPotentialClashTooltip)
				));
			}
			if (!HardLockUIIconDelegateHandle.IsValid())
			{
				HardLockUIIconDelegateHandle = ContentBrowserModule->AddAssetViewExtraStateGenerator(FAssetViewExtraStateGenerator(
					FOnGenerateAssetViewExtraStateIndicators::CreateRaw(this, &FDiversionProvider::OnGenerateAssetViewHardLockIcon),
					FOnGenerateAssetViewExtraStateIndicators::CreateRaw(this, &FDiversionProvider::OnGenerateAssetViewHardLockTooltip)
				));
			}
		}
		// Add the hard-lock actions to the Content Browser's Revision Control submenu
		// (entries self-gate on repo lock eligibility; no engine version guard needed)
		FDiversionLockContextMenu::Register();

		// Persistent hard-lock banner on every open asset editor window. Reads the same cache
		// the poll above fills, so no asset editor window costs an extra request.
		FDiversionAssetEditorLockIndicator::Register();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// Initialize custom UI commands for submit functionality
		InitializeUICommands();
#endif

		// Register status bar widget
		RegisterStatusBarWidget();
	}

	// TODO bForceConnection: used by commandlets (typically for CI/CD scripts)
}

FString FDiversionProvider::GetBranchId()
{
	return WsInfo.Get().BranchID;
}

bool FDiversionProvider::CheckDiversionAvailability()
{
	if(!DiversionUtils::IsDiversionInstalled())
	{
		DiversionUtils::PrintSupportNeededLogLine("Diversion agent is not installed. Please install the desktop app as documented at https://docs.diversion.dev/quickstart?utm_source=ue-plugin&utm_medium=plugin or contact support if the issue persists.", true);

		return false;
	}

	if (const TSharedPtr<IPlugin> Plugin = FDiversionModule::GetPlugin())
	{
		PluginVersion = Plugin->GetDescriptor().VersionName;
		UE_LOG(LogSourceControl, Log, TEXT("Diversion plugin %s"), *PluginVersion);
	}
	else
	{
		// TODO: Can we get more info here?
		DiversionUtils::PrintSupportNeededLogLine("Failed loading Diversion plugin. Try disabling and re-enabling the plugin (Edit -> Plugins -> Diversion), or reinstalling on the marketplace. Please contact support if the issue persists.");

		return false;
	}

	if(!IsAgentAlive())
	{
		DiversionUtils::PrintSupportNeededLogLine("Diversion agent is not running or properly configured. Check Diversion tray icon or open Diversion Desktop to verify agent is up.", true);

		return false;
	}

	bDiversionAvailable = GetDiversionVersion().IsValid();
	return bDiversionAvailable;
}

void FDiversionProvider::Close()
{
	if (FSlateApplication::IsInitialized() && FocusChangedHandle.IsValid())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(FocusChangedHandle);
	}

	UnregisterAndRestorePreSubmitValidationDelegates();

	// Restore the engine's original save gate and modal-dialog routing, and drop the gate's
	// per-session state (see Init)
	SaveDecisionGate.Unbind();

	// clear the cache
	StateCache.Empty();
	// Drop the lock-state tracking map alongside the cache: it holds shared refs into the
	// cache, and keeping them across a provider close would make later lock updates write
	// onto orphaned states while GetState serves fresh ones (locks have no periodic
	// full-repo refresh to self-heal, unlike the clashes map).
	LockedStates.Empty();
	++LockedStatesRevision;
	bFetchedFullLockSet = false;
	// Re-resolved on the next Init
	bHardLocksEligible.Reset();
	LockablePatterns.Reset();
	HardLocksEligibilityResolvedAt = 0.0;
	bHardLocksEligibilityInFlight = false;
	SetHardLocksAvailableFlag(false);

	bDiversionAvailable = false;
	UserEmail.Empty();
	bDiversionStopped = true;

	// Wait for all commands to finish but exit if it takes too long
	// This is to avoid a deadlock when the engine is shutting down

	auto WaitForCommandsDelegate = WaitForConditionPredicate::CreateLambda([this]() {
		Tick();
		return CommandQueue.Num() == 0;
	});
	bool Success = DiversionUtils::WaitForCondition(WaitForCommandsDelegate, 30.0);
	if(!Success)
	{
		UE_LOG(LogSourceControl, Error, TEXT("Failed to fully close Diversion provider, there are still pending commands"));
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	UPackage::PackageSavedWithContextEvent.RemoveAll(this);
#else
	UPackage::PackageSavedEvent.RemoveAll(this);
#endif
	UPackage::PreSavePackageWithContextEvent.RemoveAll(this);
	FEditorDelegates::OnPackageDeleted.RemoveAll(this);

	// Reset localization strings
	DiversionEngineTextOverrides::Disable();


	if (FContentBrowserModule* ContentBrowserModule = FModuleManager::Get().GetModulePtr<FContentBrowserModule>(TEXT("ContentBrowser")))
	{
		if (PotentialClashUIIconDelegateHandle.IsValid())
		{
			ContentBrowserModule->RemoveAssetViewExtraStateGenerator(PotentialClashUIIconDelegateHandle);
		}
		if (HardLockUIIconDelegateHandle.IsValid())
		{
			ContentBrowserModule->RemoveAssetViewExtraStateGenerator(HardLockUIIconDelegateHandle);
		}
	}
	PotentialClashUIIconDelegateHandle.Reset();
	HardLockUIIconDelegateHandle.Reset();

	FDiversionLockContextMenu::Unregister();
	FDiversionAssetEditorLockIndicator::Unregister();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	// Shutdown custom UI commands
	ShutdownUICommands();
#endif

	// Unregister status bar widget
	UnregisterStatusBarWidget();
}

FText FDiversionProvider::GetStatusText() const
{
	const FText AgentStatus = IsAgentAlive()
		? FText::Format(LOCTEXT("AgentStatus_Alive", "alive. {0}"), DiversionUtils::GetWsSyncStatusText(SyncStatus.Get()))
		: LOCTEXT("AgentStatus_NotRunning", "not running.");

	FFormatNamedArguments Args;
	Args.Add(TEXT("Version"), FText::AsCultureInvariant(GetDiversionVersion().ToString()));
	Args.Add(TEXT("PluginVersion"), FText::AsCultureInvariant(PluginVersion));
	Args.Add(TEXT("AgentAlive"), AgentStatus);
	Args.Add(TEXT("PathToWorkspaceRoot"), FText::FromString(WsInfo.Get().GetPath()));
	Args.Add(TEXT("WorkspaceName"), FText::FromString(GetWorkspaceName()));
	Args.Add(TEXT("RepositoryName"), FText::FromString(WsInfo.Get().RepoName));
	Args.Add(TEXT("BranchName"), FText::FromString(WsInfo.Get().BranchName));

	return FText::Format(LOCTEXT("ProviderStatusText","Version {Version} (plugin {PluginVersion})\nAgent {AgentAlive}\nWorkspace: {WorkspaceName}\nRepository: {RepositoryName}\nBranch: {BranchName}\n"), Args);
}

TMap<ISourceControlProvider::EStatus, FString> FDiversionProvider::GetStatus() const
{
	TMap<EStatus, FString> Result;
	Result.Add(EStatus::Enabled, IsEnabled() ? TEXT("Yes") : TEXT("No"));
	Result.Add(EStatus::Connected, (IsEnabled() && IsAvailable()) ? TEXT("Yes") : TEXT("No"));
	Result.Add(EStatus::WorkspacePath, WsInfo.Get().GetPath());
	Result.Add(EStatus::Workspace, GetWorkspaceName());
	Result.Add(EStatus::Repository, WsInfo.Get().RepoName);
	Result.Add(EStatus::Branch, WsInfo.Get().BranchName);
	return Result;
}

/** Quick check if version control is enabled */
bool FDiversionProvider::IsEnabled() const
{
	return IsRepoFound();
}

/** Diversion version for feature checking */

const FDiversionVersion& FDiversionProvider::GetDiversionVersion(EConcurrency::Type Concurrency, bool InForceUpdate)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
			"GetDiversionVersion called outside of main thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return DvVersion.Get();
	}

	return DvVersion.GetUpdate(DvVersion.Get(),
		FOnCacheUpdate::CreateLambda([this, &Concurrency]() {
			auto operation = ISourceControlOperation::Create<FAgentHealthCheck>();
			Execute(operation, nullptr, TArray<FString>(), Concurrency);
			}), InForceUpdate);
}

const FDiversionVersion& FDiversionProvider::GetDiversionVersion() const
{
	return DvVersion.Get();
}

WorkspaceInfo FDiversionProvider::GetWsInfo(EConcurrency::Type Concurrency, bool InForceUpdate)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"GetWsInfo called outside of main thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return WsInfo.Get();
	}

	return WsInfo.GetUpdate(
		WsInfo.Get(), FOnCacheUpdate::CreateLambda([this, &Concurrency]() {
			auto operation = ISourceControlOperation::Create<FGetWsInfo>();
			Execute(operation, nullptr, TArray<FString>(), Concurrency);
		}), InForceUpdate);
}

WorkspaceInfo FDiversionProvider::GetWsInfo() const
{
	return WsInfo.Get();
}

void FDiversionProvider::SetDvVersion(const FDiversionVersion& InVersion)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"SetDvVersion must be called on the main game thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return;
	}

	// Agents before 1.6.373 return the default account's access token, whichever account is asked for
	if (InVersion.IsValid() && InVersion.ToString() != DvVersion.Get().ToString() && !InVersion.IsGreaterOrEqualThan(1, 6, 373))
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Diversion agent %s is older than 1.6.373 and uses the default account's ")
			TEXT("access token for every account. Update Diversion to the latest version."), *InVersion.ToString());
	}

	DvVersion.Set(InVersion);
}

void FDiversionProvider::SetWorkspaceInfo(const WorkspaceInfo& InWsInfo)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"SetWorkspaceInfo must be called on the main game thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return;
	}

	WsInfo.Set(InWsInfo);
	bDiversionAvailable = InWsInfo.IsValid();
}

void FDiversionProvider::SetSyncStatus(const DiversionUtils::EDiversionWsSyncStatus& InSyncStatus)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"SetSyncStatus must be called on the main game thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return;
	}

	SyncStatus.Set(InSyncStatus);
}

DiversionUtils::EDiversionWsSyncStatus FDiversionProvider::GetSyncStatus() const
{
	return SyncStatus.Get();
}

bool FDiversionProvider::IsRepoFound() const
{
	FString RepoId = WsInfo.Get().RepoID;
	return (!RepoId.IsEmpty()) && (RepoId != "N/a");
}

/** Quick check if version control is available for use (useful for server-based providers) */
bool FDiversionProvider::IsAvailable() const
{
	return IsRepoFound() && IsAgentAlive() &&
		(
			SyncStatus.Get() == DiversionUtils::EDiversionWsSyncStatus::PathError  ||
			SyncStatus.Get() == DiversionUtils::EDiversionWsSyncStatus::InProgress ||
			SyncStatus.Get() == DiversionUtils::EDiversionWsSyncStatus::Completed
		);
}

const FName& FDiversionProvider::GetName(void) const
{
	return ProviderName;
}

ECommandResult::Type FDiversionProvider::GetState(const TArray<FString>& InFiles, TArray< TSharedRef<ISourceControlState, ESPMode::ThreadSafe> >& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	if (InStateCacheUsage == EStateCacheUsage::ForceUpdate)
	{
		Execute(ISourceControlOperation::Create<FUpdateStatus>(), AbsoluteFiles);
	}

	const FString WsPath = GetPathToWorkspaceRoot();

	for (const auto& AbsoluteFile : AbsoluteFiles)
	{
		TSharedRef<FDiversionState, ESPMode::ThreadSafe> State = GetStateInternal(*AbsoluteFile);

		// When ForceUpdate is requested but the state is still Unknown (e.g. the synchronous
		// Execute path deferred actual status queries to background polling), provide a
		// preliminary state based on disk presence within the workspace. The background
		// poller will later correct this with actual API data if needed.
		if (InStateCacheUsage == EStateCacheUsage::ForceUpdate &&
			State->WorkingCopyState == EWorkingCopyState::Unknown &&
			FPaths::IsUnderDirectory(*AbsoluteFile, WsPath))
		{
			State->SetStateFromDiskPresence();
		}

		OutState.Add(State);
	}

	return ECommandResult::Succeeded;
}

void FDiversionProvider::AddModifiedState(const TSharedRef<class FDiversionState>& InState)
{
	ModifiedStates.Add(InState->LocalFilename, InState);
	ChangelistState->Files.Add(InState);
}

bool FDiversionProvider::IsRepoWithSameNameExists(const FString& RepoName, EConcurrency::Type Concurrency, bool InForceUpdate)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"IsRepoWithSameNameExists must be called on the main game thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return bRepoWithSameNameExists.Get();
	}

	return bRepoWithSameNameExists.GetUpdate(bRepoWithSameNameExists.Get(),
		FOnCacheUpdate::CreateLambda([this, &Concurrency, &RepoName]() {
			auto operation = ISourceControlOperation::Create<FCheckForRepoWithSameName>();
			operation->SetRepoName(RepoName);
			Execute(operation, nullptr, TArray<FString>(), Concurrency);
		}), InForceUpdate);
}

bool FDiversionProvider::IsWorkspaceExistsInPath(const FString& RepoName, const FString& Path, EConcurrency::Type Concurrency, bool InForceUpdate)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"IsWorkspaceExistsInPath must be called on the main game thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return bWorkspaceExistsInPath.Get();
	}
	return bWorkspaceExistsInPath.GetUpdate(bWorkspaceExistsInPath.Get(),
		FOnCacheUpdate::CreateLambda([this, &Concurrency, &RepoName, &Path]() {
			auto operation = ISourceControlOperation::Create<FCheckIfWorkspaceExistsInPath>();
			operation->SetPath(Path);
			operation->SetRepoName(RepoName);
			Execute(operation, nullptr, TArray<FString>(), Concurrency);
		}), InForceUpdate);
}

void FDiversionProvider::AddCurrentChangesToChangelistState()
{
	ChangelistState->Files.Empty();
	for (auto& state : ModifiedStates)
	{
		ChangelistState->Files.Add(state.Value);
	}
}

TArray<TSharedRef<FDiversionState>> FDiversionProvider::PerformPreSubmitValidation(
	const TArray<FString>& FilesInChangeList,
	EDataValidationResult& Result,
	TArray<FText>& ValidationErrors,
	TArray<FText>& ValidationWarnings)
{
	// This is the common validation logic used by both changelist and files validation.
	// It works around Perforce-specific UE implementations.
	//
	// Since UE is coupled with perforce, the validation makes sure that if a Package's dependencies are
	// not in the changelists they are not "Added" and not "Checked-out"
	//
	// as performed in UDataValidationChangelist::IsDataValid(...)
	// in the condition: if (ExternalDependencyFileState->IsCheckedOut() || ExternalDependencyFileState->IsAdded())
	//
	// Since in Diversion everything is checked out by default, we need to work around this condition so
	// the changelist validation won't fail.
	//
	// This overrides the dependencies and "uncheck them out" for the duration of the validation check.

	// States that need to be cleaned up after the validation run
	TArray<TSharedRef<FDiversionState>> StatesToClean;

	for (const auto& FilePath : FilesInChangeList)
	{
		auto* Package = DiversionUtils::UPackageUtils::PackageFromPath(FilePath, true);
		if (Package != nullptr)
		{
			TArray<FString> ExternalDependenciesFilenames;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			TSet<TPair<FName, FName>> ExternalDependenciesSet;
#else
			TSet<FName> ExternalDependenciesSet;
#endif
			UDataValidationChangelist::GatherDependencies(Package->GetFName(), ExternalDependenciesSet);
			auto ExternalDependencies = ExternalDependenciesSet.Array();

			// Remove the files that are already in the changelist
			ExternalDependencies.RemoveAll([&FilesInChangeList](auto& File) -> bool {
			return Algo::AnyOf(FilesInChangeList, [&File](const auto& FileInChangelist) {
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				return File.Value == FileInChangelist;
#else
				return File == FileInChangelist;
#endif
				});
			});

			// Transform the dependencies packages to their filenames
			Algo::Transform(ExternalDependencies, ExternalDependenciesFilenames, [](auto& InFilename) -> FString
			{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				return USourceControlHelpers::PackageFilename(InFilename.Value.ToString());
#else
				return USourceControlHelpers::PackageFilename(InFilename.ToString());
#endif
			});

			for (const FString& DependencyFilePath : ExternalDependenciesFilenames)
			{
				auto DependencyState = GetStateInternal(DependencyFilePath);
				DependencyState->SetOverrideDefaultCheckout();
				StatesToClean.Add(DependencyState);
			}
		}
	}

	return StatesToClean;
}

void FDiversionProvider::RegisterPreSubmitValidationDelegates()
{
	// Safe to unregister: original delegates were copied (not handles) in constructor before this call
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	// UE 5.8: the submit dialog reads the "Ex" delegates; the non-Ex Register* methods are
	// deprecated no-ops, so registering them would silently drop our wrapper and let validation
	// failures slip through to commit.
	ISourceControlModule::Get().UnregisterPreSubmitFilesValidation();
	ISourceControlModule::Get().RegisterPreSubmitFilesValidationEx(
		FSourceControlPreSubmitFilesValidationExDelegate::CreateRaw(
			this, &FDiversionProvider::PreFilesValidationDelegateWrapperEx
		)
	);
	ISourceControlModule::Get().UnregisterPreSubmitChangelistValidation();
	ISourceControlModule::Get().RegisterPreSubmitChangelistValidationEx(
		FSourceControlPreSubmitChangelistValidationExDelegate::CreateRaw(
			this, &FDiversionProvider::PreChangelistValidationDelegateWrapperEx
		)
	);
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	ISourceControlModule::Get().UnregisterPreSubmitFilesValidation();
	ISourceControlModule::Get().RegisterPreSubmitFilesValidation(
		FSourceControlPreSubmitFilesValidationDelegate::CreateRaw(
			this, &FDiversionProvider::PreFilesValidationDelegateWrapper
		)
	);
	ISourceControlModule::Get().UnregisterPreSubmitChangelistValidation();
	ISourceControlModule::Get().RegisterPreSubmitChangelistValidation(
		FSourceControlPreSubmitDataValidationDelegate::CreateRaw(
			this, &FDiversionProvider::PreChangelistValidationDelegateWrapper
		)
	);
#else
	ISourceControlModule::Get().UnregisterPreSubmitDataValidation();
	ISourceControlModule::Get().RegisterPreSubmitDataValidation(
		FSourceControlPreSubmitDataValidationDelegate::CreateRaw(
			this, &FDiversionProvider::PreChangelistValidationDelegateWrapper
		)
	);
#endif
}

void FDiversionProvider::UnregisterAndRestorePreSubmitValidationDelegates()
{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	// UnregisterPreSubmit{Changelist,Files}Validation() ARE the "Ex" unregisters: in UE 5.8 the
	// Ex delegate lives in a single slot that Register/Get...Ex and these (non-deprecated, pure
	// virtual) Unregister methods all share - see FSourceControlModule::UnregisterPreSubmitFilesValidation
	// which assigns OnSourceControlPreSubmitFilesValidation = FSourceControlPreSubmitFilesValidationExDelegate().
	// There is no UnregisterPreSubmit*ValidationEx() variant (the deprecated no-op is the unrelated
	// UnregisterPreSubmitDataValidation()). So these calls fully clear our Ex wrapper before Close(),
	// preventing a dangling delegate / use-after-free into a destroyed provider.
	ISourceControlModule::Get().UnregisterPreSubmitChangelistValidation();
	ISourceControlModule::Get().UnregisterPreSubmitFilesValidation();

	// Restore original "Ex" delegates only if not shutting down the editor
	// (to avoid dangling delegate if original owner is already destroyed)
	if (!IsEngineExitRequested())
	{
		if (OriginalPreChangelistValidationDelegateEx.IsBound())
		{
			ISourceControlModule::Get().RegisterPreSubmitChangelistValidationEx(OriginalPreChangelistValidationDelegateEx);
		}
		if (OriginalPreFilesValidationDelegateEx.IsBound())
		{
			ISourceControlModule::Get().RegisterPreSubmitFilesValidationEx(OriginalPreFilesValidationDelegateEx);
		}
	}
#elif ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	ISourceControlModule::Get().UnregisterPreSubmitChangelistValidation();
	ISourceControlModule::Get().UnregisterPreSubmitFilesValidation();

	// Restore original delegates only if not shutting down the editor
	// (to avoid dangling delegate if original owner is already destroyed)
	if (!IsEngineExitRequested())
	{
		if (OriginalPreChangelistValidationDelegate.IsBound())
		{
			ISourceControlModule::Get().RegisterPreSubmitChangelistValidation(OriginalPreChangelistValidationDelegate);
		}
		if (OriginalPreFilesValidationDelegate.IsBound())
		{
			ISourceControlModule::Get().RegisterPreSubmitFilesValidation(OriginalPreFilesValidationDelegate);
		}
	}
#else
	ISourceControlModule::Get().UnregisterPreSubmitDataValidation();
	if (!IsEngineExitRequested() && OriginalPreChangelistValidationDelegate.IsBound())
	{
		ISourceControlModule::Get().RegisterPreSubmitDataValidation(OriginalPreChangelistValidationDelegate);
	}
#endif
}

#if UE_VERSION_OLDER_THAN(5, 8, 0)
void FDiversionProvider::PreChangelistValidationDelegateWrapper(
	FSourceControlChangelistPtr Changelist,
	EDataValidationResult& Result,
	TArray<FText>& ValidationErrors, TArray<FText>& ValidationWarnings)
{
	TArray<FString> FilesInChangeList;
	FilesInChangeList.Reserve(ChangelistState->Files.Num());
	for (const auto& File : ChangelistState->Files)
	{
		FilesInChangeList.Add(File->GetFilename());
	}

	TArray<TSharedRef<FDiversionState>> StatesToClean = PerformPreSubmitValidation(FilesInChangeList, Result, ValidationErrors, ValidationWarnings);

	OriginalPreChangelistValidationDelegate.ExecuteIfBound(Changelist, Result, ValidationErrors, ValidationWarnings);

	// Remember failure so Execute() can block the subsequent CheckIn (see bLastPreSubmitValidationFailed).
	// Mirror Epic's submit dialog (SourceControlWindows.cpp): treat the run as failed on an Invalid
	// result OR any reported errors, since a validator can add errors to the context without
	// returning Invalid.
	bLastPreSubmitValidationFailed = (Result == EDataValidationResult::Invalid) || (ValidationErrors.Num() > 0);

	for (auto State : StatesToClean)
	{
		State->ClearOverrideDefaultCheckout();
	}
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
void FDiversionProvider::PreFilesValidationDelegateWrapper(
	TArray<FSourceControlStateRef>& InStates,
	EDataValidationResult& Result,
	TArray<FText>& ValidationErrors,
	TArray<FText>& ValidationWarnings)
{
	TArray<FString> FilePaths;
	FilePaths.Reserve(InStates.Num());
	for (const auto& State : InStates)
	{
		FilePaths.Add(State->GetFilename());
	}

	TArray<TSharedRef<FDiversionState>> StatesToClean = PerformPreSubmitValidation(FilePaths, Result, ValidationErrors, ValidationWarnings);
	OriginalPreFilesValidationDelegate.ExecuteIfBound(InStates, Result, ValidationErrors, ValidationWarnings);

	// Remember failure so Execute() can block the subsequent CheckIn (see bLastPreSubmitValidationFailed).
	// Mirror Epic's submit dialog (SourceControlWindows.cpp): treat the run as failed on an Invalid
	// result OR any reported errors, since a validator can add errors to the context without
	// returning Invalid.
	bLastPreSubmitValidationFailed = (Result == EDataValidationResult::Invalid) || (ValidationErrors.Num() > 0);

	for (auto State : StatesToClean)
	{
		State->ClearOverrideDefaultCheckout();
	}
}
#endif // ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#endif // UE_VERSION_OLDER_THAN(5, 8, 0)

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
// UE 5.8 "Ex" variants: results come back via FSourceControlValidationDelegateResult instead of
// out-params. Same mechanism as the pre-5.8 wrappers - apply the dependency-uncheckout workaround,
// chain to the original (EditorValidatorSubsystem) delegate, then latch the failure so Execute()
// can block the follow-up CheckIn (Epic's dialog can't, because we force UsesChangelists()=false).
void FDiversionProvider::PreChangelistValidationDelegateWrapperEx(
	FSourceControlChangelistPtr Changelist,
	FSourceControlValidationDelegateResult& OutResult)
{
	TArray<FString> FilesInChangeList;
	FilesInChangeList.Reserve(ChangelistState->Files.Num());
	for (const auto& File : ChangelistState->Files)
	{
		FilesInChangeList.Add(File->GetFilename());
	}

	TArray<TSharedRef<FDiversionState>> StatesToClean = PerformPreSubmitValidation(
		FilesInChangeList, OutResult.Result, OutResult.Errors, OutResult.Warnings);

	OriginalPreChangelistValidationDelegateEx.ExecuteIfBound(Changelist, OutResult);

	// Mirror Epic's submit dialog: an Invalid result OR any reported errors counts as failure,
	// since a validator can add errors to the context without returning Invalid.
	bLastPreSubmitValidationFailed = (OutResult.Result == EDataValidationResult::Invalid) || (OutResult.Errors.Num() > 0);

	for (auto State : StatesToClean)
	{
		State->ClearOverrideDefaultCheckout();
	}
}

void FDiversionProvider::PreFilesValidationDelegateWrapperEx(
	TArray<FSourceControlStateRef>& InStates,
	FSourceControlValidationDelegateResult& OutResult)
{
	TArray<FString> FilePaths;
	FilePaths.Reserve(InStates.Num());
	for (const auto& State : InStates)
	{
		FilePaths.Add(State->GetFilename());
	}

	TArray<TSharedRef<FDiversionState>> StatesToClean = PerformPreSubmitValidation(
		FilePaths, OutResult.Result, OutResult.Errors, OutResult.Warnings);

	OriginalPreFilesValidationDelegateEx.ExecuteIfBound(InStates, OutResult);

	// Mirror Epic's submit dialog: an Invalid result OR any reported errors counts as failure,
	// since a validator can add errors to the context without returning Invalid.
	bLastPreSubmitValidationFailed = (OutResult.Result == EDataValidationResult::Invalid) || (OutResult.Errors.Num() > 0);

	for (auto State : StatesToClean)
	{
		State->ClearOverrideDefaultCheckout();
	}
}
#endif // !UE_VERSION_OLDER_THAN(5, 8, 0)

void FDiversionProvider::UpdateModifiedStates(const TMap<FString, TSharedRef<FDiversionState>>& InNewModifiedStates)
{
	for (auto& State : ModifiedStates)
	{
		if (!InNewModifiedStates.Contains(State.Key))
		{
			State.Value->ResetState();
		}
	}
	// Reset the list of modified states only if we requested a full repo status update
	ModifiedStates = InNewModifiedStates;
	AddCurrentChangesToChangelistState();
}

void FDiversionProvider::OnApplicationFocusChanged(const bool bIsFocused)
{
	if (!IsInGameThread())
	{
		return;
	}

	if (bIsFocused && bDiversionAvailable)
	{
		if (BackgroundStatus)
		{
			BackgroundStatus->TriggerInstantCallAndReset();
		}
		if (BackgroundPotentialClashes)
		{
			BackgroundPotentialClashes->TriggerInstantCallAndReset();
		}
		if (BackgroundConflictedFiles)
		{
			BackgroundConflictedFiles->TriggerInstantCallAndReset();
		}
	}
}

bool FDiversionProvider::IsPackageExpired(const FSyncInaccessiblePackages& SyncInaccessiblePackage) const
{
	return FDateTime::Now() - SyncInaccessiblePackage.LockTime > SyncInaccessiblePackageRefreshTime;
}

bool FDiversionProvider::UpdateCachedStates(const TMap<FString, FDiversionState>& InNewStates, const bool IsFullStatusUpdate)
{
	int NbStatesUpdated = 0;
	TMap<FString, TSharedRef<FDiversionState>> NewModifiedStates;

	// Update the local cached states
	for (const auto& [_, NewStateValue] : InNewStates)
	{
		const TSharedRef<FDiversionState> CachedState = GetStateInternal(NewStateValue.LocalFilename);
		CachedState->WorkingCopyState = NewStateValue.WorkingCopyState;
		CachedState->TimeStamp = NewStateValue.TimeStamp;
		if(NewStateValue.Hash != TEXT(""))
		{
			CachedState->Hash = NewStateValue.Hash;
		}
		NbStatesUpdated++;

		// Track modified states so the cache can be pruned once files are reverted.
		if(IsFullStatusUpdate)
		{
			if(CachedState->IsModified())
			{
				// Make sure to add only states that has active modifications
				NewModifiedStates.Add(CachedState->LocalFilename, CachedState);
			}
		}
	}

	// Handles updating the list of modified states and resetting
	// reverted previously modified states.
	if(IsFullStatusUpdate)
	{
		UpdateModifiedStates(NewModifiedStates);
	}

	// Remove syncing from states if necessary
	if (SyncStatus.Get() == DiversionUtils::EDiversionWsSyncStatus::Completed)
	{
		for (auto& State : SynchingStates)
		{
			State.Value->IsSyncing = false;
		}
	}

	// Keep the interval between background status calls; this does not affect calls generated by UE
	BackgroundStatusSkipToNextInterval();
	return (NbStatesUpdated > 0);
}

bool FDiversionProvider::UpdateConflictedStates(const TMap<FString, FDiversionResolveInfo>& ConflictedFilesData)
{
	int NbStatesUpdated = 0;

	// Reset the states data
	for(auto& [_, State] : ConflictedStates)
	{
		State->ClearResolveInfo();
	}
	ConflictedStates.Empty();

	// Update the conflicted states
	for(auto& [FilePath, ResolveInfo] : ConflictedFilesData)
	{
		const TSharedRef<FDiversionState> CachedState = GetStateInternal(FilePath);
		CachedState->AddPendingResolveInfo(ResolveInfo);
		ConflictedStates.Add(FilePath, CachedState);
		NbStatesUpdated++;
	}

	return (NbStatesUpdated > 0);
}

bool FDiversionProvider::RemoveConflictedState(const FString& Path)
{
	auto ConflictedState = ConflictedStates.Find(Path);
	if(ConflictedState == nullptr)
	{
		return false;
	}
	ConflictedState->Get().ClearResolveInfo();
	return ConflictedStates.Remove(Path) > 0;
}

TUniquePtr<FDiversionResolveInfo> FDiversionProvider::GetFileResolveInfo(const FString& Path) const
{
	if(const auto* ResolveInfo = ConflictedStates.Find(Path); ResolveInfo != nullptr)
	{
		return MakeUnique<FDiversionResolveInfo>(ResolveInfo->Get().GetPendingResolveInfo());
	}
	return nullptr;
}

void FDiversionProvider::GetConflictedFilesPaths(TArray<FString>& OutArray) const
{
	return ConflictedStates.GenerateKeyArray(OutArray);
}

void FDiversionProvider::SetFilesToResolve(const TMap<FString, FDiversionResolveInfo>& InFilesToResolve)
{
	FilesToResolve = InFilesToResolve;
}

bool FDiversionProvider::UpdatePotentialClashedStates(
	const TMap<FString, TArray<EDiversionPotentialClashInfo>>& InPotentialClashes,
	bool IsFullStatusUpdate)
{
	// Logged, not reported: the backend error report is a blocking HTTP call, and this fires
	// on the wrong thread - the last place that can afford to stall
	if (!DiversionValidityUtils::DiversionValidityCheck(IsInGameThread(),
		"UpdatePotentialClashedStates called outside of main thread"))
	{
		return false;
	}

	int NbStatesUpdated = 0;

	// Clear potential clashes cache only if we perform a full status update
	if(IsFullStatusUpdate)
	{
		TArray<FString> KeysToRemove;
		// Reset the potential clashes that are no longer in the list
		for(auto& [FileName, State] : PotentiallyClashedStates)
		{
			if(!InPotentialClashes.Contains(FileName))
			{
				State->PotentialClashes.ResetPotentialClashes();
				KeysToRemove.Add(FileName);
			}
			NbStatesUpdated++;
		}

		// Remove the keys that are no longer in the list
		for(const auto& Key : KeysToRemove)
		{
			PotentiallyClashedStates.Remove(Key);
		}
	}

	// Update existing states or add new ones
	for(const auto& [Filename, PotentialClashInfo] : InPotentialClashes)
	{
		if(const auto* CachedState = PotentiallyClashedStates.Find(Filename); CachedState != nullptr)
		{
			(*CachedState)->PotentialClashes.SetPotentialClashes(PotentialClashInfo);
		}
		else
		{
			TSharedRef<FDiversionState> NewCachedState = GetStateInternal(Filename);
			NewCachedState->PotentialClashes.SetPotentialClashes(PotentialClashInfo);
			PotentiallyClashedStates.Add(Filename, NewCachedState);
		}
		NbStatesUpdated++;
	}

	return (NbStatesUpdated > 0);
}

void FDiversionProvider::GetCurrentPotentialClashes(TArray<FString>& OutPotentialClashedPaths) const
{
	PotentiallyClashedStates.GetKeys(OutPotentialClashedPaths);
}


void FDiversionProvider::UpdateHardLocksEligibility(const TOptional<bool>& bInEligible)
{
	if (!DiversionValidityUtils::DiversionValidityCheck(IsInGameThread(),
		"UpdateHardLocksEligibility called outside of main thread"))
	{
		return;
	}
	bHardLocksEligible = bInEligible;
	HardLocksEligibilityResolvedAt = bHardLocksEligible.IsSet() ? FPlatformTime::Seconds() : 0.0;
	// Greys the lock-prompt setting out in Project Settings where it can do nothing
	SetHardLocksAvailableFlag(ShouldCheckHardLocks());
	UE_LOG(LogSourceControl, Log, TEXT("Hard-locks eligibility resolved: %s"),
		!bHardLocksEligible.IsSet() ? TEXT("unknown (lock checks skipped)") :
		bHardLocksEligible.GetValue() ? TEXT("eligible (lock checks enabled)") : TEXT("not eligible (lock checks skipped)"));
}

void FDiversionProvider::RequestHardLocksEligibility()
{
	if (bHardLocksEligibilityInFlight)
	{
		return;
	}
	// Only a queued command reaches the worker that releases the guard. Armed before Execute
	// (a synchronous completion would clear it inside the call), then disarmed on anything
	// but Succeeded: Failed never queues, and IssueCommand answers Cancelled - without
	// queuing - when the thread pool is full.
	bHardLocksEligibilityInFlight = true;
	const ECommandResult::Type Result = Execute(ISourceControlOperation::Create<FGetHardLocksEligibility>(),
		nullptr, TArray<FString>(), EConcurrency::Asynchronous);
	if (Result != ECommandResult::Succeeded)
	{
		bHardLocksEligibilityInFlight = false;
	}
}

bool FDiversionProvider::IsHardLocksEligibilityRefreshDue() const
{
	if (!bHardLocksEligible.IsSet())
	{
		// Never resolved, or the startup fetch failed - keep trying
		return true;
	}
	return FPlatformTime::Seconds() - HardLocksEligibilityResolvedAt >= SECONDS_TO_REFRESH_HARD_LOCKS_ELIGIBILITY;
}

void FDiversionProvider::SetLockablePatterns(const TOptional<TArray<FString>>& InPatterns)
{
	if (!DiversionValidityUtils::DiversionValidityCheck(IsInGameThread(),
		"SetLockablePatterns called outside of main thread"))
	{
		return;
	}
	LockablePatterns = InPatterns;
	if (!LockablePatterns.IsSet())
	{
		UE_LOG(LogSourceControl, Log, TEXT("Repo lockable patterns unresolved - the save gate keeps prompting"));
		return;
	}
	// Only extension patterns are evaluated client-side (see DiversionFilePattern.h); name the
	// rest once so a repo configured with globs through the API can be understood from the log
	TArray<FString> IgnoredGlobs = LockablePatterns.GetValue().FilterByPredicate(
		[](const FString& Pattern) { return !DiversionFilePattern::IsExtensionPattern(Pattern); });
	UE_LOG(LogSourceControl, Log, TEXT("Repo lockable patterns resolved: %d, of which %d glob(s) the plugin does not evaluate%s%s"),
		LockablePatterns.GetValue().Num(), IgnoredGlobs.Num(),
		IgnoredGlobs.Num() > 0 ? TEXT(": ") : TEXT(""), *FString::Join(IgnoredGlobs, TEXT(", ")));
}

bool FDiversionProvider::IsAutoLockedByRepoRule(const FString& InFullPath) const
{
	if (!ShouldCheckHardLocks() || !LockablePatterns.IsSet())
	{
		return false;
	}
	// The server matches repo-relative paths; the workspace root is the repo root
	return DiversionFilePattern::MatchesExtension(
		DiversionUtils::ConvertFullPathToRelative(InFullPath, GetPathToWorkspaceRoot()),
		LockablePatterns.GetValue());
}

bool FDiversionProvider::UpdateLockedStates(const TMap<FString, FDiversionLockInfo>& InLocks,
	bool IsFullStatusUpdate)
{
	if (!DiversionValidityUtils::DiversionValidityCheck(IsInGameThread(),
		"UpdateLockedStates called outside of main thread"))
	{
		return false;
	}

	int NbStatesUpdated = 0;
	// Not derivable from NbStatesUpdated: that counts states touched, and a poll returning the
	// same locks touches every one of them
	bool bLockSetChanged = false;

	// Clear lock state only if we perform a full repo locks update
	if(IsFullStatusUpdate)
	{
		// Only a full pass proves what is NOT locked; a partial refresh says nothing about
		// the rest of the repo
		bFetchedFullLockSet = true;

		TArray<FString> KeysToRemove;
		// Reset the locks that are no longer in the list
		for(auto& [FileName, State] : LockedStates)
		{
			if(!InLocks.Contains(FileName))
			{
				State->FileLock.ClearLockInfo();
				KeysToRemove.Add(FileName);
			}
			NbStatesUpdated++;
		}

		// Remove the keys that are no longer in the list
		for(const auto& Key : KeysToRemove)
		{
			LockedStates.Remove(Key);
		}
		bLockSetChanged = bLockSetChanged || KeysToRemove.Num() > 0;
	}

	// Update existing states or add new ones
	for(const auto& [Filename, LockInfo] : InLocks)
	{
		if(!LockInfo.IsSet())
		{
			// An unset entry is an explicit "not locked" answer for a queried file
			if(const auto* CachedState = LockedStates.Find(Filename); CachedState != nullptr)
			{
				(*CachedState)->FileLock.ClearLockInfo();
				LockedStates.Remove(Filename);
				NbStatesUpdated++;
				bLockSetChanged = true;
			}
			continue;
		}

		if(const auto* CachedState = LockedStates.Find(Filename); CachedState != nullptr)
		{
			bLockSetChanged = bLockSetChanged || (*CachedState)->FileLock.GetLockInfo() != LockInfo;
			(*CachedState)->FileLock.SetLockInfo(LockInfo);
		}
		else
		{
			TSharedRef<FDiversionState> NewCachedState = GetStateInternal(Filename);
			NewCachedState->FileLock.SetLockInfo(LockInfo);
			LockedStates.Add(Filename, NewCachedState);
			bLockSetChanged = true;
		}
		NbStatesUpdated++;
	}

	if(bLockSetChanged)
	{
		++LockedStatesRevision;
	}

	return (NbStatesUpdated > 0);
}

bool FDiversionProvider::RefreshFileLocks(const TArray<FString>& InPaths)
{
	// Synchronous by necessity: both callers need the answer before they can decide, so a
	// late async result is no use to them. The worker applies the response to the state
	// cache, which is what the callers read afterwards.
	return Execute(ISourceControlOperation::Create<FGetFileLocks>(), nullptr, InPaths,
		EConcurrency::Synchronous) == ECommandResult::Succeeded;
}

TSharedRef<FDiversionState, ESPMode::ThreadSafe> FDiversionProvider::GetStateInternal(const FString& Filename)
{
	TSharedRef<FDiversionState, ESPMode::ThreadSafe>* State = StateCache.Find(Filename);
	if (State != nullptr)
	{
		// found cached item
		return (*State);
	}
	else
	{
		// cache an unknown state for this item
		TSharedRef<FDiversionState, ESPMode::ThreadSafe> NewState = MakeShareable(new FDiversionState(Filename));
		StateCache.Add(Filename, NewState);
		return NewState;
	}
}

void FDiversionProvider::AddSyncingState(const FString& Path, const TSharedRef<class FDiversionState>& InState)
{
	SynchingStates.Add(Path, InState);
}

ECommandResult::Type FDiversionProvider::GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}
	OutState.Add(ChangelistState);

	return ECommandResult::Succeeded;
}

bool IsConfigFile(const FString& FilePath)
{
	// Check if the file has an .ini extension
	if (!FilePath.EndsWith(TEXT(".ini")))
	{
		return false;
	}

	// Get the path to the project's Config directory
	FString ConfigDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir());

	// Check if FilePath starts with the project's Config directory path
	if (FilePath.StartsWith(ConfigDir))
	{
		return true;
	}

	return false;
}

TArray<FSourceControlStateRef> FDiversionProvider::GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const
{
	TArray<FSourceControlStateRef> Result;
	for (const auto& CacheItem : StateCache)
	{
		FSourceControlStateRef State = CacheItem.Value;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		// In 5.6+ we use our own commit flow (FDiversionCommitHelper) which gathers
		// ALL files (packages and config) directly from the cache. No filtering needed
		// here since GatherCommittableFiles handles deduplication and categorization.
		if (Predicate(State))
		{
			Result.Add(State);
		}
#else
		// In older versions, UE's ChoosePackagesToCheckIn flow causes duplicate files
		// when using custom projects - config files are returned both by
		// FindAllSubmittableProjectFiles (via GetCachedStateByPredicate) and
		// FindAllSubmittableConfigFiles (via GetState). Filter them here to avoid duplicates.
		if (Predicate(State) && !IsConfigFile(State->GetFilename()))
		{
			Result.Add(State);
		}
#endif
	}
	return Result;
}

bool FDiversionProvider::RemoveFileFromCache(const FString& Filename)
{
	return StateCache.Remove(Filename) > 0;
}


FDelegateHandle FDiversionProvider::RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged)
{
	return OnSourceControlStateChanged.Add(SourceControlStateChanged);
}

void FDiversionProvider::UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle)
{
	OnSourceControlStateChanged.Remove(Handle);
}

ECommandResult::Type FDiversionProvider::Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	if (bDiversionStopped)
	{
		return ECommandResult::Failed;
	}

	// Workers/Commands must not call to other workers/commands and to this command directly!
	// Workers should call directly to the SyncAPI methods and update the provider state accordingly
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"Provider Execute called outside of main thread", FDiversionModule::Get().GetOriginalAccountID()))
	{
		return ECommandResult::Failed;
	}

	if (!IsEnabled() && !(CanExecuteBeforeEnabled.Contains(InOperation->GetName())))
	{
		// Note that IsEnabled() always returns true so unless it is changed, this code will never be executed
		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	// Block CheckIn when the custom commit view's validation failed. Scoped to our dialog via
	// bOverrideUsesChangelists so unrelated commit paths aren't gated by stale state.
	if (bOverrideUsesChangelists && bLastPreSubmitValidationFailed && InOperation->GetName() == "CheckIn")
	{
		bLastPreSubmitValidationFailed = false;
		const FText Message = LOCTEXT("PreSubmitValidationFailed", "Pre-submit validation failed. Commit aborted. See Message Log for details.");
		FMessageLog("SourceControl").Error(Message);
		InOperation->AddErrorMessge(Message);
		ShowNotification(FDiversionNotification(Message, TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail));
		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	// Query to see if we allow this operation
	TSharedPtr<IDiversionWorker, ESPMode::ThreadSafe> Worker = CreateWorker(InOperation->GetName());
	if (!Worker.IsValid())
	{
		// this operation is unsupported by this version control provider
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("OperationName"), FText::FromName(InOperation->GetName()));
		Arguments.Add(TEXT("ProviderName"), FText::FromName(GetName()));
		FText Message(FText::Format(LOCTEXT("UnsupportedOperation", "Operation '{OperationName}' not supported by revision control provider '{ProviderName}'"), Arguments));
		FMessageLog("SourceControl").Error(Message);
		InOperation->AddErrorMessge(Message);

		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	FDiversionCommand* Command = new FDiversionCommand(InOperation, Worker.ToSharedRef(), InConcurrency);
	Command->Files = AbsoluteFiles;
	Command->OperationCompleteDelegate = InOperationCompleteDelegate;

	// fire off operation
	if (InConcurrency == EConcurrency::Synchronous)
	{
		Command->bAutoDelete = false;
		return ExecuteSynchronousCommand(*Command, InOperation->GetInProgressString());
	}
	else
	{
		Command->bAutoDelete = true;
		return IssueCommand(*Command);
	}
}

bool FDiversionProvider::CanExecuteOperation(const FSourceControlOperationRef& InOperation) const
{
	return WorkersMap.Find(InOperation->GetName()) != nullptr;
}

bool FDiversionProvider::CanCancelOperation(const FSourceControlOperationRef& InOperation) const
{
	return false;
}

void FDiversionProvider::CancelOperation(const FSourceControlOperationRef& InOperation)
{
}

bool FDiversionProvider::UsesLocalReadOnlyState() const
{
	return false;
}

bool FDiversionProvider::UsesChangelists() const
{
	if (bOverrideUsesChangelists)
	{
		return false;
	}
	return true;
}

bool FDiversionProvider::UsesUncontrolledChangelists() const
{
	return false;
}

bool FDiversionProvider::UsesCheckout() const
{
	return false;
}

bool FDiversionProvider::UsesFileRevisions() const
{
	return false;
}

bool FDiversionProvider::UsesSnapshots() const
{
	return false;
}

bool FDiversionProvider::AllowsDiffAgainstDepot() const
{
	return true;
}

#if !UE_VERSION_OLDER_THAN(5, 8, 0)
bool FDiversionProvider::UsesSoftRevertOnDelete() const
{
	// Return false to keep the engine's pre-delete revert a normal (hard) revert, matching every
	// built-in provider (Git/Perforce/Subversion/Default). The revert-before-delete is already
	// handled by the OnPackageDeleted / FilesPendingDelete workaround, which skips RunReset for
	// files pending deletion (see FDiversionRevertWorker::Execute). That workaround is the single
	// cross-version code path (it also covers <=5.7, where this hook doesn't exist), so we don't
	// opt into soft revert here.
	// A future option is to return true and honor FRevert::IsSoftRevert() in the Revert
	// worker for 5.8+, retiring the OnPackageDeleted workaround on that path.
	return false;
}
#endif

#if UE_VERSION_OLDER_THAN(5, 8, 0)
TOptional<bool> FDiversionProvider::IsAtLatestRevision() const
{
	return TOptional<bool>();
}

TOptional<int> FDiversionProvider::GetNumLocalChanges() const
{
	if(GetSyncStatus() == DiversionUtils::EDiversionWsSyncStatus::Completed)
	{
		return TOptional<int>(ModifiedStates.Num());
	} else
	{
		return TOptional<int>(0);
	}
}
#else
TOptional<bool> FDiversionProvider::HasChangesToSync() const
{
	return TOptional<bool>();
}

TOptional<bool> FDiversionProvider::HasChangesToCheckIn() const
{
	if(GetSyncStatus() == DiversionUtils::EDiversionWsSyncStatus::Completed)
	{
		return TOptional<bool>(ModifiedStates.Num() > 0);
	} else
	{
		return TOptional<bool>(false);
	}
}
#endif

TSharedRef<SWidget> FDiversionProvider::OnGenerateAssetViewPotentialClashIcon(const FAssetData& AssetData)
{
	return SNew(SPotentialClashIndicator).AssetPath(
		DiversionUtils::GetFilePathFromAssetData(AssetData)
	);
}

TSharedRef<SWidget> FDiversionProvider::OnGenerateAssetViewPotentialClashTooltip(const FAssetData& AssetData)
{
	return SNew(SPotentialClashTooltip).AssetPath(
		DiversionUtils::GetFilePathFromAssetData(AssetData)
	);
}

TSharedRef<SWidget> FDiversionProvider::OnGenerateAssetViewHardLockIcon(const FAssetData& AssetData)
{
	return SNew(SHardLockIndicator).AssetPath(
		DiversionUtils::GetFilePathFromAssetData(AssetData)
	);
}

TSharedRef<SWidget> FDiversionProvider::OnGenerateAssetViewHardLockTooltip(const FAssetData& AssetData)
{
	return SNew(SHardLockTooltip).AssetPath(
		DiversionUtils::GetFilePathFromAssetData(AssetData)
	);
}

TSharedPtr<IDiversionWorker, ESPMode::ThreadSafe> FDiversionProvider::CreateWorker(const FName& InOperationName) const
{
	const FGetDiversionWorker* Operation = WorkersMap.Find(InOperationName);
	if (Operation != nullptr)
	{
		return Operation->Execute();
	}

	return nullptr;
}

void FDiversionProvider::RegisterWorker(const FName& InName, const FGetDiversionWorker& InDelegate)
{
	WorkersMap.Add(InName, InDelegate);
}

void FDiversionProvider::OutputCommandMessages(const FDiversionCommand& InCommand)
{
	FMessageLog SourceControlLog("SourceControl");

	for (int32 ErrorIndex = 0; ErrorIndex < InCommand.ErrorMessages.Num(); ++ErrorIndex)
	{
		SourceControlLog.Error(FText::FromString(InCommand.ErrorMessages[ErrorIndex]));
	}

	for (int32 InfoIndex = 0; InfoIndex < InCommand.InfoMessages.Num(); ++InfoIndex)
	{
		SourceControlLog.Info(FText::FromString(InCommand.InfoMessages[InfoIndex]));
	}

	if(InCommand.PopupNotification.IsValid())
	{
		NotificationManager.ShowNotification(*(InCommand.PopupNotification));
	}
}

void FDiversionProvider::Tick()
{

	// Revalidate the agent status and WsInfo
	IsAgentAlive(EConcurrency::Asynchronous, ReloadStatusRequired);
	GetWsInfo(EConcurrency::Asynchronous, ReloadStatusRequired);
	if(ReloadStatusRequired)
	{
		ReloadStatusRequired = false;
	}

	bool bStatesUpdated = false;
	for (int32 CommandIndex = 0; CommandIndex < CommandQueue.Num(); ++CommandIndex)
	{
		FDiversionCommand& Command = *CommandQueue[CommandIndex];
		if (Command.bExecuteProcessed)
		{
			// Remove command from the queue
			CommandQueue.RemoveAt(CommandIndex);

			// let command update the states of any files
			bStatesUpdated |= Command.Worker->UpdateStates();

			// dump any messages to output log
			OutputCommandMessages(Command);

			Command.ReturnResults();

			// commands that are left in the array during a tick need to be deleted
			if (Command.bAutoDelete)
			{
				// Only delete commands that are not running 'synchronously'
				delete &Command;
			}

			// only do one command per tick loop, as we don't want concurrent modification
			// of the command queue (which can happen in the completion delegate)
			break;
		}
	}

	if (bStatesUpdated)
	{
		OnSourceControlStateChanged.Broadcast();
	}
}

TArray< TSharedRef<ISourceControlLabel> > FDiversionProvider::GetLabels(const FString& InMatchingSpec) const
{
	TArray< TSharedRef<ISourceControlLabel> > Tags;

	return Tags;
}

TArray<FSourceControlChangelistRef> FDiversionProvider::GetChangelists(EStateCacheUsage::Type InStateCacheUsage)
{
	return TArray<FSourceControlChangelistRef>();
}

#if SOURCE_CONTROL_WITH_SLATE
TSharedRef<class SWidget> FDiversionProvider::MakeSettingsWidget() const
{
	return SNew(SDiversionSettings);
}
#endif

ECommandResult::Type FDiversionProvider::ExecuteSynchronousCommand(FDiversionCommand& InCommand, const FText& Task)
{
	ECommandResult::Type Result = ECommandResult::Failed;

	// Display the progress dialog if a string was provided
	{
		FScopedSourceControlProgress Progress(Task);

		// Issue the command asynchronously...
		IssueCommand(InCommand);

		// ... then wait for its completion (thus making it synchronous)
		while (!InCommand.bExecuteProcessed)
		{
			// Tick the command queue and update progress.
			Tick();

			Progress.Tick();

			// Sleep for a bit so we don't busy-wait so much.
			FPlatformProcess::Sleep(0.01f);
		}

		// always do one more Tick() to make sure the command queue is cleaned up.
		Tick();

		if (InCommand.bCommandSuccessful)
		{
			Result = ECommandResult::Succeeded;
		}
	}

	// Delete the command now (asynchronous commands are deleted in the Tick() method)
	if (!DiversionUtils::DiversionValidityCheck(!InCommand.bAutoDelete,
		"InCommand was configured to execute synchronously with auto delete on", WsInfo.Get().AccountID))
	{
		return Result;
	}

	// ensure commands that are not auto deleted do not end up in the command queue
	if (CommandQueue.Contains(&InCommand))
	{
		CommandQueue.Remove(&InCommand);
	}
	delete &InCommand;

	return Result;
}

ECommandResult::Type FDiversionProvider::IssueCommand(FDiversionCommand& InCommand)
{
	if (GThreadPool != nullptr && CommandQueue.Num() < GThreadPool->GetNumThreads())
	{
		// Queue this to our worker thread(s) for resolving
		GThreadPool->AddQueuedWork(&InCommand);
		CommandQueue.Add(&InCommand);
		return ECommandResult::Succeeded;
	}
	else
	{
		FText Message(
			FText::Format(LOCTEXT("NoSCCThreads", "There are no threads available to process the revision control command: {0}."), FText::FromName(InCommand.Worker->GetName()))
		);
		FMessageLog("SourceControl").Error(Message);
		InCommand.Operation->AddErrorMessge(Message);
		UE_LOG(LogSourceControl, Error, TEXT("There are no threads available to process the revision control command: %s."), *(InCommand.Worker->GetName().ToString()));

		int availableThreads = (GThreadPool != nullptr) ? GThreadPool->GetNumThreads() : 0;
		UE_LOG(LogSourceControl, Error, TEXT("There are currently %d active worker threads out of: %d"), CommandQueue.Num(), availableThreads);
		for (auto& cmd : CommandQueue)
		{
			UE_LOG(LogSourceControl, Error, TEXT("% s"), *(cmd->Worker->GetName().ToString()));
		}

		InCommand.MarkOperationCompleted(false);
		return ECommandResult::Cancelled;
	}
}

void RemoveCallbackRestoreAfterFileFinishedSyncing(const FString& RestoreDestPath, int DirectoryWatcherHandleIndex)
{
	auto& Provider = FDiversionModule::Get().GetProvider();
	FDelegateHandle DirectoryWatcherHandle = Provider.GetRestoreAfterSyncDelegateHandle(DirectoryWatcherHandleIndex);
	if (DirectoryWatcherHandle.IsValid() && FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
	{
		IDirectoryWatcher* DirectoryWatcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher").Get();
		FString DirectoryPath = FPaths::GetPath(RestoreDestPath);
		DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(DirectoryPath, DirectoryWatcherHandle);
		Provider.RemoveRestoreAfterSyncDelegateHandle(DirectoryWatcherHandleIndex);
	}
}

void RestoreFileAfterSync(const FString& RestoreDestPath,
	int DirectoryWatcherHandleIndex, const TArray<FFileChangeData>& FileChanges)
{
	FString NormalizedRestoreDestPath = RestoreDestPath.Replace(TEXT("\\"), TEXT("/"));
	for (const FFileChangeData& Change : FileChanges)
	{
		FString NormalizedChangePath = Change.Filename.Replace(TEXT("\\"), TEXT("/"));
		if (NormalizedChangePath.Equals(NormalizedRestoreDestPath) && Change.Action == FFileChangeData::FCA_Modified)
		{
			try
			{
				auto* LoadPackage = DiversionUtils::UPackageUtils::PackageFromPath(NormalizedChangePath);
				UPackageTools::ReloadPackages({ LoadPackage });
			}
			catch (const std::exception& e)
			{
				UE_LOG(LogSourceControl, Error, TEXT("Failed to reload package: %s"), *FString(e.what()));
			}

			UE_LOG(LogSourceControl, Display, TEXT("Detected modification of %s and restored unsaved changes."), *RestoreDestPath);
			// Unregister the callback after the file has been restored
			RemoveCallbackRestoreAfterFileFinishedSyncing(RestoreDestPath, DirectoryWatcherHandleIndex);
			return;
		}
	}
}

void RegisterCallbackRestoreAfterFileFinishedSyncing(const FString& PackagePath)
{
	if (FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
	{
		auto& Provider = FDiversionModule::Get().GetProvider();
		int DirectoryWatcherHandleIndex = Provider.AddRestoreAfterSyncDelegateHandle();
		IDirectoryWatcher* DirectoryWatcher = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher").Get();
		FString DirectoryPath = FPaths::GetPath(PackagePath);
		DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
			DirectoryPath,
			// The actual callback that will be called when the file is modified
			// It will also unregister its delegate handle after the file has been restored
			IDirectoryWatcher::FDirectoryChanged::CreateLambda([PackagePath, DirectoryWatcherHandleIndex](const TArray<FFileChangeData>& FileChanges)
				{
					RestoreFileAfterSync(PackagePath, DirectoryWatcherHandleIndex, FileChanges);
				}),
			Provider.GetRestoreAfterSyncDelegateHandle(DirectoryWatcherHandleIndex));

		UE_LOG(LogTemp, Display, TEXT("Started monitoring directory: %s"), *DirectoryPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DirectoryWatcher module is not loaded, cannot monitor file changes"));
	}
}

void FDiversionProvider::OnPrePackageSave(UPackage* Package, FObjectPreSaveContext SaveContext)
{
	const FString FileName = FPaths::ConvertRelativePathToFull(SaveContext.GetTargetFilename());

#if PLATFORM_WINDOWS
	const FString OSNormalizedFileName = FileName.Replace(TEXT("/"), TEXT("\\"));
#else
	const FString OSNormalizedFileName = FileName;
#endif
	if (GetSyncInaccessiblePackages(OSNormalizedFileName))
	{
		// Handling saving local-remote conflicted files
		// It's should be safe to assume that if we're saving a loaded file it
		// means we will have conflicts against the remote version
		FText OnSaveLoadedPackageMessage = FText::Format(
			LOCTEXT("DiversionLockedFiles_OnSaveMessage",
				"The file \"{0}\" has been updated in the cloud repo. "
				"To protect your unsaved local changes, Diversion created a backup copy with a `_resolved` postfix. "
				"This backup allows you to compare your changes with the incoming version."),
			FText::FromString(FileName));

		FDiversionNotification PopUpNotification(
			OnSaveLoadedPackageMessage,
			TArray<FNotificationButtonInfo>(),
			SNotificationItem::CS_Fail);
		NotificationManager.ShowNotification(PopUpNotification);
		RegisterCallbackRestoreAfterFileFinishedSyncing(OSNormalizedFileName);
		if (!DiversionUtils::UPackageUtils::BackupUnsavedChanges(Package))
		{
			// Fallback to simple OS file copy -- Note, this makes the package undetectable by UE!
			// To use it you'll need to rename it back to the original file name (overriding the existing asset).
			// This should be used only as a last resort if copying the package with UE asset tools failed.
			DiversionUtils::UPackageUtils::MakeSimpleOSCopyPackageBackup(OSNormalizedFileName, FPaths::GetPath(OSNormalizedFileName));
		}

		if (DiversionUtils::UPackageUtils::IsPackageOpenedInEditor(Package->GetName()))
		{
			DiversionUtils::UPackageUtils::ClosePackageEditor(Package);
		}
		Package->SetDirtyFlag(false);
		RemoveSyncInaccessiblePackage(OSNormalizedFileName);
	}
}

void FDiversionProvider::OnPackageSave(const FString& PackageFileName, UObject*)
{
	const FString FileName = FPaths::ConvertRelativePathToFull(PackageFileName);

	// After a file resolve operation is called, after the file was saved we need to
	// trigger a Diversion Resolve operation on it
	// If we left with 0 files to resolve, we can finalize the merge
	if (const auto FileResolveInfo = FilesToResolve.Find(FileName))
	{
		FString MergeId = FileResolveInfo->MergeId;
		auto ResolveFileOperation = ISourceControlOperation::Create<FResolveFile>();
		ResolveFileOperation->SetMergeID(MergeId);
		ResolveFileOperation->SetConflictID(FileResolveInfo->ConflictId);
		Execute(ResolveFileOperation, nullptr, {FileName}, EConcurrency::Synchronous);

		FilesToResolve.Remove(FileName);
		// We will revert the change only after finalizing the merge to avoid losing the changes
		FilesToRevert.Add(FileName);

		if (ConflictedStates.Num() + FilesToResolve.Num() == 0)
		{
			// Run finalize merge and wait for it to finish
			auto FinalizeMergeOperation = ISourceControlOperation::Create<FFinalizeMerge>();
			FinalizeMergeOperation->SetMergeID(MergeId);
			Execute(FinalizeMergeOperation, nullptr, {}, EConcurrency::Synchronous,
			FSourceControlOperationComplete::CreateLambda([this](const FSourceControlOperationRef&, ECommandResult::Type InResult) {
				if (InResult == ECommandResult::Succeeded)
				{
					// Reset the workspace to avoid showing wrong changes - We committed them in the merge already
					Execute(ISourceControlOperation::Create<FRevert>(), nullptr,
						FilesToRevert, EConcurrency::Synchronous);
				}
			}));
		}
	}
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
void FDiversionProvider::OnPackageSaveWithContext(const FString& PackageFileName, UPackage* Package, FObjectPostSaveContext ObjectSaveContext)
{
	// UE 5.4+ version - delegate to the existing implementation
	// Explicitly mark unused parameters
	(void)Package;
	(void)ObjectSaveContext;
	OnPackageSave(PackageFileName, nullptr);
}
#endif

void FDiversionProvider::OnPackageDeletedHandler(UPackage* DeletedPackage)
{
	// Use PackageFilename to convert without requiring on-disk existence,
	// then normalize the same way GetState does for cache key consistency.
	const FString PackageFilename = USourceControlHelpers::PackageFilename(DeletedPackage);
	if (PackageFilename.IsEmpty())
	{
		return;
	}

	TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames({PackageFilename});
	if (AbsoluteFiles.Num() > 0)
	{
		FScopeLock Lock(&PendingDeleteLock);
		// Clear stale entries from a previous batch that was never fully cleaned up
		const double Now = FPlatformTime::Seconds();
		if (FilesPendingDelete.Num() > 0 && (Now - PendingDeleteLastAddTime) > PendingDeleteStaleSeconds)
		{
			UE_LOG(LogSourceControl, Log, TEXT("Diversion: Clearing %d stale pending-delete entries"), FilesPendingDelete.Num());
			FilesPendingDelete.Empty();
		}
		FilesPendingDelete.Add(AbsoluteFiles[0]);
		PendingDeleteLastAddTime = Now;
	}
}

bool FDiversionProvider::IsFilePendingDelete(const FString& FilePath) const
{
	FScopeLock Lock(&PendingDeleteLock);
	return FilesPendingDelete.Contains(FilePath);
}

void FDiversionProvider::ClearPendingDelete(const FString& FilePath)
{
	FScopeLock Lock(&PendingDeleteLock);
	FilesPendingDelete.Remove(FilePath);
}

TSet<FString> FDiversionProvider::GetFilesPendingDeleteSnapshot() const
{
	FScopeLock Lock(&PendingDeleteLock);
	return FilesPendingDelete;
}

void FDiversionProvider::RemoveRestoreAfterSyncDelegateHandle(int DelegateHandleIndex)
{
	DirectoryWatcherHandles.RemoveAtSwap(DelegateHandleIndex);
}

bool FDiversionProvider::ReleaseLockedPackage(UPackage* Package, const FString& PackagePath)
{
	UE_LOG(LogSourceControl, Warning,
		   TEXT("'%s' possibly opened by UE, attempting to close it to continue Diversion agent syncing"),
		   *PackagePath);
	if (UPackageTools::UnloadPackages({ Package }))
	{
		RemoveSyncInaccessiblePackage(PackagePath);
		UE_LOG(LogSourceControl, Display, TEXT("Successfully unloaded package: '%s'"), *PackagePath);
		return true;
	}

	UE_LOG(LogSourceControl, Warning,
		   TEXT("Failed to unload the opened package: '%s', make sure the file is not being edited somewhere else, "
			   "is not 'readonly' and you have the permissions to edit it."), *PackagePath);
	return false;
}

void FDiversionProvider::HandleUnsavedPackage(UPackage* Package, const FString& PackagePath, const FString& PackageName)
{
	// Notify only if the package isn't in the SyncInaccessiblePackages list
	if (GetSyncInaccessiblePackages(PackagePath))
	{
		return;
	}

	FNotificationButtonInfo KeepChangesButton(LOCTEXT("DiversionPopup_BackupMineButton", "Backup"),
		LOCTEXT("DiversionPopup_BackupMineButton_Tooltip",
			"Save a copy of your current changes to a new file with a `_resolved` postfix. The package will be closed, and both versions can be opened from the drawer for comparison."),
		FSimpleDelegate::CreateLambda([Package, PackagePath]() {
			if (!DiversionUtils::UPackageUtils::BackupUnsavedChanges(Package))
			{
				// Fallback to simple OS file copy -- Note, this makes the package undetectable by UE!
				// To use it you'll need to rename it back to the original file name (overriding the existing asset).
				// This should be used only as a last resort if copying the package with UE asset tools failed.
				DiversionUtils::UPackageUtils::MakeSimpleOSCopyPackageBackup(PackagePath, FPaths::GetPath(PackagePath));
			}
			RegisterCallbackRestoreAfterFileFinishedSyncing(PackagePath);

			// Actions needed to resume the syncing
			Package->SetDirtyFlag(false);
			DiversionUtils::UPackageUtils::ClosePackageEditor(Package);
	}),SNotificationItem::CS_Pending);

	FNotificationButtonInfo DiscardChangesButton(LOCTEXT("DiversionPopup_AcceptIncomingButton", "Accept incoming"),
	LOCTEXT("DiversionPopup_AcceptIncomingButton_Tooltip", "Discard your local changes and update the package with the latest version from the cloud."),
	FSimpleDelegate::CreateLambda([Package](){
		// Actions needed to resume the syncing
		Package->SetDirtyFlag(false);
		DiversionUtils::UPackageUtils::ClosePackageEditor(Package);
	}),SNotificationItem::CS_Pending);

	FText UnsavedChangesMessage = FText::Format(
		LOCTEXT("LockedFiles_UnsavedMessage",
				"The package \"{0}\" has been updated in the cloud repo. "
				"To protect your unsaved local changes, you can create a backup copy with a `_recovered` suffix. "
				"This backup allows you to compare your changes with the incoming version. "
				"Otherwise, your local changes will be overwritten by the update."),
		FText::FromString(PackageName));

	FDiversionNotification PopUpNotification(
		UnsavedChangesMessage,
		TArray<FNotificationButtonInfo>({KeepChangesButton, DiscardChangesButton}),
		SNotificationItem::CS_Pending,
		/*bSticky*/ true
	);

	AddAndNotifySyncInaccessiblePackage(PackagePath, PopUpNotification);
	UE_LOG(LogSourceControl, Warning, TEXT("%s"), *UnsavedChangesMessage.ToString());
}

void FDiversionProvider::HandleOpenedInEditorPackage(UPackage* Package, const FString& PackagePath, const FString& PackageName)
{
	// Notify only if the package isn't in the SyncInaccessiblePackages list
	if (GetSyncInaccessiblePackages(PackagePath))
	{
		return;
	}

	FNotificationButtonInfo CloseButton(LOCTEXT("DiversionPopup_ClosePackageButton", "Close tabs"),
										LOCTEXT("DiversionPopup_ClosePackageButton_Tooltip", "Close the package opened editor tabs to continue syncing"),
										FSimpleDelegate::CreateLambda([Package]()
										{
											DiversionUtils::UPackageUtils::ClosePackageEditor(Package);
										}),
										SNotificationItem::CS_Pending);
	FText OpenedChangesMessage =  FText::Format(
		LOCTEXT("LockedFiles_OpenedMessage",
				"'{0}' is currently opened in the editor view - Close it in order to continue syncing"),
		FText::FromString(PackageName));

	FDiversionNotification PopUpNotification(
		OpenedChangesMessage,
		TArray<FNotificationButtonInfo>({CloseButton}),
		SNotificationItem::CS_Pending,
		/*bSticky*/ true
	);
	AddAndNotifySyncInaccessiblePackage(PackagePath, PopUpNotification);
	UE_LOG(LogSourceControl, Warning, TEXT("%s"), *OpenedChangesMessage.ToString());
}

void FDiversionProvider::AddAndNotifySyncInaccessiblePackage(const FString& Path, const FDiversionNotification& InNotificationInfo)
{
	FDiversionNotificationManager::FNotificationId NotificationId = NotificationManager.ShowNotification(InNotificationInfo);
	SyncInaccessiblePackages.Add(Path, FSyncInaccessiblePackages({NotificationId, FDateTime::Now()}));
}

void FDiversionProvider::RemoveSyncInaccessiblePackage(const FString& PackagePath)
{
	if(FSyncInaccessiblePackages* SyncInaccessiblePackage = SyncInaccessiblePackages.Find(PackagePath))
	{
		NotificationManager.DismissNotification(SyncInaccessiblePackage->NotificationId);
		SyncInaccessiblePackages.Remove(PackagePath);
	}
}

bool FDiversionProvider::GetSyncInaccessiblePackages(const FString& PackagePath)
{
	if(FSyncInaccessiblePackages* SyncInaccessiblePackage = SyncInaccessiblePackages.Find(PackagePath))
	{
		// If the package has been locked for too long, remove it from the list
		if(IsPackageExpired(*SyncInaccessiblePackage))
		{
			NotificationManager.DismissNotification(SyncInaccessiblePackage->NotificationId);
			SyncInaccessiblePackages.Remove(PackagePath);
			return false;
		}
		return true;
	}
	return false;
}

void FDiversionProvider::UpdateReadOnlyPathNotifications(const TMap<FString, FString>& InReadOnlyPaths)
{
	// The agent stopped reporting these paths, so they no longer block the sync
	for (auto It = ReadOnlyPathNotifications.CreateIterator(); It; ++It)
	{
		if (!InReadOnlyPaths.Contains(It.Key()))
		{
			NotificationManager.DismissNotification(It.Value());
			It.RemoveCurrent();
		}
	}

	for (const TPair<FString, FString>& ReadOnlyPath : InReadOnlyPaths)
	{
		if (ReadOnlyPathNotifications.Contains(ReadOnlyPath.Key))
		{
			continue;
		}

		const FText Message = FText::Format(
			LOCTEXT("ReadOnlyPath_Message", "Diversion can't sync incoming changes: {0}"),
			FText::FromString(ReadOnlyPath.Value));
		// Sticky until the path is writable again and the agent stops reporting it
		const FDiversionNotification Notification(Message, TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail, /*bSticky*/ true);
		ReadOnlyPathNotifications.Add(ReadOnlyPath.Key, NotificationManager.ShowNotification(Notification));
	}
}

bool FDiversionProvider::TryUnloadingOpenedPackage(const FString& InOpenedPackagePath)
{
	FString PackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(InOpenedPackagePath, PackageName))
	{
		// Using Try... variant - non-Try is fatal on paths with illegal chars (e.g. '.dv-conflict.uasset').
		UE_LOG(LogSourceControl, Log,
			TEXT("'%s' does not map to a valid package name, skipping unload"),
			*InOpenedPackagePath);
		return false;
	}

	UPackage* Package = DiversionUtils::UPackageUtils::PackageFromPath(InOpenedPackagePath);

	if (Package == nullptr)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("'%s' is not a valid package, skipping unload"),
			   *InOpenedPackagePath);
		return false;
	}

	if (Package->IsDirty())
	{
		HandleUnsavedPackage(Package, InOpenedPackagePath, PackageName);
		return false;
	}

	if (DiversionUtils::UPackageUtils::IsPackageOpenedInEditor(PackageName))
	{
		HandleOpenedInEditorPackage(Package, InOpenedPackagePath, PackageName);
		return false;
	}

	return ReleaseLockedPackage(Package, InOpenedPackagePath);
}

int FDiversionProvider::AddRestoreAfterSyncDelegateHandle()
{
	return DirectoryWatcherHandles.Add(FDelegateHandle());
}

FDelegateHandle& FDiversionProvider::GetRestoreAfterSyncDelegateHandle(int DelegateHandleIndex)
{
	return DirectoryWatcherHandles[DelegateHandleIndex];
}

void FDiversionProvider::InitializeUICommands()
{
	// Add to source control menu following the same pattern as existing Diversion menu
	const FToolMenuOwnerScoped SourceControlMenuOwner("DiversionCommitMenu");
	if (UToolMenu* SourceControlMenu = UToolMenus::Get()->ExtendMenu("StatusBar.ToolBar.SourceControl"))
	{
		if (SourceControlMenu)
		{
			MenuSectionName = FName("DiversionActions");
			FToolMenuSection& Section = SourceControlMenu->AddSection(MenuSectionName, LOCTEXT("DiversionRevisionControlActions", "Diversion"), FToolMenuInsert(NAME_None, EToolMenuInsertType::First));
			Section.AddMenuEntry(
				"DiversionCommitChanges",
				LOCTEXT("DiversionCommitChanges", "Commit Changes"),
				LOCTEXT("DiversionCommitChangesTooltip", "Opens a dialog with check in options for content and levels."),
				FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.Actions.Submit"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FDiversionProvider::ExecuteCommitChanges),
					FCanExecuteAction::CreateRaw(this, &FDiversionProvider::CanExecuteCommitChanges)
				)
			);
			Section.AddMenuEntry(
				"DiversionLockedFiles",
				LOCTEXT("DiversionLockedFiles", "Locked Files"),
				LOCTEXT("DiversionLockedFilesTooltip", "Shows every file currently locked in this repository, with its holder."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Lock"),
				FUIAction(
					FExecuteAction::CreateStatic(&SDiversionLockedFilesDialog::Show),
					FCanExecuteAction(),
					FIsActionChecked(),
					// Hidden rather than greyed on repos without hard locks - same fail-closed
					// self-gating as the Content Browser lock entries
					FIsActionButtonVisible::CreateRaw(this, &FDiversionProvider::ShouldCheckHardLocks)
				)
			);
			UE_LOG(LogSourceControl, Log, TEXT("Diversion: Added Commit Changes button to Source Control menu"));
		}
	}
}

void FDiversionProvider::ShutdownUICommands()
{
	// Remove our menu section during shutdown
	if (UToolMenus* ToolMenus = UToolMenus::Get())
	{
		if (UToolMenu* SourceControlMenu = ToolMenus->FindMenu("StatusBar.ToolBar.SourceControl"); SourceControlMenu && !MenuSectionName.IsNone())
		{
			SourceControlMenu->RemoveSection(MenuSectionName);
			UE_LOG(LogSourceControl, Log, TEXT("Diversion: Removed Commit Changes button from Source Control menu"));
		}
	}
}

void FDiversionProvider::ExecuteCommitChanges()
{
	// Use our custom commit flow that properly handles file gathering
	// without duplicates from GetCachedStateByPredicate
	FDiversionCommitHelper::ExecuteCommit();
}

bool FDiversionProvider::CanExecuteCommitChanges() const
{
	return FDiversionCommitHelper::CanExecuteCommit();
}

void FDiversionProvider::RegisterStatusBarWidget()
{
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDiversionProvider::RegisterStatusBarMenus));
}

void FDiversionProvider::RegisterStatusBarMenus()
{
	const FToolMenuOwnerScoped OwnerScoped("DiversionStatusBarMenu");
	UToolMenus* TM = UToolMenus::Get();

	// Use the same toolbar path as Plastic SCM plugin
	if (UToolMenu* ToolbarMenu = TM->ExtendMenu("LevelEditor.StatusBar.ToolBar"))
	{
		// Insert before SourceControl like Plastic SCM does
		FToolMenuSection& Section = ToolbarMenu->AddSection("DiversionStatus", FText::GetEmpty(), FToolMenuInsert("SourceControl", EToolMenuInsertType::Before));

		Section.AddEntry(FToolMenuEntry::InitWidget(
			"DiversionStatusWidget",
			SAssignNew(StatusPanelWidget, SDiversionStatusPanel),
			FText::GetEmpty(), true, false
		));
	}
	else
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Diversion: Could not find LevelEditor.StatusBar.ToolBar menu"));
	}
}

void FDiversionProvider::UnregisterStatusBarWidget()
{
	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->UnregisterOwnerByName("DiversionStatusBarMenu"); // clean up our specific menu entries
	}
	StatusPanelWidget.Reset();
}

#undef LOCTEXT_NAMESPACE
