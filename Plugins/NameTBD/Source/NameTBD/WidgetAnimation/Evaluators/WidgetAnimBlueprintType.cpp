// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimBlueprintType.h"

#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"
#include "HAL/IConsoleManager.h"
#include "Logging/MessageLog.h"
#include "UObject/ObjectKey.h"

#if WITH_EDITOR
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimBlueprintType, Log, All);

#define LOCTEXT_NAMESPACE "WidgetAnimBlueprintType"

#if !UE_BUILD_SHIPPING
/**
 * TEMPORARY - for testing the cost warning only; delete once it is confirmed to work.
 *
 * True treats every Blueprint type as over budget at the end of its first measurement window, so the warning can be
 * seen without building a genuinely expensive type. Still respects Warn About Expensive Blueprint Types in Project
 * Settings, so turning that off must silence it too. Each type is still warned about only once per session.
 */
static bool GForceBlueprintTypeCostWarning = false;
#endif

// =====================================================================================================
// Cost measurement
// =====================================================================================================

#if !UE_BUILD_SHIPPING
namespace
{
	/** One Blueprint type's cost: the window being measured now, the last one judged, and the session's totals. */
	struct FBlueprintTypeCost
	{
		FString Name;

		double WindowStartTime = -1.0;
		uint64 WindowStartFrame = 0;
		double WindowSeconds = 0.0;
		int32 WindowCallFrames = 0;
		uint64 LastCallFrame = MAX_uint64;

		bool bHasLastWindow = false;
		float LastMsPerFrame = 0.f;
		float LastCallFrameShare = 0.f;
		float PeakMsPerFrame = 0.f;

		int64 TotalCalls = 0;
		double TotalSeconds = 0.0;

		bool bFlagged = false;
		bool bWarned = false;
	};

	TMap<FObjectKey, FBlueprintTypeCost>& GetBlueprintTypeCosts()
	{
		static TMap<FObjectKey, FBlueprintTypeCost> Costs;
		return Costs;
	}

