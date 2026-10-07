// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/Widget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "WidgetAnimBlueprintType.generated.h"

/**
 * A track type made in Blueprint: subclass this, fill in its class defaults, override Apply Value - and it appears in
 * the Widget Animations panel's Add Animation Track menu, under Blueprint Types, like any C++ type.
 *
 * It follows exactly the contract of a C++ type (see FWidgetAnimEvaluator): the shared timeline does all the timing -
 * Duration, Delay, Easing, keys, Spring, Loop, Ping Pong, Invert, scrubbing - and hands this class only the value the
 * track is at. So a Blueprint type can later be moved to C++, for speed, without any animation that uses it changing.
 *
 * Stateless. Only the class default object is ever called, and it is shared by every track of the type, so nothing
 * should be stored on it while playing. Whatever varies from track to track belongs in the track's Settings - a
 * struct of the type's own choosing, named by Settings Struct below.
 *
 * Kept fast by design: its tracks' start and end values and its Quantize Step are read natively, never through
 * Blueprint, and Apply Value only runs on frames where the value has moved a whole Quantize Step. Its cost is measured
 * in development builds and warned about past the budget in Project Settings > Plugins > Widget Animations.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Widget Animation Blueprint Type"))
class NAMETBD_API UWidgetAnimBlueprintType : public UObject
{
	GENERATED_BODY()

public:

	UWidgetAnimBlueprintType();

	// ---------------------------------------------------------------------------------------------
	// What the type is
	// ---------------------------------------------------------------------------------------------

	/** What the type is called in the Add Animation Track menu and on its tracks. Empty uses the Blueprint's name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Type")
	FText DisplayName;

	/** A sentence or two on what the type does, shown in its tracks' tooltips. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Type", meta = (MultiLine = true))
	FText Description;

	/** What a key's value is called in the Selection panel - "Fill", "Frame", "Count". Empty reads "Value". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Type")
	FText ValueLabel;

	/** Whether this type's tracks are drawn in a colour of their own, rather than the shared Blueprint type colour. */
	UPROPERTY(EditDefaultsOnly, Category = "Type", meta = (InlineEditConditionToggle))
	bool bOverrideTrackColor = false;

	/**
	 * The colour this type's tracks are drawn in, in the Widget Animations panel. Left unticked, they use Blueprint Type
	 * Track Color from Editor Preferences > Plugins > Widget Animations Editor - one colour every Blueprint type shares,
	 * which each person can change for themselves. Authoring only; nothing at runtime reads it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Type", meta = (EditCondition = "bOverrideTrackColor"))
	FLinearColor TrackColor = FLinearColor::White;

	// ---------------------------------------------------------------------------------------------
	// What it animates
	// ---------------------------------------------------------------------------------------------

	/**
	 * The kind of widget this type animates. A track aimed at a widget that is not one uses the first one inside it -
	 * a Progress Bar type aimed at a whole health panel finds the bar. Widget accepts anything.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	TSubclassOf<UWidget> TargetClass;

	/**
	 * Which of the target's values this type writes. Tracks writing the same channel of the same widget take turns,
	 * the last one each frame winning, and share one baseline - so give two types the same channel only if they write
	 * the same thing ("Text" is what Count Up writes). None gives the type a channel of its own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	FName Channel;

	// ---------------------------------------------------------------------------------------------
	// Values
	// ---------------------------------------------------------------------------------------------

	/**
	 * The struct each track of this type carries as its Settings - usually a Blueprint Structure made for the type,
	 * holding whatever the designer should be able to set per track: a format, a colour, which icon set. Handed to
	 * every function below. None gives tracks no Settings of their own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values")
	TObjectPtr<UScriptStruct> SettingsStruct;

	/** The Start Value a new track of this type begins with. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values")
	double DefaultStartValue = 0.0;

	/** The End Value a new track of this type begins with. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values")
	double DefaultEndValue = 1.0;

	/**
	 * The smallest change in value that changes what the widget shows: 1 for whole numbers, 0.01 for a bar a hundred
	 * pixels long, 1 for a flipbook frame. Apply Value then runs only when the value moves to a different step, which
	 * is what keeps a Blueprint type cheap. 0 runs it on every frame the track plays - fine for one track, and
	 * warned about once it costs too much.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values", meta = (ClampMin = "0.0"))
	double QuantizeStep = 0.0;

	// ---------------------------------------------------------------------------------------------
	// Behaviour - override these
	// ---------------------------------------------------------------------------------------------

	/**
	 * Show Value on Target: set a bar's percent, a label's text, an image's frame. Value is where the track is between
	 * its Start and End values (and its keys), and may run past them while a Spring overshoots. Target is always a
	 * Target Class. Settings is the track's own Settings Struct.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Widget Animation")
	void ApplyValue(UWidget* Target, const FInstancedStruct& Settings, double Value) const;

	/**
	 * Record what Target shows before any animation touches it, into Baseline - use Make Instanced Struct with any
	 * struct you like - and return true. It is handed back to Restore Baseline to put Target back when the animation
	 * lets go, is scrubbed back before it starts, or its preview is turned off. Return false to never put anything back.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Widget Animation")
	bool CaptureBaseline(UWidget* Target, const FInstancedStruct& Settings, FInstancedStruct& Baseline) const;

	/** Put Target back the way Capture Baseline recorded it. */
	UFUNCTION(BlueprintNativeEvent, Category = "Widget Animation")
	void RestoreBaseline(UWidget* Target, const FInstancedStruct& Baseline) const;

	/** The name this type is shown by: Display Name, or the Blueprint's own name. */
	FText GetTypeDisplayName() const;

