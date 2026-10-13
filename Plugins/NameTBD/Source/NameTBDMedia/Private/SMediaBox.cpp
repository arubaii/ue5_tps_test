// Copyright AR GameSystems 2026. All rights reserved.

#include "SMediaBox.h"

#include "MediaBox.h"
#include "MediaBoxClip.h"
#include "MediaBoxLog.h"
#include "MediaBoxPool.h"

#include "Engine/Texture.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "MediaTexture.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace SMediaBoxPrivate
{
	/** The size of what a brush shows: its texture's, else none (it then fills the box). */
	FVector2f GetTextureSize(const UTexture* Texture)
	{
		if (!Texture)
		{
			return FVector2f::ZeroVector;
		}
		return FVector2f(Texture->GetSurfaceWidth(), Texture->GetSurfaceHeight());
	}

	/** Where in the box, how big, and which part of the image, for Fit. */
	void LayOut(EMediaBoxFit Fit, const FVector2f& Area, const FVector2f& MediaSize, const FVector2D& Alignment, const FVector2D& CropFocus,
		FVector2f& OutOffset, FVector2f& OutSize, FBox2f& OutUV)
	{
		OutOffset = FVector2f::ZeroVector;
		OutSize = Area;
		OutUV = FBox2f(FVector2f(0.f, 0.f), FVector2f(1.f, 1.f));

		if (Fit == EMediaBoxFit::Stretch || MediaSize.X <= 0.f || MediaSize.Y <= 0.f || Area.X <= 0.f || Area.Y <= 0.f)
		{
			return;
		}

		if (Fit == EMediaBoxFit::Contain)
		{
			const float Scale = FMath::Min(Area.X / MediaSize.X, Area.Y / MediaSize.Y);
			OutSize = MediaSize * Scale;
			OutOffset = (Area - OutSize) * FVector2f(Alignment);
			return;
		}

		// Cover: the whole box, the image cropped to it around Crop Focus.
		const float Scale = FMath::Max(Area.X / MediaSize.X, Area.Y / MediaSize.Y);
		const FVector2f Shown = Area / (MediaSize * Scale);
		const FVector2f UVMin = (FVector2f(1.f, 1.f) - Shown) * FVector2f(CropFocus);
		OutUV = FBox2f(UVMin, UVMin + Shown);
	}

	void DrawImage(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const FSlateBrush& Brush,
		const FVector2f& Offset, const FVector2f& Size, const FBox2f& UV, const FLinearColor& Color, ESlateDrawEffect Effects)
	{
		FSlateBrush Drawn = Brush;
		Drawn.DrawAs = ESlateBrushDrawType::Image;
		Drawn.Tiling = ESlateBrushTileType::NoTile;
		Drawn.SetUVRegion(UV);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Offset)), &Drawn, Effects, Color);
	}
}

void SMediaBox::Construct(const FArguments& InArgs)
{
	WeakOwnerBox = InArgs._OwnerBox;
}

FVector2D SMediaBox::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const UMediaBox* OwnerBox = WeakOwnerBox.Get();
	return OwnerBox ? OwnerBox->DesiredSize : FVector2D(320.0, 180.0);
}

