// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "SourceControlHelpers.h"

#if PLATFORM_LINUX || PLATFORM_MAC
#include <unistd.h>
#include <pwd.h>
#endif

namespace DiversionSettingsConstants
{

/** The section of the ini file we load our settings from */
static const FString SettingsSection = TEXT("Diversion.DiversionSettings");

}

const FString FDiversionSettings::GetBinaryPath() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return BinaryPath;
}

// This is called at startup nearly before anything else in our module: BinaryPath will then be used by the provider
void FDiversionSettings::LoadSettings()
{
	FScopeLock ScopeLock(&CriticalSection);
#if PLATFORM_WINDOWS
	FString UserProfilePath = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	BinaryPath = UserProfilePath + TEXT("\\.diversion\\bin\\dv.exe");
#elif PLATFORM_LINUX || PLATFORM_MAC
	struct passwd *pw = getpwuid(getuid());
	FString UserProfilePath = pw->pw_dir;
	BinaryPath = UserProfilePath + TEXT("/.diversion/bin/dv");
#endif
}

void FDiversionSettings::SaveSettings() const
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->SetString(*DiversionSettingsConstants::SettingsSection, TEXT("BinaryPath"), *BinaryPath, IniFile);
}
