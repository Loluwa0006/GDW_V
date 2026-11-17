// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "ISceneOutlinerColumn.h"
#include "SceneOutlinerFwd.h"

/**
 * A Scene Outliner column that displays potential clash indicators for actors.
 * Shows a warning icon with tooltip when an actor's asset has potential conflicts.
 */
class FDiversionPotentialClashColumn : public ISceneOutlinerColumn
{
public:
	FDiversionPotentialClashColumn(ISceneOutliner& SceneOutliner);
	virtual ~FDiversionPotentialClashColumn() {}

	static FName GetID();
	static FText GetDisplayName();

	//~ Begin ISceneOutlinerColumn Interface
	virtual FName GetColumnID() override;
	virtual SHeaderRow::FColumn::FArguments ConstructHeaderRowColumn() override;
	virtual const TSharedRef<SWidget> ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row) override;
	virtual bool SupportsSorting() const override { return false; }
	//~ End ISceneOutlinerColumn Interface

	/** Registers this column type with the Scene Outliner module */
	static void RegisterColumnType();

	/** Unregisters this column type from the Scene Outliner module */
	static void UnregisterColumnType();
};
