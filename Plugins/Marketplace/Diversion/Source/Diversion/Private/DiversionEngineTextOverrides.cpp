// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionEngineTextOverrides.h"

#include "Internationalization/TextLocalizationManager.h"
#include "Internationalization/TextLocalizationResource.h"

#define LOCTEXT_NAMESPACE "DiversionEngineTextOverrides"


namespace
{
	/** An engine text that Diversion rewords. The engine text is identified by namespace, key and its
	 * English source string, so the override stops applying if the engine ever changes that text. */
	struct FEngineTextOverride
	{
		const TCHAR* Namespace;
		const TCHAR* Key;
		const TCHAR* EngineSourceString;
		FText Replacement;
	};

	/** Built on each call so the replacements resolve in the current culture */
	TArray<FEngineTextOverride> GetEngineTextOverrides()
	{
		return {
			// The engine's post-save warning toast fires for packages our save veto blocked, BEFORE
			// the Diversion save-decision dialog appears - point the user at what follows. (The
			// engine's checkout dialog itself is unreachable for Diversion: IsCheckedOut() is true
			// for every tracked file, so its strings need no overrides anymore.)
			{ TEXT("FileHelpers"), TEXT("Warning_Notification"),
				TEXT("Warning: Assets have conflict in Revision Control or cannot be written to disk"),
				LOCTEXT("FileHelpers_Warning_Notification", "Some assets were not saved yet. If Diversion held them back, a save dialog will follow to decide per file.") },
			{ TEXT("UnsavedAssetsTracker"), TEXT("Locked_by_Other_Warning"),
				TEXT("Warning: Assets you have edited are locked by another user."),
				LOCTEXT("UnsavedAssetsTracker_Locked_by_Other_Warning", "Warning: Assets you have edited are also being edited in another branch or workspace.") },
			{ TEXT("AssetSourceControlContextMenu"), TEXT("SCCRevert"),
				TEXT("Revert"),
				LOCTEXT("AssetSourceControlContextMenu_SCCRevert", "Reset") },
			{ TEXT("AssetSourceControlContextMenu"), TEXT("SCCRevertTooltip"),
				TEXT("Reverts the selected assets to their original state from revision control."),
				LOCTEXT("AssetSourceControlContextMenu_SCCRevertTooltip", "Resets the selected assets to their original state from Diversion.") },
			{ TEXT("SourceControl"), TEXT("SourceControl_CheckIn"),
				TEXT("Checking file(s) into Revision Control..."),
				LOCTEXT("SourceControl_SourceControl_CheckIn", "Committing file(s) to Diversion...") },
			{ TEXT("SourceControl"), TEXT("SourceControl_Revert"),
				TEXT("Reverting file(s) in Revision Control..."),
				LOCTEXT("SourceControl_SourceControl_Revert", "Resetting file(s) in Diversion...") },
			{ TEXT("SourceControl.RevertWindow"), TEXT("Title"),
				TEXT("Revert Files"),
				LOCTEXT("SourceControl.RevertWindow_Title", "Reset Files") },
			{ TEXT("SourceControl.Revert"), TEXT("SelectFiles"),
				TEXT("Select the files that should be reverted below"),
				LOCTEXT("SourceControl.Revert_SelectFiles", "Select the files to reset below") },
			{ TEXT("SourceControl.Revert"), TEXT("RevertUnchanged"),
				TEXT("Revert Unchanged Only"),
				LOCTEXT("SourceControl.Revert_RevertUnchanged", "Reset Unchanged Only") },
			{ TEXT("SSourceControlRevert"), TEXT("RevertButton"),
				TEXT("Revert"),
				LOCTEXT("SSourceControlRevert_RevertButton", "Reset") },
			{ TEXT("FileSourceControlActions"), TEXT("SCCRevertTooltip"),
				TEXT("Reverts the selected files to their original state from revision control."),
				LOCTEXT("FileSourceControlActions_SCCRevertTooltip", "Resets the selected files to their original state from Diversion.") },
			{ TEXT("SourceControlWindows"), TEXT("ChooseAssetsToCheckInIndicator"),
				TEXT("Checking for assets to check in..."),
				LOCTEXT("SourceControlWindows_ChooseAssetsToCheckInIndicator", "Waiting for selection of assets to commit...") },
			{ TEXT("SourceControlWindows"), TEXT("NoAssetsToCheckIn"),
				TEXT("No assets to check in!"),
				LOCTEXT("SourceControlWindows_NoAssetsToCheckIn", "No changes to commit") },
			{ TEXT("UICommands.SourceControlCommands"), TEXT("SubmitContent_ToolTip"),
				TEXT("Opens a dialog with check in options for content and levels."),
				LOCTEXT("SourceControlCommands_SubmitContent_ToolTip", "Opens a dialog with commit options for content and levels.") },
			{ TEXT("UICommands.SourceControlCommands"), TEXT("SubmitContent"),
				TEXT("Submit Content"),
				LOCTEXT("SourceControlCommands_SubmitContent", "Commit Changes") },
			{ TEXT("UICommands.SourceControlCommands"), TEXT("CheckOutModifiedFiles_ToolTip"),
				TEXT("Opens a dialog to check out any assets which have been modified."),
				LOCTEXT("SourceControlCommands_CheckOutModifiedFiles_ToolTip", "Saves all modified assets.") },
			{ TEXT("UICommands.SourceControlCommands"), TEXT("CheckOutModifiedFiles"),
				TEXT("Check Out Modified Files"),
				LOCTEXT("SourceControlCommands_CheckOutModifiedFiles", "Save Modified Files") },
			{ TEXT("SourceControl.SubmitPanel"), TEXT("ChangeListDesc"),
				TEXT("Changelist Description"),
				LOCTEXT("SourceControl.SubmitPanel_ChangeListDesc", "Commit Message") },
			{ TEXT("SourceControl.SubmitPanel"), TEXT("OKButton"),
				TEXT("Submit"),
				LOCTEXT("SourceControl.SubmitPanel_OKButton", "Commit") },
			{ TEXT("SourceControl.SubmitPanel"), TEXT("KeepCheckedOut"),
				TEXT("Keep Files Checked Out"),
				LOCTEXT("SourceControl.SubmitPanel_KeepCheckedOut", "Thank you for using Diversion!") },
			{ TEXT("SourceControl.SubmitPanel"), TEXT("ChangeListDescWarning"),
				TEXT("Changelist description is required to submit"),
				LOCTEXT("SourceControl.SubmitPanel_ChangeListDescWarning", "Commit message is required") },
			{ TEXT("SourceControl.SubmitWindow"), TEXT("Title"),
				TEXT("Submit Files"),
				LOCTEXT("SourceControl.SubmitWindow_Title", "Commit Files") },
			{ TEXT("SourceControlCommands"), TEXT("SourceControlStatus_Error_ServerUnavailable"),
				TEXT("Server Unavailable"),
				LOCTEXT("SourceControlCommands_SourceControlStatus_Error_ServerUnavailable", "Sync Paused") },
		};
	}

