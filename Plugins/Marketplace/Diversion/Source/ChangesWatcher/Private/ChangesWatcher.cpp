// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "ChangesWatcher.h"

// Diversion includes
#include "ChangeBatcher.h"
#include "DiversionNotificationManager.h"
#include "DiversionVersionCompat.h"

// UE includes
#include "Algo/Copy.h"
#include "HAL/FileManager.h"
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/ObjectSaveContext.h"
#include "FileHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/MapBuildDataRegistry.h"
#include "Misc/PackagePath.h"
#include "Editor.h"
#include "Selection.h"
#include "Subsystems/AssetEditorSubsystem.h"

#define LOCTEXT_NAMESPACE "FChangesWatcherModule"

namespace
{
	// External actor/object packages live at:
	//   <MountPoint>/<__ExternalActors__|__ExternalObjects__>/<MapPath>/<shard1>/<shard2>/<name>
	// (see ULevel::GetActorPackageName / FExternalPackageHelper::GetExternalPackageName).
	// Reloading just the per-actor package isn't enough for heavy actors like landscapes —
	// the engine's package reload doesn't fully re-init landscape components. Resolving back
	// to the parent umap and reloading that flows through UPackageTools::ReloadPackages'
	// CreateNewMapForEditing + OpenEditorsForAssets path, which recreates the world and
	// re-streams all WP cells.
	bool TryGetParentMapForExternalPackage(const FString& AbsPath, FString& OutMapAbsPath)
	{
		FString LongPkgName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(AbsPath, LongPkgName))
		{
			return false;
		}

		auto MakeFolderToken = [](const TCHAR* FolderName)
		{
			return FString::Printf(TEXT("/%s/"), FolderName);
		};
		const FString ExternalActorsToken = MakeFolderToken(FPackagePath::GetExternalActorsFolderName());
		const FString ExternalObjectsToken = MakeFolderToken(FPackagePath::GetExternalObjectsFolderName());

		auto ResolveWithToken = [&LongPkgName](const FString& Token, FString& OutMapLongName) -> bool
		{
			const int32 TokenIdx = LongPkgName.Find(Token);
			if (TokenIdx == INDEX_NONE)
			{
				return false;
			}
			const FString MountPoint = LongPkgName.Left(TokenIdx);
			FString Tail = LongPkgName.Mid(TokenIdx + Token.Len());

			// Strip 3 trailing components: shard1 / shard2 / actor-or-object name.
			// The packaging scheme (Original vs Reduced) only affects shard length,
			// not the number of shards, so stripping a fixed count is correct.
			for (int32 I = 0; I < 3; ++I)
			{
				int32 LastSlash = INDEX_NONE;
				if (!Tail.FindLastChar(TEXT('/'), LastSlash))
				{
					return false;
				}
				Tail.LeftInline(LastSlash);
			}
			if (Tail.IsEmpty() || MountPoint.IsEmpty())
			{
				return false;
			}
			OutMapLongName = MountPoint + TEXT("/") + Tail;
			return true;
		};

		FString MapLongName;
		if (!ResolveWithToken(ExternalActorsToken, MapLongName) &&
			!ResolveWithToken(ExternalObjectsToken, MapLongName))
		{
			return false;
		}

		if (!FPackageName::TryConvertLongPackageNameToFilename(
				MapLongName, OutMapAbsPath, FPackageName::GetMapPackageExtension()))
		{
			return false;
		}
		OutMapAbsPath = FPaths::ConvertRelativePathToFull(OutMapAbsPath);
		return true;
	}

	// Convert an absolute file path to a long package name and look up the
	// in-memory UPackage, if any. Returns nullptr when the path doesn't resolve
	// to a UE package or the package isn't currently loaded.
	UPackage* FindLoadedPackageForPath(const FString& AbsPath)
	{
		FString LongPkgName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(AbsPath, LongPkgName))
		{
			return nullptr;
		}
		return FindPackage(nullptr, *LongPkgName);
	}

	// Returns true if any of the supplied paths corresponds to the package of
	// the editor's currently-open world. Used to refuse purges that would yank
	// the active world out from under the editor.
	bool IsActiveWorldInPaths(const TArray<FString>& AbsoluteFilenames)
	{
		UWorld* CurrentWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		UPackage* EditedMapPackage = CurrentWorld ? CurrentWorld->GetOutermost() : nullptr;
		if (!EditedMapPackage)
		{
			return false;
		}

		for (const FString& AbsPath : AbsoluteFilenames)
		{
			if (FindLoadedPackageForPath(AbsPath) == EditedMapPackage)
			{
				return true;
			}
		}
		return false;
	}
}

