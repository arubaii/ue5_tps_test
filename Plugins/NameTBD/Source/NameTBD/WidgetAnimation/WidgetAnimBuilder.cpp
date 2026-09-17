// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilder.h"

#include "Components/Image.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Factories/DataAssetFactory.h"
#include "FileHelpers.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

#define LOCTEXT_NAMESPACE "WidgetAnimBuilder"

namespace
{
	const TCHAR* WidgetAnimAssetPrefix = TEXT("WA_");
	const TCHAR* DefaultAnimationName = TEXT("NewWidgetAnimation");
	const TCHAR* FallbackSavePath = TEXT("/Game");

#if WITH_EDITOR
	/** UWidgetBlueprint lives in UMGEditor; named by path so this module doesn't have to depend on it. */
	FTopLevelAssetPath GetWidgetBlueprintClassPath()
	{
		return FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
	}
#endif

#if WITH_EDITOR
	void ShowBuilderNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.f : 6.f;

		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
#endif
}

#if WITH_EDITOR
const FName UWidgetAnimBuilder::BuilderBlueprintName(TEXT("WidgetAnimationBuilder"));
#endif

UWidgetAnimBuilder::UWidgetAnimBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The mode that shows an entrance and its exit, which is what an animation being built needs judging on.
	Preview.Mode = EWidgetAnimPreviewMode::LoopInOut;
}

// =====================================================================================================
// Preview host
// =====================================================================================================

UWidget* UWidgetAnimBuilder::GetPreviewAnimationTarget()
{
	if (PreviewImage)
	{
		return PreviewImage;
	}

	return this;
}

void UWidgetAnimBuilder::RebuildPreviewAnimations()
{
	FlattenedIn.Reset();
	FlattenedOut.Reset();

	UWidgetAnimPresetAsset* AnimationToPlay = PreviewAnimation;

	// Building: Layers go through a scratch asset, so they take the same path a saved asset would.
	if (!AnimationToPlay)
	{
		if (!PreviewAsset)
		{
			PreviewAsset = NewObject<UWidgetAnimPresetAsset>(this, NAME_None, RF_Transient);
		}

		PreviewAsset->Description = Description;
		PreviewAsset->Layers = Layers;
		AnimationToPlay = PreviewAsset;
	}

	// The preview is one use of the animation, stamped exactly the way an entry in a real widget would be.
	FWidgetAnimEntryBase PreviewUse;
	PreviewUse.Animation = AnimationToPlay;
	PreviewUse.bLoop = bPreviewLoop;
	PreviewUse.bPingPong = bPreviewPingPong;
	PreviewUse.LoopCount = PreviewLoopCount;
	PreviewUse.bInvertAnimation = bPreviewInvertAnimation;
	PreviewUse.PlaybackSpeed = PreviewPlaybackSpeed;

	PreviewUse.BuildSettings(FlattenedIn, PreviewImage);
	FlattenedOut = FlattenedIn;
}

void UWidgetAnimBuilder::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Returns immediately unless this is a live widget in the designer.
	UWidgetAnimDesignerPreview::Synchronize(this);
}

void UWidgetAnimBuilder::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	// SynchronizeProperties can run before the Slate widget exists; this is the pass where it does.
	// bPreviewPaused defaults true, so a freshly opened builder is not ticked into motion on its own.
	if (!bPreviewPaused && UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		EnsurePreviewTimer();
	}
}

void UWidgetAnimBuilder::ReleaseSlateResources(bool bReleaseChildren)
{
	UWidgetAnimDesignerPreview::Shutdown(this);
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UWidgetAnimBuilder::BeginDestroy()
{
	UWidgetAnimDesignerPreview::Shutdown(this);
	Super::BeginDestroy();
}

void UWidgetAnimBuilder::RefreshPreview(bool bRestart, bool bAnimate)
{
	if (!IsDesignTime())
	{
		return;
	}

	if (bRestart)
	{
		UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);
	}

	// Synchronize puts the image back as authored before re-reading it, so Scrub has to be told its last
	// pose is gone or it would skip re-posing a slider that hasn't moved.
	PreviewState.LastScrubAlpha = -1.f;

	UWidgetAnimDesignerPreview::Synchronize(this);

	// Synchronize alone already leaves the widget at its authored, resting look. Only pose it into the
	// animation on top when asked to - Reset asks with bAnimate false, so it stops right here.
	if (!bAnimate)
	{
		return;
	}

	// Posed now: a held mode has already stopped its timer, so there may be no next tick to wait for.
	if (UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		UWidgetAnimDesignerPreview::TickFrame(this, 0.f);
	}
}

