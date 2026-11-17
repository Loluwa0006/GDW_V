// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

#include <string_view>


namespace DiversionHttp {

	static const FString DIVERSION_HOST_STRING = TEXT("diversion.dev");

	// Custom status code returned when request is skipped due to editor being out of focus
	constexpr int32 HTTP_STATUS_CUSTOM_REQUEST_NOT_SENT = -1;
	// HTTP status codes
	constexpr int32 HTTP_STATUS_NOT_MODIFIED = 304;

	struct DIVERSIONHTTP_API HTTPCallResponse {
		HTTPCallResponse() : Contents(""), Error(TOptional<FString>()), Headers(TMap<FString, FString>()), ResponseCode(0) {}
		HTTPCallResponse(const FString& Contents, int32 ResponseCode, TMap<FString, FString> Headers) : Contents(Contents), Error(TOptional<FString>()), Headers(Headers), ResponseCode(ResponseCode) {}
		HTTPCallResponse(const FString& Error) : Contents(""), Error(Error), Headers(TMap<FString, FString>()), ResponseCode(500) {}

		FString Contents;
		TOptional<FString> Error;
		TMap<FString, FString> Headers;
		int32 ResponseCode;

		/**
		 * Extracts error message from response with smart fallback logic.
		 * Priority order:
		 * 1. Error field if set
		 * 2. "error" field from JSON contents (if contents are valid JSON)
		 * 3. Raw contents as fallback
		 * 4. Empty string if contents are empty
		 *
		 * @return Extracted error message string
		 */
		FString GetErrorMessage() const;
	};

	enum class HttpMethod
	{
		GET,
		POST,
		PUT,
		DEL,
	};


	DIVERSIONHTTP_API FString parameterToString(const FString& value);

	DIVERSIONHTTP_API FString parameterToString(int64 value);

	DIVERSIONHTTP_API FString parameterToString(int32 value);

	DIVERSIONHTTP_API FString parameterToString(float value);

	DIVERSIONHTTP_API FString parameterToString(double value);

	DIVERSIONHTTP_API FString parameterToString(bool value);

	// URL operations

	DIVERSIONHTTP_API FString URLEncode(const FString& input);

	DIVERSIONHTTP_API FString ExtractHostFromUrl(const FString& Url);

	DIVERSIONHTTP_API FString ExtractPortFromUrl(const FString& Url);

	DIVERSIONHTTP_API bool IsEncrypted(const FString& Url);

	DIVERSIONHTTP_API FString GetPathFromUrl(const FString& Url);

	DIVERSIONHTTP_API FString ConvertToFstring(const std::string_view& InValue, std::size_t InSize = -1);

}
