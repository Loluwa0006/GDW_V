// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Diversion : ModuleRules
{
	public Diversion(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		IWYUSupport = IWYUSupport.Full;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"Projects",
				"SourceControl",
				"DesktopWidgets",
				"CoreUObject",
				"Engine",
				"Json",
				"DataValidation",
				"ToolMenus",
				"ToolWidgets",
				"AssetRegistry",
				"MessageLog",
				"SourceControlWindows",
				"SceneOutliner",
				"ContentBrowser",
				"ContentBrowserData",
				"PackagesDialog",
				// Diversion dependencies
				"Common",
				"DiversionHttp",
				"AgentAPI",
				"CoreAPI"
			}
		);

		// UE 5.6+ deprecated UnsafeTypeCastWarningLevel in favor of CppCompileWarningSettings.UnsafeTypeCastWarningLevel
		// UE 5.3 is excluded: SceneOutlinerPublicTypes.h line 80 has an int32->uint8 bug that triggers C4244
#if UE_5_6_OR_LATER
		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
#elif UE_5_4_OR_LATER
		UnsafeTypeCastWarningLevel = WarningLevel.Error;
#endif

		// Only compile tests when building the editor
		if (Target.Type == TargetType.Editor)
		{
			PrivateIncludePaths.Add("Diversion/Tests");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
