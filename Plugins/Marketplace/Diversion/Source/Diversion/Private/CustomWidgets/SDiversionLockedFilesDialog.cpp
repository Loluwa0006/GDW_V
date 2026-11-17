// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "SDiversionLockedFilesDialog.h"

#include "DiversionModule.h"
#include "DiversionProvider.h"
#include "DiversionState.h"
#include "DiversionUtils.h"
#include "DiversionValidityUtils.h"

#include "Framework/Application/SlateApplication.h"
#include "ISourceControlModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DiversionLockedFiles"

namespace
{
	// Not "ColumnId": SDiversionSaveDecisionDialog.cpp declares one in its own anonymous
	// namespace, and a unity build that lands both files in the same chunk (UE 5.3 compiles
	// this module as a single unity TU) merges the two into a redefinition of File
	namespace LockedFilesColumnId
	{
		const FName File("File");
		const FName Holder("Holder");
		const FName LockedSince("LockedSince");
		const FName Source("Source");
	}

	/** The list is repo-wide, so a second copy adds nothing */
	TWeakPtr<SWindow> OpenWindow;

	bool IsDiversionProviderActive()
	{
		ISourceControlModule& SCModule = ISourceControlModule::Get();
		return SCModule.IsEnabled() && SCModule.GetProvider().GetName() == "Diversion";
	}

	class SLockedFileTableRow : public SMultiColumnTableRow<TSharedPtr<FDiversionLockedFileRow>>
	{
	public:
		SLATE_BEGIN_ARGS(SLockedFileTableRow) {}
			SLATE_ARGUMENT(TSharedPtr<FDiversionLockedFileRow>, Row)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
		{
			Row = InArgs._Row;
			SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), InOwnerTable);
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			if (InColumnName == LockedFilesColumnId::File)
			{
				// Only column whose text can get cut short, so the only one worth a tooltip
				return SNew(STextBlock)
					.Margin(FMargin(4.f, 2.f))
					.Text(FText::FromString(Row->Path))
					.ToolTipText(FText::FromString(Row->Path));
			}

			if (InColumnName == LockedFilesColumnId::Holder)
			{
				return SNew(STextBlock)
					.Margin(FMargin(4.f, 2.f))
					.Text(Row->Holder)
					.ColorAndOpacity(Row->bLockedByOther ?
						FLinearColor(1.0f, 0.25f, 0.25f) : FLinearColor(0.3f, 0.85f, 0.3f));
			}

			if (InColumnName == LockedFilesColumnId::LockedSince)
			{
				// Blank, not "Jan 01, 1970", when the server supplied no timestamp
				return SNew(STextBlock)
					.Margin(FMargin(4.f, 2.f))
					.Text(Row->LockedAt > 0
						? DiversionUtils::FormatUnixTimestamp(Row->LockedAt)
						: FText::GetEmpty());
			}

			// Cased but not translated: an unrecognized value must not become a claim
			return SNew(STextBlock)
				.Margin(FMargin(4.f, 2.f))
				.Text(Row->Source.IsEmpty()
					? FText::GetEmpty()
					: FText::FromString(Row->Source.Left(1).ToUpper() + Row->Source.RightChop(1)));
		}

	private:
		TSharedPtr<FDiversionLockedFileRow> Row;
	};
}

