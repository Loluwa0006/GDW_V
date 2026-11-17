// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionStatusPanel.h"
#include "DiversionModule.h"
#include "DiversionProvider.h"
#include "DiversionUtils.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "DiversionStyle.h"

#define LOCTEXT_NAMESPACE "DiversionStatusPanel"

void SDiversionStatusPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SHorizontalBox)
		.Visibility(this, &SDiversionStatusPanel::GetPanelVisibility)
		.ToolTipText(LOCTEXT("DiversionStatusTooltip", "Shows the current Diversion workspace branch and sync status"))

		// Main content with padding matching source control
		+ SHorizontalBox::Slot()
		.Padding(6.0f, 0.0f)
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			// Diversion icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(this, &SDiversionStatusPanel::GetDiversionIcon)
			]

			// Branch name
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(5, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SDiversionStatusPanel::GetCurrentBranch)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
			]

			// Separator
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8, 0, 8, 0))
			[
				SNew(STextBlock)
				.Text(FText::AsCultureInvariant(TEXT("|")))
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]

			// Sync status icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(this, &SDiversionStatusPanel::GetSyncStatusIcon)
				.ColorAndOpacity(this, &SDiversionStatusPanel::GetSyncStatusColor)
			]

			// Sync status text
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(5, 0, 0, 0))
			[
				SNew(STextBlock)
				.Text(this, &SDiversionStatusPanel::GetSyncStatus)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
			]
		]
	];
}

void SDiversionStatusPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (InCurrentTime - LastUpdateTime > UpdateInterval)
	{
		LastUpdateTime = InCurrentTime;
		Invalidate(EInvalidateWidget::Layout);
	}
}

FText SDiversionStatusPanel::GetCurrentBranch() const
{
	if (FDiversionProvider* Provider = GetDiversionProvider())
	{
		const WorkspaceInfo WsInfo = Provider->GetWsInfo();
		if (!WsInfo.BranchName.IsEmpty() && WsInfo.BranchName != TEXT("N/a"))
		{
			return FText::Format(LOCTEXT("BranchName", "on branch: {0}"), FText::FromString(WsInfo.BranchName));
		}
	}
	return FText::GetEmpty();
}

FText SDiversionStatusPanel::GetSyncStatus() const
{
	if (FDiversionProvider* Provider = GetDiversionProvider())
	{
		const DiversionUtils::EDiversionWsSyncStatus SyncStatus = Provider->GetSyncStatus();
		switch (SyncStatus)
		{
		case DiversionUtils::EDiversionWsSyncStatus::InProgress:
			return LOCTEXT("SyncStatus_InProgress", "Syncing...");
		case DiversionUtils::EDiversionWsSyncStatus::Completed:
			return LOCTEXT("SyncStatus_Completed", "Synced");
		case DiversionUtils::EDiversionWsSyncStatus::Paused:
			return LOCTEXT("SyncStatus_Paused", "Paused");
		case DiversionUtils::EDiversionWsSyncStatus::PathError:
			return LOCTEXT("SyncStatus_PathError", "Path Error");
		case DiversionUtils::EDiversionWsSyncStatus::Unknown:
		default:
			return FText::GetEmpty();
		}
	}
	return FText::GetEmpty();
}

FSlateColor SDiversionStatusPanel::GetSyncStatusColor() const
{
	if (FDiversionProvider* Provider = GetDiversionProvider())
	{
		const DiversionUtils::EDiversionWsSyncStatus SyncStatus = Provider->GetSyncStatus();
		switch (SyncStatus)
		{
		case DiversionUtils::EDiversionWsSyncStatus::InProgress:
			return FSlateColor(FLinearColor::Yellow);
		case DiversionUtils::EDiversionWsSyncStatus::Completed:
			return FSlateColor(FLinearColor::Green);
		case DiversionUtils::EDiversionWsSyncStatus::Paused:
			return FSlateColor(FLinearColor::Blue);
		case DiversionUtils::EDiversionWsSyncStatus::PathError:
			return FSlateColor(FLinearColor::Red);
		case DiversionUtils::EDiversionWsSyncStatus::Unknown:
		default:
			return FSlateColor::UseSubduedForeground();
		}
	}
	return FSlateColor::UseSubduedForeground();
}

const FSlateBrush* SDiversionStatusPanel::GetSyncStatusIcon() const
{
	if (FDiversionProvider* Provider = GetDiversionProvider())
	{
		const DiversionUtils::EDiversionWsSyncStatus SyncStatus = Provider->GetSyncStatus();
		switch (SyncStatus)
		{
		case DiversionUtils::EDiversionWsSyncStatus::InProgress:
			return FAppStyle::GetBrush("Icons.Refresh");
		case DiversionUtils::EDiversionWsSyncStatus::Completed:
			return FAppStyle::GetBrush("Icons.Check");
		case DiversionUtils::EDiversionWsSyncStatus::Paused:
			return FAppStyle::GetBrush("Icons.Pause");
		case DiversionUtils::EDiversionWsSyncStatus::PathError:
			return FAppStyle::GetBrush("Icons.Error");
		case DiversionUtils::EDiversionWsSyncStatus::Unknown:
		default:
			return FAppStyle::GetBrush("Icons.Help");
		}
	}
	return FAppStyle::GetBrush("Icons.Help");
}

const FSlateBrush* SDiversionStatusPanel::GetDiversionIcon() const
{
	// Use the proper style class to get the icon
	return FDiversionStyle::Get().GetBrush("Diversion.Icon");
}

FDiversionProvider* SDiversionStatusPanel::GetDiversionProvider() const
{
	if (FDiversionModule::IsLoaded())
	{
		return &FDiversionModule::Get().GetProvider();
	}
	return nullptr;
}

bool SDiversionStatusPanel::IsValidProvider() const
{
	FDiversionProvider* Provider = GetDiversionProvider();
	return Provider && Provider->IsEnabled() && Provider->IsAvailable();
}

EVisibility SDiversionStatusPanel::GetPanelVisibility() const
{
	if (!IsValidProvider())
	{
		return EVisibility::Collapsed;
	}

	FDiversionProvider* Provider = GetDiversionProvider();
	const WorkspaceInfo WsInfo = Provider->GetWsInfo();

	bool ShouldShow = !WsInfo.BranchName.IsEmpty() && WsInfo.BranchName != TEXT("N/a");
	return ShouldShow ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
