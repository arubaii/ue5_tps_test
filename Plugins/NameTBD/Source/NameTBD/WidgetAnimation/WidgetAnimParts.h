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

/** Which part of a part host: its unit - see WidgetAnimPartUnits - and its place among the parts of that unit. */
struct NAMETBD_API FWidgetAnimPartKey
{
	FName Unit;
	int32 Index = INDEX_NONE;

	FWidgetAnimPartKey() = default;
	FWidgetAnimPartKey(FName InUnit, int32 InIndex) : Unit(InUnit), Index(InIndex) {}

	/** False for "the whole widget", which is what every track that isn't aimed at a part carries. */
	bool IsSet() const { return Index != INDEX_NONE; }

	bool operator==(const FWidgetAnimPartKey& Other) const { return Index == Other.Index && Unit == Other.Unit; }
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
}
