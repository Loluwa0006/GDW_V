// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionStyle.h"
#include "CustomWidgets/DiversionHardLockUI.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleRegistry.h"

TSharedPtr<FSlateStyleSet> FDiversionStyle::StyleSet;

void FDiversionStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>("DiversionStyle");

	// Point the style to .../Plugins/UnrealDiversion/Resources
	const FString BaseDir = IPluginManager::Get().FindPlugin(TEXT("Diversion"))->GetBaseDir();
	StyleSet->SetContentRoot(BaseDir / TEXT("Resources"));

	// Register the Diversion icon
	const FVector2f Icon16(16.0, 16.0);
	StyleSet->Set("Diversion.Icon", new FSlateVectorImageBrush(StyleSet->RootToContentDir(TEXT("DiversionIcon"), TEXT(".svg")), Icon16));

	// Filled pill behind the asset editor hard-lock banner. It belongs to the style set rather
	// than a static in the widget: FSlateBrush caches a render resource handle once painted, and
	// a static would only be destroyed at process exit - after the renderer is gone. Shutdown()
	// releases it while Slate is still alive.
	StyleSet->Set("Diversion.HardLockPill",
		new FSlateRoundedBoxBrush(DiversionHardLockUI::LockedByOtherColor, 10.0f));

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet);
}

void FDiversionStyle::Shutdown()
{
	if (!StyleSet.IsValid())
	{
		return;
	}
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet);
	StyleSet.Reset();
}

const ISlateStyle& FDiversionStyle::Get()
{
	return *StyleSet.Get();
}

FName FDiversionStyle::GetStyleSetName()
{
	return "DiversionStyle";
}
