// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#include "ChangesWatcherModule.h"
#include "Modules/ModuleManager.h"
#include "DiversionConfig.h"

#define LOCTEXT_NAMESPACE "FChangesWatcherModule"

void FChangesWatcherModule::StartupModule()
{
	// Only initialize the ChangesWatcher if it's enabled in settings
	if (IsDiversionChangesWatcherEnabled())
	{
		ChangesWatcher = MakeShared<FChangesWatcher>();
	}
}

void FChangesWatcherModule::ShutdownModule()
{
	// Only reset if it was initialized
	if (ChangesWatcher.IsValid())
	{
		ChangesWatcher.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FChangesWatcherModule, ChangesWatcher)

