// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
/**
 * Diversion Core API
 * Definition of the Core API used to access low-level functionality of Diversion
 */

using System;
using System.IO;
using UnrealBuildTool;

public class CoreAPI : ModuleRules
{
    public CoreAPI(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "Json",
                "Common",
                "DiversionHttp"
            }
        );
        PCHUsage = PCHUsageMode.NoPCHs;
    }
}