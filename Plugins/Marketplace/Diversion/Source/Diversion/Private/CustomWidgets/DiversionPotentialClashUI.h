// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

namespace DiversionPotentialClashUI
{
	/** Whether another workspace holds unpublished edits to the asset at Path */
	bool AssetHasPotentialClashes(const FString& Path);
}

class SPotentialClashIndicator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPotentialClashIndicator) {}
		SLATE_ARGUMENT(FString, AssetPath)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	/** Caches the indicator brushes for access. */
	static void CacheIndicatorBrush();

	virtual ~SPotentialClashIndicator() = default;

private:

	EVisibility GetVisibility() const
	{
		return DiversionPotentialClashUI::AssetHasPotentialClashes(AssetPath) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	const FSlateBrush* GetImageBrush() const
	{
		return DiversionPotentialClashUI::AssetHasPotentialClashes(AssetPath) ? PotentialClashIcon : nullptr;
	}

	static const FSlateBrush* PotentialClashIcon;

	/** Asset path for this indicator widget.*/
	FString AssetPath;
};


class SPotentialClashTooltip : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPotentialClashTooltip) {}
		SLATE_ARGUMENT(FString, AssetPath)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	virtual ~SPotentialClashTooltip() = default;
private:

	EVisibility GetVisibility() const
	{
		return DiversionPotentialClashUI::AssetHasPotentialClashes(AssetPath) ? EVisibility::Visible : EVisibility::Collapsed;
	}

	FText GetTooltip() const;

	/** Asset path for this indicator widget.*/
	FString AssetPath;
};


