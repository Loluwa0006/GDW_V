// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionHttpConfig.h"
#include "HAL/IConsoleManager.h"

bool IsDiversionConnectionPoolMetricsEnabled()
{
	static IConsoleVariable* CVar = nullptr;
	if (!CVar)
	{
		CVar = IConsoleManager::Get().FindConsoleVariable(DiversionHttpConfig::CVarEnableConnectionPoolMetricsName);
	}
	return CVar ? CVar->GetBool() : false;
}
