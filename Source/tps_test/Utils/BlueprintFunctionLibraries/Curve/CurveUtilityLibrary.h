// CurveUtilityLibrary.h
// Small Blueprint-callable utility library for editing keys on curve assets
// at runtime/in-editor (e.g. shifting a key's time).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Curves/CurveFloat.h"
#include "CurveUtilityLibrary.generated.h"

UCLASS()
class TPS_TEST_API UCurveUtilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Moves an existing key on a float curve to a new time.
	 *
	 * @param Curve     The curve asset to modify.
	 * @param KeyIndex  Index of the key in the curve's key array (0-based).
	 *                  Note: moving a key's time can reorder the key array,
	 *                  so don't reuse a cached index across multiple calls
	 *                  without re-validating it.
	 * @param NewTime   The new time to move the key to.
	 */
	UFUNCTION(BlueprintCallable, Category = "Curve Utility")
	static void SetCurveKeyTime(UCurveFloat* Curve, int32 KeyIndex, float NewTime);

	/**
	 * Sets the value of an existing key on a float curve.
	 *
	 * @param Curve     The curve asset to modify.
	 * @param KeyIndex  Index of the key in the curve's key array (0-based).
	 * @param NewValue  The new value for the key.
	 */
	UFUNCTION(BlueprintCallable, Category = "Curve Utility")
	static void SetCurveKeyValue(UCurveFloat* Curve, int32 KeyIndex, float NewValue);

	/**
	 * Sets both the time and value of an existing key on a float curve in
	 * one call.
	 *
	 * @param Curve     The curve asset to modify.
	 * @param KeyIndex  Index of the key in the curve's key array (0-based).
	 * @param NewTime   The new time to move the key to.
	 * @param NewValue  The new value for the key.
	 */
	UFUNCTION(BlueprintCallable, Category = "Curve Utility")
	static void SetCurveKey(UCurveFloat* Curve, int32 KeyIndex, float NewTime, float NewValue);

private:

	/**
	 * Resolves the FKeyHandle for the Nth key on a curve. FIndexedCurve's
	 * index-based GetKeyHandle() is protected on recent engine versions, so
	 * this walks the public handle iterator instead.
	 *
	 * @return true if KeyIndex was valid and OutHandle was set.
	 */
	static bool FindKeyHandleByIndex(const FRichCurve& RichCurve, int32 KeyIndex, FKeyHandle& OutHandle);
};