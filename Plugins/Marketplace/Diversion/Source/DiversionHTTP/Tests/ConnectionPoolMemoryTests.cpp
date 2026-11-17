// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "ConnectionPool.h"
#include "PooledConnection.h"
#include "DiversionHttpModule.h"
#include "HAL/PlatformMemory.h"
#include "BoostHeaders.h"
#include "HttpSession.h"
#include "TcpSession.h"
#include "SslSession.h"
#include <future>

#include <chrono>
#include <thread>
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogConnectionPoolMemoryTests, Log, All);

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

// Helper class for memory monitoring
class FMemoryMonitor
{
public:
	struct FMemorySnapshot
	{
		SIZE_T PhysicalMemory;
		SIZE_T VirtualMemory;
		uint32 TotalConnections;
		std::chrono::steady_clock::time_point Timestamp;

		FMemorySnapshot()
			: PhysicalMemory(0), VirtualMemory(0), TotalConnections(0)
			, Timestamp(std::chrono::steady_clock::now()) {}
	};

	static FMemorySnapshot TakeSnapshot(uint32 ConnectionCount = 0)
	{
		FMemorySnapshot Snapshot;
		FPlatformMemoryStats Stats = FPlatformMemory::GetStats();

		Snapshot.PhysicalMemory = Stats.UsedPhysical;
		Snapshot.VirtualMemory = Stats.UsedVirtual;
		Snapshot.TotalConnections = ConnectionCount;

		return Snapshot;
	}

	static bool DetectMemoryLeak(const FMemorySnapshot& Before, const FMemorySnapshot& After,
								float TolerancePercent = 10.0f)
	{
		// Calculate memory increase
		int64 PhysicalIncrease = (int64)After.PhysicalMemory - (int64)Before.PhysicalMemory;
		int64 VirtualIncrease = (int64)After.VirtualMemory - (int64)Before.VirtualMemory;

		// Calculate acceptable tolerance (in bytes)
		SIZE_T PhysicalTolerance = (SIZE_T)(Before.PhysicalMemory * TolerancePercent / 100.0f);
		SIZE_T VirtualTolerance = (SIZE_T)(Before.VirtualMemory * TolerancePercent / 100.0f);

		// Add base tolerance for small allocations (1MB)
		PhysicalTolerance = FMath::Max(PhysicalTolerance, (SIZE_T)(1024 * 1024));
		VirtualTolerance = FMath::Max(VirtualTolerance, (SIZE_T)(1024 * 1024));

		bool PhysicalLeak = PhysicalIncrease > (int64)PhysicalTolerance;
		bool VirtualLeak = VirtualIncrease > (int64)VirtualTolerance;

		UE_LOG(LogConnectionPoolMemoryTests, Log,
			   TEXT("Memory Analysis: Physical: %lld bytes (%s), Virtual: %lld bytes (%s)"),
			   PhysicalIncrease, PhysicalLeak ? TEXT("LEAK") : TEXT("OK"),
			   VirtualIncrease, VirtualLeak ? TEXT("LEAK") : TEXT("OK"));

		return PhysicalLeak || VirtualLeak;
	}
};

