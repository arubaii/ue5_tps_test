// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCountUp.h"

#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimCountUp"

const FName FWidgetAnimCountUpEvaluator::TextChannel(TEXT("Text"));

FText FWidgetAnimCountUpEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Count Up");
}

FText FWidgetAnimCountUpEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Counts a number on a Text Block from its Start Value to its End Value - through any keys on the way - and puts the block's own text back afterwards. Aimed at a widget that is not a Text Block, it counts on the first Text Block inside it.");
}

FText FWidgetAnimCountUpEvaluator::GetValueLabel() const
{
	return LOCTEXT("ValueLabel", "Count");
}

UClass* FWidgetAnimCountUpEvaluator::GetTargetClass() const
{
	return UTextBlock::StaticClass();
}

double FWidgetAnimCountUpEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimCountUpParams* CountUp = GetParams(Params);
	if (!CountUp)
	{
		return 0.0;
	}
	return bStart ? CountUp->StartValue : CountUp->EndValue;
}

double FWidgetAnimCountUpEvaluator::GetQuantizeStep(const FInstancedStruct& Params) const
{
	// One step of the last digit shown: the text only changes when the value crosses one, so that is the only time
	// it needs formatting and setting.
	const FWidgetAnimCountUpParams* CountUp = GetParams(Params);
	const int32 Digits = CountUp ? FMath::Clamp(CountUp->Decimals, 0, 6) : 0;
	return FMath::Pow(10.0, -static_cast<double>(Digits));
}

FText FWidgetAnimCountUpEvaluator::FormatValue(const FWidgetAnimCountUpParams& Params, double Value)
{
	const int32 Digits = FMath::Clamp(Params.Decimals, 0, 6);

	// Rounded here, not left to the formatter alone, so a value a hair either side of zero can be told apart from a
	// real negative: -0.0001 counting up to 0 would otherwise read "-0" for a frame.
	const double Scale = FMath::Pow(10.0, static_cast<double>(Digits));
	double Rounded = FMath::RoundHalfFromZero(Value * Scale) / Scale;
	if (Rounded == 0.0)
	{
		Rounded = 0.0;
	}

	FNumberFormattingOptions Options;
	Options.SetUseGrouping(Params.bUseGrouping);
	Options.SetMinimumFractionalDigits(Digits);
	Options.SetMaximumFractionalDigits(Digits);
	Options.SetRoundingMode(ERoundingMode::HalfFromZero);

	const FText Number = FText::AsNumber(Rounded, &Options);
	if (Params.Format.IsEmpty())
	{
		return Number;
	}

	FFormatNamedArguments Arguments;
	Arguments.Add(TEXT("Value"), Number);
	return FText::Format(Params.Format, Arguments);
}

void FWidgetAnimCountUpEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	UTextBlock* TextBlock = Cast<UTextBlock>(Target);
	const FWidgetAnimCountUpParams* CountUp = GetParams(Params);
	if (!TextBlock || !CountUp)
	{
		return;
	}

	const FText NewText = FormatValue(*CountUp, Value);

	// Only when the text actually changes. Setting it invalidates the block's layout, and a count that is holding -
	// or crawling across a whole number over several frames - would otherwise re-lay the block out every frame.
	if (!TextBlock->GetText().EqualTo(NewText))
	{
		TextBlock->SetText(NewText);
	}
}

bool FWidgetAnimCountUpEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const UTextBlock* TextBlock = Cast<UTextBlock>(Target);
	if (!TextBlock)
	{
		return false;
	}

	FWidgetAnimTextBaseline Baseline;
	Baseline.Text = TextBlock->GetText();
	OutBaseline.InitializeAs<FWidgetAnimTextBaseline>(Baseline);
	return true;
}

void FWidgetAnimCountUpEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	UTextBlock* TextBlock = Cast<UTextBlock>(Target);
	const FWidgetAnimTextBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextBaseline>();
	if (TextBlock && Captured && !TextBlock->GetText().EqualTo(Captured->Text))
	{
		TextBlock->SetText(Captured->Text);
	}
}

#undef LOCTEXT_NAMESPACE