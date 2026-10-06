// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "WidgetAnimParts.generated.h"

// =====================================================================================================
// Parts of a widget
// =====================================================================================================
//
// Most tracks move a whole widget: its render transform and opacity. Some widgets are made of pieces a track should be
// able to move one by one - the letters of a text, above all - that have no widget of their own to move. Such a widget
// is a part host: it names its parts by unit ("Letter", "Word", "Line") and index, and takes a pose for any of them.
//
// A track reaches parts through a Stagger (see WidgetAnimExpander.h): Each Letter fans the track out into one copy per
// letter, each copy aimed at its letter as a part. The copies compose exactly as whole-widget tracks do - opacity and
// scale multiply, translation, shear and angle add - and the frame's poses for each host are handed to it in one call
// when the result is applied. A part no track wrote this frame is at rest, so a host has nothing to remember between
// frames and nothing to put back but "no poses".
//
// A part has no baseline: at rest it is simply where the host lays it out.
//
// A track can also be aimed at part of a text by what it says (see FWidgetAnimTextTarget): each place the words appear is
// then a Span part - a run of letters moved as one piece, about its own middle - or, on the text tracks that change letters
// rather than move them, the letters those tracks are limited to.

/**
 * Which part of a part host: its unit - see WidgetAnimPartUnits - and its place among the parts of that unit. A Span is a run
 * of letters: Index its first letter, Count how many.
 */
struct NAMETBD_API FWidgetAnimPartKey
{
	FName Unit;
	int32 Index = INDEX_NONE;
	int32 Count = 1;

	FWidgetAnimPartKey() = default;
	FWidgetAnimPartKey(FName InUnit, int32 InIndex, int32 InCount = 1) : Unit(InUnit), Index(InIndex), Count(InCount) {}

	/** False for "the whole widget", which is what every track that isn't aimed at a part carries. */
	bool IsSet() const { return Index != INDEX_NONE; }

	bool operator==(const FWidgetAnimPartKey& Other) const { return Index == Other.Index && Count == Other.Count && Unit == Other.Unit; }
	bool operator!=(const FWidgetAnimPartKey& Other) const { return !(*this == Other); }
};

/**
 * One part's pose for this frame: the same five values a widget's render transform and opacity hold, about the part's
 * own middle.
 */
struct NAMETBD_API FWidgetAnimPartPose
{
	FWidgetAnimPartKey Part;

	float Opacity = 1.f;
	FVector2D Scale = FVector2D(1.0, 1.0);
	FVector2D Translation = FVector2D::ZeroVector;
	FVector2D Shear = FVector2D::ZeroVector;
	float Angle = 0.f;

	bool IsIdentity() const
	{
		return Opacity == 1.f && Scale == FVector2D(1.0, 1.0) && Translation.IsZero() && Shear.IsZero() && Angle == 0.f;
	}
};

/** The units the first-party part hosts know. A host of another module may name units of its own. */
namespace WidgetAnimPartUnits
{
	/** One visible character: a grapheme cluster that isn't white space. */
	NAMETBD_API extern const FName Letter;

	/** A run of letters with no white space between them - "Hello," is one word. */
	NAMETBD_API extern const FName Word;

	/** One line of the text as it is laid out: wrapping decides where one ends. */
	NAMETBD_API extern const FName Line;

	/**
	 * A run of letters - FWidgetAnimPartKey::Index the first, Count how many - posed as one piece about its own middle: where a
	 * track's Part of the Text appears (see FWidgetAnimTextTarget). Never counted; named only by a track that found it.
	 */
	NAMETBD_API extern const FName Span;
}

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UWidgetAnimPartHost : public UInterface
{
	GENERATED_BODY()
};

/**
 * A widget with parts tracks can move one by one. Implemented by UWidgetAnimTextBlock (letters, words and lines). C++
 * only: poses arrive every frame a part moves, far too often to run Blueprint.
 */
class NAMETBD_API IWidgetAnimPartHost
{
	GENERATED_BODY()

public:

	/** How many parts of this unit the widget has right now. 0 for a unit it doesn't know. */
	virtual int32 GetWidgetAnimPartCount(FName Unit) const = 0;

	/**
	 * Whether the count above can be trusted yet. False only while it depends on something not known yet - the lines of
	 * a wrapping text before it has ever been given a width. A stagger waits a few frames for it before fanning out.
	 */
	virtual bool AreWidgetAnimPartsReady(FName Unit) const { return true; }

	/**
	 * Every part's pose this frame, replacing the last frame's: a part not listed is at rest. Several poses may name one
	 * part, and then compose; a letter inside a posed word is posed by both.
	 */
	virtual void SetWidgetAnimPartPoses(TConstArrayView<FWidgetAnimPartPose> Poses) = 0;

