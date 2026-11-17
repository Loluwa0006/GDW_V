// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionProvider.h"
#include "DiversionUtils.h"

// Backend Tier values: EDUCATION, INDIE, PROFESSIONAL, STUDIO, ENTERPRISE, UNKNOWN
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHardLocksEligibleTierMatchesStudioPlus,
	"Diversion.Tests.HardLocksEligibility.EligibleTierMatchesStudioPlus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHardLocksEligibleTierMatchesStudioPlus::RunTest(const FString& Parameters)
{
	// Eligible tiers, any casing (the backend serializes upper-case; be liberal on input)
	TestTrue(TEXT("STUDIO is eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("STUDIO")));
	TestTrue(TEXT("Studio (mixed case) is eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("Studio")));
	TestTrue(TEXT("ENTERPRISE is eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("ENTERPRISE")));
	TestTrue(TEXT("enterprise (lower case) is eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("enterprise")));

	// Every other backend tier value is not eligible
	TestFalse(TEXT("PROFESSIONAL is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("PROFESSIONAL")));
	TestFalse(TEXT("INDIE is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("INDIE")));
	TestFalse(TEXT("EDUCATION is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("EDUCATION")));
	TestFalse(TEXT("UNKNOWN is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("UNKNOWN")));

	// Degenerate inputs must never be eligible (fail-closed)
	TestFalse(TEXT("Empty tier is not eligible"), DiversionUtils::IsHardLocksEligibleTier(FString()));
	TestFalse(TEXT("Garbage tier is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("STUDIOS")));
	TestFalse(TEXT("Substring tier is not eligible"), DiversionUtils::IsHardLocksEligibleTier(TEXT("ENTER")));

	return true;
}

// ShouldCheckHardLocks must be fail-closed: skip lock checks unless positively eligible
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHardLocksEligibilityFailClosed,
	"Diversion.Tests.HardLocksEligibility.ShouldCheckHardLocksFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FHardLocksEligibilityFailClosed::RunTest(const FString& Parameters)
{
	// No Init() - mirrors the pre-resolution window
	FDiversionProvider Provider;

	TestFalse(TEXT("Unresolved eligibility must skip lock checks (fail-closed)"),
		Provider.ShouldCheckHardLocks());

	Provider.UpdateHardLocksEligibility(true);
	TestTrue(TEXT("Positively eligible repo must run lock checks"),
		Provider.ShouldCheckHardLocks());

	Provider.UpdateHardLocksEligibility(false);
	TestFalse(TEXT("Positively ineligible repo must skip lock checks"),
		Provider.ShouldCheckHardLocks());

	Provider.UpdateHardLocksEligibility(TOptional<bool>());
	TestFalse(TEXT("Resetting to unresolved must skip lock checks again (fail-closed)"),
		Provider.ShouldCheckHardLocks());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHardLocksEligibilityRefreshDue,
	"Diversion.Tests.HardLocksEligibility.RefreshDueUntilResolved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** A startup fetch that fails must not leave the session without hard locks: the poll keeps
 * asking while the value is unresolved, and stops once an answer has landed (until it ages). */
bool FHardLocksEligibilityRefreshDue::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;

	TestTrue(TEXT("Unresolved eligibility is due for a refresh on every poll"),
		Provider.IsHardLocksEligibilityRefreshDue());

	Provider.UpdateHardLocksEligibility(false);
	TestFalse(TEXT("A fresh answer - even a negative one - is not due again"),
		Provider.IsHardLocksEligibilityRefreshDue());

	Provider.UpdateHardLocksEligibility(true);
	TestFalse(TEXT("A fresh positive answer is not due again"),
		Provider.IsHardLocksEligibilityRefreshDue());
	TestTrue(TEXT("Resolved value gates lock checks"), Provider.ShouldCheckHardLocks());

	Provider.UpdateHardLocksEligibility(TOptional<bool>());
	TestTrue(TEXT("Back to unresolved is due again"),
		Provider.IsHardLocksEligibilityRefreshDue());

	return true;
}
