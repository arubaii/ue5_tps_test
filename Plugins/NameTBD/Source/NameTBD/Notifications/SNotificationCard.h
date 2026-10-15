// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "NameTBD/Widgets/WidgetSVGBrush.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeGeometry.h"
#include "NotificationTypes.h"

#include "SNotificationCard.generated.h"

class STextBlock;
class SHorizontalBox;

/** What the built-in card looks like. */
UENUM(BlueprintType)
enum class ENotificationCardLayout : uint8
{
	Toast UMETA(ToolTip = "A card: image, title, body, count, progress, buttons, close button, timer bar. Messages, achievements, invites."),
	Banner UMETA(ToolTip = "A wide band across the screen, fading at its ends, the title large in the middle between two accent lines. Objective updated, level up, area discovered."),
	Compact UMETA(ToolTip = "One short line in a small pill: image, title, body, count. Loot, XP and kill feeds.")
};

/** Where the card shows its severity's color. */
UENUM(BlueprintType)
enum class ENotificationAccent : uint8
{
	Stripe UMETA(ToolTip = "A bar down the left edge (a banner: lines beside the title)."),
	TopLine UMETA(ToolTip = "A line along the top edge."),
	Glow UMETA(ToolTip = "A soft glow round the card in the accent color."),
	None UMETA(ToolTip = "Only the glyph and the count are in the accent color.")
};

/** How the built-in notification card looks (a Notification Host's Card Style). */
USTRUCT(BlueprintType)
struct NAMETBD_API FNotificationCardStyle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	ENotificationCardLayout Layout = ENotificationCardLayout::Toast;

	/** Its width. 0: as wide as its content asks (up to the host). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0", UIMax = "1200", ForceUnits = "px"))
	float Width = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout")
	FMargin Padding = FMargin(16.f, 12.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	FLinearColor BackgroundColor = FLinearColor(0.012f, 0.014f, 0.02f, 0.88f);

	/** Blurs what is behind the card (Slate's Background Blur), for a glass card. 0: no blur. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (ClampMin = "0", UIMax = "50"))
	float BackgroundBlur = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (ClampMin = "0", UIMax = "40", ForceUnits = "px"))
	float CornerRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (ClampMin = "0", UIMax = "4", ForceUnits = "px"))
	float OutlineWidth = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (EditCondition = "OutlineWidth > 0", EditConditionHides))
	FLinearColor OutlineColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	/** A soft shadow under the card. Alpha 0: none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	FLinearColor ShadowColor = FLinearColor(0.f, 0.f, 0.f, 0.45f);

	/** The shadow's blur, as CSS (twice the Gaussian's sigma). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background", meta = (ClampMin = "0", UIMax = "64", ForceUnits = "px"))
	float ShadowBlur = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Background")
	FVector2D ShadowOffset = FVector2D(0.0, 8.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent")
	ENotificationAccent Accent = ENotificationAccent::Stripe;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (ClampMin = "1", UIMax = "8", ForceUnits = "px"))
	float AccentWidth = 3.f;

	/** The accent follows the severity (the colors below). Off: always Accent Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent")
	bool bUseSeverityColors = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "!bUseSeverityColors", EditConditionHides))
	FLinearColor AccentColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor InfoColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor SuccessColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor WarningColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor ErrorColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor RewardColor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Accent", meta = (EditCondition = "bUseSeverityColors", EditConditionHides))
	FLinearColor SocialColor;

	/** The image's size (a banner draws it half as big again). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (ClampMin = "8", UIMax = "128", ForceUnits = "px"))
	float IconSize = 32.f;

	/** With no image, a glyph for the severity (i, check, !, cross, star, bubble) on a tinted disc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bSeverityGlyph = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FSlateFontInfo TitleFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FLinearColor TitleColor = FLinearColor(0.92f, 0.93f, 0.95f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FSlateFontInfo BodyFont;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FLinearColor BodyColor = FLinearColor(0.62f, 0.64f, 0.68f, 1.f);

	/** How the count is written, from 2 up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	FText CountFormat;

	/** A thin bar along the bottom that empties as its time runs out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bShowTimerBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (EditCondition = "bShowTimerBar", EditConditionHides, ClampMin = "1", UIMax = "6", ForceUnits = "px"))
	float TimerBarHeight = 2.f;

	/** A close button (toasts the player may dismiss). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bShowCloseButton = true;

	/** In a history host: when it came ("5 min ago"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bShowTimestamp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FSlateFontInfo ButtonFont;

	/** A Secondary action's fill (Primary takes the accent, Destructive the error color). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	FLinearColor ButtonTextColor = FLinearColor(0.95f, 0.96f, 0.98f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons", meta = (ClampMin = "0", UIMax = "20", ForceUnits = "px"))
	float ButtonCornerRadius = 7.f;

	/** The action keys shown on the buttons of the notification that holds them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buttons")
	bool bShowKeyHints = true;

	FNotificationCardStyle();

	/** A starting look for Layout. */
	static FNotificationCardStyle Make(ENotificationCardLayout InLayout);

	FLinearColor GetAccent(ENotificationSeverity Severity) const;
};

DECLARE_DELEGATE_OneParam(FOnNotificationCardAction, FName /*ActionId*/);

/** What a card asks the notification system while it is shown. Every part is optional (the designer preview has none). */
struct FNotificationCardHooks
{
	/** What is left of its time, 1 to 0. */
	TFunction<float()> TimeFraction;

	/** How far a held key has got on an action, 0 to 1. */
	TFunction<float(FName)> HoldProgress;

