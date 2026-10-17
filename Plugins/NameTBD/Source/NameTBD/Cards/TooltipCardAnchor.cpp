// Copyright AR GameSystems 2026. All rights reserved.

#include "TooltipCardAnchor.h"

#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "GameFramework/PlayerController.h"

#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"

const FName UTooltipCardAnchor::BuiltInTooltipCardId(TEXT("Tooltip"));

namespace TooltipCardAnchorPrivate
{
	TSubclassOf<UInfoCardWidget> DefaultCardClass()
	{
		const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
		if (Settings && !Settings->DefaultTooltipCardClass.IsNull())
		{
			if (UClass* Loaded = Settings->DefaultTooltipCardClass.LoadSynchronous())
			{
				return Loaded;
			}
		}
		return UInfoCardTextWidget::StaticClass();
	}

	template <typename PayloadType>
	PayloadType* FindOrAddPayload(FInfoCardSpec& Spec, UObject* Outer)
	{
		for (const TObjectPtr<UNameTBD_Payload>& Payload : Spec.Payloads)
		{
			if (PayloadType* Found = Cast<PayloadType>(Payload.Get()))
			{
				return Found;
			}
		}

		PayloadType* Added = NewObject<PayloadType>(Outer);
		Spec.Payloads.Add(Added);
		return Added;
	}
}

FInfoCardSpec UTooltipCardAnchor::MakeBuiltInTooltipSpec()
{
	FInfoCardSpec Spec;
	Spec.CardId = BuiltInTooltipCardId;
	Spec.CardClass = TooltipCardAnchorPrivate::DefaultCardClass();
	Spec.CardType = ECardType::Hover;
	Spec.PositionMode = EInfoCardPosition::ToIcon;
	Spec.IconPosition = EDirection2D::Up;
	Spec.IconAnchorPadding = 6.f;

	// Any click puts a tooltip away (and it comes back once the cursor leaves and returns), as an OS tooltip does.
	Spec.RemoveCardOnPressing = ECardRemoveOnPressZone::Anywhere;

	// A short fade in, mirrored as the fade out.
	FInfoCardAdditiveAnimSettings Fade;
	Fade.AnimType = EInfoCardAdditiveAnimType::Fade;
	Fade.Duration = 0.12f;
	Fade.Easing = EWidgetAnimEasing::EaseOut;
	Fade.StartOpacity = 0.f;
	Fade.EndOpacity = 1.f;
	Fade.bMirrorInOut = true;
	Spec.AdditiveAnimations.bUseBasicAnimations = true;
	Spec.AdditiveAnimations.PlayIn.Add(Fade);

	return Spec;
}

const UInfoCardDataAsset* UTooltipCardAnchor::ResolveTemplate() const
{
	if (TooltipTemplate)
	{
		return TooltipTemplate;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	return (Settings && !Settings->DefaultTooltipTemplate.IsNull()) ? Settings->DefaultTooltipTemplate.LoadSynchronous() : nullptr;
}

TArray<FString> UTooltipCardAnchor::GetTemplateCardIdOptions() const
{
	TArray<FString> Options;
	Options.Add(TEXT("None"));

	if (const UInfoCardDataAsset* Template = ResolveTemplate())
	{
		for (const FInfoCardSpec& Spec : Template->InfoCards)
		{
			if (!Spec.CardId.IsNone())
			{
				Options.AddUnique(Spec.CardId.ToString());
			}
		}
	}
	return Options;
}

FName UTooltipCardAnchor::GetTooltipCardId() const
{
	return InfoCards.IsValidIndex(0) ? InfoCards[0].CardId : BuiltInTooltipCardId;
}

bool UTooltipCardAnchor::BuildGeneratedInfoCards(TArray<FInfoCardSpec>& OutSpecs)
{
	const UInfoCardDataAsset* Template = ResolveTemplate();

	const FInfoCardSpec* TemplateSpec = nullptr;
	if (Template && Template->InfoCards.Num() > 0)
	{
		TemplateSpec = TemplateCardId.IsNone()
			? &Template->InfoCards[0]
			: Template->InfoCards.FindByPredicate([this](const FInfoCardSpec& Candidate) { return Candidate.CardId == TemplateCardId; });

		// A Template Card ID the template no longer has falls back to its first entry rather than to no tooltip.
		if (!TemplateSpec)
		{
			TemplateSpec = &Template->InfoCards[0];
		}
	}

	FInfoCardSpec Spec = TemplateSpec ? MakeSpecFromDataAsset(*Template, *TemplateSpec, this) : MakeBuiltInTooltipSpec();

	if (!Spec.CardClass)
	{
		Spec.CardClass = TooltipCardAnchorPrivate::DefaultCardClass();
	}
	if (Spec.CardId.IsNone())
	{
		Spec.CardId = BuiltInTooltipCardId;
	}

	// The words: into the template's text payload when it has one (so its other fields stay), else a new one.
	UInfoCardPayload_Text* TextPayload = TooltipCardAnchorPrivate::FindOrAddPayload<UInfoCardPayload_Text>(Spec, this);
	TextPayload->Title = TooltipTitleText;
	TextPayload->Body = TooltipBodyText;

	if (bOverrideTooltipTextStyle)
	{
		TooltipCardAnchorPrivate::FindOrAddPayload<UInfoCardPayload_TextStyle>(Spec, this)->TextStyle = TooltipTextStyle;
	}

	if (bOverrideTooltipPosition)
	{
		TooltipPositioning.ApplyTo(Spec);
	}
#if WITH_EDITOR
	else if (IsDesignTime())
	{
		// An override not switched on shows the template's placement, so switching it on starts from there.
		TooltipPositioning = FInfoCardPositioning::FromSpec(Spec);
	}
#endif

	if (bKeepsOtherCardsOpen)
	{
		Spec.CloseOtherCardsScope = ECardCloseOthersScope::None;
	}

	if (bOverrideTooltipDelay)
	{
		Spec.HoverDelay.Source = EInfoCardValueSource::Custom;
		Spec.HoverDelay.CustomValue = TooltipDelay;
	}

	// Nothing to say, nothing to show. Kept as an entry (switched off) so the Card ID still resolves for Blueprint.
	if (TooltipBodyText.IsEmpty() && TooltipTitleText.IsEmpty())
	{
		Spec.bEnabled = false;
	}

	OutSpecs.Add(MoveTemp(Spec));
	return true;
}

void UTooltipCardAnchor::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// The old override only chose the side (To Icon), keeping the rest of the template's placement: the rest is taken from
	// this anchor's own template when it has one (already loaded with it), else from the built-in tooltip.
	if (bOverrideTooltipPosition && TooltipPosition_DEPRECATED != EDirection2D::Up)
	{
		const FInfoCardSpec* TemplateSpec = TooltipTemplate && TooltipTemplate->InfoCards.Num() > 0 ? &TooltipTemplate->InfoCards[0] : nullptr;
		TooltipPositioning = FInfoCardPositioning::FromSpec(TemplateSpec ? *TemplateSpec : MakeBuiltInTooltipSpec());
		TooltipPositioning.PositionMode = EInfoCardPosition::ToIcon;
		TooltipPositioning.IconPosition = TooltipPosition_DEPRECATED;
	}
	TooltipPosition_DEPRECATED = EDirection2D::Up;
#endif
}

