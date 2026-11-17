// Copyright 2024 Diversion Company, Inc. All Rights Reserved.
/**
 * Agent API
 * API of Diversion sync agent
 */

using System;
using System.IO;
using UnrealBuildTool;

public class AgentAPI : ModuleRules
{
    public AgentAPI(ReadOnlyTargetRules Target) : base(Target)
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