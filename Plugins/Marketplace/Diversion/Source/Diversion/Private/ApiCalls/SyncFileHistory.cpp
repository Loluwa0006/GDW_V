// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionUtils.h"
#include "DiversionCommand.h"
#include "DiversionModule.h"
#include "IDiversionStatusWorker.h"
#include "DiversionAPIAccess.h"
#include "RepositoryManipulationApi.h"


using namespace Diversion::CoreAPI;


static void PopulateSCCRevision(TSharedRef<FDiversionRevision, ESPMode::ThreadSafe> InOutSCCRev,
	const FString& InCommitId, const int64 InCreatedTs, const FString& InPath,
	const int InStatus, TOptional<FileEntry_blob> InBlob,
	const FString& InUserName, const FString& InCommitMessage, const WorkspaceInfo& InWsInfo)
{
	auto OrdinalCommitId = DiversionUtils::RefToOrdinalId(InCommitId);
	auto OrdinalCommitNumber = FCString::Atoi(*OrdinalCommitId);

	InOutSCCRev->RevisionNumber = OrdinalCommitNumber;
	InOutSCCRev->ShortCommitId = OrdinalCommitId;
	InOutSCCRev->CommitId = InCommitId;
	InOutSCCRev->CommitIdNumber = OrdinalCommitNumber;
	InOutSCCRev->Date = FDateTime::FromUnixTimestamp(InCreatedTs);
	InOutSCCRev->Filename = InPath;

	switch (InStatus)
	{
	case 1:
		InOutSCCRev->Action = "intact";
		break;
	case 2:
		InOutSCCRev->Action = "add";
		break;
	case 3:
		InOutSCCRev->Action = "modified";
		break;
	case 4:
		InOutSCCRev->Action = "delete";
		break;
	}

	if (InBlob.IsSet())
	{
		InOutSCCRev->FileHash = InBlob.GetValue().mSha;
		InOutSCCRev->FileSize = static_cast<int32>(InBlob.GetValue().mSize);
	}

	InOutSCCRev->UserName = InUserName;
	InOutSCCRev->Description = InCommitMessage;

	InOutSCCRev->WsInfo = InWsInfo;
}


FString ExtractFileNameFromCommitEntry(const Commit& InCommitEntry)
{
	if(InCommitEntry.mAuthor.mFull_name.IsSet() && !InCommitEntry.mAuthor.mFull_name->IsEmpty())
	{
		return InCommitEntry.mAuthor.mFull_name.GetValue();
	}
	else if(InCommitEntry.mAuthor.mEmail.IsSet() && !InCommitEntry.mAuthor.mEmail->IsEmpty())
	{
		return InCommitEntry.mAuthor.mEmail.GetValue();
	}
	else
	{
		return InCommitEntry.mAuthor.mId;
	}
}


bool DiversionUtils::RunGetHistory(const FDiversionCommand& InCommand, TArray<FString>& OutInfoMessages, TArray<FString>& OutErrorMessages,
	const FString& InFile, const FString* MergeFromRef)
{

	auto ErrorResponse = RepositoryManipulationApi::Fsrc_handlersv2_commit_getObjectHistoryDelegate::Bind([&]() {
		return false;
	});

	auto VariantResponse = RepositoryManipulationApi::Fsrc_handlersv2_commit_getObjectHistoryDelegate::Bind(
		[&](const TVariant<TSharedPtr<Src_handlersv2_commit_get_object_history_200_response>, TSharedPtr<Diversion::CoreAPI::Model::Error>>& Variant) {
			if (Variant.IsType<TSharedPtr<Diversion::CoreAPI::Model::Error>>())
			{
				auto Value = Variant.Get<TSharedPtr<Diversion::CoreAPI::Model::Error>>();
				OutErrorMessages.Add(FString::Printf(TEXT("Received error for get file history call: %s"), *Value->mDetail));
				return false;
			}

			if (!Variant.IsType<TSharedPtr<Src_handlersv2_commit_get_object_history_200_response>>())
			{
				// Unexpected response type
				OutErrorMessages.Add("Unexpected response type");
				return false;
			}

			auto Value = Variant.Get<TSharedPtr<Src_handlersv2_commit_get_object_history_200_response>>();
			IDiversionStatusWorker& Worker = static_cast<IDiversionStatusWorker&>(InCommand.Worker.Get());

			FString FilePath;
			auto Revisions = Value->mEntries;
			if (Revisions.Num() == 0)
			{
				OutInfoMessages.Add("No history found for the file");
				return true;
			}

			FilePath = DiversionUtils::ConvertRelativePathToDiversionFull(Value->mEntries[0].mEntry.mPath, InCommand.WsInfo.GetPath());

			TDiversionHistory History;
			for (auto& Revision : Revisions)
			{
				TSharedRef<FDiversionRevision, ESPMode::ThreadSafe> SourceControlRevision = MakeShared<FDiversionRevision>();

				FString UserName = ExtractFileNameFromCommitEntry(Revision.mCommit);
				FString CommitMessage = Revision.mCommit.mCommit_message.IsSet() ? Revision.mCommit.mCommit_message.GetValue() : "";

				PopulateSCCRevision(SourceControlRevision, Revision.mCommit.mCommit_id, Revision.mCommit.mCreated_ts, Revision.mEntry.mPath,
					Revision.mEntry.mStatus, Revision.mEntry.mBlob, UserName, CommitMessage, InCommand.WsInfo);
				History.Add(MoveTemp(SourceControlRevision));
			}

			if (TDiversionHistory* Existing = Worker.Histories.Find(FilePath))
			{
				History.Append(*Existing);
			}

			Worker.Histories.Add(FilePath, MoveTemp(History));
			return true;
		}
	);

	FString RefId = InCommand.WsInfo.WorkspaceID;
	TOptional<int32> Limit = TOptional<int32>();
	if (MergeFromRef)
	{
		RefId = *MergeFromRef;
		Limit = 1;
	}

	if (!FDiversionAPIAccess::RepositoryManipulationAPI)
	{
		UE_LOG(LogSourceControl, Error, TEXT("RunGetHistory: API not initialized"));
		return false;
	}

	return FDiversionAPIAccess::RepositoryManipulationAPI->SrcHandlersv2CommitGetObjectHistory(InCommand.WsInfo.RepoID,
		RefId, ConvertFullPathToRelative(InFile, InCommand.WsInfo.GetPath()), Limit, TOptional<int32>(), FDiversionAPIAccess::GetAccessToken(InCommand.WsInfo.AccountID), {}, 5, 120).
		HandleApiResponse(ErrorResponse, VariantResponse, OutErrorMessages);
}
