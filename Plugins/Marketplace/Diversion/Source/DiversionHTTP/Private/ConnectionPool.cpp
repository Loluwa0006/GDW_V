// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#include "ConnectionPool.h"
#include "PooledConnection.h"
#include "HttpSession.h"
#include "SslSession.h"
#include "TcpSession.h"
#include "ConnectionPoolMetrics.h"
#include "DiversionHttpConfig.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

#include <algorithm>

FConnectionPool::FConnectionPool(const FConnectionPoolConfig& InConfig,
							   std::shared_ptr<net::ssl::context> InSslContext,
							   net::io_context& InIoContext)
	: Config(InConfig)
	, SslContext(InSslContext)
	, IoContext(InIoContext)
	, ShouldStop(false)
, Metrics(std::make_unique<DiversionHttp::FConnectionPoolMetrics>())
{
	// Start background cleanup thread
	CleanupThread = std::thread([this] { RunCleanupLoop(); });

	UE_LOG(LogDiversionHttp, Log, TEXT("Connection pool initialized with max %d total connections, %d per host"),
		   Config.MaxTotalConnections, Config.MaxConnectionsPerHost);
}

FConnectionPool::~FConnectionPool()
{
	Shutdown();
}

void FConnectionPool::Shutdown()
{
	if (!ShouldStop.load())
	{
		ShouldStop.store(true);

		// Wake up the cleanup thread immediately
		ShutdownCV.notify_one();

		if (CleanupThread.joinable())
		{
			CleanupThread.join();
		}

		// Save metrics to logs directory
		SaveMetricsToFile();

		// Invalidate non-InUse connections and wait for InUse ones to be released
		const auto ShutdownStart = std::chrono::steady_clock::now();
		const auto ShutdownTimeout = std::chrono::seconds(5);

		while (std::chrono::steady_clock::now() - ShutdownStart < ShutdownTimeout)
		{
			bool HasInUseConnections = false;
			std::vector<std::shared_ptr<FPooledConnection>> ConnectionsToInvalidate;
			{
				std::unique_lock<std::shared_mutex> WriteLock(PoolMutex);
				for (auto& [Key, Connections] : ConnectionsByHost)
				{
					for (auto& Connection : Connections)
					{
						const auto State = Connection->GetState();
						if (State == EConnectionState::InUse)
						{
							HasInUseConnections = true;
						}
						else if (State != EConnectionState::Invalid)
						{
							ConnectionsToInvalidate.push_back(Connection);
						}
					}
				}
			}

			// Invalidate outside the lock to avoid blocking other threads during socket/SSL shutdown
			for (auto& Connection : ConnectionsToInvalidate)
			{
				Connection->SetState(EConnectionState::Invalid);
			}

			if (!HasInUseConnections)
			{
				break;
			}

			// Wait briefly for in-use connections to be released
			std::unique_lock<std::mutex> Lock(ShutdownMutex);
			ShutdownCV.wait_for(Lock, std::chrono::milliseconds(50));
		}

		// Final cleanup - force invalidate any remaining connections.
		// Note: force-invalidating in-use connections may interrupt in-flight I/O, but is
		// memory-safe because request threads hold their own shared_ptr to the session objects,
		// preventing use-after-free. The 5s timeout is a tradeoff between giving requests time
		// to complete and not stalling editor shutdown.
		{
			std::vector<std::shared_ptr<FPooledConnection>> RemainingConnections;
			{
				std::unique_lock<std::shared_mutex> WriteLock(PoolMutex);
				for (auto& [Key, Connections] : ConnectionsByHost)
				{
					for (auto& Connection : Connections)
					{
						if (Connection->GetState() == EConnectionState::InUse)
						{
							UE_LOG(LogDiversionHttp, Warning, TEXT("Force-invalidating in-use connection %p for %s:%s after shutdown timeout"),
								   Connection.get(), UTF8_TO_TCHAR(Connection->GetKey().Host.c_str()),
								   UTF8_TO_TCHAR(Connection->GetKey().Port.c_str()));
						}
						RemainingConnections.push_back(Connection);
					}
					Connections.clear();
				}
				ConnectionsByHost.clear();
				TotalConnections.store(0);
			}

			// Invalidate outside the lock
			for (auto& Connection : RemainingConnections)
			{
				Connection->SetState(EConnectionState::Invalid);
			}
		}

		UE_LOG(LogDiversionHttp, Log, TEXT("Connection pool shutdown complete"));
	}
}

