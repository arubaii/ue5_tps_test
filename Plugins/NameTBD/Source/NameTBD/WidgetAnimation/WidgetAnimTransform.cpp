// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTransform.h"

#include "UObject/UnrealType.h"

#if WITH_EDITOR
#include "Containers/Ticker.h"
#endif

namespace
{
	/** Default length of a freshly added Transform layer - the same 0.3s every other layer type starts at. */
	constexpr float DefaultTransformSeconds = 0.3f;

	/** Two keys closer together than this are the same key, for matching values back to a curve after an edit. */
	constexpr float KeyTimeTolerance = 1.e-3f;

	FWidgetTransform LerpWidgetTransform(const FWidgetTransform& A, const FWidgetTransform& B, float Alpha)
	{
		// Unclamped on purpose: an overshooting curve hands in an alpha past 1 (or below 0), and the widget
		// should travel past the key and come back, exactly as the curve is drawn.
		FWidgetTransform Out;
		Out.Translation = FMath::Lerp(A.Translation, B.Translation, Alpha);
		Out.Scale       = FMath::Lerp(A.Scale,       B.Scale,       Alpha);
		Out.Shear       = FMath::Lerp(A.Shear,       B.Shear,       Alpha);
		Out.Angle       = FMath::Lerp(A.Angle,       B.Angle,       Alpha);
		return Out;
	}

	/** A key's outline, with Use Authored Shape resolved. Falls back to the key's own when no authored shape is known. */
	const FShapeBoxShape& ResolveKeyShape(const FWidgetTransformKeyframe& Key, const FShapeBoxShape* AuthoredShape)
	{
		return (Key.bUseAuthoredShape && AuthoredShape) ? *AuthoredShape : Key.Shape;
	}

	/**
	 * The pose between the old keyframes at Time, linear in time. What a newly inserted curve key starts at,
	 * so adding a key never makes the widget jump - it begins exactly where the animation already was.
	 */
	FWidgetTransformKeyframe SampleKeyframes(const TArray<FWidgetTransformKeyframe>& Keyframes, float Time)
	{
		if (Keyframes.Num() == 0)
		{
			FWidgetTransformKeyframe Fresh;
			Fresh.Time = Time;
			return Fresh;
		}

		if (Time <= Keyframes[0].Time)
		{
			FWidgetTransformKeyframe Copy = Keyframes[0];
			Copy.Time = Time;
			return Copy;
		}

		for (int32 Index = 0; Index + 1 < Keyframes.Num(); ++Index)
		{
			const FWidgetTransformKeyframe& A = Keyframes[Index];
			const FWidgetTransformKeyframe& B = Keyframes[Index + 1];
			if (Time > B.Time)
			{
				continue;
			}

			const float Alpha = FMath::Clamp((Time - A.Time) / FMath::Max(B.Time - A.Time, KINDA_SMALL_NUMBER), 0.f, 1.f);

			FWidgetTransformKeyframe Blended = (Alpha < 0.5f) ? A : B;
			Blended.Time = Time;

			// A key dropped into a segment splits it, and both halves keep moving the way the whole did.
			Blended.CurveToNext = A.CurveToNext;
			Blended.Transform = LerpWidgetTransform(A.Transform, B.Transform, Alpha);
			Blended.Shape = FShapeBoxShape::Lerp(A.Shape, B.Shape, Alpha);
			return Blended;
		}

		FWidgetTransformKeyframe Copy = Keyframes.Last();
		Copy.Time = Time;
		return Copy;
	}
}

// =====================================================================================================
// FWidgetTransformAnim
// =====================================================================================================

