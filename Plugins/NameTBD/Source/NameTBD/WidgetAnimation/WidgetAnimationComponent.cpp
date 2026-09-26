// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimationComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "WidgetAnimTransform.h"
#include "Engine/Engine.h"
#include "Serialization/CustomVersion.h"
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
	void AppendAdditiveLayers(const TArray<FWidgetAnimLayer>& Layers, UWidget* AnimatedWidget, TArray<FWidgetAnimSettings>& OutSettings)
	{
		UImage* MaterialTarget = Cast<UImage>(AnimatedWidget);

		// The widget itself when it is a Shape Box, or the Shape Box it sits in - so a component on an Image
		// inside a Shape Box animates the box's outline while it moves the Image.
		UShapeBox* ShapeTarget = UWidgetAnimLibrary::ResolveShapeBox(AnimatedWidget);

		// Placed in order, each after the one above it, exactly as an entry's layers are.
		TArray<FWidgetAnimSettings> Placed;
		UWidgetAnimLibrary::BuildLayerSettings(Layers, Placed);

		OutSettings.Reserve(OutSettings.Num() + Placed.Num());
		for (FWidgetAnimSettings& Settings : Placed)
		{
			Settings.MaterialTargetImage = MaterialTarget;
			Settings.ShapeTargetBox = ShapeTarget;
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

TArray<FString> UWidgetAnimationComponent::GetAnimTargetWidgetOptions() const
{
	TArray<FString> Options;

	UUserWidget* Root = GetPathRoot();

	// The Details panel edits the component ARCHETYPE the Widget Blueprint keeps, which has no owner widget
	// to ask. Find the Widget Blueprint it belongs to and list that class's tree instead.
	if (!Root)
	{
		if (const UWidgetBlueprintGeneratedClass* GeneratedClass = GetTypedOuter<UWidgetBlueprintGeneratedClass>())
		{
			Root = GeneratedClass->GetDefaultObject<UUserWidget>();
		}
	}

#if WITH_EDITORONLY_DATA
	if (!Root)
	{
		if (const UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			if (Blueprint->GeneratedClass)
			{
				Root = Cast<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject());
			}
		}
	}
#endif

	if (Root)
	{
		UWidgetAnimLibrary::GatherTargetableWidgetPathsInTree(Root, Options);
	}

	// First, so there is a way back to "animate the widget this is on" once something has been picked.
	Options.Insert(TEXT("None"), 0);
	return Options;
}

// =====================================================================================================
// Building the effective stacks
// =====================================================================================================

void UWidgetAnimationComponent::RebuildEffectiveAnimations()
{
	EffectiveInEntries.Reset();
	EffectiveOutEntries.Reset();

	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimIn, EffectiveInEntries, EffectiveOutEntries);
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimOut, EffectiveOutEntries, EffectiveInEntries);

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

	FlattenedIn.Reset();
	FlattenedOut.Reset();
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveInEntries, Resolver, FlattenedIn);
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveOutEntries, Resolver, FlattenedOut);

	// The unnamed layers: Additive Anim Layers first, then anything code handed over. They play as one
	// track alongside the entries every time Anim In / Anim Out plays.
	UnnamedAnimIn.Reset();
	UnnamedAnimOut.Reset();
	AppendAdditiveLayers(AdditiveAnimLayersIn, AnimOwner.Get(), UnnamedAnimIn);
	AppendAdditiveLayers(AdditiveAnimLayersOut, AnimOwner.Get(), UnnamedAnimOut);
	UnnamedAnimIn.Append(ExtraAnimIn);
	UnnamedAnimOut.Append(ExtraAnimOut);

	// On top of the entry-authored stack, which is also what the designer preview and the baselines read.
	FlattenedIn.Append(UnnamedAnimIn);
	FlattenedOut.Append(UnnamedAnimOut);

	RefreshAnimIdConflicts();
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

	if (bPlayOnlyOnConstructAndDestruct || bPlayInAnimationsOnConstruct)
	{
		PlayIn();
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
	if (bPlayAnimationsOnVisibilityHidden && !bPlayOnlyOnConstructAndDestruct)
	{
		const bool bVisibleNow = IsVisibleState(AnimatedWidget->GetVisibility());
		if (bVisibleNow && !bLastSeenVisible && !bIsDeferringVisibility)
		{
			PlayIn();
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
	if (AnimatedWidget && bPreserveAuthoredTransforms)
	{
		// No-ops after the first call. Capturing again would read back a pose the animation itself wrote.
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(AnimatedWidget, FlattenedIn, FlattenedOut, AnimBaselines);
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
	UWidgetAnimLibrary::TickWidgetAnimPlayer(AnimatedWidget, AnimPlayer, AnimBaselines, 0.f, JustFinished);
}

void UWidgetAnimationComponent::PlayIn(bool bCascadeToChildren)
{
	if (!AnimOwner.IsValid())
	{
		return;
	}

	EnsureRuntimeBaselines();

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		EffectiveInEntries,
		Resolver,
		TArray<FName>(),
		/*bIsOut=*/false,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true);

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

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	// The In tracks are dropped rather than left holding: an exit that has to fight the entrance's final
	// pose every frame never quite gets where it's going.
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		EffectiveOutEntries,
		Resolver,
		TArray<FName>(),
		/*bIsOut=*/true,
		/*PlaybackSpeed=*/1.f,
		/*bRestartIfPlaying=*/true,
		/*bHoldWhenFinished=*/true);

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
	UWidgetAnimLibrary::TickWidgetAnimPlayer(AnimatedWidget, AnimPlayer, AnimBaselines, DeltaTime, JustFinished);

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
// Visibility
// =====================================================================================================

void UWidgetAnimationComponent::SetVisibilityAnimated(ESlateVisibility InVisibility)
{
	UWidget* AnimatedWidget = AnimOwner.Get();
	if (!AnimatedWidget)
	{
		return;
	}

	if (bPlayAnimationsOnVisibilityHidden && !bPlayOnlyOnConstructAndDestruct && bOwnerConstructed)
	{
		const bool bIsBecomingInvisible = !IsVisibleState(InVisibility);
		const bool bWasVisible = IsVisibleState(AnimatedWidget->GetVisibility());

		// Hide after the exit has played.
		if (bIsBecomingInvisible && bWasVisible && !IsAnimClosing())
		{
			bIsDeferringVisibility = true;
			DeferredVisibility = InVisibility;
			PlayOut();
			return;
		}

		// Show now and play the entrance.
		if (!bIsBecomingInvisible && !bWasVisible)
		{
			bIsDeferringVisibility = false;
			ApplyVisibilityToOwner(InVisibility);
			bLastSeenVisible = true;
			PlayIn();
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

	// Phase is deliberately untouched: a named one-shot is not the widget appearing or closing.
	UWidgetAnimEntryLibrary::StartEntriesById(
		AnimPlayer,
		bReverse ? EffectiveOutEntries : EffectiveInEntries,
		Resolver,
		AnimIds,
		bReverse,
		PlaybackSpeed,
		bRestartIfPlaying,
		bHoldFinalPose);

	// Additive and extra layers have no Anim ID, so they only join a "play everything" call.
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
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines);
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
	FWidgetAnimPreviewSettings Settings = DesignerPreview;
	if (DesignerPreviewModeOverride.IsSet())
	{
		Settings.Mode = DesignerPreviewModeOverride.GetValue();
		Settings.PlaybackSpeed = FMath::Max(DesignerPreview.PlaybackSpeed * DesignerPreviewOptions.Speed, 0.01f);
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

	if (Differs(Legacy.bPlayInAnimationsOnConstruct, &FWidgetAnimationLegacySettings::bPlayInAnimationsOnConstruct))
	{
		bPlayInAnimationsOnConstruct = Legacy.bPlayInAnimationsOnConstruct;
	}

	if (Differs(Legacy.bPlayAnimationsOnVisibilityHidden, &FWidgetAnimationLegacySettings::bPlayAnimationsOnVisibilityHidden))
	{
		bPlayAnimationsOnVisibilityHidden = Legacy.bPlayAnimationsOnVisibilityHidden;
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
	SyncEntryGroupTargets();
}

bool UWidgetAnimationComponent::SyncEntryGroupTargets()
{
	bool bChanged = false;

	for (FWidgetAnimEntry& Entry : AnimIn)
	{
		bChanged |= Entry.SyncGroupTargets();
	}

	for (FWidgetAnimEntry& Entry : AnimOut)
	{
		bChanged |= Entry.SyncGroupTargets();
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

	// Keeps each Transform layer's Keys list one-to-one with its curve: a key added on the curve gets an
	// entry, a deleted one loses it, and every entry's time follows its key. Deferred and skipped mid-drag -
	// see RequestSyncTransformAnims - so the Details panel is never rebuilt under an edit in progress.
	UWidgetAnimTransformLibrary::RequestSyncTransformAnims(this, PropertyChangedEvent);
}
#endif