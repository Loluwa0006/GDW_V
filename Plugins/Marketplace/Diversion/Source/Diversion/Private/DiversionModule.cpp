// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionModule.h"

#include "DiversionOperations.h"
#include "Features/IModularFeatures.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "RevisionControlStyle/RevisionControlStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "DiversionStyle.h"

#include "CustomWidgets/ConfirmationDialog.h"
#include "CustomWidgets/DiversionSceneOutlinerColumn.h"

#include "AssetToolsModule.h"
#include "SourceControlOperations.h"
#include "FileHelpers.h"

// Enable plugin config
#include "ISettingsModule.h"
#include "DiversionConfig.h"
#include "DiversionAPIAccess.h"

#define LOCTEXT_NAMESPACE "Diversion"

template<typename Type>
static TSharedRef<IDiversionWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShareable(new Type());
}

namespace
{
	// Allowlist of commandlets that need SCC. Requires -EnableSCC, the flag
	// the commandlet uses to actually touch source control.
	bool CommandletNeedsSourceControl()
	{
		FString RunValue;
		if (!FParse::Value(FCommandLine::Get(), TEXT("-run="), RunValue))
		{
			return false;
		}
		// UE accepts both the short name (-run=GatherText) and the full class name
		// (-run=GatherTextCommandlet); normalize before comparing.
		RunValue.RemoveFromEnd(TEXT("Commandlet"), ESearchCase::IgnoreCase);
		const bool bIsAllowedCommandlet = RunValue.Equals(TEXT("GatherText"), ESearchCase::IgnoreCase);
		if (!bIsAllowedCommandlet)
		{
			return false;
		}
		return FParse::Param(FCommandLine::Get(), TEXT("EnableSCC"));
	}

	// An unattended editor run that asks for the plugin anyway with -DiversionEnableUnattended,
	// such as automation tests that need the live provider. Never a commandlet - those go
	// through the allowlist above.
	bool UnattendedEditorOptedIn()
	{
		return !IsRunningCommandlet() && FParse::Param(FCommandLine::Get(), TEXT("DiversionEnableUnattended"));
	}
}

