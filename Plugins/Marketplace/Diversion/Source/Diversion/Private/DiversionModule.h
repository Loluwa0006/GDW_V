// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "DiversionSettings.h"
#include "DiversionProvider.h"
#include "DiversionUtils.h"

#include "ISourceControlModule.h"

class SWindow;
struct FConfirmationOption;
struct FConfirmationAdditionalLink;

/** Diversion Version Control Plugin for Unreal Engine */
class FDiversionModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Access the Diversion version control settings */
	FDiversionSettings& AccessSettings()
	{
		return DiversionSettings;
	}

	/** Save the Diversion version control settings */
	void SaveSettings();

	/** Access the Diversion version control provider */
	FDiversionProvider& GetProvider()
	{
		// Game thread only. A reference cannot be declined, so a caller on another thread
		// would get the provider regardless and start the data race this rule prevents -
		// hence a hard check rather than a report.
		check(IsInGameThread());
		return DiversionProvider;
	}

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, asserts if the module is not loaded yet or unloaded already.
	 */
	static FDiversionModule& Get();

	/**
	 * Checks whether the module is currently loaded.
	 */
	static bool IsLoaded();

	/**
	 * Finds information of the plugin.
	 *
	 * @return	 Pointer to the plugin's information, or nullptr.
	 */
	static const TSharedPtr<class IPlugin> GetPlugin();

	inline static FString GetAppName()
	{
		return "Plugin-Unreal";
	}

	static FString GetPluginVersion();

	FString GetOriginalAccountID() const
	{
		return OriginalAccountID;
	}

	void SetOriginalAccountID(const FString& InAccountID)
	{
		OriginalAccountID = InAccountID;
	}

private:
	/** Event handler for asset editor window opening */
	void HandleAssetOpenedInEditor(UObject* Asset);
	bool PotentialConflictExistForAsset(UObject* Asset);
	void ShowPotentialConflictConfirmationDialog(UObject* Asset);

	/** Synchronously refreshes the asset's hard-lock state and returns true if it's locked by another user */
	bool HardLockedByOtherForAsset(UObject* Asset, FDiversionLockInfo& OutLockInfo);
	void ShowHardLockDialog(UObject* Asset, const FDiversionLockInfo& LockInfo);

#pragma region AssetOpenDialogShared
	/**
	 * Scaffolding shared by the two dialogs raised when an asset is opened under a lock: the
	 * hard-lock dialog and the soft-clash confirmation. Each composes its own title, message,
	 * buttons and links - only the mechanics below are common. The button identifiers are a
	 * shared contract; ApplyAssetOpenChoice is what gives them meaning.
	 */
	enum class EAssetOpenDialogChoice
	{
		/** The window's X - treated as Close */
		AssetOpenDialog_XButton = -1,
		/** Reopen the asset editor in read-only (View) mode */
		AssetOpenDialog_ReadOnly = 0,
		/** Close the asset editor, or reload the default map when the asset is a world */
		AssetOpenDialog_Close = 1,
		/** Keep the already-opened editor and carry on editing */
		AssetOpenDialog_Proceed = 2
	};

	/** Whether this asset type can be reopened read-only. Always false before UE 5.4, where
	 * CanOpenEditorForAsset does not exist yet. */
	static bool CanOpenAssetReadOnly(UObject* Asset);

	/** The autosized, centered modal window both dialogs live in */
	static TSharedRef<SWindow> MakeAssetOpenDialogWindow(const FText& InTitle);

	/** The leading "Read Only" button, or nothing when the asset type can't be viewed */
	static TArray<TSharedRef<FConfirmationOption>> MakeReadOnlyButton(bool bInAssetCanBeReadOnly);

	/** The "Feedback" link; InFeedbackSource tags the analytics event with the originating dialog */
	static TSharedRef<FConfirmationAdditionalLink> MakeFeedbackLink(const FText& InTooltip,
		const FString& InFeedbackSource);

	/** Applies the user's choice to the asset editor; returns the analytics label for it */
	FString ApplyAssetOpenChoice(UObject* Asset, EAssetOpenDialogChoice InChoice);

	/** Fire-and-forget analytics for a dialog interaction */
	static void SendDialogAnalytics(const FString& InEventName, const TMap<FString, FString>& InProperties);
#pragma endregion


	/* Stores list of assets paths that are scheduled for re-opening as a readonly window.
	 * Used to filter out the "re-opening" asset editor window events.
	**/
	TSet<FString> OpenedEditorAssets;

private:
	/** The Diversion version control provider */
	FDiversionProvider DiversionProvider;

	/** The settings for Diversion version control */
	FDiversionSettings DiversionSettings;

	// Storing long-lived APICalls
	TMap<FString, TSharedPtr<void>> ApiCallCache;
	mutable TUniquePtr<FRWLock> ServiceStatusLock;

	/** The account ID with which the current plugin session was initiated */
	FString OriginalAccountID;

	/** Saved global state of the extra confirmation checkbox when trying to open a potentially conflicted file */
	ECheckBoxState bConflictedFileOpenConfirmCheckboxState = ECheckBoxState::Unchecked;

	/** Tracks whether the module was fully initialized (false if StartupModule returns early, e.g., in commandlet or unattended mode) */
	bool bIsInitialized = false;
};
