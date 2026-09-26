// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"

#include "AssetToolsModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Factories/DataAssetFactory.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectIterator.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "WidgetAnimPanelSource"

// =====================================================================================================
// IWidgetAnimPanelSource
// =====================================================================================================

TArray<FString> IWidgetAnimPanelSource::GetTargetOptions() const
{
	TArray<FString> Options;

	if (UUserWidget* Root = GetPreviewRoot())
	{
		UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(Root, Options);
	}

	// First, so there is a way back to "the widget itself" once something has been picked.
	Options.Insert(TEXT("None"), 0);
	return Options;
}

UWidget* IWidgetAnimPanelSource::ResolvePreviewTarget(FName Path) const
{
	UUserWidget* Root = GetPreviewRoot();
	return Root && !Path.IsNone() ? UWidgetAnimLibrary::FindWidgetByPathInTree(Root, Path) : nullptr;
}

void IWidgetAnimPanelSource::UpdateAsset()
{
	if (UWidgetAnimPresetAsset* Asset = GetAsset())
	{
		UEditorLoadingAndSavingUtils::SavePackages({ Asset->GetPackage() }, /*bOnlyDirty=*/true);
	}
}

float IWidgetAnimPanelSource::GetAnimationSpeedMultiplier() const
{
	const UObject* Edited = GetEditedObject();
	if (const UWidgetAnimPresetAsset* Asset = Cast<UWidgetAnimPresetAsset>(Edited))
	{
		return FMath::Max(Asset->AnimationSpeedMultiplier, 0.01f);
	}
	if (const UWidgetAnimBuilder* Builder = Cast<UWidgetAnimBuilder>(Edited))
	{
		return FMath::Max(Builder->AnimationSpeedMultiplier, 0.01f);
	}
	return 1.f;
}

bool IWidgetAnimPanelSource::CanSaveAsNewAsset() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return GetMode() == EMode::NewAnimation && Layers && Layers->Num() > 0;
}

bool IWidgetAnimPanelSource::CanUpdateAsset() const
{
	const UWidgetAnimPresetAsset* Asset = GetAsset();
	return Asset && Asset->GetPackage()->IsDirty();
}

namespace
{
	/**
	 * Tells the edited object its LayerGroups changed, exactly as a Details panel edit to them would: keys
	 * brought in step with their curves first, then the object's own PostEditChangeProperty, which is what
	 * refreshes a builder's previews or any builder previewing the asset.
	 */
	void NotifyLayerGroupsChanged(UObject* Object, EPropertyChangeType::Type ChangeType)
	{
		if (!Object)
		{
			return;
		}

		UWidgetAnimTransformLibrary::SyncAllTransformAnims(Object);

		// As an edit to Layers: that is what both a builder and an asset refresh their previews for, and a
		// group edit changes what the layers play on just the same.
		FProperty* LayersProperty = FindFProperty<FProperty>(Object->GetClass(), IWidgetAnimPanelSource::GetLayersPropertyName());
		FPropertyChangedEvent Event(LayersProperty, ChangeType);
		Object->PostEditChangeProperty(Event);
	}
}

UUserWidget* FindWidgetAnimDesignerPreview(const UWidgetBlueprint* Blueprint)
{
	const UClass* GeneratedClass = Blueprint ? Blueprint->GeneratedClass.Get() : nullptr;
	if (!GeneratedClass)
	{
		return nullptr;
	}

	// A copy of this widget placed in another Widget Blueprint sits inside that one's tree; the designer's
	// own preview of it does not.
	for (TObjectIterator<UUserWidget> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
	{
		UUserWidget* Candidate = *It;
		if (Candidate->GetClass() == GeneratedClass && Candidate->IsDesignTime() && !Candidate->GetTypedOuter<UWidgetTree>())
		{
			return Candidate;
		}
	}

	return nullptr;
}

// =====================================================================================================
// FWidgetAnimBuilderPanelSource
// =====================================================================================================

FWidgetAnimBuilderPanelSource::FWidgetAnimBuilderPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint)
	: Blueprint(InBlueprint)
{
}

