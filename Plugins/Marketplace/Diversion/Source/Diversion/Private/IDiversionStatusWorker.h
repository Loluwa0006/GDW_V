// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "IDiversionWorker.h"
#include "DiversionState.h"


/**
 * Interface for a worker that updates the status of the workspace
 * Note! In order for this to be valid this assumes a call to RunUpdateStatus will be made
 */
class IDiversionStatusWorker : public IDiversionWorker
{
public:
	/** flag to indicate if the workspace contains conflicts and needs to be manually updated */
	bool WorkspaceUpdateRequired;

	/** Temporary states for results */
	TMap<FString, FDiversionState> States;

	/** Map of filenames to history */
	TMap<FString, TDiversionHistory> Histories;

	/** Store the number of items fetched for offset tracking - pagination system*/
	int ItemsFetchedNum = 0;

	/** Store if the returned result is complete or not*/
	bool bIncompleteResult;

	/** Indicates that the request we sent to the backend is recursive */
	bool bRecursiveRequest = true;
};
