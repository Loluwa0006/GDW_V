// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#include "PooledConnection.h"
#include "ConnectionPool.h"
#include "HttpSession.h"
#include "SslSession.h"
#include "TcpSession.h"

#if PLATFORM_WINDOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <errno.h>
#endif

FPooledConnection::FPooledConnection(const FConnectionKey& InKey, FConnectionPool* InPool)
	: State(EConnectionState::Available)
	, Key(InKey)
	, Pool(InPool)
	, RequestCount(0)
	, FailureCount(0)
	, bSecurityValidated(false)
	, ConnectionTimeout(30)
	, RequestTimeout(60)
{
	CreatedAt = std::chrono::steady_clock::now();
	LastUsed = CreatedAt;
}

FPooledConnection::~FPooledConnection()
{
	// Ensure connection is marked as invalid and shutdown
	SetState(EConnectionState::Invalid);
}

void FPooledConnection::SetState(EConnectionState NewState)
{
	EConnectionState OldState = State.exchange(NewState);

	// If transitioning TO invalid, shutdown immediately for resource cleanup
	if (NewState == EConnectionState::Invalid && OldState != EConnectionState::Invalid)
	{
		ShutdownSessions();
	}
}

bool FPooledConnection::ClaimForUse()
{
	EConnectionState Expected = EConnectionState::Available;
	return State.compare_exchange_strong(Expected, EConnectionState::InUse);
}

void FPooledConnection::ShutdownSessions()
{
	// Gracefully shutdown SSL session first (if present)
	if (SslSession)
	{
		try
		{
			auto SslSessionPtr = std::static_pointer_cast<FHttpSSLSession>(SslSession);
			if (SslSessionPtr)
			{
				SslSessionPtr->Shutdown();
			}
		}
		catch (const std::exception& Ex)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("Exception during SSL session shutdown: %s"), UTF8_TO_TCHAR(Ex.what()));
		}
	}

	// Then shutdown TCP session (if present)
	if (TcpSession)
	{
		try
		{
			auto TcpSessionPtr = std::static_pointer_cast<FHttpTcpSession>(TcpSession);
			if (TcpSessionPtr)
			{
				TcpSessionPtr->Shutdown();
			}
		}
		catch (const std::exception& Ex)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("Exception during TCP session shutdown: %s"), UTF8_TO_TCHAR(Ex.what()));
		}
	}
}

bool FPooledConnection::Initialize(net::io_context& IoContext, net::ssl::context* SslContext)
{
	try
	{
		// Get timeouts from pool configuration
		if (Pool)
		{
			ConnectionTimeout = Pool->GetConfig().DefaultConnectionTimeout;
			RequestTimeout = Pool->GetConfig().DefaultRequestTimeout;
		}

		if (Key.UseSSL)
		{
			if (!SslContext)
			{
				UE_LOG(LogDiversionHttp, Error, TEXT("SSL context required for HTTPS connection"));
				return false;
			}

			SslSession = std::make_shared<FHttpSSLSession>(
				IoContext, *SslContext, Key.Host, Key.Port,
				ConnectionTimeout, RequestTimeout);
		}
		else
		{
			TcpSession = std::make_shared<FHttpTcpSession>(
				IoContext, Key.Host, Key.Port,
				ConnectionTimeout, RequestTimeout);
		}

		bSecurityValidated = true;
		return true;
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Failed to initialize pooled connection: %s"), UTF8_TO_TCHAR(Ex.what()));
		return false;
	}
}

bool FPooledConnection::IsHealthy() const
{
	// Check state
	if (State.load() == EConnectionState::Invalid)
	{
		return false;
	}

	// Check socket state
	if (!IsSocketOpen())
	{
		return false;
	}

	// Check if expired
	if (IsExpired(std::chrono::seconds(300))) // 5 minutes default
	{
		return false;
	}

	// Check request limits
	if (RequestCount.load() >= Pool->GetConfig().MaxRequestsPerConnection)
	{
		return false;
	}

	// Check failure count
	if (FailureCount.load() > 3)
	{
		return false;
	}

	return true;
}

