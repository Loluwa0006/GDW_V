// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "BoostHeaders.h"
#include "Types.h"
#include "DiversionHttpModule.h"
#include "ConnectionPoolMetrics.h"

#include <string>
#include <chrono>
#include <atomic>
#include <thread>
#include <shared_mutex>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp_stream = beast::tcp_stream;
using ssl_stream = beast::ssl_stream<tcp_stream>;

// Forward declarations
template<typename StreamType>
class FHttpSession;
class FPooledConnection;

struct FConnectionKey {
	std::string Host;
	std::string Port;
	bool UseSSL;

	// Default constructor
	FConnectionKey() : UseSSL(false) {}

	// Explicit constructor for compatibility with all compilers
	FConnectionKey(const std::string& InHost, const std::string& InPort, bool InUseSSL)
		: Host(InHost), Port(InPort), UseSSL(InUseSSL) {}

	bool operator==(const FConnectionKey& Other) const
	{
		return Host == Other.Host && Port == Other.Port && UseSSL == Other.UseSSL;
	}

	friend uint32 GetTypeHash(const FConnectionKey& Key)
	{
		return HashCombine(HashCombine(GetTypeHash(FString(UTF8_TO_TCHAR(Key.Host.c_str()))),
									 GetTypeHash(FString(UTF8_TO_TCHAR(Key.Port.c_str())))),
						  GetTypeHash(Key.UseSSL));
	}
};

// Hash functor for FConnectionKey to use in std::unordered_map
namespace std {
	template<>
	struct hash<FConnectionKey> {
		size_t operator()(const FConnectionKey& Key) const
		{
			// Hash combine using std::hash for strings
			size_t h1 = std::hash<std::string>{}(Key.Host);
			size_t h2 = std::hash<std::string>{}(Key.Port);
			size_t h3 = std::hash<bool>{}(Key.UseSSL);
			return h1 ^ (h2 << 1) ^ (h3 << 2);
		}
	};
}

enum class EHealthCheckType {
	SocketOnly,     // Just check if socket is open (default)
	HttpHead,       // Send HEAD request to root
	CustomEndpoint, // Use provided endpoint
	Disabled        // No health checks
};

struct FHealthCheckConfig {
	EHealthCheckType Type = EHealthCheckType::SocketOnly;
	FString CustomEndpoint; // e.g. "/health", "/api/v1/ping"
	std::chrono::seconds Timeout{5};
	uint32 MaxFailures = 3; // Mark invalid after N failures
};

struct FConnectionPoolConfig {
	// Connection limits
	uint32 MaxConnectionsPerHost = 6;
	uint32 MaxTotalConnections = 50;

	// Timeouts - per-host defaults (for localhost: fast, remote: slower)
	std::unordered_map<std::string, std::chrono::seconds> HostConnectionTimeouts;
	std::chrono::seconds DefaultConnectionTimeout{10};

	std::unordered_map<std::string, std::chrono::seconds> HostRequestTimeouts;
	std::chrono::seconds DefaultRequestTimeout{60};

	// Pool management
	std::chrono::seconds KeepAliveTimeout{300}; // 5 minutes
	std::chrono::seconds IdleCleanupInterval{20};
	std::chrono::seconds AcquireTimeout{10}; // Wait for available connection

	// Security
	uint32 MaxConnectionAge = 3600; // Force recreation after 1 hour
	uint32 MaxRequestsPerConnection = 100;
	bool RequireHostWhitelist = false;
	std::unordered_set<std::string> AllowedHosts;
	bool ValidateSSLCertificates = true;

	// Health checks
	std::unordered_map<FConnectionKey, FHealthCheckConfig> HostHealthChecks;
	FHealthCheckConfig DefaultHealthCheck;

	std::chrono::seconds GetConnectionTimeout(const std::string& Host) const
	{
		if (auto it = HostConnectionTimeouts.find(Host); it != HostConnectionTimeouts.end())
		{
			return it->second;
		}
		return DefaultConnectionTimeout;
	}

	std::chrono::seconds GetRequestTimeout(const std::string& Host) const
	{
		if (auto it = HostRequestTimeouts.find(Host); it != HostRequestTimeouts.end())
		{
			return it->second;
		}
		return DefaultRequestTimeout;
	}

