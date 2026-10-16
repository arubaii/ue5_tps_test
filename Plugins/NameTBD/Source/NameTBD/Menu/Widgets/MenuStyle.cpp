// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuStyle.h"

#include "Materials/MaterialInterface.h"
#include "Styling/CoreStyle.h"
#include "NameTBD/Menu/Settings/MenuProjectSettings.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"

namespace MenuStyleLocal
{
	FSlateFontInfo Font(const ANSICHAR* Weight, int32 Size)
	{
		return FCoreStyle::GetDefaultFontStyle(Weight, Size);
	}

	FMenuStylePalette HighContrast()
	{
		FMenuStylePalette Palette;
		Palette.Background = FLinearColor(0.f, 0.f, 0.f, 0.97f);
		Palette.Surface = FLinearColor::Black;
		Palette.Row = FLinearColor(1.f, 1.f, 1.f, 0.f);
		Palette.RowHovered = FLinearColor(1.f, 1.f, 1.f, 0.12f);
		Palette.RowFocused = FLinearColor(1.f, 0.85f, 0.f, 0.22f);
		Palette.Accent = FLinearColor(1.f, 0.85f, 0.f, 1.f);
		Palette.AccentText = FLinearColor::Black;
		Palette.Text = FLinearColor::White;
		Palette.MutedText = FLinearColor(0.9f, 0.9f, 0.9f, 1.f);
		Palette.DisabledText = FLinearColor(0.6f, 0.6f, 0.6f, 1.f);
		Palette.Divider = FLinearColor(1.f, 1.f, 1.f, 0.5f);
		Palette.Track = FLinearColor(1.f, 1.f, 1.f, 0.45f);
		Palette.FocusOutline = FLinearColor(1.f, 0.85f, 0.f, 1.f);
		Palette.Warning = FLinearColor(1.f, 0.6f, 0.f, 1.f);
		Palette.Negative = FLinearColor(1.f, 0.25f, 0.25f, 1.f);
		Palette.Scrim = FLinearColor(0.f, 0.f, 0.f, 0.85f);
		return Palette;
	}
}

// =====================================================================================================
//  Skin
// =====================================================================================================

bool FMenuPartSkin::IsSet() const
{
	return Normal.IsSet() || (bHoveredImage && Hovered.IsSet()) || (bFocusedImage && Focused.IsSet()) || (bPressedImage && Pressed.IsSet())
		|| (bSelectedImage && Selected.IsSet()) || (bDisabledImage && Disabled.IsSet());
}

const FWidgetEffectImage& FMenuPartSkin::ForState(bool bIsDisabled, bool bIsPressed, bool bIsSelected, bool bIsFocused, bool bIsHovered) const
{
	if (bIsDisabled && bDisabledImage && Disabled.IsSet())
	{
		return Disabled;
	}
	if (bIsPressed && bPressedImage && Pressed.IsSet())
	{
		return Pressed;
	}
	if (bIsSelected && bSelectedImage && Selected.IsSet())
	{
		return Selected;
	}
	if (bIsFocused && bFocusedImage && Focused.IsSet())
	{
		return Focused;
	}
	if (bIsHovered && bHoveredImage && Hovered.IsSet())
	{
		return Hovered;
	}
	return Normal;
}

bool FMenuPartSkin::UsesMaterial() const
{
	const FWidgetEffectImage* const Images[] = { &Normal, &Hovered, &Focused, &Pressed, &Selected, &Disabled };
	for (const FWidgetEffectImage* Image : Images)
	{
		if (Image->GetMaterial() != nullptr)
		{
			return true;
		}
	}
	return false;
}

bool FMenuStyleSkin::UsesMaterial() const
{
	for (TFieldIterator<FStructProperty> It(FMenuStyleSkin::StaticStruct()); It; ++It)
	{
		if (It->Struct == FMenuPartSkin::StaticStruct() && It->ContainerPtrToValuePtr<FMenuPartSkin>(this)->UsesMaterial())
		{
			return true;
		}
	}
	return false;
}

// =====================================================================================================
//  Custom widgets
// =====================================================================================================

bool FMenuWidgetRule::Matches(const UMenuSetting& Setting, FName PageId, FName SectionId, const FText& SectionTitle, EMenuChoiceDisplay ResolvedDisplay) const
{
	if (WidgetClass.IsNull())
	{
		return false;
	}
	if (SettingIds.Num() > 0)
	{
		const FString Id = Setting.Id.ToString();
		const bool bAny = SettingIds.ContainsByPredicate([&Id](const FString& Pattern)
		{
			return Pattern.Contains(TEXT("*")) || Pattern.Contains(TEXT("?")) ? Id.MatchesWildcard(Pattern) : Id.Equals(Pattern, ESearchCase::IgnoreCase);
		});
		if (!bAny)
		{
			return false;
		}
	}
	if (Sections.Num() > 0)
	{
		const FString Title = SectionTitle.ToString();
		const bool bAny = Sections.ContainsByPredicate([SectionId, &Title](const FName& Wanted)
		{
			return Wanted == SectionId || Wanted.ToString().Equals(Title, ESearchCase::IgnoreCase);
		});
		if (!bAny)
		{
			return false;
		}
	}
	if (Pages.Num() > 0 && !Pages.Contains(PageId))
	{
		return false;
	}
	if (bMatchKind && Setting.GetKind() != Kind)
	{
		return false;
	}
	if (bMatchChoiceDisplay && (Setting.GetKind() != EMenuSettingKind::Choice || ResolvedDisplay != ChoiceDisplay))
	{
		return false;
	}
	return true;
}

TSoftClassPtr<UMenuSettingRowWidget> FMenuStyleWidgets::FindRule(EMenuWidgetRulePart Part, const UMenuSetting& Setting, FName PageId, FName SectionId,
	const FText& SectionTitle, EMenuChoiceDisplay ResolvedDisplay) const
{
	for (const FMenuWidgetRule& Rule : Rules)
	{
		if (Rule.Part == Part && Rule.Matches(Setting, PageId, SectionId, SectionTitle, ResolvedDisplay))
		{
			return Rule.WidgetClass;
		}
	}
	return nullptr;
}

FMenuStyle::FMenuStyle()
{
	using namespace MenuStyleLocal;
	TitleFont = Font("Bold", 30);
	TabFont = Font("Bold", 15);
	SectionFont = Font("Bold", 12);
	LabelFont = Font("Regular", 15);
	ValueFont = Font("Bold", 14);
	DescriptionTitleFont = Font("Bold", 20);
	DescriptionFont = Font("Regular", 13);
	HintFont = Font("Regular", 12);
	ButtonFont = Font("Bold", 13);
	HighContrastColors = HighContrast();
	KeyCap.Font = Font("Bold", 11);
}

const FMenuStylePalette& FMenuStyle::GetPalette() const
{
	return MenuAccessibility::IsHighContrastEnabled() ? HighContrastColors : Colors;
}

FMenuStyle FMenuStyle::Make(EMenuStylePreset Preset)
{
	using namespace MenuStyleLocal;
	FMenuStyle Out;
	switch (Preset)
	{
	case EMenuStylePreset::Light:
		Out.Colors.Background = FLinearColor(0.93f, 0.94f, 0.96f, 0.97f);
		Out.Colors.Surface = FLinearColor::White;
		Out.Colors.Row = FLinearColor(0.f, 0.f, 0.f, 0.03f);
		Out.Colors.RowHovered = FLinearColor(0.f, 0.f, 0.f, 0.06f);
		Out.Colors.RowFocused = FLinearColor(0.1f, 0.35f, 1.f, 0.12f);
		Out.Colors.Accent = FLinearColor(0.08f, 0.32f, 0.95f, 1.f);
		Out.Colors.Text = FLinearColor(0.06f, 0.07f, 0.09f, 1.f);
		Out.Colors.MutedText = FLinearColor(0.32f, 0.35f, 0.4f, 1.f);
		Out.Colors.DisabledText = FLinearColor(0.f, 0.f, 0.f, 0.3f);
		Out.Colors.Divider = FLinearColor(0.f, 0.f, 0.f, 0.08f);
		Out.Colors.Track = FLinearColor(0.f, 0.f, 0.f, 0.14f);
		Out.Colors.FocusOutline = FLinearColor(0.08f, 0.32f, 0.95f, 0.9f);
		Out.Colors.Scrim = FLinearColor(0.f, 0.f, 0.f, 0.35f);
		Out.KeyCap.CapColor = FLinearColor(0.f, 0.f, 0.f, 0.06f);
		Out.KeyCap.OutlineColor = FLinearColor(0.f, 0.f, 0.f, 0.4f);
		Out.KeyCap.TextColor = Out.Colors.Text;
		break;
	case EMenuStylePreset::Glass:
		Out.Colors.Background = FLinearColor(0.02f, 0.03f, 0.05f, 0.35f);
		Out.Colors.Surface = FLinearColor(0.06f, 0.07f, 0.1f, 0.75f);
		Out.Colors.Row = FLinearColor(1.f, 1.f, 1.f, 0.05f);
		Out.Colors.RowHovered = FLinearColor(1.f, 1.f, 1.f, 0.1f);
		Out.Colors.RowFocused = FLinearColor(1.f, 1.f, 1.f, 0.16f);
		Out.Colors.Accent = FLinearColor(0.45f, 0.85f, 1.f, 1.f);
		Out.Colors.AccentText = FLinearColor(0.02f, 0.04f, 0.06f, 1.f);
		Out.Colors.FocusOutline = FLinearColor(1.f, 1.f, 1.f, 0.55f);
		Out.Colors.Divider = FLinearColor(1.f, 1.f, 1.f, 0.12f);
		Out.BackgroundBlur = 18.f;
		Out.CornerRadius = 14.f;
		Out.TabStyle = EMenuTabStyle::Pills;
		break;
	case EMenuStylePreset::Minimal:
		Out.Colors.Background = FLinearColor(0.f, 0.f, 0.f, 0.78f);
		Out.Colors.Surface = FLinearColor(0.03f, 0.03f, 0.03f, 0.95f);
		Out.Colors.Row = FLinearColor(1.f, 1.f, 1.f, 0.f);
		Out.Colors.RowHovered = FLinearColor(1.f, 1.f, 1.f, 0.04f);
		Out.Colors.RowFocused = FLinearColor(1.f, 1.f, 1.f, 0.08f);
		Out.Colors.Accent = FLinearColor(0.95f, 0.75f, 0.35f, 1.f);
		Out.Colors.AccentText = FLinearColor::Black;
		Out.Colors.FocusOutline = FLinearColor(0.95f, 0.75f, 0.35f, 0.f);
		Out.CornerRadius = 2.f;
		Out.FocusOutlineWidth = 0.f;
		Out.bShowPips = false;
		Out.BoolDisplay = EMenuBoolDisplay::Selector;
		Out.TitleFont = Font("Light", 34);
		Out.TabFont = Font("Regular", 15);
		break;
	case EMenuStylePreset::Console:
		Out.ScreenPadding = FMargin(96.f, 64.f);
		Out.RowHeight = 68.f;
		Out.RowSpacing = 6.f;
		Out.ValueWidth = 420.f;
		Out.DescriptionWidth = 480.f;
		Out.TitleFont = Font("Bold", 38);
		Out.TabFont = Font("Bold", 19);
		Out.SectionFont = Font("Bold", 15);
		Out.LabelFont = Font("Regular", 20);
		Out.ValueFont = Font("Bold", 19);
		Out.DescriptionTitleFont = Font("Bold", 26);
		Out.DescriptionFont = Font("Regular", 17);
		Out.HintFont = Font("Regular", 16);
		Out.ButtonFont = Font("Bold", 17);
		Out.KeyCap.Height = 34.f;
		Out.KeyCap.Font = Font("Bold", 14);
		Out.FocusOutlineWidth = 2.5f;
		Out.TabStyle = EMenuTabStyle::Pills;
		Out.BoolDisplay = EMenuBoolDisplay::Selector;
		Out.bShowSearch = false;
		break;
	case EMenuStylePreset::HighContrast:
		Out.Colors = HighContrast();
		Out.FocusOutlineWidth = 3.f;
		Out.OutlineWidth = 2.f;
		Out.TabStyle = EMenuTabStyle::Boxed;
		Out.KeyCap.CapColor = FLinearColor::Black;
		Out.KeyCap.OutlineColor = FLinearColor::White;
		Out.KeyCap.OutlineWidth = 2.f;
		break;
	default:
		break;
	}
	return Out;
}

UMenuStyleAsset::UMenuStyleAsset()
{
	Style = FMenuStyle::Make(Preset);
}

void UMenuStyleAsset::ApplyPreset(EMenuStylePreset InPreset)
{
	Preset = InPreset;
	if (InPreset != EMenuStylePreset::Custom)
	{
		Style = FMenuStyle::Make(InPreset);
	}
	NotifyStyleChanged();
}

void UMenuStyleAsset::NotifyStyleChanged()
{
	OnStyleChanged().Broadcast(this);
}

TMulticastDelegate<void(const UMenuStyleAsset*)>& UMenuStyleAsset::OnStyleChanged()
{
	static TMulticastDelegate<void(const UMenuStyleAsset*)> Delegate;
	return Delegate;
}

const FMenuStyle& UMenuStyleAsset::Resolve(const UMenuStyleAsset* Asset)
{
	if (Asset)
	{
		return Asset->Style;
	}
	if (const UMenuStyleAsset* Project = UMenuProjectSettings::Get().DefaultStyle.LoadSynchronous())
	{
		return Project->Style;
	}
	static const FMenuStyle Simple = FMenuStyle::Make(EMenuStylePreset::Simple);
	return Simple;
}

#if WITH_EDITOR
void UMenuStyleAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMenuStyleAsset, Preset))
	{
		ApplyPreset(Preset);
		return;
	}
	NotifyStyleChanged();
}
#endif
