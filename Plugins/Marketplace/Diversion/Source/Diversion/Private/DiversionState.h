// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "DiversionRevision.h"
#include "Conflict.h"

namespace EWorkingCopyState
{
	enum Type
	{
		Unknown,
		Unchanged, // called "clean" in SVN, "Pristine" in Perforce
		Added,
		Deleted,
		Modified,
		Renamed,
		Copied,
		Missing,
		NotControlled,
		Ignored,
	};
}

struct FDiversionResolveInfo : public ISourceControlState::FResolveInfo
{
	FString MergeId;
	FString ConflictId;
	TOptional<Diversion::CoreAPI::Model::Conflict::Resolved_sideEnum> ResolutionSide;
};

struct EDiversionPotentialClashInfo {
	EDiversionPotentialClashInfo(
		const FString& InCommitID,
		const FString& InWorkspaceID,
		const FString& InBranchName,
		const FString& InEmail,
		const FString& InFullName,
		int64 InMtime) :
		CommitID(InCommitID),
		WorkspaceID(InWorkspaceID),
		BranchName(InBranchName),
		Email(InEmail),
		FullName(InFullName),
		Mtime(InMtime)
	{}
	/* ID of the commit the ref is based on */
	FString CommitID;
	/* ID of the workspace being edited in. If empty the conflicting change was already committed */
	FString WorkspaceID;

	/* Name of the branch, if applicable */
	FString BranchName;


	/* Details of the other user editing the file */
	FString Email;
	FString FullName;

	/* Seconds since epoch UTC */
	int64 Mtime;
};

class PotentialClashesList
{
public:
	PotentialClashesList():
		PotentialClashes(TArray<EDiversionPotentialClashInfo>()),
		PotentialClashesRWLock(MakeShared<FRWLock>()) {}

	void ResetPotentialClashes();

	void SetPotentialClashes(const TArray<EDiversionPotentialClashInfo>& InPotentialClashes);

	void AddPotentialClash(const EDiversionPotentialClashInfo& ClashInfo);

	int GetPotentialClashesCount() const;

	TArray<EDiversionPotentialClashInfo> GetPotentialClashes() const;

private:
	TArray<EDiversionPotentialClashInfo> PotentialClashes;
	mutable TSharedRef<FRWLock> PotentialClashesRWLock;
};

struct FDiversionLockInfo {
	/* User ID of the lock holder (auth token subject) */
	FString LockedBy;
	/* Display name of the lock holder */
	FString LockedByName;
	/* Seconds since epoch UTC when the lock was acquired */
	int64 LockedAt = 0;
	/* How the lock was acquired: "auto" or "manual" */
	FString LockSource;
	/* Whether the lock is held by a user other than the current one. Computed at fetch time
	 * against the workspace AccountID so state consumers don't need identity access. */
	bool bLockedByOther = false;

	/* An entry with an empty LockedBy means "no lock" - used to clear cached lock state */
	bool IsSet() const { return !LockedBy.IsEmpty(); }

	bool operator==(const FDiversionLockInfo& Other) const
	{
		return LockedBy == Other.LockedBy
			&& LockedByName == Other.LockedByName
			&& LockedAt == Other.LockedAt
			&& LockSource == Other.LockSource
			&& bLockedByOther == Other.bLockedByOther;
	}
	bool operator!=(const FDiversionLockInfo& Other) const { return !(*this == Other); }

	/**
	 * The holder as the user should see them: their display name, falling back to the user ID
	 * when the server did not supply one, and to a generic phrase when neither is known.
	 * Localized, because it ends up inside sentences shown to the user.
	 */
	FText GetHolderText() const
	{
		if (!LockedByName.IsEmpty())
		{
			return FText::FromString(LockedByName);
		}
		if (!LockedBy.IsEmpty())
		{
			return FText::FromString(LockedBy);
		}
		return NSLOCTEXT("Diversion", "LockHolder_Unknown", "another user");
	}
};

class FileLockInfoHolder
{
public:
	FileLockInfoHolder() : LockInfoRWLock(MakeShared<FRWLock>()) {}

	void SetLockInfo(const FDiversionLockInfo& InLockInfo);

	void ClearLockInfo();

	FDiversionLockInfo GetLockInfo() const;

	bool IsLocked() const;

	bool IsLockedByOther() const;

private:
	FDiversionLockInfo LockInfo;
	mutable TSharedRef<FRWLock> LockInfoRWLock;
};

class FDiversionState : public ISourceControlState
{
public:
	FDiversionState(const FString& InLocalFilename)
		: LocalFilename(InLocalFilename)
		, LocalRevNumber(INVALID_REVISION)
		, WorkingCopyState(EWorkingCopyState::Unknown)
		, TimeStamp(0)
	{
	}

	FDiversionState(const FDiversionState& Other);
	FDiversionState(FDiversionState&& Other) noexcept;
	FDiversionState& operator=(const FDiversionState& Other);
	FDiversionState& operator=(FDiversionState&& Other) noexcept;

