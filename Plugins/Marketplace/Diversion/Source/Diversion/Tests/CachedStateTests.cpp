// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "CachedState.h"

DEFINE_LOG_CATEGORY_STATIC(LogCachedStateTests, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedStateTestGetValid, "Diversion.Tests.CachedState.GetValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)


bool FCachedStateTestGetValid::RunTest(const FString& Parameters)
{
	// Test Case 1: GetValid() should return nullptr initially
	TCached<bool> CachedBool(false, FTimespan::FromSeconds(1));
	const bool* Value = CachedBool.GetValid();
	return TestTrue(TEXT("GetValid() should return nullptr initially"), Value == nullptr);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedStateTestSetGetValid, "Diversion.Tests.CachedState.SetGetValid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCachedStateTestSetGetValid::RunTest(const FString& Parameters)
{
	bool bSuccess = true;
	// Test Case 2: GetValid() should return the value after a set
	TCached<bool> CachedBool(false, FTimespan::FromSeconds(2));
	CachedBool.Set(true);
	const bool* Value = CachedBool.GetValid();
	bSuccess &= TestTrue(TEXT("GetValid() should return the value after Set()"), Value != nullptr);
	bSuccess &= TestEqual(TEXT("GetValid() should return the correct value after Set()"), *Value, true);
	return bSuccess;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedStateTestSetGetValidTimeout, "Diversion.Tests.CachedState.SetGetValidTimeout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCachedStateTestSetGetValidTimeout::RunTest(const FString& Parameters)
{
	bool bSuccess = true;
	TCached<bool> CachedBool(false, FTimespan::FromSeconds(2));
	CachedBool.Set(true);

	// Wait for the cache to expire and verify that GetValid() returns a nullptr
	FPlatformProcess::Sleep(3);
	const bool* Value = CachedBool.GetValid();
	bSuccess &= TestTrue(TEXT("GetValid() should return the value after Set()"), Value == nullptr);
	return bSuccess;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCachedStateTestGetUpdate, "Diversion.Tests.CachedState.GetUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCachedStateTestGetUpdate::RunTest(const FString& Parameters)
{
	TCached<int> CachedInt(0, FTimespan::FromSeconds(5));
	CachedInt.Set(1);
	bool bDelegateCalled = false;
	FOnCacheUpdate OnCacheUpdateDelegate;
	OnCacheUpdateDelegate.BindLambda([&]()
	{
		CachedInt.Set(10);
		bDelegateCalled = true;
	});

	const int Value = CachedInt.GetUpdate(0, OnCacheUpdateDelegate, false);

	TestEqual(TEXT("GetUpdate() should return the correct value after Set()"), Value, 1);
	TestFalse(TEXT("GetUpdate() should not call the delegate if the cache is valid"), bDelegateCalled);

	// Wait for the cache to expire and verify that GetUpdate() calls the delegate
	bDelegateCalled = false;
	FPlatformProcess::Sleep(6);
	const int Value2 = CachedInt.GetUpdate(0, OnCacheUpdateDelegate, false);
	TestEqual(TEXT("GetUpdate() should return the correct value after Set()"), Value2, 10);
	TestTrue(TEXT("GetUpdate() should call the delegate if the cache is invalid"), bDelegateCalled);

	bDelegateCalled = false;
	const int Value3 = CachedInt.GetUpdate(0, OnCacheUpdateDelegate, false);
	TestEqual(TEXT("GetUpdate() should return the correct value after Set()"), Value3, 10);
	TestFalse(TEXT("GetUpdate() should call the delegate if forced"), bDelegateCalled);

	return true;
}