bool FPooledConnection::IsExpired(std::chrono::seconds MaxAge) const
{
	const auto Now = std::chrono::steady_clock::now();
	const auto Age = std::chrono::duration_cast<std::chrono::seconds>(Now - LastUsed);
	return Age >= MaxAge;
}

bool FPooledConnection::IsSecure() const
{
	// Check security validation flag
	if (!bSecurityValidated.load())
	{
		return false;
	}

	// Check failure count for security violations
	if (FailureCount.load() > 3)
	{
		return false;
	}

	// For SSL connections, actively verify SSL state
	if (Key.UseSSL && SslSession)
	{
		return ValidateSSLContext();
	}

	return true;
}

void FPooledConnection::MarkUsed()
{
	LastUsed = std::chrono::steady_clock::now();
	RequestCount.fetch_add(1);

	// Reset failure count on successful use
	FailureCount.store(0);
}

void FPooledConnection::ResetForNewRequest()
{
	UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("PooledConnection %p: ResetForNewRequest called, State=%d"), this, (int)State.load());

	// Only reset if connection is in a valid state
	if (State.load() == EConnectionState::Invalid)
	{
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("PooledConnection %p: Already invalid, skipping reset"), this);
		return;
	}

	// Take local copies of session shared_ptrs to prevent use-after-free
	// if another thread calls ShutdownSessions() concurrently (e.g. during pool shutdown)
	auto LocalTcpSession = TcpSession;
	auto LocalSslSession = SslSession;

	// Validate session pointers before use
	if (!LocalTcpSession && !LocalSslSession)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("PooledConnection %p: Both TcpSession and SslSession are invalid"), this);
		SetState(EConnectionState::Invalid);
		return;
	}

	// Re-check state after copying session pointers - shutdown may have invalidated us
	if (State.load() == EConnectionState::Invalid)
	{
		return;
	}

	// Reset session state for reuse
	if (LocalTcpSession && IsHealthy())
	{
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("PooledConnection %p: Resetting TcpSession %p"), this, LocalTcpSession.get());
		if (!LocalTcpSession->ResetState())
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("PooledConnection %p: TcpSession reset failed - marking connection invalid"), this);
			SetState(EConnectionState::Invalid);
			return;
		}
	}

	if (LocalSslSession && IsHealthy())
	{
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("PooledConnection %p: Resetting SslSession %p"), this, LocalSslSession.get());
		if (!LocalSslSession->ResetState())
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("PooledConnection %p: SslSession reset failed - marking connection invalid"), this);
			SetState(EConnectionState::Invalid);
			return;
		}
	}

	UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("PooledConnection %p: Reset completed successfully"), this);
}

bool FPooledConnection::ValidateConnection()
{
	if (!Pool)
	{
		return false;
	}

	const auto& HealthConfig = Pool->GetConfig().GetHealthCheck(Key);

	switch (HealthConfig.Type)
	{
		case EHealthCheckType::SocketOnly:
			return IsSocketOpen();

		case EHealthCheckType::HttpHead:
			return SendHealthCheckRequest("HEAD", "/", HealthConfig.Timeout);

		case EHealthCheckType::CustomEndpoint:
			return SendHealthCheckRequest("GET", std::string(TCHAR_TO_UTF8(*HealthConfig.CustomEndpoint)), HealthConfig.Timeout);

		case EHealthCheckType::Disabled:
			return true; // Trust the connection

		default:
			return IsSocketOpen();
	}
}

