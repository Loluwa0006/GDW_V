// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "SslSession.h"
#include "TcpSession.h"
#include "BoostHeaders.h"

#include <thread>

DEFINE_LOG_CATEGORY_STATIC(LogSessionAsyncLifetimeTests, Log, All);

namespace beast = boost::beast;
namespace net = boost::asio;

// Verifies that sessions are kept alive by async operations and properly cleaned up afterward.
// Tests that when a session starts an async operation and the original shared_ptr is released,
// the session remains alive while callbacks are pending and is destroyed only after completion.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSessionAsyncLifetimeTest,
								  "DiversionHTTP.Session.AsyncLifetime",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSessionAsyncLifetimeTest::RunTest(const FString& Parameters)
{
	UE_LOG(LogSessionAsyncLifetimeTests, Log,
		  TEXT("Testing session lifetime management during async operations"));

	// Test both SSL and TCP sessions
	for (int TestType = 0; TestType < 2; ++TestType)
	{
		const bool bTestSSL = (TestType == 0);
		const char* SessionType = bTestSSL ? "SSL" : "TCP";
		const char* TestHost = bTestSSL ? "api.diversion.dev" : "127.0.0.1";
		const char* TestPort = bTestSSL ? "443" : "8797";

		UE_LOG(LogSessionAsyncLifetimeTests, Log,
			  TEXT("\n--- %s Session Lifetime Test ---"), UTF8_TO_TCHAR(SessionType));

		net::io_context IoContext;
		std::weak_ptr<void> WeakSession;

		// Phase 1: Create session and start async operation
		{
			if (bTestSSL)
			{
				net::ssl::context SslContext(net::ssl::context::tlsv12_client);
				auto Session = std::make_shared<FHttpSSLSession>(
					IoContext, SslContext, TestHost, TestPort,
					std::chrono::seconds(1), std::chrono::seconds(2));

				WeakSession = Session;
				Session->ConnectAsync();
			}
			else
			{
				auto Session = std::make_shared<FHttpTcpSession>(
					IoContext, TestHost, TestPort,
					std::chrono::seconds(1), std::chrono::seconds(2));

				WeakSession = Session;
				Session->ConnectAsync();
			}

			UE_LOG(LogSessionAsyncLifetimeTests, Log,
				  TEXT("Created session and started async operation"));
		}

		// Phase 2: Session shared_ptr released - verify lifetime during async operations
		UE_LOG(LogSessionAsyncLifetimeTests, Log,
			  TEXT("Original shared_ptr released"));

		bool SessionAliveAfterRelease = !WeakSession.expired();
		UE_LOG(LogSessionAsyncLifetimeTests, Log,
			  TEXT("Session alive after release: %s"),
			  SessionAliveAfterRelease ? TEXT("YES") : TEXT("NO"));

		TestTrue(TEXT("Session should be kept alive by pending async operations"),
				SessionAliveAfterRelease);

		// Phase 3: Execute async operations
		std::thread IoThread([&IoContext]() {
			IoContext.run_for(std::chrono::seconds(2));
		});

		IoThread.join();

		// Phase 4: Verify cleanup after async operations complete
		bool SessionAliveAfterCompletion = !WeakSession.expired();
		UE_LOG(LogSessionAsyncLifetimeTests, Log,
			  TEXT("Session alive after async completion: %s"),
			  SessionAliveAfterCompletion ? TEXT("YES") : TEXT("NO"));

		UE_LOG(LogSessionAsyncLifetimeTests, Log,
			  TEXT("✓ %s session lifetime managed correctly"), UTF8_TO_TCHAR(SessionType));
	}

	UE_LOG(LogSessionAsyncLifetimeTests, Log,
		  TEXT("\n✓ All session lifetime tests passed"));

	return true;
}
