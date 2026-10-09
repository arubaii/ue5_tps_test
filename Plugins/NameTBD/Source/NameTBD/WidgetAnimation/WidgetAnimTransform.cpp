// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTransform.h"

#include "WidgetAnimLibrary.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"
#include "WidgetDesignAnimation.h"
#include "Components/Widget.h"

#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
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

			// A key dropped into a segment splits it, and both halves keep moving the way the whole did - a curved stretch
			// staying curved through it.
			Blended.CurveToNext = A.CurveToNext;
			Blended.PathShape = (A.PathShape != EWidgetKeyPathShape::Linear || B.PathShape != EWidgetKeyPathShape::Linear) ? EWidgetKeyPathShape::Smooth : EWidgetKeyPathShape::Linear;
			Blended.PathHandleIn = FVector2D::ZeroVector;
			Blended.PathHandleOut = FVector2D::ZeroVector;
			Blended.Transform = LerpWidgetTransform(A.Transform, B.Transform, Alpha);
			Blended.Shape = FShapeBoxShape::Lerp(A.Shape, B.Shape, Alpha);
			Blended.ShapeValues = WidgetShapeAnimation::LerpBags(A.ShapeValues, B.ShapeValues, Alpha);
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

		if (Key.Content != Content)
		{
			Key.Content = Content;
			bChanged = true;
		}

		// A Shape track's keys hold its kind's settings, laid out for it - a key added before the kind was picked, or one
		// that came from another kind, catches up here - and a Design track's its kind of widget's. Anything else holds none.
		if (Content == EWidgetKeyframeContent::Design)
		{
			bChanged |= WidgetDesignAnimation::LayOut(Key.ShapeValues, DesignKind.Get(), nullptr);
		}
		else
		{
			const UClass* BagKind = Content == EWidgetKeyframeContent::Shape ? ShapeKind.Get() : nullptr;
			bChanged |= WidgetShapeAnimation::LayOut(Key.ShapeValues, BagKind, nullptr);
		}
	}

	return bChanged;
}

bool FWidgetTransformAnim::SetShapeKind(UClass* InKind, const UObject* Source)
{
	UClass* Kind = InKind && InKind->IsChildOf(UShapeWidget::StaticClass()) && !InKind->HasAnyClassFlags(CLASS_Abstract) ? InKind : nullptr;
	bool bChanged = ShapeKind.Get() != Kind;
	ShapeKind = Kind;

	// A Design track's keys hold a widget's look, laid out by SetDesignKind - never a shape's.
	if (Content == EWidgetKeyframeContent::Design)
	{
		return bChanged;
	}
	for (FWidgetTransformKeyframe& Key : Keys)
	{
		bChanged |= WidgetShapeAnimation::LayOut(Key.ShapeValues, Content == EWidgetKeyframeContent::Shape ? Kind : nullptr, Source);
	}
	return bChanged;
}

bool FWidgetTransformAnim::SetDesignKind(UClass* InKind, const UObject* Source)
{
	// Its nearest native class - see WidgetDesignAnimation::GetDesignKind - which may be abstract (a User Widget): only its
	// settings are read off it, never an instance made.
	UClass* Kind = WidgetDesignAnimation::GetDesignKind(InKind);
	bool bChanged = DesignKind.Get() != Kind;
	DesignKind = Kind;

	if (Content != EWidgetKeyframeContent::Design)
	{
		return bChanged;
	}
	for (FWidgetTransformKeyframe& Key : Keys)
	{
		bChanged |= WidgetDesignAnimation::LayOut(Key.ShapeValues, Kind, Source);
	}
	return bChanged;
}

FWidgetTransform FWidgetTransformAnim::GetKeyTransform(int32 Index) const
{
	if (!Keys.IsValidIndex(Index) || Keys[Index].bUseAuthoredTransform)
	{
		return FWidgetTransform();
	}
	return Keys[Index].Transform;
}

