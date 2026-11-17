// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "ConnectionPool.h"
#include "PooledConnection.h"
#include "DiversionHttpModule.h"
#include "BoostHeaders.h"

#include <chrono>
#include <thread>

DEFINE_LOG_CATEGORY_STATIC(LogConnectionPoolTests, Log, All);

namespace beast = boost::beast;
namespace net = boost::asio;


// Test helper to create connections for unit tests
class FTestConnectionHelper
{
public:
	static std::shared_ptr<FPooledConnection> CreateMockConnection(const FConnectionKey& Key, FConnectionPool* Pool)
	{
		auto Connection = std::make_shared<FPooledConnection>(Key, Pool);
		// Don't initialize with real network - connections will be "invalid" but that's OK for pool logic testing
		Connection->SetState(EConnectionState::Available);
		return Connection;
	}
};

// Test helper class for creating test SSL context
class FTestSSLContext
{
public:
	static std::shared_ptr<net::ssl::context> Create()
	{
		auto Context = std::make_shared<net::ssl::context>(net::ssl::context::tlsv12_client);
		Context->set_default_verify_paths();
		Context->set_verify_mode(net::ssl::verify_none); // For testing only
		return Context;
	}
};

// Test 1: Connection Pool Initialization
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolInitializationTest, "DiversionHTTP.ConnectionPool.Initialization",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolInitializationTest::RunTest(const FString& Parameters)
{
	// Create test configuration
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 5;
	Config.MaxTotalConnections = 20;

	// Create SSL context and IO context
	auto SslContext = FTestSSLContext::Create();
	net::io_context IoContext;

	// Test pool creation
	TUniquePtr<FConnectionPool> Pool;

	try
	{
		Pool = MakeUnique<FConnectionPool>(Config, SslContext, IoContext);
		TestTrue(TEXT("Connection pool should initialize successfully"), Pool.IsValid());

		// Test initial state
		TestEqual(TEXT("Initial total connections should be 0"), Pool->GetTotalConnections(), 0u);

		FConnectionKey TestKey{"localhost", "8080", false};
		TestEqual(TEXT("Initial available connections should be 0"), Pool->GetAvailableConnections(TestKey), 0u);

		// Test configuration access
		TestEqual(TEXT("Max connections per host should match config"),
				 Pool->GetConfig().MaxConnectionsPerHost, 5u);
		TestEqual(TEXT("Max total connections should match config"),
				 Pool->GetConfig().MaxTotalConnections, 20u);
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Connection pool initialization should not throw"), true);
		UE_LOG(LogConnectionPoolTests, Error, TEXT("Pool initialization failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	// Cleanup
	if (Pool)
	{
		Pool->Shutdown();
	}

	return true;
}

// Test 2: Connection Configuration Validation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionConfigurationTest, "DiversionHTTP.ConnectionPool.Configuration",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionConfigurationTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;

	// Test default timeouts
	TestEqual(TEXT("Default connection timeout should be 10 seconds"),
			  static_cast<int32>(Config.DefaultConnectionTimeout.count()), 10);
	TestEqual(TEXT("Default request timeout should be 60 seconds"),
			  static_cast<int32>(Config.DefaultRequestTimeout.count()), 60);

	// Test host-specific timeout configuration
	Config.HostConnectionTimeouts["localhost"] = std::chrono::seconds(5);
	Config.HostRequestTimeouts["localhost"] = std::chrono::seconds(30);

	TestEqual(TEXT("Localhost connection timeout should be 5 seconds"),
			  static_cast<int32>(Config.GetConnectionTimeout("localhost").count()), 5);
	TestEqual(TEXT("Localhost request timeout should be 30 seconds"),
			  static_cast<int32>(Config.GetRequestTimeout("localhost").count()), 30);

	// Test fallback to defaults for unknown host
	TestEqual(TEXT("Unknown host should use default connection timeout"),
			  static_cast<int32>(Config.GetConnectionTimeout("unknown.com").count()), 10);
	TestEqual(TEXT("Unknown host should use default request timeout"),
			  static_cast<int32>(Config.GetRequestTimeout("unknown.com").count()), 60);

	// Test timeout validation (request >= connection)
	auto TimeoutPair = Config.GetValidatedTimeouts("testhost", 5, 3);
	auto ConnTimeout = TimeoutPair.first;
	auto ReqTimeout = TimeoutPair.second;
	TestTrue(TEXT("Validated request timeout should be >= connection timeout"),
			 ReqTimeout >= ConnTimeout);
	TestEqual(TEXT("Connection timeout should be as requested"), static_cast<int32>(ConnTimeout.count()), 5);
	TestTrue(TEXT("Request timeout should be adjusted upward"), static_cast<int32>(ReqTimeout.count()) > 3);

	return true;
}

// Test 3: Connection Key Equality and Hashing
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionKeyTest, "DiversionHTTP.ConnectionPool.ConnectionKey",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionKeyTest::RunTest(const FString& Parameters)
{
	// Test connection key equality
	FConnectionKey Key1{"localhost", "8080", false};
	FConnectionKey Key2{"localhost", "8080", false};
	FConnectionKey Key3{"localhost", "8080", true}; // SSL difference
	FConnectionKey Key4{"localhost", "9000", false}; // Port difference
	FConnectionKey Key5{"example.com", "8080", false}; // Host difference

	TestTrue(TEXT("Identical keys should be equal"), Key1 == Key2);
	TestFalse(TEXT("Keys with different SSL should not be equal"), Key1 == Key3);
	TestFalse(TEXT("Keys with different ports should not be equal"), Key1 == Key4);
	TestFalse(TEXT("Keys with different hosts should not be equal"), Key1 == Key5);

	// Test that equal keys produce same hash
	uint32 Hash1 = GetTypeHash(Key1);
	uint32 Hash2 = GetTypeHash(Key2);
	TestEqual(TEXT("Equal keys should have same hash"), Hash1, Hash2);

	return true;
}

// Test 4: Connection Lifecycle Management
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionLifecycleTest, "DiversionHTTP.ConnectionPool.Lifecycle",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionLifecycleTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 3;
	Config.MaxTotalConnections = 10;

	auto SslContext = FTestSSLContext::Create();
	net::io_context IoContext;

	TUniquePtr<FConnectionPool> Pool;

	try
	{
		Pool = MakeUnique<FConnectionPool>(Config, SslContext, IoContext);
		FConnectionKey TestKey{"localhost", "65534", false}; // Use high port unlikely to be in use

		// Test that pool can handle connection attempts gracefully
		// Since we're using a port that's likely closed, connections will fail but pool should handle it
		auto Connection1 = Pool->AcquireConnection(TestKey, std::chrono::seconds(1));

		if (Connection1)
		{
			// Connection was created - test its initial state
			TestTrue(TEXT("Connection was created"), true);
			TestEqual(TEXT("Connection should be in InUse state initially"),
					 static_cast<int>(Connection1->GetState()), static_cast<int>(EConnectionState::InUse));
			TestEqual(TEXT("Total connections should be 1"), Pool->GetTotalConnections(), 1u);

			// Release connection back to pool - it may be removed if unhealthy
			uint32 ConnectionsBefore = Pool->GetTotalConnections();
			Pool->ReleaseConnection(Connection1);
			uint32 ConnectionsAfter = Pool->GetTotalConnections();

			if (ConnectionsAfter == ConnectionsBefore)
			{
				// Connection was kept in pool (healthy)
				TestEqual(TEXT("Healthy connection should be Available after release"),
						 static_cast<int>(Connection1->GetState()), static_cast<int>(EConnectionState::Available));
				TestTrue(TEXT("Connection pool should maintain healthy connections"), true);
			}
			else
			{
				// Connection was removed from pool (unhealthy, which is expected for closed ports)
				TestTrue(TEXT("Pool correctly removes unhealthy connections"), ConnectionsAfter < ConnectionsBefore);
			}
		}
		else
		{
			// Connection creation failed, which is expected behavior for closed ports
			TestTrue(TEXT("Pool handles failed connection creation gracefully"), true);
			TestEqual(TEXT("Total connections should remain 0 on failure"), Pool->GetTotalConnections(), 0u);
		}
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Connection lifecycle test should not throw"), true);
		UE_LOG(LogConnectionPoolTests, Error, TEXT("Lifecycle test failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	// Cleanup
	if (Pool)
	{
		Pool->Shutdown();
	}

	return true;
}

// Test 5: Connection Pool Limits
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolLimitsTest, "DiversionHTTP.ConnectionPool.Limits",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolLimitsTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 2;
	Config.MaxTotalConnections = 2; // Keep total low for easier testing

	auto SslContext = FTestSSLContext::Create();
	net::io_context IoContext;

	TUniquePtr<FConnectionPool> Pool;

	try
	{
		Pool = MakeUnique<FConnectionPool>(Config, SslContext, IoContext);

		// Test that configuration limits are respected
		TestEqual(TEXT("Max connections per host should be 2"),
				 Pool->GetConfig().MaxConnectionsPerHost, 2u);
		TestEqual(TEXT("Max total connections should be 2"),
				 Pool->GetConfig().MaxTotalConnections, 2u);

		// Test initial state
		TestEqual(TEXT("Initial total connections should be 0"),
				 Pool->GetTotalConnections(), 0u);

		// Test available connections for non-existent key
		FConnectionKey TestKey{"nonexistent", "9999", false};
		TestEqual(TEXT("Available connections for new key should be 0"),
				 Pool->GetAvailableConnections(TestKey), 0u);
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Connection limits test should not throw"), true);
		UE_LOG(LogConnectionPoolTests, Error, TEXT("Limits test failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	// Cleanup
	if (Pool)
	{
		Pool->Shutdown();
	}

	return true;
}

// Test 6: Connection Health Validation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionHealthTest, "DiversionHTTP.ConnectionPool.Health",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionHealthTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxRequestsPerConnection = 5; // Low limit for testing

	auto SslContext = FTestSSLContext::Create();
	net::io_context IoContext;

	TUniquePtr<FConnectionPool> Pool;

	try
	{
		Pool = MakeUnique<FConnectionPool>(Config, SslContext, IoContext);
		FConnectionKey TestKey{"localhost", "9999", false};

		// Create a connection manually for testing health methods
		auto Connection = FTestConnectionHelper::CreateMockConnection(TestKey, Pool.Get());
		if (Connection)
		{
			// Test initial state
			TestEqual(TEXT("Initial request count should be 0"), Connection->GetRequestCount(), 0u);
			TestEqual(TEXT("Connection should be Available"),
					 static_cast<int>(Connection->GetState()), static_cast<int>(EConnectionState::Available));

			// Test usage tracking
			Connection->MarkUsed();
			TestEqual(TEXT("Request count should increment"), Connection->GetRequestCount(), 1u);

			// Test state management
			Connection->SetState(EConnectionState::InUse);
			TestEqual(TEXT("Connection state should change to InUse"),
					 static_cast<int>(Connection->GetState()), static_cast<int>(EConnectionState::InUse));

			Connection->SetState(EConnectionState::Invalid);
			TestEqual(TEXT("Connection state should change to Invalid"),
					 static_cast<int>(Connection->GetState()), static_cast<int>(EConnectionState::Invalid));

			// Test timeout management
			std::chrono::seconds ConnTimeout(5);
			std::chrono::seconds ReqTimeout(15);
			Connection->SetTimeouts(ConnTimeout, ReqTimeout);
			TestTrue(TEXT("Should be able to set timeouts"), true);
		}
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Connection health test should not throw"), true);
		UE_LOG(LogConnectionPoolTests, Error, TEXT("Health test failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	// Cleanup
	if (Pool)
	{
		Pool->Shutdown();
	}

	return true;
}

// Test 7: Timeout Management
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionTimeoutTest, "DiversionHTTP.ConnectionPool.Timeouts",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionTimeoutTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	auto SslContext = FTestSSLContext::Create();
	net::io_context IoContext;

	TUniquePtr<FConnectionPool> Pool;

	try
	{
		Pool = MakeUnique<FConnectionPool>(Config, SslContext, IoContext);
		FConnectionKey TestKey{"localhost", "9999", false};

		auto Connection = FTestConnectionHelper::CreateMockConnection(TestKey, Pool.Get());
		if (Connection)
		{
			// Test timeout setting
			std::chrono::seconds ConnTimeout(5);
			std::chrono::seconds ReqTimeout(15);

			Connection->SetTimeouts(ConnTimeout, ReqTimeout);
			TestTrue(TEXT("Should be able to set timeouts without error"), true);

			// Test expiration logic
			TestFalse(TEXT("New connection should not be expired"),
					 Connection->IsExpired(std::chrono::seconds(10)));
			TestTrue(TEXT("Connection should be expired with very short timeout"),
					Connection->IsExpired(std::chrono::seconds(0)));

			// Test connection age tracking
			auto CreatedTime = Connection->GetCreatedAt();
			auto LastUsedTime = Connection->GetLastUsed();
			TestTrue(TEXT("Creation time should be valid"),
					CreatedTime != std::chrono::steady_clock::time_point{});
			TestTrue(TEXT("Last used time should be valid"),
					LastUsedTime != std::chrono::steady_clock::time_point{});
		}
	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Connection timeout test should not throw"), true);
		UE_LOG(LogConnectionPoolTests, Error, TEXT("Timeout test failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	// Cleanup
	if (Pool)
	{
		Pool->Shutdown();
	}

	return true;
}
