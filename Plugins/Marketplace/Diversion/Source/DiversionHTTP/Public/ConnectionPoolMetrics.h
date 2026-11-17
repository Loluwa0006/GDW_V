// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include <atomic>
#include <chrono>
#include <mutex>

struct FConnectionKey;

namespace DiversionHttp {

enum class EMetricEvent
{
	ConnectionRequested,
	ConnectionPoolHit,
	ConnectionPoolMiss,
	ConnectionCreated,
	ConnectionDestroyed,
	ConnectionReused,
	RequestServed,
	RequestSucceeded,
	RequestFailed,
	HealthCheckPassed,
	HealthCheckFailed,
	ConnectionMarkedInvalid,
	ExpiredConnectionRemoved,
	SSLHandshakeSuccess,
	SSLHandshakeFailure,
	SSLValidationFailure,
	ConnectionTimeout,
	RequestTimeout,
	DNSResolutionFailure,
	NetworkError,
	HTTPError
};

struct FConnectionPoolStats
{
	// Connection acquisition metrics
	std::atomic<uint64> TotalConnectionsRequested{0};
	std::atomic<uint64> ConnectionPoolHits{0};        // Reused existing connection
	std::atomic<uint64> ConnectionPoolMisses{0};      // Had to create new connection
	std::atomic<uint64> ConnectionsCreated{0};
	std::atomic<uint64> ConnectionsDestroyed{0};

	// Connection reuse metrics
	std::atomic<uint64> TotalRequestsServed{0};
	std::atomic<uint64> ConnectionReuseCount{0};      // Number of times connections were reused

	// Connection health metrics
	std::atomic<uint64> HealthChecksPassed{0};
	std::atomic<uint64> HealthChecksFailed{0};
	std::atomic<uint64> ConnectionsMarkedInvalid{0};
	std::atomic<uint64> ExpiredConnectionsRemoved{0};

	// SSL-specific metrics
	std::atomic<uint64> SSLHandshakeSuccesses{0};
	std::atomic<uint64> SSLHandshakeFailures{0};
	std::atomic<uint64> SSLValidationFailures{0};

	// Timeout metrics
	std::atomic<uint64> ConnectionTimeouts{0};
	std::atomic<uint64> RequestTimeouts{0};

	// Performance metrics (in milliseconds)
	std::atomic<uint64> TotalConnectionTimeMs{0};     // Sum of all connection establishment times
	std::atomic<uint64> TotalRequestTimeMs{0};        // Sum of all request execution times

	// Current state
	std::atomic<uint32> CurrentActiveConnections{0};
	std::atomic<uint32> CurrentAvailableConnections{0};

	// Calculated metrics
	double GetConnectionReuseRate() const;
	double GetPoolHitRatio() const;
	double GetAverageConnectionTime() const;
	double GetAverageRequestTime() const;
	double GetHealthCheckSuccessRate() const;
	double GetSSLHandshakeSuccessRate() const;
};

struct FPerHostStats
{
	std::atomic<uint64> RequestCount{0};
	std::atomic<uint64> ConnectionCount{0};
	std::atomic<uint64> SuccessfulRequests{0};
	std::atomic<uint64> FailedRequests{0};

	// Detailed failure tracking
	std::atomic<uint64> ConnectionTimeouts{0};
	std::atomic<uint64> RequestTimeouts{0};
	std::atomic<uint64> DNSFailures{0};
	std::atomic<uint64> NetworkErrors{0};
	std::atomic<uint64> HTTPErrors{0};
	std::atomic<uint64> SSLErrors{0};

	std::chrono::steady_clock::time_point FirstRequest;
	std::chrono::steady_clock::time_point LastRequest;
	std::chrono::steady_clock::time_point LastSuccess;
	std::chrono::steady_clock::time_point LastFailure;

