// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBuilder.h"
#include "WidgetAnimTransform.h"

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

#if WITH_EDITOR
	/**
	 * What one builder class's preview was showing, kept outside any one preview instance.
	 *
	 * The designer destroys its preview and builds a new one on compile and on some edits; the new instance
	 * starts with every transient at its default - paused on the first frame - so without this every rebuild
	 * threw away the key the preview had just been taken to. Keyed by class, the same way the toolbar
	 * addresses previews, so every designer tab of one builder tells the same story.
	 */
	struct FWidgetAnimBuilderSession
	{
		bool bPaused = true;
		bool bScrubbing = false;
		bool bShowingRestPose = false;
		float ScrubAlpha = 0.f;
		int32 FocusedLayerIndex = INDEX_NONE;
		int32 FocusedKeyIndex = INDEX_NONE;

		/** The animation this was recorded against. A scrub only carries over onto the same animation. */
		uint32 StructureSignature = 0;
	};

	TMap<TWeakObjectPtr<const UClass>, FWidgetAnimBuilderSession>& GetBuilderSessions()
	{
		static TMap<TWeakObjectPtr<const UClass>, FWidgetAnimBuilderSession> Sessions;
		return Sessions;
	}

	FDelegateHandle TransformSyncListenerHandle;

	/**
	 * Which Transform key an edit was made inside, from the property chain. False unless the chain passes
	 * through FWidgetTransformAnim::Keys - checked by owner as well as by name, since FRichCurve has a Keys
	 * array too - and both indices are known.
	 */
	bool FindEditedTransformKey(FPropertyChangedChainEvent& PropertyChangedEvent, int32& OutLayerIndex, int32& OutKeyIndex)
	{
		// Adding or removing keys is the curve's business; Keys is fixed-size and only ever has values edited.
		constexpr uint32 StructuralChanges = EPropertyChangeType::ArrayAdd
			| EPropertyChangeType::ArrayRemove
			| EPropertyChangeType::ArrayClear
			| EPropertyChangeType::ArrayMove
			| EPropertyChangeType::Duplicate;

		if (PropertyChangedEvent.ChangeType & StructuralChanges)
		{
			return false;
		}

		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);
		bool bThroughKeys = false;

		for (FEditPropertyChain::TDoubleLinkedListNode* Node = PropertyChangedEvent.PropertyChain.GetHead(); Node; Node = Node->GetNextNode())
		{
			const FProperty* Property = Node->GetValue();
			if (Property && Property->GetFName() == KeysName && Property->GetOwnerStruct() == FWidgetTransformAnim::StaticStruct())
			{
				// The Keys array itself being the leaf means the whole array changed, not one key's values.
				bThroughKeys = Node->GetNextNode() != nullptr;
				break;
			}
		}

		if (!bThroughKeys)
		{
			return false;
		}

		OutLayerIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_STRING_CHECKED(UWidgetAnimBuilder, Layers));
		OutKeyIndex = PropertyChangedEvent.GetArrayIndex(KeysName.ToString());
		return OutLayerIndex != INDEX_NONE && OutKeyIndex != INDEX_NONE;
	}
#endif
}

#if WITH_EDITOR
const FName UWidgetAnimBuilder::BuilderBlueprintName(TEXT("WidgetAnimationBuilder"));
#endif

UWidgetAnimBuilder::UWidgetAnimBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The animation as authored, first key to last, once - see PlayAnimation, which keeps it on this. It used
	// to be Loop In and Out, which follows every entrance with the same keys run backwards as the exit and
	// parks longest on the LAST key between the two, so an animation being built looked like it was playing
	// in reverse - and flipping Invert only swapped the two halves, so it looked reversed either way. An
	// asset has no direction of its own; the exit is only one use of it.
	Preview.Mode = EWidgetAnimPreviewMode::PlayIn;
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

UWidget* UWidgetAnimBuilder::ResolvePreviewGroupTarget(FName Path)
{
	if (!Path.IsNone())
	{
		if (UWidget* Found = UWidgetAnimLibrary::FindWidgetByPathInTree(this, Path))
		{
			return Found;
		}
	}

	// A generic animation names no widget - and one previewed here from an asset built against another
	// Widget Blueprint names widgets this builder doesn't have. Either way it plays on the preview image.
	return GetPreviewAnimationTarget();
}