void SDiversionLockedFilesDialog::Construct(const FArguments& InArgs)
{
	RefreshRows();

	StateChangedHandle = FDiversionModule::Get().GetProvider().RegisterSourceControlStateChanged_Handle(
		FSourceControlStateChanged::FDelegate::CreateSP(this, &SDiversionLockedFilesDialog::RefreshRows));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(4.f)
				// The subscription keeping these rows current is the Diversion provider's own,
				// so once it is gone they would sit frozen and still read as live
				.Visibility_Lambda([]()
					{
						return IsDiversionProviderActive() ? EVisibility::Visible : EVisibility::Collapsed;
					})
				[
					SAssignNew(ListView, SListView<TSharedPtr<FDiversionLockedFileRow>>)
					.ListItemsSource(&Rows)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SDiversionLockedFilesDialog::GenerateRow)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(LockedFilesColumnId::File)
							.DefaultLabel(LOCTEXT("Column_File", "File"))
							.FillWidth(0.52f)
						+ SHeaderRow::Column(LockedFilesColumnId::Holder)
							.DefaultLabel(LOCTEXT("Column_Holder", "Locked by"))
							.FillWidth(0.16f)
						+ SHeaderRow::Column(LockedFilesColumnId::LockedSince)
							.DefaultLabel(LOCTEXT("Column_LockedSince", "Locked since"))
							.FillWidth(0.18f)
						+ SHeaderRow::Column(LockedFilesColumnId::Source)
							.DefaultLabel(LOCTEXT("Column_Source", "Source"))
							.DefaultTooltip(LOCTEXT("Column_Source_Tooltip",
								"How the lock was acquired, as reported by the server."))
							.FillWidth(0.14f)
					)
				]
			]

			// An empty list, a dead list and a list that failed to load look identical otherwise
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]()
					{
						return (!IsDiversionProviderActive() || Rows.Num() == 0)
							? EVisibility::Visible : EVisibility::Collapsed;
					})
				.Text_Lambda([]()
					{
						if (!IsDiversionProviderActive())
						{
							return LOCTEXT("ProviderInactive",
								"Diversion is not the active revision control provider, so lock information is no longer being updated.");
						}
						// The menu entry unlocks on tier eligibility, which resolves well before
						// the poll that fetches the locks - until it lands, empty means unknown
						return FDiversionModule::Get().GetProvider().HasFetchedFullLockSet()
							? LOCTEXT("NoLocks", "No files are locked in this repository.")
							: LOCTEXT("LocksLoading", "Checking for locked files...");
					})
			]
		]
	];
}

SDiversionLockedFilesDialog::~SDiversionLockedFilesDialog()
{
	// Slate can outlive the module at editor shutdown, and Get() is checked
	if (StateChangedHandle.IsValid() && FDiversionModule::IsLoaded())
	{
		FDiversionModule::Get().GetProvider().UnregisterSourceControlStateChanged_Handle(StateChangedHandle);
	}
}

void SDiversionLockedFilesDialog::RefreshRows()
{
	if (!DiversionValidityUtils::DiversionValidityCheck(IsInGameThread(),
		"RefreshRows called outside of main thread"))
	{
		return;
	}

	FDiversionProvider& Provider = FDiversionModule::Get().GetProvider();

	// OnSourceControlStateChanged fires on every state update, not just lock changes, and
	// auto-locking makes a thousands-strong lock set realistic
	const uint32 NewRevision = Provider.GetLockedStatesRevision();
	if (NewRevision == RowsRevision)
	{
		return;
	}
	RowsRevision = NewRevision;

	TArray<TSharedRef<FDiversionState>> LockedStates;
	Provider.GetCurrentLockedStates(LockedStates);

	const FString WorkspaceRoot = Provider.GetPathToWorkspaceRoot();
	Rows.Reset(LockedStates.Num());
	for (const TSharedRef<FDiversionState>& State : LockedStates)
	{
		const FDiversionLockInfo LockInfo = State->FileLock.GetLockInfo();
		if (!LockInfo.IsSet())
		{
			continue;
		}

		TSharedPtr<FDiversionLockedFileRow> Row = MakeShared<FDiversionLockedFileRow>();
		Row->Path = DiversionUtils::ConvertFullPathToRelative(State->GetFilename(), WorkspaceRoot);
		Row->bLockedByOther = LockInfo.bLockedByOther;
		Row->Holder = LockInfo.bLockedByOther ? LockInfo.GetHolderText() : LOCTEXT("Holder_Me", "You");
		Row->LockedAt = LockInfo.LockedAt;
		Row->Source = LockInfo.LockSource;
		Rows.Add(Row);
	}

	// The cache is a map - without this the order shuffles on every rebuild
	Rows.Sort([](const TSharedPtr<FDiversionLockedFileRow>& A, const TSharedPtr<FDiversionLockedFileRow>& B)
		{
			return A->Path < B->Path;
		});

	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SDiversionLockedFilesDialog::GenerateRow(TSharedPtr<FDiversionLockedFileRow> InRow,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLockedFileTableRow, OwnerTable).Row(InRow);
}

void SDiversionLockedFilesDialog::Show()
{
	if (TSharedPtr<SWindow> Existing = OpenWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("LockedFilesTitle", "Diversion - Locked Files"))
		.ClientSize(FVector2D(760.f, 420.f))
		.AutoCenter(EAutoCenter::PreferredWorkArea)
		.SupportsMinimize(false).SupportsMaximize(false);

	Window->SetContent(SNew(SDiversionLockedFilesDialog));
	FSlateApplication::Get().AddWindow(Window);
	OpenWindow = Window;
}

#undef LOCTEXT_NAMESPACE
