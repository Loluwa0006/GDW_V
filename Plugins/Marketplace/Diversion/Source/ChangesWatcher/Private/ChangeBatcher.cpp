// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "ChangeBatcher.h"

#include "Async/Async.h"

FChangeBatcher::FChangeBatcher(double InSilenceSeconds, FOnBatchReady InOnReadyCallback)
	: OnReadyCallback(MoveTemp(InOnReadyCallback)),
	  SilenceSeconds(InSilenceSeconds)
{}

FChangeBatcher::~FChangeBatcher()
{
	Stop();
}

void FChangeBatcher::AddChange(const FString& Change)
{
	{
		FScopeLock Lock(&WriteBufferLock);
		WriteBuffer.Add(Change);
	}
	LastChangeTime = FPlatformTime::Seconds();

	if (!TickerHandle.IsValid())
	{
		// Lazy start of the tikcer operatoin
		AddTicker();
	}
}

void FChangeBatcher::Stop()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	{
		FScopeLock Lock(&WriteBufferLock);
		WriteBuffer.Empty();
		AwaitingAcks.Empty();
	}
}

void FChangeBatcher::ResolveBatch(uint32 BatchId, EBatchDisposition Disposition)
{
	if (BatchId == 0) { return; }

	TSet<FString> StoredItems;
	{
		FScopeLock Lock(&WriteBufferLock);
		if (!AwaitingAcks.RemoveAndCopyValue(BatchId, StoredItems))
		{
			return; // Already handled or invalid
		}
	}

	// Add the items back to the queue in case of snoozing the action
	if (Disposition == EBatchDisposition::Snooze)
	{
		RequeueItems(MoveTemp(StoredItems));
	}
}

void FChangeBatcher::AddTicker()
{
	TWeakPtr<FChangeBatcher> WeakPtr = AsShared();
	auto TickerDelegate = FTickerDelegate::CreateRaw(this, &FChangeBatcher::Tick);

	AsyncTask(ENamedThreads::GameThread, [WeakPtr, TickerDelegate]() {
		if (TSharedPtr<FChangeBatcher> Pinned = WeakPtr.Pin())
		{
			if (!Pinned->TickerHandle.IsValid())
			{
				Pinned->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
					TickerDelegate, 0.1f
				);
			}
		}
	});
}

bool FChangeBatcher::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastChangeTime < SilenceSeconds)
	{
		return true;
	}

	DeliverBatch();

	// Wait for the next change to tick again
	TickerHandle.Reset();
	return false;
}

void FChangeBatcher::DeliverBatch()
{
	TSet<FString> LocalCopy;
	uint32 BatchId;

	{
		FScopeLock Lock(&WriteBufferLock);
		if (WriteBuffer.Num() == 0) { return; }

		LocalCopy = MoveTemp(WriteBuffer);
		WriteBuffer.Reset();

		BatchId = NextBatchId++;
		TSet<FString>& Slot = AwaitingAcks.Add(BatchId);
		Slot = LocalCopy; // Used in case of snoozing the current batch
	}

	TWeakPtr<FChangeBatcher> WeakPtr = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakPtr, Id=BatchId, Items = MoveTemp(LocalCopy)]() mutable {
		if (TSharedPtr<FChangeBatcher> Pinned = WeakPtr.Pin())
		{
			Pinned->OnReadyCallback.ExecuteIfBound(Id, Items);
		}
	});
}

void FChangeBatcher::RequeueItems(TSet<FString>&& Items)
{
	{
		FScopeLock Lock(&WriteBufferLock);
		WriteBuffer.Append(Items);
	}
	LastChangeTime = FPlatformTime::Seconds();

	// A new ticker may already be live: AddChange lazy-starts one whenever a
	// fresh directory-watcher event lands, and that can happen between batch
	// delivery and the user's snooze click. Mirror AddChange's lazy guard --
	// any in-flight ticker will pick up the requeued items on its next tick.
	if (!TickerHandle.IsValid())
	{
		AddTicker();
	}
}








