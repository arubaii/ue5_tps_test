// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardTypes.h"

#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"

float FInfoCardConfigurableFloat::Resolve(EInfoCardConfigurableFloatKind Kind) const
{
	if (Source == EInfoCardValueSource::Custom)
	{
		return CustomValue;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();

	float ResolvedValue = CustomValue;
	if (Settings && Settings->TryGetPresetValue(PresetName, Kind, ResolvedValue))
	{
		return ResolvedValue;
	}

	// No settings CDO (shouldn't happen), or PresetName doesn't match any
	// current preset — the preset this entry pointed at was renamed or
	// deleted since. Either way, fall back to CustomValue rather than
	// silently picking some other preset.
	return CustomValue;
}

bool FInfoCardConfigurableBool::Resolve(EInfoCardConfigurableBoolKind Kind) const
{
	if (Source == EInfoCardValueSource::Custom)
	{
		return CustomValue;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	if (!Settings)
	{
		return CustomValue;
	}

	// No PresetName to look up here — see FInfoCardConfigurableBool's own
	// comment for why these are single flat project-wide bools rather than
	// per-named-preset the way the float version is. Direct field access,
	// matching how Settings->bEnableSuccessiveHover is already read
	// elsewhere, rather than the PresetName-lookup path the float version
	// uses. bDismissClickGetsConsumedByDefault / bFlipToAvoidClippingByDefault
	// are the two new fields this feature needs added to
	// UInfoCardDeveloperSettings.
	switch (Kind)
	{
	case EInfoCardConfigurableBoolKind::DismissClickGetsConsumed:
		return Settings->bDismissClickGetsConsumedByDefault;

	case EInfoCardConfigurableBoolKind::FlipToAvoidClipping:
		return Settings->bFlipToAvoidClippingByDefault;

	default:
		return CustomValue;
	}
}

const FUISoundSlot& FInfoCardSoundSet::GetSoundForEvent(EInfoCardSoundEvent Event) const
{
	switch (Event)
	{
	case EInfoCardSoundEvent::Triggered: return Triggered;
	case EInfoCardSoundEvent::Hidden:    return Hidden;
	case EInfoCardSoundEvent::Shown:
	default:                             return Shown;
	}
}

UWorld* UNameTBD_Payload::GetWorld() const
{
	// Payloads are instanced sub-objects; borrow the world from whatever owns us
	// so Blueprint subclasses can still use world-context nodes.
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return nullptr;
	}

	if (const UObject* Owner = GetOuter())
	{
		return Owner->GetWorld();
	}

	return nullptr;
}

FWidgetAnimSettings FInfoCardAdditiveAnimSettings::ToWidgetAnimSettings() const
{
	FWidgetAnimSettings Out;

	switch (AnimType)
	{
	case EInfoCardAdditiveAnimType::Fade:   Out.AnimType = EWidgetAnimType::Fade;   break;
	case EInfoCardAdditiveAnimType::Scale:  Out.AnimType = EWidgetAnimType::Scale;  break;
	case EInfoCardAdditiveAnimType::Slide:  Out.AnimType = EWidgetAnimType::Slide;  break;
	case EInfoCardAdditiveAnimType::Rotate: Out.AnimType = EWidgetAnimType::Rotate; break;
	case EInfoCardAdditiveAnimType::Pulse:  Out.AnimType = EWidgetAnimType::Pulse;  break;
	}

	Out.Duration         = Duration;
	Out.Delay            = Delay;
	Out.Easing           = Easing;
	Out.CustomCurve      = CustomCurve;
	Out.bLoop            = bLoop;
	Out.bPingPong        = bPingPong;
	Out.LoopCount        = LoopCount;
	Out.bInvertAnimation = bInvertAnimation;
	Out.bMirrorInOut     = bMirrorInOut;

	Out.StartOpacity = StartOpacity;
	Out.EndOpacity   = EndOpacity;

	Out.StartScale = FVector2D(StartScale, StartScale);
	Out.EndScale   = FVector2D(EndScale, EndScale);
	Out.Overshoot  = Overshoot;

	Out.SlideFromDirection = SlideFromDirection;
	Out.SlideDistance      = SlideDistance;
	Out.SlideOvershoot     = SlideOvershoot;

	Out.StartAngle = StartAngle;
	Out.EndAngle   = EndAngle;

	Out.PulseScaleAmount = PulseScaleAmount;

	// No TargetWidget: UWidgetAnimLibrary::ApplyWidgetAnimResult falls back to
	// the widget being animated, which is the card.
	Out.TargetWidget = nullptr;
	Out.MaterialTargetImage = nullptr;
	return Out;
}

bool FInfoCardAdditiveAnimSettings::FromLegacyWidgetAnimSettings(const FWidgetAnimSettings& Legacy, FInfoCardAdditiveAnimSettings& Out)
{
	switch (Legacy.AnimType)
	{
	case EWidgetAnimType::Fade:   Out.AnimType = EInfoCardAdditiveAnimType::Fade;   break;
	case EWidgetAnimType::Scale:  Out.AnimType = EInfoCardAdditiveAnimType::Scale;  break;
	case EWidgetAnimType::Slide:  Out.AnimType = EInfoCardAdditiveAnimType::Slide;  break;
	case EWidgetAnimType::Rotate: Out.AnimType = EInfoCardAdditiveAnimType::Rotate; break;
	case EWidgetAnimType::Pulse:  Out.AnimType = EInfoCardAdditiveAnimType::Pulse;  break;
	default:
		return false; // MaterialScalar / MaterialVector
	}

	Out.Duration         = Legacy.Duration;
	Out.Delay            = Legacy.Delay;
	Out.Easing           = Legacy.Easing;
	Out.CustomCurve      = Legacy.CustomCurve;
	Out.bLoop            = Legacy.bLoop;
	Out.bPingPong        = Legacy.bPingPong;
	Out.LoopCount        = Legacy.LoopCount;
	Out.bInvertAnimation = Legacy.bInvertAnimation;
	Out.bMirrorInOut     = Legacy.bMirrorInOut;

	Out.StartOpacity = Legacy.StartOpacity;
	Out.EndOpacity   = Legacy.EndOpacity;

	Out.StartScale = static_cast<float>(Legacy.StartScale.X);
	Out.EndScale   = static_cast<float>(Legacy.EndScale.X);
	Out.Overshoot  = Legacy.Overshoot;

	Out.SlideFromDirection = Legacy.SlideFromDirection;
	Out.SlideDistance      = Legacy.SlideDistance;
	Out.SlideOvershoot     = Legacy.SlideOvershoot;

	Out.StartAngle = Legacy.StartAngle;
	Out.EndAngle   = Legacy.EndAngle;

	Out.PulseScaleAmount = Legacy.PulseScaleAmount;
	return true;
}

void FInfoCardAdditiveAnimations::ResolveInto(TArray<FWidgetAnimSettings>& OutIn, TArray<FWidgetAnimSettings>& OutOut) const
{
	if (!bUseBasicAnimations)
	{
		// Mirroring is resolved at the ENTRY level before any settings are built, so an entry marked Mirror In Out
		// contributes its layers to both stacks rather than only to the one it was listed in - the same order a
		// Widget Animation Component resolves its own Anim In / Anim Out in.
		TArray<FWidgetAnimEntryBase> EntriesIn;
		TArray<FWidgetAnimEntryBase> EntriesOut;
		UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimIn, EntriesIn, EntriesOut);
		UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimOut, EntriesOut, EntriesIn);

		// Null target throughout: an additive entry animates the whole card, and the animation library falls
		// back to the widget it is given when an entry names no target.
		for (const FWidgetAnimEntryBase& Entry : EntriesIn)
		{
			Entry.BuildSettings(OutIn, nullptr);
		}

		for (const FWidgetAnimEntryBase& Entry : EntriesOut)
		{
			Entry.BuildSettings(OutOut, nullptr);
		}
		return;
	}

	// Converted rather than appended directly, because the additive entries are the trimmed-down struct
	// (no target picker) and the library only knows about FWidgetAnimSettings.
	TArray<FWidgetAnimSettings> ConvertedIn;
	ConvertedIn.Reserve(PlayIn.Num());
	for (const FInfoCardAdditiveAnimSettings& Entry : PlayIn)
	{
		ConvertedIn.Add(Entry.ToWidgetAnimSettings());
	}

	TArray<FWidgetAnimSettings> ConvertedOut;
	ConvertedOut.Reserve(PlayOut.Num());
	for (const FInfoCardAdditiveAnimSettings& Entry : PlayOut)
	{
		ConvertedOut.Add(Entry.ToWidgetAnimSettings());
	}

	// Mirroring goes through the library so an additive entry marked Mirror In Out behaves exactly like
	// one on the card class's own Anim In - the card would otherwise mirror its own and not these.
	UWidgetAnimLibrary::AppendWithMirroring(ConvertedIn, OutIn, OutOut);
	UWidgetAnimLibrary::AppendWithMirroring(ConvertedOut, OutOut, OutIn);
}

