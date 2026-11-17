// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Containers/UnrealString.h"
#include "HAL/CriticalSection.h"

struct FScriptContainerElement;

class FDiversionSettings
{
public:
	/** Get the Diversion Binary Path */
	const FString GetBinaryPath() const;

	/** Load settings from ini file */
	void LoadSettings();

	/** Save settings to ini file */
	void SaveSettings() const;

private:
	/** A critical section for settings access */
	mutable FCriticalSection CriticalSection;

	/** Diversion binary path */
	FString BinaryPath;
};
