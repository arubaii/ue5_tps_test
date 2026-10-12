// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimationComponent.h"

#include "Algo/Compare.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"
#include "WidgetAnimTransform.h"
#include "Engine/Engine.h"
#include "Serialization/CustomVersion.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"
#include "WidgetAnimStateMachineRuntime.h"
#include "WidgetAnimationStateMachineComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimationComponent, Log, All);

const FGuid FWidgetAnimationComponentVersion::GUID(0x4E86197C, 0x6A5440B8, 0x9235D438, 0x82E3D644);

TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationComponent>> UWidgetAnimationComponent::ComponentsByWidget;
TArray<TWeakObjectPtr<UWidgetAnimationComponent>> UWidgetAnimationComponent::RuntimeClockComponents;
FTSTicker::FDelegateHandle UWidgetAnimationComponent::RuntimeClockHandle;

namespace
{
	FCustomVersionRegistration GRegisterWidgetAnimationComponentVersion(
		FWidgetAnimationComponentVersion::GUID,
		FWidgetAnimationComponentVersion::LatestVersion,
		TEXT("WidgetAnimationComponentVer"));

	// Entry tracks use SourceIndex 0..N-1 (their position in the entry array), so a negative sentinel can
	// never collide with one. This is what lets the unnamed layers - Additive Anim Layers plus anything
	// handed over with SetExtraAnimSettings - run as their own track without an Anim ID and still be found
	// again to stop or restart.
	constexpr int32 UnnamedLayersSourceIndex = -1;

	/** How many frames the designer preview waits for the owner's Slate widget before giving up. */
	constexpr int32 PreviewTimerRetryFrames = 30;

	/** How long an Anim ID conflict error stays on screen. Re-reporting the same conflict refreshes it rather than stacking. */
	constexpr float AnimIdConflictMessageSeconds = 10.f;

	/**
	 * Additive layers as runtime settings. No Target Widget, so they animate the widget the component is
	 * on; material layers use it too when it is an Image, and a Shape Box layer uses it when
	 * it is a Shape Box - the same narrowing an entry with no target gets in FWidgetAnimEntryBase::BuildSettings.
	 */
	void AppendAdditiveLayers(const TArray<FWidgetAnimLayer>& Layers, UWidget* AnimatedWidget, TFunctionRef<UWidget*(FName)> Resolver, TArray<FWidgetAnimSettings>& OutSettings)
	{
		// Placed in order, each after the one above it, exactly as an entry's layers are - one setting per layer.
		TArray<FWidgetAnimSettings> Placed;
		UWidgetAnimLibrary::BuildLayerSettings(Layers, Placed);

		OutSettings.Reserve(OutSettings.Num() + Placed.Num());
		for (int32 Index = 0; Index < Placed.Num(); ++Index)
		{
			FWidgetAnimSettings& Settings = Placed[Index];

			// Each layer on its own Target Widget - a path, resolved as an entry's are - or on the widget this
			// component is on. A path that finds nothing falls back to that widget, as an entry's does.
			// A layer of an animation inserted as a track moves its track's widget.
			const int32 LayerIndex = Layers.IsValidIndex(Index) ? Index : Settings.InsertedFromLayer;
			UWidget* Target = Layers.IsValidIndex(LayerIndex) && !Layers[LayerIndex].TargetWidget.IsNone() ? Resolver(Layers[LayerIndex].TargetWidget) : nullptr;
			UWidget* Moved = Target ? Target : AnimatedWidget;

			Settings.TargetWidget = Target;
			Settings.MaterialTargetWidget = Moved;

			// The widget itself when it is a Shape Box, or the Shape Box it sits in - so a component on an Image
			// inside a Shape Box animates the box's outline while it moves the Image.
			Settings.ShapeTargetBox = UWidgetAnimLibrary::ResolveShapeBox(Moved);
			OutSettings.Add(MoveTemp(Settings));
		}
	}

	template <typename StructType>
	bool AreStructArraysIdentical(const TArray<StructType>& A, const TArray<StructType>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}

		const UScriptStruct* Struct = StructType::StaticStruct();
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!Struct->CompareScriptStruct(&A[Index], &B[Index], PPF_None))
			{
				return false;
			}
		}

		return true;
	}

	template <typename StructType>
	bool AreStructsIdentical(const StructType& A, const StructType& B)
	{
		return StructType::StaticStruct()->CompareScriptStruct(&A, &B, PPF_None);
	}

	/**
	 * Calls Visit for the nearest components below Widget: a branch that has its own component is handed to
	 * it and not walked further, because that component cascades into its own children in turn. Steps into
	 * nested User Widgets, whose trees are separate objects.
	 */
	void ForEachNearestChildComponent(UWidget* Widget, TFunctionRef<void(UWidgetAnimationComponent*)> Visit, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		auto VisitChild = [&Visit, Depth](UWidget* Child)
		{
			if (!Child)
			{
				return;
			}

			if (UWidgetAnimationComponent* Component = UWidgetAnimationComponent::FindWidgetAnimationComponent(Child))
			{
				Visit(Component);
				return;
			}

			ForEachNearestChildComponent(Child, Visit, Depth + 1);
		};

		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				VisitChild(Panel->GetChildAt(Index));
			}
		}
		else if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree)
			{
				VisitChild(UserWidget->WidgetTree->RootWidget);
			}
		}
	}
}

// =====================================================================================================
// UUIComponent
// =====================================================================================================

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UWidgetAnimationComponent::OnInitialize()
{
	SetAnimOwner(GetOwner().Get(), /*bByCode=*/false);
	return true;
}
#else
void UWidgetAnimationComponent::OnInitialize()
{
	SetAnimOwner(GetOwner().Get(), /*bByCode=*/false);
}

void UWidgetAnimationComponent::OnPreConstruct(bool bIsDesignTime)
{
	// Some owners are initialized before the component system hands us the widget; pick it up here too.
	if (!AnimOwner.IsValid())
	{
		SetAnimOwner(GetOwner().Get(), /*bByCode=*/false);
	}

	if (bIsDesignTime)
	{
		NotifyOwnerDesignTimeRefresh();
	}
}
#endif

void UWidgetAnimationComponent::OnConstruct()
{
	if (!AnimOwner.IsValid())
	{
		SetAnimOwner(GetOwner().Get(), /*bByCode=*/false);
	}

	NotifyOwnerConstructed();
}

void UWidgetAnimationComponent::OnDestruct()
{
	NotifyOwnerDestructed();
}

void UWidgetAnimationComponent::BeginDestroy()
{
	StopRuntimeTicking();
	StopDesignerPreview();
	if (StateEventTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StateEventTickerHandle);
		StateEventTickerHandle.Reset();
	}
	if (const TSharedPtr<FActiveTimerHandle> Handle = MachinePreviewTimerHandle.Pin())
	{
		if (const TSharedPtr<SWidget> Widget = MachinePreviewTimerWidget.Pin())
		{
			Widget->UnRegisterActiveTimer(Handle.ToSharedRef());
		}
	}
	Unregister();
	Super::BeginDestroy();
}

// =====================================================================================================
// AnimatedWidget and lookup
// =====================================================================================================

void UWidgetAnimationComponent::BindToWidget(UWidget* InWidget)
{
	SetAnimOwner(InWidget, /*bByCode=*/true);
}

void UWidgetAnimationComponent::SetAnimOwner(UWidget* InWidget, bool bByCode)
{
	if (AnimOwner.Get() == InWidget)
	{
		bBoundByCode = bByCode;
		return;
	}

	Unregister();

	AnimOwner = InWidget;
	bBoundByCode = bByCode;

	// Templates (class defaults, Widget Blueprint archetypes) never animate anything, and registering them
	// would let a lookup on a live widget land on a template that happens to share its key.
	if (InWidget && !InWidget->IsTemplate() && !IsTemplate())
	{
		ComponentsByWidget.Add(TObjectKey<UWidget>(InWidget), this);
	}
}

void UWidgetAnimationComponent::Unregister()
{
	if (UWidget* Previous = AnimOwner.Get())
	{
		const TObjectKey<UWidget> Key(Previous);
		if (const TWeakObjectPtr<UWidgetAnimationComponent>* Existing = ComponentsByWidget.Find(Key))
		{
			if (Existing->Get() == this || !Existing->IsValid())
			{
				ComponentsByWidget.Remove(Key);
			}
		}
	}
}

UWidgetAnimationComponent* UWidgetAnimationComponent::FindWidgetAnimationComponent(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<UWidgetAnimationComponent>* Found = ComponentsByWidget.Find(TObjectKey<UWidget>(Widget)))
	{
		return Found->Get();
	}

	return nullptr;
}

void UWidgetAnimationComponent::ForEachNearestNestedComponent(UWidget* Widget, TFunctionRef<void(UWidgetAnimationComponent*)> Visit)
{
	ForEachNearestChildComponent(Widget, Visit);
}

UUserWidget* UWidgetAnimationComponent::GetPathRoot() const
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return nullptr;
	}

	// Bound by a User Widget onto itself: paths are into its own tree, as they always were.
	if (bBoundByCode)
	{
		if (UUserWidget* OwnerAsUserWidget = Cast<UUserWidget>(AnimatedWidget))
		{
			return OwnerAsUserWidget;
		}
	}

	// Added in the designer: paths are into the Widget Blueprint the component was added in, which is the
	// User Widget whose tree holds the owner - the same tree the Target Widget dropdown lists.
	if (UUserWidget* ContainingUserWidget = AnimatedWidget->GetTypedOuter<UUserWidget>())
	{
		return ContainingUserWidget;
	}

	return Cast<UUserWidget>(AnimatedWidget);
}

UWidget* UWidgetAnimationComponent::ResolveAnimTarget(FName TargetWidgetName) const
{
	// None means "the animated widget", and the library already falls back to the widget it was given.
	if (TargetWidgetName.IsNone())
	{
		return nullptr;
	}

	UUserWidget* Root = GetPathRoot();
	return Root ? UWidgetAnimLibrary::FindWidgetByPathInTree(Root, TargetWidgetName) : nullptr;
}

namespace WidgetAnimComponentOptions
{
	/**
	 * The name of the widget a UI component is kept for, read off whatever holds it: an array of structs, on one of its
	 * outers, with an entry holding the component and a name beside it - how UMG's component container pairs each
	 * component with its widget. Found by reflection, so nothing here depends on the container's private layout.
	 */
	FName FindHeldUnderName(const UObject* Held)
	{
		static constexpr int32 MaxOuters = 4;
		int32 Depth = 0;
		for (const UObject* Holder = Held ? Held->GetOuter() : nullptr; Holder && Depth < MaxOuters; Holder = Holder->GetOuter(), ++Depth)
		{
			for (TFieldIterator<FArrayProperty> It(Holder->GetClass()); It; ++It)
			{
				const FArrayProperty* ArrayProperty = *It;
				const FStructProperty* Inner = CastField<FStructProperty>(ArrayProperty->Inner);
				if (!Inner)
				{
					continue;
				}

				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Holder));
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const uint8* Element = Helper.GetRawPtr(Index);
					bool bHoldsIt = false;
					FName HeldName;
					for (TFieldIterator<FProperty> FieldIt(Inner->Struct); FieldIt; ++FieldIt)
					{
						if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*FieldIt))
						{
							bHoldsIt |= ObjectProperty->GetObjectPropertyValue_InContainer(Element) == Held;
						}
						else if (const FNameProperty* NameProperty = CastField<FNameProperty>(*FieldIt))
						{
							HeldName = NameProperty->GetPropertyValue_InContainer(Element);
						}
					}

					if (bHoldsIt)
					{
						return HeldName;
					}
				}
			}
		}
		return NAME_None;
	}

#if WITH_EDITOR
	/**
	 * The widget a live copy of Template is on - a designer preview's component made from it. What the Details panel's
	 * template, which is on no widget itself, stands for.
	 */
	UWidget* FindLiveOwner(const UObject* Template)
	{
		// The widget a component is on: a Widget Animation Component's, or a State Machine Component's.
		auto OwnerOf = [](const UObject* Candidate) -> UWidget*
		{
			if (const UWidgetAnimationComponent* AsPlayer = Cast<UWidgetAnimationComponent>(Candidate))
			{
				return AsPlayer->GetAnimatedWidget();
			}
			const UWidgetAnimationStateMachineComponent* AsMachines = Cast<UWidgetAnimationStateMachineComponent>(Candidate);
			return AsMachines ? AsMachines->GetAnimatedWidget() : nullptr;
		};

		TArray<UObject*> Candidates;
		GetObjectsOfClass(Template->GetClass(), Candidates, /*bIncludeDerivedClasses=*/true, RF_ClassDefaultObject | RF_ArchetypeObject, EInternalObjectFlags::Garbage);
		for (const UObject* Candidate : Candidates)
		{
			UWidget* CandidateOwner = OwnerOf(Candidate);
			if (Candidate == Template || !CandidateOwner)
			{
				continue;
			}

			for (const UObject* Archetype = Candidate->GetArchetype(); Archetype; Archetype = Archetype->GetArchetype())
			{
				if (Archetype == Template)
				{
					return CandidateOwner;
				}
				if (Archetype->HasAnyFlags(RF_ClassDefaultObject))
				{
					break;
				}
			}
		}
		return nullptr;
	}
#endif
}

TArray<FString> UWidgetAnimationComponent::GetAnimTargetWidgetOptions() const
{
	return GatherTargetWidgetOptions(this, AnimOwner.Get(), bBoundByCode);
}

TArray<FString> UWidgetAnimationComponent::GatherTargetWidgetOptions(const UObject* Component, UWidget* WidgetOwner, bool bOwnTree)
{
	TArray<FString> Options;

	// The widget the component is on - or, for the Details panel's template, the one its live copy is on.
#if WITH_EDITOR
	if (!WidgetOwner && Component)
	{
		WidgetOwner = WidgetAnimComponentOptions::FindLiveOwner(Component);
	}
#endif

	if (WidgetOwner)
	{
		// On a whole User Widget - bound onto itself by code, or the top of the Widget Blueprint - its paths go into its
		// own tree, so that is what is offered. Otherwise the widget and everything inside it, named as they are in the
		// tree it sits in - which is how targets are looked up.
		UUserWidget* OwnerAsUserWidget = Cast<UUserWidget>(WidgetOwner);
		if (OwnerAsUserWidget && (bOwnTree || !WidgetOwner->GetTypedOuter<UUserWidget>()))
		{
			UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(OwnerAsUserWidget, Options);
		}
		else
		{
			UWidgetAnimLibrary::GatherTargetableWidgetPaths(WidgetOwner, Options);
		}
	}
	else
	{
		// No widget to ask: the Widget Blueprint's tree as authored - its generated class keeps it - from the widget the
		// component is kept for, or all of it when that can't be told.
		const UWidgetBlueprintGeneratedClass* AuthoredClass = Component ? Component->GetTypedOuter<UWidgetBlueprintGeneratedClass>() : nullptr;
#if WITH_EDITORONLY_DATA
		if (!AuthoredClass && Component)
		{
			if (const UBlueprint* Blueprint = Component->GetTypedOuter<UBlueprint>())
			{
				AuthoredClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
			}
		}
#endif
		if (const UWidgetTree* AuthoredTree = AuthoredClass ? AuthoredClass->GetWidgetTreeArchetype() : nullptr)
		{
			const FName HeldName = WidgetAnimComponentOptions::FindHeldUnderName(Component);
			UWidget* Kept = HeldName.IsNone() ? nullptr : AuthoredTree->FindWidget(HeldName);
			UWidgetAnimLibrary::GatherTargetableWidgetPaths(Kept ? Kept : AuthoredTree->RootWidget.Get(), Options);
		}
	}

	// First, so there is a way back to "animate the widget this is on" once something has been picked.
	Options.Insert(FWidgetAnimGroupTarget::ThisWidget.ToString(), 0);
	return Options;
}

// =====================================================================================================
// When the additive layers start
// =====================================================================================================

bool UWidgetAnimationComponent::CanHostAdditiveList(bool bOutList, bool bInAnimOut, int32 EntryIndex) const
{
	const TArray<FWidgetAnimEntry>& List = bInAnimOut ? AnimOut : AnimIn;
	if (!List.IsValidIndex(EntryIndex) || !List[EntryIndex].Animation)
	{
		return false;
	}

	// In its own list's direction, or mirrored into this one.
	return bInAnimOut == bOutList || List[EntryIndex].bMirrorInOut;
}

