// Copyright AR GameSystems 2026. All rights reserved.

#include "SMenuSettingsPanel.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Kismet/GameplayStatics.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "NameTBD/Menu/Input/MenuInputSubsystem.h"
#include "NameTBD/Menu/Input/MenuKeyPrompt.h"
#include "NameTBD/Menu/Settings/MenuSettingTargets.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"
#include "MenuPartWidgets.h"
#include "SettingsMenu.h"
#include "SMenuImage.h"

#define LOCTEXT_NAMESPACE "SMenuSettingsPanel"

namespace MenuPanelLocal
{
	FText OrDefault(const FText& Text, const FText& Default)
	{
		return Text.IsEmpty() ? Default : Text;
	}
}

// =====================================================================================================
//  Construction
// =====================================================================================================

void SMenuSettingsPanel::Construct(const FArguments& InArgs, const FMenuStyleRef& InStyle)
{
	using namespace MenuPanelLocal;
	StyleHolder = InStyle;
	Settings = InArgs._Settings;
	LocalPlayer = InArgs._LocalPlayer;
	SoundContext = InArgs._SoundContext;
	PreviewPages = InArgs._PreviewPages;
	PageFilter = InArgs._PageFilter;
	bApplyButton = InArgs._ApplyButton;
	bConfirmUnsaved = InArgs._ConfirmUnsavedOnBack;
	bShowBack = InArgs._ShowBackButton;
	bShowReset = InArgs._ShowResetButton;
	Keys = InArgs._Keys;
	Texts = InArgs._Texts;
	TitleAttribute = InArgs._Title;
	MakeCustomRow = InArgs._MakeCustomRow;
	MakePartWidget = InArgs._MakePartWidget;
	HiddenSettings = InArgs._HiddenSettings;
	HiddenSections = InArgs._HiddenSections;
	BackDelegate = InArgs._OnBack;
	PageChangedDelegate = InArgs._OnPageChanged;
	SettingFocusedDelegate = InArgs._OnSettingFocused;
	SoundRuntimes.SetNum(6);
	PageFade.Snap(1.f);
	PageSlide.Snap(0.f);

	if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		SettingChangedHandle = Subsystem->OnSettingChangedNative().AddSP(this, &SMenuSettingsPanel::HandleSettingChanged);
		PagesChangedHandle = Subsystem->OnPagesChangedNative().AddSP(this, &SMenuSettingsPanel::RebuildAll);
		KeysChangedHandle = Subsystem->OnKeyBindingsChangedNative().AddSPLambda(this, [this]() { bRefreshQueued = true; });
	}
	if (UMenuInputSubsystem* Input = LocalPlayer.IsValid() ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr)
	{
		DeviceChangedHandle = Input->OnInputDeviceChangedNative().AddSPLambda(this, [this](EMenuInputDevice)
		{
			UpdateFooterPrompts();
			bRefreshQueued = true;
		});
	}
	AccessibilityHandle = MenuAccessibility::OnChanged().AddSPLambda(this, [this]()
	{
		// High contrast and reduce motion change how everything paints.
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
	});

	ComputeVisiblePages();
	CurrentPage = 0;
	for (int32 Index = 0; Index < VisiblePages.Num(); ++Index)
	{
		if (GetAllPages()[VisiblePages[Index]].Id == InArgs._InitialPage)
		{
			CurrentPage = Index;
		}
	}

	UpdateScrollBarStyle();
	// A material in the skin may animate: repaint every frame then.
	ForceVolatile(StyleHolder->Style.Skin.UsesMaterial());

	const FMenuStyleRef Holder = StyleHolder;
	const FMenuStyle& BuildStyle = StyleHolder->Style;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength_Lambda([Holder]() { return Holder->Style.BackgroundBlur; })
			.Visibility_Lambda([Holder]() { return Holder->Style.BackgroundBlur > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.BorderBackgroundColor_Lambda([Holder]() { return FSlateColor(Holder->Palette().Background); })
		]
		+ SOverlay::Slot()
		[
			SAssignNew(BackgroundImage, SMenuImage)
			.Image(StyleHolder->Style.BackgroundImage)
			.Visibility(EVisibility::HitTestInvisible)
		]
		+ SOverlay::Slot()
		.Padding(TAttribute<FMargin>::CreateLambda([Holder]() { return Holder->Style.ScreenPadding; }))
		[
			SNew(SVerticalBox)

			// Title and search.
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.HAlign(BuildStyle.TitleAlignment.GetValue())
				[
					SNew(STextBlock)
					.Text(TitleAttribute)
					.Font_Lambda([Holder]() { return Holder->Style.TitleFont; })
					.ColorAndOpacity_Lambda([Holder]() { return FSlateColor(Holder->Palette().Text); })
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BuildSearch()
				]
			]

			// Tabs, with the keys that change them on either side.
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 20.f, 0.f, 0.f)
			.HAlign(BuildStyle.TabsAlignment.GetValue())
			[
				SNew(SMenuPartBox, StyleHolder)
				.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.TabBar; })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 12.f, 0.f)
					[
						MakePrompt(Keys.PreviousTab, FText::GetEmpty(), FSimpleDelegate::CreateSP(this, &SMenuSettingsPanel::StepPage, -1), EMenuFooterAction::PreviousTab)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SAssignNew(TabsBox, SHorizontalBox)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(12.f, 0.f, 0.f, 0.f)
					[
						MakePrompt(Keys.NextTab, FText::GetEmpty(), FSimpleDelegate::CreateSP(this, &SMenuSettingsPanel::StepPage, 1), EMenuFooterAction::NextTab)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 12.f, 0.f, 0.f)
			[
				SNew(SBox)
				.HeightOverride(1.f)
				[
					SNew(SBorder)
					.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
					.BorderBackgroundColor_Lambda([Holder]() { return FSlateColor(Holder->Palette().Divider); })
				]
			]

			// The rows and the description, at most Content Max Width wide.
			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding(0.f, 16.f)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth_Lambda([Holder]() { return Holder->Style.ContentMaxWidth > 0.f ? FOptionalSize(Holder->Style.ContentMaxWidth) : FOptionalSize(); })
				.MaxDesiredWidth_Lambda([Holder]() { return Holder->Style.ContentMaxWidth > 0.f ? FOptionalSize(Holder->Style.ContentMaxWidth) : FOptionalSize(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
						.Padding(0.f)
						.ColorAndOpacity_Lambda([this]() { return FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(PageFade.Value, 0.f, 1.f)); })
						.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform> { return FSlateRenderTransform(FVector2f(PageSlide.Value, 0.f)); })
						[
							SAssignNew(ScrollBox, SScrollBox)
							.ScrollBarStyle(&ScrollBarStyle)
							+ SScrollBox::Slot()
							[
								SAssignNew(RowsBox, SVerticalBox)
							]
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(24.f, 0.f, 0.f, 0.f)
					[
						BuildDescription()
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BuildFooter()
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(DialogLayer, SOverlay)
		]
	];

	if (BuildStyle.ScrollBarThickness > 0.f)
	{
		ScrollBox->SetScrollBarThickness(FVector2D(BuildStyle.ScrollBarThickness, BuildStyle.ScrollBarThickness));
	}
	BuildTabs();
	BuildRows();
	UpdateFooterPrompts();
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SMenuSettingsPanel::HandleTick));
}

