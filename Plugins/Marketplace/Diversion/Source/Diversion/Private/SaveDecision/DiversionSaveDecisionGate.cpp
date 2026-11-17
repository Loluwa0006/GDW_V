// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionSaveDecisionGate.h"

#include "DiversionProvider.h"
#include "DiversionState.h"
#include "DiversionUtils.h"
#include "DiversionOperations.h"
#include "DiversionConfig.h"
#include "CustomWidgets/SDiversionSaveDecisionDialog.h"

#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "IPackageAutoSaver.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackagePath.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PackagesDialog.h"
#include "UObject/Package.h"
#include "UObject/PackageFileSummary.h"

#define LOCTEXT_NAMESPACE "Diversion"

namespace
{
	/** Whether the editor is currently running an auto-save of any kind */
	bool IsEditorAutoSaving()
	{
		if (GUnrealEd == nullptr)
		{
			return false;
		}
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
		// EPackageAutoSaveType::Any also covers the in-place ("Backup and Overwrite") mode,
		// which writes the real package rather than a backup copy - without it, that mode's
		// timer would reach the dialog. The typed overload exists from 5.6 (5.6/5.7/5.8
		// headers); 5.4 lacks it (CI-proven) and so does 5.5 (its API docs show only the
		// parameterless form) - both take the fallback below.
		return GUnrealEd->GetPackageAutoSaver().IsAutoSaving(EPackageAutoSaveType::Any);
#else
		// Only the transient (backup-copy) mode is detectable here. On 5.3 the engine never
		// flags in-place autosaves at all; on 5.4/5.5 the typed overload is missing, so an
		// in-place autosave can still reach the dialog on those versions.
		return GUnrealEd->GetPackageAutoSaver().IsAutoSaving();
#endif
	}

	/**
	 * Whether InPackage belongs to the level instance currently open for editing - its level
	 * package or one of that level's external (actor) packages. Those are exactly the packages
	 * a level-instance commit saves; every other package saved during the edit is an ordinary
	 * save and keeps its gate.
	 */
	bool IsPackageOfEditingLevelInstance(const UPackage* InPackage)
	{
		if (GEditor == nullptr || InPackage == nullptr)
		{
			return false;
		}
		const UWorld* World = GEditor->GetEditorWorldContext().World();
		const ULevelInstanceSubsystem* Subsystem =
			World != nullptr ? World->GetSubsystem<ULevelInstanceSubsystem>() : nullptr;
		const ILevelInstanceInterface* Editing =
			Subsystem != nullptr ? Subsystem->GetEditingLevelInstance() : nullptr;
		const ULevel* Level = Editing != nullptr ? Subsystem->GetLevelInstanceLevel(Editing) : nullptr;
		if (Level == nullptr)
		{
			return false;
		}
		return Level->GetPackage() == InPackage || Level->GetLoadedExternalObjectPackages().Contains(InPackage);
	}

	/**
	 * Whether a person made this save and could answer a dialog about it.
	 * GIsRunningUnattendedScript is the engine's own marker for scripted saves: Python's
	 * EditorAssetLibrary and Blueprint's SavePackages set it, and their ...WithDialog
	 * variants leave it clear because those callers asked for UI.
	 */
	bool IsInteractiveSave()
	{
		return !FApp::IsUnattended()
			&& !IsRunningCommandlet()
			&& !IsEngineExitRequested()
			&& !GIsRunningUnattendedScript
			&& !GIsAutomationTesting;
	}

	/**
	 * The current results of the engine's Save Content dialog. The module keeps them until
	 * the next dialog opens, so during a save that came through the dialog this is exactly
	 * what the user ticked. The pointers may dangle (see FDiversionSaveDialogResults):
	 * collected as opaque values, never dereferenced.
	 */
	FDiversionSaveDialogResults GetSaveDialogResults()
	{
		FDiversionSaveDialogResults Results;
		// GetModulePtr, not Load: a module never loaded means no dialog was ever shown
		if (FPackagesDialogModule* Module = FModuleManager::GetModulePtr<FPackagesDialogModule>("PackagesDialog"))
		{
			TArray<UPackage*> Packages;
			Module->GetResults(Packages, ECheckBoxState::Checked);
			for (const UPackage* Package : Packages)
			{
				Results.Checked.Add(Package);
			}
			Packages.Reset();
			Module->GetResults(Packages, ECheckBoxState::Unchecked);
			for (const UPackage* Package : Packages)
			{
				Results.Unchecked.Add(Package);
			}
		}
		return Results;
	}

	/** The save id in the file's header, read from disk now. Unset for a missing or unreadable
	 * file, or one that is not a package. */
	TOptional<FDiversionPackageSaveId> ReadFileSaveId(const FString& InFullPath)
	{
		const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*InFullPath, FILEREAD_Silent));
		if (!Reader.IsValid())
		{
			return {};
		}
		FPackageFileSummary Summary;
		*Reader << Summary;
		if (Reader->IsError() || Summary.Tag != PACKAGE_FILE_TAG)
		{
			return {};
		}
#if !UE_VERSION_OLDER_THAN(5, 4, 0)
		return Summary.GetSavedHash();
#else
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return Summary.Guid;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
	}

	/** The loaded package a file belongs to, or null */
	UPackage* FindPackageForFile(const FString& InFullPath)
	{
		FString PackageName;
		return FPackageName::TryConvertFilenameToLongPackageName(InFullPath, PackageName)
			? FindPackage(nullptr, *PackageName)
			: nullptr;
	}

	/** An auto-save crash-recovery copy (Saved/Autosaves) - never blocked. The directory is
	 * fixed for the process, so it is worked out once rather than per package. */
	bool IsAutosaveBackupCopy(const FString& InFullPath)
	{
		static const FString AutosaveDir =
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Autosaves"));
		return FPaths::IsUnderDirectory(InFullPath, AutosaveDir);
	}
}

