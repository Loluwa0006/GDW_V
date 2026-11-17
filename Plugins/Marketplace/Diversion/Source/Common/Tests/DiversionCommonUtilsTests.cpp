// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionCommonUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogDiversionCommonUtilsTests, Log, All);

// =============================================================================
// GetOperatingSystemName Tests
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGetOperatingSystemName,
	"Diversion.Tests.DiversionCommonUtils.GetOperatingSystemName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FGetOperatingSystemName::RunTest(const FString& Parameters)
{
	FString OsName = DiversionUtils::GetOperatingSystemName();

	TestFalse(TEXT("OS name should not be empty"), OsName.IsEmpty());

	// Verify the value matches the expected OS name for this platform
	FString ExpectedOsName;
#if PLATFORM_WINDOWS
	ExpectedOsName = TEXT("Windows");
#elif PLATFORM_MAC
	ExpectedOsName = TEXT("MacOSX");
#elif PLATFORM_LINUX
	ExpectedOsName = TEXT("Linux");
#else
	ExpectedOsName = TEXT("Other");
#endif

	TestEqual(TEXT("OS name should match the expected value for this platform"), OsName, ExpectedOsName);

	return true;
}

// =============================================================================
// UTF8ToFStringSafe Tests
// =============================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeBasic,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.Basic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeBasic::RunTest(const FString& Parameters)
{
	// Test 1: Basic ASCII string
	{
		const char* Data = "Hello World";
		FString Result = DiversionUtils::UTF8ToFStringSafe(Data, 11);
		TestEqual(TEXT("Basic ASCII string"), Result, TEXT("Hello World"));
	}

	// Test 2: Exact size boundary - prevents buffer overrun
	{
		const char Data[] = "Hello\0Garbage";
		FString Result = DiversionUtils::UTF8ToFStringSafe(Data, 5);
		TestEqual(TEXT("Exact size boundary"), Result, TEXT("Hello"));
	}

	// Test 3: Unicode characters (multi-byte UTF-8)
	{
		const char* Data = reinterpret_cast<const char*>(u8"Hello 世界");
		int32 Size = FCStringAnsi::Strlen(Data);
		FString Result = DiversionUtils::UTF8ToFStringSafe(Data, Size);
		TestEqual(TEXT("Unicode preserved"), Result, TEXT("Hello 世界"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeEmbeddedNull,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.EmbeddedNull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeEmbeddedNull::RunTest(const FString& Parameters)
{
	const char Data[] = {'H', 'e', 'l', 'l', 'o', '\0', 'W', 'o', 'r', 'l', 'd'};

	// Test 1: Default behavior - stops at embedded null
	{
		FString Result = DiversionUtils::UTF8ToFStringSafe(Data, 11, false);
		TestEqual(TEXT("Stops at embedded null"), Result, TEXT("Hello"));
	}

	// Test 2: Preserve embedded nulls flag
	{
		FString Result = DiversionUtils::UTF8ToFStringSafe(Data, 11, true);
		TestEqual(TEXT("Preserves content length with embedded null"), Result.Len(), 11);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeNonTerminated,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.NonTerminated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeNonTerminated::RunTest(const FString& Parameters)
{
	// Non-null-terminated buffer with garbage after valid data
	char Buffer[20];
	FMemory::Memset(Buffer, 'X', 20);  // Fill with garbage
	FMemory::Memcpy(Buffer, "Test", 4);  // No null terminator after "Test"

	FString Result = DiversionUtils::UTF8ToFStringSafe(Buffer, 4);
	TestEqual(TEXT("Non-null-terminated input handled correctly"), Result, TEXT("Test"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeEdgeCases,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.EdgeCases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeEdgeCases::RunTest(const FString& Parameters)
{
	// Test 1: Empty input (size 0)
	{
		FString Result = DiversionUtils::UTF8ToFStringSafe("data", 0);
		TestTrue(TEXT("Zero size returns empty"), Result.IsEmpty());
	}

	// Test 2: Null pointer
	{
		FString Result = DiversionUtils::UTF8ToFStringSafe(nullptr, 10);
		TestTrue(TEXT("Null pointer returns empty"), Result.IsEmpty());
	}

	// Test 3: Negative size
	{
		FString Result = DiversionUtils::UTF8ToFStringSafe("Test", -1);
		TestTrue(TEXT("Negative size returns empty"), Result.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeBufferLeftover,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.BufferLeftover",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeBufferLeftover::RunTest(const FString& Parameters)
{
	// Simulates the original bug: reused buffer has old response data
	char Buffer[100];
	const char* OldResponse = "{\"old\":\"data\",\"more\":\"stuff\"}";
	const char* NewResponse = "{\"new\":1}";

	// Fill buffer with old (longer) response
	FCStringAnsi::Strncpy(Buffer, OldResponse, 100);

	// Overwrite beginning with new (shorter) response - no null terminator placed
	FMemory::Memcpy(Buffer, NewResponse, FCStringAnsi::Strlen(NewResponse));

	// Should only read the new response, not the leftover old data
	FString Result = DiversionUtils::UTF8ToFStringSafe(Buffer, FCStringAnsi::Strlen(NewResponse));
	TestEqual(TEXT("No leftover data from previous response"), Result, TEXT("{\"new\":1}"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUTF8ToFStringSafeLongString,
	"Diversion.Tests.DiversionCommonUtils.UTF8ToFStringSafe.LongString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FUTF8ToFStringSafeLongString::RunTest(const FString& Parameters)
{
	// Test with large string to ensure no buffer issues
	const int32 Length = 10000;
	TArray<char> LongData;
	LongData.SetNumUninitialized(Length);
	FMemory::Memset(LongData.GetData(), 'A', Length);

	FString Result = DiversionUtils::UTF8ToFStringSafe(LongData.GetData(), Length);
	TestEqual(TEXT("Long string length preserved"), Result.Len(), Length);

	return true;
}
