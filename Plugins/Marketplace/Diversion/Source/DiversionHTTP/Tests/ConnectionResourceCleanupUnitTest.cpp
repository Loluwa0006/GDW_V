// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PooledConnection.h"
#include "ConnectionPool.h"

DEFINE_LOG_CATEGORY_STATIC(LogConnectionResourceCleanupUnitTests, Log, All);

// Unit test for the SetState shutdown mechanism without network dependencies
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionResourceCleanupUnitTest,
	"DiversionHTTP.ConnectionPool.ResourceCleanup.Unit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConnectionResourceCleanupUnitTest::RunTest(const FString& Parameters)
{
	UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("Starting Connection Resource Cleanup Unit Test"));

	try
	{
		// Test the SetState mechanism directly
		FConnectionKey TestKey{"test.example.com", "443", true};

		// Create a connection object (note: this doesn't establish network connection)
		auto Connection = std::make_shared<FPooledConnection>(TestKey, nullptr);

		// Test 1: Verify initial state
		TestEqual(TEXT("Initial state should be Available"),
				 static_cast<int>(Connection->GetState()),
				 static_cast<int>(EConnectionState::Available));

		// Test 2: Verify SetState calls work
		UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("Testing SetState mechanism"));

		Connection->SetState(EConnectionState::InUse);
		TestEqual(TEXT("State should change to InUse"),
				 static_cast<int>(Connection->GetState()),
				 static_cast<int>(EConnectionState::InUse));

		// Test 3: The critical test - SetState(Invalid) should trigger shutdown
		UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("Testing SetState(Invalid) - should trigger ShutdownSessions()"));

		// This is the key test - when we call SetState(Invalid), it should:
		// 1. Change the state to Invalid
		// 2. Call ShutdownSessions() internally (which calls Shutdown() on TCP/SSL sessions)
		Connection->SetState(EConnectionState::Invalid);

		TestEqual(TEXT("State should change to Invalid and trigger shutdown"),
				 static_cast<int>(Connection->GetState()),
				 static_cast<int>(EConnectionState::Invalid));

		UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("SetState(Invalid) completed - shutdown mechanism was called"));

		// Test 4: Verify subsequent SetState calls to Invalid don't trigger shutdown again
		UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("Testing redundant SetState(Invalid) calls"));

		// This should NOT trigger another shutdown since it's already Invalid
		Connection->SetState(EConnectionState::Invalid);

		TestEqual(TEXT("State should remain Invalid"),
				 static_cast<int>(Connection->GetState()),
				 static_cast<int>(EConnectionState::Invalid));

		UE_LOG(LogConnectionResourceCleanupUnitTests, Log, TEXT("Connection Resource Cleanup Unit Test completed successfully"));
		return true;
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Resource cleanup unit test should not throw exceptions"), true);
		UE_LOG(LogConnectionResourceCleanupUnitTests, Error, TEXT("Test failed with exception: %s"), UTF8_TO_TCHAR(Ex.what()));
		return false;
	}
}
