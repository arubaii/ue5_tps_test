// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSVGBrush.h"

#include "Async/Async.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "Rendering/SlateRenderer.h"
#include "UObject/ObjectKey.h"
#include "Widgets/SWidget.h"
#include "WidgetSVG.h"
#include "WidgetSVGRaster.h"

/** Which drawing: an SVG, at one revision of it, at one size in pixels. */
struct FWidgetSVGDrawingKey
{
	FObjectKey SVG;
	uint32 Revision = 0;
	FIntPoint PixelSize = FIntPoint::ZeroValue;

	bool operator==(const FWidgetSVGDrawingKey& Other) const
	{
		return SVG == Other.SVG && Revision == Other.Revision && PixelSize == Other.PixelSize;
	}

	friend uint32 GetTypeHash(const FWidgetSVGDrawingKey& Key)
	{
		return HashCombineFast(HashCombineFast(GetTypeHash(Key.SVG), ::GetTypeHash(Key.Revision)), GetTypeHash(Key.PixelSize));
	}
};

/** An SVG drawn at one size, held as a Slate dynamic image while any widget shows it. Game thread only. */
class FWidgetSVGDrawing : public TSharedFromThis<FWidgetSVGDrawing>
{
public:

	FWidgetSVGDrawingKey Key;

	/** Done: drawn (ImageBrush set), or failed (ImageBrush null). */
	bool bDone = false;

	/** The image, once drawn. */
	TSharedPtr<FSlateDynamicImageBrush> ImageBrush;

	/** Widgets to repaint once drawn. */
	TArray<TWeakPtr<SWidget>> Waiters;

	~FWidgetSVGDrawing()
	{
		FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
		if (ImageBrush.IsValid() && Renderer)
		{
			Renderer->ReleaseDynamicResource(*ImageBrush);
		}
	}

	/** Takes the drawn pixels (game thread): makes the Slate image and repaints the widgets waiting for it. */
	void Finish(bool bDrawn, const TArray<uint8>& Pixels, const FIntPoint& DrawnSize)
	{
		bDone = true;
		FSlateRenderer* Renderer = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetRenderer() : nullptr;
		if (bDrawn && Renderer)
		{
			static uint64 NextResourceId = 0;
			const FName ResourceName(*FString::Printf(TEXT("NameTBD_SVG_%llu"), ++NextResourceId));
			if (Renderer->GenerateDynamicImageResource(ResourceName, DrawnSize.X, DrawnSize.Y, Pixels))
			{
				ImageBrush = MakeShared<FSlateDynamicImageBrush>(ResourceName, FVector2D(DrawnSize.X, DrawnSize.Y));
			}
		}
		for (const TWeakPtr<SWidget>& Waiter : Waiters)
		{
			if (const TSharedPtr<SWidget> PinnedWaiter = Waiter.Pin())
			{
				PinnedWaiter->Invalidate(EInvalidateWidgetReason::Paint);
			}
		}
		Waiters.Empty();
	}
};

namespace WidgetSVGBrushPrivate
{
	/** Every drawing alive, so widgets showing the same SVG at the same size share one. */
	static TMap<FWidgetSVGDrawingKey, TWeakPtr<FWidgetSVGDrawing>> Drawings;

	/** Drawn at once (not on a worker thread) while a widget shows nothing yet, up to this many pixels: a typical icon. */
	static constexpr int32 MaxPixelsDrawnAtOnce = 512 * 512;

	static TSharedRef<FWidgetSVGDrawing> FindOrStart(const FWidgetSVGDrawingKey& Key, const UWidgetSVG& SVG, bool bMayDrawAtOnce)
	{
		if (const TWeakPtr<FWidgetSVGDrawing>* Found = Drawings.Find(Key))
		{
			if (const TSharedPtr<FWidgetSVGDrawing> Existing = Found->Pin())
			{
				return Existing.ToSharedRef();
			}
		}

		// Now and then, forget the drawings nothing shows any more.
		if (Drawings.Num() >= 64 && (Drawings.Num() & 31) == 0)
		{
			for (auto It = Drawings.CreateIterator(); It; ++It)
			{
				if (!It.Value().IsValid())
				{
					It.RemoveCurrent();
				}
			}
		}

		const TSharedRef<FWidgetSVGDrawing> Drawing = MakeShared<FWidgetSVGDrawing>();
		Drawing->Key = Key;
		Drawings.Add(Key, Drawing);

		// The SVG at the scale that covers the size both ways (a stretched box is then drawn smaller one way, not bigger).
		const float Scale = static_cast<float>(FMath::Max(Key.PixelSize.X / FMath::Max(SVG.SVGSize.X, 1.0), Key.PixelSize.Y / FMath::Max(SVG.SVGSize.Y, 1.0)));

		if (bMayDrawAtOnce && Key.PixelSize.X * Key.PixelSize.Y <= MaxPixelsDrawnAtOnce)
		{
			TArray<uint8> Pixels;
			FIntPoint DrawnSize;
			const bool bDrawn = WidgetSVGRaster::Draw(SVG.SVGSource, SVG.Colors, SVG.bDrawInWhite, Scale, FWidgetSVGBrush::MaxPixelSide, Pixels, DrawnSize);
			Drawing->Finish(bDrawn, Pixels, DrawnSize);
			return Drawing;
		}

		// On a worker thread, from copies; back on the game thread to make the Slate image (if it is still wanted).
		TWeakPtr<FWidgetSVGDrawing> WeakDrawing = Drawing;
		Async(EAsyncExecution::ThreadPool, [WeakDrawing, Source = SVG.SVGSource, SVGColors = SVG.Colors, bWhite = SVG.bDrawInWhite, Scale]()
		{
			TArray<uint8> Pixels;
			FIntPoint DrawnSize;
			const bool bDrawn = WidgetSVGRaster::Draw(Source, SVGColors, bWhite, Scale, FWidgetSVGBrush::MaxPixelSide, Pixels, DrawnSize);
			AsyncTask(ENamedThreads::GameThread, [WeakDrawing, bDrawn, Pixels = MoveTemp(Pixels), DrawnSize]()
			{
				if (const TSharedPtr<FWidgetSVGDrawing> PinnedDrawing = WeakDrawing.Pin())
				{
					PinnedDrawing->Finish(bDrawn, Pixels, DrawnSize);
				}
			});
		});
		return Drawing;
	}
}

