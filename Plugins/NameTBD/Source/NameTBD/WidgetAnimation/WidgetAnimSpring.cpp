// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimSpring.h"

namespace
{
	/**
	 * Damping ratios this close to 1 use the critically damped solution. The underdamped and overdamped forms
	 * both divide by a frequency that goes to zero at exactly 1, so near it they lose precision; the critical
	 * form is exact there and differs from the true curve by far less than any rest threshold.
	 */
	constexpr double CriticalBand = 1.e-4;

	/** Below this a spring has no meaningful pull at all, and is treated as never settling. */
	constexpr double MinNaturalFrequency = 1.e-3;

	/** Below this the envelope decays so slowly it is treated as never settling. */
	constexpr double MinDecayRate = 1.e-6;

	/**
	 * Motion's visualDuration mapping: the undamped period is 1.2x the visual duration. Kept identical, so a
	 * spring tuned in Motion - or in any tool that exports Motion transitions - moves the same here.
	 */
	constexpr double VisualDurationPeriodScale = 1.2;

	/** Bounce limits. 0.95 keeps a damping ratio of at least 0.05, Motion's floor; -0.9 is a ratio of 10. */
	constexpr double MinBounce = -0.9;
	constexpr double MaxBounce = 0.95;

	/** Iterations for the critically damped settle search. Halves the interval each time: 10s / 2^40 is far below a frame. */
	constexpr int32 SettleSearchIterations = 40;

	/** A track's travel runs from -1 (the start, one whole travel behind the end) to 0 (the end). */
	constexpr double TrackStartDisplacement = -1.0;

	/** The coefficients and settle time of one track's spring, worked out once per evaluation. */
	struct FResolvedTrackSpring
	{
		FWidgetSpringCoefficients Coefficients;
		double InitialVelocity = 0.0;
		double SettleSeconds = WidgetAnimSpring::MinSettleSeconds;
	};

	FResolvedTrackSpring ResolveTrackSpring(const FWidgetSpringParams& Params)
	{
		FResolvedTrackSpring Resolved;
		Resolved.Coefficients = WidgetAnimSpring::ResolveCoefficients(Params);
		Resolved.InitialVelocity = Params.InitialVelocity;

		const double Settle = WidgetAnimSpring::ComputeSettleTime(Resolved.Coefficients, TrackStartDisplacement, Resolved.InitialVelocity, Params.RestThreshold);
		Resolved.SettleSeconds = FMath::Clamp(Settle, (double)WidgetAnimSpring::MinSettleSeconds, (double)WidgetAnimSpring::MaxSettleSeconds);
		return Resolved;
	}

	/**
	 * Progress along the travel, Seconds into the track. Pinned to exactly 0 and 1 at either end: the moment
	 * the track ends it holds its end pose, so landing on precisely 1 there is what keeps the last frame and
	 * the held pose identical. The difference is at most the rest threshold, which is the point of it.
	 */
	double EvaluateTrackProgress(const FResolvedTrackSpring& Resolved, double Seconds)
	{
		if (Seconds <= 0.0)
		{
			return 0.0;
		}

		if (Seconds >= Resolved.SettleSeconds)
		{
			return 1.0;
		}

		double Displacement = 0.0;
		double Velocity = 0.0;
		WidgetAnimSpring::Evaluate(Resolved.Coefficients, TrackStartDisplacement, Resolved.InitialVelocity, Seconds, Displacement, Velocity);
		return 1.0 + Displacement;
	}
}

// =====================================================================================================
// WidgetAnimSpring
// =====================================================================================================

FWidgetSpringCoefficients WidgetAnimSpring::ResolveCoefficients(const FWidgetSpringParams& Params)
{
	FWidgetSpringCoefficients Coefficients;

	if (Params.Mode == EWidgetSpringMode::DurationAndBounce)
	{
		// Mass 1, stiffness (2pi / (1.2 x visual duration))^2 - Motion's mapping. Bounce is 1 - damping ratio
		// above 0, as in Motion and SwiftUI; below 0 it follows SwiftUI into overdamping, 1 / (1 + bounce), so
		// -0.5 is a damping ratio of 2. The pace stays fixed while the bounce changes, which is the point of
		// describing a spring this way: bouncier does not also mean slower.
		const double Duration = FMath::Max((double)Params.VisualDuration, 0.02);
		Coefficients.NaturalFrequency = (2.0 * UE_DOUBLE_PI) / (Duration * VisualDurationPeriodScale);

		const double Bounce = FMath::Clamp((double)Params.Bounce, MinBounce, MaxBounce);
		Coefficients.DampingRatio = Bounce >= 0.0 ? (1.0 - Bounce) : (1.0 / (1.0 + Bounce));
	}
	else
	{
		// Non-positive mass or stiffness is a divide by zero in either formula, so both have floors. Damping may
		// be 0 - a real, endlessly oscillating spring - and is capped by the settle limit instead.
		const double Mass = FMath::Max((double)Params.Mass, 0.01);
		const double Stiffness = FMath::Max((double)Params.Stiffness, 0.01);
		const double Damping = FMath::Max((double)Params.Damping, 0.0);

		Coefficients.NaturalFrequency = FMath::Sqrt(Stiffness / Mass);
		Coefficients.DampingRatio = Damping / (2.0 * FMath::Sqrt(Stiffness * Mass));
	}

	return Coefficients;
}

