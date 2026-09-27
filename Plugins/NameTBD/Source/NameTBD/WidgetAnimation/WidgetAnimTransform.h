// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Slate/WidgetTransform.h"

#include "NameTBD/Widgets/ShapeBox.h"   // FShapeBoxShape, for the optional Shape on each key
#include "WidgetAnimEasing.h"           // FWidgetAnimSegmentCurve, for the curve from each key to the next

#include "WidgetAnimTransform.generated.h"

class UShapeBox;

// =====================================================================================================
// Keyframe
// =====================================================================================================

/**
 * What the widget looks like at one key of a Transform layer's curve.
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
	 * The widget's render transform at this key.
	 *
	 * Composes with every other layer the way the library always has: Translation, Shear and Angle add,
	 * Scale multiplies. With Preserve Authored Transforms on, (30, 0) means thirty units right of wherever
	 * the widget was authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key")
	FWidgetTransform Transform;

	/**
	 * Uses the Shape Box's own authored outline at this key instead of the Shape below. The usual choice for
	 * the last key of an entrance: whatever the panel is distorted from, it settles into its real shape, and
	 * the same animation works on every Shape Box it is used on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (DisplayName = "Use Authored Shape", EditCondition = "bShapeEnabled", EditConditionHides, HideEditConditionToggle))
	bool bUseAuthoredShape = false;

	/** The Shape Box outline at this key. Blended between keys with FShapeBoxShape::Lerp, so every Shape Box property animates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (EditCondition = "bShapeEnabled && !bUseAuthoredShape", EditConditionHides, HideEditConditionToggle))
	FShapeBoxShape Shape;

	/**
	 * How the widget travels from this key to the next. Track Easing - the default - leaves it to the layer's
	 * Easing; anything else is this segment's own curve. Ignored on the last key, which has no next.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Key", meta = (ShowOnlyInnerProperties))
	FWidgetAnimSegmentCurve CurveToNext;

	/**
	 * Details-panel only; no runtime effect. Mirrors the layer's Animate Shape, because an EditCondition
	 * can only see properties on its own struct. The Context Menu Button's shape states use the same trick
	 * (bIsApplicable) for the same reason. Evaluation reads the layer's flag, never this one.
	 */
	UPROPERTY()
	bool bShapeEnabled = false;
};

// =====================================================================================================
// Payload
// =====================================================================================================

/**
 * A keyframed Transform layer.
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

	/** Also animates a Shape Box's whole outline, key by key. Needs this layer's Target Widget to be a Shape Box. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transform", meta = (DisplayName = "Animate Shape"))
	bool bAnimateShape = false;

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

	bool IsAnimatingShape() const { return bAnimateShape; }

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
	 * Details-only shape flag mirrored. Existing values follow their key; a newly added key starts at the
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

	/** False unless the layer's Animate Shape is on, which is what keeps it from stamping over other layers. */
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
	 * The layer at LocalTime seconds, in the curve's own time.
	 *
	 * The layer's Delay and any inversion are already resolved by the caller, so a reversed layer simply
	 * hands in a time that runs backwards. AuthoredShape is what a key's Use Authored Shape resolves to -
	 * the outline the baseline captured off the Shape Box - and null means none is known, in which case
	 * those keys fall back to their own Shape.
	 *
	 * TrackEase, when given, shapes each segment by its key's Curve To Next Key, a key left at Track Easing
	 * by TrackEase itself - the layer's Easing - and LocalTime is plain time. Null keeps every segment
	 * linear.
	 */
	static FWidgetTransformAnimResult Evaluate(const FWidgetTransformAnim& Anim, float LocalTime, const FShapeBoxShape* AuthoredShape = nullptr, const TFunctionRef<float(float)>* TrackEase = nullptr);

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