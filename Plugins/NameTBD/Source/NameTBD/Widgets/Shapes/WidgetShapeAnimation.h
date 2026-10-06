// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"

class UShapeWidget;

/**
 * A shape widget's settings as a Shape track keys them: held per key in a property bag laid out for one kind of shape (an
 * Ellipse's, a Star's...), blended between keys by reflection, and written back onto a shape by name - so a key made for a
 * Star still moves the fill, stroke and trim of an Ellipse it is played on, and a setting added to a shape later animates
 * with nothing changed here.
 *
 * Blending, field by field: floats and doubles glide; whole numbers glide and round; structs blend member by member (a
 * color, a gradient's angle, a stroke's width); arrays of the same length blend element by element - so a Custom Shape's
 * path morphs vertex by vertex, and a gradient's stops slide - and anything else, an array whose length changes included,
 * switches halfway. A brush switches halfway whole, as its image can't be blended.
 */
namespace WidgetShapeAnimation
{
	/** The settings a Shape track animates on a shape of ShapeClass: every Details setting it has but Desired Size. */
	NAMETBD_API void GetProperties(const UClass* ShapeClass, TArray<const FProperty*>& OutProperties);

	/** Whether Bag holds exactly the settings of Kind, in its layout. */
	NAMETBD_API bool IsLaidOutFor(const FInstancedPropertyBag& Bag, const UClass* Kind);

	/**
	 * Lays Bag out for Kind: a value it already holds that Kind also has is kept, the rest is taken from Source - a shape,
	 * its current settings - or Kind's defaults. A null Kind empties it. Returns true if the layout changed.
	 */
	NAMETBD_API bool LayOut(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source);

	/**
	 * LayOut, for any list of settings - a widget's look (see WidgetDesignAnimation.h) as well as a shape's: Bag laid out for
	 * Properties, of Kind, values kept where it already held them and taken from Source - an object of Kind - or Kind's defaults.
	 * IsLaidOutWith is IsLaidOutFor's half of it.
	 */
	NAMETBD_API bool LayOutWith(FInstancedPropertyBag& Bag, const UClass* Kind, const UObject* Source, const TArray<const FProperty*>& Properties);
	NAMETBD_API bool IsLaidOutWith(const FInstancedPropertyBag& Bag, const TArray<const FProperty*>& Properties);

	/** Bag's every value, read off Source by name. Values Source doesn't have are left as they are. */
	NAMETBD_API void Capture(FInstancedPropertyBag& Bag, const UObject* Source);

	/**
	 * Blends one value of Property from A to B by Alpha into Out - three values of Property's type, Out already holding
	 * a copy of A. Alpha past 0 or 1 carries on past the ends where the value can.
	 */
	NAMETBD_API void LerpValue(const FProperty* Property, const void* A, const void* B, void* Out, float Alpha);

	/** A blend of two keys' settings, for a key dropped between them. Bags of different layouts switch halfway. */
	NAMETBD_API FInstancedPropertyBag LerpBags(const FInstancedPropertyBag& A, const FInstancedPropertyBag& B, float Alpha);

	/** Where Name's value is in Bag, read as Property's type - null when Bag has no such value or can't hold it as one. */
	NAMETBD_API const void* FindValue(const FInstancedPropertyBag& Bag, const FProperty* Property);

	/**
	 * Writes the blend from A to B (each a value of Property, or null where a key has none - then the other's is used
	 * alone) onto Shape's Property. Returns true if the shape changed. Nothing is written when both are null.
	 */
	NAMETBD_API bool WriteBlend(UObject* Shape, const FProperty* Property, const void* A, const void* B, float Alpha);
}