FChangesWatcher::FChangesWatcher()
{
	ContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	if (FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
		if (IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get())
		{
			DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(
				ContentDir,
				IDirectoryWatcher::FDirectoryChanged::CreateRaw(this, &FChangesWatcher::HandleDirectoryChanged),
				WatcherHandle,
				IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges
			);
			UE_LOG(LogChangesWatcher, Log, TEXT("Watching content folder: %s"), *ContentDir);
		}
	}

	// Listen for UE's internal save to track files that were changed internally
	PostSaveHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FChangesWatcher::TrackInternallyChangedPackages);


	Batcher = MakeShared<FChangeBatcher>(
		SilenceSecondsBeforeAcceptBatch,
		FChangeBatcher::FOnBatchReady::CreateRaw(this, &FChangesWatcher::HandleChangesBatch)
	);
	NotificationManager = MakeUnique<FDiversionNotificationManager>();
}

FChangesWatcher::~FChangesWatcher()
{
	if (FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
		if (IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get())
		{
			DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(ContentDir, WatcherHandle);
		}
	}

	UPackage::PackageSavedWithContextEvent.Remove(PostSaveHandle);
	StopSnooze();
}

void FChangesWatcher::HandleDirectoryChanged(const TArray<struct FFileChangeData>& FileChanges)
{
	const double Now = FPlatformTime::Seconds();
	for (const FFileChangeData& Change : FileChanges)
	{
		if (Change.Action != FFileChangeData::FCA_Added &&
			Change.Action != FFileChangeData::FCA_Modified &&
			Change.Action != FFileChangeData::FCA_Removed)
		{
			continue;
		}

		const FString& File = Change.Filename;
		// Filter to only UE packages: .uasset or .umap
		const bool bIncludeDot = true;
		if (!FPackageName::IsPackageExtension(*FPaths::GetExtension(File, bIncludeDot)))
		{
			continue;
		}

		if (FSaveStamp* Stamp = EngineWriteMap.Find(File))
		{
			const bool  WithinWindow = (Now - Stamp->LastHitTime) < QUIET_WINDOW_SEC;
			const int64 CurrentSize = IFileManager::Get().FileSize(*File);
			const bool  SizeUnchanged = CurrentSize == Stamp->SavedSize;

			if (WithinWindow && SizeUnchanged)
			{
				// Still part of UEs own burst -> ignore
				UE_LOG(LogChangesWatcher, VeryVerbose, TEXT("Ignoring %s - within quiet window (%.3fs ago, same size %lld)"),
					*File, Now - Stamp->LastHitTime, CurrentSize);
				Stamp->LastHitTime = Now;   // extend window
				continue;
			}
			else if (WithinWindow)
			{
				UE_LOG(LogChangesWatcher, Verbose, TEXT("Processing %s - within window but size changed (%lld -> %lld)"),
					*File, Stamp->SavedSize, CurrentSize);
			}
		}

		UE_LOG(LogChangesWatcher, Verbose, TEXT("Adding change to batch: %s (Action: %d, Size: %lld)"),
			*Change.Filename, (int32)Change.Action, IFileManager::Get().FileSize(*Change.Filename));
		Batcher->AddChange(Change.Filename);
	}

	for (auto It = EngineWriteMap.CreateIterator(); It; ++It)
	{
		// Updtae the entries that track internal engine changes
		if (Now - It->Value.LastHitTime >= QUIET_WINDOW_SEC)
		{
			It.RemoveCurrent();
		}
	}
}

void FChangesWatcher::TrackInternallyChangedPackages(const FString& PackageFileName, UPackage*, FObjectPostSaveContext Context)
{
	if (Context.SaveSucceeded())
	{
		FString PackageFilePath = FPaths::ConvertRelativePathToFull(PackageFileName);
		const int64 Size = IFileManager::Get().FileSize(*PackageFilePath);
		UE_LOG(LogChangesWatcher, Verbose, TEXT("Tracking internal save: %s (Size: %lld)"),
			*PackageFilePath, Size);
		EngineWriteMap.FindOrAdd(PackageFilePath) = { FPlatformTime::Seconds(), Size };
	}
}

