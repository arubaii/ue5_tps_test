// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetSpringPresets.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailChildrenBuilder.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetSpringPresets"

// =====================================================================================================
// Settings
// =====================================================================================================

UWidgetSpringPresetSettings& UWidgetSpringPresetSettings::Get()
{
	return *GetMutableDefault<UWidgetSpringPresetSettings>();
}

void UWidgetSpringPresetSettings::AddPreset(const FWidgetSpringPreset& Preset)
{
	const int32 Existing = Presets.IndexOfByPredicate([&Preset](const FWidgetSpringPreset& Other) { return Other.Name == Preset.Name; });
	if (Existing != INDEX_NONE)
	{
		Presets[Existing] = Preset;
	}
	else
	{
		Presets.Add(Preset);
	}
	Save();
}

void UWidgetSpringPresetSettings::RemovePreset(int32 Index)
{
	if (Presets.IsValidIndex(Index))
	{
		Presets.RemoveAt(Index);
		Save();
	}
}

void UWidgetSpringPresetSettings::Save()
{
	// Into the project's DefaultEditor.ini, which is checked in: the whole team gets them.
	SaveConfig();
	TryUpdateDefaultConfigFile();
}

// =====================================================================================================
// Built-in presets
// =====================================================================================================

namespace WidgetSpringPresets
{
	namespace
	{
		FWidgetSpringParams DurationAndBounce(float VisualDuration, float Bounce)
		{
			FWidgetSpringParams Spring;
			Spring.Mode = EWidgetSpringMode::DurationAndBounce;
			Spring.VisualDuration = VisualDuration;
			Spring.Bounce = Bounce;
			return Spring;
		}
	}

	const TArray<FBuiltIn>& GetBuiltIns()
	{
		// One of each feel UI motion is made of, from no overshoot to all wobble - every one written as Duration and Bounce,
		// so they compare at a glance and change pace without changing feel.
		static const TArray<FBuiltIn> BuiltIns =
		{
			{ LOCTEXT("Smooth", "Smooth"), LOCTEXT("SmoothDesc", "Arrives without any overshoot. The right default for most of what moves on screen."), DurationAndBounce(0.5f, 0.f) },
			{ LOCTEXT("Quick", "Quick"), LOCTEXT("QuickDesc", "Fast and clean, no overshoot - hovers, highlights, small things that should just be there."), DurationAndBounce(0.25f, 0.f) },
			{ LOCTEXT("Snappy", "Snappy"), LOCTEXT("SnappyDesc", "Quick, with a touch of overshoot that reads as decided - buttons, toggles, selections."), DurationAndBounce(0.35f, 0.15f) },
			{ LOCTEXT("Gentle", "Gentle"), LOCTEXT("GentleDesc", "Unhurried, with a faint settle - panels, cards, large things easing into place."), DurationAndBounce(0.7f, 0.1f) },
			{ LOCTEXT("Bouncy", "Bouncy"), LOCTEXT("BouncyDesc", "A clear bounce - something arriving, a reward, a playful pop."), DurationAndBounce(0.5f, 0.3f) },
			{ LOCTEXT("Wobbly", "Wobbly"), LOCTEXT("WobblyDesc", "Swings past and back a few times before it settles."), DurationAndBounce(0.6f, 0.5f) },
			{ LOCTEXT("Jelly", "Jelly"), LOCTEXT("JellyDesc", "Soft and wobbling for a long while, like jelly set down - squash and stretch, cartoony UI."), DurationAndBounce(0.65f, 0.72f) },
			{ LOCTEXT("Interactive", "Interactive"), LOCTEXT("InteractiveDesc", "Very fast, almost still at the end - for something following a finger, a cursor or a stick."), DurationAndBounce(0.15f, 0.14f) },
			{ LOCTEXT("Slow", "Slow"), LOCTEXT("SlowDesc", "A long, settled move - backgrounds, ambient motion, big transitions."), DurationAndBounce(1.f, 0.f) },
			{ LOCTEXT("Heavy", "Heavy"), LOCTEXT("HeavyDesc", "Damped past critical: it eases in slowly and creeps the last of the way, as if weighed down."), DurationAndBounce(1.f, -0.4f) },
		};
		return BuiltIns;
	}

