// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCommonText.h"

#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "NameTBD/Widgets/SWidgetAnimText.h"

// =====================================================================================================
// Common Animated Text
// =====================================================================================================

UCommonAnimatedText::UCommonAnimatedText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::Text);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UCommonAnimatedText)
WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(UCommonAnimatedText)
WIDGETANIM_TEXT_WIDGET_SETTEXT_IMPL(UCommonAnimatedText)

// =====================================================================================================
// Common Animated Rich Text
// =====================================================================================================

UCommonAnimatedRichText::UCommonAnimatedRichText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::RichText);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UCommonAnimatedRichText)
WIDGETANIM_TEXT_WIDGET_RICHTEXT_IMPL(UCommonAnimatedRichText)

// =====================================================================================================
// Common Animated Numeric Text
// =====================================================================================================

UCommonAnimatedNumericText::UCommonAnimatedNumericText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::Numeric);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UCommonAnimatedNumericText)
WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(UCommonAnimatedNumericText)

bool UCommonAnimatedNumericText::GetWidgetAnimNumber(double& OutValue) const
{
	// The value it shows now, not one it is interpolating toward.
	OutValue = CurrentNumericValue;
	return true;
}

bool UCommonAnimatedNumericText::SetWidgetAnimNumber(double Value)
{
	const float NewValue = static_cast<float>(Value);
	if (NewValue != CurrentNumericValue)
	{
		SetCurrentValue(NewValue);
		TextCore.NotifyTextChanged();
	}
	return true;
}

// =====================================================================================================
// Common Animated Date Time Text
// =====================================================================================================

UCommonAnimatedDateTimeText::UCommonAnimatedDateTimeText(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::DateTime);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UCommonAnimatedDateTimeText)
WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(UCommonAnimatedDateTimeText)
