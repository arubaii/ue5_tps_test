// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVGRaster.h"

#include "WidgetSVG.h"

// nanosvg's own includes, made here so that they are not pulled into the namespace below (their include guards then
// skip them in there).
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// The vendored nanosvg (Source/ThirdParty/nanosvg), compiled as C++ in a namespace of its own, so its functions never
// clash with another copy (the engine's Slate links one) in a monolithic build. This is the only file that includes it.
THIRD_PARTY_INCLUDES_START
#define NANOSVG_CPLUSPLUS
#define NANOSVGRAST_CPLUSPLUS
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
namespace NameTBDNanoSVG
{
#include "nanosvg.h"
#include "nanosvgrast.h"
}
#undef NANOSVG_IMPLEMENTATION
#undef NANOSVGRAST_IMPLEMENTATION
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "WidgetSVGRaster"

namespace WidgetSVGRaster
{
	using namespace NameTBDNanoSVG;

	/** nanosvg's colours are 0xAABBGGRR; the alpha (the SVG's opacity) is left out. */
	static FColor ToFColor(const unsigned int PackedColor)
	{
		return FColor(PackedColor & 0xFF, (PackedColor >> 8) & 0xFF, (PackedColor >> 16) & 0xFF, 255);
	}

	/** Calls Visit(unsigned int& PackedColor) on every colour a shape is drawn with: fills, strokes, gradient stops. */
	template <typename VisitorType>
	static void ForEachColor(NSVGimage* Image, VisitorType&& Visit)
	{
		for (NSVGshape* Shape = Image->shapes; Shape; Shape = Shape->next)
		{
			for (NSVGpaint* Paint : { &Shape->fill, &Shape->stroke })
			{
				if (Paint->type == NSVG_PAINT_COLOR)
				{
					Visit(Paint->color);
				}
				else if ((Paint->type == NSVG_PAINT_LINEAR_GRADIENT || Paint->type == NSVG_PAINT_RADIAL_GRADIENT) && Paint->gradient)
				{
					for (int StopIndex = 0; StopIndex < Paint->gradient->nstops; ++StopIndex)
					{
						Visit(Paint->gradient->stops[StopIndex].color);
					}
				}
			}
		}
	}

	/** A parsed SVG, deleted with it. */
	struct FParsedSVG
	{
		NSVGimage* Image = nullptr;

		FParsedSVG() = default;
		FParsedSVG(const FParsedSVG&) = delete;
		FParsedSVG& operator=(const FParsedSVG&) = delete;
		~FParsedSVG() { if (Image) { nsvgDelete(Image); } }
	};

	static bool Fail(FText* OutError, const FText& Reason)
	{
		if (OutError)
		{
			*OutError = Reason;
		}
		return false;
	}

	/** Parses Source into Parsed; false (with a reason) if it can't be read or has no size. */
	static bool Parse(const FString& Source, FParsedSVG& Parsed, FText* OutError)
	{
		if (Source.IsEmpty())
		{
			return Fail(OutError, LOCTEXT("Empty", "The SVG is empty."));
		}

		// nsvgParse writes into the text it reads, so it gets its own UTF-8 copy.
		FTCHARToUTF8 Utf8Text(*Source);
		TArray<char> ParseBuffer;
		ParseBuffer.Append(Utf8Text.Get(), Utf8Text.Length());
		ParseBuffer.Add('\0');

		Parsed.Image = nsvgParse(ParseBuffer.GetData(), "px", 96.f);
		if (!Parsed.Image)
		{
			return Fail(OutError, LOCTEXT("ParseFailed", "The SVG could not be read."));
		}
		if (Parsed.Image->width <= 0.f || Parsed.Image->height <= 0.f)
		{
			return Fail(OutError, LOCTEXT("NoSize", "The SVG has no size: give it a width and height, or a viewBox."));
		}
		return true;
	}