bool FInfoCardAdditiveAnimations::MigrateLegacyAnimations(TArray<FWidgetAnimEntryBase>& LegacyAnimations)
{
	if (LegacyAnimations.IsEmpty())
	{
		return false;
	}

	// Before the switch, the shared list and the basic entries all played together. Only one side plays now; the
	// assets are the richer of the two, so they win, and the basic entries stay saved for switching back.
	AnimIn.Append(LegacyAnimations);
	LegacyAnimations.Reset();
	bUseBasicAnimations = false;
	return true;
}

void FInfoCardAdditiveAnimations::PostSerialize(const FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return;
	}

	MigrateLegacyAnimations(Animations);

	if (PlayInAnimations.IsEmpty() && PlayOutAnimations.IsEmpty())
	{
		return;
	}

	auto Migrate = [](TArray<FWidgetAnimSettings>& Legacy, TArray<FInfoCardAdditiveAnimSettings>& Destination)
	{
		for (const FWidgetAnimSettings& LegacyEntry : Legacy)
		{
			FInfoCardAdditiveAnimSettings Converted;
			if (FInfoCardAdditiveAnimSettings::FromLegacyWidgetAnimSettings(LegacyEntry, Converted))
			{
				Destination.Add(MoveTemp(Converted));
			}
		}
		Legacy.Reset();
	};

	Migrate(PlayInAnimations, PlayIn);
	Migrate(PlayOutAnimations, PlayOut);
}