template <typename WidgetType>
WidgetType* SMenuSettingsPanel::MakePart(const TSoftClassPtr<WidgetType>& Class)
{
	if (Class.IsNull() || !MakePartWidget)
	{
		return nullptr;
	}
	UClass* Loaded = Class.LoadSynchronous();
	if (!Loaded || Loaded->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	WidgetType* Widget = Cast<WidgetType>(MakePartWidget(Loaded));
	if (Widget)
	{
		Widget->SetStyleHolder(StyleHolder);
		CustomParts.Add(Widget);
	}
	return Widget;
}

void SMenuSettingsPanel::UpdateScrollBarStyle()
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FMenuStylePalette& Palette = StyleHolder->Palette();
	ScrollBarStyle = FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("ScrollBar");

	// The thumb: the skin's image (a texture or material; SVGs aren't brushes), else the palette's Track colour, rounded.
	const FMenuPartSkin& Thumb = Style.Skin.ScrollThumb;
	auto ThumbBrush = [&Thumb](bool bHovered, bool bPressed, const FLinearColor& Fallback) -> FSlateBrush
	{
		const FWidgetEffectImage& Image = Thumb.ForState(false, bPressed, false, false, bHovered);
		if (Image.ImageSource == EWidgetImageSource::Texture && Image.IsSet())
		{
			FSlateBrush Brush = Image.Image;
			Brush.TintColor = FSlateColor(Brush.TintColor.GetSpecifiedColor() * Thumb.Tint * (Thumb.bTintWithPalette ? Fallback : FLinearColor::White));
			return Brush;
		}
		return FSlateRoundedBoxBrush(Fallback, 4.f);
	};
	const FLinearColor Rest = Palette.Track;
	FLinearColor Hovered = Palette.Track;
	Hovered.A = FMath::Min(1.f, Hovered.A * 2.f);
	ScrollBarStyle.SetNormalThumbImage(ThumbBrush(false, false, Rest));
	ScrollBarStyle.SetHoveredThumbImage(ThumbBrush(true, false, Hovered));
	ScrollBarStyle.SetDraggedThumbImage(ThumbBrush(false, true, Palette.Accent));

	const FMenuPartSkin& Track = Style.Skin.ScrollTrack;
	if (Track.Normal.ImageSource == EWidgetImageSource::Texture && Track.Normal.IsSet())
	{
		FSlateBrush Brush = Track.Normal.Image;
		Brush.TintColor = FSlateColor(Brush.TintColor.GetSpecifiedColor() * Track.Tint);
		ScrollBarStyle.SetVerticalBackgroundImage(Brush);
	}
	else
	{
		ScrollBarStyle.SetVerticalBackgroundImage(FSlateNoResource());
	}
	ScrollBarStyle.SetVerticalTopSlotImage(FSlateNoResource());
	ScrollBarStyle.SetVerticalBottomSlotImage(FSlateNoResource());
}

void SMenuSettingsPanel::NotifyPartsStyleChanged()
{
	CustomParts.RemoveAll([](const TWeakObjectPtr<UMenuPartWidget>& Part) { return !Part.IsValid(); });
	for (const TWeakObjectPtr<UMenuPartWidget>& Part : CustomParts)
	{
		Part->NotifyStyleChanged();
	}
}

TSharedRef<SWidget> SMenuSettingsPanel::BuildSearch()
{
	using namespace MenuPanelLocal;
	const FMenuStyleRef Holder = StyleHolder;
	const FText Hint = OrDefault(Texts.Search, LOCTEXT("Search", "Search settings"));
	TSharedPtr<SWidget> Content;
	if (UMenuSearchWidget* Custom = MakePart(StyleHolder->Style.Widgets.Search))
	{
		CustomSearch = Custom;
		// Weak: the part widget (a UObject) can outlive this screen for a moment.
		const TWeakPtr<SMenuSettingsPanel> WeakPanel = SharedThis(this);
		Custom->AssignSearch(Hint, [WeakPanel](const FText& Text)
		{
			if (const TSharedPtr<SMenuSettingsPanel> Pinned = WeakPanel.Pin())
			{
				Pinned->SetSearchText(Text);
			}
		});
		Content = Custom->TakeWidget();
	}
	else
	{
		Content = SNew(SBox)
			.WidthOverride(300.f)
			[
				SNew(SMenuPartBox, StyleHolder)
				.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.SearchBox; })
				.BuiltIn_Lambda([](const FMenuStyle& PartStyle, const FMenuPartState&)
				{
					FMenuBuiltInBox Box;
					Box.Fill = PartStyle.GetPalette().Row;
					Box.Radius = 18.f;
					return Box;
				})
				.IsFocused_Lambda([this]() { return IsTyping(); })
				.Padding(FMargin(16.f, 8.f))
				[
					SAssignNew(SearchWidget, SEditableText)
					.HintText(Hint)
					.Font_Lambda([Holder]() { return Holder->Style.LabelFont; })
					.ColorAndOpacity_Lambda([Holder]() { return FSlateColor(Holder->Palette().Text); })
					.OnTextChanged_Lambda([this](const FText& Text) { SetSearchText(Text); })
				]
			];
	}
	SearchArea = SNew(SBox)
		.Visibility_Lambda([Holder]() { return Holder->Style.bShowSearch ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			Content.ToSharedRef()
		];
	return SearchArea.ToSharedRef();
}

SMenuSettingsPanel::~SMenuSettingsPanel()
{
	if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		Subsystem->OnSettingChangedNative().Remove(SettingChangedHandle);
		Subsystem->OnPagesChangedNative().Remove(PagesChangedHandle);
		Subsystem->OnKeyBindingsChangedNative().Remove(KeysChangedHandle);
	}
	if (UMenuInputSubsystem* Input = LocalPlayer.IsValid() ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr)
	{
		Input->OnInputDeviceChangedNative().Remove(DeviceChangedHandle);
	}
	MenuAccessibility::OnChanged().Remove(AccessibilityHandle);
}

