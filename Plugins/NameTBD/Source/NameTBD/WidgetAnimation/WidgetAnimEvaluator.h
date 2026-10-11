// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/InstancedStruct.h"
#include "WidgetAnimEvaluator.generated.h"

class UWidget;
class FWidgetAnimEvaluator;
struct FWidgetAnimCueContext;
struct FWidgetAnimAuthoritativeWrite;

/**
 * The base every registered track type's settings derive from.
 *
 * A registered type is a pair: a struct of settings deriving from this, and an FWidgetAnimEvaluator that knows
 * what to do with them, registered with FWidgetAnimEvaluatorRegistry under that struct. The struct IS the type -
 * there is no enum value per registered type - which is what lets a game or licensee module add one without
 * touching this plugin: a new struct is found by reflection, and its evaluator is found by the struct.
 *
 * A track holds its settings in an FInstancedStruct (FWidgetAnimLayer::TypeParams), so it carries exactly the
 * fields its own type needs instead of every type's fields side by side.
 *
 * Empty on purpose. Anything every registered type has - its timing, its keys, its looping - already lives on the
 * track itself and is shared by every type, built in or registered.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/**
	 * The letters of its text a text track is limited to - each a first letter (X) and how many (Y) - found from its track's
	 * Part of the Text (see FWidgetAnimTextTarget) as it starts, on the copy of its settings that plays. Empty: the whole text.
	 * Read only by a type whose TakesTextRange says so. Not reflected: never saved, edited or compared.
	 */
	TArray<FIntPoint> TextLetters;
};

/**
 * How a registered type's value is combined with what other tracks write in the same frame.
 *
 * Authoritative and Cue so far. The built-in types already show the other shapes this will grow into - opacity and
 * scale multiply, translation and angle add, material parameters are a keyed map - and each will be added here when
 * the first registered type needs it, rather than guessed at now.
 */
enum class EWidgetAnimComposition : uint8
{
	/**
	 * One writer per target and channel, the last to write in a frame winning. For values that cannot be added or
	 * multiplied - a piece of text, an icon, a frame of a flipbook - so two tracks on the same text take turns rather
	 * than blending into nonsense. The target's own value is captured as a baseline and put back afterwards.
	 */
	Authoritative,

	/**
	 * Writes nothing: something happens at moments on the timeline instead - an event, a sound, another animation
	 * started. The track has no pose, so nothing to compose or put back; the timeline tells the type when its moments
	 * are passed, through Fire. See WidgetAnimCue.h for when a cue fires and when it doesn't.
	 */
	Cue
};

/** What each key between a registered track's start and end holds - see FWidgetAnimEvaluator::GetKeyContent. */
enum class EWidgetAnimKeyContent : uint8
{
	/** A value, as every track's keys hold. */
	Value,

	/** A color only: the track blends from color to color, its start color to its keys' to its end color. */
	Color,

	/** A value and a color, each blended between the points - a shimmer's place and its band's color. */
	ValueAndColor
};

/**
 * What a registered track type does: how its value reaches its target, and how the target is put back.
 *
 * What it does NOT do is timing. Duration, Delay, Easing, keys and their segment curves, Spring timing, Loop, Ping
 * Pong, Invert and scrubbing all stay in the shared timeline, the same for every type. The library works out where
 * the track is between its start value, its keys and its end value, and hands the evaluator only the value that
 * comes out. That keeps every registered type scrubbable and loopable for free, and it is what will let a driver
 * other than the clock (a spring retargeted live, a value bound to gameplay) feed any type without the type knowing.
 *
 * One instance per type, shared by every track of that type, so it holds no state of its own - everything that
 * varies lives in the track's settings, handed to each call. Registered with FWidgetAnimEvaluatorRegistry, usually
 * from a module's StartupModule. TWidgetAnimEvaluator below fills in the identity for a given settings struct.
 */
class NAMETBD_API FWidgetAnimEvaluator : public TSharedFromThis<FWidgetAnimEvaluator>
{
public:

	virtual ~FWidgetAnimEvaluator() = default;

	// ---------------------------------------------------------------------------------------------
	// Identity
	// ---------------------------------------------------------------------------------------------

	/** The settings struct this evaluator is registered under - which is what a track's type is. */
	virtual const UScriptStruct* GetParamsStruct() const = 0;

	/** What the type is called: in the Add Track menu, on its tracks, in the track list. "Text Reveal". */
	virtual FText GetDisplayName() const = 0;

