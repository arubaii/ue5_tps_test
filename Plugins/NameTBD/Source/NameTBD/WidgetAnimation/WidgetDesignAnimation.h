// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"

class UWidget;

/**
 * A widget's look as a Design track keys it: the visual settings most widgets are designed with - an Image's brush and tint,
 * a Text Block's color, font and shadow, a Border's background, a Button's colors, a Progress Bar's fill - held per key in a
 * property bag laid out for one kind of widget, blended between keys by reflection, and written back onto the widget by
 * name. Built on the Shape track's bags (see WidgetShapeAnimation.h): numbers and colors glide - a brush's tint and size as
 * well as a font's size - and anything with nothing in between - a brush's image, a font face or material - cross-fades
 * into the next key's through transparency (see WriteBlends).
 *
 * Which settings: those of DesignPropertyNames that the widget's class has and shows in its Details. A key made for one kind
 * of widget still moves the settings it shares with another it is played on - a Text Block key's Color And Opacity on an
 * Image.
 */
namespace WidgetDesignAnimation
{
	/**
	 * The kind a Design track keys for a widget of WidgetClass: its nearest native class - an Image, a Text Block, the User
	 * Widget a Widget Blueprint makes - so an animation never holds on to the Widget Blueprint it was made against, and plays
	 * the same on any widget of that kind. Null for no class.
	 */
	NAMETBD_API UClass* GetDesignKind(const UClass* WidgetClass);

	/** The settings a Design track animates on a widget of WidgetClass, in the order they are listed. Empty for no class. */
	NAMETBD_API void GetProperties(const UClass* WidgetClass, TArray<const FProperty*>& OutProperties);

	/**
	 * Lays Bag out for Kind's settings - see WidgetShapeAnimation::LayOutWith: values it already holds that Kind shares are
	 * kept, the rest taken from Source, a widget of Kind, or Kind's defaults. A null Kind empties it. True if anything changed.
	 */
	NAMETBD_API bool LayOut(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source);

	/** True if Bag is laid out for Kind's settings, as LayOut leaves it. */
	NAMETBD_API bool IsLaidOutFor(const FInstancedPropertyBag& Bag, const UClass* Kind);

	/**
	 * Writes the blend from A to B of one setting onto Widget - through the setting's own setter where it has one (SetBrush,
	 * SetColorAndOpacity), which is what shows it at once. Returns true if the widget changed; bOutNeedsSync is set when a
	 * setting without a setter was written straight into the widget, which then has to be told with SynchronizeProperties.
	 */
	NAMETBD_API bool WriteBlend(UWidget* Widget, const FProperty* Property, const void* A, const void* B, float Alpha, bool& bOutNeedsSync);

	/**
	 * Writes the blend from From to To of every one of Properties onto Widget, Alpha of the way - each From and To a value of the
	 * property at the same place, or null for one a key doesn't hold. Numbers glide - a brush's tint and size, a font's size,
	 * every color - and what can't - a brush's image, a font's face or material - cross-fades through transparency: the brush
	 * by its own tint, a font by the colors its text is drawn in, out to nothing halfway and back in to the next key. Settings a
	 * style of the widget's owns (a Common Text Block's Text Style) are left to it. bOutNeedsSync is set when a setting
	 * without a setter was written straight into the widget - see WriteBlend.
	 */
	NAMETBD_API void WriteBlends(UWidget* Widget, const TArray<const FProperty*>& Properties, const TArray<const void*>& From, const TArray<const void*>& To, float Alpha, bool* bOutChanged, bool* bOutNeedsSync);
}
