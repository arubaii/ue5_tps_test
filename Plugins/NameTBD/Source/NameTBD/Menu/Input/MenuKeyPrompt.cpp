// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuKeyPrompt.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "MenuInputSubsystem.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"
#include "NameTBD/Widgets/WidgetSVG.h"

#define LOCTEXT_NAMESPACE "MenuKeyPrompt"

FMenuKeyCapStyle::FMenuKeyCapStyle()
{
	Font = FCoreStyle::GetDefaultFontStyle("Bold", 11);
}

// =====================================================================================================
//  Glyph
// =====================================================================================================

void SMenuKeyGlyph::Construct(const FArguments& InArgs)
{
	CapStyle = InArgs._CapStyle;
	SetCanTick(false);
}

void SMenuKeyGlyph::SetGlyph(const FMenuKeyGlyph& InGlyph)
{
	Glyph = InGlyph;
	bRound = Glyph.Key == EKeys::Gamepad_FaceButton_Bottom || Glyph.Key == EKeys::Gamepad_FaceButton_Right
		|| Glyph.Key == EKeys::Gamepad_FaceButton_Left || Glyph.Key == EKeys::Gamepad_FaceButton_Top;
	ImageBrush = FSlateBrush();
	if (Glyph.Image.ImageSource == EWidgetImageSource::Texture && Glyph.Image.Image)
	{
		ImageBrush.SetResourceObject(Glyph.Image.Image);
		ImageBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	SVGBrush.Reset();
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SMenuKeyGlyph::SetCapStyle(const FMenuKeyCapStyle& InStyle)
{
	CapStyle = InStyle;
	Invalidate(EInvalidateWidgetReason::Layout);
}

void SMenuKeyGlyph::SetHoldProgress(float InProgress)
{
	const float Clamped = FMath::Clamp(InProgress, 0.f, 1.f);
	if (!FMath::IsNearlyEqual(Clamped, HoldProgress))
	{
		HoldProgress = Clamped;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FText SMenuKeyGlyph::GetCapText() const
{
	return Glyph.Label.IsEmpty() ? MenuInputUtils::GetKeyLabel(Glyph.Key, EMenuGamepadFamily::Xbox) : Glyph.Label;
}

FVector2D SMenuKeyGlyph::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const float Height = CapStyle.Height;
	if (Glyph.Image.HasImage())
	{
		double Aspect = 1.0;
		if (const UTexture2D* Texture = Cast<UTexture2D>(Glyph.Image.Image))
		{
			Aspect = static_cast<double>(FMath::Max(1, Texture->GetSizeX())) / FMath::Max(1, Texture->GetSizeY());
		}
		return FVector2D(Height * Aspect, Height);
	}
	if (bRound)
	{
		return FVector2D(Height, Height);
	}
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FVector2D TextSize = Measure->Measure(GetCapText(), CapStyle.Font);
	return FVector2D(FMath::Max(static_cast<double>(Height), TextSize.X + Height * 0.6), Height);
}

int32 SMenuKeyGlyph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	if (Glyph.Image.HasImage())
	{
		const FLinearColor Color = Glyph.Image.Tint * Tint;
		if (Glyph.Image.ImageSource == EWidgetImageSource::SVG)
		{
			if (const FSlateBrush* Brush = SVGBrush.Get(Glyph.Image.SVG, nullptr, AllottedGeometry, FVector2f(LocalSize), *this))
			{
				FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Brush, Effect, Color);
			}
		}
		else
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &ImageBrush, Effect, Color);
		}
	}
	else
	{
		const float Radius = bRound ? static_cast<float>(LocalSize.Y) * 0.5f : CapStyle.Height * CapStyle.CornerRadius;
		// MakeBox draws in its tint (the brush's own tint is not applied): the cap colour goes in as the tint.
		const FSlateRoundedBoxBrush Cap(FLinearColor::White, Radius, CapStyle.OutlineColor * Tint, CapStyle.OutlineWidth);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &Cap, Effect, CapStyle.CapColor * Tint);

		if (HoldProgress > 0.f)
		{
			const FSlateRoundedBoxBrush Fill(FLinearColor::White, Radius);
			const float FillHeight = static_cast<float>(LocalSize.Y) * HoldProgress;
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
				AllottedGeometry.ToPaintGeometry(FVector2f(static_cast<float>(LocalSize.X), FillHeight), FSlateLayoutTransform(FVector2f(0.f, static_cast<float>(LocalSize.Y) - FillHeight))),
				&Fill, Effect, CapStyle.TextColor.CopyWithNewOpacity(0.35f) * Tint);
		}

		const FText Text = GetCapText();
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D TextSize = Measure->Measure(Text, CapStyle.Font);
		const FVector2f Offset(static_cast<float>((LocalSize.X - TextSize.X) * 0.5), static_cast<float>((LocalSize.Y - TextSize.Y) * 0.5));
		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(Offset)),
			Text, CapStyle.Font, Effect, CapStyle.TextColor * Tint);
	}
	return LayerId + 2;
}

