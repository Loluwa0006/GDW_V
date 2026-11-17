// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "CommonModule.h"
#include "DiversionAgentAddress.h"
#include "DiversionCommonUtils.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Logging/LogMacros.h"


FString FDiversionAgentAddress::GetAgentURL(const FString& HomeDir)
{
	constexpr int32 DefaultPort = 8797;
	const FString DefaultURL = FString::Printf(TEXT("http://localhost:%d"), DefaultPort);

	const FString ResolvedHomeDir = HomeDir.IsEmpty() ? DiversionUtils::GetUserHomeDirectory() : HomeDir;
	const FString FilePath = ResolvedHomeDir / TEXT(".diversion") / TEXT(".port");

	FString Port;

	if (FFileHelper::LoadFileToString(Port, *FilePath))
	{
		Port = Port.TrimStartAndEnd();

		if (Port.IsNumeric())
		{
			int32 PortNumber = FCString::Atoi(*Port);
			if (PortNumber > 0 && PortNumber <= 65535)
			{
				return FString::Printf(TEXT("http://localhost:%d"), PortNumber);
			}
			UE_LOG(LogDiversionCommon, Verbose, TEXT("Port number out of valid range in file: %s"), *FilePath);
		}
		else
		{
			UE_LOG(LogDiversionCommon, Verbose, TEXT("Invalid port number format in file: %s"), *FilePath);
		}
	}
	else
	{
		UE_LOG(LogDiversionCommon, Verbose, TEXT("Failed to read port from file: %s"), *FilePath);
	}

	UE_LOG(LogDiversionCommon, Verbose, TEXT("Using default port: %d"), DefaultPort);
	return DefaultURL;
}
