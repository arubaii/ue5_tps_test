// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"

#include "WidgetDataBindingTypes.generated.h"

/** When a Field Binding reads its source again. */
UENUM(BlueprintType)
enum class EWidgetDataUpdateMode : uint8
{
	Event UMETA(DisplayName = "On Event", ToolTip = "Reads the source again whenever the named event (an event dispatcher / multicast delegate, such as On Health Changed) fires. Its parameters are ignored; the value is read from Source Path. The cheapest mode for sources that announce their changes."),
	FieldNotify UMETA(DisplayName = "On Source Field Notify", ToolTip = "The object holding the last part of Source Path is Field Notify (a ViewModel, or a C++ class with Field Notify fields), and Source Path ends on one of its fields. Reads again exactly when that field broadcasts. Falls back to polling every frame, with a warning, if the source isn't Field Notify."),
	Poll UMETA(DisplayName = "Poll", ToolTip = "Reads the source on a timer (Poll Interval, 0 = every frame). Works with anything. Nothing is written or broadcast unless the value actually changed, so an unchanged value costs only the read."),
	Manual UMETA(ToolTip = "Reads the source when it is set, and afterwards only when Refresh From Source is called. For data that arrives in replies rather than continuously, such as another player's stats fetched on request.")
};

/** What happens to the ViewModel when the source goes away (destroyed, or its actor destroyed). */
UENUM(BlueprintType)
enum class EWidgetDataSourceLost : uint8
{
	KeepValues UMETA(ToolTip = "The ViewModel keeps the last values it was given."),
	ResetToDefaults UMETA(DisplayName = "Reset To Defaults", ToolTip = "Every field a Field Binding writes goes back to the ViewModel class's default value, which also lets triggers see the change (a Low Health pulse stops when its pawn is gone).")
};

/** What a trigger tests on its ViewModel field. */
UENUM(BlueprintType)
enum class EWidgetDataTriggerCondition : uint8
{
	IsTrue UMETA(DisplayName = "Is True", ToolTip = "True while the field is true: a set bool, a non-zero number, a valid object, a non-empty string or text."),
	IsFalse UMETA(DisplayName = "Is False", ToolTip = "True while the field is false (the opposite of Is True)."),
	Compare UMETA(ToolTip = "True while the field compares against Threshold, for example Health Percent < 0.25."),
	Changed UMETA(ToolTip = "Fires every time the field changes to a different value. There is no 'while' state, so Play While True acts as Play.")
};

UENUM(BlueprintType)
enum class EWidgetDataComparison : uint8
{
	Less UMETA(DisplayName = "<"),
	LessOrEqual UMETA(DisplayName = "<="),
	Greater UMETA(DisplayName = ">"),
	GreaterOrEqual UMETA(DisplayName = ">="),
	Equal UMETA(DisplayName = "=="),
	NotEqual UMETA(DisplayName = "!=")
};

/** What a trigger does to its animations. */
UENUM(BlueprintType)
enum class EWidgetDataTriggerAction : uint8
{
	PlayWhileTrue UMETA(DisplayName = "Play While True", ToolTip = "Plays when the condition becomes true and stops when it becomes false. Pair with a looping animation for a state flair such as Firing or Low Health."),
	Play UMETA(ToolTip = "Plays when the condition becomes true. Nothing happens when it becomes false. For one-shots such as a hit flash."),
	Stop UMETA(ToolTip = "Stops the animations when the condition becomes true."),
	SetValue UMETA(DisplayName = "Set Value", ToolTip = "Drives value-driven tracks (Timing = Value) with the field: every change sets the value of Value Name - a number or bool, a Vector 2D or a Linear Color, as it is. Each track maps it onto itself through its own Value At Start and Value At End. A loading bar following Load Progress, a health bar following Health. Condition is not used."),

	SetProgress UMETA(DisplayName = "Set Progress", ToolTip = "Drives percentage animations (Time In Percent) with the field: every change sets the progress of the entries with the Anim IDs - every percentage animation when empty - 0 for 0% and 1 for 100%; a bool is 0 or 1. A hold ring following Hold Progress, a charge meter following Charge. Condition is not used."),

