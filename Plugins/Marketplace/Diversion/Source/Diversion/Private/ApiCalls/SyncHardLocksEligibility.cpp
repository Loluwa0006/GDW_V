// Copyright 2024 Diversion Company, Inc. All Rights Reserved.


#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "RepositoryManipulationApi.h"
#include "OrganizationManagementApi.h"


using namespace Diversion::CoreAPI;

bool DiversionUtils::IsHardLocksEligibleTier(const FString& InTier)
{
	return InTier.Equals(TEXT("STUDIO"), ESearchCase::IgnoreCase) ||
		InTier.Equals(TEXT("ENTERPRISE"), ESearchCase::IgnoreCase);
}

bool DiversionUtils::GetHardLocksEligibility(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages,
	TArray<FString>& OutErrorMessages, TOptional<bool>& bOutEligible, TOptional<TArray<FString>>& OutLockablePatterns)
{
	bOutEligible.Reset();
	OutLockablePatterns.Reset();

	// Step 1: resolve the repo's organization
	TOptional<FString> OrganizationId;
	FString RepoOwnerUserId;

	auto RepoErrorResponse = RepositoryManipulationApi::Fsrc_handlersv2_repo_getDelegate::Bind(
		[&](const TVariant<TSharedPtr<Repo>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				if (Value.IsValid())
				{
					OutErrorMessages.Add(FString::Printf(TEXT("Received error for get repo call: %s"), *Value->mDetail));
				}
			}
			return false;
		}
	);
	auto RepoVariantResponse = RepositoryManipulationApi::Fsrc_handlersv2_repo_getDelegate::Bind(
		[&](const TVariant<TSharedPtr<Repo>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
			if (!Variant.IsType<TSharedPtr<Repo>>())
			{
				OutErrorMessages.Add("Unexpected response type for get repo call");
				return false;
			}
			auto Value = Variant.Get<TSharedPtr<Repo>>();
			if (!Value.IsValid())
			{
				OutErrorMessages.Add("Empty response for get repo call");
				return false;
			}
			if (Value->mOrganization_id.IsSet() && !Value->mOrganization_id.GetValue().IsEmpty())
			{
				OrganizationId = Value->mOrganization_id.GetValue();
			}
			RepoOwnerUserId = Value->mOwner_user_id;

			// The repo answered, so its lockable list is known - an absent attribute means no
			// patterns, not "unknown" (the save gate treats only an unset list as unknown)
			OutLockablePatterns = TArray<FString>();
			if (Value->mFile_pattern_attributes.IsSet())
			{
				if (const TArray<FString>* Lockable = Value->mFile_pattern_attributes.GetValue().Find(TEXT("lockable")))
				{
					OutLockablePatterns = *Lockable;
				}
			}
			return true;
		}
	);

	if (!FDiversionAPIAccess::RepositoryManipulationAPI || !FDiversionAPIAccess::OrganizationManagementAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("GetHardLocksEligibility: API not initialized"));
		return false;
	}

	bool Success = FDiversionAPIAccess::RepositoryManipulationAPI->SrcHandlersv2RepoGet(
		InCommand.WsInfo.RepoID,
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120)
		.HandleApiResponse(RepoErrorResponse, RepoVariantResponse, OutErrorMessages);
	if (!Success)
	{
		return false;
	}

	// Personal repos have no collaborators - hard locks don't apply
	if (!OrganizationId.IsSet())
	{
		DiversionUtils::DiversionValidityCheck(RepoOwnerUserId == InCommand.WsInfo.AccountID,
			"Personal repository is owned by a different user than the current account",
			InCommand.WsInfo.AccountID);
		bOutEligible = false;
		return true;
	}

	// Step 2: the repo's effective tier is the org owner's tier (backend get_repo_effective_tier)
	auto SubErrorResponse = OrganizationManagementApi::Fsrc_handlersv2_organization_listSubscriptionInfoDelegate::Bind(
		[&](const TVariant<TSharedPtr<OrganizationSubscriptionInfoList>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant, int StatusCode) {
			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				if (Value.IsValid())
				{
					OutErrorMessages.Add(FString::Printf(TEXT("Received error for org subscription info call: %s"), *Value->mDetail));
				}
			}
			return false;
		}
	);
	auto SubVariantResponse = OrganizationManagementApi::Fsrc_handlersv2_organization_listSubscriptionInfoDelegate::Bind(
		[&](const TVariant<TSharedPtr<OrganizationSubscriptionInfoList>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
			if (!Variant.IsType<TSharedPtr<OrganizationSubscriptionInfoList>>())
			{
				OutErrorMessages.Add("Unexpected response type for org subscription info call");
				return false;
			}
			auto Value = Variant.Get<TSharedPtr<OrganizationSubscriptionInfoList>>();
			if (!Value.IsValid())
			{
				OutErrorMessages.Add("Empty response for org subscription info call");
				return false;
			}

			for (const auto& OrgInfo : Value->mItems)
			{
				if (OrgInfo.mOrganization.mId == OrganizationId.GetValue())
				{
					const bool bTierEligible = OrgInfo.mOrganization.mOwner.mTier.IsSet() &&
						DiversionUtils::IsHardLocksEligibleTier(OrgInfo.mOrganization.mOwner.mTier.GetValue());
					bOutEligible = bTierEligible;
					return true;
				}
			}
			// Not a member of the repo's org - fail closed
			bOutEligible = false;
			return true;
		}
	);

	TArray<FString> OrgIds = { OrganizationId.GetValue() };
	Success = FDiversionAPIAccess::OrganizationManagementAPI->SrcHandlersv2OrganizationListSubscriptionInfo(
		TOptional<TArray<FString>>(OrgIds),
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120)
		.HandleApiResponse(SubErrorResponse, SubVariantResponse, OutErrorMessages);

	return Success;
}
