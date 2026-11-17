// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "ConnectionPoolMetrics.h"
#include "ConnectionPool.h"
#include "DiversionHttpModule.h"
#include "DiversionHttpConfig.h"

using namespace DiversionHttp;

// FConnectionPoolStats calculated metrics implementation
double FConnectionPoolStats::GetConnectionReuseRate() const
{
	uint64 TotalRequests = TotalRequestsServed.load();
	uint64 ReuseCount = ConnectionReuseCount.load();

	if (TotalRequests == 0)
	{
		return 0.0;
	}
	return static_cast<double>(ReuseCount) / static_cast<double>(TotalRequests);
}

double FConnectionPoolStats::GetPoolHitRatio() const
{
	uint64 Hits = ConnectionPoolHits.load();
	uint64 Total = TotalConnectionsRequested.load();

	if (Total == 0)
	{
		return 0.0;
	}
	return static_cast<double>(Hits) / static_cast<double>(Total);
}

double FConnectionPoolStats::GetAverageConnectionTime() const
{
	uint64 TotalTime = TotalConnectionTimeMs.load();
	uint64 Count = ConnectionsCreated.load();

	if (Count == 0)
	{
		return 0.0;
	}
	return static_cast<double>(TotalTime) / static_cast<double>(Count);
}

double FConnectionPoolStats::GetAverageRequestTime() const
{
	uint64 TotalTime = TotalRequestTimeMs.load();
	uint64 Count = TotalRequestsServed.load();

	if (Count == 0)
	{
		return 0.0;
	}
	return static_cast<double>(TotalTime) / static_cast<double>(Count);
}

double FConnectionPoolStats::GetHealthCheckSuccessRate() const
{
	uint64 Passed = HealthChecksPassed.load();
	uint64 Failed = HealthChecksFailed.load();
	uint64 Total = Passed + Failed;

	if (Total == 0)
	{
		return 100.0;
	}
	return static_cast<double>(Passed) / static_cast<double>(Total) * 100.0;
}

double FConnectionPoolStats::GetSSLHandshakeSuccessRate() const
{
	uint64 Success = SSLHandshakeSuccesses.load();
	uint64 Failed = SSLHandshakeFailures.load();
	uint64 Total = Success + Failed;

	if (Total == 0)
	{
		return 100.0;
	}
	return static_cast<double>(Success) / static_cast<double>(Total) * 100.0;
}