void FChangesWatcher::HandleChangesBatch(uint32 BatchId, const TSet<FString>& IncomingChanges)
{
	StopSnooze();

	// Resolve WP external actor/object packages back to their parent umap. Reloading
	// the umap goes through the engine's persistent-world reload path which fully
	// recreates the world and its actors -- needed for landscapes etc., where a
	// per-actor package reload alone doesn't refresh landscape components.
	TSet<FString> ResolvedChanges;
	for (const FString& AbsPath : IncomingChanges)
	{
		FString MapAbsPath;
		if (TryGetParentMapForExternalPackage(AbsPath, MapAbsPath))
		{
			UE_LOG(LogChangesWatcher, Verbose, TEXT("External package %s -> parent map %s"),
				*AbsPath, *MapAbsPath);
			ResolvedChanges.Add(MapAbsPath);
		}
		else
		{
			ResolvedChanges.Add(AbsPath);
		}
	}

	// Filtering out packages that are the same in cache to avoid unnecessary reloads
	TSet<FString> PackagesToReload;

	Algo::CopyIf(ResolvedChanges, PackagesToReload, [this](const FString& InChangeFileName) {
		return IsPackageDifferentOnDisk(InChangeFileName);
	});

	// If no packages to reload after the copy - just resolve the batch and return
	if (PackagesToReload.Num() == 0)
	{
		Batcher->ResolveBatch(BatchId, EBatchDisposition::Accept);
		UE_LOG(LogChangesWatcher, Log, TEXT("No changes to reload in batch %d"), BatchId);
		return;
	}

	UE_LOG(LogChangesWatcher, Log, TEXT("Batch %d: %d incoming changes, %d filtered for reload"),
		BatchId, IncomingChanges.Num(), PackagesToReload.Num());

	for (const FString& Package : PackagesToReload)
	{
		UE_LOG(LogChangesWatcher, Verbose, TEXT("Package marked for reload: %s"), *Package);
	}

	FNotificationButtonInfo ReloadButton(LOCTEXT("DiversionPopup_ReloadChangesButton", "Reload"),
		LOCTEXT("DiversionPopup_ReloadChangesButton_Tooltip",
			"Reload assets from disk, to get updates from teammates."
			"This ensures that any future modifications you make are on top of your teammate's changes, to prevent conflicts."),
		FSimpleDelegate::CreateLambda([this, PackagesToReload, IncomingChanges, BatchId]()
			{
				// Partition into modifications (file present on disk) vs deletions
				// (file gone). Deletions need a purge path; live-package reload of a
				// missing file would just produce an engine "failed to reload" error.
				TArray<FString> ToReload;
				TArray<FString> ToPurge;
				for (const FString& Path : PackagesToReload)
				{
					if (FPaths::FileExists(Path))
					{
						ToReload.Add(Path);
					}
					else
					{
						ToPurge.Add(Path);
					}
				}

				// PIE / SIE running blocks the world rebuild we're about to trigger:
				// CreateNewMapForEditing -> ShouldAbortBecauseOfPIEWorld silently
				// terminates PIE in AssumePositive mode. Surface this and snooze
				// instead so the user stops PIE deliberately before we proceed.
				if (ToPurge.Num() > 0 && GEditor && GEditor->PlayWorld != nullptr)
				{
					Snooze(BatchId, IncomingChanges);
					FText PIEActiveMsg = LOCTEXT("ChangesWatcherPIEActive",
						"Stop Play In Editor before reloading -- some changes require a level rebuild.");
					FDiversionNotification PIEActiveNotification(
						PIEActiveMsg,
						TArray<FNotificationButtonInfo>(),
						SNotificationItem::CS_Fail
					);
					NotificationManager->ShowNotification(PIEActiveNotification);
					return;
				}

				// Refuse to purge the active editor world's package -- the editor
				// can't keep running with its world UObject ripped out. Checked
				// before the active-world injection below so this "your open level
				// was deleted" case wins over the dirty-conflict notification, and
				// so the IsActiveWorldInPaths(ToPurge) check sees the unmodified
				// partition.
				if (IsActiveWorldInPaths(ToPurge))
				{
					Snooze(BatchId, IncomingChanges);
					FText ActiveWorldDeletedMsg = LOCTEXT("ChangesWatcherActiveMapDeleted",
						"The level you have open was deleted. Open a different level first, then reload.");
					FDiversionNotification ActiveWorldDeletedNotification(
						ActiveWorldDeletedMsg,
						TArray<FNotificationButtonInfo>(),
						SNotificationItem::CS_Fail
					);
					NotificationManager->ShowNotification(ActiveWorldDeletedNotification);
					return;
				}

				// When anything is being purged, also rebuild the active editor world.
				// Purged assets are typically still referenced by actors in the world
				// (UClass pointers, mesh refs, etc.) -- those refs keep the package
				// alive past GC, so the viewport keeps rendering the stale state. The
				// world rebuild releases the refs and surfaces missing-asset state.
				if (ToPurge.Num() > 0)
				{
					if (UWorld* ActiveWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
					{
						if (UPackage* WorldPkg = ActiveWorld->GetOutermost())
						{
							FString WorldFile;
							if (FPackageName::TryConvertLongPackageNameToFilename(
									WorldPkg->GetName(), WorldFile, FPackageName::GetMapPackageExtension()))
							{
								ToReload.AddUnique(FPaths::ConvertRelativePathToFull(WorldFile));
							}
						}
					}
				}

				// Block reload on dirty packages that are part of this batch -- run
				// after the active-world injection above so a dirty open level is
				// caught when an unrelated delete forces it into the reload set.
				// Without this ordering, ReloadPackagesFromDisk would silently
				// rebuild the dirty world and destroy in-progress edits. Unrelated
				// dirty assets elsewhere in the editor still don't block reload.
				// For map packages FindDirtyConflicts also walks sub-levels and
				// map build data.
				TSet<FString> DirtyCheckSet;
				DirtyCheckSet.Append(ToReload);
				DirtyCheckSet.Append(ToPurge);
				TArray<FString> DirtyConflicts = FindDirtyConflicts(DirtyCheckSet);
				if (DirtyConflicts.Num() > 0)
				{
					Snooze(BatchId, IncomingChanges);

					const int32 MaxNamesShown = 5;
					TArray<FText> Shown;
					for (int32 Index = 0; Index < FMath::Min(DirtyConflicts.Num(), MaxNamesShown); ++Index)
					{
						Shown.Add(FText::FromString(DirtyConflicts[Index]));
					}
					FText JoinedNames = FText::Join(LOCTEXT("ChangesWatcherNamesSeparator", ", "), Shown);
					if (DirtyConflicts.Num() > MaxNamesShown)
					{
						JoinedNames = FText::Format(LOCTEXT("ChangesWatcherNamesMore", "{0}, and {1} more"),
							JoinedNames, FText::AsNumber(DirtyConflicts.Num() - MaxNamesShown));
					}

					FText OpenedChangesMessage = FText::Format(
						LOCTEXT("ChangesWatcherUnsavedChangesExist",
							"Packages reload failed. Please save these unsaved packages first: {0}"),
						JoinedNames);
					FDiversionNotification UnsavedChangesErrorNotification(
						OpenedChangesMessage,
						TArray<FNotificationButtonInfo>(),
						SNotificationItem::CS_Fail
					);
					NotificationManager->ShowNotification(UnsavedChangesErrorNotification);
					return;
				}

				// Purge before reload: the purge clears RF_Standalone on deleted
				// packages, but world refs keep them alive. The subsequent world
				// reload destroys the world (releasing refs) and runs GC inside
				// CreateNewMapForEditing -- now the deleted packages collect for
				// real. The new world then loads from disk and surfaces missing-
				// asset state. Reverse order leaves the deleted package in memory
				// because it still has RF_Standalone during the world rebuild.
				PurgePackagesFromDisk(ToPurge);
				ReloadPackagesFromDisk(ToReload);
				Batcher->ResolveBatch(BatchId, EBatchDisposition::Accept);
				UE_LOG(LogChangesWatcher, Log, TEXT("Batch %d: purged %d, reloaded %d"),
					BatchId, ToPurge.Num(), ToReload.Num());
			}),
		SNotificationItem::CS_Pending);

	FNotificationButtonInfo SnoozeButton(LOCTEXT("DiversionPopup_SnoozeChangesButton", "Snooze"),
		LOCTEXT("DiversionPopup_SnoozeChangesButton_Tooltip", "Remind me again in 30 seconds"),
		FSimpleDelegate::CreateLambda([this, BatchId, IncomingChanges]()
			{
				Snooze(BatchId, IncomingChanges);
			}),
		SNotificationItem::CS_Pending);

	FText OpenedChangesMessage = LOCTEXT("ChangesWatcherBatchReady",
		"Diversion: Some open assets may have changed on disk. Consider reloading to ensure you're working with the latest version.");

	FDiversionNotification PopUpNotification(
		OpenedChangesMessage,
		TArray<FNotificationButtonInfo>({ ReloadButton, SnoozeButton }),
		SNotificationItem::CS_Pending,
		/*bSticky*/ true
	);

	NotificationManager->ShowNotification(PopUpNotification);
}

bool FChangesWatcher::IsPackageDifferentOnDisk(const FString& AbsFilePath)
{
	// Deletions: the file is gone from disk. The hash compare is unreliable for
	// missing files (registry may have stale data). Always flag as different so
	// the deletion flows through to the purge path.
	if (!FPaths::FileExists(AbsFilePath))
	{
		return true;
	}

	// Map packages: the asset registry's saved hash for an open world is updated
	// lazily, so the on-disk vs in-memory hash compare can return false negatives
	// and silently filter the umap out of the reload set. Always treat .umap
	// changes as different and let the reload path handle them.
	if (FPaths::GetExtension(AbsFilePath, /*bIncludeDot=*/false).Equals(TEXT("umap"), ESearchCase::IgnoreCase))
	{
		return true;
	}

	FString LongPackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(AbsFilePath, LongPackageName))
	{
		return true; // bad path is treat as "different value on disk"
	}

	const FName PackageFName(*LongPackageName);
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	TOptional<FAssetPackageData> DiskData = ARM.Get().GetAssetPackageDataCopy(PackageFName);
	if (!DiskData.IsSet())
	{
		ARM.Get().ScanModifiedAssetFiles({ AbsFilePath });
		DiskData = ARM.Get().GetAssetPackageDataCopy(PackageFName);
		if (!DiskData.IsSet())
		{
			return true; // Not in the registry, so assume it's different
		}
	}

#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 4, 0)
	const FIoHash DiskHash = DiskData->GetPackageSavedHash();
	if (UPackage* CachedPkg = FindPackage(nullptr, *LongPackageName))
	{
		const FIoHash MemHash = CachedPkg->GetSavedHash();
		bool bDifferent = DiskHash != MemHash;
		UE_LOG(LogChangesWatcher, Verbose, TEXT("Package comparison - %s: Different=%s"),
			*LongPackageName, bDifferent ? TEXT("YES") : TEXT("NO"));
		return bDifferent;
	}
#else
	// UE 5.3 and earlier - use GUID comparison instead of hash
	const FGuid DiskGuid = DiskData->PackageGuid;
	if (UPackage* CachedPkg = FindPackage(nullptr, *LongPackageName))
	{
		const FGuid MemGuid = CachedPkg->GetGuid();
		bool bDifferent = DiskGuid != MemGuid;
		UE_LOG(LogChangesWatcher, Verbose, TEXT("Package comparison - %s: DiskGuid=%s, MemGuid=%s, Different=%s"),
			*LongPackageName, *DiskGuid.ToString(), *MemGuid.ToString(),
			bDifferent ? TEXT("YES") : TEXT("NO"));
		return bDifferent;
	}
#endif

	// Package is not loaded, so we can't compare hashes - assume it's different
	UE_LOG(LogChangesWatcher, Verbose, TEXT("Package %s not loaded in memory - assuming different"), *LongPackageName);
	return true;
}

