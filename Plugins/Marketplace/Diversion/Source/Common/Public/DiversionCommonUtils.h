// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DiversionUtils
{
	/**
	 * Safely converts UTF-8 encoded data to FString with explicit size handling.
	 *
	 * This function ensures that exactly SizeInBytes are read from the input,
	 * preventing buffer overflows and leftover data issues common with reused buffers.
	 *
	 * @param UTF8Data - Pointer to UTF-8 encoded bytes (does not need to be null-terminated)
	 * @param SizeInBytes - Exact number of bytes to convert
	 * @param bPreserveEmbeddedNulls - If true, continues past embedded null bytes (for binary data).
	 *                                  If false (default), stops at first null byte.
	 * @return FString containing the converted text. Empty string if input is null or size <= 0.
	 *
	 * @note Preserves full Unicode character set (UTF-8 -> UTF-16 conversion)
	 */
	COMMON_API FString UTF8ToFStringSafe(const void* UTF8Data, int32 SizeInBytes, bool bPreserveEmbeddedNulls = false);

	/**
	 * Returns the user's home directory (e.g. C:/Users/username on Windows, /home/username on Linux).
	 */
	COMMON_API FString GetUserHomeDirectory();

	/**
	 * Returns the operating system family name, compatible with the Diversion analytics scheme.
	 * Values: "Windows", "MacOSX", "Linux", or "Other".
	 */
	COMMON_API FString GetOperatingSystemName();
}
