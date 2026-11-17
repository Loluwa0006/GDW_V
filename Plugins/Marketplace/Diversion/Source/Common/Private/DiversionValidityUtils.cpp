// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionValidityUtils.h"
#include "CommonModule.h"
#include "DiversionConfig.h"
#include "HAL/PlatformStackWalk.h"

namespace DiversionValidityUtils
{
	FString GetStackTrace()
	{
		try
		{
			// Define the maximum number of stack frames to capture
			const int32 MaxStackDepth = 100;
			// Array to hold the program counters
			uint64 StackTrace[MaxStackDepth];
			// Capture the stack trace
			int32 StackDepth = FPlatformStackWalk::CaptureStackBackTrace(StackTrace, MaxStackDepth);

			// String to hold the human-readable stack trace
			FString StackTraceString;
			for (int32 i = 0; i < StackDepth; ++i)
			{
				// Convert each program counter to a human-readable string
				ANSICHAR HumanReadableString[1024];
				FPlatformStackWalk::ProgramCounterToHumanReadableString(i, StackTrace[i], HumanReadableString, sizeof(HumanReadableString));
				// Remove unicode characters
				StackTraceString += FString(ANSI_TO_TCHAR(HumanReadableString)) + TEXT("\n");
			}
			return StackTraceString;
		}
		catch (...)
		{
			return "Exception during collecting stack trace";
		}
	}

	void PrintSupportNeededLogLine(const FString& InMessage, bool IsWarning)
	{
		// Not possible to pass verbosity as a parameter - using boolean to switch between warning and error
		const bool bShowSupportLinks = IsDiversionSupportLinksInLogsEnabled();
		const FString DocumentationStr = TEXT("Official documentation is available at: https://docs.diversion.dev/quickstart?utm_source=ue-plugin&utm_medium=plugin");
		const FString SupportStr = TEXT("For support please either open a ticket in the Diversion Desktop app (help icon) or join our Discord server: https://discord.com/invite/wSJgfsMwZr");

		if (IsWarning)
		{
			UE_LOG(LogDiversionCommon, Warning, TEXT("%s"), *InMessage);
			if (bShowSupportLinks)
			{
				UE_LOG(LogDiversionCommon, Warning, TEXT("%s"), *DocumentationStr);
				UE_LOG(LogDiversionCommon, Warning, TEXT("%s"), *SupportStr);
			}
		}
		else
		{
			UE_LOG(LogDiversionCommon, Error, TEXT("%s"), *InMessage);
			if (bShowSupportLinks)
			{
				UE_LOG(LogDiversionCommon, Error, TEXT("%s"), *DocumentationStr);
				UE_LOG(LogDiversionCommon, Error, TEXT("%s"), *SupportStr);
			}
		}
	}

	bool DiversionValidityCheck(
		bool InCondition,
		FString InErrorDescription,
		const FString& AccountId,
		bool InSilent,
		FSendErrorCallback SendErrorCallback
	)
	{
		if (InCondition)
		{
			// Condition is met, no need to do anything
			return true;
		}

		// Always log the error unless silent
		if (!InSilent)
		{
			PrintSupportNeededLogLine(InErrorDescription, false);
		}

		// Try to send error to backend if callback is provided and AccountId is valid
		if (SendErrorCallback && !AccountId.IsEmpty() && AccountId != "N/a")
		{
			// Collect stack trace
			FString StackTrace = GetStackTrace();
			const FString ErrorDescription = "Assertion failed, Error: " + InErrorDescription;
			SendErrorCallback(AccountId, ErrorDescription, StackTrace);
		}
		else if (!AccountId.IsEmpty() && AccountId != "N/a" && !SendErrorCallback)
		{
			UE_LOG(LogDiversionCommon, Warning, TEXT("DiversionValidityCheck: No error sending callback provided, error will not be sent to backend"));
		}

		return false;
	}
}
