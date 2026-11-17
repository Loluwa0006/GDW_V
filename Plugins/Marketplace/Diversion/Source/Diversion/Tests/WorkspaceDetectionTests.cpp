// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionOperations.h"
#include "DiversionWorkspaceInfo.h"
#include "DiversionCommand.h"
#include "DiversionProvider.h"
#include "DiversionModule.h"
#include "DiversionUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorkspaceDetectionTests, Log, All);

/**
 * Test fixture for workspace detection tests
 * Provides mock workspace data and utility functions
 */
class FWorkspaceDetectionTestFixture
{
public:
	static TArray<WorkspaceInfo> CreateMockWorkspaces()
	{
		TArray<WorkspaceInfo> Workspaces;

		// Workspace 1: Simple workspace
		WorkspaceInfo Ws1;
		Ws1.WorkspaceID = "ws1";
		Ws1.WorkspaceName = "Workspace1";
		Ws1.RepoID = "repo1";
		Ws1.RepoName = "MyRepo";
		Ws1.SetPath("/home/user/projects/myrepo");
		Workspaces.Add(Ws1);

		// Workspace 2: Similar prefix to Workspace 1
		WorkspaceInfo Ws2;
		Ws2.WorkspaceID = "ws2";
		Ws2.WorkspaceName = "Workspace2";
		Ws2.RepoID = "repo2";
		Ws2.RepoName = "MyRepo2";
		Ws2.SetPath("/home/user/projects/myrepo2");
		Workspaces.Add(Ws2);

		// Workspace 3: Nested workspace scenario
		WorkspaceInfo Ws3;
		Ws3.WorkspaceID = "ws3";
		Ws3.WorkspaceName = "Workspace3";
		Ws3.RepoID = "repo3";
		Ws3.RepoName = "NestedRepo";
		Ws3.SetPath("/home/user/nested");
		Workspaces.Add(Ws3);

		// Workspace 4: Windows-style path
		WorkspaceInfo Ws4;
		Ws4.WorkspaceID = "ws4";
		Ws4.WorkspaceName = "Workspace4";
		Ws4.RepoID = "repo4";
		Ws4.RepoName = "WindowsRepo";
		Ws4.SetPath("C:\\Users\\Dev\\Projects\\GameRepo");
		Workspaces.Add(Ws4);

		// Workspace 5: Path with spaces
		WorkspaceInfo Ws5;
		Ws5.WorkspaceID = "ws5";
		Ws5.WorkspaceName = "Workspace5";
		Ws5.RepoID = "repo5";
		Ws5.RepoName = "SpacedRepo";
		Ws5.SetPath("/home/user/my projects/game repo");
		Workspaces.Add(Ws5);

		return Workspaces;
	}

