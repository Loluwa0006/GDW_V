// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "SDiversionSaveDecisionDialog.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DiversionConstants.h"
#include "DiversionUtils.h"
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Styling/StyleColors.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DiversionSaveDecisionDialog"

namespace
{
	using DiversionUtils::EDiversionSaveCategory;
	using DiversionUtils::EDiversionSaveChoice;

	namespace ColumnId
	{
		static const FName Asset("Asset");
		static const FName File("File");
		static const FName Status("Status");
		static const FName Action("Action");
	}

	/** Above this many files, a group's header stops offering Lock & Save: the lock API takes
	 * one path per call, so a group-level lock is one blocking request per file inside the
	 * save. Individual rows still offer it. */
	constexpr int32 MaxGroupLockOffer = 25;

	FText GetStatusText(const FDiversionSaveDecisionRow& Row)
	{
		switch (Row.Category)
		{
		case EDiversionSaveCategory::HardLockedByOther:
			return FText::Format(LOCTEXT("Status_HardLocked", "Locked by {0} since {1}"),
				FText::FromString(Row.Holder),
				DiversionUtils::FormatUnixTimestamp(Row.LockedAt));
		case EDiversionSaveCategory::SoftLocked:
			return FText::Format(LOCTEXT("Status_SoftLocked", "Being edited by {0}"),
				FText::FromString(Row.Holder));
		case EDiversionSaveCategory::LockedByMe:
			return Row.LockedAt > 0
				? FText::Format(LOCTEXT("Status_LockedByMe", "Locked by you since {0}"),
					DiversionUtils::FormatUnixTimestamp(Row.LockedAt))
				: LOCTEXT("Status_LockedByMeNoDate", "Locked by you");
		case EDiversionSaveCategory::AutoLockedByRule:
			return LOCTEXT("Status_AutoLockedByRule", "Locks on save (repo rule)");
		case EDiversionSaveCategory::ChangedOnDisk:
			return LOCTEXT("Status_ChangedOnDisk", "Changed on disk");
		default:
			return LOCTEXT("Status_Unlocked", "Not locked");
		}
	}

	/** What this file's lock state means for saving - the "why", with no identity in it, so it
	 * can sit under the Status and Action cells without repeating what they already show */
	FText GetCategoryExplanation(const FDiversionSaveDecisionRow& Row)
	{
		if (Row.Category == EDiversionSaveCategory::ChangedOnDisk)
		{
			return Row.bAcceptIncomingAvailable
				? LOCTEXT("Tooltip_ChangedOnDisk",
					"This file changed on disk after you opened it, most likely a teammate's change that Diversion synced. Save Mine overwrites it (their version stays in the history), Accept Incoming drops your unsaved changes and loads it, Skip keeps your changes unsaved.")
				: LOCTEXT("Tooltip_ChangedOnDiskLevel",
					"This file changed on disk after you opened it, most likely a teammate's change that Diversion synced. Save Mine overwrites it (their version stays in the history), Skip keeps your changes unsaved. A level can't be reloaded one file at a time, so there is no Accept Incoming here.");
		}
		if (Row.Category == EDiversionSaveCategory::HardLockedByOther)
		{
			return FText::Format(LOCTEXT("Tooltip_HardLocked",
				"Saving will sync your changes to your workspace as usual, but committing this file stays blocked while {0} holds the lock."),
				FText::FromString(Row.Holder));
		}
		if (Row.Category == EDiversionSaveCategory::SoftLocked)
		{
			return LOCTEXT("Tooltip_SoftLocked",
				"Saving may create a conflict with the other workspace's changes. Locking claims the file so nobody else can commit it.");
		}
		if (Row.Category == EDiversionSaveCategory::LockedByMe)
		{
			return LOCTEXT("Tooltip_LockedByMe",
				"You already hold this file's lock, so it saves without asking. The lock releases when you commit.");
		}
		if (Row.Category == EDiversionSaveCategory::AutoLockedByRule)
		{
			return LOCTEXT("Tooltip_AutoLockedByRule",
				"This file matches the repository's lockable patterns, so saving locks it for you automatically. The lock releases when you commit.");
		}
		return LOCTEXT("Tooltip_Unlocked",
			"Locking claims the file so nobody else can commit it until you release the lock.");
	}

