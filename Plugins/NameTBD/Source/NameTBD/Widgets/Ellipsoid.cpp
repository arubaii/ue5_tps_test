#include "Ellipsoid.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

// -------------------------------------------------------------------------
// SLATE IMPLEMENTATION
// -------------------------------------------------------------------------
void SEllipsoid::Construct(const FArguments& InArgs)
{
	OuterRadiusBrush = InArgs._OuterRadiusBrush;
	InnerRadiusBrush = InArgs._InnerRadiusBrush;
	bInnerRadiusHasBrush = InArgs._bInnerRadiusHasBrush;
	OuterRadius = InArgs._OuterRadius;
	InnerRadius = InArgs._InnerRadius;
}

int32 SEllipsoid::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
                          int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FVector2D Center = LocalSize * 0.5f;

	FVector2D ActualOuter = Center * OuterRadius.Get();
	FVector2D ActualInner = Center * InnerRadius.Get();
	
	const int32 NumSegs = 128; 

	float MaxScale = 1.0f;
	if (LocalSize.X > 0 && LocalSize.Y > 0)
	{
		const FVector2D AbsoluteSize = AllottedGeometry.GetAbsoluteSize();
		MaxScale = FMath::Max(AbsoluteSize.X / LocalSize.X, AbsoluteSize.Y / LocalSize.Y);
	}
	const float FadeAmount = MaxScale > 0.0f ? (1.0f / MaxScale) : 1.0f;

	const FSlateRenderTransform& Transform = AllottedGeometry.GetAccumulatedRenderTransform();

	// Helper lambda to draw any single continuous ring or solid ellipse
	auto DrawRing = [&](const FSlateBrush* RingBrush, FVector2D RingInner, FVector2D RingOuter, int32& CurrentLayerId)
	{
		if (!RingBrush)
		{
			RingBrush = FCoreStyle::Get().GetDefaultBrush();
		}

		FLinearColor Tint = RingBrush->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint();
		FColor ColorSolid = Tint.ToFColor(true);
		FColor ColorFade = ColorSolid;
		ColorFade.A = 0;

		// Dynamic UV mapping based on proportional bounds rather than absolute pixels.
		// Note: To properly size the image, the X/Y value should be 10x the size of the X/Y radii.
		auto CalcUV = [&](const FVector2D& P)
		{
			float ScaleX = RingBrush->ImageSize.X > 0.1f ? (RingBrush->ImageSize.X / 100.0f) : 1.0f;
			float ScaleY = RingBrush->ImageSize.Y > 0.1f ? (RingBrush->ImageSize.Y / 100.0f) : 1.0f;

			float SizeX = FMath::Max(1.0f, LocalSize.X * ScaleX);
			float SizeY = FMath::Max(1.0f, LocalSize.Y * ScaleY);

			return FVector2f(
				0.5f + (P.X - Center.X) / SizeX,
				0.5f + (P.Y - Center.Y) / SizeY
			);
		};

		TArray<FSlateVertex> Verts;
		Verts.Reserve((NumSegs + 1) * 4);

		TArray<SlateIndex> Indices;
		Indices.Reserve(NumSegs * 18);

		bool bHasHole = RingInner.X > 0.1f || RingInner.Y > 0.1f;

		for (int32 i = 0; i <= NumSegs; ++i)
		{
			float Angle = (static_cast<float>(i) / NumSegs) * 2.0f * PI;
			float CosA = FMath::Cos(Angle);
			float SinA = FMath::Sin(Angle);

			FVector2D R_InnerFade(bHasHole ? FMath::Max(0.0f, RingInner.X - FadeAmount) : 0.0f, 
			                      bHasHole ? FMath::Max(0.0f, RingInner.Y - FadeAmount) : 0.0f);
			FVector2D R_InnerSolid = RingInner;
			FVector2D R_OuterSolid(FMath::Max(0.0f, RingOuter.X - FadeAmount), 
			                       FMath::Max(0.0f, RingOuter.Y - FadeAmount));
			FVector2D R_OuterFade = RingOuter + FVector2D(FadeAmount, FadeAmount);

			FVector2D P0 = Center + FVector2D(CosA * R_InnerFade.X, SinA * R_InnerFade.Y);
			FVector2D P1 = Center + FVector2D(CosA * R_InnerSolid.X, SinA * R_InnerSolid.Y);
			FVector2D P2 = Center + FVector2D(CosA * R_OuterSolid.X, SinA * R_OuterSolid.Y);
			FVector2D P3 = Center + FVector2D(CosA * R_OuterFade.X, SinA * R_OuterFade.Y);

			FColor InnerFadeColor = bHasHole ? ColorFade : ColorSolid;

			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(P0.X, P0.Y), CalcUV(P0), InnerFadeColor));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(P1.X, P1.Y), CalcUV(P1), ColorSolid));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(P2.X, P2.Y), CalcUV(P2), ColorSolid));
			Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, FVector2f(P3.X, P3.Y), CalcUV(P3), ColorFade));

			if (i < NumSegs)
			{
				int32 V0 = i * 4;
				int32 V1 = V0 + 1;
				int32 V2 = V0 + 2;
				int32 V3 = V0 + 3;
				
				int32 N0 = (i + 1) * 4;
				int32 N1 = N0 + 1;
				int32 N2 = N0 + 2;
				int32 N3 = N0 + 3;

				if (bHasHole)
				{
					Indices.Add(V0); Indices.Add(N0); Indices.Add(N1);
					Indices.Add(N1); Indices.Add(V1); Indices.Add(V0);
				}

				Indices.Add(V1); Indices.Add(N1); Indices.Add(N2);
				Indices.Add(N2); Indices.Add(V2); Indices.Add(V1);

				Indices.Add(V2); Indices.Add(N2); Indices.Add(N3);
				Indices.Add(N3); Indices.Add(V3); Indices.Add(V2);
			}
		}

		const FSlateResourceHandle& Handle = RingBrush->GetRenderingResource();

		FSlateDrawElement::MakeCustomVerts(
			OutDrawElements,
			CurrentLayerId,
			Handle,
			Verts,
			Indices,
			nullptr,
			0,
			0
		);
	};

	int32 CurrentLayer = LayerId;

	// 1. Draw the inner center fill (if enabled and if there is a hole to fill)
	if (bInnerRadiusHasBrush.Get() && (ActualInner.X > 0.1f || ActualInner.Y > 0.1f))
	{
		DrawRing(InnerRadiusBrush.Get(), FVector2D::ZeroVector, ActualInner, CurrentLayer);
		CurrentLayer++; 
	}

	// 2. Draw the outer annulus
	DrawRing(OuterRadiusBrush.Get(), ActualInner, ActualOuter, CurrentLayer);

	return CurrentLayer;
}

