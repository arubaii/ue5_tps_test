// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "WidgetAnimParameters.generated.h"

class FProperty;
class UObject;
class UScriptStruct;
class UWidgetAnimPresetAsset;
struct FWidgetAnimLayer;
struct FWidgetAnimEntryBase;

// =====================================================================================================
// Animation Parameters
//
// Named values declared on a Widget Animation (UWidgetAnimPresetAsset::Parameters) that any field of its tracks can be bound
// to in place of a constant: a slide's distance, a fade's end opacity, a key's value, a material color, a duration, a delay, a
// slide's direction (a Choice whose options are the direction's names). One animation, many results - as CSS custom
// properties used inside keyframes. A track can also play only for some options of a Choice (Plays When).
//
// Bindings are a sparse side table on each track (FWidgetAnimLayer::ParameterBindings): a field path - "SlideDistance",
// "ValueKeys.Keys[1].Value", "Transform.Keys[2].Transform.Translation" - and the parameter's name, with an optional Scale and
// Offset ("Distance x -1" for the mirrored side). The fields keep their own types and their constants: an unbound field, or
// one bound to a parameter that is gone, plays its constant.
//
// Resolved once per play, on the copy of the tracks that plays (FWidgetAnimEntryBase::BuildSettings): never on the asset, so
// two widgets playing one animation with different values never interfere, and an animation with no bindings costs nothing.
//
// Where a value comes from, highest first:
//   1. Play time - Play Widget Animation With Parameters, Set Widget Animation Parameter (FWidgetAnimParameterScope);
//   2. the use of the animation - a Widget Animation Component entry's Parameters (FWidgetAnimEntryBase::Parameters), or a
//      state machine state's bindings to its inputs, which the state sets there;
//   3. the Widget Animation Component's own Parameters (UWidgetAnimationComponent::ComponentParameters) - for every animation
//      it plays that has the parameter (a scope pushed "under the use", see FWidgetAnimParameterScope::bUnderUse);
//   4. the parameter's default.
// An animation inserted into another as a track takes the values of the one it plays in by name, where they are the same type.
// =====================================================================================================

/** What a parameter holds. */
UENUM(BlueprintType)
enum class EWidgetAnimParameterType : uint8
{
	Float UMETA(ToolTip = "A number: a distance, an opacity, an angle, a duration."),
	Integer UMETA(ToolTip = "A whole number."),
	Bool UMETA(ToolTip = "True or false - also what a track's Plays When can test."),
	Color UMETA(ToolTip = "A color: a material vector, a tint."),
	Vector2D UMETA(DisplayName = "Vector 2D", ToolTip = "Two numbers: a scale, a translation, an offset."),
	Choice UMETA(ToolTip = "One of named options - Left / Right. Bound to a dropdown field (a slide's direction) it picks the option of that name; a track's Plays When can play it only for some options.")
};

/** One parameter an animation declares: its name, type, default, and what its editors offer. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimParameter
{
	GENERATED_BODY()

	/** What it is called - what bindings, Blueprint and the state machine name it by. Unique within its animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter")
	EWidgetAnimParameterType Type = EWidgetAnimParameterType::Float;

	/** What it is for, in words: its tooltip wherever it is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (MultiLine = true))
	FString Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Float", EditConditionHides))
	float DefaultFloat = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Integer", EditConditionHides))
	int32 DefaultInteger = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Bool", EditConditionHides))
	bool bDefaultBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Color", EditConditionHides))
	FLinearColor DefaultColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Vector2D", EditConditionHides))
	FVector2D DefaultVector2D = FVector2D::ZeroVector;

	/** A Choice's options, by name. Bound to a dropdown field, name them as the field's options are named (Up, Down, Left, Right). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (EditCondition = "Type == EWidgetAnimParameterType::Choice", EditConditionHides))
	TArray<FName> Options;

	/** A Choice's default option - its first when this isn't one of them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Default", EditCondition = "Type == EWidgetAnimParameterType::Choice", EditConditionHides))
	FName DefaultOption;

	/** A Float or Integer's range in the editors that set it - a slider between Min and Max. Values outside it still play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (DisplayName = "Use Range", EditCondition = "Type == EWidgetAnimParameterType::Float || Type == EWidgetAnimParameterType::Integer", EditConditionHides))
	bool bUseRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (EditCondition = "bUseRange", EditConditionHides))
	float Min = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parameter", meta = (EditCondition = "bUseRange", EditConditionHides))
	float Max = 1.f;

	/** The default Choice option, held to the options there are. None when it has none. */
	FName GetDefaultOption() const;
};

