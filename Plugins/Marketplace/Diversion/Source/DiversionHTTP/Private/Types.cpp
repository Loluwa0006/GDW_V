// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Types.h"
#include "DiversionHttpModule.h"

#include "BoostHeaders.h"
#include <regex>
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// For encoding and decoding
#include <uri-parser/parser.hpp>
// For extracting URI values
#include <simple-uri-parser/uri_parser.h>

FString DiversionHttp::parameterToString(const FString& value)
{
	return value;
}

FString DiversionHttp::parameterToString(int64 value)
{
	return FString::Printf(TEXT("%lld"), value);
}

FString DiversionHttp::parameterToString(int32 value)
{
	return FString::Printf(TEXT("%d"), value);
}

FString DiversionHttp::parameterToString(float value)
{
	return FString::SanitizeFloat(value);
}

FString DiversionHttp::parameterToString(double value)
{
	return FString::SanitizeFloat(value);
}

FString DiversionHttp::parameterToString(bool value)
{
	return value ? TEXT("true") : TEXT("false");
}

FString DiversionHttp::URLEncode(const FString& input)
{
	// Convert UE string to UTF-8
	std::string raw = TCHAR_TO_UTF8(*input);
	std::string enc = parser::encodeUrl(raw);
	return UTF8_TO_TCHAR(enc.c_str());
}


FString DiversionHttp::GetPathFromUrl(const FString& Url)
{
	std::string s = TCHAR_TO_UTF8(*Url);
	auto u = uri::parse_uri(s);
	if (u.error != uri::Error::None)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Error parsing URL: %s"), *Url);
		return TEXT("");
	}

	std::string PathAndQueryString = u.path + (u.query_string.empty() ? "" : (std::string("?") + u.query_string));
	return UTF8_TO_TCHAR(PathAndQueryString.c_str());
}

FString DiversionHttp::ConvertToFstring(const std::string_view& InValue, std::size_t InSize)
{
	std::size_t size = InSize == -1 ? InValue.size() : InSize;
	return FString(UTF8_TO_TCHAR(InValue.data()), InValue.size()).Mid(0, size);
}

FString DiversionHttp::ExtractHostFromUrl(const FString& Url)
{
	std::string s = TCHAR_TO_UTF8(*Url);
	auto u = uri::parse_uri(s);
	if (u.error != uri::Error::None)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Error parsing URL: %s"), *Url);
		return TEXT("");
	}
	return UTF8_TO_TCHAR(u.authority.host.c_str());
}

FString DiversionHttp::ExtractPortFromUrl(const FString& Url)
{
	std::string s = TCHAR_TO_UTF8(*Url);
	auto u = uri::parse_uri(s);
	if (u.error != uri::Error::None)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Error parsing URL: %s"), *Url);
		return TEXT("");
	}
	// explicit port if present
	if (u.authority.port != 0)
	{
		return UTF8_TO_TCHAR(std::to_string(u.authority.port).c_str());
	}
	// otherwise default by scheme
	if (u.scheme == "http")
	{
		return TEXT("80");
	}
	else if (u.scheme == "https")
	{
		return TEXT("443");
	}
	else if (u.scheme == "ftp")
	{
		return TEXT("21");
	}
	else if (u.scheme == "ws")
	{
		return TEXT("80");
	}
	else if (u.scheme == "wss")
	{
		return TEXT("443");
	}
	return TEXT("");
}

bool DiversionHttp::IsEncrypted(const FString& Url)
{
	std::string s = TCHAR_TO_UTF8(*Url);
	auto u = uri::parse_uri(s);
	if (u.error != uri::Error::None)
	{
		UE_LOG(LogDiversionHttp, Error, TEXT("Error parsing URL: %s"), *Url);
		return false;
	}
	return (u.scheme == "https" || u.scheme == "wss");
}

FString DiversionHttp::HTTPCallResponse::GetErrorMessage() const
{
	// Priority 1: Check if Error field is set
	if (Error.IsSet())
	{
		return Error.GetValue();
	}

	// Priority 2: Try to parse Contents as JSON and look for "error" field
	if (!Contents.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);

		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			FString ErrorField;
			if (JsonObject->TryGetStringField(TEXT("error"), ErrorField))
			{
				return ErrorField;
			}
		}

		// Priority 3: Return raw contents if not empty
		return Contents;
	}

	// Fallback to empty string if Contents is empty
	return TEXT("");
}