void UWidgetAnimBuilder::PostLoad()
{
	Super::PostLoad();

	UWidgetAnimLibrary::NormalizeLayerGroups(LayerGroups, Layers);
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
		PreviewAsset->LayerGroups = LayerGroups;
		PreviewAsset->Layers = Layers;
		PreviewAsset->AnimationSpeedMultiplier = AnimationSpeedMultiplier;
		PreviewAsset->MinimumLength = MinimumLength;
		AnimationToPlay = PreviewAsset;
	}

	// The preview is one use of the animation, stamped exactly the way an entry in a real widget would be.
	// No Target Widgets of its own, so every group plays on its Preview Target - see ResolvePreviewGroupTarget.
	FWidgetAnimEntry PreviewUse;
	PreviewUse.Animation = AnimationToPlay;
	PreviewUse.bLoop = bPreviewLoop;
	PreviewUse.bPingPong = bPreviewPingPong;

	// Loop means loop, with no count to run out: the preview is something to watch while judging the
	// animation, and a count that stops after three passes is just a slower way of not looping.
	PreviewUse.LoopCount = 0;

	// Straight through. This used to be negated, on the belief that the entry's flag read backwards - it does
	// not: an unticked entry plays forward on the entrance stack. The negation inverted the entrance by
	// default, which ran every layer, and every Transform layer's keys, from the end back to the start.
	PreviewUse.bInvertAnimation = bPreviewInvertAnimation;
	PreviewUse.PlaybackSpeed = PreviewPlaybackSpeed;

	auto Resolver = [this](FName Path) { return ResolvePreviewGroupTarget(Path); };
	PreviewUse.BuildSettingsResolved(FlattenedIn, Resolver);

#if WITH_EDITOR
	// Tracks hidden in the Widget Animations panel keep their place in time but pose nothing here.
	UWidgetAnimLibrary::MuteHiddenLayers(AnimationToPlay->Layers, FlattenedIn);
#endif
	FlattenedOut = FlattenedIn;

	// One pass, measured off a copy of the same use built without looping.
	//
	// A looping stack has no duration - GetStackDuration answers -1, because it never finishes - but the
	// animation inside it still takes as long as it always did, and that is the number the readout wants
	// to count towards whether Loop is on or not.
	if (bPreviewLoop)
	{
		FWidgetAnimEntry SinglePassUse = PreviewUse;
		SinglePassUse.bLoop = false;

		TArray<FWidgetAnimSettings> SinglePassSettings;
		SinglePassUse.BuildSettingsResolved(SinglePassSettings, Resolver);
		PreviewPassSeconds = UWidgetAnimLibrary::GetStackDuration(SinglePassSettings);
	}
	else
	{
		PreviewPassSeconds = 0.f;
	}
}

void UWidgetAnimBuilder::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// Returns immediately unless this is a live widget in the designer. Rebuilds the stacks and puts the
	// widget back as authored before re-reading its baselines.
	UWidgetAnimDesignerPreview::Synchronize(this);

	if (!IsDesignTime())
	{
		return;
	}

	// A new preview instance picks up where the one it replaced left off.
	RestoreSession();

	// Synchronize has just put the widget back as authored. The designer calls this on its own - after
	// every Details edit on a placed builder, among other times - and nothing re-posed the widget after it,
	// which is how a preview paused on a key lost the key's pose while the readout still showed its time.
	ApplyCurrentPose();
}

void UWidgetAnimBuilder::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (!IsDesignTime())
	{
		return;
	}

	// SynchronizeProperties can run before the Slate widget exists; this is the pass where it does. A
	// paused preview needs no clock, only its frame drawn.
	if (!bPreviewPaused && UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		EnsurePreviewTimer();
	}

	ApplyCurrentPose();
}

void UWidgetAnimBuilder::ReleaseSlateResources(bool bReleaseChildren)
{
	UWidgetAnimDesignerPreview::Shutdown(this);
	bRepaintQueued = false;
	Super::ReleaseSlateResources(bReleaseChildren);
}

void UWidgetAnimBuilder::BeginDestroy()
{
	UWidgetAnimDesignerPreview::Shutdown(this);
	Super::BeginDestroy();
}

void UWidgetAnimBuilder::EnsurePreviewTimer()
{
	if (bPreviewPaused)
	{
		RequestDesignerRepaint();
		return;
	}

	UWidgetAnimDesignerPreview::EnsureTimer(this);
}

uint32 UWidgetAnimBuilder::ComputeStructureSignature() const
{
	// Only what makes it a different animation. Values - durations, delays, key poses, shapes, speed - are
	// left out on purpose: changing those is exactly the edit that should NOT start the preview over.
	uint32 Hash = GetTypeHash(FlattenedIn.Num());
	Hash = HashCombine(Hash, PointerHash(PreviewAnimation.Get()));

	for (const FWidgetAnimSettings& Layer : FlattenedIn)
	{
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.AnimType)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.bLoop)));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.bPingPong)));

		// Not Invert: it plays the same animation from the other end, over the same span. Every layer's clock
		// stays valid, so flipping it re-poses the preview where it is instead of starting it over at 0.00.

		if (Layer.AnimType == EWidgetAnimType::Transform)
		{
			// The curve's count rather than Keys': Keys only catches up a tick later, and counting it would
			// restart the preview twice for one added key.
			const FRichCurve* Curve = Layer.Transform.GetActiveCurve();
			Hash = HashCombine(Hash, GetTypeHash(Curve ? Curve->GetNumKeys() : 0));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.Transform.bAnimateShape)));
		}
	}

	return Hash;
}

