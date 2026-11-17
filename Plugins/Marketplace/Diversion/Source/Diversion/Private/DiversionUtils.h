// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "HttpModule.h"
#include "DiversionState.h"
#include "CoreAPI/Public/Merge.h"
#include "DiversionValidityUtils.h"


class FDiversionState;
class FDiversionCommand;
struct FAssetData;
struct FDiversionVersion;
struct WorkspaceInfo;

DECLARE_DELEGATE_RetVal(bool, WaitForConditionPredicate)

namespace DiversionUtils
{
	namespace UPackageUtils {
		/**
		* Get the package object from a file absolute path
		* @param InPath The absolute path to get the package name from
		* @returns the package object or nullptr if the package was not loaded
		*/
		UPackage* PackageFromPath(const FString& InPath, bool InTryLoading = false);

		/**
		* Check if a package is opened in the editor
		* @param PackageName The name of the package to check
		* @returns true if the package is opened in the editor
		*/
		bool IsPackageOpenedInEditor(const FString& PackageName);

		/**
		 * Wrap UE package saving mechanism
		 * @param Package
		 * @return true if the package was saved successfully
		 */
		bool SavePackage(UPackage* Package);

		/**
		 * Closing all editor tabs for a package
		 * @param Package
		 */
		void ClosePackageEditor(UPackage* Package);

		/**
		 * Discard all unsaved changes for a package
		 * @param Package
		 */
		void DiscardUnsavedChanges(UPackage* Package);

		bool BackupUnsavedChanges(UPackage* Package);

		bool MakeSimpleOSCopyPackageBackup(const FString& OriginalFileFullPath, const FString& TargetBackupFolderPath);

