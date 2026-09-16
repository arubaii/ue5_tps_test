// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimationComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "Serialization/CustomVersion.h"
#include "Widgets/SWidget.h"

const FGuid FWidgetAnimationComponentVersion::GUID(0x4E86197C, 0x6A5440B8, 0x9235D438, 0x82E3D644);

TMap<TObjectKey<UWidget>, TWeakObjectPtr<UWidgetAnimationComponent>> UWidgetAnimationComponent::ComponentsByWidget;

namespace
{
	FCustomVersionRegistration GRegisterWidgetAnimationComponentVersion(
		FWidgetAnimationComponentVersion::GUID,
		FWidgetAnimationComponentVersion::LatestVersion,
		TEXT("WidgetAnimationComponentVer"));

	// Entry tracks use SourceIndex 0..N-1 (their position in the entry array), so a negative sentinel can
	// never collide with one. This is what lets ExtraAnimIn / ExtraAnimOut run as their own track without
	// an Anim ID and still be found again to stop or restart.
	constexpr int32 ExtraAnimSourceIndex = -1;

	/** How many frames the designer preview waits for the owner's Slate widget before giving up. */
	constexpr int32 PreviewTimerRetryFrames = 30;

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
// Owner and lookup
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
	UWidget* Owner = AnimOwner.Get();
	if (!Owner)
	{
		return nullptr;
	}

	// Bound by a User Widget onto itself: paths are into its own tree, as they always were.
	if (bBoundByCode)
	{
		if (UUserWidget* OwnerAsUserWidget = Cast<UUserWidget>(Owner))
		{
			return OwnerAsUserWidget;
		}
	}

	// Added in the designer: paths are into the Widget Blueprint the component was added in, which is the
	// User Widget whose tree holds the owner - the same tree the Target Widget dropdown lists.
	if (UUserWidget* ContainingUserWidget = Owner->GetTypedOuter<UUserWidget>())
	{
		return ContainingUserWidget;
	}

	return Cast<UUserWidget>(Owner);
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

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	FlattenedIn.Reset();
	FlattenedOut.Reset();
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveInEntries, Resolver, FlattenedIn);
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveOutEntries, Resolver, FlattenedOut);

	// Extra layers go on top of the entry-authored stack.
	FlattenedIn.Append(ExtraAnimIn);
	FlattenedOut.Append(ExtraAnimOut);
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
	UWidget* Owner = AnimOwner.Get();
	if (bOwnerConstructed || !Owner || Owner->IsDesignTime())
	{
		return;
	}

	bOwnerConstructed = true;
	bLastSeenVisible = IsVisibleState(Owner->GetVisibility());

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
	UWidget* Owner = AnimOwner.Get();
	if (!Owner || !bOwnerConstructed)
	{
		StopRuntimeTicking();
		return;
	}

	// A component cannot intercept its owner's SetVisibility, so becoming visible is noticed here instead.
	// The pose is applied in the same frame (see ApplyCurrentAnimPose), before Slate draws the widget.
	if (bPlayAnimationsOnVisibilityHidden && !bPlayOnlyOnConstructAndDestruct)
	{
		const bool bVisibleNow = IsVisibleState(Owner->GetVisibility());
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
	UWidget* Owner = AnimOwner.Get();
	if (Owner && bPreserveAuthoredTransforms)
	{
		// No-ops after the first call. Capturing again would read back a pose the animation itself wrote.
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Owner, FlattenedIn, FlattenedOut, AnimBaselines);
	}
}