// =================================================================================================
// FInfoCardPositioning
// =================================================================================================

void FInfoCardPositioning::ApplyTo(FInfoCardSpec& Spec, bool bFromKeyboard) const
{
	// A key or a gamepad opening has no click to open at: beside the widget instead.
	const bool bBesideWidget = PositionMode == EInfoCardPosition::ToCursor && bFromKeyboard;
	Spec.PositionMode = bBesideWidget ? EInfoCardPosition::ToIcon : PositionMode;
	Spec.IconPosition = bBesideWidget ? KeyboardIconPosition : IconPosition;
	Spec.FillIconPadding = FillIconPadding;
	Spec.IconAnchorPadding = IconAnchorPadding;
	Spec.CursorPosition = CursorPosition;
	Spec.ViewportPosition = ViewportPosition;
	Spec.CardOffset = CardOffset;
	Spec.bClampToViewport = bClampToViewport;
	Spec.FlipToAvoidClipping.Source = EInfoCardValueSource::Custom;
	Spec.FlipToAvoidClipping.CustomValue = bFlipToAvoidClipping;
	Spec.ClampPadding = ClampPadding;
	Spec.bAvoidOverlappingCards = bAvoidOverlappingCards;
	// At the cursor a menu stays where it opened unless asked to follow; beside a widget it follows the widget, as cards do.
	Spec.bUpdatePositionOnTick = Spec.PositionMode == EInfoCardPosition::ToCursor ? bFollowCursor : true;
	Spec.CardTransform = CardTransform;
}

FInfoCardPositioning FInfoCardPositioning::FromSpec(const FInfoCardSpec& Spec)
{
	FInfoCardPositioning Out;
	Out.PositionMode = Spec.PositionMode;
	Out.IconPosition = Spec.IconPosition;
	Out.FillIconPadding = Spec.FillIconPadding;
	Out.IconAnchorPadding = Spec.IconAnchorPadding;
	Out.CursorPosition = Spec.CursorPosition;
	Out.ViewportPosition = Spec.ViewportPosition;
	Out.CardOffset = Spec.CardOffset;
	Out.bClampToViewport = Spec.bClampToViewport;
	Out.bFlipToAvoidClipping = Spec.FlipToAvoidClipping.Resolve(EInfoCardConfigurableBoolKind::FlipToAvoidClipping);
	Out.ClampPadding = Spec.ClampPadding;
	Out.bAvoidOverlappingCards = Spec.bAvoidOverlappingCards;
	Out.bFollowCursor = Spec.bUpdatePositionOnTick;
	Out.CardTransform = Spec.CardTransform;
	return Out;
}

FInfoCardPositioning FInfoCardPositioning::MakeBeside(EDirection2D Side, float Gap)
{
	FInfoCardPositioning Beside;
	Beside.PositionMode = EInfoCardPosition::ToIcon;
	Beside.IconPosition = Side;
	Beside.IconAnchorPadding = Gap;
	Beside.bClampToViewport = true;
	Beside.bFlipToAvoidClipping = true;
	// As an Info Card entry's default; a menu beside a row slides clear of the rest of its chain rather than covering it.
	Beside.bAvoidOverlappingCards = true;
	return Beside;
}
