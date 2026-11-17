// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionTimedDelegate.h"

void FTimedDelegateWrapper::TriggerInstantCallAndReset()
{
	if (FApp::IsUnattended())
	{
		// In unattended mode(MRQ, cmdlet runs etc. ), we can't detect the game thread
		// and trigger an async task.
		// So we just call the delegate directly only if we are in the main thread (the usual "correct" scenario)
		// and ignore it otherwise.
		// Non-game thread calls normally shouldn't happen.
		// In case they do it will just impose a minimal UI update delay.
		if(IsInGameThread())
		{
			Delegate.ExecuteIfBound();
			ResetInterval();
		}
		return;
	}

	// Ensure the function is called on the game thread
	AsyncTask(ENamedThreads::GameThread, [this]() {
		// Trigger the function instantly
		Delegate.ExecuteIfBound();

		// Start the interval again
		ResetInterval();
	});
}

void FTimedDelegateWrapper::SkipToNextInterval()
{
	// Skip to the next interval by resetting the internal timer
	ResetInterval();
}

void FTimedDelegateWrapper::Start()
{
	if (!bTimerActive)
	{
		bTimerActive = true;
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FTimedDelegateWrapper::OnEditorTick));
	}
}

void FTimedDelegateWrapper::Stop()
{
	if (bTimerActive)
	{
		bTimerActive = false;
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

bool FTimedDelegateWrapper::OnEditorTick(float DeltaTime)
{
	ElapsedTime += DeltaTime;
	if (ElapsedTime >= Interval)
	{
		// Trigger the function and reset the timer
		Delegate.ExecuteIfBound();
		ElapsedTime = 0.0f;
	}
	return true;
}

void FTimedDelegateWrapper::ResetInterval()
{
	ElapsedTime = 0.0f;
}
