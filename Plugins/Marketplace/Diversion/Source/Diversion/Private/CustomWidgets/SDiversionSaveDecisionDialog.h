// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Views/SListView.h"
#include "SaveDecision/DiversionSaveDecision.h"
#include "DiversionUtils.h"

/**
 * One file awaiting a save decision. The save gate fills the inputs, the dialog mutates the
 * outputs in place, and the gate reads them back once the modal returns
 * (see FDiversionSaveDecisionGate::PromptForSaveDecision).
 */
struct FDiversionSaveDecisionRow
{
	FString FullPath;
	FString PackageName;
	FString AssetDisplayName;
	DiversionUtils::EDiversionSaveCategory Category = DiversionUtils::EDiversionSaveCategory::UnlockedLockable;
	/** Lock holder (hard) or other-editors list (soft); empty for plain unlocked files */
	FString Holder;
	int64 LockedAt = 0;
	/** False for hard-locked-by-other and changed-on-disk rows and on tiers without hard
	 * locks - hides Lock & Save */
	bool bLockOfferAvailable = true;
	/** True for a changed-on-disk row whose file can be reloaded on its own (not a level or
	 * one of its actor packages) - shows Accept Incoming */
	bool bAcceptIncomingAvailable = false;

	/**
	 * False for a file the save handles on its own - already locked by you, or locked for you
	 * by the repo's rule. Those rows are listed without any control, purely so
	 * the dialog accounts for every file in the save. Otherwise a file that quietly succeeded
	 * and a file the editor forgot would look identical.
	 */
	bool bDecisionRequired = true;
	/** What will happen to an informational row, shown in place of the choice control */
	FText OutcomeText;

	// Output (pre-set to the per-category default, mutated by the dialog)
	DiversionUtils::EDiversionSaveChoice Choice = DiversionUtils::EDiversionSaveChoice::LockAndSave;
};

/**
 * The rows that share one situation (lock state), and therefore one group-level answer.
 * Built by the dialog from the flat row list; the gate never sees groups.
 */
struct FDiversionSaveDecisionGroup
{
	DiversionUtils::EDiversionSaveCategory Category = DiversionUtils::EDiversionSaveCategory::UnlockedLockable;
	bool bDecisionRequired = true;
	FText Title;
	TArray<TSharedPtr<FDiversionSaveDecisionRow>> Rows;
	/** Whether the group header offers Lock & Save (small groups of lockable files only -
	 * the lock API takes one path per call, so a group lock is one blocking request per file) */
	bool bOffersLock = false;
	/** Whether the group header offers Accept Incoming (every row can be reloaded) */
	bool bOffersAcceptIncoming = false;
	/** What the header control keeps showing while rows disagree with each other */
	DiversionUtils::EDiversionSaveChoice LastSetChoice = DiversionUtils::EDiversionSaveChoice::SaveUnlocked;
};

/** One list entry: a group header (Group set, Row null) or one file under it (Row set) */
struct FDiversionSaveDecisionListItem
{
	TSharedPtr<FDiversionSaveDecisionGroup> Group;
	TSharedPtr<FDiversionSaveDecisionRow> Row;
};

/**
 * The save-decision dialog, grouped by situation: every file in one lock state sits under a
 * single header carrying that group's choice, so a save of any size is at most a handful of
 * decisions - one per situation, not one per file. Individual rows keep their own control
 * and can depart from the group's answer; the header then reads "(mixed)".
 *
 * Grouping is also what keeps the dangerous case visible: files locked by someone else are
 * their own group with their own header and count, so they can neither disappear into a
 * count nor be flipped by a bulk action meant for the routine files.
 *
 * Modal; Cancel (or closing the window) means WasConfirmed() == false and the caller treats
 * every row as Skip.
 */
class SDiversionSaveDecisionDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDiversionSaveDecisionDialog)
		: _ListIsExact(false)
		, _ShowNoAutoLockRulesWarning(false)
	{}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(TArray<TSharedPtr<FDiversionSaveDecisionRow>>, Rows)
		/** True when Rows is exactly the save - the user's own selection - rather than a
		 * prediction that may list files the user left out. Hides the disclaimer. */
		SLATE_ARGUMENT(bool, ListIsExact)
		/** True when the repo has no auto-lock rules - a lock taken here is never released
		 * automatically. Shows a warning at the top of the dialog. */
		SLATE_ARGUMENT(bool, ShowNoAutoLockRulesWarning)
		/** The repo whose hard-lock settings the warning links to; empty hides that link */
		SLATE_ARGUMENT(FString, RepoId)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** True only when the user pressed Confirm - the rows' outputs are valid to apply */
	bool WasConfirmed() const { return bConfirmed; }

private:
	/** Buckets the flat row list into groups and flattens them back into list items.
	 * A single-file dialog gets no header - a group of one is noise. */
	void BuildGroupedListItems();

	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FDiversionSaveDecisionListItem> InItem,
		const TSharedRef<class STableViewBase>& OwnerTable);

	FReply OnConfirmClicked();
	FReply OnCancelClicked();
	void CloseWindow();

	TWeakPtr<SWindow> ParentWindow;
	TArray<TSharedPtr<FDiversionSaveDecisionRow>> Rows;
	TArray<TSharedPtr<FDiversionSaveDecisionListItem>> ListItems;
	TSharedPtr<SListView<TSharedPtr<FDiversionSaveDecisionListItem>>> ListView;
	bool bConfirmed = false;
};