UClass* FWidgetAnimBuilderPanelSource::GetBuilderClass() const
{
	const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get();
	return WidgetBlueprint ? WidgetBlueprint->GeneratedClass.Get() : nullptr;
}

UWidgetAnimBuilder* FWidgetAnimBuilderPanelSource::GetBuilder() const
{
	// The Class Defaults, read fresh every time: compiling replaces the object.
	UClass* BuilderClass = GetBuilderClass();
	return BuilderClass && BuilderClass->IsChildOf(UWidgetAnimBuilder::StaticClass())
		? Cast<UWidgetAnimBuilder>(BuilderClass->GetDefaultObject())
		: nullptr;
}

FWidgetAnimBuilderPanelSource::EMode FWidgetAnimBuilderPanelSource::GetMode() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return (Builder && Builder->PreviewAnimation) || bEditExistingPicked ? EMode::EditExistingAsset : EMode::NewAnimation;
}

void FWidgetAnimBuilderPanelSource::SetMode(EMode Mode)
{
	bEditExistingPicked = Mode == EMode::EditExistingAsset;

	// Back to the builder's own layers: the asset being edited is let go of. Going the other way waits for one
	// to be picked.
	if (Mode == EMode::NewAnimation)
	{
		SetPreviewAnimation(nullptr);
	}
}

UWidgetAnimPresetAsset* FWidgetAnimBuilderPanelSource::GetAsset() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder ? Builder->PreviewAnimation.Get() : nullptr;
}

void FWidgetAnimBuilderPanelSource::SetAsset(UWidgetAnimPresetAsset* Asset)
{
	bEditExistingPicked = true;
	SetPreviewAnimation(Asset);
}

void FWidgetAnimBuilderPanelSource::SetPreviewAnimation(UWidgetAnimPresetAsset* Asset)
{
	UWidgetAnimBuilder* Builder = GetBuilder();
	if (!Builder || Builder->PreviewAnimation == Asset)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetEditedAsset", "Edit Widget Animation Asset"));
	Builder->Modify();
	Builder->PreviewAnimation = Asset;

	// Told to the builder as a Details edit would be, so every live preview switches to what is now edited.
	FProperty* Property = FindFProperty<FProperty>(UWidgetAnimBuilder::StaticClass(), GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewAnimation));
	FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
	Builder->PostEditChangeProperty(Event);
}

UWidgetAnimPresetAsset* FWidgetAnimBuilderPanelSource::SaveAsNewAsset()
{
	UWidgetAnimBuilder* Builder = GetBuilder();
	if (!Builder || GetMode() != EMode::NewAnimation)
	{
		return nullptr;
	}

	// The builder's own Create - named by Animation Name, in Save Folder - then straight on to editing what
	// was saved, so further changes Update it. The working copy stays as it is for the next variant.
	UWidgetAnimPresetAsset* Created = Builder->CreateWidgetAnimation();
	if (Created)
	{
		SetAsset(Created);
	}
	return Created;
}

UObject* FWidgetAnimBuilderPanelSource::GetEditedObject() const
{
	// Edit Existing Asset: the asset (none until one is picked). New Animation: the builder's working copy.
	if (GetMode() == EMode::EditExistingAsset)
	{
		return GetAsset();
	}
	return GetBuilder();
}

TArray<FWidgetAnimLayerGroup>* FWidgetAnimBuilderPanelSource::GetGroups() const
{
	if (GetMode() == EMode::EditExistingAsset)
	{
		UWidgetAnimPresetAsset* Asset = GetAsset();
		return Asset ? &Asset->LayerGroups : nullptr;
	}

	UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder ? &Builder->LayerGroups : nullptr;
}