void UWidgetAnimBuilder::SetPreviewPaused(bool bPaused)
{
	if (bPreviewPaused == bPaused)
	{
		return;
	}

	bPreviewPaused = bPaused;

	if (!IsDesignTime())
	{
		return;
	}

	// Resuming has to start the timer again. A paused TickFrame reports that it has nothing more to draw,
	// and an active timer that says that is stopped by Slate there and then - so by the time Play is
	// pressed there is no clock left to carry on with. EnsureTimer no-ops if one is somehow still running.
	if (!bPreviewPaused && UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		EnsurePreviewTimer();
	}

	// Posed once either way, so the freeze and the release are both visible on the frame they happen
	// rather than on the next tick - which, when pausing, never comes.
	if (UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		UWidgetAnimDesignerPreview::TickFrame(this, 0.f);
	}
}

float UWidgetAnimBuilder::GetPreviewElapsedSeconds() const
{
	const bool bShowingOut = PreviewState.Playback.Phase == EWidgetAnimPhase::PlayingOut
		|| PreviewState.Playback.Phase == EWidgetAnimPhase::Hidden;

	return bShowingOut
		? UWidgetAnimLibrary::GetStackElapsed(FlattenedOut, PreviewState.Playback.OutStates)
		: UWidgetAnimLibrary::GetStackElapsed(FlattenedIn, PreviewState.Playback.InStates);
}

float UWidgetAnimBuilder::GetPreviewTotalSeconds() const
{
	const bool bShowingOut = PreviewState.Playback.Phase == EWidgetAnimPhase::PlayingOut
		|| PreviewState.Playback.Phase == EWidgetAnimPhase::Hidden;

	return bShowingOut
		? UWidgetAnimLibrary::GetStackDuration(FlattenedOut)
		: UWidgetAnimLibrary::GetStackDuration(FlattenedIn);
}

bool UWidgetAnimBuilder::GetPreviewLoopInfo(bool& OutPingPong, float& OutLoopSeconds) const
{
	const bool bShowingOut = PreviewState.Playback.Phase == EWidgetAnimPhase::PlayingOut
		|| PreviewState.Playback.Phase == EWidgetAnimPhase::Hidden;

	return bShowingOut
		? UWidgetAnimLibrary::GetStackDrivingLoopInfo(FlattenedOut, OutPingPong, OutLoopSeconds)
		: UWidgetAnimLibrary::GetStackDrivingLoopInfo(FlattenedIn, OutPingPong, OutLoopSeconds);
}

void UWidgetAnimBuilder::ResetPreview()
{
#if WITH_EDITOR
	RefreshLivePreviews(/*bRestart=*/true, /*bCopyAuthoredState=*/false, /*bAnimate=*/false);
#else
	RefreshPreview(/*bRestart=*/true, /*bAnimate=*/false);
#endif
}

#if WITH_EDITOR

void UWidgetAnimBuilder::CopyAuthoredStateFrom(const UWidgetAnimBuilder& Source)
{
	AnimationName = Source.AnimationName;
	SaveFolder = Source.SaveFolder;
	PreviewAnimation = Source.PreviewAnimation;
	Description = Source.Description;
	Layers = Source.Layers;

	Preview = Source.Preview;
	bPreviewLoop = Source.bPreviewLoop;
	bPreviewPingPong = Source.bPreviewPingPong;
	PreviewLoopCount = Source.PreviewLoopCount;
	bPreviewInvertAnimation = Source.bPreviewInvertAnimation;
	PreviewPlaybackSpeed = Source.PreviewPlaybackSpeed;
}

void UWidgetAnimBuilder::RefreshLivePreviews(bool bRestart, bool bCopyAuthoredState, bool bAnimate)
{
	// The Details panel edits a template - the Class Defaults, or the copy a parent Widget Blueprint keeps -
	// while the viewport paints a separate preview instance. Refreshing only this object would change
	// nothing visible; the live instances are found and updated directly.
	RefreshPreview(bRestart, bAnimate);

	const UWidgetAnimBuilder* Source = this;
	WidgetAnimDesignerSync::ForEachPreviewOf(this, [Source, bRestart, bCopyAuthoredState, bAnimate](UObject* PreviewObject)
	{
		if (UWidgetAnimBuilder* LiveBuilder = Cast<UWidgetAnimBuilder>(PreviewObject))
		{
			if (bCopyAuthoredState)
			{
				LiveBuilder->CopyAuthoredStateFrom(*Source);
			}
			LiveBuilder->RefreshPreview(bRestart, bAnimate);
		}
	});
}

