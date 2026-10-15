// Copyright AR GameSystems 2026. All rights reserved.

#include "SNotificationCard.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "NameTBD/Widgets/WidgetEffectsDraw.h"
#include "NameTBD/Widgets/WidgetMotion.h"
#include "NotificationSubsystem.h"

#define LOCTEXT_NAMESPACE "NotificationCard"

namespace NotificationCardPrivate
{
	using namespace WidgetEffectsDraw;

	/** White, fully rounded (half the height): pills and discs, tinted where drawn. */
	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* WhiteBox()
	{
		return FCoreStyle::Get().GetBrush("GenericWhiteBox");
	}

	FVector2f ToVector2f(const FVector2D& Value)
	{
		return FVector2f(static_cast<float>(Value.X), static_cast<float>(Value.Y));
	}

	/** Relative luminance (sRGB weights on linear values): what decides dark or light text on a fill. */
	float Luminance(const FLinearColor& Color)
	{
		return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
	}

	FButtonStyle MakeButtonStyle(const FLinearColor& Fill, float Radius, bool bOutlined)
	{
		FButtonStyle ButtonLook;
		const FLinearColor Hover = FLinearColor::LerpUsingHSV(Fill, FLinearColor(1.f, 1.f, 1.f, Fill.A), 0.18f);
		const FLinearColor Down = Fill * FLinearColor(0.8f, 0.8f, 0.8f, 1.f);
		if (bOutlined)
		{
			ButtonLook.SetNormal(FSlateRoundedBoxBrush(Fill, Radius, FLinearColor(1.f, 1.f, 1.f, 0.16f), 1.f));
			ButtonLook.SetHovered(FSlateRoundedBoxBrush(Hover, Radius, FLinearColor(1.f, 1.f, 1.f, 0.28f), 1.f));
			ButtonLook.SetPressed(FSlateRoundedBoxBrush(Down, Radius, FLinearColor(1.f, 1.f, 1.f, 0.2f), 1.f));
		}
		else
		{
			ButtonLook.SetNormal(FSlateRoundedBoxBrush(Fill, Radius));
			ButtonLook.SetHovered(FSlateRoundedBoxBrush(Hover, Radius));
			ButtonLook.SetPressed(FSlateRoundedBoxBrush(Down, Radius));
		}
		ButtonLook.SetDisabled(FSlateRoundedBoxBrush(Fill * FLinearColor(1.f, 1.f, 1.f, 0.4f), Radius));
		ButtonLook.SetNormalPadding(FMargin(0.f));
		ButtonLook.SetPressedPadding(FMargin(0.f));
		return ButtonLook;
	}

	/** A strip across Width from Y0 to Y1, cut where its fade at both ends changes. */
	void BuildFadeStrip(FWidgetShapeMesh& Mesh, float Width, float Y0, float Y1, float Fade)
	{
		const float Xs[4] = { 0.f, Width * Fade, Width * (1.f - Fade), Width };
		const uint32 Base = static_cast<uint32>(Mesh.Vertices.Num());
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Mesh.AddVertex(FVector2f(Xs[Index], Y0), 1.f);
			Mesh.AddVertex(FVector2f(Xs[Index], Y1), 1.f);
		}
		for (uint32 Index = 0; Index < 3; ++Index)
		{
			const uint32 A = Base + Index * 2;
			Mesh.AddTriangle(A, A + 1, A + 2);
			Mesh.AddTriangle(A + 1, A + 3, A + 2);
		}
	}

	/** 1 in the middle of Width, 0 at its ends, over Fade of it. */
	float EdgeFade(float X, float Width, float Fade)
	{
		const float Reach = FMath::Max(Width * Fade, 0.001f);
		return FMath::Clamp(FMath::Min(X, Width - X) / Reach, 0.f, 1.f);
	}

	/** A thin progress bar: a track and a pill-ended fill, eased to new values. */
	class SNotificationProgress : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SNotificationProgress) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			SetCanTick(false);
			Shown.Snap(0.f);
		}

		void SetProgress(float InValue, const FLinearColor& InColor, bool bAnimate)
		{
			FillColor = InColor;
			const float Clamped = FMath::Clamp(InValue, 0.f, 1.f);
			if (!bAnimate)
			{
				Shown.Snap(Clamped);
				Invalidate(EInvalidateWidgetReason::Paint);
				return;
			}
			Shown.SetTarget(Clamped, EWidgetMotion::Smooth, 0.35f);
			if (!Timer.IsValid())
			{
				Timer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SNotificationProgress::Animate));
			}
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(40.0, 4.0); }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
			const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), PillBrush(), ESlateDrawEffect::None,
				FLinearColor(1.f, 1.f, 1.f, 0.12f) * Tint);
			const double FillWidth = LocalSize.X * FMath::Clamp(Shown.Value, 0.f, 1.f);
			if (FillWidth > 0.5)
			{
				// Never thinner than it is tall, so the fill keeps round ends from the first percent.
				const double Drawn = FMath::Max(FillWidth, FMath::Min(LocalSize.Y, LocalSize.X));
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(Drawn, LocalSize.Y), FSlateLayoutTransform()),
					PillBrush(), ESlateDrawEffect::None, FillColor * Tint);
			}
			return LayerId + 1;
		}

	private:
		EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime)
		{
			const bool bMoving = Shown.Advance(InDeltaTime);
			Invalidate(EInvalidateWidgetReason::Paint);
			if (bMoving)
			{
				return EActiveTimerReturnType::Continue;
			}
			Timer.Reset();
			return EActiveTimerReturnType::Stop;
		}

		FWidgetMotionValue Shown;
		FLinearColor FillColor = FLinearColor::White;
		TSharedPtr<FActiveTimerHandle> Timer;
	};

	/** Fills a button from the left as its key is held. */
	class SNotificationHoldFill : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SNotificationHoldFill) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, TFunction<float()> InProgress, float InRadius)
		{
			Progress = MoveTemp(InProgress);
			FillBrush = FSlateRoundedBoxBrush(FLinearColor::White, InRadius);
			SetVisibility(EVisibility::HitTestInvisible);
		}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

		virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
			FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
		{
			const float Amount = Progress ? FMath::Clamp(Progress(), 0.f, 1.f) : 0.f;
			if (Amount <= 0.001f)
			{
				return LayerId;
			}
			const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2D(LocalSize.X * Amount, LocalSize.Y), FSlateLayoutTransform()),
				&FillBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.28f) * InWidgetStyle.GetColorAndOpacityTint());
			return LayerId;
		}

	private:
		TFunction<float()> Progress;
		FSlateRoundedBoxBrush FillBrush = FSlateRoundedBoxBrush(FLinearColor::White, 7.f);
	};
}