void FWidgetTransformAnim::GetPathHandles(int32 Index, FVector2D& OutIn, FVector2D& OutOut) const
{
	OutIn = FVector2D::ZeroVector;
	OutOut = FVector2D::ZeroVector;
	if (!Keys.IsValidIndex(Index))
	{
		return;
	}

	const FWidgetTransformKeyframe& Key = Keys[Index];
	if (Key.PathShape == EWidgetKeyPathShape::Bezier)
	{
		OutIn = Key.PathHandleIn;
		OutOut = Key.PathHandleOut;
		return;
	}
	if (Key.PathShape != EWidgetKeyPathShape::Smooth)
	{
		return;
	}

	// Smooth: the path runs through the key in the direction from the key before to the key after, each handle a third of
	// the way to its neighbor - so the curve never loops past a key that is close by. At the first or last key it heads
	// straight for its one neighbor.
	const FVector2D Here = GetKeyTransform(Index).Translation;
	const FVector2D Before = Keys.IsValidIndex(Index - 1) ? GetKeyTransform(Index - 1).Translation : Here;
	const FVector2D After = Keys.IsValidIndex(Index + 1) ? GetKeyTransform(Index + 1).Translation : Here;
	const FVector2D Direction = (After - Before).GetSafeNormal();
	OutIn = -Direction * (FVector2D::Distance(Here, Before) / 3.0);
	OutOut = Direction * (FVector2D::Distance(After, Here) / 3.0);
}

FVector2D FWidgetTransformAnim::EvaluatePath(int32 Segment, float Alpha) const
{
	const int32 IndexB = FMath::Min(Segment + 1, Keys.Num() - 1);
	const FVector2D P0 = GetKeyTransform(Segment).Translation;
	const FVector2D P3 = GetKeyTransform(IndexB).Translation;
	if (IndexB == Segment)
	{
		return P0;
	}

	FVector2D InA, OutA, InB, OutB;
	GetPathHandles(Segment, InA, OutA);
	GetPathHandles(IndexB, InB, OutB);

	// Straight: exactly the lerp it always was, overshoot included.
	if (OutA.IsNearlyZero() && InB.IsNearlyZero())
	{
		return FMath::Lerp(P0, P3, static_cast<double>(Alpha));
	}

	const FVector2D P1 = P0 + OutA;
	const FVector2D P2 = P3 + InB;
	auto Bezier = [&P0, &P1, &P2, &P3](double Param)
	{
		const double U = 1.0 - Param;
		return P0 * (U * U * U) + P1 * (3.0 * U * U * Param) + P2 * (3.0 * U * Param * Param) + P3 * (Param * Param * Param);
	};

	// At an even pace along the curve: Alpha is how far along it, by distance, so the track's Easing alone decides the speed.
	// Measured afresh each time - a few dozen points, cheap beside everything else a frame does.
	constexpr int32 Steps = 24;
	double Lengths[Steps + 1];
	Lengths[0] = 0.0;
	FVector2D Previous = P0;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const FVector2D Point = Bezier(static_cast<double>(Step) / Steps);
		Lengths[Step] = Lengths[Step - 1] + FVector2D::Distance(Previous, Point);
		Previous = Point;
	}
	const double Total = Lengths[Steps];
	if (Total <= UE_KINDA_SMALL_NUMBER)
	{
		return P0;
	}

	// Past either end - an overshoot - it carries on straight along the curve's direction there.
	if (Alpha <= 0.f)
	{
		const FVector2D Start = (P1 - P0).IsNearlyZero() ? (Bezier(1.0 / Steps) - P0) : (P1 - P0);
		return P0 + Start.GetSafeNormal() * (static_cast<double>(Alpha) * Total);
	}
	if (Alpha >= 1.f)
	{
		const FVector2D End = (P3 - P2).IsNearlyZero() ? (P3 - Bezier(1.0 - 1.0 / Steps)) : (P3 - P2);
		return P3 + End.GetSafeNormal() * (static_cast<double>(Alpha - 1.f) * Total);
	}

	const double Wanted = static_cast<double>(Alpha) * Total;
	int32 Step = 1;
	while (Step < Steps && Lengths[Step] < Wanted)
	{
		++Step;
	}
	const double Span = FMath::Max(Lengths[Step] - Lengths[Step - 1], UE_SMALL_NUMBER);
	const double T = (static_cast<double>(Step - 1) + (Wanted - Lengths[Step - 1]) / Span) / Steps;
	return Bezier(FMath::Clamp(T, 0.0, 1.0));
}

// =====================================================================================================
// UWidgetAnimTransformLibrary
// =====================================================================================================

