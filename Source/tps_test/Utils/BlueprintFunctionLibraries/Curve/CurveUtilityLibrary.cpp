// CurveUtilityLibrary.cpp

#include "CurveUtilityLibrary.h"

bool UCurveUtilityLibrary::FindKeyHandleByIndex(const FRichCurve& RichCurve, int32 KeyIndex, FKeyHandle& OutHandle)
{
	if (KeyIndex < 0 || KeyIndex >= RichCurve.GetNumKeys())
	{
		return false;
	}

	int32 CurrentIndex = 0;
	for (auto It = RichCurve.GetKeyHandleIterator(); It; ++It)
	{
		if (CurrentIndex == KeyIndex)
		{
			OutHandle = *It;
			return true;
		}
		++CurrentIndex;
	}

	return false;
}

void UCurveUtilityLibrary::SetCurveKeyTime(UCurveFloat* Curve, int32 KeyIndex, float NewTime)
{
	if (!Curve)
	{
		return;
	}

	FRichCurve& RichCurve = Curve->FloatCurve;

	FKeyHandle Handle;
	if (!FindKeyHandleByIndex(RichCurve, KeyIndex, Handle))
	{
		return;
	}

	RichCurve.SetKeyTime(Handle, NewTime);

#if WITH_EDITOR
	Curve->MarkPackageDirty();
#endif
}

void UCurveUtilityLibrary::SetCurveKeyValue(UCurveFloat* Curve, int32 KeyIndex, float NewValue)
{
	if (!Curve)
	{
		return;
	}

	FRichCurve& RichCurve = Curve->FloatCurve;

	FKeyHandle Handle;
	if (!FindKeyHandleByIndex(RichCurve, KeyIndex, Handle))
	{
		return;
	}

	RichCurve.SetKeyValue(Handle, NewValue);

#if WITH_EDITOR
	Curve->MarkPackageDirty();
#endif
}

void UCurveUtilityLibrary::SetCurveKey(UCurveFloat* Curve, int32 KeyIndex, float NewTime, float NewValue)
{
	if (!Curve)
	{
		return;
	}

	FRichCurve& RichCurve = Curve->FloatCurve;

	FKeyHandle Handle;
	if (!FindKeyHandleByIndex(RichCurve, KeyIndex, Handle))
	{
		return;
	}

	// Set time first, then value, on the same handle - SetKeyTime can
	// reorder the underlying key array but the handle itself stays valid
	// and keeps pointing at the same key.
	RichCurve.SetKeyTime(Handle, NewTime);
	RichCurve.SetKeyValue(Handle, NewValue);

#if WITH_EDITOR
	Curve->MarkPackageDirty();
#endif
}