TArray<FWidgetAnimLayer>* FWidgetAnimBuilderPanelSource::GetLayers() const
{
	if (GetMode() == EMode::EditExistingAsset)
	{
		UWidgetAnimPresetAsset* Asset = GetAsset();
		return Asset ? &Asset->Layers : nullptr;
	}

	UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder ? &Builder->Layers : nullptr;
}

bool FWidgetAnimBuilderPanelSource::CanEdit() const
{
	return GetEditedObject() != nullptr;
}

void FWidgetAnimBuilderPanelSource::NotifyGroupsChanged(EPropertyChangeType::Type ChangeType)
{
	// An asset's own PostEditChangeProperty tells every builder previewing it, so the preview follows.
	NotifyLayerGroupsChanged(GetEditedObject(), ChangeType);
}

UUserWidget* FWidgetAnimBuilderPanelSource::GetPreviewRoot() const
{
	UUserWidget* Found = nullptr;
	UWidgetAnimBuilder::ForEachLivePreview(GetBuilderClass(), [&Found](UWidgetAnimBuilder& Live)
	{
		if (!Found)
		{
			Found = &Live;
		}
	});
	return Found;
}

float FWidgetAnimBuilderPanelSource::GetPlayheadAlpha() const
{
	float Alpha = 0.f;
	bool bFound = false;
	UWidgetAnimBuilder::ForEachLivePreview(GetBuilderClass(), [&Alpha, &bFound](UWidgetAnimBuilder& Live)
	{
		if (!bFound)
		{
			Alpha = Live.GetPreviewScrubAlpha();
			bFound = true;
		}
	});
	return Alpha;
}

void FWidgetAnimBuilderPanelSource::ScrubToAlpha(float Alpha)
{
	UWidgetAnimBuilder::ForEachLivePreview(GetBuilderClass(), [Alpha](UWidgetAnimBuilder& Live)
	{
		Live.SetPreviewScrubAlpha(Alpha);
	});
}

void FWidgetAnimBuilderPanelSource::FocusKey(int32 FlatLayerIndex, int32 KeyIndex)
{
	UWidgetAnimBuilder::ForEachLivePreview(GetBuilderClass(), [FlatLayerIndex, KeyIndex](UWidgetAnimBuilder& Live)
	{
		Live.ScrubToTransformKey(FlatLayerIndex, KeyIndex);
	});
}

bool FWidgetAnimBuilderPanelSource::IsPlaying() const
{
	const UClass* BuilderClass = GetBuilderClass();
	return BuilderClass && UWidgetAnimBuilder::HasLivePreview(BuilderClass) && !UWidgetAnimBuilder::IsLivePreviewPaused(BuilderClass);
}

void FWidgetAnimBuilderPanelSource::TogglePlay()
{
	const UClass* BuilderClass = GetBuilderClass();
	if (!BuilderClass)
	{
		return;
	}

	// One decision for every preview, so two designer tabs can't end up one playing and one paused.
	const bool bPlay = UWidgetAnimBuilder::IsLivePreviewPaused(BuilderClass);
	UWidgetAnimBuilder::ForEachLivePreview(BuilderClass, [bPlay](UWidgetAnimBuilder& Live)
	{
		if (bPlay)
		{
			Live.PlayAnimation();
		}
		else
		{
			Live.SetPreviewPaused(true);
		}
	});
}

void FWidgetAnimBuilderPanelSource::ApplyToBuilders(TFunctionRef<void(UWidgetAnimBuilder&)> Apply)
{
	const UClass* BuilderClass = GetBuilderClass();
	if (BuilderClass && UWidgetAnimBuilder::HasLivePreview(BuilderClass))
	{
		UWidgetAnimBuilder::ForEachLivePreview(BuilderClass, Apply);
	}
	else if (UWidgetAnimBuilder* Builder = GetBuilder())
	{
		Builder->Modify();
		Apply(*Builder);
	}
}

bool FWidgetAnimBuilderPanelSource::GetLoop() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder && Builder->GetPreviewLoop();
}