// =====================================================================================================
// FNotificationCardStyle
// =====================================================================================================

FNotificationCardStyle::FNotificationCardStyle()
{
	TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	ButtonFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	CountFormat = LOCTEXT("CountFormat", "×{Count}");
	InfoColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Info);
	SuccessColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Success);
	WarningColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Warning);
	ErrorColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Error);
	RewardColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Reward);
	SocialColor = NotificationUtils::GetDefaultSeverityColor(ENotificationSeverity::Social);
	AccentColor = InfoColor;
}

FNotificationCardStyle FNotificationCardStyle::Make(ENotificationCardLayout InLayout)
{
	FNotificationCardStyle Made;
	Made.Layout = InLayout;
	switch (InLayout)
	{
	case ENotificationCardLayout::Banner:
		Made.Width = 640.f;
		Made.Padding = FMargin(32.f, 14.f);
		Made.BackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.55f);
		Made.CornerRadius = 0.f;
		Made.OutlineWidth = 0.f;
		Made.ShadowColor = FLinearColor::Transparent;
		Made.IconSize = 40.f;
		Made.TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 26);
		Made.TitleColor = FLinearColor(0.97f, 0.97f, 0.98f, 1.f);
		Made.BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 15);
		Made.BodyColor = FLinearColor(0.78f, 0.8f, 0.84f, 1.f);
		Made.bShowTimerBar = false;
		Made.bShowCloseButton = false;
		break;
	case ENotificationCardLayout::Compact:
		Made.Width = 0.f;
		Made.Padding = FMargin(10.f, 6.f);
		Made.BackgroundColor = FLinearColor(0.008f, 0.01f, 0.014f, 0.72f);
		Made.CornerRadius = 8.f;
		Made.OutlineWidth = 0.f;
		Made.ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.25f);
		Made.ShadowBlur = 12.f;
		Made.ShadowOffset = FVector2D(0.0, 3.0);
		Made.Accent = ENotificationAccent::None;
		Made.IconSize = 18.f;
		Made.TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		Made.BodyFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
		Made.bShowTimerBar = false;
		Made.bShowCloseButton = false;
		break;
	default:
		break;
	}
	return Made;
}

FLinearColor FNotificationCardStyle::GetAccent(ENotificationSeverity Severity) const
{
	if (!bUseSeverityColors)
	{
		return AccentColor;
	}
	switch (Severity)
	{
	case ENotificationSeverity::Success: return SuccessColor;
	case ENotificationSeverity::Warning: return WarningColor;
	case ENotificationSeverity::Error: return ErrorColor;
	case ENotificationSeverity::Reward: return RewardColor;
	case ENotificationSeverity::Social: return SocialColor;
	default: return InfoColor;
	}
}

// =====================================================================================================
// SNotificationGlyph
// =====================================================================================================

void SNotificationGlyph::Construct(const FArguments& InArgs)
{
	GlyphSize = InArgs._DesiredSize;
	SetCanTick(false);
}

