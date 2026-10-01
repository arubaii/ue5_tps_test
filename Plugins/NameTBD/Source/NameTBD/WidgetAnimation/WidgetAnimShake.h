// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "WidgetAnimShake.generated.h"

// =====================================================================================================
// Shake
// =====================================================================================================
//
// A Shake track jitters its widget about where it is - a hit, an error, a rumble - by moving it and turning it along a
// smooth noise, as many times a second as its Shakes Per Second, strongest where its Falloff says. It adds to whatever
// else moves the widget, as a Slide and a Rotate do, and it lands exactly where it started: at its very start and its
// very end it moves nothing. The noise is worked out from time alone, so a Shake scrubs, loops and ping-pongs like any
// track and plays the same way every time for one Seed.
//
// Like a Pulse, it has no start and end values to travel between: no keys, no spring. Value-driven (Timing = Value), its
// value is how strongly it shakes - a trauma meter - and it shakes on its own clock for as long as it has any strength.
// Staggered over a panel's children or a text's letters, each item shakes its own way.

/** How a Shake's strength changes over its run. */
UENUM(BlueprintType)
enum class EWidgetAnimShakeFalloff : uint8
{
	Steady UMETA(ToolTip = "As strong from its start to its end."),
	FadeOut UMETA(DisplayName = "Fade Out", ToolTip = "Strongest at its start, settling to nothing by its end - a hit."),
	FadeIn UMETA(DisplayName = "Fade In", ToolTip = "Nothing at its start, building to its strongest at its end - a rumble growing."),
	FadeInOut UMETA(DisplayName = "Fade In and Out", ToolTip = "Building to its strongest halfway, and settling again by its end.")
};

/**
 * A Shake track's settings. See the note at the top of WidgetAnimShake.h. Not FWidgetAnimShakeParams: that name is the
 * text Shake track's (WidgetAnimTextTracks.h), which jitters letters.
 */
USTRUCT(BlueprintType)
struct NAMETBD_API FWidgetAnimShakeTrackParams
{
	GENERATED_BODY()

	/** How far the widget is thrown each way at full strength, in slate units: X across, Y up and down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "Distance"))
	FVector2D Distance = FVector2D(6.0, 6.0);

	/** How far the widget is turned each way at full strength, in degrees. 0 only moves it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "Angle", ClampMin = "0.0", UIMax = "45.0", Units = "Degrees"))
	float Angle = 2.f;

	/** How fast it shakes: how many times a second the widget changes direction, roughly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake", meta = (DisplayName = "Shakes Per Second", ClampMin = "0.1", UIMin = "1.0", UIMax = "60.0"))
	float Frequency = 18.f;

	/** How its strength changes over its run. Value-driven, its value is its strength instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	EWidgetAnimShakeFalloff Falloff = EWidgetAnimShakeFalloff::FadeOut;

	/** Picks one shake from all the ways it could go. Tracks with different Seeds on one widget shake apart. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shake")
	int32 Seed = 0;

	/** How strong the shake is at Alpha of its run, 0 - 1, by its Falloff. Exactly 0 at either end. */
	float GetEnvelope(float Alpha) const
	{
		if (Alpha <= 0.f || Alpha >= 1.f)
		{
			return 0.f;
		}
		switch (Falloff)
		{
		case EWidgetAnimShakeFalloff::FadeOut:   return FMath::Square(1.f - Alpha);
		case EWidgetAnimShakeFalloff::FadeIn:    return Alpha;
		case EWidgetAnimShakeFalloff::FadeInOut: return FMath::Sin(Alpha * PI);
		default:                                 return 1.f;
		}
	}

	/**
	 * The offset and turn Seconds into the shake, at Strength (0 - 1). ItemSeed tells apart the items of a stagger - each
	 * child, each letter - so they don't shake as one. Smooth noise, so the widget never teleports between frames.
	 */
	void Sample(float Seconds, float Strength, int32 ItemSeed, FVector2D& OutTranslation, float& OutAngle) const
	{
		if (Strength <= 0.f)
		{
			OutTranslation = FVector2D::ZeroVector;
			OutAngle = 0.f;
			return;
		}

		// Three unrelated tracks through the same noise - one per axis, one for the turn - each pushed off by the seeds.
		// Perlin noise reads about -0.6 to 0.6 at most; scaled up so full strength reaches about the whole distance.
		const float Phase = Seconds * FMath::Max(Frequency, 0.1f);
		const float Offset = static_cast<float>((static_cast<int64>(Seed) * 7919 + static_cast<int64>(ItemSeed) * 104729) % 10007) * 1.618f;
		constexpr float NoiseScale = 1.6f;
		OutTranslation.X = Distance.X * Strength * FMath::Clamp(FMath::PerlinNoise1D(Phase + Offset + 0.5f) * NoiseScale, -1.f, 1.f);
		OutTranslation.Y = Distance.Y * Strength * FMath::Clamp(FMath::PerlinNoise1D(Phase + Offset + 311.25f) * NoiseScale, -1.f, 1.f);
		OutAngle = Angle * Strength * FMath::Clamp(FMath::PerlinNoise1D(Phase * 0.8f + Offset + 723.75f) * NoiseScale, -1.f, 1.f);
	}
};