	bool IsSameSpring(const FWidgetSpringParams& A, const FWidgetSpringParams& B)
	{
		if (A.Mode != B.Mode)
		{
			return false;
		}
		auto Near = [](float X, float Y) { return FMath::Abs(X - Y) <= FMath::Max(0.001f, 0.002f * FMath::Max(FMath::Abs(X), FMath::Abs(Y))); };
		return A.Mode == EWidgetSpringMode::DurationAndBounce
			? Near(A.VisualDuration, B.VisualDuration) && Near(A.Bounce, B.Bounce)
			: Near(A.Stiffness, B.Stiffness) && Near(A.Damping, B.Damping) && Near(A.Mass, B.Mass);
	}
}

// =====================================================================================================
// Details
// =====================================================================================================

TSharedRef<IPropertyTypeCustomization> FWidgetSpringParamsCustomization::MakeInstance()
{
	return MakeShared<FWidgetSpringParamsCustomization>();
}

void FWidgetSpringParamsCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& Utils)
{
	Handle = StructHandle;
	HeaderRow
		.NameContent()
		[
			StructHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(260.f)
		.MaxDesiredWidth(600.f)
		[
			MakePresetButton()
		];
}

void FWidgetSpringParamsCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& Utils)
{
	Handle = StructHandle;

	// Shown without its header (Show Only Inner Properties - a track's spring): the Preset menu gets a row of its own.
	if (StructHandle->HasMetaData(TEXT("ShowOnlyInnerProperties")))
	{
		ChildBuilder.AddCustomRow(LOCTEXT("PresetSearch", "Spring Preset"))
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("PresetRow", "Spring Preset"))
			]
			.ValueContent()
			.MinDesiredWidth(260.f)
			.MaxDesiredWidth(600.f)
			[
				MakePresetButton()
			];
	}

	uint32 NumChildren = 0;
	StructHandle->GetNumChildren(NumChildren);
	for (uint32 Index = 0; Index < NumChildren; ++Index)
	{
		if (const TSharedPtr<IPropertyHandle> Child = StructHandle->GetChildHandle(Index))
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
		}
	}
}

TSharedRef<SWidget> FWidgetSpringParamsCustomization::MakePresetButton()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("PresetTooltip", "Spring presets: ready-made feels from Smooth to Jelly, and the project's own (Project Settings > Plugins > Spring Presets). Save As Preset keeps this one."))
			.OnGetMenuContent(FOnGetContent::CreateSP(this, &FWidgetSpringParamsCustomization::MakePresetMenu))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &FWidgetSpringParamsCustomization::GetMatchText)
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(this, &FWidgetSpringParamsCustomization::GetSummaryText)
		];
}

bool FWidgetSpringParamsCustomization::ReadSpring(FWidgetSpringParams& OutSpring, bool& bOutMixed) const
{
	bOutMixed = false;
	if (!Handle.IsValid() || !Handle->IsValidHandle())
	{
		return false;
	}
	TArray<void*> RawData;
	Handle->AccessRawData(RawData);
	const FWidgetSpringParams* First = nullptr;
	for (const void* Raw : RawData)
	{
		const FWidgetSpringParams* Spring = static_cast<const FWidgetSpringParams*>(Raw);
		if (!Spring)
		{
			continue;
		}
		if (!First)
		{
			First = Spring;
		}
		else if (!WidgetSpringPresets::IsSameSpring(*First, *Spring))
		{
			bOutMixed = true;
		}
	}
	if (!First)
	{
		return false;
	}
	OutSpring = *First;
	return true;
}

FText FWidgetSpringParamsCustomization::GetMatchText() const
{
	FWidgetSpringParams Spring;
	bool bMixed = false;
	if (!ReadSpring(Spring, bMixed))
	{
		return LOCTEXT("NoSpring", "Preset");
	}
	if (bMixed)
	{
		return LOCTEXT("MixedSpring", "Multiple Values");
	}
	for (const FWidgetSpringPreset& Preset : UWidgetSpringPresetSettings::Get().Presets)
	{
		if (WidgetSpringPresets::IsSameSpring(Preset.Spring, Spring))
		{
			return FText::FromString(Preset.Name);
		}
	}
	for (const WidgetSpringPresets::FBuiltIn& BuiltIn : WidgetSpringPresets::GetBuiltIns())
	{
		if (WidgetSpringPresets::IsSameSpring(BuiltIn.Spring, Spring))
		{
			return BuiltIn.Name;
		}
	}
	return LOCTEXT("CustomSpring", "Custom");
}