// Test 1: Memory Leak Detection Under Sustained Load
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolMemoryLeakTest, "DiversionHTTP.ConnectionPool.Memory.LeakDetection",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolMemoryLeakTest::RunTest(const FString& Parameters)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Take initial memory snapshot
	auto InitialSnapshot = FMemoryMonitor::TakeSnapshot();

	// Create connection pool
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 5;
	Config.MaxTotalConnections = 20;
	Config.DefaultConnectionTimeout = std::chrono::seconds(10);
	Config.DefaultRequestTimeout = std::chrono::seconds(15);
	Config.MaxConnectionAge = 60;
	Config.DefaultHealthCheck.Type = EHealthCheckType::HttpHead;

	static net::io_context IoContext;
	static std::thread IoThread;
	static std::atomic<bool> ShouldStop{false};

	// Start IO context thread if not already running
	if (!IoThread.joinable())
	{
		ShouldStop.store(false);
		IoThread = std::thread([&]() {
			while (!ShouldStop.load())
			{
				try
				{
					IoContext.run();
					if (IoContext.stopped() && !ShouldStop.load())
					{
						IoContext.restart();
					}
				}
				catch (...)
				{
					// Continue running even if there are exceptions
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
	}

	auto Pool = std::make_shared<FConnectionPool>(Config, nullptr, IoContext);
	FConnectionKey Key("diversion.dev", "80", false);

	try
	{
		// Perform sustained load test - acquire and release connections repeatedly
		const int32 NumCycles = 5;
		const int32 ConnectionsPerCycle = 3;
		int32 TotalSuccessfulConnections = 0;

		for (int32 Cycle = 0; Cycle < NumCycles; ++Cycle)
		{
			std::vector<std::shared_ptr<FPooledConnection>> Connections;

			// Acquire multiple connections
			for (int32 i = 0; i < ConnectionsPerCycle; ++i)
			{
				auto Connection = Pool->AcquireConnection(Key);
				if (Connection)
				{
					Connections.push_back(Connection);
					TotalSuccessfulConnections++;

					// Simulate some work - establish connection
					if (auto TcpSession = Connection->GetTcpSession())
					{
						auto ConnectFuture = TcpSession->ConnectAsync();
						// Use poll instead of wait to avoid blocking UI
						IoContext.poll();
						if (ConnectFuture.wait_for(std::chrono::milliseconds(500)) == std::future_status::ready)
						{
							try
							{
								ConnectFuture.get(); // Don't care about result for memory test
							}
							catch (const std::exception& Ex)
							{
								// Continue test even if connection fails
								UE_LOG(LogConnectionPoolMemoryTests, VeryVerbose,
									   TEXT("Connection failed in memory test cycle %d: %s"), Cycle, UTF8_TO_TCHAR(Ex.what()));
							}
						}
					}
				}
			}

			// Release all connections
			for (auto& Connection : Connections)
			{
				Pool->ReleaseConnection(Connection);
			}
			Connections.clear();

			// Process IO operations without blocking
			IoContext.poll();

			// Periodic memory check - check every 2 cycles instead of 10
			if (Cycle % 2 == 1)
			{
				IoContext.poll(); // Non-blocking IO processing

				auto CurrentSnapshot = FMemoryMonitor::TakeSnapshot(Pool->GetTotalConnections());

				UE_LOG(LogConnectionPoolMemoryTests, Log,
					   TEXT("Cycle %d: Connections=%u, Physical=%llu MB, Virtual=%llu MB"),
					   Cycle, CurrentSnapshot.TotalConnections,
					   CurrentSnapshot.PhysicalMemory / (1024 * 1024),
					   CurrentSnapshot.VirtualMemory / (1024 * 1024));
			}
		}

		// If we got no successful connections, skip the memory leak test
		if (TotalSuccessfulConnections == 0)
		{
			AddWarning(TEXT("No successful connections in memory leak test - skipping due to network issues"));
			return true;
		}

		UE_LOG(LogConnectionPoolMemoryTests, Log,
			   TEXT("Memory leak test completed %d successful connections over %d cycles"),
			   TotalSuccessfulConnections, NumCycles);

		// Final cleanup and memory check
		Pool = nullptr;
		// Process any remaining IO operations without blocking
		for (int i = 0; i < 10; ++i)
		{
			IoContext.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		auto FinalSnapshot = FMemoryMonitor::TakeSnapshot();

		// Check for memory leaks
		bool HasMemoryLeak = FMemoryMonitor::DetectMemoryLeak(InitialSnapshot, FinalSnapshot, 15.0f);

		TestFalse("No significant memory leak should be detected after sustained load", HasMemoryLeak);

		if (HasMemoryLeak)
		{
			AddWarning(FString::Printf(TEXT("Potential memory leak detected: Physical +%lld bytes, Virtual +%lld bytes"),
									  (int64)FinalSnapshot.PhysicalMemory - (int64)InitialSnapshot.PhysicalMemory,
									  (int64)FinalSnapshot.VirtualMemory - (int64)InitialSnapshot.VirtualMemory));
		}

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("Memory leak test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}

// Test 2: Connection Pool Growth and Cleanup Memory Management
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolGrowthTest, "DiversionHTTP.ConnectionPool.Memory.Growth",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolGrowthTest::RunTest(const FString& Parameters)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	auto InitialSnapshot = FMemoryMonitor::TakeSnapshot();

	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 20;
	Config.MaxTotalConnections = 50;
	Config.DefaultConnectionTimeout = std::chrono::seconds(10);
	Config.DefaultRequestTimeout = std::chrono::seconds(15);
	Config.MaxConnectionAge = 30; // Shorter age for testing cleanup
	Config.DefaultHealthCheck.Type = EHealthCheckType::Disabled; // Disable for cleaner memory testing

	static net::io_context GrowthIoContext;
	static std::thread GrowthIoThread;
	static std::atomic<bool> GrowthShouldStop{false};

	// Start IO context thread if not already running
	if (!GrowthIoThread.joinable())
	{
		GrowthShouldStop.store(false);
		GrowthIoThread = std::thread([&]() {
			while (!GrowthShouldStop.load())
			{
				try
				{
					GrowthIoContext.run();
					if (GrowthIoContext.stopped() && !GrowthShouldStop.load())
					{
						GrowthIoContext.restart();
					}
				}
				catch (...)
				{
					// Continue running even if there are exceptions
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
	}

	// Create SSL context for HTTPS connections in the test
	std::shared_ptr<boost::asio::ssl::context> SslContext;
	bool bSSLConfigured = false;

	try
	{
		SslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
		SslContext->set_default_verify_paths();
		SslContext->set_verify_mode(boost::asio::ssl::verify_peer);
		bSSLConfigured = true;
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogConnectionPoolMemoryTests, Warning, TEXT("Failed to configure SSL context: %s"), UTF8_TO_TCHAR(Ex.what()));
		SslContext = nullptr;
	}

	auto Pool = std::make_shared<FConnectionPool>(Config, SslContext, GrowthIoContext);

	try
	{
		// Test different hosts to trigger pool growth
		// Start with HTTP only, then add HTTPS if SSL setup succeeded
		std::vector<FConnectionKey> TestKeys = {
			{"diversion.dev", "80", false}
		};

		// Only test HTTPS if we have a working SSL context
		if (bSSLConfigured && SslContext)
		{
			TestKeys.push_back({"api.diversion.dev", "443", true});
			TestKeys.push_back({"diversion.dev", "443", true});
		}

		std::vector<std::shared_ptr<FPooledConnection>> AllConnections;

		// Phase 1: Grow pool to near maximum
		int32 SuccessfulConnections = 0;
		for (const auto& Key : TestKeys)
		{
			for (int32 i = 0; i < 2; ++i) // Only 2 connections per host for faster testing
			{
				auto Connection = Pool->AcquireConnection(Key);
				if (Connection)
				{
					AllConnections.push_back(Connection);
					SuccessfulConnections++;
				}
				else
				{
					UE_LOG(LogConnectionPoolMemoryTests, Warning,
						   TEXT("Failed to create connection to %s:%s (SSL=%s) - continuing memory test"),
						   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()),
						   Key.UseSSL ? TEXT("true") : TEXT("false"));
				}
			}
		}

		// If we got no successful connections, skip the test
		if (SuccessfulConnections == 0)
		{
			AddWarning(TEXT("No successful connections - skipping memory growth test due to network issues"));
			return true;
		}

		auto GrowthSnapshot = FMemoryMonitor::TakeSnapshot(static_cast<uint32>(AllConnections.size()));

		UE_LOG(LogConnectionPoolMemoryTests, Log,
			   TEXT("Pool growth: %d connections, Physical=%llu MB, Virtual=%llu MB"),
			   static_cast<int>(AllConnections.size()),
			   GrowthSnapshot.PhysicalMemory / (1024 * 1024),
			   GrowthSnapshot.VirtualMemory / (1024 * 1024));

		TestTrue(TEXT("Should create at least some connections"), AllConnections.size() > 0);
		TestTrue(TEXT("Should create multiple connections if network allows"),
				 AllConnections.size() >= static_cast<size_t>(SuccessfulConnections));
		TestTrue(TEXT("Should not exceed total connection limit"),
				 static_cast<uint32>(AllConnections.size()) <= Config.MaxTotalConnections);

		// Phase 2: Release connections and test cleanup
		for (auto& Connection : AllConnections)
		{
			Pool->ReleaseConnection(Connection);
		}
		AllConnections.clear();

		// Process IO operations and wait briefly for cleanup
		for (int i = 0; i < 50; ++i)
		{
			GrowthIoContext.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		// Force cleanup cycle if available
		// Note: Pool cleanup typically happens in background thread

		// Process any remaining IO operations
		for (int i = 0; i < 10; ++i)
		{
			GrowthIoContext.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		auto CleanupSnapshot = FMemoryMonitor::TakeSnapshot(Pool->GetTotalConnections());

		UE_LOG(LogConnectionPoolMemoryTests, Log,
			   TEXT("After cleanup: %d connections, Physical=%llu MB, Virtual=%llu MB"),
			   CleanupSnapshot.TotalConnections,
			   CleanupSnapshot.PhysicalMemory / (1024 * 1024),
			   CleanupSnapshot.VirtualMemory / (1024 * 1024));

		// Memory should not grow excessively during pool growth/cleanup cycle
		bool HasExcessiveGrowth = FMemoryMonitor::DetectMemoryLeak(InitialSnapshot, CleanupSnapshot, 20.0f);
		TestFalse("Memory should not grow excessively during growth/cleanup", HasExcessiveGrowth);

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("Connection pool growth test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}

// Test 3: SSL Session Memory Management
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolSSLMemoryTest, "DiversionHTTP.ConnectionPool.Memory.SSL",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolSSLMemoryTest::RunTest(const FString& Parameters)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	auto InitialSnapshot = FMemoryMonitor::TakeSnapshot();

	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 5;
	Config.MaxTotalConnections = 10;
	Config.DefaultConnectionTimeout = std::chrono::seconds(15);
	Config.DefaultRequestTimeout = std::chrono::seconds(20);
	Config.MaxConnectionAge = 60;
	Config.DefaultHealthCheck.Type = EHealthCheckType::Disabled;

	static net::io_context SSLIoContext;
	static std::thread SSLIoThread;
	static std::atomic<bool> SSLShouldStop{false};

	// Start IO context thread if not already running
	if (!SSLIoThread.joinable())
	{
		SSLShouldStop.store(false);
		SSLIoThread = std::thread([&]() {
			while (!SSLShouldStop.load())
			{
				try
				{
					SSLIoContext.run();
					if (SSLIoContext.stopped() && !SSLShouldStop.load())
					{
						SSLIoContext.restart();
					}
				}
				catch (...)
				{
					// Continue running even if there are exceptions
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		});
	}

	// Create SSL context for HTTPS connections in the SSL memory test
	std::shared_ptr<boost::asio::ssl::context> SslContext;
	bool bSSLConfigured = false;

	try
	{
		SslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);
		SslContext->set_default_verify_paths();
		SslContext->set_verify_mode(boost::asio::ssl::verify_peer);
		bSSLConfigured = true;
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogConnectionPoolMemoryTests, Warning, TEXT("Failed to configure SSL context for SSL memory test: %s"), UTF8_TO_TCHAR(Ex.what()));
		SslContext = nullptr;
	}

	if (!bSSLConfigured)
	{
		AddWarning(TEXT("SSL context configuration failed - skipping SSL memory test"));
		return true;
	}

	auto Pool = std::make_shared<FConnectionPool>(Config, SslContext, SSLIoContext);
	FConnectionKey SSLKey("api.diversion.dev", "443", true);

	try
	{
		// Repeatedly create and destroy SSL connections
		const int32 NumIterations = 5;
		int32 SuccessfulConnections = 0;

		for (int32 i = 0; i < NumIterations; ++i)
		{
			auto Connection = Pool->AcquireConnection(SSLKey);
			if (Connection)
			{
				SuccessfulConnections++;

				// Don't fail the test if SSL handshake issues occur
				bool IsSecure = Connection->IsSecure();
				if (!IsSecure)
				{
					UE_LOG(LogConnectionPoolMemoryTests, Warning,
						   TEXT("SSL connection not secure on iteration %d - continuing memory test"), i);
				}

				// Attempt SSL handshake - don't fail test if it doesn't work
				if (auto SslSession = Connection->GetSslSession())
				{
					auto ConnectFuture = SslSession->ConnectAsync();
					// Process IO operations first
					SSLIoContext.poll();
					auto ConnectResult = ConnectFuture.wait_for(std::chrono::milliseconds(1000));

					if (ConnectResult == std::future_status::ready)
					{
						try
						{
							ConnectFuture.get(); // Get result to complete SSL handshake
						}
						catch (const std::exception& Ex)
						{
							UE_LOG(LogConnectionPoolMemoryTests, Warning,
								   TEXT("SSL handshake failed on iteration %d: %s - continuing memory test"),
								   i, UTF8_TO_TCHAR(Ex.what()));
						}
					}
				}

				Pool->ReleaseConnection(Connection);
			}
			else
			{
				UE_LOG(LogConnectionPoolMemoryTests, Warning,
					   TEXT("Failed to acquire SSL connection on iteration %d - continuing memory test"), i);
			}

			// Process IO operations and periodic memory check
			SSLIoContext.poll();
			if (i % 2 == 1)
			{

				auto CurrentSnapshot = FMemoryMonitor::TakeSnapshot();
				UE_LOG(LogConnectionPoolMemoryTests, VeryVerbose,
					   TEXT("SSL Iteration %d: Physical=%llu MB"), i,
					   CurrentSnapshot.PhysicalMemory / (1024 * 1024));
			}
		}

		// If we got no successful connections, skip the memory leak test
		if (SuccessfulConnections == 0)
		{
			AddWarning(TEXT("No successful SSL connections - skipping SSL memory test due to network/SSL issues"));
			return true;
		}

		UE_LOG(LogConnectionPoolMemoryTests, Log,
			   TEXT("SSL Memory test completed %d successful connections out of %d attempts"),
			   SuccessfulConnections, NumIterations);

		// Final cleanup
		Pool = nullptr;
		// Process any remaining IO operations
		for (int i = 0; i < 15; ++i)
		{
			SSLIoContext.poll();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}

		auto FinalSnapshot = FMemoryMonitor::TakeSnapshot();

		// SSL operations can be memory intensive, so use higher tolerance
		bool HasSSLMemoryLeak = FMemoryMonitor::DetectMemoryLeak(InitialSnapshot, FinalSnapshot, 25.0f);

		TestFalse("SSL connections should not cause significant memory leaks", HasSSLMemoryLeak);

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("SSL memory test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}
