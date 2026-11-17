// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Curves/CurveLinearColor.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "FileHelpers.h"
#include "PackageTools.h"
#include "ISourceControlModule.h"
#include "DiversionModule.h"
#include "DiversionProvider.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif

namespace
{
	/** The asset's whole content is one color, like the Material in the customer's report */
	void SetColor(UCurveLinearColor* InCurve, const FLinearColor& InColor)
	{
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			InCurve->FloatCurves[Channel].Reset();
			InCurve->FloatCurves[Channel].AddKey(0.f, InColor.Component(Channel));
		}
		InCurve->GetPackage()->SetDirtyFlag(true);
	}

	bool SavePackageTo(UPackage* InPackage, UObject* InAsset, const FString& InFilename)
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		// A save the gate refuses reports to this device - a warning, as in the editor's own saves
		SaveArgs.Error = GWarn;
		return UPackage::SavePackage(InPackage, InAsset, *InFilename, SaveArgs);
	}

	/**
	 * Moves InSource over InDest the way the agent's sync puts a downloaded file in place
	 * (RealFSContainer::OnWorkspaceChange): Go's os.Rename, which is MoveFileEx with
	 * REPLACE_EXISTING on Windows and rename(2) elsewhere. Retried briefly, because the agent
	 * or a virus scanner may be reading a file this test has just written; a handle the editor
	 * keeps open fails every attempt.
	 */
	bool ReplaceLikeTheAgent(const FString& InDest, const FString& InSource, uint32& OutLastError)
	{
		OutLastError = 0;
		for (int32 Attempt = 0; Attempt < 20; ++Attempt)
		{
#if PLATFORM_WINDOWS
			if (::MoveFileExW(*FPaths::ConvertRelativePathToFull(InSource).Replace(TEXT("/"), TEXT("\\")),
				*FPaths::ConvertRelativePathToFull(InDest).Replace(TEXT("/"), TEXT("\\")), MOVEFILE_REPLACE_EXISTING))
#else
			if (FPlatformFileManager::Get().GetPlatformFile().MoveFile(*InDest, *InSource))
#endif
			{
				return true;
			}
			OutLastError = FPlatformMisc::GetLastError();
			FPlatformProcess::Sleep(0.1f);
		}
		return false;
	}

	/** Runs dv in the workspace root, as a person at a terminal would */
	bool RunDv(const FString& InWorkspaceRoot, const FString& InArgs, FString& OutOutput)
	{
		int32 ReturnCode = -1;
		FString StdErr;
		const bool bRan = FPlatformProcess::ExecProcess(TEXT("dv"), *InArgs, &ReturnCode, &OutOutput, &StdErr, *InWorkspaceRoot);
		OutOutput += StdErr;
		return bRan && ReturnCode == 0;
	}

	/** Polls InCondition until it holds; false once InTimeoutSeconds pass */
	bool WaitUntil(double InTimeoutSeconds, TFunctionRef<bool()> InCondition)
	{
		const double Deadline = FPlatformTime::Seconds() + InTimeoutSeconds;
		while (!InCondition())
		{
			if (FPlatformTime::Seconds() > Deadline)
			{
				return false;
			}
			FPlatformProcess::Sleep(0.25f);
		}
		return true;
	}

	/** Where a file waits before it is moved in or uploaded. Saved/ is ignored by the workspace. */
	FString StagingPathFor(const FString& InFilename)
	{
		return FPaths::ConvertRelativePathToFull(
			FPaths::ProjectSavedDir() / TEXT("DiversionTests") / FPaths::GetCleanFilename(InFilename));
	}

	/** A one-color asset under Content for one test run, removed again when this goes out of scope */
	struct FScratchAsset
	{
		FString PackageName;
		FString Filename;
		UPackage* Package = nullptr;
		UCurveLinearColor* Asset = nullptr;

		FScratchAsset()
		{
			// A fresh name per run: a package from an earlier run may still be in memory
			PackageName = FString::Printf(TEXT("/Game/DiversionTests/ChangedOnDisk_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			Filename = FPaths::ConvertRelativePathToFull(
				FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()));
			Package = CreatePackage(*PackageName);
			Asset = NewObject<UCurveLinearColor>(Package, *FPackageName::GetShortName(PackageName),
				RF_Public | RF_Standalone);
		}

		~FScratchAsset()
		{
			// The agent tracks everything under Content - leave nothing behind, conflict
			// backups included
			if (Package != nullptr)
			{
				Package->SetDirtyFlag(false);
				UPackageTools::UnloadPackages({ Package });
			}
			const FString Directory = FPaths::GetPath(Filename);
			for (const FString& Leftover : FindFilesNamedLikeThis())
			{
				IFileManager::Get().Delete(*(Directory / Leftover), false, true, true);
			}
			IFileManager::Get().DeleteDirectory(*Directory);
			const FString StagingFilename = StagingPathFor(Filename);
			IFileManager::Get().Delete(*StagingFilename, false, true, true);
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(StagingFilename));
		}

		bool Save() const
		{
			return SavePackageTo(Package, Asset, Filename);
		}

		/** Unloads the package and loads it again from disk, as an editor that opens the asset fresh */
		bool LoadFromDisk()
		{
			Package->SetDirtyFlag(false);
			UPackageTools::UnloadPackages({ Package });
			Package = nullptr;
			Asset = nullptr;
			if (FindPackage(nullptr, *PackageName) != nullptr)
			{
				// Still in memory, so LoadPackage would not read the file
				return false;
			}
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			Asset = Package != nullptr
				? FindObject<UCurveLinearColor>(Package, *FPackageName::GetShortName(PackageName))
				: nullptr;
			return Asset != nullptr;
		}

		/** The asset's file and any file next to it that starts with its name, such as a .dv-conflict backup */
		TArray<FString> FindFilesNamedLikeThis() const
		{
			TArray<FString> Found;
			IFileManager::Get().FindFiles(Found,
				*(FPaths::GetPath(Filename) / (FPaths::GetBaseFilename(Filename) + TEXT("*"))), true, false);
			return Found;
		}
	};

	/** R1 once the provider is connected (see FSaveDecisionChangedOnDiskIntegration) */
	void RunChangedOnDiskScenario(FAutomationTestBase& Test, FDiversionProvider& Provider)
	{
		FScratchAsset Scratch;
		FString WorkspaceRoot = Provider.GetPathToWorkspaceRoot();
		FPaths::NormalizeDirectoryName(WorkspaceRoot);
		FString WorkspacePath = Scratch.Filename;
		if (!Test.TestTrue(TEXT("Setup: the asset is inside the workspace"),
			FPaths::MakePathRelativeTo(WorkspacePath, *(WorkspaceRoot + TEXT("/"))) && !WorkspacePath.StartsWith(TEXT(".."))))
		{
			return;
		}
		const auto IsSynced = [&WorkspaceRoot, &WorkspacePath]()
		{
			FString Output;
			return RunDv(WorkspaceRoot, FString::Printf(TEXT("status \"%s\""), *WorkspacePath), Output)
				&& Output.Contains(TEXT(": Synced"));
		};

		// The teammate's yellow, uploaded below. Made first, so the package's saved hash comes
		// from the shared version B loads, as in B's editor.
		const FString IncomingFilename = StagingPathFor(Scratch.Filename);
		SetColor(Scratch.Asset, FLinearColor::Yellow);
		TArray<uint8> IncomingBytes;
		if (!Test.TestTrue(TEXT("Setup: the teammate's version is written"),
			SavePackageTo(Scratch.Package, Scratch.Asset, IncomingFilename)
				&& FFileHelper::LoadFileToArray(IncomingBytes, *IncomingFilename)))
		{
			return;
		}

		// Both workspaces have it black. The agent uploads it; its per-file status only covers
		// changes it has noticed, so its file watcher gets a moment first.
		SetColor(Scratch.Asset, FLinearColor::Black);
		if (!Test.TestTrue(TEXT("Setup: the shared version is on disk"), Scratch.Save()))
		{
			return;
		}
		FPlatformProcess::Sleep(5.f);
		if (!Test.TestTrue(TEXT("Setup: the agent uploads the shared version"), WaitUntil(120.0, IsSynced)))
		{
			return;
		}

		// B opens the asset and edits it without saving. Apply in the Material Editor copies the
		// edit into the real package and resets its loaders (UpdateOriginalMaterial), which
		// releases the file without a save - so nothing is pending in B's workspace either.
		if (!Test.TestTrue(TEXT("Setup: B opens the asset"), Scratch.LoadFromDisk()))
		{
			return;
		}
		SetColor(Scratch.Asset, FLinearColor(0.5f, 0.8f, 1.f));
		ResetLoaders(Scratch.Package);

		// The teammate's change arrives from the cloud, and B's agent writes it to disk. The wait
		// reads the timestamp only: an open handle here would block the agent's move.
		const FDateTime BeforeIncoming = IFileManager::Get().GetTimeStamp(*Scratch.Filename);
		FString AddOutput;
		if (!Test.TestTrue(TEXT("Setup: dv add uploads the teammate's version to the workspace"),
			RunDv(WorkspaceRoot, FString::Printf(TEXT("add --local_path \"%s\" --path \"%s\""),
				*IncomingFilename, *WorkspacePath), AddOutput)))
		{
			Test.AddInfo(AddOutput);
			return;
		}
		TArray<uint8> SyncedBytes;
		if (!Test.TestTrue(TEXT("Setup: the agent writes the teammate's version to disk"),
			WaitUntil(120.0, [&Scratch, &BeforeIncoming]()
			{
				return IFileManager::Get().GetTimeStamp(*Scratch.Filename) != BeforeIncoming;
			})
			&& WaitUntil(120.0, IsSynced)
			&& FFileHelper::LoadFileToArray(SyncedBytes, *Scratch.Filename)
			&& SyncedBytes == IncomingBytes))
		{
			return;
		}

		// B's change is only in memory, so the file matched what the agent last synced
		// (RealFSContainer::backupIfConflicting) and nothing was kept aside
		const TArray<FString> NextToAsset = Scratch.FindFilesNamedLikeThis();
		Test.TestEqual(TEXT("The agent overwrote the file without a conflict backup"), NextToAsset.Num(), 1);

		// Without this the save below never reaches the gate's checks and the test proves nothing
		if (!Test.TestTrue(TEXT("Setup: the save gate holds saves of this file"),
			Provider.GetSaveDecisionGate().IsGateableSave(Scratch.Package, Scratch.Filename)))
		{
			return;
		}

		// A save that never reaches SavePackage would pass the check below for the wrong reason
		if (!Test.TestTrue(TEXT("Setup: B's edit is still unsaved before the save"), Scratch.Package->IsDirty()))
		{
			return;
		}
		// Records what the plugin's save gate decides for this package, by wrapping it for the
		// duration of B's save
		const FCoreUObjectDelegates::FIsPackageOKToSaveDelegate Gate = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
		int32 GateCalls = 0;
		bool bGateAllowed = false;
		FCoreUObjectDelegates::IsPackageOKToSaveDelegate.BindLambda(
			[&Gate, &Scratch, &GateCalls, &bGateAllowed](UPackage* InPackage, const FString& InFilename, FOutputDevice* InError)
			{
				const bool bAllowed = !Gate.IsBound() || Gate.Execute(InPackage, InFilename, InError);
				if (InPackage == Scratch.Package)
				{
					++GateCalls;
					bGateAllowed = bAllowed;
				}
				return bAllowed;
			});

		// B saves. The asset editor's Save button (PromptForCheckoutAndSave) cancels every save
		// while a test runs - FApp::IsUnattended counts GIsAutomationTesting - so this is the call
		// it makes for unattended saves instead. Both reach the same gate.
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Scratch.Package }, /*bOnlyDirty*/ true);
		FCoreUObjectDelegates::IsPackageOKToSaveDelegate = Gate;
		Test.AddInfo(FString::Printf(TEXT("B's save: saved %s, gate asked %d time(s), gate allowed: %s, dirty after: %s"),
			bSaved ? TEXT("yes") : TEXT("no"), GateCalls, bGateAllowed ? TEXT("yes") : TEXT("no"),
			Scratch.Package->IsDirty() ? TEXT("yes") : TEXT("no")));
		if (!Test.TestTrue(TEXT("Setup: B's save reaches the save gate"), GateCalls > 0))
		{
			return;
		}

		// Every save under the automation framework is unattended (see IsInteractiveSave), so
		// this asserts the unattended outcome: the save is refused. If unattended saves are
		// allowed and logged instead, assert the log line here.
		TArray<uint8> OnDiskBytes;
		FFileHelper::LoadFileToArray(OnDiskBytes, *Scratch.Filename);
		// Both: the version on disk alone would also survive a save that failed for another reason
		Test.TestFalse(TEXT("The gate refuses B's save"), bGateAllowed);
		Test.TestTrue(TEXT("The teammate's version is still on disk after B's save"), OnDiskBytes == IncomingBytes);
	}
}

