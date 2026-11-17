// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HttpSession.h"
#include "BoostHeaders.h"

#include <future>

DEFINE_LOG_CATEGORY_STATIC(LogSessionShutdownTests, Log, All);

namespace beast = boost::beast;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;

// Mock session that tracks shutdown calls for testing
class FMockSessionForShutdownTest : public FHttpSession<tcp_stream>
{
private:
	tcp_stream MockStream;

public:
	static inline bool bShutdownCalled = false;
	static inline int ShutdownCallCount = 0;

	FMockSessionForShutdownTest(net::io_context& IoContext, const std::string& Host, const std::string& Port)
		: FHttpSession<tcp_stream>(IoContext, tcp_stream(net::make_strand(IoContext)), Host, Port,
								   std::chrono::seconds(5), std::chrono::seconds(10))
		, MockStream(net::make_strand(IoContext))
	{}

	std::future<bool> ConnectAsync() override
	{
		std::promise<bool> promise;
		promise.set_value(true);
		return promise.get_future();
	}

	DiversionHttp::HTTPCallResponse ExecuteRequest(const http::request<http::string_body>& InRequest, FString InOutputFilePath = "") override
	{
		return DiversionHttp::HTTPCallResponse(TEXT("Mock response"), 200, TMap<FString, FString>());
	}

	void Shutdown() override
	{
		bShutdownCalled = true;
		ShutdownCallCount++;
	}

protected:
	tcp_stream& TcpStream() override { return MockStream; }
	const tcp_stream& TcpStream() const override { return MockStream; }

public:
	static void ResetTracking()
	{
		bShutdownCalled = false;
		ShutdownCallCount = 0;
	}
};

// Test: Session Shutdown Behavior
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionShutdownTest, "DiversionHTTP.ConnectionPool.SessionShutdown",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSessionShutdownTest::RunTest(const FString& Parameters)
{
	net::io_context IoContext;

	try
	{
		// Reset tracking before test
		FMockSessionForShutdownTest::ResetTracking();

		// Test 1: Shutdown should NOT be called during normal operation
		auto MockSession = std::make_shared<FMockSessionForShutdownTest>(IoContext, "localhost", "9999");

		// Test connection establishment
		auto ConnectionFuture = MockSession->ConnectAsync();
		TestTrue(TEXT("ConnectAsync should return successful connection"), ConnectionFuture.get());
		TestFalse(TEXT("Shutdown should not be called during connection"),
				 FMockSessionForShutdownTest::bShutdownCalled);

		// Test request execution
		http::request<http::string_body> TestRequest;
		TestRequest.method(http::verb::get);
		TestRequest.target("/test");
		TestRequest.set(http::field::host, "localhost");

		auto Response = MockSession->ExecuteRequest(TestRequest);
		TestFalse(TEXT("Shutdown should not be called during request execution"),
				 FMockSessionForShutdownTest::bShutdownCalled);
		TestEqual(TEXT("Mock response should have correct status"), Response.ResponseCode, 200);

		// Reset state (simulating connection reuse)
		MockSession->ResetState();
		TestFalse(TEXT("Shutdown should not be called during ResetState"),
				 FMockSessionForShutdownTest::bShutdownCalled);

		// Set timeouts (simulating connection configuration)
		MockSession->SetConnectionTimeout(std::chrono::seconds(5));
		MockSession->SetRequestTimeout(std::chrono::seconds(15));
		TestFalse(TEXT("Shutdown should not be called during timeout setting"),
				 FMockSessionForShutdownTest::bShutdownCalled);

		// Test 2: Shutdown SHOULD be called when explicitly invoked (connection pool cleanup)
		MockSession->Shutdown();
		TestTrue(TEXT("Shutdown should be called when explicitly invoked"),
				FMockSessionForShutdownTest::bShutdownCalled);
		TestEqual(TEXT("Shutdown should be called exactly once"),
				 FMockSessionForShutdownTest::ShutdownCallCount, 1);

		// Test 3: Multiple explicit shutdowns should each be tracked
		FMockSessionForShutdownTest::ResetTracking();

		auto Session1 = std::make_shared<FMockSessionForShutdownTest>(IoContext, "host1", "8080");
		auto Session2 = std::make_shared<FMockSessionForShutdownTest>(IoContext, "host2", "8080");

		TestFalse(TEXT("Shutdown should not be called during creation"),
				 FMockSessionForShutdownTest::bShutdownCalled);

		// Explicitly shutdown sessions (simulating connection pool cleanup)
		Session1->Shutdown();
		Session2->Shutdown();

		TestTrue(TEXT("Shutdown should be called for multiple sessions"),
				FMockSessionForShutdownTest::bShutdownCalled);
		TestEqual(TEXT("Shutdown should be called once per session"),
				 FMockSessionForShutdownTest::ShutdownCallCount, 2);

		UE_LOG(LogSessionShutdownTests, Log, TEXT("Shutdown test completed successfully. Total shutdown calls: %d"),
			   FMockSessionForShutdownTest::ShutdownCallCount);

	}
	catch (const std::exception& Ex)
	{
		TestFalse(TEXT("Session shutdown test should not throw"), true);
		UE_LOG(LogSessionShutdownTests, Error, TEXT("Shutdown test failed: %s"), UTF8_TO_TCHAR(Ex.what()));
	}

	return true;
}