int32 SMediaBox::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace SMediaBoxPrivate;

	UMediaBox* OwnerBox = WeakOwnerBox.Get();
	if (!OwnerBox)
	{
		return LayerId;
	}
	OwnerBox->NotifyPainted();

	const ESlateDrawEffect Effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor WidgetTint = InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor ImageTint = WidgetTint * OwnerBox->Tint;
	const FVector2f Area = FVector2f(FVector2D(AllottedGeometry.GetLocalSize()));

	// The letterbox: the whole box.
	if (OwnerBox->LetterboxColor.A > 0.f)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), FCoreStyle::Get().GetBrush("WhiteBrush"), Effects, WidgetTint * OwnerBox->LetterboxColor);
	}

	const UMediaBoxClip* PaintedClip = OwnerBox->MediaClip;
	const UTexture* PosterTexture = PaintedClip ? PaintedClip->Poster.Get() : nullptr;
	const UMediaBoxPlayer* PaintedPlayer = OwnerBox->LeasedPlayer;
	const UMediaTexture* VideoTexture = PaintedPlayer ? PaintedPlayer->GetMediaTexture() : nullptr;

	// The video's size once it has a frame, else the poster's - so the poster and video lie exactly over each other.
	FVector2f MediaSize = VideoTexture && PaintedPlayer->IsFrameReady() ? GetTextureSize(VideoTexture) : FVector2f::ZeroVector;
	if (MediaSize.X <= 0.f || MediaSize.Y <= 0.f)
	{
		MediaSize = GetTextureSize(PosterTexture);
	}

	FVector2f Offset;
	FVector2f Size;
	FBox2f UV;
	LayOut(OwnerBox->Fit, Area, MediaSize, OwnerBox->ContentAlignment, OwnerBox->CropFocus, Offset, Size, UV);

	// The poster, under the video, until the video covers it.
	const float VideoAlpha = FMath::Clamp(OwnerBox->VideoAlpha, 0.f, 1.f);
	if (PosterTexture && VideoAlpha < 1.f)
	{
		DrawImage(OutDrawElements, LayerId, AllottedGeometry, OwnerBox->PosterBrush, Offset, Size, UV, ImageTint, Effects);
	}

	// The video, fading in over it.
	if (VideoTexture && VideoAlpha > 0.f)
	{
		FLinearColor VideoTint = ImageTint;
		VideoTint.A *= VideoAlpha;
		DrawImage(OutDrawElements, LayerId + 1, AllottedGeometry, OwnerBox->VideoBrush, Offset, Size, UV, VideoTint, Effects);
	}

	// Can't play: the error image over the poster.
	if (OwnerBox->Status == EMediaBoxStatus::Error && OwnerBox->ErrorImage.GetResourceObject())
	{
		const FVector2f ErrorSize = FVector2f(OwnerBox->ErrorImage.ImageSize);
		const FVector2f ErrorDrawn = (ErrorSize.X > 0.f && ErrorSize.Y > 0.f) ? FVector2f(FMath::Min(ErrorSize.X, Area.X), FMath::Min(ErrorSize.Y, Area.Y)) : Area;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(ErrorDrawn, FSlateLayoutTransform((Area - ErrorDrawn) * 0.5f)), &OwnerBox->ErrorImage, Effects, WidgetTint * OwnerBox->ErrorImage.GetTint(InWidgetStyle));
	}

	// The subtitle line, at the bottom, centred, a line at a time.
	if (OwnerBox->bShowSubtitles && !OwnerBox->CurrentSubtitle.IsEmpty() && MediaBoxRuntime::AreSubtitlesEnabled() && FSlateApplication::IsInitialized())
	{
		const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		TArray<FString> Lines;
		OwnerBox->CurrentSubtitle.ToString().ParseIntoArrayLines(Lines);

		const FVector2f Margin = FVector2f(OwnerBox->SubtitleMargin);
		float Bottom = Area.Y - Margin.X;
		const FLinearColor TextColor = OwnerBox->SubtitleColor * WidgetTint;
		const FLinearColor BackColor = OwnerBox->SubtitleBackground * WidgetTint;
		for (int32 LineIndex = Lines.Num() - 1; LineIndex >= 0; --LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			const FVector2f TextSize = FVector2f(FVector2D(FontMeasure->Measure(Line, OwnerBox->SubtitleFont)));
			const FVector2f BackSize = TextSize + FVector2f(Margin.Y * 2.f, Margin.Y * 0.5f);
			const FVector2f BackAt((Area.X - BackSize.X) * 0.5f, Bottom - BackSize.Y);
			if (BackColor.A > 0.f)
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 3, AllottedGeometry.ToPaintGeometry(BackSize, FSlateLayoutTransform(BackAt)), FCoreStyle::Get().GetBrush("WhiteBrush"), Effects, BackColor);
			}
			const FVector2f TextAt = BackAt + (BackSize - TextSize) * 0.5f;
			FSlateDrawElement::MakeText(OutDrawElements, LayerId + 4, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(TextAt)), Line, OwnerBox->SubtitleFont, Effects, TextColor);
			Bottom = BackAt.Y;
		}
		return LayerId + 4;
	}

	return LayerId + 2;
}