void FDiversionModule::StartupModule()
{
	// Skip initialization in commandlet mode (e.g., during cook/build).
	// Diversion is a UI extension that's not needed in non-interactive modes,
	// except for the small set of commandlets that explicitly require SCC
	// and unattended editor runs that opt in.
	if ((FApp::IsUnattended() || IsRunningCommandlet()) && !CommandletNeedsSourceControl() && !UnattendedEditorOptedIn())
	{
		return;
	}

	// Mark as initialized - set early to preserve existing shutdown behavior
	// (if initialization fails partway, shutdown should still clean up)
	bIsInitialized = true;

	ServiceStatusLock =  MakeUnique<FRWLock>();

	// Initialize static API access - this creates and owns all API managers
	// Worker threads will access APIs through this static interface
	FDiversionAPIAccess::Initialize(OriginalAccountID);

	// Register our operations
	// Note: this provider does not uses the "CheckOut" command, which is a Perforce "lock", as Diversion has no lock command (all tracked files in the working copy are always already checked-out).
	DiversionProvider.RegisterWorker("Connect", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionConnectWorker>));
	DiversionProvider.RegisterWorker("UpdateStatus", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionUpdateStatusWorker>));
	DiversionProvider.RegisterWorker("MarkForAdd", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionMarkForAddWorker>));
	DiversionProvider.RegisterWorker("Delete", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionDeleteWorker>));
	DiversionProvider.RegisterWorker("Copy", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionCopyWorker>));
	DiversionProvider.RegisterWorker("CheckIn", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionCheckInWorker>));
	DiversionProvider.RegisterWorker("Revert", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionRevertWorker>));
	DiversionProvider.RegisterWorker("Resolve", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionResolveWorker>));
	DiversionProvider.RegisterWorker("Sync", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionSyncWorker>));
	DiversionProvider.RegisterWorker("UpdateChangelistsStatus", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionGetPendingChangelistsWorker>));

	// Unsupported operations (irrelevant for diversion)
	DiversionProvider.RegisterWorker("NewChangelist", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("Shelve", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("Unshelve", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("DeleteChangelist", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("EditChangelist", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("RevertUnchanged", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("MoveToChangelist", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));
	DiversionProvider.RegisterWorker("DeleteShelved", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionOperationNotSupportedWorker>));


	// Diversion Operations workers
	DiversionProvider.RegisterWorker("SendAnalytics", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionAnalyticsEventWorker>));
	DiversionProvider.RegisterWorker("AgentHealthCheck", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionAgentHealthCheckWorker>));
	DiversionProvider.RegisterWorker("GetWsInfo", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionWsInfoWorker>));
	DiversionProvider.RegisterWorker("InitRepo", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionInitRepoWorker>));
	DiversionProvider.RegisterWorker("FinalizeMerge", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionFinalizeMergeWorker>));
	DiversionProvider.RegisterWorker("ResolveFile", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionResolveFileWorker>));
	DiversionProvider.RegisterWorker("UpdateWorkspace", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionUpdateWorkspaceWorker>));
	DiversionProvider.RegisterWorker("GetPotentialClashes", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionGetPotentialClashes>));
	DiversionProvider.RegisterWorker("GetFileLocks", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionGetFileLocksWorker>));
	DiversionProvider.RegisterWorker("UpdateFileLock", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionUpdateFileLockWorker>));
	DiversionProvider.RegisterWorker("GetHardLocksEligibility", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionGetHardLocksEligibilityWorker>));
	DiversionProvider.RegisterWorker("GetConflictedFiles", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionGetConflictedFiles>));
	DiversionProvider.RegisterWorker("CheckIfWorkspaceExistsInPath", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionCheckIfWorkspaceExistsInPathWorker>));
	DiversionProvider.RegisterWorker("CheckForRepoWithSameName", FGetDiversionWorker::CreateStatic(&CreateWorker<FDiversionCheckForRepoWithSameNameWorker>));
	// load our settings
	DiversionSettings.LoadSettings();

	// Bind our version control provider to the editor
	IModularFeatures::Get().RegisterModularFeature("SourceControl", &DiversionProvider);
	OpenedEditorAssets.Empty();

	if(ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "Diversion",
			LOCTEXT("DiversionSettingsName", "Diversion"),
			LOCTEXT("DiversionSettingsDescription", "Configure the Diversion plugin"),
			GetMutableDefault<UDiversionConfig>());
	}

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnAssetEditorOpened().AddRaw(this,
		&FDiversionModule::HandleAssetOpenedInEditor);

	// Initialize Diversion style set for icons
	FDiversionStyle::Initialize();

	// Register Scene Outliner column for potential clash indicators
	FDiversionPotentialClashColumn::RegisterColumnType();
}

void FDiversionModule::ShutdownModule()
{
	// Skip cleanup if we never initialized (commandlet mode)
	if (!bIsInitialized)
	{
		return;
	}

	// Fail in-flight HTTP requests fast so the drains below don't block on long-running calls.
	// Only here, on the teardown path, not in Provider::Close() - the signal is a one-way latch
	// and Close() runs on every provider toggle, which must leave the reused clients usable.
	FDiversionAPIAccess::BeginShutdown();

	// Shut down the provider first - this ensures all worker threads are completed
	DiversionProvider.Close();

	// Now it's safe to clear static API access after all workers have finished
	FDiversionAPIAccess::Shutdown();

	if(ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		// Unregister Diversion settings
		SettingsModule->UnregisterSettings("Project", "Plugins", "Diversion");
	}

	// Shutdown Diversion style set
	FDiversionStyle::Shutdown();

	// Unregister Scene Outliner column
	FDiversionPotentialClashColumn::UnregisterColumnType();

	// unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &DiversionProvider);
}


