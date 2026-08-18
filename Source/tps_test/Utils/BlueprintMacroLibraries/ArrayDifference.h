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
	static TArray<T> ArrayDifference(const TArray<T>& A, const TArray<T>& B)
	{
		TArray<T> Result;
		Result.Reserve(A.Num());
		for (const T& Element : A)
		{
			if (!B.Contains(Element))
			{
				Result.Add(Element);
			}
		}
		return Result;
	}

	/** Elements in exactly one of A or B (disjunctive union). */
	template<typename T>
	static TArray<T> ArraySymmetricDifference(const TArray<T>& A, const TArray<T>& B)
	{
		TArray<T> Result = ArrayDifference(A, B);
		Result.Append(ArrayDifference(B, A));
		return Result;
	}
};