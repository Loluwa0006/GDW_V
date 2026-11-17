// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Runtime/Launch/Resources/Version.h"

// UE_VERSION_NEWER_THAN_OR_EQUAL was introduced in UE 5.6
// Include Epic's header when available, otherwise define our own fallback
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
#include "Misc/EngineVersionComparison.h"
#else

#ifndef UE_VERSION_NEWER_THAN_OR_EQUAL
#define UE_VERSION_NEWER_THAN_OR_EQUAL(MajorVersion, MinorVersion, PatchVersion) \
	((ENGINE_MAJOR_VERSION > (MajorVersion)) || \
	 (ENGINE_MAJOR_VERSION == (MajorVersion) && ENGINE_MINOR_VERSION > (MinorVersion)) || \
	 (ENGINE_MAJOR_VERSION == (MajorVersion) && ENGINE_MINOR_VERSION == (MinorVersion) && ENGINE_PATCH_VERSION >= (PatchVersion)))
#endif

#ifndef UE_VERSION_OLDER_THAN
#define UE_VERSION_OLDER_THAN(MajorVersion, MinorVersion, PatchVersion) \
	((ENGINE_MAJOR_VERSION < (MajorVersion)) || \
	 (ENGINE_MAJOR_VERSION == (MajorVersion) && ENGINE_MINOR_VERSION < (MinorVersion)) || \
	 (ENGINE_MAJOR_VERSION == (MajorVersion) && ENGINE_MINOR_VERSION == (MinorVersion) && ENGINE_PATCH_VERSION < (PatchVersion)))
#endif

#endif
