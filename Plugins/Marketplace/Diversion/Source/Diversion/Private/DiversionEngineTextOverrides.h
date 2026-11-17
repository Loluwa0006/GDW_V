// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Rewords engine revision-control texts for Diversion ("Revert" -> "Reset", "Submit" -> "Commit", ...).
 * The replacements are localized, and are re-applied whenever the text localization refreshes
 * (e.g. on a culture change), so they follow the editor language.
 */
namespace DiversionEngineTextOverrides
{
	/** Applies the overrides and keeps re-applying them after every localization refresh */
	void Enable();

	/** Stops re-applying the overrides and restores the engine's own texts */
	void Disable();

	bool IsEnabled();
}