std::shared_ptr<FPooledConnection> FConnectionPool::AcquireConnection(const FConnectionKey& Key,
															   std::chrono::seconds MaxWaitTime)
{
	const auto ConnectionRequestStart = std::chrono::steady_clock::now();
	Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionRequested);

	// Security check - validate host if whitelist is enabled
	if (Config.RequireHostWhitelist)
	{
		if (Config.AllowedHosts.find(Key.Host) == Config.AllowedHosts.end())
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("Host %s not in allowed hosts whitelist"), UTF8_TO_TCHAR(Key.Host.c_str()));
			return nullptr;
		}
	}

	if (ShouldStop.load())
	{
		UE_LOG(LogDiversionHttp, Verbose, TEXT("Connection pool is shutting down, rejecting connection request for %s:%s"),
			   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
		return nullptr;
	}

	const auto StartTime = std::chrono::steady_clock::now();
	uint32 RetryCount = 0;
	const uint32 MaxRetries = 5;

	while (std::chrono::steady_clock::now() - StartTime < MaxWaitTime && RetryCount < MaxRetries)
	{
		if (ShouldStop.load())
		{
			return nullptr;
		}
		// 1. Try to find available connection
		if (auto Connection = TryGetAvailableConnection(Key))
		{
			Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionPoolHit);
			Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionReused);
			UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Reusing existing connection for %s:%s"),
				   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));

			// Record connection acquisition timing
			auto ConnectionRequestEnd = std::chrono::steady_clock::now();
			auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(ConnectionRequestEnd - ConnectionRequestStart);
			Metrics->RecordTiming(DiversionHttp::EMetricEvent::ConnectionRequested, Duration);

			return Connection;
		}

		// 2. Try to create new connection if under limits
		if (TotalConnections.load() < Config.MaxTotalConnections)
		{
			if (auto Connection = TryCreateNewConnection(Key))
			{
				Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionPoolMiss);
				UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Created new connection for %s:%s"),
					   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));

				// Record connection acquisition timing
				auto ConnectionRequestEnd = std::chrono::steady_clock::now();
				auto Duration = std::chrono::duration_cast<std::chrono::milliseconds>(ConnectionRequestEnd - ConnectionRequestStart);
				Metrics->RecordTiming(DiversionHttp::EMetricEvent::ConnectionRequested, Duration);

				return Connection;
			}
		}
		else
		{
			Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionPoolMiss);
		}

		// 3. Wait with exponential backoff
		const auto BackoffMs = std::min(100u << RetryCount, 1000u); // 100ms, 200ms, 400ms, 800ms, 1000ms
		std::this_thread::sleep_for(std::chrono::milliseconds(BackoffMs));

		RetryCount++;
	}

	// Log failure
	UE_LOG(LogDiversionHttp, Warning, TEXT("Connection pool exhausted for %s:%s after %d retries. Total connections: %d"),
		   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()),
		   RetryCount, TotalConnections.load());

	return nullptr;
}

std::shared_ptr<FPooledConnection> FConnectionPool::TryGetAvailableConnection(const FConnectionKey& Key)
{
	if (ShouldStop.load())
	{
		return nullptr;
	}

	std::shared_lock<std::shared_mutex> ReadLock(PoolMutex);

	if (auto it = ConnectionsByHost.find(Key); it != ConnectionsByHost.end())
	{
		for (auto& Connection : it->second)
		{
			if (Connection &&
				Connection->GetState() == EConnectionState::Available &&
				Connection->IsHealthy() &&
				Connection->IsSecure() &&
				Connection->ClaimForUse())
			{
				Connection->MarkUsed();

				// Try to reset for new request, if it fails the connection will be marked invalid
				Connection->ResetForNewRequest();

				// Double-check state after reset attempt
				if (Connection->GetState() != EConnectionState::Invalid)
				{
					UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("ConnectionPool: Successfully reset and returning connection %p"), Connection.get());
					return Connection;
				}
				else
				{
					UE_LOG(LogDiversionHttp, Warning, TEXT("ConnectionPool: Connection %p became invalid during reset - will create new connection"), Connection.get());
					// Connection became invalid during reset, continue loop to try other connections
				}
			}
		}
	}

	return nullptr;
}

