#pragma once
#include "WildcardThunkHelpers.h"

// =============================================================================================================
// Must be included in a .cpp, not .h
// =============================================================================================================



// =============================================================================================================
// WILDCARD_THUNK_GET — inline CustomThunk generator for "(fixed params..., wildcard-out) -> bool" UFUNCTIONs.
//
// Must be invoked INSIDE the class body, immediately after the matching UFUNCTION(..., CustomThunk, ...)
// declaration — same place you'd normally hand-write DECLARE_FUNCTION(execFoo) { ... }.
//
// Usage:
//   UFUNCTION(BlueprintPure, CustomThunk, meta = (CustomStructureParam = "OutValue", StructTypePin = "ValueType"))
//   static bool MyPickerFunction(const FMyFixedParamType& FixedParam, UScriptStruct* ValueType, int32& OutValue);
//
//   WILDCARD_THUNK_GET(
//       MyPickerFunction,                                                     // FuncName: matches the UFUNCTION above
//       P_GET_STRUCT_REF(FMyFixedParamType, FixedParam); P_GET_OBJECT(UScriptStruct, ValueType);,  // ExtractFixedParams: one P_GET_* per fixed param, IN DECLARATION ORDER, each ending in ';'
//       bResult = Generic_MyImplementation(FixedParam, WildcardArg.Property, WildcardArg.Address);  // ResultExpr: sets bResult using WildcardArg.Property / WildcardArg.Address
//   )
//
// Notes:
//   - The wildcard param must be LAST in the UFUNCTION's parameter list.
//   - ExtractFixedParams and ResultExpr are pasted as raw statements — don't omit the trailing ';'.
//   - WildcardArg is already null-checked for you (ResultExpr only runs if WildcardArg.IsValid()).
// =============================================================================================================

#define WILDCARD_THUNK_GET(FuncName, ExtractFixedParams, ResultExpr)              \
DECLARE_FUNCTION(exec##FuncName)                                                  \
{                                                                                  \
ExtractFixedParams                                                            \
FWildcardStructArg WildcardArg = FWildcardThunkHelpers::StepWildcardStruct(Stack); \
P_FINISH;                                                                     \
bool bResult = false;                                                         \
P_NATIVE_BEGIN;                                                               \
if (WildcardArg.IsValid())                                                    \
{                                                                             \
ResultExpr                                                                \
}                                                                             \
P_NATIVE_END;                                                                 \
*static_cast<bool*>(RESULT_PARAM) = bResult;                                  \
}


// =============================================================================================================
// WILDCARD_THUNK_SET — inline CustomThunk generator for "(fixed params..., wildcard-in) -> bool" UFUNCTIONs.
//
// Must be invoked INSIDE the class body, immediately after the matching UFUNCTION(..., CustomThunk, ...)
// declaration.
//
// Usage:
//   UFUNCTION(BlueprintCallable, CustomThunk, meta = (CustomStructureParam = "InValue"))
//   static bool MySetterFunction(FMyFixedParamType& FixedParam, const int32& InValue);
//
//   WILDCARD_THUNK_SET(
//       MySetterFunction,
//       P_GET_STRUCT_REF(FMyFixedParamType, FixedParam);,                     // ExtractFixedParams
//       bResult = Generic_MySetImplementation(FixedParam, WildcardArg.Property, WildcardArg.Address);  // ResultExpr
//   )
// =============================================================================================================
#define WILDCARD_THUNK_SET(FuncName, ExtractFixedParams, ResultExpr)              \
DECLARE_FUNCTION(exec##FuncName)                                                  \
{                                                                                  \
    ExtractFixedParams                                                            \
    FWildcardStructArg WildcardArg = FWildcardThunkHelpers::StepWildcardStruct(Stack); \
    P_FINISH;                                                                     \
    bool bResult = false;                                                         \
    P_NATIVE_BEGIN;                                                               \
    if (WildcardArg.IsValid())                                                    \
    {                                                                             \
        ResultExpr                                                                \
    }                                                                             \
    P_NATIVE_END;                                                                 \
    *static_cast<bool*>(RESULT_PARAM) = bResult;                                  \
}