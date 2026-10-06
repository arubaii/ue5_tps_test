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

	float EvaluateBezier(const FVector2D& Leave, const FVector2D& Arrive, float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		if (X <= 0.f)
		{
			return 0.f;
		}
		if (X >= 1.f)
		{
			return 1.f;
		}

		// Both handles' X inside the segment keeps x(t) rising, so exactly one t answers each X.
		const double X1 = FMath::Clamp(Leave.X, 0.0, 1.0);
		const double X2 = FMath::Clamp(Arrive.X, 0.0, 1.0);
		const double Y1 = Leave.Y;
		const double Y2 = Arrive.Y;

		// The polynomial coefficients of x(t) and y(t), each a + b t + c t^2 + d t^3 with the ends at 0 and 1.
		const double CX = 3.0 * X1;
		const double BX = 3.0 * (X2 - X1) - CX;
		const double AX = 1.0 - CX - BX;
		const double CY = 3.0 * Y1;
		const double BY = 3.0 * (Y2 - Y1) - CY;
		const double AY = 1.0 - CY - BY;

		auto SampleX = [AX, BX, CX](double T) { return ((AX * T + BX) * T + CX) * T; };
		auto SampleY = [AY, BY, CY](double T) { return ((AY * T + BY) * T + CY) * T; };
		auto SlopeX = [AX, BX, CX](double T) { return (3.0 * AX * T + 2.0 * BX) * T + CX; };

		// Newton's method from X itself converges in a few steps on any usual curve; where the slope flattens - a handle
		// lying on the time axis - it is finished by halving, which always converges.
		double T = X;
		bool bSolved = false;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const double Error = SampleX(T) - X;
			if (FMath::Abs(Error) < 1.e-7)
			{
				bSolved = true;
				break;
			}
			const double Slope = SlopeX(T);
			if (FMath::Abs(Slope) < 1.e-6)
			{
				break;
			}
			T -= Error / Slope;
		}

		if (!bSolved || T < 0.0 || T > 1.0)
		{
			double Low = 0.0;
			double High = 1.0;
			T = X;
			for (int32 Step = 0; Step < 40; ++Step)
			{
				const double Value = SampleX(T);
				if (FMath::Abs(Value - X) < 1.e-7)
				{
					break;
				}
				(Value < X ? Low : High) = T;
				T = (Low + High) * 0.5;
			}
		}

		return static_cast<float>(SampleY(T));
	}

	bool GetBezierHandles(EWidgetAnimEasing Easing, FVector2D& OutLeave, FVector2D& OutArrive)
	{
		// A quadratic raised to a cubic: x(t) = t needs its handles at a third and two thirds of the way, and t^2 and
		// 1 - (1 - t)^2 are exactly these heights - so the handles draw exactly the curve Evaluate eases by.
		switch (Easing)
		{
		case EWidgetAnimEasing::Linear:
			OutLeave = FVector2D(1.0 / 3.0, 1.0 / 3.0);
			OutArrive = FVector2D(2.0 / 3.0, 2.0 / 3.0);
			return true;
		case EWidgetAnimEasing::EaseIn:
			OutLeave = FVector2D(1.0 / 3.0, 0.0);
			OutArrive = FVector2D(2.0 / 3.0, 1.0 / 3.0);
			return true;
		case EWidgetAnimEasing::EaseOut:
			OutLeave = FVector2D(1.0 / 3.0, 2.0 / 3.0);
			OutArrive = FVector2D(2.0 / 3.0, 1.0);
			return true;
		case EWidgetAnimEasing::EaseInOut:
			// Two squares joined in the middle have no single Bezier; this one is within a hundredth of it everywhere.
			OutLeave = FVector2D(0.45, 0.0);
			OutArrive = FVector2D(0.55, 1.0);
			return true;
		default:
			return false;
		}
	}
}