// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "DiversionFilePattern.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFilePatternMatchesServer,
	"Diversion.Tests.FilePattern.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

/** Every verdict here was checked against Python's posixpath.splitext and the backend's
 * _EXTENSION_RE before being written. Extend the same way. */
bool FFilePatternMatchesServer::RunTest(const FString& Parameters)
{
	using namespace DiversionFilePattern;

	// --- Which patterns the plugin evaluates (_EXTENSION_RE: ^\.\w+$)
	TestTrue(TEXT(".uasset is an extension pattern"), IsExtensionPattern(TEXT(".uasset")));
	TestTrue(TEXT("Digits and underscores are word characters"), IsExtensionPattern(TEXT(".u_asset2")));
	TestFalse(TEXT("A bare dot is not"), IsExtensionPattern(TEXT(".")));
	TestFalse(TEXT("No leading dot is not"), IsExtensionPattern(TEXT("uasset")));
	TestFalse(TEXT("A glob is not"), IsExtensionPattern(TEXT("*.uasset")));
	TestFalse(TEXT("A dotted glob is not"), IsExtensionPattern(TEXT(".git*")));
	TestFalse(TEXT("A path is not"), IsExtensionPattern(TEXT("Content/Maps*")));
	TestFalse(TEXT("A double extension is not"), IsExtensionPattern(TEXT(".tar.gz")));

	const TArray<FString> Patterns = { TEXT(".uasset"), TEXT(".umap"), TEXT("Content/Maps*") };

	// --- Extension matching, posixpath.splitext semantics
	TestTrue(TEXT("Matches by extension"),
		MatchesExtension(TEXT("Content/Props/SM_Rock.uasset"), Patterns));
	TestTrue(TEXT("Matches anywhere in the tree"),
		MatchesExtension(TEXT("Plugins/Foo/Content/Levels/L1.umap"), Patterns));
	TestFalse(TEXT("Case-sensitive, as on the server"),
		MatchesExtension(TEXT("Content/Props/SM_Rock.UASSET"), Patterns));
	TestFalse(TEXT("Only the last extension counts"),
		MatchesExtension(TEXT("Content/Props/SM_Rock.uasset.bak"), Patterns));
	TestTrue(TEXT("Dotted filename keeps its last extension"),
		MatchesExtension(TEXT("Content/Props/SM.Rock.uasset"), Patterns));
	TestFalse(TEXT("No extension matches nothing"),
		MatchesExtension(TEXT("Content/Props/README"), Patterns));
	TestFalse(TEXT("Unrelated extension does not match"),
		MatchesExtension(TEXT("Config/DefaultEngine.ini"), Patterns));
	TestFalse(TEXT("Empty pattern list matches nothing"),
		MatchesExtension(TEXT("Content/Props/SM_Rock.uasset"), TArray<FString>()));

	// --- Dotfiles: splitext gives them no extension
	TestFalse(TEXT("Dotfile has no extension on the server"),
		MatchesExtension(TEXT(".gitignore"), { TEXT(".gitignore") }));
	TestFalse(TEXT("Leading dots never start an extension"),
		MatchesExtension(TEXT("Content/..uasset"), Patterns));
	TestTrue(TEXT("Dotted directory does not matter"),
		MatchesExtension(TEXT(".hidden/SM_Rock.uasset"), Patterns));
	TestTrue(TEXT("Extension after a leading dot still counts"),
		MatchesExtension(TEXT("Content/.SM_Rock.uasset"), Patterns));

	// --- Globs are not evaluated: never a match, never an approximation
	TestFalse(TEXT("A glob the server would match is not evaluated"),
		MatchesExtension(TEXT("Content/Maps/Main.txt"), Patterns));
	TestFalse(TEXT("A glob-only list matches nothing"),
		MatchesExtension(TEXT("Content/Props/SM_Rock.uasset"), { TEXT("*.uasset") }));

	return true;
}
