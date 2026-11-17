// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionAPIAccess.h"
#include "SupportApi.h"

using namespace Diversion::CoreAPI;

bool DiversionUtils::SendErrorToBE(const FString& AccountID, const FString& InErrorMessageToReport, const FString& InStackTrace)
{
	auto ErrorResponse = SupportApi::Fsrc_handlers_support_errorReportDelegate::Bind([&]() {
		return false;
	});

	auto VariantResponse = SupportApi::Fsrc_handlers_support_errorReportDelegate::Bind([&]() {
		return true;
	});

	TSharedPtr<ErrorReport> Request = MakeShared<ErrorReport>();
	Request->mSource = "Unreal-Diversion-Plugin";
	Request->mError = InErrorMessageToReport;
	Request->mStack = InStackTrace;

	if (!FDiversionAPIAccess::SupportAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("SendErrorToBE: API not initialized"));
		return false;
	}

	TArray<FString> ErrorMessages;
	return FDiversionAPIAccess::SupportAPI->SrcHandlersSupportErrorReport(Request,
		FDiversionAPIAccess::GetAccessToken(AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, ErrorMessages);
}