void SNotificationGlyph::SetIcon(const FNotificationIcon& InIcon, ENotificationSeverity InSeverity, const FLinearColor& InAccent, bool bInGlyphFallback)
{
	Kind = ENotificationGlyphKind::Icon;
	Icon = InIcon;
	Severity = InSeverity;
	Color = InAccent;
	bGlyphFallback = bInGlyphFallback;
	ImageBrush = FSlateBrush();
	if (Icon.ImageSource == EWidgetImageSource::Texture && Icon.Image)
	{
		ImageBrush.SetResourceObject(Icon.Image);
		ImageBrush.ImageSize = FVector2f(static_cast<float>(GlyphSize.X), static_cast<float>(GlyphSize.Y));
		ImageBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SNotificationGlyph::SetKind(ENotificationGlyphKind InKind, const FLinearColor& InColor)
{
	Kind = InKind;
	Color = InColor;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SNotificationGlyph::SetGlyphSize(const FVector2D& InSize)
{
	GlyphSize = InSize;
	ImageBrush.ImageSize = FVector2f(static_cast<float>(InSize.X), static_cast<float>(InSize.Y));
	Invalidate(EInvalidateWidgetReason::Layout);
}

bool SNotificationGlyph::HasContent() const
{
	return Kind != ENotificationGlyphKind::Icon || Icon.HasImage() || bGlyphFallback;
}

FVector2D SNotificationGlyph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return HasContent() ? GlyphSize : FVector2D::ZeroVector;
}

int32 SNotificationGlyph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace NotificationCardPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	switch (Kind)
	{
	case ENotificationGlyphKind::Close:
	{
		const float Inset = static_cast<float>(FMath::Min(LocalSize.X, LocalSize.Y)) * 0.28f;
		const FVector2f Size = ToVector2f(LocalSize);
		TArray<FVector2f> Line;
		Line.Add(FVector2f(Inset, Inset));
		Line.Add(Size - FVector2f(Inset, Inset));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Line, ESlateDrawEffect::None, Color * Tint, true, 1.5f);
		Line[0] = FVector2f(Size.X - Inset, Inset);
		Line[1] = FVector2f(Inset, Size.Y - Inset);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Line, ESlateDrawEffect::None, Color * Tint, true, 1.5f);
		return LayerId;
	}
	case ENotificationGlyphKind::AccentLine:
	{
		FWidgetShapeMesh Mesh;
		const float Width = static_cast<float>(LocalSize.X);
		BuildFadeStrip(Mesh, Width, 0.f, static_cast<float>(LocalSize.Y), 0.5f);
		const FLinearColor LineColor = Color;
		PaintMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh, Tint, [&LineColor, Width](const FWidgetShapeMeshVertex& Vertex)
		{
			FLinearColor Out = LineColor;
			Out.A *= EdgeFade(Vertex.Position.X, Width, 0.5f);
			return Out;
		});
		return LayerId;
	}
	default:
		break;
	}

	if (Icon.ImageSource == EWidgetImageSource::SVG && Icon.SVG)
	{
		if (const FSlateBrush* Brush = SVGBrush.Get(Icon.SVG, nullptr, AllottedGeometry, ToVector2f(LocalSize), *this))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry, Brush, ESlateDrawEffect::None, Icon.Tint * Tint);
		}
		return LayerId;
	}
	if (Icon.ImageSource == EWidgetImageSource::Texture && Icon.Image)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, PaintGeometry, &ImageBrush, ESlateDrawEffect::None, Icon.Tint * Tint);
		return LayerId;
	}
	if (bGlyphFallback)
	{
		PaintSeverity(AllottedGeometry, OutDrawElements, LayerId, Tint);
	}
	return LayerId;
}

void SNotificationGlyph::PaintSeverity(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint) const
{
	using namespace NotificationCardPrivate;

	// Drawn on a 24-unit grid, scaled to the glyph and centered.
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float Side = static_cast<float>(FMath::Min(LocalSize.X, LocalSize.Y));
	const float Scale = Side / 24.f;
	const FVector2f Origin = (ToVector2f(LocalSize) - FVector2f(Side, Side)) * 0.5f;
	auto At = [&Origin, Scale](float X, float Y) { return Origin + FVector2f(X, Y) * Scale; };

	FLinearColor Disc = Color;
	Disc.A *= 0.18f;
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(FVector2D(Side, Side), FSlateLayoutTransform(FVector2D(Origin.X, Origin.Y))),
		PillBrush(), ESlateDrawEffect::None, Disc * Tint);

	const int32 GlyphLayer = LayerId + 1;
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();
	const FLinearColor Ink = Color * Tint;
	const float Thickness = FMath::Max(1.6f * Scale, 1.f);
	auto Lines = [&](std::initializer_list<FVector2f> Points)
	{
		TArray<FVector2f> Line(Points);
		FSlateDrawElement::MakeLines(OutDrawElements, GlyphLayer, PaintGeometry, Line, ESlateDrawEffect::None, Ink, true, Thickness);
	};
	auto Dot = [&](float X, float Y, float Diameter)
	{
		const FVector2f Middle = At(X, Y);
		const float Size = Diameter * Scale;
		FSlateDrawElement::MakeBox(OutDrawElements, GlyphLayer, AllottedGeometry.ToPaintGeometry(FVector2D(Size, Size), FSlateLayoutTransform(FVector2D(Middle.X - Size * 0.5f, Middle.Y - Size * 0.5f))),
			PillBrush(), ESlateDrawEffect::None, Ink);
	};

	switch (Severity)
	{
	case ENotificationSeverity::Success:
		Lines({ At(7.5f, 12.5f), At(10.5f, 15.5f), At(16.5f, 8.8f) });
		break;
	case ENotificationSeverity::Warning:
		Lines({ At(12.f, 5.8f), At(18.6f, 17.4f), At(5.4f, 17.4f), At(12.f, 5.8f) });
		Lines({ At(12.f, 9.8f), At(12.f, 13.4f) });
		Dot(12.f, 15.5f, 1.9f);
		break;
	case ENotificationSeverity::Error:
		Lines({ At(8.6f, 8.6f), At(15.4f, 15.4f) });
		Lines({ At(15.4f, 8.6f), At(8.6f, 15.4f) });
		break;
	case ENotificationSeverity::Reward:
	{
		TArray<FVector2f> Star;
		for (int32 Point = 0; Point <= 10; ++Point)
		{
			const float Angle = -HALF_PI + PI * 0.2f * static_cast<float>(Point);
			const float Reach = (Point % 2 == 0) ? 6.6f : 2.8f;
			Star.Add(At(12.f + FMath::Cos(Angle) * Reach, 12.6f + FMath::Sin(Angle) * Reach));
		}
		FSlateDrawElement::MakeLines(OutDrawElements, GlyphLayer, PaintGeometry, Star, ESlateDrawEffect::None, Ink, true, Thickness);
		break;
	}
	case ENotificationSeverity::Social:
		Lines({ At(6.5f, 7.f), At(17.5f, 7.f), At(17.5f, 14.5f), At(11.5f, 14.5f), At(8.2f, 17.5f), At(8.6f, 14.5f), At(6.5f, 14.5f), At(6.5f, 7.f) });
		break;
	default:
		Dot(12.f, 7.6f, 2.2f);
		Lines({ At(12.f, 10.6f), At(12.f, 17.f) });
		break;
	}
}

