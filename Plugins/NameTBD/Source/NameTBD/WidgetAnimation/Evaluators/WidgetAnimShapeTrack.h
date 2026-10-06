// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"

#include "WidgetAnimShapeTrack.generated.h"

struct FWidgetTransformAnim;

/*
 * The Shape track: keyframed shape settings (EWidgetAnimType::Shape).
 *
 * Authored like a Transform track - the same keys, curves, timing, springs and values - with each key holding a shape's
 * settings instead of a render transform (FWidgetTransformKeyframe::ShapeValues), or Use Authored Shape for the shape as
 * designed. Played through the channel registered types write through - one write per shape and frame, the last track to
 * write it winning - because a shape's settings, like a text, can be put back but not added up: the write says which key
 * the track is leaving and how far toward the next, and the evaluator below blends the two keys' settings onto the shape.
 *
 * The evaluator is registered, so the baseline it captures (the shape's authored settings) can be put back by type like
 * any registered type's, but it isn't offered in the Add Track menu: a Shape track is a type of its own there.
 */

/** One key of a Shape track as it plays - see FWidgetTransformKeyframe. */
USTRUCT()
struct NAMETBD_API FWidgetAnimShapeKey
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUseAuthoredShape = true;

	UPROPERTY()
	FInstancedPropertyBag Values;
};

/** A Shape track's keys as it plays: built from the track by MakeParams, into its settings' Type Params. Never authored. */
USTRUCT(meta = (Hidden))
struct NAMETBD_API FWidgetAnimShapeKeysParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The kind of shape the keys were made for - found inside a widget that isn't a shape itself. */
	UPROPERTY()
	TSubclassOf<UShapeWidget> ShapeKind;

	UPROPERTY()
	TArray<FWidgetAnimShapeKey> Keys;
};

/** What a Shape track's shape held before it was animated: every setting a Shape track can key. */
USTRUCT()
struct NAMETBD_API FWidgetAnimShapeKeysBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	FInstancedPropertyBag Values;
};

class NAMETBD_API FWidgetAnimShapeKeysEvaluator : public TWidgetAnimEvaluator<FWidgetAnimShapeKeysParams>
{
public:
	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual bool IsListedInAddMenu() const override { return false; }
	virtual UClass* GetTargetClass() const override;
	virtual UObject* ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override { return bStart ? 0.0 : 1.0; }
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;
	virtual void ApplyWrite(UObject* Target, const FInstancedStruct& Params, const FWidgetAnimAuthoritativeWrite& Write, const FInstancedStruct* Captured) const override;
	virtual bool BlendsWriteValues() const override { return false; }

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

	/** See WidgetAnimShapeTrack::ResolveShape. */
	static UShapeWidget* FindShape(UWidget* Widget, const UClass* Kind);

private:
	/** The blend from key Segment toward the next, Alpha of the way, onto Target. */
	void ApplyKeys(UShapeWidget* Target, const FWidgetAnimShapeKeysParams& Keys, int32 Segment, float Alpha, const FWidgetAnimShapeKeysBaseline* Authored) const;
};

namespace WidgetAnimShapeTrack
{
	/** The Shape track's evaluator, as registered. Null before the NameTBD module has registered it. */
	NAMETBD_API const FWidgetAnimEvaluator* FindEvaluator();

	/** A Shape track's keys as it plays them - what FWidgetAnimLayer::ToSettings puts in its settings' Type Params. */
	NAMETBD_API FInstancedStruct MakeParams(const FWidgetTransformAnim& Anim);

	/** The shape inside Widget a Shape track for Kind plays on: Widget itself when it is a shape, else the first Kind in it. */
	NAMETBD_API UShapeWidget* ResolveShape(UWidget* Widget, const UClass* Kind);
}