FDiversionSaveDecisionGate::~FDiversionSaveDecisionGate()
{
	Unbind();
}

void FDiversionSaveDecisionGate::Bind()
{
	// The engine's save gate is single-cast and already bound (writable-folder check).
	// Capture that binding, put ours in front, delegate to it first. Restored in Unbind().
	if (!bIsPackageOKToSaveDelegateBound)
	{
		PreviousIsPackageOKToSaveDelegate = FCoreUObjectDelegates::IsPackageOKToSaveDelegate;
		FCoreUObjectDelegates::IsPackageOKToSaveDelegate.BindRaw(this, &FDiversionSaveDecisionGate::IsPackageOKToSave);
		bIsPackageOKToSaveDelegateBound = true;
	}

	RefusedSaveNoise.Bind();

	// Mark the dialog's current results as seen before every GC. A first-seen set therefore
	// never contains a pointer that survived a GC (see FDiversionSaveDialogResults) - even
	// when the engine's dialog was cancelled and no save ever reached this gate.
	if (!PreGarbageCollectHandle.IsValid())
	{
		PreGarbageCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddLambda(
			[this]() { ConsumedDialogResults = GetSaveDialogResults(); });
	}

#if DIVERSION_TRACKS_FILE_SAVE_IDS
	if (!PackageSavedHandle.IsValid())
	{
		PackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FDiversionSaveDecisionGate::OnPackageSaved);
	}
#endif
}

void FDiversionSaveDecisionGate::Unbind()
{
	// Restore the engine's original save gate (see Bind)
	if (bIsPackageOKToSaveDelegateBound)
	{
		FCoreUObjectDelegates::IsPackageOKToSaveDelegate = PreviousIsPackageOKToSaveDelegate;
		PreviousIsPackageOKToSaveDelegate.Unbind();
		bIsPackageOKToSaveDelegateBound = false;
	}

	RefusedSaveNoise.Unbind();

	if (PreGarbageCollectHandle.IsValid())
	{
		FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreGarbageCollectHandle);
		PreGarbageCollectHandle.Reset();
	}

#if DIVERSION_TRACKS_FILE_SAVE_IDS
	if (PackageSavedHandle.IsValid())
	{
		UPackage::PackageSavedWithContextEvent.Remove(PackageSavedHandle);
		PackageSavedHandle.Reset();
	}
	FileSaveIds.Empty();
#endif

	ToastedThisSave.Empty();
	AcceptedIncomingPaths.Empty();
	GestureDiskAnswers.Empty();
	DiskCheckCache.Empty();
	GestureAnswers.Empty();
	bGestureEvaluated = false;
	bGestureCancelled = false;
	bGestureListFromDialog = false;
	GestureVerifiedLocks.Empty();
	bGestureLockRefreshFailed = false;
	bGestureLockAcquireFailed = false;
	ConsumedDialogResults = FDiversionSaveDialogResults();
	if (SaveGestureResetTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SaveGestureResetTickerHandle);
		SaveGestureResetTickerHandle.Reset();
	}
	bSaveGestureResetPending = false;
}

bool FDiversionSaveDecisionGate::ShouldNeverHoldSave(UPackage* InPackage, const FString& InFullPath,
	bool bInChangedOnDisk) const
{
	// A merge is open - finalize saves inline and dies on a failed save. The files it resolves
	// are written over on purpose; any other file changed on disk still gets its question.
	if (Provider.GetWorkspaceMergesList().Num() > 0 || Provider.GetBranchMergesList().Num() > 0)
	{
		if (!bInChangedOnDisk || Provider.FilesToResolve.Contains(InFullPath))
		{
			return true;
		}
	}

	// The package has never been written to disk. Holding the first save of a new map loses
	// it outright, and an unwritten path cannot be locked by anyone.
	if (InPackage != nullptr && InPackage->HasAnyPackageFlags(PKG_NewlyCreated))
	{
		return true;
	}

	// The editor's auto-save timer, not a decision the user is making
	if (IsEditorAutoSaving())
	{
		return true;
	}

	// Play In Editor owns this save. Its own saves never write over a package's loaded file, so
	// a save over a file changed on disk is the user's and keeps its question.
	if (!bInChangedOnDisk && GEditor != nullptr && GEditor->PlayWorld != nullptr)
	{
		return true;
	}

	// Part of the level instance open for editing: a forced commit (leaving the edit,
	// closing the editor) unloads the level whether or not its save succeeded, so a refusal
	// discards the edit (CommitLevelInstanceInternal, bDiscardOnFailure). Other packages
	// saved during the edit keep their gate.
	if (IsPackageOfEditingLevelInstance(InPackage))
	{
		return true;
	}

	return false;
}

bool FDiversionSaveDecisionGate::IsGateableSave(UPackage* InPackage, const FString& InFullPath,
	bool bInChangedOnDisk) const
{
	// A file someone else changed on disk is questioned wherever it lives, and with the agent
	// down too (no workspace path then); every other file only inside the workspace. Both
	// lookups are references, not copies - this is asked of every dirty package in the save.
	const FString& WsPath = Provider.WsInfo.Get().GetPath();
	if (!bInChangedOnDisk && (WsPath.IsEmpty() || !FPaths::IsUnderDirectory(InFullPath, WsPath)))
	{
		return false;
	}

	if (IsAutosaveBackupCopy(InFullPath))
	{
		return false;
	}

	return !ShouldNeverHoldSave(InPackage, InFullPath, bInChangedOnDisk);
}