// =====================================================================================================
// SNotificationCard
// =====================================================================================================

void SNotificationCard::Construct(const FArguments& InArgs, const FNotificationCardStyle& InStyle, const FNotificationView& InView, FNotificationCardHooks InHooks)
{
	bHistory = InArgs._bHistory;
	CardStyle = InStyle;
	View = InView;
	Hooks = MoveTemp(InHooks);
	SetCanTick(false);
	bShortcutShown = Hooks.IsShortcutTarget && Hooks.IsShortcutTarget();
	RebuildBrushes();
	Rebuild();
	AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SNotificationCard::Animate));
}

void SNotificationCard::SetView(const FNotificationView& InView, bool bStacked)
{
	const bool bActionsChanged = InView.Actions.Num() != View.Actions.Num() || InView.Severity != View.Severity;
	View = InView;
	if (bStacked)
	{
		BumpAge = 0.f;
		if (!AnimateTimer.IsValid())
		{
			AnimateTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SNotificationCard::Animate));
		}
	}
	if (bActionsChanged)
	{
		RebuildBrushes();
	}
	RefreshContent();
}

void SNotificationCard::SetCardStyle(const FNotificationCardStyle& InStyle)
{
	CardStyle = InStyle;
	ShadowHash = 0;
	GlowHash = 0;
	RebuildBrushes();
	Rebuild();
}

void SNotificationCard::RefreshHints()
{
	bShortcutShown = Hooks.IsShortcutTarget && Hooks.IsShortcutTarget();
	if (ActionRow.IsValid())
	{
		ActionRow->ClearChildren();
		if (ShowsActions())
		{
			for (const FNotificationAction& Action : View.Actions)
			{
				ActionRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)[MakeActionButton(Action)];
			}
		}
		ActionRow->SetVisibility(ShowsActions() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	}
}

FLinearColor SNotificationCard::GetAccent() const
{
	return CardStyle.GetAccent(View.Severity);
}

bool SNotificationCard::ShowsActions() const
{
	return !bHistory && View.Actions.Num() > 0;
}

bool SNotificationCard::ShowsClose() const
{
	return !bHistory && CardStyle.bShowCloseButton && View.bCanDismiss && CardStyle.Layout == ENotificationCardLayout::Toast;
}

bool SNotificationCard::ShowsTimer() const
{
	return !bHistory && CardStyle.bShowTimerBar && View.Duration > 0.f && Hooks.TimeFraction;
}

void SNotificationCard::RebuildBrushes()
{
	const float Radius = CardStyle.CornerRadius;
	FillBrush = FSlateRoundedBoxBrush(FLinearColor::White, Radius);
	OutlineBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, Radius, CardStyle.OutlineColor, CardStyle.OutlineWidth);
	AccentBrush = FSlateRoundedBoxBrush(FLinearColor::White, CardStyle.AccentWidth * 0.5f);
	KeyCapBrush = FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f), 4.f, FLinearColor(1.f, 1.f, 1.f, 0.3f), 1.f);

	const FLinearColor Accent = GetAccent();
	PrimaryButton = NotificationCardPrivate::MakeButtonStyle(Accent, CardStyle.ButtonCornerRadius, false);
	SecondaryButton = NotificationCardPrivate::MakeButtonStyle(CardStyle.ButtonColor, CardStyle.ButtonCornerRadius, true);
	DestructiveButton = NotificationCardPrivate::MakeButtonStyle(CardStyle.ErrorColor, CardStyle.ButtonCornerRadius, false);
	CloseButton = NotificationCardPrivate::MakeButtonStyle(FLinearColor::Transparent, 6.f, false);
	CloseButton.SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.1f), 6.f));
	CloseButton.SetPressed(FSlateRoundedBoxBrush(FLinearColor(1.f, 1.f, 1.f, 0.16f), 6.f));
}

