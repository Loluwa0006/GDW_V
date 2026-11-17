// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class COMMON_API FDiversionAgentAddress
{
public:
	static FString GetAgentURL(const FString& HomeDir = TEXT(""));
};