	/** The group-level version of the explanation - no single holder to name */
	FText GetGroupExplanation(const FDiversionSaveDecisionGroup& Group)
	{
		switch (Group.Category)
		{
		case EDiversionSaveCategory::HardLockedByOther:
			return LOCTEXT("GroupTooltip_HardLocked",
				"These files are locked by other users. Saving syncs your changes to your workspace as usual, but committing them stays blocked until each lock is released.");
		case EDiversionSaveCategory::SoftLocked:
			return LOCTEXT("GroupTooltip_SoftLocked",
				"Other workspaces have unpublished edits to these files - saving may create conflicts. Locking claims a file so nobody else can commit it.");
		case EDiversionSaveCategory::LockedByMe:
			return LOCTEXT("GroupTooltip_LockedByMe",
				"You hold these locks - the files save without asking. Locks release when you commit.");
		case EDiversionSaveCategory::AutoLockedByRule:
			return LOCTEXT("GroupTooltip_AutoLockedByRule",
				"These files match the repository's lockable patterns: saving locks them for you automatically, so there is nothing to decide.");
		case EDiversionSaveCategory::ChangedOnDisk:
			return LOCTEXT("GroupTooltip_ChangedOnDisk",
				"These files changed on disk after you opened them, most likely teammates' changes that Diversion synced. Saving overwrites them; their versions stay in the history.");
		default:
			return LOCTEXT("GroupTooltip_Unlocked",
				"Locking claims a file so nobody else can commit it until you release the lock.");
		}
	}

	FText GetGroupTitle(EDiversionSaveCategory Category)
	{
		switch (Category)
		{
		case EDiversionSaveCategory::HardLockedByOther:
			return LOCTEXT("Group_HardLocked", "Locked by others");
		case EDiversionSaveCategory::SoftLocked:
			return LOCTEXT("Group_SoftLocked", "Being edited elsewhere");
		case EDiversionSaveCategory::LockedByMe:
			return LOCTEXT("Group_LockedByMe", "Locked by you");
		case EDiversionSaveCategory::AutoLockedByRule:
			return LOCTEXT("Group_AutoLockedByRule", "Locked on save by repo rule");
		case EDiversionSaveCategory::ChangedOnDisk:
			return LOCTEXT("Group_ChangedOnDisk", "Changed on disk");
		default:
			return LOCTEXT("Group_Unlocked", "Not locked");
		}
	}

	/** Fixed presentation order: the groups that need a decision first, dangerous first */
	int32 GetGroupSortOrder(const FDiversionSaveDecisionGroup& Group)
	{
		const int32 CategoryOrder =
			Group.Category == EDiversionSaveCategory::ChangedOnDisk ? 0 :
			Group.Category == EDiversionSaveCategory::HardLockedByOther ? 1 :
			Group.Category == EDiversionSaveCategory::SoftLocked ? 2 :
			Group.Category == EDiversionSaveCategory::UnlockedLockable ? 3 :
			Group.Category == EDiversionSaveCategory::LockedByMe ? 4 : 5;
		return (Group.bDecisionRequired ? 0 : 10) + CategoryOrder;
	}

