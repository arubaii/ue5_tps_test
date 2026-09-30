// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/Widget.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "WidgetAnimBlueprintCue.generated.h"

/**
 * A cue type made in Blueprint: subclass this, fill in its class defaults, implement On Cue - and it appears in the
 * Widget Animations panel's Add Animation Track menu, under Blueprint Cues. For a particle burst, a camera shake, a
 * haptic pulse: anything the game owns that should happen at a moment of an animation.
 *
 * It follows exactly the contract of a C++ cue (see WidgetAnimCue.h): the timeline decides when it fires - its start,
 * its keys, its end, once per loop, only forward unless the track says otherwise, never while scrubbing - and On Cue is
 * only told that it has. So it can later be moved to C++ without any animation that uses it changing.
 *
 * Stateless. Only the class default object is ever called, and it is shared by every track of the type, so nothing
 * should be stored on it. Whatever varies from track to track belongs in the track's Settings - a struct of the type's
 * own choosing, named by Settings Struct below.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "Widget Animation Blueprint Cue"))
class NAMETBD_API UWidgetAnimBlueprintCue : public UObject
{
	GENERATED_BODY()

public:

	UWidgetAnimBlueprintCue();

	// ---------------------------------------------------------------------------------------------
	// What the cue is
	// ---------------------------------------------------------------------------------------------

	/** What the cue is called in the Add Animation Track menu and on its tracks. Empty uses the Blueprint's name. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue")
	FText DisplayName;

	/** A sentence or two on what the cue does, shown in its tracks' tooltips. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (MultiLine = true))
	FText Description;

	/** What a key's value is called in the Selection panel - "Strength", "Count". Empty reads "Value". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue")
	FText ValueLabel;

	/** Whether this cue's tracks are drawn in a color of their own, rather than the shared cue color. */
	UPROPERTY(EditDefaultsOnly, Category = "Cue", meta = (InlineEditConditionToggle))
	bool bOverrideTrackColor = false;

	/**
	 * The color this cue's tracks are drawn in, in the Widget Animations panel. Left unticked, they use Cue Track Color
	 * from Editor Preferences > Plugins > Widget Animations Editor. Authoring only; nothing at runtime reads it.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Cue", meta = (EditCondition = "bOverrideTrackColor"))
	FLinearColor TrackColor = FLinearColor::White;

	/**
	 * Fires while the editor's previews play too - the Widget Animations panel's and the designer toolbar's. Off (the
	 * default), it only fires in a game: a preview has no game running for most things a cue does to act on.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Cue", meta = (DisplayName = "Fire In Editor Previews"))
	bool bFireAtDesignTime = false;

	// ---------------------------------------------------------------------------------------------
	// What it acts on
	// ---------------------------------------------------------------------------------------------

	/**
	 * The kind of widget this cue acts on. A track aimed at a widget that is not one hands On Cue the first one inside
	 * it; Widget accepts anything. On Cue is still called when there is none, with no Target.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Target")
	TSubclassOf<UWidget> TargetClass;

	// ---------------------------------------------------------------------------------------------
	// Values
	// ---------------------------------------------------------------------------------------------

	/**
	 * The struct each track of this cue carries as its Settings - usually a Blueprint Structure made for the cue, holding
	 * whatever the designer should set per track: which particle, how strong a shake. Handed to On Cue. None gives tracks
	 * no Settings of their own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values")
	TObjectPtr<UScriptStruct> SettingsStruct;

	/** The value a new track of this cue carries at its start and end. Each key carries its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Values")
	double DefaultValue = 0.0;

	// ---------------------------------------------------------------------------------------------
	// Behavior - implement this
	// ---------------------------------------------------------------------------------------------

	/**
	 * The cue has fired. Target is the Target Class widget the track is aimed at (none when there is none), Settings the
	 * track's own Settings Struct, and Info everything else - which point fired and its value, the widget playing the
	 * animation, whether it was passed going backward. Called a frame after the moment, so it may do anything.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Widget Animation")
	void OnCue(UWidget* Target, const FInstancedStruct& Settings, const FWidgetAnimCueInfo& Info) const;

	/** The name this cue is shown by: Display Name, or the Blueprint's own name. */
	FText GetCueDisplayName() const;
};

/**
 * The settings a track of a Blueprint cue carries: which cue it is, the value its start and end carry, and the cue's own
 * Settings - with the cue rules every cue shares. What a Registered Type track's Type Params holds when its cue was made
 * in Blueprint.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Blueprint Cue"))
struct NAMETBD_API FWidgetAnimBlueprintCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	/** Which Blueprint cue this track is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Cue")
	TSubclassOf<UWidgetAnimBlueprintCue> Type;

	/** The value handed to On Cue at the track's start and end. Each key carries its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Cue")
	double Value = 0.0;

	/** The cue's own per-track settings - its Settings Struct - handed to On Cue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blueprint Cue")
	FInstancedStruct Settings;

	/** The cue's class default object, which is what is called. Null when no cue is set. */
	const UWidgetAnimBlueprintCue* GetTypeObject() const;

	/** Settings for a new track of InType: its default value, and its Settings Struct at its defaults. */
	static FInstancedStruct MakeFor(TSubclassOf<UWidgetAnimBlueprintCue> InType);
};

/**
 * The one C++ cue every Blueprint cue runs through, registered by the NameTBD module under
 * FWidgetAnimBlueprintCueParams. Reads everything natively and calls Blueprint only in On Cue, when a cue fires.
 */
class NAMETBD_API FWidgetAnimBlueprintCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimBlueprintCueParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetTrackDisplayName(const FInstancedStruct& Params) const override;
	virtual FText GetTrackDescription(const FInstancedStruct& Params) const override;
	virtual FText GetTrackValueLabel(const FInstancedStruct& Params) const override;
	virtual bool IsListedInAddMenu() const override { return false; }

	virtual UClass* GetTrackTargetClass(const FInstancedStruct& Params) const override;
	virtual bool FiresAtDesignTime(const FInstancedStruct& Params) const override;
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;

protected:

	virtual double GetPointValue(const FInstancedStruct& Params) const override;
};