FIntPoint FWidgetSVGBrush::GetPixelSize(const FGeometry& Geometry, const FVector2f& LocalSize)
{
	const FSlateRenderTransform& RenderTransform = Geometry.GetAccumulatedRenderTransform();
	const FVector2f AxisX = RenderTransform.TransformVector(FVector2f(LocalSize.X, 0.f));
	const FVector2f AxisY = RenderTransform.TransformVector(FVector2f(0.f, LocalSize.Y));
	return FIntPoint(
		FMath::Clamp(FMath::CeilToInt(AxisX.Size() - 0.01f), 1, MaxPixelSide),
		FMath::Clamp(FMath::CeilToInt(AxisY.Size() - 0.01f), 1, MaxPixelSide));
}

const FSlateBrush* FWidgetSVGBrush::Get(const UWidgetSVG* SVG, const FSlateBrush* Template, const FGeometry& Geometry, const FVector2f& LocalSize, const SWidget& Widget)
{
	if (LocalSize.X <= 0.f || LocalSize.Y <= 0.f)
	{
		return nullptr;
	}
	const FSlateBrush* Brush = GetAtPixelSize(SVG, Template, GetPixelSize(Geometry, LocalSize), &Widget);
	if (Brush)
	{
		Composed.ImageSize = FVector2D(LocalSize);
	}
	return Brush;
}

const FSlateBrush* FWidgetSVGBrush::GetAtPixelSize(const UWidgetSVG* SVG, const FSlateBrush* Template, const FIntPoint& PixelSize, const SWidget* Widget)
{
	using namespace WidgetSVGBrushPrivate;

	if (!SVG || SVG->SVGSource.IsEmpty() || !FSlateApplication::IsInitialized())
	{
		Reset();
		return nullptr;
	}

	FWidgetSVGDrawingKey Key;
	Key.SVG = FObjectKey(SVG);
	Key.Revision = SVG->GetRevision();
	Key.PixelSize = FIntPoint(FMath::Clamp(PixelSize.X, 1, MaxPixelSide), FMath::Clamp(PixelSize.Y, 1, MaxPixelSide));

	// Another SVG altogether: what was shown is not a stand-in for it.
	if (Shown.IsValid() && Shown->Key.SVG != Key.SVG)
	{
		Shown.Reset();
	}

	// One drawing at a time: while the one asked for is being made, a new size waits for it (the next paint asks again).
	const bool bWantedPending = Wanted.IsValid() && !Wanted->bDone && Shown.IsValid();
	if (!Wanted.IsValid() || (!(Wanted->Key == Key) && !bWantedPending))
	{
		Wanted = FindOrStart(Key, *SVG, /*bMayDrawAtOnce=*/!Shown.IsValid());
	}

	if (Wanted->bDone)
	{
		Shown = Wanted;
	}
	else if (Widget)
	{
		Wanted->Waiters.AddUnique(ConstCastSharedRef<SWidget>(Widget->AsShared()));
	}

	if (!Shown.IsValid() || !Shown->ImageBrush.IsValid())
	{
		return nullptr;
	}

	// The drawing as the image, with the template's look.
	Composed = *Shown->ImageBrush;
	if (Template)
	{
		Composed.DrawAs = Template->DrawAs;
		Composed.Tiling = Template->Tiling;
		Composed.Mirroring = Template->Mirroring;
		Composed.Margin = Template->Margin;
		Composed.TintColor = Template->TintColor;
		Composed.OutlineSettings = Template->OutlineSettings;
		Composed.ImageSize = Template->ImageSize;
	}
	else
	{
		Composed.DrawAs = ESlateBrushDrawType::Image;
	}
	return &Composed;
}

void FWidgetSVGBrush::Reset()
{
	Shown.Reset();
	Wanted.Reset();
	Composed = FSlateBrush();
}