	bool Read(const FString& Source, FVector2D& OutSize, TArray<FColor>& OutColors, FText* OutError)
	{
		FParsedSVG Parsed;
		if (!Parse(Source, Parsed, OutError))
		{
			return false;
		}
		OutSize = FVector2D(Parsed.Image->width, Parsed.Image->height);
		OutColors.Reset();
		ForEachColor(Parsed.Image, [&OutColors](unsigned int& PackedColor)
		{
			OutColors.AddUnique(ToFColor(PackedColor));
		});
		return true;
	}

	TArray<FWidgetSVGColor> MergeColors(const TArray<FColor>& Found, const TArray<FWidgetSVGColor>& Previous)
	{
		TArray<FWidgetSVGColor> Merged;
		Merged.Reserve(Found.Num());
		for (const FColor& FoundColor : Found)
		{
			FWidgetSVGColor& Added = Merged.AddDefaulted_GetRef();
			Added.Original = FoundColor;
			const FWidgetSVGColor* Kept = Previous.FindByPredicate([&FoundColor](const FWidgetSVGColor& Entry) { return Entry.Original == FoundColor; });
			Added.DrawAs = Kept ? Kept->DrawAs : FoundColor;
		}
		return Merged;
	}

	bool Draw(const FString& Source, const TArray<FWidgetSVGColor>& Colors, bool bDrawInWhite, float Scale, int32 MaxSide,
		TArray<uint8>& OutBGRA, FIntPoint& OutPixelSize, FText* OutError)
	{
		FParsedSVG Parsed;
		if (!Parse(Source, Parsed, OutError))
		{
			return false;
		}

		// Recolour: Draw As (alpha multiplying the SVG's own), or white.
		ForEachColor(Parsed.Image, [&Colors, bDrawInWhite](unsigned int& PackedColor)
		{
			const uint32 SVGAlpha = PackedColor >> 24;
			if (bDrawInWhite)
			{
				PackedColor = (SVGAlpha << 24) | 0x00FFFFFFu;
				return;
			}
			const FColor OriginalColor = ToFColor(PackedColor);
			const FWidgetSVGColor* Entry = Colors.FindByPredicate([&OriginalColor](const FWidgetSVGColor& Candidate) { return Candidate.Original == OriginalColor; });
			if (Entry)
			{
				const uint32 DrawAlpha = (SVGAlpha * Entry->DrawAs.A + 127) / 255;
				PackedColor = uint32(Entry->DrawAs.R) | (uint32(Entry->DrawAs.G) << 8) | (uint32(Entry->DrawAs.B) << 16) | (DrawAlpha << 24);
			}
		});

		// Size: Scale x the SVG's own size, the longest side held to MaxSide.
		float RasterScale = FMath::Max(Scale, 0.001f);
		const int32 SideCap = FMath::Clamp(MaxSide, 1, 8192);
		const float LongestSide = FMath::Max(Parsed.Image->width, Parsed.Image->height) * RasterScale;
		if (LongestSide > SideCap)
		{
			RasterScale *= SideCap / LongestSide;
		}
		OutPixelSize.X = FMath::Clamp(FMath::CeilToInt(Parsed.Image->width * RasterScale - KINDA_SMALL_NUMBER), 1, SideCap);
		OutPixelSize.Y = FMath::Clamp(FMath::CeilToInt(Parsed.Image->height * RasterScale - KINDA_SMALL_NUMBER), 1, SideCap);

		NSVGrasterizer* Rasterizer = nsvgCreateRasterizer();
		if (!Rasterizer)
		{
			return Fail(OutError, LOCTEXT("NoRasterizer", "Out of memory drawing the SVG."));
		}
		OutBGRA.SetNumZeroed(OutPixelSize.X * OutPixelSize.Y * 4);
		nsvgRasterize(Rasterizer, Parsed.Image, 0.f, 0.f, RasterScale, OutBGRA.GetData(), OutPixelSize.X, OutPixelSize.Y, OutPixelSize.X * 4);
		nsvgDeleteRasterizer(Rasterizer);

		// nanosvg draws RGBA: swap to BGRA.
		for (int32 PixelIndex = 0; PixelIndex < OutBGRA.Num(); PixelIndex += 4)
		{
			Swap(OutBGRA[PixelIndex], OutBGRA[PixelIndex + 2]);
		}
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