bool FDiversionSaveDecisionGate::IsPackageOKToSave(UPackage* InPackage, const FString& InFilename, FOutputDevice* ErrorLog)
{
	// Chain the engine's original gate first (writable-folder permission list)
	if (PreviousIsPackageOKToSaveDelegate.IsBound() &&
		!PreviousIsPackageOKToSaveDelegate.Execute(InPackage, InFilename, ErrorLog))
	{
		return false;
	}

	if (!IsInGameThread())
	{
		return true;
	}

	// Disk reads from an earlier call are stale for this one
	DiskCheckCache.Empty();

#if DIVERSION_TRACKS_FILE_SAVE_IDS
	// Before this save can renew the package's save id
	RememberFileSaveId(InPackage);
#endif

	// Whether this save would overwrite someone else's change is a question about the file
	// alone - asked with the agent down and outside the workspace too
	const FString FullPath = FPaths::ConvertRelativePathToFull(InFilename);
	const bool bChangedOnDisk = IsChangedOnDiskCached(InPackage, FullPath);

	// Cancel meant the whole save. With the agent down the dialog could list only the files
	// changed on disk, so the rest of the save stops here instead of at the answer lookup below.
	if (bGestureCancelled && !Provider.bDiversionAvailable && !IsAutosaveBackupCopy(FullPath) &&
		!ShouldNeverHoldSave(InPackage, FullPath, bChangedOnDisk))
	{
		ScheduleSaveGestureReset();
		return RefuseSave(InFilename, ErrorLog);
	}

	// Anything else cannot be decided without the agent -> allow. The gate is a courtesy; the
	// server rejects commits of locked files either way.
	if (!bChangedOnDisk && !Provider.bDiversionAvailable)
	{
		return true;
	}

	// Only the in-place autosave writes the real file. Nobody asked for this save, so nobody
	// can answer it; the user's own next save of the file asks instead.
	if (bChangedOnDisk && IsEditorAutoSaving())
	{
		UE_LOG(LogSourceControl, Warning,
			TEXT("Save-decision: %s changed on disk after it was loaded - the autosave did not overwrite it."), *InFilename);
		return RefuseSave(InFilename, ErrorLog);
	}

	// Checked first, so a save we would never hold makes no lock lookup (autosave and PIE
	// stay off the network). The reset is still scheduled: its end-of-save cleanup must also
	// run for a save whose files all bypass the gate.
	if (!IsGateableSave(InPackage, FullPath, bChangedOnDisk))
	{
		ScheduleSaveGestureReset();
		return true;
	}

	const FString DisplayName = FPaths::GetBaseFilename(InFilename);

	// Already answered in this save's dialog. The lock, if asked for, is taken now rather
	// than when the dialog closed, so a listed file that is never saved costs nothing.
	if (const DiversionUtils::EDiversionSaveChoice* Answer = GestureAnswers.Find(FullPath))
	{
		ScheduleSaveGestureReset();
		return ApplySaveChoice(FullPath, DisplayName, *Answer, ErrorLog);
	}

	// The user cancelled this save. A file the prediction missed has no recorded answer,
	// but Cancel meant the whole save - skip it like the rest, do not ask again.
	if (bGestureCancelled)
	{
		ScheduleSaveGestureReset();
		return RefuseSave(InFilename, ErrorLog);
	}

	// Lock state must be verified against the backend before it can gate a save - a stale
	// cache entry would let a locked file through. Verified once per save, not per file.
	// A file changed on disk still gets its question when locks cannot be verified - that
	// question needs no backend. A file outside the workspace has no lock to verify (a lookup
	// with no path in the workspace lists every lock in the repo).
	FDiversionLockInfo LockInfo;
	const FString& WsPath = Provider.WsInfo.Get().GetPath();
	const bool bLocksEligible = Provider.bDiversionAvailable && Provider.ShouldCheckHardLocks() &&
		!WsPath.IsEmpty() && FPaths::IsUnderDirectory(FullPath, WsPath);
	if (bLocksEligible)
	{
		if (const FDiversionLockInfo* Verified = GestureVerifiedLocks.Find(FullPath))
		{
			LockInfo = *Verified;
		}
		else if (bGestureLockRefreshFailed)
		{
			// A lookup already failed this save - let the rest through rather than paying a
			// connect timeout per file
			if (!bChangedOnDisk)
			{
				return true;
			}
		}
		else
		{
			ScheduleSaveGestureReset();
			if (Provider.RefreshFileLocks({ FullPath }))
			{
				LockInfo = Provider.GetStateInternal(FullPath)->FileLock.GetLockInfo();
				GestureVerifiedLocks.Add(FullPath, LockInfo);
			}
			else
			{
				// Backend unreachable - say so once, let every save through
				bGestureLockRefreshFailed = true;
				Provider.ShowNotification(FDiversionNotification(
					LOCTEXT("DiversionPopup_LockVerifyFailed",
						"Diversion could not verify file locks - saving anyway. Hard-locked files are still protected by the server."),
					TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail));
				if (!bChangedOnDisk)
				{
					return true;
				}
			}
		}
	}

	DiversionUtils::EDiversionSaveCategory Category = DiversionUtils::EDiversionSaveCategory::UnlockedLockable;
	FString Holder;
	switch (EvaluateFile(FullPath, IsInteractiveSave(), Category, LockInfo, Holder))
	{
	case DiversionUtils::EDiversionSaveVetoAction::Allow:
		if (Category == DiversionUtils::EDiversionSaveCategory::HardLockedByOther && !IsInteractiveSave())
		{
			// The only trace a scripted run leaves of a lock it saved over; whoever hits the
			// commit rejection later needs to find this line.
			UE_LOG(LogSourceControl, Warning,
				TEXT("Save-decision: %s is locked by %s and was saved unattended - the commit stays blocked until the lock is released."),
				*InFilename, *Holder);
		}
		// This file needs no decision, but others in the same save may - and the dialog must
		// list this file too, or Cancel could not stop it (it would be on disk before the
		// next file asks).
		if (IsInteractiveSave() && !bGestureEvaluated)
		{
			ScheduleSaveGestureReset();
			return PromptForSaveDecisions(FullPath, InFilename, ErrorLog);
		}
		return true;

	case DiversionUtils::EDiversionSaveVetoAction::Refuse:
		// The only trace a scripted run leaves of a save it did not get: the file on disk is
		// someone else's version, and the package keeps its unsaved changes
		UE_LOG(LogSourceControl, Warning,
			TEXT("Save-decision: %s changed on disk after it was loaded and was not saved - nobody could be asked whether to overwrite it."),
			*InFilename);
		return RefuseSave(InFilename, ErrorLog);

	case DiversionUtils::EDiversionSaveVetoAction::Prompt:
	default:
		break;
	}

	// Only an interactive save reaches here - RouteSaveDecision returns Prompt only when a
	// person is behind the save.
	ScheduleSaveGestureReset();
	return PromptForSaveDecisions(FullPath, InFilename, ErrorLog);
}

