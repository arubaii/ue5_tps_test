// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListItemStyle.h"

#include "FlowList.h"
#include "NameTBD/WidgetAnimation/WidgetAnimHost.h"
#include "NameTBD/WidgetAnimation/WidgetDesignAnimation.h"
#include "NameTBD/Widgets/ShapeBox.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITORONLY_DATA
#include "Engine/Blueprint.h"
#endif

namespace FlowListItemStyleLocal
{
	/**
	 * The name a component is kept under where it is held: an array of structs among its outers, with an entry holding the
	 * component and a name beside it - how UMG's component container pairs each component with its widget. Found by
	 * reflection, as the Widget Animation Component finds its own, so nothing depends on the container's private layout.
	 */
	FName FindHeldUnderName(const UObject* Held)
	{
		static constexpr int32 MaxOuters = 4;
		int32 Depth = 0;
		for (const UObject* Holder = Held ? Held->GetOuter() : nullptr; Holder && Depth < MaxOuters; Holder = Holder->GetOuter(), ++Depth)
		{
			for (TFieldIterator<FArrayProperty> It(Holder->GetClass()); It; ++It)
			{
				const FArrayProperty* ArrayProperty = *It;
				const FStructProperty* Inner = CastField<FStructProperty>(ArrayProperty->Inner);
				if (!Inner)
				{
					continue;
				}
				FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Holder));
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const uint8* Element = Helper.GetRawPtr(Index);
					bool bHoldsIt = false;
					FName HeldName;
					for (TFieldIterator<FProperty> FieldIt(Inner->Struct); FieldIt; ++FieldIt)
					{
						if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(*FieldIt))
						{
							bHoldsIt |= ObjectProperty->GetObjectPropertyValue_InContainer(Element) == Held;
						}
						else if (const FNameProperty* NameProperty = CastField<FNameProperty>(*FieldIt))
						{
							HeldName = NameProperty->GetPropertyValue_InContainer(Element);
						}
					}
					if (bHoldsIt)
					{
						return HeldName;
					}
				}
			}
		}
		return NAME_None;
	}

	/** The settings a shape look holds for Widget: a shape widget's shape settings, a Shape Box's outline, or none. And their kind. */
	const UClass* GetShapeProperties(const UWidget* Widget, TArray<const FProperty*>& OutProperties)
	{
		OutProperties.Reset();
		if (!Widget)
		{
			return nullptr;
		}
		if (Widget->IsA<UShapeWidget>())
		{
			const UClass* Kind = Widget->GetClass();
			while (Kind && !Kind->HasAnyClassFlags(CLASS_Native))
			{
				Kind = Kind->GetSuperClass();
			}
			WidgetShapeAnimation::GetProperties(Kind, OutProperties);
			return Kind;
		}
		if (Widget->IsA<UShapeBox>())
		{
			if (const FProperty* Shape = FindFProperty<FProperty>(UShapeBox::StaticClass(), TEXT("Shape")))
			{
				OutProperties.Add(Shape);
			}
			return UShapeBox::StaticClass();
		}
		return nullptr;
	}

	constexpr float RestDistance = 0.001f;
	constexpr float RestSpeed = 0.01f;
}

UFlowListItemStyle::UFlowListItemStyle()
{
	Transition.Mode = EWidgetSpringMode::DurationAndBounce;
	Transition.VisualDuration = 0.22f;
	Transition.Bounce = 0.f;
}

// =====================================================================================================
// Looks
// =====================================================================================================

UWidget* UFlowListItemStyle::GetStyledWidget() const
{
	return GetOwner().Get();
}

UWidget* UFlowListItemStyle::FindLayoutSource() const
{
	if (UWidget* Styled = GetStyledWidget())
	{
		return Styled;
	}

#if WITH_EDITOR
	// The Details panel shows the template, which is on no widget: a designer preview's copy of it is.
	for (TObjectIterator<UFlowListItemStyle> It(RF_ClassDefaultObject | RF_ArchetypeObject, /*bIncludeDerivedClasses=*/true, EInternalObjectFlags::Garbage); It; ++It)
	{
		const UFlowListItemStyle* Candidate = *It;
		if (Candidate == this || !Candidate->GetStyledWidget())
		{
			continue;
		}
		for (const UObject* Archetype = Candidate->GetArchetype(); Archetype; Archetype = Archetype->GetArchetype())
		{
			if (Archetype == this)
			{
				return Candidate->GetStyledWidget();
			}
			if (Archetype->HasAnyFlags(RF_ClassDefaultObject))
			{
				break;
			}
		}
	}

	// No preview: the widget as authored in the Widget Blueprint's tree, by the name the component is kept under.
	const UWidgetBlueprintGeneratedClass* AuthoredClass = GetTypedOuter<UWidgetBlueprintGeneratedClass>();
#if WITH_EDITORONLY_DATA
	if (!AuthoredClass)
	{
		if (const UBlueprint* Blueprint = GetTypedOuter<UBlueprint>())
		{
			AuthoredClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		}
	}
#endif
	if (const UWidgetTree* AuthoredTree = AuthoredClass ? AuthoredClass->GetWidgetTreeArchetype() : nullptr)
	{
		const FName HeldName = FlowListItemStyleLocal::FindHeldUnderName(this);
		return HeldName.IsNone() ? nullptr : AuthoredTree->FindWidget(HeldName);
	}
#endif
	return nullptr;
}