int32 UWidgetAnimTransformLibrary::FindSegment(const FWidgetTransformAnim& Anim, float LocalTime, const TFunctionRef<float(float)>* TrackEase, bool bExtrapolate, int32& OutSegment, float& OutAlpha)
{
	OutSegment = 0;
	OutAlpha = 0.f;

	const FRichCurve* Rich = Anim.GetActiveCurve();
	if (!Rich)
	{
		return 0;
	}

	const TArray<FRichCurveKey>& CurveKeys = Rich->GetConstRefOfKeys();

	// Tolerant of the two lists being out of step - a Curve asset edited elsewhere, a layer built in code
	// that never synced. Extra curve keys hold the last key that has a value; extra values are ignored.
	const int32 NumKeys = FMath::Min(CurveKeys.Num(), Anim.Keys.Num());
	if (NumKeys <= 1)
	{
		return NumKeys;
	}

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

	// Pinned at the keys themselves, whatever a curve makes of its ends: at a key, the pose is that key's.
	float Alpha = LinearAlpha;
	if (TrackEase && LinearAlpha > 0.f && LinearAlpha < 1.f)
	{
		Alpha = WidgetAnimEasing::EvaluateSegment(Anim.Keys[Segment].CurveToNext, LinearAlpha, *TrackEase);
	}

	OutSegment = Segment;
	OutAlpha = Alpha;
	return NumKeys;
}

FWidgetTransformAnimResult UWidgetAnimTransformLibrary::Evaluate(const FWidgetTransformAnim& Anim, EWidgetKeyframeContent Content, float LocalTime, const FShapeBoxShape* AuthoredShape, const TFunctionRef<float(float)>* TrackEase, bool bExtrapolate)
{
	FWidgetTransformAnimResult Result;

	int32 Segment = 0;
	float Alpha = 0.f;
	const int32 NumKeys = FindSegment(Anim, LocalTime, TrackEase, bExtrapolate, Segment, Alpha);
	if (NumKeys == 0 || Content == EWidgetKeyframeContent::Shape || Content == EWidgetKeyframeContent::Design)
	{
		return Result;
	}

	const FWidgetTransformKeyframe& KeyA = Anim.Keys[Segment];
	const FWidgetTransformKeyframe& KeyB = Anim.Keys[NumKeys > 1 ? Segment + 1 : Segment];

	if (Content == EWidgetKeyframeContent::Transform)
	{
		// A key that Uses Authored Transform is the identity: composed over the widget's own render transform, that is the
		// widget exactly as it was designed.
		const int32 IndexB = NumKeys > 1 ? Segment + 1 : Segment;
		const FWidgetTransform PoseA = Anim.GetKeyTransform(Segment);
		const FWidgetTransform PoseB = Anim.GetKeyTransform(IndexB);
		const FWidgetTransform Pose = NumKeys > 1 ? LerpWidgetTransform(PoseA, PoseB, Alpha) : PoseA;

		// Where it goes follows the path through the keys - straight, or curved by their Path - and the rest blends straight.
		Result.Translation = NumKeys > 1 ? Anim.EvaluatePath(Segment, Alpha) : Pose.Translation;
		Result.Scale       = Pose.Scale;
		Result.Shear       = Pose.Shear;
		Result.Angle       = Pose.Angle;
		return Result;
	}

	// A Shape Box track: the outline only, the transform left at the identity. Blended by the shape itself, by
	// reflection, so a property added to FShapeBoxShape later animates with no change here. Lerp extrapolates, so an
	// overshooting segment overshoots the outline too.
	Result.Shape = NumKeys > 1
		? FShapeBoxShape::Lerp(ResolveKeyShape(KeyA, AuthoredShape), ResolveKeyShape(KeyB, AuthoredShape), Alpha)
		: ResolveKeyShape(KeyA, AuthoredShape);
	Result.bHasShape = true;
	return Result;
}

namespace WidgetAnimTransformSync
{
	/**
	 * Which properties can hold an FWidgetTransformAnim somewhere inside them - each struct worked out once, for one sync. A
	 * property of a kind not known here is taken to, so nothing that might hold one is ever passed by.
	 */
	struct FReach
	{
		TMap<const UStruct*, bool> Known;