DiversionUtils::EDiversionSaveVetoAction FDiversionSaveDecisionGate::EvaluateFile(const FString& InFullPath,
	bool bInInteractive, DiversionUtils::EDiversionSaveCategory& OutCategory,
	FDiversionLockInfo& OutLockInfo, FString& OutHolder) const
{
	// Overwriting someone else's change outranks every lock question
	OutLockInfo = FDiversionLockInfo();
	if (IsChangedOnDiskCached(FindPackageForFile(InFullPath), InFullPath))
	{
		OutHolder.Empty();
		return DiversionUtils::RouteChangedOnDiskSave(bInInteractive, OutCategory);
	}

	// Lock state comes from what this save already verified. Soft-clash state comes from the
	// polled cache with no extra query - a missed clash costs a warning, not an overwrite.
	const bool bLocksEligible = Provider.ShouldCheckHardLocks();
	if (bLocksEligible)
	{
		if (const FDiversionLockInfo* Verified = GestureVerifiedLocks.Find(InFullPath))
		{
			OutLockInfo = *Verified;
		}
	}

	const TSharedRef<FDiversionState> State = Provider.GetStateInternal(InFullPath);
	const DiversionUtils::EDiversionSaveVetoAction Action = DiversionUtils::RouteSaveDecision(
		OutLockInfo, State->HasPotentialClashes(), Provider.IsAutoLockedByRepoRule(InFullPath),
		bLocksEligible, IsDiversionLockOnSavePromptEnabled(), bInInteractive, OutCategory);

	OutHolder.Empty();
	if (OutCategory == DiversionUtils::EDiversionSaveCategory::HardLockedByOther)
	{
		OutHolder = OutLockInfo.GetHolderText().ToString();
	}
	else if (OutCategory == DiversionUtils::EDiversionSaveCategory::SoftLocked)
	{
		OutHolder = State->GetOtherEditorNames().ToString();
	}
	return Action;
}

TSharedPtr<FDiversionSaveDecisionRow> FDiversionSaveDecisionGate::MakeDecisionRow(const FString& InFullPath,
	DiversionUtils::EDiversionSaveCategory InCategory, const FDiversionLockInfo& InLockInfo,
	const FString& InHolder) const
{
	TSharedPtr<FDiversionSaveDecisionRow> Row = MakeShared<FDiversionSaveDecisionRow>();
	Row->FullPath = InFullPath;
	Row->AssetDisplayName = FPaths::GetBaseFilename(InFullPath);
	// Drives the dialog's File column and its asset icon; empty is tolerated (no icon, blank
	// column) for a path the asset registry cannot resolve
	FPackageName::TryConvertFilenameToLongPackageName(InFullPath, Row->PackageName);
	Row->Category = InCategory;
	Row->Holder = InHolder;
	Row->LockedAt = InLockInfo.LockedAt;
	// No lock offer for a file someone else holds, for a file whose question is the change on
	// disk, nor on tiers without hard locks
	Row->bLockOfferAvailable =
		InCategory != DiversionUtils::EDiversionSaveCategory::HardLockedByOther &&
		InCategory != DiversionUtils::EDiversionSaveCategory::ChangedOnDisk &&
		Provider.bDiversionAvailable && Provider.ShouldCheckHardLocks();
	// Accept Incoming reloads the one file, which does not reload a level
	Row->bAcceptIncomingAvailable =
		InCategory == DiversionUtils::EDiversionSaveCategory::ChangedOnDisk
		&& !DiversionUtils::UPackageUtils::IsLevelPackage(InFullPath);
	Row->Choice = DiversionUtils::DefaultSaveChoice(InCategory, Row->bLockOfferAvailable,
		IsDiversionLockByDefaultOnSaveEnabled());
	return Row;
}

TArray<FString> FDiversionSaveDecisionGate::GatherPendingSaveCandidates(const FString& InTriggerFullPath)
{
	TArray<UPackage*> DirtyPackages;
	UEditorLoadingAndSavingUtils::GetDirtyMapPackages(DirtyPackages);
	UEditorLoadingAndSavingUtils::GetDirtyContentPackages(DirtyPackages);

	// A save that came through the engine's Save Content dialog saves exactly the ticked
	// packages; the triggering package being among them is the sign. A save without the
	// dialog (Ctrl+S in an asset editor, a level save) keeps the full prediction.
	//
	// The dialog's results outlive their save, so they only count the first time this gate
	// sees them; anything read once is stale. A new dialog with identical ticks is misread
	// as stale, which only widens the list to the prediction - never narrows it.
	const FDiversionSaveDialogResults Dialog = GetSaveDialogResults();
	const bool bDialogIsFresh = !Dialog.Checked.IsEmpty() && !(Dialog == ConsumedDialogResults);
	ConsumedDialogResults = Dialog;
	FString TriggerPackageName;
	const UPackage* TriggerPackage =
		FPackageName::TryConvertFilenameToLongPackageName(InTriggerFullPath, TriggerPackageName)
			? FindPackage(nullptr, *TriggerPackageName)
			: nullptr;
	bGestureListFromDialog = bDialogIsFresh && TriggerPackage != nullptr && Dialog.Checked.Contains(TriggerPackage);

	TArray<FString> Candidates;
	for (UPackage* Package : DirtyPackages)
	{
		if (Package == nullptr || (bGestureListFromDialog && !Dialog.Checked.Contains(Package)))
		{
			continue;
		}
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename,
			Package->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension()))
		{
			continue;
		}
		const FString FullPath = FPaths::ConvertRelativePathToFull(Filename);
		if (FullPath == InTriggerFullPath || GestureAnswers.Contains(FullPath))
		{
			continue;
		}
		// Outside the usual rule (agent down, outside the workspace) only a file changed on disk
		// has a question; its header is read only then - EvaluateFile reads the others' later
		const bool bGateable = Provider.bDiversionAvailable && IsGateableSave(Package, FullPath);
		if (!bGateable && !(IsChangedOnDiskCached(Package, FullPath) && IsGateableSave(Package, FullPath, /*bInChangedOnDisk*/ true)))
		{
			continue;
		}
		Candidates.AddUnique(FullPath);
	}
	return Candidates;
}

