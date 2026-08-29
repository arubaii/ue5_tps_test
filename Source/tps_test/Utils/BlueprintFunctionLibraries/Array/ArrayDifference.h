#pragma once

#include "CoreMinimal.h"
#include "ArrayDifference.generated.h"

UCLASS()
class TPS_TEST_API UArrayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Elements in A that are not in B. Order follows A; duplicates in A are preserved. */
	template<typename T>
	static TArray<T> ArrayDifference(const TArray<T>& a, const TArray<T>& b)
	{
		TArray<T> result;
		result.Reserve(a.Num());
		for (const T& element : a)
		{
			if (!b.Contains(element))
			{
				result.Add(element);
			}
		}
		return result;
	}

	/** Elements in exactly one of A or B (disjunctive union). */
	template<typename T>
	static TArray<T> ArraySymmetricDifference(const TArray<T>& a, const TArray<T>& b)
	{
		TArray<T> result = ArrayDifference(a, b);
		result.Append(ArrayDifference(b, a));
		return result;
	}
};