void FDiversionModule::SaveSettings()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	DiversionSettings.SaveSettings();
}

FDiversionModule& FDiversionModule::Get()
{
	return FModuleManager::GetModuleChecked<FDiversionModule>("Diversion");
}

bool FDiversionModule::IsLoaded()
{
	return FModuleManager::Get().IsModuleLoaded("Diversion");
}

const TSharedPtr<IPlugin> FDiversionModule::GetPlugin()
{
	return IPluginManager::Get().FindPlugin(TEXT("Diversion"));;
}

FString FDiversionModule::GetPluginVersion()
{
	auto Plugin = GetPlugin();
	if (!Plugin.IsValid())
	{
		return "N/a";
	}
	return Plugin->GetDescriptor().VersionName;
}

void FDiversionModule::HandleAssetOpenedInEditor(UObject* Asset)
{
	/**
	* OnAssetEditorOpened
	* This is an event response that is fired once an asset was finished opening (but not yet presented to the screen).
	*
	* Function flow:
	* Open the asset for the first time and try to figure out if we need to show confirmation dialog
	* In case yes - show the dialog
	* If the Asset can be opened as read-only (Decided by UE), there will be a "read-only" button in the dialog
	* If the user clicked on "read-only" - open the asset as read-only and add it to the set of opened assets.
	* In order to perform "read-only" opening, the asset editor window needs to be closed first.
	* Since we don't want to show the dialog again when opening for read-only,
	* we need to filter out the re-opening of the asset editor window.
	* Which is done by the code:
	* 	OpenedEditorAssets.Add(AssetPath);
	* And filtered by:
	* 	bool IsReopeningWindowAsReadOnly = OpenedEditorAssets.Contains(AssetPath);
	*	if (IsReopeningWindowAsReadOnly) {
	*		OpenedEditorAssets.Remove(AssetPath);
	*		return;
	*	}
	* In the function beginning.
	* In all other cases, either close the window (user chose cancel or X button)
	* or open the asset for editing (user chose "Open For Edit").
	 */
	if (Asset == nullptr)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Editor opening event received an empty asset"));
		return;
	}

	// This handler stays bound for the module's lifetime, regardless of which revision-control
	// provider is currently active. Bail out unless Diversion is the active, available provider -
	// the lock/clash checks below fire synchronous API calls and cast the active provider's
	// states to FDiversionState, which is only valid when the active provider is ours.
	// This is the single gate for both flows; the check functions rely on it as a precondition.
	if (!ISourceControlModule::Get().IsEnabled() ||
		&ISourceControlModule::Get().GetProvider() != static_cast<ISourceControlProvider*>(&DiversionProvider) ||
		!DiversionProvider.IsAvailable())
	{
		return;
	}

	// Filter out the re-opening of the asset editor window
	// Since this is event response to file that already been opened (implied by the delegate call)
	// I need to know when a reopening event was triggered by the plugin code and ignore it
	// (Not the user-induced original opening of the asset editor).
	// This adds it to a set that enables me to decide if the editor was re-opened,
	// so ignoring it in the function start,
	// or was it originally opened by the user, so continue normally.
	// Shared by both the hard-lock and the soft-lock dialogs.
	FString AssetPath = Asset->GetPathName();
	bool IsReopeningWindowAsReadOnly = OpenedEditorAssets.Contains(AssetPath);
	if (IsReopeningWindowAsReadOnly)
	{
		OpenedEditorAssets.Remove(AssetPath);
		return;
	}

	// Hard lock takes precedence over the soft-lock confirmation and cannot be disabled by settings
	FDiversionLockInfo LockInfo;
	if (HardLockedByOtherForAsset(Asset, LockInfo))
	{
		ShowHardLockDialog(Asset, LockInfo);
		return;
	}

	if(!IsDiversionSoftLockEnabled())
	{
		// The user disabled this feature
		// This is to avoid unnecessary API call to
		// get potential conflicts
		return;
	}

	// Understand if this is a potential conflicted asset
	if(!PotentialConflictExistForAsset(Asset))
	{
		return;
	}

	ShowPotentialConflictConfirmationDialog(Asset);
}

