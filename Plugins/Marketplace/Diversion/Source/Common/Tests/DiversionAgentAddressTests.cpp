// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "DiversionAgentAddress.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiversionAgentAddressTests, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDiversionAgentAddressTest, "Diversion.Tests.AgentAddress.GetAgentURL",
								  EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDiversionAgentAddressTest::RunTest(const FString& Parameters)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Setup: Create test directory
	FString TestHomeDir = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("DiversionTest");
	FString TestDir = TestHomeDir / TEXT(".diversion");

	// Clean up any existing test data
	PlatformFile.DeleteDirectoryRecursively(*TestHomeDir);

	// Create test directory
	if (!PlatformFile.DirectoryExists(*TestDir))
	{
		PlatformFile.CreateDirectoryTree(*TestDir);
	}

	FString PortFilePath = TestDir / TEXT(".port");

	// Log paths
	UE_LOG(LogDiversionAgentAddressTests, Log, TEXT("TestHomeDir: %s"), *TestHomeDir);
	UE_LOG(LogDiversionAgentAddressTests, Log, TEXT("PortFilePath: %s"), *PortFilePath);

	// Test Case 1: Valid port in .port file
	{
		FString ValidPort = TEXT("8080");
		FFileHelper::SaveStringToFile(ValidPort, *PortFilePath, FFileHelper::EEncodingOptions::ForceAnsi);

		FString ExpectedURL = FString::Printf(TEXT("http://localhost:%s"), *ValidPort);
		FString ActualURL = FDiversionAgentAddress::GetAgentURL(TestHomeDir);

		TestEqual(TEXT("Valid port should return correct URL"), ActualURL, ExpectedURL);
	}

	// Test Case 2: Invalid port in .port file
	{
		FString InvalidPort = TEXT("NotANumber");
		FFileHelper::SaveStringToFile(InvalidPort, *PortFilePath, FFileHelper::EEncodingOptions::ForceAnsi);

		FString ActualURL = FDiversionAgentAddress::GetAgentURL(TestHomeDir);
		FString ExpectedURL = TEXT("http://localhost:8797");

		TestEqual(TEXT("Invalid port should return default URL"), ActualURL, ExpectedURL);
	}

	// Test Case 3: .port file does not exist
	{
		PlatformFile.DeleteFile(*PortFilePath);

		FString ActualURL = FDiversionAgentAddress::GetAgentURL(TestHomeDir);
		FString ExpectedURL = TEXT("http://localhost:8797");

		TestEqual(TEXT("Missing .port file should return default URL"), ActualURL, ExpectedURL);
	}

	// Cleanup: Remove test directory and files
	PlatformFile.DeleteDirectoryRecursively(*TestHomeDir);

	return true;
}