	SetInput UMETA(DisplayName = "Set State Machine Input", ToolTip = "Drives a Widget Animation State Machine's input with the field: every change sets the input named Input Name on the machine the Animation Widget plays. A Bool input is true while the field is (a non-zero number, a set bool); a Number takes the number; a Trigger fires each time the field changes to something true. Is Selected, Has Unread, Charge. Condition is not used."),

	SetParameter UMETA(DisplayName = "Set Animation Parameter", ToolTip = "Drives an Animation Parameter with the field: every change sets the parameter named Parameter Name on the widget the Animation Widget plays - for the entries with the Anim IDs, or every animation when empty - and what plays takes it at once (Set Widget Animation Parameter). A Float or Integer field gives a number, a bool a Bool, a Vector 2D or Linear Color itself, a Name, String, Text or enum a Choice's option by name. A slide as far as Offset, a flash in Team Color. Condition is not used.")
};

/**
 * One row of the binder's first table: a value on the source object, copied into a ViewModel field.
 *
 * Only ever copies. Anything derived from several inputs (a health percent, a danger level) belongs in the
 * ViewModel or behind a getter on the source, where it can be named and tested; Source Path can end on a
 * getter function (Blueprint Pure, no inputs), so "HealthComponent.GetHealthPercent" works.
 */
USTRUCT(BlueprintType)
struct NAMETBDMVVM_API FWidgetFieldBinding
{
	GENERATED_BODY()

	/**
	 * Where the value is on the source: property names separated by dots, stepping into objects and structs,
	 * for example "HealthComponent.Health". Any part can be a getter function (Blueprint Pure, no inputs), and
	 * on an actor any part can name a component. The dropdown lists what Source Class has.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (GetOptions = "GetSourcePathOptions"))
	FString SourcePath;

	/** The ViewModel field that receives the value. Must be a Field Notify field of the ViewModel Class. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (DisplayName = "ViewModel Field", GetOptions = "GetViewModelFieldOptions"))
	FName ViewModelField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding")
	EWidgetDataUpdateMode Update = EWidgetDataUpdateMode::Poll;

	/**
	 * The event whose firing means "read again": an event dispatcher / multicast delegate on the source, by
	 * path like Source Path, for example "HealthComponent.OnHealthChanged". Its parameters are ignored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (EditCondition = "Update == EWidgetDataUpdateMode::Event", EditConditionHides, GetOptions = "GetEventPathOptions"))
	FString EventPath;

	/** Seconds between reads. 0 reads every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Binding", meta = (EditCondition = "Update == EWidgetDataUpdateMode::Poll", EditConditionHides, ClampMin = "0", UIMin = "0", Units = "s"))
	float PollInterval = 0.f;
};

/**
 * One row of the binder's second table: a condition on a ViewModel field that plays or stops Widget
 * Animations by Anim ID.
 *
 * Edge-triggered: a trigger acts when its condition changes, not while it holds, so a Play While True on a
 * looping animation starts it once and stops it once however many times the field changes in between.
 */
USTRUCT(BlueprintType)
struct NAMETBDMVVM_API FWidgetFieldTrigger
{
	GENERATED_BODY()