bool FDiversionModule::HardLockedByOtherForAsset(UObject* Asset, FDiversionLockInfo& OutLockInfo)
{
	// Precondition (enforced by HandleAssetOpenedInEditor): Diversion is the active,
	// available revision-control provider.
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	auto& Provider = FDiversionModule::Get().GetProvider();
	// Skip the lock query entirely on repos whose tier can't have hard locks
	if (!Provider.ShouldCheckHardLocks())
	{
		return false;
	}

	// Always query the backend - cached lock state must never gate an open decision
	FString FilePath = FPaths::ConvertRelativePathToFull(DiversionUtils::GetFilePathFromAssetData(FAssetData(Asset)));
	if (!Provider.RefreshFileLocks({FilePath}))
	{
		// The backend couldn't confirm - fall through to the CACHED lock state instead of
		// failing open blindly: hard locks outrank every soft surface, so a cached "locked
		// by other" must still show the hard-lock dialog (slightly stale holder data beats
		// the wrong dialog). A cached "unlocked" opens normally and save/commit-time
		// enforcement still protects.
		UE_LOG(LogSourceControl, Warning,
			TEXT("Failed to verify hard-lock state for %s - falling back to the cached state"), *FilePath);
	}

	UPackage* CurPackage = Asset->GetOutermost();
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(CurPackage, EStateCacheUsage::Use);
	TSharedPtr<FDiversionState> DiversionState = StaticCastSharedPtr<FDiversionState>(SourceControlState);
	if(!DiversionUtils::DiversionValidityCheck(DiversionState.IsValid(),
		"Failed converting SourceControlState to DiversionState",
		GetOriginalAccountID()))
	{
		return false;
	}

	if (!DiversionState->IsHardLockedByOther())
	{
		return false;
	}

	OutLockInfo = DiversionState->FileLock.GetLockInfo();
	return true;
}

bool FDiversionModule::CanOpenAssetReadOnly(UObject* Asset)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CanOpenEditorForAsset(Asset,
		EAssetTypeActivationOpenedMethod::View, nullptr);
#else
	// The function CanOpenEditorForAsset is not available in UE 5.3 and below - default to false
	return false;
#endif
}

TSharedRef<SWindow> FDiversionModule::MakeAssetOpenDialogWindow(const FText& InTitle)
{
	return SNew(SWindow)
		.Title(InTitle)
		.SizingRule(ESizingRule::Autosized)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false).SupportsMaximize(false);
}