		bool Struct(const UStruct* InStruct)
		{
			if (!InStruct)
			{
				return false;
			}
			if (InStruct == FWidgetTransformAnim::StaticStruct())
			{
				return true;
			}
			if (const bool* Found = Known.Find(InStruct))
			{
				return *Found;
			}

			// Reached again from inside itself: taken to, until it is known.
			Known.Add(InStruct, true);
			bool bCan = false;
			for (TFieldIterator<FProperty> It(InStruct); It && !bCan; ++It)
			{
				bCan = Property(*It);
			}
			Known.Add(InStruct, bCan);
			return bCan;
		}

		bool Property(const FProperty* InProperty)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(InProperty))
			{
				return Struct(StructProperty->Struct);
			}
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InProperty))
			{
				return Property(ArrayProperty->Inner);
			}
			if (const FSetProperty* SetProperty = CastField<FSetProperty>(InProperty))
			{
				return Property(SetProperty->ElementProp);
			}
			if (const FMapProperty* MapProperty = CastField<FMapProperty>(InProperty))
			{
				return Property(MapProperty->KeyProp) || Property(MapProperty->ValueProp);
			}

			// Plain values, with nothing inside - and object references, which a sync never follows.
			const bool bPlain = InProperty->IsA<FNumericProperty>() || InProperty->IsA<FBoolProperty>() || InProperty->IsA<FEnumProperty>()
				|| InProperty->IsA<FStrProperty>() || InProperty->IsA<FNameProperty>() || InProperty->IsA<FTextProperty>()
				|| InProperty->IsA<FObjectPropertyBase>() || InProperty->IsA<FInterfaceProperty>()
				|| InProperty->IsA<FDelegateProperty>() || InProperty->IsA<FMulticastDelegateProperty>();
			return !bPlain;
		}
	};
}

bool UWidgetAnimTransformLibrary::SyncAllTransformAnims(UObject* Object, bool* bOutResized)
{
	if (!Object)
	{
		return false;
	}

	bool bChanged = false;

	// Every FWidgetTransformAnim at any depth - inside layer arrays, inside entries, inside nested structs.
	// Object references are not followed, so each authoring object only ever syncs its own data. Nothing that can't hold one
	// is gone into: walking every property of every track - nearly none of which can - cost the most of every edit to an
	// animation of hundreds of tracks, and is done on every frame of a drag.
	WidgetAnimTransformSync::FReach Reach;
	for (TPropertyValueIterator<FProperty> It(Object->GetClass(), Object); It; ++It)
	{
		const FProperty* Property = It.Key();
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);

		// A track's keys hold what its type says they hold. Set on the way into the track, so the sync of its keys
		// below - reached next, inside it - mirrors the type it has now.
		if (StructProperty && StructProperty->Struct == FWidgetAnimLayer::StaticStruct())
		{
			FWidgetAnimLayer* Layer = static_cast<FWidgetAnimLayer*>(const_cast<void*>(It.Value()));
			const EWidgetKeyframeContent Content = WidgetAnimKeyframeContentFor(Layer->AnimType);
			bChanged |= Layer->Transform.Content != Content;
			Layer->Transform.Content = Content;
		}
		else if (StructProperty && StructProperty->Struct == FWidgetAnimSettings::StaticStruct())
		{
			FWidgetAnimSettings* Settings = static_cast<FWidgetAnimSettings*>(const_cast<void*>(It.Value()));
			const EWidgetKeyframeContent Content = WidgetAnimKeyframeContentFor(Settings->AnimType);
			bChanged |= Settings->Transform.Content != Content;
			Settings->Transform.Content = Content;
		}

		if (StructProperty && StructProperty->Struct == FWidgetTransformAnim::StaticStruct())
		{
			FWidgetTransformAnim* Anim = static_cast<FWidgetTransformAnim*>(const_cast<void*>(It.Value()));
			bChanged |= Anim->SyncKeysToCurve(bOutResized);
		}
		else if (Property && !Reach.Property(Property))
		{
			It.SkipRecursiveProperty();
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
	 * Whether an edit could have put Keys out of step with its curve: the curve itself changed, the track's type
	 * changed (the keys mirror what it holds), or something structural happened - a layer added or duplicated, an
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

		if ((Owner == FWidgetAnimLayer::StaticStruct() || Owner == FWidgetAnimSettings::StaticStruct())
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, AnimType))
		{
			return true;
		}

		return Owner == FWidgetTransformAnim::StaticStruct()
			&& (Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Curve)
				|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, ShapeKind)
				|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, DesignKind));
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