TSharedRef<SWidget> SNotificationCard::MakeCloseButton()
{
	TWeakPtr<SNotificationCard> WeakCard = StaticCastSharedRef<SNotificationCard>(AsShared());
	TSharedRef<SNotificationGlyph> Cross = SNew(SNotificationGlyph).DesiredSize(FVector2D(12.0, 12.0));
	Cross->SetKind(ENotificationGlyphKind::Close, CardStyle.BodyColor);
	return SNew(SButton)
		.ButtonStyle(&CloseButton)
		.IsFocusable(false)
		.ContentPadding(FMargin(5.f))
		.ToolTipText(LOCTEXT("Close", "Close"))
		.Visibility_Lambda([WeakCard]()
		{
			const TSharedPtr<SNotificationCard> Card = WeakCard.Pin();
			return Card.IsValid() && Card->ShowsClose() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		.OnClicked_Lambda([WeakCard]()
		{
			if (const TSharedPtr<SNotificationCard> Card = WeakCard.Pin())
			{
				Card->Hooks.OnClose.ExecuteIfBound();
			}
			return FReply::Handled();
		})
		[
			Cross
		];
}

TSharedRef<SWidget> SNotificationCard::MakeActionButton(const FNotificationAction& Action)
{
	using namespace NotificationCardPrivate;

	const FButtonStyle* ButtonLook = &PrimaryButton;
	FLinearColor Fill = GetAccent();
	if (Action.Style == ENotificationActionStyle::Secondary)
	{
		ButtonLook = &SecondaryButton;
		Fill = CardStyle.ButtonColor;
	}
	else if (Action.Style == ENotificationActionStyle::Destructive)
	{
		ButtonLook = &DestructiveButton;
		Fill = CardStyle.ErrorColor;
	}
	// Text that reads on its fill: dark on a light accent (a gold Accept), the button text color otherwise.
	const FLinearColor TextColor = (Fill.A > 0.5f && Luminance(Fill) > 0.45f) ? FLinearColor(0.02f, 0.02f, 0.03f, 1.f) : CardStyle.ButtonTextColor;

	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox);
	if (bShortcutShown && CardStyle.bShowKeyHints)
	{
		const FKey Key = Hooks.DisplayKey ? Hooks.DisplayKey(Action) : (Action.KeyboardKey.IsValid() ? Action.KeyboardKey : Action.GamepadKey);
		if (Key.IsValid())
		{
			const FNotificationKeyGlyph Glyph = UNotificationSubsystem::GetKeyGlyph(Key);
			if (Glyph.Image.HasImage())
			{
				TSharedRef<SNotificationGlyph> KeyImage = SNew(SNotificationGlyph).DesiredSize(FVector2D(18.0, 18.0));
				KeyImage->SetIcon(Glyph.Image, ENotificationSeverity::Info, FLinearColor::White, false);
				Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)[KeyImage];
			}
			else
			{
				FSlateFontInfo KeyFont = CardStyle.ButtonFont;
				KeyFont.Size = FMath::Max(KeyFont.Size - 2.f, 7.f);
				Content->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
				[
					SNew(SBorder)
					.BorderImage(&KeyCapBrush)
					.Padding(FMargin(5.f, 1.f))
					[
						SNew(STextBlock).Text(Glyph.Label).Font(KeyFont).ColorAndOpacity(TextColor)
					]
				];
			}
		}
	}
	Content->AddSlot().AutoWidth().VAlign(VAlign_Center)
	[
		SNew(STextBlock).Text(Action.Label).Font(CardStyle.ButtonFont).ColorAndOpacity(TextColor)
	];

	const FName ActionId = Action.ActionId;
	TWeakPtr<SNotificationCard> WeakCard = StaticCastSharedRef<SNotificationCard>(AsShared());
	TFunction<float()> HoldGetter = [WeakCard, ActionId]()
	{
		const TSharedPtr<SNotificationCard> Card = WeakCard.Pin();
		return Card.IsValid() && Card->Hooks.HoldProgress ? Card->Hooks.HoldProgress(ActionId) : 0.f;
	};

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SButton)
			.ButtonStyle(ButtonLook)
			.IsFocusable(false)
			.ContentPadding(FMargin(12.f, 6.f))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([WeakCard, ActionId]()
			{
				if (const TSharedPtr<SNotificationCard> Card = WeakCard.Pin())
				{
					Card->Hooks.OnAction.ExecuteIfBound(ActionId);
				}
				return FReply::Handled();
			})
			[
				Content
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SNotificationHoldFill, MoveTemp(HoldGetter), CardStyle.ButtonCornerRadius)
		];
}

TSharedRef<SWidget> SNotificationCard::MakeActionRow()
{
	ActionRow = SNew(SHorizontalBox);
	RefreshHints();
	return ActionRow.ToSharedRef();
}

