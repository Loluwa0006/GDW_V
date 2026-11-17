// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionUtils.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiversionUtilsTests, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiversionUtilsIsLevelPackage, "Diversion.Tests.DiversionUtils.IsLevelPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDiversionUtilsIsLevelPackage::RunTest(const FString& Parameters)
{
	using DiversionUtils::UPackageUtils::IsLevelPackage;
	const FString Content = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());

	TestTrue(TEXT("A level"), IsLevelPackage(Content / TEXT("Maps/Arena.umap")));
	TestTrue(TEXT("An actor package of a level"),
		IsLevelPackage(Content / TEXT("__ExternalActors__/Maps/Arena/A/BC/D1E2F3G4H5.uasset")));
	TestTrue(TEXT("An object package of a level"),
		IsLevelPackage(Content / TEXT("__ExternalObjects__/Maps/Arena/A/BC/D1E2F3G4H5.uasset")));
	TestFalse(TEXT("A plain asset"), IsLevelPackage(Content / TEXT("Materials/M_Base.uasset")));
	TestFalse(TEXT("An asset whose name only starts like the folder"),
		IsLevelPackage(Content / TEXT("Props/__ExternalActors__Backup.uasset")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalizePathWithTrailingSeparatorBasic, "Diversion.Tests.DiversionUtils.NormalizePathWithTrailingSeparator.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNormalizePathWithTrailingSeparatorBasic::RunTest(const FString& Parameters)
{
	// Test 1: Simple path without trailing separator
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/projects"));
		TestTrue(TEXT("Path should end with separator"), Result.EndsWith(TEXT("/")));
		TestTrue(TEXT("Path should start correctly"), Result.StartsWith(TEXT("/home/user/projects")));
	}

	// Test 2: Path already with trailing separator
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/projects/"));
		TestTrue(TEXT("Path with existing separator should still end with separator"), Result.EndsWith(TEXT("/")));
		// Should not have double separators
		TestFalse(TEXT("Path should not end with double separators"), Result.EndsWith(TEXT("//")));
	}

	// Test 3: Empty path
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT(""));
		// Empty path stays empty after normalization
		TestTrue(TEXT("Empty path should return empty string"), Result.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalizePathWithTrailingSeparatorWindows, "Diversion.Tests.DiversionUtils.NormalizePathWithTrailingSeparator.WindowsPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNormalizePathWithTrailingSeparatorWindows::RunTest(const FString& Parameters)
{
	// Test Windows-style paths - note that FPaths::NormalizeDirectoryName converts backslashes to forward slashes

	// Test 1: Windows path with backslashes - should convert all backslashes to forward slashes
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("C:\\Users\\Dev\\Projects"));
		TestEqual(TEXT("Windows path should be normalized with forward slashes and trailing separator"),
			Result, FString(TEXT("C:/Users/Dev/Projects/")));
	}

	// Test 2: Windows path with forward slashes
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("C:/Users/Dev/Projects"));
		TestTrue(TEXT("Windows path with forward slashes should end with separator"), Result.EndsWith(TEXT("/")));
	}

	// Test 3: Windows path with trailing backslash
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("C:\\Users\\Dev\\Projects\\"));
		TestTrue(TEXT("Windows path with trailing backslash should end with separator"), Result.EndsWith(TEXT("/")));
		TestFalse(TEXT("Should not have double separators"), Result.EndsWith(TEXT("//")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalizePathWithTrailingSeparatorEdgeCases, "Diversion.Tests.DiversionUtils.NormalizePathWithTrailingSeparator.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNormalizePathWithTrailingSeparatorEdgeCases::RunTest(const FString& Parameters)
{
	// Test 1: Path with multiple consecutive separators
	// Note: FPaths::NormalizeDirectoryName does NOT collapse multiple slashes - use FPaths::RemoveDuplicateSlashes for that
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home//user///projects"));
		// Multiple slashes are preserved (by design) - only trailing separator is ensured
		TestEqual(TEXT("Path with multiple separators should preserve them and end with trailing separator"),
			Result, FString(TEXT("/home//user///projects/")));
	}

	// Test 2: Invalid paths should return unchanged and log an error
	{
		AddExpectedError(TEXT("NormalizePathWithTrailingSeparator:"), EAutomationExpectedErrorFlags::Contains, 5);

		TestEqual(TEXT("Root path '/' should return unchanged"),
			DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/")), FString(TEXT("/")));
		TestEqual(TEXT("Root path '\\' should return unchanged"),
			DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("\\")), FString(TEXT("\\")));
		TestEqual(TEXT("Current dir '.' should return unchanged"),
			DiversionUtils::NormalizePathWithTrailingSeparator(TEXT(".")), FString(TEXT(".")));
		TestEqual(TEXT("Current dir './' should return unchanged"),
			DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("./")), FString(TEXT("./")));
		TestEqual(TEXT("Current dir '.\\' should return unchanged"),
			DiversionUtils::NormalizePathWithTrailingSeparator(TEXT(".\\")), FString(TEXT(".\\")));
	}

	// Test 3: Path with spaces - should preserve spaces and normalize correctly
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/my projects/game repo"));
		TestEqual(TEXT("Path with spaces should be fully preserved with trailing separator"),
			Result, FString(TEXT("/home/user/my projects/game repo/")));
	}

	// Test 4: Path with dots
	{
		FString Result = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/project.name"));
		TestTrue(TEXT("Path with dots should end with separator"), Result.EndsWith(TEXT("/")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalizePathWithTrailingSeparatorConsistency, "Diversion.Tests.DiversionUtils.NormalizePathWithTrailingSeparator.Consistency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNormalizePathWithTrailingSeparatorConsistency::RunTest(const FString& Parameters)
{
	// Test that the function produces consistent results for equivalent paths

	// Test 1: Same path with and without trailing separator should produce same result
	{
		FString Result1 = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/projects"));
		FString Result2 = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/projects/"));
		TestEqual(TEXT("Path with and without trailing separator should produce same result"), Result1, Result2);
	}

	// Test 2: Calling the function twice should produce the same result (idempotent)
	{
		FString FirstPass = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("/home/user/projects"));
		FString SecondPass = DiversionUtils::NormalizePathWithTrailingSeparator(FirstPass);
		TestEqual(TEXT("Function should be idempotent"), FirstPass, SecondPass);
	}

	// Test 3: Windows and Unix style paths for same location should normalize similarly
	{
		FString UnixStyle = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("C:/Users/Dev"));
		FString WindowsStyle = DiversionUtils::NormalizePathWithTrailingSeparator(TEXT("C:\\Users\\Dev"));
		TestEqual(TEXT("Unix and Windows style paths should normalize to same result"), UnixStyle, WindowsStyle);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNormalizePathWithTrailingSeparatorUsageScenario, "Diversion.Tests.DiversionUtils.NormalizePathWithTrailingSeparator.UsageScenario",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNormalizePathWithTrailingSeparatorUsageScenario::RunTest(const FString& Parameters)
{
	// Test the actual use case - path prefix matching for workspace detection

	FString WorkspacePath = TEXT("/home/user/projects/myrepo");
	FString ProjectPath = TEXT("/home/user/projects/myrepo/UnrealProject");
	FString SimilarPath = TEXT("/home/user/projects/myrepo2/UnrealProject");

	FString NormalizedWorkspace = DiversionUtils::NormalizePathWithTrailingSeparator(WorkspacePath);
	FString NormalizedProject = DiversionUtils::NormalizePathWithTrailingSeparator(ProjectPath);
	FString NormalizedSimilar = DiversionUtils::NormalizePathWithTrailingSeparator(SimilarPath);

	// Test 1: Project inside workspace should match (StartsWith)
	TestTrue(TEXT("Project path should start with workspace path"),
		NormalizedProject.StartsWith(NormalizedWorkspace));

	// Test 2: Similar path should NOT match (this is the key bug that trailing separator fixes)
	TestFalse(TEXT("Similar path (/myrepo2) should NOT start with workspace path (/myrepo/)"),
		NormalizedSimilar.StartsWith(NormalizedWorkspace));

	// Test 3: Without proper normalization, the similar path would incorrectly match
	// This demonstrates why the trailing separator is important
	TestTrue(TEXT("Without trailing separator, similar paths would incorrectly match"),
		FString(TEXT("/home/user/projects/myrepo2")).StartsWith(TEXT("/home/user/projects/myrepo")));

	return true;
}
