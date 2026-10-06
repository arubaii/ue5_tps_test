// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"

#include "WidgetAnimDesignTrack.generated.h"

class UWidget;
struct FWidgetTransformAnim;

/*
 * The Design track: keyframed visual design (EWidgetAnimType::Design) - an Image's brush and tint, a Text Block's color and
 * font, a Border's background - blending one designed look into another.
 *
 * Authored like a Shape track - the same keys, curves, timing and springs - each key holding a widget's look
 * (FWidgetTransformKeyframe::ShapeValues, laid out for the track's FWidgetTransformAnim::DesignKind; see
 * WidgetDesignAnimation.h), or Use Authored Design for the widget exactly as designed: by default the first key, so the track
 * starts from the widget as it is. Played through the channel registered types write through, as a Shape track is - one
 * write per widget and frame, the last track to write it winning - since a look can be put back but not added up.
 *
 * The evaluator is registered, so the look it captures can be put back by type like any registered type's, but it isn't
 * offered in the Add Track menu: Design is a track type of its own there. Not shown in the Graph Editor - a look is no one
 * number to draw.
 */

/** One key of a Design track as it plays - see FWidgetTransformKeyframe. */
USTRUCT()
struct NAMETBD_API FWidgetAnimDesignKey
{
	GENERATED_BODY()

	UPROPERTY()
	bool bUseAuthoredDesign = true;

	UPROPERTY()
	FInstancedPropertyBag Values;
};

/** A Design track's keys as it plays: built from the track by MakeParams, into its settings' Type Params. Never authored. */
USTRUCT(meta = (Hidden))
struct NAMETBD_API FWidgetAnimDesignKeysParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The kind of widget the keys were made for - found inside a widget that isn't one itself. */
	UPROPERTY()
	TSubclassOf<UWidget> DesignKind;

	UPROPERTY()
	TArray<FWidgetAnimDesignKey> Keys;
};

/** What a Design track's widget looked like before it was animated: every setting a Design track can key on it. */
USTRUCT()
struct NAMETBD_API FWidgetAnimDesignKeysBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	FInstancedPropertyBag Values;
};

class NAMETBD_API FWidgetAnimDesignKeysEvaluator : public TWidgetAnimEvaluator<FWidgetAnimDesignKeysParams>
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

	/** See WidgetAnimDesignTrack::ResolveWidget. */
	static UWidget* FindDesignWidget(UWidget* Widget, const UClass* Kind);

private:
	/** The blend from key Segment toward the next, Alpha of the way, onto Target. */
	void ApplyKeys(UWidget* Target, const FWidgetAnimDesignKeysParams& Keys, int32 Segment, float Alpha, const FWidgetAnimDesignKeysBaseline* Authored) const;
};

namespace WidgetAnimDesignTrack
{
	/** The Design track's evaluator, as registered. Null before the NameTBD module has registered it. */
	NAMETBD_API const FWidgetAnimEvaluator* FindEvaluator();

	/** A Design track's keys as it plays them - what FWidgetAnimLayer::ToSettings puts in its settings' Type Params. */
	NAMETBD_API FInstancedStruct MakeParams(const FWidgetTransformAnim& Anim);

	/** The widget a Design track for Kind plays on: Widget itself when it is one, else the first Kind in it, else Widget. */
	NAMETBD_API UWidget* ResolveWidget(UWidget* Widget, const UClass* Kind);
}