int32 UWidgetAnimationComponent::ResolveAdditiveStart(bool bOutList, bool& bOutInAnimOut) const
{
	const FWidgetAnimAdditiveStart& Start = bOutList ? AdditiveAnimLayersOutStart : AdditiveAnimLayersInStart;
	bOutInAnimOut = Start.bInAnimOut;
	if (!Start.IsSet())
	{
		return INDEX_NONE;
	}

	// Where it was picked, if it is still there; else wherever its animation went - the list reordered.
	const TArray<FWidgetAnimEntry>& List = Start.bInAnimOut ? AnimOut : AnimIn;
	int32 Found = List.IsValidIndex(Start.EntryIndex) && List[Start.EntryIndex].Animation == Start.Animation ? Start.EntryIndex : INDEX_NONE;
	if (Found == INDEX_NONE)
	{
		Found = List.IndexOfByPredicate([&Start](const FWidgetAnimEntry& Entry) { return Entry.Animation == Start.Animation; });
	}

	return Found != INDEX_NONE && CanHostAdditiveList(bOutList, Start.bInAnimOut, Found) ? Found : INDEX_NONE;
}

bool UWidgetAnimationComponent::RefreshAdditiveStarts()
{
	bool bChanged = false;
	for (const bool bOutList : { false, true })
	{
		FWidgetAnimAdditiveStart& Start = bOutList ? AdditiveAnimLayersOutStart : AdditiveAnimLayersInStart;
		if (!Start.IsSet())
		{
			if (Start.EntryIndex != INDEX_NONE || Start.Animation)
			{
				Start.Reset();
				bChanged = true;
			}
			continue;
		}

		bool bInAnimOut = false;
		const int32 Found = ResolveAdditiveStart(bOutList, bInAnimOut);
		if (Found == INDEX_NONE)
		{
			// Its entry is gone: the list starts right away again.
			Start.Reset();
			bChanged = true;
		}
		else if (Found != Start.EntryIndex)
		{
			Start.EntryIndex = Found;
			bChanged = true;
		}
	}
	return bChanged;
}

int32 UWidgetAnimationComponent::ToEffectiveIndex(bool bOutDirection, bool bInAnimOut, int32 EntryIndex) const
{
	// AppendEntriesWithMirroring runs over Anim In, then Anim Out: the In direction is every Anim In entry, then the
	// Anim Out entries mirrored into it; the Out direction is the Anim In entries mirrored into it, then every Anim
	// Out entry.
	auto CountMirrored = [](const TArray<FWidgetAnimEntry>& List, int32 Before)
	{
		int32 Count = 0;
		for (int32 Index = 0; Index < FMath::Min(Before, List.Num()); ++Index)
		{
			Count += List[Index].bMirrorInOut ? 1 : 0;
		}
		return Count;
	};

	if (!bOutDirection)
	{
		return bInAnimOut ? AnimIn.Num() + CountMirrored(AnimOut, EntryIndex) : EntryIndex;
	}
	return bInAnimOut ? CountMirrored(AnimIn, AnimIn.Num()) + EntryIndex : CountMirrored(AnimIn, EntryIndex);
}

void UWidgetAnimationComponent::AppendLinkedAdditive(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings, int32 First) const
{
	const int32 LinkedEffective = bOut ? LinkedEffectiveOut : LinkedEffectiveIn;
	const TArray<FWidgetAnimSettings>& Linked = bOut ? LinkedAdditiveOut : LinkedAdditiveIn;
	const TArray<FWidgetAnimEntry>& Entries = bOut ? EffectiveOutEntries : EffectiveInEntries;
	if (EffectiveIndex != LinkedEffective || Linked.Num() == 0 || !Entries.IsValidIndex(EffectiveIndex) || !EntrySettings.IsValidIndex(First))
	{
		return;
	}

	// As one more part of the entry's animation: its per-use flags, its Playback Speed - not its animation's Speed
	// Multiplier, which the inline editor already shows its tracks at - and its clock, so the two loop as one.
	const FWidgetAnimEntry& Entry = Entries[EffectiveIndex];
	const int32 LoopGroup = EntrySettings[First].LoopGroup;

	EntrySettings.Reserve(EntrySettings.Num() + Linked.Num());
	for (const FWidgetAnimSettings& Layer : Linked)
	{
		FWidgetAnimSettings& Stamped = EntrySettings.Add_GetRef(Layer);
		Stamped.bLoop = Entry.bLoop;
		Stamped.bPingPong = Entry.bPingPong;
		Stamped.LoopCount = Entry.LoopCount;
		Stamped.bInvertAnimation = Stamped.bInvertAnimation || Entry.bInvertAnimation;
		Stamped.bMirrorInOut = Entry.bMirrorInOut;
		Stamped.PlaybackSpeed = FMath::Max(Entry.PlaybackSpeed, 0.01f);
		Stamped.LoopGroup = LoopGroup;
	}
}

// =====================================================================================================
// Building the effective stacks
// =====================================================================================================

void UWidgetAnimationComponent::RebuildEffectiveAnimations()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimComponentRebuildAnimations);
	EffectiveInEntries.Reset();
	EffectiveOutEntries.Reset();

	// An owner's defaults in place of lists left empty - see SetDefaultEntriesProvider. Entries of its own, once it has
	// any, always win.
	TArray<FWidgetAnimEntry> ProvidedIn;
	TArray<FWidgetAnimEntry> ProvidedOut;
	const bool bProvided = AnimIn.Num() == 0 && AnimOut.Num() == 0 && DefaultEntriesProvider && DefaultEntriesProvider(ProvidedIn, ProvidedOut);

	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(bProvided ? ProvidedIn : AnimIn, EffectiveInEntries, EffectiveOutEntries);
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(bProvided ? ProvidedOut : AnimOut, EffectiveOutEntries, EffectiveInEntries);

	// The working copies are brought up to the animations' current groups even when the authored entries
	// are behind - an animation that gained a group since this widget was last saved plays it on its default.
	for (FWidgetAnimEntry& Entry : EffectiveInEntries)
	{
		Entry.SyncGroupTargets();
	}
	for (FWidgetAnimEntry& Entry : EffectiveOutEntries)
	{
		Entry.SyncGroupTargets();
	}

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	// Each Additive Anim Layers list starts right away - one of the unnamed layers - or within one of the entries,
	// as part of it. An entry with nothing to play can't carry it, so the list starts right away then.
	auto LinkList = [this, &Resolver](bool bOut, const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimSettings>& Linked, int32& LinkedEffective)
	{
		Linked.Reset();
		LinkedEffective = INDEX_NONE;

		bool bInAnimOut = false;
		const int32 EntryIndex = ResolveAdditiveStart(bOut, bInAnimOut);
		const int32 Effective = EntryIndex == INDEX_NONE ? INDEX_NONE : ToEffectiveIndex(bOut, bInAnimOut, EntryIndex);
		const TArray<FWidgetAnimEntry>& Entries = bOut ? EffectiveOutEntries : EffectiveInEntries;
		if (Layers.Num() > 0 && Entries.IsValidIndex(Effective) && Entries[Effective].IsUsable())
		{
			AppendAdditiveLayers(Layers, AnimOwner.Get(), Resolver, Linked);
			LinkedEffective = Effective;
		}
	};
	LinkList(/*bOut=*/false, AdditiveAnimLayersIn, LinkedAdditiveIn, LinkedEffectiveIn);
	LinkList(/*bOut=*/true, AdditiveAnimLayersOut, LinkedAdditiveOut, LinkedEffectiveOut);

	// Every entry, followed by the list that starts within it, if one does.
	auto Flatten = [this, &Resolver](const TArray<FWidgetAnimEntry>& Entries, bool bOut, TArray<FWidgetAnimSettings>& OutSettings)
	{
		OutSettings.Reset();
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const int32 First = OutSettings.Num();
			Entries[Index].BuildSettingsResolved(OutSettings, Resolver);
			AppendLinkedAdditive(Index, bOut, OutSettings, First);
		}
	};
	Flatten(EffectiveInEntries, /*bOut=*/false, FlattenedIn);
	Flatten(EffectiveOutEntries, /*bOut=*/true, FlattenedOut);

	// The unnamed layers: Additive Anim Layers first - those that start right away - then anything code handed
	// over. They play as one track alongside the entries every time Anim In / Anim Out plays.
	UnnamedAnimIn.Reset();
	UnnamedAnimOut.Reset();
	if (LinkedEffectiveIn == INDEX_NONE)
	{
		AppendAdditiveLayers(AdditiveAnimLayersIn, AnimOwner.Get(), Resolver, UnnamedAnimIn);
	}
	if (LinkedEffectiveOut == INDEX_NONE)
	{
		AppendAdditiveLayers(AdditiveAnimLayersOut, AnimOwner.Get(), Resolver, UnnamedAnimOut);
	}
	UnnamedAnimIn.Append(ExtraAnimIn);
	UnnamedAnimOut.Append(ExtraAnimOut);

	// This widget's own settings for its value-driven tracks, on every stack built here. An entry's own settings are
	// built afresh each time it starts, and get them in PrepareEntrySettings.
	ApplyValueOverrides(LinkedAdditiveIn);
	ApplyValueOverrides(LinkedAdditiveOut);
	ApplyValueOverrides(UnnamedAnimIn);
	ApplyValueOverrides(UnnamedAnimOut);
	ApplyValueOverrides(FlattenedIn);
	ApplyValueOverrides(FlattenedOut);

	// On top of the entry-authored stack, which is also what the designer preview and the baselines read.
	FlattenedIn.Append(UnnamedAnimIn);
	FlattenedOut.Append(UnnamedAnimOut);

	RefreshAnimIdConflicts();
	RefreshEntryTriggers();
}

void UWidgetAnimationComponent::RefreshAnimIdConflicts()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimComponentAnimIdConflicts);
	TArray<FName> Found;
	UWidget* AnimatedWidget = AnimOwner.Get();

	// Sharing an ID across entries is intended - one named effect built from animations on several widgets.
	// Two entries under one ID going the same direction on the SAME widget is not: a by-ID call stacks
	// both onto that widget, and there is no telling which was meant. Checked per direction because an In
	// entry and an Out entry sharing an ID is the ordinary way to pair them.
	auto Scan = [this, AnimatedWidget, &Found](const TArray<FWidgetAnimEntry>& Entries)
	{
		TSet<TPair<FName, UWidget*>> Seen;
		for (const FWidgetAnimEntry& Entry : Entries)
		{
			if (Entry.AnimId.IsNone() || !Entry.IsUsable())
			{
				continue;
			}

			// Every widget this entry animates - one per Track Group, though groups may share one. Collected
			// first, so an entry whose own groups share a widget isn't reported as conflicting with itself.
			TSet<UWidget*> EntryTargets;
			for (int32 GroupIndex = 0; GroupIndex < FMath::Max(1, Entry.Animation->LayerGroups.Num()); ++GroupIndex)
			{
				// No target (or one that doesn't resolve) animates the owner, exactly as the library falls back.
				UWidget* Target = ResolveAnimTarget(Entry.GetGroupTargetName(GroupIndex));
				EntryTargets.Add(Target ? Target : AnimatedWidget);
			}

			for (UWidget* Target : EntryTargets)
			{
				bool bAlreadySeen = false;
				Seen.Add(TPair<FName, UWidget*>(Entry.AnimId, Target), &bAlreadySeen);
				if (bAlreadySeen)
				{
					Found.AddUnique(Entry.AnimId);
				}
			}
		}
	};

	Scan(EffectiveInEntries);
	Scan(EffectiveOutEntries);

	Found.Sort(FNameLexicalLess());

	// Reported when a conflict first appears rather than on every rebuild, so the log is not flooded.
	for (const FName& AnimId : Found)
	{
		if (!ConflictingAnimIds.Contains(AnimId))
		{
			ReportAnimIdConflict(AnimId);
		}
	}

	ConflictingAnimIds = MoveTemp(Found);
}

void UWidgetAnimationComponent::ReportAnimIdConflict(FName AnimId) const
{
	const UWidget* AnimatedWidget = AnimOwner.Get();
	const FString TargetWidgetName = AnimatedWidget ? AnimatedWidget->GetName() : FString(TEXT("None"));

	const FString Message = FString::Printf(
		TEXT("[WidgetAnimationComponent.%s] Animation ID '%s' is currently conflicting. Change the ID(s) which conflict for proper functionality."),
		*TargetWidgetName,
		*AnimId.ToString());

	UE_LOG(LogWidgetAnimationComponent, Error, TEXT("%s"), *Message);

	if (GEngine)
	{
		// Keyed on this component and this ID, so reporting the same conflict again replaces the line
		// on screen instead of adding another copy of it.
		const uint64 Key = (static_cast<uint64>(PointerHash(this)) << 32) | static_cast<uint64>(GetTypeHash(AnimId));
		GEngine->AddOnScreenDebugMessage(Key, AnimIdConflictMessageSeconds, FColor::Red, Message);
	}
}

bool UWidgetAnimationComponent::ReportConflictsIn(const TArray<FName>& AnimIds) const
{
	bool bAnyConflict = false;
	for (const FName& AnimId : AnimIds)
	{
		if (ConflictingAnimIds.Contains(AnimId))
		{
			ReportAnimIdConflict(AnimId);
			bAnyConflict = true;
		}
	}
	return bAnyConflict;
}

void UWidgetAnimationComponent::SetExtraAnimSettings(const TArray<FWidgetAnimSettings>& InExtraAnimIn, const TArray<FWidgetAnimSettings>& InExtraAnimOut)
{
	ExtraAnimIn = InExtraAnimIn;
	ExtraAnimOut = InExtraAnimOut;

	RebuildEffectiveAnimations();

	// Before the owner is constructed nothing has been captured or animated yet, so dropping the set lets
	// the capture at construction include whatever the new layers touch. After that the widgets are
	// already mid-animation, and a fresh capture would read back a pose the animation wrote.
	if (!bOwnerConstructed)
	{
		AnimBaselines.ResetBaselines();
		ResetPoseBaselines.ResetBaselines();
	}
}

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UWidgetAnimationComponent::NotifyOwnerConstructed()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (bOwnerConstructed || !AnimatedWidget || AnimatedWidget->IsDesignTime())
	{
		return;
	}

	bOwnerConstructed = true;
	bLastSeenVisible = IsVisibleState(AnimatedWidget->GetVisibility());

	RebuildEffectiveAnimations();

	// Set up before the baselines are captured, so the widgets they animate are captured too.
	InitializeMachines();

	EnsureRuntimeBaselines();
	StartRuntimeTicking();

	if (bPlayOnlyOnConstructAndDestruct)
	{
		PlayIn();
	}
	else
	{
		// Only the Anim In entries set to Play In Animation On Construct.
		PlayForTrigger(EEntryTrigger::Construct, /*bOut=*/false);
	}

	// The state machines start at their Entry - posed now, before the widget is first drawn.
	StartMachines(/*bOnlyOnConstruct=*/true);
	if (AreMachinesRunning())
	{
		ApplyCurrentAnimPose();
	}
}

void UWidgetAnimationComponent::NotifyOwnerDestructed()
{
	if (!bOwnerConstructed)
	{
		return;
	}

	bOwnerConstructed = false;
	bIsDeferringVisibility = false;
	PendingExit = EPendingExit::None;
	StopRuntimeTicking();

	// A widget can be constructed again (removed and re-added); it should start from a clean player.
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	AnimPlayer.Phase = EWidgetAnimPhase::Idle;

	// And clean machines, started again at their Entry - their inputs kept (a Selected set from a Blueprint, a View Model's
	// value), which InitializeMachines carries over on the next construct.
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::Stop(Instance);
	}
	MachineLastPosed.Reset();
}

void UWidgetAnimationComponent::StartRuntimeTicking()
{
	if (bOnRuntimeClock)
	{
		return;
	}
	bOnRuntimeClock = true;

	// Fresh: nothing is known to rest until it has been worked out again.
	bTracksResting = false;
	bTracksHeldLastTick = false;

	if (!bInRuntimeClockList)
	{
		bInRuntimeClockList = true;
		RuntimeClockComponents.Add(this);
	}

	// The core ticker rather than the owner's tick: a UI Component can sit on any widget, most of which
	// never tick, and the core ticker runs before Slate lays out the frame, so a pose written here is the
	// pose that frame is drawn with. It keeps running while the game is paused, as UI tick always has.
	// One ticker for every component rather than one each: a screen of a thousand animated widgets is one call into
	// the ticker a frame, not a thousand.
	if (!RuntimeClockHandle.IsValid())
	{
		RuntimeClockHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&UWidgetAnimationComponent::TickRuntimeClock));
	}
}

