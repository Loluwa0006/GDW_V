// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#pragma once

#include "Widgets/Notifications/SNotificationList.h"

struct FDiversionNotification
{
	// bSticky is for notifications the user must handle now, or that we dismiss
	// programmatically later. Everything else is informational and self-expires.
	FDiversionNotification(const FText& Message, const TArray<FNotificationButtonInfo>& Button,
		SNotificationItem::ECompletionState Type, bool bSticky = false)
		: Message(Message),
		  Button(Button),
		  Type(Type),
		  bSticky(bSticky)
	{
	}

	FText Message;
	TArray<FNotificationButtonInfo> Button;
	SNotificationItem::ECompletionState Type;
	bool bSticky;
};


class COMMON_API FDiversionNotificationManager
{
public:
	typedef int32 FNotificationId;
	FNotificationId ShowNotification(const FDiversionNotification& InDiversionNotificationInfo);

	void DismissNotification(FNotificationId NotificationId);

private:

	struct FOpenNotification
	{
		FNotificationId NotificationId;
		TWeakPtr<class SNotificationItem> NotificationItem;
		const FText NotificationMessage;

		FOpenNotification(FNotificationId InNotificationId,
						  const TWeakPtr<class SNotificationItem>& InNotificationItem,
						  const FText InNotificationMessage)
			: NotificationId(InNotificationId)
			, NotificationItem(InNotificationItem)
			, NotificationMessage( InNotificationMessage)
		{
		}
		FOpenNotification() : NotificationId(INDEX_NONE) {}
	};
	// Notifications that wasn't actively dismissed
	TArray<FOpenNotification> OpenNotifications;
	FNotificationId NextNotificationId = 0;
};
