// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetAnimTextTracks.h"
#include "WidgetAnimCountUp.generated.h"

/** How a Count's length is set. */
UENUM(BlueprintType)
enum class EWidgetAnimCountPace : uint8
{
	Duration UMETA(ToolTip = "The track's Duration, however far it counts - timed and eased like any track."),

	PerSecond UMETA(DisplayName = "Per Second", ToolTip = "Count Speed a second, on average: a count twice as far takes twice as long, within Shortest and Longest."),

	BySizeOfChange UMETA(DisplayName = "By Size Of Change", ToolTip = "Longer the more digits the change has - Seconds Per Digit for each - so 5 to 10 is quick and 0 to 1,000,000 takes its time without dragging, within Shortest and Longest.")
};

/** Where a Count starts from. */
UENUM(BlueprintType)
enum class EWidgetAnimCountFrom : uint8
{
	StartValue UMETA(DisplayName = "Start Value", ToolTip = "Start Value, always."),

	NumberShown UMETA(DisplayName = "Number Shown Now", ToolTip = "The number the text shows as the track starts - the last count's end, say - so each count carries on from the one before.")
};

/** Where a Count counts to. */
UENUM(BlueprintType)
enum class EWidgetAnimCountTo : uint8
{
	EndValue UMETA(DisplayName = "End Value", ToolTip = "End Value, always."),

	NumberInText UMETA(DisplayName = "Number In Text", ToolTip = "The number the text holds as the track starts: set the text (or a numeric text's value) to the new total, then play, and it counts up to it.")
};

/** How a Count writes its number. */
UENUM(BlueprintType)
enum class EWidgetAnimCountStyle : uint8
{
	Number UMETA(ToolTip = "12,500 - with Decimals, and grouped as the player's culture groups thousands."),

	Percent UMETA(ToolTip = "The number as a percentage: 75 reads 75%."),

	Compact UMETA(ToolTip = "Shortened with K, M, B and T: 12.5K, 3.2M. Decimals sets the digits after the point."),

	Time UMETA(DisplayName = "Time (m:ss)", ToolTip = "Seconds as minutes and seconds: 125 reads 2:05."),

	TimeWithHours UMETA(DisplayName = "Time (h:mm:ss)", ToolTip = "Seconds as hours, minutes and seconds: 3725 reads 1:02:05."),

	TimeWithHundredths UMETA(DisplayName = "Time (m:ss.cc)", ToolTip = "Seconds to the hundredth: 65.5 reads 1:05.50 - a race timer.")
};

/** Which way a Slot Machine's new digits roll in. */
UENUM(BlueprintType)
enum class EWidgetAnimCountSlotFrom : uint8
{
	WithTheCount UMETA(DisplayName = "With The Count", ToolTip = "From below as the number goes up, from above as it goes down - the way a reel turns."),
	Below UMETA(ToolTip = "Always from below: the digits roll up."),
	Above UMETA(ToolTip = "Always from above: the digits roll down.")
};

/** Which way a Roulette's new digits roll in. */
UENUM(BlueprintType)
enum class EWidgetAnimCountRouletteFrom : uint8
{
	WithTheCount UMETA(DisplayName = "With The Count", ToolTip = "From the right as the number goes up, from the left as it goes down."),
	Right UMETA(ToolTip = "Always from the right: the digits roll left."),
	Left UMETA(ToolTip = "Always from the left: the digits roll right.")
};

