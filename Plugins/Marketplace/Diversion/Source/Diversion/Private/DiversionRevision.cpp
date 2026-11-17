// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionRevision.h"

#include "ISourceControlModule.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "DiversionModule.h"
#include "DiversionUtils.h"
#include "ScopedSourceControlProgress.h"

#define LOCTEXT_NAMESPACE "Diversion"

bool FDiversionRevision::Get(FString& InOutFilename, EConcurrency::Type InConcurrency) const
{
	if (InConcurrency != EConcurrency::Synchronous)
	{
		UE_LOG(LogSourceControl, Warning, TEXT("Only EConcurrency::Synchronous is tested/supported for this operation."));
	}

	// if a filename for the temp file wasn't supplied generate a unique-ish one
	if (InOutFilename.Len() == 0)
	{
		// create the diff dir if we don't already have it (Diversion won't)
		IFileManager::Get().MakeDirectory(*FPaths::DiffDir(), true);
		// create a unique temp file name based on the unique commit Id
		const FString TempFileName = FString::Printf(TEXT("%stemp-%s-%s"), *FPaths::DiffDir(), *ShortCommitId, *FPaths::GetCleanFilename(Filename));
		InOutFilename = FPaths::ConvertRelativePathToFull(TempFileName);
	}

	// Diff against the revision
	const FString Parameter = FString::Printf(TEXT("%s:%s"), *CommitId, *Filename);

	bool bCommandSuccessful;
	if (FPaths::FileExists(InOutFilename))
	{
		bCommandSuccessful = true; // if the temp file already exists, reuse it directly
	}
	else
	{
		TArray<FString> InfoMessages;
		TArray<FString> ErrorMessages;
		bCommandSuccessful = DiversionUtils::DownloadBlob(InfoMessages, ErrorMessages, CommitId, InOutFilename, Filename, WsInfo);
	}
	return bCommandSuccessful;
}

bool FDiversionRevision::GetAnnotated(TArray<FAnnotationLine>& OutLines) const
{
	return false;
}

bool FDiversionRevision::GetAnnotated(FString& InOutFilename) const
{
	return false;
}

const FString& FDiversionRevision::GetFilename() const
{
	return Filename;
}

int32 FDiversionRevision::GetRevisionNumber() const
{
	return RevisionNumber;
}

const FString& FDiversionRevision::GetRevision() const
{
	return ShortCommitId;
}

const FString& FDiversionRevision::GetDescription() const
{
	return Description;
}

const FString& FDiversionRevision::GetClientSpec() const
{
	static FString EmptyString(TEXT(""));
	return EmptyString;
}

const FString& FDiversionRevision::GetAction() const
{
	return Action;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FDiversionRevision::GetBranchSource() const
{
	// if this revision was copied/moved from some other revision
	return BranchSource;
}

const FDateTime& FDiversionRevision::GetDate() const
{
	return Date;
}

int32 FDiversionRevision::GetCheckInIdentifier() const
{
	return CommitIdNumber;
}

int32 FDiversionRevision::GetFileSize() const
{
	return FileSize;
}

#undef LOCTEXT_NAMESPACE