std::shared_ptr<FPooledConnection> FConnectionPool::TryCreateNewConnection(const FConnectionKey& Key)
{
	if (ShouldStop.load())
	{
		return nullptr;
	}

	// Check per-host limits
	{
		std::shared_lock<std::shared_mutex> ReadLock(PoolMutex);
		if (auto it = ConnectionsByHost.find(Key); it != ConnectionsByHost.end())
		{
			uint32 HostConnectionCount = it->second.size();
			if (HostConnectionCount >= Config.MaxConnectionsPerHost)
			{
				return nullptr;
			}
		}
	}

	// Create new connection
	auto Connection = std::make_shared<FPooledConnection>(Key, this);

	// Initialize with appropriate context
	net::ssl::context* SslContextPtr = Key.UseSSL ? SslContext.get() : nullptr;
	if (!Connection->Initialize(IoContext, SslContextPtr))
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Failed to initialize connection for %s:%s"),
			   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()));
		return nullptr;
	}

	// Add to pool - only increment counter after successful initialization
	{
		std::unique_lock<std::shared_mutex> WriteLock(PoolMutex);
		ConnectionsByHost[Key].push_back(Connection);
		TotalConnections.fetch_add(1);
	}

	Connection->SetState(EConnectionState::InUse);
	Connection->MarkUsed();

	Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionCreated, &Key);

	// Update connection counts
	Metrics->UpdateConnectionCounts(TotalConnections.load(), GetAvailableConnections(Key));

	return Connection;
}

void FConnectionPool::RemoveConnectionFromPool(const std::shared_ptr<FPooledConnection>& Connection)
{
	std::unique_lock<std::shared_mutex> WriteLock(PoolMutex);
	if (auto it = ConnectionsByHost.find(Connection->GetKey()); it != ConnectionsByHost.end())
	{
		auto& Connections = it->second;
		auto InitialSize = Connections.size();
		Connections.erase(std::remove_if(Connections.begin(), Connections.end(),
			[&Connection](const std::shared_ptr<FPooledConnection>& Conn) {
				return Conn == Connection;
			}), Connections.end());

		int32 RemovedCount = static_cast<int32>(InitialSize - Connections.size());
		if (RemovedCount > 0)
		{
			TotalConnections.fetch_sub(RemovedCount);
		}
	}
}

void FConnectionPool::ReleaseConnection(std::shared_ptr<FPooledConnection> Connection)
{
	if (!Connection)
	{
		return;
	}

	// During shutdown, invalidate immediately and signal Shutdown() to proceed
	if (ShouldStop.load())
	{
		Connection->SetState(EConnectionState::Invalid);
		RemoveConnectionFromPool(Connection);
		ShutdownCV.notify_one();
		return;
	}

	// Check if connection is still valid
	if (Connection->GetState() == EConnectionState::Invalid ||
		!Connection->IsHealthy())
	{
		RemoveConnectionFromPool(Connection);

		Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionDestroyed);
		Metrics->RecordEvent(DiversionHttp::EMetricEvent::ConnectionMarkedInvalid);

		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Removed invalid connection for %s:%s"),
			   UTF8_TO_TCHAR(Connection->GetKey().Host.c_str()),
			   UTF8_TO_TCHAR(Connection->GetKey().Port.c_str()));
	}
	else
	{
		// Return to available pool
		Connection->SetState(EConnectionState::Available);

		// Update connection counts
		Metrics->UpdateConnectionCounts(TotalConnections.load(), GetAvailableConnections(Connection->GetKey()));

		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Returned connection to pool for %s:%s"),
			   UTF8_TO_TCHAR(Connection->GetKey().Host.c_str()),
			   UTF8_TO_TCHAR(Connection->GetKey().Port.c_str()));
	}
}