	/** It holds the action keys now (its key hints show). */
	TFunction<bool()> IsShortcutTarget;

	/** The key an action shows. */
	TFunction<FKey(const FNotificationAction&)> DisplayKey;

	FOnNotificationCardAction OnAction;
	FSimpleDelegate OnClose;
};

/** What a glyph leaf draws. */
enum class ENotificationGlyphKind : uint8
{
	/** The icon's image; with none, the severity glyph (or nothing). */
	Icon,
	/** A cross (the close button). */
	Close,
	/** A line fading at both ends (beside a banner's title). */
	AccentLine
};

/** Draws a notification's image (texture, material, SVG), a severity glyph, a close cross or an accent line. */
class NAMETBD_API SNotificationGlyph : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SNotificationGlyph) : _DesiredSize(FVector2D(24.0, 24.0)) {}
		SLATE_ARGUMENT(FVector2D, DesiredSize)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetIcon(const FNotificationIcon& InIcon, ENotificationSeverity InSeverity, const FLinearColor& InAccent, bool bInGlyphFallback);
	void SetKind(ENotificationGlyphKind InKind, const FLinearColor& InColor);
	void SetGlyphSize(const FVector2D& InSize);

	/** Draws something (an image, or a glyph). */
	bool HasContent() const;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void PaintSeverity(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint) const;

	ENotificationGlyphKind Kind = ENotificationGlyphKind::Icon;
	FNotificationIcon Icon;
	ENotificationSeverity Severity = ENotificationSeverity::Info;
	FLinearColor Color = FLinearColor::White;
	bool bGlyphFallback = true;
	FVector2D GlyphSize = FVector2D(24.0, 24.0);
	FSlateBrush ImageBrush;
	mutable FWidgetSVGBrush SVGBrush;
};

/**
 * The built-in look of a notification: a card (Toast), a band (Banner) or a pill (Compact), all vector-drawn - soft shadow,
 * rounded background, accent, severity glyph or image, title, body, count (popping as it grows), progress, action buttons
 * with key hints and hold fills, close button, and a timer bar. Never takes keyboard focus: actions are taken with the mouse
 * or their keys.
 */
class NAMETBD_API SNotificationCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNotificationCard) : _bHistory(false) {}
		/** Shown in a history: no timer bar, no actions, a timestamp. */
		SLATE_ARGUMENT(bool, bHistory)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FNotificationCardStyle& InStyle, const FNotificationView& InView, FNotificationCardHooks InHooks);

	/** New content. bStacked: one merged in - the count pops and the card flashes. */
	void SetView(const FNotificationView& InView, bool bStacked);
	void SetCardStyle(const FNotificationCardStyle& InStyle);

	/** Rebuilds the action row (the key hints follow who holds the keys and the device used). */
	void RefreshHints();

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	void Rebuild();
	void RefreshContent();
	void RebuildBrushes();
	TSharedRef<SWidget> MakeActionRow();
	TSharedRef<SWidget> MakeActionButton(const FNotificationAction& Action);
	TSharedRef<SWidget> MakeCloseButton();
	EActiveTimerReturnType Animate(double InCurrentTime, float InDeltaTime);
	FLinearColor GetAccent() const;
	FText GetCountText() const;
	FText GetTimestampText() const;
	bool ShowsActions() const;
	bool ShowsClose() const;
	bool ShowsTimer() const;
	/** A soft box under the card (Blur as CSS, Offset), cached in Mesh by Hash. */
	void PaintSoftBox(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint,
		const FLinearColor& BoxColor, float Blur, const FVector2f& Offset, FWidgetShapeMesh& Mesh, uint32& Hash) const;
	void PaintBand(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FLinearColor& Tint) const;

	FNotificationCardStyle CardStyle;
	FNotificationView View;
	FNotificationCardHooks Hooks;
	bool bHistory = false;

	TSharedPtr<SNotificationGlyph> IconGlyph;
	TSharedPtr<STextBlock> TitleBlock;
	TSharedPtr<STextBlock> BodyBlock;
	TSharedPtr<STextBlock> CountBlock;
	TSharedPtr<STextBlock> TimestampBlock;
	TSharedPtr<SWidget> ProgressLine;
	TSharedPtr<class SBox> ProgressBox;
	TSharedPtr<SHorizontalBox> ActionRow;

	/** White, tinted where drawn (a rounded box's fill takes the draw tint). */
	FSlateRoundedBoxBrush FillBrush = FSlateRoundedBoxBrush(FLinearColor::White, 12.f);
	FSlateRoundedBoxBrush OutlineBrush = FSlateRoundedBoxBrush(FLinearColor::Transparent, 12.f, FLinearColor::White, 1.f);
	FSlateRoundedBoxBrush AccentBrush = FSlateRoundedBoxBrush(FLinearColor::White, 2.f);
	FSlateRoundedBoxBrush KeyCapBrush = FSlateRoundedBoxBrush(FLinearColor::White, 4.f);
	FButtonStyle PrimaryButton;
	FButtonStyle SecondaryButton;
	FButtonStyle DestructiveButton;
	FButtonStyle CloseButton;

	/** Seconds since the last merge (the count pop and flash); large when none. */
	float BumpAge = 10.f;
	bool bShortcutShown = false;
	TSharedPtr<FActiveTimerHandle> AnimateTimer;

	/** Soft shadow / glow meshes, rebuilt when what they depend on changes. */
	mutable FWidgetShapeMesh ShadowMesh;
	mutable uint32 ShadowHash = 0;
	mutable FWidgetShapeMesh GlowMesh;
	mutable uint32 GlowHash = 0;
};
