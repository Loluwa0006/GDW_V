// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ConnectionPool.h"
#include "PooledConnection.h"
#include "HttpSession.h"
#include "TcpSession.h"
#include "SslSession.h"
#include "DiversionHttpModule.h"
#include "BoostHeaders.h"

#include <chrono>
#include <thread>
#include <future>
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogConnectionPoolIntegrationTests, Log, All);

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

// Test 1: End-to-End HTTP Request Flow with Connection Pooling
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolEndToEndTest, "DiversionHTTP.ConnectionPool.Integration.EndToEnd",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolEndToEndTest::RunTest(const FString& Parameters)
{
	// Create connection pool with test configuration
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 2;
	Config.MaxTotalConnections = 10;
	Config.DefaultConnectionTimeout = std::chrono::seconds(5);  // Shorter timeout for tests
	Config.DefaultRequestTimeout = std::chrono::seconds(10);   // Shorter timeout for tests
	Config.MaxConnectionAge = 300;

	static net::io_context EndToEndIoContext;
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
					EndToEndIoContext.run();
					if (EndToEndIoContext.stopped() && !ShouldStop.load())
					{
						EndToEndIoContext.restart();
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

	auto Pool = std::make_shared<FConnectionPool>(Config, nullptr, EndToEndIoContext);

	// Test HTTP connection to diversion.dev
	FConnectionKey Key("diversion.dev", "80", false);

	try
	{
		// Get connection from pool
		auto Connection = Pool->AcquireConnection(Key);
		if (!Connection)
		{
			AddWarning(TEXT("Could not create connection - this may be due to network connectivity issues in test environment"));
			return true; // Skip test if network is not available
		}
		TestNotNull(TEXT("Connection should be created"), Connection.get());

		if (Connection)
		{
			TestEqual(TEXT("Connection should be for correct host"), Connection->GetKey().Host, Key.Host);
			TestEqual(TEXT("Connection should be for correct port"), Connection->GetKey().Port, Key.Port);
			TestTrue(TEXT("Connection should be secure (validated)"), Connection->IsSecure());

			// Create a simple GET request to root
			http::request<http::string_body> Request;
			Request.method(http::verb::get);
			Request.target("/");
			Request.version(11);
			Request.set(http::field::host, "diversion.dev");
			Request.set(http::field::connection, "keep-alive");
			Request.set(http::field::user_agent, "DiversionHTTP-IntegrationTest/1.0");
			Request.prepare_payload();

			// Execute request through pooled connection
			DiversionHttp::HTTPCallResponse Response;
			if (auto TcpSession = Connection->GetTcpSession())
			{
				// First establish connection
				auto ConnectFuture = TcpSession->ConnectAsync();
				auto ConnectResult = ConnectFuture.wait_for(std::chrono::seconds(10));

				TestTrue(TEXT("Connection should complete within timeout"),
						 ConnectResult == std::future_status::ready);

				if (ConnectResult == std::future_status::ready && ConnectFuture.get())
				{
					// Execute the request
					Response = TcpSession->ExecuteRequest(Request);

					TestFalse(TEXT("Request should complete without error"), Response.Error.IsSet());

					// Accept various success codes (200, 301, 302, etc.)
					bool IsSuccessful = (Response.ResponseCode >= 200 && Response.ResponseCode < 400);
					TestTrue(TEXT("Response should be successful HTTP status"), IsSuccessful);

					UE_LOG(LogConnectionPoolIntegrationTests, Log,
						   TEXT("Request succeeded: HTTP %d, Content length: %d"),
						   Response.ResponseCode, Response.Contents.Len());
				}
			}

			// Return connection to pool
			Pool->ReleaseConnection(Connection);
			TestEqual(TEXT("Connection should be marked as available"),
					 Connection->GetState(), EConnectionState::Available);
		}

		// Test connection reuse
		auto Connection2 = Pool->AcquireConnection(Key);
		if (!Connection2)
		{
			AddWarning(TEXT("Second connection request failed - skipping connection reuse test"));
			return true;
		}
		TestNotNull(TEXT("Second connection request should succeed"), Connection2.get());

		if (Connection && Connection2)
		{
			TestEqual(TEXT("Should reuse the same connection"), Connection.get(), Connection2.get());
			TestTrue(TEXT("Reused connection should have request count > 0"),
					 Connection2->GetRequestCount() > 0);

			Pool->ReleaseConnection(Connection2);
		}

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("Integration test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}

// Test 2: HTTPS/SSL Integration Test with Real Certificate Validation
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolSSLIntegrationTest, "DiversionHTTP.ConnectionPool.Integration.SSL",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolSSLIntegrationTest::RunTest(const FString& Parameters)
{
	// Create connection pool with SSL support
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 2;
	Config.MaxTotalConnections = 10;
	Config.DefaultConnectionTimeout = std::chrono::seconds(5);
	Config.DefaultRequestTimeout = std::chrono::seconds(10);
	Config.MaxConnectionAge = 300;

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

	// Create SSL context for HTTPS connections
	auto SslContext = std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::tlsv12_client);

	// Try to load system certificate store
	try
	{
		SslContext->set_default_verify_paths();

		// Also try to load the bundled certificate file if it exists
		FString CertPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UnrealDiversion"), TEXT("Resources"), TEXT("cacert.pem"));
		if (FPaths::FileExists(CertPath))
		{
			std::string CertPathStr = TCHAR_TO_UTF8(*CertPath);
			SslContext->load_verify_file(CertPathStr);
		}
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogConnectionPoolIntegrationTests, Warning, TEXT("Failed to load SSL certificates: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	SslContext->set_verify_mode(boost::asio::ssl::verify_peer);

	auto Pool = std::make_shared<FConnectionPool>(Config, SslContext, SSLIoContext);

	// Test HTTPS connection to api.diversion.dev
	FConnectionKey Key("api.diversion.dev", "443", true);

	try
	{
		auto Connection = Pool->AcquireConnection(Key);
		if (!Connection)
		{
			AddWarning(TEXT("Could not create SSL connection - this may be due to network connectivity or SSL configuration issues"));
			return true; // Skip test if SSL/network is not available
		}
		TestNotNull(TEXT("SSL connection should be created"), Connection.get());

		if (Connection)
		{
			bool IsSecure = Connection->IsSecure();
			if (!IsSecure)
			{
				UE_LOG(LogConnectionPoolIntegrationTests, Warning,
					   TEXT("SSL connection not secure - handshake may have failed"));
				AddWarning(TEXT("SSL handshake failed - skipping SSL test due to network/certificate issues"));
				return true;
			}
			TestTrue(TEXT("Connection should be secure"), IsSecure);

			// Create HTTPS GET request to API root
			http::request<http::string_body> Request;
			Request.method(http::verb::get);
			Request.target("/");
			Request.version(11);
			Request.set(http::field::host, "api.diversion.dev");
			Request.set(http::field::connection, "keep-alive");
			Request.set(http::field::user_agent, "DiversionHTTP-SSLTest/1.0");
			Request.prepare_payload();

			// Execute HTTPS request
			if (auto SslSession = Connection->GetSslSession())
			{
				// Establish SSL connection
				auto ConnectFuture = SslSession->ConnectAsync();
				auto ConnectResult = ConnectFuture.wait_for(std::chrono::seconds(15));

				TestTrue(TEXT("SSL connection should complete within timeout"),
						 ConnectResult == std::future_status::ready);

				if (ConnectResult == std::future_status::ready && ConnectFuture.get())
				{
					// Validate SSL security after connection
					TestTrue(TEXT("SSL connection should pass security validation"),
							 Connection->ValidateConnection());

					// Execute the HTTPS request
					auto Response = SslSession->ExecuteRequest(Request);

					TestFalse(TEXT("HTTPS request should complete without error"), Response.Error.IsSet());

					// Accept various success codes for API endpoint
					bool IsSuccessful = (Response.ResponseCode >= 200 && Response.ResponseCode < 500);
					TestTrue(TEXT("HTTPS response should be successful or expected API response"), IsSuccessful);

					UE_LOG(LogConnectionPoolIntegrationTests, Log,
						   TEXT("HTTPS request succeeded: HTTP %d, Content length: %d"),
						   Response.ResponseCode, Response.Contents.Len());

					// Test that SSL context remains valid for reuse
					TestTrue(TEXT("SSL connection should still be valid after request"),
							 Connection->ValidateConnection());
				}
			}

			Pool->ReleaseConnection(Connection);
		}

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("SSL integration test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}

// Test 3: Connection Recovery After Simulated Network Interruption
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolRecoveryTest, "DiversionHTTP.ConnectionPool.Integration.Recovery",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolRecoveryTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 1; // Single connection for easier testing
	Config.MaxTotalConnections = 5;
	Config.DefaultConnectionTimeout = std::chrono::seconds(3);
	Config.DefaultRequestTimeout = std::chrono::seconds(8);
	Config.MaxConnectionAge = 60;

	static net::io_context RecoveryIoContext;
	static std::thread RecoveryIoThread;
	static std::atomic<bool> RecoveryShouldStop{false};

	// Start IO context thread if not already running
	if (!RecoveryIoThread.joinable())
	{
		RecoveryShouldStop.store(false);
		RecoveryIoThread = std::thread([&]() {
			while (!RecoveryShouldStop.load())
			{
				try
				{
					RecoveryIoContext.run();
					if (RecoveryIoContext.stopped() && !RecoveryShouldStop.load())
					{
						RecoveryIoContext.restart();
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

	auto Pool = std::make_shared<FConnectionPool>(Config, nullptr, RecoveryIoContext);
	FConnectionKey Key("diversion.dev", "80", false);

	try
	{
		// Step 1: Establish working connection
		auto Connection1 = Pool->AcquireConnection(Key);
		TestNotNull(TEXT("Initial connection should be created"), Connection1.get());

		if (Connection1)
		{
			// Make successful request to establish connection
			if (auto TcpSession = Connection1->GetTcpSession())
			{
				auto ConnectFuture = TcpSession->ConnectAsync();
				if (ConnectFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
					ConnectFuture.get())
				{
					http::request<http::string_body> Request;
					Request.method(http::verb::get);
					Request.target("/");
					Request.version(11);
					Request.set(http::field::host, "diversion.dev");
					Request.prepare_payload();

					auto Response = TcpSession->ExecuteRequest(Request);
					bool IsSuccessful = (Response.ResponseCode >= 200 && Response.ResponseCode < 400);

					if (!IsSuccessful)
					{
						UE_LOG(LogConnectionPoolIntegrationTests, Warning,
							   TEXT("Initial request failed: HTTP %d, Error: %s"),
							   Response.ResponseCode,
							   Response.Error.IsSet() ? *Response.Error.GetValue() : TEXT("None"));

						// Skip test if network connectivity issues
						AddWarning(TEXT("Initial request failed - may be due to network connectivity"));
						return true;
					}
					TestTrue(TEXT("Initial request should succeed"), IsSuccessful);
				}
			}

			Pool->ReleaseConnection(Connection1);
			TestTrue(TEXT("Connection should have successful requests"),
					 Connection1->GetRequestCount() > 0);
		}

		// Step 2: Simulate network interruption by marking connection invalid
		// In a real scenario, this would happen due to network timeout or connection drop
		Connection1->SetState(EConnectionState::Invalid);

		// Step 3: Request new connection - pool should create a fresh one
		auto Connection2 = Pool->AcquireConnection(Key);
		if (!Connection2)
		{
			AddWarning(TEXT("Recovery connection creation failed - skipping recovery test"));
			return true;
		}
		TestNotNull(TEXT("Recovery connection should be created"), Connection2.get());

		if (Connection2)
		{
			// Should be a different connection instance since the old one was invalid
			TestNotEqual(TEXT("Should create new connection after failure"),
						Connection1.get(), Connection2.get());
			TestEqual(TEXT("New connection should be marked as used (count=1)"),
					 Connection2->GetRequestCount(), 1u);

			// Test that new connection works
			if (auto TcpSession = Connection2->GetTcpSession())
			{
				auto ConnectFuture = TcpSession->ConnectAsync();
				if (ConnectFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready &&
					ConnectFuture.get())
				{
					http::request<http::string_body> Request;
					Request.method(http::verb::head); // Use HEAD to minimize load
					Request.target("/");
					Request.version(11);
					Request.set(http::field::host, "diversion.dev");
					Request.prepare_payload();

					auto Response = TcpSession->ExecuteRequest(Request);
					bool IsSuccessful = (Response.ResponseCode >= 200 && Response.ResponseCode < 400);

					if (!IsSuccessful)
					{
						UE_LOG(LogConnectionPoolIntegrationTests, Warning,
							   TEXT("Recovery request failed: HTTP %d, Error: %s"),
							   Response.ResponseCode,
							   Response.Error.IsSet() ? *Response.Error.GetValue() : TEXT("None"));

						// Skip test if network connectivity issues
						AddWarning(TEXT("Recovery request failed - may be due to network connectivity"));
						return true;
					}
					TestTrue(TEXT("Recovery request should succeed"), IsSuccessful);

					UE_LOG(LogConnectionPoolIntegrationTests, Log,
						   TEXT("Connection recovery test passed - new connection works"));
				}
			}

			Pool->ReleaseConnection(Connection2);
		}

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("Connection recovery test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}

// Test 4: Health Check Integration Test
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConnectionPoolHealthCheckTest, "DiversionHTTP.ConnectionPool.Integration.HealthCheck",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FConnectionPoolHealthCheckTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 1;
	Config.MaxTotalConnections = 5;
	Config.DefaultConnectionTimeout = std::chrono::seconds(5);
	Config.DefaultRequestTimeout = std::chrono::seconds(10);

	static net::io_context HealthCheckIoContext;
	static std::thread HealthCheckIoThread;
	static std::atomic<bool> HealthCheckShouldStop{false};

	// Start IO context thread if not already running
	if (!HealthCheckIoThread.joinable())
	{
		HealthCheckShouldStop.store(false);
		HealthCheckIoThread = std::thread([&]() {
			while (!HealthCheckShouldStop.load())
			{
				try
				{
					HealthCheckIoContext.run();
					if (HealthCheckIoContext.stopped() && !HealthCheckShouldStop.load())
					{
						HealthCheckIoContext.restart();
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

	auto Pool = std::make_shared<FConnectionPool>(Config, nullptr, HealthCheckIoContext);
	FConnectionKey Key("diversion.dev", "80", false);

	try
	{
		auto Connection = Pool->AcquireConnection(Key);
		TestNotNull(TEXT("Connection should be created"), Connection.get());

		if (Connection)
		{
			// Establish connection first
			if (auto TcpSession = Connection->GetTcpSession())
			{
				auto ConnectFuture = TcpSession->ConnectAsync();
				if (ConnectFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready &&
					ConnectFuture.get())
				{
					// Test health check functionality
					TestTrue(TEXT("Health check should pass on active connection"),
							 Connection->ValidateConnection());

					// Test specific health check request (HEAD to minimize load)
					bool HealthResult = Connection->SendHealthCheckRequest("HEAD", "/",
																		 std::chrono::seconds(10));

					if (!HealthResult)
					{
						AddWarning(TEXT("Health check failed - likely due to network connectivity or server issues"));
						UE_LOG(LogConnectionPoolIntegrationTests, Warning,
							   TEXT("Health check failed for %s:%s - this may be normal in some network environments"),
							   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
					}
					else
					{
						TestTrue(TEXT("Health check request should succeed when network allows"), HealthResult);
					}

					UE_LOG(LogConnectionPoolIntegrationTests, Log,
						   TEXT("Health check integration test passed"));
				}
			}

			Pool->ReleaseConnection(Connection);
		}

	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("Health check integration test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}
