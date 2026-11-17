// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Client-side mirror of the backend's file-pattern matching (core: file_pattern_attributes.py,
 * file_matches_any) - the rule that decides which files the server auto-locks on sync.
 *
 * Only the extension form is mirrored: a pattern matching _EXTENSION_RE (^\.\w+$) matches a
 * file whose posixpath.splitext extension equals it, case-sensitively as on the Linux server.
 * That is every pattern the product can produce today - the desktop app's Hard Locks settings
 * accept nothing else (HardLocksTab.tsx, isValidExtension) and the CLI has no such command.
 *
 * The server also accepts fnmatch globs through the raw API. Those are deliberately NOT
 * evaluated here: a glob never matches, so a file it covers keeps today's save prompt rather
 * than being labelled "locks on sync" by an approximation that could be wrong. Under-matching
 * costs one redundant prompt; over-matching would misreport what the server will do.
 */
namespace DiversionFilePattern
{
	/** Whether the pattern is of the extension form the plugin evaluates (backend _EXTENSION_RE) */
	COMMON_API bool IsExtensionPattern(const FString& InPattern);

	/** Whether the repo-relative path's extension matches any extension-form pattern in the list.
	 * Non-extension (glob) patterns in the list are skipped. */
	COMMON_API bool MatchesExtension(const FString& InRelativePath, const TArray<FString>& InPatterns);
}