bool UFlowListItemStyle::SyncLooksToOwner()
{
	const UWidget* Source = FindLayoutSource();
	if (!Source)
	{
		return false;
	}

	bool bChanged = false;
	const UClass* Kind = WidgetDesignAnimation::GetDesignKind(Source->GetClass());
	for (FInstancedPropertyBag* Look : { &UnselectedLook, &SelectedLook })
	{
		if (!WidgetDesignAnimation::IsLaidOutFor(*Look, Kind))
		{
			bChanged |= WidgetDesignAnimation::LayOut(*Look, Kind, Source);
		}
	}

	TArray<const FProperty*> ShapeProperties;
	const UClass* ShapeKind = FlowListItemStyleLocal::GetShapeProperties(Source, ShapeProperties);
	for (FInstancedPropertyBag* Look : { &UnselectedShape, &SelectedShape })
	{
		if (ShapeProperties.Num() > 0)
		{
			if (!WidgetShapeAnimation::IsLaidOutWith(*Look, ShapeProperties))
			{
				bChanged |= WidgetShapeAnimation::LayOutWith(*Look, ShapeKind, Source, ShapeProperties);
			}
		}
		else if (Look->IsValid())
		{
			Look->Reset();
			bChanged = true;
		}
	}

	if (bChanged)
	{
		bDifferencesGathered = false;
	}
	return bChanged;
}

void UFlowListItemStyle::GatherDifferences()
{
	DesignDifferences.Reset();
	ShapeDifferences.Reset();
	bDifferencesGathered = true;

	const UWidget* Widget = GetStyledWidget();
	if (!Widget)
	{
		return;
	}

	// Only what the two looks set differently is ever written: everything else stays the widget's own, for anything else
	// that drives it.
	TArray<const FProperty*> Properties;
	WidgetDesignAnimation::GetProperties(Widget->GetClass(), Properties);
	for (const FProperty* Property : Properties)
	{
		const void* From = WidgetShapeAnimation::FindValue(UnselectedLook, Property);
		const void* To = WidgetShapeAnimation::FindValue(SelectedLook, Property);
		if (From && To && !Property->Identical(From, To))
		{
			DesignDifferences.Add(Property);
		}
	}

	FlowListItemStyleLocal::GetShapeProperties(Widget, Properties);
	for (const FProperty* Property : Properties)
	{
		const void* From = WidgetShapeAnimation::FindValue(UnselectedShape, Property);
		const void* To = WidgetShapeAnimation::FindValue(SelectedShape, Property);
		if (From && To && !Property->Identical(From, To))
		{
			ShapeDifferences.Add(Property);
		}
	}
}