/**
 * A value for one parameter: what a use of an animation, a Play call or a state hands it. Only the field of its Type is read.
 * Made in Blueprint with the Make Animation Parameter nodes (UWidgetAnimParameterLibrary).
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimParameterValue
{
	GENERATED_BODY()

	/** The parameter it sets, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	EWidgetAnimParameterType Type = EWidgetAnimParameterType::Float;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	float Float = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	int32 Integer = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	bool bBool = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FLinearColor Color = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter", meta = (DisplayName = "Vector 2D"))
	FVector2D Vector2D = FVector2D::ZeroVector;

	/** A Choice's option, by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameter")
	FName Option;

	/**
	 * On a Widget Animation Component entry's Parameters: whether this use sets it, rather than leaving the animation's default.
	 * Always true on a value made anywhere else.
	 */
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (DisplayName = "Set Here"))
	bool bOverride = true;

	/** The parameter's default, as a value. */
	static FWidgetAnimParameterValue FromDefault(const FWidgetAnimParameter& Parameter);

	/** The value as up to four numbers: Float, Integer and Bool in X, a Color's RGBA, a Vector 2D's XY. A Choice's is its option's index in Options. */
	FVector4 AsVector(const TArray<FName>* ChoiceOptions = nullptr) const;

	/** Sets the value from up to four numbers, as AsVector gives them; a Choice from the option at X's index in ChoiceOptions. */
	void SetFromVector(const FVector4& Vector, const TArray<FName>* ChoiceOptions = nullptr);

	/** Whether two values of the same parameter are the same value. */
	bool SameValueAs(const FWidgetAnimParameterValue& Other) const;

	/** The value in words, for the editors: "40", "(1, 0.5)", "Left". */
	FString ToDisplayString() const;
};

