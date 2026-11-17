// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Tickable.h"

enum class EBatchDisposition {
	Accept,
	Snooze
};


class FChangeBatcher : public TSharedFromThis<FChangeBatcher>
{
public:
	DECLARE_DELEGATE_TwoParams(FOnBatchReady, uint32, const TSet<FString>&);

	FChangeBatcher(double InSilenceSeconds, FOnBatchReady InOnReadyCallback);
	~FChangeBatcher();

	void AddChange(const FString& Change);

	void Stop();

	void ResolveBatch(uint32 BatchId, EBatchDisposition Disposition);

private:

	void AddTicker();
	bool Tick(float DeltaTime);
	void DeliverBatch();
	void RequeueItems(TSet<FString>&& Items);


private:
	FOnBatchReady OnReadyCallback;
	double SilenceSeconds;
	double LastChangeTime;

	FTSTicker::FDelegateHandle TickerHandle;

	// Thread safe write buffer
	TSet<FString> WriteBuffer;
	mutable FCriticalSection WriteBufferLock;

	TMap<uint32, TSet<FString>> AwaitingAcks;
	uint32 NextBatchId = 0;
};