		/**
		 * Whether a package file is a level or one of its actor/object packages. Reloading one
		 * of those alone does not bring back the level as it is on disk - the whole level has
		 * to be reloaded (see the Changes Watcher).
		 * @param InFullPath Absolute path of the package file
		 */
		bool IsLevelPackage(const FString& InFullPath);

	}

enum class EDiversionWsSyncStatus {
	Unknown,
	PathError,
	Paused,
	InProgress,
	Completed
};

FText GetWsSyncStatusText(EDiversionWsSyncStatus InStatus);

/** Formats a Unix timestamp as a localized date and time for display */
FText FormatUnixTimestamp(int64 InUnixTimestamp);

/**
* Printing common support referencing log line
* @param InMessage The message to print in addition to the support links
* @param IsWarning If true, the log line will be a warning
*/
void PrintSupportNeededLogLine(const FString& InMessage, bool IsWarning = false);

/**
* Starts the diversion agent process
* @param InPathToDiversionBinary The path to the diversion binary
* @returns true if the agent was started successfully
*/
bool StartDiversionAgent(const FString& InPathToDiversionBinary);

/**
 * Get the workspace revision number from a commit ID
 * @param CommitID The commit ID to parse
 * @returns the workspace revision number
 */
int GetWorkspaceRevisionByCommit(const FString& CommitID);

/**
 * Helper function to wait for a condition to be true.
 * @note This function will block the calling thread until the condition is true or the timeout is reached.
 * @param Condition The condition to wait for
 * @param Timeout The maximum time to wait for the condition to be true
 * @param InWaitPollInterval The interval to wait between condition checks
 * @returns true if the condition was true before the timeout
 */
bool WaitForCondition(const WaitForConditionPredicate& Condition, float Timeout, float InWaitPollInterval = 0.1f);

FString ConvertFullPathToRelative(const FString& InPath, FString RootDir = "");

FString ConvertRelativePathToDiversionFull(const FString& InPath, FString RootDir = "");

FString RefToOrdinalId(const FString& RefId);

void ShowErrorNotification(const FText& ErrorMessage);

bool IsDiversionInstalled();

FString GetFilePathFromAssetData(const FAssetData& AssetData);

/**
 * Extract common prefixes from the given paths list and removes duplicates.
 * Inclusive for folders path as well.
 * @param InPaths The paths to extract common prefixes from. Pass 0 for no limit.
 * @param InBaseRepoPath The base path to extract the common prefixes from.
 * @param InLimit The maximum number of prefixes to return.
 * @returns Set containing the common prefixes as relative paths to the repo root provided.
 */
TSet<FString> GetPathsCommonPrefixes(const TArray<FString>& InPaths, const FString& InBaseRepoPath, int32 InLimit = 0);

FString FindCommonAncestorDirectory(const TArray<FString>& InPaths);

/**
 * Normalize a directory path and ensure it ends with a trailing forward slash.
 *
 * Normalization behavior:
 * - Converts all backslashes to forward slashes
 * - Ensures path ends with a single trailing forward slash
 * - Does NOT collapse multiple consecutive slashes (use FPaths::RemoveDuplicateSlashes for that)
 * - Empty input returns empty string
 * - Invalid paths ("/", "\\", ".", "./", ".\\") return unchanged and log an error
 *
 * @param InPath The path to normalize
 * @returns The normalized path with a trailing forward slash (always uses / regardless of OS)
 */
FString NormalizePathWithTrailingSeparator(const FString& InPath);

FString GetStackTrace();

bool DiversionValidityCheck(bool InCondition, FString InErrorDescription, const FString& AccountId, bool InSilent = false);

/**
 * Get the Diversion state for an asset at the given path.
 *
 * @note Must be called from the game thread.
 * @param AssetPath The full file path to the asset
 * @returns The Diversion state for the asset, or nullptr if:
 *          - Not on game thread
 *          - Source control is not enabled
 *          - Provider is not Diversion
 *          - No state exists for the path
 */
TSharedPtr<FDiversionState, ESPMode::ThreadSafe> GetDiversionStateForPath(const FString& AssetPath);

#define AUTHORIZATION_HEADER_KEY "Authorization"
#define APP_VERSION_HEADER_KEY "X-DV-App-Version"
#define APP_NAME_HEADER_KEY "X-DV-App-Name"
#define CORRELATION_ID_HEADER_KEY "X-Sentry-Correlation-ID"

TMap<FString, FString> GetDiversionHeaders();

// API calls
bool GetWorkspaceConfigByPath(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InPath);
bool SendAnalyticsEvent(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const TMap<
						FString, FString>& InProperties);
bool RunAgentHealthCheck(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool AgentInSync(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool GetConflictedFiles(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, TMap<FString, FDiversionResolveInfo>& OutConflicts,
	TArray<Diversion::CoreAPI::Model::Merge>& OutWorkspaceMergesList, TArray<Diversion::CoreAPI::Model::Merge>& OutBranchMergesList);
bool RunGetMerges(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, TArray<Diversion::CoreAPI::Model::Merge>& OutMerges);
bool RunUpdateStatus(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, bool WaitForSync = true);
bool RunCommit(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InDescription, bool WaitForSync = true, FString* OutCommitId = nullptr);
bool RunReset(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool WaitForAgentSync(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, float InSecondsToTimeout = 10.f);
bool RunGetHistory(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InFile,
	const FString* MergeFromRef);
bool GetWsBlobInfo(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InFile);
bool RunResolvePath(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	const FString& InMergeId, const FString& InConflictId, bool WaitForSync = true);
bool RunFinalizeMerge(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InMergeId);
// Exceptions to the API calls due to being executed irregularly by the engine
bool DownloadFileFromURL(const FString& Url, const FString& SavePath);
bool DownloadBlob(TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InRefId, const FString& InOutputFilePath, const FString& InFilePath, WorkspaceInfo InWsInfo);
bool RunRepoInit(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, const FString& InRepoRootPath, const FString& InRepoName);
bool GetPotentialFileClashes(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, TMap<FString, TArray<EDiversionPotentialClashInfo>>& OutPotentialClashes, bool& OutRecurseCall);
bool GetFileLocks(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, TMap<FString, FDiversionLockInfo>& OutLocks, bool& OutFullRepoQuery);

/** The lock mutation a FUpdateFileLock operation performs */
enum class EDiversionLockAction : uint8
{
	Lock,
	Unlock,
	/** Admin-only release of another user's lock (server rejects non-admins with 403) */
	ForceUnlock
};

/** A single file the lock mutation failed for, with the server's status code and detail */
struct FDiversionLockOpFailure
{
	FString Path;
	int32 StatusCode = 0;
	FString Detail;
};

/**
 * Lock or unlock files (see EDiversionLockAction). Per-file failures (409 already-locked,
 * 403 not-holder/not-admin) are recorded in OutFailures without aborting the remaining
 * files; the return value is false only on transport-level failure. Unlocking a file with
 * no lock (404) counts as success. OutUpdatedLocks receives the resulting state per
 * succeeded file - a set entry for Lock, an unset entry (cleared) for Unlock.
 */
bool UpdateFileLocks(const FDiversionCommand& InCommand, EDiversionLockAction InAction,
	TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	TMap<FString, FDiversionLockInfo>& OutUpdatedLocks, TArray<FDiversionLockOpFailure>& OutFailures);

/**
 * Resolves whether the repo's effective tier supports hard locks, and reads the repo's
 * lockable patterns off the same GET /repos/{id} call. OutLockablePatterns is set (possibly
 * empty) whenever the repo was fetched, unset when it was not.
 */
bool GetHardLocksEligibility(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, TOptional<bool>& bOutEligible, TOptional<TArray<FString>>& OutLockablePatterns);

/** Whether the tier supports hard locks. Mirrors the backend's STUDIO_PLUS_TIERS
 * (tier_utils.py) and the app's isLockingEnabled (rolloutUtils.ts) - keep all three in sync. */
bool IsHardLocksEligibleTier(const FString& InTier);
bool GetWorkspaceSyncProgress(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool NotifyAgentSyncRequired(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool UpdateWorkspace(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages);
bool GetRemoteRepos(FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, bool InOwnedOnly, TArray<FString>& OutReposList);
bool GetAllLocalWorkspaces(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages, TArray<WorkspaceInfo>& OutLocalWorkspaces);

// Since this API doesn't affect the state of the Provider and doesn't return any value, it can live without the context of a FDiversionCommand
bool SendErrorToBE(const FString& AccountID, const FString& InErrorMessageToReport, const FString& InStackTrace);
} // namespace DiversionUtils
