// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "DiversionOperations.h"
#include "DiversionAPIAccess.h"
#include "RepositoryMergeManipulationApi.h"


using namespace Diversion::CoreAPI;


bool DiversionUtils::RunResolvePath(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	const FString& InMergeId, const FString& InConflictId, bool WaitForSync)
{
	if (WaitForSync && !WaitForAgentSync(InCommand, OutInfoMessages, OutErrorMessages))
	{
		return false;
	}

	FString FilePath;
	if (InCommand.Files.Num() <= 0)
	{
		FString ErrMsg = "RunResolvePath: No file path provided";
		UE_LOG(LogSourceControl, Error, TEXT("%s"), *ErrMsg);
		OutErrorMessages.Add(ErrMsg);
		return false;
	}
	FilePath = InCommand.Files[0];

	// Get info of the specific blob
	if(!GetWsBlobInfo(InCommand, OutInfoMessages, OutInfoMessages, FilePath))
	{
		UE_LOG(LogSourceControl, Error, TEXT("Diversion: RunResolvePath: Failed getting blob info"));
		return false;
	}

	FDiversionResolveFileWorker& Worker = static_cast<FDiversionResolveFileWorker&>(InCommand.Worker.Get());

	auto ErrorResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_setResultDelegate::Bind(
		[&]() {
			return false;
		}
	);
	auto VariantResponse = RepositoryMergeManipulationApi::Fsrc_handlersv2_merge_setResultDelegate::Bind(
		[&]() {
			OutInfoMessages.Add("File resolved");
			return true;
		}
	);

	if (!FDiversionAPIAccess::RepositoryMergeManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunResolvePath: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryMergeManipulationAPI->SrcHandlersv2MergeSetResult(
		InCommand.WsInfo.RepoID, InMergeId, InConflictId, Worker.FileEntry.mMode, MakeShared<HttpContent>(), Worker.FileEntry.mBlob->mSize,
		Worker.FileEntry.mBlob->mSha, Worker.FileEntry.mBlob->mStorage_backend, Worker.FileEntry.mBlob->mStorage_uri, DiversionUtils::ConvertFullPathToRelative(FilePath, InCommand.WsInfo.GetPath()),
		FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