void UWidgetAnimBuilder::RestartPreviewClock()
{
	UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);

	// The scrub pose and the focused key belonged to the run that has just been thrown away.
	bPreviewScrubbing = false;
	PreviewScrubAlpha = 0.f;
	FocusedLayerIndex = INDEX_NONE;
	FocusedKeyIndex = INDEX_NONE;
	bShowingRestPose = false;
}

void UWidgetAnimBuilder::RefreshPreview(bool bForceRestart, bool bAnimate)
{
	if (!IsDesignTime())
	{
		return;
	}

	// Re-reads the stacks and baselines, leaving the widget at its authored look.
	UWidgetAnimDesignerPreview::Synchronize(this);

	// Usually already done by SynchronizeProperties. When this is the first thing a new instance is asked to
	// do, the session has to be adopted before its signature is compared, or it would start over for nothing.
	RestoreSession();

	const uint32 NewSignature = ComputeStructureSignature();
	if (bForceRestart || NewSignature != StructureSignature)
	{
		RestartPreviewClock();
	}
	StructureSignature = NewSignature;

	if (!bAnimate)
	{
		// Reset: stay on the authored look Synchronize left, frozen there until Play, a scrub or a key edit.
		bShowingRestPose = true;
		RequestDesignerRepaint();
		SaveSession();
		return;
	}

	ApplyCurrentPose();
	SaveSession();
}

void UWidgetAnimBuilder::ApplyCurrentPose()
{
	if (!IsDesignTime())
	{
		return;
	}

	if (bShowingRestPose)
	{
		// Synchronize already left the authored look on screen; nothing goes on top of it.
		RequestDesignerRepaint();
		return;
	}

	if (bPreviewScrubbing)
	{
		// A focused key is followed rather than remembered as a fraction: its time, the layer's delay or the
		// animation's length may just have changed, and the pose wanted is still that key's.
		if (FocusedLayerIndex != INDEX_NONE)
		{
			const float KeyAlpha = GetTransformKeyScrubAlpha(FocusedLayerIndex, FocusedKeyIndex);
			if (KeyAlpha >= 0.f)
			{
				PreviewScrubAlpha = KeyAlpha;
			}
			else
			{
				// The key or its layer is gone. Stay where the slider is, as an ordinary scrub.
				FocusedLayerIndex = INDEX_NONE;
				FocusedKeyIndex = INDEX_NONE;
			}
		}

		// A scrub walks the entrance. Putting the harness back on it too means Play carries on from there.
		if (IsShowingOutStack())
		{
			UWidgetAnimLibrary::RestartWidgetAnimPreview(PreviewState);
		}

		// Posed straight onto the widget rather than through the harness, so this works whatever Preview Mode
		// is set to and without the mode having to be switched to Scrub behind the user's back.
		UWidgetAnimLibrary::ApplyWidgetAnimationsAtAlpha(
			GetPreviewAnimationTarget(),
			FlattenedIn,
			PreviewScrubAlpha,
			/*bIsOutAnimation=*/false,
			PreviewBaselines);

		// The harness's own Scrub mode would otherwise think its last pose is still on screen.
		PreviewState.LastScrubAlpha = -1.f;
	}
	else if (UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		// Synchronize put the widget back as authored, so Scrub mode's "slider hasn't moved" check has to be
		// told its last pose is gone too.
		PreviewState.LastScrubAlpha = -1.f;

		// The harness's current frame, at zero delta: posed, not advanced.
		UWidgetAnimDesignerPreview::TickFrame(this, 0.f);
	}

	RequestDesignerRepaint();
}

void UWidgetAnimBuilder::RequestDesignerRepaint()
{
	const TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	SlateWidget->Invalidate(EInvalidateWidgetReason::Paint);

	// A running clock repaints every frame already, and one queued repaint is enough.
	if (PreviewState.bTimerActive || bRepaintQueued)
	{
		return;
	}

	// The designer viewport is not repainted just because a widget in it changed, so a one-off pose - a
	// scrub, a key edit, a paused refresh - asks for a single frame. An active timer is what gets Slate to
	// tick and paint; this one stops after its first call, so nothing is left ticking while paused.
	bRepaintQueued = true;

	const TWeakObjectPtr<UWidgetAnimBuilder> WeakThis(this);
	SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakThis](double, float) -> EActiveTimerReturnType
		{
			if (UWidgetAnimBuilder* Builder = WeakThis.Get())
			{
				Builder->bRepaintQueued = false;
			}

			return EActiveTimerReturnType::Stop;
		}));
}

