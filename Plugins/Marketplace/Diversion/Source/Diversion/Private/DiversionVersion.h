// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

struct FDiversionVersion
{
	FDiversionVersion(const FString& VersionStr);

	bool IsGreaterOrEqualThan(int InMajor, int InMinor, int InBuild) const;

	int GetMajor() const { return Major; }
	int GetMinor() const { return Minor; }
	int GetBuild() const { return Build; }

	FString ToString() const { return String; }

	bool IsValid() const { return (String != "") && IsAgentAlive; }

	bool SetVersion(const FString& InVersionString);
	void SetVersion(const int InMajor, const int InMinor, const int InBuild) { Major = InMajor; Minor = InMinor; Build = InBuild; }

	void SetAgentAlive(bool InIsAgentAlive) { IsAgentAlive = InIsAgentAlive; }
private:
	bool ParseVersion(const FString& InVersionString);
	int Major = 0;
	int Minor = 0;
	int Build = 0;
	FString String;

	// Since we get version from the health check API, we store the health check status here as well
	bool IsAgentAlive = false;
};
