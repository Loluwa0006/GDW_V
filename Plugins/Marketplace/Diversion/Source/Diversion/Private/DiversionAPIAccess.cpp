// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionAPIAccess.h"
#include "DiversionConstants.h"
#include "DiversionUtils.h"
#include "DiversionAgentAddress.h"
#include "DiversionCredentialsManager.h"
#include "DiversionHttpManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "ISourceControlModule.h"

// API headers
#include "DefaultApi.h"
#include "SupportApi.h"
#include "AnalyticsApi.h"
#include "RepositoryManipulationApi.h"
#include "RepositoryMergeManipulationApi.h"
#include "RepositoryCommitManipulationApi.h"
#include "RepositoryWorkspaceManipulationApi.h"
#include "RepositoryManagementApi.h"
#include "RepositoryLockManagementApi.h"
#include "OrganizationManagementApi.h"

// Define static members - Public pointers
Diversion::AgentAPI::DefaultApi* FDiversionAPIAccess::AgentAPI = nullptr;
Diversion::CoreAPI::SupportApi* FDiversionAPIAccess::SupportAPI = nullptr;
Diversion::CoreAPI::AnalyticsApi* FDiversionAPIAccess::AnalyticsAPI = nullptr;
Diversion::CoreAPI::RepositoryManagementApi* FDiversionAPIAccess::RepositoryManagementAPI = nullptr;
Diversion::CoreAPI::RepositoryManipulationApi* FDiversionAPIAccess::RepositoryManipulationAPI = nullptr;
Diversion::CoreAPI::RepositoryMergeManipulationApi* FDiversionAPIAccess::RepositoryMergeManipulationAPI = nullptr;
Diversion::CoreAPI::RepositoryCommitManipulationApi* FDiversionAPIAccess::RepositoryCommitManipulationAPI = nullptr;
Diversion::CoreAPI::RepositoryWorkspaceManipulationApi* FDiversionAPIAccess::RepositoryWorkspaceManipulationAPI = nullptr;
Diversion::CoreAPI::RepositoryLockManagementApi* FDiversionAPIAccess::RepositoryLockManagementAPI = nullptr;
Diversion::CoreAPI::OrganizationManagementApi* FDiversionAPIAccess::OrganizationManagementAPI = nullptr;

// Define static members - Private ownership
TSharedPtr<DiversionHttp::FHttpRequestManager> FDiversionAPIAccess::AgentAPIClient = nullptr;
TSharedPtr<DiversionHttp::FHttpRequestManager> FDiversionAPIAccess::CoreAPIClient = nullptr;

TUniquePtr<Diversion::AgentAPI::DefaultApi> FDiversionAPIAccess::AgentAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::SupportApi> FDiversionAPIAccess::SupportAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::AnalyticsApi> FDiversionAPIAccess::AnalyticsAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryManagementApi> FDiversionAPIAccess::RepositoryManagementAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryManipulationApi> FDiversionAPIAccess::RepositoryManipulationAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryMergeManipulationApi> FDiversionAPIAccess::RepositoryMergeManipulationAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryCommitManipulationApi> FDiversionAPIAccess::RepositoryCommitManipulationAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryWorkspaceManipulationApi> FDiversionAPIAccess::RepositoryWorkspaceManipulationAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::RepositoryLockManagementApi> FDiversionAPIAccess::RepositoryLockManagementAPIManager = nullptr;
TUniquePtr<Diversion::CoreAPI::OrganizationManagementApi> FDiversionAPIAccess::OrganizationManagementAPIManager = nullptr;

TUniquePtr<FCredentialsManager> FDiversionAPIAccess::CredentialsManager = nullptr;

std::atomic<bool> FDiversionAPIAccess::bIsInitialized{ false };
FThreadSafeCounter FDiversionAPIAccess::InFlightWorkers;
FString FDiversionAPIAccess::CachedOriginalAccountID;

