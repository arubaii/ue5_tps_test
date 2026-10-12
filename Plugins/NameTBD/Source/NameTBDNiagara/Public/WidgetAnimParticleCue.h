// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimParameters.h"
#include "StructUtils/PropertyBag.h"

#include "WidgetAnimParticleCue.generated.h"

class UNiagaraSystem;
class UNiagaraUIWidget;
class UWidget;

/** What a Particles cue does when it fires. */
UENUM(BlueprintType)
enum class EWidgetAnimParticleCueAction : uint8
{
	BurstAtTarget UMETA(DisplayName = "Burst", ToolTip = "Plays a one-shot of the track's Niagara System (or the Niagara Particles widget's own) - where the Particles widget is, or on the track's widget (Burst At). Several can play at once; a stagger plays one per item."),

	Play UMETA(ToolTip = "Plays the track's Niagara System as the Niagara Particles widget's system (or its own, when the track names none), from its start."),

	StopEmitting UMETA(DisplayName = "Stop Emitting", ToolTip = "Stops the Niagara Particles widget spawning; its particles live out their lives."),

	Clear UMETA(ToolTip = "Removes every particle of the Niagara Particles widget at once.")
};

/** Where a Particles cue's burst plays. */
UENUM(BlueprintType)
enum class EWidgetAnimParticleCuePlace : uint8
{
	ParticlesWidget UMETA(DisplayName = "At Particles Widget", ToolTip = "Where the Niagara Particles widget is - at its Origin, as placed in the designer and wherever it has moved since."),

	TrackWidget UMETA(DisplayName = "At Track's Widget", ToolTip = "On the widget this track is aimed at - its Track Group's Target Widget, or each item of a stagger - at Burst Point. The Niagara Particles widget is then only the layer the particles are drawn in: one over the whole screen serves every button.")
};

/** Which Niagara Particles widgets a Particles cue plays on, of those it finds. */
UENUM(BlueprintType)
enum class EWidgetAnimParticleCueScope : uint8
{
	FirstFound UMETA(DisplayName = "First Found", ToolTip = "The first Niagara Particles widget found: the one named in Particles Widget, else the track's widget itself, or the first inside it, else the nearest around it."),

	AllFound UMETA(DisplayName = "All Found", ToolTip = "Every Niagara Particles widget found, each with its own Niagara System: every one named Particles Widget in the User Widgets around the track's widget; unnamed, every one at or inside the track's widget, else every one in the nearest User Widget around it that has any.")
};