void UWidgetAnimBuilder::SaveSession() const
{
#if WITH_EDITOR
	if (!IsDesignTime())
	{
		return;
	}

	FWidgetAnimBuilderSession& Session = GetBuilderSessions().FindOrAdd(TWeakObjectPtr<const UClass>(GetClass()));
	Session.bPaused = bPreviewPaused;
	Session.bScrubbing = bPreviewScrubbing;
	Session.bShowingRestPose = bShowingRestPose;
	Session.ScrubAlpha = PreviewScrubAlpha;
	Session.FocusedLayerIndex = FocusedLayerIndex;
	Session.FocusedKeyIndex = FocusedKeyIndex;
	Session.StructureSignature = StructureSignature;
#endif
}

void UWidgetAnimBuilder::RestoreSession()
{
	if (bSessionRestored || !IsDesignTime())
	{
		return;
	}

	bSessionRestored = true;

	// Measured first, so the session can tell whether it was recorded against this same animation, and so
	// the next refresh has something to compare against.
	StructureSignature = ComputeStructureSignature();

#if WITH_EDITOR
	const FWidgetAnimBuilderSession* Session = GetBuilderSessions().Find(TWeakObjectPtr<const UClass>(GetClass()));
	if (!Session)
	{
		return;
	}

	bPreviewPaused = Session->bPaused;

	// A frozen frame only means anything on the animation it was frozen on. A rebuild caused by adding a
	// layer starts that new animation from the top, the same as the old instance would have.
	if (Session->StructureSignature == StructureSignature)
	{
		bPreviewScrubbing = Session->bScrubbing;
		bShowingRestPose = Session->bShowingRestPose;
		PreviewScrubAlpha = Session->ScrubAlpha;
		FocusedLayerIndex = Session->FocusedLayerIndex;
		FocusedKeyIndex = Session->FocusedKeyIndex;
	}

	// Synchronize ran while this instance still thought it was paused, so a playing session needs its clock.
	if (!bPreviewPaused && UWidgetAnimDesignerPreview::ShouldRun(this))
	{
		EnsurePreviewTimer();
	}
#endif
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

	if (!bPreviewPaused)
	{
		// The clock never moved with the scrub slider, so playing on from a scrubbed frame (or a focused key)
		// would jump back to wherever the preview was frozen. Starting over is the honest reading of "play
		// this animation". Same after Reset, whose resting look is not a frame of the animation at all.
		// Same once a Play In has run to its end and is holding the last frame: playing again means playing
		// it again, from the top. A looping entrance never ends up here - it is still moving while Shown.
		const bool bHoldingEnd = PreviewState.bStarted
			&& Preview.Mode == EWidgetAnimPreviewMode::PlayIn
			&& !bPreviewLoop
			&& PreviewState.Playback.Phase == EWidgetAnimPhase::Shown;

		if (bPreviewScrubbing || bShowingRestPose || bHoldingEnd)
		{
			RestartPreviewClock();
		}

		// Pausing let the timer stop, so playing needs a fresh one. EnsureTimer no-ops if one is still running.
		if (UWidgetAnimDesignerPreview::ShouldRun(this))
		{
			EnsurePreviewTimer();
		}
	}

	// Posed once either way, so the freeze and the release are both visible on the frame they happen.
	ApplyCurrentPose();
	SaveSession();
}

bool UWidgetAnimBuilder::IsShowingOutStack() const
{
	return PreviewState.Playback.Phase == EWidgetAnimPhase::PlayingOut
		|| PreviewState.Playback.Phase == EWidgetAnimPhase::Hidden;
}

