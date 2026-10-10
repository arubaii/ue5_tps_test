// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/**
 * JSON in and out of Unreal properties - how every tool reads and writes widgets, slots, tracks, assets and components.
 *
 * Writing takes what a model naturally writes and puts it where it belongs:
 *   - names matched loosely: case, spaces, underscores and a bool's "b" don't matter, display names work, and a name that is
 *     the start of exactly one field picks it ("Typeface" -> TypefaceFontName);
 *   - keys can be paths: "Fill.Color", "Stroke.Paint.Color", "ValueKeys.Keys[1].Value"; "Name+" appends to a list;
 *   - colors as "#RRGGBB" / "#RRGGBBAA" (sRGB, as the color picker shows them), [r,g,b,a] (linear) or a few names;
 *     vectors as [x, y]; margins as a number, [h, v] or [l, t, r, b]; enums by name or display name; assets by path;
 *     classes by name ("TextBlock", "RectangleShape") or path; an instanced struct as {"$type": "Name", ...}; an instanced
 *     object as {"$class": "Name", ...}; a property bag by its fields' names; anything else as Unreal's own text format.
 * Only the keys given are written - everything else keeps its value.
 *
 * Reading gives only what differs from the defaults, as compact JSON: what a model needs, nothing it doesn't.
 */
namespace NameTBDAIJson
{
	/** Lowercase, without spaces, underscores, dashes and dots. */
	FString Normalize(const FString& Name);

	/** A field of Struct by a loosely written name - see above. Null, with OutError listing what there is, if none. */
	FProperty* FindField(const UStruct* Struct, const FString& Name, FString* OutError = nullptr);

	/** Writes Value into one property's memory. Owner: the object it belongs to (outer for new instanced objects). */
	bool WriteValue(FProperty* Property, void* Address, const TSharedPtr<FJsonValue>& Value, UObject* Owner, FString& OutError);

	/**
	 * Writes every key of Object (paths allowed) into Container, an instance of Struct. Errors are collected per key, and the
	 * other keys still written. OutTopFields: the struct's own fields that were written, for change notifications.
	 */
	void WriteObject(const UStruct* Struct, void* Container, const TSharedPtr<FJsonObject>& Object, UObject* Owner,
		TArray<FString>& OutErrors, TArray<FProperty*>* OutTopFields = nullptr);

	/**
	 * Writes Props onto an object as the Details panel would: Modify, PreEditChange and PostEditChangeProperty for each of its
	 * own fields written - so a widget lays itself out again, a ready-made button restyles, a Niagara widget lays out its User
	 * Parameters. True when every key took.
	 */
	bool ApplyToObject(UObject* Object, const TSharedPtr<FJsonObject>& Props, TArray<FString>& OutErrors);

	/** A value as JSON: numbers, bools, strings, colors as hex, vectors as arrays, structs as their non-default fields. */
	TSharedPtr<FJsonValue> ReadValue(const FProperty* Property, const void* Address, int32 Depth = 3);

	/**
	 * The fields of Container that differ from Defaults (null: the struct's own defaults), editable ones only. MaxFields keeps
	 * it short; Depth how far into structs and lists it goes.
	 */
	TSharedPtr<FJsonObject> ReadNonDefault(const UStruct* Struct, const void* Container, const void* Defaults, int32 Depth = 3, int32 MaxFields = 60);

	/** An object's editable fields that differ from its archetype (for a widget in a Widget Blueprint: its class's defaults). */
	TSharedPtr<FJsonObject> ReadObjectNonDefault(const UObject* Object, int32 Depth = 3);

	/** One field by path ("Fill.Color", "Font.Size") as JSON, or null with OutError. */
	TSharedPtr<FJsonValue> ReadPath(const UStruct* Struct, const void* Container, const FString& Path, FString& OutError);

	bool ParseColor(const TSharedPtr<FJsonValue>& Value, FLinearColor& OutColor);
	FString ColorToHex(const FLinearColor& Color);

	/** A class by short name ("TextBlock", "UTextBlock", "Rectangle Shape"), path, or Blueprint asset path; must be a Base. */
	UClass* FindClass(const FString& Name, const UClass* Base, FString* OutError = nullptr);

	// ---- JSON text --------------------------------------------------------------------------------------------------

	FString ToString(const TSharedPtr<FJsonObject>& Object, bool bPretty = false);
	FString ToString(const TSharedPtr<FJsonValue>& Value);
	TSharedPtr<FJsonObject> ParseObject(const FString& Text);
	TSharedPtr<FJsonValue> ParseValue(const FString& Text);

	// ---- Reading tool arguments -------------------------------------------------------------------------------------

	FString GetString(const TSharedPtr<FJsonObject>& Args, const FString& Key, const FString& Default = FString());
	double GetNumber(const TSharedPtr<FJsonObject>& Args, const FString& Key, double Default = 0.0);
	bool GetBool(const TSharedPtr<FJsonObject>& Args, const FString& Key, bool bDefault = false);
	TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Args, const FString& Key);
	TArray<TSharedPtr<FJsonValue>> GetArray(const TSharedPtr<FJsonObject>& Args, const FString& Key);
	TArray<FString> GetStringArray(const TSharedPtr<FJsonObject>& Args, const FString& Key);
	bool Has(const TSharedPtr<FJsonObject>& Args, const FString& Key);

	/** Making values. */
	TSharedPtr<FJsonValue> Str(const FString& Value);
	TSharedPtr<FJsonValue> Num(double Value);
	TSharedPtr<FJsonValue> Bool(bool bValue);
	TSharedPtr<FJsonValue> Arr(const TArray<FString>& Values);
	TSharedPtr<FJsonValue> Obj(const TSharedPtr<FJsonObject>& Object);

	/** A JSON list holding one object. */
	TArray<TSharedPtr<FJsonValue>> ListOf(const TSharedPtr<FJsonObject>& Object);
}