TArray<TSharedPtr<FDiversionSaveDecisionRow>> FDiversionSaveDecisionGate::BuildSaveDecisionRows(
	const FString& InTriggerFullPath)
{
	// Gathered once per save; a file arriving later was missed by the prediction and gets
	// its own dialog.
	TArray<FString> Candidates;
	if (!bGestureEvaluated)
	{
		Candidates = GatherPendingSaveCandidates(InTriggerFullPath);
	}

	// Verify the whole set at once - RefreshFileLocks answers for many paths in one listing.
	// Only files inside the workspace can hold a lock (see IsPackageOKToSave).
	const FString& WsPath = Provider.WsInfo.Get().GetPath();
	const TArray<FString> LockCandidates = Candidates.FilterByPredicate([&WsPath](const FString& InPath)
		{
			return !WsPath.IsEmpty() && FPaths::IsUnderDirectory(InPath, WsPath);
		});
	if (LockCandidates.Num() > 0 && Provider.bDiversionAvailable && Provider.ShouldCheckHardLocks() && !bGestureLockRefreshFailed)
	{
		if (Provider.RefreshFileLocks(LockCandidates))
		{
			for (const FString& Candidate : LockCandidates)
			{
				GestureVerifiedLocks.Add(Candidate,
					Provider.GetStateInternal(Candidate)->FileLock.GetLockInfo());
			}
		}
		else
		{
			// Could not verify the others - drop them rather than show unverified lock
			// state; they are let through as they arrive. A file changed on disk stays: its
			// question needs no lock state.
			bGestureLockRefreshFailed = true;
			Candidates.RemoveAll([this](const FString& InPath)
				{
					return !IsChangedOnDiskCached(FindPackageForFile(InPath), InPath);
				});
		}
	}

	const auto BuildRow = [this](const FString& InPath, DiversionUtils::EDiversionSaveVetoAction& OutAction)
	{
		DiversionUtils::EDiversionSaveCategory Category = DiversionUtils::EDiversionSaveCategory::UnlockedLockable;
		FDiversionLockInfo LockInfo;
		FString Holder;
		OutAction = EvaluateFile(InPath, /*bInInteractive*/ true, Category, LockInfo, Holder);
		return MakeDecisionRow(InPath, Category, LockInfo, Holder);
	};

	// The triggering file is listed first and classified like every other. Files needing no
	// decision are listed without a control: a file that is not listed cannot be stopped by
	// Cancel.
	TArray<FString> Paths;
	Paths.Add(InTriggerFullPath);
	Paths.Append(Candidates);

	TArray<TSharedPtr<FDiversionSaveDecisionRow>> Rows;
	TArray<TSharedPtr<FDiversionSaveDecisionRow>> InformationalRows;
	for (const FString& Path : Paths)
	{
		DiversionUtils::EDiversionSaveVetoAction Action;
		TSharedPtr<FDiversionSaveDecisionRow> Row = BuildRow(Path, Action);
		if (Action == DiversionUtils::EDiversionSaveVetoAction::Prompt)
		{
			Rows.Add(Row);
			continue;
		}
		Row->bDecisionRequired = false;
		Row->OutcomeText = Row->Category == DiversionUtils::EDiversionSaveCategory::AutoLockedByRule
			? LOCTEXT("Outcome_WillSaveAutoLock", "Will save - locked on sync (repo rule)")
			: LOCTEXT("Outcome_WillSave", "Will save");
		InformationalRows.Add(Row);
	}
	// Decision rows first; the dialog buckets everything by situation either way
	Rows.Append(InformationalRows);
	return Rows;
}

bool FDiversionSaveDecisionGate::ShowSaveDecisionDialog(
	const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows)
{
	const bool bMultiple = InRows.Num() > 1;
	TSharedRef<SWindow> DialogWindow = SNew(SWindow)
		.Title(bMultiple
			? LOCTEXT("DiversionSaveDecisionTitlePlural", "Diversion - Save Files")
			: LOCTEXT("DiversionSaveDecisionTitle", "Diversion - Save File"))
		.SizingRule(ESizingRule::UserSized)
		.ClientSize(FVector2D(1010.f, bMultiple ? 500.f : 300.f))
		.MinWidth(760.f).MinHeight(260.f)
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false).SupportsMaximize(false);

	TSharedRef<SDiversionSaveDecisionDialog> Dialog = SNew(SDiversionSaveDecisionDialog)
		.ParentWindow(DialogWindow)
		.Rows(InRows)
		.ListIsExact(bGestureListFromDialog)
		// Only worth saying when the dialog actually offers a lock
		.ShowNoAutoLockRulesWarning(Provider.HasNoAutoLockRules() && InRows.ContainsByPredicate(
			[](const TSharedPtr<FDiversionSaveDecisionRow>& Row)
			{
				return Row->bDecisionRequired && Row->bLockOfferAvailable;
			}))
		.RepoId(Provider.GetWsInfo().RepoID);

	DialogWindow->SetContent(Dialog);
	FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);
	const bool bConfirmed = Dialog->WasConfirmed();
	DialogWindow->RequestDestroyWindow();
	// The sync kept running while the dialog was open
	DiskCheckCache.Empty();
	return bConfirmed;
}

