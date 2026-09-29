// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimTextHost.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Internationalization/Internationalization.h"

// =====================================================================================================
// Appearing
// =====================================================================================================

float FWidgetAnimTextAppear::Ease(float Alpha) const
{
	// Overshoot replaces the easing, as it does on a Scale or Slide track: a pop that travels past its place and settles.
	return Overshoot > 0.f
		? WidgetAnimEasing::EaseOutBack(FMath::Clamp(Alpha, 0.f, 1.f), Overshoot)
		: WidgetAnimEasing::Evaluate(Easing, nullptr, Alpha);
}

FWidgetAnimTextAppear FWidgetAnimTextRevealLook::MakeAppear(EWidgetAnimTextRevealStyle InStyle, float InAppearTime, EWidgetAnimTextSlideFrom InSlideFrom,
	float InDistance, bool bInFadeIn, const FWidgetAnimTextAppear& InCustom)
{
	if (InStyle == EWidgetAnimTextRevealStyle::Custom)
	{
		return InCustom;
	}

	// Every style starts from a part in its place, as it is, and says only what differs.
	FWidgetAnimTextAppear Appear;
	Appear.Time = FMath::Max(InAppearTime, 0.f);
	Appear.Opacity = bInFadeIn ? 0.f : 1.f;
	Appear.Offset = FVector2D::ZeroVector;
	Appear.Easing = EWidgetAnimEasing::EaseOut;

	const float Distance = FMath::Max(InDistance, 0.f);
	switch (InStyle)
	{
	case EWidgetAnimTextRevealStyle::Typewriter:
		// On at once: nothing to ease between.
		Appear.Time = 0.f;
		Appear.Opacity = 0.f;
		break;

	case EWidgetAnimTextRevealStyle::Fade:
		Appear.Opacity = 0.f;
		break;

	case EWidgetAnimTextRevealStyle::Slide:
		switch (InSlideFrom)
		{
		case EWidgetAnimTextSlideFrom::Above: Appear.Offset = FVector2D(0.0, -Distance); break;
		case EWidgetAnimTextSlideFrom::Left:  Appear.Offset = FVector2D(-Distance, 0.0); break;
		case EWidgetAnimTextSlideFrom::Right: Appear.Offset = FVector2D(Distance, 0.0); break;
		case EWidgetAnimTextSlideFrom::Below:
		default:                              Appear.Offset = FVector2D(0.0, Distance); break;
		}
		break;

	case EWidgetAnimTextRevealStyle::Pop:
		Appear.Scale = 0.f;
		Appear.Overshoot = 1.7f;
		break;

	case EWidgetAnimTextRevealStyle::Stomp:
		// Speeding up into its place and stopping dead there - which is what reads as an impact.
		Appear.Scale = 2.2f;
		Appear.Easing = EWidgetAnimEasing::EaseIn;
		break;

	case EWidgetAnimTextRevealStyle::Drop:
		Appear.Offset = FVector2D(0.0, -Distance * 1.5f);
		Appear.Overshoot = 1.2f;
		break;

	case EWidgetAnimTextRevealStyle::Spin:
		Appear.Angle = -180.f;
		Appear.Scale = 0.2f;
		break;

	case EWidgetAnimTextRevealStyle::Flip:
		Appear.Stretch = FVector2D(1.0, 0.0);
		break;

	case EWidgetAnimTextRevealStyle::Stretch:
		Appear.Stretch = FVector2D(0.0, 1.0);
		Appear.Overshoot = 0.6f;
		break;

	case EWidgetAnimTextRevealStyle::Blur:
		Appear.Opacity = 0.f;
		Appear.Blur = Distance * 0.5f;
		Appear.Scale = 1.08f;
		break;

	case EWidgetAnimTextRevealStyle::Wipe:
		Appear.Opacity = 1.f;
		Appear.bWipe = true;
		break;

	case EWidgetAnimTextRevealStyle::Scatter:
		Appear.Scatter = Distance * 3.f;
		break;

	default:
		break;
	}
	return Appear;
}

// =====================================================================================================
// WidgetAnimText
// =====================================================================================================

