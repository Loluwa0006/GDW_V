// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

namespace DiversionHardLockUI
{
	/** Red for a lock held by another user, green for one held by the current user. Shared so
	 *  every hard-lock surface signals a lock with the same two colours. */
	extern const FLinearColor LockedByOtherColor;
	extern const FLinearColor LockedByMeColor;

	/** Whether the asset at Path carries a hard lock, held by anyone */
	bool IsAssetHardLocked(const FString& Path);

	/** Whether the asset at Path is hard-locked by a user other than the current one */
	bool IsAssetHardLockedByOther(const FString& Path);

	/**
	 * "Locked by {holder} since {date}" for the asset at Path, empty when it carries no lock.
	 * Shared by every hard-lock surface so the wording stays identical wherever it appears.
	 */
	FText GetHardLockText(const FString& Path);
}

class SHardLockIndicator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHardLockIndicator) {}
		SLATE_ARGUMENT(FString, AssetPath)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	/** Caches the indicator brushes for access. */
	static void CacheIndicatorBrush();

	/** The shared lock brush; call CacheIndicatorBrush() first. */
	static const FSlateBrush* GetIconBrush() { return HardLockIcon; }

	virtual ~SHardLockIndicator() = default;

private:

	EVisibility GetVisibility() const
	{
		return DiversionHardLockUI::IsAssetHardLocked(AssetPath) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	const FSlateBrush* GetImageBrush() const
	{
		return DiversionHardLockUI::IsAssetHardLocked(AssetPath) ? HardLockIcon : nullptr;
	}

	/** Red when locked by another user, green when locked by the current user */
	FSlateColor GetLockColor() const
	{
		return DiversionHardLockUI::IsAssetHardLockedByOther(AssetPath) ?
			DiversionHardLockUI::LockedByOtherColor : DiversionHardLockUI::LockedByMeColor;
	}

	static const FSlateBrush* HardLockIcon;

	/** Asset path for this indicator widget.*/
	FString AssetPath;
};


class SHardLockTooltip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHardLockTooltip) {}
		SLATE_ARGUMENT(FString, AssetPath)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	virtual ~SHardLockTooltip() = default;
private:

	EVisibility GetVisibility() const
	{
		return DiversionHardLockUI::IsAssetHardLocked(AssetPath) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetTooltip() const
	{
		return DiversionHardLockUI::GetHardLockText(AssetPath);
	}

	/** Asset path for this indicator widget.*/
	FString AssetPath;
};


/**
 * Persistent "someone else holds this lock" banner for an open asset editor window.
 *
 * Unlike the Content Browser icon this shows only locks held by OTHERS - your own lock is
 * not news while you are the one editing. Every visual is a TAttribute over the state cache,
 * so the banner appears, retargets and clears on its own as the background lock poll lands;
 * no re-registration and no per-window bookkeeping.
 */
class SHardLockBanner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHardLockBanner) {}
		SLATE_ARGUMENT(FString, AssetPath)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	virtual ~SHardLockBanner() = default;

private:

	EVisibility GetVisibility() const
	{
		return DiversionHardLockUI::IsAssetHardLockedByOther(AssetPath) ?
			EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetBannerText() const
	{
		return DiversionHardLockUI::GetHardLockText(AssetPath);
	}

	/** Asset path for this banner widget.*/
	FString AssetPath;
};