	/** The choice every row in the group agrees on, or unset while rows disagree */
	TOptional<EDiversionSaveChoice> GetGroupUniformChoice(const FDiversionSaveDecisionGroup& Group)
	{
		TOptional<EDiversionSaveChoice> Uniform;
		for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : Group.Rows)
		{
			if (!Uniform.IsSet())
			{
				Uniform = Row->Choice;
			}
			else if (Uniform.GetValue() != Row->Choice)
			{
				return {};
			}
		}
		return Uniform;
	}

	/** The uniform choice as far as the group HEADER may show it: unset both when the rows
	 * disagree and when they agree on a choice the header has no segment for (every row
	 * hand-set to Lock & Save in a group too large to offer it, or to Accept Incoming in a
	 * group with a level among its files). Both render as "(mixed)" - the control must never
	 * select a segment that contradicts the rows. */
	TOptional<EDiversionSaveChoice> GetGroupHeaderChoice(const FDiversionSaveDecisionGroup& Group)
	{
		TOptional<EDiversionSaveChoice> Uniform = GetGroupUniformChoice(Group);
		if (Uniform.IsSet() &&
			((Uniform.GetValue() == EDiversionSaveChoice::LockAndSave && !Group.bOffersLock) ||
				(Uniform.GetValue() == EDiversionSaveChoice::AcceptIncoming && !Group.bOffersAcceptIncoming)))
		{
			Uniform.Reset();
		}
		return Uniform;
	}

	/** A cell's own text, with the explanation underneath. Cells are narrower than their
	 * content - the file path and the lock holder both truncate at this dialog's width - so
	 * every cell carries its full text rather than a shared row-level message. */
	FText MakeCellTooltip(const FText& InCellText, const FText& InExplanation)
	{
		return FText::Format(LOCTEXT("CellTooltip", "{0}\n\n{1}"), InCellText, InExplanation);
	}

	/** "Save" reads differently per category: plain files save without a lock, clashed and
	 * locked files save despite the warning, a file changed on disk saves your version over
	 * the incoming one */
	FText GetSaveChoiceLabel(EDiversionSaveCategory Category)
	{
		switch (Category)
		{
		case EDiversionSaveCategory::UnlockedLockable:
			return LOCTEXT("Choice_SaveWithoutLock", "Save Without Lock");
		case EDiversionSaveCategory::ChangedOnDisk:
			return LOCTEXT("Choice_SaveMine", "Save Mine");
		default:
			return LOCTEXT("Choice_SaveAnyway", "Save Anyway");
		}
	}

	/** Class icon for the asset inside the package, the same way the engine's dialogs show it */
	const FSlateBrush* GetAssetIcon(const FString& PackageName)
	{
		UClass* AssetClass = nullptr;
		if (!PackageName.IsEmpty() && FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			FAssetRegistryModule& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> Assets;
			AssetRegistry.Get().GetAssetsByPackageName(FName(*PackageName), Assets);
			if (Assets.Num() > 0)
			{
				AssetClass = Assets[0].GetClass();
			}
		}
		return FSlateIconFinder::FindIconForClass(AssetClass).GetIcon();
	}

	/** One file row; columns are generated on demand to match the header layout */
	class SDiversionSaveDecisionRowWidget : public SMultiColumnTableRow<TSharedPtr<FDiversionSaveDecisionListItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SDiversionSaveDecisionRowWidget)
			: _Indent(false)
		{}
			SLATE_ARGUMENT(TSharedPtr<FDiversionSaveDecisionRow>, Item)
			/** True when the row sits under a group header, so it reads as the header's child */
			SLATE_ARGUMENT(bool, Indent)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
		{
			Item = InArgs._Item;
			bIndent = InArgs._Indent;
			SMultiColumnTableRow<TSharedPtr<FDiversionSaveDecisionListItem>>::Construct(
				FSuperRowType::FArguments().Padding(FMargin(0.f, 2.f)), InOwnerTable);
			// Fallback for the gaps between cells; each cell overrides it with its own content
			SetToolTipText(GetCategoryExplanation(*Item));
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			// Every text cell reports its own full contents on hover - the dialog is narrower
			// than the paths and holder names it shows, so a truncated cell must still be
			// readable without resizing the window
			if (InColumnName == ColumnId::Asset)
			{
				return SNew(SBox).VAlign(VAlign_Center)
					.Padding(FMargin(bIndent ? 18.f : 4.f, 0.f, 0.f, 0.f))
					.ToolTipText(MakeCellTooltip(FText::FromString(Item->AssetDisplayName),
						GetCategoryExplanation(*Item)))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(SImage).Image(GetAssetIcon(Item->PackageName))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(Item->AssetDisplayName))
						]
					];
			}
			if (InColumnName == ColumnId::File)
			{
				return SNew(SBox).VAlign(VAlign_Center).Padding(4.f, 0.f)
					.ToolTipText(MakeCellTooltip(FText::FromString(Item->PackageName),
						GetCategoryExplanation(*Item)))
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item->PackageName))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					];
			}
			if (InColumnName == ColumnId::Status)
			{
				return SNew(SBox).VAlign(VAlign_Center).Padding(4.f, 0.f)
					.ToolTipText(MakeCellTooltip(GetStatusText(*Item), GetCategoryExplanation(*Item)))
					[
						SNew(STextBlock)
						.Text(GetStatusText(*Item))
						.ColorAndOpacity(Item->Category == EDiversionSaveCategory::UnlockedLockable
							? FSlateColor::UseSubduedForeground()
							: FSlateColor(FStyleColors::Warning))
					];
			}
			if (InColumnName == ColumnId::Action)
			{
				TSharedPtr<FDiversionSaveDecisionRow> Row = Item;
				if (!Row->bDecisionRequired)
				{
					// Nothing to choose - the row is here so the save is fully accounted for
					return SNew(SBox).VAlign(VAlign_Center).Padding(4.f, 0.f)
						.ToolTipText(GetCategoryExplanation(*Item))
						[
							SNew(STextBlock)
							.Text(Row->OutcomeText)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						];
				}
				TSharedRef<SSegmentedControl<EDiversionSaveChoice>> ChoiceControl =
					SNew(SSegmentedControl<EDiversionSaveChoice>)
					.Value_Lambda([Row]() { return Row->Choice; })
					.OnValueChanged_Lambda([Row](EDiversionSaveChoice NewChoice) { Row->Choice = NewChoice; });

				if (Row->bLockOfferAvailable)
				{
					ChoiceControl->AddSlot(EDiversionSaveChoice::LockAndSave)
						.Text(LOCTEXT("Choice_LockAndSave", "Lock & Save"));
				}
				ChoiceControl->AddSlot(EDiversionSaveChoice::SaveUnlocked)
					.Text(GetSaveChoiceLabel(Row->Category));
				if (Row->bAcceptIncomingAvailable)
				{
					ChoiceControl->AddSlot(EDiversionSaveChoice::AcceptIncoming)
						.Text(LOCTEXT("Choice_AcceptIncoming", "Accept Incoming"));
				}
				ChoiceControl->AddSlot(EDiversionSaveChoice::Skip)
					.Text(LOCTEXT("Choice_Skip", "Skip"));

				return SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Fill).Padding(4.f, 2.f)
					.ToolTipText(GetCategoryExplanation(*Item))
					[
						ChoiceControl
					];
			}
			return SNullWidget::NullWidget;
		}

	private:
		TSharedPtr<FDiversionSaveDecisionRow> Item;
		bool bIndent = false;
	};

	/** A group header row. Built on the same column layout as the file rows, so the group's
	 * choice control sits in the Action column and lines up exactly with the per-row controls
	 * beneath it. A group of one file gets no control - its single row already carries the
	 * choice, and a second copy of it would just be noise. */
	class SDiversionSaveDecisionGroupHeaderWidget : public SMultiColumnTableRow<TSharedPtr<FDiversionSaveDecisionListItem>>
	{
	public:
		SLATE_BEGIN_ARGS(SDiversionSaveDecisionGroupHeaderWidget) {}
			SLATE_ARGUMENT(TSharedPtr<FDiversionSaveDecisionGroup>, Group)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable)
		{
			Group = InArgs._Group;
			SMultiColumnTableRow<TSharedPtr<FDiversionSaveDecisionListItem>>::Construct(
				FSuperRowType::FArguments().Padding(FMargin(0.f, 10.f, 0.f, 2.f)).ShowSelection(false),
				InOwnerTable);
			SetToolTipText(GetGroupExplanation(*Group));
		}

		virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
		{
			if (InColumnName == ColumnId::Asset)
			{
				return SNew(SBox).VAlign(VAlign_Center).Padding(4.f, 0.f)
					[
						SNew(STextBlock)
						.Text(FText::Format(LOCTEXT("GroupTitleFmt", "{0} ({1})"),
							Group->Title, FText::AsNumber(Group->Rows.Num())))
						.Font(FAppStyle::Get().GetFontStyle("NormalFontBold"))
					];
			}
			if (InColumnName == ColumnId::Action && Group->bDecisionRequired && Group->Rows.Num() > 1)
			{
				TSharedPtr<FDiversionSaveDecisionGroup> LocalGroup = Group;
				TSharedRef<SSegmentedControl<EDiversionSaveChoice>> GroupControl =
					SNew(SSegmentedControl<EDiversionSaveChoice>)
					.Value_Lambda([LocalGroup]()
						{
							const TOptional<EDiversionSaveChoice> Header = GetGroupHeaderChoice(*LocalGroup);
							return Header.IsSet() ? Header.GetValue() : LocalGroup->LastSetChoice;
						})
					.OnValueChanged_Lambda([LocalGroup](EDiversionSaveChoice NewChoice)
						{
							LocalGroup->LastSetChoice = NewChoice;
							for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : LocalGroup->Rows)
							{
								if ((NewChoice == EDiversionSaveChoice::LockAndSave && !Row->bLockOfferAvailable) ||
									(NewChoice == EDiversionSaveChoice::AcceptIncoming && !Row->bAcceptIncomingAvailable))
								{
									continue;
								}
								Row->Choice = NewChoice;
							}
						});

				if (Group->bOffersLock)
				{
					GroupControl->AddSlot(EDiversionSaveChoice::LockAndSave)
						.Text(LOCTEXT("Choice_LockAndSave", "Lock & Save"));
				}
				GroupControl->AddSlot(EDiversionSaveChoice::SaveUnlocked)
					.Text(GetSaveChoiceLabel(Group->Category));
				if (Group->bOffersAcceptIncoming)
				{
					GroupControl->AddSlot(EDiversionSaveChoice::AcceptIncoming)
						.Text(LOCTEXT("Choice_AcceptIncoming", "Accept Incoming"));
				}
				GroupControl->AddSlot(EDiversionSaveChoice::Skip)
					.Text(LOCTEXT("Choice_Skip", "Skip"));

				return SNew(SBox).VAlign(VAlign_Center).HAlign(HAlign_Fill).Padding(4.f, 2.f)
					[
						SNew(SHorizontalBox)
						// The header stops speaking for its rows the moment they disagree
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("GroupMixed", "(mixed)"))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Visibility_Lambda([LocalGroup]()
								{
									return GetGroupHeaderChoice(*LocalGroup).IsSet()
										? EVisibility::Collapsed : EVisibility::Visible;
								})
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							GroupControl
						]
					];
			}
			return SNullWidget::NullWidget;
		}

	private:
		TSharedPtr<FDiversionSaveDecisionGroup> Group;
	};
}

