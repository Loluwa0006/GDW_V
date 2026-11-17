// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#include <atomic>

// Gets Core API access tokens from the Diversion agent, which refreshes expired tokens and
// saves them
class FCredentialsManager
{

public:
	// Returns an empty string when the agent can't provide a valid token
	FString GetUserAccessToken(const FString& InUserId);

private:
	enum class ETokenProblem : uint8
	{
		None,
		// Logged only: the agent is down, or answered in an unexpected way
		AgentError,
		LoginRequired,
		NoCredentials,
		ExpiredToken,
	};

	struct FTokenFailure
	{
		ETokenProblem Problem = ETokenProblem::AgentError;
		FString LogMessage;
	};

	struct FAccountState
	{
		// The problem the user was last told about, until a token arrives
		ETokenProblem ReportedProblem = ETokenProblem::None;
		// Failures of requests started before this one succeeded are stale
		uint64 LastSuccessfulRequestId = 0;
	};

	// InAccount is empty for the agent's default account
	bool RequestTokenFromAgent(const FString& InAccount, FString& OutAccessToken, FTokenFailure& OutFailure) const;

	void OnTokenReceived(const FString& InAccount, uint64 InRequestId);

	// Logs and shows a problem once per account, until a token arrives for that account
	void ReportFailure(const FString& InAccount, uint64 InRequestId, const FTokenFailure& InFailure);

	// Protects AccountStates. Not held during the agent request.
	FCriticalSection CredentialsLock;

	TMap<FString, FAccountState> AccountStates;

	std::atomic<uint64> NextRequestId{ 1 };
};
