// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionCommonUtils.h"
#include "DiversionVersionCompat.h"
#include "HAL/PlatformMisc.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <ShlObj.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace DiversionUtils
{

FString GetUserHomeDirectory()
{
#if PLATFORM_WINDOWS
	TCHAR HomeDir[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_PROFILE, nullptr, 0, HomeDir)))
	{
		return FString(HomeDir);
	}
#elif PLATFORM_LINUX || PLATFORM_MAC
	return FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#else
#error Unsupported platform
#endif

	return FString();
}

FString GetOperatingSystemName()
{
#if PLATFORM_WINDOWS
	return TEXT("Windows");
#elif PLATFORM_MAC
	return TEXT("MacOSX");
#elif PLATFORM_LINUX
	return TEXT("Linux");
#else
	return TEXT("Other");
#endif
}

FString UTF8ToFStringSafe(const void* UTF8Data, int32 SizeInBytes, bool bPreserveEmbeddedNulls)
{
	if (UTF8Data == nullptr || SizeInBytes <= 0)
	{
		return FString();
	}

	const char* CharData = static_cast<const char*>(UTF8Data);

	if (bPreserveEmbeddedNulls)
	{
		// Use FUTF8ToTCHAR with explicit size - preserves embedded nulls
		FUTF8ToTCHAR Converter(CharData, SizeInBytes);
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 4, 0)
		return FString::ConstructFromPtrSize(Converter.Get(), Converter.Length());
#else
		return FString(Converter.Length(), Converter.Get());
#endif
	}
	else
	{
		// Create a null-terminated copy at exact boundary
		// This ensures we don't read past SizeInBytes AND stops at embedded nulls
		TArray<char> Buffer;
		Buffer.SetNumUninitialized(SizeInBytes + 1);
		FMemory::Memcpy(Buffer.GetData(), UTF8Data, SizeInBytes);
		Buffer[SizeInBytes] = '\0';

		FUTF8ToTCHAR Converter(Buffer.GetData());
		return FString(Converter.Get());
	}
}

}
