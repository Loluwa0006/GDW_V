// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Async/Async.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"

DECLARE_DELEGATE(DiversionTimerDelegate);

class FTimedDelegateWrapper
{
public:
	FTimedDelegateWrapper(const DiversionTimerDelegate& InDelegate, float InInterval)
		: Delegate(InDelegate), Interval(InInterval), bTimerActive(false)
	{
	}

	~FTimedDelegateWrapper()
	{
		Stop();
	}

	void TriggerInstantCallAndReset();

	void SkipToNextInterval();

	void Start();

	void Stop();

	bool OnEditorTick(float DeltaTime);

	void ResetInterval();

private:
	DiversionTimerDelegate Delegate;
	float Interval;
	float ElapsedTime = 0.0f;
	bool bTimerActive;
	FTSTicker::FDelegateHandle TickHandle;
};
