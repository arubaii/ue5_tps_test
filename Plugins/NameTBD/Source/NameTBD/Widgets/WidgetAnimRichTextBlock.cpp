// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimRichTextBlock.h"

#include "NameTBD/WidgetAnimation/WidgetAnimationComponent.h"
#include "SWidgetAnimText.h"

UWidgetAnimRichTextBlock::UWidgetAnimRichTextBlock(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetupTextCore(EWidgetAnimTextWidgetKind::RichText);
}

WIDGETANIM_TEXT_WIDGET_IMPL(UWidgetAnimRichTextBlock)
WIDGETANIM_TEXT_WIDGET_RICHTEXT_IMPL(UWidgetAnimRichTextBlock)
