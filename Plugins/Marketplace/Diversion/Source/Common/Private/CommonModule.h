// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogDiversionCommon, Log, All);

class FCommonModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