void UFlowListItemStyle::Apply(bool bForce)
{
	UWidget* Widget = GetStyledWidget();
	if (!Widget)
	{
		return;
	}
	if (!bDifferencesGathered)
	{
		GatherDifferences();
		bForce = true;
	}
	if (!bForce && FMath::Abs(AppliedAmount - Amount) < 0.0005f)
	{
		return;
	}
	AppliedAmount = Amount;
	const float Alpha = Amount;

	// The widget's look: numbers and colors glide, an image or a font face cross-fades.
	if (DesignDifferences.Num() > 0)
	{
		TArray<const void*> From;
		TArray<const void*> To;
		for (const FProperty* Property : DesignDifferences)
		{
			From.Add(WidgetShapeAnimation::FindValue(UnselectedLook, Property));
			To.Add(WidgetShapeAnimation::FindValue(SelectedLook, Property));
		}
		bool bChanged = false;
		bool bNeedsSync = false;
		WidgetDesignAnimation::WriteBlends(Widget, DesignDifferences, From, To, Alpha, &bChanged, &bNeedsSync);
		if (bNeedsSync)
		{
			Widget->SynchronizeProperties();
		}
	}

	// Its shape.
	if (ShapeDifferences.Num() > 0)
	{
		if (UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
		{
			bool bChanged = false;
			for (const FProperty* Property : ShapeDifferences)
			{
				bChanged |= WidgetShapeAnimation::WriteBlend(Shape, Property, WidgetShapeAnimation::FindValue(UnselectedShape, Property),
					WidgetShapeAnimation::FindValue(SelectedShape, Property), Alpha);
			}
			if (bChanged)
			{
				Shape->InvalidateShape();
			}
		}
		else if (UShapeBox* Box = Cast<UShapeBox>(Widget))
		{
			const FProperty* Property = ShapeDifferences[0];
			const void* From = WidgetShapeAnimation::FindValue(UnselectedShape, Property);
			const void* To = WidgetShapeAnimation::FindValue(SelectedShape, Property);
			if (From && To)
			{
				FShapeBoxShape Blend = *static_cast<const FShapeBoxShape*>(From);
				WidgetShapeAnimation::LerpValue(Property, From, To, &Blend, Alpha);
				UWidgetAnimLibrary::ApplyShapeToBox(Box, Blend);
			}
		}
	}

	for (const FFlowListStyleMaterialParameter& Parameter : MaterialParameters)
	{
		if (Parameter.Parameter.IsNone())
		{
			continue;
		}
		if (UMaterialInstanceDynamic* Material = UWidgetAnimLibrary::GetWidgetDynamicMaterial(Widget, Parameter.Slot))
		{
			if (Parameter.bColor)
			{
				Material->SetVectorParameterValue(Parameter.Parameter, FMath::Lerp(Parameter.UnselectedColor, Parameter.SelectedColor, Alpha));
			}
			else
			{
				Material->SetScalarParameterValue(Parameter.Parameter, FMath::Lerp(Parameter.UnselectedScalar, Parameter.SelectedScalar, Alpha));
			}
		}
	}

	if (bStyleRenderOpacity)
	{
		Widget->SetRenderOpacity(FMath::Lerp(UnselectedRenderOpacity, SelectedRenderOpacity, Alpha));
	}
	if (bStyleRenderTransform)
	{
		FWidgetTransform Blend;
		Blend.Translation = FMath::Lerp(UnselectedTransform.Translation, SelectedTransform.Translation, static_cast<double>(Alpha));
		Blend.Scale = FMath::Lerp(UnselectedTransform.Scale, SelectedTransform.Scale, static_cast<double>(Alpha));
		Blend.Shear = FMath::Lerp(UnselectedTransform.Shear, SelectedTransform.Shear, static_cast<double>(Alpha));
		Blend.Angle = FMath::Lerp(UnselectedTransform.Angle, SelectedTransform.Angle, Alpha);
		Widget->SetRenderTransform(Blend);
	}

	// Selected past halfway: the event, and an animation each way. Not for the look it starts in.
	const bool bSelectedNow = Alpha >= 0.5f;
	if (bHasLastSelected && bSelectedNow != bLastSelected && !Widget->IsDesignTime())
	{
		const FName AnimId = bSelectedNow ? SelectedAnimId : UnselectedAnimId;
		if (!AnimId.IsNone())
		{
			UWidgetAnimPlayLibrary::PlayWidgetAnimation(Widget, { AnimId });
		}
		OnSelectedChanged.Broadcast(bSelectedNow);
	}
	bLastSelected = bSelectedNow;
	bHasLastSelected = true;
}

// =====================================================================================================
// Following the list
// =====================================================================================================

float UFlowListItemStyle::ReadGoal(const UFlowList** OutList, int32* OutIndex) const
{
	const UWidget* Widget = GetStyledWidget();
	UWidget* Item = nullptr;
	int32 Index = INDEX_NONE;
	const UFlowList* List = UFlowList::FindFlowListOfItem(Widget, Item, Index);
	if (OutList)
	{
		*OutList = List;
	}
	if (OutIndex)
	{
		*OutIndex = Index;
	}

	if (ForcedLook.IsSet())
	{
		return ForcedLook.GetValue() ? 1.f : 0.f;
	}

	// Hover is the item's - so the whole item lights - or the widget's own outside a list.
	const UWidget* Hovered = Item ? Item : Widget;
	const float Hover = Hovered && Hovered->IsHovered() ? 1.f : 0.f;
	const float Selected = List && Index != INDEX_NONE && Index == List->GetSelectedIndex() ? 1.f : 0.f;

	switch (Trigger)
	{
	case EFlowListStyleTrigger::Focus:
		return List && Item ? FMath::Clamp(1.f - FMath::Abs(List->GetItemDistance(Item)), 0.f, 1.f) : 0.f;
	case EFlowListStyleTrigger::Hovered:
		return Hover;
	case EFlowListStyleTrigger::SelectedOrHovered:
		return FMath::Max(Selected, Hover);
	case EFlowListStyleTrigger::Selected:
	default:
		return Selected;
	}
}

bool UFlowListItemStyle::Advance(float DeltaTime)
{
	const UFlowList* List = nullptr;
	int32 Index = INDEX_NONE;
	const float Goal = ReadGoal(&List, &Index);

	// Another item - a virtual list's entry reused - starts in its own look, as a new one would: no animation, no event.
	if (List != LastList.Get() || Index != LastItemIndex)
	{
		const bool bFirst = LastItemIndex == INDEX_NONE && !LastList.IsValid();
		LastList = List;
		LastItemIndex = Index;
		if (!bFirst)
		{
			Amount = Goal;
			Velocity = 0.f;
			bHasLastSelected = false;
			return false;
		}
	}

	// Nearness To Focus follows the list exactly: the list's own motion is the animation.
	if (Trigger == EFlowListStyleTrigger::Focus && !ForcedLook.IsSet())
	{
		Amount = Goal;
		Velocity = 0.f;
		return false;
	}

	FWidgetSpringState State;
	State.Value = Amount;
	State.Velocity = Velocity;
	const bool bAtRest = UWidgetAnimSpringLibrary::AdvanceSpring(Transition, Goal, State, DeltaTime,
		FlowListItemStyleLocal::RestDistance, FlowListItemStyleLocal::RestSpeed);
	Amount = State.Value;
	Velocity = State.Velocity;
	return !bAtRest;
}

bool UFlowListItemStyle::OnTick(float DeltaTime)
{
	if (!BoundList.IsValid())
	{
		BindList();
	}
	const bool bMoving = Advance(FMath::Min(DeltaTime, 0.1f));
	Apply(false);

	// Hover can't be heard, only looked at: a hover trigger keeps looking. Everything else is woken by the list.
	const bool bWatchHover = Trigger == EFlowListStyleTrigger::Hovered || Trigger == EFlowListStyleTrigger::SelectedOrHovered;
	if (bMoving || bWatchHover)
	{
		return true;
	}
	TickerHandle.Reset();
	return false;
}

void UFlowListItemStyle::HandleListUpdated()
{
	if (!TickerHandle.IsValid())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UFlowListItemStyle::OnTick));
	}
}

