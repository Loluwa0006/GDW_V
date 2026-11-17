// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Console variable configuration constants
namespace DiversionHttpConfig
{
	constexpr const TCHAR* CVarEnableConnectionPoolMetricsName = TEXT("Diversion.Http.EnableConnectionPoolMetrics");
	constexpr const TCHAR* CVarEnableConnectionPoolMetricsHelp = TEXT("Enable HTTP connection pool metrics collection (saved to logs on shutdown)");
}

DIVERSIONHTTP_API bool IsDiversionConnectionPoolMetricsEnabled();