	/** ISourceControlState interface */
	virtual int32 GetHistorySize() const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetHistoryItem(int32 HistoryIndex) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(int32 RevisionNumber) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(const FString& InRevision) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetCurrentRevision() const override;
	virtual FResolveInfo GetResolveInfo() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayTooltip() const override;
	virtual const FString& GetFilename() const override;
	virtual const FDateTime& GetTimeStamp() const override;
	virtual bool CanCheckIn() const override;
	/*
	 * Semantic map of the engine's checkout vocabulary in Diversion terms - read this
	 * before overloading any predicate further:
	 *
	 * Diversion has no checkout step: every tracked file is always editable. So
	 * - IsCheckedOut()      == "tracked" (true for every controlled file, soft-clashed and
	 *                          hard-locked included); it does NOT mean an exclusive claim
	 *                          exists. This keeps every file out of the engine checkout/save
	 *                          dialog - save-time lock UX lives in the IsPackageOKToSave
	 *                          veto + the save-decision dialog, NOT in engine dialogs.
	 *                          Beware: engine helpers like FUnsavedAssetsAutoCheckout
	 *                          early-out on it.
	 * - CanCheckout()       == false always (nothing to acquire via the engine flow).
	 * - IsCheckedOutOther() == overloaded to mean soft clash (another workspace has
	 *                          unpublished edits) OR hard-locked-by-other, because it
	 *                          drives exactly the passive engine warning surfaces we want
	 *                          for both (icons, tooltips, unsaved-assets toolbar warning).
	 * - CanEdit()           == true always - even for hard-locked files. Lock enforcement
	 *                          is deliberately NOT wired into the engine's checkout/edit
	 *                          gates: it lives in the save veto + save-decision dialog and
	 *                          the server's commit rejection. Use IsHardLocked /
	 *                          IsHardLockedByOther below for lock state.
	 */
	virtual bool CanCheckout() const override;
	virtual bool IsCheckedOut() const override;
	virtual bool IsCheckedOutOther(FString* Who = nullptr) const override;
	virtual bool IsCheckedOutInOtherBranch(const FString& CurrentBranch = FString()) const override { return false;  }
	virtual bool IsModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return false; }
	virtual bool IsCheckedOutOrModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return IsCheckedOutInOtherBranch(CurrentBranch) || IsModifiedInOtherBranch(CurrentBranch); }
	virtual TArray<FString> GetCheckedOutBranches() const override { return TArray<FString>(); }
	virtual FString GetOtherUserBranchCheckedOuts() const override { return FString(); }
	virtual bool GetOtherBranchHeadModification(FString& HeadBranchOut, FString& ActionOut, int32& HeadChangeListOut) const override { return false; }
	virtual bool IsCurrent() const override;
	virtual bool IsSourceControlled() const override;
	virtual bool IsAdded() const override;
	virtual bool IsDeleted() const override;
	virtual bool IsIgnored() const override;
	virtual bool CanEdit() const override;
	virtual bool IsUnknown() const override;
	virtual bool IsModified() const override;
	virtual bool CanAdd() const override;
	virtual bool CanDelete() const override;
	virtual bool IsConflicted() const override;
	virtual bool CanRevert() const override;

	bool IsAutoSoftLocked() const;

	/**
	 * The soft-clash condition itself: another workspace has unpublished edits to this file,
	 * the soft-lock setting is on, and the file is not already conflicted. Unlike
	 * IsAutoSoftLocked this does not stop being true once the file is modified locally - it
	 * is the question to ask at save time, where whether you saved once already has no
	 * bearing on whether someone else is still editing the file.
	 */
	bool HasPotentialClashes() const;

	/** True if the file is hard-locked on the server (by anyone) */
	bool IsHardLocked() const;

	/** True if the file is hard-locked by another user; optionally returns a display string for the holder */
	bool IsHardLockedByOther(FString* Who = nullptr) const;

	/** FDiversionState interface */
	void ResetState();

	/** Get the list of other users currently editing this file (Potential clash)*/
	FText GetOtherEditorsList() const;

	/** Compact, name-only form of the other editors ("Alice", "Alice, Bob and 2 more") for
	 * embedding in sentences - GetOtherEditorsList is a multi-line tooltip blob */
	FText GetOtherEditorNames(int32 MaxNames = 2) const;

	/** Set WorkingCopyState based on whether the file/directory exists on disk.
	 *  If present, marks as Unchanged and sets TimeStamp to the file's last write time.
	 *  If absent, marks as NotControlled. */
	void SetStateFromDiskPresence();

	void ClearResolveInfo();

	const FDiversionResolveInfo& GetPendingResolveInfo() const;

	void AddPendingResolveInfo(const FDiversionResolveInfo& InResolveInfo);

	void SetOverrideDefaultCheckout()
	{
		// Used to manually tag files as not checked-out
		bOverrideDefaultCheckout = true;
	}

	void ClearOverrideDefaultCheckout()
	{
		bOverrideDefaultCheckout = false;
	}

public:
	/** History of the item, if any */
	TDiversionHistory History;

	/** Filename on disk */
	FString LocalFilename;

	/** Latest rev number at which a file was synced to before being edited */
	int LocalRevNumber;

	UE_DEPRECATED(5.3, "Use PendingResolveInfo.BaseRevision instead")
	FString PendingMergeBaseFileHash;

	/** State of the working copy */
	EWorkingCopyState::Type WorkingCopyState;

	/** The timestamp of the last update */
	FDateTime TimeStamp;

	/** Potential clashes with other editing users */
	PotentialClashesList PotentialClashes;

	/** Server-side hard-lock state of the file */
	FileLockInfoHolder FileLock;

	/** Hash of the entry */
	FString Hash;

	/** Flag to indicate if the file is currently being synced or is it's working state valid */
	bool IsSyncing = false;

private:
	/** Pending rev info with which a file must be resolved, invalid if no resolve pending */
	FDiversionResolveInfo PendingResolveInfo;

	bool bOverrideDefaultCheckout = false;
};
