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
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimationComponent, Log, All);

const FGuid FWidgetAnimationComponentVersion::GUID(0x4E86197C, 0x6A5440B8, 0x9235D438, 0x82E3D644);

TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationComponent>> UWidgetAnimationComponent::ComponentsByWidget;

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
	 * on; material layers use it too when it is an Image, and a Transform layer's Animate Shape uses it when
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
	UWidget* FindLiveOwner(const UWidgetAnimationComponent* Template)
	{
		for (TObjectIterator<UWidgetAnimationComponent> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
		{
			const UWidgetAnimationComponent* Candidate = *It;
			if (Candidate == Template || !Candidate->GetAnimatedWidget())
			{
				continue;
			}

			for (const UObject* Archetype = Candidate->GetArchetype(); Archetype; Archetype = Archetype->GetArchetype())
			{
				if (Archetype == Template)
				{
					return Candidate->GetAnimatedWidget();
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
	TArray<FString> Options;

	// The widget the component is on - or, for the Details panel's template, the one its live copy is on.
	UWidget* WidgetOwner = AnimOwner.Get();
#if WITH_EDITOR
	if (!WidgetOwner)
	{
		WidgetOwner = WidgetAnimComponentOptions::FindLiveOwner(this);
	}
#endif

	if (WidgetOwner)
	{
		// On a whole User Widget - bound onto itself by code, or the top of the Widget Blueprint - its paths go into its
		// own tree, so that is what is offered. Otherwise the widget and everything inside it, named as they are in the
		// tree it sits in - which is how targets are looked up.
		UUserWidget* OwnerAsUserWidget = Cast<UUserWidget>(WidgetOwner);
		if (OwnerAsUserWidget && (bBoundByCode || !WidgetOwner->GetTypedOuter<UUserWidget>()))
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
		const UWidgetBlueprintGeneratedClass* AuthoredClass = GetTypedOuter<UWidgetBlueprintGeneratedClass>();
#if WITH_EDITORONLY_DATA
		if (!AuthoredClass)
		{
			if (const UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
			{
				AuthoredClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
			}
		}
#endif
		if (const UWidgetTree* AuthoredTree = AuthoredClass ? AuthoredClass->GetWidgetTreeArchetype() : nullptr)
		{
			const FName HeldName = WidgetAnimComponentOptions::FindHeldUnderName(this);
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

			// Every widget this entry animates - one per Layer Group, though groups may share one. Collected
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
}

void UWidgetAnimationComponent::StartRuntimeTicking()
{
	if (RuntimeTickHandle.IsValid())
	{
		return;
	}

	// The core ticker rather than the owner's tick: a UI Component can sit on any widget, most of which
	// never tick, and the core ticker runs before Slate lays out the frame, so a pose written here is the
	// pose that frame is drawn with. It keeps running while the game is paused, as UI tick always has.
	RuntimeTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float DeltaTime)
	{
		RuntimeTick(DeltaTime);
		return true;
	}));
}

void UWidgetAnimationComponent::StopRuntimeTicking()
{
	if (RuntimeTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RuntimeTickHandle);
		RuntimeTickHandle.Reset();
	}
}

void UWidgetAnimationComponent::RuntimeTick(float DeltaTime)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !bOwnerConstructed)
	{
		StopRuntimeTicking();
		return;
	}

	// A component cannot intercept its owner's SetVisibility, so becoming visible is noticed here instead.
	// The pose is applied in the same frame (see ApplyCurrentAnimPose), before Slate draws the widget.
	if (bAnyVisibilityTrigger && !bPlayOnlyOnConstructAndDestruct)
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

	// No-ops after the first call. Capturing again would read back a pose the animation itself wrote.
	if (bPreserveAuthoredTransforms)
	{
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, FlattenedIn, FlattenedOut, AnimBaselines);
	}
	else
	{
		// Not composed over - Preserve Authored Transforms is off - but still needed to put widgets back when an
		// animation is stopped with Reset To Start. Without it that reset had nothing to restore, so the widget
		// stayed wherever the animation had gotten to.
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, FlattenedIn, FlattenedOut, ResetPoseBaselines);
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
	UWidgetAnimLibrary::TickWidgetAnimPlayer(AnimatedWidget, AnimPlayer, AnimBaselines, DeltaTime, OutJustFinished);
	--PlayerTickDepth;

	if (PlayerTickDepth == 0 && bTextChangePending)
	{
		bTextChangePending = false;
		PlayForTextChange();
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
	if (!bAnyEntryAnswers && !bUnnamedOnly)
	{
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

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/false, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
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

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	// The In tracks are dropped rather than left holding: an exit that has to fight the entrance's final
	// pose every frame never quite gets where it's going.
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	auto AppendLinked = [this](int32 EntryIndex, TArray<FWidgetAnimSettings>& EntrySettings) { PrepareEntrySettings(EntryIndex, /*bOut=*/true, EntrySettings); };
	const TFunctionRef<void(int32, TArray<FWidgetAnimSettings>&)> AppendExtra(AppendLinked);
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
	else if (AnimPlayer.Phase == EWidgetAnimPhase::PlayingOut && !IsSettlingInDirection(/*bIsOut=*/true))
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

	// Nothing plays before the owner is constructed (or at all in the designer), so the value is only remembered: the
	// tracks start from it when they do start, rather than from their Start Value.
	if (!AnimOwner.IsValid() || !bOwnerConstructed)
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

	const int32 Taken = UWidgetAnimEntryLibrary::SetEntriesValue(AnimPlayer, EffectiveInEntries, Resolver, Id, Value, bSnap, bStartIfNotPlaying, &AppendExtra);

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
	return FlattenedOut.Num() > 0 || PendingExit != EPendingExit::None || UWidgetAnimLibrary::IsAnyValueLayerHoldingExit(AnimPlayer);
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

	if (bAnyVisibilityTrigger && !bPlayOnlyOnConstructAndDestruct && bOwnerConstructed)
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
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget || !AnimatedWidget->IsDesignTime())
	{
		return;
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
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// An animation picked, swapped or cleared: its Target Widgets get a row per Layer Group. Only once the
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
	}

	// Keeps each Transform layer's Keys list one-to-one with its curve: a key added on the curve gets an
	// entry, a deleted one loses it, and every entry's time follows its key. Deferred and skipped mid-drag -
	// see RequestSyncTransformAnims - so the Details panel is never rebuilt under an edit in progress.
	UWidgetAnimTransformLibrary::RequestSyncTransformAnims(this, PropertyChangedEvent);
}
#endif