TSharedRef<SWidget> SMenuSettingsPanel::BuildDescription()
{
	const FMenuStyleRef Holder = StyleHolder;
	auto Line = [Holder](TFunction<FText()> Get, bool bTitle, bool bWarning, bool bMuted)
	{
		return SNew(STextBlock)
			.Text_Lambda([Get]() { return Get(); })
			.AutoWrapText(true)
			.Font_Lambda([Holder, bTitle]() { return bTitle ? Holder->Style.DescriptionTitleFont : Holder->Style.DescriptionFont; })
			.ColorAndOpacity_Lambda([Holder, bWarning, bMuted]()
			{
				const FMenuStylePalette& Palette = Holder->Palette();
				return FSlateColor(bWarning ? Palette.Warning : bMuted ? Palette.MutedText : Palette.Text);
			})
			.Visibility_Lambda([Get]() { return Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; });
	};

	TSharedRef<SBox> Frame = SNew(SBox)
		.WidthOverride_Lambda([Holder]() { return FOptionalSize(Holder->Style.DescriptionWidth); })
		.Visibility_Lambda([this, Holder]()
		{
			const bool bWide = GetTickSpaceGeometry().GetLocalSize().X <= 0.0 || GetTickSpaceGeometry().GetLocalSize().X >= 1100.0;
			return Holder->Style.bShowDescriptionPanel && bWide && DescribedSetting.IsValid() ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden;
		});
	if (UMenuDescriptionWidget* Custom = MakePart(StyleHolder->Style.Widgets.Description))
	{
		CustomDescription = Custom;
		Frame->SetContent(Custom->TakeWidget());
		return Frame;
	}

	Frame->SetContent(
			SNew(SMenuPartBox, StyleHolder)
			.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.DescriptionPanel; })
			.BuiltIn_Lambda([](const FMenuStyle& PartStyle, const FMenuPartState&)
			{
				FMenuBuiltInBox Box;
				Box.Fill = PartStyle.GetPalette().Surface;
				Box.Radius = 12.f;
				return Box;
			})
			.Padding(FMargin(24.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					Line([this]() { return DescTitle; }, true, false, false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
				[
					Line([this]() { return DescValueLine; }, false, false, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f).HAlign(HAlign_Left)
				[
					SAssignNew(PreviewImage, SMenuImage)
					.ImageSize(FVector2D(FMath::Max(64.f, Holder->Style.DescriptionWidth - 48.f), 0.f))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					Line([this]() { return DescBody; }, false, false, false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					Line([this]() { return DescOption; }, false, false, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					Line([this]() { return DescWarning; }, false, true, false)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
				[
					Line([this]() { return DescNote; }, false, false, true)
				]
			]);
	return Frame;
}

TSharedRef<SWidget> SMenuSettingsPanel::MakePrompt(const TArray<FKey>& PromptKeys, const FText& Label, FSimpleDelegate OnClicked, EMenuFooterAction Action,
	TAttribute<bool> IsPromptEnabled)
{
	const FMenuStyleRef Holder = StyleHolder;
	if (UMenuFooterPromptWidget* Custom = MakePart(StyleHolder->Style.Widgets.FooterPrompt))
	{
		TSharedRef<FCustomPartEntry> Entry = MakeShared<FCustomPartEntry>();
		Entry->Widget = Custom;
		Entry->Keys = PromptKeys;
		Entry->IsEnabled = IsPromptEnabled;
		CustomPrompts.Add(Entry);
		Custom->AssignPrompt(Action, Label, [OnClicked]() { OnClicked.ExecuteIfBound(); });
		const TWeakObjectPtr<UMenuFooterPromptWidget> WeakPrompt = Custom;
		return SNew(SMenuCustomPart)
			.OnClicked_Lambda([WeakPrompt]()
			{
				if (UMenuFooterPromptWidget* Pinned = WeakPrompt.Get())
				{
					Pinned->Press();
				}
			})
			.OnPointerState_Lambda([Entry, WeakPrompt](bool bHovered, bool bPressed)
			{
				Entry->bHovered = bHovered;
				Entry->bPressed = bPressed;
				if (UMenuFooterPromptWidget* Pinned = WeakPrompt.Get())
				{
					Pinned->SetPromptState(Entry->IsEnabled.Get(true), bHovered, bPressed);
				}
			})
			[
				Custom->TakeWidget()
			];
	}

	const bool bUppercase = StyleHolder->Style.bUppercaseButtons;
	TSharedPtr<SMenuKeyPrompt> Prompt;
	SAssignNew(Prompt, SMenuKeyPrompt)
		.CapStyle(StyleHolder->Style.KeyCap)
		.LabelFont(StyleHolder->Style.HintFont)
		.Label(bUppercase ? Label.ToUpper() : Label)
		.LabelColor_Lambda([Holder]() { return FSlateColor(Holder->Palette().MutedText); })
		.OnClicked(FOnMenuKeyPromptClicked::CreateLambda([OnClicked]() { OnClicked.ExecuteIfBound(); }));
	Prompt->SetEnabled(IsPromptEnabled);
	Prompts.Add({ Prompt, PromptKeys });
	return SNew(SMenuPartBox, StyleHolder)
		.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.FooterPrompt; })
		.Visibility_Lambda([Prompt]() { return Prompt->GetVisibility(); })
		[
			Prompt.ToSharedRef()
		];
}

TSharedRef<SWidget> SMenuSettingsPanel::BuildFooter()
{
	using namespace MenuPanelLocal;
	TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
	Footer->AddSlot().FillWidth(1.f);

	if (bShowReset)
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)
		[
			MakePrompt(Keys.Reset, OrDefault(Texts.Reset, LOCTEXT("Reset", "Reset to Defaults")), FSimpleDelegate::CreateSP(this, &SMenuSettingsPanel::ResetCurrentPage), EMenuFooterAction::Reset)
		];
	}
	const auto RevertVisibility = [this]()
	{
		const UMenuSettingsSubsystem* Subsystem = Settings.Get();
		return Subsystem && Subsystem->IsEditing() && Subsystem->HasChangesSinceEditingBegan() ? EVisibility::Visible : EVisibility::Collapsed;
	};
	const FText RevertLabel = OrDefault(Texts.Revert, LOCTEXT("Revert", "Revert Changes"));
	if (!StyleHolder->Style.Widgets.FooterPrompt.IsNull())
	{
		// Your prompt widget for Revert too (no key: it is clicked).
		const TSharedRef<SWidget> Revert = MakePrompt(TArray<FKey>(), RevertLabel, FSimpleDelegate::CreateSP(this, &SMenuSettingsPanel::RevertChanges), EMenuFooterAction::Revert);
		Revert->SetVisibility(TAttribute<EVisibility>::CreateLambda(RevertVisibility));
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)[Revert];
	}
	else
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)
		[
			SNew(SMenuButton, StyleHolder)
			.Kind(EMenuButtonKind::Ghost)
			.IsFocusable(false)
			.Text(RevertLabel)
			.OnClicked(FOnMenuControlClicked::CreateSP(this, &SMenuSettingsPanel::RevertChanges))
			.Visibility_Lambda(RevertVisibility)
		];
	}
	if (bApplyButton)
	{
		const TSharedRef<SWidget> Apply = MakePrompt(Keys.Apply, OrDefault(Texts.Apply, LOCTEXT("Apply", "Apply")), FSimpleDelegate::CreateSP(this, &SMenuSettingsPanel::ApplyChanges),
			EMenuFooterAction::Apply, TAttribute<bool>::CreateLambda([this]() { return Settings.IsValid() && Settings->HasPendingChanges(); }));
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)[Apply];
	}
	if (bShowBack)
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(20.f, 0.f, 0.f, 0.f)
		[
			MakePrompt(Keys.Back, OrDefault(Texts.Back, LOCTEXT("Back", "Back")), FSimpleDelegate::CreateLambda([this]() { RequestBack(); }), EMenuFooterAction::Back)
		];
	}
	return SNew(SMenuPartBox, StyleHolder)
		.Part_Lambda([](const FMenuStyle& PartStyle) { return &PartStyle.Skin.Footer; })
		.Visibility_Lambda([Holder = StyleHolder]() { return Holder->Style.bShowFooterHints ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
		[
			Footer
		];
}

// =====================================================================================================
//  Pages and rows
// =====================================================================================================

const TArray<FMenuSettingsPage>& SMenuSettingsPanel::GetAllPages() const
{
	if (const UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		return Subsystem->GetPages();
	}
	return PreviewPages;
}

void SMenuSettingsPanel::ComputeVisiblePages()
{
	VisiblePages.Reset();
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	auto Usable = [](const FMenuSettingsPage& Page) { return MenuSettingValues::IsCurrentPlatformIn(Page.Platforms); };
	if (PageFilter.Num() > 0)
	{
		for (const FName& Wanted : PageFilter)
		{
			const int32 Found = Pages.IndexOfByPredicate([&Wanted](const FMenuSettingsPage& Page) { return Page.Id == Wanted; });
			if (Found != INDEX_NONE && Usable(Pages[Found]))
			{
				VisiblePages.Add(Found);
			}
		}
	}
	else
	{
		for (int32 Index = 0; Index < Pages.Num(); ++Index)
		{
			if (Usable(Pages[Index]))
			{
				VisiblePages.Add(Index);
			}
		}
	}
	CurrentPage = FMath::Clamp(CurrentPage, 0, FMath::Max(0, VisiblePages.Num() - 1));
}

bool SMenuSettingsPanel::AreConditionsMetNow(const TArray<FMenuSettingCondition>& Conditions) const
{
	if (const UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		return Subsystem->TestConditions(Conditions);
	}
	// The designer: the preview values (system values pass).
	for (const FMenuSettingCondition& Condition : Conditions)
	{
		if (Condition.SettingId.ToString().StartsWith(TEXT("@")))
		{
			continue;
		}
		FString Value;
		if (const FString* Found = PreviewValues.Find(Condition.SettingId))
		{
			Value = *Found;
		}
		else
		{
			for (const FMenuSettingsPage& Page : PreviewPages)
			{
				for (const FMenuSettingsSection& Section : Page.Sections)
				{
					for (const UMenuSetting* Candidate : Section.Settings)
					{
						if (Candidate && Candidate->Id == Condition.SettingId)
						{
							Value = Candidate->GetDefaultValue(GetContext());
						}
					}
				}
			}
		}
		if (!Condition.Test(Value))
		{
			return false;
		}
	}
	return true;
}

bool SMenuSettingsPanel::IsSettingShownNow(const UMenuSetting& Setting) const
{
	if (const UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		return Subsystem->IsSettingShown(Setting.Id) || (Setting.GetKind() == EMenuSettingKind::KeyBinding && Subsystem->FindSetting(Setting.Id) == nullptr);
	}
	FText Reason;
	if (!MenuSettingValues::IsCurrentPlatformIn(Setting.Platforms))
	{
		return false;
	}
	if (!Setting.IsAvailable(GetContext(), Reason) && Setting.WhenUnavailable == EMenuUnavailableBehavior::Hide)
	{
		return false;
	}
	return AreConditionsMetNow(Setting.ShownWhen);
}

void SMenuSettingsPanel::BuildTabs()
{
	if (!TabsBox.IsValid())
	{
		return;
	}
	TabsBox->ClearChildren();
	CustomTabs.Reset();
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	for (int32 Index = 0; Index < VisiblePages.Num(); ++Index)
	{
		const FMenuSettingsPage& Page = Pages[VisiblePages[Index]];
		const FText PageName = Page.DisplayName.IsEmpty() ? FText::FromName(Page.Id) : Page.DisplayName;
		TSharedPtr<SWidget> TabWidget;
		if (UMenuTabWidget* Custom = MakePart(StyleHolder->Style.Widgets.Tab))
		{
			TSharedRef<FCustomPartEntry> Entry = MakeShared<FCustomPartEntry>();
			Entry->Widget = Custom;
			Entry->TabIndex = Index;
			CustomTabs.Add(Entry);
			const TWeakPtr<SMenuSettingsPanel> WeakPanel = SharedThis(this);
			Custom->AssignTab(Page.Id, PageName, Page.Icon, Index, [WeakPanel, Index]()
			{
				if (const TSharedPtr<SMenuSettingsPanel> Pinned = WeakPanel.Pin())
				{
					Pinned->SetPage(Index);
				}
			});
			Custom->SetTabState(SearchText.IsEmpty() && CurrentPage == Index, false, false);
			const TWeakObjectPtr<UMenuTabWidget> WeakTab = Custom;
			TabWidget = SNew(SMenuCustomPart)
				.OnClicked_Lambda([this, Index]() { SetPage(Index); })
				.OnPointerState_Lambda([this, Entry, WeakTab](bool bHovered, bool bPressed)
				{
					Entry->bHovered = bHovered;
					Entry->bPressed = bPressed;
					if (UMenuTabWidget* Pinned = WeakTab.Get())
					{
						Pinned->SetTabState(SearchText.IsEmpty() && CurrentPage == Entry->TabIndex, bHovered, bPressed);
					}
				})
				[
					Custom->TakeWidget()
				];
		}
		else
		{
			TabWidget = SNew(SMenuButton, StyleHolder)
				.Kind(EMenuButtonKind::Tab)
				.IsFocusable(false)
				.Text(PageName)
				.Icon(StyleHolder->Style.bShowTabIcons ? Page.Icon : FMenuImage())
				.IconOnly(StyleHolder->Style.bTabIconsOnly)
				.IsSelected_Lambda([this, Index]() { return SearchText.IsEmpty() && CurrentPage == Index; })
				.OnClicked_Lambda([this, Index]() { SetPage(Index); });
		}
		TabsBox->AddSlot()
			.AutoWidth()
			.Padding(Index > 0 ? StyleHolder->Style.TabSpacing : 0.f, 0.f, 0.f, 0.f)
			[
				TabWidget.ToSharedRef()
			];
	}
}

void SMenuSettingsPanel::AddHeader(const FText& Text, bool bMinor)
{
	const FMenuStyleRef Holder = StyleHolder;
	const bool bFirst = RowsBox->NumSlots() == 0;
	TSharedPtr<SWidget> Heading;
	if (UMenuHeadingWidget* Custom = MakePart(StyleHolder->Style.Widgets.Heading))
	{
		Custom->AssignHeading(Text, bMinor);
		Heading = Custom->TakeWidget();
	}
	else
	{
		const FText Shown = !bMinor && StyleHolder->Style.bUppercaseSections ? Text.ToUpper() : Text;
		Heading = SNew(SMenuPartBox, StyleHolder)
			.Part_Lambda([bMinor](const FMenuStyle& PartStyle) { return bMinor ? &PartStyle.Skin.CategoryHeading : &PartStyle.Skin.SectionHeading; })
			[
				SNew(STextBlock)
				.Text(Shown)
				.Font_Lambda([Holder, bMinor]() { return bMinor ? Holder->Style.HintFont : Holder->Style.SectionFont; })
				.ColorAndOpacity_Lambda([Holder, bMinor]() { return FSlateColor(bMinor ? Holder->Palette().MutedText : Holder->Palette().Accent); })
			];
	}
	RowsBox->AddSlot()
		.AutoHeight()
		.Padding(4.f, bFirst ? 0.f : (bMinor ? 10.f : Holder->Style.SectionSpacing), 4.f, 8.f)
		[
			Heading.ToSharedRef()
		];
}

EMenuChoiceDisplay SMenuSettingsPanel::ResolveChoiceDisplay(const UMenuSetting& Setting) const
{
	const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(&Setting);
	if (!Choice)
	{
		return EMenuChoiceDisplay::Selector;
	}
	TArray<FMenuSettingOption> Options;
	if (const UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		Options = Subsystem->GetSettingOptions(Setting.Id);
	}
	else
	{
		Choice->GetOptions(GetContext(), Options, GetPreviewValue(Setting));
	}
	switch (Choice->Display)
	{
	case EMenuChoiceDisplay::Dropdown:
		return EMenuChoiceDisplay::Dropdown;
	case EMenuChoiceDisplay::Buttons:
		return Options.Num() <= 4 ? EMenuChoiceDisplay::Buttons : EMenuChoiceDisplay::Selector;
	case EMenuChoiceDisplay::Auto:
		return Options.Num() > StyleHolder->Style.SelectorMaxOptions ? EMenuChoiceDisplay::Dropdown : EMenuChoiceDisplay::Selector;
	default:
		return EMenuChoiceDisplay::Selector;
	}
}

bool SMenuSettingsPanel::IsHiddenHere(const UMenuSetting& Setting) const
{
	const FString Id = Setting.Id.ToString();
	return HiddenSettings.ContainsByPredicate([&Id](const FString& Pattern)
	{
		return Pattern.Contains(TEXT("*")) || Pattern.Contains(TEXT("?")) ? Id.MatchesWildcard(Pattern) : Id.Equals(Pattern, ESearchCase::IgnoreCase);
	});
}

bool SMenuSettingsPanel::IsSectionHiddenHere(const FMenuSettingsSection& Section) const
{
	const FString Title = Section.Title.ToString();
	return HiddenSections.ContainsByPredicate([&Section, &Title](const FName& Hidden)
	{
		return Hidden == Section.GetMergeId() || Hidden.ToString().Equals(Title, ESearchCase::IgnoreCase);
	});
}

void SMenuSettingsPanel::AddRow(UMenuSetting* Setting, const FMenuSettingsPage& Page, const FMenuSettingsSection& Section)
{
	const float Spacing = StyleHolder->Style.RowSpacing;
	FMenuCustomRow Custom;
	if (MakeCustomRow && Setting)
	{
		FMenuCustomRowRequest Request;
		Request.Setting = Setting;
		Request.PageId = Page.Id;
		Request.SectionId = Section.GetMergeId();
		Request.SectionTitle = Section.Title;
		Request.Display = ResolveChoiceDisplay(*Setting);
		Custom = MakeCustomRow(Request);
	}
	// Custom or not, the row is one of the screen's rows: focus, the description, narration, sounds and footer actions find it.
	TSharedRef<SMenuSettingRow> Row = SNew(SMenuSettingRow, this, Setting)
		.CustomRow(Custom.Widget.IsValid() && Custom.bWholeRow ? Custom.Widget : TSharedPtr<SWidget>())
		.CustomControl(Custom.Widget.IsValid() && !Custom.bWholeRow ? Custom.Widget : TSharedPtr<SWidget>())
		.CustomWidget(Custom.Object);
	Rows.Add(Row);
	RowsBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, Spacing)[Row];
}

