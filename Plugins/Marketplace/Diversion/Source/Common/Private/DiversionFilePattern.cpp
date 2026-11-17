// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionFilePattern.h"

#include "Misc/Paths.h"

namespace
{
	/**
	 * posixpath.splitext()[1]: the last '.' of the final path component, unless every character
	 * before it in that component is also a '.' - so ".gitignore" and "..foo" have no extension,
	 * while "a.b.c" has ".c" and ".a.b" has ".b". FPaths::GetExtension differs on dotfiles.
	 */
	FString PosixExtension(const FString& Path)
	{
		const FString Filename = FPaths::GetCleanFilename(Path);
		int32 DotIdx = INDEX_NONE;
		if (!Filename.FindLastChar(TEXT('.'), DotIdx))
		{
			return FString();
		}
		int32 FirstNonDot = 0;
		while (FirstNonDot < Filename.Len() && Filename[FirstNonDot] == TEXT('.'))
		{
			++FirstNonDot;
		}
		return DotIdx > FirstNonDot ? Filename.Mid(DotIdx) : FString();
	}
}

bool DiversionFilePattern::IsExtensionPattern(const FString& InPattern)
{
	// The backend's _EXTENSION_RE: ^\.\w+$
	if (InPattern.Len() < 2 || InPattern[0] != TEXT('.'))
	{
		return false;
	}
	for (int32 Idx = 1; Idx < InPattern.Len(); ++Idx)
	{
		if (!FChar::IsAlnum(InPattern[Idx]) && InPattern[Idx] != TEXT('_'))
		{
			return false;
		}
	}
	return true;
}

bool DiversionFilePattern::MatchesExtension(const FString& InRelativePath, const TArray<FString>& InPatterns)
{
	const FString Extension = PosixExtension(InRelativePath);
	if (Extension.IsEmpty())
	{
		return false;
	}
	for (const FString& Pattern : InPatterns)
	{
		// Glob patterns are deliberately not evaluated - see the header
		if (IsExtensionPattern(Pattern) && Extension.Equals(Pattern, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}
	return false;
}