	void WarnAboutBlueprintType(const FBlueprintTypeCost& Cost, bool bForced)
	{
		FNumberFormattingOptions MsFormat;
		MsFormat.SetMinimumFractionalDigits(3).SetMaximumFractionalDigits(3);

		FText Message = FText::Format(
			LOCTEXT("CostWarning", "Blueprint animation type '{0}' is expensive: {1} ms of Blueprint per frame on average, running Blueprint on {2}% of frames. Give it a Quantize Step so Apply Value only runs when the value visibly changes, or move it to a C++ evaluator - the same contract, so no animation using it has to change."),
			FText::FromString(Cost.Name),
			FText::AsNumber(Cost.LastMsPerFrame, &MsFormat),
			FText::AsNumber(FMath::RoundToInt(Cost.LastCallFrameShare * 100.f)));

		if (bForced)
		{
			Message = FText::Format(LOCTEXT("CostWarningForced", "{0} (Forced for testing by GForceBlueprintTypeCostWarning.)"), Message);
		}

		UE_LOG(LogWidgetAnimBlueprintType, Warning, TEXT("%s"), *Message.ToString());
		FMessageLog("PIE").Warning(Message);

#if WITH_EDITOR
		// The one warning that can't be missed while authoring in the designer, where no play session's log is open.
		if (FSlateApplication::IsInitialized())
		{
			FNotificationInfo Info(Message);
			Info.ExpireDuration = 8.f;
			Info.bUseLargeFont = false;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
#endif
	}

	/**
	 * Adds one Blueprint call's time to its type, and judges the type each time a measurement window closes. Judged
	 * per frame rather than per call, because the budget is a frame's worth of time - fifty tracks each calling once
	 * cost the same as one track calling fifty times.
	 */
	void RecordBlueprintCall(const UClass* TypeClass, double Seconds)
	{
		if (!TypeClass)
		{
			return;
		}

		const UWidgetAnimRuntimeSettings& Settings = UWidgetAnimRuntimeSettings::Get();
		const double Now = FPlatformTime::Seconds();
		const uint64 Frame = GFrameCounter;

		FBlueprintTypeCost& Cost = GetBlueprintTypeCosts().FindOrAdd(FObjectKey(TypeClass));
		if (Cost.Name.IsEmpty())
		{
			Cost.Name = TypeClass->GetName();
		}

		if (Cost.WindowStartTime < 0.0)
		{
			Cost.WindowStartTime = Now;
			Cost.WindowStartFrame = Frame;
		}

		Cost.WindowSeconds += Seconds;
		Cost.TotalSeconds += Seconds;
		++Cost.TotalCalls;
		if (Cost.LastCallFrame != Frame)
		{
			++Cost.WindowCallFrames;
			Cost.LastCallFrame = Frame;
		}

		if (Now - Cost.WindowStartTime < FMath::Max(Settings.MeasurementWindowSeconds, 0.25f))
		{
			return;
		}

		// The window is over: work out what it cost per frame, whether the type called Blueprint on nearly every one.
		const uint64 Frames = FMath::Max<uint64>(Frame - Cost.WindowStartFrame + 1, 1);
		Cost.LastMsPerFrame = static_cast<float>(Cost.WindowSeconds * 1000.0 / static_cast<double>(Frames));
		Cost.LastCallFrameShare = FMath::Min(static_cast<float>(Cost.WindowCallFrames) / static_cast<float>(Frames), 1.f);
		Cost.PeakMsPerFrame = FMath::Max(Cost.PeakMsPerFrame, Cost.LastMsPerFrame);
		Cost.bHasLastWindow = true;

		if (Settings.bWarnAboutExpensiveBlueprintTypes)
		{
			const bool bForced = GForceBlueprintTypeCostWarning;
			const bool bOverBudget = Cost.LastMsPerFrame > Settings.BlueprintTypeBudgetMs;
			// A handful of frames says nothing about "every frame"; ten is enough to be a pattern.
			const bool bEveryFrame = Settings.EveryFrameCallThreshold < 1.f && Frames >= 10 && Cost.LastCallFrameShare >= Settings.EveryFrameCallThreshold;

			if (bForced || bOverBudget || bEveryFrame)
			{
				Cost.bFlagged = true;
				if (!Cost.bWarned)
				{
					Cost.bWarned = true;
					WarnAboutBlueprintType(Cost, bForced && !bOverBudget && !bEveryFrame);
				}
			}
		}

		Cost.WindowStartTime = Now;
		Cost.WindowStartFrame = Frame;
		Cost.WindowSeconds = 0.0;
		Cost.WindowCallFrames = 0;
	}

	/** Times one Blueprint call and hands it to RecordBlueprintCall. */
	struct FScopedBlueprintTypeTimer
	{
		explicit FScopedBlueprintTypeTimer(const UWidgetAnimBlueprintType* InTypeObject)
			: TypeClass(InTypeObject ? InTypeObject->GetClass() : nullptr)
			, StartTime(FPlatformTime::Seconds())
		{
		}

		~FScopedBlueprintTypeTimer()
		{
			RecordBlueprintCall(TypeClass, FPlatformTime::Seconds() - StartTime);
		}

		const UClass* TypeClass;
		double StartTime;
	};

	/** WidgetAnim.BlueprintTypeReport: every Blueprint type's cost this session, for profiling passes and QA. */
	FAutoConsoleCommand GBlueprintTypeReportCommand(
		TEXT("WidgetAnim.BlueprintTypeReport"),
		TEXT("Lists what each Blueprint widget animation type has cost this session."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const TMap<FObjectKey, FBlueprintTypeCost>& Costs = GetBlueprintTypeCosts();
			UE_LOG(LogWidgetAnimBlueprintType, Display, TEXT("Blueprint widget animation types: %d measured."), Costs.Num());
			for (const TPair<FObjectKey, FBlueprintTypeCost>& Pair : Costs)
			{
				const FBlueprintTypeCost& Cost = Pair.Value;
				UE_LOG(LogWidgetAnimBlueprintType, Display, TEXT("  %s: %lld calls, %.3f ms in total; last window %.3f ms/frame on %.0f%% of frames; peak %.3f ms/frame%s"),
					*Cost.Name, Cost.TotalCalls, Cost.TotalSeconds * 1000.0, Cost.LastMsPerFrame, Cost.LastCallFrameShare * 100.f, Cost.PeakMsPerFrame,
					Cost.bFlagged ? TEXT("  [OVER BUDGET]") : TEXT(""));
			}
		}));
}