/**
 * Particles: fires Niagara particles at a moment of the animation - a burst where a card lands, embers starting as a panel
 * opens, stopping as it closes.
 *
 * The track carries its own Niagara System and User Parameters: it plays that, whatever the Niagara Particles widget is set
 * to - left None, it plays nothing. So one Particles widget serves every track, each with its effect. Or Use Widget's Niagara
 * System: each widget plays its own system, with its own parameters - on the first found, or on all of them.
 *
 * Plays on a Niagara Particles widget: the one named by Particles Widget; else the track's widget itself when it is one (aim
 * the track's Track Group at it - the simplest setup), or the first inside it; else the nearest around it. Seen in the
 * editor's previews too, where that widget has Preview In Designer on.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Particles"))
struct NAMETBDNIAGARA_API FWidgetAnimParticleCueParams : public FWidgetAnimCueParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles")
	EWidgetAnimParticleCueAction Action = EWidgetAnimParticleCueAction::BurstAtTarget;

	/**
	 * Plays each Niagara Particles widget's own Niagara System, with its own User Parameters, rather than one set here - so one
	 * track fires whatever each widget is set up with. Off, the track plays its Niagara System below.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Use Widget's Niagara System", EditCondition = "Action == EWidgetAnimParticleCueAction::BurstAtTarget || Action == EWidgetAnimParticleCueAction::Play", EditConditionHides))
	bool bUseWidgetSystem = false;

	/** Which of the Niagara Particles widgets found it acts on: the first, or every one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Act On", EditCondition = "bUseWidgetSystem || Action == EWidgetAnimParticleCueAction::StopEmitting || Action == EWidgetAnimParticleCueAction::Clear", EditConditionHides))
	EWidgetAnimParticleCueScope ActOn = EWidgetAnimParticleCueScope::FirstFound;

	/**
	 * The system this track plays - burst, or played as the widget's system. None: nothing is burst or played (to play the
	 * Niagara Particles widget's own, turn on Use Widget's Niagara System). The widget's User Parameters reach it too, by
	 * name, under the track's own below. For a burst, a one-shot system (Loop Behavior: Once). Greyed out while Use Widget's
	 * Niagara System is on.
	 *
	 * Shown and greyed by the Widget Animations panel (ShowWhen / EnableWhen - see FTypeParamsDetails in SWidgetAnimTimeline.cpp)
	 * rather than an EditCondition: a property bag under an EditCondition came back read-only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Niagara System", ShowWhen = "Action=BurstAtTarget|Play", EnableWhen = "bUseWidgetSystem=false"))
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;

	/**
	 * The track's Niagara System's User parameters, listed from the system itself, each starting at the system's own value -
	 * as the Niagara Particles widget lists its own. Change one and this track plays the system with it, over the widget's
	 * value of it; the rest are the widget's, else the system's. Kept in step with the system each time the track is edited.
	 * Greyed out while Use Widget's Niagara System is on.
	 */
	UPROPERTY(EditAnywhere, Category = "Particles", meta = (DisplayName = "User Parameters", FixedLayout, ShowWhen = "Action=BurstAtTarget|Play", EnableWhen = "bUseWidgetSystem=false"))
	FInstancedPropertyBag UserParameters;

	/** Where the burst plays: where the Niagara Particles widget is, or on the widget this track is aimed at. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Burst At", EditCondition = "Action == EWidgetAnimParticleCueAction::BurstAtTarget", EditConditionHides))
	EWidgetAnimParticleCuePlace Place = EWidgetAnimParticleCuePlace::ParticlesWidget;

	/** Where on the track's widget: 0, 0 its top left, 0.5, 0.5 its middle, 1, 1 its bottom right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Burst Point", EditCondition = "Action == EWidgetAnimParticleCueAction::BurstAtTarget && Place == EWidgetAnimParticleCuePlace::TrackWidget", EditConditionHides))
	FVector2D BurstPoint = FVector2D(0.5, 0.5);

	/**
	 * The Niagara Particles widget to play on, by its name in the Widget Blueprint the track's widget is in or any around it.
	 * None: the track's widget when it is one, else the first inside it, else the nearest around it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Particles", meta = (DisplayName = "Particles Widget"))
	FName ParticlesWidget;

	/** The Niagara System's own values of User Parameters when they were last laid out: which of them this track changed. */
	UPROPERTY()
	FInstancedPropertyBag UserParameterDefaults;

	/** User Parameters that are Positions in the system (laid out as vectors) - set as positions. */
	UPROPERTY()
	TArray<FName> PositionParameters;

	/** Whether this fire plays the track's own Niagara System, rather than each widget's. */
	bool PlaysTrackSystem() const
	{
		return !bUseWidgetSystem && (Action == EWidgetAnimParticleCueAction::BurstAtTarget || Action == EWidgetAnimParticleCueAction::Play);
	}

	/** Whether a Burst / Play fires nothing: the track's own system is asked for (Use Widget's Niagara System off) and is None. */
	bool PlaysNothing() const
	{
		return PlaysTrackSystem() && NiagaraSystem == nullptr;
	}

	/** Whether it acts on every Niagara Particles widget found, not just the first. */
	bool ActsOnAll() const
	{
		return ActOn == EWidgetAnimParticleCueScope::AllFound && (bUseWidgetSystem || Action == EWidgetAnimParticleCueAction::StopEmitting || Action == EWidgetAnimParticleCueAction::Clear);
	}
};

