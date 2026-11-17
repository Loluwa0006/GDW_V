// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ConnectionPool.h"
#include "PooledConnection.h"
#include "HttpSession.h"
#include "SslSession.h"
#include "DiversionHttpModule.h"
#include "BoostHeaders.h"

#include <chrono>
#include <thread>
#include <future>
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogGZipIntegrationTests, Log, All);

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

/**
 * Test 1: Verify that a known gzip endpoint returns gzip-compressed response and our code decompresses it correctly
 * This uses nghttp2.org/httpbin/gzip which ALWAYS returns gzip-compressed data
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGZipHttpbinTest, "DiversionHTTP.GZip.Integration.GzipDecompression",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGZipHttpbinTest::RunTest(const FString& Parameters)
{
	FConnectionPoolConfig Config;
	Config.MaxConnectionsPerHost = 2;
	Config.MaxTotalConnections = 10;
	Config.DefaultConnectionTimeout = std::chrono::seconds(10);
	Config.DefaultRequestTimeout = std::chrono::seconds(30);
	Config.MaxConnectionAge = 300;

	static net::io_context HttpbinIoContext;
	static std::thread IoThread;
	static std::atomic<bool> ShouldStop{false};

	// Start IO context thread if not already running
	if (!IoThread.joinable())
	{
		ShouldStop.store(false);
		IoThread = std::thread([]() {
			while (!ShouldStop.load())
			{
				try
				{
					HttpbinIoContext.run();
					if (HttpbinIoContext.stopped() && !ShouldStop.load())
					{
						HttpbinIoContext.restart();
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
	try
	{
		SslContext->set_default_verify_paths();
		FString CertPath = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UnrealDiversion"), TEXT("Resources"), TEXT("cacert.pem"));
		if (FPaths::FileExists(CertPath))
		{
			std::string CertPathStr = TCHAR_TO_UTF8(*CertPath);
			SslContext->load_verify_file(CertPathStr);
		}
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogGZipIntegrationTests, Warning, TEXT("Failed to load SSL certificates: %s"), UTF8_TO_TCHAR(Ex.what()));
	}
	SslContext->set_verify_mode(boost::asio::ssl::verify_peer);

	auto Pool = std::make_shared<FConnectionPool>(Config, SslContext, HttpbinIoContext);
	// Use nghttp2.org's httpbin mirror which is more reliable
	FConnectionKey Key("nghttp2.org", "443", true);

	try
	{
		auto Connection = Pool->AcquireConnection(Key);
		if (!Connection)
		{
			AddWarning(TEXT("Could not create SSL connection - skipping test due to network issues"));
			return true;
		}
		TestNotNull(TEXT("SSL connection should be created"), Connection.get());

		if (Connection)
		{
			// Create request to /httpbin/gzip endpoint - this ALWAYS returns gzip-compressed data
			http::request<http::string_body> Request;
			Request.method(http::verb::get);
			Request.target("/httpbin/gzip");  // nghttp2.org/httpbin/gzip always returns gzipped response
			Request.version(11);
			Request.set(http::field::host, "nghttp2.org");
			Request.set(http::field::connection, "keep-alive");
			Request.set(http::field::user_agent, "DiversionHTTP-GZipTest/1.0");
			Request.set(http::field::accept_encoding, "gzip, deflate");
			Request.prepare_payload();

			if (auto SslSession = Connection->GetSslSession())
			{
				auto ConnectFuture = SslSession->ConnectAsync();
				auto ConnectResult = ConnectFuture.wait_for(std::chrono::seconds(15));

				TestTrue(TEXT("SSL connection should complete within timeout"),
						 ConnectResult == std::future_status::ready);

				if (ConnectResult != std::future_status::ready || !ConnectFuture.get())
				{
					AddWarning(TEXT("SSL connection failed - skipping test due to network issues"));
					Pool->ReleaseConnection(Connection);
					return true;
				}

				{
					auto Response = SslSession->ExecuteRequest(Request);

					// Check for connection errors
					TestFalse(TEXT("Request should not have connection errors"), Response.Error.IsSet());

					if (Response.Error.IsSet())
					{
						UE_LOG(LogGZipIntegrationTests, Error, TEXT("Request failed: %s"), *Response.Error.GetValue());
						Pool->ReleaseConnection(Connection);
						return false;
					}

					// Check if the response has Content-Encoding: gzip header
					const FString* ContentEncoding = Response.Headers.Find(TEXT("Content-Encoding"));

					UE_LOG(LogGZipIntegrationTests, Log, TEXT("Response Headers from nghttp2.org/httpbin/gzip:"));
					for (const auto& Header : Response.Headers)
					{
						UE_LOG(LogGZipIntegrationTests, Log, TEXT("  %s: %s"), *Header.Key, *Header.Value);
					}

					if (ContentEncoding && ContentEncoding->Contains(TEXT("gzip")))
					{
						UE_LOG(LogGZipIntegrationTests, Log, TEXT("SUCCESS: Response was gzip-compressed (Content-Encoding: %s)"), **ContentEncoding);

						// Verify the decompressed content is valid JSON containing "gzipped": true
						bool ContentIsValid = Response.Contents.Contains(TEXT("gzipped")) &&
											 Response.Contents.Contains(TEXT("true"));

						TestTrue(TEXT("Decompressed content should contain 'gzipped: true'"), ContentIsValid);

						if (ContentIsValid)
						{
							UE_LOG(LogGZipIntegrationTests, Log, TEXT("SUCCESS: Decompression worked! Content: %s"), *Response.Contents.Left(500));
						}
						else
						{
							UE_LOG(LogGZipIntegrationTests, Error, TEXT("Decompression may have failed. Content: %s"), *Response.Contents.Left(500));
						}
					}
					else
					{
						// nghttp2.org/httpbin/gzip should ALWAYS return gzipped content
						UE_LOG(LogGZipIntegrationTests, Error, TEXT("nghttp2.org/httpbin/gzip did not return gzipped response - unexpected! HTTP %d"), Response.ResponseCode);
						TestTrue(TEXT("nghttp2.org/httpbin/gzip should return Content-Encoding: gzip"), false);
					}

					UE_LOG(LogGZipIntegrationTests, Log,
						   TEXT("nghttp2.org/httpbin/gzip test completed: HTTP %d, Content length: %d"),
						   Response.ResponseCode, Response.Contents.Len());
				}
			}

			Pool->ReleaseConnection(Connection);
		}
	}
	catch (const std::exception& Ex)
	{
		AddError(FString::Printf(TEXT("GZip decompression test failed with exception: %s"),
								UTF8_TO_TCHAR(Ex.what())));
		return false;
	}

	return true;
}