void UWidgetAnimationComponent::StopRuntimeTicking()
{
	// Left in the clock's list, flagged off: the clock drops it on its next tick. Leaving costs nothing, however many
	// widgets are removed at once.
	bOnRuntimeClock = false;
}

bool UWidgetAnimationComponent::TickRuntimeClock(float DeltaTime)
{
	SCOPE_CYCLE_COUNTER(STAT_WidgetAnimRuntimeClock);
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimRuntimeClock);

	TArray<TWeakObjectPtr<UWidgetAnimationComponent>>& Components = RuntimeClockComponents;

	// Those that join during this tick (a widget constructed by another's animation event) start next frame, as a ticker
	// added during a tick always did; those that leave are dropped as the list is walked, keeping everyone's order.
	const int32 NumAtStart = Components.Num();
	int32 Kept = 0;
	for (int32 Index = 0; Index < NumAtStart; ++Index)
	{
		UWidgetAnimationComponent* Component = Components[Index].Get();
		if (!Component || !Component->bOnRuntimeClock)
		{
			if (Component)
			{
				Component->bInRuntimeClockList = false;
			}
			continue;
		}

		// Kept before it ticks: its tick may add components to the end of the list, never remove any from it.
		Components[Kept++] = Components[Index];
		Component->RuntimeTick(DeltaTime);
	}

	// Those that joined during the tick move down behind the ones kept.
	const int32 NumJoined = Components.Num() - NumAtStart;
	for (int32 Joined = 0; Joined < NumJoined; ++Joined)
	{
		Components[Kept + Joined] = Components[NumAtStart + Joined];
	}
	Components.SetNum(Kept + NumJoined);
	SET_DWORD_STAT(STAT_WidgetAnimComponents, Kept);

	if (Components.Num() == 0)
	{
		// Nothing on the clock: off the core ticker until something is again.
		RuntimeClockHandle.Reset();
		return false;
	}
	return true;
}

void UWidgetAnimationComponent::ShutdownRuntimeClock()
{
	if (RuntimeClockHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RuntimeClockHandle);
		RuntimeClockHandle.Reset();
	}
	RuntimeClockComponents.Empty();
}

void UWidgetAnimationComponent::RuntimeTick(float DeltaTime)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !(bOwnerConstructed || bDesignTimePlayback))
	{
		StopRuntimeTicking();
		return;
	}

	// A component cannot intercept its owner's SetVisibility, so becoming visible is noticed here instead.
	// The pose is applied in the same frame (see ApplyCurrentAnimPose), before Slate draws the widget.
	if (bOwnerConstructed && ReactsToVisibility() && !bPlayOnlyOnConstructAndDestruct)
	{
		const bool bVisibleNow = IsVisibleState(AnimatedWidget->GetVisibility());
		if (bVisibleNow && !bLastSeenVisible && !bIsDeferringVisibility)
		{
			PlayForTrigger(EEntryTrigger::Visibility, /*bOut=*/false);
		}
		bLastSeenVisible = bVisibleNow;
	}

	TickAnimationPlayback(DeltaTime);
}

bool UWidgetAnimationComponent::IsVisibleState(ESlateVisibility Visibility)
{
	return Visibility == ESlateVisibility::Visible
		|| Visibility == ESlateVisibility::SelfHitTestInvisible
		|| Visibility == ESlateVisibility::HitTestInvisible;
}

// =====================================================================================================
// Playback
// =====================================================================================================

void UWidgetAnimationComponent::EnsureRuntimeBaselines()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return;
	}

	// The state machines' widgets are captured too - in the same first call, since there is no second.
	TArray<FWidgetAnimSettings> MachineSettings;
	if (HasPlayedMachineRows() && !(bPreserveAuthoredTransforms ? AnimBaselines.bCaptured : ResetPoseBaselines.bCaptured))
	{
		MachineSettings = FlattenedIn;
		GatherMachineSettings(MachineSettings);
	}
	const TArray<FWidgetAnimSettings>& InToCapture = MachineSettings.Num() > 0 ? MachineSettings : FlattenedIn;

	// No-ops after the first call. Capturing again would read back a pose the animation itself wrote.
	if (bPreserveAuthoredTransforms)
	{
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, InToCapture, FlattenedOut, AnimBaselines);
	}
	else
	{
		// Not composed over - Preserve Authored Transforms is off - but still needed to put widgets back when an
		// animation is stopped with Reset To Start. Without it that reset had nothing to restore, so the widget
		// stayed wherever the animation had gotten to. The track player poses with it too, so a track not posing its
		// widget this frame - not started yet, or run back past its start by a ping-pong's return leg - puts it back
		// rather than leaving it wherever its last frame had it: a cell a hair short of its full size, its glow not
		// quite off.
		ResetPoseBaselines.bComposeTransforms = false;
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, InToCapture, FlattenedOut, ResetPoseBaselines);
	}
}

void UWidgetAnimationComponent::ApplyCurrentAnimPose()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return;
	}

	// Starting a track only resets its clock. A zero-delta tick applies time zero now, so the first frame
	// is never drawn in the authored pose.
	TArray<FName> JustFinished;
	TickPlayer(AnimatedWidget, 0.f, JustFinished);
}

void UWidgetAnimationComponent::TickPlayer(UWidget* AnimatedWidget, float DeltaTime, TArray<FName>& OutJustFinished)
{
	++PlayerTickDepth;

	// A frame with time, while everything rests, is skipped: the pose it would work out is the one already applied. A
	// pose asked for outright (no time: a track just started, a reset) is always worked out and applied. See
	// bTracksResting - this is what lets a screen hold thousands of widgets that have finished animating, or wait in a
	// state-machine state, and pay next to nothing for them each frame.
	const bool bMachinesRunning = AreMachinesRunning();
	if (bMachinesRunning || MachineLastPosed.Num() > 0)
	{
		bool bMachinesTicked = false;
		bool bSkip = false;
		if (bMachinesRunning && CanSkipTrackEvaluation(AnimatedWidget, DeltaTime))
		{
			// The machines read their inputs and move on first; only if one of them now poses differently is the frame
			// worked out and applied.
			bMachinesTicked = true;
			bSkip = !TickMachines(DeltaTime);
		}

		if (bSkip)
		{
			OutJustFinished.Reset();
			INC_DWORD_STAT(STAT_WidgetAnimComponentsResting);
		}
		else
		{
			// The tracks and the state machines worked out into one result, applied once: a machine's pose and a one-shot
			// played by ID on the same widget compose instead of taking turns.
			FWidgetAnimResult Result;
			UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(AnimatedWidget, AnimPlayer, GetPlayerBaselines(), DeltaTime, OutJustFinished, Result);
			if (!bMachinesTicked)
			{
				TickMachines(DeltaTime);
			}
			ComposeMachines(Result);
			UWidgetAnimLibrary::ApplyComposedResult(AnimatedWidget, Result, GetPlayerBaselines());
			UWidgetAnimLibrary::ReleaseFinishedTracks(AnimPlayer);
			for (FWidgetAnimMachineInstance& Instance : MachineInstances)
			{
				WidgetAnimStateMachines::ReleaseFinishedTracks(Instance);
				Instance.bPoseChanged = false;
			}
			NoteTracksAfterTick();
		}
	}
	else if (CanSkipTrackEvaluation(AnimatedWidget, DeltaTime))
	{
		OutJustFinished.Reset();
		INC_DWORD_STAT(STAT_WidgetAnimComponentsResting);
	}
	else
	{
		UWidgetAnimLibrary::TickWidgetAnimPlayer(AnimatedWidget, AnimPlayer, GetPlayerBaselines(), DeltaTime, OutJustFinished);
		NoteTracksAfterTick();
	}
	--PlayerTickDepth;

	if (PlayerTickDepth == 0 && bTextChangePending)
	{
		bTextChangePending = false;
		PlayForTextChange();
	}
	if (PlayerTickDepth == 0 && bParameterRefreshPending)
	{
		bParameterRefreshPending = false;
		RefreshPlayingParameters(nullptr, NAME_None);
	}
}

bool UWidgetAnimationComponent::PlayForTextChange()
{
	if (!AnimOwner.IsValid() || !bOwnerConstructed || !bAnyTextChangeTrigger)
	{
		return false;
	}

	// Leaving, or gone: a new text doesn't bring the widget back - showing it again does, by its own triggers.
	if (PendingExit != EPendingExit::None || AnimPlayer.Phase == EWidgetAnimPhase::PlayingOut || AnimPlayer.Phase == EWidgetAnimPhase::Hidden)
	{
		return false;
	}

	// The player's tracks can't be started over while a frame of them is being applied - a text set by an event the
	// reveal just fired, say. Played as soon as that frame is done instead, before it is drawn.
	if (PlayerTickDepth > 0)
	{
		bTextChangePending = true;
		return true;
	}

	EnsureRuntimeBaselines();

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };
	auto Answers = [](const FWidgetAnimEntry& Entry) { return Entry.bPlayOnTextChange; };
	const TFunctionRef<bool(const FWidgetAnimEntry&)> Filter(Answers);
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/false, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);

	// Only these entries, from their start; whatever else plays - a looping shimmer, an entrance still settling - goes on.
	// The values Set Widget Animation Parameter set here, for the animations this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	const int32 Started = UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		EffectiveInEntries,
		Resolver,
		TArray<FName>(),
		/*bIsOut=*/false,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true,
		&Filter,
		&AppendExtra);
	ParameterScopes.Reset();
	if (Started == 0)
	{
		return false;
	}

	AnimPlayer.Phase = EWidgetAnimPhase::PlayingIn;

	// Posed now, before the new text is ever drawn whole.
	ApplyCurrentAnimPose();
	return true;
}

bool UWidgetAnimationComponent::IsOnTextWidget() const
{
	// Bound at runtime; on the template the Details show, the text widget is what the component was made inside.
	const UWidget* Widget = AnimOwner.Get();
	if (!Widget)
	{
		Widget = Cast<UWidget>(GetOuter());
	}
	return Widget && Widget->Implements<UWidgetAnimTextHost>();
}

bool UWidgetAnimationComponent::PlayForTrigger(EEntryTrigger Trigger, bool bOut)
{
	if (!AnimOwner.IsValid())
	{
		return false;
	}

	EnsureRuntimeBaselines();

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };
	auto Answers = [Trigger](const FWidgetAnimEntry& Entry)
	{
		return Trigger == EEntryTrigger::Construct ? Entry.bPlayInAnimationOnConstruct : Entry.bPlayAnimationOnVisibilityHidden;
	};
	const TFunctionRef<bool(const FWidgetAnimEntry&)> Filter(Answers);

	const TArray<FWidgetAnimEntry>& Entries = bOut ? EffectiveOutEntries : EffectiveInEntries;

	// Nothing set to play on this trigger: leave whatever is playing alone, as if the trigger hadn't happened. The
	// unnamed layers alone still play on construct when there are no entries at all, as they always did.
	const bool bAnyEntryAnswers = Entries.ContainsByPredicate([&Answers](const FWidgetAnimEntry& Entry) { return Entry.IsUsable() && Answers(Entry); });
	const TArray<FWidgetAnimSettings>& Unnamed = bOut ? UnnamedAnimOut : UnnamedAnimIn;
	const bool bUnnamedOnly = !bAnyEntryAnswers && Trigger == EEntryTrigger::Construct && Entries.Num() == 0 && Unnamed.Num() > 0;

	// The state machines hear of it as their Closing input: their way to Exit, or back in through Entry - whether or not an
	// entry answers the trigger too.
	if (Trigger == EEntryTrigger::Visibility)
	{
		SetMachinesClosing(bOut);
	}

	if (!bAnyEntryAnswers && !bUnnamedOnly)
	{
		// Only a state machine's way to Exit plays: the hide waits for it all the same.
		if (Trigger == EEntryTrigger::Visibility && bOut && AreMachinesExiting())
		{
			AnimPlayer.Phase = EWidgetAnimPhase::PlayingOut;
			return true;
		}
		if (Trigger == EEntryTrigger::Visibility && !bOut && AnimPlayer.Phase != EWidgetAnimPhase::PlayingIn && AreMachinesRunning())
		{
			AnimPlayer.Phase = EWidgetAnimPhase::Shown;
		}
		return false;
	}

	if (!bOut)
	{
		// The entrance after all: whatever exit was waiting for a value is not going to happen.
		PendingExit = EPendingExit::None;
	}
	else if (!bStartingPendingExit && Trigger == EEntryTrigger::Visibility && PrepareValuesForExit(/*bCascade=*/true))
	{
		// A value-driven animation still has a way to go - a bar gliding to full - so the exit, and with it the hide
		// Set Visibility Animated deferred, waits for it. Playing Out already, so the widget counts as closing.
		PendingExit = EPendingExit::VisibilityTrigger;
		bPendingExitCascade = true;
		AnimPlayer.Phase = EWidgetAnimPhase::PlayingOut;
		return true;
	}

	// The same as Play In / Play Out from here on - the other direction's tracks dropped, the phase set, the pose
	// applied this frame and the children cascaded into - only with the entries this trigger doesn't play left out.
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	auto AppendLinked = [this, bOut](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, bOut, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
	// The values Set Widget Animation Parameter set here, for the animations this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		Entries,
		Resolver,
		TArray<FName>(),
		bOut,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true,
		&Filter,
		&AppendExtra);
	ParameterScopes.Reset();

	UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, UnnamedLayersSourceIndex, Unnamed, bOut, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

	AnimPlayer.Phase = bOut ? EWidgetAnimPhase::PlayingOut : EWidgetAnimPhase::PlayingIn;

	ApplyCurrentAnimPose();
	CascadeAnimationToChildren(/*bPlayingIn=*/!bOut);
	return true;
}

void UWidgetAnimationComponent::RefreshEntryTriggers()
{
	// The authored entries show their trigger settings; the effective copies are what gets asked whether any
	// entry - mirrored ones included - plays on a visibility change.
	const bool bOnText = IsOnTextWidget();
	for (FWidgetAnimEntry& Entry : AnimIn)
	{
		Entry.bShowConstructTrigger = true;
		Entry.bShowVisibilityTrigger = true;
		Entry.bShowTextChangeTrigger = bOnText;
	}
	for (FWidgetAnimEntry& Entry : AnimOut)
	{
		Entry.bShowConstructTrigger = false;
		Entry.bShowVisibilityTrigger = true;
		Entry.bShowTextChangeTrigger = false;
	}

	auto PlaysOnVisibility = [](const FWidgetAnimEntry& Entry) { return Entry.bPlayAnimationOnVisibilityHidden; };
	bAnyVisibilityTrigger = EffectiveInEntries.ContainsByPredicate(PlaysOnVisibility) || EffectiveOutEntries.ContainsByPredicate(PlaysOnVisibility);

	// Only an entrance plays on a new text; project defaults count too - they arrive as effective entries.
	bAnyTextChangeTrigger = bOnText && EffectiveInEntries.ContainsByPredicate([](const FWidgetAnimEntry& Entry) { return Entry.bPlayOnTextChange; });
}

void UWidgetAnimationComponent::MigrateTriggersIntoEntries()
{
	// Each is only saved when it differs from its default, so only a component that had changed it carries it into
	// its entries - and does again on every load until it is resaved, which is harmless: the result is the same.
	if (!bPlayInAnimationsOnConstruct_DEPRECATED)
	{
		for (FWidgetAnimEntry& Entry : AnimIn)
		{
			Entry.bPlayInAnimationOnConstruct = false;
		}
		bPlayInAnimationsOnConstruct_DEPRECATED = true;
	}

	if (bPlayAnimationsOnVisibilityHidden_DEPRECATED)
	{
		for (FWidgetAnimEntry& Entry : AnimIn)
		{
			Entry.bPlayAnimationOnVisibilityHidden = true;
		}
		for (FWidgetAnimEntry& Entry : AnimOut)
		{
			Entry.bPlayAnimationOnVisibilityHidden = true;
		}
		bPlayAnimationsOnVisibilityHidden_DEPRECATED = false;
	}
}