void FConnectionPool::CleanupExpiredConnections()
{
	std::unique_lock<std::shared_mutex> WriteLock(PoolMutex);

	uint32 RemovedConnections = 0;
	const auto Now = std::chrono::steady_clock::now();

	for (auto& [Key, Connections] : ConnectionsByHost)
	{
		auto initialSize = Connections.size();
		Connections.erase(std::remove_if(Connections.begin(), Connections.end(),
			[&](const std::shared_ptr<FPooledConnection>& Connection) {
				bool ShouldRemove = false;

				// Remove if invalid
				if (Connection->GetState() == EConnectionState::Invalid)
				{
					ShouldRemove = true;
				}
				// Remove if expired
				else if (Connection->IsExpired(Config.KeepAliveTimeout))
				{
					ShouldRemove = true;
				}
				// Remove if too old (security)
				else if (Connection->IsExpired(std::chrono::seconds(Config.MaxConnectionAge)))
				{
					ShouldRemove = true;
				}
				// Remove if too many requests (prevent memory leaks)
				else if (Connection->GetRequestCount() >= Config.MaxRequestsPerConnection)
				{
					ShouldRemove = true;
				}
				// Remove if not healthy
				else if (!Connection->IsHealthy())
				{
					ShouldRemove = true;
				}

				if (ShouldRemove)
				{
					RemovedConnections++;
					Connection->SetState(EConnectionState::Invalid);
					Metrics->RecordEvent(DiversionHttp::EMetricEvent::ExpiredConnectionRemoved);
				}

				return ShouldRemove;
			}), Connections.end());
	}

	TotalConnections.fetch_sub(RemovedConnections);

	if (RemovedConnections > 0)
	{
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Cleanup removed %d expired connections. Total remaining: %d"),
			   RemovedConnections, TotalConnections.load());
	}
}

uint32 FConnectionPool::GetAvailableConnections(const FConnectionKey& Key) const
{
	std::shared_lock<std::shared_mutex> ReadLock(PoolMutex);

	if (auto it = ConnectionsByHost.find(Key); it != ConnectionsByHost.end())
	{
		const auto& Connections = it->second;
		return std::count_if(Connections.begin(), Connections.end(),
			[](const std::shared_ptr<FPooledConnection>& Connection) {
				return Connection->GetState() == EConnectionState::Available &&
					   Connection->IsHealthy() &&
					   Connection->IsSecure();
			});
	}

	return 0;
}

void FConnectionPool::RunCleanupLoop()
{
	UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Connection pool cleanup thread started"));

	while (!ShouldStop.load())
	{
		try
		{
			CleanupExpiredConnections();

			// Wait for cleanup interval or shutdown signal
			std::unique_lock<std::mutex> Lock(ShutdownMutex);
			ShutdownCV.wait_for(Lock, Config.IdleCleanupInterval, [this]() {
				return ShouldStop.load();
			});
		}
		catch (const std::exception& Ex)
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("Error in connection pool cleanup: %s"), UTF8_TO_TCHAR(Ex.what()));
		}
		catch (...)
		{
			UE_LOG(LogDiversionHttp, Error, TEXT("Unknown error in connection pool cleanup"));
		}
	}

	UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Connection pool cleanup thread stopped"));
}

