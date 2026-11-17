// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/TextLocalizationManager.h"
#include "DiversionEngineTextOverrides.h"
#include "DiversionVersionCompat.h"

namespace
{
	/** Culture changes and RefreshResources load the localization data on a task in newer engines */
	void WaitForLocalization()
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 4, 0)
		FTextLocalizationManager::Get().WaitForAsyncTasks();
#endif
	}

	/** Bound to the engine's live-table entry, so ToString() shows whatever is displayed right now */
	FText EngineRevertText()
	{
		return NSLOCTEXT("AssetSourceControlContextMenu", "SCCRevert", "Revert");
	}

	/** The plugin's own (localized) replacement for that engine text */
	FText DiversionResetText()
	{
		return NSLOCTEXT("DiversionEngineTextOverrides", "AssetSourceControlContextMenu_SCCRevert", "Reset");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEngineTextOverridesFollowCulture,
	"Diversion.Tests.EngineTextOverrides.FollowCulture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FEngineTextOverridesFollowCulture::RunTest(const FString& Parameters)
{
	FInternationalization& I18N = FInternationalization::Get();
	const FString OriginalCulture = I18N.GetCurrentCulture()->GetName();
	const bool bWasEnabled = DiversionEngineTextOverrides::IsEnabled();

	I18N.SetCurrentCulture(TEXT("en"));
	WaitForLocalization();
	DiversionEngineTextOverrides::Enable();
	TestEqual(TEXT("English: engine text is replaced"), EngineRevertText().ToString(), FString(TEXT("Reset")));

	// A culture change reloads the engine's translations; the override must come back, translated
	I18N.SetCurrentCulture(TEXT("ja"));
	WaitForLocalization();
	const FString JapaneseReset = DiversionResetText().ToString();
	TestNotEqual(TEXT("Japanese: the plugin's translation is loaded"), JapaneseReset, FString(TEXT("Reset")));
	TestEqual(TEXT("Japanese: engine text shows the plugin's translation"), EngineRevertText().ToString(), JapaneseReset);

	DiversionEngineTextOverrides::Disable();
	WaitForLocalization();
	TestFalse(TEXT("Disabled: no longer tracking refreshes"), DiversionEngineTextOverrides::IsEnabled());
	TestNotEqual(TEXT("Disabled: engine text is restored"), EngineRevertText().ToString(), JapaneseReset);

	I18N.SetCurrentCulture(OriginalCulture);
	WaitForLocalization();
	if (bWasEnabled)
	{
		DiversionEngineTextOverrides::Enable();
	}
	return true;
}