void UFlowListItemStyle::BindList()
{
	UWidget* Item = nullptr;
	int32 Index = INDEX_NONE;
	UFlowList* List = UFlowList::FindFlowListOfItem(GetStyledWidget(), Item, Index);
	if (List == BoundList.Get())
	{
		return;
	}
	if (UFlowList* Old = BoundList.Get())
	{
		Old->OnListUpdated.Remove(ListUpdatedHandle);
	}
	ListUpdatedHandle.Reset();
	BoundList = List;
	if (List)
	{
		ListUpdatedHandle = List->OnListUpdated.AddUObject(this, &UFlowListItemStyle::HandleListUpdated);
	}
}

void UFlowListItemStyle::Start()
{
	// Looks never laid out - a component added in code, or one whose Details were never shown - start as the widget is.
	SyncLooksToOwner();
	bDifferencesGathered = false;
	BindList();

	// Into the look it should start in, without animating there.
	const UFlowList* List = nullptr;
	Amount = ReadGoal(&List, &LastItemIndex);
	LastList = List;
	Velocity = 0.f;
	bHasLastSelected = false;
	Apply(true);

	HandleListUpdated();
}

void UFlowListItemStyle::Stop()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (UFlowList* List = BoundList.Get())
	{
		List->OnListUpdated.Remove(ListUpdatedHandle);
	}
	ListUpdatedHandle.Reset();
	BoundList.Reset();
}

void UFlowListItemStyle::ForceLook(bool bSelected)
{
	ForcedLook = bSelected;
	HandleListUpdated();
}

void UFlowListItemStyle::ClearForcedLook()
{
	ForcedLook.Reset();
	HandleListUpdated();
}

// =====================================================================================================
// Lifetime
// =====================================================================================================

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UFlowListItemStyle::OnInitialize()
{
	return true;
}
#else
void UFlowListItemStyle::OnInitialize()
{
}

void UFlowListItemStyle::OnPreConstruct(bool bIsDesignTime)
{
	// In the designer the preview starts in the look its list gives it, as it will at runtime.
	if (bIsDesignTime)
	{
		Start();
	}
}
#endif

void UFlowListItemStyle::OnConstruct()
{
	Start();
}

void UFlowListItemStyle::OnDestruct()
{
	Stop();
}

void UFlowListItemStyle::PostLoad()
{
	Super::PostLoad();
	bDifferencesGathered = false;
}

void UFlowListItemStyle::BeginDestroy()
{
	Stop();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UFlowListItemStyle::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncLooksToOwner();
	bDifferencesGathered = false;
}
#endif
