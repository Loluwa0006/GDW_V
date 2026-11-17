// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionNotificationManager.h"
#include "Framework/Notifications/NotificationManager.h"

#include "CommonModule.h"

#include "Runtime/Launch/Resources/Version.h"

#define LOCTEXT_NAMESPACE "Diversion"

namespace
{
	constexpr float AutoDismissDurationSeconds = 5.0f;
	constexpr float AutoDismissFadeOutSeconds = 0.5f;
}

FDiversionNotificationManager::FNotificationId FDiversionNotificationManager::ShowNotification(const FDiversionNotification& InDiversionNotificationInfo)
{
	FText NotificationMessage = InDiversionNotificationInfo.Message;
	UE_LOG(LogDiversionCommon, Display, TEXT("Showing notification: %s"), *NotificationMessage.ToString());

	// Self-expired notifications leave a dead entry behind - drop them so the lookup below
	// can't match one that's already gone.
	OpenNotifications.RemoveAll([](const FOpenNotification& Notification)
		{ return !Notification.NotificationItem.IsValid(); });

	// Dismiss a notification if it's already open
	const FOpenNotification* ExistingNotification = OpenNotifications.FindByPredicate(
		[NotificationMessage](const FOpenNotification& Notification)
		{
			return Notification.NotificationMessage.CompareTo(NotificationMessage) == 0;
		});
	if(ExistingNotification != nullptr)
	{
		DismissNotification(ExistingNotification->NotificationId);
	}

	const bool bSticky = InDiversionNotificationInfo.bSticky;

	FNotificationInfo NotificationInfo(InDiversionNotificationInfo.Message);
	NotificationInfo.bFireAndForget = !bSticky;
	NotificationInfo.bUseThrobber = false;
	NotificationInfo.FadeOutDuration = bSticky ? 0.f : AutoDismissFadeOutSeconds;
	NotificationInfo.ExpireDuration = bSticky ? FLT_MAX : AutoDismissDurationSeconds;

	int32 NotificationId = NextNotificationId++;
	for(auto& Button : InDiversionNotificationInfo.Button)
	{
		// Making sure the button will dismiss the notification
		FNotificationButtonInfo ButtonCopy = Button;
		ButtonCopy.Callback = FSimpleDelegate::CreateLambda([NotificationId, NotificationMessage, Button, this]()
			{
				Button.Callback.ExecuteIfBound();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION <= 4
				FString ButtonText = Button.Text.ToString();
#else
				FString ButtonText = Button.Text.Get().ToString();
#endif
				UE_LOG(LogDiversionCommon, Display, TEXT("button clicked: %s for Notification: %s "), *ButtonText, *NotificationMessage.ToString());
				DismissNotification(NotificationId);
			});
		NotificationInfo.ButtonDetails.Add(ButtonCopy);
	}
	if(bSticky)
	{
		const FText DismissText = LOCTEXT("DismissMessageButton", "Dismiss");
		const FText OkText = LOCTEXT("DismissMessageButton_Ok", "Ok");
		NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(DismissText,
			FText(),
			FSimpleDelegate::CreateLambda([NotificationId, NotificationMessage, this]()
			{
				UE_LOG(LogDiversionCommon, Display, TEXT("Notification dismissed by the user: %s"), *NotificationMessage.ToString());
				DismissNotification(NotificationId);
			}), SNotificationItem::CS_Fail));
		NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(OkText,
			FText(),
			FSimpleDelegate::CreateLambda([NotificationId, NotificationMessage, this]()
			{
				UE_LOG(LogDiversionCommon, Display, TEXT("Notification acknowledged by the user: %s"), *NotificationMessage.ToString());
				DismissNotification(NotificationId);
			}), SNotificationItem::CS_Success));
		NotificationInfo.ButtonDetails.Add(FNotificationButtonInfo(DismissText,
			FText(),
			FSimpleDelegate::CreateLambda([NotificationId, NotificationMessage, this]()
			{
				UE_LOG(LogDiversionCommon, Display, TEXT("Notification dismissed by the user: %s"), *NotificationMessage.ToString());
				DismissNotification(NotificationId);
			}), SNotificationItem::CS_Pending));
	}


	TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
	if(NotificationItem.IsValid())
	{
		NotificationItem->SetCompletionState(InDiversionNotificationInfo.Type);
		OpenNotifications.Emplace(NotificationId, NotificationItem, NotificationMessage);
	}

	return NotificationId;
}

void FDiversionNotificationManager::DismissNotification(int32 NotificationId)
{
	int32 NotificationIndex = OpenNotifications.IndexOfByPredicate([NotificationId](const FOpenNotification& N)
		{ return N.NotificationId == NotificationId; });
	if (NotificationIndex != INDEX_NONE)
	{
		TSharedPtr<SNotificationItem> NotificationItem = OpenNotifications[NotificationIndex].NotificationItem.Pin();
		if (NotificationItem.IsValid())
		{
			NotificationItem->SetExpireDuration(0.0f);
			NotificationItem->ExpireAndFadeout();
		}

		OpenNotifications.RemoveAtSwap(NotificationIndex);
	}
}

#undef LOCTEXT_NAMESPACE