	/** Optional. Passed to On Trigger Changed, so a graph can tell triggers apart (for a sound, say). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	FName TriggerName;

	/** The ViewModel field this trigger watches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "ViewModel Field", GetOptions = "GetViewModelFieldOptions"))
	FName ViewModelField;

	/** Not used by Set Value, Set Progress or Set State Machine Input, which act on every change of the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	EWidgetDataTriggerCondition Condition = EWidgetDataTriggerCondition::IsTrue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "Condition == EWidgetDataTriggerCondition::Compare && Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	EWidgetDataComparison Comparison = EWidgetDataComparison::Less;

	/** The value the field is compared against. Bools are 0 and 1, enums their numeric value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "Condition == EWidgetDataTriggerCondition::Compare && Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	double Threshold = 0.0;

	/**
	 * How far past Threshold the value has to come back before the condition counts as false again, so a
	 * value hovering on the threshold doesn't flicker the animation on and off. With < 0.25 and Hysteresis
	 * 0.05, Low Health starts below 0.25 and stops only above 0.30. Used by <, <=, > and >=.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "Condition == EWidgetDataTriggerCondition::Compare && Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides, ClampMin = "0", UIMin = "0"))
	double Hysteresis = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	EWidgetDataTriggerAction Action = EWidgetDataTriggerAction::PlayWhileTrue;

	/**
	 * Set Value: the Value Name of the value-driven tracks the field drives - the name given on the tracks in the
	 * Widget Animations editor, listed in the Widget Animation Component's Value Tracks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (EditCondition = "Action == EWidgetDataTriggerAction::SetValue", EditConditionHides))
	FName ValueName;

	/**
	 * The Anim IDs to play or stop - or, for Set Progress, whose percentage animations the field drives. Empty means every
	 * Anim In entry, the same as Play Widget Animation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Anim IDs", EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetInput", EditConditionHides))
	TArray<FName> AnimIds;

	/**
	 * Set State Machine Input: the input of the state machine the field drives - an input of a Widget Animation State Machine
	 * played by the Animation Widget's Widget Animation Component (or one inside it, or outwards from it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Input Name", EditCondition = "Action == EWidgetDataTriggerAction::SetInput", EditConditionHides))
	FName InputName;

	/**
	 * Set Animation Parameter: the parameter the field drives - an Animation Parameter (see the Widget Animations panel's
	 * Parameters) of the animations the Animation Widget plays, by name.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Parameter Name", EditCondition = "Action == EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	FName ParameterName;

	/**
	 * The widget whose Widget Animation Component (or animation host) owns the Anim IDs. None is the widget
	 * this binder is on. When the picked widget has no component, the search continues outwards through its
	 * parents, the same way Play Widget Animation does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger", meta = (DisplayName = "Animation Widget", GetOptions = "GetAnimationWidgetOptions"))
	FName AnimationWidget;

	/**
	 * When the binder starts (the widget is constructed, or a new source is set), a condition that is already
	 * true acts straight away. On by default, so a row created for a badly hurt pawn starts pulsing at once.
	 * Off: only a change after binding acts. A condition that turns false at a rebind always stops its
	 * animations either way. For Set Value: on, the value at binding is shown at once (snapped, not traveled to),
	 * so a row pointed at another pawn shows that pawn's health straight away; off, nothing is set until it changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trigger")
	bool bEvaluateOnBind = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	EWidgetAnimPlayDirection Direction = EWidgetAnimPlayDirection::Forward;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides, ClampMin = "0.01", UIMin = "0.01"))
	float PlaybackSpeed = 1.f;

	/** Off: a one-shot returns the widget to normal when it ends. On: it stays on its final pose. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	bool bHoldFinalPose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	bool bRestartIfPlaying = true;

	/** When stopping: snap the widgets back to where they were before the animation began, rather than holding them where it got to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Trigger", meta = (EditCondition = "Action != EWidgetDataTriggerAction::Play && Action != EWidgetDataTriggerAction::SetValue && Action != EWidgetDataTriggerAction::SetProgress && Action != EWidgetDataTriggerAction::SetInput && Action != EWidgetDataTriggerAction::SetParameter", EditConditionHides))
	bool bResetToStartOnStop = true;
};

/** A design-time value for one ViewModel field, written as text the way the Details panel would (42, 0.25, true, an enum name). */
USTRUCT(BlueprintType)
struct NAMETBDMVVM_API FWidgetDataMockValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mock", meta = (DisplayName = "ViewModel Field", GetOptions = "GetViewModelFieldOptions"))
	FName ViewModelField;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mock")
	FString Value;
};