void UWidgetAnimBuilder::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();

	// Mode, holds, speed, scrub: the shared harness pushes those mid-drag without disturbing what's playing.
	if (MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Preview))
	{
		UWidgetAnimDesignerPreview::NotifyPropertyChanged(this, PropertyChangedEvent);
		return;
	}

	const bool bAffectsPreview =
		MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Layers)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewAnimation)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewLoop)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewPingPong)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewLoopCount)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewInvertAnimation)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewPlaybackSpeed);

	if (!bAffectsPreview)
	{
		return;
	}

	// Pushed on every change rather than left to the designer's own rebuild, which only happens on commit
	// and not reliably for Class Defaults. A value still being dragged updates what is playing; a finished
	// edit - a layer added, a type changed, a drag released - starts the animation over so it can be judged
	// from the top.
	const bool bRestart = PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive;
	RefreshLivePreviews(bRestart, /*bCopyAuthoredState=*/true);
}

const FText UWidgetAnimBuilder::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Animation");
}

// =====================================================================================================
// Previewing an existing asset
// =====================================================================================================

UBlueprint* UWidgetAnimBuilder::FindBuilderBlueprint()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(GetWidgetBlueprintClassPath());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> WidgetBlueprints;
	AssetRegistry.GetAssets(Filter, WidgetBlueprints);

	for (const FAssetData& AssetData : WidgetBlueprints)
	{
		if (AssetData.AssetName != BuilderBlueprintName)
		{
			continue;
		}

		// Exact name is not enough on its own: the one that's wanted is the one that can actually preview.
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UWidgetAnimBuilder::StaticClass()))
		{
			return Blueprint;
		}
	}

	return nullptr;
}

void UWidgetAnimBuilder::OpenPreviewFor(UWidgetAnimPresetAsset* Animation)
{
	if (!Animation || !GEditor)
	{
		return;
	}

	UBlueprint* BuilderBlueprint = FindBuilderBlueprint();
	if (!BuilderBlueprint)
	{
		const IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		const FText Message = AssetRegistry.IsLoadingAssets()
			? LOCTEXT("StillScanning", "The editor is still discovering assets. Try View Animation Preview again in a moment.")
			: FText::Format(
				LOCTEXT("NoBuilder", "No Widget Blueprint named {0} deriving from Widget Anim Builder was found."),
				FText::FromName(BuilderBlueprintName));

		ShowBuilderNotification(Message, /*bSuccess=*/false);
		return;
	}

	// Set on the Class Defaults, which is what the designer builds its preview from and what the Details
	// panel shows. Modify() so it is undoable and the Widget Blueprint knows it has changed.
	UWidgetAnimBuilder* Defaults = Cast<UWidgetAnimBuilder>(BuilderBlueprint->GeneratedClass->GetDefaultObject());
	if (!Defaults)
	{
		return;
	}

	Defaults->Modify();
	Defaults->PreviewAnimation = Animation;

	// Opening an editor that is already open just brings it to the front.
	if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		AssetEditors->OpenEditorForAsset(BuilderBlueprint);
	}

	// A freshly opened designer reads the new value from the defaults on its own. One that was already open
	// has a preview built before the change, so that is updated directly - only the field this sets, so
	// anything else edited on a placed builder is left alone.
	WidgetAnimDesignerSync::ForEachPreviewOf(Defaults, [Animation](UObject* PreviewObject)
	{
		if (UWidgetAnimBuilder* LiveBuilder = Cast<UWidgetAnimBuilder>(PreviewObject))
		{
			LiveBuilder->PreviewAnimation = Animation;
			LiveBuilder->RefreshPreview(/*bRestart=*/true);
		}
	});
}

void UWidgetAnimBuilder::ForEachLivePreview(const UClass* BuilderClass, TFunctionRef<void(UWidgetAnimBuilder&)> Func)
{
	if (!BuilderClass)
	{
		return;
	}

	for (TObjectIterator<UWidgetAnimBuilder> It; It; ++It)
	{
		UWidgetAnimBuilder* Builder = *It;
		if (!IsValid(Builder) || Builder->GetClass() != BuilderClass)
		{
			continue;
		}

		// On screen, not merely in memory: a cached Slate widget is what separates the designer's preview
		// instance from the templates and the leftovers waiting to be collected.
		if (!Builder->IsDesignTime() || !Builder->GetCachedWidget().IsValid())
		{
			continue;
		}

		Func(*Builder);
	}
}

bool UWidgetAnimBuilder::IsLivePreviewPaused(const UClass* BuilderClass)
{
	bool bPaused = false;
	ForEachLivePreview(BuilderClass, [&bPaused](UWidgetAnimBuilder& Builder)
	{
		bPaused = bPaused || Builder.IsPreviewPaused();
	});

	return bPaused;
}

bool UWidgetAnimBuilder::HasLivePreview(const UClass* BuilderClass)
{
	bool bFound = false;
	ForEachLivePreview(BuilderClass, [&bFound](UWidgetAnimBuilder&)
	{
		bFound = true;
	});

	return bFound;
}