namespace WidgetAnimText
{
	namespace
	{
		/** The first widget at or beneath Widget that Accept takes, depth first, through panels and into User Widgets. */
		UWidget* FindFirst(UWidget* Widget, TFunctionRef<bool(const UWidget*)> Accept)
		{
			if (!Widget)
			{
				return nullptr;
			}
			if (Accept(Widget))
			{
				return Widget;
			}
			if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
			{
				return UserWidget->WidgetTree ? FindFirst(UserWidget->WidgetTree->RootWidget, Accept) : nullptr;
			}
			if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
				{
					if (UWidget* Found = FindFirst(Panel->GetChildAt(ChildIndex), Accept))
					{
						return Found;
					}
				}
			}
			return nullptr;
		}
	}

	bool IsText(const UObject* Object)
	{
		return Object && (Object->Implements<UWidgetAnimTextHost>() || Object->IsA<UTextBlock>() || Object->IsA<URichTextBlock>());
	}

	UObject* ResolveTarget(UWidget* Widget)
	{
		// A text host anywhere inside wins over a plain text found first: a button whose label is an Animated Text Block
		// but whose tooltip text sits earlier in its tree still animates its label letter by letter.
		if (UWidget* Host = FindFirst(Widget, [](const UWidget* Candidate) { return Candidate->Implements<UWidgetAnimTextHost>(); }))
		{
			return Host;
		}
		return FindFirst(Widget, [](const UWidget* Candidate) { return IsText(Candidate); });
	}

	IWidgetAnimTextHost* AsHost(UObject* Object)
	{
		return Cast<IWidgetAnimTextHost>(Object);
	}

	FText GetText(const UObject* Object)
	{
		if (const IWidgetAnimTextHost* Host = Cast<IWidgetAnimTextHost>(const_cast<UObject*>(Object)))
		{
			return Host->GetWidgetAnimText();
		}
		if (const UTextBlock* TextBlock = Cast<UTextBlock>(Object))
		{
			return TextBlock->GetText();
		}
		if (const URichTextBlock* RichText = Cast<URichTextBlock>(Object))
		{
			return RichText->GetText();
		}
		return FText::GetEmpty();
	}

	void SetText(UObject* Object, const FText& InText)
	{
		if (IWidgetAnimTextHost* Host = Cast<IWidgetAnimTextHost>(Object))
		{
			Host->SetWidgetAnimText(InText);
		}
		else if (UTextBlock* TextBlock = Cast<UTextBlock>(Object))
		{
			TextBlock->SetText(InText);
		}
		else if (URichTextBlock* RichText = Cast<URichTextBlock>(Object))
		{
			RichText->SetText(InText);
		}
	}

	FString GetDisplayString(const UObject* Object)
	{
		if (const IWidgetAnimTextHost* Host = Cast<IWidgetAnimTextHost>(const_cast<UObject*>(Object)))
		{
			return Host->GetWidgetAnimDisplayString();
		}
		if (const URichTextBlock* RichText = Cast<URichTextBlock>(Object))
		{
			FString Display;
			TArray<WidgetAnimRichText::FRun> Runs;
			WidgetAnimRichText::Parse(RichText->GetText().ToString(), Display, Runs);
			return Display;
		}
		return GetText(Object).ToString();
	}

	bool ParseNumber(const FString& Text, double& OutValue)
	{
		// The separators numbers are written with - the locale's, which FText::AsNumber writes them in: "1.250,5" is a
		// number in German.
		TCHAR Decimal = TEXT('.');
		TCHAR Grouping = TEXT(',');
		const FCulturePtr Culture = FInternationalization::Get().GetCurrentLocale();
		if (Culture.IsValid())
		{
			const FDecimalNumberFormattingRules& Rules = Culture->GetDecimalNumberFormattingRules();
			Decimal = Rules.DecimalSeparatorCharacter;
			Grouping = Rules.GroupingSeparatorCharacter;
		}

		const int32 Length = Text.Len();
		for (int32 Start = 0; Start < Length; ++Start)
		{
			if (!FChar::IsDigit(Text[Start]))
			{
				continue;
			}

			FString Digits;
			const bool bNegative = Start > 0 && (Text[Start - 1] == TEXT('-') || Text[Start - 1] == TEXT('\x2212'));
			if (bNegative)
			{
				Digits.AppendChar(TEXT('-'));
			}

			bool bSeenDecimal = false;
			for (int32 Index = Start; Index < Length; ++Index)
			{
				const TCHAR Char = Text[Index];
				const bool bNextIsDigit = Index + 1 < Length && FChar::IsDigit(Text[Index + 1]);
				if (FChar::IsDigit(Char))
				{
					Digits.AppendChar(Char);
				}
				else if (Char == Decimal && !bSeenDecimal && bNextIsDigit)
				{
					Digits.AppendChar(TEXT('.'));
					bSeenDecimal = true;
				}
				else if ((Char == Grouping || Char == TEXT('\x00A0') || Char == TEXT('\x202F')) && !bSeenDecimal && bNextIsDigit)
				{
					// A thousands separator, skipped.
				}
				else
				{
					break;
				}
			}

			OutValue = FCString::Atod(*Digits);
			return true;
		}
		return false;
	}

	bool GetNumber(const UObject* Object, double& OutValue)
	{
		if (const IWidgetAnimTextHost* Host = Cast<IWidgetAnimTextHost>(const_cast<UObject*>(Object)))
		{
			if (Host->GetWidgetAnimNumber(OutValue))
			{
				return true;
			}
		}
		return ParseNumber(GetDisplayString(Object), OutValue);
	}

	bool GetColor(const UObject* Object, FLinearColor& OutColor)
	{
		const UTextBlock* TextBlock = Cast<UTextBlock>(Object);
		if (!TextBlock || TextBlock->Implements<UWidgetAnimTextHost>())
		{
			return false;
		}
		OutColor = TextBlock->GetColorAndOpacity().GetSpecifiedColor();
		return true;
	}

	void SetColor(UObject* Object, const FLinearColor& InColor)
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(Object);
		if (TextBlock && !TextBlock->Implements<UWidgetAnimTextHost>())
		{
			TextBlock->SetColorAndOpacity(FSlateColor(InColor));
		}
	}
}

