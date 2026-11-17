// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class SHorizontalBox;


struct FConfirmationOption {
	FConfirmationOption(
		const FText& InText,
		const uint32 InIdentifier,
		const FButtonStyle* InButtonStyle,
		const bool InRequiresExtraConfirmation = false) :
			Text(InText),
			Identifier(InIdentifier),
			ButtonStyle(InButtonStyle),
			bRequiresExtraConfirmation(InRequiresExtraConfirmation) {}
	FText Text;
	uint32 Identifier;
	const FButtonStyle* ButtonStyle;
	bool bRequiresExtraConfirmation;
};

struct FConfirmationAdditionalLink {
	FConfirmationAdditionalLink(
		const FText& InText,
		const FText& InToolTipText,
		const TFunction<void()>& InOnNavigate) :
			Text(InText),
			ToolTipText(InToolTipText),
			OnNavigate(InOnNavigate) {}
	FText Text;
	FText ToolTipText;
	TFunction<void()> OnNavigate;
};

class SConfirmationDialog : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SConfirmationDialog)
		: _DefaultExtraConfirmState(nullptr)
		{}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FText, Message)
		SLATE_ARGUMENT(FText, ExtraConfirmationText)
		SLATE_ARGUMENT(TArray<TSharedRef<FConfirmationOption>>, Buttons)
		SLATE_ARGUMENT(TArray<TSharedRef<FConfirmationAdditionalLink>>, AdditionalLinks)
		SLATE_ARGUMENT(ECheckBoxState*, DefaultExtraConfirmState)
	SLATE_END_ARGS();

	/**
	 * Construct this widget.
	 * @param InArgs Slate arguments
	 */
	void Construct(const FArguments& InArgs);

	int GetResult()
	{
		return IdentifierResult;
	}

private:

	void LoadPreviousExtraConfirmationState();

	ECheckBoxState ExtraConfirmation = ECheckBoxState::Unchecked;
	// Storing the state of the bExtraConfirmation between dialog boxes sessions
	// i.e. will remember if we checked this box in the past
	ECheckBoxState* DefaultExtraConfirmState = nullptr;
	TSharedRef<SWidget> ConstructButtons();
	TSharedRef<SHorizontalBox> ConstructAdditionalLinks();

	TSharedPtr<SWindow> ParentWindow;
	int IdentifierResult = -1;
	TArray<TSharedRef<FConfirmationOption>> ButtonsDefs;
	TArray<TSharedRef<FConfirmationAdditionalLink>> AdditionalLinks;
};