void UWidgetAnimBuilder::NotifyAnimationAssetEdited(const UWidgetAnimPresetAsset* Animation, bool bRestart)
{
	if (!Animation)
	{
		return;
	}

	// Class defaults are skipped by the iterator, and RefreshPreview ignores anything not on screen.
	for (TObjectIterator<UWidgetAnimBuilder> It; It; ++It)
	{
		UWidgetAnimBuilder* Builder = *It;
		if (IsValid(Builder) && Builder->PreviewAnimation == Animation)
		{
			Builder->RefreshPreview(bRestart);
		}
	}
}

// =====================================================================================================
// Creating the asset
// =====================================================================================================

FString UWidgetAnimBuilder::GetSavePackagePath() const
{
	FString Folder = SaveFolder.Path;
	Folder.TrimStartAndEndInline();
	Folder.ReplaceInline(TEXT("\\"), TEXT("/"));

	if (!Folder.IsEmpty())
	{
		// The picker stores /Game/... paths; anything typed by hand relative to Content is taken as such.
		if (!Folder.StartsWith(TEXT("/")))
		{
			Folder = FString(TEXT("/Game/")) + Folder;
		}

		while (Folder.Len() > 1 && Folder.EndsWith(TEXT("/")))
		{
			Folder.LeftChopInline(1);
		}

		return Folder;
	}

	// Beside the Widget Blueprint being edited. The designer's own preview instances live in a transient
	// package, which is not somewhere an asset can go.
	if (const UPackage* Package = GetPackage())
	{
		const FString PackageName = Package->GetName();
		const bool bIsRealContent = Package != GetTransientPackage()
			&& !PackageName.StartsWith(TEXT("/Temp"))
			&& !PackageName.StartsWith(TEXT("/Engine"))
			&& FPackageName::IsValidLongPackageName(PackageName);

		if (bIsRealContent)
		{
			return FPackageName::GetLongPackagePath(PackageName);
		}
	}

	return FallbackSavePath;
}

FString UWidgetAnimBuilder::GetPrefixedAssetName() const
{
	FString Name = ObjectTools::SanitizeObjectName(AnimationName.TrimStartAndEnd());

	// Prefix added once, whether or not it was typed.
	if (Name.StartsWith(WidgetAnimAssetPrefix, ESearchCase::IgnoreCase))
	{
		Name.RightChopInline(FCString::Strlen(WidgetAnimAssetPrefix));
	}

	while (Name.StartsWith(TEXT("_")))
	{
		Name.RightChopInline(1);
	}

	if (Name.IsEmpty())
	{
		Name = DefaultAnimationName;
	}

	return FString(WidgetAnimAssetPrefix) + Name;
}

#endif // WITH_EDITOR

void UWidgetAnimBuilder::CreateWidgetAnimation()
{
#if WITH_EDITOR
	if (PreviewAnimation)
	{
		ShowBuilderNotification(
			LOCTEXT("PreviewingAsset", "Clear Preview Animation first. Create Widget Animation saves Layers, not the animation being previewed."),
			/*bSuccess=*/false);
		return;
	}

	if (Layers.Num() == 0)
	{
		ShowBuilderNotification(LOCTEXT("NoLayers", "Add at least one layer before creating a Widget Animation."), /*bSuccess=*/false);
		return;
	}

	const FString PackagePath = GetSavePackagePath();
	const FString BaseAssetName = GetPrefixedAssetName();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	// A clash gets a number on the end (WA_Pop1), so the prefix always survives.
	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / BaseAssetName, FString(), UniquePackageName, UniqueAssetName);

	// Created directly rather than through the save dialog, which would let the name lose its WA_ prefix.
	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UWidgetAnimPresetAsset::StaticClass();

	UWidgetAnimPresetAsset* Asset = Cast<UWidgetAnimPresetAsset>(
		AssetTools.CreateAsset(UniqueAssetName, PackagePath, UWidgetAnimPresetAsset::StaticClass(), Factory));

	if (!Asset)
	{
		ShowBuilderNotification(
			FText::Format(LOCTEXT("CreateFailed", "Could not create {0} in {1}."), FText::FromString(UniqueAssetName), FText::FromString(PackagePath)),
			/*bSuccess=*/false);
		return;
	}

	// Motion only. Loop, ping-pong, invert and speed are per-use decisions and belong on the entry that uses it.
	Asset->Modify();
	Asset->Description = Description;
	Asset->Layers = Layers;
	Asset->PostEditChange();
	Asset->MarkPackageDirty();

	// Saved straight away, so closing the editor can't quietly lose it.
	UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/false);

	if (GEditor)
	{
		TArray<UObject*> ToSelect{ Asset };
		GEditor->SyncBrowserToObjects(ToSelect);
	}

	ShowBuilderNotification(
		FText::Format(LOCTEXT("Created", "Created {0}"), FText::FromString(UniquePackageName)),
		/*bSuccess=*/true);
#endif
}

#undef LOCTEXT_NAMESPACE