// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Slate/WidgetTransform.h"
#include "StructUtils/PropertyBag.h"

#include "NameTBD/Widgets/ShapeBox.h"   // FShapeBoxShape, for a Shape Box track's keys
#include "WidgetAnimEasing.h"           // FWidgetAnimSegmentCurve, for the curve from each key to the next

#include "WidgetAnimTransform.generated.h"

class UShapeBox;
class UShapeWidget;

/**
 * What the keys of a keyframed track hold - which of the three keyframed track types it is. The keys, their times and
 * the curves between them work the same way on all three; only what each key says the widget looks like differs.
 */
UENUM(BlueprintType)
enum class EWidgetKeyframeContent : uint8
{
	/** A Transform track: the widget's render transform at each key. */
	Transform,

	/** A Shape Box track: a Shape Box's outline at each key. */
	ShapeBox UMETA(DisplayName = "Shape Box"),

	/** A Shape track: a shape widget's settings at each key - an Ellipse's, a Star's, a Custom Shape's. */
	Shape
};

// =====================================================================================================
// Keyframe
// =====================================================================================================

/**
 * What the widget looks like at one key of a keyframed track's curve - a Transform, Shape Box or Shape track.
 *
 * Key N here is key N of the curve, counting in time order. The curve says WHEN each key happens and HOW
 * the motion travels between them; this says WHAT the widget looks like when it gets there.
 *
 * Transform is the same Render Transform block every UMG widget has - Translation, Scale, Shear, Angle
 * under one dropdown - which is also how the Context Menu Button's shape states author it.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetTransformKeyframe
{
	GENERATED_BODY()

	/**
	 * When this key happens, in seconds. Read-only: it mirrors the curve key it belongs to and follows it
	 * when that key is moved. Also what the list row is titled by, so the list reads as a timeline.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Key", meta = (DisplayName = "Time (s)"))
	float Time = 0.f;

	/**
	 * The widget's render transform at this key, on a Transform track.
	 *
	 * Composes with every other layer the way the library always has: Translation, Shear and Angle add,
	 * Scale multiplies. With Preserve Authored Transforms on, (30, 0) means thirty units right of wherever
	 * the widget was authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (EditCondition = "Content == EWidgetKeyframeContent::Transform", EditConditionHides, HideEditConditionToggle))
	FWidgetTransform Transform;

	/**
	 * On a Shape Box or Shape track: the shape exactly as it was authored on the widget, at this key, rather than the
	 * settings below - so no shape has to be set up again for a key that only lands back on it. The usual choice for the
	 * last key of an entrance: whatever the shape is distorted from, it settles into its real self, and the same
	 * animation works on every shape it is used on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (DisplayName = "Use Authored Shape", EditCondition = "Content != EWidgetKeyframeContent::Transform", EditConditionHides, HideEditConditionToggle))
	bool bUseAuthoredShape = true;

	/** The Shape Box outline at this key, on a Shape Box track. Blended between keys with FShapeBoxShape::Lerp, so every Shape Box property animates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (EditCondition = "Content == EWidgetKeyframeContent::ShapeBox && !bUseAuthoredShape", EditConditionHides, HideEditConditionToggle))
	FShapeBoxShape Shape;

	/**
	 * The shape's settings at this key, on a Shape track: one field for each setting of the kind of shape the track
	 * animates (see FWidgetTransformAnim::ShapeKind) - its fill, stroke, trim and layout, and a Star's points and radii,
	 * an Ellipse's sweep, a Custom Shape's path. Blended between keys field by field: numbers and colors glide, a path
	 * morphs vertex by vertex when both keys have as many, and anything with nothing in between - an on/off, a choice
	 * from a list - switches halfway.
	 */
	UPROPERTY(EditAnywhere, Category = "Key", meta = (DisplayName = "Shape", FixedLayout, EditCondition = "Content == EWidgetKeyframeContent::Shape && !bUseAuthoredShape", EditConditionHides, HideEditConditionToggle))
	FInstancedPropertyBag ShapeValues;

	/**
	 * How the widget travels from this key to the next. Track Easing - the default - leaves it to the layer's
	 * Easing; anything else is this segment's own curve. Ignored on the last key, which has no next.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ShowOnlyInnerProperties))
	FWidgetAnimSegmentCurve CurveToNext;

	/**
	 * Details-panel only; no runtime effect. Mirrors the track's type (FWidgetTransformAnim::Content), because an
	 * EditCondition can only see properties on its own struct. The Context Menu Button's shape states use the same
	 * trick (bIsApplicable) for the same reason. Evaluation reads the track's type, never this.
	 */
	UPROPERTY()
	EWidgetKeyframeContent Content = EWidgetKeyframeContent::Transform;
};

// =====================================================================================================
// Payload
// =====================================================================================================

