// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIWidgetStyling.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "NameTBD/Buttons/Widgets/StyledButtons.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/WidgetSVG.h"
#include "NameTBD/Widgets/WidgetSVGImage.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIHelpers.h"
#include "NameTBDAIJson.h"
#include "NameTBDAITheme.h"
#include "NameTBDAITools.h"
#include "Styling/SlateTypes.h"
#include "UObject/Package.h"

using namespace NameTBDAIJson;

namespace NameTBDAIWidgetStylingLocal
{
	/** A style struct read off a widget by its property name (WidgetStyle), whatever accessors this engine version has. */
	template <typename StyleType>
	bool ReadStyle(const UWidget* Widget, FName PropertyName, StyleType& OutStyle)
	{
		const FStructProperty* Property = Widget ? FindFProperty<FStructProperty>(Widget->GetClass(), PropertyName) : nullptr;
		if (!Property || Property->Struct != StyleType::StaticStruct())
		{
			return false;
		}
		OutStyle = *Property->ContainerPtrToValuePtr<StyleType>(Widget);
		return true;
	}

	template <typename StyleType>
	void WriteStyle(UWidget* Widget, FName PropertyName, const StyleType& Style)
	{
		const FStructProperty* Property = Widget ? FindFProperty<FStructProperty>(Widget->GetClass(), PropertyName) : nullptr;
		if (Property && Property->Struct == StyleType::StaticStruct())
		{
			Widget->Modify();
			*Property->ContainerPtrToValuePtr<StyleType>(Widget) = Style;
		}
	}

	void WriteColor(UObject* Object, FName PropertyName, const FLinearColor& Color)
	{
		const FStructProperty* Property = Object ? FindFProperty<FStructProperty>(Object->GetClass(), PropertyName) : nullptr;
		if (Property && Property->Struct == TBaseStructure<FLinearColor>::Get())
		{
			*Property->ContainerPtrToValuePtr<FLinearColor>(Object) = Color;
		}
	}

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A = Alpha;
		return Color;
	}

	FLinearColor Mix(const FLinearColor& A, const FLinearColor& B, float Alpha)
	{
		return FMath::Lerp(A, B, Alpha);
	}

	float Luminance(const FLinearColor& Color)
	{
		// Linear colours: the WCAG relative luminance directly.
		return 0.2126f * Color.R + 0.7152f * Color.G + 0.0722f * Color.B;
	}

	/** Resources/AI/Icons.json, read again when it changes on disk. */
	TSharedPtr<FJsonObject> LoadIcons()
	{
		static TSharedPtr<FJsonObject> Cached;
		static FDateTime CachedStamp;
		const FString File = NameTBDAIDocs::GetResourcesDir() / TEXT("Icons.json");
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*File);
		if (!Cached.IsValid() || Stamp != CachedStamp)
		{
			Cached = ParseObject(NameTBDAIDocs::LoadResourceText(TEXT("Icons.json")));
			CachedStamp = Stamp;
			if (!Cached.IsValid())
			{
				Cached = MakeShared<FJsonObject>();
			}
		}
		return Cached;
	}

	/** "Volume-Up" -> "volume_up". */
	FString IconKey(const FString& Name)
	{
		FString Key = Name.TrimStartAndEnd().ToLower();
		Key.ReplaceInline(TEXT("-"), TEXT("_"));
		Key.ReplaceInline(TEXT(" "), TEXT("_"));
		return Key;
	}

	/** An icon's name with aliases worked out, or empty when there is none. */
	FString ResolveIconName(const FString& Name)
	{
		const TSharedPtr<FJsonObject> Icons = LoadIcons();
		const TSharedPtr<FJsonObject> Library = GetObject(Icons, TEXT("icons"));
		const TSharedPtr<FJsonObject> Aliases = GetObject(Icons, TEXT("aliases"));
		FString Key = IconKey(Name);
		if (Aliases.IsValid() && Aliases->HasTypedField<EJson::String>(Key))
		{
			Key = Aliases->GetStringField(Key);
		}
		return Library.IsValid() && Library->HasTypedField<EJson::String>(Key) ? Key : FString();
	}

	bool IsUndrawable(uint32 Char)
	{
		return (Char >= 0x2300 && Char <= 0x23FF)     // technical: media controls
			|| (Char >= 0x2460 && Char <= 0x24FF)     // enclosed alphanumerics
			|| (Char >= 0x25A0 && Char <= 0x25FF)     // geometric shapes
			|| (Char >= 0x2600 && Char <= 0x27BF)     // symbols, dingbats
			|| (Char >= 0x2900 && Char <= 0x297F)     // supplemental arrows
			|| (Char >= 0x2B00 && Char <= 0x2BFF)     // more arrows and shapes
			|| (Char >= 0xE000 && Char <= 0xF8FF)     // private use (icon fonts)
			|| Char == 0xFE0F                         // emoji presentation selector
			|| (Char >= 0xD800 && Char <= 0xDFFF)     // UTF-16 halves of emoji and other astral characters
			|| Char >= 0x10000;                       // the same, where TCHAR is 32 bits
	}

	/** The colour Widget itself draws as a backdrop for what is inside or over it, if it is one (alpha included). */
	bool GetOwnBackdrop(const UWidget* Widget, FLinearColor& OutColor)
	{
		if (const UButton* Button = Cast<UButton>(Widget))
		{
			if (const UContextMenuButton* PluginButton = Cast<UContextMenuButton>(Button))
			{
				if (PluginButton->BackgroundShape)
				{
					// A vector background (its look is the button's own): not read here.
					return false;
				}
			}
			return NameTBDAIWidgetStyling::GetButtonFill(Widget, OutColor);
		}
		if (const UBorder* Border = Cast<UBorder>(Widget))
		{
			if (Border->Background.DrawAs == ESlateBrushDrawType::NoDrawType || Border->Background.GetResourceObject() != nullptr)
			{
				return false;
			}
			OutColor = Border->GetBrushColor() * Border->Background.TintColor.GetSpecifiedColor();
			return true;
		}
		if (const UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
		{
			if (!Shape->bFill)
			{
				return false;
			}
			if (Shape->Fill.Type == EWidgetShapePaintType::Color)
			{
				OutColor = Shape->Fill.Color;
				return true;
			}
			if ((Shape->Fill.Type == EWidgetShapePaintType::LinearGradient || Shape->Fill.Type == EWidgetShapePaintType::RadialGradient) && Shape->Fill.GradientStops.Num() > 0)
			{
				FLinearColor Sum = FLinearColor::Transparent;
				for (const FWidgetShapeGradientStop& Stop : Shape->Fill.GradientStops)
				{
					Sum += Stop.Color;
				}
				OutColor = Sum / static_cast<float>(Shape->Fill.GradientStops.Num());
				return true;
			}
			return false;
		}
		if (const UImage* Image = Cast<UImage>(Widget))
		{
			if (Image->GetBrush().GetResourceObject() != nullptr)
			{
				return false;
			}
			OutColor = Image->GetColorAndOpacity() * Image->GetBrush().TintColor.GetSpecifiedColor();
			return true;
		}
		return false;
	}

	bool FillsItsSlot(const UWidget* Widget)
	{
		if (const UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(Widget->Slot))
		{
			return OverlaySlot->GetHorizontalAlignment() == HAlign_Fill && OverlaySlot->GetVerticalAlignment() == VAlign_Fill;
		}
		return false;
	}

	/**
	 * What is behind Widget: walking up, an ancestor that draws a backdrop, or an earlier sibling in an overlay filling it; a
	 * translucent one blended over what is behind it in turn. Else the theme's background. False: under a plugin button's style.
	 */
	bool FindBackground(const UWidget* Widget, bool bDark, FLinearColor& OutColor)
	{
		const FLinearColor Screen = NameTBDAIHelpers::GetPalette(bDark).Background;
		for (const UWidget* Current = Widget; Current; Current = Current->GetParent())
		{
			if (Current != Widget)
			{
				if (Current->IsA<UStyledButtonBase>())
				{
					return false;
				}
				FLinearColor Own;
				if (GetOwnBackdrop(Current, Own) && Own.A > 0.02f)
				{
					if (Own.A < 0.99f)
					{
						FLinearColor Behind;
						if (!FindBackground(Current, bDark, Behind))
						{
							Behind = Screen;
						}
						OutColor = WithAlpha(Mix(Behind, Own, Own.A), 1.f);
					}
					else
					{
						OutColor = WithAlpha(Own, 1.f);
					}
					return true;
				}
			}
			const UPanelWidget* Parent = Current->GetParent();
			if (const UOverlay* Overlay = Cast<UOverlay>(Parent))
			{
				for (int32 Index = Overlay->GetChildIndex(Current) - 1; Index >= 0; --Index)
				{
					const UWidget* Sibling = Overlay->GetChildAt(Index);
					FLinearColor Own;
					if (Sibling && Sibling->GetVisibility() != ESlateVisibility::Collapsed && FillsItsSlot(Sibling) && GetOwnBackdrop(Sibling, Own) && Own.A > 0.02f)
					{
						FLinearColor Behind = Screen;
						if (Own.A < 0.99f)
						{
							FindBackground(Sibling, bDark, Behind);
						}
						OutColor = WithAlpha(Mix(Behind, Own, FMath::Min(Own.A, 1.f)), 1.f);
						return true;
					}
				}
			}
		}
		OutColor = WithAlpha(Screen, 1.f);
		return true;
	}
}

namespace NameTBDAIWidgetStyling
{
	using namespace NameTBDAIWidgetStylingLocal;

	EButtonVariant ParseVariant(const FString& Variant)
	{
		const FString Key = Normalize(Variant);
		if (Key == TEXT("primary") || Key == TEXT("accent") || Key == TEXT("filled") || Key == TEXT("cta"))
		{
			return EButtonVariant::Primary;
		}
		if (Key == TEXT("ghost") || Key == TEXT("text") || Key == TEXT("flat") || Key == TEXT("icon") || Key == TEXT("tertiary"))
		{
			return EButtonVariant::Ghost;
		}
		return EButtonVariant::Secondary;
	}

	bool IsThemedEngineWidget(const UWidget* Widget)
	{
		if (!Widget)
		{
			return false;
		}
		if (Widget->IsA<UButton>())
		{
			// The plugin's buttons have their own style (ready-made) or are styled by their targets.
			return !Widget->IsA<UContextMenuButton>();
		}
		return Widget->IsA<USlider>() || Widget->IsA<UProgressBar>() || Widget->IsA<UCheckBox>() || Widget->IsA<UEditableTextBox>();
	}

	FLinearColor ReadableOn(const FLinearColor& Background)
	{
		const UNameTBDAITheme& Theme = UNameTBDAITheme::Get();
		const FLinearColor Light = WithAlpha(Theme.Dark.Text, 1.f);
		const FLinearColor Dark = WithAlpha(Theme.Light.Text, 1.f);
		return ContrastRatio(Light, Background) >= ContrastRatio(Dark, Background) ? Light : Dark;
	}

	float ContrastRatio(const FLinearColor& A, const FLinearColor& B)
	{
		const float La = Luminance(A);
		const float Lb = Luminance(B);
		return (FMath::Max(La, Lb) + 0.05f) / (FMath::Min(La, Lb) + 0.05f);
	}

	bool GetButtonFill(const UWidget* Widget, FLinearColor& OutFill)
	{
		const UButton* Button = Cast<UButton>(Widget);
		FButtonStyle Style;
		if (!Button || !ReadStyle(Button, TEXT("WidgetStyle"), Style) || Style.Normal.DrawAs == ESlateBrushDrawType::NoDrawType)
		{
			return false;
		}
		FLinearColor Tint = Style.Normal.TintColor.GetSpecifiedColor() * Button->GetBackgroundColor();
		if (Style.Normal.GetResourceObject() != nullptr)
		{
			// A texture (the engine's default button is a light grey one): read as light grey, tinted.
			Tint *= FLinearColor(0.72f, 0.72f, 0.72f, 1.f);
		}
		OutFill = Tint;
		return true;
	}

	void ThemeEngineWidget(UWidget* Widget, bool bDark, EButtonVariant Variant)
	{
		if (!IsThemedEngineWidget(Widget))
		{
			return;
		}
		const UNameTBDAITheme& Theme = UNameTBDAITheme::Get();
		const FNameTBDAIPalette& Palette = Theme.GetPalette(bDark);
		const float Radius = Theme.RadiusSmall;

		if (UButton* Button = Cast<UButton>(Widget))
		{
			FButtonStyle Style;
			ReadStyle(Button, TEXT("WidgetStyle"), Style);
			FLinearColor Rest = Palette.SurfaceRaised;
			FLinearColor Hover = Mix(Palette.SurfaceRaised, Palette.Text, 0.08f);
			FLinearColor Down = Mix(Palette.SurfaceRaised, Palette.Background, 0.4f);
			FLinearColor Edge = Palette.Outline;
			float EdgeWidth = 1.f;
			switch (Variant)
			{
			case EButtonVariant::Primary:
				Rest = Palette.Accent;
				Hover = Mix(Palette.Accent, FLinearColor::White, 0.12f);
				Down = Palette.Accent * 0.82f;
				Down.A = Palette.Accent.A;
				Edge = FLinearColor::Transparent;
				EdgeWidth = 0.f;
				break;
			case EButtonVariant::Ghost:
				Rest = WithAlpha(Palette.SurfaceRaised, 0.f);
				Hover = WithAlpha(Mix(Palette.SurfaceRaised, Palette.Text, 0.08f), 0.9f);
				Down = WithAlpha(Palette.SurfaceRaised, 0.9f);
				Edge = FLinearColor::Transparent;
				EdgeWidth = 0.f;
				break;
			default:
				break;
			}
			// Over a clear ghost button the text reads against what is behind: the theme's surface.
			const FLinearColor TextColor = ReadableOn(Variant == EButtonVariant::Ghost ? Palette.Surface : Rest);
			FLinearColor HoverEdge = Variant == EButtonVariant::Secondary ? WithAlpha(Palette.Accent, 0.7f) : Edge;
			Style.SetNormal(FSlateRoundedBoxBrush(Rest, Radius, Edge, EdgeWidth));
			Style.SetHovered(FSlateRoundedBoxBrush(Hover, Radius, HoverEdge, EdgeWidth));
			Style.SetPressed(FSlateRoundedBoxBrush(Down, Radius, HoverEdge, EdgeWidth));
			Style.SetDisabled(FSlateRoundedBoxBrush(WithAlpha(Rest, Rest.A * 0.4f), Radius, WithAlpha(Edge, Edge.A * 0.4f), EdgeWidth));
			Style.SetNormalPadding(FMargin(14.f, 8.f));
			Style.SetPressedPadding(FMargin(14.f, 9.f, 14.f, 7.f));
			Style.SetNormalForeground(FSlateColor(TextColor));
			Style.SetHoveredForeground(FSlateColor(TextColor));
			Style.SetPressedForeground(FSlateColor(TextColor));
			Style.SetDisabledForeground(FSlateColor(WithAlpha(TextColor, 0.45f)));
			WriteStyle(Button, TEXT("WidgetStyle"), Style);
			WriteColor(Button, TEXT("BackgroundColor"), FLinearColor::White);
			WriteColor(Button, TEXT("ColorAndOpacity"), FLinearColor::White);
			return;
		}

		if (USlider* Slider = Cast<USlider>(Widget))
		{
			FSliderStyle Style;
			ReadStyle(Slider, TEXT("WidgetStyle"), Style);
			const FLinearColor Track = Mix(Palette.SurfaceRaised, Palette.Text, 0.18f);
			Style.SetNormalBarImage(FSlateRoundedBoxBrush(Track, 2.f));
			Style.SetHoveredBarImage(FSlateRoundedBoxBrush(Mix(Track, Palette.Text, 0.1f), 2.f));
			Style.SetDisabledBarImage(FSlateRoundedBoxBrush(WithAlpha(Track, 0.4f), 2.f));
			Style.SetNormalThumbImage(FSlateRoundedBoxBrush(Palette.Text, 8.f, FVector2D(16.0, 16.0)));
			Style.SetHoveredThumbImage(FSlateRoundedBoxBrush(Palette.Accent, 8.f, FVector2D(16.0, 16.0)));
			Style.SetDisabledThumbImage(FSlateRoundedBoxBrush(WithAlpha(Palette.Text, 0.4f), 8.f, FVector2D(16.0, 16.0)));
			Style.SetBarThickness(4.f);
			WriteStyle(Slider, TEXT("WidgetStyle"), Style);
			WriteColor(Slider, TEXT("SliderBarColor"), FLinearColor::White);
			WriteColor(Slider, TEXT("SliderHandleColor"), FLinearColor::White);
			// The filled part of the bar, where this engine version draws one.
			WriteColor(Slider, TEXT("SliderProgressColor"), Palette.Accent);
			return;
		}

		if (UProgressBar* Progress = Cast<UProgressBar>(Widget))
		{
			FProgressBarStyle Style;
			ReadStyle(Progress, TEXT("WidgetStyle"), Style);
			Style.SetBackgroundImage(FSlateRoundedBoxBrush(Palette.SurfaceRaised, 4.f));
			Style.SetFillImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));
			Style.SetMarqueeImage(FSlateRoundedBoxBrush(FLinearColor::White, 4.f));
			WriteStyle(Progress, TEXT("WidgetStyle"), Style);
			WriteColor(Progress, TEXT("FillColorAndOpacity"), Palette.Accent);
			return;
		}

		if (UCheckBox* Check = Cast<UCheckBox>(Widget))
		{
			FCheckBoxStyle Style;
			ReadStyle(Check, TEXT("WidgetStyle"), Style);
			const FVector2D Box(20.0, 20.0);
			const FLinearColor Edge = Mix(Palette.Outline, Palette.Text, 0.35f);
			Style.SetUncheckedImage(FSlateRoundedBoxBrush(Palette.SurfaceRaised, 4.f, Edge, 1.5f, Box));
			Style.SetUncheckedHoveredImage(FSlateRoundedBoxBrush(Mix(Palette.SurfaceRaised, Palette.Text, 0.08f), 4.f, Palette.Accent, 1.5f, Box));
			Style.SetUncheckedPressedImage(FSlateRoundedBoxBrush(Palette.SurfaceRaised, 4.f, Palette.Accent, 2.f, Box));
			Style.SetCheckedImage(FSlateRoundedBoxBrush(Palette.Accent, 4.f, Palette.Accent, 1.5f, Box));
			Style.SetCheckedHoveredImage(FSlateRoundedBoxBrush(Mix(Palette.Accent, FLinearColor::White, 0.12f), 4.f, Palette.Accent, 1.5f, Box));
			Style.SetCheckedPressedImage(FSlateRoundedBoxBrush(Palette.Accent * 0.85f, 4.f, Palette.Accent, 1.5f, Box));
			Style.SetUndeterminedImage(FSlateRoundedBoxBrush(WithAlpha(Palette.Accent, 0.5f), 4.f, Palette.Accent, 1.5f, Box));
			WriteStyle(Check, TEXT("WidgetStyle"), Style);
			return;
		}

		if (UEditableTextBox* TextBox = Cast<UEditableTextBox>(Widget))
		{
			FEditableTextBoxStyle Style;
			ReadStyle(TextBox, TEXT("WidgetStyle"), Style);
			Style.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Palette.SurfaceRaised, Radius, Palette.Outline, 1.f));
			Style.SetBackgroundImageHovered(FSlateRoundedBoxBrush(Mix(Palette.SurfaceRaised, Palette.Text, 0.05f), Radius, Mix(Palette.Outline, Palette.Text, 0.3f), 1.f));
			Style.SetBackgroundImageFocused(FSlateRoundedBoxBrush(Palette.SurfaceRaised, Radius, Palette.Accent, 1.5f));
			Style.SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(WithAlpha(Palette.SurfaceRaised, 0.5f), Radius, Palette.Outline, 1.f));
			Style.SetForegroundColor(FSlateColor(Palette.Text));
			Style.SetPadding(FMargin(10.f, 6.f));
			WriteStyle(TextBox, TEXT("WidgetStyle"), Style);
		}
	}

	// ---- Icons ----

	TSharedPtr<FJsonObject> ListIcons()
	{
		const TSharedPtr<FJsonObject> Icons = LoadIcons();
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		TArray<FString> Names;
		if (const TSharedPtr<FJsonObject> Library = GetObject(Icons, TEXT("icons")))
		{
			Library->Values.GetKeys(Names);
		}
		Out->SetField(TEXT("icons"), Arr(Names));
		if (const TSharedPtr<FJsonObject> Aliases = GetObject(Icons, TEXT("aliases")))
		{
			Out->SetObjectField(TEXT("aliases"), Aliases);
		}
		Out->SetStringField(TEXT("use"), TEXT("Spec \"icon\": a name here, or an SVG / texture asset path. On a Button it becomes the content (with \"text\": icon and label side by side); elsewhere an SVG Image. Never write symbol characters or emoji for icons: the font draws them as boxes."));
		return Out;
	}

	UObject* FindOrMakeIcon(const FString& NameOrPath, FNameTBDAIToolContext& Context, FString& OutError)
	{
		if (NameOrPath.StartsWith(TEXT("/")))
		{
			UObject* Found = NameTBDAIHelpers::FindAsset(NameOrPath, nullptr, OutError);
			if (Found && !Found->IsA<UWidgetSVG>() && !Found->IsA<UTexture>())
			{
				OutError = FString::Printf(TEXT("%s is a %s, not an SVG or a texture."), *NameOrPath, *Found->GetClass()->GetName());
				return nullptr;
			}
			return Found;
		}

		const FString Name = ResolveIconName(NameOrPath);
		if (Name.IsEmpty())
		{
			OutError = FString::Printf(TEXT("No icon '%s' (list_options icons lists them; or pass an SVG asset path)."), *NameOrPath);
			return nullptr;
		}

		// One asset per icon, shared by every widget that shows it.
		const FString Folder = NameTBDAIHelpers::ResolveFolder(TEXT("Icons"));
		const FString AssetName = FString::Printf(TEXT("SVG_Icon_%s"), *Name);
		const FString PackageName = Folder / AssetName;
		if (UWidgetSVG* Existing = LoadObject<UWidgetSVG>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			return Existing;
		}

		const TSharedPtr<FJsonObject> Icons = LoadIcons();
		const FString Group = GetString(Icons, TEXT("group"), TEXT("<g fill=\"none\" stroke=\"#000\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\">"));
		const FString Body = GetString(GetObject(Icons, TEXT("icons")), Name);
		const FString Markup = FString::Printf(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\">%s%s</g></svg>"), *Group, *Body);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("Couldn't make the package %s."), *PackageName);
			return nullptr;
		}
		Package->FullyLoad();
		UWidgetSVG* SVG = NewObject<UWidgetSVG>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FText ParseError;
		if (!SVG->SetSVGSource(Markup, ParseError))
		{
			OutError = FString::Printf(TEXT("The icon '%s' didn't parse: %s"), *Name, *ParseError.ToString());
			return nullptr;
		}
		// Drawn in white: the widget's colour is the icon's colour.
		SVG->bDrawInWhite = true;
		SVG->PostEditChange();
		FAssetRegistryModule::AssetCreated(SVG);
		Package->MarkPackageDirty();
		Context.Changes->NoteCreated(SVG, FString::Printf(TEXT("Created the %s icon"), *Name));
		return SVG;
	}

	FString IconForGlyph(const FString& Text)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		Trimmed.ReplaceInline(TEXT("️"), TEXT(""));
		if (Trimmed.IsEmpty())
		{
			return FString();
		}
		const TSharedPtr<FJsonObject> Glyphs = GetObject(LoadIcons(), TEXT("glyphs"));
		if (Glyphs.IsValid())
		{
			for (const auto& Pair : Glyphs->Values)
			{
				FString Key = Pair.Key;
				Key.ReplaceInline(TEXT("️"), TEXT(""));
				if (Key == Trimmed && Pair.Value->Type == EJson::String)
				{
					return ResolveIconName(Pair.Value->AsString());
				}
			}
		}
		return FString();
	}

	FString RemoveUndrawableGlyphs(const FString& Text, FString& OutRemoved)
	{
		FString Kept;
		OutRemoved.Reset();
		for (const TCHAR Char : Text)
		{
			if (IsUndrawable(static_cast<uint32>(Char)))
			{
				OutRemoved.AppendChar(Char);
			}
			else
			{
				Kept.AppendChar(Char);
			}
		}
		return OutRemoved.IsEmpty() ? Text : Kept.TrimStartAndEnd();
	}

	// ---- Checks ----

	void FixContrast(const TArray<UWidget*>& Widgets, bool bDark, TArray<FString>& OutNotes)
	{
		for (UWidget* Widget : Widgets)
		{
			UTextBlock* Text = Cast<UTextBlock>(Widget);
			UWidgetSVGImage* Icon = Cast<UWidgetSVGImage>(Widget);
			if (!Text && !Icon)
			{
				continue;
			}
			FLinearColor Color;
			if (Text)
			{
				const FSlateColor TextColor = Text->GetColorAndOpacity();
				if (!TextColor.IsColorSpecified())
				{
					// Follows its button's foreground (themed readable).
					continue;
				}
				Color = TextColor.GetSpecifiedColor();
			}
			else
			{
				Color = Icon->ColorAndOpacity;
			}
			if (Color.A < 0.05f)
			{
				continue;
			}
			FLinearColor Background;
			if (!FindBackground(Widget, bDark, Background))
			{
				continue;
			}
			// Seen over the background: its own alpha blends it in.
			const FLinearColor Seen = Mix(Background, WithAlpha(Color, 1.f), Color.A);
			const bool bLarge = Text && Text->GetFont().Size >= 18;
			const float Needed = (Icon || bLarge) ? 3.f : 4.5f;
			const float Ratio = ContrastRatio(Seen, Background);
			if (Ratio >= Needed)
			{
				continue;
			}
			const FLinearColor Fixed = WithAlpha(ReadableOn(Background), FMath::Max(Color.A, 0.85f));
			Widget->Modify();
			if (Text)
			{
				Text->SetColorAndOpacity(FSlateColor(Fixed));
			}
			else
			{
				Icon->SetColorAndOpacity(Fixed);
			}
			OutNotes.Add(FString::Printf(TEXT("%s: its colour %s was hard to read on %s behind it (%.1f:1); made %s."), *Widget->GetName(),
				*WithAlpha(Color, 1.f).ToFColor(true).ToHex(), *Background.ToFColor(true).ToHex(), Ratio, *WithAlpha(Fixed, 1.f).ToFColor(true).ToHex()));
		}
	}

	TArray<TSharedPtr<FJsonValue>> DescribeBoundParts(const UClass* ParentClass)
	{
		TArray<TSharedPtr<FJsonValue>> Parts;
		if (!ParentClass)
		{
			return Parts;
		}
		for (TFieldIterator<FObjectProperty> It(ParentClass); It; ++It)
		{
			const bool bRequired = It->HasMetaData(TEXT("BindWidget"));
			const bool bOptional = It->HasMetaData(TEXT("BindWidgetOptional"));
			if (!bRequired && !bOptional)
			{
				continue;
			}
			TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
			Part->SetStringField(TEXT("name"), It->GetName());
			Part->SetStringField(TEXT("class"), It->PropertyClass ? It->PropertyClass->GetName() : FString());
			Part->SetBoolField(TEXT("optional"), bOptional);
			Parts.Add(MakeShared<FJsonValueObject>(Part));
		}
		return Parts;
	}

	UClass* FindBoundPartClass(const UClass* ParentClass, const FString& Name, bool& bOutOptional)
	{
		bOutOptional = false;
		const FObjectProperty* Property = ParentClass ? FindFProperty<FObjectProperty>(ParentClass, FName(*Name)) : nullptr;
		if (!Property || !(Property->HasMetaData(TEXT("BindWidget")) || Property->HasMetaData(TEXT("BindWidgetOptional"))))
		{
			return nullptr;
		}
		bOutOptional = Property->HasMetaData(TEXT("BindWidgetOptional"));
		return Property->PropertyClass;
	}
}