/** One field of a track bound to a parameter. See FWidgetAnimLayer::ParameterBindings. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimParameterBinding
{
	GENERATED_BODY()

	/** The field, as a path into the track: "SlideDistance", "ValueKeys.Keys[1].Value", "Transform.Keys[0].Transform.Angle". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
	FString FieldPath;

	/** The parameter it takes its value from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
	FName Parameter;

	/** Multiplies a number before it is written: -1 mirrors a distance or an angle. Ignored for a Bool or Choice. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
	float Scale = 1.f;

	/** Added to a number after Scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Binding")
	float Offset = 0.f;
};

/** A track that plays only for some values of a parameter. See FWidgetAnimLayer::PlaysWhen. */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimParameterCondition
{
	GENERATED_BODY()

	/** The Choice or Bool parameter tested. None: the track always plays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plays When")
	FName Parameter;

	/** A Choice: the options it plays for. A Bool: plays while true (or, with Options holding "False", while false). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Plays When")
	TArray<FName> Options;

	bool IsSet() const { return !Parameter.IsNone(); }
};

/**
 * Values handed to the animations started while it lives, on this thread - how a Play call, Set Widget Animation Parameter
 * or the editor's preview gets values to the tracks it starts without every host passing them through. Nested scopes stack:
 * the innermost wins. Animation or AnimId, when set, limit it to the uses of that animation, or with that Anim ID.
 */
struct NAMETBD_API FWidgetAnimParameterScope
{
	FWidgetAnimParameterScope(TConstArrayView<FWidgetAnimParameterValue> InValues, const UObject* InAnimation = nullptr, FName InAnimId = NAME_None, bool bInUnderUse = false);
	~FWidgetAnimParameterScope();

	FWidgetAnimParameterScope(const FWidgetAnimParameterScope&) = delete;
	FWidgetAnimParameterScope& operator=(const FWidgetAnimParameterScope&) = delete;

	TConstArrayView<FWidgetAnimParameterValue> Values;
	const UObject* Animation = nullptr;
	FName AnimId;

	/**
	 * Under the use: its values go in before a use's own (an entry's Parameters, a state's Parameters From Inputs), which win
	 * over them - a component's Parameters, for every animation it plays. Only its values ticked to be set (bOverride) count.
	 * Off (play-time values): over the use's own.
	 */
	bool bUnderUse = false;

	/** Whether it applies to a use of Animation with Anim ID UseAnimId. */
	bool AppliesTo(const UObject* UseAnimation, FName UseAnimId) const;

private:
	FWidgetAnimParameterScope* Outer = nullptr;
	friend struct FWidgetAnimParameterResolver;
};

/** Several scopes in effect together - pushed one after another, let go of in reverse when it goes, or on Reset. */
struct NAMETBD_API FWidgetAnimParameterScopeStack
{
	FWidgetAnimParameterScopeStack() = default;
	~FWidgetAnimParameterScopeStack() { Reset(); }

	FWidgetAnimParameterScopeStack(const FWidgetAnimParameterScopeStack&) = delete;
	FWidgetAnimParameterScopeStack& operator=(const FWidgetAnimParameterScopeStack&) = delete;

	/** Values must outlive the stack, unchanged. bUnderUse: see FWidgetAnimParameterScope::bUnderUse. */
	void Push(TConstArrayView<FWidgetAnimParameterValue> Values, const UObject* Animation = nullptr, FName AnimId = NAME_None, bool bUnderUse = false);

	void Reset();

private:
	TArray<TUniquePtr<FWidgetAnimParameterScope>, TInlineAllocator<2>> Scopes;
};

/**
 * Every parameter of one animation with the value it plays with, worked out from its declarations and wherever values come
 * from - and what applies them to a copy of its tracks.
 */
struct NAMETBD_API FWidgetAnimParameterResolver
{
	/** The animation's parameters, by name, with their values. */
	TMap<FName, FWidgetAnimParameterValue> Values;

	/** Their declarations, by name - for a Choice's options. */
	TMap<FName, const FWidgetAnimParameter*> Declarations;

	/**
	 * Works out the values for one use of Animation: its defaults, then Entry's own Parameters (when given), then the active
	 * scopes that apply to it, outermost first, then Outer (an animation this one is inserted into, by name and type). False
	 * when the animation declares no parameters - nothing to resolve.
	 */
	bool Resolve(const UWidgetAnimPresetAsset& Animation, const FWidgetAnimEntryBase* Entry, const FWidgetAnimParameterResolver* Outer = nullptr, FName UseAnimId = NAME_None);

	/** Puts a value in, where the animation has the parameter and the value is of its type. */
	void Set(const FWidgetAnimParameterValue& Value);

	/**
	 * Copies Layers into OutLayers with every binding applied and returns true - or false, leaving OutLayers alone, when no
	 * layer is bound or conditional (the caller plays the originals). A layer whose Plays When doesn't hold is flagged in
	 * OutMuted (its index), to be muted where it plays.
	 */
	bool Apply(const TArray<FWidgetAnimLayer>& Layers, TArray<FWidgetAnimLayer>& OutLayers, TSet<int32>& OutMuted) const;

	/** Whether Condition holds with these values. True when it is unset or names a parameter there isn't. */
	bool Holds(const struct FWidgetAnimParameterCondition& Condition) const;

	/** The resolver the tracks being built now take their values from - set while an animation's tracks are built. */
	static const FWidgetAnimParameterResolver* GetCurrent();

	/** Makes Resolver the current one while it lives. */
	struct NAMETBD_API FCurrentScope
	{
		explicit FCurrentScope(const FWidgetAnimParameterResolver* Resolver);
		~FCurrentScope();
	private:
		const FWidgetAnimParameterResolver* Previous = nullptr;
	};
};

namespace WidgetAnimParameters
{
	/**
	 * Writes Value into the field FieldPath names inside Memory (a Struct), through Scale and Offset. Paths step through
	 * structs, arrays ("Keys[2]"), instanced structs (a registered type's Type Params) and property bags (a Particles track's
	 * User Parameters). Numbers go into float, double and integer fields; a Bool into a bool; a Color into a Linear Color; a
	 * Vector 2D into a Vector 2D (or a Vector's X and Y); a Choice into an enum field (by the option's name), a name or a
	 * string. False when the path doesn't lead to a field the value can go into - the field then keeps its constant.
	 */
	NAMETBD_API bool WriteField(const UScriptStruct* Struct, void* Memory, const FString& FieldPath, const FWidgetAnimParameterValue& Value, float Scale = 1.f, float Offset = 0.f,
		const TArray<FName>* ChoiceOptions = nullptr);

	/**
	 * The field FieldPath names inside Struct - its property - or null. With Memory (a Struct), paths into instanced structs and
	 * property bags are followed too, and an array index must be one the array has.
	 */
	NAMETBD_API const FProperty* FindField(const UScriptStruct* Struct, const FString& FieldPath, const void* Memory = nullptr);

	/** Reads the field FieldPath names inside Memory (a Struct) into a value of the type InOutValue already has. False when it can't. */
	NAMETBD_API bool ReadField(const UScriptStruct* Struct, const void* Memory, const FString& FieldPath, FWidgetAnimParameterValue& InOutValue);

	/** The parameter type a field is best set by: a Bool for a bool, a Choice for a dropdown, a Color, a Vector 2D, an Integer, a Float. */
	NAMETBD_API bool GetNaturalType(const FProperty* Field, EWidgetAnimParameterType& OutType);

	/** Whether a parameter of Type can be bound to Field. */
	NAMETBD_API bool CanBind(const FProperty* Field, EWidgetAnimParameterType Type);

	/** Whether a name is free for a new parameter of Parameters (Ignore: the index of one being renamed). */
	NAMETBD_API bool IsNameFree(const TArray<FWidgetAnimParameter>& Parameters, FName Name, int32 Ignore = INDEX_NONE);

	/** Base, or Base numbered ("Distance 2") until it is free. */
	NAMETBD_API FName MakeUniqueName(const TArray<FWidgetAnimParameter>& Parameters, const FString& Base, int32 Ignore = INDEX_NONE);
}

/** Makes parameter values in Blueprint - for Play Widget Animation With Parameters and the other nodes that take them. */
UCLASS()
class NAMETBD_API UWidgetAnimParameterLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Float)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterFloat(FName Name, float Value);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Integer)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterInteger(FName Name, int32 Value);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Bool)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterBool(FName Name, bool bValue);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Color)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterColor(FName Name, FLinearColor Value);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Vector 2D)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterVector2D(FName Name, FVector2D Value);

	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters", meta = (DisplayName = "Make Animation Parameter (Choice)", Keywords = "widget animation parameter"))
	static FWidgetAnimParameterValue MakeAnimationParameterChoice(FName Name, FName Option);

	/** The names of the parameters Animation declares. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters")
	static TArray<FName> GetAnimationParameterNames(const UWidgetAnimPresetAsset* Animation);

	/** A parameter's default on Animation, as a value - false when it has none of that name. */
	UFUNCTION(BlueprintPure, Category = "Widget Animation|Parameters")
	static bool GetAnimationParameterDefault(const UWidgetAnimPresetAsset* Animation, FName Name, FWidgetAnimParameterValue& OutValue);
};