// =====================================================================================================
//  Prompt
// =====================================================================================================

void SMenuKeyPrompt::Construct(const FArguments& InArgs)
{
	ClickedDelegate = InArgs._OnClicked;
	FSlateFontInfo Font = InArgs._LabelFont;
	if (!Font.HasValidFont())
	{
		Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	}
	const TAttribute<FText> LabelAttribute = InArgs._Label;

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SAssignNew(GlyphWidget, SMenuKeyGlyph)
			.CapStyle(InArgs._CapStyle)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(InArgs._Spacing, 0.f, 0.f, 0.f)
		[
			SAssignNew(LabelWidget, STextBlock)
			.Text(LabelAttribute)
			.Font(Font)
			.ColorAndOpacity(InArgs._LabelColor)
			.Visibility_Lambda([LabelAttribute]() { return LabelAttribute.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
		]
	];
}

void SMenuKeyPrompt::SetGlyph(const FMenuKeyGlyph& InGlyph)
{
	GlyphWidget->SetGlyph(InGlyph);
}

void SMenuKeyPrompt::SetCapStyle(const FMenuKeyCapStyle& InStyle)
{
	GlyphWidget->SetCapStyle(InStyle);
}

void SMenuKeyPrompt::SetLabelFont(const FSlateFontInfo& InFont)
{
	if (InFont.HasValidFont())
	{
		LabelWidget->SetFont(InFont);
	}
}

void SMenuKeyPrompt::SetHoldProgress(float InProgress)
{
	GlyphWidget->SetHoldProgress(InProgress);
}

FReply SMenuKeyPrompt::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (ClickedDelegate.IsBound() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bPressed = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SMenuKeyPrompt::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bPressed)
	{
		return FReply::Unhandled();
	}
	bPressed = false;
	if (MyGeometry.IsUnderLocation(MouseEvent.GetScreenSpacePosition()))
	{
		ClickedDelegate.ExecuteIfBound();
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SMenuKeyPrompt::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return ClickedDelegate.IsBound() ? FCursorReply::Cursor(EMouseCursor::Hand) : FCursorReply::Unhandled();
}

// =====================================================================================================
//  UMG
// =====================================================================================================

UMenuKeyPrompt::UMenuKeyPrompt()
{
	LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	KeyboardKey = EKeys::Enter;
	GamepadKey = EKeys::Gamepad_FaceButton_Bottom;
}

TSharedRef<SWidget> UMenuKeyPrompt::RebuildWidget()
{
	PromptWidget = SNew(SMenuKeyPrompt)
		.CapStyle(KeyCap)
		.LabelFont(LabelFont)
		.Spacing(Spacing)
		.Label(TAttribute<FText>::CreateWeakLambda(this, [this]() { return Label; }))
		.LabelColor(LabelColor)
		.OnClicked(FOnMenuKeyPromptClicked::CreateUObject(this, &UMenuKeyPrompt::HandleClicked));

	if (!IsDesignTime() && !bBound)
	{
		if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
		{
			if (UMenuInputSubsystem* Input = LocalPlayer->GetSubsystem<UMenuInputSubsystem>())
			{
				DeviceHandle = Input->OnInputDeviceChangedNative().AddUObject(this, &UMenuKeyPrompt::HandleDeviceChanged);
			}
			if (UMenuSettingsSubsystem* Settings = LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>())
			{
				Settings->OnKeyBindingsChanged.AddUniqueDynamic(this, &UMenuKeyPrompt::HandleBindingsChanged);
			}
			bBound = true;
		}
	}

	return SNew(SBox)
		[
			PromptWidget.ToSharedRef()
		];
}

void UMenuKeyPrompt::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (PromptWidget.IsValid())
	{
		PromptWidget->SetCapStyle(KeyCap);
		PromptWidget->SetLabelFont(LabelFont);
	}
	RefreshPrompt();
}