void UWidgetAnimationComponent::PlayIn(bool bCascadeToChildren)
{
	if (!AnimOwner.IsValid())
	{
		return;
	}

	EnsureRuntimeBaselines();

	// The entrance after all: whatever exit was waiting for a value is not going to happen.
	PendingExit = EPendingExit::None;

	// The state machines come back in through their Entry - and start, if they were waiting for Play In.
	SetMachinesClosing(false);
	StartMachines(/*bOnlyOnConstruct=*/false);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/false, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
	// The values Set Widget Animation Parameter set here, for the animations this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		EffectiveInEntries,
		Resolver,
		TArray<FName>(),
		/*bIsOut=*/false,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true,
		/*Filter=*/nullptr,
		&AppendExtra);
	ParameterScopes.Reset();

	UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, UnnamedLayersSourceIndex, UnnamedAnimIn, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

	AnimPlayer.Phase = EWidgetAnimPhase::PlayingIn;

	// Before the cascade, so a parent is already posed by the time its children pose themselves.
	ApplyCurrentAnimPose();

	if (bCascadeToChildren)
	{
		CascadeAnimationToChildren(/*bPlayingIn=*/true);
	}
}

void UWidgetAnimationComponent::PlayOut(bool bCascadeToChildren)
{
	if (!AnimOwner.IsValid())
	{
		return;
	}

	EnsureRuntimeBaselines();

	if (!bStartingPendingExit)
	{
		// Asked again while already waiting: the one waiting exit covers it.
		if (PendingExit != EPendingExit::None)
		{
			bPendingExitCascade |= bCascadeToChildren;
			return;
		}

		// A value-driven animation - here, or below when this cascades - still has a way to go: a loading bar gliding
		// to full, which Complete First has just sent there. The exit starts once it arrives (TickAnimationPlayback).
		// Playing Out already, so the widget counts as closing and a screen closing waits for it.
		if (PrepareValuesForExit(bCascadeToChildren))
		{
			PendingExit = EPendingExit::PlayOut;
			bPendingExitCascade = bCascadeToChildren;
			AnimPlayer.Phase = EWidgetAnimPhase::PlayingOut;
			return;
		}
	}

	// The state machines take their way to Exit.
	SetMachinesClosing(true);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	// The In tracks are dropped rather than left holding: an exit that has to fight the entrance's final
	// pose every frame never quite gets where it's going.
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/true, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
	// The values Set Widget Animation Parameter set here, for the animations this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		EffectiveOutEntries,
		Resolver,
		TArray<FName>(),
		/*bIsOut=*/true,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true,
		/*Filter=*/nullptr,
		&AppendExtra);
	ParameterScopes.Reset();

	UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, UnnamedLayersSourceIndex, UnnamedAnimOut, /*bIsOut=*/true, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

	AnimPlayer.Phase = EWidgetAnimPhase::PlayingOut;

	ApplyCurrentAnimPose();

	if (bCascadeToChildren)
	{
		CascadeAnimationToChildren(/*bPlayingIn=*/false);
	}
}

void UWidgetAnimationComponent::CascadeAnimationToChildren(bool bPlayingIn)
{
	ForEachNearestChildComponent(AnimOwner.Get(), [this, bPlayingIn](UWidgetAnimationComponent* Nested)
	{
		if (Nested == this)
		{
			return;
		}

		// Cascading stays on, so each level triggers its own children in turn.
		if (bPlayingIn)
		{
			Nested->PlayIn(/*bCascadeToChildren=*/true);
		}
		else
		{
			Nested->PlayOut(/*bCascadeToChildren=*/true);
		}
	});
}

bool UWidgetAnimationComponent::IsAnimClosing() const
{
	return AnimPlayer.Phase == EWidgetAnimPhase::PlayingOut || AnimPlayer.Phase == EWidgetAnimPhase::Hidden;
}

bool UWidgetAnimationComponent::IsPlayingInDirection(bool bIsOut) const
{
	for (const FWidgetAnimTrack& Track : AnimPlayer.Tracks)
	{
		if (!Track.bFinished && Track.bIsOut == bIsOut)
		{
			return true;
		}
	}

	return false;
}

bool UWidgetAnimationComponent::IsSettlingInDirection(bool bIsOut) const
{
	for (const FWidgetAnimTrack& Track : AnimPlayer.Tracks)
	{
		if (!Track.bSettled && Track.bIsOut == bIsOut)
		{
			return true;
		}
	}

	return false;
}

bool UWidgetAnimationComponent::TickAnimationPlayback(float DeltaTime)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return false;
	}

	TArray<FName> JustFinished;
	TickPlayer(AnimatedWidget, DeltaTime, JustFinished);

	// An exit waiting for a value to arrive is Playing Out with nothing of its own playing yet, which the check below
	// would take for an exit already over. It starts here, once every value it waits on has got where it was going.
	if (PendingExit != EPendingExit::None)
	{
		TryStartPendingExit();
		return false;
	}

	// Settled rather than playing: an endless loop is still playing, but it must not hold the entrance
	// (or the exit, and with it the deferred visibility and OnOutAnimationFinished) open forever.
	if (AnimPlayer.Phase == EWidgetAnimPhase::PlayingIn && !IsSettlingInDirection(/*bIsOut=*/false))
	{
		AnimPlayer.Phase = EWidgetAnimPhase::Shown;
	}
	else if (AnimPlayer.Phase == EWidgetAnimPhase::PlayingOut && !IsSettlingInDirection(/*bIsOut=*/true) && !AreMachinesExiting())
	{
		AnimPlayer.Phase = EWidgetAnimPhase::Hidden;
		HandleOutAnimationFinished();
		return true;
	}

	return false;
}

void UWidgetAnimationComponent::HandleOutAnimationFinished()
{
	if (bIsDeferringVisibility)
	{
		bIsDeferringVisibility = false;
		ApplyVisibilityToOwner(DeferredVisibility);
	}

	OnOutAnimationFinished.Broadcast(this);

	// Last: a listener may remove the owner from its parent (an info card does).
	OnOutAnimationFinishedNative.Broadcast(this);
}

// =====================================================================================================
// Value-driven tracks
// =====================================================================================================

bool UWidgetAnimationComponent::SetWidgetAnimationValue(FName ValueName, float Value, bool bSnap, bool bStartIfNotPlaying)
{
	return SetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromFloat(Value), bSnap, bStartIfNotPlaying);
}

bool UWidgetAnimationComponent::SetWidgetAnimationValueInteger(FName ValueName, int32 Value, bool bSnap, bool bStartIfNotPlaying)
{
	return SetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromFloat(static_cast<double>(Value)), bSnap, bStartIfNotPlaying);
}

bool UWidgetAnimationComponent::SetWidgetAnimationValueVector2D(FName ValueName, FVector2D Value, bool bSnap, bool bStartIfNotPlaying)
{
	return SetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromVector2D(Value), bSnap, bStartIfNotPlaying);
}

bool UWidgetAnimationComponent::SetWidgetAnimationValueColor(FName ValueName, FLinearColor Value, bool bSnap, bool bStartIfNotPlaying)
{
	return SetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), WidgetAnimValue::FromColor(Value), bSnap, bStartIfNotPlaying);
}

bool UWidgetAnimationComponent::SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying)
{
	if (Id.ValueName.IsNone())
	{
		return false;
	}

	// Nothing plays before the owner is constructed (or in the designer, unless a data preview plays there), so the value
	// is only remembered: the tracks start from it when they do start, rather than from their Start Value.
	if (!AnimOwner.IsValid() || !(bOwnerConstructed || bDesignTimePlayback))
	{
		UWidgetAnimLibrary::SetWidgetAnimTrackValue(AnimPlayer, Id, Value, /*bSnap=*/true);
		return false;
	}

	EnsureRuntimeBaselines();

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/false, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);

	// The unnamed layers too, when they take this value and aren't running - never restarted when they are, which
	// would play their clock tracks again.
	if (bStartIfNotPlaying && UWidgetAnimLibrary::HasValueLayer(UnnamedAnimIn, Id)
		&& !AnimPlayer.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return !Track.bIsOut && Track.SourceIndex == UnnamedLayersSourceIndex; }))
	{
		UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, UnnamedLayersSourceIndex, UnnamedAnimIn, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/false, /*bHoldWhenFinished=*/true);
	}

	// The values Set Widget Animation Parameter set here, for any entry this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	int32 Taken = UWidgetAnimEntryLibrary::SetEntriesValue(AnimPlayer, EffectiveInEntries, Resolver, Id, Value, bSnap, bStartIfNotPlaying, &AppendExtra);
	ParameterScopes.Reset();

	// The value tracks the state machines' states play take it too. A percentage state's progress is its Progress Input's.
	if (Id.ValueName != WidgetAnimValue::ProgressValueName)
	{
		for (FWidgetAnimMachineInstance& Instance : MachineInstances)
		{
			Taken += WidgetAnimStateMachines::SetTrackValue(Instance, Id, Value, bSnap);
		}
	}

	// Shown this frame: a snap, or a track just started at its value. The ticker takes it from here.
	ApplyCurrentAnimPose();
	return Taken > 0;
}

bool UWidgetAnimationComponent::SetWidgetAnimationProgress(FName AnimId, float Progress, bool bSnap, bool bStartIfNotPlaying)
{
	return SetWidgetAnimationValues(FWidgetAnimValueId::Progress(AnimId), WidgetAnimValue::FromFloat(Progress), bSnap, bStartIfNotPlaying);
}

float UWidgetAnimationComponent::GetWidgetAnimationProgress(FName AnimId, bool bTargetProgress) const
{
	return static_cast<float>(GetWidgetAnimationValues(FWidgetAnimValueId::Progress(AnimId), bTargetProgress).X);
}

FVector4 UWidgetAnimationComponent::GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const
{
	FVector4 Value(0.0, 0.0, 0.0, 0.0);
	UWidgetAnimLibrary::GetWidgetAnimTrackValue(AnimPlayer, Id, bTargetValue, Value);
	return Value;
}

float UWidgetAnimationComponent::GetWidgetAnimationValue(FName ValueName, bool bTargetValue) const
{
	return static_cast<float>(GetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), bTargetValue).X);
}

int32 UWidgetAnimationComponent::GetWidgetAnimationValueInteger(FName ValueName, bool bTargetValue) const
{
	return FMath::RoundToInt32(GetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), bTargetValue).X);
}

FVector2D UWidgetAnimationComponent::GetWidgetAnimationValueVector2D(FName ValueName, bool bTargetValue) const
{
	const FVector4 Value = GetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), bTargetValue);
	return FVector2D(Value.X, Value.Y);
}

FLinearColor UWidgetAnimationComponent::GetWidgetAnimationValueColor(FName ValueName, bool bTargetValue) const
{
	const FVector4 Value = GetWidgetAnimationValues(FWidgetAnimValueId::Anywhere(ValueName), bTargetValue);
	return FLinearColor(static_cast<float>(Value.X), static_cast<float>(Value.Y), static_cast<float>(Value.Z), static_cast<float>(Value.W));
}

bool UWidgetAnimationComponent::HasWidgetAnimationValue(const FWidgetAnimValueId& Id) const
{
	if (Id.ValueName.IsNone())
	{
		return false;
	}

	// A percentage animation's progress: an entry with the Anim ID playing one.
	if (Id.ValueName == WidgetAnimValue::ProgressValueName)
	{
		for (const TArray<FWidgetAnimEntry>* List : { &AnimIn, &AnimOut })
		{
			for (const FWidgetAnimEntry& Candidate : *List)
			{
				if (Candidate.IsPercentAnimation() && (Id.AnimId.IsNone() || Candidate.AnimId == Id.AnimId) && (Id.IsAnywhere() || Candidate.Animation.Get() == Id.Animation))
				{
					return true;
				}
			}
		}
		return false;
	}

	// An animation inserted as a track's value tracks too, whose values belong to the animation they play in.
	auto TakesIt = [&Id](const TArray<FWidgetAnimLayer>& Tracks, const UWidgetAnimPresetAsset* Scope)
	{
		bool bTakes = false;
		UWidgetAnimLibrary::ForEachLayerIncludingInserted(Tracks, Scope, [&Id, Scope, &bTakes](const UWidgetAnimPresetAsset*, const FWidgetAnimLayer& Track)
		{
			bTakes = bTakes || (Track.IsValueDriven() && Id.Matches(Scope, Track.ValueName));
		});
		return bTakes;
	};

	for (const TArray<FWidgetAnimEntry>* List : { &AnimIn, &AnimOut })
	{
		for (const FWidgetAnimEntry& Candidate : *List)
		{
			if (const UWidgetAnimPresetAsset* Animation = Candidate.Animation.Get())
			{
				if (TakesIt(Animation->Layers, Animation))
				{
					return true;
				}
			}
		}
	}

	// Additive tracks belong to no animation: their scope is none.
	return TakesIt(AdditiveAnimLayersIn, nullptr) || TakesIt(AdditiveAnimLayersOut, nullptr);
}

bool UWidgetAnimationComponent::HasExitToPlay() const
{
	return FlattenedOut.Num() > 0 || PendingExit != EPendingExit::None || UWidgetAnimLibrary::IsAnyValueLayerHoldingExit(AnimPlayer)
		|| MachineInstances.ContainsByPredicate([](const FWidgetAnimMachineInstance& Instance) { return Instance.bStarted && Instance.Machine && Instance.Machine->HasExitPath(); });
}

void UWidgetAnimationComponent::ApplyValueOverrides(TArray<FWidgetAnimSettings>& Settings) const
{
	if (ValueOverrides.Num() == 0)
	{
		return;
	}

	// An animation track following a value (see FWidgetAnimProgressDrive) is overridden too: one copy of its drive with
	// this widget's settings, handed to every layer that shares it.
	TMap<const FWidgetAnimProgressDrive*, TSharedPtr<FWidgetAnimProgressDrive>> OverriddenDrives;
	for (FWidgetAnimSettings& Layer : Settings)
	{
		const FWidgetAnimProgressDrive* Drive = Layer.ProgressDrive.Get();
		if (!Drive || Drive->ValueName == WidgetAnimValue::ProgressValueName)
		{
			continue;
		}

		if (const TSharedPtr<FWidgetAnimProgressDrive>* Done = OverriddenDrives.Find(Drive))
		{
			Layer.ProgressDrive = *Done;
			continue;
		}

		const FWidgetAnimValueOverride* DriveRow = ValueOverrides.FindByPredicate([&Layer, Drive](const FWidgetAnimValueOverride& Candidate)
		{
			return Candidate.bOverride && Candidate.ValueName == Drive->ValueName && Candidate.Animation.Get() == Layer.ValueScope.Get();
		});
		if (DriveRow)
		{
			TSharedPtr<FWidgetAnimProgressDrive> Copy = MakeShared<FWidgetAnimProgressDrive>(*Drive);
			Copy->Options = DriveRow->Options;
			Copy->Options.Kind = EWidgetAnimValueKind::Float;
			OverriddenDrives.Add(Drive, Copy);
			Layer.ProgressDrive = Copy;
		}
	}

	for (FWidgetAnimSettings& Layer : Settings)
	{
		if (!Layer.IsValueDriven())
		{
			continue;
		}

		const FWidgetAnimValueOverride* Row = ValueOverrides.FindByPredicate([&Layer](const FWidgetAnimValueOverride& Candidate)
		{
			return Candidate.bOverride && Candidate.ValueName == Layer.ValueName && Candidate.Animation.Get() == Layer.ValueScope.Get();
		});
		if (Row)
		{
			// Everything but the kind, which is the track's: a row shared by tracks of two kinds still leaves each
			// reading the fields of its own.
			const EWidgetAnimValueKind TrackKind = Layer.ValueOptions.Kind;
			Layer.ValueOptions = Row->Options;
			Layer.ValueOptions.Kind = TrackKind;
		}
	}
}

void UWidgetAnimationComponent::PrepareEntrySettings(int32 EffectiveIndex, bool bOut, TArray<FWidgetAnimSettings>& EntrySettings) const
{
	AppendLinkedAdditive(EffectiveIndex, bOut, EntrySettings, 0);
	ApplyValueOverrides(EntrySettings);
}