/**
 * Count: a number counting from one value to another - a score landing, a price ticking down, XP filling in, a timer.
 *
 * Its pace is its Duration, or by Pace so many a second, or longer the bigger the change: the Widget Animations editor
 * then draws the track striped, as it does a value track, since its Duration means nothing. It can count from the number
 * already shown and to the number the text was just given, so one animation counts any change the game makes - set the
 * text to the new total and play.
 *
 * With Value timing it follows a number set from gameplay (Set Widget Animation Value) - set the track's Value At Start
 * and Value At End to its Start and End Value so the value is the number itself - and Steady smoothing counts toward
 * each new number at Steady Speed, speeding up to catch up on a big jump: a coin total that ticks up however often coins
 * arrive.
 *
 * Change Style says how each new number replaces the last on an animated text widget: at once, or its changed digits
 * (or the whole number) rolling in like a slot machine's reel or a roulette strip, fading, or melting into the next.
 *
 * On Common Animated Numeric Text, Use Widget's Format hands the number to the widget, which formats it its own way. On
 * any other text it writes the text, and puts the text back afterwards.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Count"))
struct NAMETBD_API FWidgetAnimCountUpParams : public FWidgetAnimTypeParams
{
	GENERATED_BODY()

	/** The number shown when the track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count", meta = (DisplayName = "Start Value"))
	double StartValue = 0.0;

	/** The number the track lands on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count", meta = (DisplayName = "End Value"))
	double EndValue = 100.0;

	/** Where it starts from: Start Value, or the number the text shows as the track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count", meta = (DisplayName = "Count From"))
	EWidgetAnimCountFrom CountFrom = EWidgetAnimCountFrom::StartValue;

	/** Where it counts to: End Value, or the number the text holds as the track starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count", meta = (DisplayName = "Count To"))
	EWidgetAnimCountTo CountTo = EWidgetAnimCountTo::EndValue;

	/** What sets how long it counts for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Pace", meta = (DisplayName = "Pace"))
	EWidgetAnimCountPace Pace = EWidgetAnimCountPace::Duration;

	/** How much it counts a second, on average. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Pace", meta = (DisplayName = "Count Speed", ClampMin = "0.001", EditCondition = "Pace == EWidgetAnimCountPace::PerSecond", EditConditionHides))
	double CountSpeed = 100.0;

	/** Seconds for each digit the change has: a change of 1,000 has three. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Pace", meta = (DisplayName = "Seconds Per Digit", ClampMin = "0.0", UIMax = "1.0", Units = "s", EditCondition = "Pace == EWidgetAnimCountPace::BySizeOfChange", EditConditionHides))
	float SecondsPerDigit = 0.3f;

	/** The shortest a paced count takes, however small the change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Pace", meta = (DisplayName = "Shortest", ClampMin = "0.01", UIMax = "2.0", Units = "s", EditCondition = "Pace != EWidgetAnimCountPace::Duration", EditConditionHides))
	float MinDuration = 0.25f;

	/** The longest a paced count takes, however big the change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Pace", meta = (DisplayName = "Longest", ClampMin = "0.01", UIMax = "10.0", Units = "s", EditCondition = "Pace != EWidgetAnimCountPace::Duration", EditConditionHides))
	float MaxDuration = 3.f;

	/** How the number is written. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Look", meta = (DisplayName = "Style"))
	EWidgetAnimCountStyle Style = EWidgetAnimCountStyle::Number;

	/** Digits shown after the decimal point. 0 counts in whole numbers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Look", meta = (ClampMin = "0", ClampMax = "6", UIMax = "4", EditCondition = "Style == EWidgetAnimCountStyle::Number || Style == EWidgetAnimCountStyle::Percent || Style == EWidgetAnimCountStyle::Compact", EditConditionHides))
	int32 Decimals = 0;

	/** Separates thousands the way the player's culture does - 12,500 rather than 12500. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Look", meta = (DisplayName = "Group Thousands", EditCondition = "Style == EWidgetAnimCountStyle::Number || Style == EWidgetAnimCountStyle::Percent", EditConditionHides))
	bool bUseGrouping = true;

	/**
	 * The text around the number, with {Value} where the number goes - "{Value} XP", "$ {Value}", "x{Value}". Empty
	 * shows the number alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Look")
	FText Format = INVTEXT("{Value}");

	/**
	 * On Common Animated Numeric Text: the number goes to the widget, formatted by its own Numeric Type and formatting
	 * settings, and Style, Decimals and Format are left out. Other texts ignore it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Look", meta = (DisplayName = "Use Widget's Format"))
	bool bUseWidgetFormat = true;

	/**
	 * How each new number replaces the last: at once, or rolling in like a slot machine's reel or a roulette strip,
	 * fading, or melting into it. Animated text widgets only - a plain Text Block changes at once.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Change Style"))
	EWidgetAnimTextChangeStyle ChangeStyle = EWidgetAnimTextChangeStyle::Instant;

	/** Whether only the digits that changed move, or the whole number as one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Animate", EditCondition = "ChangeStyle != EWidgetAnimTextChangeStyle::Instant", EditConditionHides))
	EWidgetAnimTextChangeScope ChangeScope = EWidgetAnimTextChangeScope::ChangedDigits;

	/** Which way new digits roll in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Roll From", EditCondition = "ChangeStyle == EWidgetAnimTextChangeStyle::SlotMachine", EditConditionHides))
	EWidgetAnimCountSlotFrom SlotFrom = EWidgetAnimCountSlotFrom::WithTheCount;

	/** Which way new digits roll in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Roll From", EditCondition = "ChangeStyle == EWidgetAnimTextChangeStyle::Roulette", EditConditionHides))
	EWidgetAnimCountRouletteFrom RouletteFrom = EWidgetAnimCountRouletteFrom::WithTheCount;

	/**
	 * Seconds each change takes. A count faster than this keeps its reels turning, each one carrying on toward the
	 * newest number; Fade and Blend suit numbers that change now and then - a score, a timer - better than a fast count.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Change Time", ClampMin = "0.01", UIMax = "1.0", Units = "s", EditCondition = "ChangeStyle != EWidgetAnimTextChangeStyle::Instant", EditConditionHides))
	float ChangeTime = 0.3f;

	/** How each change moves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Change Easing", EditCondition = "ChangeStyle != EWidgetAnimTextChangeStyle::Instant", EditConditionHides))
	EWidgetAnimEasing ChangeEasing = EWidgetAnimEasing::EaseOut;

	/**
	 * A digit rolls through every digit between its old and new one, as a reel does - 2 to 7 passes 3, 4, 5 and 6. Off,
	 * it rolls straight to the new one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Count|Change", meta = (DisplayName = "Spin Through Digits", EditCondition = "(ChangeStyle == EWidgetAnimTextChangeStyle::SlotMachine || ChangeStyle == EWidgetAnimTextChangeStyle::Roulette) && ChangeScope == EWidgetAnimTextChangeScope::ChangedDigits", EditConditionHides))
	bool bSpinThroughDigits = true;

	/** Where it counts from and to on the widget it is playing on, worked out as it starts. Never saved. */
	double PreparedStart = 0.0;
	double PreparedEnd = 0.0;
	bool bPrepared = false;

	/** The value it last wrote on its widget - which way the number went - on the playing copy. Never saved. */
	mutable double LastAppliedValue = 0.0;
	mutable bool bHasLastApplied = false;

	/** Its start or end, as prepared on its widget where it was. */
	double GetStart() const { return bPrepared ? PreparedStart : StartValue; }
	double GetEnd() const { return bPrepared ? PreparedEnd : EndValue; }

	/** Which way new characters come in, for FWidgetAnimTextEffects::ChangeFrom: +1, -1, or 0 with the count. */
	int32 GetChangeFrom() const;
};