protected:

	virtual bool CaptureBaseline_Implementation(UWidget* Target, const FInstancedStruct& Settings, FInstancedStruct& Baseline) const;
	virtual void RestoreBaseline_Implementation(UWidget* Target, const FInstancedStruct& Baseline) const;
};

/**
 * The settings a track of a Blueprint type carries: which type it is, where it starts and ends, and the type's own
 * Settings. What a Registered Type track's Type Params holds when its type was made in Blueprint.
 *
 * Start and End are here, natively, rather than in the Blueprint's Settings so that reading them - which the timeline
 * does every frame - never runs Blueprint.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Blueprint Type"))
struct NAMETBD_API FWidgetAnimBlueprintTypeParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** Which Blueprint type this track is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Type")
	TSubclassOf<UWidgetAnimBlueprintType> Type;

	/** The value when the track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Type")
	double StartValue = 0.0;

	/** The value the track ends on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Type")
	double EndValue = 1.0;

	/** The type's own per-track settings - its Settings Struct, which they always follow - handed to every one of its functions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Type", meta = (StructTypeConst))
	FInstancedStruct Settings;

	/** The type's class default object, which is what is called. Null when no type is set. */
	const UWidgetAnimBlueprintType* GetTypeObject() const;

	/**
	 * Whether Settings hold what the type's Settings Struct says they should: that struct, or nothing when it has none.
	 * Always so with no type set.
	 */
	bool IsSettingsStructCurrent() const;

	/**
	 * Settings made afresh, at their defaults, for the type's Settings Struct when they aren't one - so a track's Settings
	 * follow its type, never picked by hand. True if anything changed. See IsSettingsStructCurrent.
	 */
	bool SyncSettingsStruct();

	/** Settings for a new track of InType: its default Start and End values, and its Settings Struct at its defaults. */
	static FInstancedStruct MakeFor(TSubclassOf<UWidgetAnimBlueprintType> InType);
};

/**
 * The one C++ evaluator every Blueprint type runs through, registered by the NameTBD module under
 * FWidgetAnimBlueprintTypeParams. Reads everything it can natively - endpoints, quantize step, target class, channel -
 * calls Blueprint only for the three functions that are the type's behaviour, and times those calls.
 */
class NAMETBD_API FWidgetAnimBlueprintEvaluator : public TWidgetAnimEvaluator<FWidgetAnimBlueprintTypeParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetTrackDisplayName(const FInstancedStruct& Params) const override;
	virtual FText GetTrackDescription(const FInstancedStruct& Params) const override;
	virtual FText GetTrackValueLabel(const FInstancedStruct& Params) const override;
	virtual bool IsListedInAddMenu() const override { return false; }

	virtual UClass* GetTrackTargetClass(const FInstancedStruct& Params) const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual double GetQuantizeStep(const FInstancedStruct& Params) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;
};

/** What a Blueprint type's baseline is stored as: which type captured it, and what it captured. */
USTRUCT()
struct NAMETBD_API FWidgetAnimBlueprintTypeBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UWidgetAnimBlueprintType> Type;

	UPROPERTY()
	FInstancedStruct Captured;
};

/**
 * What Blueprint types cost, measured around every Blueprint call in development builds and judged against Project
 * Settings > Plugins > Widget Animations. Every function here does nothing, and reports nothing, in Shipping.
 */
namespace WidgetAnimBlueprintTypeCost
{
	/** Whether Type has been found over budget this session. The Widget Animations panel marks its tracks. */
	NAMETBD_API bool IsFlagged(const UClass* Type);

	/** Type's cost over the last whole measurement window: milliseconds per frame, and the share of frames it called Blueprint on. */
	NAMETBD_API bool GetLastWindow(const UClass* Type, float& OutMsPerFrame, float& OutCallFrameShare);
}