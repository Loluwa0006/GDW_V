// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DiversionRefusedSaveNoise.h"
#include "DiversionState.h"
#include "DiversionSaveDecision.h"
#include "DiversionUtils.h"
#include "Misc/EngineVersionComparison.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtr.h"
#if !UE_VERSION_OLDER_THAN(5, 4, 0)
#include "IO/IoHash.h"
#endif

/**
 * What a package file's header records about the save that wrote it, and the same for the
 * package in memory: the saved hash from 5.4 (a random value per save until 5.5, a hash of the
 * file from 5.6), the package GUID before that. Equal values mean the package and its file
 * come from the same save.
 */
#if !UE_VERSION_OLDER_THAN(5, 4, 0)
using FDiversionPackageSaveId = FIoHash;
#else
using FDiversionPackageSaveId = FGuid;
#endif

/**
 * Before 5.6 every save renews the package's save id, including the autosave's backup copy
 * (SavePackage2: MakeNewGuid / a new saved hash unless SAVE_KeepGUID, which autosave never
 * passes). After an autosave the package no longer matches its own file, so the gate keeps the
 * id the file has itself. From 5.6 the id changes only when the package saves to its own file
 * (SavePackageUtilities IsUpdatingLoadedPath excludes autosaves), and the package's own id is
 * the reference.
 */
#define DIVERSION_TRACKS_FILE_SAVE_IDS UE_VERSION_OLDER_THAN(5, 6, 0)

class FDiversionProvider;
struct FDiversionSaveDecisionRow;

/**
 * Which packages were ticked (and not ticked) in the engine's Save Content dialog.
 *
 * The dialog module hands out raw pointers that dangle once a package is unloaded or GC'd,
 * so entries here are opaque values: never dereferenced, and never wrapped in FObjectKey or
 * FWeakObjectPtr (their constructors read the object - the same use-after-free). Address
 * reuse cannot mislead either: entries are only matched against live packages for a set the
 * gate sees for the first time, and the gate re-reads the dialog before every GC and after
 * every save, so a first-seen set contains no pointer that survived a GC.
 */
struct FDiversionSaveDialogResults
{
	TSet<const UPackage*> Checked;
	TSet<const UPackage*> Unchecked;
	bool operator==(const FDiversionSaveDialogResults& Other) const
	{
		return Checked.Num() == Other.Checked.Num() && Unchecked.Num() == Other.Unchecked.Num()
			&& Checked.Includes(Other.Checked) && Unchecked.Includes(Other.Unchecked);
	}
};

/**
 * The save gate: for every package the editor saves, decide to save it, ask the user first,
 * or refuse it (because the user chose Skip or Accept Incoming, or because nobody can answer
 * and the save would overwrite a change on disk). Hooks the engine's
 * IsPackageOKToSaveDelegate; owned by the provider, bound in Init(), unbound in Close().
 *
 * The question is answered inside the save call itself, so a save is never failed and
 * retried - several engine flows lose data when that happens (see ShouldNeverHoldSave).
 *
 * A save of many files asks once: the first file gathers the whole save, one dialog
 * collects every answer, and each answer is applied when its file reaches the gate. All of
 * this state belongs to one save and is cleared right after it (ResetSaveGesture).
 */
class FDiversionSaveDecisionGate
{
public:
	explicit FDiversionSaveDecisionGate(FDiversionProvider& InProvider)
		: Provider(InProvider)
	{
	}

	/** Calls Unbind(): every callback holding a raw `this` must be cleared even when the
	 * owner is destroyed without Close() (a test provider on the stack, an unusual shutdown). */
	~FDiversionSaveDecisionGate();

	/** Chain the engine's save gate and modal-dialog routing (no-op if already bound) */
	void Bind();

	/**
	 * Restore the engine's original delegates and drop all per-session state (no-op if not
	 * bound). Symmetric with Bind(); safe to call more than once.
	 */
	void Unbind();

