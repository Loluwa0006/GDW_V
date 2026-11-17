// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "DiversionProvider.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiversionProviderTests, Log, All);

// Regression test for the fatal-on-launch crash caused by '.dv-conflict.uasset'
// sidecar files the Diversion agent writes into /Content/. Pre-fix,
// TryUnloadingOpenedPackage called FPackageName::FilenameToLongPackageName
// (the non-Try variant) which aborts the editor on paths whose derived
// package name contains illegal chars (e.g. the inner '.' in
// '/Game/NewBlueprint.dv-conflict'). If this test ever crashes the runner
// instead of asserting, the fatal call has been reintroduced.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiversionProviderTryUnloadingConflictSidecar,
	"Diversion.Tests.DiversionProvider.TryUnloadingOpenedPackage.ConflictSidecarReturnsFalse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDiversionProviderTryUnloadingConflictSidecar::RunTest(const FString& Parameters)
{
	// Deliberately constructed without Init() to exercise the pre-init path - the fix must
	// return before any provider state is touched. Do not add setup here; it would mask
	// the regression by short-circuiting before the fatal call site is reached.
	FDiversionProvider Provider;

	const FString ConflictSidecarPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("NewBlueprint.dv-conflict.uasset"));

	// If the non-Try variant sneaks back in, this call fires UE_LOG(..., Fatal, ...)
	// and aborts the process - reaching TestFalse at all is the regression signal.
	const bool bUnloaded = Provider.TryUnloadingOpenedPackage(ConflictSidecarPath);

	return TestFalse(
		TEXT("TryUnloadingOpenedPackage should return false for paths that do not map to a valid UE package name"),
		bUnloaded);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiversionProviderReadOnlyPathNotifications,
	"Diversion.Tests.DiversionProvider.ReadOnlyPathNotifications",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A read-only path blocking the sync gets a notification while the agent reports it,
// and loses it once the agent stops reporting it.
bool FDiversionProviderReadOnlyPathNotifications::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;
	const FString ReadOnlyPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ReadOnly.uasset"));
	const FString OtherPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Other.uasset"));
	const TMap<FString, FString> Reported = { { ReadOnlyPath, TEXT("Cannot write to ReadOnly.uasset - file is tagged as read-only") } };

	Provider.UpdateReadOnlyPathNotifications(Reported);
	TestTrue(TEXT("Reported path is notified"), Provider.HasReadOnlyPathNotification(ReadOnlyPath));
	TestFalse(TEXT("Unreported path is not notified"), Provider.HasReadOnlyPathNotification(OtherPath));

	Provider.UpdateReadOnlyPathNotifications(Reported);
	TestTrue(TEXT("Path reported again keeps its notification"), Provider.HasReadOnlyPathNotification(ReadOnlyPath));

	Provider.UpdateReadOnlyPathNotifications(TMap<FString, FString>());
	TestFalse(TEXT("Path no longer reported is dismissed"), Provider.HasReadOnlyPathNotification(ReadOnlyPath));

	return true;
}
