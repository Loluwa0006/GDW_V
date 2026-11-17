// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionVersion.h"

#include "ISourceControlModule.h"

FDiversionVersion::FDiversionVersion(const FString& VersionStr)
{
	ParseVersion(VersionStr);
}

bool FDiversionVersion::IsGreaterOrEqualThan(int InMajor, int InMinor, int InBuild) const
{
	return (Major > InMajor) || (Major == InMajor && Minor > InMinor) || (Major == InMajor && Minor == InMinor && Build >= InBuild);
}

bool FDiversionVersion::SetVersion(const FString& InVersionString)
{
	if (!ParseVersion(InVersionString))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Failed to parse Diversion version string: %s"), *InVersionString);
		return false;
	}
	return true;
}

bool FDiversionVersion::ParseVersion(const FString& InVersionString)
{
	bool bResult = false;

	if (InVersionString.IsEmpty() || InVersionString[0] != TEXT('v'))
	{
		return false;
	}

	String = InVersionString.RightChop(1);

	TArray<FString> ParsedVersionString;
	String.ParseIntoArray(ParsedVersionString, TEXT("."));
	if (ParsedVersionString.Num() == 3)
	{
		const FString& StrMajor = ParsedVersionString[0];
		const FString& StrMinor = ParsedVersionString[1];
		const FString& StrBuild = ParsedVersionString[2];
		if (StrMajor.IsNumeric() && StrMinor.IsNumeric() && StrBuild.IsNumeric())
		{
			Major = FCString::Atoi(*StrMajor);
			Minor = FCString::Atoi(*StrMinor);
			Build = FCString::Atoi(*StrBuild);
			bResult = true;
		}
	}

	if(!bResult)
	{
		String = "";
	}
	return bResult;
}