bool FChangesWatcher::ReloadPackagesFromDisk(const TArray<FString>& AbsoluteFilenames)
{
	if (AbsoluteFilenames.Num() == 0)
	{
		return false;
	}

	// Filter out paths that don't map to valid UE package names (e.g. '.dv-conflict.uasset'
	// sidecars) — ScanModifiedAssetFiles calls the fatal FilenameToLongPackageName internally.
	TArray<FString> ValidPackageFiles = AbsoluteFilenames.FilterByPredicate([](const FString& Filename)
	{
		FString Unused;
		return FPackageName::TryConvertFilenameToLongPackageName(Filename, Unused);
	});

	// Log anything the filter dropped so a real .uasset rejected here (unexpected illegal
	// chars, encoding weirdness) is visible instead of disappearing without a trace.
	if (ValidPackageFiles.Num() < AbsoluteFilenames.Num())
	{
		for (const FString& Filename : AbsoluteFilenames)
		{
			if (!ValidPackageFiles.Contains(Filename))
			{
				UE_LOG(LogChangesWatcher, Verbose, TEXT("Skipping %s -- does not map to a valid UE package name"), *Filename);
			}
		}
	}

	if (ValidPackageFiles.Num() == 0)
	{
		return false;
	}

	// Refresh the asset registry's package data for the changed files so the
	// content browser shows current thumbnails/info immediately after reload.
	// UPackageTools::ReloadPackages itself doesn't depend on this; the linker
	// reload reads the file fresh.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	ARM.Get().ScanModifiedAssetFiles(ValidPackageFiles);

	TArray<UPackage*> Pkgs;
	TArray<UPackage*> BlueprintPackages;

	for (const FString& AbsoluteFilename : ValidPackageFiles)
	{
		// Close editors & collect package pointer if loaded
		if (UPackage* Pkg = FindLoadedPackageForPath(AbsoluteFilename))
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			UObject* Asset = Pkg->FindAssetInPackage();

			// Check if this is a Blueprint package for special handling
			if (Asset && Asset->IsA<UBlueprint>())
			{
				BlueprintPackages.Add(Pkg);
			}

			// Skip for UWorld: the level editor isn't an asset editor, and
			// UPackageTools::ReloadPackages already handles persistent-world
			// swap via CreateNewMapForEditing + OpenEditorsForAssets.
			if (Asset && !Asset->IsA<UWorld>())
			{
				AssetEditorSubsystem->CloseAllEditorsForAsset(Asset);
			}
			Pkgs.Add(Pkg);
		}
	}

	if (Pkgs.Num() == 0)
	{
		return false;
	}

	// UE 5.7's UPackageTools::ReloadPackages restores the persistent world via
	// OpenEditorsForAssets after its CreateNewMapForEditing swap; 5.3-5.6 skip
	// that restoration step and leave the user on an Untitled map. Cache the
	// active world's path now and re-open it below if the engine didn't.
	// Idempotent on 5.7 -- the path check skips when it already matches.
	FName ActiveWorldPathToRestore = NAME_None;
	if (UWorld* ActiveWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		if (UPackage* ActiveWorldPkg = ActiveWorld->GetOutermost())
		{
			if (Pkgs.Contains(ActiveWorldPkg))
			{
				ActiveWorldPathToRestore = FName(*ActiveWorld->GetPathName());
			}
		}
	}

	bool bAnyReloaded = false;
	FText Error;

	// UPackageTools::ReloadPackages already sorts packages by dependency
	// (::SortPackagesForReload) and handles persistent-world swap.
	UEditorLoadingAndSavingUtils::ReloadPackages(
		Pkgs,
		/*out*/ bAnyReloaded,
		/*out*/ Error,
		EReloadPackagesInteractionMode::AssumePositive);

	// Re-open the persistent world on UE versions where the engine's reload
	// path doesn't restore it. Run before the error early-return because the
	// kick-to-Untitled (CreateNewMapForEditing) happens regardless of whether
	// the subsequent package reload reports an error.
	if (!ActiveWorldPathToRestore.IsNone() && GEditor)
	{
		UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
		const FName CurrentPath = CurrentWorld
			? FName(*CurrentWorld->GetPathName())
			: NAME_None;
		if (CurrentPath != ActiveWorldPathToRestore)
		{
			if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				UE_LOG(LogChangesWatcher, Log,
					TEXT("Re-opening persistent world after reload: %s"),
					*ActiveWorldPathToRestore.ToString());
				AssetEditorSubsystem->OpenEditorsForAssets({ ActiveWorldPathToRestore });
			}
		}
	}

	if (!Error.IsEmpty())
	{
		UE_LOG(LogChangesWatcher, Warning, TEXT("Reload error: %s"), *Error.ToString());
		return false;
	}

	if (bAnyReloaded && BlueprintPackages.Num() > 0)
	{
		ValidatePostReloadState(BlueprintPackages);
	}

	if (bAnyReloaded)
	{
		UE_LOG(LogChangesWatcher, Log, TEXT("Successfully reloaded %d packages (%d Blueprints)"),
			Pkgs.Num(), BlueprintPackages.Num());
	}

	return bAnyReloaded;
}