void FDiversionSaveDecisionGate::RecordSaveDecisions(
	const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows, bool bInConfirmed)
{
	if (!bInConfirmed)
	{
		// Cancel/X cancels the save, not just the files that happened to ask something - so
		// informational rows are skipped too, and so is any file the prediction missed
		bGestureCancelled = true;
		for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : InRows)
		{
			GestureAnswers.Add(Row->FullPath, DiversionUtils::EDiversionSaveChoice::Skip);
		}
		return;
	}

	// Every answer is recorded before any is acted on
	for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : InRows)
	{
		if (!Row->bDecisionRequired)
		{
			// Listed for visibility only - let it route normally when its own save arrives
			continue;
		}
		GestureAnswers.Add(Row->FullPath, Row->Choice);
		if (Row->Category == DiversionUtils::EDiversionSaveCategory::ChangedOnDisk)
		{
			GestureDiskAnswers.Add(Row->FullPath);
		}
	}
}

bool FDiversionSaveDecisionGate::PromptForSaveDecisions(const FString& InTriggerFullPath,
	const FString& InTriggerFilename, FOutputDevice* ErrorLog)
{
	const TArray<TSharedPtr<FDiversionSaveDecisionRow>> Rows = BuildSaveDecisionRows(InTriggerFullPath);
	bGestureEvaluated = true;

	// Nothing in this save needs a decision - no dialog, nothing to record, the save proceeds
	const bool bAnyDecision = Rows.ContainsByPredicate(
		[](const TSharedPtr<FDiversionSaveDecisionRow>& Row) { return Row->bDecisionRequired; });
	if (!bAnyDecision)
	{
		return true;
	}

	const bool bConfirmed = ShowSaveDecisionDialog(Rows);
	RecordSaveDecisions(Rows, bConfirmed);
	SendSaveDecisionAnalytics(Rows, bConfirmed);

	// Only the triggering file is acted on now; the others act when their own save arrives,
	// so a listed file that is never saved never gets locked. A trigger that needed no
	// decision has an answer only after Cancel (Skip, like every row).
	const FString TriggerDisplayName = FPaths::GetBaseFilename(InTriggerFilename);
	if (const DiversionUtils::EDiversionSaveChoice* TriggerChoice = GestureAnswers.Find(InTriggerFullPath))
	{
		return ApplySaveChoice(InTriggerFullPath, TriggerDisplayName, *TriggerChoice, ErrorLog);
	}
	// No question of its own, but it waited for the dialog like the rest (read afresh: the
	// cache was emptied when the dialog closed)
	if (IsChangedOnDiskCached(FindPackageForFile(InTriggerFullPath), InTriggerFullPath))
	{
		return AskAboutLateChangeOnDisk(InTriggerFullPath, TriggerDisplayName, ErrorLog);
	}
	return true;
}

void FDiversionSaveDecisionGate::SendSaveDecisionAnalytics(
	const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows, bool bInConfirmed)
{
	int32 DecisionCount = 0;
	int32 SkippedCount = 0;
	for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : InRows)
	{
		if (!Row->bDecisionRequired)
		{
			continue;
		}
		++DecisionCount;
		SkippedCount += Row->Choice == DiversionUtils::EDiversionSaveChoice::Skip ? 1 : 0;
	}

	auto SendAnalyticsOperation = ISourceControlOperation::Create<FSendAnalytics>();
	SendAnalyticsOperation->SetEventName(FText::FromString("UE SaveDecision Dialog Interacted"));
	SendAnalyticsOperation->SetEventProperties({
		{"chosen_action", bInConfirmed ? "confirm" : "cancel"},
		{"row_count", FString::FromInt(InRows.Num())},
		{"decision_count", FString::FromInt(DecisionCount)},
		{"skipped", FString::FromInt(bInConfirmed ? SkippedCount : DecisionCount)}});
	Provider.Execute(SendAnalyticsOperation, nullptr, TArray<FString>(), EConcurrency::Asynchronous);
}

bool FDiversionSaveDecisionGate::ApplySaveChoice(const FString& InFullPath, const FString& InDisplayName,
	DiversionUtils::EDiversionSaveChoice InChoice, FOutputDevice* ErrorLog)
{
	// An answer that saves was given before the dialog closed, and the sync does not wait for a
	// dialog. Unless the answer was about the change on disk itself, check the file again: the
	// cache was emptied when the dialog closed, and a later call reads its file afresh.
	const bool bWouldSave = InChoice == DiversionUtils::EDiversionSaveChoice::LockAndSave ||
		InChoice == DiversionUtils::EDiversionSaveChoice::SaveUnlocked;
	if (bWouldSave && !GestureDiskAnswers.Contains(InFullPath) &&
		IsChangedOnDiskCached(FindPackageForFile(InFullPath), InFullPath))
	{
		return AskAboutLateChangeOnDisk(InFullPath, InDisplayName, ErrorLog);
	}

	bool bAllowed = false;
	switch (InChoice)
	{
	case DiversionUtils::EDiversionSaveChoice::LockAndSave:
		// A lost race demotes to Skip - honoring the winner's lock - and an unlockable path
		// demotes to saving without one
		bAllowed = AcquireLockForSave(InFullPath, InDisplayName) != DiversionUtils::EDiversionSaveChoice::Skip;
		break;

	case DiversionUtils::EDiversionSaveChoice::SaveUnlocked:
		bAllowed = true;
		break;

	case DiversionUtils::EDiversionSaveChoice::AcceptIncoming:
		// Not saved; the file on disk is loaded once the save is over
		AcceptedIncomingPaths.AddUnique(InFullPath);
		break;

	case DiversionUtils::EDiversionSaveChoice::Skip:
	default:
		break;
	}

	if (!bAllowed)
	{
		// The save is about to fail by the user's own choice - claim the engine's
		// failed-to-save prompt for it so they are not asked to retry what they just declined
		RefuseSave(InFullPath, ErrorLog);
	}
	return bAllowed;
}