bool UWidgetAnimationComponent::RefreshValueOverrides()
{
	// Every value-driven track's defaults, by animation and name, in the order first found: the entries in both lists,
	// then the additive tracks.
	struct FFoundValue
	{
		UObject* Animation = nullptr;
		FName ValueName;
		FWidgetAnimValueOptions Defaults;
		FInstancedStruct ValueInput;
	};
	TArray<FFoundValue> Found;
	// An animation inserted into these as a track has its value tracks gathered too: they belong to the one it plays in.
	auto Gather = [&Found](UWidgetAnimPresetAsset* Animation, const TArray<FWidgetAnimLayer>& Layers)
	{
		UWidgetAnimLibrary::ForEachLayerIncludingInserted(Layers, Animation, [&Found, Animation](const UWidgetAnimPresetAsset*, const FWidgetAnimLayer& Layer)
		{
			if (!Layer.IsValueDriven() || Layer.ValueName.IsNone())
			{
				return;
			}
			if (FFoundValue* Existing = Found.FindByPredicate([Animation, &Layer](const FFoundValue& Value) { return Value.Animation == Animation && Value.ValueName == Layer.ValueName; }))
			{
				// Tracks sharing a name share one value, fed by the first of them with an input - as it plays.
				if (!Existing->ValueInput.IsValid() && Layer.ValueInput.IsValid())
				{
					Existing->ValueInput = Layer.ValueInput;
				}
				return;
			}

			FFoundValue& Value = Found.AddDefaulted_GetRef();
			Value.Animation = Animation;
			Value.ValueName = Layer.ValueName;
			Value.Defaults = Layer.ValueOptions;
			Value.Defaults.Kind = Layer.GetEffectiveValueKind();
			Value.ValueInput = Layer.ValueInput;
		});
	};
	for (const TArray<FWidgetAnimEntry>* Entries : { &AnimIn, &AnimOut })
	{
		for (const FWidgetAnimEntry& Entry : *Entries)
		{
			if (Entry.Animation)
			{
				Gather(Entry.Animation, Entry.Animation->Layers);
			}
		}
	}
	Gather(nullptr, AdditiveAnimLayersIn);
	Gather(nullptr, AdditiveAnimLayersOut);

	TArray<FWidgetAnimValueOverride> Rows;
	Rows.Reserve(Found.Num());
	for (const FFoundValue& Value : Found)
	{
		const FWidgetAnimValueOverride* Existing = ValueOverrides.FindByPredicate([&Value](const FWidgetAnimValueOverride& Row)
		{
			return Row.ValueName == Value.ValueName && Row.Animation.Get() == Value.Animation;
		});

		FWidgetAnimValueOverride& Row = Rows.AddDefaulted_GetRef();
		Row.Animation = Value.Animation;
		Row.Source = Value.Animation ? Value.Animation->GetName() : FString(TEXT("Additive Tracks"));
		Row.ValueName = Value.ValueName;
		Row.Kind = Value.Defaults.Kind;
		Row.ValueInput = Value.ValueInput;
		Row.bOverride = Existing && Existing->bOverride;

		// Overridden: this widget's settings, kept. Not: the track's own, so the row always shows what will be used.
		Row.Options = Row.bOverride ? Existing->Options : Value.Defaults;
		Row.Options.Kind = Value.Defaults.Kind;
		Row.Options.bCanEditKind = false;
	}

	const bool bChanged = Rows.Num() != ValueOverrides.Num() || !Algo::Compare(Rows, ValueOverrides, [](const FWidgetAnimValueOverride& A, const FWidgetAnimValueOverride& B)
	{
		return A.ValueName == B.ValueName && A.Animation == B.Animation && A.Source == B.Source && A.Kind == B.Kind && A.bOverride == B.bOverride
			&& A.ValueInput == B.ValueInput
			&& FWidgetAnimValueOptions::StaticStruct()->CompareScriptStruct(&A.Options, &B.Options, PPF_None);
	});

	if (bChanged)
	{
		ValueOverrides = MoveTemp(Rows);
	}
	return bChanged;
}

bool UWidgetAnimationComponent::AssignAdditiveValueNames()
{
	// One scope for both lists - Additive Tracks - so a name is unique across them.
	TSet<FName> Taken;
	for (const FWidgetAnimLayer& Layer : AdditiveAnimLayersOut)
	{
		if (Layer.IsValueDriven() && !Layer.ValueName.IsNone())
		{
			Taken.Add(Layer.ValueName);
		}
	}
	const bool bInChanged = UWidgetAnimLibrary::AssignDefaultValueNames(AdditiveAnimLayersIn, &Taken);
	const bool bOutChanged = UWidgetAnimLibrary::AssignDefaultValueNames(AdditiveAnimLayersOut, &Taken);
	return bInChanged || bOutChanged;
}

bool UWidgetAnimationComponent::PrepareValuesForExit(bool bCascade)
{
	bool bWaiting = UWidgetAnimLibrary::PrepareValueLayersForExit(AnimPlayer);

	// Down the same walk the exit cascades along, so a loading bar in a nested widget holds its screen's exit too -
	// the screen fades once the bar is full, not while it is still filling. Every one is asked, not just until the
	// first that waits: each Complete First has to be sent to 1 now, so it is on its way while the others arrive.
	if (bCascade)
	{
		ForEachNearestChildComponent(AnimOwner.Get(), [this, &bWaiting](UWidgetAnimationComponent* Nested)
		{
			if (Nested != this && Nested->bOwnerConstructed)
			{
				const bool bNestedWaiting = Nested->PrepareValuesForExit(/*bCascade=*/true);
				bWaiting = bWaiting || bNestedWaiting;
			}
		});
	}

	return bWaiting;
}

bool UWidgetAnimationComponent::TryStartPendingExit()
{
	if (PendingExit == EPendingExit::None)
	{
		return false;
	}

	if (PrepareValuesForExit(bPendingExitCascade))
	{
		return false;
	}

	const EPendingExit Exit = PendingExit;
	PendingExit = EPendingExit::None;

	TGuardValue<bool> Guard(bStartingPendingExit, true);
	if (Exit == EPendingExit::PlayOut)
	{
		PlayOut(bPendingExitCascade);
	}
	else if (!PlayForTrigger(EEntryTrigger::Visibility, /*bOut=*/true))
	{
		// Nothing answers the trigger anymore (the entries were changed while it waited): the exit is simply over.
		AnimPlayer.Phase = EWidgetAnimPhase::Hidden;
		HandleOutAnimationFinished();
	}

	return true;
}

// =====================================================================================================
// Visibility
// =====================================================================================================

void UWidgetAnimationComponent::SetVisibilityAnimated(ESlateVisibility InVisibility)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return;
	}

	if (ReactsToVisibility() && !bPlayOnlyOnConstructAndDestruct && bOwnerConstructed)
	{
		const bool bIsBecomingInvisible = !IsVisibleState(InVisibility);
		const bool bWasVisible = IsVisibleState(AnimatedWidget->GetVisibility());

		// Hide after the exit has played - the Anim Out entries set to play on visibility. With none, it hides now.
		if (bIsBecomingInvisible && bWasVisible && !IsAnimClosing())
		{
			bIsDeferringVisibility = true;
			DeferredVisibility = InVisibility;
			if (PlayForTrigger(EEntryTrigger::Visibility, /*bOut=*/true))
			{
				return;
			}
			bIsDeferringVisibility = false;
		}

		// Show now and play the entrance - the Anim In entries set to play on visibility.
		if (!bIsBecomingInvisible && !bWasVisible)
		{
			bIsDeferringVisibility = false;
			ApplyVisibilityToOwner(InVisibility);
			bLastSeenVisible = true;
			PlayForTrigger(EEntryTrigger::Visibility, /*bOut=*/false);
			return;
		}
	}

	bIsDeferringVisibility = false;
	ApplyVisibilityToOwner(InVisibility);
}

void UWidgetAnimationComponent::ApplyVisibilityToOwner(ESlateVisibility InVisibility)
{
	if (UWidget* AnimatedWidget = AnimOwner.Get())
	{
		// Guarded so an owner whose SetVisibility routes into SetVisibilityAnimated passes this call through.
		TGuardValue<bool> Guard(bApplyingVisibility, true);
		AnimatedWidget->SetVisibility(InVisibility);
		bLastSeenVisible = IsVisibleState(InVisibility);
	}
}

// =====================================================================================================
// Play by ID
// =====================================================================================================

void UWidgetAnimationComponent::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	if (!AnimOwner.IsValid())
	{
		return;
	}

	// Reported, not refused: playing every entry that shares the ID is what this has always done.
	ReportConflictsIn(AnimIds);

	EnsureRuntimeBaselines();

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	// Phase is deliberately untouched: a named one-shot is not the widget appearing or closing. An additive list
	// that starts within an entry plays with it, whatever called for it.
	auto AppendLinked = [this, bReverse](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, bReverse, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
	// The values Set Widget Animation Parameter set here, for the animations this starts.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		bReverse ? EffectiveOutEntries : EffectiveInEntries,
		Resolver,
		AnimIds,
		bReverse,
		PlaybackSpeed,
		bRestartIfPlaying,
		bHoldFinalPose,
		/*Filter=*/nullptr,
		&AppendExtra);
	ParameterScopes.Reset();

	// Additive layers that start right away, and extra layers, have no Anim ID, so they only join a "play everything" call.
	if (AnimIds.Num() == 0)
	{
		const TArray<FWidgetAnimSettings>& Unnamed = bReverse ? UnnamedAnimOut : UnnamedAnimIn;
		UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, UnnamedLayersSourceIndex, Unnamed, bReverse, PlaybackSpeed, bRestartIfPlaying, bHoldFinalPose);
	}

	ApplyCurrentAnimPose();
}

void UWidgetAnimationComponent::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	ReportConflictsIn(AnimIds);

	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, AnimIds, /*bRemoveTracks=*/bResetToStart);

	if (bResetToStart)
	{
		// Every widget back to its authored pose, then the tracks still playing re-applied over it this frame. The
		// authored pose is in AnimBaselines when Preserve Authored Transforms is on, ResetPoseBaselines when it isn't.
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines.bCaptured ? AnimBaselines : ResetPoseBaselines);
		ApplyCurrentAnimPose();
	}
}

bool UWidgetAnimationComponent::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	ReportConflictsIn(AnimIds);
	return UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(AnimPlayer, AnimIds);
}

void UWidgetAnimationComponent::EndLoopingAnimation(FName AnimId, bool bSoftSettle)
{
	if (!AnimOwner.IsValid() || AnimId.IsNone())
	{
		return;
	}

	// Ambiguous: ending one of several same-named animations and not the others is never what was meant.
	if (ConflictingAnimIds.Contains(AnimId))
	{
		ReportAnimIdConflict(AnimId);
		return;
	}

	const bool bAnyAffected = UWidgetAnimLibrary::EndWidgetAnimTrackLoops(AnimPlayer, AnimId, bSoftSettle);

	// A soft settle carries on under the normal clock. A hard end is posed now, so the jump to the end
	// pose is on this frame rather than the next.
	if (bAnyAffected && !bSoftSettle)
	{
		ApplyCurrentAnimPose();
	}
}

// =====================================================================================================
// Designer preview
// =====================================================================================================

void UWidgetAnimationComponent::NotifyOwnerDesignTimeRefresh()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimComponentDesignTimeRefresh);
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !AnimatedWidget->IsDesignTime())
	{
		return;
	}

	// The state machine preview starts again too, when the panel asks for it - the widget it posed may be a new one.
	if (bMachinePreviewing)
	{
		StopDesignerStateMachinePreview();
	}

	// A data preview's playing ends: whoever plays by name again starts it again, from the pose read below.
	if (bDesignTimePlayback)
	{
		bDesignTimePlayback = false;
		StopRuntimeTicking();
		UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
		AnimPlayer.Phase = EWidgetAnimPhase::Idle;
	}

	// Put back whatever a previous preview left before reading the authored pose again.
	if (AnimBaselines.bCaptured)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines);
	}
	AnimBaselines.ResetBaselines();

	RebuildEffectiveAnimations();

	// The toolbar's copies follow the stacks they are stamped from.
	if (DesignerPreviewModeOverride.IsSet())
	{
		RebuildDesignerOverrideStacks();
	}

	// Always captured at design time: Off has to be able to put everything back exactly.
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, FlattenedIn, FlattenedOut, AnimBaselines);

	// Fresh state: the preview starts again from the top whenever the designer rebuilds the widget.
	DesignerPreviewState = FWidgetAnimPreviewState();

	if (GetEffectiveDesignerPreview().Mode == EWidgetAnimPreviewMode::Off)
	{
		StopDesignerPreview();
		return;
	}

	PreviewRetriesLeft = PreviewTimerRetryFrames;
	EnsureDesignerPreviewTimer();
}

void UWidgetAnimationComponent::BeginDesignTimePlayback()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (bDesignTimePlayback || bOwnerConstructed || !AnimatedWidget || !AnimatedWidget->IsDesignTime())
	{
		return;
	}

	// The toolbar's preview poses the same widgets: it gives way, as it does to the state machine preview.
	if (DesignerPreviewModeOverride.IsSet() || PreviewTimerHandle.IsValid())
	{
		StopDesignerPreview();
	}

	bDesignTimePlayback = true;
	if (EffectiveInEntries.Num() == 0 && EffectiveOutEntries.Num() == 0)
	{
		RebuildEffectiveAnimations();
	}

	// The designer captured the authored pose already (NotifyOwnerDesignTimeRefresh): this keeps it.
	EnsureRuntimeBaselines();
	StartRuntimeTicking();
}

void UWidgetAnimationComponent::EnsureDesignerPreviewTimer()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !AnimatedWidget->IsDesignTime() || GetEffectiveDesignerPreview().Mode == EWidgetAnimPreviewMode::Off)
	{
		return;
	}

	if (PreviewTimerHandle.IsValid() && PreviewTimerWidget.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> SlateWidget = AnimatedWidget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		// The owner's Slate widget is built after its properties are synchronized. Try again next frame.
		if (!PreviewRetryHandle.IsValid() && PreviewRetriesLeft > 0)
		{
			PreviewRetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
			{
				PreviewRetryHandle.Reset();
				--PreviewRetriesLeft;
				EnsureDesignerPreviewTimer();
				return false;
			}));
		}
		return;
	}

	// An active timer rather than the core ticker: it is what tells the designer viewport to keep
	// repainting while the preview runs.
	TSharedRef<FActiveTimerHandle> Handle = SlateWidget->RegisterActiveTimer(0.f, 
		FWidgetActiveTimerDelegate::CreateWeakLambda(this,
		[this](double /*CurrentTime*/, float DeltaTime) -> EActiveTimerReturnType
		{
			UWidget* PreviewOwner = AnimOwner.Get();
			if (!PreviewOwner)
			{
				return EActiveTimerReturnType::Stop;
			}

			const bool bNeedsMoreFrames = UWidgetAnimLibrary::TickWidgetAnimPreview(
				PreviewOwner, GetDesignerPreviewIn(), GetDesignerPreviewOut(), DesignerPreviewState, GetEffectiveDesignerPreview(), AnimBaselines, DeltaTime);

			return bNeedsMoreFrames ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
		}));

	PreviewTimerHandle = Handle;
	PreviewTimerWidget = SlateWidget;
}

FWidgetAnimPreviewSettings UWidgetAnimationComponent::GetEffectiveDesignerPreview() const
{
	// The saved DesignerPreview is no longer used: Off unless the toolbar is previewing, at its defaults.
	FWidgetAnimPreviewSettings Settings;
	if (DesignerPreviewModeOverride.IsSet())
	{
		Settings.Mode = DesignerPreviewModeOverride.GetValue();
		Settings.PlaybackSpeed = FMath::Max(DesignerPreviewOptions.Speed, 0.01f);
		Settings.ScrubAlpha = DesignerPreviewScrubAlpha;
		Settings.bScrubOutAnimation = bDesignerPreviewScrubOut;
	}
	return Settings;
}

void UWidgetAnimationComponent::RebuildDesignerOverrideStacks()
{
	// The toolbar's choices replace each entry's own loop and ping-pong, as one use of the animation would;
	// Invert flips whatever direction the entry already runs in, so an inverted entry plays forward again.
	auto Stamp = [this](const TArray<FWidgetAnimSettings>& Source, TArray<FWidgetAnimSettings>& Out)
	{
		Out = Source;
		for (FWidgetAnimSettings& Layer : Out)
		{
			Layer.bLoop = DesignerPreviewOptions.bLoop;
			Layer.bPingPong = DesignerPreviewOptions.bPingPong;
			Layer.LoopCount = 0;
			Layer.bInvertAnimation = Layer.bInvertAnimation != DesignerPreviewOptions.bInvert;
		}
	};

	Stamp(FlattenedIn, DesignerOverrideIn);
	Stamp(FlattenedOut, DesignerOverrideOut);
}

