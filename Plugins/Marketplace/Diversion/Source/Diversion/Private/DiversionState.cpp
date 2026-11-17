// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionState.h"
#include "RevisionControlStyle/RevisionControlStyle.h"
#include "Textures/SlateIcon.h"
#include "HAL/PlatformFileManager.h"

#include "DiversionModule.h"
#include "DiversionUtils.h"
#include "DiversionConfig.h"
#include "Conflict.h"

using namespace Diversion::CoreAPI::Model;

#define LOCTEXT_NAMESPACE "Diversion.State"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void PotentialClashesList::ResetPotentialClashes()
{
	FWriteScopeLock Lock(*PotentialClashesRWLock);
	PotentialClashes.Empty();
}

void PotentialClashesList::SetPotentialClashes(const TArray<EDiversionPotentialClashInfo>& InPotentialClashes)
{
	FWriteScopeLock Lock(*PotentialClashesRWLock);
	PotentialClashes = InPotentialClashes;
}

void PotentialClashesList::AddPotentialClash(const EDiversionPotentialClashInfo& ClashInfo)
{
	FWriteScopeLock Lock(*PotentialClashesRWLock);
	PotentialClashes.Add(ClashInfo);
}

int PotentialClashesList::GetPotentialClashesCount() const
{
	FReadScopeLock Lock(*PotentialClashesRWLock);
	return PotentialClashes.Num();
}

TArray<EDiversionPotentialClashInfo> PotentialClashesList::GetPotentialClashes() const
{
	FReadScopeLock Lock(*PotentialClashesRWLock);
	return PotentialClashes;
}

void FileLockInfoHolder::SetLockInfo(const FDiversionLockInfo& InLockInfo)
{
	FWriteScopeLock Lock(*LockInfoRWLock);
	LockInfo = InLockInfo;
}

void FileLockInfoHolder::ClearLockInfo()
{
	FWriteScopeLock Lock(*LockInfoRWLock);
	LockInfo = FDiversionLockInfo();
}

FDiversionLockInfo FileLockInfoHolder::GetLockInfo() const
{
	FReadScopeLock Lock(*LockInfoRWLock);
	return LockInfo;
}

bool FileLockInfoHolder::IsLocked() const
{
	FReadScopeLock Lock(*LockInfoRWLock);
	return LockInfo.IsSet();
}

bool FileLockInfoHolder::IsLockedByOther() const
{
	FReadScopeLock Lock(*LockInfoRWLock);
	return LockInfo.IsSet() && LockInfo.bLockedByOther;
}

FDiversionState::FDiversionState(const FDiversionState& Other) = default;
FDiversionState::FDiversionState(FDiversionState&& Other) noexcept = default;
FDiversionState& FDiversionState::operator=(const FDiversionState& Other) = default;
FDiversionState& FDiversionState::operator=(FDiversionState&& Other) noexcept = default;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