void FConnectionPoolMetrics::RecordEvent(EMetricEvent Event, const FConnectionKey* Key)
{
	// Check if metrics are enabled at runtime
	if (!IsDiversionConnectionPoolMetricsEnabled())
	{
		return;
	}

	switch (Event)
	{
		case EMetricEvent::ConnectionRequested:
			Stats.TotalConnectionsRequested.fetch_add(1);
			break;
		case EMetricEvent::ConnectionPoolHit:
			Stats.ConnectionPoolHits.fetch_add(1);
			break;
		case EMetricEvent::ConnectionPoolMiss:
			Stats.ConnectionPoolMisses.fetch_add(1);
			break;
		case EMetricEvent::ConnectionCreated:
			Stats.ConnectionsCreated.fetch_add(1);
			if (Key)
			{
				std::lock_guard<std::mutex> Lock(HostStatsMutex);
				FString HostKey = GetHostKey(*Key);
				FPerHostStats& HostStat = HostStats.FindOrAdd(HostKey);
				HostStat.ConnectionCount.fetch_add(1);
			}
			break;
		case EMetricEvent::ConnectionDestroyed:
			Stats.ConnectionsDestroyed.fetch_add(1);
			break;
		case EMetricEvent::ConnectionReused:
			Stats.ConnectionReuseCount.fetch_add(1);
			break;
		case EMetricEvent::RequestServed:
			Stats.TotalRequestsServed.fetch_add(1);
			if (Key)
			{
				std::lock_guard<std::mutex> Lock(HostStatsMutex);
				FString HostKey = GetHostKey(*Key);
				FPerHostStats& HostStat = HostStats.FindOrAdd(HostKey);
				HostStat.RequestCount.fetch_add(1);
				HostStat.LastRequest = std::chrono::steady_clock::now();
				if (HostStat.FirstRequest == std::chrono::steady_clock::time_point{})
				{
					HostStat.FirstRequest = HostStat.LastRequest;
				}
			}
			break;
		case EMetricEvent::HealthCheckPassed:
			Stats.HealthChecksPassed.fetch_add(1);
			break;
		case EMetricEvent::HealthCheckFailed:
			Stats.HealthChecksFailed.fetch_add(1);
			break;
		case EMetricEvent::ConnectionMarkedInvalid:
			Stats.ConnectionsMarkedInvalid.fetch_add(1);
			break;
		case EMetricEvent::ExpiredConnectionRemoved:
			Stats.ExpiredConnectionsRemoved.fetch_add(1);
			break;
		case EMetricEvent::SSLHandshakeSuccess:
			Stats.SSLHandshakeSuccesses.fetch_add(1);
			break;
		case EMetricEvent::SSLHandshakeFailure:
			Stats.SSLHandshakeFailures.fetch_add(1);
			break;
		case EMetricEvent::SSLValidationFailure:
			Stats.SSLValidationFailures.fetch_add(1);
			break;
		case EMetricEvent::ConnectionTimeout:
			Stats.ConnectionTimeouts.fetch_add(1);
			break;
		case EMetricEvent::RequestTimeout:
			Stats.RequestTimeouts.fetch_add(1);
			break;
		case EMetricEvent::DNSResolutionFailure:
			Stats.ConnectionTimeouts.fetch_add(1); // Group with connection issues
			break;
		case EMetricEvent::NetworkError:
			Stats.ConnectionTimeouts.fetch_add(1); // Group with connection issues
			break;
		case EMetricEvent::HTTPError:
			// Track as general failure, specific tracking in per-host stats
			break;
		case EMetricEvent::RequestSucceeded:
		case EMetricEvent::RequestFailed:
			// These are handled by RecordRequestResult
			break;
	}
}

void FConnectionPoolMetrics::RecordTiming(EMetricEvent Event, std::chrono::milliseconds Duration)
{
	if (!IsDiversionConnectionPoolMetricsEnabled())
	{
		return;
	}

	switch (Event)
	{
		case EMetricEvent::ConnectionRequested:
			Stats.TotalConnectionTimeMs.fetch_add(Duration.count());
			break;
		case EMetricEvent::RequestServed:
			Stats.TotalRequestTimeMs.fetch_add(Duration.count());
			break;
		default:
			// Other events don't support timing
			break;
	}
}

void FConnectionPoolMetrics::UpdateConnectionCounts(uint32 ActiveCount, uint32 AvailableCount)
{
	if (!IsDiversionConnectionPoolMetricsEnabled())
	{
		return;
	}

	Stats.CurrentActiveConnections.store(ActiveCount);
	Stats.CurrentAvailableConnections.store(AvailableCount);
}