float UWidgetAnimBuilder::GetPreviewElapsedSeconds() const
{
	// A scrub never moves the playing clock - it poses a frame directly - so while scrubbing the readout
	// reports where the scrub is instead. Same clock ApplyWidgetAnimationsAtAlpha measures the alpha on.
	if (bPreviewScrubbing)
	{
		return PreviewScrubAlpha * UWidgetAnimLibrary::GetExpandedPassDuration(const_cast<UWidgetAnimBuilder*>(this)->GetPreviewAnimationTarget(), FlattenedIn);
	}

	const bool bShowingOut = IsShowingOutStack();
	const float RawElapsed = bShowingOut
		? UWidgetAnimLibrary::GetStackElapsed(FlattenedOut, PreviewState.Playback.OutStates)
		: UWidgetAnimLibrary::GetStackElapsed(FlattenedIn, PreviewState.Playback.InStates);

	// While looping, the clock is reported against one pass - it walks up to the total and starts again,
	// or walks back down it when Ping Pong is on, which is what the animation is visibly doing.
	float Position = RawElapsed;
	if (bPreviewLoop && PreviewPassSeconds > 0.f)
	{
		const float RepeatSeconds = bPreviewPingPong ? PreviewPassSeconds * 2.f : PreviewPassSeconds;
		const float InRepeat = FMath::Fmod(FMath::Max(RawElapsed, 0.f), RepeatSeconds);
		Position = InRepeat > PreviewPassSeconds ? RepeatSeconds - InRepeat : InRepeat;
	}

	// The exit is the entrance played backwards, so time spent on it walks the animation back towards its
	// start: a position, where the pose actually is, rather than a stopwatch that counts up either way.
	// Reported as a stopwatch, the slider sat on the opposite side from the pose all through the exit, and
	// scrubbing out of a paused exit jumped to the mirrored frame - the inverted scrub. Exact for a single
	// layer; for staggered layers, where each one reverses on its own delay, it is the nearest single time.
	if (bShowingOut)
	{
		const float TotalSeconds = GetPreviewTotalSeconds();
		if (TotalSeconds > 0.f)
		{
			Position = FMath::Clamp(TotalSeconds - Position, 0.f, TotalSeconds);
		}
	}

	return Position;
}

float UWidgetAnimBuilder::GetPreviewTotalSeconds() const
{
	if (bPreviewLoop && PreviewPassSeconds > 0.f)
	{
		return PreviewPassSeconds;
	}

	return IsShowingOutStack()
		? UWidgetAnimLibrary::GetStackDuration(FlattenedOut)
		: UWidgetAnimLibrary::GetStackDuration(FlattenedIn);
}

void UWidgetAnimBuilder::OnPreviewFinished()
{
	// Reached when a Play Once has drawn its final frame and the harness has no more frames to give. It
	// stays on that frame, frozen, the way a timeline stops at its end - the playhead parked on the last
	// moment rather than snapping back to 0 as if nothing had played. Play starts it over from the top.
	//
	// No timer and no repaint request here on purpose: this runs from inside the active timer that is about
	// to stop, and registering another one from in there would be registering it mid-iteration. The frame
	// is already on screen.
	bPreviewPaused = true;

	if (const TSharedPtr<SWidget> SlateWidget = GetCachedWidget())
	{
		SlateWidget->Invalidate(EInvalidateWidgetReason::Paint);
	}

	SaveSession();
}

void UWidgetAnimBuilder::PlayAnimation()
{
	if (Preview.Mode != EWidgetAnimPreviewMode::PlayIn)
	{
		// Through SetPlaybackOption, so the template keeps it too and the next preview instance plays the
		// same way. Restarting is right: whatever the old mode had on screen is not the animation from the top.
		SetPlaybackOption([](UWidgetAnimBuilder& Target) { Target.Preview.Mode = EWidgetAnimPreviewMode::PlayIn; }, /*bRestart=*/true);
	}

	SetPreviewPaused(false);
}

// =====================================================================================================
// Playback controls
// =====================================================================================================

void UWidgetAnimBuilder::SetPlaybackOption(TFunctionRef<void(UWidgetAnimBuilder&)> Apply, bool bRestart)
{
	Apply(*this);

#if WITH_EDITOR
	// The toolbar reaches the live preview instance, which the designer throws away and rebuilds freely.
	// Writing the same value to the object it was built from is what makes the choice stick, and is what
	// keeps the Details panel - which edits that object - telling the same story as the toolbar.
	if (UWidgetAnimBuilder* Template = Cast<UWidgetAnimBuilder>(GetArchetype()))
	{
		if (Template != this)
		{
			Template->Modify();
			Apply(*Template);
			Template->MarkPackageDirty();
		}
	}
#endif

	RefreshPreview(bRestart);
}

void UWidgetAnimBuilder::SetPreviewLoop(bool bLoop)
{
	if (bPreviewLoop == bLoop)
	{
		return;
	}

	// Loop is baked into the flattened stacks, so this is a different animation from the one playing.
	SetPlaybackOption([bLoop](UWidgetAnimBuilder& Target) { Target.bPreviewLoop = bLoop; }, /*bRestart=*/true);
}

void UWidgetAnimBuilder::SetPreviewInvertAnimation(bool bInvert)
{
	if (bPreviewInvertAnimation == bInvert)
	{
		return;
	}

	// No restart: the playhead stays where it is and the pose there flips. Paused or playing, the preview
	// carries on from the same moment - see ComputeStructureSignature, which leaves Invert out for this.
	SetPlaybackOption([bInvert](UWidgetAnimBuilder& Target) { Target.bPreviewInvertAnimation = bInvert; }, /*bRestart=*/false);
}