TArray<FName> SMenuSettingsPanel::CollectShownIds() const
{
	TArray<FName> Ids;
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	const FString Search = SearchText.ToString().TrimStartAndEnd();
	for (int32 PageIndex = 0; PageIndex < VisiblePages.Num(); ++PageIndex)
	{
		if (Search.IsEmpty() && PageIndex != CurrentPage)
		{
			continue;
		}
		const FMenuSettingsPage& Page = Pages[VisiblePages[PageIndex]];
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			if (IsSectionHiddenHere(Section) || !AreConditionsMetNow(Section.ShownWhen))
			{
				continue;
			}
			for (const UMenuSetting* Setting : Section.Settings)
			{
				if (Setting && !IsHiddenHere(*Setting) && IsSettingShownNow(*Setting) && (Search.IsEmpty() || Setting->MatchesSearch(Search)))
				{
					Ids.Add(Setting->Id);
				}
			}
		}
	}
	return Ids;
}

void SMenuSettingsPanel::BuildRows()
{
	if (!RowsBox.IsValid())
	{
		return;
	}
	const FName Keep = FocusedRow.IsValid() ? FocusedRow.Pin()->GetSettingId() : NAME_None;
	const bool bHadFocus = HasFocusedDescendants() || HasAnyUserFocus().IsSet();
	RowsBox->ClearChildren();
	Rows.Reset();

	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	const FString Search = SearchText.ToString().TrimStartAndEnd();
	for (int32 PageIndex = 0; PageIndex < VisiblePages.Num(); ++PageIndex)
	{
		if (Search.IsEmpty() && PageIndex != CurrentPage)
		{
			continue;
		}
		const FMenuSettingsPage& Page = Pages[VisiblePages[PageIndex]];
		bool bPageHeader = false;
		for (const FMenuSettingsSection& Section : Page.Sections)
		{
			if (IsSectionHiddenHere(Section) || !AreConditionsMetNow(Section.ShownWhen))
			{
				continue;
			}
			TArray<UMenuSetting*> Shown;
			for (UMenuSetting* Setting : Section.Settings)
			{
				if (Setting && !IsHiddenHere(*Setting) && IsSettingShownNow(*Setting) && (Search.IsEmpty() || Setting->MatchesSearch(Search)))
				{
					Shown.Add(Setting);
				}
			}
			if (Shown.Num() == 0)
			{
				continue;
			}
			if (!Search.IsEmpty())
			{
				if (!bPageHeader)
				{
					AddHeader(Page.DisplayName.IsEmpty() ? FText::FromName(Page.Id) : Page.DisplayName, false);
					bPageHeader = true;
				}
			}
			else if (!Section.Title.IsEmpty())
			{
				AddHeader(Section.Title, false);
			}
			FText Category;
			for (UMenuSetting* Setting : Shown)
			{
				if (const UMenuSettingKeyBinding* Binding = Cast<UMenuSettingKeyBinding>(Setting))
				{
					if (!Binding->DisplayCategory.IsEmpty() && !Binding->DisplayCategory.EqualTo(Category))
					{
						Category = Binding->DisplayCategory;
						AddHeader(Category, true);
					}
				}
				AddRow(Setting, Page, Section);
			}
		}
	}

	if (RowsBox->NumSlots() == 0)
	{
		const FMenuStyleRef Holder = StyleHolder;
		RowsBox->AddSlot()
			.AutoHeight()
			.Padding(4.f, 24.f)
			[
				SNew(STextBlock)
				.Text(Search.IsEmpty() ? LOCTEXT("Empty", "Nothing to set here yet.") : FText::Format(LOCTEXT("NoMatch", "No settings match \"{0}\"."), SearchText))
				.Font_Lambda([Holder]() { return Holder->Style.LabelFont; })
				.ColorAndOpacity_Lambda([Holder]() { return FSlateColor(Holder->Palette().MutedText); })
			];
	}
	ShownIds = CollectShownIds();

	if (bHadFocus && !IsTyping())
	{
		if (!Keep.IsNone() && ShownIds.Contains(Keep))
		{
			FocusSetting(Keep);
		}
		else
		{
			FocusFirstRow();
		}
	}
}

