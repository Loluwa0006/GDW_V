// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionOperations.h"
#include "DiversionModule.h"
#include "DiversionCommand.h"
#include "DiversionAPIAccess.h"
#include "DefaultApi.h"

using namespace Diversion::AgentAPI;

bool DiversionUtils::RunAgentHealthCheck(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages)
{
	auto ErrorResponse = DefaultApi::FisAliveDelegate::Bind([&]() {
		return false;
	});

	auto VariantResponse = DefaultApi::FisAliveDelegate::Bind([&](const TVariant<TSharedPtr<IsAlive_200_response>>& Variant) {
		if (!Variant.IsType<TSharedPtr<Diversion::AgentAPI::Model::IsAlive_200_response>>())
		{
			// Unexpected response type
			OutErrorMessages.Add("Unexpected response type");
			return false;
		}

		auto IsAliveResponse = Variant.Get<TSharedPtr<Diversion::AgentAPI::Model::IsAlive_200_response>>();
		FDiversionAgentHealthCheckWorker& Worker = static_cast<FDiversionAgentHealthCheckWorker&>(InCommand.Worker.Get());
		if (IsAliveResponse->mVersion.IsSet())
		{
			Worker.AgentVersion = IsAliveResponse->mVersion.GetValue();
			Worker.IsAgentAlive = true;
		}
		else
		{
			Worker.AgentVersion = "N/a";
			Worker.IsAgentAlive = false;
		}

		return true;
	});

	if (!FDiversionAPIAccess::AgentAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunAgentHealthCheck: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::AgentAPI->IsAlive(TOptional<bool>(), FString(), {}, 5, 5)
		.HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}