FWidgetTransformAnim::FWidgetTransformAnim()
{
	// Two linear keys from 0 to 1: a straight line, so a new layer interpolates linearly until its shape is
	// changed. Y 0 and 1 are only a convenient pair - interpolation reads the curve's shape, not its height.
	FRichCurve* Rich = Curve.GetRichCurve();
	Rich->Reset();

	const FKeyHandle Start = Rich->AddKey(0.f, 0.f);
	const FKeyHandle End = Rich->AddKey(DefaultTransformSeconds, 1.f);
	Rich->SetKeyInterpMode(Start, RCIM_Linear);
	Rich->SetKeyInterpMode(End, RCIM_Linear);

	Keys.SetNum(2);
	Keys[0].Time = 0.f;
	Keys[1].Time = DefaultTransformSeconds;
}

const FRichCurve* FWidgetTransformAnim::GetActiveCurve() const
{
	// The asset's curve when one is picked on the row, otherwise the inline one - FRuntimeFloatCurve decides.
	return Curve.GetRichCurveConst();
}

float FWidgetTransformAnim::GetDuration() const
{
	const FRichCurve* Rich = GetActiveCurve();
	if (!Rich || Rich->GetNumKeys() == 0)
	{
		return KINDA_SMALL_NUMBER;
	}

	return FMath::Max(Rich->GetConstRefOfKeys().Last().Time, KINDA_SMALL_NUMBER);
}

bool FWidgetTransformAnim::SyncKeysToCurve(bool* bOutResized)
{
	const FRichCurve* Rich = GetActiveCurve();
	if (!Rich)
	{
		return false;
	}

	const TArray<FRichCurveKey>& CurveKeys = Rich->GetConstRefOfKeys();
	bool bChanged = false;

	if (Keys.Num() != CurveKeys.Num())
	{
		// A key was added or removed. Keys that did not move keep their values by matching time; a key with
		// no match is new and starts at the pose the animation already had there. The count being unchanged
		// is handled below instead, by position - that is a key being dragged, and its values go with it.
		const TArray<FWidgetTransformKeyframe> Previous = Keys;
		TArray<bool> Claimed;
		Claimed.Init(false, Previous.Num());

		TArray<FWidgetTransformKeyframe> Rebuilt;
		Rebuilt.Reserve(CurveKeys.Num());

		for (const FRichCurveKey& CurveKey : CurveKeys)
		{
			int32 Match = INDEX_NONE;
			for (int32 Index = 0; Index < Previous.Num(); ++Index)
			{
				if (!Claimed[Index] && FMath::Abs(Previous[Index].Time - CurveKey.Time) <= KeyTimeTolerance)
				{
					Match = Index;
					break;
				}
			}

			if (Match != INDEX_NONE)
			{
				Claimed[Match] = true;
				Rebuilt.Add(Previous[Match]);
			}
			else
			{
				Rebuilt.Add(SampleKeyframes(Previous, CurveKey.Time));
			}
		}

		Keys = MoveTemp(Rebuilt);
		bChanged = true;

		if (bOutResized)
		{
			*bOutResized = true;
		}
	}

	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		FWidgetTransformKeyframe& Key = Keys[Index];

		if (!FMath::IsNearlyEqual(Key.Time, CurveKeys[Index].Time))
		{
			Key.Time = CurveKeys[Index].Time;
			bChanged = true;
		}

		if (Key.bShapeEnabled != bAnimateShape)
		{
			Key.bShapeEnabled = bAnimateShape;
			bChanged = true;
		}
	}

	return bChanged;
}

// =====================================================================================================
// UWidgetAnimTransformLibrary
// =====================================================================================================