#define WIDGETANIM_TIME_BLUEPRINT_CALL(TypeObject) const FScopedBlueprintTypeTimer BlueprintCallTimer(TypeObject)
#else
#define WIDGETANIM_TIME_BLUEPRINT_CALL(TypeObject)
#endif

namespace WidgetAnimBlueprintTypeCost
{
	bool IsFlagged(const UClass* Type)
	{
#if !UE_BUILD_SHIPPING
		if (!Type || !UWidgetAnimRuntimeSettings::Get().bWarnAboutExpensiveBlueprintTypes)
		{
			return false;
		}
		const FBlueprintTypeCost* Cost = GetBlueprintTypeCosts().Find(FObjectKey(Type));
		return Cost && Cost->bFlagged;
#else
		return false;
#endif
	}

	bool GetLastWindow(const UClass* Type, float& OutMsPerFrame, float& OutCallFrameShare)
	{
		OutMsPerFrame = 0.f;
		OutCallFrameShare = 0.f;
#if !UE_BUILD_SHIPPING
		const FBlueprintTypeCost* Cost = Type ? GetBlueprintTypeCosts().Find(FObjectKey(Type)) : nullptr;
		if (Cost && Cost->bHasLastWindow)
		{
			OutMsPerFrame = Cost->LastMsPerFrame;
			OutCallFrameShare = Cost->LastCallFrameShare;
			return true;
		}
#endif
		return false;
	}
}

// =====================================================================================================
// UWidgetAnimBlueprintType
// =====================================================================================================

UWidgetAnimBlueprintType::UWidgetAnimBlueprintType()
{
	TargetClass = UWidget::StaticClass();
}

FText UWidgetAnimBlueprintType::GetTypeDisplayName() const
{
	return DisplayName.IsEmpty() ? GetClass()->GetDisplayNameText() : DisplayName;
}

bool UWidgetAnimBlueprintType::CaptureBaseline_Implementation(UWidget* Target, const FInstancedStruct& Settings, FInstancedStruct& Baseline) const
{
	return false;
}

void UWidgetAnimBlueprintType::RestoreBaseline_Implementation(UWidget* Target, const FInstancedStruct& Baseline) const
{
}

// =====================================================================================================
// FWidgetAnimBlueprintTypeParams
// =====================================================================================================

const UWidgetAnimBlueprintType* FWidgetAnimBlueprintTypeParams::GetTypeObject() const
{
	const UClass* TypeClass = Type.Get();
	if (!TypeClass || TypeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	return TypeClass->GetDefaultObject<UWidgetAnimBlueprintType>();
}

FInstancedStruct FWidgetAnimBlueprintTypeParams::MakeFor(TSubclassOf<UWidgetAnimBlueprintType> InType)
{
	FInstancedStruct Out;
	Out.InitializeAs<FWidgetAnimBlueprintTypeParams>();

	FWidgetAnimBlueprintTypeParams& Params = Out.GetMutable<FWidgetAnimBlueprintTypeParams>();
	Params.Type = InType;
	if (const UWidgetAnimBlueprintType* TypeObject = Params.GetTypeObject())
	{
		Params.StartValue = TypeObject->DefaultStartValue;
		Params.EndValue = TypeObject->DefaultEndValue;
		if (TypeObject->SettingsStruct)
		{
			Params.Settings.InitializeAs(TypeObject->SettingsStruct);
		}
	}
	return Out;
}

// =====================================================================================================
// FWidgetAnimBlueprintEvaluator
// =====================================================================================================

FText FWidgetAnimBlueprintEvaluator::GetDisplayName() const
{
	return LOCTEXT("BlueprintTypeName", "Blueprint Type");
}

FText FWidgetAnimBlueprintEvaluator::GetTrackDisplayName(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	if (const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr)
	{
		return TypeObject->GetTypeDisplayName();
	}
	return LOCTEXT("NoBlueprintTypeName", "Blueprint Type (none picked)");
}

FText FWidgetAnimBlueprintEvaluator::GetTrackDescription(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	return TypeObject ? TypeObject->Description : FText::GetEmpty();
}

FText FWidgetAnimBlueprintEvaluator::GetTrackValueLabel(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	return TypeObject && !TypeObject->ValueLabel.IsEmpty() ? TypeObject->ValueLabel : GetValueLabel();
}

UClass* FWidgetAnimBlueprintEvaluator::GetTrackTargetClass(const FInstancedStruct& Params) const
{
	// No type picked yet: no target at all, so the track does nothing rather than calling into nothing.
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	if (!TypeObject)
	{
		return nullptr;
	}
	return TypeObject->TargetClass ? TypeObject->TargetClass.Get() : UWidget::StaticClass();
}

FName FWidgetAnimBlueprintEvaluator::GetChannel(const FInstancedStruct& Params) const
{
	// A type left at None gets a channel of its own - its class's name - so two unrelated Blueprint types on one
	// widget never take turns by accident.
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	if (!TypeObject)
	{
		return NAME_None;
	}
	return TypeObject->Channel.IsNone() ? TypeObject->GetClass()->GetFName() : TypeObject->Channel;
}

double FWidgetAnimBlueprintEvaluator::GetEndpointValue(const FInstancedStruct& Params, bool bStart) const
{
	// Native on purpose: the timeline reads these every frame, and that must never run Blueprint.
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	if (!BlueprintParams)
	{
		return 0.0;
	}
	return bStart ? BlueprintParams->StartValue : BlueprintParams->EndValue;
}

double FWidgetAnimBlueprintEvaluator::GetQuantizeStep(const FInstancedStruct& Params) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	return TypeObject ? FMath::Max(TypeObject->QuantizeStep, 0.0) : 0.0;
}