void FConnectionPoolMetrics::RecordRequestResult(const FConnectionKey& Key, bool bSuccess, EMetricEvent FailureReason)
{
	if (!IsDiversionConnectionPoolMetricsEnabled())
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(HostStatsMutex);
	FString HostKey = GetHostKey(Key);
	FPerHostStats& HostStat = HostStats.FindOrAdd(HostKey);

	auto Now = std::chrono::steady_clock::now();

	if (bSuccess)
	{
		HostStat.SuccessfulRequests.fetch_add(1);
		HostStat.LastSuccess = Now;
	}
	else
	{
		HostStat.FailedRequests.fetch_add(1);
		HostStat.LastFailure = Now;

		// Track specific failure reasons
		switch (FailureReason)
		{
			case EMetricEvent::ConnectionTimeout:
				HostStat.ConnectionTimeouts.fetch_add(1);
				break;
			case EMetricEvent::RequestTimeout:
				HostStat.RequestTimeouts.fetch_add(1);
				break;
			case EMetricEvent::DNSResolutionFailure:
				HostStat.DNSFailures.fetch_add(1);
				break;
			case EMetricEvent::NetworkError:
				HostStat.NetworkErrors.fetch_add(1);
				break;
			case EMetricEvent::HTTPError:
				HostStat.HTTPErrors.fetch_add(1);
				break;
			case EMetricEvent::SSLHandshakeFailure:
			case EMetricEvent::SSLValidationFailure:
				HostStat.SSLErrors.fetch_add(1);
				break;
			default:
				// Generic failure
				break;
		}
	}
}

FPerHostStats FConnectionPoolMetrics::GetHostStats(const FConnectionKey& Key) const
{
	std::lock_guard<std::mutex> Lock(HostStatsMutex);
	FString HostKey = GetHostKey(Key);

	const FPerHostStats* Found = HostStats.Find(HostKey);
	return Found ? *Found : FPerHostStats{};
}

TMap<FString, FPerHostStats> FConnectionPoolMetrics::GetAllHostStats() const
{
	std::lock_guard<std::mutex> Lock(HostStatsMutex);
	return HostStats;
}

FString FConnectionPoolMetrics::GetFormattedStats() const
{
	FString Output;
	Output += TEXT("=== Connection Pool Statistics ===\n");

	Output += FString::Printf(TEXT("Total Connections Requested: %llu\n"), Stats.TotalConnectionsRequested.load());
	Output += FString::Printf(TEXT("Pool Hit Ratio: %.2f%% (%llu hits / %llu total)\n"),
		Stats.GetPoolHitRatio() * 100.0, Stats.ConnectionPoolHits.load(), Stats.TotalConnectionsRequested.load());
	Output += FString::Printf(TEXT("Connection Reuse Rate: %.2f\n"), Stats.GetConnectionReuseRate());

	Output += FString::Printf(TEXT("Active Connections: %u\n"), Stats.CurrentActiveConnections.load());
	Output += FString::Printf(TEXT("Available Connections: %u\n"), Stats.CurrentAvailableConnections.load());

	Output += FString::Printf(TEXT("Average Connection Time: %.2fms\n"), Stats.GetAverageConnectionTime());
	Output += FString::Printf(TEXT("Average Request Time: %.2fms\n"), Stats.GetAverageRequestTime());

	Output += FString::Printf(TEXT("Health Check Success Rate: %.2f%%\n"), Stats.GetHealthCheckSuccessRate());
	Output += FString::Printf(TEXT("SSL Handshake Success Rate: %.2f%%\n"), Stats.GetSSLHandshakeSuccessRate());

	Output += FString::Printf(TEXT("Connection Timeouts: %llu\n"), Stats.ConnectionTimeouts.load());
	Output += FString::Printf(TEXT("Request Timeouts: %llu\n"), Stats.RequestTimeouts.load());
	Output += FString::Printf(TEXT("Invalid Connections: %llu\n"), Stats.ConnectionsMarkedInvalid.load());

	return Output;
}

