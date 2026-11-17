// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "DiversionState.h"
#include "FileLock.h"

/**
 * Shared by the lock API calls (SyncFileLocks, SyncUpdateFileLock). Lives here in ApiCalls/
 * so CoreAPI model types stay out of the broadly-included headers; inline so unity builds
 * merging the API-call translation units see one definition (two anonymous-namespace copies
 * previously collided under UE 5.3's unity chunking - C2084).
 */
inline FDiversionLockInfo MakeLockInfoFromModel(const Diversion::CoreAPI::Model::FileLock& InLock,
	const FString& InCurrentUserID)
{
	FDiversionLockInfo LockInfo;
	LockInfo.LockedBy = InLock.mLocked_by;
	LockInfo.LockedByName = InLock.mLocked_by_display_name;
	LockInfo.LockedAt = InLock.mLocked_at;
	LockInfo.LockSource = InLock.mLock_source;
	LockInfo.bLockedByOther = InLock.mLocked_by != InCurrentUserID;
	return LockInfo;
}