DiversionUtils::EDiversionSaveChoice FDiversionSaveDecisionGate::AcquireLockForSave(const FString& InFullPath,
	const FString& InDisplayName)
{
	// The lock service already failed once this save - do not pay another connect timeout
	// per file; the rest of the save goes through unlocked.
	if (bGestureLockAcquireFailed)
	{
		return DiversionUtils::EDiversionSaveChoice::SaveUnlocked;
	}

	TSharedRef<FUpdateFileLock> LockOperation = ISourceControlOperation::Create<FUpdateFileLock>();
	LockOperation->SetLockAction(DiversionUtils::EDiversionLockAction::Lock);
	LockOperation->SetSuppressResultNotification(true);
	Provider.Execute(LockOperation, nullptr, { InFullPath }, EConcurrency::Synchronous);

	const TMap<FString, FDiversionLockInfo> AcquiredLocks = LockOperation->GetResultUpdatedLocks();
	const TArray<DiversionUtils::FDiversionLockOpFailure> Failures = LockOperation->GetResultFailures();

	if (AcquiredLocks.Contains(InFullPath))
	{
		// Held now - cached so this save does not query again for the same file
		GestureVerifiedLocks.Add(InFullPath, AcquiredLocks[InFullPath]);
		return DiversionUtils::EDiversionSaveChoice::LockAndSave;
	}

	// The server never answered. Save without the lock rather than refuse - a dead
	// connection must not cost the user their save; the commit check still protects. Said
	// once per save.
	if (Failures.Num() == 0)
	{
		bGestureLockAcquireFailed = true;
		Provider.ShowNotification(FDiversionNotification(
			LOCTEXT("DiversionPopup_LockServiceUnreachable",
				"Diversion could not reach the lock service - saving without locks."),
			TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail));
		return DiversionUtils::EDiversionSaveChoice::SaveUnlocked;
	}

	// The cache may still hold the pre-race state here - the status code covers that
	const DiversionUtils::FDiversionLockOpFailure& Failure = Failures[0];
	const FDiversionLockInfo Cached = Provider.GetStateInternal(InFullPath)->FileLock.GetLockInfo();
	const DiversionUtils::ELockAcquisitionFailure Classification =
		DiversionUtils::ClassifyLockAcquisitionFailure(Failure.StatusCode, Cached);
	const DiversionUtils::EDiversionSaveChoice Outcome = DiversionUtils::ResolveLockFailure(Classification);

	// Messaging only - the decision is already made above
	switch (Classification)
	{
	case DiversionUtils::ELockAcquisitionFailure::HeldByKnownUser:
	case DiversionUtils::ELockAcquisitionFailure::HeldByUnknownUser:
	{
		// A cache that does not know who holds the lock still has to name someone
		const FText Holder = Cached.bLockedByOther ? Cached.GetHolderText() : FDiversionLockInfo().GetHolderText();
		ToastOncePerSave(InFullPath, FText::Format(
			LOCTEXT("DiversionPopup_LockRaceLost",
				"\"{0}\" is locked by {1} - the file was not saved."),
			FText::FromString(InDisplayName), Holder));
		break;
	}
	case DiversionUtils::ELockAcquisitionFailure::Unexpected:
	default:
		UE_LOG(LogSourceControl, Warning,
			TEXT("Save-decision: locking %s failed (HTTP %d): %s - saving without a lock"),
			*InFullPath, Failure.StatusCode, *Failure.Detail);
		ToastOncePerSave(InFullPath, FText::Format(
			LOCTEXT("DiversionPopup_LockFailed",
				"\"{0}\" could not be locked (error {1}) - saved without a lock."),
			FText::FromString(InDisplayName), FText::AsNumber(Failure.StatusCode)));
		break;
	}
	return Outcome;
}

void FDiversionSaveDecisionGate::ScheduleSaveGestureReset()
{
	if (bSaveGestureResetPending)
	{
		return;
	}
	bSaveGestureResetPending = true;
	SaveGestureResetTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FDiversionSaveDecisionGate::ResetSaveGesture));
}

bool FDiversionSaveDecisionGate::ResetSaveGesture(float)
{
	bSaveGestureResetPending = false;
	SaveGestureResetTickerHandle.Reset();

	// The save has finished, so the engine's log page is complete and safe to clean up.
	// Runs before the state below is cleared - it uses it to identify the page.
	RefusedSaveNoise.FlushAfterSave();

	// Mark the dialog's current results as seen, even when this save never read them:
	// results still present when a save ends predate the next one by definition.
	ConsumedDialogResults = GetSaveDialogResults();

	ReloadAcceptedIncoming();
	GestureDiskAnswers.Empty();
	DiskCheckCache.Empty();

#if DIVERSION_TRACKS_FILE_SAVE_IDS
	for (auto It = FileSaveIds.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
#endif

	GestureAnswers.Empty();
	bGestureEvaluated = false;
	bGestureCancelled = false;
	bGestureListFromDialog = false;
	GestureVerifiedLocks.Empty();
	ToastedThisSave.Empty();
	bGestureLockRefreshFailed = false;
	bGestureLockAcquireFailed = false;
	return false; // one-shot
}


void FDiversionSaveDecisionGate::ToastOncePerSave(const FString& InFullPath, const FText& InMessage)
{
	bool bAlreadyToasted = false;
	ToastedThisSave.Add(InFullPath, &bAlreadyToasted);
	if (!bAlreadyToasted)
	{
		Provider.ShowNotification(FDiversionNotification(InMessage,
			TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail));
	}
}

bool FDiversionSaveDecisionGate::IsChangedOnDisk(const UPackage* InPackage, const FString& InFullPath) const
{
	// Only a save over the file this package came from can overwrite someone else's change - a
	// new package, or a save anywhere else, has nothing of its own on disk to compare with
	if (InPackage == nullptr || InPackage->HasAnyPackageFlags(PKG_NewlyCreated))
	{
		return false;
	}
	FString TargetPackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(InFullPath, TargetPackageName) ||
		TargetPackageName != InPackage->GetLoadedPath().GetPackageName())
	{
		return false;
	}
	// A file that is gone has nothing to overwrite
	const TOptional<FDiversionPackageSaveId> OnDisk = ReadFileSaveId(InFullPath);
	return OnDisk.IsSet() && OnDisk.GetValue() != GetExpectedFileSaveId(InPackage);
}

