// Copyright AR GameSystems 2026. All rights reserved.

#include "UserWidget_Animatable.h"

UUserWidget_Animatable::UUserWidget_Animatable(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	WidgetAnimationComponent = ObjectInitializer.CreateDefaultSubobject<UWidgetAnimationComponent>(this, TEXT("WidgetAnimationComponent"));
}

UWidgetAnimationComponent& UUserWidget_Animatable::GetAnim() const
{
	check(WidgetAnimationComponent);

	if (!IsTemplate() && WidgetAnimationComponent->GetAnimatedWidget() != this)
	{
		// const_cast: binding is bookkeeping on the component, not a change to this widget.
		WidgetAnimationComponent->BindToWidget(const_cast<UUserWidget_Animatable*>(this));
	}

	return *WidgetAnimationComponent;
}

// =====================================================================================================
// Pass-throughs
// =====================================================================================================

void UUserWidget_Animatable::PlayIn(bool bCascadeToChildren)
{
	GetAnim().PlayIn(bCascadeToChildren);
}

void UUserWidget_Animatable::PlayOut(bool bCascadeToChildren)
{
	GetAnim().PlayOut(bCascadeToChildren);
}

bool UUserWidget_Animatable::IsAnimClosing() const
{
	return GetAnim().IsAnimClosing();
}

EWidgetAnimPhase UUserWidget_Animatable::GetAnimPhase() const
{
	return GetAnim().GetAnimPhase();
}

void UUserWidget_Animatable::RebuildEffectiveAnimations()
{
	GetAnim().RebuildEffectiveAnimations();
}

void UUserWidget_Animatable::SetExtraAnimSettings(const TArray<FWidgetAnimSettings>& InExtraAnimIn, const TArray<FWidgetAnimSettings>& InExtraAnimOut)
{
	GetAnim().SetExtraAnimSettings(InExtraAnimIn, InExtraAnimOut);
}

TArray<FString> UUserWidget_Animatable::GetAnimTargetWidgetOptions() const
{
	return GetAnim().GetAnimTargetWidgetOptions();
}

UWidget* UUserWidget_Animatable::ResolveAnimTarget(FName TargetWidgetName) const
{
	return GetAnim().ResolveAnimTarget(TargetWidgetName);
}

bool UUserWidget_Animatable::TickAnimationPlayback(float DeltaTime)
{
	// The component already ticks itself; this is only for graphs that clock it by hand.
	return GetAnim().TickAnimationPlayback(DeltaTime);
}

void UUserWidget_Animatable::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	GetAnim().PlayWidgetAnimationsById(AnimIds, bReverse, PlaybackSpeed, bHoldFinalPose, bRestartIfPlaying);
}

void UUserWidget_Animatable::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	GetAnim().StopWidgetAnimationsById(AnimIds, bResetToStart);
}

bool UUserWidget_Animatable::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return GetAnim().AreWidgetAnimationsPlayingById(AnimIds);
}

bool UUserWidget_Animatable::SetWidgetAnimationValues(FName ValueName, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying)
{
	return GetAnim().SetWidgetAnimationValues(ValueName, Value, bSnap, bStartIfNotPlaying);
}

FVector4 UUserWidget_Animatable::GetWidgetAnimationValues(FName ValueName, bool bTargetValue) const
{
	return GetAnim().GetWidgetAnimationValues(ValueName, bTargetValue);
}

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UUserWidget_Animatable::SetVisibility(ESlateVisibility InVisibility)
{
	// Straight through when the component is the one applying it, or when there is nothing to route to.
	if (!WidgetAnimationComponent || IsTemplate() || WidgetAnimationComponent->IsApplyingVisibility())
	{
		Super::SetVisibility(InVisibility);
		return;
	}

	GetAnim().SetVisibilityAnimated(InVisibility);
}

void UUserWidget_Animatable::NativeConstruct()
{
	Super::NativeConstruct();

	UWidgetAnimationComponent& Anim = GetAnim();
	if (!OutFinishedHandle.IsValid())
	{
		OutFinishedHandle = Anim.OnOutAnimationFinishedNative.AddUObject(this, &UUserWidget_Animatable::HandleComponentOutFinished);
	}

	Anim.NotifyOwnerConstructed();
}

void UUserWidget_Animatable::NativeDestruct()
{
	if (WidgetAnimationComponent)
	{
		WidgetAnimationComponent->NotifyOwnerDestructed();
		WidgetAnimationComponent->OnOutAnimationFinishedNative.Remove(OutFinishedHandle);
	}
	OutFinishedHandle.Reset();

	Super::NativeDestruct();
}

void UUserWidget_Animatable::HandleComponentOutFinished(UWidgetAnimationComponent* /*Component*/)
{
	OnOutAnimationFinished();
}

void UUserWidget_Animatable::OnOutAnimationFinished_Implementation()
{
	// Nothing by default: the component has already applied any deferred visibility.
}

void UUserWidget_Animatable::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// The component previews itself in the designer; a built-in one just needs telling when that is.
	if (IsDesignTime() && WidgetAnimationComponent && !IsTemplate())
	{
		GetAnim().NotifyOwnerDesignTimeRefresh();
	}
}

void UUserWidget_Animatable::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	if (IsDesignTime() && WidgetAnimationComponent && !IsTemplate())
	{
		GetAnim().EnsureDesignerPreviewTimer();
	}
}

// =====================================================================================================
// Migration
// =====================================================================================================

void UUserWidget_Animatable::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FWidgetAnimationComponentVersion::GUID);
}

void UUserWidget_Animatable::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (WidgetAnimationComponent && GetLinkerCustomVersion(FWidgetAnimationComponentVersion::GUID) < FWidgetAnimationComponentVersion::MovedIntoComponent)
	{
		const UUserWidget_Animatable* Archetype = Cast<UUserWidget_Animatable>(GetArchetype());
		if (UWidgetAnimationComponent::ShouldDiffLegacyAgainstArchetype(Archetype))
		{
			const FWidgetAnimationLegacySettings ArchetypeLegacy = Archetype->GatherLegacyWidgetAnimationSettings();
			WidgetAnimationComponent->ImportLegacySettings(GatherLegacyWidgetAnimationSettings(), &ArchetypeLegacy);
		}
		else
		{
			WidgetAnimationComponent->ImportLegacySettings(GatherLegacyWidgetAnimationSettings(), nullptr);
		}
	}
#endif
}

#if WITH_EDITORONLY_DATA
FWidgetAnimationLegacySettings UUserWidget_Animatable::GatherLegacyWidgetAnimationSettings() const
{
	FWidgetAnimationLegacySettings Legacy;
	Legacy.bPlayOnlyOnConstructAndDestruct   = bPlayOnlyOnConstructAndDestruct_DEPRECATED;
	Legacy.bPlayInAnimationsOnConstruct      = bPlayInAnimationsOnConstruct_DEPRECATED;
	Legacy.bPlayAnimationsOnVisibilityHidden = bPlayAnimationsOnVisibilityHidden_DEPRECATED;
	Legacy.AnimIn                            = AnimIn_DEPRECATED;
	Legacy.AnimOut                           = AnimOut_DEPRECATED;
	Legacy.bPreserveAuthoredTransforms       = bPreserveAuthoredTransforms_DEPRECATED;
	Legacy.DesignerPreview                   = DesignerPreview_DEPRECATED;
	return Legacy;
}
#endif