// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UToolMenu;

/**
 * Puts a persistent "locked by {holder}" banner on the toolbar of every open asset editor
 * window whose asset another user hard-locks, for as long as the lock holds.
 *
 * The banner exists because a lock taken while a window is open used to surface only at the
 * next save, by which point the user's work may already be unsalvageable. It reads nothing
 * but the state cache the potential-clashes poll already fills, so an editor window costs no
 * extra backend request no matter how many are open.
 */
class FDiversionAssetEditorLockIndicator
{
public:
	/** Extend the shared asset editor toolbar; safe to call repeatedly (Init can rerun without Close) */
	static void Register();
	static void Unregister();

private:
	static void PopulateSection(UToolMenu* InMenu);
};