void SNotificationCard::Rebuild()
{
	using namespace NotificationCardPrivate;

	const ENotificationCardLayout CardLayout = CardStyle.Layout;
	const bool bBanner = CardLayout == ENotificationCardLayout::Banner;
	const bool bCompact = CardLayout == ENotificationCardLayout::Compact;
	const float IconSide = bBanner ? CardStyle.IconSize * 1.5f : CardStyle.IconSize;

	IconGlyph = SNew(SNotificationGlyph).DesiredSize(FVector2D(IconSide, IconSide));
	TitleBlock = SNew(STextBlock)
		.Font(CardStyle.TitleFont)
		.ColorAndOpacity(CardStyle.TitleColor)
		.AutoWrapText(!bCompact && !bBanner)
		.Justification(bBanner ? ETextJustify::Center : ETextJustify::Left);
	BodyBlock = SNew(STextBlock)
		.Font(CardStyle.BodyFont)
		.ColorAndOpacity(CardStyle.BodyColor)
		.AutoWrapText(!bCompact && !bBanner)
		.WrapTextAt(bBanner && CardStyle.Width > 0.f ? CardStyle.Width * 0.8f : 0.f)
		.Justification(bBanner ? ETextJustify::Center : ETextJustify::Left);
	CountBlock = SNew(STextBlock).Font(CardStyle.TitleFont).RenderTransformPivot(FVector2D(0.5, 0.5));
	FSlateFontInfo StampFont = CardStyle.BodyFont;
	StampFont.Size = FMath::Max(StampFont.Size - 2.f, 7.f);
	TimestampBlock = SNew(STextBlock).Font(StampFont).ColorAndOpacity(CardStyle.BodyColor * FLinearColor(1.f, 1.f, 1.f, 0.7f));
	ProgressLine = SNew(SNotificationProgress);

	TSharedPtr<SWidget> Body;
	if (bBanner)
	{
		TSharedRef<SNotificationGlyph> LeftLine = SNew(SNotificationGlyph).DesiredSize(FVector2D(56.0, 2.0));
		TSharedRef<SNotificationGlyph> RightLine = SNew(SNotificationGlyph).DesiredSize(FVector2D(56.0, 2.0));
		const bool bLines = CardStyle.Accent == ENotificationAccent::Stripe;
		LeftLine->SetKind(ENotificationGlyphKind::AccentLine, GetAccent());
		RightLine->SetKind(ENotificationGlyphKind::AccentLine, GetAccent());
		LeftLine->SetVisibility(bLines ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		RightLine->SetVisibility(bLines ? EVisibility::HitTestInvisible : EVisibility::Collapsed);

		Body = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 6.f)[IconGlyph.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 16.f, 0.f)[LeftLine]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[TitleBlock.ToSharedRef()]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10.f, 0.f, 0.f, 0.f)[CountBlock.ToSharedRef()]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)[RightLine]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 4.f, 0.f, 0.f)[BodyBlock.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 2.f, 0.f, 0.f)[TimestampBlock.ToSharedRef()]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f, 0.f, 0.f)
			[
				SAssignNew(ProgressBox, SBox).WidthOverride(260.f).HeightOverride(4.f)[ProgressLine.ToSharedRef()]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 10.f, 0.f, 0.f)[MakeActionRow()];
	}
	else if (bCompact)
	{
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)[IconGlyph.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[TitleBlock.ToSharedRef()]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)[BodyBlock.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)[CountBlock.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(ProgressBox, SBox).WidthOverride(56.f).HeightOverride(3.f)[ProgressLine.ToSharedRef()]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)[TimestampBlock.ToSharedRef()]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)[MakeActionRow()];
	}
	else
	{
		Body = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 0.f, 12.f, 0.f)[IconGlyph.ToSharedRef()]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[TitleBlock.ToSharedRef()]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)[CountBlock.ToSharedRef()]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f, 0.f, 0.f)[TimestampBlock.ToSharedRef()]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)[BodyBlock.ToSharedRef()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					SAssignNew(ProgressBox, SBox).HeightOverride(4.f)[ProgressLine.ToSharedRef()]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)[MakeActionRow()]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(10.f, -2.f, -6.f, 0.f)[MakeCloseButton()];
	}

	// Room for the accent stripe inside the left padding.
	FMargin Padding = CardStyle.Padding;
	if (!bBanner && CardStyle.Accent == ENotificationAccent::Stripe)
	{
		Padding.Left += CardStyle.AccentWidth + 2.f;
	}
	if (!bBanner && CardStyle.Accent == ENotificationAccent::TopLine)
	{
		Padding.Top += CardStyle.AccentWidth * 0.5f;
	}

	TSharedRef<SWidget> Padded = SNew(SBox)
		.Padding(Padding)
		.WidthOverride(CardStyle.Width > 0.f ? FOptionalSize(CardStyle.Width) : FOptionalSize())
		[
			Body.ToSharedRef()
		];

	if (CardStyle.BackgroundBlur > 0.f && !bBanner)
	{
		const float Radius = CardStyle.CornerRadius;
		ChildSlot
		[
			SNew(SBackgroundBlur)
			.BlurStrength(CardStyle.BackgroundBlur)
			.CornerRadius(FVector4(Radius, Radius, Radius, Radius))
			.Padding(FMargin(0.f))
			[
				Padded
			]
		];
	}
	else
	{
		ChildSlot
		[
			Padded
		];
	}

	RefreshContent();
}

FText SNotificationCard::GetCountText() const
{
	if (View.Count < 2)
	{
		return FText::GetEmpty();
	}
	FFormatNamedArguments Args;
	Args.Add(TEXT("Count"), FText::AsNumber(View.Count));
	return FText::Format(FTextFormat(CardStyle.CountFormat), Args);
}

