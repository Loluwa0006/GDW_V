// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Types.h"


template<typename ParamType>
struct FApiResponseDelegate {
	using TApiResponseDelegate = TDelegate<bool(const ParamType&, int, TMap<FString, FString>)>;

	template<typename FunctorType, std::enable_if_t<std::is_invocable_r_v<bool, FunctorType>, int> = 0>
	static TApiResponseDelegate Bind(FunctorType&& InFunc)
	{
		return TApiResponseDelegate::CreateLambda(
			// Capture by value. Move if desired.
			[Func = Forward<FunctorType>(InFunc)](ParamType /*Unused*/, int /*Unused*/, TMap<FString, FString> /*Unused*/)
			{
				return Func();
			}
		);
	}

	template<typename FunctorType, std::enable_if_t<std::is_invocable_r_v<bool, FunctorType, ParamType>, int> = 0>
	static TApiResponseDelegate Bind(FunctorType&& InFunc)
	{
		return TApiResponseDelegate::CreateLambda(
			[Func = Forward<FunctorType>(InFunc)](ParamType Param, int /*Unused*/, TMap<FString, FString> /*Unused*/)
			{
				return Func(Param);
			}
		);
	}

	template<typename FunctorType, std::enable_if_t<std::is_invocable_r_v<bool, FunctorType, ParamType, int>, int> = 0>
	static TApiResponseDelegate Bind(FunctorType&& InFunc)
	{
		return TApiResponseDelegate::CreateLambda(
			[Func = Forward<FunctorType>(InFunc)](ParamType Param, int StatusCode, TMap<FString, FString> /*Unused*/)
			{
				return Func(Param, StatusCode);
			}
		);
	}

	template<typename FunctorType, std::enable_if_t<std::is_invocable_r_v<bool, FunctorType, ParamType, int, TMap<FString, FString>>, int> = 0>
	static TApiResponseDelegate Bind(FunctorType&& InFunc)
	{
		return TApiResponseDelegate::CreateLambda(
			[Func = Forward<FunctorType>(InFunc)](ParamType Param, int StatusCode, TMap<FString, FString> Headers)
			{
				return Func(Param, StatusCode, Headers);
			}
		);
	}
};



template<typename T>
struct THTTPResult
{
	using ArgumentDelegate = typename FApiResponseDelegate<T>::TApiResponseDelegate;


	TOptional<T> Value;
	FString Error;
	int StatusCode;
	TMap<FString, FString> Headers;
	bool isSuccess = false;

	// Helper constructors
	static THTTPResult<T> Success(TOptional<T> InValue, int InStatusCode, const TMap<FString, FString>& Headers)
		{ return THTTPResult<T>{MoveTemp(InValue), TEXT(""), InStatusCode, Headers, true}; }
	static THTTPResult<T> Failure(const FString& InError, int InStatusCode, const TMap<FString, FString>& Headers, TOptional<T> InValue = TOptional<T>())
		{ return THTTPResult<T>{MoveTemp(InValue), InError, InStatusCode, Headers, false}; }

	bool IsSuccess() const { return isSuccess; }


	bool HandleApiResponse(const ArgumentDelegate& HandleErrors,
		const ArgumentDelegate& HandleResponse,
		TArray<FString>& OutErrorMessages) {
		if (StatusCode == DiversionHttp::HTTP_STATUS_CUSTOM_REQUEST_NOT_SENT)
		{
			return false;
		}

		// Handle errors
		if (!IsSuccess())
		{
			// Generic error
			if (!Value.IsSet())
			{
				OutErrorMessages.Add(Error);
				return false;
			}

			if (HandleErrors.IsBound())
			{
				return HandleErrors.Execute(Value.GetValue(), StatusCode, Headers);
			}
			return false;
		}

		// Handle empty response
		if (!Value.IsSet())
		{
			OutErrorMessages.Add("Empty response");
			return false;
		}

		if (HandleResponse.IsBound())
		{
			return HandleResponse.Execute(Value.GetValue(), StatusCode, Headers);
		}
		return true;
	}
};