	/**
	 * The gate itself, bound to FCoreUObjectDelegates::IsPackageOKToSaveDelegate - the one
	 * synchronous hook every non-cook save passes through. Calls the engine's original
	 * handler first. Lock state is verified against the backend per save, never trusted from
	 * the cache; if verification fails, the save is allowed - the server rejects commits of
	 * locked files regardless. Public because the wiring tests call it directly.
	 */
	bool IsPackageOKToSave(UPackage* InPackage, const FString& InFilename, FOutputDevice* ErrorLog);

	/**
	 * Saves that must always go through, no questions asked. Refusing these loses work or
	 * breaks an engine flow:
	 * - An open merge: finalizing saves inline and treats a failed save as fatal. For a file
	 *   changed on disk (bInChangedOnDisk), only the files the merge resolves.
	 * - A never-saved package: the engine renames a new map back on a failed save and the
	 *   level is lost. No lock can exist for an unwritten path anyway.
	 * - Auto-save: the editor's timer, not a user decision.
	 * - Play In Editor: the save belongs to the PIE flow. Not for a file changed on disk: PIE's
	 *   own saves never write over a package's loaded file, so that save is the user's.
	 * - The level instance being edited: its commit discards the edit on a failed save.
	 * The server still rejects commits of locked files, so nothing is unprotected.
	 */
	bool ShouldNeverHoldSave(UPackage* InPackage, const FString& InFullPath = FString(),
		bool bInChangedOnDisk = false) const;

	/** Whether the gate may question this save at all: inside the workspace (any path for a
	 * file changed on disk), not an autosave backup copy, and not one of the
	 * ShouldNeverHoldSave cases. Asked of the file being saved and of every predicted file, so
	 * both use the same rule. */
	bool IsGateableSave(UPackage* InPackage, const FString& InFullPath, bool bInChangedOnDisk = false) const;

	/**
	 * Whether saving InPackage to InFullPath would overwrite a version of the file this editor
	 * never loaded - a teammate's commit the sync wrote, a dv reset, a branch switch, another
	 * tool. Compares the save id in the file's header, read fresh, with the one the package was
	 * loaded or last saved with. Needs neither the agent nor the workspace. False for a new
	 * package, a save to any other path, and a file that is gone. Public because the disk-check
	 * tests call it directly.
	 */
	bool IsChangedOnDisk(const UPackage* InPackage, const FString& InFullPath) const;

	/** The save id the package in memory carries (see FDiversionPackageSaveId). Public because
	 * the disk-check tests report whether an autosave renewed it. */
	static FDiversionPackageSaveId GetPackageSaveId(const UPackage* InPackage);

	/** Files already toasted about in this save - at most one toast per file per save.
	 * Public because the wiring tests pre-seed it to stay UI-free under NullRHI. */
	TSet<FString> ToastedThisSave;

	/** Lock state already verified during this gesture, keyed by full path. A Save All over a
	 * World Partition level reaches the gate once per external actor package; without this,
	 * each one would pay its own synchronous round trip. Cleared with the gesture. Public
	 * because the wiring tests seed it in place of a backend lookup. */
	TMap<FString, FDiversionLockInfo> GestureVerifiedLocks;

	/** Answers the user gave for this save, keyed by full path. Filled for every file in the
	 * dialog, then consumed as each of those files reaches the gate. Files listed but never
	 * saved simply leave their answer unused. Public because the wiring tests seed a Skip in
	 * it as their veto instrument. */
	TMap<FString, DiversionUtils::EDiversionSaveChoice> GestureAnswers;