bool FChangesWatcher::PurgePackagesFromDisk(const TArray<FString>& AbsoluteFilenames)
{
	// Note: UObjectRedirectors in *other* packages that point at the assets
	// we're purging will become dangling after this. The standard
	// ObjectTools::DeleteObjects flow prompts to fix them up; we skip that --
	// next-load resolution will surface them as missing/broken references.
	if (AbsoluteFilenames.Num() == 0)
	{
		return false;
	}

	TArray<UObject*> ObjectsToPurge;
	auto CollectObjectToPurge = [&ObjectsToPurge](UObject* Obj)
	{
		if (!Obj || ObjectsToPurge.Contains(Obj))
		{
			return;
		}
		if (Obj->IsAsset() && GEditor)
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Obj);
			GEditor->GetSelectedObjects()->Deselect(Obj);
		}
		ObjectsToPurge.Add(Obj);
	};

	for (const FString& AbsPath : AbsoluteFilenames)
	{
		UPackage* Pkg = FindLoadedPackageForPath(AbsPath);
		if (!Pkg)
		{
			// Not loaded in memory -- registry will catch up via its own watcher.
			continue;
		}

		// Clear dirty so the engine doesn't refuse GC for an unsaved package.
		// Dirty packages should already have been blocked by FindDirtyConflicts;
		// this is a defensive belt for any edge case that slipped through.
		if (Pkg->IsDirty())
		{
			UE_LOG(LogChangesWatcher, Warning,
				TEXT("Purging dirty package %s -- FindDirtyConflicts didn't catch it"),
				*Pkg->GetName());
		}
		Pkg->SetDirtyFlag(false);
		CollectObjectToPurge(Pkg);

		// Two-phase: gather subobjects first, then act on them. CollectObjectToPurge
		// invokes broadcasts (closing asset editors) which can call NewObject, and
		// NewObject during ForEachObjectWithPackage's iteration is illegal.
		TArray<UObject*> ObjectsInPackage;
		ForEachObjectWithPackage(Pkg, [&ObjectsInPackage](UObject* Obj)
		{
			ObjectsInPackage.Add(Obj);
			return true;
		});
		for (UObject* Subobject : ObjectsInPackage)
		{
			CollectObjectToPurge(Subobject);
		}
	}

	if (ObjectsToPurge.Num() == 0)
	{
		return false;
	}

	// Let subsystems (BlueprintActionDatabase etc.) drop references before GC.
	FEditorDelegates::OnAssetsPreDelete.Broadcast(ObjectsToPurge);

	for (UObject* Obj : ObjectsToPurge)
	{
		if (Obj->IsRooted())
		{
			Obj->RemoveFromRoot();
		}
		Obj->ClearFlags(RF_Public | RF_Standalone);
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	UE_LOG(LogChangesWatcher, Log, TEXT("Purged %d objects from %d package(s)"),
		ObjectsToPurge.Num(), AbsoluteFilenames.Num());

	return true;
}