void UWidgetAnimBuilder::SetPreviewPingPong(bool bPingPong)
{
	if (bPreviewPingPong == bPingPong)
	{
		return;
	}

	SetPlaybackOption([bPingPong](UWidgetAnimBuilder& Target) { Target.bPreviewPingPong = bPingPong; }, /*bRestart=*/true);
}

void UWidgetAnimBuilder::SetPreviewPlaybackSpeed(float Speed)
{
	const float ClampedSpeed = FMath::Max(Speed, 0.01f);
	if (FMath::IsNearlyEqual(PreviewPlaybackSpeed, ClampedSpeed))
	{
		return;
	}

	// No restart: speed is the one playback value worth changing while watching, which is the whole point
	// of quarter speed on a 0.2s pop.
	SetPlaybackOption([ClampedSpeed](UWidgetAnimBuilder& Target) { Target.PreviewPlaybackSpeed = ClampedSpeed; }, /*bRestart=*/false);
}

float UWidgetAnimBuilder::GetPreviewScrubAlpha() const
{
	if (bPreviewScrubbing)
	{
		return PreviewScrubAlpha;
	}

	// Nothing scrubbed, so the slider follows the animation and reads as a progress bar. An endlessly
	// looping stack has no total to be a fraction of, and sits at 0.
	const float TotalSeconds = GetPreviewTotalSeconds();
	if (TotalSeconds <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(GetPreviewElapsedSeconds() / TotalSeconds, 0.f, 1.f);
}

void UWidgetAnimBuilder::SetPreviewScrubAlpha(float Alpha)
{
	if (!IsDesignTime())
	{
		return;
	}

	// Dragging the slider is choosing a frame by hand, so it stops following whichever key was edited last.
	FocusedLayerIndex = INDEX_NONE;
	FocusedKeyIndex = INDEX_NONE;

	BeginScrubAt(Alpha);
}

void UWidgetAnimBuilder::BeginScrubAt(float Alpha)
{
	PreviewScrubAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	bPreviewScrubbing = true;
	bShowingRestPose = false;

	// Frozen directly rather than through SetPreviewPaused, which would pose the harness frame first only
	// for it to be replaced a line later. A clock that was running stops itself on its next tick.
	bPreviewPaused = true;

	ApplyCurrentPose();
	SaveSession();
}

float UWidgetAnimBuilder::GetTransformKeyScrubAlpha(int32 LayerIndex, int32 KeyIndex) const
{
	// FlattenedIn holds one settings entry per authored layer, in order, so the index carries straight over.
	if (!FlattenedIn.IsValidIndex(LayerIndex))
	{
		return -1.f;
	}

	const FWidgetAnimSettings& Layer = FlattenedIn[LayerIndex];

	float KeySeconds = UWidgetAnimLibrary::GetKeyStackTime(Layer, KeyIndex);
	if (KeySeconds < 0.f)
	{
		return -1.f;
	}

	// With Invert on, the layer runs its keys backwards, so its key sits mirrored within the layer's span - a
	// span as long as the layer runs, whatever sets that: a Transform curve, a spring, or Duration.
	if (Layer.bInvertAnimation)
	{
		const float Speed = Layer.GetPlaySpeed();
		const float LayerStart = FMath::Max(Layer.Delay, 0.f) / Speed;
		const float LayerLength = UWidgetAnimLibrary::GetLayerRunSeconds(Layer) / Speed;
		KeySeconds = LayerStart + LayerLength - (KeySeconds - LayerStart);
	}

	// Along the pass as the preview scrubs it, with every stagger fanned out.
	const float PassSeconds = UWidgetAnimLibrary::GetExpandedPassDuration(const_cast<UWidgetAnimBuilder*>(this)->GetPreviewAnimationTarget(), FlattenedIn);
	return PassSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(KeySeconds / PassSeconds, 0.f, 1.f) : 0.f;
}

void UWidgetAnimBuilder::ScrubToTransformKey(int32 LayerIndex, int32 KeyIndex)
{
	if (!IsDesignTime())
	{
		return;
	}

	const float KeyAlpha = GetTransformKeyScrubAlpha(LayerIndex, KeyIndex);
	if (KeyAlpha < 0.f)
	{
		return;
	}

	FocusedLayerIndex = LayerIndex;
	FocusedKeyIndex = KeyIndex;

	// Paused, on the entrance, at the key's moment: readout, slider and pose all agree.
	BeginScrubAt(KeyAlpha);
}

void UWidgetAnimBuilder::ResetPreview()
{
#if WITH_EDITOR
	RefreshLivePreviews(/*bForceRestart=*/true, /*bCopyAuthoredState=*/false, /*bAnimate=*/false);
#else
	RefreshPreview(/*bForceRestart=*/true, /*bAnimate=*/false);
#endif
}

#if WITH_EDITOR

bool UWidgetAnimBuilder::CopyAuthoredStateFrom(const UWidgetAnimBuilder& Source)
{
	static const FName AuthoredProperties[] =
	{
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationName),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, SaveFolder),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewAnimation),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Description),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, LayerGroups),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Layers),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationSpeedMultiplier),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, MinimumLength),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Preview),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewLoop),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewPingPong),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewInvertAnimation),
		GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewPlaybackSpeed),
	};

	const UClass* BuilderClass = UWidgetAnimBuilder::StaticClass();
	bool bChanged = false;

	for (const FName PropertyName : AuthoredProperties)
	{
		const FProperty* Property = BuilderClass->FindPropertyByName(PropertyName);
		if (!Property || Property->Identical_InContainer(&Source, this))
		{
			continue;
		}

		Property->CopyCompleteValue_InContainer(this, &Source);
		bChanged = true;
	}

	return bChanged;
}

