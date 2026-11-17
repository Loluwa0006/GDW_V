// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DiversionValidityUtils
{
	/**
	 * Get a human-readable stack trace
	 * @return Stack trace as a string
	 */
	COMMON_API FString GetStackTrace();

	/**
	 * Print a support needed log line with documentation and support information
	 * @param InMessage The message to log
	 * @param IsWarning If true, log as warning; otherwise log as error
	 */
	COMMON_API void PrintSupportNeededLogLine(const FString& InMessage, bool IsWarning);

	/**
	 * Callback function type for sending errors to backend
	 * @param AccountID The account ID
	 * @param ErrorMessage The error message
	 * @param StackTrace The stack trace
	 * @return true if error was sent successfully
	 */
	typedef TFunction<bool(const FString& /* AccountID */, const FString& /* ErrorMessage */, const FString& /* StackTrace */)> FSendErrorCallback;

	/**
	 * Check a condition and report error if it fails
	 * @param InCondition The condition to check
	 * @param InErrorDescription Description of the error
	 * @param AccountId Account ID for error reporting (optional, can be empty)
	 * @param InSilent If true, suppress log output
	 * @param SendErrorCallback Optional callback for sending error to backend
	 * @return true if condition is met, false otherwise
	 */
	COMMON_API bool DiversionValidityCheck(
		bool InCondition,
		FString InErrorDescription,
		const FString& AccountId = TEXT(""),
		bool InSilent = false,
		FSendErrorCallback SendErrorCallback = nullptr
	);
}
