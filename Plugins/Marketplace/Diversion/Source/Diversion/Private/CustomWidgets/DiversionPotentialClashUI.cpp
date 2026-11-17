// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionPotentialClashUI.h"

#include "DiversionState.h"
#include "DiversionUtils.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


const FSlateBrush* SPotentialClashIndicator::PotentialClashIcon = nullptr;

namespace DiversionPotentialClashUI
{
	bool AssetHasPotentialClashes(const FString& Path)
	{
		TSharedPtr<FDiversionState, ESPMode::ThreadSafe> DiversionState = DiversionUtils::GetDiversionStateForPath(Path);
		return DiversionState ? DiversionState->PotentialClashes.GetPotentialClashesCount() > 0 : false;
	}
}

/**
* Construct this widget.
* @param InArgs Slate arguments
*/

void SPotentialClashIndicator::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	SetVisibility(MakeAttributeSP(this, &SPotentialClashIndicator::GetVisibility));

	ChildSlot
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
// UI changes make this needed in 5.6 and later to fix icon placement
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
#endif
				[
					SNew(SBox)
						[
							SNew(SImage)
								.Image(this, &SPotentialClashIndicator::GetImageBrush)
								.ColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.0f)) // Yellow tint
								.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
						]
				]
		];
}

/** Caches the indicator brushes for access. */

void SPotentialClashIndicator::CacheIndicatorBrush()
{
	if (PotentialClashIcon == nullptr)
	{
		PotentialClashIcon = FAppStyle::GetBrush("Icons.Lock");
	}
}

/**
* Construct this widget.
* @param InArgs Slate arguments
*/

void SPotentialClashTooltip::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	SetVisibility(MakeAttributeSP(this, &SPotentialClashTooltip::GetVisibility));

	ChildSlot
		[
			SNew(STextBlock)
				.Text(this, &SPotentialClashTooltip::GetTooltip)
		];
}

FText SPotentialClashTooltip::GetTooltip() const
{
	TSharedPtr<FDiversionState, ESPMode::ThreadSafe> DiversionState = DiversionUtils::GetDiversionStateForPath(AssetPath);
	if (!DiversionState)
	{
		return FText();
	}
	return DiversionState->GetOtherEditorsList();
}
