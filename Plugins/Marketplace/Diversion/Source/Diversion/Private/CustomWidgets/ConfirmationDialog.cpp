// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "ConfirmationDialog.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"

/**
* Construct this widget.
* @param InArgs Slate arguments
*/
#define LOCTEXT_NAMESPACE "ConfirmationDialog"

void SConfirmationDialog::Construct(const FArguments& InArgs)
{
	IdentifierResult = -1;
	ParentWindow = InArgs._ParentWindow;
	ButtonsDefs = InArgs._Buttons;
	AdditionalLinks = InArgs._AdditionalLinks;
	DefaultExtraConfirmState = InArgs._DefaultExtraConfirmState;

	const FSlateBrush* IconBrush = FAppStyle::Get().GetBrush("Icons.WarningWithColor.Large");
	FSlateFontInfo MessageFont(FAppStyle::GetFontStyle("StandardDialog.LargeFont"));

	LoadPreviousExtraConfirmationState();

	// The confirmation checkboxes are only relevant when some button requires extra confirmation
	bool bAnyButtonRequiresExtraConfirmation = false;
	for (const auto& ButtonDef : ButtonsDefs)
	{
		if (ButtonDef->bRequiresExtraConfirmation)
		{
			bAnyButtonRequiresExtraConfirmation = true;
			break;
		}
	}
	const EVisibility ExtraConfirmationVisibility =
		bAnyButtonRequiresExtraConfirmation ? EVisibility::Visible : EVisibility::Collapsed;

	ChildSlot
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(16.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.FillHeight(1.0f)
					.MaxHeight(550)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						.HAlign(HAlign_Left)
						[
							SNew(SImage)
							.DesiredSizeOverride(FVector2D(24.f, 24.f))
							.Image(IconBrush)
						]
						+ SHorizontalBox::Slot()
						.Padding(16.f, 0.f, 0.f, 0.f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(InArgs._Message)
								.Font(MessageFont)
								.WrapTextAt(512.f)
							]
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Left)
					.Padding(0.f, 32.f, 0.f, 0.f)
					[
						SNew(SCheckBox)
						.Visibility(ExtraConfirmationVisibility)
						.IsChecked_Lambda([this]()
						{
							return ExtraConfirmation;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState InCheckBoxState)
						{
							ExtraConfirmation = InCheckBoxState;
							if(DefaultExtraConfirmState != nullptr && ExtraConfirmation ==  ECheckBoxState::Unchecked)
							{
								// Disable this if ExtraConfirmation is unchecked
								*DefaultExtraConfirmState = ECheckBoxState::Unchecked;
							}
						})
						.Padding(FMargin(4.0f, 0.0f))
						.Content()
						[
							SNew(STextBlock)
							.Text(InArgs._ExtraConfirmationText)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Left)
					[
						SNew(SCheckBox)
						.Visibility(ExtraConfirmationVisibility)
						.IsChecked_Lambda([this]()
						{
							if(DefaultExtraConfirmState == nullptr)
							{
								return ECheckBoxState::Unchecked;
							}

							if(ExtraConfirmation != ECheckBoxState::Checked)
							{
								// DefaultExtraConfirmState is only relevant when ExtraConfirmation is checked
								return ECheckBoxState::Unchecked;
							}

							return *DefaultExtraConfirmState;
						})
						.OnCheckStateChanged_Lambda([this](ECheckBoxState InCheckBoxState){
							if(DefaultExtraConfirmState== nullptr)
							{
								return;
							}
							*DefaultExtraConfirmState = InCheckBoxState;
						})
						.IsEnabled_Lambda([this]()
						{
							bool enabled = DefaultExtraConfirmState != nullptr && ExtraConfirmation == ECheckBoxState::Checked;
							return enabled;
						})
						.Padding(FMargin(4.0f, 0.0f))
						.Content()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("KeepMarked_Text", "Keep these checkboxes marked for next time"))
						]
					]
					+SVerticalBox::Slot()
					.Padding(0.f, 32.f, 0.f, 0.f)
					.AutoHeight()
					.HAlign(HAlign_Right)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							ConstructButtons()
						]
					]
					+SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Left)
					[
						ConstructAdditionalLinks()
					]
				]
		];
}

void SConfirmationDialog::LoadPreviousExtraConfirmationState()
{
	// Set the default value of the extra confirmation state from the stored data
	if (DefaultExtraConfirmState != nullptr)
	{
		ExtraConfirmation = *DefaultExtraConfirmState;
	}
}

TSharedRef<SWidget> SConfirmationDialog::ConstructButtons()
{
	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

	for (TSharedRef<FConfirmationOption> ButtonDef : ButtonsDefs)
	{
		TSharedRef<SButton> Button = SNew(SButton).
			Text(ButtonDef->Text).
			IsEnabled_Lambda([this, ButtonDef]()
			{
				return ButtonDef->bRequiresExtraConfirmation ? (ExtraConfirmation == ECheckBoxState::Checked) : true;
			}).
			OnClicked_Lambda([this, ButtonDef]() {
			IdentifierResult = ButtonDef->Identifier;
			if (ParentWindow.IsValid())
			{
				ParentWindow->RequestDestroyWindow();
			}
			return FReply::Handled();
				});

		if (ButtonDef->ButtonStyle != nullptr)
		{
			Button->SetButtonStyle(ButtonDef->ButtonStyle);
		}

		HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(5)
			.VAlign(VAlign_Bottom)
			[Button];
	}

	return HorizontalBox;
}

TSharedRef<SHorizontalBox> SConfirmationDialog::ConstructAdditionalLinks()
{
	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);
	for(int i = 0; i < AdditionalLinks.Num(); i++)
	{
		TSharedRef<FConfirmationAdditionalLink> Link = AdditionalLinks[i];
		HorizontalBox->AddSlot()
		.AutoWidth()
		[
			SNew(SHyperlink)
			.Text(Link->Text)
			.ToolTipText(Link->ToolTipText)
			.OnNavigate_Lambda([Link]()
			{
				Link->OnNavigate();
			})
		];

		// If we have more links, add a separator
		if (i < AdditionalLinks.Num() - 1)
		{
			HorizontalBox->AddSlot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AdditionalLinksSeparator", " | "))
			];
		}
	}
	return HorizontalBox;
}

#undef LOCTEXT_NAMESPACE
