// Copyright 2024 Diversion Company, Inc. All Rights Reserved.


#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryLockManagementApi.h"
#include "DiversionLockInfoConversion.h"


using namespace Diversion::CoreAPI;

namespace
{
	/** Lists the repo's locks, optionally narrowed to a path prefix. Cost scales with the
	 * number of locks held, not with the number of files asked about. */
	bool ListFileLocks(const FDiversionCommand& InCommand, TArray<FString>& OutErrorMessages,
		TMap<FString, FDiversionLockInfo>& OutLocks, const TOptional<FString>& InPathPrefix)
	{
		const FString WsPath = InCommand.WsInfo.GetPath();
		const int32 PageLimit = 1000;
		int32 Skip = 0;
		bool bLastPage = false;

		while (!bLastPage)
		{
			int32 PageItemCount = 0;
			auto ErrorResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_listLocksDelegate::Bind(
				[&](const TVariant<TSharedPtr<FileLockList>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
					if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
					{
						auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
						if (Value.IsValid())
						{
							OutErrorMessages.Add(FString::Printf(TEXT("Received error for list file locks call: %s"), *Value->mDetail));
						}
					}
					return false;
				}
			);
			auto VariantResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_listLocksDelegate::Bind(
				[&](const TVariant<TSharedPtr<FileLockList>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
					if (!Variant.IsType<TSharedPtr<FileLockList>>())
					{
						OutErrorMessages.Add("Unexpected response type for list file locks call");
						return false;
					}
					auto Value = Variant.Get<TSharedPtr<FileLockList>>();
					if (!Value.IsValid())
					{
						OutErrorMessages.Add("Empty response for list file locks call");
						return false;
					}

					PageItemCount = Value->mItems.Num();
					for (const auto& Lock : Value->mItems)
					{
						const FString FullPath = DiversionUtils::ConvertRelativePathToDiversionFull(Lock.mPath, WsPath);
						OutLocks.Add(FullPath, MakeLockInfoFromModel(Lock, InCommand.WsInfo.AccountID));
					}
					return true;
				}
			);

			if (!FDiversionAPIAccess::RepositoryLockManagementAPI)
			{
				UE_LOG(LogSourceControl, Error, TEXT("GetFileLocks: API not initialized"));
				return false;
			}

			// Short timeouts: the save veto runs this synchronously on the game thread - a hung
			// backend must not freeze the editor (the veto fails open on timeout).
			const int ConnectionTimeoutSeconds = 3;
			const int RequestTimeoutSeconds = 30;
			bool Success = FDiversionAPIAccess::RepositoryLockManagementAPI->SrcHandlersv2FileLocksListLocks(
				InCommand.WsInfo.RepoID, PageLimit, Skip, TOptional<FString>(), InPathPrefix,
				FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {},
				ConnectionTimeoutSeconds, RequestTimeoutSeconds)
				.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);

			if (!Success)
			{
				return false;
			}

			bLastPage = PageItemCount < PageLimit;
			Skip += PageItemCount;
		}

		return true;
	}

	bool GetSingleFileLock(const FDiversionCommand& InCommand, TArray<FString>& OutErrorMessages,
		const FString& InFullPath, TMap<FString, FDiversionLockInfo>& OutLocks)
	{
		const FString WsPath = InCommand.WsInfo.GetPath();
		const FString RelativePath = DiversionUtils::ConvertFullPathToRelative(InFullPath, WsPath);

		auto ErrorResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_getLockDelegate::Bind(
			[&](const TVariant<TSharedPtr<FileLock>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
				if (StatusCode == 404)
				{
					// No lock exists for this path - a successful "not locked" answer
					OutLocks.Add(InFullPath, FDiversionLockInfo());
					return true;
				}
				if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
				{
					auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
					if (Value.IsValid())
					{
						OutErrorMessages.Add(FString::Printf(TEXT("Received error for get file lock call: %s"), *Value->mDetail));
					}
				}
				return false;
			}
		);
		auto VariantResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_getLockDelegate::Bind(
			[&](const TVariant<TSharedPtr<FileLock>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
				if (!Variant.IsType<TSharedPtr<FileLock>>())
				{
					OutErrorMessages.Add("Unexpected response type for get file lock call");
					return false;
				}
				auto Value = Variant.Get<TSharedPtr<FileLock>>();
				if (!Value.IsValid())
				{
					OutErrorMessages.Add("Empty response for get file lock call");
					return false;
				}
				OutLocks.Add(InFullPath, MakeLockInfoFromModel(*Value, InCommand.WsInfo.AccountID));
				return true;
			}
		);

		if (!FDiversionAPIAccess::RepositoryLockManagementAPI)
		{
			UE_LOG(LogSourceControl, Error, TEXT("GetFileLocks: API not initialized"));
			return false;
		}

		// Short timeouts: this call runs synchronously on the game thread on every asset
		// open - a hung backend must not freeze the editor (fail-open handles the timeout).
		const int ConnectionTimeoutSeconds = 3;
		const int RequestTimeoutSeconds = 30;
		return FDiversionAPIAccess::RepositoryLockManagementAPI->SrcHandlersv2FileLocksGetLock(
			InCommand.WsInfo.RepoID, RelativePath,
			FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {},
			ConnectionTimeoutSeconds, RequestTimeoutSeconds)
			.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
	}
}

bool DiversionUtils::GetFileLocks(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, TMap<FString, FDiversionLockInfo>& OutLocks, bool& OutFullRepoQuery)
{
	const FString WsPath = InCommand.WsInfo.GetPath();

	// A single queried path equal to the workspace root means a repo-wide locks refresh
	OutFullRepoQuery = (InCommand.Files.Num() == 1) && (InCommand.Files[0] == WsPath);
	if (OutFullRepoQuery)
	{
		return ListFileLocks(InCommand, OutErrorMessages, OutLocks, TOptional<FString>());
	}

	TArray<FString> RequestedPaths;
	for (const FString& Path : InCommand.Files)
	{
		if (!FPaths::IsUnderDirectory(Path, WsPath))
		{
			UE_LOG(LogSourceControl, Log, TEXT("Path: %s is not contained in the repo, skipping."), *Path);
			continue;
		}
		RequestedPaths.Add(Path);
	}

	if (RequestedPaths.Num() == 1)
	{
		return GetSingleFileLock(InCommand, OutErrorMessages, RequestedPaths[0], OutLocks);
	}

	// Asking per file would cost one blocking round trip each, which a Save All over a World
	// Partition level turns into thousands. One prefixed listing answers for all of them.
	const FString AncestorPrefix = DiversionUtils::ConvertFullPathToRelative(
		DiversionUtils::FindCommonAncestorDirectory(RequestedPaths), WsPath);
	// Every lock the listing found is freshly verified, including ones under the prefix that
	// nobody asked about - keep them all so the state cache benefits from the same round trip
	if (!ListFileLocks(InCommand, OutErrorMessages, OutLocks,
		AncestorPrefix.IsEmpty() ? TOptional<FString>() : TOptional<FString>(AncestorPrefix)))
	{
		return false;
	}

	// A requested path the listing did not return holds no lock, and has to say so explicitly:
	// an unset entry is what clears a lock that has since been released
	for (const FString& Path : RequestedPaths)
	{
		if (!OutLocks.Contains(Path))
		{
			OutLocks.Add(Path, FDiversionLockInfo());
		}
	}
	return true;
}