void FWidgetAnimBlueprintEvaluator::Apply(UObject* Target, const FInstancedStruct& Params, double Value) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	UWidget* Widget = Cast<UWidget>(Target);
	if (!TypeObject || !Widget)
	{
		return;
	}

	WIDGETANIM_TIME_BLUEPRINT_CALL(TypeObject);
	TypeObject->ApplyValue(Widget, BlueprintParams->Settings, Value);
}

bool FWidgetAnimBlueprintEvaluator::CaptureBaseline(UObject* Target, const FInstancedStruct& Params, FInstancedStruct& OutBaseline) const
{
	const FWidgetAnimBlueprintTypeParams* BlueprintParams = GetParams(Params);
	const UWidgetAnimBlueprintType* TypeObject = BlueprintParams ? BlueprintParams->GetTypeObject() : nullptr;
	UWidget* Widget = Cast<UWidget>(Target);
	if (!TypeObject || !Widget)
	{
		return false;
	}

	FInstancedStruct Captured;
	bool bCaptured = false;
	{
		WIDGETANIM_TIME_BLUEPRINT_CALL(TypeObject);
		bCaptured = TypeObject->CaptureBaseline(Widget, BlueprintParams->Settings, Captured);
	}
	if (!bCaptured)
	{
		return false;
	}

	// Which type captured it goes with it: every Blueprint type is restored through this one evaluator, which has to
	// know whose Restore Baseline to call.
	FWidgetAnimBlueprintTypeBaseline Baseline;
	Baseline.Type = BlueprintParams->Type;
	Baseline.Captured = MoveTemp(Captured);
	OutBaseline.InitializeAs<FWidgetAnimBlueprintTypeBaseline>(MoveTemp(Baseline));
	return true;
}

void FWidgetAnimBlueprintEvaluator::RestoreBaseline(UObject* Target, const FInstancedStruct& Baseline) const
{
	const FWidgetAnimBlueprintTypeBaseline* Captured = Baseline.GetPtr<FWidgetAnimBlueprintTypeBaseline>();
	const UClass* TypeClass = Captured ? Captured->Type.Get() : nullptr;
	UWidget* Widget = Cast<UWidget>(Target);
	if (!TypeClass || TypeClass->HasAnyClassFlags(CLASS_Abstract) || !Widget)
	{
		return;
	}

	const UWidgetAnimBlueprintType* TypeObject = TypeClass->GetDefaultObject<UWidgetAnimBlueprintType>();
	WIDGETANIM_TIME_BLUEPRINT_CALL(TypeObject);
	TypeObject->RestoreBaseline(Widget, Captured->Captured);
}

#undef WIDGETANIM_TIME_BLUEPRINT_CALL
#undef LOCTEXT_NAMESPACE