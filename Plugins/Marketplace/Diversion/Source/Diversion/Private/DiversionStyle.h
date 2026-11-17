// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

class FDiversionStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