bool FDiversionSaveDecisionGate::IsChangedOnDiskCached(const UPackage* InPackage, const FString& InFullPath) const
{
	if (const bool* Known = DiskCheckCache.Find(InFullPath))
	{
		return *Known;
	}
	const bool bChanged = IsChangedOnDisk(InPackage, InFullPath);
	DiskCheckCache.Add(InFullPath, bChanged);
	return bChanged;
}

bool FDiversionSaveDecisionGate::AskAboutLateChangeOnDisk(const FString& InFullPath, const FString& InDisplayName,
	FOutputDevice* ErrorLog)
{
	if (!IsInteractiveSave())
	{
		UE_LOG(LogSourceControl, Warning,
			TEXT("Save-decision: %s changed on disk while its save waited and was not saved - nobody could be asked whether to overwrite it."),
			*InFullPath);
		return RefuseSave(InFullPath, ErrorLog);
	}

	// The new answer replaces the earlier one and is about the change on disk, so applying it
	// does not ask again
	const TArray<TSharedPtr<FDiversionSaveDecisionRow>> Rows = {
		MakeDecisionRow(InFullPath, DiversionUtils::EDiversionSaveCategory::ChangedOnDisk, FDiversionLockInfo(), FString()) };
	const bool bConfirmed = ShowSaveDecisionDialog(Rows);
	RecordSaveDecisions(Rows, bConfirmed);
	SendSaveDecisionAnalytics(Rows, bConfirmed);
	GestureDiskAnswers.Add(InFullPath);
	return ApplySaveChoice(InFullPath, InDisplayName, GestureAnswers.FindChecked(InFullPath), ErrorLog);
}

FDiversionPackageSaveId FDiversionSaveDecisionGate::GetPackageSaveId(const UPackage* InPackage)
{
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	return InPackage->GetSavedHash();
#elif !UE_VERSION_OLDER_THAN(5, 4, 0)
	// GetSavedHash is not const before 5.6
	return const_cast<UPackage*>(InPackage)->GetSavedHash();
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return InPackage->GetGuid();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
}

FDiversionPackageSaveId FDiversionSaveDecisionGate::GetExpectedFileSaveId(const UPackage* InPackage) const
{
#if DIVERSION_TRACKS_FILE_SAVE_IDS
	if (const FDiversionPackageSaveId* Known = FileSaveIds.Find(InPackage))
	{
		return *Known;
	}
#endif
	return GetPackageSaveId(InPackage);
}

#if DIVERSION_TRACKS_FILE_SAVE_IDS
void FDiversionSaveDecisionGate::RememberFileSaveId(const UPackage* InPackage)
{
	// Every save reaches the gate before it renews the id, so the first one seen is still the
	// id the package was loaded with
	if (InPackage != nullptr && !FileSaveIds.Contains(InPackage))
	{
		FileSaveIds.Add(InPackage, GetPackageSaveId(InPackage));
	}
}

void FDiversionSaveDecisionGate::OnPackageSaved(const FString& InFilename, UPackage* InPackage,
	FObjectPostSaveContext InSaveContext)
{
	// Only a save to the package's own file changes what that file holds; an autosave's backup
	// copy renewed the package's id and left the file alone
	FString SavedPackageName;
	if (InPackage != nullptr && IsInGameThread() &&
		FPackageName::TryConvertFilenameToLongPackageName(InFilename, SavedPackageName) &&
		SavedPackageName == InPackage->GetName())
	{
		FileSaveIds.Add(InPackage, GetPackageSaveId(InPackage));
	}
}
#endif

void FDiversionSaveDecisionGate::ReloadAcceptedIncoming()
{
	for (const FString& Path : AcceptedIncomingPaths)
	{
		UPackage* Package = FindPackageForFile(Path);
		if (Package == nullptr)
		{
			continue;
		}
		// The unsaved changes were the user's to drop: clean first, so the reload does not ask
		// about them again
		if (DiversionUtils::UPackageUtils::IsPackageOpenedInEditor(Package->GetName()))
		{
			DiversionUtils::UPackageUtils::ClosePackageEditor(Package);
		}
		Package->SetDirtyFlag(false);
		DiversionUtils::UPackageUtils::DiscardUnsavedChanges(Package);
	}
	AcceptedIncomingPaths.Empty();
}

bool FDiversionSaveDecisionGate::RefuseSave(const FString& InFilename, FOutputDevice* ErrorLog,
	const FText& InToast)
{
	RefusedSaveNoise.NoteRefused(InFilename, ErrorLog);
	ScheduleSaveGestureReset();
	if (!InToast.IsEmpty())
	{
		ToastOncePerSave(FPaths::ConvertRelativePathToFull(InFilename), InToast);
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
