// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionHardLockUI.h"

#include "DiversionState.h"
#include "DiversionUtils.h"
#include "Styling/AppStyle.h"
#include "DiversionStyle.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DiversionHardLock"

const FSlateBrush* SHardLockIndicator::HardLockIcon = nullptr;

namespace DiversionHardLockUI
{
	const FLinearColor LockedByOtherColor(1.0f, 0.25f, 0.25f);
	const FLinearColor LockedByMeColor(0.3f, 0.85f, 0.3f);

	bool IsAssetHardLocked(const FString& Path)
	{
		TSharedPtr<FDiversionState, ESPMode::ThreadSafe> DiversionState = DiversionUtils::GetDiversionStateForPath(Path);
		return DiversionState ? DiversionState->FileLock.IsLocked() : false;
	}

	bool IsAssetHardLockedByOther(const FString& Path)
	{
		TSharedPtr<FDiversionState, ESPMode::ThreadSafe> DiversionState = DiversionUtils::GetDiversionStateForPath(Path);
		return DiversionState ? DiversionState->FileLock.IsLockedByOther() : false;
	}

	FText GetHardLockText(const FString& Path)
	{
		TSharedPtr<FDiversionState, ESPMode::ThreadSafe> DiversionState = DiversionUtils::GetDiversionStateForPath(Path);
		if (!DiversionState)
		{
			return FText();
		}

		const FDiversionLockInfo LockInfo = DiversionState->FileLock.GetLockInfo();
		if (!LockInfo.IsSet())
		{
			return FText();
		}

		const FText LockedSince = DiversionUtils::FormatUnixTimestamp(LockInfo.LockedAt);
		if (LockInfo.bLockedByOther)
		{
			return FText::Format(LOCTEXT("HardLockedByOther_Tooltip", "Locked by {0} since {1}"),
				LockInfo.GetHolderText(), LockedSince);
		}
		return FText::Format(LOCTEXT("HardLockedByMe_Tooltip", "Locked by you since {0}"),
			LockedSince);
	}
}

namespace
{
	/** Icon and label sit on the filled pill, so they invert rather than take the lock colour */
	const FLinearColor HardLockBannerContentColor(FLinearColor::White);

	/**
	 * The pill's fill, owned by the plugin's style set (FDiversionStyle) so Shutdown() releases
	 * it while Slate is still alive - a brush outliving the renderer still holds a cached
	 * resource handle.
	 *
	 * Deliberately not the engine's AssetEditor.ReadOnlyBorder: that brush is tinted
	 * FStyleColors::Foreground - a *text* colour - so SBorder would multiply our red by whatever
	 * the active theme sets it to, and it does not exist at all before UE 5.5 (we ship 5.3+).
	 */
	const FSlateBrush* GetHardLockPillBrush()
	{
		return FDiversionStyle::Get().GetBrush("Diversion.HardLockPill");
	}
}

/**
* Construct this widget.
* @param InArgs Slate arguments
*/

void SHardLockIndicator::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	SetVisibility(MakeAttributeSP(this, &SHardLockIndicator::GetVisibility));

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
								.Image(this, &SHardLockIndicator::GetImageBrush)
								.ColorAndOpacity(this, &SHardLockIndicator::GetLockColor)
								.DesiredSizeOverride(FVector2D(16.0f, 16.0f))
						]
				]
		];
}

/** Caches the indicator brushes for access. */

void SHardLockIndicator::CacheIndicatorBrush()
{
	if (HardLockIcon == nullptr)
	{
		HardLockIcon = FAppStyle::GetBrush("Icons.Lock");
	}
}

/**
* Construct this widget.
* @param InArgs Slate arguments
*/

void SHardLockTooltip::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	SetVisibility(MakeAttributeSP(this, &SHardLockTooltip::GetVisibility));

	ChildSlot
		[
			SNew(STextBlock)
				.Text(this, &SHardLockTooltip::GetTooltip)
		];
}

/**
* Construct this widget.
* @param InArgs Slate arguments
*/

void SHardLockBanner::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	// The banner is built once when the host toolbar is generated, but every visual below is
	// an attribute over the state cache - so it appears and clears with the lock, with no
	// toolbar regeneration and nothing to unregister when the lock is released.
	SetVisibility(MakeAttributeSP(this, &SHardLockBanner::GetVisibility));

	// The host toolbar can be generated before the provider has cached its brushes
	SHardLockIndicator::CacheIndicatorBrush();

	// A filled pill rather than loose text, so it reads as a bar in the toolbar row.
	// A row of its own above the toolbar is not reachable - the engine owns that vertical box
	// (SStandaloneAssetEditorToolkitHost::SetToolbar) and rewrites the slot on every
	// RegenerateMenusAndToolbars, which Blueprint editors do on each compile.
	ChildSlot
		[
			SNew(SBorder)
				.VAlign(VAlign_Center)
				.BorderImage(GetHardLockPillBrush())
				.Padding(FMargin(10.0f, 2.0f))
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(SBox)
								.HeightOverride(16.0f)
								.WidthOverride(16.0f)
								[
									SNew(SImage)
										.Image(SHardLockIndicator::GetIconBrush())
										.ColorAndOpacity(HardLockBannerContentColor)
								]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Text(this, &SHardLockBanner::GetBannerText)
								.ColorAndOpacity(HardLockBannerContentColor)
						]
				]
		];
}

#undef LOCTEXT_NAMESPACE