void SDiversionSaveDecisionDialog::Construct(const FArguments& InArgs)
{
	ParentWindow = InArgs._ParentWindow;
	Rows = InArgs._Rows;
	BuildGroupedListItems();

	const bool bMultiple = Rows.Num() > 1;
	// A dialog that only asks about changes on disk has no lock to explain. Any other question
	// is about a lock, offered or held by someone else.
	const bool bAnyLockQuestion = Rows.ContainsByPredicate([](const TSharedPtr<FDiversionSaveDecisionRow>& Row)
		{
			return Row->bDecisionRequired && Row->Category != EDiversionSaveCategory::ChangedOnDisk;
		});
	const FText HeaderText = bAnyLockQuestion
		? (bMultiple
			? LOCTEXT("HeaderText",
				"Choose what to do with each group of files before saving. Locking a file prevents others from committing it until you release the lock.")
			: LOCTEXT("HeaderTextSingle",
				"Choose what to do with this file before saving. Locking it prevents others from committing it until you release the lock."))
		: (bMultiple
			? LOCTEXT("HeaderTextNoLock", "Choose what to do with each group of files before saving.")
			: LOCTEXT("HeaderTextSingleNoLock", "Choose what to do with this file before saving."));

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(16.f)
		[
			SNew(SVerticalBox)

			// Without auto-lock rules nothing releases a lock taken here - say so before the
			// user picks Lock & Save
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SBorder)
				.Visibility(InArgs._ShowNoAutoLockRulesWarning ? EVisibility::Visible : EVisibility::Collapsed)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SImage).Image(FAppStyle::GetBrush("Icons.WarningWithColor"))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("NoAutoLockRulesWarning",
							"This repository has no auto-lock rules. Without them, a lock is not released automatically - you will need to unlock the file manually."))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(SHyperlink)
						.Text(LOCTEXT("NoAutoLockRulesMoreInfo", "More info"))
						.ToolTipText(LOCTEXT("NoAutoLockRulesMoreInfoTooltip", "Learn about auto-lock rules"))
						.OnNavigate_Lambda([]()
						{
							FPlatformProcess::LaunchURL(TEXT("https://docs.diversion.dev/hard-locks?utm_source=ue-plugin&utm_medium=plugin#auto-lock"), nullptr, nullptr);
						})
					]
					// Shown to everyone: only admins can edit the rules, but anyone can view them
					// and pass the page on
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
					[
						SNew(SHyperlink)
						.Visibility(InArgs._RepoId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						.Text(LOCTEXT("AutoLockSettings", "Auto-lock settings"))
						.ToolTipText(LOCTEXT("AutoLockSettingsTooltip", "Open this repository's hard-lock settings in Diversion"))
						.OnNavigate_Lambda([RepoId = InArgs._RepoId]()
						{
							// Repo IDs (dv.repo.<uuid>) are URL-safe as-is
							const FString Url = FString(TEXT(DIVERSION_WEB_URL "settings/hard-locks?utm_source=ue-plugin&utm_medium=plugin&repoId="))
								+ RepoId;
							FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
						})
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(HeaderText)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
				[
					SNew(SHyperlink)
					.Visibility(bAnyLockQuestion ? EVisibility::Visible : EVisibility::Collapsed)
					.Text(LOCTEXT("HardLocksMoreInfo", "More info"))
					.ToolTipText(LOCTEXT("HardLocksMoreInfoTooltip", "Learn about hard locks"))
					.OnNavigate_Lambda([]()
					{
						FPlatformProcess::LaunchURL(TEXT("https://docs.diversion.dev/hard-locks?utm_source=ue-plugin&utm_medium=plugin#hard-locks"), nullptr, nullptr);
					})
				]
			]

			// File table - recessed panel like the engine's checkout dialog
			+ SVerticalBox::Slot().FillHeight(1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(4.f)
				[
					SAssignNew(ListView, SListView<TSharedPtr<FDiversionSaveDecisionListItem>>)
					.ListItemsSource(&ListItems)
					.SelectionMode(ESelectionMode::None)
					.OnGenerateRow(this, &SDiversionSaveDecisionDialog::GenerateRow)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(ColumnId::Asset)
							.DefaultLabel(LOCTEXT("Column_Asset", "Asset"))
							.FillWidth(0.22f)
						+ SHeaderRow::Column(ColumnId::File)
							.DefaultLabel(LOCTEXT("Column_File", "File"))
							.FillWidth(0.34f)
						+ SHeaderRow::Column(ColumnId::Status)
							.DefaultLabel(LOCTEXT("Column_Status", "Status"))
							.FillWidth(0.2f)
						+ SHeaderRow::Column(ColumnId::Action)
							.DefaultLabel(LOCTEXT("Column_Action", "Action"))
							.FixedWidth(330.f)
					)
				]
			]

			// When the list is a prediction rather than the user's own selection, say so - it
			// beats letting the user believe everything listed is about to be written
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Visibility(bMultiple && !InArgs._ListIsExact ? EVisibility::Visible : EVisibility::Collapsed)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("PredictionDisclaimer",
					"Listed: every unsaved file. If you left files out of this save, they appear here too but will not be saved - an answer only takes effect when its file is actually saved."))
			]

			// Footer
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
					.Text(LOCTEXT("Confirm", "Confirm"))
					.OnClicked(this, &SDiversionSaveDecisionDialog::OnConfirmClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.ToolTipText(LOCTEXT("CancelTooltip", "Save nothing - the files keep their unsaved changes"))
					.OnClicked(this, &SDiversionSaveDecisionDialog::OnCancelClicked)
				]
			]
		]
	];
}

