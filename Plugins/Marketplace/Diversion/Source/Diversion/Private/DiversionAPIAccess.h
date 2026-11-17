// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include <atomic>

// Forward declarations for API managers
namespace Diversion
{
	namespace AgentAPI { class DefaultApi; }
	namespace CoreAPI
	{
		class SupportApi;
		class AnalyticsApi;
		class RepositoryManagementApi;
		class RepositoryManipulationApi;
		class RepositoryMergeManipulationApi;
		class RepositoryCommitManipulationApi;
		class RepositoryWorkspaceManipulationApi;
		class RepositoryLockManagementApi;
		class OrganizationManagementApi;
	}
}

namespace DiversionHttp
{
	class FHttpRequestManager;
}

class FCredentialsManager;

/**
 * Thread-safe static access to Diversion API managers and credentials.
 * Owns and initializes API managers during module startup.
 * Safe to read from worker threads after initialization is complete.
 *
 * The API manager objects are owned by this class and created during Initialize().
 * Memory barriers ensure writes are visible to worker threads.
 */
class FDiversionAPIAccess
{
public:
	// Static pointers set during module startup, safe to read from any thread after initialization
	static Diversion::AgentAPI::DefaultApi* AgentAPI;
	static Diversion::CoreAPI::SupportApi* SupportAPI;
	static Diversion::CoreAPI::AnalyticsApi* AnalyticsAPI;
	static Diversion::CoreAPI::RepositoryManagementApi* RepositoryManagementAPI;
	static Diversion::CoreAPI::RepositoryManipulationApi* RepositoryManipulationAPI;
	static Diversion::CoreAPI::RepositoryMergeManipulationApi* RepositoryMergeManipulationAPI;
	static Diversion::CoreAPI::RepositoryCommitManipulationApi* RepositoryCommitManipulationAPI;
	static Diversion::CoreAPI::RepositoryWorkspaceManipulationApi* RepositoryWorkspaceManipulationAPI;
	static Diversion::CoreAPI::RepositoryLockManagementApi* RepositoryLockManagementAPI;
	static Diversion::CoreAPI::OrganizationManagementApi* OrganizationManagementAPI;

	/**
	 * Initialize API managers and credentials manager.
	 * Creates all API manager instances and sets up HTTP clients.
	 * Must be called from game thread during FDiversionModule::StartupModule()
	 * @param OriginalAccountID The account ID for logging and validity checks
	 */
	static void Initialize(const FString& OriginalAccountID);

	/**
	 * Shutdown API access - clears pointers and destroys API managers.
	 * Must be called from game thread during FDiversionModule::ShutdownModule()
	 * Must be called AFTER provider shutdown to ensure all workers have completed.
	 */
	static void Shutdown();

	/**
	 * Signal that shutdown has begun so in-flight workers can drain quickly: HTTP requests issued
	 * after this fail fast instead of being sent. Call before Shutdown(). Thread-safe; idempotent.
	 */
	static void BeginShutdown();

	/**
	 * Check if the API access layer has been initialized.
	 * Thread-safe, can be called from any thread.
	 * @return true if initialized and ready for use
	 */
	static bool IsInitialized();

	/**
	 * Register the calling worker thread as in-flight before it touches any API object.
	 * Must be paired with a matching ExitWorker() call (only when this returns true).
	 * @return true if the worker may proceed; false if shutdown has begun, in which case the
	 *         caller must NOT access any API object and must NOT call ExitWorker().
	 * Thread-safe.
	 */
	static bool TryEnterWorker();

	/**
	 * Mark an in-flight worker (previously admitted by TryEnterWorker) as finished.
	 * Releases the shutdown barrier's hold on the API objects. Thread-safe.
	 */
	static void ExitWorker();

	/**
	 * Get access token for a user. Thread-safe, can be called from worker threads.
	 * @param InUserID The user ID to get the token for
	 * @return The access token string
	 */
	static FString GetAccessToken(const FString& InUserID);

	/**
	 * Get the CoreAPI HTTP client for direct file downloads.
	 * Thread-safe after initialization.
	 * @return Shared pointer to the CoreAPI HTTP client
	 */
	static TSharedPtr<DiversionHttp::FHttpRequestManager> GetCoreAPIClient();

private:
	// HTTP clients (owned by this class)
	static TSharedPtr<DiversionHttp::FHttpRequestManager> AgentAPIClient;
	static TSharedPtr<DiversionHttp::FHttpRequestManager> CoreAPIClient;

	// API managers (owned by this class)
	static TUniquePtr<Diversion::AgentAPI::DefaultApi> AgentAPIManager;
	static TUniquePtr<Diversion::CoreAPI::SupportApi> SupportAPIManager;
	static TUniquePtr<Diversion::CoreAPI::AnalyticsApi> AnalyticsAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryManagementApi> RepositoryManagementAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryManipulationApi> RepositoryManipulationAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryMergeManipulationApi> RepositoryMergeManipulationAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryCommitManipulationApi> RepositoryCommitManipulationAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryWorkspaceManipulationApi> RepositoryWorkspaceManipulationAPIManager;
	static TUniquePtr<Diversion::CoreAPI::RepositoryLockManagementApi> RepositoryLockManagementAPIManager;
	static TUniquePtr<Diversion::CoreAPI::OrganizationManagementApi> OrganizationManagementAPIManager;

	// Credentials manager (thread-safe with internal locking)
	static TUniquePtr<FCredentialsManager> CredentialsManager;

	// Initialization flag: written on the game thread, read from worker threads. Atomic (not plain
	// volatile) so the concurrent access is well-defined rather than a data race; the explicit
	// MemoryBarrier()s around it still supply the StoreLoad ordering the worker/shutdown handshake
	// needs (acquire/release alone would not).
	static std::atomic<bool> bIsInitialized;

	// Count of worker threads currently executing with live API objects. Shutdown() clears
	// bIsInitialized and then waits for this to reach zero before destroying the API objects,
	// preventing a use-after-free when a background worker is mid-call during teardown.
	static FThreadSafeCounter InFlightWorkers;

	// Original account ID for logging
	static FString CachedOriginalAccountID;
};
