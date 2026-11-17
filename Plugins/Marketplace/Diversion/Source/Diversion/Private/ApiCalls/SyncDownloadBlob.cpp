// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::DownloadBlob(TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, const FString& InRefId, const FString& InOutputFilePath,
	const FString& InFilePath, WorkspaceInfo InWsInfo)
{
	FString RedirectUrl = "";
	auto ErrorResponse = RepositoryManipulationApi::Fsrc_handlersv2_files_getBlobDelegate::Bind(
		[&]() {
			return false;
		}
	);

	auto VariantResponse = RepositoryManipulationApi::Fsrc_handlersv2_files_getBlobDelegate::Bind(
		[&](const TVariant<TSharedPtr<HttpContent>, void*>& Variant, int StatusCode, TMap<FString, FString> Headers) {
			switch (StatusCode)
			{
				case 200:
				{
					// File was downloaded
					if (!Variant.IsType<TSharedPtr<HttpContent>>())
					{
						// Unexpected response type
						OutErrorMessages.Add("Unexpected response type");
						return false;
					}
					// Write the file to disk
					auto Value = Variant.Get<TSharedPtr<HttpContent>>();
					Value->WriteToFile(InOutputFilePath);
					OutInfoMessages.Add("File downloaded successfully");
					return true;
				}
				case 204:
				{
					// No content - look for the location header for redirection URL
					if (FString* location = Headers.Find("Location"))
					{
						RedirectUrl = *location;
						OutInfoMessages.Add("Received redirection URL for file");
						return true;
					}
					return false;
				}
				default:
				{
					// Unexpected response type
					OutErrorMessages.Add("Unexpected response type");
					return false;
				}
			}
		}
	);

	if (!FDiversionAPIAccess::RepositoryManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("DownloadBlob: API not initialized"));
		return false;
	}

	bool Success = FDiversionAPIAccess::RepositoryManipulationAPI->SrcHandlersv2FilesGetBlob(InWsInfo.RepoID,
		InRefId, ConvertFullPathToRelative(InFilePath, InWsInfo.GetPath()),
		FDiversionAPIAccess::GetAccessToken(InWsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);

	if (!RedirectUrl.IsEmpty())
	{
		// If the redirect URL was populated it means we need to download the file from there

		// Download the file from the redirect URL
		DiversionHttp::FHttpRequestManager FileDownloaderRequestManager(RedirectUrl);
		FString RequestPath = DiversionHttp::GetPathFromUrl(RedirectUrl);


		FString FileDownloadToken = FString();
		if (DiversionHttp::ExtractHostFromUrl(RedirectUrl).Contains(DiversionHttp::DIVERSION_HOST_STRING))
		{
			FileDownloadToken = FDiversionAPIAccess::GetAccessToken(InWsInfo.AccountID);
		}

		auto Response = FileDownloaderRequestManager.DownloadFileFromUrl(InOutputFilePath, RequestPath, FileDownloadToken, {}, 5, 120);
		if (Response.Error.IsSet())
		{
			FString ErrorMessage = Response.GetErrorMessage();
			OutErrorMessages.Add(ErrorMessage);
			UE_LOG(LogSourceControl, Error, TEXT("Error downloading file: %s"), *ErrorMessage);
			return false;
		}

		if (Response.ResponseCode >= 400)
		{
			FString ErrorMessage = Response.GetErrorMessage();
			OutErrorMessages.Add(ErrorMessage);
			UE_LOG(LogSourceControl, Error, TEXT("Error downloading file: %s"), *ErrorMessage);
			return false;
		}

		if (Response.Contents.Equals(InOutputFilePath))
		{
			OutInfoMessages.Add("File downloaded successfully");
			Success = true;
		}
	}

	return Success;
}