void WidgetAnimSpring::Evaluate(const FWidgetSpringCoefficients& Coefficients, double Displacement, double Velocity, double Time, double& OutDisplacement, double& OutVelocity)
{
	const double Omega = FMath::Max(Coefficients.NaturalFrequency, MinNaturalFrequency);
	const double Zeta = FMath::Max(Coefficients.DampingRatio, 0.0);
	const double T = FMath::Max(Time, 0.0);

	if (Zeta < 1.0 - CriticalBand)
	{
		// Underdamped: a sine wave inside a decaying envelope - the overshoot and the wobble.
		const double DampedOmega = Omega * FMath::Sqrt(1.0 - Zeta * Zeta);
		const double Decay = Zeta * Omega;
		const double A = Displacement;
		const double B = (Velocity + Decay * Displacement) / DampedOmega;

		const double Envelope = FMath::Exp(-Decay * T);
		const double Cos = FMath::Cos(DampedOmega * T);
		const double Sin = FMath::Sin(DampedOmega * T);

		OutDisplacement = Envelope * (A * Cos + B * Sin);
		OutVelocity = Envelope * (Cos * (B * DampedOmega - Decay * A) - Sin * (A * DampedOmega + Decay * B));
		return;
	}

	if (Zeta <= 1.0 + CriticalBand)
	{
		// Critically damped: the fastest arrival that never passes the target (from rest).
		const double A = Displacement;
		const double B = Velocity + Omega * Displacement;
		const double Envelope = FMath::Exp(-Omega * T);

		OutDisplacement = Envelope * (A + B * T);
		OutVelocity = Envelope * (B - Omega * (A + B * T));
		return;
	}

	// Overdamped: two decaying exponentials, one fast and one slow, and no oscillation.
	const double Root = FMath::Sqrt(Zeta * Zeta - 1.0);
	const double SlowRate = -Omega * (Zeta - Root);
	const double FastRate = -Omega * (Zeta + Root);
	const double CFast = (Velocity - SlowRate * Displacement) / (FastRate - SlowRate);
	const double CSlow = Displacement - CFast;

	const double ESlow = FMath::Exp(SlowRate * T);
	const double EFast = FMath::Exp(FastRate * T);

	OutDisplacement = CSlow * ESlow + CFast * EFast;
	OutVelocity = CSlow * SlowRate * ESlow + CFast * FastRate * EFast;
}

double WidgetAnimSpring::ComputeSettleTime(const FWidgetSpringCoefficients& Coefficients, double Displacement, double Velocity, double Tolerance)
{
	// Every case bounds the displacement from above with something that only ever shrinks, and solves for
	// when the bound drops under Tolerance. Past that point the real curve can't come back out, so the track
	// can end there and hold. A bound is slightly pessimistic - the track runs a touch long, never short.
	const double Tol = FMath::Max(Tolerance, 1.e-6);
	const double Omega = Coefficients.NaturalFrequency;
	const double Zeta = FMath::Max(Coefficients.DampingRatio, 0.0);

	if (Omega <= MinNaturalFrequency)
	{
		return MaxSettleSeconds;
	}

	if (Zeta < 1.0 - CriticalBand)
	{
		// |x(t)| <= sqrt(A^2 + B^2) * e^(-zeta omega t): the amplitude of the wobble, decaying.
		const double Decay = Zeta * Omega;
		if (Decay <= MinDecayRate)
		{
			return MaxSettleSeconds;
		}

		const double DampedOmega = Omega * FMath::Sqrt(1.0 - Zeta * Zeta);
		const double A = Displacement;
		const double B = (Velocity + Decay * Displacement) / DampedOmega;
		const double Amplitude = FMath::Sqrt(A * A + B * B);

		if (Amplitude <= Tol)
		{
			return 0.0;
		}

		return FMath::Min(FMath::Loge(Amplitude / Tol) / Decay, (double)MaxSettleSeconds);
	}

	if (Zeta <= 1.0 + CriticalBand)
	{
		// |x(t)| <= (|A| + |B| t) e^(-omega t). Rises until its peak, then falls for good - so the answer is found
		// by halving the interval from the peak out, compared in logs to stay clear of underflow.
		const double A = FMath::Abs(Displacement);
		const double B = FMath::Abs(Velocity + Omega * Displacement);
		const double LogTol = FMath::Loge(Tol);

		if (B <= UE_DOUBLE_SMALL_NUMBER)
		{
			if (A <= Tol)
			{
				return 0.0;
			}
			return FMath::Min(FMath::Loge(A / Tol) / Omega, (double)MaxSettleSeconds);
		}

		auto LogBound = [A, B, Omega](double T)
		{
			return FMath::Loge(A + B * T) - Omega * T;
		};

		const double Peak = FMath::Max(0.0, 1.0 / Omega - A / B);
		if (LogBound(Peak) <= LogTol)
		{
			// Never outside the tolerance at all: the bound is largest at its peak.
			return 0.0;
		}

		if (LogBound(MaxSettleSeconds) > LogTol)
		{
			return MaxSettleSeconds;
		}

		double Low = Peak;
		double High = MaxSettleSeconds;
		for (int32 Iteration = 0; Iteration < SettleSearchIterations; ++Iteration)
		{
			const double Mid = 0.5 * (Low + High);
			if (LogBound(Mid) > LogTol)
			{
				Low = Mid;
			}
			else
			{
				High = Mid;
			}
		}
		return High;
	}

	// |x(t)| <= (|CSlow| + |CFast|) e^(slow rate t): the fast exponential is always inside the slow one.
	const double Root = FMath::Sqrt(Zeta * Zeta - 1.0);
	const double SlowRate = -Omega * (Zeta - Root);
	const double FastRate = -Omega * (Zeta + Root);
	const double CFast = (Velocity - SlowRate * Displacement) / (FastRate - SlowRate);
	const double CSlow = Displacement - CFast;
	const double Amplitude = FMath::Abs(CSlow) + FMath::Abs(CFast);

	if (Amplitude <= Tol)
	{
		return 0.0;
	}

	if (-SlowRate <= MinDecayRate)
	{
		return MaxSettleSeconds;
	}

	return FMath::Min(FMath::Loge(Amplitude / Tol) / -SlowRate, (double)MaxSettleSeconds);
}