TArray<FString> FChangesWatcher::FindDirtyConflicts(const TSet<FString>& PackagesToReload)
{
	// UPackage::IsDirty only catches state marked dirty via UObject::Modify
	// (which the editor's transactions do). Mid-edit changes that haven't
	// yet called Modify can slip through -- same blind spot as the engine's
	// UEditorLoadingAndSavingUtils::GetDirtyContentPackages.
	TArray<FString> DirtyNames;
	TSet<UPackage*> Visited;

	auto CheckPackage = [&](UPackage* Pkg)
	{
		if (!Pkg || Visited.Contains(Pkg))
		{
			return;
		}
		Visited.Add(Pkg);
		if (Pkg->IsDirty())
		{
			DirtyNames.AddUnique(Pkg->GetName());
		}
	};

	for (const FString& AbsPath : PackagesToReload)
	{
		UPackage* Pkg = FindLoadedPackageForPath(AbsPath);
		if (!Pkg)
		{
			continue;
		}

		CheckPackage(Pkg);

		// For a map package, sub-levels and the map build data are conceptually
		// part of "this level" -- a dirty sub-level or build data registry would
		// be lost on reload, so block in that case too. World Partition routes
		// per-actor edits into external actor packages (one .uasset per actor
		// under __ExternalActors__/<map>/...), so moving an actor in a WP world
		// dirties that external package, not the persistent map's package.
		// Walk Level->Actors so the WP case is covered.
		if (Pkg->ContainsMap())
		{
			if (UWorld* World = UWorld::FindWorldInPackage(Pkg))
			{
				for (ULevel* Level : World->GetLevels())
				{
					if (!Level)
					{
						continue;
					}
					CheckPackage(Level->GetOutermost());
					if (UMapBuildDataRegistry* BuildData = Level->MapBuildData)
					{
						CheckPackage(BuildData->GetOutermost());
					}
					for (AActor* Actor : Level->Actors)
					{
						if (!Actor)
						{
							continue;
						}
						if (UPackage* ExtPkg = Actor->GetExternalPackage())
						{
							CheckPackage(ExtPkg);
						}
					}
				}
			}
		}
	}

	return DirtyNames;
}