FVector2D SEllipsoid::ComputeDesiredSize(float) const
{
	if (const FSlateBrush* ActiveBrush = OuterRadiusBrush.Get())
	{
		return ActiveBrush->ImageSize;
	}
	return FVector2D(100.f, 100.f);
}

void SEllipsoid::SetOuterRadiusBrush(const TAttribute<const FSlateBrush*>& InBrush) { OuterRadiusBrush = InBrush; }
void SEllipsoid::SetInnerRadiusBrush(const TAttribute<const FSlateBrush*>& InBrush) { InnerRadiusBrush = InBrush; }
void SEllipsoid::SetInnerRadiusHasBrush(const TAttribute<bool>& InHasBrush) { bInnerRadiusHasBrush = InHasBrush; }
void SEllipsoid::SetOuterRadius(const TAttribute<FVector2D>& InOuterRadius) { OuterRadius = InOuterRadius; }
void SEllipsoid::SetInnerRadius(const TAttribute<FVector2D>& InInnerRadius) { InnerRadius = InInnerRadius; }

// -------------------------------------------------------------------------
// UMG IMPLEMENTATION
// -------------------------------------------------------------------------
UEllipsoid::UEllipsoid()
{
	bInnerRadiusHasBrush = false;
	OuterRadius = FVector2D(10.0f, 10.0f);
	InnerRadius = FVector2D(0.0f, 0.0f);
}

TSharedRef<SWidget> UEllipsoid::RebuildWidget()
{
	MyEllipsoid = SNew(SEllipsoid)
		.OuterRadiusBrush(&OuterRadiusBrush)
		.InnerRadiusBrush(&InnerRadiusBrush)
		.bInnerRadiusHasBrush(bInnerRadiusHasBrush)
		.OuterRadius(OuterRadius * 0.1f)
		.InnerRadius(InnerRadius * 0.1f);

	return MyEllipsoid.ToSharedRef();
}

void UEllipsoid::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (MyEllipsoid.IsValid())
	{
		MyEllipsoid->SetOuterRadiusBrush(&OuterRadiusBrush);
		MyEllipsoid->SetInnerRadiusBrush(&InnerRadiusBrush);
		MyEllipsoid->SetInnerRadiusHasBrush(bInnerRadiusHasBrush);
		MyEllipsoid->SetOuterRadius(OuterRadius * 0.1f);
		MyEllipsoid->SetInnerRadius(InnerRadius * 0.1f);
	}
}

void UEllipsoid::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyEllipsoid.Reset();
}

#if WITH_EDITOR
const FText UEllipsoid::GetPaletteCategory()
{
	return FText::FromString("AR GameSystems UI");
}
#endif