void FWidgetAnimBuilderPanelSource::SetLoop(bool bValue)
{
	ApplyToBuilders([bValue](UWidgetAnimBuilder& Target) { Target.SetPreviewLoop(bValue); });
}

bool FWidgetAnimBuilderPanelSource::GetPingPong() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder && Builder->GetPreviewPingPong();
}

void FWidgetAnimBuilderPanelSource::SetPingPong(bool bValue)
{
	ApplyToBuilders([bValue](UWidgetAnimBuilder& Target) { Target.SetPreviewPingPong(bValue); });
}

bool FWidgetAnimBuilderPanelSource::GetInvert() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder && Builder->GetPreviewInvertAnimation();
}

void FWidgetAnimBuilderPanelSource::SetInvert(bool bValue)
{
	ApplyToBuilders([bValue](UWidgetAnimBuilder& Target) { Target.SetPreviewInvertAnimation(bValue); });
}

float FWidgetAnimBuilderPanelSource::GetSpeed() const
{
	const UWidgetAnimBuilder* Builder = GetBuilder();
	return Builder ? Builder->GetPreviewPlaybackSpeed() : 1.f;
}

void FWidgetAnimBuilderPanelSource::SetSpeed(float Speed)
{
	ApplyToBuilders([Speed](UWidgetAnimBuilder& Target) { Target.SetPreviewPlaybackSpeed(Speed); });
}

// =====================================================================================================
// FWidgetAnimAssetPanelSource
// =====================================================================================================

namespace
{
	/** The asset each Widget Blueprint was last editing, so reopening the drawer - or docking it - picks up where it was. For this session only. */
	TMap<FSoftObjectPath, TWeakObjectPtr<UWidgetAnimPresetAsset>>& GetLastEditedAssets()
	{
		static TMap<FSoftObjectPath, TWeakObjectPtr<UWidgetAnimPresetAsset>> LastEdited;
		return LastEdited;
	}

	/** Each Widget Blueprint's New Animation working copy, for this session. */
	TMap<FSoftObjectPath, TStrongObjectPtr<UWidgetAnimPresetAsset>>& GetWorkingCopies()
	{
		static TMap<FSoftObjectPath, TStrongObjectPtr<UWidgetAnimPresetAsset>> WorkingCopies;
		return WorkingCopies;
	}

	/** After this long without the panel on screen, the preview is put back as authored. */
	constexpr double PanelIdleSeconds = 0.25;

	/** How often a missing designer preview is looked for again. */
	constexpr double RootSearchInterval = 0.5;
}

FWidgetAnimAssetPanelSource::FWidgetAnimAssetPanelSource(TWeakObjectPtr<UWidgetBlueprint> InBlueprint)
	: Blueprint(InBlueprint)
{
	if (const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get())
	{
		if (const TWeakObjectPtr<UWidgetAnimPresetAsset>* Last = GetLastEditedAssets().Find(FSoftObjectPath(WidgetBlueprint)))
		{
			Asset = *Last;
		}
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FWidgetAnimAssetPanelSource::Tick));
}

FWidgetAnimAssetPanelSource::~FWidgetAnimAssetPanelSource()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	RestorePreview();
}

UWidgetAnimPresetAsset* FWidgetAnimAssetPanelSource::GetWorkingCopy() const
{
	const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get();
	if (!WidgetBlueprint)
	{
		return nullptr;
	}

	// One per Widget Blueprint, kept for the session, so closing and reopening the drawer - or docking it -
	// finds the new animation as it was left. Never saved as such: Save as Animation Asset is what keeps it.
	TStrongObjectPtr<UWidgetAnimPresetAsset>& WorkingCopy = GetWorkingCopies().FindOrAdd(FSoftObjectPath(WidgetBlueprint));
	if (!WorkingCopy.IsValid())
	{
		WorkingCopy.Reset(NewObject<UWidgetAnimPresetAsset>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional));
		WorkingCopy->LayerGroups.AddDefaulted();
	}
	return WorkingCopy.Get();
}

