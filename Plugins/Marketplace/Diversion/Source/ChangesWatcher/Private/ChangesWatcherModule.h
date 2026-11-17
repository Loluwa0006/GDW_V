// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "ChangesWatcher.h"

class FChangesWatcherModule : public IModuleInterface, public TSharedFromThis<FChangesWatcherModule>
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FChangesWatcher> ChangesWatcher;
};
