// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#include "IDiversionWorker.h"

#include "DiversionModule.h"
#include "DiversionProvider.h"
#include "PackageTools.h"
#include "ISourceControlModule.h"

#define LOCTEXT_NAMESPACE "Diversion"

bool IDiversionWorker::UpdateStates() const
{
	ReleaseLockedPackages();
	UpdateReadOnlyPathNotifications();
	return UpdateSyncStatus();
}

bool IDiversionWorker::UpdateSyncStatus() const
{
	if (SyncStatus.IsSet())
	{
		FDiversionProvider& Provider = FDiversionModule::Get().GetProvider();
		Provider.SetSyncStatus(SyncStatus.GetValue());
	}
	return true;
}

void IDiversionWorker::ReleaseLockedPackages() const
{
	bool RequiresReload = false;
	auto& Provider = FDiversionModule::Get().GetProvider();
	for (auto& LockdedPackagePath : OpenedPackagePaths)
	{
		RequiresReload |= Provider.TryUnloadingOpenedPackage(LockdedPackagePath);
	}
	if (RequiresReload)
	{
		FDiversionModule::Get().GetProvider().SetReloadStatus();
	}
}

void IDiversionWorker::UpdateReadOnlyPathNotifications() const
{
	if (ReadOnlyPaths.IsSet())
	{
		FDiversionModule::Get().GetProvider().UpdateReadOnlyPathNotifications(ReadOnlyPaths.GetValue());
	}
}

#undef LOCTEXT_NAMESPACE