	/** Every part back at rest - what putting the widget's baseline back means for its parts. */
	virtual void ClearWidgetAnimPartPoses() = 0;
};

/**
 * Letters and words of a string, the way every part host and text type in the plugin counts them - so a Text Reveal of
 * "5 letters" and an Each Letter stagger agree on what a letter is.
 */
namespace WidgetAnimTextParts
{
	/** Where one part is in its string: [Begin, End) in characters. */
	struct FRange
	{
		int32 Begin = 0;
		int32 End = 0;
	};

	/** Every letter of Text, in order: grapheme clusters - an accented letter, an emoji - that aren't white space. */
	NAMETBD_API void GetLetters(const FString& Text, TArray<FRange>& OutLetters);

	/**
	 * Every word of Text, given its letters: runs of letters with nothing between them. OutWordOfLetter, when given, says
	 * which word each letter is in.
	 */
	NAMETBD_API void GetWords(const TArray<FRange>& Letters, TArray<FRange>& OutWords, TArray<int32>* OutWordOfLetter = nullptr);

	/** Whether Letter is in any of Ranges - each a first letter (X) and how many (Y). Empty ranges cover every letter. */
	inline bool Covers(const TArray<FIntPoint>& Ranges, int32 Letter)
	{
		if (Ranges.Num() == 0)
		{
			return true;
		}
		for (const FIntPoint& Range : Ranges)
		{
			if (Letter >= Range.X && Letter < Range.X + Range.Y)
			{
				return true;
			}
		}
		return false;
	}
}

/** Which of the places a track's Part of the Text appears it animates, when the text has it more than once. */
UENUM(BlueprintType)
enum class EWidgetAnimTextOccurrence : uint8
{
	First UMETA(DisplayName = "The First", ToolTip = "Only where it first appears in the text."),
	Every UMETA(DisplayName = "Every One", ToolTip = "Everywhere it appears - each place moved as a piece of its own, about its own middle, all at once."),
	Number UMETA(DisplayName = "One By Number", ToolTip = "Only the one Which One picks - 2 is the second place it appears."),
	Last UMETA(DisplayName = "The Last", ToolTip = "Only where it last appears in the text.")
};

/**
 * Part of a text, picked by what it says: the words or letters a track on an animated text widget animates - "World" in
 * "Hello World" - leaving the rest of the text as it is. For text that is written into the widget (or set at runtime from
 * the same words): it is found again every time the track starts, in the text as the widget draws it.
 *
 * Tracks that move - Fade, Scale, Slide, Rotate, Pulse, Shake, Transform - move each place it appears as one piece, about
 * its own middle; with a Stagger of Each Letter or Each Word, just its letters or words take their turns. Text Color, Shimmer,
 * Wave and the text Shake change only its letters. Any other track type plays on the whole text as before. Not found, a track
 * plays on nothing - so a typo shows at once rather than animating the whole text.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimTextTarget
{
	GENERATED_BODY()

	/**
	 * The words or letters to animate, typed as the text shows them - "World", "50%", "Press A". Style tags of a rich text are
	 * not part of what it shows. Empty: the whole text.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part of the Text", meta = (DisplayName = "Text"))
	FString Text;

	/** Only where it appears with the same capitals: "world" no longer finds "World". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part of the Text", meta = (DisplayName = "Match Case"))
	bool bMatchCase = false;

	/** Only where it is a whole word, or whole words: "art" no longer finds the letters inside "start". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part of the Text", meta = (DisplayName = "Whole Words Only"))
	bool bWholeWords = false;

	/** Which place it animates when the text has it more than once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part of the Text", meta = (DisplayName = "Which"))
	EWidgetAnimTextOccurrence Occurrence = EWidgetAnimTextOccurrence::First;

	/** Which place it animates, counting from 1: 2 is the second place it appears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Part of the Text", meta = (DisplayName = "Which One", ClampMin = "1", EditCondition = "Occurrence == EWidgetAnimTextOccurrence::Number", EditConditionHides))
	int32 Number = 1;

	/** Whether anything is typed - only then is the track limited to part of its text. */
	bool IsSet() const { return !Text.IsEmpty(); }

	/**
	 * Where it is in Display - a text as drawn - counted in letters as WidgetAnimTextParts counts them: each place a first
	 * letter (X) and how many (Y), in order. Empty when it isn't there.
	 */
	void FindLetterRanges(const FString& Display, TArray<FIntPoint>& OutRanges) const;

	bool operator==(const FWidgetAnimTextTarget& Other) const
	{
		return Text == Other.Text && bMatchCase == Other.bMatchCase && bWholeWords == Other.bWholeWords && Occurrence == Other.Occurrence && Number == Other.Number;
	}
};
