// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "CustomWidgets/DiversionLockContextMenu.h"

namespace
{
	FDiversionLockMenuFileInfo MakeFileInfo(const FString& Path, bool bLocked, bool bByOther, const FString& Holder = FString())
	{
		FDiversionLockMenuFileInfo Info;
		Info.FilePath = Path;
		Info.bIsLocked = bLocked;
		Info.bLockedByOther = bByOther;
		Info.HolderDisplayName = Holder;
		return Info;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMenuStateBuckets,
	"Diversion.Tests.LockContextMenu.ComputeLockMenuStateBuckets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLockMenuStateBuckets::RunTest(const FString& Parameters)
{
	// Empty selection yields no targets
	{
		const FDiversionLockMenuState State = ComputeLockMenuState({});
		TestEqual(TEXT("Empty: no lock targets"), State.LockTargets.Num(), 0);
		TestEqual(TEXT("Empty: no unlock targets"), State.UnlockTargets.Num(), 0);
		TestEqual(TEXT("Empty: no force targets"), State.ForceUnlockTargets.Num(), 0);
	}

	// Every file lands in exactly one bucket
	{
		const FDiversionLockMenuState State = ComputeLockMenuState({
			MakeFileInfo(TEXT("C:/ws/Content/Free.uasset"), false, false),
			MakeFileInfo(TEXT("C:/ws/Content/Mine.uasset"), true, false),
			MakeFileInfo(TEXT("C:/ws/Content/Theirs.uasset"), true, true, TEXT("Alice")),
		});
		TestEqual(TEXT("Mixed: one lock target"), State.LockTargets.Num(), 1);
		TestEqual(TEXT("Mixed: lock target is the unlocked file"), State.LockTargets[0], TEXT("C:/ws/Content/Free.uasset"));
		TestEqual(TEXT("Mixed: one unlock target"), State.UnlockTargets.Num(), 1);
		TestEqual(TEXT("Mixed: unlock target is my file"), State.UnlockTargets[0], TEXT("C:/ws/Content/Mine.uasset"));
		TestEqual(TEXT("Mixed: one force target"), State.ForceUnlockTargets.Num(), 1);
		TestEqual(TEXT("Mixed: force target keeps its holder"), State.ForceUnlockTargets[0].HolderDisplayName, TEXT("Alice"));
	}

	// Homogeneous selections fill only their own bucket
	{
		const FDiversionLockMenuState State = ComputeLockMenuState({
			MakeFileInfo(TEXT("A.uasset"), true, true, TEXT("Bob")),
			MakeFileInfo(TEXT("B.uasset"), true, true, TEXT("Carol")),
		});
		TestEqual(TEXT("All-other: no lock targets"), State.LockTargets.Num(), 0);
		TestEqual(TEXT("All-other: no unlock targets"), State.UnlockTargets.Num(), 0);
		TestEqual(TEXT("All-other: two force targets"), State.ForceUnlockTargets.Num(), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMenuForceUnlockMessage,
	"Diversion.Tests.LockContextMenu.ForceUnlockMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLockMenuForceUnlockMessage::RunTest(const FString& Parameters)
{
	// Single target: names the holder and the asset
	{
		const FText Message = BuildForceUnlockMessage({ MakeFileInfo(TEXT("C:/ws/Content/Hero.uasset"), true, true, TEXT("Alice")) });
		TestTrue(TEXT("Single: names holder"), Message.ToString().Contains(TEXT("Alice")));
		TestTrue(TEXT("Single: names asset without extension"), Message.ToString().Contains(TEXT("'Hero'")));
	}

	// Multiple targets: one holder+file line each
	{
		const FText Message = BuildForceUnlockMessage({
			MakeFileInfo(TEXT("A.uasset"), true, true, TEXT("Alice")),
			MakeFileInfo(TEXT("B.uasset"), true, true, TEXT("Bob")),
		});
		TestTrue(TEXT("Multi: mentions count"), Message.ToString().Contains(TEXT("2")));
		TestTrue(TEXT("Multi: lists first holder"), Message.ToString().Contains(TEXT("Alice")));
		TestTrue(TEXT("Multi: lists second holder"), Message.ToString().Contains(TEXT("Bob")));
	}

	// Above the cap the list truncates with a remainder note
	{
		TArray<FDiversionLockMenuFileInfo> Targets;
		for (int32 i = 0; i < 5; ++i)
		{
			Targets.Add(MakeFileInfo(FString::Printf(TEXT("File%d.uasset"), i), true, true, FString::Printf(TEXT("User%d"), i)));
		}
		const FText Message = BuildForceUnlockMessage(Targets, /*MaxListed*/ 3);
		TestTrue(TEXT("Truncated: lists up to the cap"), Message.ToString().Contains(TEXT("User2")));
		TestFalse(TEXT("Truncated: capped entries are not listed"), Message.ToString().Contains(TEXT("User3")));
		TestTrue(TEXT("Truncated: remainder note present"), Message.ToString().Contains(TEXT("2 more")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMenuResultNotification,
	"Diversion.Tests.LockContextMenu.ResultNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLockMenuResultNotification::RunTest(const FString& Parameters)
{
	using DiversionUtils::EDiversionLockAction;
	using DiversionUtils::FDiversionLockOpFailure;

	bool bIsFailure = false;

	// Nothing succeeded, nothing failed (e.g. all paths skipped) - nothing to report
	TestTrue(TEXT("No-op yields empty message"),
		BuildLockResultNotification(EDiversionLockAction::Lock, 0, {}, bIsFailure).IsEmpty());
	TestFalse(TEXT("No-op is not a failure"), bIsFailure);

	// Pure success wording per action
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::Lock, 2, {}, bIsFailure);
		TestFalse(TEXT("Lock success is not a failure"), bIsFailure);
		TestTrue(TEXT("Lock success mentions locking"), Message.ToString().Contains(TEXT("Locked")));
	}
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::Unlock, 1, {}, bIsFailure);
		TestTrue(TEXT("Unlock success mentions unlocking"), Message.ToString().Contains(TEXT("Unlocked")));
	}

	// Server detail is surfaced verbatim when present
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::Lock, 0,
			{ { TEXT("C:/ws/Hero.uasset"), 409, TEXT("file is locked by Alice") } }, bIsFailure);
		TestTrue(TEXT("409 is a failure"), bIsFailure);
		TestTrue(TEXT("409 surfaces the server detail"), Message.ToString().Contains(TEXT("locked by Alice")));
	}

	// 403 unlock points at Force Unlock; 403 force points at admin permissions
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::Unlock, 0,
			{ { TEXT("Hero.uasset"), 403, FString() } }, bIsFailure);
		TestTrue(TEXT("403 unlock mentions Force Unlock"), Message.ToString().Contains(TEXT("Force Unlock")));
	}
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::ForceUnlock, 0,
			{ { TEXT("Hero.uasset"), 403, FString() } }, bIsFailure);
		TestTrue(TEXT("403 force mentions admin"), Message.ToString().Contains(TEXT("admin")));
	}

	// Multiple failures aggregate into one message with a remainder note
	{
		const FText Message = BuildLockResultNotification(EDiversionLockAction::Lock, 1,
			{ { TEXT("A.uasset"), 409, FString() }, { TEXT("B.uasset"), 409, FString() } }, bIsFailure);
		TestTrue(TEXT("Aggregated failures are a failure"), bIsFailure);
		TestTrue(TEXT("Aggregation notes the remainder"), Message.ToString().Contains(TEXT("1 more")));
		// A mixed batch must lead with what succeeded - not read as total failure
		TestTrue(TEXT("Mixed batch mentions the success count"), Message.ToString().Contains(TEXT("Locked 1")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLockMenuTransportFailureNotification,
	"Diversion.Tests.LockContextMenu.TransportFailureNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLockMenuTransportFailureNotification::RunTest(const FString& Parameters)
{
	using DiversionUtils::EDiversionLockAction;

	// A dead connection must never be a silent no-op: even with zero results the
	// transport toast has content
	{
		const FText Message = BuildLockTransportFailureNotification(EDiversionLockAction::Lock, 0, 0);
		TestFalse(TEXT("Zero-result transport failure still yields a message"), Message.IsEmpty());
		TestTrue(TEXT("Transport failure names the lock service"),
			Message.ToString().Contains(TEXT("lock service")));
		TestTrue(TEXT("Transport failure names the interrupted action"),
			Message.ToString().Contains(TEXT("lock operation")));
	}

	// Files processed before the failure and files never attempted are both accounted for
	{
		const FText Message = BuildLockTransportFailureNotification(EDiversionLockAction::Lock, 2, 3);
		TestTrue(TEXT("Partial success is credited"), Message.ToString().Contains(TEXT("Locked 2")));
		TestTrue(TEXT("Unattempted remainder is counted"),
			Message.ToString().Contains(TEXT("3 files were not attempted")));
	}

	// Per-action verb
	{
		const FText Message = BuildLockTransportFailureNotification(EDiversionLockAction::Unlock, 0, 1);
		TestTrue(TEXT("Unlock transport failure says unlock"),
			Message.ToString().Contains(TEXT("unlock operation")));
		TestTrue(TEXT("Single unattempted file uses singular"),
			Message.ToString().Contains(TEXT("1 file was not attempted")));
	}

	return true;
}