	/**
	 * A sentence or two on what the type does, shown in its tracks' tooltips. Supplied by the type rather than kept
	 * with the plugin's own tooltip text because a type from another module has nowhere else to put it.
	 */
	virtual FText GetDescription() const { return FText::GetEmpty(); }

	/** What the value at a key is called in the Selection panel - "Count" on a Count. */
	virtual FText GetValueLabel() const;

	/**
	 * The same three, for one track of this type. A type whose tracks are all alike leaves these alone; one that
	 * stands in for many types - the bridge that runs Blueprint types - names each track by the type it holds.
	 */
	virtual FText GetTrackDisplayName(const FInstancedStruct& Params) const { return GetDisplayName(); }
	virtual FText GetTrackDescription(const FInstancedStruct& Params) const { return GetDescription(); }
	virtual FText GetTrackValueLabel(const FInstancedStruct& Params) const { return GetValueLabel(); }

	/**
	 * Whether the Add Track menu offers this type as an entry of its own. False for an evaluator that only stands in
	 * for other types, such as the Blueprint bridge, whose types are listed one by one instead.
	 */
	virtual bool IsListedInAddMenu() const { return true; }

	/**
	 * The heading the Add Track menu lists this type under - "Text" for the text tracks. Empty lists it under
	 * Registered Types.
	 */
	virtual FText GetCategory() const { return FText::GetEmpty(); }

	/**
	 * Whether a new track of this type starts with Linear easing rather than the usual Ease Out: types whose value is a
	 * steady pace, such as a text typed out, where easing would only make the pace uneven.
	 */
	virtual bool PrefersLinearEasing() const { return false; }

	// ---------------------------------------------------------------------------------------------
	// Target
	// ---------------------------------------------------------------------------------------------

	/** The kind of widget this type animates - a Text Block for anything that writes text. */
	virtual UClass* GetTargetClass() const;

	/** The kind of widget one track of this type animates. GetTargetClass(), unless the type varies by track. */
	virtual UClass* GetTrackTargetClass(const FInstancedStruct& Params) const { return GetTargetClass(); }

	/**
	 * The object this type writes when a track of it is played on Widget - the Track Group's target, or the widget
	 * the animation is played on when the group names none.
	 *
	 * By default Widget itself when it is a GetTargetClass(), or else the first one inside it, depth first - so a
	 * Count pointed at a whole button counts on the button's label. Null when there is none, and the track then
	 * quietly does nothing, the way a Material track does on something that is not an Image.
	 */
	virtual UObject* ResolveTarget(UWidget* Widget, const FInstancedStruct& Params) const;

	/**
	 * Which of the target's values this track writes. Two tracks writing the same channel of the same target take
	 * turns - the last one wins that frame - and share one baseline, whatever their types: a Count and a Blueprint
	 * type that writes text, on one label, both write its "Text".
	 */
	virtual FName GetChannel(const FInstancedStruct& Params) const { return NAME_None; }

	// ---------------------------------------------------------------------------------------------
	// Value
	// ---------------------------------------------------------------------------------------------

	virtual EWidgetAnimComposition GetComposition() const { return EWidgetAnimComposition::Authoritative; }

	/** Whether this type is a cue: it fires at moments rather than writing a value. See EWidgetAnimComposition::Cue. */
	bool IsCue() const { return GetComposition() == EWidgetAnimComposition::Cue; }

	/**
	 * The smallest change in value that changes what the target shows - 1 for a whole-number count, 0.01 for two
	 * decimals. When set, Apply is only called on frames where the value has moved to a different step, so a count
	 * crawling between whole numbers, or a track holding its end, costs nothing. 0 calls Apply on every frame the
	 * track writes, which is right for a truly continuous value and expensive for anything else - above all a
	 * Blueprint type, where every call runs Blueprint.
	 */
	virtual double GetQuantizeStep(const FInstancedStruct& Params) const { return 0.0; }

	/**
	 * Whether a track of this type keeps changing what its target shows while its value stands still - a Scramble's
	 * stand-in characters picked afresh several times a second, from the clock, at any value short of settled. A host
	 * whose tracks have all finished stops working them out every frame (see UWidgetAnimationComponent::TickPlayer),
	 * unless one of them says so here. False for a type whose look follows only its value - nearly every type.
	 */
	virtual bool AnimatesAtRest(const FInstancedStruct& Params) const { return false; }

	/**
	 * Whether the type can be limited to some of its text's letters - a track's Part of the Text (see FWidgetAnimTextTarget) -
	 * reading them from FWidgetAnimTypeParams::TextLetters. False: such a track plays on the whole text, as before.
	 */
	virtual bool TakesTextRange(const FInstancedStruct& Params) const { return false; }

