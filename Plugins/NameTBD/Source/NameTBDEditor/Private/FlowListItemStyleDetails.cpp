// Copyright AR GameSystems 2026. All rights reserved.

#include "FlowListItemStyleDetails.h"

#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/Widgets/FlowList/FlowListItemStyle.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UObjectGlobals.h"

namespace FlowListItemStyleDetailsLocal
{
	bool IsEmptyLook(const FInstancedPropertyBag& Look)
	{
		const UPropertyBag* Layout = Look.GetPropertyBagStruct();
		return !Layout || Layout->GetPropertyDescs().Num() == 0;
	}

	/** Whether the widget has a material a parameter could be set on - an Image's or Border's brush, a Text Block's font, a Retainer Box's effect. */
	bool HasAnyMaterialSlot(const UWidget* Widget)
	{
		for (const EWidgetAnimMaterialSlot Slot : { EWidgetAnimMaterialSlot::Brush, EWidgetAnimMaterialSlot::Font, EWidgetAnimMaterialSlot::FontOutline, EWidgetAnimMaterialSlot::Effect })
		{
			if (UWidgetAnimLibrary::WidgetHasMaterialSlot(Widget, Slot))
			{
				return true;
			}
		}
		return false;
	}
}

TSharedRef<IDetailCustomization> FFlowListItemStyleDetails::MakeInstance()
{
	return MakeShared<FFlowListItemStyleDetails>();
}

FFlowListItemStyleDetails::~FFlowListItemStyleDetails()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

void FFlowListItemStyleDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	WeakBuilder = DetailBuilder;
	if (DetailBuilder.IsValid())
	{
		CustomizeDetails(*DetailBuilder);
	}
}

void FFlowListItemStyleDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace FlowListItemStyleDetailsLocal;

	DetailBuilder.GetObjectsBeingCustomized(Customized);

	TArray<UFlowListItemStyle*> Styles;
	bool bLaidOut = false;
	for (const TWeakObjectPtr<UObject>& Object : Customized)
	{
		if (UFlowListItemStyle* Style = Cast<UFlowListItemStyle>(Object.Get()))
		{
			Styles.Add(Style);
			if (Style->SyncLooksToOwner())
			{
				Style->MarkPackageDirty();
				bLaidOut = true;
			}
		}
	}

	// The rows were made from the looks as they were: shown again, next frame, from the looks as they are now.
	if (bLaidOut)
	{
		const TWeakPtr<IPropertyUtilities> WeakUtilities = DetailBuilder.GetPropertyUtilities();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakUtilities](float)
		{
			if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
			{
				Utilities->ForceRefresh();
			}
			return false;
		}));
	}

	if (Styles.Num() == 0)
	{
		return;
	}

	// Only what applies to the widget is shown - not an empty look, or a setting greyed out.
	auto AllStyles = [&Styles](TFunctionRef<bool(const UFlowListItemStyle&)> Predicate)
	{
		for (const UFlowListItemStyle* Style : Styles)
		{
			if (!Predicate(*Style))
			{
				return false;
			}
		}
		return true;
	};
	auto Hide = [&DetailBuilder](FName PropertyName)
	{
		DetailBuilder.HideProperty(DetailBuilder.GetProperty(PropertyName, UFlowListItemStyle::StaticClass()));
	};

	// A look with nothing in it: a widget with no design settings (a Shape Box), or no shape (anything but a shape or a
	// Shape Box) - or one not found yet.
	if (AllStyles([](const UFlowListItemStyle& Style) { return IsEmptyLook(Style.UnselectedLook) && IsEmptyLook(Style.SelectedLook); }))
	{
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, UnselectedLook));
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, SelectedLook));
	}
	if (AllStyles([](const UFlowListItemStyle& Style) { return IsEmptyLook(Style.UnselectedShape) && IsEmptyLook(Style.SelectedShape); }))
	{
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, UnselectedShape));
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, SelectedShape));
	}

	// Material parameters, on a widget with no material to set them on. Kept while any are set, so none are hidden away.
	if (AllStyles([](const UFlowListItemStyle& Style)
	{
		const UWidget* Source = Style.FindLayoutSource();
		return Source && !HasAnyMaterialSlot(Source) && Style.MaterialParameters.Num() == 0;
	}))
	{
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, MaterialParameters));
	}

	// Nearness To Focus follows the list itself: no Transition.
	if (AllStyles([](const UFlowListItemStyle& Style) { return Style.Trigger == EFlowListStyleTrigger::Focus; }))
	{
		Hide(GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, Transition));
	}

	// Choices that change which rows apply lay the Details out again - see the class comment.
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FFlowListItemStyleDetails::HandleObjectPropertyChanged);
}

void FFlowListItemStyleDetails::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (!Object || (Event.ChangeType & EPropertyChangeType::Interactive) != 0
		|| !Customized.ContainsByPredicate([Object](const TWeakObjectPtr<UObject>& Customizing) { return Customizing.Get() == Object; }))
	{
		return;
	}
	static const FName Trigger = GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, Trigger);
	static const FName Mode = GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Mode);
	static const FName IsColor = GET_MEMBER_NAME_CHECKED(FFlowListStyleMaterialParameter, bColor);
	static const FName MaterialParameters = GET_MEMBER_NAME_CHECKED(UFlowListItemStyle, MaterialParameters);
	const FName Name = Event.GetPropertyName();
	const bool bArrayChange = (Event.ChangeType & (EPropertyChangeType::ArrayAdd | EPropertyChangeType::ArrayRemove | EPropertyChangeType::ArrayClear)) != 0;
	if (Name == Trigger || Name == Mode || Name == IsColor || (bArrayChange && Event.GetMemberPropertyName() == MaterialParameters))
	{
		RefreshSoon();
	}
}

void FFlowListItemStyleDetails::RefreshSoon()
{
	// Next frame, never under the row that was just edited.
	const TWeakPtr<IDetailLayoutBuilder> Weak = WeakBuilder;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
	{
		if (const TSharedPtr<IDetailLayoutBuilder> Builder = Weak.Pin())
		{
			Builder->ForceRefreshDetails();
		}
		return false;
	}));
}
