// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogChangesWatcher, Log, All);

class FDiversionNotificationManager;
class FChangeBatcher;
class FObjectPostSaveContext;
struct FFileChangeData;

struct FSaveStamp
{
	double       LastHitTime;   // FPlatformTime::Seconds()
	int64        SavedSize;     // In bytes
};

class FChangesWatcher : public TSharedFromThis<FChangesWatcher>
{
public:
	FChangesWatcher();
	~FChangesWatcher();

private:
	// Called whenever any file under Content/ changes
	void HandleDirectoryChanged(const TArray<struct FFileChangeData>& /*FileChanges*/);
	void TrackInternallyChangedPackages(const FString& PackageFileName, UPackage*, FObjectPostSaveContext);

	void HandleChangesBatch(uint32 BatchId, const TSet<FString>& IncomingChanges);

	// Utility functions
	bool IsPackageDifferentOnDisk(const FString& AbsFilePath);
	bool ReloadPackagesFromDisk(const TArray<FString>& AbsoluteFilename);
	bool PurgePackagesFromDisk(const TArray<FString>& AbsoluteFilenames);
	void ValidatePostReloadState(const TArray<UPackage*>& ReloadedPackages);

	TArray<FString> FindDirtyConflicts(const TSet<FString>& PackagesToReload);
	void Snooze(uint32 BatchId, const TSet<FString>& IncomingChanges);
	void StopSnooze();

	// Delegate handles so we can unregister on shutdown
	FDelegateHandle WatcherHandle;
	FDelegateHandle PostSaveHandle;

	TMap<FString, FSaveStamp> EngineWriteMap;

	// Internal changes Window timeout
	static constexpr double QUIET_WINDOW_SEC = 2.0;

	// Absolute path to the Project's Content folder
	FString ContentDir;
	// External Changes Batching window timeout
	double SilenceSecondsBeforeAcceptBatch = 10.0;
	TSharedPtr<FChangeBatcher> Batcher;
	TUniquePtr<FDiversionNotificationManager> NotificationManager;

	FTSTicker::FDelegateHandle SnoozeTimerHandle;
};