	// ---------------------------------------------------------------------------------------------
	// Play-time length
	// ---------------------------------------------------------------------------------------------

	/**
	 * Whether a track of this type works something out when it starts playing, from the widget it plays on - how long
	 * it runs, say, from how many letters its text has. PrepareForPlay is only called when this is true.
	 */
	virtual bool NeedsPlayPreparation(const FInstancedStruct& Params) const { return false; }

	/**
	 * Called as a track of this type starts playing - and whenever a preview poses it - on the track's own copy of its
	 * settings: Target is what ResolveTarget found on the widget it plays on (null when nothing). May fill in runtime
	 * state of Params (non-reflected members, which each playing copy owns), and returns how many seconds the track
	 * runs, or a negative number to keep AuthoredRun. A new length moves the tracks placed after it, as a stagger does.
	 */
	virtual float PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const { return -1.f; }

	/**
	 * Whether Target can be prepared for yet - false while what PrepareForPlay counts isn't known, such as a wrapping
	 * text's lines before it has been given a width. A player waits a few frames for it, as it does for a stagger.
	 */
	virtual bool IsReadyToPrepare(UObject* Target, const FInstancedStruct& Params) const { return true; }

	/**
	 * Whether a track of this type, waiting for its start, shows its start rather than leaving its target alone: a text
	 * still to be typed out is hidden from the moment the animation starts, not shown whole until its track begins.
	 * Only asked of types that prepare for play.
	 */
	virtual bool HoldsStartPose(const FInstancedStruct& Params) const { return false; }

	/**
	 * Whether the track's length comes from PrepareForPlay rather than its Duration - a text typed out at a Typing
	 * Speed, a count at so many per second. Such a track plays as a Tween (a Spring's length is its own), Linear too if
	 * the type PrefersLinearEasing, and the Widget Animations editor draws it striped, as it draws a value track, since
	 * its authored Duration means nothing.
	 */
	virtual bool HasDerivedLength(const FInstancedStruct& Params) const { return false; }

	/** A few words saying what sets a derived length, shown on the track in place of its Duration - "30 letters/s". */
	virtual FText GetDerivedLengthLabel(const FInstancedStruct& Params) const { return FText::GetEmpty(); }

	/**
	 * The track's value at its start (bStart) or its end. Every key in between holds its own value on the track, the
	 * way it does on every keyed type, so these two are all a type has to say about where it travels.
	 */
	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const = 0;

	/**
	 * Writes Value to Target - once per frame, after every track has had its say. Value is continuous and may run
	 * past the endpoints while a spring overshoots; rounding it to something the target can show (a whole number, a
	 * number of letters) is this function's business, not the timeline's.
	 */
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const = 0;

	// ---------------------------------------------------------------------------------------------
	// Colors
	// ---------------------------------------------------------------------------------------------

	/**
	 * What the keys between a track's start and end hold: a value (the default), a color, or both. A type whose keys
	 * hold a color says what its start and end colors are with GetEndpointColor, and receives the color the timeline
	 * blends to in ApplyWithColor - so a Text Color passes from red to gold to white, one color per key.
	 */
	virtual EWidgetAnimKeyContent GetKeyContent(const FInstancedStruct& Params) const { return EWidgetAnimKeyContent::Value; }

	/** What a key's color is called in the Selection panel - "Band Color" on a Shimmer. */
	virtual FText GetKeyColorLabel(const FInstancedStruct& Params) const;

	/** The track's color at its start (bStart) or its end, for a type whose keys hold colors. */
	virtual FLinearColor GetEndpointColor(const FInstancedStruct& Params, bool bStart) const { return FLinearColor::White; }

	/**
	 * Apply, for a type whose keys hold colors: Value as Apply receives it, and Color blended from the start color
	 * through the keys' to the end color the same way. Calls Apply by default.
	 */
	virtual void ApplyWithColor(UObject* Target, const FInstancedStruct& Params, double Value, const FLinearColor& Color) const { Apply(Target, Params, Value); }

	/**
	 * What applying one live write does: ApplyWithColor for a write carrying a color, Apply otherwise. Overridden by a
	 * type that needs more of the write than its value - which key it is between (FWidgetAnimAuthoritativeWrite::
	 * KeySegment), or what the target held before any animation (Captured: the baseline this type's CaptureBaseline
	 * recorded for Target, or null when none was) - as the Shape track does for its keys' Use Authored Shape.
	 */
	virtual void ApplyWrite(UObject* Target, const FInstancedStruct& Params, const FWidgetAnimAuthoritativeWrite& Write, const FInstancedStruct* Captured) const;