void UWidgetAnimBuilder::ForThisAndLivePreviews(TFunctionRef<void(UWidgetAnimBuilder&)> Func)
{
	Func(*this);

	WidgetAnimDesignerSync::ForEachPreviewOf(this, [&Func](UObject* PreviewObject)
	{
		if (UWidgetAnimBuilder* LiveBuilder = Cast<UWidgetAnimBuilder>(PreviewObject))
		{
			Func(*LiveBuilder);
		}
	});
}

void UWidgetAnimBuilder::RefreshLivePreviews(bool bForceRestart, bool bCopyAuthoredState, bool bAnimate)
{
	// The Details panel edits one object - the Class Defaults, the copy a parent Widget Blueprint keeps, or
	// a preview instance the designer migrates edits out of - while the viewport paints preview instances.
	// This object is refreshed if it is itself on screen, and every live preview built from it is brought in
	// line and refreshed too. RefreshPreview decides for each one whether it has to start over.
	RefreshPreview(bForceRestart, bAnimate);

	const UWidgetAnimBuilder* Source = this;
	WidgetAnimDesignerSync::ForEachPreviewOf(this, [Source, bForceRestart, bCopyAuthoredState, bAnimate](UObject* PreviewObject)
	{
		if (UWidgetAnimBuilder* LiveBuilder = Cast<UWidgetAnimBuilder>(PreviewObject))
		{
			if (bCopyAuthoredState)
			{
				LiveBuilder->CopyAuthoredStateFrom(*Source);
			}
			LiveBuilder->RefreshPreview(bForceRestart, bAnimate);
		}
	});
}

void UWidgetAnimBuilder::EnsureSyncListener()
{
	if (!TransformSyncListenerHandle.IsValid())
	{
		TransformSyncListenerHandle = UWidgetAnimTransformLibrary::OnTransformAnimsSynced().AddStatic(&UWidgetAnimBuilder::HandleTransformAnimsSynced);
	}
}

void UWidgetAnimBuilder::HandleTransformAnimsSynced(UObject* Object, bool /*bResized*/)
{
	if (UWidgetAnimBuilder* Builder = Cast<UWidgetAnimBuilder>(Object))
	{
		if (IsValid(Builder))
		{
			// Only what actually differs is copied, and a preview only starts over if the animation's shape
			// changed - so a sync that only retimed keys moves nothing on screen but the keys themselves.
			Builder->RefreshLivePreviews(/*bForceRestart=*/false, /*bCopyAuthoredState=*/true);
		}
		return;
	}

	if (const UWidgetAnimPresetAsset* Asset = Cast<UWidgetAnimPresetAsset>(Object))
	{
		NotifyAnimationAssetEdited(Asset, /*bForceRestart=*/false);
	}
}