void UWidgetAnimationComponent::PlayDesignerPreviewOverride(bool bOut, const FWidgetAnimDesignerPlayback& Options)
{
	// The exit is only ever seen after the entrance has settled, so Play Out plays from the settled pose.
	// Looping is the stamped layers' own - endless, with Ping Pong if asked - so the mode is always Once.
	// The toolbar's preview and the state machine preview pose the same widgets: the one asked for last plays.
	if (bMachinePreviewing)
	{
		StopDesignerStateMachinePreview();
	}

	DesignerPreviewOptions = Options;
	DesignerPreviewModeOverride = bOut ? EWidgetAnimPreviewMode::PlayOut : EWidgetAnimPreviewMode::PlayIn;
	bDesignerPreviewScrubOut = bOut;

	// From the top: the running timer dropped, the widget put back as authored and read again, fresh state -
	// and the stamped stacks rebuilt from what was read.
	StopDesignerPreview();
	NotifyOwnerDesignTimeRefresh();
}

void UWidgetAnimationComponent::ScrubDesignerPreviewOverride(float Alpha, const FWidgetAnimDesignerPlayback& Options)
{
	if (bMachinePreviewing)
	{
		StopDesignerStateMachinePreview();
	}

	const bool bWasDriving = DesignerPreviewModeOverride.IsSet();

	DesignerPreviewOptions = Options;
	DesignerPreviewScrubAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	DesignerPreviewModeOverride = EWidgetAnimPreviewMode::Scrub;

	if (!bWasDriving)
	{
		// First touch of the toolbar on this widget: set up as Play would, then freeze.
		StopDesignerPreview();
		NotifyOwnerDesignTimeRefresh();
		return;
	}

	// Already driving: the player sees the new mode or alpha on its next frame and poses it. Only its timer
	// needs waking - a scrub poses one frame and stops asking for more.
	RebuildDesignerOverrideStacks();
	EnsureDesignerPreviewTimer();
}

void UWidgetAnimationComponent::UpdateDesignerPreviewOptions(const FWidgetAnimDesignerPlayback& Options)
{
	if (!DesignerPreviewModeOverride.IsSet())
	{
		return;
	}

	DesignerPreviewOptions = Options;
	RebuildDesignerOverrideStacks();

	// A frozen scrub re-poses on the next frame only if asked; a playing preview simply carries on with it.
	DesignerPreviewState.LastScrubAlpha = -1.f;
	EnsureDesignerPreviewTimer();
}

void UWidgetAnimationComponent::ResetDesignerPreviewOverride()
{
	// Off: put back as authored, and held there - until played again, or until the designer rebuilds the
	// widget, which drops the override and hands the preview back to Designer Preview.
	DesignerPreviewModeOverride = EWidgetAnimPreviewMode::Off;
	DesignerPreviewScrubAlpha = 0.f;
	bDesignerPreviewScrubOut = false;
	StopDesignerPreview();
	NotifyOwnerDesignTimeRefresh();
}

bool UWidgetAnimationComponent::IsDesignerPreviewShowingOut() const
{
	if (GetEffectiveDesignerPreview().Mode == EWidgetAnimPreviewMode::Scrub)
	{
		return bDesignerPreviewScrubOut;
	}

	const EWidgetAnimPhase Phase = DesignerPreviewState.Playback.Phase;
	return Phase == EWidgetAnimPhase::PlayingOut || Phase == EWidgetAnimPhase::Hidden;
}

float UWidgetAnimationComponent::GetDesignerPreviewDuration() const
{
	// One pass, loops ignored: what a looping preview walks along and starts again.
	return UWidgetAnimLibrary::GetStackPassDuration(IsDesignerPreviewShowingOut() ? GetDesignerPreviewOut() : GetDesignerPreviewIn());
}

float UWidgetAnimationComponent::GetDesignerPreviewSeconds() const
{
	const float Duration = GetDesignerPreviewDuration();

	// A scrub never moves the playing clock - it poses a frame directly - so the readout is the scrub.
	if (GetEffectiveDesignerPreview().Mode == EWidgetAnimPreviewMode::Scrub)
	{
		return DesignerPreviewScrubAlpha * Duration;
	}

	const bool bShowingOut = IsDesignerPreviewShowingOut();
	const float RawElapsed = bShowingOut
		? UWidgetAnimLibrary::GetStackElapsed(GetDesignerPreviewOut(), DesignerPreviewState.Playback.OutStates)
		: UWidgetAnimLibrary::GetStackElapsed(GetDesignerPreviewIn(), DesignerPreviewState.Playback.InStates);

	// Looping, it is reported against one pass - up to the end and round again, or back down with Ping Pong.
	float Position = FMath::Max(RawElapsed, 0.f);
	if (DesignerPreviewModeOverride.IsSet() && DesignerPreviewOptions.bLoop && Duration > 0.f)
	{
		const float Repeat = DesignerPreviewOptions.bPingPong ? Duration * 2.f : Duration;
		const float InRepeat = FMath::Fmod(Position, Repeat);
		Position = InRepeat > Duration ? Repeat - InRepeat : InRepeat;
	}

	// The exit is the entrance played backwards; the readout is where the pose is, walking back to the start.
	if (bShowingOut && Duration > 0.f)
	{
		Position = Duration - Position;
	}

	return FMath::Clamp(Position, 0.f, FMath::Max(Duration, 0.f));
}

float UWidgetAnimationComponent::GetDesignerPreviewAlpha() const
{
	const float Duration = GetDesignerPreviewDuration();
	return Duration > KINDA_SMALL_NUMBER ? GetDesignerPreviewSeconds() / Duration : 0.f;
}

bool UWidgetAnimationComponent::IsDesignerPreviewPlaying() const
{
	if (!DesignerPreviewModeOverride.IsSet())
	{
		return false;
	}

	const EWidgetAnimPreviewMode Mode = DesignerPreviewModeOverride.GetValue();
	return (Mode == EWidgetAnimPreviewMode::PlayIn || Mode == EWidgetAnimPreviewMode::PlayOut)
		&& PreviewTimerHandle.IsValid();
}

void UWidgetAnimationComponent::StopDesignerPreview()
{
	if (PreviewRetryHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreviewRetryHandle);
		PreviewRetryHandle.Reset();
	}

	TSharedPtr<FActiveTimerHandle> Handle = PreviewTimerHandle.Pin();
	TSharedPtr<SWidget> Widget = PreviewTimerWidget.Pin();
	if (Handle.IsValid() && Widget.IsValid())
	{
		Widget->UnRegisterActiveTimer(Handle.ToSharedRef());
	}

	PreviewTimerHandle.Reset();
	PreviewTimerWidget.Reset();
}

// =====================================================================================================
// Migration
// =====================================================================================================

void UWidgetAnimationComponent::ImportLegacySettings(const FWidgetAnimationLegacySettings& Legacy, const FWidgetAnimationLegacySettings* ArchetypeLegacy)
{
	const auto Differs = [ArchetypeLegacy](bool bValue, bool FWidgetAnimationLegacySettings::* Member)
	{
		return !ArchetypeLegacy || bValue != ArchetypeLegacy->*Member;
	};

	if (Differs(Legacy.bPlayOnlyOnConstructAndDestruct, &FWidgetAnimationLegacySettings::bPlayOnlyOnConstructAndDestruct))
	{
		bPlayOnlyOnConstructAndDestruct = Legacy.bPlayOnlyOnConstructAndDestruct;
	}

	// Carried by the deprecated component settings into the entries, below, once the entries are in.
	if (Differs(Legacy.bPlayInAnimationsOnConstruct, &FWidgetAnimationLegacySettings::bPlayInAnimationsOnConstruct))
	{
		bPlayInAnimationsOnConstruct_DEPRECATED = Legacy.bPlayInAnimationsOnConstruct;
	}

	if (Differs(Legacy.bPlayAnimationsOnVisibilityHidden, &FWidgetAnimationLegacySettings::bPlayAnimationsOnVisibilityHidden))
	{
		bPlayAnimationsOnVisibilityHidden_DEPRECATED = Legacy.bPlayAnimationsOnVisibilityHidden;
	}

	if (Differs(Legacy.bPreserveAuthoredTransforms, &FWidgetAnimationLegacySettings::bPreserveAuthoredTransforms))
	{
		bPreserveAuthoredTransforms = Legacy.bPreserveAuthoredTransforms;
	}

	if (!ArchetypeLegacy || !AreStructArraysIdentical(Legacy.AnimIn, ArchetypeLegacy->AnimIn))
	{
		AnimIn = Legacy.AnimIn;
	}

	if (!ArchetypeLegacy || !AreStructArraysIdentical(Legacy.AnimOut, ArchetypeLegacy->AnimOut))
	{
		AnimOut = Legacy.AnimOut;
	}

	if (!ArchetypeLegacy || !AreStructsIdentical(Legacy.DesignerPreview, ArchetypeLegacy->DesignerPreview))
	{
		DesignerPreview = Legacy.DesignerPreview;
	}

	MigrateTriggersIntoEntries();
	RefreshEntryTriggers();
}

bool UWidgetAnimationComponent::ShouldDiffLegacyAgainstArchetype(const UObject* Archetype)
{
	if (!Archetype || Archetype->GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return false;
	}

	return Archetype->GetLinkerCustomVersion(FWidgetAnimationComponentVersion::GUID) >= FWidgetAnimationComponentVersion::MovedIntoComponent;
}

void UWidgetAnimationComponent::PostLoad()
{
	Super::PostLoad();
	MigrateTriggersIntoEntries();
	SyncEntryGroupTargets();
	RefreshEntryTriggers();
	RefreshAdditiveStarts();
	AssignAdditiveValueNames();
	RefreshValueOverrides();
	SyncStateMachines();
	SyncComponentParameters();
}

bool UWidgetAnimationComponent::SyncEntryGroupTargets()
{
	bool bChanged = false;

	// New rows are checked against what this widget's tree has - see FWidgetAnimEntry::SyncGroupTargets.
	const TArray<FString> TargetOptions = GetAnimTargetWidgetOptions();

	for (FWidgetAnimEntry& Entry : AnimIn)
	{
		bChanged |= Entry.SyncGroupTargets(&TargetOptions);
	}

	for (FWidgetAnimEntry& Entry : AnimOut)
	{
		bChanged |= Entry.SyncGroupTargets(&TargetOptions);
	}

	return bChanged;
}

#if WITH_EDITOR
void UWidgetAnimationComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimComponentPostEditChange);
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// An animation picked, swapped or cleared: its Target Widgets get a row per Track Group. Only once the
	// edit is committed - resizing an array under a drag in progress would pull the row out from under it.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		SyncEntryGroupTargets();
	}

	// A new entry shows its trigger settings, and a changed one is heard by the visibility watch.
	RefreshEntryTriggers();

	// An entry an additive list starts within moved, or went: the list follows it, or starts right away again.
	RefreshAdditiveStarts();

	// An animation picked or swapped, or an additive track made value-driven: Value Tracks lists what is used now.
	// Only once the edit is committed, like the Target Widgets above.
	if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		AssignAdditiveValueNames();
		RefreshValueOverrides();

		// A machine picked or swapped: its Target Widgets and Inputs get a row each.
		SyncStateMachines();

		// An animation or machine picked, swapped or cleared: Parameters lists what is played now.
		SyncComponentParameters();
	}

	// One of its own Parameters set or changed: what is playing takes it at once, as with Set Widget Animation Parameter.
	if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UWidgetAnimationComponent, ComponentParameters))
	{
		RefreshPlayingParameters(nullptr, NAME_None);
	}

	// Keeps each Transform layer's Keys list one-to-one with its curve: a key added on the curve gets an
	// entry, a deleted one loses it, and every entry's time follows its key. Deferred and skipped mid-drag -
	// see RequestSyncTransformAnims - so the Details panel is never rebuilt under an edit in progress.
	UWidgetAnimTransformLibrary::RequestSyncTransformAnims(this, PropertyChangedEvent);
}
#endif
// =====================================================================================================
// State machines
// =====================================================================================================

const FWidgetAnimMachineContext& UWidgetAnimationComponent::GetMachineContext()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	MachineContext.Host = AnimatedWidget;
	MachineContext.bDesignTime = AnimatedWidget && AnimatedWidget->IsDesignTime();
	MachineContext.Baselines = &AnimBaselines;
	MachineContext.RestBaselines = AnimBaselines.bCaptured ? &AnimBaselines : &ResetPoseBaselines;

	// Made once: weak, so a machine never keeps the component alive or calls into one that is gone.
	if (!MachineContext.ResolvePath)
	{
		const TWeakObjectPtr<UWidgetAnimationComponent> WeakThis(this);
		MachineContext.ResolvePath = [WeakThis](FName Path) -> UWidget*
		{
			const UWidgetAnimationComponent* Self = WeakThis.Get();
			return Self ? Self->ResolveAnimTarget(Path) : nullptr;
		};
		MachineContext.OnStateEvent = [WeakThis](const FWidgetAnimStateEventInfo& Info)
		{
			if (UWidgetAnimationComponent* Self = WeakThis.Get())
			{
				Self->QueueStateEvent(Info);
			}
		};
	}
	return MachineContext;
}

void UWidgetAnimationComponent::InitializeMachines()
{
	// Inputs set before the widget was constructed - a View Model bound early, a Blueprint setting a Selected - carry over.
	struct FKeptInput
	{
		const UWidgetAnimStateMachineAsset* Machine = nullptr;
		FWidgetAnimMachineInputState Input;
	};
	TArray<FKeptInput> Kept;
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		for (const FWidgetAnimMachineInputState& Input : Instance.Inputs)
		{
			if (Input.Type != EWidgetAnimMachineInputType::Trigger)
			{
				FKeptInput& Entry = Kept.AddDefaulted_GetRef();
				Entry.Machine = Instance.Machine;
				Entry.Input = Input;
			}
		}
		WidgetAnimStateMachines::Stop(Instance);
	}

	MachineInstances.Reset();
	MachineLastPosed.Reset();
	TArray<const FWidgetAnimStateMachineEntry*> Rows;
	GetPlayedMachineRows(Rows);
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		if (!Rows[RowIndex]->StateMachine)
		{
			continue;
		}
		FWidgetAnimMachineInstance& Instance = MachineInstances.AddDefaulted_GetRef();
		WidgetAnimStateMachines::Initialize(Instance, *Rows[RowIndex], RowIndex);

		for (const FKeptInput& Entry : Kept)
		{
			FWidgetAnimMachineInputState* Input = Entry.Machine == Instance.Machine ? Instance.FindInput(Entry.Input.Name) : nullptr;
			if (Input && Input->Type == EWidgetAnimMachineInputType::Hold && Entry.Input.Type == EWidgetAnimMachineInputType::Hold)
			{
				// A Press And Hold is timed again from whether it is held: only a hold set from Blueprint carries over.
				Input->bHoldSetDown = Entry.Input.bHoldSetDown;
			}
			else if (Input && Input->Type == Entry.Input.Type)
			{
				Input->bValue = Entry.Input.bValue;
				Input->Number = Entry.Input.Number;
				Input->Vector = Entry.Input.Vector;
				Input->PreviousNumber = Input->Number;
				Input->PreviousVector = Input->Vector;
			}
		}
	}
}

void UWidgetAnimationComponent::StartMachines(bool bOnlyOnConstruct)
{
	if (!AnimOwner.IsValid())
	{
		return;
	}
	if (MachineInstances.Num() == 0 && HasPlayedMachineRows())
	{
		InitializeMachines();
	}

	const FWidgetAnimMachineContext& Context = GetMachineContext();
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		const FWidgetAnimStateMachineEntry* Row = GetPlayedMachineRow(Instance.RowIndex);
		const bool bAsked = !bOnlyOnConstruct || (Row && Row->bStartOnConstruct);
		if (!Instance.bStarted && bAsked)
		{
			WidgetAnimStateMachines::Start(Instance, Context);
		}
	}
}

bool UWidgetAnimationComponent::AreMachinesRunning() const
{
	return MachineInstances.ContainsByPredicate([](const FWidgetAnimMachineInstance& Instance) { return Instance.bStarted; });
}

void UWidgetAnimationComponent::GetPlayedMachineRows(TArray<const FWidgetAnimStateMachineEntry*>& OutRows) const
{
	OutRows.Reset();
	for (const FWidgetAnimStateMachineEntry& Row : StateMachines)
	{
		OutRows.Add(&Row);
	}
	if (const UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent())
	{
		for (const FWidgetAnimStateMachineEntry& Row : Machines->StateMachines)
		{
			OutRows.Add(&Row);
		}
	}
}