UWidgetAnimPresetAsset* FWidgetAnimAssetPanelSource::GetEditedAnimation() const
{
	return Mode == EMode::NewAnimation ? GetWorkingCopy() : Asset.Get();
}

UObject* FWidgetAnimAssetPanelSource::GetEditedObject() const
{
	return GetEditedAnimation();
}

TArray<FWidgetAnimLayerGroup>* FWidgetAnimAssetPanelSource::GetGroups() const
{
	UWidgetAnimPresetAsset* Edited = GetEditedAnimation();
	return Edited ? &Edited->LayerGroups : nullptr;
}

TArray<FWidgetAnimLayer>* FWidgetAnimAssetPanelSource::GetLayers() const
{
	UWidgetAnimPresetAsset* Edited = GetEditedAnimation();
	return Edited ? &Edited->Layers : nullptr;
}

bool FWidgetAnimAssetPanelSource::CanEdit() const
{
	return GetEditedAnimation() != nullptr;
}

void FWidgetAnimAssetPanelSource::NotifyGroupsChanged(EPropertyChangeType::Type ChangeType)
{
	NotifyLayerGroupsChanged(GetEditedAnimation(), ChangeType);
}

void FWidgetAnimAssetPanelSource::SetMode(EMode InMode)
{
	if (Mode == InMode)
	{
		return;
	}

	// What was posed comes off before the other animation's poses go on.
	RestorePreview();
	Mode = InMode;
	Alpha = 0.f;
	bPlaying = false;
	bPlayingForward = true;
}

UUserWidget* FWidgetAnimAssetPanelSource::GetPreviewRoot() const
{
	if (UUserWidget* Cached = CachedRoot.Get())
	{
		if (IsValid(Cached))
		{
			return Cached;
		}
	}

	const double Now = FPlatformTime::Seconds();
	if (LastRootSearchTime >= 0.0 && Now - LastRootSearchTime < RootSearchInterval)
	{
		return nullptr;
	}
	LastRootSearchTime = Now;

	CachedRoot = FindWidgetAnimDesignerPreview(Blueprint.Get());
	return CachedRoot.Get();
}

void FWidgetAnimAssetPanelSource::SetAsset(UWidgetAnimPresetAsset* InAsset)
{
	if (Asset.Get() == InAsset && Mode == EMode::EditExistingAsset)
	{
		return;
	}

	// The last one's poses come off before the next one's go on.
	RestorePreview();

	Asset = InAsset;
	Mode = EMode::EditExistingAsset;
	Alpha = 0.f;
	bPlaying = false;
	bPlayingForward = true;

	if (const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get())
	{
		GetLastEditedAssets().Add(FSoftObjectPath(WidgetBlueprint), Asset);
	}
}

