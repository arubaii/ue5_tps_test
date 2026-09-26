// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimEasing.h"

namespace WidgetAnimEasing
{
	float EaseOutBack(float Alpha, float Overshoot)
	{
		const float C1 = Overshoot;
		const float C3 = C1 + 1.f;
		const float T = Alpha - 1.f;
		return 1.f + C3 * FMath::Pow(T, 3.f) + C1 * FMath::Pow(T, 2.f);
	}

	float Evaluate(EWidgetAnimEasing Easing, const UCurveFloat* Curve, float Alpha)
	{
		Alpha = FMath::Clamp(Alpha, 0.f, 1.f);

		switch (Easing)
		{
		case EWidgetAnimEasing::Linear:
			return Alpha;
		case EWidgetAnimEasing::EaseIn:
			return FMath::InterpEaseIn(0.f, 1.f, Alpha, 2.f);
		case EWidgetAnimEasing::EaseOut:
			return FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f);
		case EWidgetAnimEasing::EaseInOut:
			return FMath::InterpEaseInOut(0.f, 1.f, Alpha, 2.f);
		case EWidgetAnimEasing::Custom:
			return Curve ? Curve->GetFloatValue(Alpha) : Alpha;
		}

		return Alpha;
	}

	float EvaluateWithOvershoot(EWidgetAnimEasing Easing, const UCurveFloat* Curve, float Alpha, float Overshoot)
	{
		// Deliberately not clamped before EaseOutBack: the whole point of the curve is that it leaves 0..1
		// in the middle. It is still fed a clamped input, which the caller guarantees.
		return Overshoot > 0.f
			? EaseOutBack(FMath::Clamp(Alpha, 0.f, 1.f), Overshoot)
			: Evaluate(Easing, Curve, Alpha);
	}
}