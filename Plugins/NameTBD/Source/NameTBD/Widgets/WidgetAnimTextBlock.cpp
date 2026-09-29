// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextBlock.h"

#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "SWidgetAnimText.h"

UWidgetAnimTextBlock::UWidgetAnimTextBlock(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::Text);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UWidgetAnimTextBlock)
WIDGETANIM_TEXT_WIDGET_TEXTBLOCK_IMPL(UWidgetAnimTextBlock)
WIDGETANIM_TEXT_WIDGET_SETTEXT_IMPL(UWidgetAnimTextBlock)