void FDiversionAPIAccess::Initialize(const FString& OriginalAccountID)
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"FDiversionAPIAccess::Initialize must be called on the main game thread", OriginalAccountID))
	{
		return;
	}

	// Cache the account ID for logging
	CachedOriginalAccountID = OriginalAccountID;

	const bool DONT_REQUIRE_EDITOR_FOCUS = false;
	const bool REQUIRE_EDITOR_FOCUS = true;

	// Create the Agent API request manager
	AgentAPIClient = MakeShared<DiversionHttp::FHttpRequestManager>(FDiversionAgentAddress::GetAgentURL(),
		DONT_REQUIRE_EDITOR_FOCUS, DiversionUtils::GetDiversionHeaders());
	AgentAPIClient->SetUrlProvider([]() { return FDiversionAgentAddress::GetAgentURL(); });
	AgentAPIManager = MakeUnique<Diversion::AgentAPI::DefaultApi>(AgentAPIClient);

	// Create the Core API request managers
	CoreAPIClient = MakeShared<DiversionHttp::FHttpRequestManager>(DIVERSION_API_HOST, DIVERSION_API_PORT, REQUIRE_EDITOR_FOCUS,
		DiversionUtils::GetDiversionHeaders());
	SupportAPIManager = MakeUnique<Diversion::CoreAPI::SupportApi>(CoreAPIClient);
	AnalyticsAPIManager = MakeUnique<Diversion::CoreAPI::AnalyticsApi>(CoreAPIClient);
	RepositoryManagementAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryManagementApi>(CoreAPIClient);
	RepositoryManipulationAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryManipulationApi>(CoreAPIClient);
	RepositoryMergeManipulationAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryMergeManipulationApi>(CoreAPIClient);
	RepositoryCommitManipulationAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryCommitManipulationApi>(CoreAPIClient);
	RepositoryWorkspaceManipulationAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryWorkspaceManipulationApi>(CoreAPIClient);
	RepositoryLockManagementAPIManager = MakeUnique<Diversion::CoreAPI::RepositoryLockManagementApi>(CoreAPIClient);
	OrganizationManagementAPIManager = MakeUnique<Diversion::CoreAPI::OrganizationManagementApi>(CoreAPIClient);

	// Set public pointers to the owned managers
	AgentAPI = AgentAPIManager.Get();
	SupportAPI = SupportAPIManager.Get();
	AnalyticsAPI = AnalyticsAPIManager.Get();
	RepositoryManagementAPI = RepositoryManagementAPIManager.Get();
	RepositoryManipulationAPI = RepositoryManipulationAPIManager.Get();
	RepositoryMergeManipulationAPI = RepositoryMergeManipulationAPIManager.Get();
	RepositoryCommitManipulationAPI = RepositoryCommitManipulationAPIManager.Get();
	RepositoryWorkspaceManipulationAPI = RepositoryWorkspaceManipulationAPIManager.Get();
	RepositoryLockManagementAPI = RepositoryLockManagementAPIManager.Get();
	OrganizationManagementAPI = OrganizationManagementAPIManager.Get();

	// Create credentials manager for thread-safe token access
	CredentialsManager = MakeUnique<FCredentialsManager>();

	// Memory barrier to ensure all writes are visible to other threads before setting initialized flag
	FPlatformMisc::MemoryBarrier();

	// Mark as initialized (this write happens after the barrier, so other threads will see all previous writes)
	bIsInitialized = true;
}