	void ApplyEngineTextOverrides()
	{
		FTextLocalizationResource LocalizationResource;
		for (const FEngineTextOverride& Override : GetEngineTextOverrides())
		{
			LocalizationResource.AddEntry(FTextKey(Override.Namespace), FTextKey(Override.Key),
				FString(Override.EngineSourceString), Override.Replacement.ToString(), 100);
		}
		FTextLocalizationManager::Get().UpdateFromLocalizationResource(LocalizationResource);
	}

	/** A culture change (or any other localization refresh) reloads the engine's translations over our
	 * overrides - put them back. Updating the live table raises the same event again, hence the guard. */
	void OnTextRevisionChanged()
	{
		static bool bApplyingOverrides = false;
		if (bApplyingOverrides)
		{
			return;
		}
		TGuardValue<bool> ApplyingGuard(bApplyingOverrides, true);
		ApplyEngineTextOverrides();
	}

	FDelegateHandle TextRevisionChangedHandle;
}

namespace DiversionEngineTextOverrides
{
	void Enable()
	{
		ApplyEngineTextOverrides();
		if (!TextRevisionChangedHandle.IsValid())
		{
			TextRevisionChangedHandle = FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddStatic(&OnTextRevisionChanged);
		}
	}

	void Disable()
	{
		// Stop re-applying first, or the refresh below would bring the overrides back
		FTextLocalizationManager::Get().OnTextRevisionChangedEvent.Remove(TextRevisionChangedHandle);
		TextRevisionChangedHandle.Reset();
		FTextLocalizationManager::Get().RefreshResources();
	}

	bool IsEnabled()
	{
		return TextRevisionChangedHandle.IsValid();
	}
}

#undef LOCTEXT_NAMESPACE