	/**
	 * Routes one already-lock-verified file. Returns the action, and on Prompt fills the
	 * category and holder the dialog row needs. Public because the routing tests observe
	 * the category a file gets from the provider's state.
	 */
	DiversionUtils::EDiversionSaveVetoAction EvaluateFile(const FString& InFullPath, bool bInInteractive,
		DiversionUtils::EDiversionSaveCategory& OutCategory, FDiversionLockInfo& OutLockInfo,
		FString& OutHolder) const;

private:
	/**
	 * Shows one dialog for everything in this save that needs a decision and returns whether
	 * InTriggerFullPath may proceed. The other answers wait in GestureAnswers until each
	 * file reaches the gate. Shows nothing when no file needs a decision.
	 *
	 * Runs on the save's first gateable file even when that file itself needs no decision:
	 * the engine saves files in its own order, and an unlisted file would already be on disk
	 * before the dialog opened - out of Cancel's reach.
	 *
	 * Modal on purpose: the engine needs its answer inside this call, and that is safe here -
	 * the gate runs before the package is written and before GC is locked (SavePackage2,
	 * ValidatePackage).
	 *
	 * The rest of the save is a prediction and can list files that will not be saved; an
	 * answer costs nothing until its file arrives - nothing is locked or saved for a file
	 * that never comes.
	 */
	bool PromptForSaveDecisions(const FString& InTriggerFullPath, const FString& InTriggerFilename,
		FOutputDevice* ErrorLog);

	/** The triggering file plus the predicted rest of the save, as dialog rows: the ones needing
	 * a decision first, then the ones listed only so the save is fully accounted for */
	TArray<TSharedPtr<FDiversionSaveDecisionRow>> BuildSaveDecisionRows(const FString& InTriggerFullPath);

	/** Runs the modal; returns false if the user cancelled */
	bool ShowSaveDecisionDialog(const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows);

	/** Stores the answers for this save */
	void RecordSaveDecisions(const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows, bool bInConfirmed);

	void SendSaveDecisionAnalytics(const TArray<TSharedPtr<FDiversionSaveDecisionRow>>& InRows, bool bInConfirmed);

	/** The other packages of this save, as full paths - exact when the save came through the
	 * engine's Save Content dialog, otherwise predicted from the dirty packages (which can
	 * overshoot). Records which case applies in bGestureListFromDialog. */
	TArray<FString> GatherPendingSaveCandidates(const FString& InTriggerFullPath);

	/** Builds the dialog row for a file that needs a decision */
	TSharedPtr<FDiversionSaveDecisionRow> MakeDecisionRow(const FString& InFullPath,
		DiversionUtils::EDiversionSaveCategory InCategory, const FDiversionLockInfo& InLockInfo,
		const FString& InHolder) const;

	/**
	 * Applies a chosen action and returns whether the save may proceed. Shared by the dialog
	 * answer and by an answer replayed from the gesture or the session.
	 */
	bool ApplySaveChoice(const FString& InFullPath, const FString& InDisplayName,
		DiversionUtils::EDiversionSaveChoice InChoice, FOutputDevice* ErrorLog);

	/** Takes the hard lock for one file. A lost race demotes the choice to Skip; any other
	 * failure demotes to saving without the lock. */
	DiversionUtils::EDiversionSaveChoice AcquireLockForSave(const FString& InFullPath,
		const FString& InDisplayName);

	/**
	 * Arms a one-shot ticker that clears the per-save state after the save ends. The ticker
	 * cannot fire during the save: a save runs as one synchronous loop that never returns to
	 * the ticker, and the modal dialog's loop does not pump it either.
	 */
	void ScheduleSaveGestureReset();
	bool ResetSaveGesture(float DeltaTime);

	/** Toast about this file at most once per save */
	void ToastOncePerSave(const FString& InFullPath, const FText& InMessage);

	/** Reloads every file whose save was answered with Accept Incoming. Runs after the save:
	 * a package cannot be reloaded while it is being saved. */
	void ReloadAcceptedIncoming();

	/** IsChangedOnDisk, read once per file within one gate call (DiskCheckCache) */
	bool IsChangedOnDiskCached(const UPackage* InPackage, const FString& InFullPath) const;

	/**
	 * Asks about a change on disk that appeared after the file's question was answered - the
	 * sync does not wait for a dialog. Shows the changed-on-disk row for this file alone, or
	 * refuses the save when nobody can answer. Returns whether the save may proceed.
	 */
	bool AskAboutLateChangeOnDisk(const FString& InFullPath, const FString& InDisplayName, FOutputDevice* ErrorLog);

