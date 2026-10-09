// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimParticleCue.h"

#include "NiagaraUIParameters.h"
#include "NiagaraUIRuntime.h"
#include "NiagaraUIWidget.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "NiagaraSystem.h"

#define LOCTEXT_NAMESPACE "WidgetAnimParticleCue"

FText FWidgetAnimParticleCueEvaluator::GetDisplayName() const
{
	return LOCTEXT("ParticlesName", "Particles");
}

FText FWidgetAnimParticleCueEvaluator::GetDescription() const
{
	return LOCTEXT("ParticlesDescription", "Fires Niagara particles on a Niagara Particles widget, or on every one found: a burst of the track's own Niagara System and User Parameters (or each widget's own) where the widget is or on the track's widget; or Play, Stop Emitting, Clear. Seen in the editor's previews too.");
}

FText FWidgetAnimParticleCueEvaluator::GetTrackSummary(const FInstancedStruct& Params) const
{
	const FWidgetAnimParticleCueParams* CueParams = GetParams(Params);
	if (!CueParams)
	{
		return FText::GetEmpty();
	}

	const UNiagaraSystem* TrackSystem = CueParams->PlaysTrackSystem() ? CueParams->NiagaraSystem.Get() : nullptr;
	FText What;
	switch (CueParams->Action)
	{
	case EWidgetAnimParticleCueAction::BurstAtTarget:
		What = TrackSystem
			? FText::Format(LOCTEXT("BurstSystemSummary", "Burst {0}"), FText::FromString(TrackSystem->GetName()))
			: LOCTEXT("BurstSummary", "Burst");
		break;
	case EWidgetAnimParticleCueAction::Play:
		What = TrackSystem
			? FText::Format(LOCTEXT("PlaySystemSummary", "Play {0}"), FText::FromString(TrackSystem->GetName()))
			: LOCTEXT("PlaySummary", "Play");
		break;
	case EWidgetAnimParticleCueAction::StopEmitting:
		What = LOCTEXT("StopSummary", "Stop Emitting");
		break;
	case EWidgetAnimParticleCueAction::Clear:
	default:
		What = LOCTEXT("ClearSummary", "Clear");
		break;
	}

	if (CueParams->ActsOnAll())
	{
		What = FText::Format(LOCTEXT("AllSummary", "{0} (all)"), What);
	}
	return CueParams->ParticlesWidget.IsNone()
		? What
		: FText::Format(LOCTEXT("SummaryOnWidget", "{0} on {1}"), What, FText::FromName(CueParams->ParticlesWidget));
}

void FWidgetAnimParticleCueEvaluator::Fire(const FWidgetAnimCueContext& Context) const
{
	const FWidgetAnimParticleCueParams* CueParams = Context.Params ? GetParams(*Context.Params) : nullptr;
	UWidget* TrackWidget = Context.Info.Target ? Context.Info.Target.Get() : Context.Info.Host.Get();
	if (!CueParams || !TrackWidget)
	{
		return;
	}

	TArray<UNiagaraUIWidget*> Found;
	FindParticlesWidgets(TrackWidget, Context.Info.Host, CueParams->ParticlesWidget, CueParams->ActsOnAll(), Found);
	if (Found.Num() == 0)
	{
		// Said once per widget and name: a cue fired every loop would otherwise fill the log.
		NiagaraUIRuntime::WarnOnce(FString::Printf(TEXT("A Particles cue on %s found no Niagara Particles widget%s%s - add one, or name an existing one in the cue's Particles Widget."),
			*TrackWidget->GetName(),
			CueParams->ParticlesWidget.IsNone() ? TEXT("") : TEXT(" named "),
			CueParams->ParticlesWidget.IsNone() ? TEXT("") : *CueParams->ParticlesWidget.ToString()));
		return;
	}

	// The track's own system and the User Parameters it changed - or, with Use Widget's Niagara System, each widget's own.
	UNiagaraSystem* TrackSystem = CueParams->PlaysTrackSystem() ? CueParams->NiagaraSystem.Get() : nullptr;
	FNiagaraUIParameterOverrides Overrides;
	const FNiagaraUIParameterOverrides* TrackOverrides = nullptr;
	if (TrackSystem && NiagaraUIParameters::HasChanges(CueParams->UserParameters, CueParams->UserParameterDefaults))
	{
		Overrides.Values = CueParams->UserParameters;
		Overrides.SystemValues = CueParams->UserParameterDefaults;
		Overrides.PositionNames = CueParams->PositionParameters;
		TrackOverrides = &Overrides;
	}

	// For a burst on the track's widget: the point as it is drawn now - its Render Transform and animation included.
	FVector2D AbsolutePoint = FVector2D::ZeroVector;
	if (CueParams->Action == EWidgetAnimParticleCueAction::BurstAtTarget && CueParams->Place == EWidgetAnimParticleCuePlace::TrackWidget)
	{
		const FGeometry& Geometry = TrackWidget->GetCachedGeometry();
		const FVector2D LocalSize(Geometry.GetLocalSize());
		AbsolutePoint = FVector2D(Geometry.LocalToAbsolute(LocalSize * CueParams->BurstPoint));
	}

	for (UNiagaraUIWidget* Particles : Found)
	{
		switch (CueParams->Action)
		{
		case EWidgetAnimParticleCueAction::BurstAtTarget:
			if (CueParams->Place == EWidgetAnimParticleCuePlace::ParticlesWidget)
			{
				// Where the Particles widget is - its Origin, worked out as it is drawn.
				Particles->StartBurst(TrackSystem, FVector2f::ZeroVector, /*bAtOrigin=*/true, TrackOverrides);
			}
			else
			{
				// The track widget's point, put into the Particles widget's own space, wherever that one is.
				const FVector2f LocalPoint = FVector2f(FVector2D(Particles->GetCachedGeometry().AbsoluteToLocal(AbsolutePoint)));
				Particles->StartBurst(TrackSystem, LocalPoint, /*bAtOrigin=*/false, TrackOverrides);
			}
			break;
		case EWidgetAnimParticleCueAction::Play:
			// The track's system as the widget's own, or the widget's system when the track names none (or uses the widget's).
			Particles->PlayWithOverrides(TrackSystem, TrackOverrides);
			break;
		case EWidgetAnimParticleCueAction::StopEmitting:
			Particles->StopEmitting();
			break;
		case EWidgetAnimParticleCueAction::Clear:
			Particles->Clear();
			break;
		default:
			break;
		}
	}
}