/**
 * A keyframed layer: a Transform track, a Shape Box track or a Shape track - which one is Content, set from the track's
 * type. All three key, time and ease the same way; only what each key holds differs (see FWidgetTransformKeyframe).
 *
 * TIMING lives on the curve. Its keys' times are the keyframe times, so the last key's time is how long the
 * layer runs - which is why Duration and Easing are hidden on a Transform layer. The layer's Delay still
 * applies, and so do loop, ping-pong, invert, mirror and speed on whatever entry plays it.
 *
 * INTERPOLATION: each key is reached exactly at its time, and each segment - from a key to the next - is shaped
 * on its own: by that key's Curve To Next Key, or, left at Track Easing, by the layer's Easing. The way a
 * keyframe editor sets the interpolation between two keys. With two keys that is the layer's Easing over the
 * whole run, as it always was.
 *
 * VALUES live in Keys: one entry per curve key, in time order. Add or delete a curve key and Keys grows or
 * shrinks with it - a new entry starts at whatever the animation looked like at that moment, so inserting
 * a key never makes the widget jump.
 *
 * The curve can be edited inline or point at a Curve asset instead (the asset picker on the curve row),
 * which is how one timing is shared across many animations.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetTransformAnim
{
	GENERATED_BODY()

	/** Seeds a 0.3s linear move between two identity keys, so a new layer is valid and ready to edit. */
	FWidgetTransformAnim();

	/**
	 * When each key happens: one curve key per Transform key, its X the key's time. Timing only - how the
	 * motion travels between keys is the layer's Easing and each key's Curve To Next Key - so it is not shown anywhere;
	 * keys are added, deleted and retimed on the Widget Animations panel's tracks. Its Y values and
	 * interpolation are ignored. A Curve asset set here by older data still supplies the times.
	 */
	UPROPERTY()
	FRuntimeFloatCurve Curve;

	/**
	 * What the keys hold - which keyframed track this is. Mirrors the track's type, which is what sets it (see
	 * WidgetAnimKeyframeContentFor); never edited on its own.
	 */
	UPROPERTY()
	EWidgetKeyframeContent Content = EWidgetKeyframeContent::Transform;

	/**
	 * A Shape track's kind of shape - Star, Ellipse, Custom Shape - which decides the settings each key holds. Matched to
	 * the track's target widget in the Widget Animations panel, or picked there when the track plays on the widget the
	 * animation is put on. Played on a shape of another kind, the settings the two kinds share (fill, stroke, trim,
	 * layout) still animate.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Transform")
	TSubclassOf<UShapeWidget> ShapeKind;

	/** What the widget looks like at each curve key, in time order. Kept the same length as the curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, EditFixedSize, Category = "Transform", meta = (TitleProperty = "Time"))
	TArray<FWidgetTransformKeyframe> Keys;

	/** The curve actually in use: the Curve asset when one is picked, otherwise the inline one. */
	const FRichCurve* GetActiveCurve() const;

	/**
	 * The time of the last key, in seconds. What the layer clock runs for, in place of Duration. A first key
	 * placed after 0 is a hold on that key's pose until it arrives.
	 */
	float GetDuration() const;

	/** Whether the keys hold render transforms, Shape Box outlines or shape settings. See Content. */
	bool IsAnimatingTransform() const { return Content == EWidgetKeyframeContent::Transform; }
	bool IsAnimatingShape() const { return Content == EWidgetKeyframeContent::ShapeBox; }
	bool IsAnimatingShapeWidget() const { return Content == EWidgetKeyframeContent::Shape; }

	/**
	 * Makes this a Shape track for shapes of InKind: every key's settings laid out for that kind, keeping each value a
	 * key already had that the new kind shares and taking the rest from Source - a shape of that kind, its current
	 * settings - or the kind's defaults. Returns true if anything changed.
	 */
	bool SetShapeKind(UClass* InKind, const UObject* Source = nullptr);

	/** Whether any key but the last has a Curve To Next Key of its own, rather than the layer's Easing. */
	bool HasSegmentCurves() const
	{
		for (int32 Index = 0; Index + 1 < Keys.Num(); ++Index)
		{
			if (!Keys[Index].CurveToNext.IsTrackEasing())
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Brings Keys back into step with the curve after an edit: one entry per curve key, times mirrored, the
	 * Details-only Content mirrored, a Shape track's settings laid out for its kind. Existing values follow their key; a newly added key starts at the
	 * pose the animation had at that time. Returns true if anything changed; bOutResized says whether the
	 * number of keys did, which is the one change that adds or removes rows in the Details panel.
	 *
	 * Editor-facing - evaluation copes with a mismatch on its own, so nothing breaks if this never runs.
	 */
	bool SyncKeysToCurve(bool* bOutResized = nullptr);
};

/**
 * What one Transform layer contributed on one frame, in the same channels the library accumulates into.
 *
 * A plain value type, so this header does not have to know about FWidgetAnimResult and the core library
 * does not have to know how keys are interpolated. The next family (spring, sequencing) adds its own
 * header and result type and folds in the same way.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetTransformAnimResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Translation = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Scale = FVector2D(1.f, 1.f);

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FVector2D Shear = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	float Angle = 0.f;

	/** True only for a Shape Box track, which is what keeps every other keyframed track from stamping over its outline. */
	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	bool bHasShape = false;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FShapeBoxShape Shape;
};

