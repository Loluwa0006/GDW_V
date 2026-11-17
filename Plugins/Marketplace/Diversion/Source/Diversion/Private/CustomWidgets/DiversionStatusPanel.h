// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FDiversionProvider;

class SDiversionStatusPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDiversionStatusPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDiversionStatusPanel() = default;

	void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	FText GetCurrentBranch() const;
	FText GetSyncStatus() const;

	FSlateColor GetSyncStatusColor() const;
	const FSlateBrush* GetSyncStatusIcon() const;
	const FSlateBrush* GetDiversionIcon() const;

	FDiversionProvider* GetDiversionProvider() const;
	bool IsValidProvider() const;
	EVisibility GetPanelVisibility() const;

	mutable double LastUpdateTime = 0.0;
	static constexpr double UpdateInterval = 1.0;
};
