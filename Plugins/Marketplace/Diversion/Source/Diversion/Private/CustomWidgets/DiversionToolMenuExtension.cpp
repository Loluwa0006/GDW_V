// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionToolMenuExtension.h"

namespace DiversionToolMenus
{
	void RegisterDynamicSection(const FName& InMenuName, const FName& InOwnerName,
		const FName& InSectionName, FNewToolMenuDelegate InPopulate)
	{
		// Commandlet / -nullrhi runs have no menu system to extend
		if (!UToolMenus::IsToolMenuUIEnabled())
		{
			return;
		}
		// Scopes everything added below to InOwnerName, so UnregisterOwner can drop it wholesale
		const FToolMenuOwnerScoped OwnerScoped(InOwnerName);
		// ExtendMenu, not FindMenu: engine menus are frequently registered lazily on first use,
		// and an extension recorded beforehand is merged in when that finally happens
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(InMenuName);
		if (Menu == nullptr || Menu->FindSection(InSectionName) != nullptr)
		{
			return;
		}
		Menu->AddDynamicSection(InSectionName, MoveTemp(InPopulate));
	}

	void UnregisterOwner(const FName& InOwnerName)
	{
		if (UToolMenus::IsToolMenuUIEnabled())
		{
			UToolMenus::Get()->UnregisterOwnerByName(InOwnerName);
		}
	}
}