void SDiversionSaveDecisionDialog::BuildGroupedListItems()
{
	ListItems.Reset();

	// A single file needs no grouping - the header would just repeat the row
	if (Rows.Num() == 1)
	{
		TSharedPtr<FDiversionSaveDecisionListItem> Item = MakeShared<FDiversionSaveDecisionListItem>();
		Item->Row = Rows[0];
		ListItems.Add(Item);
		return;
	}

	// Bucket by situation: the same lock state and the same kind of row share one group
	TArray<TSharedPtr<FDiversionSaveDecisionGroup>> Groups;
	for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : Rows)
	{
		TSharedPtr<FDiversionSaveDecisionGroup>* Existing = Groups.FindByPredicate(
			[&Row](const TSharedPtr<FDiversionSaveDecisionGroup>& Group)
			{
				return Group->Category == Row->Category && Group->bDecisionRequired == Row->bDecisionRequired;
			});
		TSharedPtr<FDiversionSaveDecisionGroup> Group;
		if (Existing != nullptr)
		{
			Group = *Existing;
		}
		else
		{
			Group = MakeShared<FDiversionSaveDecisionGroup>();
			Group->Category = Row->Category;
			Group->bDecisionRequired = Row->bDecisionRequired;
			Group->Title = GetGroupTitle(Row->Category);
			Group->LastSetChoice = Row->Choice; // the per-category default, uniform on arrival
			Groups.Add(Group);
		}
		Group->Rows.Add(Row);
	}

	Groups.Sort([](const TSharedPtr<FDiversionSaveDecisionGroup>& A, const TSharedPtr<FDiversionSaveDecisionGroup>& B)
		{
			return GetGroupSortOrder(*A) < GetGroupSortOrder(*B);
		});

	for (const TSharedPtr<FDiversionSaveDecisionGroup>& Group : Groups)
	{
		Group->bOffersLock = Group->bDecisionRequired
			&& Group->Rows.Num() <= MaxGroupLockOffer
			&& Group->Rows[0]->bLockOfferAvailable;
		Group->bOffersAcceptIncoming = Group->bDecisionRequired
			&& !Group->Rows.ContainsByPredicate([](const TSharedPtr<FDiversionSaveDecisionRow>& Row)
				{
					return !Row->bAcceptIncomingAvailable;
				});
		if (Group->bDecisionRequired && !Group->bOffersLock)
		{
			// A crowd the header will not offer to lock must not lock by default either -
			// Confirm untouched would otherwise issue one lock request per file. The rows
			// keep Lock & Save as an explicit per-file choice.
			for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : Group->Rows)
			{
				if (Row->Choice == EDiversionSaveChoice::LockAndSave)
				{
					Row->Choice = EDiversionSaveChoice::SaveUnlocked;
				}
			}
			if (Group->LastSetChoice == EDiversionSaveChoice::LockAndSave)
			{
				Group->LastSetChoice = EDiversionSaveChoice::SaveUnlocked;
			}
		}

		TSharedPtr<FDiversionSaveDecisionListItem> HeaderItem = MakeShared<FDiversionSaveDecisionListItem>();
		HeaderItem->Group = Group;
		ListItems.Add(HeaderItem);

		for (const TSharedPtr<FDiversionSaveDecisionRow>& Row : Group->Rows)
		{
			TSharedPtr<FDiversionSaveDecisionListItem> RowItem = MakeShared<FDiversionSaveDecisionListItem>();
			RowItem->Row = Row;
			ListItems.Add(RowItem);
		}
	}
}

TSharedRef<ITableRow> SDiversionSaveDecisionDialog::GenerateRow(TSharedPtr<FDiversionSaveDecisionListItem> InItem,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InItem->Row.IsValid())
	{
		return SNew(SDiversionSaveDecisionRowWidget, OwnerTable)
			.Item(InItem->Row)
			.Indent(Rows.Num() > 1);
	}
	return SNew(SDiversionSaveDecisionGroupHeaderWidget, OwnerTable).Group(InItem->Group);
}

FReply SDiversionSaveDecisionDialog::OnConfirmClicked()
{
	bConfirmed = true;
	CloseWindow();
	return FReply::Handled();
}

FReply SDiversionSaveDecisionDialog::OnCancelClicked()
{
	bConfirmed = false;
	CloseWindow();
	return FReply::Handled();
}

void SDiversionSaveDecisionDialog::CloseWindow()
{
	if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE
