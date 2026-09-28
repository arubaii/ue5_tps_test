// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/Widgets/ShapeBox.h"

#include "AssetToolsModule.h"
#include "Components/Image.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PropertyHandle.h"
#include "Templates/UnrealTemplate.h"
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
		FProperty* LayersProperty = FindFProperty<FProperty>(Object->GetClass(), TEXT("Layers"));
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
		if (PosedRoot.IsValid() || Baselines.Baselines.Num() > 0 || Baselines.AuthoritativeBaselines.Num() > 0)
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
		// Which registered type, too: swapping a Count Up for another type may write another channel of the target.
		Signature = HashCombine(Signature, PointerHash(Layer.TypeParams.GetScriptStruct()));
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

// =====================================================================================================
// FWidgetAnimLayerArrayPanelSource
// =====================================================================================================

namespace
{
	/**
	 * Which layer-array source is posing each widget. One at a time: two of them posing the same widget - the In and
	 * Out lists of one component, both on screen - would each capture the other's pose as the authored one.
	 */
	TMap<TWeakObjectPtr<UWidget>, FWidgetAnimLayerArrayPanelSource*>& GetLayerArrayPoseOwners()
	{
		static TMap<TWeakObjectPtr<UWidget>, FWidgetAnimLayerArrayPanelSource*> Owners;
		return Owners;
	}

	/** How often a missing preview widget is looked for again - finding it may walk every widget. */
	constexpr double PreviewWidgetSearchInterval = 0.5;

	/** How long edits must have stopped before they are told through the Details - see SetArrayHandle. */
	constexpr double CommitSettleSeconds = 0.6;
}

FWidgetAnimLayerArrayPanelSource::FWidgetAnimLayerArrayPanelSource(UObject* InOwner, FName InArrayPropertyName, bool bInPlaysAsOut)
	: Owner(InOwner)
	, ArrayPropertyName(InArrayPropertyName)
	, bPlaysAsOut(bInPlaysAsOut)
{
	// Every edit here is a Modify() in a transaction; an object that isn't transactional would record nothing to
	// undo. Details-edited objects already are - this only makes sure.
	if (InOwner && !InOwner->HasAnyFlags(RF_Transactional))
	{
		InOwner->SetFlags(RF_Transactional);
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FWidgetAnimLayerArrayPanelSource::Tick));
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FWidgetAnimLayerArrayPanelSource::OnObjectPropertyChanged);
}

FWidgetAnimLayerArrayPanelSource::~FWidgetAnimLayerArrayPanelSource()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);

	// Put back as authored, without telling a subclass that is already being destroyed.
	if (UWidget* Widget = PosedWidget.Get())
	{
		if (IsValid(Widget))
		{
			UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Baselines);
		}
	}

	for (auto It = GetLayerArrayPoseOwners().CreateIterator(); It; ++It)
	{
		if (It.Value() == this || !It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

TArray<FWidgetAnimLayerGroup>* FWidgetAnimLayerArrayPanelSource::GetGroups() const
{
	if (!GetLayers())
	{
		return nullptr;
	}

	// Always exactly one, playing on the widget itself - whatever an edit elsewhere in the panel might have done.
	if (Groups.Num() != 1)
	{
		Groups.Reset();
		Groups.AddDefaulted();
	}
	return &Groups;
}

TArray<FWidgetAnimLayer>* FWidgetAnimLayerArrayPanelSource::GetLayers() const
{
	UObject* Object = Owner.Get();
	if (!Object || !IsValid(Object))
	{
		return nullptr;
	}

	// Looked up every time rather than cached: a Blueprint recompile can hand the object a new class.
	const FArrayProperty* ArrayProperty = FindFProperty<FArrayProperty>(Object->GetClass(), ArrayPropertyName);
	const FStructProperty* Inner = ArrayProperty ? CastField<FStructProperty>(ArrayProperty->Inner) : nullptr;
	if (!Inner || Inner->Struct != FWidgetAnimLayer::StaticStruct())
	{
		return nullptr;
	}

	return ArrayProperty->ContainerPtrToValuePtr<TArray<FWidgetAnimLayer>>(Object);
}

void FWidgetAnimLayerArrayPanelSource::NotifyGroupsChanged(EPropertyChangeType::Type ChangeType)
{
	UObject* Object = Owner.Get();
	TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Object || !Layers)
	{
		return;
	}

	// The list has one group; anything a layer brought in from elsewhere is folded into it.
	for (FWidgetAnimLayer& Layer : *Layers)
	{
		Layer.LayerGroup = 0;
	}

	UWidgetAnimTransformLibrary::SyncAllTransformAnims(Object);

	// Told to the object as a Details edit of the list would be - but not to the Details panel or UMG's designer,
	// which would rebuild everything showing it. See the class comment.
	FProperty* ArrayProperty = FindFProperty<FProperty>(Object->GetClass(), ArrayPropertyName);
	FPropertyChangedEvent Event(ArrayProperty, ChangeType);
	Object->PostEditChangeProperty(Event);

	// Inside a Blueprint - a component on a Widget Blueprint's widget - the Blueprint is out of date until it is
	// compiled, as after any Details edit. Only its status is set: broadcasting the change would rebuild the designer.
	if (UBlueprint* Blueprint = Object->GetTypedOuter<UBlueprint>())
	{
		if (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings)
		{
			Blueprint->Status = BS_Dirty;
		}
	}

	bEditedWhilePosed = true;
	MarkCommitPending();
	Claim();
}

void FWidgetAnimLayerArrayPanelSource::SetArrayHandle(const TSharedPtr<IPropertyHandle>& InHandle)
{
	ArrayHandle = InHandle;
}

void FWidgetAnimLayerArrayPanelSource::MarkCommitPending()
{
	bCommitPending = true;
	LastEditTime = FPlatformTime::Seconds();
}

void FWidgetAnimLayerArrayPanelSource::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// Edits made in the Selection panel reach the object without passing through here; they are caught as they
	// land. Mid-drag ones only push the settling back.
	if (bCommitting || !Object || Object != Owner.Get())
	{
		return;
	}

	if (Event.ChangeType == EPropertyChangeType::Interactive)
	{
		LastEditTime = FPlatformTime::Seconds();
		return;
	}
	MarkCommitPending();
}

void FWidgetAnimLayerArrayPanelSource::CommitEdits()
{
	bCommitScheduled = false;

	UObject* Object = Owner.Get();
	if (!bCommitPending || !Object || !IsValid(Object))
	{
		return;
	}
	bCommitPending = false;

	// Told as the Details' own edit of the whole list would be. Whatever that does in reply - refreshing the Details,
	// rebuilding the designer's preview - comes back as property changes of its own, which aren't new edits.
	TGuardValue<bool> CommittingGuard(bCommitting, true);

	PrepareCommit();

	const TSharedPtr<IPropertyHandle> Handle = ArrayHandle.Pin();
	if (Handle.IsValid() && Handle->IsValidHandle())
	{
		Handle->NotifyPreChange();
		Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
		Handle->NotifyFinishedChangingProperties();
		FinishCommit(/*bThroughHandle=*/true);
		return;
	}

	// No Details row to tell - the list is only open in its Expand window: the Blueprint is marked modified, so
	// its preview and everything placing it pick the edits up once it is compiled.
	if (UBlueprint* Blueprint = Object->GetTypedOuter<UBlueprint>())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	FinishCommit(/*bThroughHandle=*/false);
}

UUserWidget* FWidgetAnimLayerArrayPanelSource::GetPreviewRoot() const
{
	UWidget* Widget = GetPreviewWidget();
	if (!Widget)
	{
		return nullptr;
	}

	if (UUserWidget* AsUserWidget = Cast<UUserWidget>(Widget))
	{
		return AsUserWidget;
	}
	return Widget->GetTypedOuter<UUserWidget>();
}

UWidget* FWidgetAnimLayerArrayPanelSource::ResolveLayerTarget(UWidget* Widget, FName Path) const
{
	if (!Widget || Path.IsNone())
	{
		return nullptr;
	}

	// Paths are into the User Widget the widget sits in - the Widget Blueprint the list was authored in.
	UUserWidget* Root = Widget->GetTypedOuter<UUserWidget>();
	if (!Root)
	{
		Root = Cast<UUserWidget>(Widget);
	}
	return Root ? UWidgetAnimLibrary::FindWidgetByPathInTree(Root, Path) : nullptr;
}

UWidget* FWidgetAnimLayerArrayPanelSource::GetPreviewWidget() const
{
	if (UWidget* Cached = CachedWidget.Get())
	{
		if (IsValid(Cached))
		{
			return Cached;
		}
	}

	const double Now = FPlatformTime::Seconds();
	if (LastWidgetSearchTime >= 0.0 && Now - LastWidgetSearchTime < PreviewWidgetSearchInterval)
	{
		return nullptr;
	}
	LastWidgetSearchTime = Now;

	CachedWidget = FindPreviewWidget();
	return CachedWidget.Get();
}

void FWidgetAnimLayerArrayPanelSource::BuildSettings(UWidget* Widget, TArray<FWidgetAnimSettings>& OutSettings, int32* OutFirstOwn) const
{
	// The animation the list plays within first, as at runtime, where the list plays as part of it.
	AppendContextSettings(Widget, OutSettings);
	if (OutFirstOwn)
	{
		*OutFirstOwn = OutSettings.Num();
	}

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	// Exactly as the component places its additive layers: in sequence, each after the one above it, each on its
	// Target Widget or the widget itself - an Image for material tracks, the Shape Box it is or sits in for shape tracks.
	const int32 FirstSetting = OutSettings.Num();
	TArray<FWidgetAnimSettings> Placed;
	UWidgetAnimLibrary::BuildLayerSettings(*Layers, Placed);

	OutSettings.Reserve(OutSettings.Num() + Placed.Num());
	for (int32 Index = 0; Index < Placed.Num(); ++Index)
	{
		FWidgetAnimSettings& Settings = Placed[Index];

		// Each track on its own Target Widget, or on the widget itself - resolved as the component resolves it.
		UWidget* Target = Layers->IsValidIndex(Index) ? ResolveLayerTarget(Widget, (*Layers)[Index].TargetWidget) : nullptr;
		UWidget* Moved = Target ? Target : Widget;

		Settings.TargetWidget = Target;
		Settings.MaterialTargetImage = Cast<UImage>(Moved);
		Settings.ShapeTargetBox = UWidgetAnimLibrary::ResolveShapeBox(Moved);
		Settings.bInvertAnimation = (Settings.bInvertAnimation != bInvert) != GetContextInvert();
		OutSettings.Add(MoveTemp(Settings));
	}

	// Tracks hidden in the panel keep their place in time but pose nothing.
	UWidgetAnimLibrary::MuteHiddenLayers(*Layers, OutSettings, FirstSetting);
}

void FWidgetAnimLayerArrayPanelSource::ScrubToAlpha(float InAlpha)
{
	Alpha = FMath::Clamp(InAlpha, 0.f, 1.f);
	bPlaying = false;
	Claim();
}

void FWidgetAnimLayerArrayPanelSource::FocusKey(int32 FlatLayerIndex, int32 KeyIndex)
{
	TArray<FWidgetAnimSettings> Settings;
	int32 FirstOwn = 0;
	BuildSettings(GetPreviewWidget(), Settings, &FirstOwn);

	// The list's layers come after any animation it plays within.
	const int32 SettingIndex = FirstOwn + FlatLayerIndex;
	const float PassSeconds = UWidgetAnimLibrary::GetStackPassDuration(Settings);
	if (!Settings.IsValidIndex(SettingIndex) || PassSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float KeySeconds = UWidgetAnimLibrary::GetTransformKeyStackTime(Settings[SettingIndex], KeyIndex);
	if (KeySeconds < 0.f)
	{
		return;
	}

	// Played inverted - Invert, or as an exit, which plays in reverse - its key is posed at the mirrored moment.
	float KeyAlpha = FMath::Clamp(KeySeconds / PassSeconds, 0.f, 1.f);
	if ((bInvert != bPlaysAsOut) != GetContextInvert())
	{
		KeyAlpha = 1.f - KeyAlpha;
	}

	ScrubToAlpha(KeyAlpha);
}

void FWidgetAnimLayerArrayPanelSource::TogglePlay()
{
	Claim();

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

void FWidgetAnimLayerArrayPanelSource::NotifyPanelActive()
{
	LastPanelActiveTime = FPlatformTime::Seconds();
}

void FWidgetAnimLayerArrayPanelSource::Claim()
{
	bClaimed = true;
}

UWidget* FWidgetAnimLayerArrayPanelSource::PutBackPose()
{
	UWidget* Widget = PosedWidget.Get();
	const bool bAlive = Widget && IsValid(Widget);

	// Only while the widget it was captured on is still alive: the designer rebuilds its preview freely, and a
	// rebuilt preview is already back as authored.
	if (bAlive)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(Baselines);
		if (const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget())
		{
			SlateWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
		}
	}

	TMap<TWeakObjectPtr<UWidget>, FWidgetAnimLayerArrayPanelSource*>& PoseOwners = GetLayerArrayPoseOwners();
	if (FWidgetAnimLayerArrayPanelSource** PoseOwner = PoseOwners.Find(PosedWidget))
	{
		if (*PoseOwner == this)
		{
			PoseOwners.Remove(PosedWidget);
		}
	}

	Baselines = FWidgetAnimBaselineSet();
	PosedWidget.Reset();
	PosedSignature = 0;

	return bAlive ? Widget : nullptr;
}

void FWidgetAnimLayerArrayPanelSource::RestorePreview()
{
	const bool bEdited = bEditedWhilePosed;
	UWidget* Released = PutBackPose();

	bEditedWhilePosed = false;
	bClaimed = false;
	bPlaying = false;

	if (Released)
	{
		OnPreviewReleased(Released, bEdited);
	}
}

bool FWidgetAnimLayerArrayPanelSource::Tick(float DeltaTime)
{
	// Settled edits, told through the Details once nothing is held down - never mid-drag, and never from inside
	// this ticker: telling them may rebuild the Details, and with them the panels holding this source, so it is
	// done from a ticker of its own that keeps the source alive until it is finished.
	if (bCommitPending && !bCommitScheduled && FPlatformTime::Seconds() - LastEditTime > CommitSettleSeconds
		&& FSlateApplication::IsInitialized() && FSlateApplication::Get().GetPressedMouseButtons().Num() == 0)
	{
		bCommitScheduled = true;
		const TSharedRef<IWidgetAnimPanelSource> KeepAlive = AsShared();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([KeepAlive](float)
		{
			static_cast<FWidgetAnimLayerArrayPanelSource&>(KeepAlive.Get()).CommitEdits();
			return false;
		}));
	}

	const bool bPanelActive = LastPanelActiveTime >= 0.0 && FPlatformTime::Seconds() - LastPanelActiveTime < PanelIdleSeconds;

	// Not on screen, or not used yet: nothing is being judged, so nothing stays posed.
	if (!bPanelActive || !bClaimed || !GetLayers())
	{
		if (PosedWidget.IsValid() || Baselines.Baselines.Num() > 0 || Baselines.AuthoritativeBaselines.Num() > 0)
		{
			RestorePreview();
		}
		if (!bPanelActive)
		{
			bClaimed = false;
			bPlaying = false;
		}
		return true;
	}

	UWidget* Widget = GetPreviewWidget();
	if (!Widget)
	{
		return true;
	}

	// Taken over from whichever source was posing this widget before - which puts it back first.
	const TWeakObjectPtr<UWidget> WidgetKey(Widget);
	TMap<TWeakObjectPtr<UWidget>, FWidgetAnimLayerArrayPanelSource*>& PoseOwners = GetLayerArrayPoseOwners();
	FWidgetAnimLayerArrayPanelSource* Previous = PoseOwners.FindRef(WidgetKey);
	if (Previous && Previous != this)
	{
		Previous->RestorePreview();
	}

	TArray<FWidgetAnimSettings> Settings;
	BuildSettings(Widget, Settings);

	// What is moved, and how: when that changes - a track type swapped, a new preview instance - the old poses
	// come off and the authored state is read again before posing.
	uint32 Signature = PointerHash(Widget);
	for (const FWidgetAnimSettings& Layer : Settings)
	{
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Layer.AnimType)));
		Signature = HashCombine(Signature, GetTypeHash(static_cast<uint8>(Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.bAnimateShape)));
		Signature = HashCombine(Signature, PointerHash(Layer.TypeParams.GetScriptStruct()));
		Signature = HashCombine(Signature, PointerHash(Layer.TargetWidget.Get()));
	}

	if (PosedWidget.Get() != Widget || Signature != PosedSignature)
	{
		// A new widget - the first pose, or a preview the designer rebuilt - is taken over; the same one is only
		// put back and read again.
		const bool bFirstPose = PosedWidget.Get() != Widget;
		PutBackPose();

		if (bFirstPose)
		{
			OnPreviewTaken(Widget);
		}

		if (bPlaysAsOut)
		{
			UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Widget, TArray<FWidgetAnimSettings>(), Settings, Baselines);
		}
		else
		{
			UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Widget, Settings, TArray<FWidgetAnimSettings>(), Baselines);
		}
		PosedWidget = Widget;
		PosedSignature = Signature;
		PoseOwners.Add(WidgetKey, this);
	}

	// The clock. One pass is the list at this Speed; Loop wraps it, Ping Pong bounces it.
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

	UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(Widget, Settings, Alpha, bPlaysAsOut, Baselines);

	if (const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget())
	{
		SlateWidget->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
	}
	if (UUserWidget* Root = GetPreviewRoot())
	{
		if (Root != Widget)
		{
			if (const TSharedPtr<SWidget> RootSlate = Root->GetCachedWidget())
			{
				RootSlate->Invalidate(EInvalidateWidgetReason::LayoutAndVolatility);
			}
		}
	}

	return true;
}

FString FWidgetAnimLayerArrayPanelSource::GetNewAssetPackagePath() const
{
	// Beside the Widget Blueprint (or whatever asset) the list lives in.
	const UObject* Object = Owner.Get();
	return Object ? FPackageName::GetLongPackagePath(Object->GetOutermost()->GetName()) : FString(TEXT("/Game"));
}

FString FWidgetAnimLayerArrayPanelSource::GetNewAssetBaseName() const
{
	// WBP_Card -> WA_Card_Tracks: the asset's name, prefix swapped, and what it came from.
	const UObject* Object = Owner.Get();
	FString BaseName = Object ? FPackageName::GetShortName(Object->GetOutermost()->GetName()) : FString(TEXT("Tracks"));
	BaseName.RemoveFromStart(TEXT("WBP_"));
	BaseName.RemoveFromStart(TEXT("W_"));
	return FString::Printf(TEXT("WA_%s_Tracks"), *BaseName);
}

UWidgetAnimPresetAsset* FWidgetAnimLayerArrayPanelSource::SaveAsNewAsset()
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || Layers->Num() == 0)
	{
		return nullptr;
	}

	const FString PackagePath = GetNewAssetPackagePath();

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	FString UniquePackageName;
	FString UniqueAssetName;
	AssetTools.CreateUniqueAssetName(PackagePath / GetNewAssetBaseName(), FString(), UniquePackageName, UniqueAssetName);

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	Factory->DataAssetClass = UWidgetAnimPresetAsset::StaticClass();

	UWidgetAnimPresetAsset* NewAsset = Cast<UWidgetAnimPresetAsset>(
		AssetTools.CreateAsset(UniqueAssetName, PackagePath, UWidgetAnimPresetAsset::StaticClass(), Factory));

	if (!NewAsset)
	{
		return nullptr;
	}

	// A group per widget the tracks move: the tracks on the widget itself in a group aimed at nothing in particular -
	// a use of the asset points it at its own widget - and each Target Widget in a group of its own, previewing on
	// that path, which is also where a new use of the asset starts from.
	NewAsset->Modify();
	NewAsset->LayerGroups.Reset();
	NewAsset->Layers = *Layers;

	TArray<FName> GroupTargets;
	for (FWidgetAnimLayer& Layer : NewAsset->Layers)
	{
		int32 Group = GroupTargets.IndexOfByKey(Layer.TargetWidget);
		if (Group == INDEX_NONE)
		{
			Group = GroupTargets.Add(Layer.TargetWidget);
			FWidgetAnimLayerGroup& NewGroup = NewAsset->LayerGroups.AddDefaulted_GetRef();
			NewGroup.PreviewTarget = Layer.TargetWidget;
			NewGroup.GroupName = Layer.TargetWidget.IsNone() ? FString() : Layer.TargetWidget.ToString();
		}
		Layer.LayerGroup = Group;

		// In an animation the group says which widget a layer moves.
		Layer.TargetWidget = NAME_None;
	}
	NewAsset->AnimationSpeedMultiplier = 1.f;
	NewAsset->PostEditChange();
	NewAsset->MarkPackageDirty();

	// Saved straight away, as the panel's other Save does, so closing the editor can't quietly lose it.
	UEditorLoadingAndSavingUtils::SavePackages({ NewAsset->GetPackage() }, /*bOnlyDirty=*/false);

	if (GEditor)
	{
		TArray<UObject*> ToSelect{ NewAsset };
		GEditor->SyncBrowserToObjects(ToSelect);
	}

	return NewAsset;
}


#undef LOCTEXT_NAMESPACE