	// Validation helper - ensures request timeout >= connection timeout
	std::pair<std::chrono::seconds, std::chrono::seconds> GetValidatedTimeouts(
		const std::string& Host,
		int RequestedConnectionTimeout = -1,
		int RequestedRequestTimeout = -1) const {

		auto ConnectionTimeout = (RequestedConnectionTimeout > 0)
			? std::chrono::seconds(RequestedConnectionTimeout)
			: GetConnectionTimeout(Host);

		auto RequestTimeout = (RequestedRequestTimeout > 0)
			? std::chrono::seconds(RequestedRequestTimeout)
			: GetRequestTimeout(Host);

		// ENFORCE: Request timeout must be >= Connection timeout
		if (RequestTimeout < ConnectionTimeout)
		{
			UE_LOG(LogDiversionHttp, Warning,
				TEXT("Request timeout (%llds) cannot be shorter than connection timeout (%llds) for host %s. Adjusting request timeout."),
				RequestTimeout.count(), ConnectionTimeout.count(), UTF8_TO_TCHAR(Host.c_str()));
			RequestTimeout = ConnectionTimeout + std::chrono::seconds(5); // Add 5s buffer
		}

		return {ConnectionTimeout, RequestTimeout};
	}

	const FHealthCheckConfig& GetHealthCheck(const FConnectionKey& Key) const
	{
		if (auto it = HostHealthChecks.find(Key); it != HostHealthChecks.end())
		{
			return it->second;
		}
		return DefaultHealthCheck;
	}
};

class FConnectionPool {
public:
	explicit FConnectionPool(const FConnectionPoolConfig& InConfig,
						   std::shared_ptr<net::ssl::context> InSslContext,
						   net::io_context& InIoContext);

	virtual ~FConnectionPool();

	// Main API
	std::shared_ptr<FPooledConnection> AcquireConnection(const FConnectionKey& Key,
												   std::chrono::seconds MaxWaitTime = std::chrono::seconds(10));
	void ReleaseConnection(std::shared_ptr<FPooledConnection> Connection);

	// Management
	void CleanupExpiredConnections();
	void Shutdown();

	// Configuration access
	const FConnectionPoolConfig& GetConfig() const { return Config; }

	// Runtime configuration updates
	void UpdateConfiguration(const FConnectionPoolConfig& NewConfig);
	void UpdateTimeouts(const std::unordered_map<std::string, std::chrono::seconds>& ConnectionTimeouts,
					   const std::unordered_map<std::string, std::chrono::seconds>& RequestTimeouts);
	void UpdateConnectionLimits(uint32 MaxPerHost, uint32 MaxTotal);
	void UpdateHealthCheckConfig(const FConnectionKey& Key, const FHealthCheckConfig& HealthConfig);

	// Statistics
	uint32 GetTotalConnections() const { return TotalConnections.load(); }
	uint32 GetAvailableConnections(const FConnectionKey& Key) const;

	// Metrics access
	DiversionHttp::IConnectionPoolMetrics* GetMetrics() const { return Metrics.get(); }
	FString GetMetricsReport() const { return Metrics->GetFormattedStats(); }

protected:
	// Internal methods (protected for testing)
	virtual std::shared_ptr<FPooledConnection> TryCreateNewConnection(const FConnectionKey& Key);

	// Configuration and contexts (protected for testing)
	FConnectionPoolConfig Config;
	std::shared_ptr<net::ssl::context> SslContext;
	net::io_context& IoContext;

private:
	// Internal methods
	std::shared_ptr<FPooledConnection> TryGetAvailableConnection(const FConnectionKey& Key);
	void RunCleanupLoop();
	void SaveMetricsToFile();
	void RemoveConnectionFromPool(const std::shared_ptr<FPooledConnection>& Connection);
	void CleanupExcessConnections(); // Assumes ConnectionsMutex is already locked

	// Thread safety
	mutable std::shared_mutex PoolMutex;

	// Pool data
	std::unordered_map<FConnectionKey, std::vector<std::shared_ptr<FPooledConnection>>> ConnectionsByHost;
	std::atomic<uint32> TotalConnections{0};

	// Background cleanup
	std::atomic<bool> ShouldStop{false};
	std::thread CleanupThread;
	std::condition_variable ShutdownCV;
	std::mutex ShutdownMutex;

	// Metrics collection
	std::unique_ptr<DiversionHttp::IConnectionPoolMetrics> Metrics;
};