FText SNotificationCard::GetTimestampText() const
{
	if (!bHistory || !CardStyle.bShowTimestamp || View.PostedAt.GetTicks() == 0)
	{
		return FText::GetEmpty();
	}
	const FTimespan Age = FDateTime::UtcNow() - View.PostedAt;
	if (Age.GetTotalMinutes() < 1.0)
	{
		return LOCTEXT("JustNow", "Just now");
	}
	if (Age.GetTotalHours() < 1.0)
	{
		return FText::Format(LOCTEXT("MinutesAgo", "{0} min ago"), FText::AsNumber(FMath::FloorToInt(Age.GetTotalMinutes())));
	}
	if (Age.GetTotalDays() < 1.0)
	{
		return FText::Format(LOCTEXT("HoursAgo", "{0} h ago"), FText::AsNumber(FMath::FloorToInt(Age.GetTotalHours())));
	}
	return FText::AsDate(View.PostedAt, EDateTimeStyle::Short, FText::GetInvariantTimeZone());
}

void SNotificationCard::RefreshContent()
{
	using namespace NotificationCardPrivate;

	const FLinearColor Accent = GetAccent();
	IconGlyph->SetIcon(View.Icon, View.Severity, Accent, CardStyle.bSeverityGlyph);
	IconGlyph->SetVisibility(IconGlyph->HasContent() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);

	TitleBlock->SetText(View.Title);
	TitleBlock->SetVisibility(View.Title.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	BodyBlock->SetText(View.Body);
	BodyBlock->SetVisibility(View.Body.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);

	const FText Count = GetCountText();
	CountBlock->SetText(Count);
	CountBlock->SetColorAndOpacity(Accent);
	CountBlock->SetVisibility(Count.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);

	const FText Stamp = GetTimestampText();
	TimestampBlock->SetText(Stamp);
	TimestampBlock->SetVisibility(Stamp.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible);

	const TSharedPtr<SNotificationProgress> Progress = StaticCastSharedPtr<SNotificationProgress>(ProgressLine);
	Progress->SetProgress(View.Progress, Accent, /*bAnimate=*/true);
	ProgressBox->SetVisibility(View.HasProgress() ? EVisibility::HitTestInvisible : EVisibility::Collapsed);

	RefreshHints();
	Invalidate(EInvalidateWidgetReason::Layout);
}

EActiveTimerReturnType SNotificationCard::Animate(double InCurrentTime, float InDeltaTime)
{
	BumpAge += InDeltaTime;
	if (CountBlock.IsValid())
	{
		const float Pop = BumpAge < 0.3f ? 1.f + 0.35f * FMath::Square(1.f - BumpAge / 0.3f) : 1.f;
		CountBlock->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FScale2f(Pop))));
	}
	Invalidate(EInvalidateWidgetReason::Paint);

	// A history card has no clock to show: it only runs for a pop.
	if (bHistory && BumpAge > 0.5f)
	{
		AnimateTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void SNotificationCard::PaintSoftBox(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint,
	const FLinearColor& BoxColor, float Blur, const FVector2f& Offset, FWidgetShapeMesh& Mesh, uint32& Hash) const
{
	using namespace WidgetEffectsDraw;

	const FVector2f Size = NotificationCardPrivate::ToVector2f(AllottedGeometry.GetLocalSize());
	if (Size.X <= 1.f || Size.Y <= 1.f || BoxColor.A <= 0.001f)
	{
		return;
	}
	const float PixelScale = QuantizeScale(GetPixelScale(AllottedGeometry));
	const float AntiAlias = 1.f / PixelScale;

	FSoftBox Box;
	Box.Center = Size * 0.5f + Offset;
	Box.HalfSize = Size * 0.5f;
	const float Radius = FMath::Min(CardStyle.CornerRadius, FMath::Min(Box.HalfSize.X, Box.HalfSize.Y));
	Box.Radii = FVector4f(Radius, Radius, Radius, Radius);
	Box.Sigma = FMath::Max(Blur, 0.f) * 0.5f;

	uint32 NewHash = GetTypeHash(Size);
	NewHash = HashCombine(NewHash, GetTypeHash(Offset));
	NewHash = HashCombine(NewHash, GetTypeHash(Box.Sigma));
	NewHash = HashCombine(NewHash, GetTypeHash(Radius));
	NewHash = HashCombine(NewHash, GetTypeHash(PixelScale));
	if (NewHash != Hash || Mesh.IsEmpty())
	{
		Hash = NewHash;
		Mesh.Reset();
		TArray<float> Distances;
		const float Deepest = FMath::Min(Box.HalfSize.X, Box.HalfSize.Y) * 0.999f;
		MakeRingDistances(-Deepest, 3.2f * Box.Sigma + AntiAlias, FMath::Max(Box.Sigma * 0.75f, 2.f * AntiAlias), 0.f, 2.2f * Box.Sigma,
			FMath::Max(Box.Sigma * 0.35f, AntiAlias), 32, Distances);
		BuildRings(Box.Center, Box.HalfSize, Box.Radii, Distances, FMath::Max(Box.Sigma, 4.f * AntiAlias), PixelScale, true, Mesh);
		for (FWidgetShapeMeshVertex& Vertex : Mesh.Vertices)
		{
			Vertex.Coverage = EvaluateSoftBox(Box, Vertex.Position, AntiAlias);
		}
	}
	PaintMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh, Tint, [&BoxColor](const FWidgetShapeMeshVertex&) { return BoxColor; });
}

