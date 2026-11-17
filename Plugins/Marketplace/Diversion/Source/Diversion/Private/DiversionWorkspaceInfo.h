// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"

struct WorkspaceInfo {
	WorkspaceInfo()
	{
		WorkspaceID = "N/a";
		WorkspaceName = "N/a";
		RepoID = "N/a";
		RepoName = "N/a";
		NormalizedPath = "N/a";
		AccountID = "N/a";
		BranchID = "N/a";
		BranchName = "N/a";
		CommitID = "N/a";
	}
	FString WorkspaceID;
	FString WorkspaceName;
	FString RepoID;
	FString RepoName;
	FString AccountID;
	FString BranchID;
	FString BranchName;
	FString CommitID;

	void SetPath(const FString& In)
	{
		NormalizedPath = In;
		FPaths::NormalizeDirectoryName(NormalizedPath);
	}

	const FString& GetPath() const { return NormalizedPath;}

	bool IsValid() const
	{
		return WorkspaceID != "N/a" && RepoID != "N/a" && AccountID != "N/a" && BranchID != "N/a" && CommitID != "N/a";
	}

private:
	FString NormalizedPath;
};
