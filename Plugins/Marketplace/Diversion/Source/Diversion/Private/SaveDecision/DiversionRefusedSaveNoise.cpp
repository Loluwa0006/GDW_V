// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionRefusedSaveNoise.h"

#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

/** Stamped onto the line we write for a refused file, so the flush can recognise the log page
 * as ours - file names alone could match a foreign message that merely mentions the same file */
static const TCHAR* RefusedSaveLogCanary = TEXT("[Diversion:SaveDecision]");

namespace
{
	/**
	 * True when Text contains Token as a whole path/name token - a plain Contains would let
	 * "/Game/Hero" claim a message about "/Game/Hero2", or "Hero.uasset" claim
	 * "SuperHero.uasset". A match must not butt against identifier characters on either side;
	 * '.' stays a valid right boundary so object paths ("/Game/Hero.Hero") still match their
	 * package name.
	 */
	bool ContainsPathToken(const FString& Text, const FString& Token)
	{
		int32 SearchStart = 0;
		int32 Idx = INDEX_NONE;
		while (!Token.IsEmpty() &&
			(Idx = Text.Find(Token, ESearchCase::IgnoreCase, ESearchDir::FromStart, SearchStart)) != INDEX_NONE)
		{
			const int32 End = Idx + Token.Len();
			const bool bStartBoundary = Idx == 0 ||
				!(FChar::IsAlnum(Text[Idx - 1]) || Text[Idx - 1] == TEXT('_'));
			const bool bEndBoundary = End >= Text.Len() ||
				!(FChar::IsAlnum(Text[End]) || Text[End] == TEXT('_') || Text[End] == TEXT('/'));
			if (bStartBoundary && bEndBoundary)
			{
				return true;
			}
			SearchStart = Idx + 1;
		}
		return false;
	}
}

void FDiversionRefusedSaveNoise::Bind()
{
	// Single-cast, bound by the engine to UEditorEngine::OnModalMessageDialog - capture it and
	// put ourselves in front. Restored in Unbind().
	if (!bBound)
	{
		PreviousModalMessageDialog = FCoreDelegates::ModalMessageDialog;
		FCoreDelegates::ModalMessageDialog.BindRaw(this, &FDiversionRefusedSaveNoise::OnModalMessageDialog);
		bBound = true;
	}
}

void FDiversionRefusedSaveNoise::Unbind()
{
	if (bBound)
	{
		FCoreDelegates::ModalMessageDialog = PreviousModalMessageDialog;
		PreviousModalMessageDialog.Unbind();
		bBound = false;
	}
	RefusedNameTokens.Empty();
	bMessageLogWasOpen = false;
}

void FDiversionRefusedSaveNoise::NoteRefused(const FString& InFilename, FOutputDevice* ErrorLog)
{
	if (RefusedNameTokens.Num() == 0)
	{
		bMessageLogWasOpen = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("MessageLog")).IsValid();
	}

	// The engine's messages name the file ("SK_Body.uasset") in the warning and the package
	// ("/Game/LockTest/SK_Body") in the per-package summary, so record both
	RefusedNameTokens.AddUnique(FPaths::GetCleanFilename(InFilename));
	FString PackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(InFilename, PackageName))
	{
		RefusedNameTokens.AddUnique(PackageName);
	}

	// The engine is about to write its own warning to this device anyway, so this line costs
	// no extra noise: it marks the page as ours, and on a page the flush must leave intact it
	// is the one line that says what actually happened.
	if (ErrorLog != nullptr)
	{
		ErrorLog->Logf(ELogVerbosity::Warning,
			TEXT("Diversion: '%s' was not saved - you chose to skip it. %s"),
			*InFilename, RefusedSaveLogCanary);
	}
}

bool FDiversionRefusedSaveNoise::NamesRefusedFile(const FString& InText) const
{
	return RefusedNameTokens.ContainsByPredicate(
		[&InText](const FString& Token) { return ContainsPathToken(InText, Token); });
}

EAppReturnType::Type FDiversionRefusedSaveNoise::OnModalMessageDialog(EAppMsgCategory InCategory,
	EAppMsgType::Type InType, const FText& InText, const FText& InTitle)
{
	// The engine's failed-to-save prompts are CancelRetryContinue (interactive) or Ok
	// (non-interactive flows); nothing else can belong to a save we just refused.
	const bool bIsFailedSavePrompt = InType == EAppMsgType::CancelRetryContinue || InType == EAppMsgType::Ok;
	if (bIsFailedSavePrompt && RefusedNameTokens.Num() > 0 && NamesRefusedFile(InText.ToString()))
	{
		return InType == EAppMsgType::Ok ? EAppReturnType::Ok : EAppReturnType::Continue;
	}

	if (PreviousModalMessageDialog.IsBound())
	{
		return PreviousModalMessageDialog.Execute(InCategory, InType, InText, InTitle);
	}
	// No original handler (should not happen in-editor): answer with the safe default
	return InType == EAppMsgType::Ok ? EAppReturnType::Ok : EAppReturnType::Continue;
}

void FDiversionRefusedSaveNoise::FlushAfterSave()
{
	if (RefusedNameTokens.Num() == 0)
	{
		return;
	}

	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	TSharedRef<IMessageLogListing> Listing = MessageLogModule.GetLogListing("EditorErrors");
	const TArray<TSharedRef<FTokenizedMessage>>& Messages = Listing->GetFilteredMessages();

	// Clear all-or-nothing: the listing has no per-message removal, so the page is only touched
	// when it carries one of our canary lines AND every message on it names a refused file. A
	// genuine disk failure sharing the page keeps the whole page intact.
	bool bFoundCanary = false;
	for (const TSharedRef<FTokenizedMessage>& Message : Messages)
	{
		const FString Text = Message->ToText().ToString();
		if (Text.Contains(RefusedSaveLogCanary))
		{
			bFoundCanary = true;
			continue;
		}
		if (!NamesRefusedFile(Text))
		{
			RefusedNameTokens.Empty();
			return;
		}
	}

	if (bFoundCanary)
	{
		Listing->ClearMessages();
		if (!bMessageLogWasOpen)
		{
			// The tab only exists because of our noise - close it again. A tab the user already
			// had open stays open, just minus the noise.
			if (TSharedPtr<SDockTab> MessageLogTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId("MessageLog")))
			{
				MessageLogTab->RequestCloseTab();
			}
		}
	}

	RefusedNameTokens.Empty();
	bMessageLogWasOpen = false;
}