// =====================================================================================================
// Evaluation
// =====================================================================================================

#if WITH_EDITOR
/**
 * Broadcast once a deferred key sync has actually changed something on Object. bResized is true when a key
 * was added or removed rather than only retimed.
 *
 * How anything that shows Object somewhere else - the builder's designer preview, which is a separate
 * instance of what the Details panel edits - learns that the keys it copied are out of date, without this
 * header having to know who those listeners are.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnWidgetTransformAnimsSynced, UObject* /*Object*/, bool /*bResized*/);
#endif

UCLASS()
class NAMETBD_API UWidgetAnimTransformLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * The layer at LocalTime seconds, in the curve's own time: its render transform when Content is Transform, its
	 * Shape Box outline when it is ShapeBox. A Shape track's settings are blended where they are applied instead - see
	 * WidgetAnimShapeTrack.h - from the segment FindSegment finds.
	 *
	 * The layer's Delay and any inversion are already resolved by the caller, so a reversed layer simply
	 * hands in a time that runs backwards. AuthoredShape is what a key's Use Authored Shape resolves to -
	 * the outline the baseline captured off the Shape Box - and null means none is known, in which case
	 * those keys fall back to their own Shape.
	 *
	 * TrackEase, when given, shapes each segment by its key's Curve To Next Key, a key left at Track Easing
	 * by TrackEase itself - the layer's Easing - and LocalTime is plain time. Null keeps every segment
	 * linear.
	 *
	 * bExtrapolate: a LocalTime before the first key or past the last carries on along the first or last segment,
	 * in a straight line, instead of holding the key - how a spring-timed track overshoots its last key.
	 */
	static FWidgetTransformAnimResult Evaluate(const FWidgetTransformAnim& Anim, EWidgetKeyframeContent Content, float LocalTime, const FShapeBoxShape* AuthoredShape = nullptr, const TFunctionRef<float(float)>* TrackEase = nullptr, bool bExtrapolate = false);

	/**
	 * Where LocalTime falls among the keys, the way Evaluate finds it: OutSegment is the key it is leaving (the last
	 * key but one at most), OutAlpha how far toward the next it is - eased by TrackEase as Evaluate eases it, and past 0
	 * or 1 only where bExtrapolate lets it run on. Returns how many keys there are to blend; 1 is a single key held
	 * (OutSegment 0, OutAlpha 0), 0 nothing at all.
	 */
	static int32 FindSegment(const FWidgetTransformAnim& Anim, float LocalTime, const TFunctionRef<float(float)>* TrackEase, bool bExtrapolate, int32& OutSegment, float& OutAlpha);

	/**
	 * Runs SyncKeysToCurve on every FWidgetTransformAnim anywhere inside Object - in layer arrays, in
	 * nested structs, wherever. Found by reflection rather than by naming each owner's arrays, so the preset
	 * asset, the builder, the animation component and anything added later all stay in sync without knowing
	 * how the others store their layers. Returns true if anything changed.
	 */
	static bool SyncAllTransformAnims(UObject* Object, bool* bOutResized = nullptr);

#if WITH_EDITOR
	/**
	 * What PostEditChangeProperty should call: SyncAllTransformAnims, but safely.
	 *
	 * Deferred to the next editor tick and skipped entirely for mid-drag (Interactive) changes, and for every
	 * edit that cannot put Keys out of step with the curve - which is every bool, enum and value inside a key.
	 * Several edits in one frame collapse into one sync.
	 *
	 * The sync never calls PostEditChange on the object. That used to follow any resize, and a PostEditChange
	 * with no property is the "everything changed" notification: the Details panel regenerates from scratch
	 * and the Blueprint editor treats it as a Blueprint change and rebuilds the designer preview - the whole
	 * editor flicker. Neither is needed. The Details panel validates its array nodes every tick and rebuilds
	 * just the Keys rows when their count changes, and anything else that needs to know listens to
	 * OnTransformAnimsSynced instead.
	 */
	static void RequestSyncTransformAnims(UObject* Object, const FPropertyChangedEvent& PropertyChangedEvent);

	/** Fired after a deferred sync, once per object whose keys it changed. See FOnWidgetTransformAnimsSynced. */
	static FOnWidgetTransformAnimsSynced& OnTransformAnimsSynced();
#endif
};