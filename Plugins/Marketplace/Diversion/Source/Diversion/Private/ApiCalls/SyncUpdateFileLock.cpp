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
	bool LockSingleFile(const FDiversionCommand& InCommand, TArray<FString>& OutErrorMessages,
		const FString& InFullPath, TMap<FString, FDiversionLockInfo>& OutUpdatedLocks,
		TArray<DiversionUtils::FDiversionLockOpFailure>& OutFailures)
	{
		const FString WsPath = InCommand.WsInfo.GetPath();
		const FString RelativePath = DiversionUtils::ConvertFullPathToRelative(InFullPath, WsPath);

		auto ErrorResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_lockFileDelegate::Bind(
			[&](const TVariant<TSharedPtr<FileLock>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
				// A rejected lock (409 already locked / 400) is a per-file result, not a
				// transport failure - record it and keep processing the remaining files
				FString Detail;
				if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
				{
					auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
					if (Value.IsValid())
					{
						Detail = Value->mDetail;
					}
				}
				OutFailures.Add({ InFullPath, StatusCode, Detail });
				return true;
			}
		);
		auto VariantResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_lockFileDelegate::Bind(
			[&](const TVariant<TSharedPtr<FileLock>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
				if (!Variant.IsType<TSharedPtr<FileLock>>())
				{
					OutErrorMessages.Add("Unexpected response type for lock file call");
					return false;
				}
				auto Value = Variant.Get<TSharedPtr<FileLock>>();
				if (!Value.IsValid())
				{
					OutErrorMessages.Add("Empty response for lock file call");
					return false;
				}
				OutUpdatedLocks.Add(InFullPath, MakeLockInfoFromModel(*Value, InCommand.WsInfo.AccountID));
				return true;
			}
		);

		if (!FDiversionAPIAccess::RepositoryLockManagementAPI)
		{
			UE_LOG(LogSourceControl, Error, TEXT("UpdateFileLocks: API not initialized"));
			return false;
		}

		// Short timeouts: the context-menu actions execute synchronously on the game thread
		// (deliberate user action - the result must be correct when the click returns), so a
		// hung backend must not freeze the editor (same reasoning as GetSingleFileLock)
		const int ConnectionTimeoutSeconds = 3;
		const int RequestTimeoutSeconds = 30;
		return FDiversionAPIAccess::RepositoryLockManagementAPI->SrcHandlersv2FileLocksLockFile(
			InCommand.WsInfo.RepoID, RelativePath,
			FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {},
			ConnectionTimeoutSeconds, RequestTimeoutSeconds)
			.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
	}

	bool UnlockSingleFile(const FDiversionCommand& InCommand, TArray<FString>& OutErrorMessages,
		const FString& InFullPath, bool bForce, TMap<FString, FDiversionLockInfo>& OutUpdatedLocks,
		TArray<DiversionUtils::FDiversionLockOpFailure>& OutFailures)
	{
		const FString WsPath = InCommand.WsInfo.GetPath();
		const FString RelativePath = DiversionUtils::ConvertFullPathToRelative(InFullPath, WsPath);

		auto ErrorResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_unlockFileDelegate::Bind(
			[&](const TVariant<void*, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
				if (StatusCode == 404)
				{
					// No lock exists - the file already is in the desired state
					OutUpdatedLocks.Add(InFullPath, FDiversionLockInfo());
					return true;
				}
				// 403 (not the holder / not an admin) and the rest are per-file results
				FString Detail;
				if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
				{
					auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
					if (Value.IsValid())
					{
						Detail = Value->mDetail;
					}
				}
				OutFailures.Add({ InFullPath, StatusCode, Detail });
				return true;
			}
		);
		auto VariantResponse = RepositoryLockManagementApi::Fsrc_handlersv2_fileLocks_unlockFileDelegate::Bind(
			[&](const TVariant<void*, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
				// 204: released - clear the cached lock state via an unset entry
				OutUpdatedLocks.Add(InFullPath, FDiversionLockInfo());
				return true;
			}
		);

		if (!FDiversionAPIAccess::RepositoryLockManagementAPI)
		{
			UE_LOG(LogSourceControl, Error, TEXT("UpdateFileLocks: API not initialized"));
			return false;
		}

		// force is only serialized when set: a plain unlock must omit it so a non-holder
		// gets the 403 instead of silently force-releasing someone else's lock.
		// Short timeouts: synchronous game-thread call (see LockSingleFile).
		const int ConnectionTimeoutSeconds = 3;
		const int RequestTimeoutSeconds = 30;
		return FDiversionAPIAccess::RepositoryLockManagementAPI->SrcHandlersv2FileLocksUnlockFile(
			InCommand.WsInfo.RepoID, RelativePath,
			bForce ? TOptional<bool>(true) : TOptional<bool>(),
			FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {},
			ConnectionTimeoutSeconds, RequestTimeoutSeconds)
			.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
	}
}

bool DiversionUtils::UpdateFileLocks(const FDiversionCommand& InCommand, EDiversionLockAction InAction,
	TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	TMap<FString, FDiversionLockInfo>& OutUpdatedLocks, TArray<FDiversionLockOpFailure>& OutFailures)
{
	const FString WsPath = InCommand.WsInfo.GetPath();

	bool Success = true;
	for (const auto& Path : InCommand.Files)
	{
		if (!FPaths::IsUnderDirectory(Path, WsPath))
		{
			UE_LOG(LogSourceControl, Log, TEXT("Path: %s is not contained in the repo, skipping."), *Path);
			continue;
		}
		if (InAction == EDiversionLockAction::Lock)
		{
			Success &= LockSingleFile(InCommand, OutErrorMessages, Path, OutUpdatedLocks, OutFailures);
		}
		else
		{
			Success &= UnlockSingleFile(InCommand, OutErrorMessages, Path,
				InAction == EDiversionLockAction::ForceUnlock, OutUpdatedLocks, OutFailures);
		}
		if (!Success)
		{
			// Transport-level failure (per-file rejections return true above). Deliberate stop:
			// retrying a dead connection stalls the game thread per file; callers account for
			// the unattempted remainder (worker toast / save-decision retry).
			break;
		}
	}
	return Success;
}
