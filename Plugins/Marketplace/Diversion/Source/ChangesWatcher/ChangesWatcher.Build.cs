// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

using UnrealBuildTool;
public class ChangesWatcher : ModuleRules
{
	public ChangesWatcher(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"AssetRegistry",
			"UnrealEd",
			"Common",
			"Engine",
			"Slate",
			"SlateCore",
		});

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"DirectoryWatcher",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"DirectoryWatcher",
			}
		);
	}
}