const FWidgetAnimStateMachineEntry* UWidgetAnimationComponent::GetPlayedMachineRow(int32 RowIndex) const
{
	if (StateMachines.IsValidIndex(RowIndex))
	{
		return &StateMachines[RowIndex];
	}
	const UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent();
	const int32 MachinesIndex = RowIndex - StateMachines.Num();
	return Machines && Machines->StateMachines.IsValidIndex(MachinesIndex) ? &Machines->StateMachines[MachinesIndex] : nullptr;
}

bool UWidgetAnimationComponent::HasPlayedMachineRows() const
{
	if (StateMachines.Num() > 0)
	{
		return true;
	}
	const UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent();
	return Machines && Machines->StateMachines.Num() > 0;
}

UWidgetAnimationStateMachineComponent* UWidgetAnimationComponent::GetMachineComponent() const
{
	return UWidgetAnimationStateMachineComponent::FindWidgetAnimationStateMachineComponent(AnimOwner.Get());
}

EWidgetAnimTracksWithMachines UWidgetAnimationComponent::GetTracksWithMachines() const
{
	const UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent();
	return Machines && Machines->StateMachines.Num() > 0 ? Machines->TracksWithStateMachines : TracksWithStateMachines;
}

void UWidgetAnimationComponent::BindAsMachinePlayer(UWidget* InWidget, bool bInPreserveAuthoredTransforms)
{
	bPreserveAuthoredTransforms = bInPreserveAuthoredTransforms;
	SetAnimOwner(InWidget, /*bByCode=*/false);
}

bool UWidgetAnimationComponent::ReactsToVisibility() const
{
	// Entries set to play on visibility - or a state machine with a way to Exit, which plays as the widget is hidden.
	return bAnyVisibilityTrigger || MachineInstances.ContainsByPredicate([](const FWidgetAnimMachineInstance& Instance)
	{
		return Instance.bStarted && Instance.Machine && Instance.Machine->HasExitPath();
	});
}

bool UWidgetAnimationComponent::AreMachinesExiting() const
{
	return MachineInstances.ContainsByPredicate([](const FWidgetAnimMachineInstance& Instance)
	{
		return Instance.bClosing && WidgetAnimStateMachines::IsExitPending(Instance);
	});
}

void UWidgetAnimationComponent::GatherMachineSettings(TArray<FWidgetAnimSettings>& OutSettings)
{
	const FWidgetAnimMachineContext& Context = GetMachineContext();
	TArray<const FWidgetAnimStateMachineEntry*> Rows;
	GetPlayedMachineRows(Rows);
	for (const FWidgetAnimStateMachineEntry* Row : Rows)
	{
		WidgetAnimStateMachines::GatherAllSettings(*Row, Context, OutSettings);
	}
}

void UWidgetAnimationComponent::SetMachinesClosing(bool bClosing)
{
	if (MachineInstances.Num() == 0)
	{
		return;
	}
	const FWidgetAnimMachineContext& Context = GetMachineContext();
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::SetClosing(Instance, Context, bClosing);
	}
}

bool UWidgetAnimationComponent::ForEachMachine(const UWidgetAnimStateMachineAsset* StateMachine, TFunctionRef<bool(FWidgetAnimMachineInstance&)> Visit)
{
	// Set before the widget is constructed - a View Model bound early, a Blueprint setting Selected in Pre Construct: the
	// instances are made now, not started, so the value is there when they start (InitializeMachines keeps it).
	if (MachineInstances.Num() == 0 && HasPlayedMachineRows())
	{
		InitializeMachines();
	}

	bool bAny = false;
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if (!StateMachine || Instance.Machine == StateMachine)
		{
			bAny |= Visit(Instance);
		}
	}
	return bAny;
}

const FWidgetAnimMachineInstance* UWidgetAnimationComponent::FindMachine(const UWidgetAnimStateMachineAsset* StateMachine) const
{
	return MachineInstances.FindByPredicate([StateMachine](const FWidgetAnimMachineInstance& Instance) { return !StateMachine || Instance.Machine == StateMachine; });
}

void UWidgetAnimationComponent::TickAndComposeMachines(float DeltaTime, FWidgetAnimResult& Result)
{
	TickMachines(DeltaTime);
	ComposeMachines(Result);
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		Instance.bPoseChanged = false;
	}
}

bool UWidgetAnimationComponent::TickMachines(float DeltaTime)
{
	const FWidgetAnimMachineContext& Context = GetMachineContext();

	// Moved once a frame at runtime, as the track player is: a second tick in the same frame re-poses.
	if (!Context.bDesignTime && DeltaTime > 0.f)
	{
		if (MachinesLastAdvanceFrame == GFrameCounter)
		{
			DeltaTime = 0.f;
		}
		else
		{
			MachinesLastAdvanceFrame = GFrameCounter;
		}
	}

	// The values Set Widget Animation Parameter set here, for the animations the states start.
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);

	bool bPoseChanged = false;
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::Tick(Instance, Context, DeltaTime);
		bPoseChanged = bPoseChanged || Instance.bPoseChanged;
	}
	return bPoseChanged;
}

void UWidgetAnimationComponent::ComposeMachines(FWidgetAnimResult& Result)
{
	WidgetAnimStateMachines::Compose(MakeArrayView(MachineInstances), GetTracksWithMachines(), GetMachineContext(), Result, MachineLastPosed);
}

bool UWidgetAnimationComponent::AreTracksHolding(bool& bOutReadsInputs) const
{
	bOutReadsInputs = false;
	for (const FWidgetAnimTrack& Track : AnimPlayer.Tracks)
	{
		if (!Track.bFinished || !Track.bHoldWhenFinished || Track.bNeedsExpansion)
		{
			return false;
		}
	}
	// A type that changes its target by itself while its value stands still - a Scramble still flickering - never rests.
	if (UWidgetAnimLibrary::AnimatesAtRest(AnimPlayer))
	{
		return false;
	}
	bOutReadsInputs = UWidgetAnimLibrary::HasValueInputs(AnimPlayer);
	return true;
}

void UWidgetAnimationComponent::NoteTracksAfterTick()
{
	// Resting once the tracks have held still through two ticks in a row with nothing touching them in between: the
	// second one posed them where they now stay, and every later frame would pose them exactly there again.
	bool bReadsInputs = false;
	const bool bHolding = AreTracksHolding(bReadsInputs);
	bTracksResting = bHolding && bTracksHeldLastTick && RestRevision == AnimPlayer.Revision;
	bTracksHeldLastTick = bHolding;
	bRestReadsInputs = bReadsInputs;
	RestRevision = AnimPlayer.Revision;
}

bool UWidgetAnimationComponent::CanSkipTrackEvaluation(UWidget* AnimatedWidget, float DeltaTime)
{
	if (DeltaTime <= 0.f || !bTracksResting || RestRevision != AnimPlayer.Revision)
	{
		return false;
	}

	// Checked again - cheaply, on flags - whatever touched the player: a track started, restarted or unfinished in any
	// way that didn't count as a change wakes it all the same.
	for (const FWidgetAnimTrack& Track : AnimPlayer.Tracks)
	{
		if (!Track.bFinished || !Track.bHoldWhenFinished || Track.bNeedsExpansion)
		{
			bTracksResting = false;
			return false;
		}
	}

	// Tracks following a Value Input - a card tilting toward the pointer - read it every frame, resting or not; a read
	// that moves a value sets it, which counts as a change, and the frame is worked out after all.
	if (bRestReadsInputs)
	{
		UWidgetAnimLibrary::ReadValueInputs(AnimatedWidget, AnimPlayer, DeltaTime);
		if (RestRevision != AnimPlayer.Revision)
		{
			bTracksResting = false;
			return false;
		}
	}
	return true;
}

void UWidgetAnimationComponent::QueueStateEvent(const FWidgetAnimStateEventInfo& Info)
{
	PendingStateEvents.Add(Info);

	// The next frame, never in the middle of a frame being applied: a handler may change anything, the widget included.
	if (!StateEventTickerHandle.IsValid())
	{
		StateEventTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			StateEventTickerHandle.Reset();
			FlushStateEvents();
			return false;
		}));
	}
}

namespace WidgetAnimComponentStateEvents
{
	/** Calls Owner's function of this name - a state's Custom Event - with its parameters filled by type. True if it had one. */
	bool CallStateEvent(UObject* EventOwner, FName FunctionName, const FWidgetAnimStateEventInfo& Info)
	{
		UFunction* Function = EventOwner->FindFunction(FunctionName);
		if (!Function)
		{
			return false;
		}

		// The whole frame, not only the parameters: a function of that name (rather than the Custom Event) may have locals,
		// and InitializeStruct covers them too.
		uint8* Params = static_cast<uint8*>(FMemory_Alloca_Aligned(FMath::Max<int32>(Function->GetStructureSize(), 1), Function->GetMinAlignment()));
		Function->InitializeStruct(Params);
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(*It))
			{
				if (StructProperty->Struct == FWidgetAnimStateEventInfo::StaticStruct())
				{
					StructProperty->CopyCompleteValue(StructProperty->ContainerPtrToValuePtr<void>(Params), &Info);
				}
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(*It))
			{
				NameProperty->SetPropertyValue_InContainer(Params, Info.State);
			}
		}
		EventOwner->ProcessEvent(Function, Params);
		Function->DestroyStruct(Params);
		return true;
	}
}

void UWidgetAnimationComponent::FlushStateEvents()
{
	TArray<FWidgetAnimStateEventInfo> Events = MoveTemp(PendingStateEvents);
	PendingStateEvents.Reset();

	for (const FWidgetAnimStateEventInfo& Info : Events)
	{
		OnStateMachineStateChanged.Broadcast(Info);

		// And the State Machine Component's, whose machines this plays.
		if (UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent())
		{
			Machines->OnStateMachineStateChanged.Broadcast(Info);
		}

		// Its Custom Event - On <Machine> Entered <State>, or a Fire Event action's On <Machine> <Event Name> - on the nearest
		// User Widget around the widget playing the machine that has one: the Widget Blueprint the component was set up in, usually.
		const FName FunctionName = Info.EventName.IsNone()
			? WidgetAnimMachineEvents::GetStateEventFunctionName(Info.Machine, Info.State, Info.bEntered)
			: WidgetAnimMachineEvents::GetActionEventFunctionName(Info.Machine, Info.EventName);
		if (FunctionName.IsNone())
		{
			continue;
		}
		static constexpr int32 MaxOwnerDepth = 64;
		UObject* EventOwner = Info.Host;
		for (int32 Depth = 0; EventOwner && Depth < MaxOwnerDepth; ++Depth, EventOwner = EventOwner->GetOuter())
		{
			if (EventOwner->IsA<UUserWidget>() && WidgetAnimComponentStateEvents::CallStateEvent(EventOwner, FunctionName, Info))
			{
				break;
			}
		}
	}
}

bool UWidgetAnimationComponent::SetStateMachineBool(FName Input, bool bValue, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, bValue](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetBool(Instance, Input, bValue); });
}

bool UWidgetAnimationComponent::SetStateMachineNumber(FName Input, float Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, Value](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetNumber(Instance, Input, Value); });
}

bool UWidgetAnimationComponent::SetStateMachineInteger(FName Input, int32 Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, Value](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetInteger(Instance, Input, Value); });
}

bool UWidgetAnimationComponent::SetStateMachineChoice(FName Input, FName Option, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, Option](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetChoice(Instance, Input, Option); });
}

bool UWidgetAnimationComponent::SetStateMachineVector2D(FName Input, FVector2D Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, Value](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetVector2D(Instance, Input, Value); });
}

FName UWidgetAnimationComponent::GetStateMachineChoice(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	FName Option;
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && WidgetAnimStateMachines::GetChoice(Instance, Input, Option))
		{
			return Option;
		}
	}
	return NAME_None;
}

FVector2D UWidgetAnimationComponent::GetStateMachineVector2D(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	FVector2D Value = FVector2D::ZeroVector;
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && WidgetAnimStateMachines::GetVector2D(Instance, Input, Value))
		{
			return Value;
		}
	}
	return FVector2D::ZeroVector;
}

bool UWidgetAnimationComponent::FireStateMachineTrigger(FName Input, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::FireTrigger(Instance, Input); });
}

bool UWidgetAnimationComponent::SetStateMachineInputValue(FName Input, double Value, UWidgetAnimStateMachineAsset* StateMachine)
{
	return ForEachMachine(StateMachine, [Input, Value](FWidgetAnimMachineInstance& Instance) { return WidgetAnimStateMachines::SetFromNumber(Instance, Input, Value); });
}

bool UWidgetAnimationComponent::GetStateMachineBool(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	bool bValue = false;
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && WidgetAnimStateMachines::GetBool(Instance, Input, bValue))
		{
			return bValue;
		}
	}
	return false;
}

float UWidgetAnimationComponent::GetStateMachineNumber(FName Input, UWidgetAnimStateMachineAsset* StateMachine) const
{
	float Value = 0.f;
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && WidgetAnimStateMachines::GetNumber(Instance, Input, Value))
		{
			return Value;
		}
	}
	return 0.f;
}

bool UWidgetAnimationComponent::IsInState(FName State, UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	return MachineInstances.ContainsByPredicate([State, StateMachine, Layer](const FWidgetAnimMachineInstance& Instance)
	{
		return (!StateMachine || Instance.Machine == StateMachine) && WidgetAnimStateMachines::IsInState(Instance, State, Layer);
	});
}

FName UWidgetAnimationComponent::GetCurrentStateOfLayer(UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && Instance.bStarted)
		{
			// Of a machine with that layer: the first that has it.
			if (!Layer.IsNone() && Instance.Machine && Instance.Machine->FindLayerIndex(Layer) == INDEX_NONE)
			{
				continue;
			}
			return WidgetAnimStateMachines::GetCurrentState(Instance, Layer);
		}
	}
	return NAME_None;
}

TArray<float> UWidgetAnimationComponent::GetStateMachineBlendWeights(UWidgetAnimStateMachineAsset* StateMachine, FName Layer) const
{
	TArray<float> Weights;
	for (const FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		if ((!StateMachine || Instance.Machine == StateMachine) && Instance.bStarted)
		{
			if (!Layer.IsNone() && Instance.Machine && Instance.Machine->FindLayerIndex(Layer) == INDEX_NONE)
			{
				continue;
			}
			WidgetAnimStateMachines::GetBlendWeights(Instance, Layer, Weights);
			break;
		}
	}
	return Weights;
}

void UWidgetAnimationComponent::StartStateMachines()
{
	if (!bOwnerConstructed || !AnimOwner.IsValid())
	{
		return;
	}
	EnsureRuntimeBaselines();
	StartMachines(/*bOnlyOnConstruct=*/false);
	ApplyCurrentAnimPose();
}

void UWidgetAnimationComponent::StopStateMachines()
{
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::Stop(Instance);
	}

	// The widgets they posed go back to what the rest says, this frame.
	if (bOwnerConstructed)
	{
		ApplyCurrentAnimPose();
	}
}

bool UWidgetAnimationComponent::HasStateMachineInput(const UWidgetAnimStateMachineAsset* StateMachine, FName Input) const
{
	if (Input.IsNone())
	{
		return false;
	}
	TArray<const FWidgetAnimStateMachineEntry*> Rows;
	GetPlayedMachineRows(Rows);
	for (const FWidgetAnimStateMachineEntry* Row : Rows)
	{
		if (Row->StateMachine && (!StateMachine || Row->StateMachine == StateMachine) && (Row->StateMachine->FindInput(Input) || WidgetAnimMachineInputs::IsBuiltIn(Input)))
		{
			return true;
		}
	}
	return false;
}

bool UWidgetAnimationComponent::PlaysStateMachine(const UWidgetAnimStateMachineAsset* StateMachine) const
{
	TArray<const FWidgetAnimStateMachineEntry*> Rows;
	GetPlayedMachineRows(Rows);
	return StateMachine && Rows.ContainsByPredicate([StateMachine](const FWidgetAnimStateMachineEntry* Row) { return Row->StateMachine == StateMachine; });
}

