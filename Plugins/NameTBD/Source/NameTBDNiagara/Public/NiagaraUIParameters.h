// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "StructUtils/PropertyBag.h"

#include "NiagaraUIParameters.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;

/**
 * A Niagara System's User parameters as a property bag, the way the Niagara Particles widget and the Particles track list them:
 * every parameter the system exposes, each at the system's own value until changed. Three bags travel together:
 *  - Values:        what is shown and edited - only those changed from the system's value are ever handed on;
 *  - SystemValues:  the system's own values when it was last laid out - what tells a changed value from one following the system;
 *  - PositionNames: the Position parameters, laid out as vectors, handed back as positions.
 *
 * Values: float, int, bool, enum, color, vectors, position, quat, objects. Data interfaces (fix5): the curves - as a float,
 * color or vector curve struct - and the arrays - as an array of the element type - each set on the component's own copy of
 * the data interface (arrays through Niagara's array function library; a changed curve by swapping in a copy and making the
 * simulation again). Other data interfaces - Texture Sample (GPU only), meshes, grids, render targets - aren't listed.
 */
namespace NiagaraUIParameters
{
	/** A User parameter's name as the bag lists it, and as the Set ... Parameter functions take it: without "User.". */
	NAMETBDNIAGARA_API FName GetShortName(FName Name);

	/**
	 * Lays Values out again from System: its User parameters as they are now, each at the system's value unless Values had it
	 * changed (kept as long as it is still there, of the same type). No system empties them. Returns whether the rows changed -
	 * parameters added, removed or retyped - and bOutChanged whether anything did, values included.
	 */
	NAMETBDNIAGARA_API bool LayOut(UNiagaraSystem* System, FInstancedPropertyBag& Values, FInstancedPropertyBag& SystemValues, TArray<FName>& PositionNames, bool* bOutChanged = nullptr);

	/** Whether parameter Desc of Values holds something other than the system's own value. */
	NAMETBDNIAGARA_API bool IsChanged(const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const FPropertyBagPropertyDesc& Desc);

	/** Whether any parameter of Values is changed. */
	NAMETBDNIAGARA_API bool HasChanges(const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues);

	/** Hands parameter Desc of Bag to Component. */
	NAMETBDNIAGARA_API void Apply(UNiagaraComponent* Component, const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, const TArray<FName>& PositionNames);

	/** Hands Component every changed parameter of Values - the rest it already has, at the system's own (current) values. */
	NAMETBDNIAGARA_API void ApplyChanged(UNiagaraComponent* Component, const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const TArray<FName>& PositionNames);

	/** Hands Component the system's own value of every parameter Values changed - what ApplyChanged put on, taken back off. */
	NAMETBDNIAGARA_API void RestoreChanged(UNiagaraComponent* Component, const FInstancedPropertyBag& Values, const FInstancedPropertyBag& SystemValues, const TArray<FName>& PositionNames);

	/** Hands an object parameter on: as a material, a texture, or any object. */
	NAMETBDNIAGARA_API void ApplyObject(UNiagaraComponent* Component, FName Name, UObject* Value);
}

/**
 * User parameter values handed to one simulation on top of the Niagara Particles widget's own - a Particles track's. Kept
 * with the simulation, so they are taken back off before it plays anything else.
 */
USTRUCT()
struct NAMETBDNIAGARA_API FNiagaraUIParameterOverrides
{
	GENERATED_BODY()

	UPROPERTY()
	FInstancedPropertyBag Values;

	UPROPERTY()
	FInstancedPropertyBag SystemValues;

	UPROPERTY()
	TArray<FName> PositionNames;

	bool HasChanges() const { return NiagaraUIParameters::HasChanges(Values, SystemValues); }

	void ApplyChanged(UNiagaraComponent* Component) const { NiagaraUIParameters::ApplyChanged(Component, Values, SystemValues, PositionNames); }

	void RestoreChanged(UNiagaraComponent* Component) const { NiagaraUIParameters::RestoreChanged(Component, Values, SystemValues, PositionNames); }

	void Reset() { *this = FNiagaraUIParameterOverrides(); }
};