FText FWidgetSpringParamsCustomization::GetSummaryText() const
{
	FWidgetSpringParams Spring;
	bool bMixed = false;
	if (!ReadSpring(Spring, bMixed) || bMixed)
	{
		return FText::GetEmpty();
	}

	// How it moves, whichever way it is written: its pace, how bouncy, and how long until it settles.
	FNumberFormattingOptions TwoPlaces;
	TwoPlaces.MinimumFractionalDigits = 2;
	TwoPlaces.MaximumFractionalDigits = 2;
	const float Ratio = UWidgetAnimSpringLibrary::GetDampingRatio(Spring);
	return FText::Format(LOCTEXT("SpringSummary", "{0} s, damping ratio {1}, settles in {2} s"),
		FText::AsNumber(UWidgetAnimSpringLibrary::GetVisualDuration(Spring), &TwoPlaces),
		FText::AsNumber(Ratio, &TwoPlaces),
		FText::AsNumber(UWidgetAnimSpringLibrary::GetSettleTime(Spring), &TwoPlaces));
}

void FWidgetSpringParamsCustomization::ApplySpring(const FWidgetSpringParams& Spring, const FText& Name)
{
	if (!Handle.IsValid() || !Handle->IsValidHandle())
	{
		return;
	}
	const FScopedTransaction Transaction(FText::Format(LOCTEXT("ApplyPreset", "Apply Spring Preset {0}"), Name));
	auto Child = [this](FName ChildName) { return Handle->GetChildHandle(ChildName); };
	if (Spring.Mode == EWidgetSpringMode::DurationAndBounce)
	{
		if (const TSharedPtr<IPropertyHandle> VisualDuration = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, VisualDuration)))
		{
			VisualDuration->SetValue(Spring.VisualDuration);
		}
		if (const TSharedPtr<IPropertyHandle> Bounce = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Bounce)))
		{
			Bounce->SetValue(Spring.Bounce);
		}
	}
	else
	{
		if (const TSharedPtr<IPropertyHandle> Stiffness = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Stiffness)))
		{
			Stiffness->SetValue(Spring.Stiffness);
		}
		if (const TSharedPtr<IPropertyHandle> Damping = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Damping)))
		{
			Damping->SetValue(Spring.Damping);
		}
		if (const TSharedPtr<IPropertyHandle> Mass = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Mass)))
		{
			Mass->SetValue(Spring.Mass);
		}
	}

	// The mode last: Details that show and hide fields by it lay themselves out again with the values already in.
	if (const TSharedPtr<IPropertyHandle> Mode = Child(GET_MEMBER_NAME_CHECKED(FWidgetSpringParams, Mode)))
	{
		Mode->SetValueFromFormattedString(StaticEnum<EWidgetSpringMode>()->GetNameStringByValue(static_cast<int64>(Spring.Mode)));
	}
}

void FWidgetSpringParamsCustomization::SaveAsPreset(const FString& Name)
{
	FWidgetSpringParams Spring;
	bool bMixed = false;
	const FString Trimmed = Name.TrimStartAndEnd();
	if (Trimmed.IsEmpty() || !ReadSpring(Spring, bMixed) || bMixed)
	{
		return;
	}
	FWidgetSpringPreset Preset;
	Preset.Name = Trimmed;
	Preset.Spring = Spring;
	UWidgetSpringPresetSettings::Get().AddPreset(Preset);
}

