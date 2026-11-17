// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * One hard-locked file, copied out of the provider's cache so the window holds no state refs.
 * Raw values rather than display strings, so formatting costs a screenful and not a repo.
 */
struct FDiversionLockedFileRow
{
	/** Repo-relative: locks cover Config/, Source/ and binaries with no asset path */
	FString Path;
	FText Holder;
	/** Epoch seconds; 0 when the server sent none */
	int64 LockedAt = 0;
	/** LockSource verbatim - what the server means by each value is not defined client-side */
	FString Source;
	bool bLockedByOther = false;
};

/**
 * Repo-wide list of every hard lock currently held. Read-only, non-modal, single-instance.
 *
 * Issues no requests of its own: the potential-clashes poll already fetches the full repo lock
 * set, so this rebuilds off OnSourceControlStateChanged. Request volume is identical whether
 * the window is open or closed.
 */
class SDiversionLockedFilesDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDiversionLockedFilesDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDiversionLockedFilesDialog() override;

	/** Opens the window, or focuses the one already open */
	static void Show();

private:
	void RefreshRows();

	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<FDiversionLockedFileRow> InRow,
		const TSharedRef<class STableViewBase>& OwnerTable);

	TArray<TSharedPtr<FDiversionLockedFileRow>> Rows;
	TSharedPtr<SListView<TSharedPtr<FDiversionLockedFileRow>>> ListView;
	FDelegateHandle StateChangedHandle;
	uint32 RowsRevision = 0;
};
