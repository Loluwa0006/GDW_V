// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "DiversionUtils.h"
#include "DiversionState.h"
#include "DiversionProvider.h"
#include "DiversionConfig.h"
#include "Merge.h"

namespace
{
	FDiversionLockInfo MakeLockInfo(const FString& LockedBy, const FString& LockedByName, bool bByOther)
	{
		FDiversionLockInfo Info;
		Info.LockedBy = LockedBy;
		Info.LockedByName = LockedByName;
		Info.bLockedByOther = bByOther;
		return Info;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionLockFailureClassification,
	"Diversion.Tests.SaveDecision.ClassifyLockFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSaveDecisionLockFailureClassification::RunTest(const FString& Parameters)
{
	using DiversionUtils::ClassifyLockAcquisitionFailure;
	using DiversionUtils::ELockAcquisitionFailure;

	const FDiversionLockInfo NoLock;
	const FDiversionLockInfo TheirLock = MakeLockInfo(TEXT("user-b"), TEXT("Alice"), /*bByOther*/ true);
	const FDiversionLockInfo OurLock = MakeLockInfo(TEXT("user-a"), TEXT("Me"), /*bByOther*/ false);

	// A cached holder wins regardless of the status code
	TestTrue(TEXT("409 + cached holder names the holder"),
		ClassifyLockAcquisitionFailure(409, TheirLock) == ELockAcquisitionFailure::HeldByKnownUser);
	TestTrue(TEXT("Other status + cached holder still names the holder"),
		ClassifyLockAcquisitionFailure(400, TheirLock) == ELockAcquisitionFailure::HeldByKnownUser);

	// Lock-conflict codes without a known holder notify namelessly
	TestTrue(TEXT("409 without cached holder is held by someone unnamed"),
		ClassifyLockAcquisitionFailure(409, NoLock) == ELockAcquisitionFailure::HeldByUnknownUser);
	// A stale "ours" cache entry can't name the actual holder either
	TestTrue(TEXT("409 with stale own-lock cache is held by someone unnamed"),
		ClassifyLockAcquisitionFailure(409, OurLock) == ELockAcquisitionFailure::HeldByUnknownUser);
	// 423 Locked is the same fact from any other path - the fresh race the lock exists for
	TestTrue(TEXT("423 without cached holder is held by someone unnamed"),
		ClassifyLockAcquisitionFailure(423, NoLock) == ELockAcquisitionFailure::HeldByUnknownUser);

	// A failed request must never be mistaken for a verdict on the file
	TestTrue(TEXT("400 is unexpected, not a verdict on the file"),
		ClassifyLockAcquisitionFailure(400, NoLock) == ELockAcquisitionFailure::Unexpected);
	TestTrue(TEXT("401 is unexpected, not a verdict on the file"),
		ClassifyLockAcquisitionFailure(401, NoLock) == ELockAcquisitionFailure::Unexpected);
	TestTrue(TEXT("403 is unexpected, not a verdict on the file"),
		ClassifyLockAcquisitionFailure(403, NoLock) == ELockAcquisitionFailure::Unexpected);
	TestTrue(TEXT("500 is unexpected, not a verdict on the file"),
		ClassifyLockAcquisitionFailure(500, NoLock) == ELockAcquisitionFailure::Unexpected);
	TestTrue(TEXT("502 is unexpected, not a verdict on the file"),
		ClassifyLockAcquisitionFailure(502, NoLock) == ELockAcquisitionFailure::Unexpected);

	// What each failure does to the save
	using DiversionUtils::ResolveLockFailure;
	using DiversionUtils::EDiversionSaveChoice;

	TestTrue(TEXT("A file held by someone else is skipped"),
		ResolveLockFailure(ELockAcquisitionFailure::HeldByKnownUser) == EDiversionSaveChoice::Skip);
	TestTrue(TEXT("A file held by someone unnamed is skipped too"),
		ResolveLockFailure(ELockAcquisitionFailure::HeldByUnknownUser) == EDiversionSaveChoice::Skip);
	TestTrue(TEXT("An unexpected error still saves - no lock is no reason to lose work"),
		ResolveLockFailure(ELockAcquisitionFailure::Unexpected) == EDiversionSaveChoice::SaveUnlocked);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionRouting,
	"Diversion.Tests.SaveDecision.RouteSaveDecision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSaveDecisionRouting::RunTest(const FString& Parameters)
{
	using namespace DiversionUtils;

	const FDiversionLockInfo NoLock;
	const FDiversionLockInfo TheirLock = MakeLockInfo(TEXT("user-b"), TEXT("Alice"), /*bByOther*/ true);
	const FDiversionLockInfo OurLock = MakeLockInfo(TEXT("user-a"), TEXT("Me"), /*bByOther*/ false);

	// Route(...) argument order: LockInfo, bSoftLocked, bEligible, bPromptSetting, bInteractive.
	// The repo-rule flag is false throughout; RouteRule(...) below is the same with it true.
	EDiversionSaveCategory Category;
	const auto Route = [&Category](const FDiversionLockInfo& Lock, bool bSoft, bool bEligible,
		bool bPrompt, bool bInteractive)
	{
		return RouteSaveDecision(Lock, bSoft, /*bAutoLockedByRule*/ false, bEligible, bPrompt, bInteractive, Category);
	};
	const auto RouteRule = [&Category](const FDiversionLockInfo& Lock, bool bSoft, bool bEligible,
		bool bPrompt, bool bInteractive)
	{
		return RouteSaveDecision(Lock, bSoft, /*bAutoLockedByRule*/ true, bEligible, bPrompt, bInteractive, Category);
	};

	// --- LockedByMe: our lock allows silently, whatever the settings
	TestTrue(TEXT("Own lock allows"),
		Route(OurLock, false, true, true, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Own lock category"), Category == EDiversionSaveCategory::LockedByMe);
	TestTrue(TEXT("Own lock allows even when also soft-clashed"),
		Route(OurLock, true, true, true, true) == EDiversionSaveVetoAction::Allow);

	// --- HardLockedByOther: a decision when someone can make it, and a plain save when nobody can
	TestTrue(TEXT("Their lock prompts"),
		Route(TheirLock, false, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Their lock category"), Category == EDiversionSaveCategory::HardLockedByOther);
	TestTrue(TEXT("Their lock prompts even with the opt-in setting off"),
		Route(TheirLock, false, true, false, true) == EDiversionSaveVetoAction::Prompt);
	// A save the user did not ask for is never refused - the commit rejection enforces the lock
	TestTrue(TEXT("Their lock fails open non-interactive"),
		Route(TheirLock, false, true, true, false) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Their lock category survives failing open"),
		Category == EDiversionSaveCategory::HardLockedByOther);
	// Hard state outranks soft state
	TestTrue(TEXT("Their lock wins over a simultaneous soft clash"),
		Route(TheirLock, true, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Hard-over-soft category"), Category == EDiversionSaveCategory::HardLockedByOther);

	// --- SoftLocked: prompts when someone can answer, fails open otherwise,
	//     and is evaluated even on tiers without hard locks
	TestTrue(TEXT("Soft clash prompts"),
		Route(NoLock, true, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Soft clash category"), Category == EDiversionSaveCategory::SoftLocked);
	TestTrue(TEXT("Soft clash fails open non-interactive"),
		Route(NoLock, true, true, true, false) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Soft clash prompts on lock-ineligible tiers too"),
		Route(NoLock, true, false, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Soft clash prompts with the opt-in setting off"),
		Route(NoLock, true, true, false, true) == EDiversionSaveVetoAction::Prompt);

	// --- UnlockedLockable: the opt-in prompt, gated on tier + setting + interactivity
	TestTrue(TEXT("Plain file prompts with setting on"),
		Route(NoLock, false, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Plain file category"), Category == EDiversionSaveCategory::UnlockedLockable);
	TestTrue(TEXT("Plain file allows with setting off"),
		Route(NoLock, false, true, false, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Plain file allows non-interactive"),
		Route(NoLock, false, true, true, false) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Plain file allows on lock-ineligible tiers"),
		Route(NoLock, false, false, true, true) == EDiversionSaveVetoAction::Allow);
	// A lock entry on an ineligible tier is stale noise - it must not produce a hard category
	TestTrue(TEXT("Lock state is ignored when the tier is ineligible"),
		Route(TheirLock, false, false, true, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Ineligible-tier category is unlocked"), Category == EDiversionSaveCategory::UnlockedLockable);

	// --- AutoLockedByRule: a file the repo locks on sync has nothing to decide, but the rule
	//     never outranks someone else's lock or a soft clash, and means nothing off-tier
	TestTrue(TEXT("Rule-matched file allows with the prompt setting on"),
		RouteRule(NoLock, false, true, true, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Rule-matched category"), Category == EDiversionSaveCategory::AutoLockedByRule);
	TestTrue(TEXT("Rule-matched file allows with the prompt setting off"),
		RouteRule(NoLock, false, true, false, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Rule-matched category survives the setting being off"),
		Category == EDiversionSaveCategory::AutoLockedByRule);
	TestTrue(TEXT("Rule means nothing on a lock-ineligible tier"),
		RouteRule(NoLock, false, false, true, true) == EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Ineligible-tier rule category is plain unlocked"),
		Category == EDiversionSaveCategory::UnlockedLockable);
	TestTrue(TEXT("Their lock still prompts on a rule-matched file"),
		RouteRule(TheirLock, false, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Their lock category wins over the rule"), Category == EDiversionSaveCategory::HardLockedByOther);
	TestTrue(TEXT("Soft clash still prompts on a rule-matched file"),
		RouteRule(NoLock, true, true, true, true) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Soft clash category wins over the rule"), Category == EDiversionSaveCategory::SoftLocked);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionAutoLockRuleSkipsPrompt,
	"Diversion.Tests.SaveDecision.SaveVetoAutoLockRuleSkipsPrompt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * A file the repo auto-locks on sync must not be offered "save without lock" - the server
 * would reverse that answer seconds later (QA saw nine World Partition actors come back
 * locked after choosing it). Such a file routes as AutoLockedByRule and saves without asking.
 *
 * Observed through the gate's own routing (EvaluateFile) so the category is asserted directly,
 * deterministically in interactive and unattended runs alike.
 */
bool FSaveDecisionAutoLockRuleSkipsPrompt::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;

	WorkspaceInfo Ws;
	Ws.WorkspaceID = TEXT("test-workspace");
	Ws.RepoID = TEXT("test-repo");
	Ws.AccountID = TEXT("test-account");
	Ws.BranchID = TEXT("test-branch");
	Ws.CommitID = TEXT("test-commit");
	Ws.SetPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
	Provider.SetWorkspaceInfo(Ws);
	Provider.UpdateHardLocksEligibility(true);

	UDiversionConfig* Config = GetMutableDefault<UDiversionConfig>();
	const bool bOriginalPrompt = Config->bPromptToLockOnSave;
	Config->bPromptToLockOnSave = true;

	const FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("DiversionSaveVetoAutoLockRuleTest.uasset"));

	// Eligibility is on, so the gate wants the lock state verified against the backend first;
	// there is none here, so seed the gesture cache with a verified "no lock" instead
	Provider.GetSaveDecisionGate().GestureVerifiedLocks.Add(FullPath, FDiversionLockInfo());

	DiversionUtils::EDiversionSaveCategory Category;
	FDiversionLockInfo LockInfo;
	FString Holder;
	const auto Evaluate = [&]()
	{
		return Provider.GetSaveDecisionGate().EvaluateFile(FullPath, /*bInInteractive*/ true, Category, LockInfo, Holder);
	};

	// Control: with the patterns unresolved the file is plain unlocked and would prompt
	TestTrue(TEXT("Patterns unresolved: routes as unlocked and prompts"),
		Evaluate() == DiversionUtils::EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Patterns unresolved: category is plain unlocked"),
		Category == DiversionUtils::EDiversionSaveCategory::UnlockedLockable);

	// Resolved but not covering the file: still plain unlocked
	Provider.SetLockablePatterns(TArray<FString>{ TEXT(".umap") });
	TestTrue(TEXT("Patterns not matching: still prompts"),
		Evaluate() == DiversionUtils::EDiversionSaveVetoAction::Prompt);
	TestFalse(TEXT("IsAutoLockedByRepoRule agrees"), Provider.IsAutoLockedByRepoRule(FullPath));

	// The rule under test: a covered file has nothing to decide
	Provider.SetLockablePatterns(TArray<FString>{ TEXT(".umap"), TEXT(".uasset") });
	TestTrue(TEXT("IsAutoLockedByRepoRule sees the match"), Provider.IsAutoLockedByRepoRule(FullPath));
	TestTrue(TEXT("Patterns matching: saves without a decision"),
		Evaluate() == DiversionUtils::EDiversionSaveVetoAction::Allow);
	TestTrue(TEXT("Patterns matching: category is the repo rule"),
		Category == DiversionUtils::EDiversionSaveCategory::AutoLockedByRule);

	// And the rule is tier-gated like every other lock check
	Provider.UpdateHardLocksEligibility(false);
	TestFalse(TEXT("Rule means nothing on an ineligible tier"), Provider.IsAutoLockedByRepoRule(FullPath));

	Config->bPromptToLockOnSave = bOriginalPrompt;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionNoAutoLockRules,
	"Diversion.Tests.SaveDecision.NoAutoLockRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * Drives the save dialog's "no auto-lock rules" warning: only a resolved, empty pattern list on
 * an eligible tier counts - an unresolved list must not claim the repo has no rules.
 */
bool FSaveDecisionNoAutoLockRules::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;
	Provider.UpdateHardLocksEligibility(true);

	TestFalse(TEXT("Patterns unresolved: not known to have no rules"), Provider.HasNoAutoLockRules());

	Provider.SetLockablePatterns(TArray<FString>());
	TestTrue(TEXT("Resolved and empty: no rules"), Provider.HasNoAutoLockRules());

	Provider.SetLockablePatterns(TArray<FString>{ TEXT(".umap") });
	TestFalse(TEXT("Resolved with a pattern: has rules"), Provider.HasNoAutoLockRules());

	Provider.SetLockablePatterns(TArray<FString>());
	Provider.UpdateHardLocksEligibility(false);
	TestFalse(TEXT("Ineligible tier: no lock to warn about"), Provider.HasNoAutoLockRules());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionDefaultChoice,
	"Diversion.Tests.SaveDecision.DefaultSaveChoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * The answer a dialog row starts on: the "Lock by Default on Save" setting picks between
 * Lock & Save and Save without lock, but never overrides someone else's lock (Skip) or offers
 * a lock where there is none to take.
 */
bool FSaveDecisionDefaultChoice::RunTest(const FString& Parameters)
{
	using namespace DiversionUtils;

	TestTrue(TEXT("Lock by default: unlocked file starts on Lock & Save"),
		DefaultSaveChoice(EDiversionSaveCategory::UnlockedLockable, true, true) == EDiversionSaveChoice::LockAndSave);
	TestTrue(TEXT("Save by default: unlocked file starts on Save"),
		DefaultSaveChoice(EDiversionSaveCategory::UnlockedLockable, true, false) == EDiversionSaveChoice::SaveUnlocked);
	TestTrue(TEXT("Save by default applies to soft clashes too"),
		DefaultSaveChoice(EDiversionSaveCategory::SoftLocked, true, false) == EDiversionSaveChoice::SaveUnlocked);
	TestTrue(TEXT("No lock on offer: starts on Save whatever the setting"),
		DefaultSaveChoice(EDiversionSaveCategory::UnlockedLockable, false, true) == EDiversionSaveChoice::SaveUnlocked);
	TestTrue(TEXT("Someone else's lock: starts on Skip with the setting on"),
		DefaultSaveChoice(EDiversionSaveCategory::HardLockedByOther, false, true) == EDiversionSaveChoice::Skip);
	TestTrue(TEXT("Someone else's lock: starts on Skip with the setting off"),
		DefaultSaveChoice(EDiversionSaveCategory::HardLockedByOther, false, false) == EDiversionSaveChoice::Skip);
	TestTrue(TEXT("Changed on disk: starts on Skip with the setting on"),
		DefaultSaveChoice(EDiversionSaveCategory::ChangedOnDisk, false, true) == EDiversionSaveChoice::Skip);
	TestTrue(TEXT("Changed on disk: starts on Skip with the setting off"),
		DefaultSaveChoice(EDiversionSaveCategory::ChangedOnDisk, false, false) == EDiversionSaveChoice::Skip);

	// The setting is hidden while the prompt is off, so it must not take effect then either
	UDiversionConfig* Config = GetMutableDefault<UDiversionConfig>();
	const bool bOriginalPrompt = Config->bPromptToLockOnSave;
	const bool bOriginalLockByDefault = Config->bLockByDefaultOnSave;
	Config->bLockByDefaultOnSave = false;
	Config->bPromptToLockOnSave = true;
	TestFalse(TEXT("Prompt on: the setting applies"), IsDiversionLockByDefaultOnSaveEnabled());
	Config->bPromptToLockOnSave = false;
	TestTrue(TEXT("Prompt off: the hidden setting is ignored"), IsDiversionLockByDefaultOnSaveEnabled());
	Config->bPromptToLockOnSave = bOriginalPrompt;
	Config->bLockByDefaultOnSave = bOriginalLockByDefault;

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionChangedOnDiskRouting,
	"Diversion.Tests.SaveDecision.RouteChangedOnDiskSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * A file changed on disk is always a question when someone can answer it, and never saved
 * when nobody can - not saving loses nothing, saving loses the change on disk.
 */
bool FSaveDecisionChangedOnDiskRouting::RunTest(const FString& Parameters)
{
	using namespace DiversionUtils;

	EDiversionSaveCategory Category = EDiversionSaveCategory::UnlockedLockable;
	TestTrue(TEXT("Interactive: prompts"),
		RouteChangedOnDiskSave(/*bInInteractiveSession*/ true, Category) == EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Interactive: changed-on-disk category"), Category == EDiversionSaveCategory::ChangedOnDisk);

	Category = EDiversionSaveCategory::UnlockedLockable;
	TestTrue(TEXT("Unattended: refused"),
		RouteChangedOnDiskSave(/*bInInteractiveSession*/ false, Category) == EDiversionSaveVetoAction::Refuse);
	TestTrue(TEXT("Unattended: changed-on-disk category"), Category == EDiversionSaveCategory::ChangedOnDisk);

	TestEqual(TEXT("Accept Incoming has its analytics name"),
		FString(SaveChoiceToString(EDiversionSaveChoice::AcceptIncoming)), FString(TEXT("accept_incoming")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionProviderMergeWiring,
	"Diversion.Tests.SaveDecision.SaveVetoMergeWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * Provider-level wiring test for the merge short-circuit: RouteSaveDecision's merge cases
 * are unit-covered, but only this test proves IsPackageOKToSave actually derives the flag
 * from the provider's open-merge lists (workspace AND branch - a merge is only ever filed
 * under one of them). A veto-able control file - one with a Skip already answered for this
 * save, deterministic in both interactive and unattended runs and needing no backend lock
 * query - must flip to Allow when the merges list is non-empty, and flip back when it empties.
 */
bool FSaveDecisionProviderMergeWiring::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;

	// Availability gates the whole veto (fail-open when down); valid workspace info turns it on
	WorkspaceInfo Ws;
	Ws.WorkspaceID = TEXT("test-workspace");
	Ws.RepoID = TEXT("test-repo");
	Ws.AccountID = TEXT("test-account");
	Ws.BranchID = TEXT("test-branch");
	Ws.CommitID = TEXT("test-commit");
	Ws.SetPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
	Provider.SetWorkspaceInfo(Ws);

	const FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("DiversionSaveVetoMergeWiringTest.uasset"));

	// A Skip already answered for this save vetoes the file - unless the save is one that
	// must never be held, which is decided before answers are consulted
	Provider.GetSaveDecisionGate().GestureAnswers.Add(FullPath, DiversionUtils::EDiversionSaveChoice::Skip);
	// Mark the file as already toasted about, so the refusal stays UI-free under NullRHI
	Provider.GetSaveDecisionGate().ToastedThisSave.Add(FullPath);

	// Control: with no merge open, the Skip must veto - otherwise the merge assertions below
	// would pass vacuously
	TestFalse(TEXT("No merge open: the Skip vetoes the save"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(nullptr, FullPath, nullptr));

	// The wiring under test: an open merge must reach RouteSaveDecision and win over the
	// veto from EITHER list. GetConflictedFiles splits merges by ref - workspace-update
	// merges into the workspace list, branch-to-branch merges into the branch list - and
	// the resolve/finalize flow runs for both kinds (the 2.0.23 regression: only the
	// workspace list was checked, so every branch merge still hit the prompt).
	Diversion::CoreAPI::Model::Merge OpenMerge;
	OpenMerge.mId = TEXT("test-merge-id");
	Provider.SetWorkspaceMergesList({ OpenMerge });
	TestTrue(TEXT("Workspace merge open: the same save is allowed"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(nullptr, FullPath, nullptr));

	Provider.SetWorkspaceMergesList({});
	Provider.SetBranchMergesList({ OpenMerge });
	TestTrue(TEXT("Branch merge open: the same save is allowed"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(nullptr, FullPath, nullptr));

	// The flag must be derived per save, not latched by the first one
	Provider.SetBranchMergesList({});
	TestFalse(TEXT("Merge closed again: the veto is back"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(nullptr, FullPath, nullptr));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionMergeKeepsDiskCheck,
	"Diversion.Tests.SaveDecision.MergeKeepsDiskCheck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * An open merge exempts every save from the lock questions, but a file changed on disk keeps
 * its question unless the merge itself resolves it - the merge writes those over on purpose.
 */
bool FSaveDecisionMergeKeepsDiskCheck::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;
	FDiversionSaveDecisionGate& Gate = Provider.GetSaveDecisionGate();
	const FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("DiversionMergeKeepsDiskCheckTest.uasset"));

	TestFalse(TEXT("No merge open: a file changed on disk is held"),
		Gate.ShouldNeverHoldSave(nullptr, FullPath, /*bInChangedOnDisk*/ true));

	Diversion::CoreAPI::Model::Merge OpenMerge;
	OpenMerge.mId = TEXT("test-merge-id");
	Provider.SetWorkspaceMergesList({ OpenMerge });
	TestTrue(TEXT("Merge open: any other save is never held"),
		Gate.ShouldNeverHoldSave(nullptr, FullPath, /*bInChangedOnDisk*/ false));
	TestFalse(TEXT("Merge open: a file changed on disk is still held"),
		Gate.ShouldNeverHoldSave(nullptr, FullPath, /*bInChangedOnDisk*/ true));

	Provider.FilesToResolve.Add(FullPath, FDiversionResolveInfo());
	TestTrue(TEXT("Merge open: a file the merge resolves is never held"),
		Gate.ShouldNeverHoldSave(nullptr, FullPath, /*bInChangedOnDisk*/ true));

	Provider.SetWorkspaceMergesList({});
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionSoftClashSurvivesModification,
	"Diversion.Tests.SaveDecision.SaveVetoSoftClashSurvivesModification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * A soft clash must reach the save gate for every save of the file, not just the first.
 *
 * IsAutoSoftLocked() carries a 2024 rule - true only while the file is NOT locally modified -
 * written for the old flow where soft locks rode the engine's checkout dialog: warn once,
 * then stop nagging and keep Revert available. Fed into the save gate, that rule made a
 * soft-clashed file route as "not locked" on every save after the first. The gate must ask
 * the raw clash condition instead; the once-only rule stays where it still belongs
 * (CanCheckIn / CanRevert).
 *
 * Observed through the gate's own routing (EvaluateFile) so the category is asserted directly.
 */
bool FSaveDecisionSoftClashSurvivesModification::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;

	WorkspaceInfo Ws;
	Ws.WorkspaceID = TEXT("test-workspace");
	Ws.RepoID = TEXT("test-repo");
	Ws.AccountID = TEXT("test-account");
	Ws.BranchID = TEXT("test-branch");
	Ws.CommitID = TEXT("test-commit");
	Ws.SetPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
	Provider.SetWorkspaceInfo(Ws);

	const FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("DiversionSaveVetoSoftClashTest.uasset"));

	UDiversionConfig* Config = GetMutableDefault<UDiversionConfig>();
	const bool bOriginalSoftLock = Config->bEnableSoftLock;
	Config->bEnableSoftLock = true;

	const TSharedRef<FDiversionState> State = Provider.GetStateInternal(FullPath);
	State->PotentialClashes.AddPotentialClash(
		EDiversionPotentialClashInfo(TEXT("other-commit"), TEXT("other-workspace"),
			TEXT("main"), TEXT("other@example.com"), TEXT("Other User"), /*Mtime*/ 0));

	DiversionUtils::EDiversionSaveCategory Category;
	FDiversionLockInfo LockInfo;
	FString Holder;
	const auto Evaluate = [&]()
	{
		return Provider.GetSaveDecisionGate().EvaluateFile(FullPath, /*bInInteractive*/ true, Category, LockInfo, Holder);
	};

	// Control: an unmodified soft-clashed file routes as SoftLocked
	State->WorkingCopyState = EWorkingCopyState::Unchanged;
	TestTrue(TEXT("Unmodified soft-clashed file: prompts"),
		Evaluate() == DiversionUtils::EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Unmodified soft-clashed file: routes as soft-clashed"),
		Category == DiversionUtils::EDiversionSaveCategory::SoftLocked);

	// The rule under test: once the file has been saved (Modified), the clash must still
	// reach the gate. Under the old once-only rule this routed as "not locked".
	State->WorkingCopyState = EWorkingCopyState::Modified;
	TestTrue(TEXT("Modified soft-clashed file: still prompts"),
		Evaluate() == DiversionUtils::EDiversionSaveVetoAction::Prompt);
	TestTrue(TEXT("Modified soft-clashed file: still routes as soft-clashed"),
		Category == DiversionUtils::EDiversionSaveCategory::SoftLocked);

	// And the once-only rule itself is untouched where it still belongs
	TestFalse(TEXT("IsAutoSoftLocked keeps its modified-file escape for CanCheckIn/CanRevert"),
		State->IsAutoSoftLocked());
	TestTrue(TEXT("The raw clash condition is what the gate now reads"),
		State->HasPotentialClashes());

	Config->bEnableSoftLock = bOriginalSoftLock;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSaveDecisionNewlyCreatedPackage,
	"Diversion.Tests.SaveDecision.SaveVetoNewlyCreatedPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/**
 * A package that has never been written to disk must never be held for a save decision.
 *
 * Holding a save means failing it and re-issuing it after the user answers. A brand-new map
 * cannot survive that: SaveWorld renames the package to its target name, and on a failed save
 * renames it BACK (FileHelpers.cpp), so the re-issued save can no longer resolve the package
 * and the level is silently lost - the editor reports success and writes nothing. QA hit this
 * on File > New Level > Save against 2.0.23 and 2.0.24.
 *
 * Uses the same veto-able control as the merge wiring test - a Skip already answered for this
 * save - so a regression shows up as the file being held rather than allowed.
 */
bool FSaveDecisionNewlyCreatedPackage::RunTest(const FString& Parameters)
{
	FDiversionProvider Provider;

	WorkspaceInfo Ws;
	Ws.WorkspaceID = TEXT("test-workspace");
	Ws.RepoID = TEXT("test-repo");
	Ws.AccountID = TEXT("test-account");
	Ws.BranchID = TEXT("test-branch");
	Ws.CommitID = TEXT("test-commit");
	Ws.SetPath(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
	Provider.SetWorkspaceInfo(Ws);

	const FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("DiversionSaveVetoNewPackageTest.uasset"));

	Provider.GetSaveDecisionGate().GestureAnswers.Add(FullPath, DiversionUtils::EDiversionSaveChoice::Skip);
	// Mark the file as already toasted about, so the refusal stays UI-free under NullRHI
	Provider.GetSaveDecisionGate().ToastedThisSave.Add(FullPath);

	// Control: with no package at all, the Skip holds the save - otherwise the assertion
	// below would pass for the wrong reason
	TestFalse(TEXT("No package: the Skip holds the save"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(nullptr, FullPath, nullptr));

	UPackage* ExistingPackage = CreatePackage(TEXT("/Game/DiversionSaveVetoNewPackageTest"));
	TestNotNull(TEXT("Test package was created"), ExistingPackage);
	ExistingPackage->ClearPackageFlags(PKG_NewlyCreated);
	TestFalse(TEXT("An already-saved package is still held"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(ExistingPackage, FullPath, nullptr));

	// The regression under test
	ExistingPackage->SetPackageFlags(PKG_NewlyCreated);
	TestTrue(TEXT("A newly created package is never held"),
		Provider.GetSaveDecisionGate().IsPackageOKToSave(ExistingPackage, FullPath, nullptr));

	ExistingPackage->ClearPackageFlags(PKG_NewlyCreated);
	return true;
}