#if PLATFORM_WINDOWS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionLoadedPackageBlocksSync,
	"Diversion.Tests.SaveDecision.Integration.LoadedPackageBlocksSync",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// DIV-14109: why the sync is blocked in R2/R3 and not in R1. The plugin's unsaved-changes
// dialog runs only when the agent cannot move a downloaded file over the original, which on
// Windows happens while UE holds the original open without FILE_SHARE_DELETE. Needs no
// Diversion workspace - this is the engine's behavior.
bool FSaveDecisionLoadedPackageBlocksSync::RunTest(const FString& Parameters)
{
	FScratchAsset Scratch;
	SetColor(Scratch.Asset, FLinearColor::Black);
	if (!TestTrue(TEXT("Setup: the asset is saved"), Scratch.Save()) ||
		!TestTrue(TEXT("Setup: the asset is loaded from disk"), Scratch.LoadFromDisk()))
	{
		return false;
	}

	// The incoming file is a copy of the original, so a move that succeeds changes nothing
	const FString StagingFilename = StagingPathFor(Scratch.Filename);
	const auto CanSyncReplace = [this, &Scratch, &StagingFilename](const TCHAR* InWhen)
	{
		if (IFileManager::Get().Copy(*StagingFilename, *Scratch.Filename) != COPY_OK)
		{
			AddError(FString::Printf(TEXT("Setup: could not copy the file %s"), InWhen));
			return false;
		}
		uint32 LastError = 0;
		const bool bReplaced = ReplaceLikeTheAgent(Scratch.Filename, StagingFilename, LastError);
		AddInfo(FString::Printf(TEXT("%s: the sync's move %s (last error %u)"), InWhen,
			bReplaced ? TEXT("succeeds") : TEXT("fails"), LastError));
		IFileManager::Get().Delete(*StagingFilename, false, true, true);
		return bReplaced;
	};

	TestFalse(TEXT("Loaded from disk: the sync is blocked"), CanSyncReplace(TEXT("Loaded from disk")));

	SetColor(Scratch.Asset, FLinearColor::Yellow);
	TestFalse(TEXT("Edited, not saved: the sync is still blocked"), CanSyncReplace(TEXT("Edited, not saved")));

	if (!TestTrue(TEXT("Setup: the edit is saved"), Scratch.Save()))
	{
		return false;
	}
	TestTrue(TEXT("Saved: the sync replaces the file"), CanSyncReplace(TEXT("Saved")));

	// Apply in the Material Editor resets the original package's loaders (UpdateOriginalMaterial)
	if (!TestTrue(TEXT("Setup: the asset is loaded from disk again"), Scratch.LoadFromDisk()))
	{
		return false;
	}
	ResetLoaders(Scratch.Package);
	TestTrue(TEXT("Loaders reset: the sync replaces the file"), CanSyncReplace(TEXT("Loaders reset")));

	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionChangedOnDiskCheck,
	"Diversion.Tests.SaveDecision.Integration.ChangedOnDiskCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// DIV-14109: the save-time check on the engine it runs on. Needs neither the agent nor a
// workspace, so it runs on every supported engine version. Loading, saving and autosaving
// must leave the file "unchanged" - before 5.6 an autosave renews the package's save id, which
// is what the gate's bookkeeping is for. A different version moved over the file must be
// "changed", and a save over it is refused: nobody can answer under the automation framework.
bool FSaveDecisionChangedOnDiskCheck::RunTest(const FString& Parameters)
{
	// Never connected, like an editor whose agent is down - the check must not need it
	FDiversionProvider Provider;
	FDiversionSaveDecisionGate& Gate = Provider.GetSaveDecisionGate();
	Gate.Bind();
	ON_SCOPE_EXIT
	{
		Gate.Unbind();
	};

	FScratchAsset Scratch;
	const FString IncomingFilename = StagingPathFor(Scratch.Filename);
	SetColor(Scratch.Asset, FLinearColor::Yellow);
	TArray<uint8> IncomingBytes;
	if (!TestTrue(TEXT("Setup: the teammate's version is written"),
		SavePackageTo(Scratch.Package, Scratch.Asset, IncomingFilename)
			&& FFileHelper::LoadFileToArray(IncomingBytes, *IncomingFilename)))
	{
		return false;
	}

	SetColor(Scratch.Asset, FLinearColor::Black);
	if (!TestTrue(TEXT("Setup: the asset is saved"), Scratch.Save()))
	{
		return false;
	}
	TestFalse(TEXT("Just saved: unchanged"), Gate.IsChangedOnDisk(Scratch.Package, Scratch.Filename));

	if (!TestTrue(TEXT("Setup: the asset is loaded from disk"), Scratch.LoadFromDisk()))
	{
		return false;
	}
	TestFalse(TEXT("Loaded from disk: unchanged"), Gate.IsChangedOnDisk(Scratch.Package, Scratch.Filename));

	// An edit, then what the editor's autosave does (FEditorFileUtils::AutosaveContentPackagesEx):
	// a backup copy under Saved/Autosaves, then the package marked dirty again. The autosave
	// itself never runs under the automation framework (CanAutoSave).
	SetColor(Scratch.Asset, FLinearColor(0.5f, 0.8f, 1.f));
	const FString AutosaveFilename = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
		/ TEXT("Autosaves") / TEXT("DiversionTests") / FPaths::GetCleanFilename(Scratch.Filename));
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*AutosaveFilename, false, true, true);
		IFileManager::Get().DeleteDirectory(*FPaths::GetPath(AutosaveFilename));
	};
	const FDiversionPackageSaveId BeforeAutosave = FDiversionSaveDecisionGate::GetPackageSaveId(Scratch.Package);
	GEditor->Exec(nullptr, *FString::Printf(TEXT("OBJ SAVEPACKAGE PACKAGE=\"%s\" FILE=\"%s\" SILENT=true AUTOSAVING=true"),
		*Scratch.PackageName, *AutosaveFilename));
	Scratch.Package->MarkPackageDirty();
	if (!TestTrue(TEXT("Setup: the autosave wrote its copy"), FPaths::FileExists(AutosaveFilename)))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("The autosave %s the package's save id"),
		FDiversionSaveDecisionGate::GetPackageSaveId(Scratch.Package) == BeforeAutosave ? TEXT("kept") : TEXT("renewed")));
	TestFalse(TEXT("After an autosave: unchanged"), Gate.IsChangedOnDisk(Scratch.Package, Scratch.Filename));

	if (!TestTrue(TEXT("Setup: the edit is saved"), Scratch.Save()))
	{
		return false;
	}
	TestFalse(TEXT("Saved again: unchanged"), Gate.IsChangedOnDisk(Scratch.Package, Scratch.Filename));

	// Another unsaved edit, and the teammate's version arrives
	SetColor(Scratch.Asset, FLinearColor::Red);
	uint32 LastError = 0;
	if (!TestTrue(TEXT("Setup: the sync moves the teammate's version in"),
		ReplaceLikeTheAgent(Scratch.Filename, IncomingFilename, LastError)))
	{
		return false;
	}
	TestTrue(TEXT("The teammate's version on disk: changed"), Gate.IsChangedOnDisk(Scratch.Package, Scratch.Filename));

	TestFalse(TEXT("A save over it is refused"), Scratch.Save());
	TArray<uint8> OnDiskBytes;
	FFileHelper::LoadFileToArray(OnDiskBytes, *Scratch.Filename);
	TestTrue(TEXT("The teammate's version is still on disk"), OnDiskBytes == IncomingBytes);
	TestTrue(TEXT("The edit is still unsaved"), Scratch.Package->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionChangedOnDiskIntegration,
	"Diversion.Tests.SaveDecision.Integration.ChangedOnDiskIsNotOverwritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// DIV-14109, silent overwrite (R1). Needs the project inside an initialized Diversion
// workspace, the agent running, dv on the PATH and Diversion as the active provider, so the save
// gate is live for files under Content. With -unattended, pass -DiversionEnableUnattended too:
// the module does not start otherwise.
//
// Workspace B opens the asset, edits it without saving and applies the edit, as the Material
// Editor's Apply does. That releases the file, so the sync is not blocked and no dialog runs.
// A teammate's newer version arrives from the cloud (dv add), and B saves. The teammate's
// version must survive that save.
// The scratch asset is uploaded to the cloud workspace and deleted again; nothing is committed.
bool FSaveDecisionChangedOnDiskIntegration::RunTest(const FString& Parameters)
{
	// The provider connects to the agent after startup - wait for it before calling the setup missing
	const double Deadline = FPlatformTime::Seconds() + 60.0;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Deadline]()
	{
		FDiversionProvider& Provider = FDiversionModule::Get().GetProvider();
		if (&ISourceControlModule::Get().GetProvider() == &Provider && Provider.IsAvailable())
		{
			RunChangedOnDiskScenario(*this, Provider);
			return true;
		}
		if (FPlatformTime::Seconds() < Deadline)
		{
			return false;
		}
		AddError(TEXT("Needs Diversion as the active revision control provider, the agent running and the project in an initialized workspace. With -unattended, also pass -DiversionEnableUnattended."));
		return true;
	}));
	return true;
}