void FConnectionPool::SaveMetricsToFile()
{
	// Only save if metrics are enabled
	if (!IsDiversionConnectionPoolMetricsEnabled())
	{
		return;
	}

	// Generate timestamp for unique filename
	FDateTime Now = FDateTime::Now();
	FString Timestamp = Now.ToString(TEXT("%Y%m%d_%H%M%S"));

	// Create host identifier for filename from first host in the pool
	FString HostIdentifier = TEXT("Unknown");
	{
		std::shared_lock<std::shared_mutex> ReadLock(PoolMutex);
		if (!ConnectionsByHost.empty())
		{
			auto it = ConnectionsByHost.begin();
			const FConnectionKey& FirstKey = it->first;
			HostIdentifier = FString::Printf(TEXT("%s_%s"),
				UTF8_TO_TCHAR(FirstKey.Host.c_str()),
				UTF8_TO_TCHAR(FirstKey.Port.c_str()));
			// Replace dots and colons with underscores for filename safety
			HostIdentifier = HostIdentifier.Replace(TEXT("."), TEXT("_"));
			HostIdentifier = HostIdentifier.Replace(TEXT(":"), TEXT("_"));
		}
	}

	// Create metrics filename with host identifier
	FString LogsDir = FPaths::ProjectLogDir();
	FString FileName = FString::Printf(TEXT("ConnectionPoolMetrics_%s_%s.log"), *HostIdentifier, *Timestamp);
	FString FilePath = FPaths::Combine(LogsDir, FileName);
	FString AbsoluteFilePath = FPaths::ConvertRelativePathToFull(FilePath);

	// Get formatted metrics
	FString MetricsContent = TEXT("=== Diversion HTTP Connection Pool Metrics ===\n");
	MetricsContent += FString::Printf(TEXT("Generated: %s\n\n"), *Now.ToString());
	MetricsContent += Metrics->GetFormattedStats();
	MetricsContent += TEXT("\n");
	MetricsContent += Metrics->GetFormattedHostStats();

	// Write to file
	if (FFileHelper::SaveStringToFile(MetricsContent, *FilePath))
	{
		UE_LOG(LogDiversionHttp, Log, TEXT("Connection pool metrics saved to: %s"), *AbsoluteFilePath);
	}
	else
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Failed to save connection pool metrics to: %s"), *AbsoluteFilePath);
	}
}

void FConnectionPool::UpdateConfiguration(const FConnectionPoolConfig& NewConfig)
{
	std::unique_lock<std::shared_mutex> Lock(PoolMutex);

	UE_LOG(LogDiversionHttp, Log, TEXT("Updating connection pool configuration"));

	// Store old config for comparison
	auto OldConfig = Config;
	Config = NewConfig;

	// Apply changes that require immediate action

	// 1. Update connection limits - may require cleanup if limits decreased
	if (NewConfig.MaxTotalConnections < OldConfig.MaxTotalConnections ||
		NewConfig.MaxConnectionsPerHost < OldConfig.MaxConnectionsPerHost)
	{
		UE_LOG(LogDiversionHttp, Log, TEXT("Connection limits reduced - cleaning up excess connections"));
		CleanupExcessConnections();
	}

	// 2. Update existing connections with new timeout values
	for (auto& [Key, Connections] : ConnectionsByHost)
	{
		for (auto& Connection : Connections)
		{
			if (Connection)
			{
				auto NewConnectionTimeout = Config.GetConnectionTimeout(Key.Host);
				auto NewRequestTimeout = Config.GetRequestTimeout(Key.Host);
				Connection->SetTimeouts(NewConnectionTimeout, NewRequestTimeout);
			}
		}
	}

	// 3. Update metrics configuration
	if (Metrics)
	{
		// Metrics object will pick up new config on next update cycle
		UE_LOG(LogDiversionHttp, VeryVerbose, TEXT("Metrics will use new configuration on next update"));
	}

	UE_LOG(LogDiversionHttp, Log,
		   TEXT("Configuration updated: MaxPerHost=%u->%u, MaxTotal=%u->%u"),
		   OldConfig.MaxConnectionsPerHost, NewConfig.MaxConnectionsPerHost,
		   OldConfig.MaxTotalConnections, NewConfig.MaxTotalConnections);
}

void FConnectionPool::UpdateTimeouts(const std::unordered_map<std::string, std::chrono::seconds>& ConnectionTimeouts,
								   const std::unordered_map<std::string, std::chrono::seconds>& RequestTimeouts)
{
	std::unique_lock<std::shared_mutex> Lock(PoolMutex);

	UE_LOG(LogDiversionHttp, Log, TEXT("Updating connection pool timeouts for %d connection hosts, %d request hosts"),
		   static_cast<int>(ConnectionTimeouts.size()), static_cast<int>(RequestTimeouts.size()));

	// Update configuration
	Config.HostConnectionTimeouts = ConnectionTimeouts;
	Config.HostRequestTimeouts = RequestTimeouts;

	// Apply to existing connections
	for (auto& [Key, Connections] : ConnectionsByHost)
	{
		auto NewConnectionTimeout = Config.GetConnectionTimeout(Key.Host);
		auto NewRequestTimeout = Config.GetRequestTimeout(Key.Host);

		for (auto& Connection : Connections)
		{
			if (Connection)
			{
				Connection->SetTimeouts(NewConnectionTimeout, NewRequestTimeout);
			}
		}

		UE_LOG(LogDiversionHttp, VeryVerbose,
			   TEXT("Updated timeouts for host %s: Connection=%llds, Request=%llds"),
			   UTF8_TO_TCHAR(Key.Host.c_str()), NewConnectionTimeout.count(), NewRequestTimeout.count());
	}
}