void UWidgetAnimationComponent::ApplyCurrentAnimPose()
{
	UWidget* Owner = AnimOwner.Get();
	if (!Owner)
	{
		return;
	}

	// Starting a track only resets its clock. A zero-delta tick applies time zero now, so the first frame
	// is never drawn in the authored pose.
	TArray<FName> JustFinished;
	UWidgetAnimLibrary::TickWidgetAnimPlayer(Owner, AnimPlayer, AnimBaselines, 0.f, JustFinished);
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

	UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, ExtraAnimSourceIndex, ExtraAnimIn, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

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

	UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, ExtraAnimSourceIndex, ExtraAnimOut, /*bIsOut=*/true, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

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

bool UWidgetAnimationComponent::TickAnimationPlayback(float DeltaTime)
{
	UWidget* Owner = AnimOwner.Get();
	if (!Owner)
	{
		return false;
	}

	TArray<FName> JustFinished;
	UWidgetAnimLibrary::TickWidgetAnimPlayer(Owner, AnimPlayer, AnimBaselines, DeltaTime, JustFinished);

	// Asked per direction, so a looping named animation can't hold the entrance open forever.
	if (AnimPlayer.Phase == EWidgetAnimPhase::PlayingIn && !IsPlayingInDirection(/*bIsOut=*/false))
	{
		AnimPlayer.Phase = EWidgetAnimPhase::Shown;
	}
	else if (AnimPlayer.Phase == EWidgetAnimPhase::PlayingOut && !IsPlayingInDirection(/*bIsOut=*/true))
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
	UWidget* Owner = AnimOwner.Get();
	if (!Owner)
	{
		return;
	}

	if (bPlayAnimationsOnVisibilityHidden && !bPlayOnlyOnConstructAndDestruct && bOwnerConstructed)
	{
		const bool bIsBecomingInvisible = !IsVisibleState(InVisibility);
		const bool bWasVisible = IsVisibleState(Owner->GetVisibility());

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
	if (UWidget* Owner = AnimOwner.Get())
	{
		// Guarded so an owner whose SetVisibility routes into SetVisibilityAnimated passes this call through.
		TGuardValue<bool> Guard(bApplyingVisibility, true);
		Owner->SetVisibility(InVisibility);
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

	// The extra layers have no Anim ID, so they only join a "play everything" call.
	if (AnimIds.Num() == 0)
	{
		const TArray<FWidgetAnimSettings>& Extra = bReverse ? ExtraAnimOut : ExtraAnimIn;
		UWidgetAnimLibrary::StartWidgetAnimTrack(AnimPlayer, NAME_None, ExtraAnimSourceIndex, Extra, bReverse, PlaybackSpeed, bRestartIfPlaying, bHoldFinalPose);
	}

	ApplyCurrentAnimPose();
}

void UWidgetAnimationComponent::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	UWidgetAnimLibrary::StopWidgetAnimTracks(AnimPlayer, AnimIds, /*bRemoveTracks=*/bResetToStart);

	if (bResetToStart)
	{
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(AnimBaselines);
		ApplyCurrentAnimPose();
	}
}

bool UWidgetAnimationComponent::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(AnimPlayer, AnimIds);
}

// =====================================================================================================
// Designer preview
// =====================================================================================================

void UWidgetAnimationComponent::NotifyOwnerDesignTimeRefresh()
{
	UWidget* Owner = AnimOwner.Get();
	if (!Owner || !Owner->IsDesignTime())
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

	// Always captured at design time: Off has to be able to put everything back exactly.
	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(Owner, FlattenedIn, FlattenedOut, AnimBaselines);

	// Fresh state: the preview starts again from the top whenever the designer rebuilds the widget.
	DesignerPreviewState = FWidgetAnimPreviewState();

	if (DesignerPreview.Mode == EWidgetAnimPreviewMode::Off)
	{
		StopDesignerPreview();
		return;
	}

	PreviewRetriesLeft = PreviewTimerRetryFrames;
	EnsureDesignerPreviewTimer();
}

void UWidgetAnimationComponent::EnsureDesignerPreviewTimer()
{
	UWidget* Owner = AnimOwner.Get();
	if (!Owner || !Owner->IsDesignTime() || DesignerPreview.Mode == EWidgetAnimPreviewMode::Off)
	{
		return;
	}

	if (PreviewTimerHandle.IsValid() && PreviewTimerWidget.IsValid())
	{
		return;
	}

	TSharedPtr<SWidget> SlateWidget = Owner->GetCachedWidget();
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
	TSharedRef<FActiveTimerHandle> Handle = SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateWeakLambda(this,
		[this](double /*CurrentTime*/, float DeltaTime) -> EActiveTimerReturnType
		{
			UWidget* PreviewOwner = AnimOwner.Get();
			if (!PreviewOwner)
			{
				return EActiveTimerReturnType::Stop;
			}

			const bool bNeedsMoreFrames = UWidgetAnimLibrary::TickWidgetAnimPreview(
				PreviewOwner, FlattenedIn, FlattenedOut, DesignerPreviewState, DesignerPreview, AnimBaselines, DeltaTime);

			return bNeedsMoreFrames ? EActiveTimerReturnType::Continue : EActiveTimerReturnType::Stop;
		}));

	PreviewTimerHandle = Handle;
	PreviewTimerWidget = SlateWidget;
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