void UTooltipCardAnchor::SetTooltipCardText(FText NewText, FText NewTitle)
{
	TooltipBodyText = NewText;
	TooltipTitleText = NewTitle;
	RebuildGeneratedInfoCards();
}

// =================================================================================================
// UInfoCardLibrary
// =================================================================================================

UInfoCardAnchor* UInfoCardLibrary::FindInfoCardAnchorAround(UWidget* Widget)
{
	UWidget* Current = Widget;
	for (int32 Depth = 0; Current && Depth < 256; ++Depth)
	{
		if (UInfoCardAnchor* Anchor = Cast<UInfoCardAnchor>(Current))
		{
			return Anchor;
		}

		if (UPanelWidget* Parent = Current->GetParent())
		{
			Current = Parent;
			continue;
		}

		// The root of a widget tree: carry on from the User Widget that tree belongs to, which sits in its own parent's tree.
		UUserWidget* OwningUserWidget = Current->GetTypedOuter<UUserWidget>();
		Current = (OwningUserWidget != Current) ? OwningUserWidget : nullptr;
	}
	return nullptr;
}

UTooltipCardAnchor* UInfoCardLibrary::FindTooltipAnchorAround(UWidget* Widget)
{
	UWidget* Current = Widget;
	for (int32 Depth = 0; Current && Depth < 64; ++Depth)
	{
		UInfoCardAnchor* Anchor = FindInfoCardAnchorAround(Current);
		if (!Anchor)
		{
			return nullptr;
		}
		if (UTooltipCardAnchor* Tooltip = Cast<UTooltipCardAnchor>(Anchor))
		{
			return Tooltip;
		}

		// Some other kind of anchor (a context menu around the same widget): keep looking above it.
		Current = Anchor->GetParent() ? static_cast<UWidget*>(Anchor->GetParent()) : static_cast<UWidget*>(Anchor->GetTypedOuter<UUserWidget>());
	}
	return nullptr;
}

UTooltipCardAnchor* UInfoCardLibrary::SetWidgetTooltipCard(UWidget* Widget, FText Text, FText Title, TSubclassOf<UInfoCardWidget> CardClass)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (UTooltipCardAnchor* Anchor = FindTooltipAnchorAround(Widget))
	{
		Anchor->SetTooltipCardText(Text, Title);
		return Anchor;
	}

	if (Text.IsEmpty() && Title.IsEmpty())
	{
		Widget->SetToolTip(nullptr);
		return nullptr;
	}

	const TSubclassOf<UInfoCardWidget> ClassToMake = CardClass ? CardClass : TooltipCardAnchorPrivate::DefaultCardClass();
	UInfoCardWidget* Card = nullptr;
	if (APlayerController* OwningPlayer = Widget->GetOwningPlayer())
	{
		Card = CreateWidget<UInfoCardWidget>(OwningPlayer, ClassToMake);
	}
	else if (UWorld* World = Widget->GetWorld())
	{
		Card = CreateWidget<UInfoCardWidget>(World, ClassToMake);
	}

	if (!Card)
	{
		return nullptr;
	}

	// Slate places an engine tooltip; the info card system must leave it alone (no viewport, no click-away, no keep-open).
	Card->MarkAsEmbedded();

	UInfoCardPayload_Text* TextPayload = NewObject<UInfoCardPayload_Text>(Card);
	TextPayload->Title = Title;
	TextPayload->Body = Text;
	Card->SetPayloads({ TextPayload });

	Widget->SetToolTip(Card);
	return nullptr;
}