bool FWidgetAnimParticleCueEvaluator::RefreshParams(FInstancedStruct& Params) const
{
	FWidgetAnimParticleCueParams* CueParams = Params.GetMutablePtr<FWidgetAnimParticleCueParams>();
	if (!CueParams)
	{
		return false;
	}

	// Laid out from the track's system even while Use Widget's Niagara System hides them, so turning it off finds them as left.
	bool bChanged = false;
	NiagaraUIParameters::LayOut(CueParams->NiagaraSystem, CueParams->UserParameters, CueParams->UserParameterDefaults, CueParams->PositionParameters, &bChanged);
	return bChanged;
}

uint32 FWidgetAnimParticleCueEvaluator::GetParamsLayoutHash(const FInstancedStruct& Params) const
{
	const FWidgetAnimParticleCueParams* CueParams = GetParams(Params);
	return CueParams ? PointerHash(CueParams->UserParameters.GetPropertyBagStruct()) : 0;
}

UNiagaraUIWidget* FWidgetAnimParticleCueEvaluator::FindParticlesWidget(UWidget* TrackWidget, UWidget* Host, FName WidgetName)
{
	TArray<UNiagaraUIWidget*> Widgets;
	FindParticlesWidgets(TrackWidget, Host, WidgetName, /*bAll=*/false, Widgets);
	return Widgets.Num() > 0 ? Widgets[0] : nullptr;
}

namespace WidgetAnimParticleCuePrivate
{
	/** Every Niagara Particles widget at or beneath Widget, through panels and into User Widgets - or the first, unless bAll. */
	void CollectParticlesWidgets(UWidget* Widget, bool bAll, TArray<UNiagaraUIWidget*>& OutWidgets)
	{
		if (!Widget || (!bAll && OutWidgets.Num() > 0))
		{
			return;
		}
		if (UNiagaraUIWidget* Particles = Cast<UNiagaraUIWidget>(Widget))
		{
			OutWidgets.AddUnique(Particles);
			return;
		}
		if (const UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree)
			{
				CollectParticlesWidgets(UserWidget->WidgetTree->RootWidget, bAll, OutWidgets);
			}
			return;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				CollectParticlesWidgets(Panel->GetChildAt(ChildIndex), bAll, OutWidgets);
			}
		}
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

void FWidgetAnimParticleCueEvaluator::FindParticlesWidgets(UWidget* TrackWidget, UWidget* Host, FName WidgetName, bool bAll, TArray<UNiagaraUIWidget*>& OutWidgets)
{
	using namespace WidgetAnimParticleCuePrivate;

	if (!WidgetName.IsNone())
	{
		// By name: in the User Widget the track's widget (or the host) is in, then in each one around it.
		for (UWidget* From : { TrackWidget, Host })
		{
			for (UUserWidget* UserWidget = FirstUserWidget(From); UserWidget; UserWidget = UserWidget->GetTypedOuter<UUserWidget>())
			{
				if (UNiagaraUIWidget* Named = Cast<UNiagaraUIWidget>(UserWidget->GetWidgetFromName(WidgetName)))
				{
					OutWidgets.AddUnique(Named);
					if (!bAll)
					{
						return;
					}
				}
			}
		}
		return;
	}

	// Unnamed: the track's widget itself - a track aimed at the Particles widget - or what is inside it.
	CollectParticlesWidgets(TrackWidget, bAll, OutWidgets);
	if (OutWidgets.Num() > 0)
	{
		return;
	}

	// Else the nearest around it: in the User Widget it is in, then in each one around that - the first that has any.
	for (UUserWidget* UserWidget = FirstUserWidget(TrackWidget); UserWidget; UserWidget = UserWidget->GetTypedOuter<UUserWidget>())
	{
		CollectParticlesWidgets(UserWidget, bAll, OutWidgets);
		if (OutWidgets.Num() > 0)
		{
			return;
		}
	}
}

#undef LOCTEXT_NAMESPACE
