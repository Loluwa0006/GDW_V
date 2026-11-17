// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolMenus.h"

/**
 * Shared plumbing for the plugin's dynamic engine-menu extensions.
 *
 * Every Diversion surface that hangs off an engine menu needs the same three things: the
 * headless-mode guard, an owner scope so teardown can remove the whole extension by name,
 * and idempotency - the provider's Init can rerun without a matching Close, so registering
 * twice must not leave two sections behind.
 */
namespace DiversionToolMenus
{
	/**
	 * Attach a dynamic section named InSectionName to the engine menu InMenuName, owned by
	 * InOwnerName. The populate delegate runs on every generation of that menu, so it may
	 * gate on live state. Safe to call repeatedly.
	 */
	void RegisterDynamicSection(const FName& InMenuName, const FName& InOwnerName,
		const FName& InSectionName, FNewToolMenuDelegate InPopulate);

	/** Remove every menu extension registered under InOwnerName. */
	void UnregisterOwner(const FName& InOwnerName);
}
