// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Types.h"

// PIMPL
class FHttpRequestManagerImpl;


namespace DiversionHttp {
	class DIVERSIONHTTP_API FHttpRequestManager
	{
	public:
		explicit FHttpRequestManager(const FString& HostUrl, bool bRequireEditorFocus = true, const TMap<FString, FString>& DefaultHeaders = {},
									 int HttpVersion = 11);
		FHttpRequestManager(const FString& Host, const FString& Port, bool bRequireEditorFocus = true, const TMap<FString, FString>& DefaultHeaders = {},
			bool UseSSL=true, int HttpVersion = 11);
		~FHttpRequestManager();

		// Note: There's no current support for redirections
		HTTPCallResponse SendRequest(
			const FString& Url,
			HttpMethod Method,
			const FString& Token,
			const FString& ContentType,
			const FString& Content,
			const TMap<FString, FString>& Headers,
			int ConnectionTimeoutSeconds = 5,
			// Total request time (from the second we called the function to the response)
			int RequestTimeoutSeconds = 120) const;

		HTTPCallResponse DownloadFileFromUrl(
			const FString& OutputFilePath,
			const FString& Url,
			const FString& Token,
			const TMap<FString, FString>& Headers,
			int ConnectionTimeoutSeconds = 5,
			int RequestTimeoutSeconds = 120) const;

		void SetHost(const FString& Host) const;
		void SetPort(const FString& Port) const;
		void SetUseSSL(const bool UseSSL) const;
		void SetDefaultHeaders(const TMap<FString, FString>& Headers);

		/**
		 * Signal that the manager is shutting down. Subsequent requests fail fast with
		 * HTTP_STATUS_CUSTOM_REQUEST_NOT_SENT instead of being sent. Idempotent and thread-safe.
		 */
		void BeginShutdown() const;

		/** Set a callback that provides the base URL dynamically. Called before each request to refresh host/port. */
		void SetUrlProvider(TFunction<FString()> InUrlProvider);

	private:
		void RefreshUrlFromProvider() const;

		TUniquePtr<FHttpRequestManagerImpl> Impl;
		TMap<FString, FString> DefaultHeaders;
		TFunction<FString()> UrlProvider;
	};
}