class NAMETBDNIAGARA_API FWidgetAnimParticleCueEvaluator : public TWidgetAnimCueEvaluator<FWidgetAnimParticleCueParams>
{
public:

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const override;
	virtual bool FiresAtDesignTime(const FInstancedStruct& Params) const override { return true; }
	virtual void Fire(const FWidgetAnimCueContext& Context) const override;
	virtual bool RefreshParams(FInstancedStruct& Params) const override;
	virtual uint32 GetParamsLayoutHash(const FInstancedStruct& Params) const override;

	/**
	 * The Niagara Particles widget a cue plays on: the one named WidgetName in the User Widgets around TrackWidget (or Host);
	 * unnamed, TrackWidget itself or the first inside it, else the first in each User Widget around it, nearest first.
	 */
	static UNiagaraUIWidget* FindParticlesWidget(UWidget* TrackWidget, UWidget* Host, FName WidgetName);

	/**
	 * FindParticlesWidget, or with bAll every one: each named WidgetName in the User Widgets around TrackWidget (or Host);
	 * unnamed, every one at or inside TrackWidget, else every one in the nearest User Widget around it that has any.
	 */
	static void FindParticlesWidgets(UWidget* TrackWidget, UWidget* Host, FName WidgetName, bool bAll, TArray<UNiagaraUIWidget*>& OutWidgets);
};

/**
 * Particles tracks' User Parameters set from Blueprint - for a value that comes from gameplay rather than the animation: a
 * burst's color from the item's rarity, a count from a score.
 *
 * For a value that should follow one of the animation's own parameters, bind the track's User Parameter to an Animation
 * Parameter instead (the chip at the end of User Parameters' row in the Widget Animations panel) and set it with Set Widget
 * Animation Parameter - it is then part of the animation, with its default and preview.
 */
UCLASS(meta = (DisplayName = "Widget Animation Particles"))
class NAMETBDNIAGARA_API UWidgetAnimParticleTrackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Sets a User Parameter of the Particles tracks that Widget plays, from their next fire on - as if each track had that
	 * value set: the tracks of the Widget Animations it (or a widget inside it) plays - Anim In, Anim Out, by Anim ID, its
	 * state machines' states. Parameter: the User parameter's name (without "User.") and its value - Make Animation
	 * Parameter (Float), (Color), (Vector 2D)... AnimId: only the animations played with that Anim ID; None: every one. Only
	 * tracks that play their own Niagara System take it; with Use Widget's Niagara System, each Niagara Particles widget's
	 * own Set ... Parameter is the way. Kept until Clear Particle Track Parameters; a value set on a widget nearer the track
	 * wins over one set around it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Particles", meta = (DefaultToSelf = "Widget", Keywords = "niagara user parameter particles track cue"))
	static void SetParticleTrackParameter(UWidget* Widget, FWidgetAnimParameterValue Parameter, FName AnimId);

	/** Lets go of what Set Particle Track Parameter set on Widget: for the animations with that Anim ID, or - None - all of it. */
	UFUNCTION(BlueprintCallable, Category = "Widget Animation|Particles", meta = (DefaultToSelf = "Widget", Keywords = "niagara user parameter particles track cue"))
	static void ClearParticleTrackParameters(UWidget* Widget, FName AnimId);

	/**
	 * Writes what Set Particle Track Parameter set on the widgets around a fire - the track's widget, the widget playing it,
	 * and each User Widget around that, the nearest winning - into OutParams, a copy of Params. False, OutParams untouched,
	 * when nothing applies.
	 */
	static bool ApplyTrackParameters(const FWidgetAnimCueInfo& Info, const FWidgetAnimParticleCueParams& Params, FWidgetAnimParticleCueParams& OutParams);
};
