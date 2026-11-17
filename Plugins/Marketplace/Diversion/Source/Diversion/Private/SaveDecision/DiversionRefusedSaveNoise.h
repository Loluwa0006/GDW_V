// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/CoreDelegates.h"

/**
 * Hides the engine's reaction to a save the user chose not to make.
 *
 * Refusing a save is the only way the save-decision gate ever fails one, and the engine
 * treats every failed save as an error: SavePackage2 writes "Asset '...' is not allowed to
 * save", FileHelpers collects it into the "Save Output" page and opens the Message Log, then
 * offers a Cancel / Retry / Continue prompt. All of that reports a problem for a decision the
 * user made on purpose - and none of it can be turned off at the source (only SAVE_NoError
 * suppresses the warning, and editor save gestures never pass it).
 *
 * So it is cleaned up afterwards, and only ever for files the user actually refused. Tell it
 * about each refusal as it happens, then call FlushAfterSave() once the save has unwound.
 */
class FDiversionRefusedSaveNoise
{
public:
	~FDiversionRefusedSaveNoise() { Unbind(); }

	/** Chain the editor's modal-dialog routing so the Retry prompt can be answered silently */
	void Bind();
	/** Restore the engine's handler and forget any pending refusals */
	void Unbind();

	/**
	 * Record that this file was refused, and stamp a canary-marked line explaining why onto
	 * the same log the engine is about to write its warning to. ErrorLog may be null.
	 */
	void NoteRefused(const FString& InFilename, FOutputDevice* ErrorLog);

	/** Clear the log noise the refusals produced and forget them. Call once the save is over. */
	void FlushAfterSave();

private:
	/**
	 * Answers the engine's "failed to save - Cancel / Retry / Continue" with Continue and no
	 * UI, but only when it names a file refused in this save; everything else passes through
	 * to the engine's own handler. Offering Retry would invite the user to undo the choice
	 * they just made.
	 */
	EAppReturnType::Type OnModalMessageDialog(EAppMsgCategory InCategory, EAppMsgType::Type InType,
		const FText& InText, const FText& InTitle);

	/** Whether InText names one of the refused files */
	bool NamesRefusedFile(const FString& InText) const;

	/** The engine's original ModalMessageDialog binding, chained by OnModalMessageDialog */
	TDelegate<EAppReturnType::Type(EAppMsgCategory, EAppMsgType::Type, const FText&, const FText&)> PreviousModalMessageDialog;
	bool bBound = false;

	/** Package name and file name of each refused file - the engine's messages use both */
	TArray<FString> RefusedNameTokens;

	/** Whether the Message Log was already open when the first refusal happened; the flush
	 * only closes a tab our own noise opened */
	bool bMessageLogWasOpen = false;
};