int32 FDiversionState::GetHistorySize() const
{
	return History.Num();
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FDiversionState::GetHistoryItem(int32 HistoryIndex) const
{
	if (!History.IsValidIndex(HistoryIndex))
	{
		DiversionUtils::DiversionValidityCheck(false,
			"HistoryIndex is out of range", FDiversionModule::Get().GetOriginalAccountID());
		return nullptr;
	}

	return History[HistoryIndex];
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FDiversionState::FindHistoryRevision(int32 RevisionNumber) const
{
	for (const auto& Revision : History)
	{
		if (Revision->GetRevisionNumber() == RevisionNumber)
		{
			return Revision;
		}
	}

	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FDiversionState::FindHistoryRevision(const FString& InRevision) const
{
	for (const auto& Revision : History)
	{
		if (Revision->GetRevision() == InRevision)
		{
			return Revision;
		}
	}

	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FDiversionState::GetCurrentRevision() const
{
	if (LocalRevNumber == INVALID_REVISION)
	{
		return nullptr;
	}

	return FindHistoryRevision(LocalRevNumber);
}

ISourceControlState::FResolveInfo FDiversionState::GetResolveInfo() const
{
	return PendingResolveInfo;
}

FSlateIcon FDiversionState::GetIcon() const
{
	if(IsSyncing)
	{
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.Actions.Refresh");
	}
	if(IsConflicted())
	{
		if(PendingResolveInfo.ResolutionSide.IsSet())
		{
			return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.ConflictResolution.Clear");
		}
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.Conflicted");
	}

	switch (WorkingCopyState)
	{
	case EWorkingCopyState::Modified:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.CheckedOut");
	case EWorkingCopyState::Added:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.OpenForAdd");
	case EWorkingCopyState::Renamed:
	case EWorkingCopyState::Copied:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.Branched");
	case EWorkingCopyState::Deleted: // Deleted & Missing files does not show in Content Browser
	case EWorkingCopyState::Missing:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.MarkedForDelete");
	case EWorkingCopyState::NotControlled:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.NotInDepot");
	case EWorkingCopyState::Unknown:
	case EWorkingCopyState::Unchanged: // Unchanged is the same as "Pristine" (not checked out) for Perforce, ie no icon
	case EWorkingCopyState::Ignored:
	default:
		return FSlateIcon();
	}
}


FText FDiversionState::GetDisplayName() const
{
	if (IsSyncing)
	{
		return LOCTEXT("Syncing", "Syncing");
	}
	if(IsConflicted())
	{
		return LOCTEXT("ContentsConflict", "Contents Conflict");
	}

	switch(WorkingCopyState)
	{
	case EWorkingCopyState::Unknown:
		return LOCTEXT("Unknown", "Unknown");
	case EWorkingCopyState::Unchanged:
		return LOCTEXT("Unchanged", "Unchanged");
	case EWorkingCopyState::Added:
		return LOCTEXT("Added", "Added");
	case EWorkingCopyState::Deleted:
		return LOCTEXT("Deleted", "Deleted");
	case EWorkingCopyState::Modified:
		return LOCTEXT("Modified", "Modified");
	case EWorkingCopyState::Renamed:
		return LOCTEXT("Renamed", "Renamed");
	case EWorkingCopyState::Copied:
		return LOCTEXT("Copied", "Copied");
	case EWorkingCopyState::Ignored:
		return LOCTEXT("Ignored", "Ignored");
	case EWorkingCopyState::NotControlled:
		return LOCTEXT("NotControlled", "Not Under Revision Control");
	case EWorkingCopyState::Missing:
		return LOCTEXT("Missing", "Missing");
	}

	return FText();
}

FText FDiversionState::GetDisplayTooltip() const
{
	if(IsSyncing)
	{
		return LOCTEXT("Syncing_Tooltip", "Syncing item state with the server");
	}
	if(IsConflicted())
	{
		if(PendingResolveInfo.ResolutionSide.IsSet())
		{
			switch (PendingResolveInfo.ResolutionSide.GetValue())
			{
				case Conflict::Resolved_sideEnum::BASE:
					return LOCTEXT("ContentsConflict_Tooltip_AcceptCurrent", "File conflict resolved to 'Current'.");
				case Conflict::Resolved_sideEnum::OTHER:
					return LOCTEXT("ContentsConflict_Tooltip_AcceptIncoming", "File conflict resolved to 'Incoming'.");
				case Conflict::Resolved_sideEnum::RESULT:
					return LOCTEXT("ContentsConflict_Tooltip_AcceptCombined", "File conflict resolved to 'Combined'.");
			}
		}
		return LOCTEXT("ContentsConflict_Tooltip", "The contents of the item conflict with updates received from the repository.");
	}

	switch(WorkingCopyState)
	{
	case EWorkingCopyState::Unknown:
		return LOCTEXT("Unknown_Tooltip", "Unknown revision control state");
	case EWorkingCopyState::Unchanged:
		return LOCTEXT("Pristine_Tooltip", "There are no modifications");
	case EWorkingCopyState::Added:
		return LOCTEXT("Added_Tooltip", "Item is scheduled for addition");
	case EWorkingCopyState::Deleted:
		return LOCTEXT("Deleted_Tooltip", "Item is scheduled for deletion");
	case EWorkingCopyState::Modified:
		return LOCTEXT("Modified_Tooltip", "Item has been modified");
	case EWorkingCopyState::Renamed:
		return LOCTEXT("Renamed_Tooltip", "Item has been renamed");
	case EWorkingCopyState::Copied:
		return LOCTEXT("Copied_Tooltip", "Item has been copied");
	case EWorkingCopyState::Ignored:
		return LOCTEXT("Ignored_Tooltip", "Item is being ignored.");
	case EWorkingCopyState::NotControlled:
		return LOCTEXT("NotControlled_Tooltip", "Item is not under version control.");
	case EWorkingCopyState::Missing:
		return LOCTEXT("Missing_Tooltip", "Item is missing (e.g., you moved or deleted it without using Diversion). This also indicates that a directory is incomplete (a checkout or update was interrupted).");
	}

	return FText();
}

const FString& FDiversionState::GetFilename() const
{
	return LocalFilename;
}

const FDateTime& FDiversionState::GetTimeStamp() const
{
	return TimeStamp;
}

// Deleted and Missing assets cannot appear in the Content Browser, but the do in the Submit files to Version Control window!
bool FDiversionState::CanCheckIn() const
{
	return !IsAutoSoftLocked() && !IsHardLockedByOther() && (
		WorkingCopyState == EWorkingCopyState::Added
		|| WorkingCopyState == EWorkingCopyState::Deleted
		|| WorkingCopyState == EWorkingCopyState::Missing
		|| WorkingCopyState == EWorkingCopyState::Modified
		|| WorkingCopyState == EWorkingCopyState::Renamed);
}

bool FDiversionState::CanCheckout() const
{
	return false; // With Diversion all tracked files in the working copy are always already checked-out (as opposed to Perforce)
}

bool FDiversionState::IsCheckedOut() const
{
	// Override feature to set an object to be unchecked-out
	// This is used to overcome perforce specific code of the Changelist validation
	// See PreChangelistValidationDelegateWrapper in DiversionSourceControlProvider.cpp
	if (bOverrideDefaultCheckout)
	{
		return false;
	}

	// Every tracked file is "checked out" in Diversion - there is no checkout step. This
	// deliberately keeps ALL files (soft-clashed and hard-locked alike) out of the engine's
	// checkout/save dialog: both lock kinds get their save-time UX from the IsPackageOKToSave
	// gate and the save-decision dialog instead (soft-clashed files used to flip to false
	// here to ride the engine dialog via re-worded strings - that ride is retired).
	return IsSourceControlled();
}

bool FDiversionState::IsCheckedOutOther(FString* Who) const
{
	// Hard lock: repo-wide and backend-enforced - reported regardless of the soft-lock
	// setting and with NO local-modification escape (unlike the soft lock below).
	// Fills Who with the lock holder so engine dialogs can show "Checked out by: {user}".
	if (IsHardLockedByOther(Who))
	{
		return true;
	}
	return HasPotentialClashes();
}

bool FDiversionState::HasPotentialClashes() const
{
	return
		IsDiversionSoftLockEnabled() && // Disable this feature if the user wants to
		(PotentialClashes.GetPotentialClashesCount() > 0) &&
		!IsConflicted(); // If the file is already conflicted, we don't want to auto lock it
}

bool FDiversionState::IsCurrent() const
{
	return true; // @todo check the state of the HEAD versus the state of tracked branch on remote
}

bool FDiversionState::IsSourceControlled() const
{
	return WorkingCopyState != EWorkingCopyState::NotControlled && WorkingCopyState != EWorkingCopyState::Ignored && WorkingCopyState != EWorkingCopyState::Unknown;
}

bool FDiversionState::IsAdded() const
{
	return WorkingCopyState == EWorkingCopyState::Added;
}

bool FDiversionState::IsDeleted() const
{
	return WorkingCopyState == EWorkingCopyState::Deleted || WorkingCopyState == EWorkingCopyState::Missing;
}

bool FDiversionState::IsIgnored() const
{
	return WorkingCopyState == EWorkingCopyState::Ignored;
}

bool FDiversionState::CanEdit() const
{
	return true; // With Diversion all files in the working copy are always editable (as opposed to Perforce)
}

bool FDiversionState::CanDelete() const
{
	// Hard-locked-by-other files must not be deletable (P4 parity: delete would dirty
	// referencers and the deletion itself could not be committed anyway)
	return IsSourceControlled() && IsCurrent() && !IsHardLockedByOther();
}

bool FDiversionState::IsUnknown() const
{
	return WorkingCopyState == EWorkingCopyState::Unknown;
}

bool FDiversionState::IsModified() const
{
	// Note: for Perforce, a checked-out file is locked for modification (whereas with Diversion all tracked files are checked-out),
	// so for a clean "check-in" (commit) checked-out files unmodified should be removed from the changeset (the index)
	//
	// Thus, before check-in UE Editor call RevertUnchangedFiles() in PromptForCheckin() and CheckinFiles().
	//
	// So here we must take care to enumerate all states that need to be committed,
	// all other will be discarded :
	//  - Unknown
	//  - Unchanged
	//  - NotControlled
	//  - Ignored
	return WorkingCopyState == EWorkingCopyState::Added
		|| WorkingCopyState == EWorkingCopyState::Deleted
		|| WorkingCopyState == EWorkingCopyState::Modified
		|| WorkingCopyState == EWorkingCopyState::Renamed
		|| WorkingCopyState == EWorkingCopyState::Copied
		|| WorkingCopyState == EWorkingCopyState::Missing;
}


bool FDiversionState::CanAdd() const
{
	return WorkingCopyState == EWorkingCopyState::NotControlled;
}

bool FDiversionState::IsConflicted() const
{
	return PendingResolveInfo;
}

bool FDiversionState::CanRevert() const
{
	// Hard-locked files can't be checked in, but reverting (discarding local changes) must
	// stay available - it's the sanctioned way out of edits made to a file someone else locked.
	return CanCheckIn() || (IsModified() && IsHardLockedByOther());
}

bool FDiversionState::IsAutoSoftLocked() const
{
	// Deliberately keyed to the soft-clash condition, NOT IsCheckedOutOther(): the latter
	// now also reports hard locks, whose enforcement must stay independent of IsModified().
	// HasPotentialClashes embeds the soft-lock enable setting.
	return HasPotentialClashes() && !IsModified();
}

void FDiversionState::SetStateFromDiskPresence()
{
	if (FPaths::FileExists(LocalFilename) || FPaths::DirectoryExists(LocalFilename))
	{
		WorkingCopyState = EWorkingCopyState::Unchanged;
		TimeStamp = FPlatformFileManager::Get().GetPlatformFile().GetTimeStamp(*LocalFilename);
	}
}

bool FDiversionState::IsHardLocked() const
{
	return FileLock.IsLocked();
}

bool FDiversionState::IsHardLockedByOther(FString* Who) const
{
	const FDiversionLockInfo LockInfo = FileLock.GetLockInfo();
	if (!LockInfo.IsSet() || !LockInfo.bLockedByOther)
	{
		return false;
	}
	if (Who != nullptr)
	{
		*Who = LockInfo.GetHolderText().ToString();
	}
	return true;
}

void FDiversionState::ResetState()
{
	WorkingCopyState = EWorkingCopyState::Unchanged;
	PotentialClashes.ResetPotentialClashes();
	TimeStamp = FDateTime::MinValue();
	IsSyncing = false;
}

FText FDiversionState::GetOtherEditorsList() const
{
	// Generate a local copy of the potential clashes array
	// Used since this array might get changed in the background by the worker thread
	TArray<EDiversionPotentialClashInfo> PotentialClashesCopy = PotentialClashes.GetPotentialClashes();

	TArray<FText> Lines;
	Lines.Add(LOCTEXT("OtherEditors_Header", "Potential Conflict!\nFile is also edited by:"));
	for (int i = 0; i < PotentialClashesCopy.Num(); i++)
	{
		const auto& Clash = PotentialClashesCopy[i];
		FFormatNamedArguments Args;
		Args.Add(TEXT("Name"), FText::FromString(!Clash.FullName.IsEmpty() ? Clash.FullName : Clash.Email));
		Args.Add(TEXT("Time"), DiversionUtils::FormatUnixTimestamp(Clash.Mtime));
		Args.Add(TEXT("Branch"), FText::FromString(Clash.BranchName));
		Lines.Add(!Clash.WorkspaceID.IsEmpty()
			? FText::Format(LOCTEXT("OtherEditors_Line_NotCommitted", "{Name} at {Time} on branch {Branch} (not-committed)"), Args)
			: FText::Format(LOCTEXT("OtherEditors_Line_Committed", "{Name} at {Time} on branch {Branch} (committed)"), Args));
		constexpr int MAX_OTHER_EDITORS = 4;
		if (i > MAX_OTHER_EDITORS)
		{
			Lines.Add(FText::Format(LOCTEXT("OtherEditors_More", "... and {0} more"), FText::AsNumber(PotentialClashesCopy.Num() - i)));
			break;
		}
	}
	return FText::Join(FText::AsCultureInvariant(TEXT("\n")), Lines);
}

FText FDiversionState::GetOtherEditorNames(int32 MaxNames) const
{
	TArray<EDiversionPotentialClashInfo> PotentialClashesCopy = PotentialClashes.GetPotentialClashes();

	TArray<FString> Names;
	for (const EDiversionPotentialClashInfo& Clash : PotentialClashesCopy)
	{
		const FString Name = !Clash.FullName.IsEmpty() ? Clash.FullName : Clash.Email;
		if (!Name.IsEmpty())
		{
			Names.AddUnique(Name);
		}
	}
	if (Names.IsEmpty())
	{
		return LOCTEXT("OtherEditorNames_Unknown", "another workspace");
	}

	TArray<FText> ShownNames;
	const int32 ShownCount = FMath::Min(Names.Num(), MaxNames);
	for (int32 Index = 0; Index < ShownCount; ++Index)
	{
		ShownNames.Add(FText::FromString(Names[Index]));
	}
	const FText Result = FText::Join(LOCTEXT("OtherEditorNames_Separator", ", "), ShownNames);
	if (Names.Num() > ShownCount)
	{
		return FText::Format(LOCTEXT("OtherEditorNames_More", "{0} and {1} more"), Result, FText::AsNumber(Names.Num() - ShownCount));
	}
	return Result;
}

void FDiversionState::ClearResolveInfo()
{
	PendingResolveInfo.RemoteRevision.Empty();
	PendingResolveInfo.RemoteFile.Empty();
}

const FDiversionResolveInfo& FDiversionState::GetPendingResolveInfo() const
{
	return PendingResolveInfo;
}

void FDiversionState::AddPendingResolveInfo(const FDiversionResolveInfo& InResolveInfo)
{
	PendingResolveInfo = InResolveInfo;
}

#undef LOCTEXT_NAMESPACE