bool FPooledConnection::SendHealthCheckRequest(const std::string& Method, const std::string& Endpoint, std::chrono::seconds Timeout)
{
	try
	{
		// Try to atomically claim the connection for health check
		if (!ClaimForUse())
		{
			// Connection is busy or invalid - skip health check
			EConnectionState CurrentState = GetState();
			UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Health check skipped - connection not available for %s:%s (State: %d)"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), (int)CurrentState);
			return CurrentState != EConnectionState::Invalid; // Return false only if truly invalid
		}

		// Connection acquired - perform health check
		bool IsHealthy = false;

		// Quick socket validation first
		if (!IsSocketOpen())
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("Health check failed - socket not open for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
		}
		else if (IsSecure() && !ValidateSSLContext())
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("Health check failed - SSL context invalid for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
		}
		else
		{
			// Create lightweight HTTP health check request
			beast::http::request<beast::http::string_body> HealthRequest;
			HealthRequest.method(Method == "HEAD" ? beast::http::verb::head : beast::http::verb::get);
			HealthRequest.target(Endpoint);
			HealthRequest.version(11); // HTTP/1.1
			HealthRequest.set(beast::http::field::host, Key.Host);
			HealthRequest.set(beast::http::field::connection, "keep-alive");
			HealthRequest.set(beast::http::field::user_agent, "DiversionHTTP-HealthCheck/1.0");
			HealthRequest.prepare_payload();

			// Execute health check request
			DiversionHttp::HTTPCallResponse HealthResponse;

			auto LocalSslSession = SslSession;
			auto LocalTcpSession = TcpSession;

			if (LocalSslSession)
			{
				auto OriginalTimeout = LocalSslSession->GetRequestTimeout();
				LocalSslSession->SetRequestTimeout(Timeout);
				HealthResponse = LocalSslSession->ExecuteRequest(HealthRequest);
				LocalSslSession->SetRequestTimeout(OriginalTimeout);
			}
			else if (LocalTcpSession)
			{
				auto OriginalTimeout = LocalTcpSession->GetRequestTimeout();
				LocalTcpSession->SetRequestTimeout(Timeout);
				HealthResponse = LocalTcpSession->ExecuteRequest(HealthRequest);
				LocalTcpSession->SetRequestTimeout(OriginalTimeout);
			}

			// Validate response
			if (HealthResponse.Error.IsSet())
			{
				UE_LOG(LogDiversionHttp, Warning, TEXT("Health check request failed for %s:%s - %s"),
					   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()),
					   *HealthResponse.Error.GetValue());
			}
			else
			{
				int32 StatusCode = HealthResponse.ResponseCode;
				IsHealthy = (StatusCode >= 200 && StatusCode <= 299) || StatusCode == 404;

				if (IsHealthy)
				{
					UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Health check succeeded for %s:%s - HTTP %d"),
						   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), StatusCode);
					// Update usage tracking since this was a successful request
					MarkUsed();
				}
				else
				{
					UE_LOG(LogDiversionHttp, Warning, TEXT("Health check failed for %s:%s - HTTP %d"),
						   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), StatusCode);
				}
			}
		}

		// Release connection back to available state
		State.store(EConnectionState::Available);

		if (!IsHealthy)
		{
			FailureCount.fetch_add(1);
		}

		return IsHealthy;
	}
	catch (const std::exception& Ex)
	{
		// Ensure connection is released even on exception
		State.store(EConnectionState::Available);

		UE_LOG(LogDiversionHttp, Warning, TEXT("Health check failed for %s:%s - %s"),
			   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), UTF8_TO_TCHAR(Ex.what()));

		FailureCount.fetch_add(1);
		return false;
	}
}

void FPooledConnection::SetTimeouts(std::chrono::seconds InConnectionTimeout, std::chrono::seconds InRequestTimeout)
{
	ConnectionTimeout = InConnectionTimeout;
	RequestTimeout = InRequestTimeout;

	// Take local copies to prevent use-after-free if ShutdownSessions() runs concurrently
	auto LocalTcpSession = TcpSession;
	auto LocalSslSession = SslSession;

	if (LocalTcpSession)
	{
		LocalTcpSession->SetConnectionTimeout(InConnectionTimeout);
		LocalTcpSession->SetRequestTimeout(InRequestTimeout);
	}

	if (LocalSslSession)
	{
		LocalSslSession->SetConnectionTimeout(InConnectionTimeout);
		LocalSslSession->SetRequestTimeout(InRequestTimeout);
	}
}