void UWidgetAnimBuilder::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Also runs SynchronizeProperties when this object is itself on screen, which re-poses it.
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Keys follow the curve on the next tick, once this edit has unwound, and only for the edits that can
	// put them out of step - never for a bool, an enum or a value inside a key. See RequestSyncTransformAnims.
	EnsureSyncListener();
	UWidgetAnimTransformLibrary::RequestSyncTransformAnims(this, PropertyChangedEvent);

	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();

	if (MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Preview))
	{
		// Mode, holds, speed, scrub alpha: a new way of watching the animation, not a frame to stay frozen
		// on, so any scrub or focused key is let go first. The shared harness then pushes the change
		// mid-drag without disturbing what's playing.
		ForThisAndLivePreviews([](UWidgetAnimBuilder& Builder)
		{
			Builder.bPreviewScrubbing = false;
			Builder.FocusedLayerIndex = INDEX_NONE;
			Builder.FocusedKeyIndex = INDEX_NONE;
			Builder.bShowingRestPose = false;
			Builder.SaveSession();
		});

		UWidgetAnimDesignerPreview::NotifyPropertyChanged(this, PropertyChangedEvent);

		// A committed edit also carries the value across for the kinds of change the harness does not push.
		if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
		{
			RefreshLivePreviews(/*bForceRestart=*/false, /*bCopyAuthoredState=*/true);
		}
		return;
	}

	const bool bAffectsPreview =
		MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, LayerGroups)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Layers)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationSpeedMultiplier)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, MinimumLength)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewAnimation)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Description)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewLoop)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewPingPong)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, bPreviewInvertAnimation)
		|| MemberName == GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, PreviewPlaybackSpeed);

	if (!bAffectsPreview)
	{
		return;
	}

	// Pushed on every change, mid-drag included, rather than left to the designer's own rebuild, which only
	// happens on commit and not reliably for Class Defaults. Not forced to restart: a layer added, a type
	// changed or a key added starts the animation over because the animation's shape changed; a checkbox
	// ticked or a value typed only updates what is on screen - playing carries on, a paused key stays put.
	RefreshLivePreviews(/*bForceRestart=*/false, /*bCopyAuthoredState=*/true);
}

void UWidgetAnimBuilder::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// PostEditChangeProperty runs from in here, so the previews already hold the edited value below.
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	// Editing a keyframe's values takes the preview to that key's time, paused, so the change is seen at the
	// moment it controls - live while a value is still being dragged. Only when Layers is what is playing:
	// with a Preview Animation picked, the Layers being edited are not on screen.
	if (PreviewAnimation)
	{
		return;
	}

	int32 LayerIndex = INDEX_NONE;
	int32 KeyIndex = INDEX_NONE;
	if (!FindEditedTransformKey(PropertyChangedEvent, LayerIndex, KeyIndex))
	{
		return;
	}

	// Whichever of these is on screen: this object when the Details panel is showing a preview directly,
	// every preview built from it when it is showing the Class Defaults or a template.
	ForThisAndLivePreviews([LayerIndex, KeyIndex](UWidgetAnimBuilder& Builder)
	{
		Builder.ScrubToTransformKey(LayerIndex, KeyIndex);
	});
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
			LiveBuilder->RefreshPreview(/*bForceRestart=*/true);
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

void UWidgetAnimBuilder::NotifyAnimationAssetKeyEdited(const UWidgetAnimPresetAsset* Animation, int32 LayerIndex, int32 KeyIndex)
{
	if (!Animation)
	{
		return;
	}

	// ScrubToTransformKey ignores anything that is not on screen.
	for (TObjectIterator<UWidgetAnimBuilder> It; It; ++It)
	{
		UWidgetAnimBuilder* Builder = *It;
		if (IsValid(Builder) && Builder->PreviewAnimation == Animation)
		{
			Builder->ScrubToTransformKey(LayerIndex, KeyIndex);
		}
	}
}

void UWidgetAnimBuilder::NotifyAnimationAssetEdited(const UWidgetAnimPresetAsset* Animation, bool bForceRestart)
{
	if (!Animation)
	{
		return;
	}

	// The asset's keys are synced a tick after this, and the builders previewing it need telling then too.
	EnsureSyncListener();

	// Class defaults are skipped by the iterator, and RefreshPreview ignores anything not on screen. Each one
	// decides for itself whether the edit made it a different animation - see RefreshPreview.
	for (TObjectIterator<UWidgetAnimBuilder> It; It; ++It)
	{
		UWidgetAnimBuilder* Builder = *It;
		if (IsValid(Builder) && Builder->PreviewAnimation == Animation)
		{
			Builder->RefreshPreview(bForceRestart);
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

UWidgetAnimPresetAsset* UWidgetAnimBuilder::CreateWidgetAnimation()
{
#if WITH_EDITOR
	if (PreviewAnimation)
	{
		ShowBuilderNotification(
			LOCTEXT("PreviewingAsset", "Clear Preview Animation first. Create Widget Animation saves the builder's own layers, not the animation being previewed."),
			/*bSuccess=*/false);
		return nullptr;
	}

	if (Layers.Num() == 0)
	{
		ShowBuilderNotification(LOCTEXT("NoLayers", "Add at least one layer before creating a Widget Animation."), /*bSuccess=*/false);
		return nullptr;
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
		return nullptr;
	}

	// Motion only. Loop, ping-pong, invert and speed are per-use decisions and belong on the entry that uses it.
	Asset->Modify();
	Asset->Description = Description;
	Asset->LayerGroups = LayerGroups;
	Asset->Layers = Layers;
	Asset->AnimationSpeedMultiplier = AnimationSpeedMultiplier;
	Asset->MinimumLength = MinimumLength;
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
	return Asset;
#else
	return nullptr;
#endif
}

#undef LOCTEXT_NAMESPACE