void FChangesWatcher::Snooze(uint32 BatchId, const TSet<FString>& IncomingChanges)
{
	Batcher->ResolveBatch(BatchId, EBatchDisposition::Snooze);
	// Start snooze
	if (!SnoozeTimerHandle.IsValid())
	{
		// Avoid redundant AsyncTask if already on game thread
		if (IsInGameThread())
		{
			SnoozeTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([this, BatchId, IncomingChanges](float DeltaTime) {
					HandleChangesBatch(BatchId, IncomingChanges);
					return false;
					}), 30.0f);
		}
		else
		{
			TWeakPtr<FChangesWatcher> WeakPtr = AsShared();
			AsyncTask(ENamedThreads::GameThread, [WeakPtr, BatchId, IncomingChanges]() mutable {
				if (TSharedPtr<FChangesWatcher> Pinned = WeakPtr.Pin())
				{
					if (!Pinned->SnoozeTimerHandle.IsValid())
					{
						Pinned->SnoozeTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
							FTickerDelegate::CreateLambda([Pinned, BatchId, IncomingChanges](float DeltaTime) {
								Pinned->HandleChangesBatch(BatchId, IncomingChanges);
								return false;
								}),
							30.0f);
					}
				}
				});
		}
	}
}

void FChangesWatcher::StopSnooze()
{
	if (SnoozeTimerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SnoozeTimerHandle);
		SnoozeTimerHandle.Reset();
	}
}

void FChangesWatcher::ValidatePostReloadState(const TArray<UPackage*>& ReloadedPackages)
{
	for (UPackage* Package : ReloadedPackages)
	{
		if (!IsValid(Package))
		{
			continue;
		}

		if (UBlueprint* Blueprint = Cast<UBlueprint>(Package->FindAssetInPackage()))
		{
			// Check if Blueprint needs recompilation
			if (Blueprint->Status == BS_Dirty || Blueprint->Status == BS_Unknown)
			{
				UE_LOG(LogChangesWatcher, Log, TEXT("Blueprint %s needs recompilation after reload"),
					*Blueprint->GetName());

				// Mark for compilation - UE will handle the actual compilation timing
				// This ensures Blueprint gets recompiled with proper dependency order
				Blueprint->Status = BS_Dirty;
			}

			// Validate Blueprint class integrity
			if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->GetDefaultObject())
			{
				UE_LOG(LogChangesWatcher, Verbose, TEXT("Blueprint %s validation passed"),
					*Blueprint->GetName());
			}
			else
			{
				UE_LOG(LogChangesWatcher, Warning, TEXT("Blueprint %s may have compilation issues after reload"),
					*Blueprint->GetName());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

DEFINE_LOG_CATEGORY(LogChangesWatcher);
