// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimCountUp.h"

#define LOCTEXT_NAMESPACE "WidgetAnimCountUp"

const FName FWidgetAnimCountUpEvaluator::TextChannel(TEXT("Text"));

namespace WidgetAnimCount
{
	/** Whether Target is a numeric text taking the number itself, for Params. */
	IWidgetAnimTextHost* GetNumberTaker(UObject* Target, const FWidgetAnimCountUpParams& Params)
	{
		IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target);
		double Unused = 0.0;
		return Params.bUseWidgetFormat && Host && Host->GetWidgetAnimNumber(Unused) ? Host : nullptr;
	}

	/** Value rounded to Digits after the point, half away from zero, and never a negative zero. */
	double Round(double Value, int32 Digits)
	{
		const double Scale = FMath::Pow(10.0, static_cast<double>(Digits));
		const double Rounded = FMath::RoundHalfFromZero(Value * Scale) / Scale;
		return Rounded == 0.0 ? 0.0 : Rounded;
	}

	FText FormatNumber(double Value, int32 Digits, bool bGrouping)
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(bGrouping);
		Options.SetMinimumFractionalDigits(Digits);
		Options.SetMaximumFractionalDigits(Digits);
		Options.SetRoundingMode(ERoundingMode::HalfFromZero);
		return FText::AsNumber(Round(Value, Digits), &Options);
	}

	FText FormatCompact(double Value, int32 Digits)
	{
		static const TCHAR* Suffixes[] = { TEXT(""), TEXT("K"), TEXT("M"), TEXT("B"), TEXT("T") };
		constexpr int32 LastTier = static_cast<int32>(UE_ARRAY_COUNT(Suffixes)) - 1;
		double Scaled = FMath::Abs(Value);
		int32 Tier = 0;
		while (Scaled >= 1000.0 && Tier < LastTier)
		{
			Scaled /= 1000.0;
			++Tier;
		}
		// 999,950 rounds up to 1000.0K: shown as 1.0M instead.
		if (Tier < LastTier && Round(Scaled, Digits) >= 1000.0)
		{
			Scaled /= 1000.0;
			++Tier;
		}

		// Whole numbers below a thousand have no decimals to show.
		const int32 Shown = Tier == 0 ? 0 : Digits;
		const FText Number = FormatNumber(Value < 0.0 ? -Scaled : Scaled, Shown, /*bGrouping=*/false);
		return Tier == 0 ? Number : FText::Format(INVTEXT("{0}{1}"), Number, FText::FromString(Suffixes[Tier]));
	}

	FText FormatTime(double Seconds, bool bHours, bool bHundredths)
	{
		const bool bNegative = Seconds < 0.0;
		double Remaining = FMath::Abs(Seconds);

		// Hundredths are rounded to; whole seconds are counted down to (a timer shows 0:01 until it reaches 0).
		int64 TotalHundredths = static_cast<int64>(FMath::RoundHalfFromZero(Remaining * 100.0));
		int64 TotalSeconds = bHundredths ? TotalHundredths / 100 : static_cast<int64>(FMath::FloorToDouble(Remaining + 1.e-6));
		const int64 Hundredths = TotalHundredths % 100;

		int64 Hours = 0;
		if (bHours)
		{
			Hours = TotalSeconds / 3600;
			TotalSeconds %= 3600;
		}
		const int64 Minutes = TotalSeconds / 60;
		const int64 Secs = TotalSeconds % 60;

		FString Out = bNegative && (TotalSeconds > 0 || Hours > 0 || Minutes > 0 || (bHundredths && Hundredths > 0)) ? TEXT("-") : TEXT("");
		if (bHours)
		{
			Out += FString::Printf(TEXT("%lld:%02lld:%02lld"), Hours, Minutes, Secs);
		}
		else
		{
			Out += FString::Printf(TEXT("%lld:%02lld"), Minutes, Secs);
		}
		if (bHundredths)
		{
			Out += FString::Printf(TEXT(".%02lld"), Hundredths);
		}
		return FText::FromString(Out);
	}
}

FText FWidgetAnimCountUpEvaluator::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "Count");
}