	/**
	 * Whether two writes of this type to one channel can be blended by blending their values - a state machine's
	 * cross-fade does. False for a type whose value only means something with the rest of its write (a key's place
	 * along the keys): such writes change hands halfway instead.
	 */
	virtual bool BlendsWriteValues() const { return true; }

	// ---------------------------------------------------------------------------------------------
	// Baseline
	// ---------------------------------------------------------------------------------------------

	/**
	 * Records what Target's channel holds before any animation touches it, into OutBaseline - a struct of the
	 * evaluator's own choosing. False when there is nothing to record, and the track then never puts anything back.
	 */
	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const { return false; }

	/** Puts back what CaptureBaseline recorded. */
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const {}

	// ---------------------------------------------------------------------------------------------
	// Cues
	// ---------------------------------------------------------------------------------------------

	/**
	 * A cue type's one moment of behavior: the playhead has passed one of the track's moments. Called a frame after the
	 * moment was passed, never in the middle of an animation being applied, so it may play, stop or destroy anything -
	 * the animation it came from included. Only called on a type whose composition is Cue. See WidgetAnimCue.h.
	 */
	virtual void Fire(const FWidgetAnimCueContext& Context) const {}

	/**
	 * Whether a cue of this type fires while an editor preview plays - the Widget Animations panel's, or the designer
	 * toolbar's. A sound does, so it can be heard where it is timed; an event, which the game handles, doesn't.
	 */
	virtual bool FiresAtDesignTime(const FInstancedStruct& Params) const { return false; }

	/**
	 * A few words on what one track of this type does, drawn on its track in the Widget Animations editor in place of
	 * its easing - "Landed", "UI_Click". Empty draws the easing, as every other track does.
	 */
	virtual FText GetTrackSummary(const FInstancedStruct& Params) const { return FText::GetEmpty(); }

	// ---------------------------------------------------------------------------------------------
	// Editing
	// ---------------------------------------------------------------------------------------------

	/**
	 * Called in the editor after a track's settings were edited, on the track and on what the Details panel shows of it:
	 * brings up to date whatever the settings derive from something else - a Particles track's User Parameters, listed from
	 * its Niagara System. True if it changed anything.
	 */
	virtual bool RefreshParams(FInstancedStruct& Params) const { return false; }

	/**
	 * What decides the rows the settings show in the Details panel, beyond their struct - a property bag laid out from an
	 * asset. When it changes the Widget Animations panel lays its Selection panel out again. 0: nothing beyond the struct.
	 */
	virtual uint32 GetParamsLayoutHash(const FInstancedStruct& Params) const { return 0; }

protected:

	/** The first TargetClass at or beneath Widget, depth first, through panels and into User Widgets. */
	static UObject* FindFirstTarget(UWidget* Widget, const UClass* TargetClass);
};

/**
 * An evaluator for one settings struct, with the identity filled in and the settings handed over typed.
 *
 *     class FMyEvaluator : public TWidgetAnimEvaluator<FMyParams> { ... };
 *
 * Everything else is the same as FWidgetAnimEvaluator.
 */
template <typename TParams>
class TWidgetAnimEvaluator : public FWidgetAnimEvaluator
{
public:

	virtual const UScriptStruct* GetParamsStruct() const override { return TParams::StaticStruct(); }

protected:

	/** The track's settings as this type's struct - or a subclass of it. Null only if the registry was bypassed. */
	static const TParams* GetParams(const FInstancedStruct& Params) { return Params.GetPtr<TParams>(); }
};

/**
 * Every registered track type, by its settings struct.
 *
 * Registration is a module's business: first-party types are registered by the NameTBD module as it starts, a
 * game's by its own module, each unregistered again as its module shuts down. A track whose type is not registered
 * - its module not loaded, say - is kept intact and simply plays as nothing.
 *
 * Game thread only, like everything else that touches widgets.
 */
class NAMETBD_API FWidgetAnimEvaluatorRegistry
{
public:

	static FWidgetAnimEvaluatorRegistry& Get();

	/** Adds Evaluator under its settings struct, replacing whatever was registered there - a hot reload, say. */
	void Register(const TSharedRef<FWidgetAnimEvaluator>& Evaluator);

	/** Removes the evaluator registered under ParamsStruct, if there is one. */
	void Unregister(const UScriptStruct* ParamsStruct);