FWidgetTransformAnimResult UWidgetAnimTransformLibrary::Evaluate(const FWidgetTransformAnim& Anim, float LocalTime, const FShapeBoxShape* AuthoredShape, const TFunctionRef<float(float)>* TrackEase, bool bExtrapolate)
{
	FWidgetTransformAnimResult Result;

	const FRichCurve* Rich = Anim.GetActiveCurve();
	if (!Rich)
	{
		return Result;
	}

	const TArray<FRichCurveKey>& CurveKeys = Rich->GetConstRefOfKeys();

	// Tolerant of the two lists being out of step - a Curve asset edited elsewhere, a layer built in code
	// that never synced. Extra curve keys hold the last key that has a value; extra values are ignored.
	const int32 NumKeys = FMath::Min(CurveKeys.Num(), Anim.Keys.Num());
	if (NumKeys == 0)
	{
		return Result;
	}

	FWidgetTransform Pose;
	FShapeBoxShape Shape;

	if (NumKeys == 1)
	{
		Pose = Anim.Keys[0].Transform;
		Shape = ResolveKeyShape(Anim.Keys[0], AuthoredShape);
	}
	else
	{
		// The segment LocalTime falls in. Before the first key it is the first segment at alpha 0 (a hold on
		// key 0); past the last key it is the last segment at alpha 1 (a hold on the final pose).
		int32 Segment = 0;
		while (Segment < NumKeys - 2 && LocalTime >= CurveKeys[Segment + 1].Time)
		{
			++Segment;
		}

		// Linear in time between the two keys, when the layer's Easing was already applied to LocalTime. Given a
		// TrackEase instead, LocalTime is plain time and each segment is shaped here, by its own curve.
		const float SegmentStart = CurveKeys[Segment].Time;
		const float SegmentLength = FMath::Max(CurveKeys[Segment + 1].Time - SegmentStart, KINDA_SMALL_NUMBER);
		float LinearAlpha = (LocalTime - SegmentStart) / SegmentLength;
		if (bExtrapolate)
		{
			// Only the ends run on: before the first key along the first segment, past the last along the last. A
			// time inside the run is inside a segment, as always.
			LinearAlpha = Segment > 0 ? FMath::Max(LinearAlpha, 0.f) : LinearAlpha;
			LinearAlpha = Segment < NumKeys - 2 ? FMath::Min(LinearAlpha, 1.f) : LinearAlpha;
		}
		else
		{
			LinearAlpha = FMath::Clamp(LinearAlpha, 0.f, 1.f);
		}

		const FWidgetTransformKeyframe& KeyA = Anim.Keys[Segment];
		const FWidgetTransformKeyframe& KeyB = Anim.Keys[Segment + 1];

		// Pinned at the keys themselves, whatever a curve makes of its ends: at a key, the pose is that key's.
		float Alpha = LinearAlpha;
		if (TrackEase && LinearAlpha > 0.f && LinearAlpha < 1.f)
		{
			Alpha = WidgetAnimEasing::EvaluateSegment(KeyA.CurveToNext, LinearAlpha, *TrackEase);
		}

		Pose = LerpWidgetTransform(KeyA.Transform, KeyB.Transform, Alpha);

		if (Anim.bAnimateShape)
		{
			// Blended by the shape itself, by reflection, so a property added to FShapeBoxShape later animates
			// with no change here. Lerp extrapolates, so an overshooting segment overshoots the outline too.
			Shape = FShapeBoxShape::Lerp(ResolveKeyShape(KeyA, AuthoredShape), ResolveKeyShape(KeyB, AuthoredShape), Alpha);
		}
	}

	Result.Translation = Pose.Translation;
	Result.Scale       = Pose.Scale;
	Result.Shear       = Pose.Shear;
	Result.Angle       = Pose.Angle;

	if (Anim.bAnimateShape)
	{
		Result.Shape = Shape;
		Result.bHasShape = true;
	}

	return Result;
}

bool UWidgetAnimTransformLibrary::SyncAllTransformAnims(UObject* Object, bool* bOutResized)
{
	if (!Object)
	{
		return false;
	}

	bool bChanged = false;

	// Every FWidgetTransformAnim at any depth - inside layer arrays, inside entries, inside nested structs.
	// Object references are not followed, so each authoring object only ever syncs its own data.
	for (TPropertyValueIterator<FStructProperty> It(Object->GetClass(), Object); It; ++It)
	{
		const FStructProperty* StructProperty = It.Key();
		if (StructProperty && StructProperty->Struct == FWidgetTransformAnim::StaticStruct())
		{
			FWidgetTransformAnim* Anim = static_cast<FWidgetTransformAnim*>(const_cast<void*>(It.Value()));
			bChanged |= Anim->SyncKeysToCurve(bOutResized);
		}
	}

	return bChanged;
}