void UWidgetAnimationComponent::SetWidgetAnimationParameter(const FWidgetAnimParameterValue& Value, const UWidgetAnimPresetAsset* Animation, FName AnimId)
{
	if (Value.Name.IsNone())
	{
		return;
	}

	// One group per animation and Anim ID it is set for: setting it again replaces the value.
	FWidgetAnimSetParameterGroup* Group = SetParameterGroups.FindByPredicate([Animation, AnimId](const FWidgetAnimSetParameterGroup& Candidate)
	{
		return Candidate.bForAnimation == (Animation != nullptr) && Candidate.Animation.Get() == Animation && Candidate.AnimId == AnimId;
	});
	if (!Group)
	{
		Group = &SetParameterGroups.AddDefaulted_GetRef();
		Group->Animation = Animation;
		Group->bForAnimation = Animation != nullptr;
		Group->AnimId = AnimId;
	}

	FWidgetAnimParameterValue Set = Value;
	Set.bOverride = true;
	if (FWidgetAnimParameterValue* Existing = Group->Values.FindByPredicate([&Value](const FWidgetAnimParameterValue& Candidate) { return Candidate.Name == Value.Name; }))
	{
		if (Existing->SameValueAs(Set))
		{
			return;
		}
		*Existing = Set;
	}
	else
	{
		Group->Values.Add(Set);
	}

	// Live: what plays now takes it at once.
	RefreshPlayingParameters(Animation, AnimId);
}

void UWidgetAnimationComponent::RefreshPlayingParameters(const UWidgetAnimPresetAsset* Animation, FName AnimId)
{
	if (!AnimOwner.IsValid() || (AnimPlayer.Tracks.Num() == 0 && MachineInstances.Num() == 0))
	{
		return;
	}

	// The tracks can't be changed while a frame of them is being applied: once it is done.
	if (PlayerTickDepth > 0)
	{
		bParameterRefreshPending = true;
		return;
	}

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };
	FWidgetAnimParameterScopeStack ParameterScopes;
	PushParameterScopes(ParameterScopes);

	bool bAnyChanged = false;
	for (FWidgetAnimTrack& Track : AnimPlayer.Tracks)
	{
		if (Track.bFinished && !Track.bHoldWhenFinished)
		{
			continue;
		}
		const TArray<FWidgetAnimEntry>& Entries = Track.bIsOut ? EffectiveOutEntries : EffectiveInEntries;
		if (!Entries.IsValidIndex(Track.SourceIndex))
		{
			continue;
		}
		const FWidgetAnimEntry& Entry = Entries[Track.SourceIndex];
		const UWidgetAnimPresetAsset* Played = Entry.Animation.Get();
		if (!Entry.IsUsable() || Entry.AnimId != Track.AnimId || !Played || Played->Parameters.Num() == 0
			|| (Animation && Played != Animation) || (!AnimId.IsNone() && Entry.AnimId != AnimId))
		{
			continue;
		}

		// Worked out again with the values as they are now, and laid over the track's own - one for one, so where each part
		// is in its play stays as it was. The settings added on top of the entry's (linked tracks) are left as they are.
		TArray<FWidgetAnimSettings> Rebuilt;
		Entry.BuildSettingsResolved(Rebuilt, Resolver);
		const int32 Count = FMath::Min(Rebuilt.Num(), Track.Settings.Num());
		bool bTrackChanged = false;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (!FWidgetAnimSettings::StaticStruct()->CompareScriptStruct(&Track.Settings[Index], &Rebuilt[Index], PPF_None))
			{
				Track.Settings[Index] = MoveTemp(Rebuilt[Index]);
				bTrackChanged = true;
			}
		}
		if (bTrackChanged)
		{
			Track.bSettled = false;
			bAnyChanged = true;
		}
	}

	// The state machines' playing states, as the entries' tracks: they follow at once rather than on their next start.
	if (MachineInstances.Num() > 0)
	{
		const FWidgetAnimMachineContext& Context = GetMachineContext();
		for (FWidgetAnimMachineInstance& Instance : MachineInstances)
		{
			bAnyChanged |= WidgetAnimStateMachines::RefreshParameters(Instance, Context);
		}
	}
	ParameterScopes.Reset();

	if (bAnyChanged)
	{
		ApplyCurrentAnimPose();
	}
}

void UWidgetAnimationComponent::ClearWidgetAnimationParameters(const UWidgetAnimPresetAsset* Animation, FName AnimId)
{
	if (!Animation && AnimId.IsNone())
	{
		SetParameterGroups.Reset();
	}
	else
	{
		SetParameterGroups.RemoveAll([Animation, AnimId](const FWidgetAnimSetParameterGroup& Group)
		{
			return (!Animation || Group.Animation.Get() == Animation) && (AnimId.IsNone() || Group.AnimId == AnimId);
		});
	}
	RefreshPlayingParameters(Animation, AnimId);
}

bool UWidgetAnimationComponent::GetWidgetAnimationParameter(FName Name, const UWidgetAnimPresetAsset* Animation, FName AnimId, FWidgetAnimParameterValue& OutValue) const
{
	// Set here, the latest set first.
	for (int32 Index = SetParameterGroups.Num() - 1; Index >= 0; --Index)
	{
		const FWidgetAnimSetParameterGroup& Group = SetParameterGroups[Index];
		if (Group.bForAnimation && !Group.Animation.IsValid())
		{
			continue;
		}
		if (!Group.AppliesTo(Animation, AnimId))
		{
			continue;
		}
		if (const FWidgetAnimParameterValue* Found = Group.Values.FindByPredicate([Name](const FWidgetAnimParameterValue& Value) { return Value.Name == Name; }))
		{
			OutValue = *Found;
			return true;
		}
	}

	// The component's own row for it, when set here - under each entry's own value.
	const FWidgetAnimParameterValue* ComponentRow = ComponentParameters.FindByPredicate([Name](const FWidgetAnimParameterValue& Value) { return Value.Name == Name && Value.bOverride; });

	// Else as an entry plays it: its own value, the component's, or the animation's default.
	for (const TArray<FWidgetAnimEntry>* Entries : { &EffectiveInEntries, &EffectiveOutEntries })
	{
		for (const FWidgetAnimEntry& Entry : *Entries)
		{
			const UWidgetAnimPresetAsset* EntryAnimation = Entry.Animation.Get();
			if (!EntryAnimation || (Animation && EntryAnimation != Animation) || (!AnimId.IsNone() && Entry.AnimId != AnimId))
			{
				continue;
			}
			const FWidgetAnimParameter* Declared = EntryAnimation->FindParameter(Name);
			if (!Declared)
			{
				continue;
			}
			const FWidgetAnimParameterValue* Own = Entry.Parameters.FindByPredicate([Name](const FWidgetAnimParameterValue& Value) { return Value.Name == Name && Value.bOverride; });
			OutValue = Own ? *Own : (ComponentRow ? *ComponentRow : FWidgetAnimParameterValue::FromDefault(*Declared));
			return true;
		}
	}

	// An animation no entry here plays - one a state machine plays, or asked about by name: the component's row, else its default.
	const FWidgetAnimParameter* Declared = Animation ? Animation->FindParameter(Name) : FindParameterDeclaration(Name);
	if (Declared)
	{
		OutValue = ComponentRow ? *ComponentRow : FWidgetAnimParameterValue::FromDefault(*Declared);
		return true;
	}
	return false;
}

void UWidgetAnimationComponent::PushParameterScopes(FWidgetAnimParameterScopeStack& Stack) const
{
	// The component's own Parameters, under each use's own values, for every animation it plays.
	if (ComponentParameters.Num() > 0)
	{
		Stack.Push(ComponentParameters, nullptr, NAME_None, /*bUnderUse=*/true);
	}

	for (const FWidgetAnimSetParameterGroup& Group : SetParameterGroups)
	{
		// For an animation since unloaded: for nothing, rather than for everything.
		if (Group.Values.Num() == 0 || (Group.bForAnimation && !Group.Animation.IsValid()))
		{
			continue;
		}
		Stack.Push(Group.Values, Group.Animation.Get(), Group.AnimId);
	}
}

void UWidgetAnimationComponent::GatherParameterDeclarations(TArray<const FWidgetAnimParameter*>& OutDeclarations) const
{
	OutDeclarations.Reset();
	TSet<const UWidgetAnimPresetAsset*> Seen;
	auto FromAnimation = [&OutDeclarations, &Seen](const UWidgetAnimPresetAsset* Animation)
	{
		bool bAlreadySeen = false;
		Seen.Add(Animation, &bAlreadySeen);
		if (!Animation || bAlreadySeen)
		{
			return;
		}
		for (const FWidgetAnimParameter& Parameter : Animation->Parameters)
		{
			const bool bListed = OutDeclarations.ContainsByPredicate([&Parameter](const FWidgetAnimParameter* Listed) { return Listed->Name == Parameter.Name; });
			if (!Parameter.Name.IsNone() && !bListed)
			{
				OutDeclarations.Add(&Parameter);
			}
		}
	};
	for (const TArray<FWidgetAnimEntry>* Entries : { &AnimIn, &AnimOut })
	{
		for (const FWidgetAnimEntry& Entry : *Entries)
		{
			FromAnimation(Entry.Animation.Get());
		}
	}
	TArray<const FWidgetAnimStateMachineEntry*> Rows;
	GetPlayedMachineRows(Rows);
	for (const FWidgetAnimStateMachineEntry* Row : Rows)
	{
		if (Row->StateMachine)
		{
			TArray<UWidgetAnimPresetAsset*> Animations;
			Row->StateMachine->GatherAnimations(Animations);
			for (const UWidgetAnimPresetAsset* Animation : Animations)
			{
				FromAnimation(Animation);
			}
		}
	}
}

const FWidgetAnimParameter* UWidgetAnimationComponent::FindParameterDeclaration(FName Name) const
{
	TArray<const FWidgetAnimParameter*> Declarations;
	GatherParameterDeclarations(Declarations);
	const FWidgetAnimParameter* const* Found = Declarations.FindByPredicate([Name](const FWidgetAnimParameter* Declared) { return Declared->Name == Name; });
	return Found ? *Found : nullptr;
}

bool UWidgetAnimationComponent::SyncComponentParameters()
{
	TArray<const FWidgetAnimParameter*> Declarations;
	GatherParameterDeclarations(Declarations);

	// As an entry lists its animation's: a row set here and still the same parameter is kept, any other follows the default.
	TArray<FWidgetAnimParameterValue> Synced;
	Synced.Reserve(Declarations.Num());
	for (const FWidgetAnimParameter* Parameter : Declarations)
	{
		const FWidgetAnimParameterValue* Existing = ComponentParameters.FindByPredicate([Parameter](const FWidgetAnimParameterValue& Value)
		{
			return Value.Name == Parameter->Name && Value.Type == Parameter->Type;
		});
		if (Existing && Existing->bOverride)
		{
			FWidgetAnimParameterValue& Kept = Synced.Add_GetRef(*Existing);
			if (Kept.Type == EWidgetAnimParameterType::Choice && !Parameter->Options.Contains(Kept.Option))
			{
				Kept.Option = Parameter->GetDefaultOption();
			}
		}
		else
		{
			FWidgetAnimParameterValue& Default = Synced.Add_GetRef(FWidgetAnimParameterValue::FromDefault(*Parameter));
			Default.bOverride = false;
		}
	}

	bool bChanged = Synced.Num() != ComponentParameters.Num() || bHasComponentParameters != (Synced.Num() > 0);
	for (int32 Index = 0; !bChanged && Index < Synced.Num(); ++Index)
	{
		const FWidgetAnimParameterValue& Was = ComponentParameters[Index];
		const FWidgetAnimParameterValue& Now = Synced[Index];
		bChanged = Was.Name != Now.Name || Was.Type != Now.Type || Was.bOverride != Now.bOverride || !Was.SameValueAs(Now);
	}
	if (bChanged)
	{
		ComponentParameters = MoveTemp(Synced);
		bHasComponentParameters = ComponentParameters.Num() > 0;
	}
	return bChanged;
}

bool UWidgetAnimationComponent::SyncStateMachines()
{
	if (StateMachines.Num() == 0)
	{
		return false;
	}

	// New rows are checked against what this widget's tree has - see FWidgetAnimEntry::SyncGroupTargets.
	const TArray<FString> TargetOptions = GetAnimTargetWidgetOptions();
	bool bChanged = false;
	for (FWidgetAnimStateMachineEntry& Row : StateMachines)
	{
		bChanged |= Row.SyncToMachine(&TargetOptions);
	}
	return bChanged;
}

// =====================================================================================================
// State machines in the designer
// =====================================================================================================

bool UWidgetAnimationComponent::StartDesignerStateMachinePreview()
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !AnimatedWidget->IsDesignTime() || !HasPlayedMachineRows())
	{
		return false;
	}
	if (bMachinePreviewing)
	{
		return true;
	}

	const TSharedPtr<SWidget> SlateWidget = AnimatedWidget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return false;
	}

	// The toolbar's preview gives way: the two pose the same widgets.
	DesignerPreviewModeOverride.Reset();
	StopDesignerPreview();

	// From the authored pose, read again with the machines' widgets in it.
	if (AnimBaselines.bCaptured)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines);
	}
	AnimBaselines.ResetBaselines();
	RebuildEffectiveAnimations();
	SyncStateMachines();
	if (UWidgetAnimationStateMachineComponent* Machines = GetMachineComponent())
	{
		Machines->SyncStateMachines();
	}
	InitializeMachines();
	TArray<FWidgetAnimSettings> InToCapture = FlattenedIn;
	GatherMachineSettings(InToCapture);
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, InToCapture, FlattenedOut, AnimBaselines);

	bMachinePreviewing = true;
	const FWidgetAnimMachineContext& Context = GetMachineContext();
	{
		FWidgetAnimParameterScopeStack ParameterScopes;
		PushParameterScopes(ParameterScopes);
		for (FWidgetAnimMachineInstance& Instance : MachineInstances)
		{
			WidgetAnimStateMachines::Start(Instance, Context);
		}
	}
	TickDesignerMachines(0.f);

	// An active timer, which is also what keeps the designer repainting while the machines move.
	const TSharedRef<FActiveTimerHandle> Handle = SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateWeakLambda(this,
		[this](double /*CurrentTime*/, float DeltaTime) -> EActiveTimerReturnType
		{
			if (!bMachinePreviewing || !AnimOwner.IsValid())
			{
				return EActiveTimerReturnType::Stop;
			}
			// Paused: posed where it is, its time standing still.
			TickDesignerMachines(bMachinePreviewPaused ? 0.f : DeltaTime);
			return EActiveTimerReturnType::Continue;
		}));
	MachinePreviewTimerHandle = Handle;
	MachinePreviewTimerWidget = SlateWidget;
	return true;
}

void UWidgetAnimationComponent::StopDesignerStateMachinePreview()
{
	if (!bMachinePreviewing)
	{
		return;
	}
	bMachinePreviewing = false;

	const TSharedPtr<FActiveTimerHandle> Handle = MachinePreviewTimerHandle.Pin();
	const TSharedPtr<SWidget> Widget = MachinePreviewTimerWidget.Pin();
	if (Handle.IsValid() && Widget.IsValid())
	{
		Widget->UnRegisterActiveTimer(Handle.ToSharedRef());
	}
	MachinePreviewTimerHandle.Reset();
	MachinePreviewTimerWidget.Reset();

	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::Stop(Instance);
	}
	MachineInstances.Reset();
	MachineLastPosed.Reset();

	if (AnimBaselines.bCaptured)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines);
	}
}

void UWidgetAnimationComponent::TickDesignerMachines(float DeltaTime)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return;
	}

	FWidgetAnimResult Result;
	TickAndComposeMachines(DeltaTime, Result);
	UWidgetAnimLibrary::ApplyComposedResult(AnimatedWidget, Result, AnimBaselines);
	for (FWidgetAnimMachineInstance& Instance : MachineInstances)
	{
		WidgetAnimStateMachines::ReleaseFinishedTracks(Instance);
	}
}

void UWidgetAnimationComponent::SetDesignerStateMachineBuiltIn(FName Input, bool bValue, const UWidgetAnimStateMachineAsset* StateMachine)
{
	const FWidgetAnimMachineContext& Context = GetMachineContext();
	ForEachMachine(StateMachine, [&Context, Input, bValue](FWidgetAnimMachineInstance& Instance)
	{
		if (Input == WidgetAnimMachineInputs::Closing)
		{
			WidgetAnimStateMachines::SetClosing(Instance, Context, bValue);
		}
		else
		{
			WidgetAnimStateMachines::SetBuiltIn(Instance, Input, bValue);
		}
		return true;
	});
}