void SNotificationCard::PaintBand(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint) const
{
	using namespace NotificationCardPrivate;

	const FVector2f Size = ToVector2f(AllottedGeometry.GetLocalSize());
	const float Fade = 0.22f;
	FWidgetShapeMesh Mesh;
	BuildFadeStrip(Mesh, Size.X, 0.f, Size.Y, Fade);
	const FLinearColor Band = CardStyle.BackgroundColor;
	const float Width = Size.X;
	PaintMesh(OutDrawElements, LayerId, AllottedGeometry, Mesh, Tint, [&Band, Width, Fade](const FWidgetShapeMeshVertex& Vertex)
	{
		FLinearColor Out = Band;
		Out.A *= EdgeFade(Vertex.Position.X, Width, Fade);
		return Out;
	});

	// Hairlines along the top and bottom of the band, in the accent, fading the same way.
	if (CardStyle.Accent == ENotificationAccent::TopLine || CardStyle.Accent == ENotificationAccent::Glow)
	{
		FWidgetShapeMesh Lines;
		const float Thickness = FMath::Max(CardStyle.AccentWidth * 0.5f, 1.f);
		BuildFadeStrip(Lines, Size.X, 0.f, Thickness, Fade * 1.5f);
		BuildFadeStrip(Lines, Size.X, Size.Y - Thickness, Size.Y, Fade * 1.5f);
		const FLinearColor Accent = GetAccent();
		PaintMesh(OutDrawElements, LayerId + 1, AllottedGeometry, Lines, Tint, [&Accent, Width, Fade](const FWidgetShapeMeshVertex& Vertex)
		{
			FLinearColor Out = Accent;
			Out.A *= EdgeFade(Vertex.Position.X, Width, Fade * 1.5f);
			return Out;
		});
	}
}

int32 SNotificationCard::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace NotificationCardPrivate;

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor Accent = GetAccent();
	const float Flash = BumpAge < 0.45f ? 1.f - BumpAge / 0.45f : 0.f;
	int32 Layer = LayerId;

	if (CardStyle.Layout == ENotificationCardLayout::Banner)
	{
		PaintBand(AllottedGeometry, OutDrawElements, Layer, Tint);
		Layer += 2;
	}
	else
	{
		PaintSoftBox(AllottedGeometry, OutDrawElements, Layer, Tint, CardStyle.ShadowColor, CardStyle.ShadowBlur, ToVector2f(CardStyle.ShadowOffset), ShadowMesh, ShadowHash);
		if (CardStyle.Accent == ENotificationAccent::Glow || Flash > 0.f)
		{
			FLinearColor Glow = Accent;
			Glow.A = (CardStyle.Accent == ENotificationAccent::Glow ? 0.35f : 0.f) + 0.35f * Flash;
			PaintSoftBox(AllottedGeometry, OutDrawElements, Layer, Tint, Glow, 22.f, FVector2f::ZeroVector, GlowMesh, GlowHash);
		}
		++Layer;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), &FillBrush, ESlateDrawEffect::None, CardStyle.BackgroundColor * Tint);
		if (Flash > 0.f)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), &FillBrush, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.07f * Flash) * Tint);
		}
		if (CardStyle.OutlineWidth > 0.f)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), &OutlineBrush, ESlateDrawEffect::None, Tint);
		}

		const float Inset = FMath::Min(CardStyle.CornerRadius, static_cast<float>(LocalSize.Y) * 0.5f);
		if (CardStyle.Accent == ENotificationAccent::Stripe)
		{
			const float Left = FMath::Max(CardStyle.Padding.Left * 0.5f - CardStyle.AccentWidth * 0.5f, 3.f);
			const float Top = FMath::Max(Inset * 0.6f, 6.f);
			const float Height = FMath::Max(static_cast<float>(LocalSize.Y) - Top * 2.f, CardStyle.AccentWidth);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2D(CardStyle.AccentWidth, Height), FSlateLayoutTransform(FVector2D(Left, Top))),
				&AccentBrush, ESlateDrawEffect::None, Accent * Tint);
		}
		else if (CardStyle.Accent == ENotificationAccent::TopLine)
		{
			const float Width = FMath::Max(static_cast<float>(LocalSize.X) - Inset * 2.f, 0.f);
			FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2D(Width, CardStyle.AccentWidth), FSlateLayoutTransform(FVector2D(Inset, 0.0))),
				&AccentBrush, ESlateDrawEffect::None, Accent * Tint);
		}
	}

	Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Layer + 1, InWidgetStyle, bParentEnabled);

	if (ShowsTimer())
	{
		const float Remaining = FMath::Clamp(Hooks.TimeFraction(), 0.f, 1.f);
		const float Inset = FMath::Min(CardStyle.CornerRadius * 0.7f, static_cast<float>(LocalSize.X) * 0.25f);
		const float Full = FMath::Max(static_cast<float>(LocalSize.X) - Inset * 2.f, 0.f);
		const float Height = CardStyle.TimerBarHeight;
		FLinearColor BarColor = Accent;
		BarColor.A *= 0.75f;
		FSlateDrawElement::MakeBox(OutDrawElements, ++Layer,
			AllottedGeometry.ToPaintGeometry(FVector2D(Full * Remaining, Height), FSlateLayoutTransform(FVector2D(Inset, LocalSize.Y - Height - 1.0))),
			WhiteBox(), ESlateDrawEffect::None, BarColor * Tint);
	}
	return Layer;
}

#undef LOCTEXT_NAMESPACE