void SMenuSettingsPanel::RefreshRows()
{
	if (CollectShownIds() != ShownIds)
	{
		// What is shown changed (HDR turned on shows HDR Brightness): the page again, keeping focus.
		BuildRows();
	}
	else
	{
		for (const TSharedPtr<SMenuSettingRow>& Row : Rows)
		{
			Row->Refresh();
		}
	}
	UpdateDescription(DescribedSetting.Get());
}

void SMenuSettingsPanel::RebuildAll()
{
	const FName Page = GetCurrentPageId();
	ComputeVisiblePages();
	for (int32 Index = 0; Index < VisiblePages.Num(); ++Index)
	{
		if (GetAllPages()[VisiblePages[Index]].Id == Page)
		{
			CurrentPage = Index;
		}
	}
	BuildTabs();
	BuildRows();
	if (BackgroundImage.IsValid())
	{
		BackgroundImage->SetImage(StyleHolder->Style.BackgroundImage);
	}
	UpdateScrollBarStyle();
	ForceVolatile(StyleHolder->Style.Skin.UsesMaterial());
	if (ScrollBox.IsValid() && StyleHolder->Style.ScrollBarThickness > 0.f)
	{
		ScrollBox->SetScrollBarThickness(FVector2D(StyleHolder->Style.ScrollBarThickness, StyleHolder->Style.ScrollBarThickness));
	}
	NotifyPartsStyleChanged();
	UpdateFooterPrompts();
}

void SMenuSettingsPanel::HandleStyleChanged()
{
	RebuildAll();
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
}

void SMenuSettingsPanel::HandleSettingChanged(FName SettingId, const FString& Value)
{
	// Many settings can report one change (a preset sets every quality): refresh once, next frame.
	bRefreshQueued = true;
}

FName SMenuSettingsPanel::GetCurrentPageId() const
{
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	return VisiblePages.IsValidIndex(CurrentPage) && Pages.IsValidIndex(VisiblePages[CurrentPage]) ? Pages[VisiblePages[CurrentPage]].Id : NAME_None;
}

void SMenuSettingsPanel::SetPage(int32 PageIndex, bool bAnimate)
{
	if (!VisiblePages.IsValidIndex(PageIndex) || (PageIndex == CurrentPage && SearchText.IsEmpty()))
	{
		return;
	}
	const int32 Direction = PageIndex > CurrentPage ? 1 : -1;
	CurrentPage = PageIndex;
	if (!SearchText.IsEmpty())
	{
		SearchText = FText::GetEmpty();
		if (SearchWidget.IsValid())
		{
			SearchWidget->SetText(FText::GetEmpty());
		}
		if (UMenuSearchWidget* Custom = CustomSearch.Get())
		{
			Custom->ShowSearchText(FText::GetEmpty());
		}
	}
	FocusedRow.Reset();
	BuildRows();
	if (ScrollBox.IsValid())
	{
		ScrollBox->SetScrollOffset(0.f);
	}
	PlaySound(EMenuSoundEvent::Tab);

	const FMenuStyle& Style = StyleHolder->Style;
	if (bAnimate && Style.PageTransitionSeconds > 0.f)
	{
		const bool bReduce = MenuAccessibility::IsReduceMotionEnabled();
		PageFade.Snap(0.f);
		PageFade.SetTarget(1.f, bReduce ? EWidgetMotion::Linear : Style.Motion, Style.PageTransitionSeconds);
		PageSlide.Snap(Style.bSlidePages && !bReduce ? 28.f * Direction : 0.f);
		PageSlide.SetTarget(0.f, Style.Motion == EWidgetMotion::Bouncy ? EWidgetMotion::Smooth : Style.Motion, Style.PageTransitionSeconds);
	}
	if (HasFocusedDescendants() || HasAnyUserFocus().IsSet())
	{
		FocusFirstRow();
	}
	const FName PageId = GetCurrentPageId();
	PageChangedDelegate.ExecuteIfBound(PageId);
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	if (MenuAccessibility::IsMenuNarrationEnabled() && VisiblePages.IsValidIndex(CurrentPage))
	{
		Announce(Pages[VisiblePages[CurrentPage]].DisplayName);
	}
}

bool SMenuSettingsPanel::SetPageById(FName PageId)
{
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	for (int32 Index = 0; Index < VisiblePages.Num(); ++Index)
	{
		if (Pages[VisiblePages[Index]].Id == PageId)
		{
			SetPage(Index);
			return true;
		}
	}
	return false;
}

void SMenuSettingsPanel::StepPage(int32 Direction)
{
	if (VisiblePages.Num() <= 1)
	{
		return;
	}
	SetPage((CurrentPage + Direction + VisiblePages.Num()) % VisiblePages.Num());
}

void SMenuSettingsPanel::FocusFirstRow()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	if (Rows.Num() > 0)
	{
		FSlateApplication::Get().SetUserFocus(GetUserIndex(), Rows[0], EFocusCause::SetDirectly);
	}
	else
	{
		FSlateApplication::Get().SetUserFocus(GetUserIndex(), SharedThis(this), EFocusCause::SetDirectly);
	}
}

void SMenuSettingsPanel::FocusSetting(FName SettingId)
{
	for (const TSharedPtr<SMenuSettingRow>& Row : Rows)
	{
		if (Row->GetSettingId() == SettingId && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetUserFocus(GetUserIndex(), Row, EFocusCause::SetDirectly);
			return;
		}
	}
}

void SMenuSettingsPanel::SetSearchText(const FText& Text)
{
	if (Text.EqualTo(SearchText))
	{
		return;
	}
	SearchText = Text;
	if (SearchWidget.IsValid() && !SearchWidget->GetText().EqualTo(Text))
	{
		SearchWidget->SetText(Text);
	}
	if (UMenuSearchWidget* Custom = CustomSearch.Get())
	{
		Custom->ShowSearchText(Text);
	}
	BuildRows();
}

// =====================================================================================================
//  Description and prompts
// =====================================================================================================

