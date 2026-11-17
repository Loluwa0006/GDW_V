// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DiversionHttp : ModuleRules
{
	public DiversionHttp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[] {
			"Core",
			"Json",
			"OpenSSL",
			"Projects",
			"zlib",
			"Boost",
			"Slate",
			"Common",
		});

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "ThirdParty"));

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// CertOpenSystemStore/CertEnumCertificatesInStore for importing Windows root certificates
			PublicSystemLibraries.Add("crypt32.lib");
		}
	}
}
