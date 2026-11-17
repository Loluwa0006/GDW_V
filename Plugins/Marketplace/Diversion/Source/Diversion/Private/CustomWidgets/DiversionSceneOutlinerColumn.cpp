// Copyright 2024 Diversion Company, Inc. All Rights Reserved.

#include "DiversionSceneOutlinerColumn.h"
#include "DiversionPotentialClashUI.h"
#include "DiversionState.h"
#include "DiversionUtils.h"

#include "ISourceControlModule.h"
#include "SceneOutlinerModule.h"
#include "Styling/AppStyle.h"
#include "SceneOutlinerPublicTypes.h"
#include "ActorTreeItem.h"
#include "Engine/Level.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "Diversion.SceneOutlinerColumn"

namespace DiversionSceneOutlinerColumnLocal
{
	static FName ColumnID("DiversionPotentialClash");

	/** Converts a package name to a full file path */
	FString PackageNameToFilePath(const FString& PackageName)
	{
		FString Filename;
		// Use TryConvertLongPackageNameToFilename instead of DoesPackageExist to avoid disk access.
		// Since we're called with packages from loaded actors, we know they exist.
		if (FPackageName::TryConvertLongPackageNameToFilename(PackageName, Filename))
		{
			return FPaths::ConvertRelativePathToFull(Filename);
		}
		return FString();
	}

	/** Gets the asset path for a given actor (handles both regular and external actors) */
	FString GetAssetPathForActor(AActor* Actor)
	{
		if (!Actor)
		{
			UE_LOG(LogSourceControl, Verbose, TEXT("GetAssetPathForActor: Actor is null"));
			return FString();
		}

		FString PackageName;

		// For external actors (World Partition), each actor is in its own package
		UPackage* ExternalPackage = Actor->GetExternalPackage();
		if (ExternalPackage)
		{
			PackageName = ExternalPackage->GetName();
		}
		else
		{
			// For regular actors, use the level package
			ULevel* Level = Actor->GetLevel();
			if (!Level)
			{
				UE_LOG(LogSourceControl, Verbose, TEXT("GetAssetPathForActor: Actor '%s' has no level"), *Actor->GetName());
				return FString();
			}

			UPackage* Package = Level->GetOutermost();
			if (!Package)
			{
				UE_LOG(LogSourceControl, Verbose, TEXT("GetAssetPathForActor: Actor '%s' level has no package"), *Actor->GetName());
				return FString();
			}

			PackageName = Package->GetName();
		}

		// Convert package name to full file path (source control uses file paths, not package names)
		FString FilePath = PackageNameToFilePath(PackageName);
		UE_LOG(LogSourceControl, Verbose, TEXT("GetAssetPathForActor: Actor '%s' -> Package: '%s' -> FilePath: '%s'"), *Actor->GetName(), *PackageName, *FilePath);
		return FilePath;
	}

	/** Gets the Diversion state for an actor's package */
	TSharedPtr<FDiversionState, ESPMode::ThreadSafe> GetDiversionStateForActor(AActor* Actor)
	{
		FString AssetPath = GetAssetPathForActor(Actor);
		return DiversionUtils::GetDiversionStateForPath(AssetPath);
	}

	/** Result of checking for potential clashes */
	struct FPotentialClashResult
	{
		bool bHasClashes = false;
		FText Tooltip;
	};

	/** Checks if an actor has potential clashes and returns tooltip in a single lookup */
	FPotentialClashResult GetPotentialClashInfo(AActor* Actor)
	{
		FPotentialClashResult Result;

		TSharedPtr<FDiversionState, ESPMode::ThreadSafe> State = GetDiversionStateForActor(Actor);
		if (!State)
		{
			return Result;
		}

		int32 ClashCount = State->PotentialClashes.GetPotentialClashesCount();
		UE_LOG(LogSourceControl, Verbose, TEXT("GetPotentialClashInfo: Actor '%s' has %d potential clashes"), *Actor->GetName(), ClashCount);

		if (ClashCount > 0)
		{
			Result.bHasClashes = true;
			Result.Tooltip = State->GetOtherEditorsList();
		}

		return Result;
	}
}

FDiversionPotentialClashColumn::FDiversionPotentialClashColumn(ISceneOutliner& /*SceneOutliner*/)
{
}

FName FDiversionPotentialClashColumn::GetID()
{
	return DiversionSceneOutlinerColumnLocal::ColumnID;
}

FText FDiversionPotentialClashColumn::GetDisplayName()
{
	return LOCTEXT("PotentialClashColumnName", "Potential Conflicts");
}

FName FDiversionPotentialClashColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FDiversionPotentialClashColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(24.0f)
		.HAlignHeader(HAlign_Center)
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.DefaultTooltip(LOCTEXT("PotentialClashColumnTooltip", "Potential Conflicts"))
		[
			SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground()) // Gray-white tint for header
				.Image(FAppStyle::GetBrush("Icons.Lock"))
		];
}

const TSharedRef<SWidget> FDiversionPotentialClashColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	// Only handle actor tree items
	if (FActorTreeItem* ActorItem = TreeItem->CastTo<FActorTreeItem>())
	{
		AActor* Actor = ActorItem->Actor.Get();
		if (Actor)
		{
			// Single lookup for both clash status and tooltip
			DiversionSceneOutlinerColumnLocal::FPotentialClashResult ClashInfo = DiversionSceneOutlinerColumnLocal::GetPotentialClashInfo(Actor);
			if (ClashInfo.bHasClashes)
			{
				return SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
							.WidthOverride(16.0f)
							.HeightOverride(16.0f)
							[
								SNew(SImage)
									.Image(FAppStyle::GetBrush("Icons.Lock"))
									.ColorAndOpacity(FLinearColor(1.0f, 0.7f, 0.0f)) // Yellow tint for row icons
									.ToolTipText(ClashInfo.Tooltip)
							]
					];
			}
		}
	}

	return SNullWidget::NullWidget;
}

void FDiversionPotentialClashColumn::RegisterColumnType()
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	// Register column type following Epic's pattern:
	// - Visibility: Visible by default
	// - Priority: 31 (after Source Control which is 30)
	// - Factory: Empty (RegisterDefaultColumnType creates its own from the template)
	// - CanBeHidden: true
	// - FillSize: empty (uses FixedWidth from ConstructHeaderRowColumn)
	// - ColumnLabel: Localized display name
	FSceneOutlinerColumnInfo ColumnInfo(
		ESceneOutlinerColumnVisibility::Visible,
		static_cast<uint8>(31),  // Cast for UE 5.3 compatibility (expects uint8, later versions use int32)
		FCreateSceneOutlinerColumn(),
		true,
		TOptional<float>(),
		TAttribute<FText>::CreateStatic(&FDiversionPotentialClashColumn::GetDisplayName)
	);

	SceneOutlinerModule.RegisterDefaultColumnType<FDiversionPotentialClashColumn>(ColumnInfo);
}

void FDiversionPotentialClashColumn::UnregisterColumnType()
{
	if (FSceneOutlinerModule* SceneOutlinerModule = FModuleManager::GetModulePtr<FSceneOutlinerModule>("SceneOutliner"))
	{
		SceneOutlinerModule->UnRegisterColumnType<FDiversionPotentialClashColumn>();
	}
}

#undef LOCTEXT_NAMESPACE
