// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "DiversionUtils.h"
#include "ISourceControlModule.h"

DECLARE_DELEGATE(FOnCacheUpdate);


// A class that represents a value that will be cached and updated when needed
template <typename T>
class TCached final
{
public:
	explicit TCached(T DefaultValue, const FTimespan& InCheckInterval = FTimespan::FromSeconds(30))
	:  CheckInterval(InCheckInterval), Value(DefaultValue){}

	const T* GetValid()
	{
		if(LastCheckTime + CheckInterval < FDateTime::Now())
		{
			return nullptr;
		}
		return &Value;
	}

	const T& GetUpdate(T NilValue, FOnCacheUpdate InOnUpdate = FOnCacheUpdate(), bool InForceUpdate = false)
	{
		const T* Temp = InForceUpdate ? nullptr : GetValid();
		if(Temp == nullptr)
		{
			// Mark that we queried
			LastCheckTime = FDateTime::Now();
			InOnUpdate.ExecuteIfBound();
			Temp = GetValid();
			if (Temp == nullptr)
			{
				// "Reset" the contents to some empty symbolizing value
				Value = NilValue;
			}
		}
		return Value;
	}

	const T& Get() const
	{
		return Value;
	}

	void Set(const T& InValue)
	{
		Value = InValue;
		LastCheckTime = FDateTime::Now();
	}

	void ResetTimer()
	{
		// Force a refresh
		LastCheckTime = FDateTime::MinValue();
	}

private:
	FDateTime LastCheckTime;
	FTimespan CheckInterval;
	T Value;
};