	// Custom copy constructor needed for atomic members
	FPerHostStats() = default;
	FPerHostStats(const FPerHostStats& Other)
		: RequestCount(Other.RequestCount.load())
		, ConnectionCount(Other.ConnectionCount.load())
		, SuccessfulRequests(Other.SuccessfulRequests.load())
		, FailedRequests(Other.FailedRequests.load())
		, ConnectionTimeouts(Other.ConnectionTimeouts.load())
		, RequestTimeouts(Other.RequestTimeouts.load())
		, DNSFailures(Other.DNSFailures.load())
		, NetworkErrors(Other.NetworkErrors.load())
		, HTTPErrors(Other.HTTPErrors.load())
		, SSLErrors(Other.SSLErrors.load())
		, FirstRequest(Other.FirstRequest)
		, LastRequest(Other.LastRequest)
		, LastSuccess(Other.LastSuccess)
		, LastFailure(Other.LastFailure)
	{}

	FPerHostStats& operator=(const FPerHostStats& Other)
	{
		if (this != &Other)
		{
			RequestCount.store(Other.RequestCount.load());
			ConnectionCount.store(Other.ConnectionCount.load());
			SuccessfulRequests.store(Other.SuccessfulRequests.load());
			FailedRequests.store(Other.FailedRequests.load());
			ConnectionTimeouts.store(Other.ConnectionTimeouts.load());
			RequestTimeouts.store(Other.RequestTimeouts.load());
			DNSFailures.store(Other.DNSFailures.load());
			NetworkErrors.store(Other.NetworkErrors.load());
			HTTPErrors.store(Other.HTTPErrors.load());
			SSLErrors.store(Other.SSLErrors.load());
			FirstRequest = Other.FirstRequest;
			LastRequest = Other.LastRequest;
			LastSuccess = Other.LastSuccess;
			LastFailure = Other.LastFailure;
		}
		return *this;
	}

	// Calculated metrics
	double GetSuccessRate() const
	{
		uint64 Total = SuccessfulRequests.load() + FailedRequests.load();
		return Total == 0 ? 100.0 : (static_cast<double>(SuccessfulRequests.load()) / static_cast<double>(Total)) * 100.0;
	}
};

// Interface for metrics collection
class DIVERSIONHTTP_API IConnectionPoolMetrics
{
public:
	virtual ~IConnectionPoolMetrics() = default;

	// Single event-based recording method
	virtual void RecordEvent(EMetricEvent Event, const FConnectionKey* Key = nullptr) = 0;
	virtual void RecordTiming(EMetricEvent Event, std::chrono::milliseconds Duration) = 0;
	virtual void UpdateConnectionCounts(uint32 ActiveCount, uint32 AvailableCount) = 0;
	virtual void RecordRequestResult(const FConnectionKey& Key, bool bSuccess, EMetricEvent FailureReason = EMetricEvent::RequestSucceeded) = 0;

	// Statistics access
	virtual FString GetFormattedStats() const = 0;
	virtual FString GetFormattedHostStats() const = 0;
	virtual void ResetStats() = 0;
};

// Active metrics implementation
class DIVERSIONHTTP_API FConnectionPoolMetrics : public IConnectionPoolMetrics
{
public:
	FConnectionPoolMetrics() = default;
	~FConnectionPoolMetrics() override = default;

	// IConnectionPoolMetrics implementation
	void RecordEvent(EMetricEvent Event, const FConnectionKey* Key = nullptr) override;
	void RecordTiming(EMetricEvent Event, std::chrono::milliseconds Duration) override;
	void UpdateConnectionCounts(uint32 ActiveCount, uint32 AvailableCount) override;
	void RecordRequestResult(const FConnectionKey& Key, bool bSuccess, EMetricEvent FailureReason = EMetricEvent::RequestSucceeded) override;
	FString GetFormattedStats() const override;
	FString GetFormattedHostStats() const override;
	void ResetStats() override;

	// Direct access to detailed stats (only available on active implementation)
	const FConnectionPoolStats& GetStats() const { return Stats; }
	FPerHostStats GetHostStats(const FConnectionKey& Key) const;
	TMap<FString, FPerHostStats> GetAllHostStats() const;

private:
	FConnectionPoolStats Stats;
	mutable std::mutex HostStatsMutex;
	TMap<FString, FPerHostStats> HostStats;
	FString GetHostKey(const FConnectionKey& Key) const;
};


} // namespace DiversionHttp