	static bool TestWorkspaceDetection(const FString& ProjectPath, const TArray<WorkspaceInfo>& Workspaces, bool ExpectedResult)
	{
		// Normalize paths for comparison using the utility function
		FString ProjectPathWithSeparator = DiversionUtils::NormalizePathWithTrailingSeparator(ProjectPath);

		bool bFound = Workspaces.ContainsByPredicate([&ProjectPathWithSeparator](const WorkspaceInfo& WsInfo) {
			FString WsPathWithSeparator = DiversionUtils::NormalizePathWithTrailingSeparator(WsInfo.GetPath());
			return ProjectPathWithSeparator.StartsWith(WsPathWithSeparator);
		});

		return bFound == ExpectedResult;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestBasic, "Diversion.Tests.WorkspaceDetection.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestBasic::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces = FWorkspaceDetectionTestFixture::CreateMockWorkspaces();

	// Test 1: Project at workspace root
	TestTrue(TEXT("Project at workspace root should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo", Workspaces, true));

	// Test 2: Project in subdirectory of workspace
	TestTrue(TEXT("Project in workspace subdirectory should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo/UnrealProject", Workspaces, true));

	// Test 3: Project in deeply nested subdirectory
	TestTrue(TEXT("Project in deeply nested workspace subdirectory should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo/Games/MyGame/UnrealProject", Workspaces, true));

	// Test 4: Project outside any workspace
	TestTrue(TEXT("Project outside any workspace should not be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/other/project", Workspaces, false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestSimilarPaths, "Diversion.Tests.WorkspaceDetection.SimilarPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestSimilarPaths::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces = FWorkspaceDetectionTestFixture::CreateMockWorkspaces();

	// Test 1: Project in myrepo2 should not match myrepo
	TestTrue(TEXT("Project in myrepo2 should not be detected as being in myrepo"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo2/project", Workspaces, true));

	// Test 2: Verify it's actually in myrepo2
	WorkspaceInfo Ws2;
	Ws2.SetPath("/home/user/projects/myrepo2");
	TArray<WorkspaceInfo> OnlyWs2 = { Ws2 };
	TestTrue(TEXT("Project should be correctly identified as being in myrepo2"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo2/project", OnlyWs2, true));

	// Test 3: Path that starts with workspace name but isn't inside it
	TestTrue(TEXT("Path /home/user/projects/myrepo_other should not match /home/user/projects/myrepo"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/myrepo_other/project", Workspaces, false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestWindowsPaths, "Diversion.Tests.WorkspaceDetection.WindowsPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestWindowsPaths::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces = FWorkspaceDetectionTestFixture::CreateMockWorkspaces();

	// Test Windows-style paths
	TestTrue(TEXT("Windows path at workspace root should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("C:\\Users\\Dev\\Projects\\GameRepo", Workspaces, true));

	TestTrue(TEXT("Windows path in subdirectory should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("C:\\Users\\Dev\\Projects\\GameRepo\\UnrealGame", Workspaces, true));

	// Test with forward slashes on Windows paths (UE normalizes these)
	TestTrue(TEXT("Windows path with forward slashes should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("C:/Users/Dev/Projects/GameRepo/UnrealGame", Workspaces, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestSpacesInPaths, "Diversion.Tests.WorkspaceDetection.SpacesInPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestSpacesInPaths::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces = FWorkspaceDetectionTestFixture::CreateMockWorkspaces();

	// Test paths with spaces
	TestTrue(TEXT("Path with spaces at workspace root should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/my projects/game repo", Workspaces, true));

	TestTrue(TEXT("Path with spaces in subdirectory should be detected"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/my projects/game repo/My Game", Workspaces, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestEdgeCases, "Diversion.Tests.WorkspaceDetection.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestEdgeCases::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces = FWorkspaceDetectionTestFixture::CreateMockWorkspaces();

	// Test 1: Empty project path
	TestTrue(TEXT("Empty project path should not match any workspace"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("", Workspaces, false));

	// Test 2: Root path (expect error log from NormalizePathWithTrailingSeparator)
	AddExpectedError(TEXT("NormalizePathWithTrailingSeparator:"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("Root path should not match any workspace"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/", Workspaces, false));

	// Test 3: Parent directory of workspace
	TestTrue(TEXT("Parent directory of workspace should not match"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects", Workspaces, false));

	// Test 4: Case sensitivity (this behavior may vary by platform)
	// On case-insensitive file systems, this might match
	#if PLATFORM_WINDOWS || PLATFORM_MAC
		// Case-insensitive platforms
		TestTrue(TEXT("Different case path should match on case-insensitive platforms"),
			FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/MyRepo/project", Workspaces, true));
	#else
		// Case-sensitive platforms
		TestTrue(TEXT("Different case path should not match on case-sensitive platforms"),
			FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/projects/MyRepo/project", Workspaces, false));
	#endif

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestMultipleWorkspaces, "Diversion.Tests.WorkspaceDetection.MultipleWorkspaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestMultipleWorkspaces::RunTest(const FString& Parameters)
{
	// Test scenario where multiple workspaces exist and we need to find the correct one
	TArray<WorkspaceInfo> Workspaces;

	// Create overlapping workspace scenarios
	WorkspaceInfo Ws1;
	Ws1.SetPath("/projects/game");
	Ws1.RepoName = "GameRepo";
	Workspaces.Add(Ws1);

	WorkspaceInfo Ws2;
	Ws2.SetPath("/projects/game-engine");
	Ws2.RepoName = "GameEngineRepo";
	Workspaces.Add(Ws2);

	WorkspaceInfo Ws3;
	Ws3.SetPath("/projects/game/submodule");
	Ws3.RepoName = "SubmoduleRepo";
	Workspaces.Add(Ws3);

	// Test correct workspace detection
	TestTrue(TEXT("Project in /projects/game/MyGame should match GameRepo"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/projects/game/MyGame", Workspaces, true));

	TestTrue(TEXT("Project in /projects/game-engine/MyEngine should match GameEngineRepo"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/projects/game-engine/MyEngine", Workspaces, true));

	TestTrue(TEXT("Project in /projects/game/submodule/feature should match SubmoduleRepo (nested workspace)"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/projects/game/submodule/feature", Workspaces, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkspaceDetectionTestNormalization, "Diversion.Tests.WorkspaceDetection.PathNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FWorkspaceDetectionTestNormalization::RunTest(const FString& Parameters)
{
	TArray<WorkspaceInfo> Workspaces;

	WorkspaceInfo Ws1;
	Ws1.SetPath("/home/user/workspace/");  // With trailing slash
	Ws1.RepoName = "TestRepo";
	Workspaces.Add(Ws1);

	// Test various path formats that should all match
	TestTrue(TEXT("Path without trailing slash should match"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/workspace", Workspaces, true));

	TestTrue(TEXT("Path with trailing slash should match"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/workspace/", Workspaces, true));

	// Note: FPaths::NormalizeDirectoryName does not collapse multiple slashes
	TestTrue(TEXT("Path with multiple slashes does not match (not collapsed by NormalizeDirectoryName)"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user//workspace///project", Workspaces, false));

	// FPaths::NormalizeDirectoryName does resolve ./ segments
	TestTrue(TEXT("Path with ./ should be normalized and match"),
		FWorkspaceDetectionTestFixture::TestWorkspaceDetection("/home/user/workspace/./project", Workspaces, true));

	return true;
}
