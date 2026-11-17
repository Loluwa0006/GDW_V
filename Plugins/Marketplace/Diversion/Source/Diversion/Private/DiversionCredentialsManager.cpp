// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionCredentialsManager.h"
#include "Async/Async.h"
#include "ISourceControlModule.h"
#include "Misc/DateTime.h"

#include "DefaultApi.h"
#include "DiversionAPIAccess.h"
#include "DiversionModule.h"
#include "DiversionNotificationManager.h"

#define LOCTEXT_NAMESPACE "Diversion"

namespace
{
	using FCoreAccessTokenResponse = Diversion::AgentAPI::Model::CoreAccessToken_200_response;
	using FCoreAccessTokenResult = THTTPResult<TVariant<void*, TSharedPtr<FCoreAccessTokenResponse>>>;

	// The agent answers from memory, so the request timeout only matters while the agent refreshes
	// the token with the auth server. The agent finishes and saves that refresh even after this
	// request gives up, and the next request gets the new token.
	const int32 TokenConnectionTimeoutSeconds = 5;
	const int32 TokenRequestTimeoutSeconds = 30;

	// The account ID the plugin uses when the workspace has none: the agent's default account
	bool IsDefaultAccount(const FString& InUserId)
	{
		return InUserId.IsEmpty() || InUserId == TEXT("N/a");
	}

	FString AccountForLog(const FString& InAccount)
	{
		return InAccount.IsEmpty() ? TEXT("(default)") : InAccount;
	}
}

FString FCredentialsManager::GetUserAccessToken(const FString& InUserId)
{
	// No caching: the agent keeps the token in memory, so a localhost request per API call is cheap.
	// Concurrent requests for one account are fine: the agent refreshes each account's token once.
	const FString Account = IsDefaultAccount(InUserId) ? FString() : InUserId;
	const uint64 RequestId = NextRequestId++;

	FString AccessToken;
	FTokenFailure Failure;
	if (!RequestTokenFromAgent(Account, AccessToken, Failure))
	{
		ReportFailure(Account, RequestId, Failure);
		return "";
	}

	OnTokenReceived(Account, RequestId);
	return AccessToken;
}

bool FCredentialsManager::RequestTokenFromAgent(const FString& InAccount, FString& OutAccessToken,
	FTokenFailure& OutFailure) const
{
	if (!FDiversionAPIAccess::AgentAPI)
	{
		OutFailure.LogMessage = TEXT("API not initialized");
		return false;
	}

	const TOptional<FString> AccountID = InAccount.IsEmpty() ? TOptional<FString>() : TOptional<FString>(InAccount);
	const FCoreAccessTokenResult Result = FDiversionAPIAccess::AgentAPI->CoreAccessToken(AccountID, FString(), {},
		TokenConnectionTimeoutSeconds, TokenRequestTimeoutSeconds);
	if (!Result.IsSuccess())
	{
		OutFailure.LogMessage = FString::Printf(TEXT("%d %s"), Result.StatusCode, *Result.Error);
		if (Result.StatusCode == 401)
		{
			OutFailure.Problem = ETokenProblem::LoginRequired;
		}
		else if (Result.StatusCode == 400 || Result.StatusCode == 404)
		{
			OutFailure.Problem = ETokenProblem::NoCredentials;
		}
		return false;
	}

	if (!Result.Value.IsSet() || !Result.Value->IsType<TSharedPtr<FCoreAccessTokenResponse>>())
	{
		OutFailure.LogMessage = TEXT("unexpected response");
		return false;
	}

	const TSharedPtr<FCoreAccessTokenResponse> Response = Result.Value->Get<TSharedPtr<FCoreAccessTokenResponse>>();
	if (!Response.IsValid() || Response->mAccessToken.IsEmpty())
	{
		OutFailure.LogMessage = TEXT("empty access token");
		return false;
	}

	// An agent that refreshes tokens never returns an expired one; older agents do
	if (Response->mExpiresAt <= FDateTime::UtcNow().ToUnixTimestamp())
	{
		OutFailure.Problem = ETokenProblem::ExpiredToken;
		OutFailure.LogMessage = TEXT("the token has expired");
		return false;
	}

	OutAccessToken = Response->mAccessToken;
	return true;
}

void FCredentialsManager::OnTokenReceived(const FString& InAccount, uint64 InRequestId)
{
	bool bRecovered = false;
	{
		FScopeLock Lock(&CredentialsLock);
		FAccountState& State = AccountStates.FindOrAdd(InAccount);
		State.LastSuccessfulRequestId = FMath::Max(State.LastSuccessfulRequestId, InRequestId);
		bRecovered = State.ReportedProblem != ETokenProblem::None;
		State.ReportedProblem = ETokenProblem::None;
	}

	if (bRecovered)
	{
		UE_LOG(LogSourceControl, Display, TEXT("Got an access token for account %s from the agent again"), *AccountForLog(InAccount));
	}
}

void FCredentialsManager::ReportFailure(const FString& InAccount, uint64 InRequestId, const FTokenFailure& InFailure)
{
	{
		FScopeLock Lock(&CredentialsLock);
		FAccountState& State = AccountStates.FindOrAdd(InAccount);
		if (InRequestId < State.LastSuccessfulRequestId || State.ReportedProblem == InFailure.Problem)
		{
			UE_LOG(LogSourceControl, Verbose, TEXT("Failed to get an access token for account %s from the agent: %s"),
				*AccountForLog(InAccount), *InFailure.LogMessage);
			return;
		}
		State.ReportedProblem = InFailure.Problem;
	}

	UE_LOG(LogSourceControl, Error, TEXT("Failed to get an access token for account %s from the agent: %s"),
		*AccountForLog(InAccount), *InFailure.LogMessage);

	const FText Account = InAccount.IsEmpty() ? LOCTEXT("DefaultAccount", "your account") : FText::FromString(InAccount);
	FText Message;
	switch (InFailure.Problem)
	{
	case ETokenProblem::LoginRequired:
		Message = FText::Format(LOCTEXT("TokenLoginRequired",
			"Diversion: the session for {0} has expired. Log in to Diversion again."), Account);
		break;
	case ETokenProblem::NoCredentials:
		Message = FText::Format(LOCTEXT("TokenNoCredentials",
			"Diversion: no login found for {0}. Log in to Diversion with this account, or update Diversion to the latest version."), Account);
		break;
	case ETokenProblem::ExpiredToken:
		Message = FText::Format(LOCTEXT("TokenExpired",
			"Diversion: could not get a valid login for {0}. Update Diversion to the latest version, and log in again if this continues."), Account);
		break;
	default:
		return;
	}

	// Sticky: until the user logs in or updates, every Core API call fails
	AsyncTask(ENamedThreads::GameThread, [Message]()
	{
		if (FDiversionModule::IsLoaded())
		{
			FDiversionModule::Get().GetProvider().ShowNotification(
				FDiversionNotification(Message, TArray<FNotificationButtonInfo>(), SNotificationItem::CS_Fail, /*bSticky*/ true));
		}
	});
}

#undef LOCTEXT_NAMESPACE