FText FWidgetAnimCountUpEvaluator::GetDescription() const
{
	return LOCTEXT("Description", "Counts a number from its start to its end - through any keys on the way - as a number, a percentage, a shortened 12.5K, or a time. Its pace is its Duration, or so many a second, or longer the bigger the change. It can carry on from the number shown and count to the number the text was just given. Puts the text back afterwards.");
}

FText FWidgetAnimCountUpEvaluator::GetValueLabel() const
{
	return LOCTEXT("ValueLabel", "Count");
}

// =====================================================================================================
// Play-time length
// =====================================================================================================

bool FWidgetAnimCountUpEvaluator::NeedsPlayPreparation(const FInstancedStruct& Params) const
{
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	return Count && (Count->CountFrom != EWidgetAnimCountFrom::StartValue || Count->CountTo != EWidgetAnimCountTo::EndValue
		|| Count->Pace != EWidgetAnimCountPace::Duration);
}

float FWidgetAnimCountUpEvaluator::GetPacedSeconds(const FWidgetAnimCountUpParams& Params, double From, double To)
{
	const double Change = FMath::Abs(To - From);
	double Seconds = 0.0;
	switch (Params.Pace)
	{
	case EWidgetAnimCountPace::PerSecond:
		Seconds = Change / FMath::Max(Params.CountSpeed, 0.001);
		break;

	case EWidgetAnimCountPace::BySizeOfChange:
		// Digits in the change, counted smoothly: 9 is nearly one, 10 is one, 1,000,000 is six.
		Seconds = FMath::Max(Params.SecondsPerDigit, 0.f) * (FMath::Loge(Change + 1.0) / FMath::Loge(10.0));
		break;

	case EWidgetAnimCountPace::Duration:
	default:
		return -1.f;
	}

	const float Shortest = FMath::Max(Params.MinDuration, 0.01f);
	const float Longest = FMath::Max(Params.MaxDuration, Shortest);
	return FMath::Clamp(static_cast<float>(Seconds), Shortest, Longest);
}

float FWidgetAnimCountUpEvaluator::PrepareForPlay(UObject* Target, FInstancedStruct& Params, float AuthoredRun) const
{
	FWidgetAnimCountUpParams* Count = Params.GetMutablePtr<FWidgetAnimCountUpParams>();
	if (!Count)
	{
		return -1.f;
	}

	// Both read from the same number when both ask for it, which counts nowhere - the text holds the new total only
	// once it has been given one.
	double Shown = 0.0;
	const bool bHasShown = Target && WidgetAnimText::GetNumber(Target, Shown);

	Count->PreparedStart = Count->CountFrom == EWidgetAnimCountFrom::NumberShown && bHasShown ? Shown : Count->StartValue;
	Count->PreparedEnd = Count->CountTo == EWidgetAnimCountTo::NumberInText && bHasShown ? Shown : Count->EndValue;
	Count->bPrepared = true;

	return GetPacedSeconds(*Count, Count->PreparedStart, Count->PreparedEnd);
}

bool FWidgetAnimCountUpEvaluator::HasDerivedLength(const FInstancedStruct& Params) const
{
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	return Count && Count->Pace != EWidgetAnimCountPace::Duration;
}

FText FWidgetAnimCountUpEvaluator::GetDerivedLengthLabel(const FInstancedStruct& Params) const
{
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	if (!Count)
	{
		return FText::GetEmpty();
	}

	FNumberFormattingOptions Options;
	Options.SetMaximumFractionalDigits(2);
	if (Count->Pace == EWidgetAnimCountPace::PerSecond)
	{
		return FText::Format(LOCTEXT("PerSecondLabel", "{0}/s"), FText::AsNumber(Count->CountSpeed, &Options));
	}
	return FText::Format(LOCTEXT("PerDigitLabel", "{0}s/digit"), FText::AsNumber(Count->SecondsPerDigit, &Options));
}

// =====================================================================================================
// Value
// =====================================================================================================

double FWidgetAnimCountUpEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	if (!Count)
	{
		return 0.0;
	}
	return bStart ? Count->GetStart() : Count->GetEnd();
}

