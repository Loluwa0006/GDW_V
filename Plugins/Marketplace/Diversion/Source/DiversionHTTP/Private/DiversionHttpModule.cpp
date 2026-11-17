// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionHttpModule.h"
#include "DiversionHttpConfig.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_MODULE(FDiversionHttpModule, DiversionHttp)
DEFINE_LOG_CATEGORY(LogDiversionHttp);

// Console variable for HTTP connection pool metrics
static TAutoConsoleVariable<bool> CVarEnableConnectionPoolMetrics(
	DiversionHttpConfig::CVarEnableConnectionPoolMetricsName,
	false,
	DiversionHttpConfig::CVarEnableConnectionPoolMetricsHelp,
	ECVF_Default
);

void FDiversionHttpModule::StartupModule()
{
}

void FDiversionHttpModule::ShutdownModule()
{
}