	/** The save id the package's file was written with, as far as the editor knows */
	FDiversionPackageSaveId GetExpectedFileSaveId(const UPackage* InPackage) const;

#if DIVERSION_TRACKS_FILE_SAVE_IDS
	/** Records the package's save id before its first save can renew it - until then it is the
	 * id its file was loaded from */
	void RememberFileSaveId(const UPackage* InPackage);

	/** After a save to the package's own file, the file holds the package's new id */
	void OnPackageSaved(const FString& InFilename, UPackage* InPackage, FObjectPostSaveContext InSaveContext);

	/** Save id of each package's own file, kept across saves (see DIVERSION_TRACKS_FILE_SAVE_IDS).
	 * Keyed by package object, so a reloaded package starts over. */
	TMap<TWeakObjectPtr<const UPackage>, FDiversionPackageSaveId> FileSaveIds;
	FDelegateHandle PackageSavedHandle;
#endif

	/**
	 * Refuses one save: records it so the engine's error reporting for it can be cleaned up,
	 * toasts InToast when one is given, and returns false so callers can `return RefuseSave(...)`.
	 */
	bool RefuseSave(const FString& InFilename, FOutputDevice* ErrorLog,
		const FText& InToast = FText::GetEmpty());

	/** The provider that owns this gate; outlives it by construction */
	FDiversionProvider& Provider;

	/** Hides the engine's error reporting for the saves this gate refuses */
	FDiversionRefusedSaveNoise RefusedSaveNoise;

	/** The engine's original IsPackageOKToSaveDelegate binding, chained by IsPackageOKToSave */
	FCoreUObjectDelegates::FIsPackageOKToSaveDelegate PreviousIsPackageOKToSaveDelegate;
	bool bIsPackageOKToSaveDelegateBound = false;

	/** Consumes the dialog results before every GC, so a "fresh" set can never span one
	 * (the liveness guarantee FDiversionSaveDialogResults relies on). Bound in Bind(). */
	FDelegateHandle PreGarbageCollectHandle;

	/** The engine's Save Content dialog results as last read by this gate. Outlives the
	 * gesture on purpose: the engine keeps results until the next dialog, so a set that was
	 * already read once belongs to an earlier save (see GatherPendingSaveCandidates). */
	FDiversionSaveDialogResults ConsumedDialogResults;

#pragma region PerSaveGestureState
	/** Everything below is scoped to one save gesture and cleared by ResetSaveGesture */

	/** Whether this save has already been looked at as a whole - the first gateable file to
	 * arrive gathers the rest, and either shows the dialog or finds nothing needs one. A file
	 * that arrives after that, one the prediction missed, gets its own prompt rather than none. */
	bool bGestureEvaluated = false;

	/** Set when the user cancelled this save's dialog: every file of the save is refused,
	 * including any the prediction missed and therefore has no recorded answer for */
	bool bGestureCancelled = false;

	/** Whether this save's file list is the user's own selection from the engine's Save
	 * Content dialog (exact) rather than the dirty-package prediction (may list files the
	 * user left out). Decides whether the dialog shows its prediction disclaimer. */
	bool bGestureListFromDialog = false;

	/** Set once the backend fails a lookup: the rest of the gesture then fails open instead
	 * of paying a connect timeout per file */
	bool bGestureLockRefreshFailed = false;

	/** The same, for taking a lock rather than reading one */
	bool bGestureLockAcquireFailed = false;

	/** Files answered with Accept Incoming in this save, reloaded once it is over */
	TArray<FString> AcceptedIncomingPaths;

	/** Files whose answer in this save was given about their change on disk. Any other answer
	 * that would save is checked against the disk again before it applies. */
	TSet<FString> GestureDiskAnswers;

	/** Whether each file was changed on disk, as read during the current gate call. One call
	 * asks about a file several times (its own check, its routing, and every predicted file
	 * of a Save All); one header read each is enough. Emptied when each call starts and
	 * whenever a dialog closes - the sync does not stop while a save runs or a dialog is open. */
	mutable TMap<FString, bool> DiskCheckCache;

	bool bSaveGestureResetPending = false;
	FTSTicker::FDelegateHandle SaveGestureResetTickerHandle;
#pragma endregion
};
