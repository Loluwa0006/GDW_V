// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISourceControlChangelistState.h"
#include "Misc/EngineVersionComparison.h"


class FDiversionChangelistState final : public ISourceControlChangelistState
{
public:
	FDiversionChangelistState() = default;

	/**
	 * Get the name of the icon graphic we should use to display the state in a UI.
	 * @returns the name of the icon to display
	 */
	virtual FName GetIconName() const override
	{
		return FName("SourceControl.Changelist");
	}

	/**
	 * Get the name of the small icon graphic we should use to display the state in a UI.
	 * @returns the name of the icon to display
	 */
	virtual FName GetSmallIconName() const override
	{
		return GetIconName();
	}

	/**
	 * Get a text representation of the state
	 * @returns	the text to display for this state
	 */
	virtual FText GetDisplayText() const override
	{
		return NSLOCTEXT("DiversionChangelist", "WorkspaceChanges", "Workspace Changes");
	}

	/**
	 * Get a text representation of the state
	 * @returns	the text to display for this state
	 */
	virtual FText GetDescriptionText() const override
	{
		return NSLOCTEXT("DiversionChangelist", "WorkspaceChanges", "Workspace Changes");
	}

	/**
	 * Irrelevant for Diversion
	 */
	virtual bool SupportsPersistentDescription() const override { return false; }

	/**
	 * Get a tooltip to describe this state
	 * @returns	the text to display for this states tooltip
	 */
	virtual FText GetDisplayTooltip() const override
	{
		return NSLOCTEXT("DiversionChangelist", "WorkspaceChanges_Tooltip", "Diversion workspace changes");
	}

	/**
	 * Get the timestamp of the last update that was made to this state.
	 * @returns	the timestamp of the last update
	 */
	virtual const FDateTime& GetTimeStamp() const override
	{
		return TimeStamp;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	virtual const TArray<FSourceControlStateRef> GetFilesStates() const override
	{
		return Files;
	}
	virtual int32 GetFilesStatesNum() const override
	{
		return Files.Num();
	}
#else
	virtual const TArray<FSourceControlStateRef>& GetFilesStates() const override
	{
		return Files;
	}
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 4
	virtual const TArray<FSourceControlStateRef> GetShelvedFilesStates() const override
	{
		return TArray<FSourceControlStateRef>();
	}

	virtual int32 GetShelvedFilesStatesNum() const override
	{
		return 0;
	}
#else
private:
	TArray<FSourceControlStateRef> ShelvedFiles;
public:
	virtual const TArray<FSourceControlStateRef>& GetShelvedFilesStates() const override
	{
		return ShelvedFiles;
	}
#endif

	virtual FSourceControlChangelistRef GetChangelist() const override
	{
		return MakeShareable( new ISourceControlChangelist());
	}

public:
	TArray<FSourceControlStateRef> Files;

	/** The timestamp of the last update */
	FDateTime TimeStamp;
};