TSharedRef<SWidget> FWidgetSpringParamsCustomization::MakePresetMenu()
{
	FMenuBuilder Menu(/*bInShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const TWeakPtr<FWidgetSpringParamsCustomization> WeakThisPtr = SharedThis(this);

	FWidgetSpringParams Current;
	bool bMixed = false;
	const bool bHasCurrent = ReadSpring(Current, bMixed) && !bMixed;

	auto AddEntry = [&Menu, WeakThisPtr, bHasCurrent, Current](const FText& Name, const FText& ToolTip, const FWidgetSpringParams& Spring)
	{
		const bool bChecked = bHasCurrent && WidgetSpringPresets::IsSameSpring(Current, Spring);
		Menu.AddMenuEntry(Name, ToolTip, FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakThisPtr, Spring, Name]()
				{
					if (const TSharedPtr<FWidgetSpringParamsCustomization> Pinned = WeakThisPtr.Pin())
					{
						Pinned->ApplySpring(Spring, Name);
					}
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([bChecked]() { return bChecked; })),
			NAME_None, EUserInterfaceActionType::Check);
	};
	auto Describe = [](const FText& Description, const FWidgetSpringParams& Spring)
	{
		const FText Values = Spring.Mode == EWidgetSpringMode::DurationAndBounce
			? FText::Format(LOCTEXT("DescDuration", "Visual Duration {0} s, Bounce {1}"), FText::AsNumber(Spring.VisualDuration), FText::AsNumber(Spring.Bounce))
			: FText::Format(LOCTEXT("DescPhysical", "Stiffness {0}, Damping {1}, Mass {2}"), FText::AsNumber(Spring.Stiffness), FText::AsNumber(Spring.Damping), FText::AsNumber(Spring.Mass));
		return Description.IsEmpty() ? Values : FText::Format(LOCTEXT("DescJoined", "{0}\n{1}"), Description, Values);
	};

	// The project's own first: they are what a team has settled on.
	const TArray<FWidgetSpringPreset>& Own = UWidgetSpringPresetSettings::Get().Presets;
	if (Own.Num() > 0)
	{
		Menu.BeginSection(NAME_None, LOCTEXT("OwnSection", "Project Presets"));
		for (const FWidgetSpringPreset& Preset : Own)
		{
			AddEntry(FText::FromString(Preset.Name), Describe(FText::FromString(Preset.Description), Preset.Spring), Preset.Spring);
		}
		Menu.EndSection();
	}

	Menu.BeginSection(NAME_None, LOCTEXT("BuiltInSection", "Presets"));
	for (const WidgetSpringPresets::FBuiltIn& BuiltIn : WidgetSpringPresets::GetBuiltIns())
	{
		AddEntry(BuiltIn.Name, Describe(BuiltIn.Description, BuiltIn.Spring), BuiltIn.Spring);
	}
	Menu.EndSection();

	// Keeping this one: a name, then Enter or Save.
	Menu.BeginSection(NAME_None, LOCTEXT("SaveSection", "Save As Preset"));
	{
		const TSharedRef<SEditableTextBox> NameBox = SNew(SEditableTextBox)
			.HintText(LOCTEXT("PresetNameHint", "Preset name"))
			.ToolTipText(LOCTEXT("PresetNameTooltip", "Saves this spring under this name for the whole project - a name already used is replaced. Enter to save."))
			.IsEnabled(bHasCurrent)
			.OnTextCommitted_Lambda([WeakThisPtr](const FText& Text, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter)
				{
					if (const TSharedPtr<FWidgetSpringParamsCustomization> Pinned = WeakThisPtr.Pin())
					{
						Pinned->SaveAsPreset(Text.ToString());
						FSlateApplication::Get().DismissAllMenus();
					}
				}
			});
		Menu.AddWidget(
			SNew(SBox)
			.WidthOverride(220.f)
			.Padding(FMargin(4.f, 2.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					NameBox
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(SButton)
					.Text(LOCTEXT("SavePreset", "Save"))
					.IsEnabled(bHasCurrent)
					.OnClicked_Lambda([WeakThisPtr, NameBox]()
					{
						if (const TSharedPtr<FWidgetSpringParamsCustomization> Pinned = WeakThisPtr.Pin())
						{
							Pinned->SaveAsPreset(NameBox->GetText().ToString());
						}
						FSlateApplication::Get().DismissAllMenus();
						return FReply::Handled();
					})
				]
			],
			FText::GetEmpty(), /*bNoIndent=*/true);

		if (Own.Num() > 0)
		{
			Menu.AddSubMenu(LOCTEXT("RemovePreset", "Remove Preset"), LOCTEXT("RemovePresetTooltip", "Takes one of the project's presets off every spring's menu. Renaming and reordering: Project Settings > Plugins > Spring Presets."),
				FNewMenuDelegate::CreateLambda([](FMenuBuilder& SubMenu)
				{
					const TArray<FWidgetSpringPreset>& Presets = UWidgetSpringPresetSettings::Get().Presets;
					for (int32 Index = 0; Index < Presets.Num(); ++Index)
					{
						SubMenu.AddMenuEntry(FText::FromString(Presets[Index].Name), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([Index]() { UWidgetSpringPresetSettings::Get().RemovePreset(Index); })));
					}
				}));
		}
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