void FDiversionAPIAccess::Shutdown()
{
	if (!DiversionUtils::DiversionValidityCheck(IsInGameThread(),
		"FDiversionAPIAccess::Shutdown must be called on the main game thread", CachedOriginalAccountID))
	{
		return;
	}

	// Phase 1: stop admitting new workers. A worker that observes this flag as false in
	// TryEnterWorker() bails before touching any API object.
	bIsInitialized = false;
	FPlatformMisc::MemoryBarrier();

	// Phase 2: wait for already-admitted workers to finish using the API objects before we
	// destroy them. Without this a background worker (e.g. UpdateStatus) can be mid-call inside an
	// API manager when the lines below free it, dereferencing freed memory and crashing on editor
	// shutdown. BeginShutdown() has already signalled the HTTP clients to fail fast, so
	// in-flight workers return promptly and this normally drains near-instantly.
	//
	// The wait is bounded: this runs on the game thread (see the IsInGameThread check above), so an
	// indefinite wait would hang the editor on shutdown if a worker ever failed to drain. The
	// timeout is generous enough to cover a worker blocked on the longest-running backend API call
	// (currently ~120s) with margin. If it still expires we log and proceed with teardown,
	// accepting the small residual use-after-free risk over a permanent editor hang.
	const int32 PendingWorkers = InFlightWorkers.GetValue();
	if (PendingWorkers > 0)
	{
		UE_LOG(LogSourceControl, Log,
			TEXT("FDiversionAPIAccess::Shutdown: waiting for %d in-flight worker(s) to finish before freeing API objects"),
			PendingWorkers);
		const double DrainStartTime = FPlatformTime::Seconds();

		// 3 minutes: covers the longest backend API call (~120s) with margin.
		const float DrainTimeoutSeconds = 180.0f;
		// Poll every 10ms (not the 100ms default) so we yield to draining workers promptly and
		// proceed with teardown as soon as the last one exits, keeping shutdown snappy.
		const float DrainPollIntervalSeconds = 0.01f;
		WaitForConditionPredicate WorkersDrainedDelegate = WaitForConditionPredicate::CreateLambda([]()
		{
			return InFlightWorkers.GetValue() == 0;
		});
		const bool bDrained = DiversionUtils::WaitForCondition(
			WorkersDrainedDelegate, DrainTimeoutSeconds, DrainPollIntervalSeconds);

		if (bDrained)
		{
			UE_LOG(LogSourceControl, Log,
				TEXT("FDiversionAPIAccess::Shutdown: in-flight workers drained in %.2fs; tearing down API objects"),
				FPlatformTime::Seconds() - DrainStartTime);
		}
		else
		{
			UE_LOG(LogSourceControl, Error,
				TEXT("FDiversionAPIAccess::Shutdown: timed out after %.0fs waiting for %d in-flight worker(s) to drain; ")
				TEXT("tearing down API objects anyway"),
				DrainTimeoutSeconds, InFlightWorkers.GetValue());
		}
	}

	// Phase 3: all workers have drained - safe to tear everything down.

	// Clear public pointers
	AgentAPI = nullptr;
	SupportAPI = nullptr;
	AnalyticsAPI = nullptr;
	RepositoryManagementAPI = nullptr;
	RepositoryManipulationAPI = nullptr;
	RepositoryMergeManipulationAPI = nullptr;
	RepositoryCommitManipulationAPI = nullptr;
	RepositoryWorkspaceManipulationAPI = nullptr;
	RepositoryLockManagementAPI = nullptr;
	OrganizationManagementAPI = nullptr;

	// Destroy owned API managers
	AgentAPIManager.Reset();
	SupportAPIManager.Reset();
	AnalyticsAPIManager.Reset();
	RepositoryManagementAPIManager.Reset();
	RepositoryManipulationAPIManager.Reset();
	RepositoryMergeManipulationAPIManager.Reset();
	RepositoryCommitManipulationAPIManager.Reset();
	RepositoryWorkspaceManipulationAPIManager.Reset();
	RepositoryLockManagementAPIManager.Reset();
	OrganizationManagementAPIManager.Reset();

	// Destroy HTTP clients
	AgentAPIClient.Reset();
	CoreAPIClient.Reset();

	// Destroy credentials manager
	CredentialsManager.Reset();

	// Clear cached account ID
	CachedOriginalAccountID.Empty();
}

void FDiversionAPIAccess::BeginShutdown()
{
	if (AgentAPIClient.IsValid())
	{
		AgentAPIClient->BeginShutdown();
	}
	if (CoreAPIClient.IsValid())
	{
		CoreAPIClient->BeginShutdown();
	}
}

bool FDiversionAPIAccess::IsInitialized()
{
	return bIsInitialized;
}

bool FDiversionAPIAccess::TryEnterWorker()
{
	// Register first, then check the flag. Shutdown() clears the flag and then waits for the
	// counter to drain, so this store-before-load ordering (the interlocked Increment and the
	// barrier below both act as full memory barriers) guarantees that any worker admitted here
	// keeps the API objects alive until the matching ExitWorker().
	InFlightWorkers.Increment();
	FPlatformMisc::MemoryBarrier();
	if (!bIsInitialized)
	{
		InFlightWorkers.Decrement();
		return false;
	}
	return true;
}

void FDiversionAPIAccess::ExitWorker()
{
	InFlightWorkers.Decrement();
}

FString FDiversionAPIAccess::GetAccessToken(const FString& InUserID)
{
	if (!CredentialsManager.IsValid())
	{
		UE_LOG(LogSourceControl, Error, TEXT("FDiversionAPIAccess::GetAccessToken called before Initialize()"));
		return "";
	}

	return CredentialsManager->GetUserAccessToken(InUserID);
}

TSharedPtr<DiversionHttp::FHttpRequestManager> FDiversionAPIAccess::GetCoreAPIClient()
{
	return CoreAPIClient;
}
