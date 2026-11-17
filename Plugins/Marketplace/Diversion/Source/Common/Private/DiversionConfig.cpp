// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionConfig.h"
#include "UObject/Package.h"

UDiversionConfig::UDiversionConfig()
{
}

bool IsDiversionSoftLockEnabled()
{
		const UDiversionConfig* ConcertClientConfig = GetDefault<UDiversionConfig>();
		return ConcertClientConfig->bEnableSoftLock;
}

bool IsDiversionLockOnSavePromptEnabled()
{
		const UDiversionConfig* DiversionConfig = GetDefault<UDiversionConfig>();
		return DiversionConfig->bPromptToLockOnSave;
}

bool IsDiversionLockByDefaultOnSaveEnabled()
{
		const UDiversionConfig* DiversionConfig = GetDefault<UDiversionConfig>();
		// The setting is hidden while the prompt is off, so it must not take effect then either
		return !DiversionConfig->bPromptToLockOnSave || DiversionConfig->bLockByDefaultOnSave;
}

bool IsDiversionChangesWatcherEnabled()
{
		const UDiversionConfig* DiversionConfig = GetDefault<UDiversionConfig>();
		return DiversionConfig->bEnableChangesWatcher;
}

bool IsDiversionSupportLinksInLogsEnabled()
{
		const UDiversionConfig* DiversionConfig = GetDefault<UDiversionConfig>();
		return DiversionConfig->bShowSupportLinksInLogs;
}

int32 GetDiversionResponseCacheSize()
{
		const UDiversionConfig* DiversionConfig = GetDefault<UDiversionConfig>();
		return FMath::Max(10, DiversionConfig->ResponseCacheSize);
}
