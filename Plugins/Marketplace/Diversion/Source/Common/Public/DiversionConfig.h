// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"

#include "DiversionConfig.generated.h"

UCLASS(config = Editor, defaultconfig)
class COMMON_API UDiversionConfig : public UObject
{
		GENERATED_BODY()
public:
		UDiversionConfig();

		/*
		 * Mark this setting object as editor only.
		 * This so soft object path reference made by this setting object won't be automatically grabbed by the cooker.
		 * @see UPackage::Save, FSoftObjectPathThreadContext::GetSerializationOptions, FSoftObjectPath::ImportTextItem
		 */
		virtual bool IsEditorOnly() const override
		{
				return true;
		}
		/**
		 * True if this client should be "headless"? (ie, not display any UI).
		 *
		 * Note: there is deliberately NO equivalent toggle for hard locks. Hard locks are a
		 * repo-wide configuration (lockable patterns, Studio+ tier) enforced by the backend -
		 * the plugin only surfaces them and must not offer a way to disable or bypass them locally.
		 */
		UPROPERTY(config, EditAnywhere, Category="General Settings", Meta=(ConfigRestartRequired=false,
				DisplayName="Enable Diversion Auto Soft Lock Confirmations",
				Tooltip="If unchecked, Diversion will not warn for potential conflicts before opening and saving files that have been modified in another branch or workspace"))
		bool bEnableSoftLock = true;

		/**
		 * Gates the save-time lock prompt for plain (unlocked, clash-free) assets only.
		 * Files that are soft-locked or hard-locked by another user always prompt at save
		 * regardless of this setting - see the hard-lock note above. Off does not mean
		 * unlocked: assets matching the repo's lockable patterns are locked by the server
		 * on sync either way (see DiversionFilePattern.h).
		 */
		UPROPERTY(config, EditAnywhere, Category="General Settings", Meta=(ConfigRestartRequired=false,
				EditCondition="bHardLocksAvailable",
				DisplayName="Prompt to Lock Assets on Save",
				Tooltip="Ask per asset whether to lock it when saving. Turning this off only skips the question. Lockable assets are still locked automatically on save. Requires a Studio or Enterprise repository."))
		bool bPromptToLockOnSave = true;

		/**
		 * The answer the save-decision dialog starts on for files it offers to lock: Lock & Save
		 * when on, Save (without lock) when off. Only a starting point - the user can still pick
		 * either per file or group. Files locked by someone else always start on Skip. Hidden
		 * (and ignored) where the save prompt cannot appear.
		 */
		UPROPERTY(config, EditAnywhere, Category="General Settings", Meta=(ConfigRestartRequired=false,
				EditCondition="bHardLocksAvailable && bPromptToLockOnSave", EditConditionHides,
				DisplayName="Lock by Default on Save",
				Tooltip="If checked, the save dialog starts on Lock & Save for each asset. Uncheck to start on Save (without lock) instead. You can still change the choice per asset."))
		bool bLockByDefaultOnSave = true;

		/**
		 * Whether the current repository's tier supports hard locks. Not a setting: the provider
		 * writes it when eligibility resolves (and clears it on close), and the EditCondition
		 * above reads it to grey the prompt setting out where it can do nothing. The stored
		 * value of the setting is never touched, so a later Studio+ repo sees it as left.
		 */
		UPROPERTY(Transient)
		bool bHardLocksAvailable = false;

		/**
		 * Enable automatic reload of externally modified files.
		 */
		UPROPERTY(config, EditAnywhere, Category="General Settings", Meta=(ConfigRestartRequired=true,
				DisplayName="Enable External Changes Watcher",
				Tooltip="If checked, Diversion will automatically detect and reload files that have been modified externally. e.g. by switching branches, resetting changes from the app etc. (requires editor restart)"))
		bool bEnableChangesWatcher = false;

		/**
		 * Whether error/warning log messages that suggest contacting support also
		 * print the documentation and Discord links alongside them.
		 */
		UPROPERTY(config, EditAnywhere, Category="Advanced Settings", Meta=(ConfigRestartRequired=false,
				DisplayName="Show Support Links in Logs",
				Tooltip="If checked, Diversion error and warning messages that suggest contacting support also print the documentation and Discord links. Uncheck to keep these links out of the log."))
		bool bShowSupportLinksInLogs = true;

		/**
		 * Maximum number of HTTP responses to cache for ETag-based caching.
		 * Higher values use more memory but may reduce network requests for unchanged resources.
		 */
		UPROPERTY(config, EditAnywhere, Category="Advanced Settings", Meta=(ConfigRestartRequired=true,
				DisplayName="HTTP Response Cache Size",
				Tooltip="Maximum number of HTTP responses to cache for conditional requests (ETag caching). Higher values use more memory but may reduce network requests. Minimum: 10. (requires editor restart)",
				ClampMin=10))
		int32 ResponseCacheSize = 100;
};

COMMON_API bool IsDiversionSoftLockEnabled();
COMMON_API bool IsDiversionLockOnSavePromptEnabled();
COMMON_API bool IsDiversionLockByDefaultOnSaveEnabled();
COMMON_API bool IsDiversionChangesWatcherEnabled();
COMMON_API bool IsDiversionSupportLinksInLogsEnabled();
COMMON_API int32 GetDiversionResponseCacheSize();