double FWidgetAnimCountUpEvaluator::GetQuantizeStep(const FInstancedStruct& Params) const
{
	// One step of the last digit shown: the text only changes when the value crosses one, so that is the only time it
	// needs formatting and setting. A shortened number's last digit depends on its size, so it is formatted every frame
	// it moves (and only set when the text changes).
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	if (!Count)
	{
		return 1.0;
	}
	switch (Count->Style)
	{
	case EWidgetAnimCountStyle::Compact:            return 0.0;
	case EWidgetAnimCountStyle::Time:
	case EWidgetAnimCountStyle::TimeWithHours:      return 0.0;
	case EWidgetAnimCountStyle::TimeWithHundredths: return 0.01;
	default:                                        return FMath::Pow(10.0, -static_cast<double>(FMath::Clamp(Count->Decimals, 0, 6)));
	}
}

FText FWidgetAnimCountUpEvaluator::FormatValue(const FWidgetAnimCountUpParams& Params, double Value)
{
	const int32 Digits = FMath::Clamp(Params.Decimals, 0, 6);

	FText Number;
	switch (Params.Style)
	{
	case EWidgetAnimCountStyle::Percent:
	{
		FNumberFormattingOptions Options;
		Options.SetUseGrouping(Params.bUseGrouping);
		Options.SetMinimumFractionalDigits(Digits);
		Options.SetMaximumFractionalDigits(Digits);
		Options.SetRoundingMode(ERoundingMode::HalfFromZero);
		Number = FText::AsPercent(WidgetAnimCount::Round(Value, Digits) / 100.0, &Options);
		break;
	}
	case EWidgetAnimCountStyle::Compact:            Number = WidgetAnimCount::FormatCompact(Value, Digits); break;
	case EWidgetAnimCountStyle::Time:               Number = WidgetAnimCount::FormatTime(Value, false, false); break;
	case EWidgetAnimCountStyle::TimeWithHours:      Number = WidgetAnimCount::FormatTime(Value, true, false); break;
	case EWidgetAnimCountStyle::TimeWithHundredths: Number = WidgetAnimCount::FormatTime(Value, false, true); break;
	case EWidgetAnimCountStyle::Number:
	default:                                        Number = WidgetAnimCount::FormatNumber(Value, Digits, Params.bUseGrouping); break;
	}

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
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	if (!Target || !Count)
	{
		return;
	}

	// A numeric text takes the number and formats it itself.
	if (IWidgetAnimTextHost* Taker = WidgetAnimCount::GetNumberTaker(Target, *Count))
	{
		Taker->SetWidgetAnimNumber(Value);
		return;
	}

	// Only when the text actually changes. Setting it invalidates the text's layout, and a count that is holding - or
	// crawling across a whole number over several frames - would otherwise lay it out again every frame.
	WidgetAnimPlainText::Write(Target, FormatValue(*Count, Value));
}

bool FWidgetAnimCountUpEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const FWidgetAnimCountUpParams* Count = GetParams(Params);
	if (!Count || !WidgetAnimText::IsText(Target))
	{
		return false;
	}

	FWidgetAnimTextTrackBaseline Baseline;
	if (WidgetAnimCount::GetNumberTaker(Target, *Count))
	{
		Baseline.bHasNumber = WidgetAnimText::GetNumber(Target, Baseline.Number);
	}
	Baseline.Text = WidgetAnimText::GetText(Target);
	Baseline.bHasText = !Baseline.bHasNumber;
	OutBaseline.InitializeAs<FWidgetAnimTextTrackBaseline>(Baseline);
	return true;
}

void FWidgetAnimCountUpEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	if (const FWidgetAnimTextTrackBaseline* Captured = Baseline.GetPtr<FWidgetAnimTextTrackBaseline>())
	{
		if (Captured->bHasNumber)
		{
			if (IWidgetAnimTextHost* Host = WidgetAnimText::AsHost(Target))
			{
				double Current = 0.0;
				if (!Host->GetWidgetAnimNumber(Current) || Current != Captured->Number)
				{
					Host->SetWidgetAnimNumber(Captured->Number);
				}
			}
		}
		else if (Captured->bHasText)
		{
			WidgetAnimPlainText::Write(Target, Captured->Text);
		}
		return;
	}

	// A baseline recorded in the shape an earlier version kept.
	if (const FWidgetAnimTextBaseline* Old = Baseline.GetPtr<FWidgetAnimTextBaseline>())
	{
		WidgetAnimPlainText::Write(Target, Old->Text);
	}
}

#undef LOCTEXT_NAMESPACE
