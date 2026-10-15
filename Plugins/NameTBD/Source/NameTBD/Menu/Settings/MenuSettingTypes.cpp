// Copyright AR GameSystems 2026. All rights reserved.

#include "MenuSettingTypes.h"

#include "HAL/PlatformProperties.h"

DEFINE_LOG_CATEGORY(LogNameTBDSettings);

bool FMenuSettingCondition::Test(const FString& Actual) const
{
	switch (Compare)
	{
	case EMenuSettingCompare::IsTrue:
		return MenuSettingValues::ToBool(Actual);
	case EMenuSettingCompare::IsFalse:
		return !MenuSettingValues::ToBool(Actual);
	case EMenuSettingCompare::IsAnyOf:
	{
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
			if (Part.Equals(Actual, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}
	default:
		break;
	}

	const bool bNumbers = MenuSettingValues::IsNumber(Actual) && MenuSettingValues::IsNumber(Value);
	int32 Order = 0;
	if (bNumbers)
	{
		const double A = MenuSettingValues::ToNumber(Actual);
		const double B = MenuSettingValues::ToNumber(Value);
		Order = FMath::IsNearlyEqual(A, B, 1e-6) ? 0 : (A < B ? -1 : 1);
	}
	else
	{
		Order = Actual.Compare(Value, ESearchCase::IgnoreCase);
		Order = Order == 0 ? 0 : (Order < 0 ? -1 : 1);
	}

	switch (Compare)
	{
	case EMenuSettingCompare::Equals: return Order == 0;
	case EMenuSettingCompare::NotEquals: return Order != 0;
	case EMenuSettingCompare::Greater: return Order > 0;
	case EMenuSettingCompare::GreaterOrEqual: return Order >= 0;
	case EMenuSettingCompare::Less: return Order < 0;
	case EMenuSettingCompare::LessOrEqual: return Order <= 0;
	default: return false;
	}
}

namespace MenuSettingValues
{
	FString FromBool(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	bool ToBool(const FString& Value)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		if (IsNumber(Trimmed))
		{
			return ToNumber(Trimmed) != 0.0;
		}
		return Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
			|| Trimmed.Equals(TEXT("on"), ESearchCase::IgnoreCase);
	}

	FString FromNumber(double Value)
	{
		if (FMath::IsNearlyEqual(Value, FMath::RoundToDouble(Value), 1e-6))
		{
			return FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(Value)));
		}
		FString Out = FString::Printf(TEXT("%.4f"), Value);
		while (Out.EndsWith(TEXT("0")))
		{
			Out.LeftChopInline(1);
		}
		if (Out.EndsWith(TEXT(".")))
		{
			Out.LeftChopInline(1);
		}
		return Out;
	}

	double ToNumber(const FString& Value, double Default)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		if (Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			return 1.0;
		}
		if (Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			return 0.0;
		}
		return IsNumber(Trimmed) ? FCString::Atod(*Trimmed) : Default;
	}

	bool IsNumber(const FString& Value)
	{
		const FString Trimmed = Value.TrimStartAndEnd();
		return !Trimmed.IsEmpty() && Trimmed.IsNumeric();
	}

	FString FromResolution(const FIntPoint& Resolution)
	{
		return FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y);
	}

	bool ToResolution(const FString& Value, FIntPoint& OutResolution)
	{
		FString Left;
		FString Right;
		if (!Value.Split(TEXT("x"), &Left, &Right, ESearchCase::IgnoreCase) || !Left.IsNumeric() || !Right.IsNumeric())
		{
			return false;
		}
		OutResolution = FIntPoint(FCString::Atoi(*Left), FCString::Atoi(*Right));
		return OutResolution.X > 0 && OutResolution.Y > 0;
	}

	bool IsCurrentPlatformIn(const TArray<FString>& Platforms)
	{
		if (Platforms.Num() == 0)
		{
			return true;
		}
		const FString Name = FPlatformProperties::IniPlatformName();
		const bool bDesktop = PLATFORM_DESKTOP;
		const bool bMobile = PLATFORM_ANDROID || PLATFORM_IOS;
		const bool bConsole = !bDesktop && !bMobile;
		for (const FString& Entry : Platforms)
		{
			if (Entry.Equals(Name, ESearchCase::IgnoreCase)
				|| (bDesktop && Entry.Equals(TEXT("Desktop"), ESearchCase::IgnoreCase))
				|| (bMobile && Entry.Equals(TEXT("Mobile"), ESearchCase::IgnoreCase))
				|| (bConsole && Entry.Equals(TEXT("Console"), ESearchCase::IgnoreCase)))
			{
				return true;
			}
		}
		return false;
	}
}
