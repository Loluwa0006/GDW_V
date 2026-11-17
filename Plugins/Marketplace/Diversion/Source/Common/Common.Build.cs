// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

using System.Collections.Generic;
using UnrealBuildTool;

public class Common : ModuleRules
{
	public Common(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});

		// Only compile tests when building the editor
		if (Target.Type == TargetType.Editor)
		{
			PrivateIncludePaths.Add("Common/Tests");
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
