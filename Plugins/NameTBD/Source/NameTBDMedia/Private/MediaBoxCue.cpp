// Copyright AR GameSystems 2026. All rights reserved.

#include "MediaBoxCue.h"

#include "MediaBox.h"
#include "MediaBoxClip.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"

#define LOCTEXT_NAMESPACE "MediaBoxCue"

namespace MediaBoxFindPrivate
{
	/** The first Media Box at or beneath Widget, through panels and into User Widgets. */
	UMediaBox* FindUnder(UWidget* Widget)
	{
		if (!Widget)
		{
			return nullptr;
		}
		if (UMediaBox* Box = Cast<UMediaBox>(Widget))
		{
			return Box;
		}
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			return UserWidget->WidgetTree ? FindUnder(UserWidget->WidgetTree->RootWidget) : nullptr;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				if (UMediaBox* Box = FindUnder(Panel->GetChildAt(ChildIndex)))
				{
					return Box;
				}
			}
		}
		return nullptr;
	}

	/** The User Widget a widget is in, or the widget itself when it is one. */
	UUserWidget* FirstUserWidget(UWidget* Widget)
	{
		if (UUserWidget* AsUserWidget = Cast<UUserWidget>(Widget))
		{
			return AsUserWidget;
		}
		return Widget ? Widget->GetTypedOuter<UUserWidget>() : nullptr;
	}
}

UMediaBox* MediaBoxFind::Find(UWidget* From, UWidget* Host, FName BoxName)
{
	using namespace MediaBoxFindPrivate;

	if (!BoxName.IsNone())
	{
		for (UWidget* Start : { From, Host })
		{
			for (UUserWidget* UserWidget = FirstUserWidget(Start); UserWidget; UserWidget = UserWidget->GetTypedOuter<UUserWidget>())
			{
				if (UMediaBox* Named = Cast<UMediaBox>(UserWidget->GetWidgetFromName(BoxName)))
				{
					return Named;
				}
			}
		}
		return nullptr;
	}

	if (UMediaBox* Under = FindUnder(From))
	{
		return Under;
	}
	for (UUserWidget* UserWidget = FirstUserWidget(From); UserWidget; UserWidget = UserWidget->GetTypedOuter<UUserWidget>())
	{
		if (UMediaBox* Around = FindUnder(UserWidget))
		{
			return Around;
		}
	}
	return nullptr;
}

// =====================================================================================================
// Media cue
// =====================================================================================================

FText FWidgetAnimMediaCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("MediaName", "Media");
}

FText FWidgetAnimMediaCueEvaluator::GetDescription() const
{
	return LOCTEXT("MediaDescription", "Plays, pauses, stops, seeks or swaps the clip of a Media Box: the one named, else the track's widget, inside it, or the nearest around it. Seen in the editor's previews where the box has Preview In Designer on.");
}

FText FWidgetAnimMediaCueEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimMediaCueParams* CueParams = GetParams(Params);
	if (!CueParams)
	{
		return FText::GetEmpty();
	}

	FText What;
	switch (CueParams->Action)
	{
	case EWidgetAnimMediaCueAction::Play:            What = LOCTEXT("PlaySummary", "Play"); break;
	case EWidgetAnimMediaCueAction::Pause:           What = LOCTEXT("PauseSummary", "Pause"); break;
	case EWidgetAnimMediaCueAction::Stop:            What = LOCTEXT("StopSummary", "Stop"); break;
	case EWidgetAnimMediaCueAction::TogglePlayPause: What = LOCTEXT("ToggleSummary", "Toggle Play / Pause"); break;
	case EWidgetAnimMediaCueAction::Seek:            What = FText::Format(LOCTEXT("SeekSummary", "Seek {0} s"), FText::AsNumber(CueParams->SeekTime)); break;
	case EWidgetAnimMediaCueAction::SetClip:
	default:
		What = CueParams->CueClip
			? FText::Format(LOCTEXT("SetClipSummary", "Set Clip {0}"), FText::FromString(CueParams->CueClip->GetName()))
			: LOCTEXT("SetClipNoneSummary", "Set Clip (none)");
		break;
	}

	return CueParams->MediaBoxName.IsNone()
		? What
		: FText::Format(LOCTEXT("SummaryOnBox", "{0} on {1}"), What, FText::FromName(CueParams->MediaBoxName));
}

void FWidgetAnimMediaCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimMediaCueParams* CueParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	UWidget* TrackWidget = Context.Info.Target ? Context.Info.Target.Get() : Context.Info.Host.Get();
	if (!CueParams || !TrackWidget)
	{
		return;
	}

	UMediaBox* Box = MediaBoxFind::Find(TrackWidget, Context.Info.Host, CueParams->MediaBoxName);
	if (!Box)
	{
		return;
	}

	switch (CueParams->Action)
	{
	case EWidgetAnimMediaCueAction::Play:            Box->Play(); break;
	case EWidgetAnimMediaCueAction::Pause:           Box->Pause(); break;
	case EWidgetAnimMediaCueAction::Stop:            Box->Stop(); break;
	case EWidgetAnimMediaCueAction::TogglePlayPause: Box->TogglePlayPause(); break;
	case EWidgetAnimMediaCueAction::Seek:            Box->Seek(CueParams->SeekTime); break;
	case EWidgetAnimMediaCueAction::SetClip:         Box->SetClip(CueParams->CueClip, CueParams->bPlayClip); break;
	default: break;
	}
}

// =====================================================================================================
// Media Progress input
// =====================================================================================================

bool FWidgetAnimInput_MediaProgress::Sample(const FWidgetAnimInputContext& Context, FVector4& OutValue)
{
	// Looked up once per widget it measures, not every frame.
	if (!Found.IsValid() || FoundFor.Get() != Context.MeasuredWidget)
	{
		Found = MediaBoxFind::Find(Context.MeasuredWidget, Context.HostWidget, MediaBoxName);
		FoundFor = Context.MeasuredWidget;
	}
	const UMediaBox* Box = Found.Get();
	if (!Box)
	{
		return false;
	}

	double Value = 0.0;
	switch (Measure)
	{
	case EMediaBoxInputMeasure::Seconds: Value = Box->GetTime(); break;
	case EMediaBoxInputMeasure::Playing: Value = Box->IsPlaying() ? 1.0 : 0.0; break;
	case EMediaBoxInputMeasure::Progress:
	default:                             Value = Box->GetProgress(); break;
	}
	OutValue = WidgetAnimValue::FromFloat(Value);
	return true;
}

FString FWidgetAnimInput_MediaProgress::GetSummary() const
{
	switch (Measure)
	{
	case EMediaBoxInputMeasure::Seconds: return TEXT("Media Seconds");
	case EMediaBoxInputMeasure::Playing: return TEXT("Media Playing");
	default:                             return TEXT("Media Progress");
	}
}

#undef LOCTEXT_NAMESPACE
