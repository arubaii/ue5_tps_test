// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FFieldClass;
class FProperty;
class UEnum;
struct FWidgetAnimLayer;

/**
 * A track's keys as their values - the numbers each key holds, one "channel" per number: a Transform key's Translation X,
 * Translation Y, Scale X... a Fade's Opacity, a Scale track's Scale X and Y, a Shape Box or Shape key's every number (a corner's
 * Size X, a fill color's R) - and its choices and on/offs, held as numbers (a corner's Style, a shape's Fill). What the
 * Selection panel's Key Values and Keys list (SWidgetAnimKeyValues) show and edit, a key's channels at a time, and only those
 * the key alters.
 *
 * Keys are counted as the Track Editor draws them: on a track that travels from a start value to an end value, key 0 is its
 * start value, the last key its end value and every key between one of its Value Keys; on a keyframed track (Transform, Shape
 * Box, Shape, Design), its keys in order.
 *
 * A channel is ALTERED at a key when its value there differs from the key before or the key after - the key is where it
 * changes, or where a change starts. A channel the whole track holds still is altered nowhere, and isn't shown.
 */
namespace WidgetAnimKeyValues
{
	/** Where a channel's number lives on a key. */
	enum class ESource : uint8
	{
		/** A start-to-end track's point: its start or end value, or a Value Key's. See EPointField. */
		Point,

		/** A Transform track's key's Transform - see TransformField. */
		Transform,

		/** A Shape Box or Shape / Design track's key's settings, down through their structs to a number - see RootName / Tail. */
		Field
	};

	/** Which number of a point: its value, a Scale's Y, or a color's channel. */
	enum class EPointField : uint8
	{
		Value,
		ValueY,
		ColorR,
		ColorG,
		ColorB,
		ColorA
	};

	/** One number every key of a track holds. Made by GetChannels; stays good while the track's type and its keys' layout do. */
	struct FChannel
	{
		FText Label;
		ESource Source = ESource::Point;
		EPointField PointField = EPointField::Value;

		/** Transform: 0 Translation X, 1 Translation Y, 2 Scale X, 3 Scale Y, 4 Shear X, 5 Shear Y, 6 Angle. */
		int32 TransformField = 0;

		/**
		 * Field: the setting (a property of FShapeBoxShape, or a Shape / Design key's bag, by name) and the members down through
		 * its structs to the number. RootProperty is only trusted for a Shape Box's (a fixed struct); a bag's is looked up again.
		 */
		FName RootName;
		const FProperty* RootProperty = nullptr;
		FFieldClass* RootClass = nullptr;
		TArray<const FProperty*> Tail;

		/**
		 * A choice from a list - a Shape Box corner's Bevel / Chamfer, a shape's Fit: its list. Shown as a drop-down of the
		 * entries' names, read and written as the entry's value. Null for a number.
		 */
		const UEnum* Enum = nullptr;

		/** On or off - a shape's Fill: shown as a check box, read and written as 1 or 0. */
		bool bBool = false;

		/** Whether the number is whole - shown and dragged in whole steps. A choice's and an on/off's are. */
		bool bInteger = false;

		/**
		 * The range its box's drag bar spans, when it has a natural one - an opacity's or a color's 0 to 1, a setting's own UIMin /
		 * UIMax (Perspective 0 to 1, Angle -45 to 45). Unset: the box drags freely with no bar (an entry box's own default was a
		 * 0 to 100 bar, meaningless for a scale of 1 or a translation of 400).
		 */
		TOptional<double> SliderMin;
		TOptional<double> SliderMax;

		/**
		 * The setting it is one number of, when that holds several - a Shape Box's Corners, Edge Bow - with the name the box is
		 * listed under inside it ("Top Left Size X"). A key's list folds a setting of several numbers under its name. Empty for a
		 * number that is a setting of its own.
		 */
		FText Group;
		FText ShortLabel;

		/** What it does, in a line - NameTBD::Tooltips::WidgetAnimEditor::KeyValueDescription, else its setting's own tooltip. */
		FText Description;
	};

	/** Every channel of a track, in the order the Selection panel lists them. None for a track whose keys hold no number. */
	void GetChannels(const FWidgetAnimLayer& Layer, TArray<FChannel>& OutChannels);

	/** How many keys the track has as the Track Editor counts them: points on a start-to-end track, keys on a keyframed one. */
	int32 GetNumKeys(const FWidgetAnimLayer& Layer);

	/** The channel's number at a key - unset where the key doesn't hold it (a Shape key using the authored shape). */
	TOptional<double> Read(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel);

	/**
	 * Whether the channel's number at a key can be set here, and if not why: a Slide's end (always 0), a registered type's start
	 * and end (its settings, under Track Values), a Shape key using the authored shape (turn Use Authored off first).
	 */
	bool CanWrite(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel, FText* OutWhyNot = nullptr);

	/**
	 * Sets the channel's number at a key - rounded for a whole number. A Transform key using the authored transform stops using
	 * it, at the pose it stood for (identity), and takes the number. False when it can't be set (see CanWrite).
	 */
	bool Write(FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel, double Value);

	/**
	 * The channel's number at a key as a path into its track - what an Animation Parameter is bound to (see
	 * FWidgetAnimParameterBinding::FieldPath): "ValueKeys.Keys[1].Value", "StartOpacity", "Transform.Keys[2].Transform.Scale.X",
	 * "Transform.Keys[0].Shape.Corners.TopLeft.Style", "TypeParams.StartValue". False where the number isn't a field of its own
	 * (a Slide's end, a registered type's fixed start or end).
	 */
	bool GetFieldPath(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel, FString& OutPath);

	/** Whether the channel is altered at a key - see the note at the top. */
	bool IsAltered(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel);

	/** Whether a key stands for the widget as authored - a keyframed key's Use Authored Transform / Shape. */
	bool IsKeyAuthored(const FWidgetAnimLayer& Layer, int32 Key);

	/**
	 * A value typed into a key value box, worked out against the number it had (Current) - for one box, or for each of several
	 * selected at once, each with its own number:
	 *   "12", "2 * (3 + 1)", "-5"   that number, the same for every box;
	 *   "+8", "*2", "/4"            relative: each box's own number plus 8, times 2, divided by 4;
	 *   "+= 8", "-= 8", "*= 2"      relative, as above - "-= 8" takes 8 off (a plain "-8" is minus eight);
	 *   "x * 2 + 1"                 x (or v) is each box's own number.
	 * Numbers, + - * / %, ^ (power), parentheses and unary minus. Unset for anything it can't read, or a division by zero.
	 */
	TOptional<double> Evaluate(const FString& Expression, double Current);
}
