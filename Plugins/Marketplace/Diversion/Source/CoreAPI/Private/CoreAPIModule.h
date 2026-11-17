// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCoreAPI, Log, All);

class COREAPI_API CoreAPIModule : public IModuleInterface
{
public:
	void StartupModule() final;
	void ShutdownModule() final;
};
