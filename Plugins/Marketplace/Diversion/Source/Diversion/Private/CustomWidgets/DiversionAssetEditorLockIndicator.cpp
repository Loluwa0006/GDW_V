// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionAssetEditorLockIndicator.h"

#include "DiversionHardLockUI.h"
#include "DiversionToolMenuExtension.h"
#include "DiversionUtils.h"

#include "AssetRegistry/AssetData.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

namespace
{
	/** The engine's common-actions asset editor toolbar (Save / Browse). FAssetEditorToolkit
	 *  registers it once and every asset editor window generates the same menu through
	 *  GenerateCommonActionsToolbar, so one extension reaches them all - no per-window
	 *  bookkeeping and no per-asset-editor registration. */
	const FName AssetEditorCommonActionsToolbarName("AssetEditorToolbar.CommonActions");
	const FName DiversionLockIndicatorOwnerName("DiversionAssetEditorLockIndicator");
	/** Key for our dynamic section - the extension slot we add, not where the pill lands */
	const FName DiversionLockIndicatorSectionName("DiversionHardLock");
	/** The engine's own section in that toolbar, holding Save and Browse. The pill joins it
	 *  rather than opening a section of its own: a new toolbar section is drawn with a leading
	 *  separator (UToolMenu::bSeparateSections, FToolBarBuilder::ApplySectionBeginning), and
	 *  that separator is a block in its own right - it would show in every asset editor
	 *  whether or not anything is locked. Same-named sections merge, so this appends.
	 *
	 *  Owned by FAssetEditorToolkit::GenerateCommonActionsToolbar - re-check this name there on
	 *  an engine upgrade. Nothing fails loudly if it moves: FindOrAddSection would just create a
	 *  section of our own again, bringing that leading separator back. */
	const FName AssetEditorCommonActionsSectionName("CommonActions");
}

void FDiversionAssetEditorLockIndicator::Register()
{
	DiversionToolMenus::RegisterDynamicSection(AssetEditorCommonActionsToolbarName,
		DiversionLockIndicatorOwnerName, DiversionLockIndicatorSectionName,
		FNewToolMenuDelegate::CreateStatic(&FDiversionAssetEditorLockIndicator::PopulateSection));
}

void FDiversionAssetEditorLockIndicator::Unregister()
{
	DiversionToolMenus::UnregisterOwner(DiversionLockIndicatorOwnerName);
}

void FDiversionAssetEditorLockIndicator::PopulateSection(UToolMenu* InMenu)
{
	// No provider or lock-eligibility gate here, unlike the Content Browser lock entries: this
	// section is built once when the window opens, so a gate evaluated now would stick for the
	// window's whole life. The banner self-gates instead - it stays collapsed unless the cache
	// says the asset is hard-locked by someone else under the Diversion provider, which is
	// never true on a non-Diversion provider or a repo whose tier has no locks.
	const UAssetEditorToolkitMenuContext* Context = InMenu->FindContext<UAssetEditorToolkitMenuContext>();
	if (Context == nullptr)
	{
		return;
	}

	// Multi-object editors are rare and have no single subject to name; the primary asset is
	// the one whose lock would block this window's save
	const TArray<UObject*> EditedObjects = Context->GetEditingObjects();
	if (EditedObjects.Num() == 0)
	{
		return;
	}

	// Empty for anything with no package on disk (transient or in-memory subjects). Note the
	// path is resolved once here and baked into the widget: a toolkit does not regenerate its
	// toolbar when its asset is renamed or first saved, so a window opened on an unsaved asset
	// keeps no banner, and one whose asset is renamed keeps watching the old path. Both are
	// rare next to the cost of re-resolving the path on every paint.
	const FString FilePath = DiversionUtils::GetFilePathFromAssetData(FAssetData(EditedObjects[0]));
	if (FilePath.IsEmpty())
	{
		return;
	}

	FToolMenuSection& Section = InMenu->FindOrAddSection(AssetEditorCommonActionsSectionName);
	// bNoIndent so the pill sits flush instead of being inset like a labelled control, and
	// not searchable - it is a status readout, not something the user can invoke
	Section.AddEntry(FToolMenuEntry::InitWidget("DiversionHardLockBanner",
		SNew(SHardLockBanner).AssetPath(FilePath), FText::GetEmpty(),
		/*bNoIndent*/ true, /*bSearchable*/ false));
}