TArray<TSharedRef<FConfirmationOption>> FDiversionModule::MakeReadOnlyButton(bool bInAssetCanBeReadOnly)
{
	TArray<TSharedRef<FConfirmationOption>> Buttons;
	if (bInAssetCanBeReadOnly)
	{
		Buttons.Add(MakeShared<FConfirmationOption>(LOCTEXT("AssetOpenDialog_ReadOnly", "Read Only"), static_cast<uint32>(EAssetOpenDialogChoice::AssetOpenDialog_ReadOnly),
			&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"), false));
	}
	return Buttons;
}

TSharedRef<FConfirmationAdditionalLink> FDiversionModule::MakeFeedbackLink(const FText& InTooltip,
	const FString& InFeedbackSource)
{
	return MakeShared<FConfirmationAdditionalLink>(
		LOCTEXT("Feedback_Text", "Feedback"),
		InTooltip,
		[InFeedbackSource]()
		{
			FPlatformProcess::LaunchURL(TEXT("https://discord.com/invite/wSJgfsMwZr"), nullptr, nullptr);
			SendDialogAnalytics("UE Feedback", {{"feedback_source", InFeedbackSource}});
		});
}

FString FDiversionModule::ApplyAssetOpenChoice(UObject* Asset, EAssetOpenDialogChoice InChoice)
{
	switch (InChoice)
	{
	case EAssetOpenDialogChoice::AssetOpenDialog_ReadOnly:
		// Reopening as read-only re-enters HandleAssetOpenedInEditor; record the path so that
		// second pass knows to ignore it rather than raising the same dialog again
		OpenedEditorAssets.Add(Asset->GetPathName());
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Asset);
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset,
			EToolkitMode::Standalone, nullptr, true, EAssetTypeActivationOpenedMethod::View);
		return TEXT("Read Only");

	case EAssetOpenDialogChoice::AssetOpenDialog_Proceed:
		// Keep the already-opened editor; lock enforcement happens at save/commit time
		return TEXT("Proceed");

	case EAssetOpenDialogChoice::AssetOpenDialog_XButton:
	case EAssetOpenDialogChoice::AssetOpenDialog_Close:
	default:
		if (Asset->IsA<UWorld>())
		{
			// A world is already open at this point - there is nothing to "close" back to, so
			// schedule a load of the default map instead
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float DeltaTime)
			{
				FEditorFileUtils::LoadDefaultMapAtStartup();
				return false; // Don't repeat
			}), 0.0f);
		}
		else
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Asset);
		}
		return TEXT("Close");
	}
}

void FDiversionModule::SendDialogAnalytics(const FString& InEventName, const TMap<FString, FString>& InProperties)
{
	auto SendAnalyticsOperation = ISourceControlOperation::Create<FSendAnalytics>();
	SendAnalyticsOperation->SetEventName(FText::FromString(InEventName));
	SendAnalyticsOperation->SetEventProperties(InProperties);
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.Execute(SendAnalyticsOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);
}

