// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDiversionHttp, Log, All);

class FDiversionHttpModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