UWidgetAnimPresetAsset* FWidgetAnimAssetPanelSource::SaveAsNewAsset()
{
	const UWidgetBlueprint* WidgetBlueprint = Blueprint.Get();
	UWidgetAnimPresetAsset* WorkingCopy = GetWorkingCopy();
	if (!WidgetBlueprint || !WorkingCopy || Mode != EMode::NewAnimation)
	{
		return nullptr;
	}

	// Beside the Widget Blueprint, named after it: WBP_Card -> WA_Card, numbered on a clash so the prefix survives.
	const FString PackagePath = FPackageName::GetLongPackagePath(WidgetBlueprint->GetOutermost()->GetName());
	FString BaseName = WidgetBlueprint->GetName();
	BaseName.RemoveFromStart(TEXT("WBP_"));
	BaseName.RemoveFromStart(TEXT("W_"));
	BaseName = TEXT("WA_") + BaseName;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / BaseName, FString(), UniquePackageName, UniqueAssetName);

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UWidgetAnimPresetAsset::StaticClass();

	UWidgetAnimPresetAsset* NewAsset = Cast<UWidgetAnimPresetAsset>(
		AssetTools.CreateAsset(UniqueAssetName, PackagePath, UWidgetAnimPresetAsset::StaticClass(), Factory));

	if (!NewAsset)
	{
		return nullptr;
	}

	NewAsset->Modify();
	NewAsset->Description = WorkingCopy->Description;
	NewAsset->LayerGroups = WorkingCopy->LayerGroups;
	NewAsset->Layers = WorkingCopy->Layers;
	NewAsset->AnimationSpeedMultiplier = WorkingCopy->AnimationSpeedMultiplier;
	NewAsset->PostEditChange();
	NewAsset->MarkPackageDirty();

	// Saved straight away, as the builder's Create does, so closing the editor can't quietly lose it.
	UEditorLoadingAndSavingUtils::SavePackages({ NewAsset->GetPackage() }, /*bOnlyDirty=*/false);

	if (GEditor)
	{
		TArray<UObject*> ToSelect{ NewAsset };
		GEditor->SyncBrowserToObjects(ToSelect);
	}

	// The working copy starts over for the next new animation, and the panel moves on to the saved one, so
	// further changes are made to it and Update Animation Asset keeps them.
	WorkingCopy->Description = FText::GetEmpty();
	WorkingCopy->Layers.Reset();
	WorkingCopy->AnimationSpeedMultiplier = 1.f;
	WorkingCopy->LayerGroups.Reset();
	WorkingCopy->LayerGroups.AddDefaulted();

	SetAsset(NewAsset);
	return NewAsset;
}

float FWidgetAnimAssetPanelSource::GetPlayheadAlpha() const
{
	return Alpha;
}

void FWidgetAnimAssetPanelSource::ScrubToAlpha(float InAlpha)
{
	Alpha = FMath::Clamp(InAlpha, 0.f, 1.f);
	bPlaying = false;
}

void FWidgetAnimAssetPanelSource::FocusKey(int32 FlatLayerIndex, int32 KeyIndex)
{
	UUserWidget* Root = GetPreviewRoot();
	TArray<FWidgetAnimSettings> Settings;
	BuildFlattened(Root, Settings);

	const float PassSeconds = UWidgetAnimLibrary::GetStackPassDuration(Settings);
	if (!Settings.IsValidIndex(FlatLayerIndex) || PassSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float KeySeconds = UWidgetAnimLibrary::GetTransformKeyStackTime(Settings[FlatLayerIndex], KeyIndex);
	if (KeySeconds < 0.f)
	{
		return;
	}

	// An inverted layer plays its keys from the other end, so its key is posed at the mirrored moment.
	float KeyAlpha = FMath::Clamp(KeySeconds / PassSeconds, 0.f, 1.f);
	if (bInvert)
	{
		KeyAlpha = 1.f - KeyAlpha;
	}

	ScrubToAlpha(KeyAlpha);
}

void FWidgetAnimAssetPanelSource::TogglePlay()
{
	if (bPlaying)
	{
		bPlaying = false;
		return;
	}

	// From the top when frozen on the last frame, as a timeline does; from where it is otherwise.
	if (Alpha >= 1.f - KINDA_SMALL_NUMBER)
	{
		Alpha = 0.f;
	}

	bPlayingForward = true;
	bPlaying = true;
}

void FWidgetAnimAssetPanelSource::NotifyPanelActive()
{
	LastPanelActiveTime = FPlatformTime::Seconds();
}

void FWidgetAnimAssetPanelSource::BuildFlattened(UUserWidget* Root, TArray<FWidgetAnimSettings>& OutSettings) const
{
	const UWidgetAnimPresetAsset* Edited = GetEditedAnimation();
	if (!Edited)
	{
		return;
	}

	// One use of the animation, as an entry on this widget would make it - with each group on its Preview
	// Target, which is exactly what a new entry's Target Widgets start from. Looping is the clock's job here,
	// so the settings are built for one pass and posed at a point along it.
	FWidgetAnimEntry PreviewUse;
	PreviewUse.Animation = const_cast<UWidgetAnimPresetAsset*>(Edited);
	PreviewUse.bInvertAnimation = bInvert;

	const int32 FirstSetting = OutSettings.Num();
	PreviewUse.BuildSettingsResolved(OutSettings, [Root](FName Path) -> UWidget*
	{
		return Root && !Path.IsNone() ? UWidgetAnimLibrary::FindWidgetByPathInTree(Root, Path) : nullptr;
	});

	// Tracks hidden in the panel keep their place in time but pose nothing.
	UWidgetAnimLibrary::MuteHiddenLayers(Edited->Layers, OutSettings, FirstSetting);
}

void FWidgetAnimAssetPanelSource::RestorePreview()
{
	// Only while the widgets it was captured on are still alive: the designer rebuilds its preview freely,
	// and a rebuilt preview is already back as authored.
	if (UUserWidget* Root = PosedRoot.Get())
	{
		if (IsValid(Root))
		{
			UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Baselines);
			if (const TSharedPtr<SWidget> SlateWidget = Root->GetCachedWidget())
			{
				SlateWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
			}
		}
	}

	Baselines = FWidgetAnimBaselineSet();
	PosedRoot.Reset();
	PosedSignature = 0;
}