/** What a Text Block said before a Count - or anything else that writes its text - touched it. Kept for old baselines. */
USTRUCT()
struct NAMETBD_API FWidgetAnimTextBaseline
{
	GENERATED_BODY()

	UPROPERTY()
	FText Text;
};

/** The Count type's evaluator. Registered by the NameTBD module; see FWidgetAnimEvaluator. */
class NAMETBD_API FWidgetAnimCountUpEvaluator : public TWidgetAnimTextTrackEvaluator<FWidgetAnimCountUpParams>
{
public:

	/** The channel it writes: a text's own text. Anything else that writes the whole text shares it. */
	static const FName TextChannel;

	virtual FText GetDisplayName() const override;
	virtual FText GetDescription() const override;
	virtual FText GetValueLabel() const override;
	virtual FName GetChannel(const FInstancedStruct& Params) const override { return TextChannel; }

	virtual bool NeedsPlayPreparation(const FInstancedStruct& Params) const override;
	virtual float PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const override;
	virtual bool HasDerivedLength(const FInstancedStruct& Params) const override;
	virtual FText GetDerivedLengthLabel(const FInstancedStruct& Params) const override;

	virtual double GetEndpointValue(const FInstancedStruct& Params, bool bStart) const override;
	virtual double GetQuantizeStep(const FInstancedStruct& Params) const override;
	virtual void Apply(UObject* Target, const FInstancedStruct& Params, double Value) const override;

	virtual bool CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const override;
	virtual void RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const override;

	/** Value as a Count with these settings writes it - rounded, in its Style, and placed in its Format. */
	static FText FormatValue(const FWidgetAnimCountUpParams& Params, double Value);

	/** Seconds a count from From to To takes at Params' Pace. Negative for Duration, which keeps the track's own. */
	static float GetPacedSeconds(const FWidgetAnimCountUpParams& Params, double From, double To);

protected:

	/** Its change style - how the number changes - taken off the text. */
	virtual bool ClearEffects(FWidgetAnimTextEffects& Effects) const override;
};