void UMenuKeyPrompt::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	if (bBound)
	{
		if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
		{
			if (UMenuInputSubsystem* Input = LocalPlayer->GetSubsystem<UMenuInputSubsystem>())
			{
				Input->OnInputDeviceChangedNative().Remove(DeviceHandle);
			}
			if (UMenuSettingsSubsystem* Settings = LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>())
			{
				Settings->OnKeyBindingsChanged.RemoveDynamic(this, &UMenuKeyPrompt::HandleBindingsChanged);
			}
		}
		bBound = false;
	}
	PromptWidget.Reset();
}

void UMenuKeyPrompt::RefreshPrompt()
{
	if (!PromptWidget.IsValid())
	{
		return;
	}
	const ULocalPlayer* LocalPlayer = IsDesignTime() ? nullptr : GetOwningLocalPlayer();
	const UMenuInputSubsystem* Input = LocalPlayer ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr;

	EMenuInputDevice Device = EMenuInputDevice::KeyboardMouse;
	switch (ShowDevice)
	{
	case EMenuKeyPromptDevice::Gamepad: Device = EMenuInputDevice::Gamepad; break;
	case EMenuKeyPromptDevice::KeyboardMouse: Device = EMenuInputDevice::KeyboardMouse; break;
	default: Device = Input ? Input->GetCurrentDevice() : EMenuInputDevice::KeyboardMouse; break;
	}
	const bool bTouch = Device == EMenuInputDevice::Touch;
	if (bTouch)
	{
		Device = EMenuInputDevice::KeyboardMouse;
	}

	// The mapping context's key (with the player's rebinding of it); with no player (the designer) the context's own key.
	FKey Key;
	if (InputAction)
	{
		Key = Input ? Input->GetKeyForActionInContext(InputAction, InputMappingContext, Device)
			: MenuInputUtils::GetContextKey(InputMappingContext, InputAction, Device == EMenuInputDevice::Gamepad);
	}
	if (!Key.IsValid())
	{
		Key = Device == EMenuInputDevice::Gamepad ? GamepadKey : KeyboardKey;
	}
	ShownKey = Key;

	FMenuKeyGlyph Glyph;
	if (Input)
	{
		Glyph = Input->GetKeyGlyph(Key);
	}
	else
	{
		// The designer: the project's art for the key, as the game will show it.
		Glyph = MenuInputUtils::GetProjectKeyGlyph(Key);
	}
	PromptWidget->SetGlyph(Glyph);

	// Touch has no keys to press: the prompt goes (a touch screen shows its own buttons).
	const bool bHide = bHideWhenUnbound && (!Key.IsValid() || bTouch);
	PromptWidget->SetVisibility(bHide ? EVisibility::Collapsed : EVisibility::Visible);
}

void UMenuKeyPrompt::SetInputAction(UInputAction* InAction)
{
	InputAction = InAction;
	RefreshPrompt();
}

void UMenuKeyPrompt::SetInputMappingContext(UInputMappingContext* InContext)
{
	InputMappingContext = InContext;
	RefreshPrompt();
}

void UMenuKeyPrompt::SetKeys(FKey InKeyboardKey, FKey InGamepadKey)
{
	KeyboardKey = InKeyboardKey;
	GamepadKey = InGamepadKey;
	RefreshPrompt();
}

void UMenuKeyPrompt::SetLabel(FText InLabel)
{
	Label = InLabel;
}

void UMenuKeyPrompt::SetHoldProgress(float InProgress)
{
	if (PromptWidget.IsValid())
	{
		PromptWidget->SetHoldProgress(InProgress);
	}
}

void UMenuKeyPrompt::HandleDeviceChanged(EMenuInputDevice Device)
{
	RefreshPrompt();
}

void UMenuKeyPrompt::HandleBindingsChanged()
{
	RefreshPrompt();
}

void UMenuKeyPrompt::HandleClicked()
{
	OnPromptClicked.Broadcast();
}

#if WITH_EDITOR
const FText UMenuKeyPrompt::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

#undef LOCTEXT_NAMESPACE