FString FConnectionPoolMetrics::GetFormattedHostStats() const
{
	std::lock_guard<std::mutex> Lock(HostStatsMutex);

	FString Output;
	Output += TEXT("=== Per-Host Statistics ===\n");

	for (const auto& Pair : HostStats)
	{
		const FString& Host = Pair.Key;
		const FPerHostStats& HostStat = Pair.Value;

		Output += FString::Printf(TEXT("\nHost: %s\n"), *Host);
		Output += FString::Printf(TEXT("  Total Requests: %llu\n"), HostStat.RequestCount.load());
		Output += FString::Printf(TEXT("  Successful Requests: %llu\n"), HostStat.SuccessfulRequests.load());
		Output += FString::Printf(TEXT("  Failed Requests: %llu\n"), HostStat.FailedRequests.load());
		Output += FString::Printf(TEXT("  Success Rate: %.2f%%\n"), HostStat.GetSuccessRate());
		Output += FString::Printf(TEXT("  Connections Created: %llu\n"), HostStat.ConnectionCount.load());

		// Detailed failure breakdown
		if (HostStat.FailedRequests.load() > 0)
		{
			Output += FString::Printf(TEXT("  Failure Breakdown:\n"));
			if (HostStat.ConnectionTimeouts.load() > 0)
			{
				Output += FString::Printf(TEXT("    Connection Timeouts: %llu\n"), HostStat.ConnectionTimeouts.load());
			}
			if (HostStat.RequestTimeouts.load() > 0)
			{
				Output += FString::Printf(TEXT("    Request Timeouts: %llu\n"), HostStat.RequestTimeouts.load());
			}
			if (HostStat.DNSFailures.load() > 0)
			{
				Output += FString::Printf(TEXT("    DNS Resolution Failures: %llu\n"), HostStat.DNSFailures.load());
			}
			if (HostStat.NetworkErrors.load() > 0)
			{
				Output += FString::Printf(TEXT("    Network Errors: %llu\n"), HostStat.NetworkErrors.load());
			}
			if (HostStat.HTTPErrors.load() > 0)
			{
				Output += FString::Printf(TEXT("    HTTP Errors: %llu\n"), HostStat.HTTPErrors.load());
			}
			if (HostStat.SSLErrors.load() > 0)
			{
				Output += FString::Printf(TEXT("    SSL Errors: %llu\n"), HostStat.SSLErrors.load());
			}
		}

		if (HostStat.FirstRequest != std::chrono::steady_clock::time_point{})
		{
			auto Now = std::chrono::steady_clock::now();
			auto Duration = std::chrono::duration_cast<std::chrono::seconds>(Now - HostStat.FirstRequest);
			Output += FString::Printf(TEXT("  Active Duration: %lld seconds\n"), Duration.count());
		}
	}

	return Output;
}

void FConnectionPoolMetrics::ResetStats()
{
	// Reset all atomic counters individually
	Stats.TotalConnectionsRequested.store(0);
	Stats.ConnectionPoolHits.store(0);
	Stats.ConnectionPoolMisses.store(0);
	Stats.ConnectionsCreated.store(0);
	Stats.ConnectionsDestroyed.store(0);
	Stats.TotalRequestsServed.store(0);
	Stats.ConnectionReuseCount.store(0);
	Stats.HealthChecksPassed.store(0);
	Stats.HealthChecksFailed.store(0);
	Stats.ConnectionsMarkedInvalid.store(0);
	Stats.ExpiredConnectionsRemoved.store(0);
	Stats.SSLHandshakeSuccesses.store(0);
	Stats.SSLHandshakeFailures.store(0);
	Stats.SSLValidationFailures.store(0);
	Stats.ConnectionTimeouts.store(0);
	Stats.RequestTimeouts.store(0);
	Stats.TotalConnectionTimeMs.store(0);
	Stats.TotalRequestTimeMs.store(0);
	Stats.CurrentActiveConnections.store(0);
	Stats.CurrentAvailableConnections.store(0);

	std::lock_guard<std::mutex> Lock(HostStatsMutex);
	HostStats.Empty();

	UE_LOG(LogDiversionHttp, Log, TEXT("Connection pool metrics reset"));
}

FString FConnectionPoolMetrics::GetHostKey(const FConnectionKey& Key) const
{
	return FString::Printf(TEXT("%s:%s%s"),
		UTF8_TO_TCHAR(Key.Host.c_str()),
		UTF8_TO_TCHAR(Key.Port.c_str()),
		Key.UseSSL ? TEXT(" (SSL)") : TEXT(""));
}
