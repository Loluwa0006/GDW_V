// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionVersion.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiversionVersionIsGreaterOrEqualThan, "Diversion.Tests.DiversionVersion.IsGreaterOrEqualThan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDiversionVersionIsGreaterOrEqualThan::RunTest(const FString& Parameters)
{
	const FDiversionVersion Version(TEXT("v1.6.373"));

	TestTrue(TEXT("Equal version"), Version.IsGreaterOrEqualThan(1, 6, 373));
	TestTrue(TEXT("Older build"), Version.IsGreaterOrEqualThan(1, 6, 372));
	TestFalse(TEXT("Newer build"), Version.IsGreaterOrEqualThan(1, 6, 374));
	TestTrue(TEXT("Older minor, newer build"), Version.IsGreaterOrEqualThan(1, 5, 999));
	TestFalse(TEXT("Newer minor, older build"), Version.IsGreaterOrEqualThan(1, 7, 0));
	TestTrue(TEXT("Older major"), Version.IsGreaterOrEqualThan(0, 9, 999));
	TestFalse(TEXT("Newer major"), Version.IsGreaterOrEqualThan(2, 0, 0));

	TestFalse(TEXT("Same minor, older build is not newer"), FDiversionVersion(TEXT("v1.6.0")).IsGreaterOrEqualThan(1, 6, 373));

	return true;
}