#if WITH_EDITOR
namespace
{
	/** Objects edited since the last deferred sync ran. Editor-only, game thread only. */
	TSet<TWeakObjectPtr<UObject>>& GetPendingTransformSyncs()
	{
		static TSet<TWeakObjectPtr<UObject>> Pending;
		return Pending;
	}

	bool bTransformSyncScheduled = false;

	/**
	 * Whether an edit could have put Keys out of step with its curve: the curve itself changed, Animate Shape
	 * was toggled (the keys mirror it), or something structural happened - a layer added or duplicated, an
	 * undo, a paste. A plain bool or enum or value inside a key cannot, so those do no work here at all.
	 */
	bool CouldAffectTransformKeys(const FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (!Property)
		{
			return true;
		}

		constexpr uint32 StructuralChanges = EPropertyChangeType::Unspecified
			| EPropertyChangeType::ArrayAdd
			| EPropertyChangeType::ArrayRemove
			| EPropertyChangeType::ArrayClear
			| EPropertyChangeType::ArrayMove
			| EPropertyChangeType::Duplicate;

		if (PropertyChangedEvent.ChangeType & StructuralChanges)
		{
			return true;
		}

		const UStruct* Owner = Property->GetOwnerStruct();
		if (Owner == FRuntimeFloatCurve::StaticStruct() || Owner == FRichCurve::StaticStruct() || Owner == FRichCurveKey::StaticStruct())
		{
			return true;
		}

		return Owner == FWidgetTransformAnim::StaticStruct()
			&& (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Curve)
				|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, bAnimateShape));
	}
}

FOnWidgetTransformAnimsSynced& UWidgetAnimTransformLibrary::OnTransformAnimsSynced()
{
	static FOnWidgetTransformAnimsSynced Delegate;
	return Delegate;
}

void UWidgetAnimTransformLibrary::RequestSyncTransformAnims(UObject* Object, const FPropertyChangedEvent& PropertyChangedEvent)
{
	// Mid-drag: the curve editor (or a spin box) is still inside its own mouse handling. Leave the data
	// alone until the drag commits - a non-Interactive event always follows it.
	if (!Object || PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	// Nothing to do for the vast majority of edits - every bool, enum and value inside a key.
	if (!CouldAffectTransformKeys(PropertyChangedEvent))
	{
		return;
	}

	GetPendingTransformSyncs().Add(Object);

	if (bTransformSyncScheduled)
	{
		return;
	}

	bTransformSyncScheduled = true;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
	{
		// Taken out and cleared before anything runs, so a listener that edits and requests again lands in
		// a fresh set and a fresh ticker rather than in the one being walked here.
		const TArray<TWeakObjectPtr<UObject>> Objects = GetPendingTransformSyncs().Array();
		GetPendingTransformSyncs().Reset();
		bTransformSyncScheduled = false;

		// Every object is synced before anyone is told. A template and the preview built from it are often
		// synced in the same pass; telling listeners in between would have them copy the half that has not
		// been synced yet over the half that has.
		TArray<TPair<TWeakObjectPtr<UObject>, bool>, TInlineAllocator<4>> Changed;

		for (const TWeakObjectPtr<UObject>& WeakObject : Objects)
		{
			UObject* Object = WeakObject.Get();
			if (!Object)
			{
				continue;
			}

			bool bResized = false;
			if (SyncAllTransformAnims(Object, &bResized))
			{
				Changed.Emplace(WeakObject, bResized);
			}

			// Deliberately no PostEditChange here, even on a resize. See RequestSyncTransformAnims.
		}

		for (const TPair<TWeakObjectPtr<UObject>, bool>& Entry : Changed)
		{
			if (UObject* Object = Entry.Key.Get())
			{
				OnTransformAnimsSynced().Broadcast(Object, Entry.Value);
			}
		}

		return false; // one shot
	}));
}
#endif