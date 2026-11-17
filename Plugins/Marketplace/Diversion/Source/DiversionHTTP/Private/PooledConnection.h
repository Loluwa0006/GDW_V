// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "BoostHeaders.h"
#include "Types.h"
#include "DiversionHttpModule.h"
#include "ConnectionPool.h"

#include <string>
#include <chrono>
#include <atomic>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;

// Forward declarations
template<typename StreamType>
class FHttpSession;
class FConnectionPool;

enum class EConnectionState {
	Available,    // Ready for use
	InUse,       // Currently processing request
	Invalid,     // Needs to be discarded
	Connecting   // Being established
};

class FPooledConnection {
public:
	explicit FPooledConnection(const FConnectionKey& InKey, FConnectionPool* InPool);
	~FPooledConnection();

	// Connection management
	bool Initialize(net::io_context& IoContext, net::ssl::context* SslContext = nullptr);
	bool IsHealthy() const;
	bool IsExpired(std::chrono::seconds MaxAge) const;
	bool IsSecure() const;
	void MarkUsed();
	void ResetForNewRequest();

	// Health checking
	bool ValidateConnection();
	bool SendHealthCheckRequest(const std::string& Method, const std::string& Endpoint, std::chrono::seconds Timeout);
	bool IsSocketOpen() const;

	// Session access
	std::shared_ptr<FHttpSession<tcp_stream>> GetTcpSession() const { return TcpSession; }
	std::shared_ptr<FHttpSession<ssl_stream>> GetSslSession() const { return SslSession; }

	// State management
	EConnectionState GetState() const { return State; }
	void SetState(EConnectionState NewState);
	// Atomically claim an Available connection for use. Returns true if successful.
	bool ClaimForUse();

	// Connection info
	const FConnectionKey& GetKey() const { return Key; }
	uint32 GetRequestCount() const { return RequestCount; }
	uint32 GetFailureCount() const { return FailureCount; }
	std::chrono::steady_clock::time_point GetLastUsed() const { return LastUsed; }
	std::chrono::steady_clock::time_point GetCreatedAt() const { return CreatedAt; }

	// Failure tracking
	void IncrementFailureCount() { FailureCount.fetch_add(1); }

	// Timeout management
	void SetTimeouts(std::chrono::seconds ConnectionTimeout, std::chrono::seconds RequestTimeout);

	// Native socket handle access (for testing)
	int GetNativeSocketHandle() const;

private:
	// Socket validation helpers
	bool ValidateSSLContext() const;

	// Resource cleanup
	void ShutdownSessions();

	// Connection data - using std::shared_ptr for enable_shared_from_this compatibility
	std::shared_ptr<FHttpSession<tcp_stream>> TcpSession;
	std::shared_ptr<FHttpSession<ssl_stream>> SslSession;

	// State
	std::atomic<EConnectionState> State{EConnectionState::Available};
	FConnectionKey Key;
	FConnectionPool* Pool; // Non-owning reference

	// Usage tracking
	std::atomic<uint32> RequestCount{0};
	std::atomic<uint32> FailureCount{0};
	std::chrono::steady_clock::time_point LastUsed;
	std::chrono::steady_clock::time_point CreatedAt;

	// Security
	std::atomic<bool> bSecurityValidated{false};

	// Timeouts
	std::chrono::seconds ConnectionTimeout{30};
	std::chrono::seconds RequestTimeout{60};
};