void FDiversionModule::ShowHardLockDialog(UObject* Asset, const FDiversionLockInfo& LockInfo)
{
	const bool AssetCanBeReadOnly = CanOpenAssetReadOnly(Asset);

	FString AssetName = Asset->GetName();
	FString AssetPath = Asset->GetOutermost()->GetPathName();
	// Making sure to show a value
	AssetPath = !AssetPath.IsEmpty() ? AssetPath : AssetName;

	const FText LockedSince = DiversionUtils::FormatUnixTimestamp(LockInfo.LockedAt);

	FText Message = FText::Format(LOCTEXT("HardLockMessage",
		"{0} is locked\n\nLocked by: {1}\nSince: {2}\nAsset: {3}\n\nYou can open and edit it, and saves sync to your workspace as usual.\nCommitting this file stays blocked until it is unlocked by its holder or a repository admin."),
		FText::FromString(AssetName), LockInfo.GetHolderText(), LockedSince, FText::FromString(AssetPath));

	TSharedRef<SWindow> DialogWindow = MakeAssetOpenDialogWindow(
		LOCTEXT("DiversionHardLockTitle", "Diversion - File Locked"));

	// Opening is never blocked - the lock is enforced at save/commit time. "Open" proceeds
	// with the normal Edit open (read-only View is offered above only for asset types that
	// support it). There is deliberately no disable link - a hard lock has no in-editor override.
	TArray<TSharedRef<FConfirmationOption>> Buttons = MakeReadOnlyButton(AssetCanBeReadOnly);
	Buttons.Append({
		MakeShared<FConfirmationOption>(LOCTEXT("AssetOpenDialog_Open", "Open"), static_cast<uint32>(EAssetOpenDialogChoice::AssetOpenDialog_Proceed),
			AssetCanBeReadOnly ? nullptr : &FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"),
			false),
		MakeShared<FConfirmationOption>(LOCTEXT("AssetOpenDialog_Close", "Close"), static_cast<uint32>(EAssetOpenDialogChoice::AssetOpenDialog_Close),
			nullptr, false)
	});

	TArray<TSharedRef<FConfirmationAdditionalLink>> AdditionalLinks = {
		MakeShared<FConfirmationAdditionalLink>(
		LOCTEXT("HardLockDocs_Text", "Learn More"),
		LOCTEXT("HardLockDocs_Tooltip", "Open the Diversion documentation to learn more about hard locks"),
		[]()
		{
			FPlatformProcess::LaunchURL(TEXT("https://docs.diversion.dev/hard-locks?utm_source=ue-plugin&utm_medium=plugin#hard-locks"), nullptr, nullptr);
			SendDialogAnalytics("UE HardLock Docs Clicked", {});
		}
		),
		MakeFeedbackLink(
			LOCTEXT("Feedback_Tooltip", "Your feedback is valuable to us! If you are having issues please let us know."),
			"hard_lock_confirmation")
	};

	TSharedRef<SConfirmationDialog> ConfirmationDialog = SNew(SConfirmationDialog)
		.ParentWindow(DialogWindow)
		.Message(Message)
		.Buttons(Buttons)
		.AdditionalLinks(AdditionalLinks);

	// Open ConfirmationDialog
	DialogWindow->SetContent(
		ConfirmationDialog
	);

	FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
	// "Open" is this dialog's proceed action, so report it under its own label
	const EAssetOpenDialogChoice Choice = static_cast<EAssetOpenDialogChoice>(ConfirmationDialog->GetResult());
	FString SelectedActionText = ApplyAssetOpenChoice(Asset, Choice);
	if (Choice == EAssetOpenDialogChoice::AssetOpenDialog_Proceed)
	{
		SelectedActionText = TEXT("Open");
	}
	SendDialogAnalytics("UE HardLock Dialog Interacted", {{"chosen_action", SelectedActionText}});

	DialogWindow->RequestDestroyWindow();
}

bool FDiversionModule::PotentialConflictExistForAsset(UObject* Asset)
{
	// Precondition (enforced by HandleAssetOpenedInEditor): Diversion is the active,
	// available revision-control provider.
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	// Run update status to get the most recent file state from the backend
	FString FilePath = FPaths::ConvertRelativePathToFull(DiversionUtils::GetFilePathFromAssetData(FAssetData(Asset)));
	auto& Provider = FDiversionModule::Get().GetProvider();
	Provider.Execute(ISourceControlOperation::Create<FGetPotentialClashes>(), nullptr,
		{FilePath},
		EConcurrency::Synchronous);

	// Try getting Assets Diversion state
	UPackage* CurPackage = Asset->GetOutermost();
	FSourceControlStatePtr SourceControlState = SourceControlProvider.GetState(CurPackage, EStateCacheUsage::Use);
	TSharedPtr<FDiversionState> DiversionState = StaticCastSharedPtr<FDiversionState>(SourceControlState);
	if(!DiversionUtils::DiversionValidityCheck(DiversionState.IsValid(),
		"Failed converting SourceControlState to DiversionState",
		GetOriginalAccountID()))
	{
		return false;
	}

	// Hard locks outrank the soft-clash dialog on every surface: a hard-locked-by-other
	// file must never fall into the soft confirmation (IsCheckedOutOther reports hard
	// locks too; the hard-lock dialog was already offered upstream)
	if (DiversionState->IsHardLockedByOther() || !DiversionState->IsCheckedOutOther())
	{
		return false;
	}

	return true;
}

