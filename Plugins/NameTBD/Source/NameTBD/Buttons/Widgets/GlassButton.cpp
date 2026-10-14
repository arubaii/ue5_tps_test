// Copyright AR GameSystems 2026. All rights reserved.

#include "GlassButton.h"
#include "StyledButtonTheme.h"

#include "Components/TextBlock.h"
#include "NameTBD/Widgets/Glass/GlassPanel.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

#define LOCTEXT_NAMESPACE "StyledButtons"

namespace GlassButtonParts
{
	const FName Glass(TEXT("Glass"));
	const FName Label(TEXT("Label"));

	EWidgetMotion ToWidgetMotion(EStyledButtonMotion Motion)
	{
		switch (Motion)
		{
		case EStyledButtonMotion::Snappy: return EWidgetMotion::Snappy;
		case EStyledButtonMotion::Bouncy: return EWidgetMotion::Bouncy;
		case EStyledButtonMotion::Gentle: return EWidgetMotion::Gentle;
		case EStyledButtonMotion::Instant: return EWidgetMotion::Instant;
		case EStyledButtonMotion::Smooth:
		default: return EWidgetMotion::Smooth;
		}
	}
}

UGlassButton::UGlassButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TextColor = StyledButtonTheme::Text();
}

UGlassPanel* UGlassButton::GetGlassPart() const
{
	// GetPart only looks the part up (it changes nothing), it just isn't marked const.
	return const_cast<UGlassButton*>(this)->GetPart<UGlassPanel>(GlassButtonParts::Glass);
}

void UGlassButton::BuildParts()
{
	UGlassPanel* GlassPart = MakePart<UGlassPanel>(GlassButtonParts::Glass);
	UTextBlock* LabelBlock = MakeLabel(GlassButtonParts::Label, LOCTEXT("GlassLabel", "Button"), 15);
	GlassPart->SetContent(LabelBlock);
	RememberPart(GlassButtonParts::Glass, GlassPart);
	RememberPart(GlassButtonParts::Label, LabelBlock);
	ButtonTextWidgetName = LabelBlock->GetFName();
	SetButtonContent(GlassPart);
}

void UGlassButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	// The glass is the button's look; the background is a clear shape of the same corners, there for the press to scale.
	const EStyledButtonCorners CornerStyle = Corners;
	const float Rounding = CornerRounding;
	MakeBackground<URectangleShape>([CornerStyle, Rounding](URectangleShape& Shape)
	{
		SetCorners(Shape, CornerStyle, Rounding);
		Solid(Shape, Clear());
	});
	SetContentLayout(FMargin(0.f), HAlign_Fill, VAlign_Fill);

	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Pressed).Transform.Scale = FVector2D(PressScale, PressScale);

	if (UGlassPanel* GlassPart = FindPart<UGlassPanel>(GlassButtonParts::Glass))
	{
		GlassPart->SetPreset(GlassPreset);
		GlassPart->bFullyRounded = Corners == EStyledButtonCorners::Pill;
		GlassPart->bIndividualCorners = false;
		GlassPart->CornerRadius = Corners == EStyledButtonCorners::Rounded ? CornerRounding : 0.f;
		GlassPart->ContentPadding = ContentPadding;
		GlassPart->HorizontalAlignment = HAlign_Center;
		GlassPart->VerticalAlignment = VAlign_Center;

		// Driven by the button (OnVisualStateChanged), so gamepad focus lights it as hover does.
		GlassPart->Reaction = EGlassReaction::Manual;
		const FGlassHoverResponse Defaults;
		FGlassHoverResponse Response = Defaults;
		Response.BlurBoost = Defaults.BlurBoost * HoverGlow;
		Response.Brighten = Defaults.Brighten * HoverGlow;
		Response.EdgeBoost = Defaults.EdgeBoost * HoverGlow;
		Response.SheenBoost = Defaults.SheenBoost * HoverGlow;
		GlassPart->HoverResponse = Response;
		GlassPart->Motion = GlassButtonParts::ToWidgetMotion(Motion);
		GlassPart->EnterDuration = EnterDuration;
		GlassPart->ExitDuration = ExitDuration;
		GlassPart->SynchronizeProperties();
	}

	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(GlassButtonParts::Label))
	{
		const FLinearColor Resting = TextColor * FLinearColor(0.9f, 0.9f, 0.9f, 1.f);
		LabelBlock->SetColorAndOpacity(FSlateColor(Resting));
		AddTint(LabelBlock, Resting, { { EContextMenuState::Hovered, TextColor }, { EContextMenuState::Pressed, TextColor } });
	}
	AddDisabledFade();
}

void UGlassButton::OnVisualStateChanged(EContextMenuState NewState, bool bInstant)
{
	Super::OnVisualStateChanged(NewState, bInstant);
	if (UGlassPanel* GlassPart = GetGlassPart())
	{
		// On (a toggle's Enabled) keeps it lit, as hovered.
		const bool bPressed = NewState == EContextMenuState::Pressed;
		const bool bLit = bPressed || NewState == EContextMenuState::Hovered || NewState == EContextMenuState::Enabled;
		GlassPart->SetInteractionState(bLit, bPressed, bInstant);
	}
}

#undef LOCTEXT_NAMESPACE