void SMenuSettingsPanel::UpdateDescription(UMenuSetting* Setting)
{
	DescribedSetting = Setting;
	DescTitle = DescValueLine = DescBody = DescOption = DescWarning = DescNote = FText::GetEmpty();
	if (!Setting)
	{
		if (PreviewImage.IsValid())
		{
			PreviewImage->SetImage(FMenuImage());
		}
		if (UMenuDescriptionWidget* Custom = CustomDescription.Get())
		{
			Custom->Describe(nullptr, FMenuSettingDescription());
		}
		return;
	}
	FMenuSettingDescription Described;
	const FMenuSettingContext Context = GetContext();
	UMenuSettingsSubsystem* Subsystem = Settings.Get();
	const FString Value = Subsystem ? Subsystem->GetSettingValue(Setting->Id) : GetPreviewValue(*Setting);
	DescTitle = Setting->GetLabel();
	DescBody = Setting->Description;

	FMenuImage Image = Setting->Preview;
	if (Setting->HoldsValue())
	{
		FText Shown = Setting->FormatValue(Value, Context);
		FText Default = Setting->FormatValue(Setting->GetDefaultValue(Context), Context);
		if (const UMenuSettingChoice* Choice = Cast<UMenuSettingChoice>(Setting))
		{
			TArray<FMenuSettingOption> Options;
			Choice->GetOptions(Context, Options, Value);
			const int32 Index = UMenuSettingChoice::FindOption(Options, Value);
			if (Options.IsValidIndex(Index))
			{
				Shown = Options[Index].GetLabel();
				DescOption = Options[Index].Description;
				if (Options[Index].Preview.HasImage())
				{
					Image = Options[Index].Preview;
				}
			}
		}
		DescValueLine = FText::Format(LOCTEXT("ValueLine", "Current: {0}    Default: {1}"), Shown, Default);
		Described.CurrentValue = Shown;
		Described.DefaultValue = Default;
	}

	FText Reason;
	const bool bEnabled = Subsystem ? Subsystem->IsSettingEnabled(Setting->Id, Reason) : Setting->IsAvailable(Context, Reason);
	TArray<FText> Notes;
	if (!bEnabled)
	{
		DescWarning = Reason;
	}
	if (Setting->bRequiresRestart)
	{
		Notes.Add(LOCTEXT("Restart", "Takes effect after the game restarts."));
	}
	if (Setting->bRequiresConfirmation)
	{
		Notes.Add(LOCTEXT("ConfirmNote", "You'll be asked to keep this change; it goes back by itself if you don't."));
	}
	if (Subsystem && Subsystem->IsRestartRequired())
	{
		Notes.Add(LOCTEXT("RestartPending", "Some changes need a restart of the game."));
	}
	DescNote = FText::Join(FText::FromString(TEXT("\n")), Notes);
	if (PreviewImage.IsValid())
	{
		PreviewImage->SetImage(Image);
	}
	if (UMenuDescriptionWidget* Custom = CustomDescription.Get())
	{
		Described.Title = DescTitle;
		Described.ValueLine = DescValueLine;
		Described.Body = DescBody;
		Described.OptionText = DescOption;
		Described.Warning = DescWarning;
		Described.Note = DescNote;
		Described.Preview = Image;
		Described.bEnabled = bEnabled;
		Described.bRequiresRestart = Setting->bRequiresRestart;
		Custom->Describe(Setting, Described);
	}
}

FKey SMenuSettingsPanel::KeyForDevice(const TArray<FKey>& PromptKeys) const
{
	const UMenuInputSubsystem* Input = LocalPlayer.IsValid() ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr;
	const bool bGamepad = Input && Input->IsUsingGamepad();
	for (const FKey& Key : PromptKeys)
	{
		if (Key.IsGamepadKey() == bGamepad)
		{
			return Key;
		}
	}
	return FKey();
}