// =====================================================================================================
// UWidgetAnimSpringLibrary
// =====================================================================================================

float UWidgetAnimSpringLibrary::GetSettleTime(const FWidgetSpringParams& Params)
{
	return (float)ResolveTrackSpring(Params).SettleSeconds;
}

float UWidgetAnimSpringLibrary::GetVisualDuration(const FWidgetSpringParams& Params)
{
	const FWidgetSpringCoefficients Coefficients = WidgetAnimSpring::ResolveCoefficients(Params);
	const double Omega = FMath::Max(Coefficients.NaturalFrequency, MinNaturalFrequency);
	return (float)((2.0 * UE_DOUBLE_PI) / (Omega * VisualDurationPeriodScale));
}

float UWidgetAnimSpringLibrary::GetDampingRatio(const FWidgetSpringParams& Params)
{
	return (float)WidgetAnimSpring::ResolveCoefficients(Params).DampingRatio;
}

float UWidgetAnimSpringLibrary::EvaluateProgress(const FWidgetSpringParams& Params, float Seconds)
{
	return (float)EvaluateTrackProgress(ResolveTrackSpring(Params), Seconds);
}

float UWidgetAnimSpringLibrary::EvaluateDirectedProgress(const FWidgetSpringParams& Params, float Alpha, bool bTowardStart)
{
	// Resolved once: the settle time is what turns the track's 0..1 alpha back into seconds of spring time.
	const FResolvedTrackSpring Resolved = ResolveTrackSpring(Params);
	const double ClampedAlpha = FMath::Clamp((double)Alpha, 0.0, 1.0);

	if (!bTowardStart)
	{
		return (float)EvaluateTrackProgress(Resolved, ClampedAlpha * Resolved.SettleSeconds);
	}

	// Heading back to the start, forward in time: (1 - Alpha) of the track has elapsed since it left the end.
	return (float)(1.0 - EvaluateTrackProgress(Resolved, (1.0 - ClampedAlpha) * Resolved.SettleSeconds));
}

bool UWidgetAnimSpringLibrary::AdvanceSpring(const FWidgetSpringParams& Params, float Target, FWidgetSpringState& State, float DeltaTime, float RestDistance, float RestSpeed)
{
	const FWidgetSpringCoefficients Coefficients = WidgetAnimSpring::ResolveCoefficients(Params);

	// One exact step from wherever it is now, however long the frame was. Retargeting needs nothing special:
	// the displacement is simply measured from the new target, and the velocity carries straight over.
	double Displacement = 0.0;
	double Velocity = 0.0;
	WidgetAnimSpring::Evaluate(Coefficients, (double)State.Value - Target, State.Velocity, FMath::Max(DeltaTime, 0.f), Displacement, Velocity);

	if (FMath::Abs(Displacement) <= FMath::Max(RestDistance, 0.f) && FMath::Abs(Velocity) <= FMath::Max(RestSpeed, 0.f))
	{
		State.Value = Target;
		State.Velocity = 0.f;
		return true;
	}

	State.Value = (float)(Target + Displacement);
	State.Velocity = (float)Velocity;
	return false;
}