void FConnectionPool::UpdateConnectionLimits(uint32 MaxPerHost, uint32 MaxTotal)
{
	std::unique_lock<std::shared_mutex> Lock(PoolMutex);

	UE_LOG(LogDiversionHttp, Log,
		   TEXT("Updating connection limits: MaxPerHost=%u->%u, MaxTotal=%u->%u"),
		   Config.MaxConnectionsPerHost, MaxPerHost,
		   Config.MaxTotalConnections, MaxTotal);

	Config.MaxConnectionsPerHost = MaxPerHost;
	Config.MaxTotalConnections = MaxTotal;

	// If limits were reduced, cleanup excess connections
	if (TotalConnections.load() > MaxTotal)
	{
		CleanupExcessConnections();
	}
}

void FConnectionPool::UpdateHealthCheckConfig(const FConnectionKey& Key, const FHealthCheckConfig& HealthConfig)
{
	std::unique_lock<std::shared_mutex> Lock(PoolMutex);

	UE_LOG(LogDiversionHttp, VeryVerbose,
		   TEXT("Updating health check config for %s:%s (SSL:%s)"),
		   UTF8_TO_TCHAR(Key.Host.c_str()), UTF8_TO_TCHAR(Key.Port.c_str()),
		   Key.UseSSL ? TEXT("true") : TEXT("false"));

	Config.HostHealthChecks[Key] = HealthConfig;

	// Health check changes don't require immediate action on existing connections
	// They'll be applied on next health check cycle
}

void FConnectionPool::CleanupExcessConnections()
{
	// This method assumes PoolMutex is already locked

	uint32 CurrentTotal = TotalConnections.load();
	if (CurrentTotal <= Config.MaxTotalConnections)
	{
		return; // No cleanup needed
	}

	uint32 ConnectionsToRemove = CurrentTotal - Config.MaxTotalConnections;
	uint32 RemovedCount = 0;

	UE_LOG(LogDiversionHttp, Log,
		   TEXT("Cleaning up %u excess connections (current: %u, limit: %u)"),
		   ConnectionsToRemove, CurrentTotal, Config.MaxTotalConnections);

	// Strategy: Remove oldest idle connections first
	for (auto& [Key, ConnectionList] : ConnectionsByHost)
	{
		if (RemovedCount >= ConnectionsToRemove)
		{
			break;
		}

		uint32 HostLimit = Config.MaxConnectionsPerHost;

		// Sort by last used time (oldest first) and remove excess
		std::sort(ConnectionList.begin(), ConnectionList.end(),
			[](const std::shared_ptr<FPooledConnection>& A, const std::shared_ptr<FPooledConnection>& B) {
				if (!A)
				{
					return true;
				}
				if (!B)
				{
					return false;
				}
				return A->GetLastUsed() < B->GetLastUsed();
			});

		// Remove connections beyond host limit or that are idle/available
		for (int32 i = static_cast<int32>(ConnectionList.size()) - 1; i >= 0 && RemovedCount < ConnectionsToRemove; --i)
		{
			auto Connection = ConnectionList[i];
			if (!Connection ||
				Connection->GetState() == EConnectionState::Available ||
				static_cast<uint32>(ConnectionList.size()) > HostLimit)
			{
				UE_LOG(LogDiversionHttp, VeryVerbose,
					   TEXT("Removing excess connection for %s:%s"),
					   UTF8_TO_TCHAR(Key.Host.c_str()),
					   UTF8_TO_TCHAR(Key.Port.c_str()));

				ConnectionList.erase(ConnectionList.begin() + i);
				TotalConnections.fetch_sub(1);
				RemovedCount++;
			}
		}
	}

	UE_LOG(LogDiversionHttp, Log,
		   TEXT("Removed %u excess connections, new total: %u"),
		   RemovedCount, TotalConnections.load());
}

