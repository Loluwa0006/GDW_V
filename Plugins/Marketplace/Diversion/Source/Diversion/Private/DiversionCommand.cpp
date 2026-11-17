// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionCommand.h"

#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "SourceControlHelpers.h"
#include "Misc/ScopeExit.h"

FDiversionCommand::FDiversionCommand(const TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>& InOperation, const TSharedRef<class IDiversionWorker, ESPMode::ThreadSafe>& InWorker, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
	: Operation(InOperation)
	, Worker(InWorker)
	, OperationCompleteDelegate(InOperationCompleteDelegate)
	, bExecuteProcessed(0)
	, bCommandSuccessful(false)
	, bAutoDelete(true)
	, Concurrency(InConcurrency)
{
	// grab the providers settings here, so we don't access them once the worker thread is
	// launched. GetProvider() below enforces the game thread, so this needs no check of its own.
	FDiversionModule& Diversion = FDiversionModule::Get();

	auto& Provider = Diversion.GetProvider();
	// Get the latest values - revalidate the cache
	WsInfo = Provider.GetWsInfo();
	IsAgentAlive = Provider.IsAgentAlive();
	Provider.GetConflictedFilesPaths(ConflictedFiles);
	Provider.GetCurrentPotentialClashes(ExistingPotentialConflicts);
	SyncStatus = Provider.GetSyncStatus();
	FilesPendingDelete = Provider.GetFilesPendingDeleteSnapshot();
}

void FDiversionCommand::DoWork()
{
	Worker->Execute(*this);
}

void FDiversionCommand::Abandon()
{
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}

void FDiversionCommand::DoThreadedWork()
{
	// Register as an in-flight worker and ensure the API access layer is live before doing work.
	// This holds off FDiversionAPIAccess::Shutdown() from destroying the API objects until DoWork()
	// returns, preventing a use-after-free if the editor begins shutting down mid-command.
	if (!FDiversionAPIAccess::TryEnterWorker())
	{
		// Not an error during shutdown: TryEnterWorker() also returns false once shutdown has begun.
		UE_LOG(LogSourceControl, Log, TEXT("Diversion worker skipped: API access unavailable (not initialized or shutting down)"));
		MarkOperationCompleted(false);
		return;
	}
	ON_SCOPE_EXIT { FDiversionAPIAccess::ExitWorker(); };

	DoWork();
}

ECommandResult::Type FDiversionCommand::ReturnResults()
{
	// Save any messages that have accumulated
	for (FString& String : InfoMessages)
	{
		Operation->AddInfoMessge(FText::FromString(String));
	}
	for (FString& String : ErrorMessages)
	{
		Operation->AddErrorMessge(FText::FromString(String));
	}

	// run the completion delegate if we have one bound
	ECommandResult::Type Result = bCommandSuccessful ? ECommandResult::Succeeded : ECommandResult::Failed;
	OperationCompleteDelegate.ExecuteIfBound(Operation, Result);

	return Result;
}

void FDiversionCommand::MarkOperationCompleted(bool InCommandSuccessful)
{
	bCommandSuccessful = InCommandSuccessful;
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}