bool FWidgetAnimAssetPanelSource::Tick(float DeltaTime)
{
	const bool bPanelActive = LastPanelActiveTime >= 0.0 && FPlatformTime::Seconds() - LastPanelActiveTime < PanelIdleSeconds;

	// Not on screen: nothing is being judged, so nothing stays posed.
	if (!bPanelActive || !GetEditedAnimation())
	{
		if (PosedRoot.IsValid() || Baselines.Baselines.Num() > 0)
		{
			RestorePreview();
		}
		bPlaying = false;
		return true;
	}

	UUserWidget* Root = GetPreviewRoot();
	if (!Root)
	{
		return true;
	}

	TArray<FWidgetAnimSettings> Settings;
	BuildFlattened(Root, Settings);

	// Which widgets are moved, and how: when that changes - a group pointed elsewhere, a layer type swapped,
	// a new preview instance - the old poses come off and the authored state is read again before posing.
	uint32 Signature = PointerHash(Root);
	for (const FWidgetAnimSettings& Layer : Settings)
	{
		Signature = HashCombine(Signature, PointerHash(Layer.TargetWidget.Get()));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Layer.AnimType)));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.bAnimateShape)));
	}

	if (PosedRoot.Get() != Root || Signature != PosedSignature)
	{
		RestorePreview();
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Root, Settings, TArray<FWidgetAnimSettings>(), Baselines);
		PosedRoot = Root;
		PosedSignature = Signature;
	}

	// The clock. One pass is the animation at this Speed; Loop wraps it, Ping Pong bounces it.
	const float PassSeconds = UWidgetAnimLibrary::GetStackPassDuration(Settings) / FMath::Max(Speed, 0.01f);
	if (bPlaying && PassSeconds > KINDA_SMALL_NUMBER)
	{
		const float Step = DeltaTime / PassSeconds;
		Alpha += bPlayingForward ? Step : -Step;

		if (Alpha >= 1.f || Alpha <= 0.f)
		{
			if (!bLoop)
			{
				Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
				bPlaying = false;
			}
			else if (bPingPong)
			{
				Alpha = FMath::Clamp(Alpha, 0.f, 1.f);
				bPlayingForward = !bPlayingForward;
			}
			else
			{
				Alpha = FMath::Frac(Alpha);
			}
		}
	}

	UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(Root, Settings, Alpha, /*bIsOutAnimation=*/false, Baselines);

	if (const TSharedPtr<SWidget> SlateWidget = Root->GetCachedWidget())
	{
		SlateWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE