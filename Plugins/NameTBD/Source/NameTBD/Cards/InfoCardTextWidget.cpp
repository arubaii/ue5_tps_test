// Copyright AR GameSystems 2026. All rights reserved.

#include "InfoCardTextWidget.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace InfoCardTextDefaults
{
	/** The engine's default UI font (Roboto) as an asset, so a saved style keeps a real font. Constructors only. */
	FSlateFontInfo MakeFont(const TCHAR* Typeface, int32 Size)
	{
		static ConstructorHelpers::FObjectFinder<UFont> DefaultFont(*UWidget::GetDefaultFontName());
		if (DefaultFont.Object)
		{
			return FSlateFontInfo(DefaultFont.Object, Size, FName(Typeface));
		}
		return FCoreStyle::GetDefaultFontStyle(FName(Typeface), Size);
	}
}

FInfoCardTextStyle::FInfoCardTextStyle()
{
	Background.DrawAs = ESlateBrushDrawType::RoundedBox;
	Background.TintColor = FSlateColor(FLinearColor(0.016f, 0.017f, 0.021f, 0.96f));
	Background.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
	Background.OutlineSettings.Color = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.08f));
	Background.OutlineSettings.Width = 1.f;
	Background.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;

	// Replaced with the font asset by the UObjects that hold a style (see InfoCardTextDefaults::MakeFont); this is only the
	// fallback for a style made in code.
	TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11);
	BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
}

UInfoCardPayload_TextStyle::UInfoCardPayload_TextStyle()
{
	if (!IsRunningDedicatedServer())
	{
		TextStyle.TitleFont = InfoCardTextDefaults::MakeFont(TEXT("Bold"), 11);
		TextStyle.BodyFont = InfoCardTextDefaults::MakeFont(TEXT("Regular"), 10);
	}
}

UInfoCardTextWidget::UInfoCardTextWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!IsRunningDedicatedServer())
	{
		TextCardStyle.TitleFont = InfoCardTextDefaults::MakeFont(TEXT("Bold"), 11);
		TextCardStyle.BodyFont = InfoCardTextDefaults::MakeFont(TEXT("Regular"), 10);
	}
	ActiveTextStyle = TextCardStyle;
}

bool UInfoCardTextWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// Only the C++ class builds its own layout. A Widget Blueprint made from it owns its tree (even an empty one), and
	// widgets added here would be ones that tree has no GUIDs for.
	if (WidgetTree && !WidgetTree->RootWidget && !Cast<UWidgetBlueprintGeneratedClass>(GetClass()))
	{
		BuildDefaultTree();
	}

	ActiveTextStyle = TextCardStyle;
	ApplyActiveTextStyle();
	return true;
}

void UInfoCardTextWidget::BuildDefaultTree()
{
	CardBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CardBackground"));
	CardWidthBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CardWidthBox"));
	UVerticalBox* Lines = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Lines"));
	TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TitleText"));
	BodyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("BodyText"));

	WidgetTree->RootWidget = CardBackground;
	CardBackground->SetContent(CardWidthBox);
	CardWidthBox->SetContent(Lines);
	Lines->AddChildToVerticalBox(TitleText);
	Lines->AddChildToVerticalBox(BodyText);

	TitleText->SetAutoWrapText(true);
	BodyText->SetAutoWrapText(true);
}

void UInfoCardTextWidget::ApplyActiveTextStyle()
{
	if (CardBackground)
	{
		CardBackground->SetBrush(ActiveTextStyle.Background);
		CardBackground->SetPadding(ActiveTextStyle.Padding);
	}

	if (CardWidthBox)
	{
		if (ActiveTextStyle.MaxWidth > 0.f)
		{
			CardWidthBox->SetMaxDesiredWidth(ActiveTextStyle.MaxWidth);
		}
		else
		{
			CardWidthBox->ClearMaxDesiredWidth();
		}
	}

	if (TitleText)
	{
		TitleText->SetFont(ActiveTextStyle.TitleFont);
		TitleText->SetColorAndOpacity(ActiveTextStyle.TitleColor);
		TitleText->SetJustification(ActiveTextStyle.Justification);
	}

	if (BodyText)
	{
		BodyText->SetFont(ActiveTextStyle.BodyFont);
		BodyText->SetColorAndOpacity(ActiveTextStyle.BodyColor);
		BodyText->SetJustification(ActiveTextStyle.Justification);

		// The gap only exists while both lines show; SetCardTexts keeps it right as either one empties.
		if (UVerticalBoxSlot* BodySlot = Cast<UVerticalBoxSlot>(BodyText->Slot))
		{
			const bool bTitleShown = TitleText && TitleText->GetVisibility() != ESlateVisibility::Collapsed;
			BodySlot->SetPadding(FMargin(0.f, bTitleShown ? ActiveTextStyle.LineGap : 0.f, 0.f, 0.f));
		}
	}
}

void UInfoCardTextWidget::SetCardTexts(FText InTitle, FText InBody)
{
	if (TitleText)
	{
		TitleText->SetText(InTitle);
		TitleText->SetVisibility(InTitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (BodyText)
	{
		BodyText->SetText(InBody);
		BodyText->SetVisibility(InBody.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	ApplyActiveTextStyle();
}

void UInfoCardTextWidget::SetTextCardStyle(const FInfoCardTextStyle& NewTextStyle)
{
	ActiveTextStyle = NewTextStyle;
	ApplyActiveTextStyle();
}

void UInfoCardTextWidget::ApplyPayloads_Implementation(const TArray<UNameTBD_Payload*>& InPayloads)
{
	Super::ApplyPayloads_Implementation(InPayloads);

	const UInfoCardPayload_Text* TextPayload = nullptr;
	const UInfoCardPayload_TextStyle* StylePayload = nullptr;
	for (const UNameTBD_Payload* Payload : InPayloads)
	{
		if (!TextPayload)
		{
			TextPayload = Cast<UInfoCardPayload_Text>(Payload);
		}
		if (!StylePayload)
		{
			StylePayload = Cast<UInfoCardPayload_TextStyle>(Payload);
		}
	}

	// A card re-delivered its payloads (Refresh Card, a Tooltip Card Anchor's text changing) goes back to its own style when
	// the style payload is gone, rather than keeping the last one it was handed.
	ActiveTextStyle = StylePayload ? StylePayload->TextStyle : TextCardStyle;

	SetCardTexts(TextPayload ? TextPayload->Title : FText::GetEmpty(), TextPayload ? TextPayload->Body : FText::GetEmpty());
}