void SMenuSettingsPanel::UpdateFooterPrompts()
{
	const UMenuInputSubsystem* Input = LocalPlayer.IsValid() ? LocalPlayer->GetSubsystem<UMenuInputSubsystem>() : nullptr;
	const bool bTouch = Input && Input->GetCurrentDevice() == EMenuInputDevice::Touch;
	for (const FPromptEntry& Entry : Prompts)
	{
		const FKey Key = KeyForDevice(Entry.Keys);
		FMenuKeyGlyph Glyph;
		if (Input)
		{
			Glyph = Input->GetKeyGlyph(Key);
		}
		else
		{
			Glyph.Key = Key;
			Glyph.Label = MenuInputUtils::GetKeyLabel(Key, EMenuGamepadFamily::Xbox);
		}
		Entry.Prompt->SetGlyph(Glyph);
		Entry.Prompt->SetCapStyle(StyleHolder->Style.KeyCap);
		// Touch shows the prompts as plain buttons: no key, still tappable.
		Entry.Prompt->SetVisibility(Key.IsValid() || bTouch ? EVisibility::Visible : EVisibility::Collapsed);
	}
	for (const TSharedRef<FCustomPartEntry>& Entry : CustomPrompts)
	{
		UMenuFooterPromptWidget* Custom = Cast<UMenuFooterPromptWidget>(Entry->Widget.Get());
		if (!Custom)
		{
			continue;
		}
		const FKey Key = KeyForDevice(Entry->Keys);
		FMenuKeyGlyph Glyph;
		if (Input)
		{
			Glyph = Input->GetKeyGlyph(Key);
		}
		else
		{
			Glyph.Key = Key;
			Glyph.Label = MenuInputUtils::GetKeyLabel(Key, EMenuGamepadFamily::Xbox);
		}
		Custom->SetGlyph(Glyph);
		// A prompt with keys hides while the device has none of them (touch keeps it, tappable); Revert has none and stays.
		if (Entry->Keys.Num() > 0)
		{
			Custom->SetVisibility(Key.IsValid() || bTouch ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

// =====================================================================================================
//  Input
// =====================================================================================================

bool SMenuSettingsPanel::MatchesKey(const TArray<FKey>& Candidates, const FKey& Key) const
{
	return Candidates.Contains(Key);
}

bool SMenuSettingsPanel::IsTyping() const
{
	if (SearchWidget.IsValid() && SearchWidget->HasAnyUserFocus().IsSet())
	{
		return true;
	}
	return CustomSearch.IsValid() && SearchArea.IsValid() && SearchArea->HasFocusedDescendants();
}

FReply SMenuSettingsPanel::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (const TSharedPtr<SMenuSettingRow> Last = FocusedRow.Pin())
	{
		return FReply::Handled().SetUserFocus(Last.ToSharedRef(), EFocusCause::SetDirectly);
	}
	if (Rows.Num() > 0)
	{
		return FReply::Handled().SetUserFocus(Rows[0].ToSharedRef(), EFocusCause::SetDirectly);
	}
	return FReply::Handled();
}

FReply SMenuSettingsPanel::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (ActiveDialog.IsValid() || bCapturingKey)
	{
		// The dialog (or the key wait) has the keys; let navigation through to it.
		return FReply::Unhandled();
	}
	const FKey Key = InKeyEvent.GetKey();
	if (IsTyping() && !Key.IsGamepadKey() && Key != EKeys::Escape && Key != EKeys::Enter && Key != EKeys::Down)
	{
		return FReply::Unhandled();
	}
	if (IsTyping() && (Key == EKeys::Enter || Key == EKeys::Down))
	{
		// Out of the search box into the results.
		FocusFirstRow();
		return FReply::Handled();
	}
	if (MatchesKey(Keys.PreviousTab, Key))
	{
		StepPage(-1);
		return FReply::Handled();
	}
	if (MatchesKey(Keys.NextTab, Key))
	{
		StepPage(1);
		return FReply::Handled();
	}
	if (bApplyButton && MatchesKey(Keys.Apply, Key))
	{
		ApplyChanges();
		return FReply::Handled();
	}
	if (bShowReset && MatchesKey(Keys.Reset, Key))
	{
		ResetCurrentPage();
		return FReply::Handled();
	}
	if (MatchesKey(Keys.Back, Key) || FSlateApplication::Get().GetNavigationActionFromKey(InKeyEvent) == EUINavigationAction::Back)
	{
		RequestBack();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

bool SMenuSettingsPanel::RequestBack()
{
	if (bCapturingKey)
	{
		if (const TSharedPtr<SMenuSettingRow> Row = FocusedRow.Pin())
		{
			Row->CancelCapture();
		}
		return true;
	}
	if (ActiveDialog.IsValid())
	{
		QueuedDialogChoice = ActiveDialogCancel;
		bDialogChoiceQueued = true;
		return true;
	}
	if (!SearchText.IsEmpty())
	{
		SetSearchText(FText::GetEmpty());
		FocusFirstRow();
		return true;
	}
	UMenuSettingsSubsystem* Subsystem = Settings.Get();
	if (Subsystem && bApplyButton && bConfirmUnsaved && Subsystem->HasPendingChanges())
	{
		const TArray<FMenuDialogButton> Buttons = {
			{ LOCTEXT("ApplyButton", "Apply"), EMenuButtonKind::Primary },
			{ LOCTEXT("DiscardButton", "Discard"), EMenuButtonKind::Secondary },
			{ LOCTEXT("CancelButton", "Cancel"), EMenuButtonKind::Ghost },
		};
		ShowDialog(LOCTEXT("UnsavedTitle", "Apply your changes?"), LOCTEXT("UnsavedBody", "Some settings you changed haven't been applied yet."), Buttons, 0, 2,
			[this](int32 Choice)
			{
				if (Choice == 0)
				{
					ApplyChanges();
					Back();
				}
				else if (Choice == 1)
				{
					RevertChanges();
					Back();
				}
			});
		return true;
	}
	Back();
	return false;
}

void SMenuSettingsPanel::Back()
{
	PlaySound(EMenuSoundEvent::Back);
	BackDelegate.ExecuteIfBound();
}

void SMenuSettingsPanel::ApplyChanges()
{
	if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		if (Subsystem->HasPendingChanges())
		{
			Subsystem->ApplySettings();
			PlaySound(EMenuSoundEvent::Accept);
		}
	}
}

void SMenuSettingsPanel::RevertChanges()
{
	if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
	{
		Subsystem->RevertSettings();
		PlaySound(EMenuSoundEvent::Back);
	}
	else
	{
		PreviewValues.Reset();
		RefreshRows();
	}
}

void SMenuSettingsPanel::ResetCurrentPage()
{
	const FName PageId = GetCurrentPageId();
	const TArray<FMenuSettingsPage>& Pages = GetAllPages();
	const FText PageName = VisiblePages.IsValidIndex(CurrentPage) ? Pages[VisiblePages[CurrentPage]].DisplayName : FText::GetEmpty();
	Confirm(LOCTEXT("ResetTitle", "Reset to defaults?"), FText::Format(LOCTEXT("ResetBody", "Every {0} setting goes back to its default."), PageName),
		[this, PageId]()
		{
			if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
			{
				Subsystem->ResetPage(PageId);
			}
			else
			{
				PreviewValues.Reset();
			}
			bRefreshQueued = true;
		});
}

// =====================================================================================================
//  Dialogs
// =====================================================================================================

void SMenuSettingsPanel::ShowDialog(const FText& DialogTitle, const TAttribute<FText>& Body, const TArray<FMenuDialogButton>& Buttons, int32 DefaultButton, int32 CancelButton, TFunction<void(int32)> Choice)
{
	if (ActiveDialog.IsValid())
	{
		CloseDialog();
	}
	if (FSlateApplication::IsInitialized())
	{
		FocusBeforeDialog = FSlateApplication::Get().GetUserFocusedWidget(GetUserIndex());
	}
	const TWeakPtr<SMenuSettingsPanel> WeakPanel = SharedThis(this);
	auto Queue = [WeakPanel](int32 Index)
	{
		// Handled next frame: the dialog isn't removed while its own button is being clicked.
		if (const TSharedPtr<SMenuSettingsPanel> Pinned = WeakPanel.Pin())
		{
			Pinned->QueuedDialogChoice = Index;
			Pinned->bDialogChoiceQueued = true;
		}
	};
	if (UMenuDialogWidget* Custom = MakePart(StyleHolder->Style.Widgets.Dialog))
	{
		TArray<FText> Labels;
		TArray<int32> Kinds;
		for (const FMenuDialogButton& Button : Buttons)
		{
			Labels.Add(Button.Text);
			Kinds.Add(Button.Kind == EMenuButtonKind::Primary ? 0 : Button.Kind == EMenuButtonKind::Ghost ? 2 : 1);
		}
		Custom->AssignDialog(DialogTitle, Body.Get(), Labels, Kinds, DefaultButton, CancelButton, Queue);
		CustomDialog = Custom;
		CustomDialogBody = Body;
		const TWeakObjectPtr<UMenuDialogWidget> WeakDialog = Custom;
		TSharedRef<SWidget> Wrapper = SNew(SMenuCustomPart)
			.IsFocusable(true)
			.OnBack_Lambda([WeakDialog]()
			{
				if (UMenuDialogWidget* Pinned = WeakDialog.Get())
				{
					Pinned->CancelDialog();
				}
			})
			[
				Custom->TakeWidget()
			];
		ActiveDialog = Wrapper;
		ActiveDialogChoice = MoveTemp(Choice);
		ActiveDialogCancel = CancelButton;
		DialogLayer->AddSlot()[Wrapper];
		UWidget* FocusTarget = Custom->GetDialogFocusWidget();
		const TSharedPtr<SWidget> FocusSlate = FocusTarget ? FocusTarget->GetCachedWidget() : TSharedPtr<SWidget>();
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().SetUserFocus(GetUserIndex(), FocusSlate.IsValid() ? FocusSlate : TSharedPtr<SWidget>(Wrapper), EFocusCause::SetDirectly);
		}
	}
	else
	{
		TSharedRef<SMenuDialog> Dialog = SNew(SMenuDialog, StyleHolder)
			.Title(DialogTitle)
			.Body(Body)
			.Buttons(Buttons)
			.DefaultButton(DefaultButton)
			.CancelButton(CancelButton)
			.OnChoice_Lambda(Queue);
		ActiveDialog = Dialog;
		ActiveDialogChoice = MoveTemp(Choice);
		ActiveDialogCancel = CancelButton;
		DialogLayer->AddSlot()[Dialog];
		Dialog->FocusDefault(GetUserIndex());
	}
	if (MenuAccessibility::IsMenuNarrationEnabled())
	{
		Announce(FText::Format(LOCTEXT("DialogNarration", "{0}. {1}"), DialogTitle, Body.Get()));
	}
}

void SMenuSettingsPanel::CloseDialog()
{
	if (ActiveDialog.IsValid())
	{
		DialogLayer->RemoveSlot(ActiveDialog.ToSharedRef());
		ActiveDialog.Reset();
	}
	CustomDialog.Reset();
	CustomDialogBody = TAttribute<FText>();
	ActiveDialogChoice = nullptr;
	ActiveDialogCancel = INDEX_NONE;
	bKeepDialogShown = false;
	if (FSlateApplication::IsInitialized())
	{
		if (const TSharedPtr<SWidget> Previous = FocusBeforeDialog.Pin())
		{
			FSlateApplication::Get().SetUserFocus(GetUserIndex(), Previous, EFocusCause::SetDirectly);
		}
		else
		{
			FocusFirstRow();
		}
	}
}

void SMenuSettingsPanel::Confirm(const FText& DialogTitle, const FText& Body, TFunction<void()> OnYes)
{
	const TArray<FMenuDialogButton> Buttons = {
		{ LOCTEXT("Yes", "Yes"), EMenuButtonKind::Primary },
		{ LOCTEXT("No", "No"), EMenuButtonKind::Secondary },
	};
	ShowDialog(DialogTitle, Body, Buttons, 1, 1, [OnYes](int32 Choice)
	{
		if (Choice == 0 && OnYes)
		{
			OnYes();
		}
	});
}

void SMenuSettingsPanel::ShowKeepChangesDialog()
{
	const TWeakObjectPtr<UMenuSettingsSubsystem> WeakSettings = Settings;
	const TAttribute<FText> Body = TAttribute<FText>::CreateLambda([WeakSettings]()
	{
		const float Left = WeakSettings.IsValid() ? WeakSettings->GetConfirmationTimeLeft() : 0.f;
		return FText::Format(LOCTEXT("KeepBody", "Your display changed. It goes back in {0} seconds unless you keep it."), FText::AsNumber(FMath::CeilToInt(Left)));
	});
	const TArray<FMenuDialogButton> Buttons = {
		{ LOCTEXT("Keep", "Keep Changes"), EMenuButtonKind::Primary },
		{ LOCTEXT("RevertDisplay", "Revert"), EMenuButtonKind::Secondary },
	};
	ShowDialog(LOCTEXT("KeepTitle", "Keep these display settings?"), Body, Buttons, 0, 1, [WeakSettings](int32 Choice)
	{
		if (UMenuSettingsSubsystem* Subsystem = WeakSettings.Get())
		{
			Subsystem->ConfirmSettings(Choice == 0);
		}
	});
	bKeepDialogShown = true;
}

bool SMenuSettingsPanel::OpenOptionList(const TSharedRef<SMenuSettingRow>& Row)
{
	const UMenuSetting* Setting = Row->GetSetting();
	if (!Setting || !DialogLayer.IsValid())
	{
		return false;
	}
	if (ActiveDialog.IsValid())
	{
		CloseDialog();
	}
	TArray<FText> Labels;
	for (const FMenuSettingOption& Option : Row->GetOptions())
	{
		Labels.Add(Option.GetLabel());
	}
	FocusBeforeDialog = Row;
	const TWeakPtr<SMenuSettingRow> WeakRow = Row;
	const TArray<FMenuSettingOption> Options = Row->GetOptions();
	const TWeakPtr<SMenuSettingsPanel> WeakPanel = SharedThis(this);
	auto Queue = [WeakPanel](int32 Index)
	{
		if (const TSharedPtr<SMenuSettingsPanel> Pinned = WeakPanel.Pin())
		{
			Pinned->QueuedDialogChoice = Index;
			Pinned->bDialogChoiceQueued = true;
		}
	};
	TSharedPtr<SWidget> ListWidget;
	TSharedPtr<SWidget> FocusTarget;
	TSharedPtr<SMenuOptionList> BuiltInList;
	if (UMenuOptionListWidget* Custom = MakePart(StyleHolder->Style.Widgets.OptionList))
	{
		Custom->AssignList(Setting->GetLabel(), Labels, Row->GetOptionIndex(), Queue);
		const TWeakObjectPtr<UMenuOptionListWidget> WeakList = Custom;
		ListWidget = SNew(SMenuCustomPart)
			.IsFocusable(true)
			.OnBack_Lambda([WeakList]()
			{
				if (UMenuOptionListWidget* Pinned = WeakList.Get())
				{
					Pinned->CancelList();
				}
			})
			[
				Custom->TakeWidget()
			];
		UWidget* FocusWidget = Custom->GetListFocusWidget();
		FocusTarget = FocusWidget ? FocusWidget->GetCachedWidget() : ListWidget;
	}
	else
	{
		BuiltInList = SNew(SMenuOptionList, StyleHolder)
			.Title(Setting->GetLabel())
			.Options(Labels)
			.Current(Row->GetOptionIndex())
			.OnPicked_Lambda(Queue);
		ListWidget = BuiltInList;
	}
	ActiveDialog = ListWidget;
	ActiveDialogCancel = INDEX_NONE;
	ActiveDialogChoice = [WeakRow, Options](int32 Index)
	{
		const TSharedPtr<SMenuSettingRow> Pinned = WeakRow.Pin();
		if (Pinned.IsValid() && Options.IsValidIndex(Index))
		{
			Pinned->ChangeValue(Options[Index].Value);
		}
	};
	DialogLayer->AddSlot()[ListWidget.ToSharedRef()];
	if (BuiltInList.IsValid())
	{
		BuiltInList->FocusCurrent(GetUserIndex());
	}
	else if (FocusTarget.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetUserFocus(GetUserIndex(), FocusTarget, EFocusCause::SetDirectly);
	}
	return true;
}

// =====================================================================================================
//  Tick
// =====================================================================================================

EActiveTimerReturnType SMenuSettingsPanel::HandleTick(double InCurrentTime, float InDeltaTime)
{
	if (bDialogChoiceQueued)
	{
		bDialogChoiceQueued = false;
		const TFunction<void(int32)> Choice = ActiveDialogChoice;
		const int32 Index = QueuedDialogChoice;
		const bool bWasKeep = bKeepDialogShown;
		CloseDialog();
		if (Choice)
		{
			Choice(Index);
		}
		if (bWasKeep)
		{
			bKeepDialogShown = false;
		}
	}
	if (bRefreshQueued)
	{
		bRefreshQueued = false;
		RefreshRows();
	}

	// A display change waits to be kept: the countdown dialog, closed when the subsystem stops waiting (kept, reverted, timed out).
	const UMenuSettingsSubsystem* Subsystem = Settings.Get();
	const bool bAwaiting = Subsystem && Subsystem->IsAwaitingConfirmation();
	if (bAwaiting && !bKeepDialogShown && !ActiveDialog.IsValid())
	{
		ShowKeepChangesDialog();
	}
	else if (!bAwaiting && bKeepDialogShown)
	{
		CloseDialog();
	}

	// Custom parts follow what they show: the current tab, a prompt's enabled state, a dialog's countdown.
	for (const TSharedRef<FCustomPartEntry>& Entry : CustomTabs)
	{
		if (UMenuTabWidget* Tab = Cast<UMenuTabWidget>(Entry->Widget.Get()))
		{
			Tab->SetTabState(SearchText.IsEmpty() && CurrentPage == Entry->TabIndex, Entry->bHovered, Entry->bPressed);
		}
	}
	for (const TSharedRef<FCustomPartEntry>& Entry : CustomPrompts)
	{
		if (UMenuFooterPromptWidget* Prompt = Cast<UMenuFooterPromptWidget>(Entry->Widget.Get()))
		{
			Prompt->SetPromptState(Entry->IsEnabled.Get(true), Entry->bHovered, Entry->bPressed);
		}
	}
	if (UMenuDialogWidget* Dialog = CustomDialog.Get())
	{
		if (CustomDialogBody.IsBound())
		{
			Dialog->SetBody(CustomDialogBody.Get());
		}
	}

	const bool bFading = PageFade.Advance(InDeltaTime);
	const bool bSliding = PageSlide.Advance(InDeltaTime);
	if (bFading || bSliding)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return EActiveTimerReturnType::Continue;
}

// =====================================================================================================
//  Row host
// =====================================================================================================

UMenuSettingsSubsystem* SMenuSettingsPanel::GetSettings() const
{
	return Settings.Get();
}

FMenuSettingContext SMenuSettingsPanel::GetContext() const
{
	FMenuSettingContext Context;
	Context.LocalPlayer = LocalPlayer.Get();
	Context.Subsystem = Settings.Get();
	return Context;
}

int32 SMenuSettingsPanel::GetUserIndex() const
{
	if (const ULocalPlayer* Player = LocalPlayer.Get())
	{
		if (const TSharedPtr<const FSlateUser> User = Player->GetSlateUser())
		{
			return User->GetUserIndex();
		}
	}
	return 0;
}

FString SMenuSettingsPanel::GetPreviewValue(const UMenuSetting& Setting) const
{
	if (const FString* Found = PreviewValues.Find(Setting.Id))
	{
		return *Found;
	}
	return Setting.GetDefaultValue(GetContext());
}

void SMenuSettingsPanel::SetPreviewValue(const UMenuSetting& Setting, const FString& Value)
{
	PreviewValues.Add(Setting.Id, Value);
	bRefreshQueued = true;
}

void SMenuSettingsPanel::OnRowFocused(const TSharedRef<SMenuSettingRow>& Row)
{
	FocusedRow = Row;
	UpdateDescription(Row->GetSetting());
	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollDescendantIntoView(Row, !MenuAccessibility::IsReduceMotionEnabled(), EDescendantScrollDestination::IntoView, 24.f);
	}
	SettingFocusedDelegate.ExecuteIfBound(Row->GetSettingId());
	if (MenuAccessibility::IsMenuNarrationEnabled())
	{
		FText Spoken = FText::Format(LOCTEXT("RowNarration", "{0}, {1}"), Row->GetSetting() ? Row->GetSetting()->GetLabel() : FText::GetEmpty(), Row->GetValueText());
		if (!Row->IsSettingEnabled())
		{
			Spoken = FText::Format(LOCTEXT("RowNarrationDisabled", "{0}, unavailable. {1}"), Spoken, Row->GetDisabledReason());
		}
		Announce(Spoken);
	}
}

void SMenuSettingsPanel::OnRowHovered(const TSharedRef<SMenuSettingRow>& Row, bool bHovered)
{
	if (bHovered)
	{
		UpdateDescription(Row->GetSetting());
	}
	else if (const TSharedPtr<SMenuSettingRow> Focused = FocusedRow.Pin())
	{
		UpdateDescription(Focused->GetSetting());
	}
}

void SMenuSettingsPanel::OnKeyCaptureChanged(const TSharedRef<SMenuSettingRow>& Row, bool bCapturing)
{
	bCapturingKey = bCapturing;
	FocusedRow = Row;
}

void SMenuSettingsPanel::PlaySound(EMenuSoundEvent Event)
{
	const FMenuStyle& Style = StyleHolder->Style;
	const FUISoundSlot* Slot = nullptr;
	switch (Event)
	{
	case EMenuSoundEvent::Navigate: Slot = &Style.NavigateSound; break;
	case EMenuSoundEvent::Change: Slot = &Style.ChangeSound; break;
	case EMenuSoundEvent::Accept: Slot = &Style.AcceptSound; break;
	case EMenuSoundEvent::Back: Slot = &Style.BackSound; break;
	case EMenuSoundEvent::Tab: Slot = &Style.TabSound; break;
	default: Slot = &Style.ErrorSound; break;
	}
	if (!Slot || !Slot->IsValid() || !SoundContext.IsValid())
	{
		return;
	}
	// Throttled in real time (a paused game's clock stands still), so a held key or a dragged slider doesn't machine-gun.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastSoundTime < 0.035)
	{
		return;
	}
	LastSoundTime = Now;
	FUISoundSetSettings SetSettings;
	SetSettings.MinTimeBetweenPlays = 0.f;
	SetSettings.bSilentOnFirstEvent = false;
	FUISoundPlayer::Play(SoundContext.Get(), SoundRuntimes[static_cast<int32>(Event)], SetSettings, *Slot);
}

void SMenuSettingsPanel::Announce(const FText& Text)
{
	if (!Text.IsEmpty())
	{
		UGameplayStatics::AnnounceAccessibleString(Text.ToString());
	}
}

#undef LOCTEXT_NAMESPACE