	/**
	 * The evaluator for ParamsStruct - or, if that struct has none of its own, for the nearest struct it derives
	 * from that does, so a game's variant of a Count plays as a Count without registering anything. Null when
	 * nothing in its line is registered.
	 */
	const FWidgetAnimEvaluator* Find(const UScriptStruct* ParamsStruct) const;

	/** The evaluator for the settings a track holds. Null for empty settings. */
	const FWidgetAnimEvaluator* Find(const FInstancedStruct& Params) const { return Find(Params.GetScriptStruct()); }

	/** Every registered evaluator, in display-name order - the order the Add Track menu lists them in. */
	void GetAll(TArray<const FWidgetAnimEvaluator*>& OutEvaluators) const;

	/** Broadcast whenever a type is registered or unregistered, so menus and panels can refresh. */
	FSimpleMulticastDelegate& OnChanged() { return ChangedDelegate; }

private:

	TMap<const UScriptStruct*, TSharedRef<FWidgetAnimEvaluator>> Evaluators;
	FSimpleMulticastDelegate ChangedDelegate;
};

/**
 * One registered track's write for this frame, in FWidgetAnimResult's authoritative channel.
 *
 * Deliberately not reflected and deliberately cheap: it points at the settings it was evaluated from instead of
 * copying them, so folding a frame allocates nothing. That is safe because a result is applied in the same call that
 * built it everywhere but one place - UWidgetAnimLibrary::TickWidgetAnimations, which hands its result to the caller
 * and so copies the settings into OwnedParams first (see DetachParams).
 */
struct NAMETBD_API FWidgetAnimAuthoritativeWrite
{
	/** The widget the track was played on, before its evaluator narrows it. Null: the fallback widget. */
	UWidget* TargetWidget = nullptr;

	FName Channel;

	const FWidgetAnimEvaluator* Evaluator = nullptr;

	/**
	 * The track's settings, while the result is still inside the call that built it. Set on baseline writes too, since
	 * which object the channel belongs to can depend on them.
	 */
	const FInstancedStruct* Params = nullptr;

	/** A copy of the settings, once DetachParams has run. Read only when Params is null. */
	FInstancedStruct OwnedParams;

	double Value = 0.0;

	/**
	 * For a keyframed type that writes through this channel (the Shape track): the key the track is leaving, Value then
	 * being how far toward the next it is. INDEX_NONE for every other write.
	 */
	int32 KeySegment = INDEX_NONE;

	/** The color the track has blended to, for a type whose keys hold colors (bHasColor). */
	FLinearColor Color = FLinearColor::White;
	bool bHasColor = false;

	/**
	 * Only the captured baseline, put in for a track that isn't writing this frame - not started yet, hidden, or
	 * just let go. A track that IS writing the same channel wins over it, whichever order the two came in, exactly
	 * as an authored Shape Box outline does.
	 */
	bool bIsBaseline = false;

	const FInstancedStruct& GetParams() const { return Params ? *Params : OwnedParams; }

	/** Copies the settings in, so the write outlives the settings it was evaluated from. */
	void DetachParams()
	{
		if (Params)
		{
			OwnedParams = *Params;
			Params = nullptr;
		}
	}
};

/**
 * What one registered track's target held before any animation touched it.
 *
 * Keyed by the resolved target and the channel rather than by track, so two tracks writing the same text share one
 * capture - capturing twice would record a value an animation had already written.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimAuthoritativeBaseline
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	TObjectPtr<UObject> Target = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Animation")
	FName Channel;

	/** Which type captured it - the settings struct its evaluator is registered under - and so which puts it back. */
	UPROPERTY()
	TObjectPtr<UScriptStruct> CapturedBy = nullptr;

	UPROPERTY()
	FInstancedStruct Captured;
};

/**
 * A registered track's start or end value, for code that reads a keyed track's points without knowing its type -
 * see WidgetAnimKeyPoints. 0 when the type isn't registered.
 */
NAMETBD_API double WidgetAnimGetRegisteredEndpoint(const FInstancedStruct& Params, bool bStart);

/** A registered track's start or end color, when its keys hold colors - see WidgetAnimKeyPoints. White otherwise. */
NAMETBD_API FLinearColor WidgetAnimGetRegisteredEndpointColor(const FInstancedStruct& Params, bool bStart);

/** What a registered track's keys hold. Value when its type isn't registered. */
NAMETBD_API EWidgetAnimKeyContent WidgetAnimGetRegisteredKeyContent(const FInstancedStruct& Params);

/** Whether a registered track's settings are a cue type's - see EWidgetAnimComposition::Cue. False when unregistered. */
NAMETBD_API bool WidgetAnimIsRegisteredCue(const FInstancedStruct& Params);