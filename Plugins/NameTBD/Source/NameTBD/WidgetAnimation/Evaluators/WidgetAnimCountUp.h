// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "WidgetAnimCountUp.generated.h"

/**
 * Count Up: a number on a Text Block counting from one value to another - a score landing, a price ticking down,
 * XP filling in.
 *
 * Timed like any other track - Duration and Easing, or a Spring, which overshoots the number and settles back - and
 * keyed like one: a key of 50 halfway along counts to 50, and a Hold curve on it waits there before counting on.
 *
 * The first registered track type, and the proof of the path: it writes text, which cannot be blended, so it is the
 * one writer of its Text Block's text while it plays, and the text the block was authored with is put back after.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Count Up"))
struct NAMETBD_API FWidgetAnimCountUpParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The number shown when the track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count Up", meta = (DisplayName = "Start Value"))
	double StartValue = 0.0;

	/** The number the track lands on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count Up", meta = (DisplayName = "End Value"))
	double EndValue = 100.0;

	/** Digits shown after the decimal point. 0 counts in whole numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count Up", meta = (ClampMin = "0", ClampMax = "6", UIMax = "4"))
	int32 Decimals = 0;

	/** Separates thousands the way the player's culture does - 12,500 rather than 12500. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count Up", meta = (DisplayName = "Group Thousands"))
	bool bUseGrouping = true;

	/**
	 * The text around the number, with {Value} where the number goes - "{Value} XP", "$ {Value}", "x{Value}". Empty
	 * shows the number alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count Up")
	FText Format = INVTEXT("{Value}");
};

/** What a Text Block said before a Count Up - or anything else that writes its text - touched it. */
USTRUCT()
struct NAMETBD_API FWidgetAnimTextBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	FText Text;
};

/** The Count Up type's evaluator. Registered by the NameTBD module; see FWidgetAnimEvaluator. */
class NAMETBD_API FWidgetAnimCountUpEvaluator : public TWidgetAnimEvaluator<FWidgetAnimCountUpParams>
{
public:

	/** The channel every text-writing type shares, so they take turns on one label and share one baseline. */
	static const FName TextChannel;

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;

	virtual UClass* GetTargetClass() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override { return TextChannel; }

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual double GetQuantizeStep(const FInstancedStruct& Params) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

	/** Value as a Count Up with these settings shows it - rounded, grouped and placed in its Format. */
	static FText FormatValue(const FWidgetAnimCountUpParams& Params, double Value);
};