// =====================================================================================================
// WidgetAnimRichText
// =====================================================================================================

void WidgetAnimRichText::Parse(const FString& Markup, FString& OutDisplay, TArray<FRun>& OutRuns)
{
	OutDisplay.Reset(Markup.Len());
	OutRuns.Reset();

	FName CurrentStyle;
	int32 RunBegin = 0;

	auto CloseRun = [&OutDisplay, &OutRuns, &RunBegin, &CurrentStyle]()
	{
		if (OutDisplay.Len() > RunBegin)
		{
			FRun& Run = OutRuns.AddDefaulted_GetRef();
			Run.Begin = RunBegin;
			Run.End = OutDisplay.Len();
			Run.Style = CurrentStyle;
		}
		RunBegin = OutDisplay.Len();
	};

	auto IsNameChar = [](TCHAR Char) { return FChar::IsAlnum(Char) || Char == TEXT('_') || Char == TEXT('.') || Char == TEXT('-'); };

	const int32 Length = Markup.Len();
	int32 Index = 0;
	while (Index < Length)
	{
		const TCHAR Char = Markup[Index];

		// Escapes, as the Rich Text Block's own parser turns them back.
		if (Char == TEXT('&'))
		{
			struct FEscape
			{
				const TCHAR* Key;
				TCHAR Value;
			};
			static const FEscape Escapes[] =
			{
				{ TEXT("&lt;"), TEXT('<') }, { TEXT("&gt;"), TEXT('>') }, { TEXT("&amp;"), TEXT('&') },
				{ TEXT("&quot;"), TEXT('"') }, { TEXT("&apos;"), TEXT('\'') }
			};
			bool bEscaped = false;
			for (const FEscape& Escape : Escapes)
			{
				const int32 EscapeLength = FCString::Strlen(Escape.Key);
				if (FCString::Strncmp(*Markup + Index, Escape.Key, EscapeLength) == 0)
				{
					OutDisplay.AppendChar(Escape.Value);
					Index += EscapeLength;
					bEscaped = true;
					break;
				}
			}
			if (!bEscaped)
			{
				OutDisplay.AppendChar(Char);
				++Index;
			}
			continue;
		}

		if (Char == TEXT('<'))
		{
			// A closing tag - </> or </Name> - ends the styled run.
			if (Index + 1 < Length && Markup[Index + 1] == TEXT('/'))
			{
				const int32 Close = Markup.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Index);
				if (Close != INDEX_NONE && !CurrentStyle.IsNone())
				{
					CloseRun();
					CurrentStyle = NAME_None;
					Index = Close + 1;
					continue;
				}
			}
			// An opening tag - <Name> or <Name attribute="..."> - starts one; a self-closing one is a decorator, left out.
			else if (Index + 1 < Length && IsNameChar(Markup[Index + 1]))
			{
				int32 NameEnd = Index + 1;
				while (NameEnd < Length && IsNameChar(Markup[NameEnd]))
				{
					++NameEnd;
				}
				const int32 Close = Markup.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameEnd);
				if (Close != INDEX_NONE)
				{
					const bool bSelfClosing = Close > Index && Markup[Close - 1] == TEXT('/');
					if (!bSelfClosing)
					{
						CloseRun();
						CurrentStyle = FName(*Markup.Mid(Index + 1, NameEnd - Index - 1));
					}
					Index = Close + 1;
					continue;
				}
			}
		}

		// Anything else - a '<' that opens no tag included - is text.
		OutDisplay.AppendChar(Char);
		++Index;
	}

	CloseRun();
}