void FDiversionModule::ShowPotentialConflictConfirmationDialog(UObject* Asset)
{
	const bool AssetCanBeReadOnly = CanOpenAssetReadOnly(Asset);

	FString AssetName = Asset->GetName();
	FString AssetPath = Asset->GetOutermost()->GetPathName();
	// Making sure to show a value
	AssetPath = !AssetPath.IsEmpty() ? AssetPath : AssetName;

	FText Message = FText::Format(LOCTEXT("AutoSoftLockMessage",
		"Trying to open: \"{0}\"\n\nA newer version of this asset exists in another workspace and it was automatically soft-locked. The file can be opened for viewing and editing, but if changes are saved a conflict might be created - proceed with caution.\n\nTo see who is editing this file, close this dialog and hover the file in the Content Drawer."),
		 FText::FromString(AssetPath));

	TSharedRef<SWindow> DialogWindow = MakeAssetOpenDialogWindow(
		LOCTEXT("DiversionPotentialClashTitle", "Diversion - Auto Soft-Locked Asset"));

	TArray<TSharedRef<FConfirmationOption>> Buttons = MakeReadOnlyButton(AssetCanBeReadOnly);
	Buttons.Append({
		MakeShared<FConfirmationOption>(LOCTEXT("AssetOpenDialog_Close", "Close"), static_cast<uint32>(EAssetOpenDialogChoice::AssetOpenDialog_Close),
			nullptr, false),
		MakeShared<FConfirmationOption>(LOCTEXT("AssetOpenDialog_OpenForEdit", "Open For Edit"), static_cast<uint32>(EAssetOpenDialogChoice::AssetOpenDialog_Proceed),
			&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton.Danger"),
			true)
	});

	TArray<TSharedRef<FConfirmationAdditionalLink>> AdditionalLinks = {
		MakeFeedbackLink(
			LOCTEXT("SoftLockFeedback_Tooltip", "This is an experimental feature. Your feedback is valuable to us! If you are having issues please let us know."),
			"soft_lock_confirmation"),
		MakeShared<FConfirmationAdditionalLink>(
		LOCTEXT("Disabled_Text", "Disable"),
		LOCTEXT("Disabled_Tooltip", "To disable this feature, click here to open the settings dialog and uncheck \"Enable Diversion Auto Soft Lock Confirmations\""),
		[DialogWindow](){
				// Close the current dialog to enable the settings dialog opening
				DialogWindow->RequestDestroyWindow();

				SendDialogAnalytics("UE Disable SoftLock Clicked", {});

				// Open the settings dialog
				ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
				if (SettingsModule)
				{
					SettingsModule->ShowViewer("Project", "Plugins", "Diversion");
				}
			}
		)
	};

	TSharedRef<SConfirmationDialog> ConfirmationDialog = SNew(SConfirmationDialog)
		.ParentWindow(DialogWindow)
		.Message(Message)
		.Buttons(Buttons)
		.AdditionalLinks(AdditionalLinks)
		.ExtraConfirmationText(LOCTEXT("ConflictedFileOpen_ExtraConfirmation", "I understand that editing this file may create a conflict"))
		.DefaultExtraConfirmState(&bConflictedFileOpenConfirmCheckboxState);

	// Open ConfirmationDialog
	DialogWindow->SetContent(
		ConfirmationDialog
	);

	FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
	// "Open For Edit" is this dialog's proceed action, so report it under its own label
	const EAssetOpenDialogChoice Choice = static_cast<EAssetOpenDialogChoice>(ConfirmationDialog->GetResult());
	FString SelectedActionText = ApplyAssetOpenChoice(Asset, Choice);
	if (Choice == EAssetOpenDialogChoice::AssetOpenDialog_Proceed)
	{
		SelectedActionText = TEXT("Open For Edit");
	}
	SendDialogAnalytics("UE SoftLock Dialog Interacted", {{"chosen_action", SelectedActionText},
		{"save_checkbox_state", bConflictedFileOpenConfirmCheckboxState == ECheckBoxState::Checked ? "checked" : "unchecked"}});

	DialogWindow->RequestDestroyWindow();
}

IMPLEMENT_MODULE(FDiversionModule, Diversion);

#undef LOCTEXT_NAMESPACE
