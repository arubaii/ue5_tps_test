// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class FProperty;
class UFunction;
class UStruct;

/**
 * A dotted path into an object, such as "HealthComponent.Health" or "Stats.Ammo.Current".
 *
 * Each segment is looked up on whatever the previous one produced: a property (object properties are stepped
 * into, struct properties are looked inside), a getter function (no inputs and one output: a C++ return
 * value or a Blueprint function's single output pin, such as a Blueprint Pure "Get Health Percent"), or, on
 * an actor, a component with that name. The last segment is the value itself.
 *
 * The lookups are cached per segment, keyed by the class or struct they were made on, so reading the same
 * path every frame costs a pointer compare per segment rather than a search through the class's properties.
 * A different class at some segment (another pawn type, a recompiled Blueprint) simply looks it up again.
 */
struct NAMETBDMVVM_API FWidgetDataPath
{
	FWidgetDataPath() = default;
	explicit FWidgetDataPath(const FString& InPath) { Set(InPath); }

	void Set(const FString& InPath);
	void Reset() { Set(FString()); }

	bool IsEmpty() const { return Segments.IsEmpty(); }
	const FString& ToString() const { return PathText; }
	const TArray<FName>& GetSegments() const { return Segments; }
	FName GetLastSegment() const { return Segments.IsEmpty() ? NAME_None : Segments.Last(); }

private:

	friend struct FWidgetDataValue;

	struct FSegmentCache
	{
		TWeakObjectPtr<const UStruct> Scope;
		FProperty* Property = nullptr;
		TWeakObjectPtr<UFunction> Getter;
	};

	FString PathText;
	TArray<FName> Segments;
	mutable TArray<FSegmentCache> Cache;
};

/**
 * What a path resolved to: the property describing the value and a pointer to it.
 *
 * When the path goes through a getter function, or a property that is read through a native getter, the
 * value lives in a buffer this struct owns, so the pointer is good for as long as the struct is. Reading a
 * path that is only properties allocates nothing.
 */
struct NAMETBDMVVM_API FWidgetDataValue
{
	FWidgetDataValue() = default;
	~FWidgetDataValue() { Release(); }

	FWidgetDataValue(const FWidgetDataValue&) = delete;
	FWidgetDataValue& operator=(const FWidgetDataValue&) = delete;

	/** The property that describes the value. */
	const FProperty* Property = nullptr;

	/** The value. Points into the object, or into a buffer owned by this struct. */
	void* ValuePtr = nullptr;

	/**
	 * The object the last segment was found on. Null when the last segment was inside a struct. A Field Notify
	 * subscription and an event binding both need this object rather than the value.
	 */
	UObject* OwnerObject = nullptr;

	/** True when ValuePtr points at the real storage rather than a copy, which is what a delegate binding needs. */
	bool bDirectStorage = false;

	bool IsValid() const { return Property != nullptr && ValuePtr != nullptr; }

	/**
	 * Walks Path from Root. Returns false, with a description in OutError, when a segment can't be found, an
	 * object on the way is empty, or a segment asks to look inside a value that has nothing inside it.
	 * With bCallGetters off, getter functions count as not found; used when the storage itself is wanted.
	 */
	static bool Resolve(UObject* Root, const FWidgetDataPath& Path, FWidgetDataValue& Out, FString* OutError = nullptr, bool bCallGetters = true);

private:

	struct FBuffer
	{
		const UFunction* Function = nullptr;
		const FProperty* CopiedProperty = nullptr;
		uint8* Memory = nullptr;
	};

	TArray<FBuffer, TInlineAllocator<2>> Buffers;

	void Release();
};

/** One value of a property's type, initialised and destroyed properly. Used as a conversion target. */
struct NAMETBDMVVM_API FWidgetDataScratch
{
	explicit FWidgetDataScratch(const FProperty* InProperty);
	~FWidgetDataScratch();

	FWidgetDataScratch(const FWidgetDataScratch&) = delete;
	FWidgetDataScratch& operator=(const FWidgetDataScratch&) = delete;

	void* Get() const { return Memory; }
	const FProperty* GetProperty() const { return ScratchProperty; }

private:
	const FProperty* ScratchProperty = nullptr;
	void* Memory = nullptr;
};

/** Reading, converting and writing reflected values for the data binder. */
namespace WidgetDataValues
{
	/** Bool, any integer or float, and enums. The kinds a number can be read from and written to. */
	NAMETBDMVVM_API bool IsNumeric(const FProperty* Property);

	/** Reads Value as a number. Bool is 0 or 1, an enum is its underlying value. False for anything else. */
	NAMETBDMVVM_API bool ReadNumber(const FProperty* Property, const void* Value, double& OutNumber);

	/** Writes Number into Value. Integers and enums round half away from zero; bool is "not zero". */
	NAMETBDMVVM_API bool WriteNumber(const FProperty* Property, void* Value, double Number);

	/**
	 * Whether Value counts as "true": a set bool, a non-zero number or enum, a valid object, a non-empty
	 * string, text or name. False for kinds with no natural answer, such as structs.
	 */
	NAMETBDMVVM_API bool IsTruthy(const FProperty* Property, const void* Value);

	/** Whether Convert can turn a From value into a To value. */
	NAMETBDMVVM_API bool CanConvert(const FProperty* From, const FProperty* To);

	/** Copies when the types match, converts numbers into numbers and anything with a truth value into a bool. */
	NAMETBDMVVM_API bool Convert(const FProperty* From, const void* FromValue, const FProperty* To, void* ToValue);

	/**
	 * Writes NewValue into Field on Target and broadcasts the Field Notify change, but only when the value is
	 * actually different; an unchanged value is neither written nor broadcast. Uses the property's native
	 * setter and getter where it has them. Returns true when something was written.
	 */
	NAMETBDMVVM_API bool WriteField(UObject* Target, const FProperty* Field, const void* NewValue);

	/** Broadcasts FieldName on Target if Target is Field Notify and has a field with that name. */
	NAMETBDMVVM_API void BroadcastField(UObject* Target, FName FieldName);

	/**
	 * The one output of a function that takes no inputs: a C++ return value, or a Blueprint function's single
	 * output pin. Null for anything else. This is what a path segment can call.
	 */
	NAMETBDMVVM_API const FProperty* GetGetterOutput(const UFunction* Function);

	/** Whether Function is callable by a path segment (see GetGetterOutput). */
	NAMETBDMVVM_API bool IsGetterFunction(const UFunction* Function);
}