bool FPooledConnection::IsSocketOpen() const
{
	try
	{
		auto LocalTcpSession = TcpSession;
		auto LocalSslSession = SslSession;

		if (LocalTcpSession)
		{
			return LocalTcpSession->IsSocketOpen();
		}

		if (LocalSslSession)
		{
			return LocalSslSession->IsSocketOpen();
		}

		return false;
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("Error checking socket state: %s"), UTF8_TO_TCHAR(Ex.what()));
		return false;
	}
}

int FPooledConnection::GetNativeSocketHandle() const
{
	try
	{
		auto LocalTcpSession = TcpSession;
		auto LocalSslSession = SslSession;

		// Try TCP session first
		if (LocalTcpSession && LocalTcpSession->IsSocketOpen())
		{
			auto TcpSessionPtr = std::static_pointer_cast<FHttpTcpSession>(LocalTcpSession);
			if (TcpSessionPtr)
			{
				auto& Socket = TcpSessionPtr->TcpStream().socket();

#if PLATFORM_WINDOWS
				// Windows uses SOCKET (UINT_PTR) type
				return static_cast<int>(Socket.native_handle());
#else
				// Unix-like systems use int file descriptors
				return Socket.native_handle();
#endif
			}
		}

		// Try SSL session (get underlying TCP socket)
		if (LocalSslSession && LocalSslSession->IsSocketOpen())
		{
			auto SslSessionPtr = std::static_pointer_cast<FHttpSSLSession>(LocalSslSession);
			if (SslSessionPtr)
			{
				auto& Socket = SslSessionPtr->TcpStream().socket();

#if PLATFORM_WINDOWS
				// Windows uses SOCKET (UINT_PTR) type
				return static_cast<int>(Socket.native_handle());
#else
				// Unix-like systems use int file descriptors
				return Socket.native_handle();
#endif
			}
		}

		return -1; // Invalid handle
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("Error getting native socket handle: %s"), UTF8_TO_TCHAR(Ex.what()));
		return -1;
	}
}

bool FPooledConnection::ValidateSSLContext() const
{
	auto LocalSslSession = SslSession;

	if (!Key.UseSSL || !LocalSslSession)
	{
		return true; // Not SSL or no SSL session
	}

	try
	{
		// Get the native SSL handle from the SSL session
		auto SSLSessionPtr = std::static_pointer_cast<FHttpSSLSession>(LocalSslSession);
		if (!SSLSessionPtr)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL session cast failed for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			return false;
		}

		SSL* ssl = SSLSessionPtr->GetSSLHandle();
		if (!ssl)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL handle not available for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			return false;
		}

		// Verify SSL handshake is complete
		if (!SSL_is_init_finished(ssl))
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL handshake not completed for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			return false;
		}

		// Verify certificate was validated during handshake
		long verify_result = SSL_get_verify_result(ssl);
		if (verify_result != X509_V_OK)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL certificate verification failed for %s:%s - error code: %ld"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), verify_result);
			return false;
		}

		// Get and verify peer certificate exists
		X509* peer_cert = SSL_get_peer_certificate(ssl);
		if (!peer_cert)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("No peer certificate found for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			return false;
		}

		// Verify certificate is still valid (not expired)
		if (X509_cmp_current_time(X509_get_notAfter(peer_cert)) < 0)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL certificate has expired for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			X509_free(peer_cert);
			return false;
		}

		// Verify certificate is not yet valid (before notBefore date)
		if (X509_cmp_current_time(X509_get_notBefore(peer_cert)) > 0)
		{
			UE_LOG(LogDiversionHttp, Warning, TEXT("SSL certificate not yet valid for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
			X509_free(peer_cert);
			return false;
		}

		X509_free(peer_cert);

		// Finally check if socket is still open
		bool SocketOpen = IsSocketOpen();
		if (!SocketOpen)
		{
			UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("SSL validation failed - socket closed for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
		}

		return SocketOpen;
	}
	catch (const std::exception& Ex)
	{
		UE_LOG(LogDiversionHttp, Warning, TEXT("SSL validation failed for %s:%s: %s"),
			   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()), UTF8_TO_TCHAR(Ex.what()));
		return false;
	}
}
