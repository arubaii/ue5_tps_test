// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimTimeline.h"

#include "SWidgetAnimAnchoredTip.h"

#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimLayerEditProxy.h"
#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"
#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"

#include "Algo/AllOf.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Base64.h"
#include "Algo/Find.h"
#include "Components/Widget.h"
#include "Slate/WidgetTransform.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Curves/CurveFloat.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTimeline"

// Every tooltip and help line the panel shows lives in NameTBDTooltips.h, so they can be read and reworded in one
// place - as every fixed color lives in NameTBDColors.h.
namespace WidgetAnimTips = NameTBD::Tooltips::WidgetAnimEditor;

namespace WidgetAnimTimeline
{
	/** How close, in pixels, a click has to be to a key diamond to be on it. */
	constexpr float KeyHitRadius = 6.f;

	/**
	 * How far out from a section's edge, and how far in, the mouse can be to grab that edge and resize it. Out
	 * reaches further than in: a Transform layer has keys sitting right on both of its edges, and a press just
	 * inside the section there is meant for the key.
	 */
	constexpr float SectionEdgeGrabOutside = 6.f;
	constexpr float SectionEdgeGrabInside = 3.f;

	/** A section narrower than this, in pixels, can only be grabbed at its edges from outside. */
	constexpr float SectionEdgeMinInnerWidth = 14.f;

	/** Two edges this close in seconds line up, for Linked Edges. */
	constexpr float EdgeAlignTolerance = 0.0005f;

	/** How far the mouse has to move before a press on a key becomes a drag, or a right-press a pan. */
	constexpr float DragThreshold = 3.f;

	/** Two keys are never closer than this, so a drag can't push one onto its neighbor and swap them. */
	constexpr float MinKeySpacing = 0.001f;

	/** The shortest stretch of time the tracks ever show, so a one-frame animation isn't one giant diamond. */
	constexpr float MinVisibleSeconds = 0.25f;

	/** How close, in pixels, the mouse has to be to a row edge to resize the rows from it. */
	constexpr float RowEdgeGrabDistance = 3.f;

	/** Width of the color band down the left of a group's rows, in the list and in the Track Editor. */
	constexpr float GroupBandWidth = 4.f;

	/** Height of the strips naming the Animation Layers list and the Track Editor. */
	constexpr float SectionHeaderHeight = 26.f;

	/** Header icons are drawn at this size whatever size their brush was authored at; the transport's larger. */
	constexpr float IconSize = 16.f;
	constexpr float TransportIconSize = 22.f;

	/** The Track Editor's Fit button: a size up from the header icons, so it reads as the strip's one control. */
	constexpr float FitIconSize = 18.f;

	/** How much smaller the inline panel's Selection panel is drawn than a Details panel's own rows. */
	constexpr float InlineSelectionScale = 0.85f;

	// The editor's own palette, read from the active theme so the panel sits in UE exactly as Sequencer does.
	FLinearColor ThemeColor(const FSlateColor& Color) { return Color.GetSpecifiedColor(); }
	FLinearColor RecessedColor()   { return ThemeColor(FStyleColors::Recessed); }
	FLinearColor BackgroundColor() { return ThemeColor(FStyleColors::Background); }
	FLinearColor PanelColor()      { return ThemeColor(FStyleColors::Panel); }
	FLinearColor HeaderColor()     { return ThemeColor(FStyleColors::Header); }
	FLinearColor HoverColor()      { return ThemeColor(FStyleColors::Hover); }
	FLinearColor SelectColor()     { return ThemeColor(FStyleColors::Select); }
	FLinearColor ForegroundColor() { return ThemeColor(FStyleColors::Foreground); }

	FLinearColor WithAlpha(FLinearColor Color, float Alpha) { Color.A = Alpha; return Color; }

	/** Leaves out of an enum property's dropdown every value IsAllowed turns down. */
	void HideEnumValues(const TSharedPtr<IPropertyHandle>& Handle, const UEnum* Enum, TFunctionRef<bool(int64)> IsAllowed)
	{
		if (!Handle.IsValid() || !Handle->IsValidHandle() || !Enum)
		{
			return;
		}

		const TSharedRef<FPropertyRestriction> Restriction = MakeShared<FPropertyRestriction>(WidgetAnimTips::NotForThisTrackType());
		bool bAnyHidden = false;

		// The last entry is the _MAX the enum is generated with, never a choice.
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			if (!IsAllowed(Enum->GetValueByIndex(Index)))
			{
				Restriction->AddHiddenValue(Enum->GetNameStringByIndex(Index));
				bAnyHidden = true;
			}
		}

		if (bAnyHidden)
		{
			Handle->AddRestriction(Restriction);
		}
	}

	/**
	 * Leaves out of a track's Timing, Value Type and Value Drives dropdowns the choices that mean nothing on the tracks
	 * being edited (on every one of them, when several are): Spring on a Pulse; a Value Type its type can't take - a
	 * Color anywhere but a Material Vector, a Vector 2D anywhere but a Scale or a Transform driving two axes; and, on a
	 * Transform, a Value Drives with only one axis while it takes a Vector 2D, and the Shape Box settings while its
	 * Animate Shape is off. A choice already made still shows as it is; one
	 * its type can't take is read as Float at runtime - see FWidgetAnimLayer::GetEffectiveValueKind.
	 *
	 * Worked out when the Selection panel is built, which it is again whenever a track's type, Timing, Value Type or
	 * Value Drives changes - see ComputeLayoutSignature.
	 */
	void RestrictLayerChoices(const TSharedPtr<IPropertyHandle>& Child, const TArray<const FWidgetAnimLayer*>& Layers)
	{
		const FProperty* Property = Child.IsValid() ? Child->GetProperty() : nullptr;
		if (!Property || Layers.Num() == 0)
		{
			return;
		}

		const FName Name = Property->GetFName();
		if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Timing))
		{
			HideEnumValues(Child, StaticEnum<EWidgetAnimTiming>(), [&Layers](int64 Value)
			{
				return Algo::AllOf(Layers, [Value](const FWidgetAnimLayer* Layer)
				{
					return WidgetAnimTimingAllowed(Layer->AnimType, static_cast<EWidgetAnimTiming>(Value));
				});
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueDrives))
		{
			HideEnumValues(Child, StaticEnum<EWidgetAnimTransformValueTarget>(), [&Layers](int64 Value)
			{
				const EWidgetAnimTransformValueTarget Target = static_cast<EWidgetAnimTransformValueTarget>(Value);
				bool bX = true;
				bool bY = true;
				const bool bShapeField = WidgetAnimValue::IsShapePart(WidgetAnimValue::GetTransformPart(Target, bX, bY));
				return Algo::AllOf(Layers, [Target, bShapeField](const FWidgetAnimLayer* Layer)
				{
					return (Layer->ValueOptions.Kind != EWidgetAnimValueKind::Vector2D || WidgetAnimValue::TransformTargetTakesTwoAxes(Target))
						&& (!bShapeField || Layer->Transform.IsAnimatingShape());
				});
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueOptions))
		{
			HideEnumValues(Child->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueOptions, Kind)), StaticEnum<EWidgetAnimValueKind>(), [&Layers](int64 Value)
			{
				return Algo::AllOf(Layers, [Value](const FWidgetAnimLayer* Layer)
				{
					return WidgetAnimValueKindAllowed(Layer->AnimType, Layer->ValueDrives, static_cast<EWidgetAnimValueKind>(Value));
				});
			});
		}
	}

	// Every fixed color lives in NameTBDColors.h, under NameTBD::Colors::WidgetAnimEditor, so it can be tuned
	// in one place. Only the theme colors above come from the editor itself.
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	const FLinearColor& SeparatorColor = Palette::RowSeparator;

	/** Rounded boxes, tinted where they are drawn: fill, and a 1px outline. What sections are made of in Sequencer. */
	const FSlateBrush* RoundedFill(float Radius = 3.f)
	{
		static const FSlateRoundedBoxBrush Brush2(FLinearColor::White, 2.f);
		static const FSlateRoundedBoxBrush Brush3(FLinearColor::White, 3.f);
		static const FSlateRoundedBoxBrush Brush5(FLinearColor::White, 5.f);
		static const FSlateRoundedBoxBrush Brush7(FLinearColor::White, 7.f);

		if (Radius <= 2.5f) { return &Brush2; }
		if (Radius <= 4.f)  { return &Brush3; }
		if (Radius <= 6.f)  { return &Brush5; }
		return &Brush7;
	}

	const FSlateBrush* RoundedOutline()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 3.f, FLinearColor::White, 1.f);
		return &Brush;
	}

	/** The graph node title brushes a section is drawn with - the editor's own gradient, gloss and sheen. */
	const FSlateBrush* NodeTitleGloss()     { return FAppStyle::GetBrush("Graph.Node.TitleGloss"); }
	const FSlateBrush* NodeColorSpill()     { return FAppStyle::GetBrush("Graph.Node.ColorSpill"); }
	const FSlateBrush* NodeTitleHighlight() { return FAppStyle::GetBrush("Graph.Node.TitleHighlight"); }

	/** The Animation Controls pill, and a lit segment inside it - rounded right round, as a media player's controls are. */
	const FSlateBrush* ControlsPillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::ControlsPill, 13.f);
		return &Brush;
	}

	const FSlateBrush* ControlsSegmentBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 10.f);
		return &Brush;
	}

	/** The Track Editor's tooltips: the panel's own dark panel with a faint outline, not the editor's light tooltip. */
	const FSlateBrush* ToolTipBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::TooltipBackground, 5.f, Palette::TooltipOutline, 1.f);
		return &Brush;
	}

	/**
	 * A plain line of help as the panel's own tooltip: its dark panel and its text, as every track tooltip and
	 * header button has, rather than the editor's light one. For the panel's own controls; anything the editor
	 * itself provides - Dock in Layout, the Selection panel's fields - keeps the editor's.
	 */
	TSharedPtr<IToolTip> MakePanelToolTip(const TAttribute<FText>& Text)
	{
		// The text as content of its own, not SToolTip's Text: SToolTip has no color for its own text block, and
		// the panel's tooltip text is its own color on its own dark panel.
		return SNew(SToolTip)
			.BorderImage(ToolTipBrush())
			.TextMargin(FMargin(0.f))
			[
				SNew(SBox)
				.Padding(FMargin(12.f, 9.f))
				.MaxDesiredWidth(420.f)
				[
					SNew(STextBlock)
					.Text(Text)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(Palette::TooltipValue)
					.AutoWrapText(true)
				]
			];
	}

	/** Background of a small inline label - a group's target. */
	const FSlateBrush* PillBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::Pill, 6.f);
		return &Brush;
	}

	/** The Play/Pause tints, shared with the toolbar's. */
	const FLinearColor& PlayTint = Palette::PlayTint;
	const FLinearColor& PauseTint = Palette::PauseTint;

	const FSlateBrush* WhiteBrush()
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	/** The first of Candidates the app style has - icon names move between engine versions. Null if none do. */
	const FSlateBrush* PickBrush(std::initializer_list<FName> Candidates)
	{
		const ISlateStyle& Style = FAppStyle::Get();
		for (const FName Candidate : Candidates)
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Candidate, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return nullptr;
	}

	/**
	 * An icon for one of the Animation Controls: the plugin's own, if a file named Name (.svg or .png) has been
	 * dropped into NameTBD/Resources; else the first of the editor's Candidates that exists; else null, and the
	 * control shows its name instead. Looked up once each, and kept.
	 */
	const FSlateBrush* FindControlIcon(const FString& Name, std::initializer_list<FName> Candidates)
	{
		static TMap<FString, TSharedPtr<FSlateBrush>> PluginIcons;

		if (!PluginIcons.Contains(Name))
		{
			TSharedPtr<FSlateBrush> Brush;
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD")))
			{
				const FString Base = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Resources") / Name);
				if (FPaths::FileExists(Base + TEXT(".svg")))
				{
					Brush = MakeShared<FSlateVectorImageBrush>(Base + TEXT(".svg"), FVector2D(16.0, 16.0));
				}
				else if (FPaths::FileExists(Base + TEXT(".png")))
				{
					Brush = MakeShared<FSlateImageBrush>(Base + TEXT(".png"), FVector2D(16.0, 16.0));
				}
			}
			PluginIcons.Add(Name, Brush);
		}

		const TSharedPtr<FSlateBrush>& Own = PluginIcons.FindChecked(Name);
		return Own.IsValid() ? Own.Get() : PickBrush(Candidates);
	}

	/**
	 * An icon at IconSize, however big its brush was authored. The candidates found for an icon are not all
	 * the same size - the pause icon found in 5.7 is a 40px toolbar icon - so an unboxed image grows the row
	 * it sits in, and every button in it.
	 */
	TSharedRef<SWidget> MakeFixedIcon(TAttribute<const FSlateBrush*> Brush, TAttribute<FSlateColor> Color = FSlateColor::UseForeground(), float Size = IconSize)
	{
		return SNew(SBox)
			.WidthOverride(Size)
			.HeightOverride(Size)
			[
				SNew(SImage)
				.Image(Brush)
				.ColorAndOpacity(Color)
			];
	}

	/** A small borderless icon button, or its text when none of the icons exist in this engine version. */
	TSharedRef<SWidget> MakeIconButton(std::initializer_list<FName> Icons, const FText& Fallback, const FText& ToolTip, FOnClicked OnClicked, float Size = IconSize)
	{
		const FSlateBrush* Brush = PickBrush(Icons);

		TSharedRef<SWidget> Content = Brush
			? MakeFixedIcon(Brush, FSlateColor::UseForeground(), Size)
			: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(Fallback));

		// Its tip above it, pinned - not following the cursor across the row of buttons beside it.
		return SNew(SWidgetAnimAnchoredTip)
			.Text(ToolTip)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(Size > IconSize ? FMargin(6.f, 3.f) : FMargin(4.f, 2.f))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.OnClicked(OnClicked)
				[
					Content
				]
			];
	}

	/** A tick step for the ruler that leaves at least MinPixels between labels. */
	float ChooseTickStep(float PixelsPerSecond, float MinPixels)
	{
		static const float Steps[] = { 0.005f, 0.01f, 0.02f, 0.025f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f, 30.f, 60.f };
		for (const float Step : Steps)
		{
			if (Step * PixelsPerSecond >= MinPixels)
			{
				return Step;
			}
		}
		return 60.f;
	}

	FString FormatSeconds(float Seconds, float Step)
	{
		const int32 Decimals = Step >= 1.f ? 0 : (Step >= 0.1f ? 1 : (Step >= 0.01f ? 2 : 3));
		return FString::Printf(TEXT("%.*f"), Decimals, Seconds);
	}

	/**
	 * Every built-in layer type the Add Layer menus offer, in the enum's order. Registered Type is left out: it is not
	 * a type of its own but the slot every registered type shares, and those are offered one by one instead - see
	 * ForEachRegisteredType.
	 */
	void ForEachAnimType(TFunctionRef<void(EWidgetAnimType)> Visit)
	{
		const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
		const int32 NumTypes = TypeEnum ? TypeEnum->NumEnums() - 1 : 0; // the last entry is the generated _MAX
		for (int32 Index = 0; Index < NumTypes; ++Index)
		{
			const EWidgetAnimType AnimType = static_cast<EWidgetAnimType>(TypeEnum->GetValueByIndex(Index));
			if (AnimType != EWidgetAnimType::Custom && !TypeEnum->HasMetaData(TEXT("Hidden"), Index))
			{
				Visit(AnimType);
			}
		}
	}

	/**
	 * Every C++ registered track type the Add menus offer, in name order - the text tracks, and any a game's own modules have
	 * added. Not the Blueprint bridge, which is not a type of its own; see ForEachBlueprintType.
	 */
	void ForEachRegisteredType(TFunctionRef<void(const FWidgetAnimEvaluator&)> Visit)
	{
		TArray<const FWidgetAnimEvaluator*> Evaluators;
		FWidgetAnimEvaluatorRegistry::Get().GetAll(Evaluators);
		for (const FWidgetAnimEvaluator* Evaluator : Evaluators)
		{
			if (Evaluator->IsListedInAddMenu())
			{
				Visit(*Evaluator);
			}
		}
	}

	/**
	 * Every Blueprint track type in the project, in name order, by its class's path - found through the asset registry,
	 * so types that have never been opened are listed too without loading them. Loaded only when one is picked.
	 */
	void ForEachBlueprintType(TFunctionRef<void(const FTopLevelAssetPath&, const FText&)> Visit)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TSet<FTopLevelAssetPath> DerivedPaths;
		AssetRegistry.GetDerivedClassNames({ UWidgetAnimBlueprintType::StaticClass()->GetClassPathName() }, TSet<FTopLevelAssetPath>(), DerivedPaths);

		TArray<TPair<FText, FTopLevelAssetPath>> Types;
		for (const FTopLevelAssetPath& ClassPath : DerivedPaths)
		{
			const FString ClassName = ClassPath.GetAssetName().ToString();

			// Blueprint compilation's leftovers are classes too, but never types anyone made.
			if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			// A class already loaded knows its own display name and whether it is abstract - a base for other types,
			// not one to put on a track. One not loaded yet is named by its asset.
			FText Name = FText::FromString(ClassName.EndsWith(TEXT("_C")) ? ClassName.LeftChop(2) : ClassName);
			if (const UClass* LoadedClass = FindObject<UClass>(ClassPath))
			{
				if (LoadedClass->HasAnyClassFlags(CLASS_Abstract))
				{
					continue;
				}
				Name = LoadedClass->GetDefaultObject<UWidgetAnimBlueprintType>()->GetTypeDisplayName();
			}
			Types.Emplace(Name, ClassPath);
		}

		Types.Sort([](const TPair<FText, FTopLevelAssetPath>& A, const TPair<FText, FTopLevelAssetPath>& B)
		{
			return A.Key.CompareTo(B.Key) < 0;
		});

		for (const TPair<FText, FTopLevelAssetPath>& Type : Types)
		{
			Visit(Type.Value, Type.Key);
		}
	}
}

// =====================================================================================================
// The Track Editor's tooltip
// =====================================================================================================

/**
 * One tooltip for the whole Track Editor, whose content is made fresh each time it opens for whatever is under
 * the mouse - a track, or a key - and which is empty, so never shows, over anything else. Drawn in the panel's
 * own colors rather than the editor's light tooltip: see NameTBDColors.h, Track tooltips.
 */
class SWidgetAnimTrackToolTip : public SToolTip
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimTrackToolTip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<bool()> InHasContent, TFunction<TSharedRef<SWidget>()> InMakeContent)
	{
		HasContent = MoveTemp(InHasContent);
		MakeContent = MoveTemp(InMakeContent);

		SToolTip::Construct(SToolTip::FArguments()
			.TextMargin(FMargin(0.f))
			.BorderImage(WidgetAnimTimeline::ToolTipBrush()));
	}

	virtual bool IsEmpty() const override
	{
		return !HasContent || !HasContent();
	}

	virtual void OnOpening() override
	{
		if (MakeContent)
		{
			SetContentWidget(MakeContent());
		}
	}

private:

	TFunction<bool()> HasContent;
	TFunction<TSharedRef<SWidget>()> MakeContent;
};

// =====================================================================================================
// The Track Editor: ruler, group lines, track lines, sections, keys, playhead
// =====================================================================================================

/**
 * The Track Editor, drawn by hand the way Sequencer draws its track area: a time ruler pinned across the top,
 * and under it SWidgetAnimTimeline::GetTrackLines - each group's own line, then the lines its tracks sit on,
 * several to a line where they run one after another. Scrolled in time by its horizontal scroll bar, a
 * right-drag, a middle-drag or the wheel's zoom, and down its lines by a right-drag, a middle-drag or
 * Shift + wheel. It lines up with nothing beside it - the Animation Layers list is its own panel.
 */
class SWidgetAnimTimelineTracks : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimTimelineTracks) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel, TSharedPtr<SScrollBar> InHorizontalScrollBar)
	{
		Panel = InPanel;
		HorizontalScrollBar = InHorizontalScrollBar;

		// Zoomed in, bars and keys lie well outside the visible stretch of time; without this they are drawn
		// there too - over the list beside the tracks.
		SetClipping(EWidgetClipping::ClipToBounds);

		const TSharedRef<SWidgetAnimTrackToolTip> TrackToolTip = SNew(SWidgetAnimTrackToolTip,
			[this]() { return HasToolTipTarget(); },
			[this]() { return MakeToolTipContent(); });
		SetToolTip(TSharedPtr<IToolTip>(TrackToolTip));
	}

	/** Fit the whole animation into view again, and keep fitting it as it changes, until the next zoom or pan. */
	void FitToAnimation()
	{
		bUserView = false;
	}

	/** Where the view is - its span of time, whether it was zoomed or panned by hand, how far down it is scrolled - to put back later. */
	void GetViewState(FVector2f& OutRange, bool& bOutUserView, float& OutScrollY) const
	{
		OutRange = FVector2f(ViewMin, ViewMax);
		bOutUserView = bUserView;
		OutScrollY = ScrollY;
	}

	void SetViewState(const FVector2f& Range, bool bInUserView, float InScrollY)
	{
		ViewMin = Range.X;
		ViewMax = Range.Y;
		bUserView = bInUserView;
		ScrollY = InScrollY;
	}

	/** The horizontal scroll bar was dragged: where its thumb now starts, as a fraction of the whole range. */
	void ScrollHorizontally(float OffsetFraction)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner || Owner->IsDragging())
		{
			return;
		}

		float RangeMin = 0.f;
		float RangeMax = 1.f;
		GetScrollRange(Owner->GetPassSeconds(), RangeMin, RangeMax);

		const float ViewWidth = ViewMax - ViewMin;
		ViewMin = RangeMin + FMath::Clamp(OffsetFraction, 0.f, 1.f) * (RangeMax - RangeMin);
		ViewMax = ViewMin + ViewWidth;
		bUserView = true;
	}

	/** Scrolls the lines until one is in view, and the time until a stretch of it is. */
	void RevealLine(int32 LineIndex)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner || LineIndex == INDEX_NONE)
		{
			return;
		}

		const float RowHeight = Owner->GetRowHeight();
		const float ViewHeight = FMath::Max(LastSize.Y - SWidgetAnimTimeline::RulerHeight, RowHeight);
		const float Top = (GetNumContextRows() + LineIndex) * RowHeight;

		if (Top < ScrollY)
		{
			ScrollY = Top;
		}
		else if (Top + RowHeight > ScrollY + ViewHeight)
		{
			ScrollY = Top + RowHeight - ViewHeight;
		}
		ClampScroll(LastSize.Y);
	}

	void RevealTime(float Start, float End)
	{
		const float ViewWidth = ViewMax - ViewMin;
		const float Margin = ViewWidth * 0.08f;
		if (Start >= ViewMin && End <= ViewMax)
		{
			return;
		}

		// Longer than the view: its start, at the left. Otherwise just far enough to bring it all in.
		if (End - Start > ViewWidth - Margin * 2.f || Start < ViewMin)
		{
			ViewMin = Start - Margin;
		}
		else
		{
			ViewMin = End + Margin - ViewWidth;
		}
		ViewMax = ViewMin + ViewWidth;
		bUserView = true;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		// It fills whatever it is given and scrolls inside that, so it only asks for room for a few lines.
		return FVector2D(200.f, SWidgetAnimTimeline::RulerHeight + 4.f * SWidgetAnimTimeline::DefaultRowHeight);
	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:

	using ELayerDrag = SWidgetAnimTimeline::ELayerDrag;
	using FTrackLine = SWidgetAnimTimeline::FTrackLine;

	enum class EDrag : uint8
	{
		None,
		Scrub,
		KeyPending,
		Key,
		LayerPending,
		Layer,
		RightPending,
		Pan,
		ResizeRows,
		MarqueePending,
		Marquee
	};

	/** Which edge of a section is under the mouse, for resizing it. */
	enum class EEdge : uint8
	{
		None,
		Start,
		End
	};

	/** The fold arrow on a group's line: this size, drawn just before the group's bar - or where time 0 is, for a group with no tracks. */
	static constexpr float FoldArrowSize = 10.f;

	float GetFoldX(int32 GroupIndex, float Width) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const FVector2f GroupSpan = Owner ? Owner->GetGroupSpan(GroupIndex) : FVector2f(0.f, -1.f);
		const float BarStart = TimeToX(GroupSpan.Y >= GroupSpan.X ? GroupSpan.X : 0.f, Width);
		return FMath::Max(BarStart - FoldArrowSize - 5.f, WidgetAnimTimeline::GroupBandWidth + 2.f);
	}

	/** What is under the mouse on the lines. */
	struct FHit
	{
		int32 Line = INDEX_NONE;

		/** The track the mouse is on, or whose edge or key it is on. */
		int32 Layer = INDEX_NONE;
		int32 Key = INDEX_NONE;
		EEdge Edge = EEdge::None;

		/** Inside the track's section - not just beside it on its line. */
		bool bOnSection = false;

		/** On a group line's fold arrow. */
		bool bOnFold = false;
	};

	/**
	 * Fits the view to the animation, unless it has been zoomed or panned by hand - or something is being
	 * dragged. A drag that stretches the animation would otherwise rescale the view under the mouse every
	 * frame, and the drag would chase its own tail.
	 */
	void UpdateView(float PassSeconds, bool bFrozen) const
	{
		if (bUserView || bFrozen)
		{
			return;
		}

		const float Span = FMath::Max(PassSeconds, WidgetAnimTimeline::MinVisibleSeconds);
		ViewMin = -Span * 0.04f;
		ViewMax = Span * 1.08f;
	}

	/** The whole stretch of time the horizontal scroll bar covers: the animation with a margin, and the view. */
	void GetScrollRange(float PassSeconds, float& OutMin, float& OutMax) const
	{
		const float Span = FMath::Max(PassSeconds, WidgetAnimTimeline::MinVisibleSeconds);
		OutMin = FMath::Min(-Span * 0.04f, ViewMin);
		OutMax = FMath::Max(Span * 1.08f, ViewMax);
	}

	float TimeToX(float Seconds, float Width) const
	{
		return (Seconds - ViewMin) / FMath::Max(ViewMax - ViewMin, KINDA_SMALL_NUMBER) * Width;
	}

	float XToTime(float X, float Width) const
	{
		return ViewMin + X / FMath::Max(Width, 1.f) * (ViewMax - ViewMin);
	}

	/** The snap distance in seconds at the current zoom - 0 with Track Snapping off. */
	float GetSnapThresholdSeconds(float Width) const
	{
		const UWidgetAnimEditorSettings& Settings = UWidgetAnimEditorSettings::Get();
		if (!Settings.bTrackSnapping)
		{
			return 0.f;
		}

		const float SecondsPerPixel = (ViewMax - ViewMin) / FMath::Max(Width, 1.f);
		return Settings.SnapDistancePixels * SecondsPerPixel;
	}

	float GetRowHeight() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner ? Owner->GetRowHeight() : SWidgetAnimTimeline::DefaultRowHeight;
	}

	int32 GetNumLines() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner ? Owner->GetTrackLines().Num() : 0;
	}

	/** How many rows of context tracks sit above the lines - see SWidgetAnimTimeline::GetContextSpans. */
	int32 GetNumContextRows() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner ? Owner->GetNumContextRows() : 0;
	}

	/** Where a context row's top is drawn, scrolled with the lines below it. */
	float GetContextRowTop(int32 Row) const
	{
		return SWidgetAnimTimeline::RulerHeight + Row * GetRowHeight() - ScrollY;
	}

	/** Where a line's top is drawn, in local pixels, with the lines scrolled - under any context rows. */
	float GetLineTop(int32 LineIndex) const
	{
		return SWidgetAnimTimeline::RulerHeight + (GetNumContextRows() + LineIndex) * GetRowHeight() - ScrollY;
	}

	/** How far down the lines can be scrolled: to the last line, and one line's room below it. */
	float GetMaxScrollY(float Height) const
	{
		const float Content = (GetNumContextRows() + GetNumLines() + 1) * GetRowHeight();
		return FMath::Max(Content - (Height - SWidgetAnimTimeline::RulerHeight), 0.f);
	}

	void ClampScroll(float Height) const
	{
		ScrollY = FMath::Clamp(ScrollY, 0.f, GetMaxScrollY(Height));
	}

	/** The line under a local Y, or INDEX_NONE over the ruler or below the last line. */
	int32 LineAt(float Y) const
	{
		if (Y < SWidgetAnimTimeline::RulerHeight)
		{
			return INDEX_NONE;
		}

		const int32 Line = FMath::FloorToInt((Y - SWidgetAnimTimeline::RulerHeight + ScrollY) / GetRowHeight()) - GetNumContextRows();
		return Line >= 0 && Line < GetNumLines() ? Line : INDEX_NONE;
	}

	/**
	 * The line edge under a local Y that resizes the lines, as how many lines lie above it - only ever the bottom of
	 * the last line - or INDEX_NONE. Dragging it resizes every line at once.
	 */
	int32 RowEdgeAt(float Y) const
	{
		if (Y < SWidgetAnimTimeline::RulerHeight)
		{
			return INDEX_NONE;
		}

		// Only the bottom edge of the last line. Every edge used to resize the lines, which put a resize grip along
		// the top and bottom of every track line - in the way of pressing a track near its edge, and of starting a
		// selection box between two lines.
		const int32 Edge = GetNumLines();
		const float EdgeY = GetLineTop(Edge);

		return Edge >= 1 && EdgeY > SWidgetAnimTimeline::RulerHeight
			&& FMath::Abs(Y - EdgeY) <= WidgetAnimTimeline::RowEdgeGrabDistance
			? Edge
			: INDEX_NONE;
	}

	const FTrackLine* GetLine(int32 LineIndex) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner && Owner->GetTrackLines().IsValidIndex(LineIndex) ? &Owner->GetTrackLines()[LineIndex] : nullptr;
	}

	/** A layer's start and length, in seconds from the top. Zero when it has none. */
	FVector2f GetLayerSpan(int32 LayerIndex) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner && Owner->GetTrackSpans().IsValidIndex(LayerIndex) ? Owner->GetTrackSpans()[LayerIndex] : FVector2f::ZeroVector;
	}

	/** A layer's section, in local pixels: left, right, top and bottom - exactly where OnPaint draws it. Invalid while its group is folded. */
	FBox2f GetSectionRect(int32 LayerIndex, float Width) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const int32 LineIndex = Owner ? Owner->GetLayerLine(LayerIndex) : INDEX_NONE;
		if (LineIndex == INDEX_NONE)
		{
			return FBox2f(ForceInit);
		}

		const FVector2f Span = GetLayerSpan(LayerIndex);
		const float Top = GetLineTop(LineIndex) + 3.f;
		const float Left = TimeToX(Span.X, Width);
		const float Right = Left + FMath::Max(TimeToX(Span.X + Span.Y, Width) - Left, 4.f);
		return FBox2f(FVector2f(Left, Top), FVector2f(Right, Top + GetRowHeight() - 7.f));
	}

	/** The key of a layer under local X, or INDEX_NONE. */
	int32 KeyAt(int32 LayerIndex, float X, float Width) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner)
		{
			return INDEX_NONE;
		}

		const float Start = GetLayerSpan(LayerIndex).X;
		TArray<float> KeyTimes;
		Owner->GetKeyTimes(LayerIndex, KeyTimes);

		int32 Best = INDEX_NONE;
		float BestDistance = WidgetAnimTimeline::KeyHitRadius;
		for (int32 Index = 0; Index < KeyTimes.Num(); ++Index)
		{
			const float Distance = FMath::Abs(TimeToX(Start + KeyTimes[Index], Width) - X);
			if (Distance <= BestDistance)
			{
				BestDistance = Distance;
				Best = Index;
			}
		}
		return Best;
	}

	/**
	 * What is under a local position. On a section: a key there wins, then its edges from just inside, then the
	 * section itself - a Transform layer's first and last keys sit right on its edges, and a press just inside
	 * is meant for the key. Beside a section: just outside it always reaches its edge; failing that, a key
	 * poking out past the edge.
	 */
	FHit HitTest(const FVector2f& Local, float Width) const
	{
		using namespace WidgetAnimTimeline;

		FHit Hit;
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		Hit.Line = LineAt(Local.Y);
		const FTrackLine* Line = GetLine(Hit.Line);
		if (!Owner || !Line)
		{
			return Hit;
		}

		if (Line->Kind == FTrackLine::EKind::Group)
		{
			// The fold arrow, with a little room round it so it's easy to hit.
			const float FoldX = GetFoldX(Line->Group, Width);
			Hit.bOnFold = Local.X >= FoldX - 4.f && Local.X <= FoldX + FoldArrowSize + 4.f;
			return Hit;
		}

		for (const int32 LayerIndex : Line->Layers)
		{
			const FBox2f Rect = GetSectionRect(LayerIndex, Width);
			if (!Rect.bIsValid || Local.Y < Rect.Min.Y - 1.f || Local.Y > Rect.Max.Y + 1.f || Local.X < Rect.Min.X || Local.X > Rect.Max.X)
			{
				continue;
			}

			Hit.Layer = LayerIndex;
			Hit.bOnSection = true;
			Hit.Key = KeyAt(LayerIndex, Local.X, Width);

			if (Hit.Key == INDEX_NONE && Rect.Max.X - Rect.Min.X >= SectionEdgeMinInnerWidth)
			{
				if (Local.X <= Rect.Min.X + SectionEdgeGrabInside && Owner->CanResizeLayer(LayerIndex, /*bStartEdge=*/true))
				{
					Hit.Edge = EEdge::Start;
				}
				else if (Local.X >= Rect.Max.X - SectionEdgeGrabInside && Owner->CanResizeLayer(LayerIndex, /*bStartEdge=*/false))
				{
					Hit.Edge = EEdge::End;
				}
			}
			return Hit;
		}

		// Beside the sections: the nearest edge within reach.
		float BestDistance = TNumericLimits<float>::Max();
		for (const int32 LayerIndex : Line->Layers)
		{
			const FBox2f Rect = GetSectionRect(LayerIndex, Width);
			if (!Rect.bIsValid || Local.Y < Rect.Min.Y - 1.f || Local.Y > Rect.Max.Y + 1.f)
			{
				continue;
			}

			if (Local.X >= Rect.Min.X - SectionEdgeGrabOutside && Local.X < Rect.Min.X && Rect.Min.X - Local.X < BestDistance
				&& Owner->CanResizeLayer(LayerIndex, /*bStartEdge=*/true))
			{
				BestDistance = Rect.Min.X - Local.X;
				Hit.Layer = LayerIndex;
				Hit.Edge = EEdge::Start;
			}
			if (Local.X > Rect.Max.X && Local.X <= Rect.Max.X + SectionEdgeGrabOutside && Local.X - Rect.Max.X < BestDistance
				&& Owner->CanResizeLayer(LayerIndex, /*bStartEdge=*/false))
			{
				BestDistance = Local.X - Rect.Max.X;
				Hit.Layer = LayerIndex;
				Hit.Edge = EEdge::End;
			}
		}

		if (Hit.Edge == EEdge::None)
		{
			for (const int32 LayerIndex : Line->Layers)
			{
				const int32 Key = KeyAt(LayerIndex, Local.X, Width);
				if (Key != INDEX_NONE)
				{
					Hit.Layer = LayerIndex;
					Hit.Key = Key;
					break;
				}
			}
		}

		return Hit;
	}

	void PushMenu(const FPointerEvent& MouseEvent, const TSharedRef<SWidget>& Menu)
	{
		const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, Menu, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}

	bool IsKeyPressed(int32 LayerIndex, int32 Key) const
	{
		return (Drag == EDrag::KeyPending || Drag == EDrag::Key) && PressLayer == LayerIndex && PressKey == Key;
	}

	/** The selection box, corners in order however it was dragged. */
	FBox2f GetMarqueeRect() const
	{
		return FBox2f(
			FVector2f(FMath::Min(MarqueeStart.X, MarqueeEnd.X), FMath::Min(MarqueeStart.Y, MarqueeEnd.Y)),
			FVector2f(FMath::Max(MarqueeStart.X, MarqueeEnd.X), FMath::Max(MarqueeStart.Y, MarqueeEnd.Y)));
	}

	/**
	 * What the selection box takes in: the tracks it touches when it holds one of them whole - boxing a track
	 * selects it - else the keys inside it, else the tracks it touches. Never keys and tracks together: they are
	 * edited differently, and the Selection panel shows one or the other.
	 */
	void CollectMarquee(float Width, TArray<SWidgetAnimTimeline::FKeyRef>& OutKeys, TArray<int32>& OutLayers) const
	{
		OutKeys.Reset();
		OutLayers.Reset();

		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner)
		{
			return;
		}

		const FBox2f Marquee = GetMarqueeRect();
		bool bAnyWhole = false;

		for (const FTrackLine& Line : Owner->GetTrackLines())
		{
			if (Line.Kind != FTrackLine::EKind::Lane)
			{
				continue;
			}

			for (const int32 LayerIndex : Line.Layers)
			{
				const FBox2f Rect = GetSectionRect(LayerIndex, Width);
				if (!Rect.bIsValid || Rect.Max.X < Marquee.Min.X || Rect.Min.X > Marquee.Max.X || Rect.Max.Y < Marquee.Min.Y || Rect.Min.Y > Marquee.Max.Y)
				{
					continue;
				}

				OutLayers.Add(LayerIndex);
				bAnyWhole |= Rect.Min.X >= Marquee.Min.X && Rect.Max.X <= Marquee.Max.X && Rect.Min.Y >= Marquee.Min.Y && Rect.Max.Y <= Marquee.Max.Y;

				// A key is in the box when its diamond's center is - the middle of the section, at its time.
				TArray<float> KeyTimes;
				Owner->GetKeyTimes(LayerIndex, KeyTimes);
				const float KeyY = (Rect.Min.Y + Rect.Max.Y) * 0.5f;
				const float Start = GetLayerSpan(LayerIndex).X;
				for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
				{
					const float KeyX = TimeToX(Start + KeyTimes[Key], Width);
					if (KeyX >= Marquee.Min.X && KeyX <= Marquee.Max.X && KeyY >= Marquee.Min.Y && KeyY <= Marquee.Max.Y)
					{
						OutKeys.Add(SWidgetAnimTimeline::FKeyRef{ LayerIndex, Key });
					}
				}
			}
		}

		if (bAnyWhole || OutKeys.Num() == 0)
		{
			OutKeys.Reset();
		}
		else
		{
			OutLayers.Reset();
		}
	}

	/** Which section edge is lit on a layer: the one being dragged, else the one under the mouse. */
	EEdge GetLitEdge(int32 LayerIndex) const
	{
		if ((Drag == EDrag::Layer || Drag == EDrag::LayerPending) && PressLayer == LayerIndex)
		{
			return PendingLayerDrag == ELayerDrag::ResizeStart ? EEdge::Start
				: PendingLayerDrag == ELayerDrag::ResizeEnd ? EEdge::End
				: EEdge::None;
		}
		return Drag == EDrag::None && Hover.Layer == LayerIndex ? Hover.Edge : EEdge::None;
	}

	// ---- Tooltip ---------------------------------------------------------------------------------------

	bool HasToolTipTarget() const
	{
		return Drag == EDrag::None && TipLayer != INDEX_NONE;
	}

	TSharedRef<SWidget> MakeToolTipContent() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner || TipLayer == INDEX_NONE)
		{
			return SNullWidget::NullWidget;
		}
		return TipKey != INDEX_NONE ? Owner->MakeKeyToolTip(TipLayer, TipKey) : Owner->MakeLayerToolTip(TipLayer);
	}

	/**
	 * What the tooltip is about: a key, or a track's body - not its edges, which are for resizing. A change
	 * closes the open tooltip, so the next one opens fresh for what is under the mouse now.
	 */
	void SetToolTipTarget(int32 Layer, int32 Key)
	{
		if (Layer == TipLayer && Key == TipKey)
		{
			return;
		}

		const bool bWasShowing = TipLayer != INDEX_NONE;
		TipLayer = Layer;
		TipKey = Key;

		if (bWasShowing)
		{
			FSlateApplication::Get().CloseToolTip();
		}
	}

	void ClearHover()
	{
		Hover = FHit();
		HoverRowEdge = INDEX_NONE;
		HoverX = -1.f;
		SetToolTipTarget(INDEX_NONE, INDEX_NONE);
	}

	TWeakPtr<SWidgetAnimTimeline> Panel;
	TSharedPtr<SScrollBar> HorizontalScrollBar;

	mutable float ViewMin = 0.f;
	mutable float ViewMax = 1.f;
	bool bUserView = false;

	/** How far down the lines are scrolled, in pixels, and the size the widget was last drawn at. */
	mutable float ScrollY = 0.f;
	mutable FVector2f LastSize = FVector2f(400.f, 200.f);

	EDrag Drag = EDrag::None;
	FVector2f PressPosition = FVector2f::ZeroVector;
	int32 PressKey = INDEX_NONE;
	int32 PressLayer = INDEX_NONE;

	/** The time under the mouse at the press - what a track drag measures its distance from. */
	float PressTime = 0.f;

	/** Where the mouse last was during a scrub, key drag or track drag - what the view scrolls after at an edge. */
	TOptional<FVector2f> LastDragLocal;

	/** A scrub, key drag or track drag carried to the mouse at Local - on a move, or after the view scrolled under it. */
	void ApplyDragAt(const FVector2f& Local, float Width);

	/** What a press on a section will do once it moves far enough to be a drag. */
	ELayerDrag PendingLayerDrag = ELayerDrag::Move;

	/**
	 * A plain press on a layer that is one of several selected keeps them all selected, so they can be dragged
	 * together. Released without dragging, it was a click: the selection becomes just that layer.
	 */
	bool bCollapseSelectionOnRelease = false;

	/** Where the pressed key sat relative to the mouse, so a drag doesn't make the key jump under the cursor. */
	float PressKeyOffset = 0.f;

	/** What is under the mouse, while nothing is being dragged. Keys look and behave like buttons: hovered is drawn lighter. */
	FHit Hover;

	/** Where the mouse is across the tracks, for the faint hover-time line. Negative when it isn't over them. */
	float HoverX = -1.f;

	/** The line edge under the mouse - see RowEdgeAt - and a resize of the lines in progress: the edge dragged, from where, from what height. */
	int32 HoverRowEdge = INDEX_NONE;
	int32 ResizeEdge = INDEX_NONE;
	float ResizeStartY = 0.f;
	float ResizeStartHeight = 0.f;

	/** What the tooltip shows: a track, or one of its keys. */
	int32 TipLayer = INDEX_NONE;
	int32 TipKey = INDEX_NONE;

	/** The selection box: where it was started and where the mouse is now, in local pixels, and the modifiers it started with. */
	FVector2f MarqueeStart = FVector2f::ZeroVector;
	FVector2f MarqueeEnd = FVector2f::ZeroVector;
	bool bMarqueeAdd = false;
	bool bMarqueeToggle = false;

	/**
	 * A plain press on one of several selected keys keeps them all selected, so they can be dragged together.
	 * Released without dragging, it was a click: the selection becomes just that key.
	 */
	bool bCollapseKeySelectionOnRelease = false;
};

void SWidgetAnimTimelineTracks::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SLeafWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	if (!Owner)
	{
		return;
	}

	LastSize = AllottedGeometry.GetLocalSize();
	const float PassSeconds = Owner->GetPassSeconds();
	UpdateView(PassSeconds, Owner->IsDragging());
	ClampScroll(LastSize.Y);

	// A scrub, key or track dragged to an edge - or past it - scrolls the view after it, faster the further out the
	// mouse is, and the drag follows the time now under the mouse. Held still at the edge, it keeps going.
	const bool bEdgeScrolling = (Drag == EDrag::Scrub || Drag == EDrag::Key || Drag == EDrag::Layer) && HasMouseCapture() && LastDragLocal.IsSet();
	if (bEdgeScrolling && LastSize.X > 1.f)
	{
		constexpr float EdgeZone = 24.f;
		const float Width = LastSize.X;
		float Push = 0.f;
		const float MouseX = LastDragLocal->X;
		if (MouseX < EdgeZone)
		{
			Push = -FMath::Min((EdgeZone - MouseX) / EdgeZone, 4.f);
		}
		else if (MouseX > Width - EdgeZone)
		{
			Push = FMath::Min((MouseX - (Width - EdgeZone)) / EdgeZone, 4.f);
		}

		if (Push != 0.f)
		{
			// A view's width a second at the edge itself, more past it. Not back past a little before the start.
			const float ViewWidth = ViewMax - ViewMin;
			float Shift = ViewWidth * Push * InDeltaTime;
			const float LowestMin = FMath::Min(ViewMin, -ViewWidth * 0.05f);
			if (ViewMin + Shift < LowestMin)
			{
				Shift = LowestMin - ViewMin;
			}

			// A scrub has nothing to reach past the end; a key or a track can be dragged on past it.
			const float HighestMax = FMath::Max(ViewMax, FMath::Max(PassSeconds, WidgetAnimTimeline::MinVisibleSeconds) * 1.08f);
			if (Drag == EDrag::Scrub && ViewMax + Shift > HighestMax)
			{
				Shift = HighestMax - ViewMax;
			}

			if (!FMath::IsNearlyZero(Shift))
			{
				ViewMin += Shift;
				ViewMax += Shift;
				bUserView = true;
				ApplyDragAt(LastDragLocal.GetValue(), Width);
			}
		}
	}

	// The horizontal scroll bar follows the view: the whole animation, with the view's stretch of it as the thumb.
	if (HorizontalScrollBar.IsValid())
	{
		float RangeMin = 0.f;
		float RangeMax = 1.f;
		GetScrollRange(PassSeconds, RangeMin, RangeMax);
		const float Range = FMath::Max(RangeMax - RangeMin, KINDA_SMALL_NUMBER);
		HorizontalScrollBar->SetState((ViewMin - RangeMin) / Range, FMath::Clamp((ViewMax - ViewMin) / Range, 0.f, 1.f));
	}
}

int32 SWidgetAnimTimelineTracks::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetAnimTimeline;

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* Flat = WhiteBrush();

	const float RulerHeight = SWidgetAnimTimeline::RulerHeight;
	const float RowHeight = GetRowHeight();

	auto Box = [&](float X, float Y, float W, float H, const FLinearColor& Color, int32 Layer, const FSlateBrush* Brush = nullptr)
	{
		if (W <= 0.f || H <= 0.f)
		{
			return;
		}

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
			Brush ? Brush : Flat,
			ESlateDrawEffect::None,
			Color);
	};

	auto Text = [&](const FString& String, float X, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(FVector2f(600.f, 20.f), FSlateLayoutTransform(FVector2f(X, Y))),
			String,
			Font,
			ESlateDrawEffect::None,
			Color);
	};

	auto Diamond = [&](float X, float Y, float DiamondSize, const FLinearColor& Color, int32 Layer)
	{
		const float Half = DiamondSize * 0.5f;
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(DiamondSize, DiamondSize),
				FSlateLayoutTransform(FVector2f(X - Half, Y - Half)),
				FSlateRenderTransform(FQuat2f(FMath::DegreesToRadians(45.f))),
				FVector2f(0.5f, 0.5f)),
			Flat,
			ESlateDrawEffect::None,
			Color);
	};

	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	auto MeasureText = [&Measure](const FString& String, const FSlateFontInfo& Font) { return FVector2f(Measure->Measure(String, Font)); };

	// Layers, bottom to top, one for every pass that has to sit over the one before it. Slate only promises the
	// order of different layers - within one layer it batches elements by brush and draws each batch whole - so
	// no two passes of a section share a layer.
	const int32 BackgroundLayer = LayerId;
	const int32 RowLayer = LayerId + 1;
	const int32 GridLayer = LayerId + 2;
	const int32 HaloLayer = LayerId + 3;
	const int32 OutlineLayer = LayerId + 4;
	const int32 BackingLayer = LayerId + 5;
	const int32 FrameLayer = LayerId + 6;
	const int32 BodyLayer = LayerId + 7;
	const int32 GlossLayer = LayerId + 8;
	const int32 SpillLayer = LayerId + 9;
	const int32 SheenLayer = LayerId + 10;
	const int32 SectionOverlayLayer = LayerId + 11;
	const int32 TextShadowLayer = LayerId + 12;
	const int32 KeyGlowLayer = LayerId + 14;
	const int32 KeyOutlineLayer = LayerId + 15;
	const int32 KeyFillLayer = LayerId + 16;
	const int32 LabelLayer = LayerId + 17;
	const int32 RulerLayer = LayerId + 19;
	const int32 RangeLayer = LayerId + 21;
	const int32 PlayheadLayer = LayerId + 22;

	// Track field on the recessed color, as Sequencer's is.
	Box(0.f, 0.f, Size.X, Size.Y, RecessedColor(), BackgroundLayer);

	if (!Owner)
	{
		Box(0.f, 0.f, Size.X, RulerHeight, HeaderColor(), RulerLayer);
		return RulerLayer;
	}

	LastSize = Size;
	const float PassSeconds = Owner->GetPassSeconds();
	UpdateView(PassSeconds, Owner->IsDragging());
	ClampScroll(Size.Y);

	const TArray<FTrackLine>& Lines = Owner->GetTrackLines();
	const TArray<FVector2f>& Spans = Owner->GetTrackSpans();
	const SWidgetAnimTimeline::FSelection& Selection = Owner->GetSelection();
	const TArray<FWidgetAnimLayer>* Layers = Owner->GetLayers();
	const bool bReadOnly = Owner->IsReadOnly();

	const float StartX = TimeToX(0.f, Size.X);
	const float EndX = TimeToX(PassSeconds, Size.X);
	const float RowsBottom = FMath::Min(GetLineTop(Lines.Num()), Size.Y);

	// Section text is the graph node title's - the macro node's - read from the editor's own style rather than
	// copied from it: the same font, color and drop shadow, whatever this engine version makes them.
	const FTextBlockStyle& NodeTitleStyle = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("Graph.Node.NodeTitle");
	const FSlateFontInfo& SectionFont = NodeTitleStyle.Font;

	// Text on a track: a slight drop shadow under it, for legibility over the gloss and the track's color, in the
	// same font and size. Its color and offset are TrackTextShadow and TrackTextShadowOffset in NameTBDColors.h.
	// Text slanted like italics, for a font with no italic typeface of its own: sheared about its line's middle, so
	// its tops lean right and it stays where upright text would be.
	auto SlantedText = [&](const FString& String, float X, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, int32 Layer)
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			Layer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(600.f, 20.f),
				FSlateLayoutTransform(FVector2f(X, Y)),
				FSlateRenderTransform(FMatrix2x2f(1.f, 0.f, -Palette::TrackTextSlant, 1.f)),
				FVector2f(0.f, 0.5f)),
			String,
			Font,
			ESlateDrawEffect::None,
			Color);
	};

	// Text on a track: a slight drop shadow under it, for legibility over the gloss and the track's color, in the
	// same font and size. Its color and offset are TrackTextShadow and TrackTextShadowOffset in NameTBDColors.h.
	// bSlant draws it slanted - a hidden track's text, in a font without an italic typeface - see ItalicOf.
	auto TrackText = [&](const FString& String, float X, float Y, const FSlateFontInfo& Font, const FLinearColor& Color, bool bSlant = false)
	{
		const FLinearColor ShadowColor = WithAlpha(Palette::TrackTextShadow, Palette::TrackTextShadow.A * Color.A);
		if (bSlant)
		{
			SlantedText(String, X + Palette::TrackTextShadowOffset, Y + Palette::TrackTextShadowOffset, Font, ShadowColor, TextShadowLayer);
			SlantedText(String, X, Y, Font, Color, TextShadowLayer + 1);
			return;
		}
		Text(String, X + Palette::TrackTextShadowOffset, Y + Palette::TrackTextShadowOffset, Font, ShadowColor, TextShadowLayer);
		Text(String, X, Y, Font, Color, TextShadowLayer + 1);
	};

	// A font's italic: its own italic typeface when its font family has one ("Bold" -> "BoldItalic", "Regular" ->
	// "Italic"), in the same size. False when it has none, and the text is slanted instead - see SlantedText.
	auto ItalicOf = [](const FSlateFontInfo& Font, FSlateFontInfo& OutFont)
	{
		OutFont = Font;
		const FCompositeFont* Composite = Font.GetCompositeFont();
		if (!Composite)
		{
			return false;
		}

		const FString Base = Font.TypefaceFontName.IsNone() || Font.TypefaceFontName == TEXT("Regular") ? FString() : Font.TypefaceFontName.ToString();
		const FName Wanted(*(Base + TEXT("Italic")));
		for (const FTypefaceEntry& Entry : Composite->DefaultTypeface.Fonts)
		{
			if (Entry.Name == Wanted)
			{
				OutFont.TypefaceFontName = Wanted;
				return true;
			}
		}
		return false;
	};

	// The font a track's text is drawn in to fit Available pixels: its own, or - zoomed out, with Shrink To Fit in the
	// editor preferences - a smaller one, down to half its size. False when it doesn't fit either way; OutSize is what
	// it measures in the font picked.
	const bool bShrinkTrackText = UWidgetAnimEditorSettings::Get().TrackTextZoomOut == EWidgetAnimTrackTextZoomOut::Shrink;
	auto FitTrackText = [&](const FString& String, const FSlateFontInfo& Font, float Available, FSlateFontInfo& OutFont, FVector2f& OutSize)
	{
		OutFont = Font;
		OutSize = MeasureText(String, Font);
		if (OutSize.X <= Available)
		{
			return true;
		}
		if (!bShrinkTrackText || Available <= 0.f || OutSize.X <= 0.f)
		{
			return false;
		}

		// Glyph widths don't scale exactly with the size - hinting, rounding to whole pixels - so the first guess is
		// measured again and nudged down until it fits.
		float Scale = Available / OutSize.X;
		for (int32 Attempt = 0; Attempt < 4 && Scale >= Palette::TrackTextMinShrink; ++Attempt)
		{
			OutFont.Size = Font.Size * Scale;
			OutSize = MeasureText(String, OutFont);
			if (OutSize.X <= Available)
			{
				return true;
			}
			Scale *= 0.92f;
		}
		return false;
	};

	// Keys scale a little with the line height, so tall lines aren't sparse and short ones aren't crowded. A
	// group line's summary keys are drawn a size down from those, never bigger.
	const float KeyDiamond = FMath::Clamp(RowHeight * Palette::KeySizeRatio, Palette::KeySizeMin, Palette::KeySizeMax);
	const float SummaryDiamond = FMath::Min(FMath::Max(KeyDiamond * 0.85f, 4.f), KeyDiamond);

	// ---- Lines, clipped to below the ruler so they scroll under it -----------------------------------------

	const FGeometry RowsGeometry = AllottedGeometry.MakeChild(
		FVector2f(Size.X, FMath::Max(Size.Y - RulerHeight, 0.f)),
		FSlateLayoutTransform(FVector2f(0.f, RulerHeight)));
	OutDrawElements.PushClip(FSlateClippingZone(RowsGeometry));

	auto IsLineVisible = [&](int32 LineIndex)
	{
		const float Top = GetLineTop(LineIndex);
		return Top + RowHeight >= RulerHeight && Top <= Size.Y;
	};

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		if (!IsLineVisible(LineIndex))
		{
			continue;
		}

		const FTrackLine& Line = Lines[LineIndex];
		const float Y = GetLineTop(LineIndex);
		const bool bGroupLine = Line.Kind == FTrackLine::EKind::Group;

		// Group lines read as headers, like Sequencer's folder rows; track lines alternate very gently.
		const FLinearColor Base = bGroupLine
			? HeaderColor()
			: (Line.Ordinal % 2 == 0 ? BackgroundColor() : FMath::Lerp(BackgroundColor(), RecessedColor(), 0.5f));
		Box(0.f, Y, Size.X, RowHeight, Base, RowLayer);

		const bool bGroupSelected = bGroupLine && Selection.Group == Line.Group && Selection.Layer == INDEX_NONE;
		bool bDropTarget = false;
		for (const int32 LayerIndex : Line.Layers)
		{
			bDropTarget |= Owner->IsDraggingLayers() && Owner->IsLayerBeingDragged(LayerIndex);
		}

		if (bGroupSelected)
		{
			Box(0.f, Y, Size.X, RowHeight, WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha), RowLayer);
		}
		else if (bDropTarget)
		{
			Box(0.f, Y, Size.X, RowHeight, Palette::LineDropTarget, RowLayer);
		}
		else if (LineIndex == Hover.Line && Drag == EDrag::None)
		{
			Box(0.f, Y, Size.X, RowHeight, WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha * 0.5f), RowLayer);
		}

		// A thin line under every line, as between Sequencer's tracks, and the group's color down its left edge.
		Box(0.f, Y + RowHeight - 1.f, Size.X, 1.f, SeparatorColor, RowLayer);
		Box(0.f, Y, GroupBandWidth, RowHeight, SWidgetAnimTimeline::GetGroupColor(Line.Group), GridLayer);
	}

	// The animation the list plays within: its tracks, grayed and out of reach, on rows of their own above the list's,
	// so the list's tracks can be placed against it. Nothing here can be selected, dragged or edited.
	const int32 NumContextRows = GetNumContextRows();
	const TArray<FWidgetAnimLayer>* ContextLayers = Owner->GetContextLayers();
	if (NumContextRows > 0 && ContextLayers)
	{
		for (int32 Row = 0; Row < NumContextRows; ++Row)
		{
			const float Y = GetContextRowTop(Row);
			if (Y + RowHeight >= RulerHeight && Y <= Size.Y)
			{
				Box(0.f, Y, Size.X, RowHeight, Palette::ContextRowBackground, RowLayer);
				Box(0.f, Y + RowHeight - 1.f, Size.X, 1.f, Row == NumContextRows - 1 ? Palette::ContextRowsDivider : SeparatorColor, RowLayer);
			}
		}

		const FSlateFontInfo ContextFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
		const TArray<FVector2f>& ContextSpans = Owner->GetContextSpans();
		for (int32 Index = 0; Index < ContextSpans.Num() && ContextLayers->IsValidIndex(Index); ++Index)
		{
			const float Y = GetContextRowTop(Owner->GetContextRow(Index));
			if (Y + RowHeight < RulerHeight || Y > Size.Y)
			{
				continue;
			}

			const FVector2f Span = ContextSpans[Index];
			const float X0 = TimeToX(Span.X, Size.X);
			const float X1 = TimeToX(Span.X + Span.Y, Size.X);
			const float BarWidth = FMath::Max(X1 - X0, 2.f);
			const FWidgetAnimLayer& ContextLayer = (*ContextLayers)[Index];
			const FLinearColor BarColor = SWidgetAnimTimeline::GetLockedColor(SWidgetAnimTimeline::GetLayerTypeColor(ContextLayer)).CopyWithNewOpacity(Palette::ContextTrackOpacity);
			Box(X0, Y + 4.f, BarWidth, RowHeight - 8.f, BarColor, FrameLayer, RoundedFill(3.f));

			const FString ContextLabel = SWidgetAnimTimeline::GetLayerTypeName(ContextLayer).ToString();
			const FVector2f ContextLabelSize = MeasureText(ContextLabel, ContextFont);
			if (ContextLabelSize.X + 10.f <= BarWidth)
			{
				Text(ContextLabel, X0 + 5.f + Palette::TrackTextShadowOffset, Y + (RowHeight - ContextLabelSize.Y) * 0.5f + Palette::TrackTextShadowOffset, ContextFont,
					WithAlpha(Palette::TrackTextShadow, Palette::TrackTextShadow.A * Palette::ContextTrackText.A), LabelLayer - 1);
				Text(ContextLabel, X0 + 5.f, Y + (RowHeight - ContextLabelSize.Y) * 0.5f, ContextFont, Palette::ContextTrackText, LabelLayer);
			}
		}
	}

	// Outside the animation - before its start, after its end - is shaded, the way the native track area
	// shades outside the playback range.
	Box(0.f, RulerHeight, StartX, RowsBottom - RulerHeight, Palette::OutOfRangeShade, GridLayer);
	Box(EndX, RulerHeight, Size.X - EndX, RowsBottom - RulerHeight, Palette::OutOfRangeShade, GridLayer);

	// Grid down the lines, stepped by index rather than by adding floats, so it never drifts off the ruler's labels.
	const float PixelsPerSecond = Size.X / FMath::Max(ViewMax - ViewMin, KINDA_SMALL_NUMBER);
	const float Step = ChooseTickStep(PixelsPerSecond, 72.f);
	const int32 MinorPerMajor = 5;
	const float MinorStep = Step / MinorPerMajor;
	const int64 FirstTick = FMath::FloorToInt64(ViewMin / MinorStep);

	for (int64 TickIndex = FirstTick; TickIndex * MinorStep <= ViewMax; ++TickIndex)
	{
		const float X = TimeToX(TickIndex * MinorStep, Size.X);
		const bool bMajor = ((TickIndex % MinorPerMajor) + MinorPerMajor) % MinorPerMajor == 0;
		Box(X, RulerHeight, 1.f, RowsBottom - RulerHeight, bMajor ? Palette::GridMajor : Palette::GridMinor, GridLayer);
	}

	// ---- Group lines: name, span and key summary ----------------------------------------------------------

	const FSlateFontInfo GroupFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo GroupTargetFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FTrackLine& Line = Lines[LineIndex];
		if (Line.Kind != FTrackLine::EKind::Group || !IsLineVisible(LineIndex))
		{
			continue;
		}

		const float LineTop = GetLineTop(LineIndex);
		const float CenterY = LineTop + RowHeight * 0.5f;
		const FVector2f GroupSpan = Owner->GetGroupSpan(Line.Group);
		const FLinearColor GroupColor = SWidgetAnimTimeline::GetGroupColor(Line.Group);

		if (GroupSpan.Y >= GroupSpan.X)
		{
			// The stretch the group is moving: a slim rounded bar in its (muted) color, as a parent row
			// summarizes its children in Sequencer.
			const float X0 = TimeToX(GroupSpan.X, Size.X);
			const float X1 = TimeToX(GroupSpan.Y, Size.X);
			Box(X0, CenterY - 3.f, FMath::Max(X1 - X0, 4.f), 6.f, bReadOnly ? SWidgetAnimTimeline::GetLockedColor(GroupColor) : GroupColor, FrameLayer, RoundedFill(2.f));

			// Every key of the group's layers, light and dark-outlined so they read on any group color.
			for (int32 LayerIndex = 0; Layers && LayerIndex < Layers->Num() && Spans.IsValidIndex(LayerIndex); ++LayerIndex)
			{
				if (Owner->GetLayerGroup(LayerIndex) != Line.Group)
				{
					continue;
				}

				TArray<float> KeyTimes;
				Owner->GetKeyTimes(LayerIndex, KeyTimes);
				for (const float KeyTime : KeyTimes)
				{
					const float X = TimeToX(Spans[LayerIndex].X + KeyTime, Size.X);
					Diamond(X, CenterY, SummaryDiamond + 2.f, Palette::KeySummaryOutline, KeyOutlineLayer);
					Diamond(X, CenterY, SummaryDiamond, Palette::KeySummary, KeyFillLayer);
				}
			}
		}

		// Embedded in the timeline rather than pinned to the panel: a small fold arrow just before the group's
		// bar, and its name - and the widget it animates - just after it, quiet, with no backing of their own.
		// Both travel with the bar as the view pans and zooms.
		const bool bFoldHovered = Hover.Line == LineIndex && Hover.bOnFold && Drag == EDrag::None;
		if (const FSlateBrush* Arrow = Owner->IsTrackGroupCollapsed(Line.Group)
			? PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" })
			: PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" }))
		{
			Box(GetFoldX(Line.Group, Size.X), CenterY - FoldArrowSize * 0.5f, FoldArrowSize, FoldArrowSize,
				WithAlpha(ForegroundColor(), bFoldHovered ? 0.95f : 0.45f), LabelLayer + 1, Arrow);
		}

		const FString Name = Owner->GetGroupName(Line.Group).ToString();
		const FString Target = FString::Printf(TEXT("  %s"), *Owner->GetGroupTargetLabel(Line.Group).ToString());
		const FVector2f NameSize = MeasureText(Name, GroupFont);
		const FVector2f TargetSize = MeasureText(Target, GroupTargetFont);
		const float NameX = GroupSpan.Y >= GroupSpan.X
			? TimeToX(GroupSpan.Y, Size.X) + 8.f
			: GetFoldX(Line.Group, Size.X) + FoldArrowSize + 4.f;
		Text(Name, NameX, CenterY - NameSize.Y * 0.5f, GroupFont, WithAlpha(ForegroundColor(), 0.7f), LabelLayer + 1);
		Text(Target, NameX + NameSize.X, CenterY - TargetSize.Y * 0.5f, GroupTargetFont, WithAlpha(ForegroundColor(), 0.4f), LabelLayer + 1);
	}

	// ---- Track lines: sections ----------------------------------------------------------------------------

	const FSlateBrush* LockBrush = PickBrush({ "Icons.Lock", "PropertyWindow.Locked" });
	const FSlateBrush* HiddenBrush = PickBrush({ "Icons.Hidden", "Level.NotVisibleIcon16x" });

	for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FTrackLine& Line = Lines[LineIndex];
		if (Line.Kind != FTrackLine::EKind::Lane || !IsLineVisible(LineIndex))
		{
			continue;
		}

		for (const int32 LayerIndex : Line.Layers)
		{
			if (!Layers || !Layers->IsValidIndex(LayerIndex) || !Spans.IsValidIndex(LayerIndex))
			{
				continue;
			}

			const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
			const FVector2f Span = Spans[LayerIndex];
			const FBox2f Rect = GetSectionRect(LayerIndex, Size.X);
			const float BarX0 = Rect.Min.X;
			const float BarTop = Rect.Min.Y;
			const float BarWidth = Rect.Max.X - Rect.Min.X;
			const float BarHeight = Rect.Max.Y - Rect.Min.Y;

			// A stagger reaches on past its first item, to where its last item ends: a tail drawn after the section.
			const float FanSeconds = Owner->GetLayerFanSeconds(LayerIndex);
			const float TailRight = FanSeconds > 0.f ? TimeToX(Span.X + Span.Y + FanSeconds, Size.X) : BarX0 + BarWidth;

			// Nowhere near the view: nothing of it to draw.
			if (BarX0 > Size.X + 8.f || FMath::Max(BarX0 + BarWidth, TailRight) < -8.f)
			{
				continue;
			}

			const bool bLayerSelected = Owner->IsLayerSelected(LayerIndex);
			const bool bTrackLocked = Owner->IsLayerLocked(LayerIndex);
			const bool bTrackHidden = Owner->IsLayerHidden(LayerIndex);

			// A hidden track's text is in italics - the font's italic typeface, or slanted where it has none.
			FSlateFontInfo LayerFont = SectionFont;
			const bool bSlantLayerText = bTrackHidden && !ItalicOf(SectionFont, LayerFont);
			const bool bWashedOut = bReadOnly || bTrackLocked || bTrackHidden;
			const bool bBarHovered = Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.Key == INDEX_NONE && Hover.Edge == EEdge::None && Hover.bOnSection;

			FLinearColor TypeColor = SWidgetAnimTimeline::GetLayerTypeColor(Layer);
			if (bWashedOut)
			{
				TypeColor = SWidgetAnimTimeline::GetLockedColor(TypeColor);
			}

			// The stagger's tail: a thinner band of the track's color from the end of its first item to the end of its
			// last, with a tick where each item after the first starts - when the preview has counted them. The ticks
			// are left out once they would crowd into a smear.
			if (TailRight > BarX0 + BarWidth + 0.5f)
			{
				const float TailHeight = BarHeight * Palette::SectionStaggerTailHeight;
				const float TailTop = BarTop + (BarHeight - TailHeight) * 0.5f;
				const float TailLeft = BarX0 + BarWidth;
				Box(TailLeft, TailTop, TailRight - TailLeft, TailHeight, WithAlpha(TypeColor, Palette::SectionStaggerTailOpacity), BackingLayer, RoundedFill(2.f));

				const FWidgetAnimExpander* Expander = Layer.Expander.GetPtr<FWidgetAnimExpander>();
				const int32 NumItems = Owner->GetLayerFanCount(LayerIndex);
				const float PixelsPerItem = (TailRight - BarX0) / FMath::Max(NumItems, 1);
				if (Expander && NumItems > 1 && PixelsPerItem >= 3.f)
				{
					TArray<float> Offsets;
					Expander->ComputeOffsets(NumItems, Offsets);
					const float TimeScale = Owner->GetTimeScale();
					for (const float Offset : Offsets)
					{
						if (Offset > 0.f)
						{
							const float TickX = TimeToX(Span.X + Offset * TimeScale, Size.X);
							Box(TickX, TailTop, 1.f, TailHeight, Palette::SectionStaggerTick, BackingLayer + 1);
						}
					}
				}

				// What it fans out to, and how many, on the tail - where there's room for it.
				if (Expander)
				{
					const FString Summary = Expander->GetSummary();
					const FString TailLabel = NumItems != INDEX_NONE ? FString::Printf(TEXT("%s \u00D7%d"), *Summary, NumItems) : Summary;
					FSlateFontInfo TailFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
					const bool bSlantTail = bTrackHidden && !ItalicOf(FCoreStyle::GetDefaultFontStyle("Regular", 8), TailFont);
					const FVector2f TailLabelSize = MeasureText(TailLabel, TailFont);
					if (!TailLabel.IsEmpty() && TailLabelSize.X + 8.f <= TailRight - TailLeft)
					{
						// On the dark field beside the track rather than on it, so always light text over a dark shadow.
						const FLinearColor TailColor = WithAlpha(ForegroundColor(), 0.75f);
						const float TailX = TailLeft + 4.f;
						const float TailY = BarTop + (BarHeight - TailLabelSize.Y) * 0.5f;
						const FLinearColor TailShadow = WithAlpha(Palette::TrackTextShadow, Palette::TrackTextShadow.A * TailColor.A);
						if (bSlantTail)
						{
							SlantedText(TailLabel, TailX + Palette::TrackTextShadowOffset, TailY + Palette::TrackTextShadowOffset, TailFont, TailShadow, LabelLayer - 1);
							SlantedText(TailLabel, TailX, TailY, TailFont, TailColor, LabelLayer);
						}
						else
						{
							Text(TailLabel, TailX + Palette::TrackTextShadowOffset, TailY + Palette::TrackTextShadowOffset, TailFont, TailShadow, LabelLayer - 1);
							Text(TailLabel, TailX, TailY, TailFont, TailColor, LabelLayer);
						}
					}
				}
			}

			// A section, drawn the way the editor draws a graph node's title: a dark rounded frame, an opaque body,
			// and on the body the node gloss, the layer color spilled over it, and a highlight sheen along the
			// top. The gloss and spill textures are partly transparent; the body is what shows through them, the
			// same on every section. Selection is the orange outline of a selected Blueprint node; hover lifts
			// the color a touch.
			const float Lip = FMath::Clamp(BarHeight * Palette::SectionLipRatio, Palette::SectionLipMin, Palette::SectionLipMax);
			const float Frame = Palette::SectionFrameThickness;
			const float InnerX = BarX0 + Frame;
			const float InnerTop = BarTop + Frame;
			const float InnerWidth = BarWidth - Frame * 2.f;
			const float InnerHeight = BarHeight - Frame - Lip;

			if (bLayerSelected)
			{
				const float Rim = Palette::SectionSelectedOutlineSize;
				const float Halo = Rim + Palette::SectionSelectedHaloSize;
				Box(BarX0 - Halo, BarTop - Halo, BarWidth + Halo * 2.f, BarHeight + Halo * 2.f, Palette::SectionSelectedHalo, HaloLayer, RoundedFill(3.f + Halo));
				Box(BarX0 - Rim, BarTop - Rim, BarWidth + Rim * 2.f, BarHeight + Rim * 2.f, Palette::SectionSelectedOutline, OutlineLayer, RoundedFill(3.f + Rim));
			}

			Box(BarX0 + Palette::SectionBleedWidth, BarTop + Palette::SectionBleedWidth,
				BarWidth - Palette::SectionBleedWidth * 2.f, BarHeight - Palette::SectionBleedWidth * 2.f,
				Palette::SectionBacking, BackingLayer, RoundedFill());

			Box(BarX0, BarTop, BarWidth, BarHeight, Palette::SectionFrame, FrameLayer, RoundedFill());

			const bool bValueDriven = Layer.IsValueDriven();
			const bool bDerivedLength = SWidgetAnimTimeline::HasDerivedLength(Layer);
			const bool bStriped = bValueDriven || bDerivedLength;
			if (InnerWidth > 0.f && InnerHeight > 0.f)
			{
				const FLinearColor Spill = WithAlpha(bBarHovered && !bWashedOut ? TypeColor * Palette::SectionHoverLift : TypeColor, bStriped ? Palette::SectionValueSpillOpacity : 1.f);

				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Palette::SectionBody, BodyLayer, RoundedFill(2.f));
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, bWashedOut ? SWidgetAnimTimeline::GetLockedColor(FLinearColor::White) : FLinearColor::White, GlossLayer, NodeTitleGloss());
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Spill, SpillLayer, NodeColorSpill());
				Box(InnerX, InnerTop, InnerWidth, FMath::Min(InnerHeight, Palette::SectionSheenHeight), FLinearColor::White, SheenLayer, NodeTitleHighlight());

				// The macro node's half-tone line: across the body, part way down, everything below is a shade darker - most
				// at the right end, fading out toward the middle, so the line disappears as it runs left. One gradient, so
				// the fade is smooth with no seams, clipped to the body so its rounded corners stay round. Over the sheen
				// rather than on its layer: Slate only promises the order of different layers.
				const float HalfToneTop = InnerTop + FMath::RoundToFloat(InnerHeight * Palette::SectionHalfToneLine);
				// In proportion to the track, as the node stretches its texture - capped only if SectionHalfToneMaxReach is set.
				const float HalfToneWidth = Palette::SectionHalfToneMaxReach > 0.f
					? FMath::Min(InnerWidth * Palette::SectionHalfToneReach, Palette::SectionHalfToneMaxReach)
					: InnerWidth * Palette::SectionHalfToneReach;
				const float HalfToneHeight = InnerTop + InnerHeight - HalfToneTop;
				if (Palette::SectionHalfTone.A > 0.f && HalfToneWidth >= 1.f && HalfToneHeight > 0.f)
				{
					const FGeometry HalfToneGeometry = AllottedGeometry.MakeChild(FVector2f(InnerWidth, InnerHeight), FSlateLayoutTransform(FVector2f(InnerX, InnerTop)));
					OutDrawElements.PushClip(FSlateClippingZone(HalfToneGeometry));

					// Stops along its width - Orient_Vertical puts them along X - enough to follow the falloff's curve.
					const float HalfToneOpacity = Palette::SectionHalfTone.A * (bWashedOut ? Palette::LockedBrightness : 1.f);
					constexpr int32 NumStops = 9;
					TArray<FSlateGradientStop> Stops;
					Stops.Reserve(NumStops);
					for (int32 Stop = 0; Stop < NumStops; ++Stop)
					{
						const float Along = static_cast<float>(Stop) / (NumStops - 1);
						const float Strength = FMath::Pow(Along, Palette::SectionHalfToneFalloff);
						Stops.Add(FSlateGradientStop(FVector2D(Along * HalfToneWidth, 0.f), WithAlpha(Palette::SectionHalfTone, HalfToneOpacity * Strength)));
					}

					FSlateDrawElement::MakeGradient(
						OutDrawElements,
						SectionOverlayLayer,
						AllottedGeometry.ToPaintGeometry(FVector2f(HalfToneWidth, HalfToneHeight), FSlateLayoutTransform(FVector2f(InnerX + InnerWidth - HalfToneWidth, HalfToneTop))),
						MoveTemp(Stops),
						Orient_Vertical,
						ESlateDrawEffect::None);

					OutDrawElements.PopClip();
				}

				// A value-driven track is striped, so its place and length on the timeline don't read as when it
				// plays: gameplay's value decides that. So is one whose length its widget decides - a text typed at its
				// Typing Speed - since its Duration means nothing. Diagonal bands, clipped to the body, only those in view.
				if (bStriped)
				{
					const FGeometry BodyGeometry = AllottedGeometry.MakeChild(FVector2f(InnerWidth, InnerHeight), FSlateLayoutTransform(FVector2f(InnerX, InnerTop)));
					OutDrawElements.PushClip(FSlateClippingZone(BodyGeometry));

					const float StripeLength = InnerHeight * UE_SQRT_2 + Palette::SectionValueStripeWidth * 2.f;
					const float FirstX = FMath::Max(InnerX - InnerHeight, -InnerHeight);
					const float LastX = FMath::Min(InnerX + InnerWidth, Size.X);
					const FLinearColor StripeColor = bWashedOut ? SWidgetAnimTimeline::GetLockedColor(Palette::SectionValueStripe) : Palette::SectionValueStripe;
					for (float StripeX = FirstX - FMath::Fmod(FirstX - InnerX, Palette::SectionValueStripeSpacing); StripeX < LastX; StripeX += Palette::SectionValueStripeSpacing)
					{
						// Each band is a thin upright box turned 45 degrees about its middle, which sits on the body's
						// vertical center - so it crosses the whole height whatever the row's height.
						FSlateDrawElement::MakeBox(
							OutDrawElements,
							SectionOverlayLayer,
							AllottedGeometry.ToPaintGeometry(
								FVector2f(Palette::SectionValueStripeWidth, StripeLength),
								FSlateLayoutTransform(FVector2f(StripeX + InnerHeight * 0.5f - Palette::SectionValueStripeWidth * 0.5f, InnerTop + InnerHeight * 0.5f - StripeLength * 0.5f)),
								FSlateRenderTransform(FQuat2f(FMath::DegreesToRadians(45.f))),
								FVector2f(0.5f, 0.5f)),
							Flat,
							ESlateDrawEffect::None,
							StripeColor);
					}

					OutDrawElements.PopClip();
				}
			}

			// The resize handle: a lit strip along the edge under the mouse, or the one being dragged.
			const EEdge LitEdge = GetLitEdge(LayerIndex);
			if (LitEdge != EEdge::None)
			{
				const float HandleX = LitEdge == EEdge::Start ? BarX0 : BarX0 + BarWidth - Palette::SectionEdgeHandleWidth;
				Box(HandleX, BarTop, Palette::SectionEdgeHandleWidth, BarHeight, Palette::SectionEdgeHandle, SectionOverlayLayer);
			}

			// A hidden track wears a closed eye at its start, a locked one a lock - both, if it is both - and its
			// text moves along to make room.
			const float Padding = Palette::SectionTextPadding;
			float TextLeft = BarX0 + Padding;
			float GlyphX = BarX0 + Padding * 0.5f;
			for (const FSlateBrush* Glyph : { bTrackHidden ? HiddenBrush : nullptr, bTrackLocked ? LockBrush : nullptr })
			{
				if (Glyph && GlyphX - BarX0 + Palette::LockedGlyphSize + Padding <= BarWidth)
				{
					const float GlyphSize = FMath::Min(Palette::LockedGlyphSize, InnerHeight);
					Box(GlyphX, InnerTop + (InnerHeight - GlyphSize) * 0.5f, GlyphSize, GlyphSize, Palette::LockedGlyph, SectionOverlayLayer, Glyph);
					GlyphX += GlyphSize + 3.f;
					TextLeft = GlyphX + Padding * 0.5f;
				}
			}

			// Every key of the track, in seconds from its start - its start and end among them on a keyed track.
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(LayerIndex, KeyTimes);

			// A track some of whose segments have curves of their own has no one easing to name: each segment names
			// its own curve instead, small, in the middle of its stretch - see below.
			const bool bLabelSegments = Owner->UsesSegmentCurves(LayerIndex) && Owner->HasSegmentCurves(LayerIndex) && KeyTimes.Num() >= 2;

			// Inside it: its type and length on the left, its easing - or Spring - on the right, in the graph node
			// title's own text style. Whichever doesn't fit is dropped, the easing first, so text never spills out.
			// On a plain list, a track moving another widget than the list's own says which.
			const bool bNamesTarget = Owner->GetSource() && Owner->GetSource()->SupportsLayerTargets() && !Layer.TargetWidget.IsNone();
			// A value-driven track says so in place of its length, which only times its preview sweep.
			// One whose length its widget decides says what decides it, beside the length the preview works out.
			FString LengthLabel = bValueDriven
				? LOCTEXT("ValueDrivenSectionLength", "(Value Driven)").ToString()
				: FString::Printf(TEXT("%.2fs"), Span.Y);
			if (bDerivedLength)
			{
				const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
				const FText DerivedLabel = Evaluator ? Evaluator->GetDerivedLengthLabel(Layer.TypeParams) : FText::GetEmpty();
				if (!DerivedLabel.IsEmpty())
				{
					LengthLabel = FString::Printf(TEXT("%s (%s)"), *LengthLabel, *DerivedLabel.ToString());
				}
			}
			const FString Label = bNamesTarget
				? FString::Printf(TEXT("%s \u2192 %s  %s"), *SWidgetAnimTimeline::GetLayerTypeName(Layer).ToString(), *Layer.TargetWidget.ToString(), *LengthLabel)
				: FString::Printf(TEXT("%s  %s"), *SWidgetAnimTimeline::GetLayerTypeName(Layer).ToString(), *LengthLabel);
			const FString EasingLabel = SWidgetAnimTimeline::GetEasingName(Layer).ToString();
			const FVector2f EasingSize = MeasureText(EasingLabel, LayerFont);
			const float TextOpacity = bWashedOut ? Palette::LockedBrightness + 0.2f : 1.f;
			const FLinearColor TitleColor = NodeTitleStyle.ColorAndOpacity.GetSpecifiedColor() * FLinearColor(1.f, 1.f, 1.f, TextOpacity);

			// At its own size when it fits; zoomed out, shrunk to fit or left out - see FitTrackText.
			FSlateFontInfo TitleFont;
			FVector2f LabelSize;
			const bool bTitleShown = FitTrackText(Label, LayerFont, BarWidth - (TextLeft - BarX0) - Padding, TitleFont, LabelSize);
			const bool bTitleFullSize = bTitleShown && TitleFont.Size == LayerFont.Size;
			const float TextY = InnerTop + (InnerHeight - LabelSize.Y) * 0.5f;
			// The full-size title's line, which the segment names and the easing sit on, even while the title is shrunk.
			const float FullTextY = InnerTop + (InnerHeight - MeasureText(Label, LayerFont).Y) * 0.5f;
			if (bTitleShown)
			{
				TrackText(Label, TextLeft, TextY, TitleFont, TitleColor, bSlantLayerText);
			}

			// Each segment's curve, named in the middle of its stretch - a segment with a curve of its own brighter
			// than one left at the track's Easing. Any that doesn't fit between its keys, clear of the title, is left
			// unnamed; its tooltip still lists every segment's curve.
			if (bLabelSegments)
			{
				FSlateFontInfo SegmentFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
				const bool bSlantSegmentText = bTrackHidden && !ItalicOf(FCoreStyle::GetDefaultFontStyle("Regular", 9), SegmentFont);

				// On the title's baseline, so the two read as one line of text. Centering each by its measured height
				// doesn't: a line's height includes the room below the baseline for descenders, so a smaller font
				// centered that way sits visibly higher than a larger one beside it.
				const float TitleBaselineY = FullTextY + Measure->GetMaxCharacterHeight(LayerFont) + Measure->GetBaseline(LayerFont);
				const float SegmentAscent = Measure->GetMaxCharacterHeight(SegmentFont) + Measure->GetBaseline(SegmentFont);
				const float TitleRight = bTitleShown ? TextLeft + LabelSize.X + 8.f : TextLeft;
				const float KeyClearance = KeyDiamond * 0.5f + 4.f;

				for (int32 Segment = 0; Segment + 1 < KeyTimes.Num(); ++Segment)
				{
					const FWidgetAnimSegmentCurve* Curve = Owner->GetSegmentCurve(LayerIndex, Segment);
					if (!Curve)
					{
						continue;
					}

					const FString CurveName = Curve->IsTrackEasing()
						? SWidgetAnimTimeline::GetEasingName(Layer).ToString()
						: SWidgetAnimTimeline::GetSegmentCurveName(*Curve).ToString();
					const FVector2f CurveNameSize = MeasureText(CurveName, SegmentFont);

					const float SegmentLeft = FMath::Max(TimeToX(Span.X + KeyTimes[Segment], Size.X) + KeyClearance, TitleRight);
					const float SegmentRight = FMath::Min(TimeToX(Span.X + KeyTimes[Segment + 1], Size.X) - KeyClearance, BarX0 + BarWidth - 4.f);
					if (SegmentRight - SegmentLeft < CurveNameSize.X)
					{
						continue;
					}

					const float CurveNameX = FMath::Clamp((SegmentLeft + SegmentRight - CurveNameSize.X) * 0.5f, SegmentLeft, SegmentRight - CurveNameSize.X);
					const float CurveNameY = TitleBaselineY - SegmentAscent;
					const float Opacity = TextOpacity * (Curve->IsTrackEasing() ? Palette::SegmentLabelTrackEasingOpacity : Palette::SegmentLabelOwnCurveOpacity);

					TrackText(CurveName, CurveNameX, CurveNameY, SegmentFont, FLinearColor(1.f, 1.f, 1.f, Opacity), bSlantSegmentText);
				}
			}

			// Only beside a title at its full size: once the title has to shrink, the easing gives way to it.
			if (!bLabelSegments && bTitleFullSize && TextLeft - BarX0 + LabelSize.X + EasingSize.X + Padding * 2.f + 12.f <= BarWidth)
			{
				TrackText(EasingLabel, BarX0 + BarWidth - Padding - EasingSize.X, TextY, LayerFont, TitleColor * FLinearColor(1.f, 1.f, 1.f, Palette::SectionSecondaryTextOpacity), bSlantLayerText);
			}

			// Keys: diamonds, button-like - normal, hovered (lighter, bigger), pressed (darker), selected (orange,
			// with a glow). A key that can't be edited is drawn grayed.
			const bool bKeysEditable = Owner->CanEditKeys(LayerIndex);
			const float KeyY = InnerTop + InnerHeight * 0.5f;

			for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
			{
				const float X = TimeToX(Span.X + KeyTimes[Key], Size.X);

				const bool bSelected = Owner->IsKeySelected(LayerIndex, Key);
				const bool bPressed = bKeysEditable && IsKeyPressed(LayerIndex, Key);
				const bool bHovered = !bPressed && Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.Key == Key;

				FLinearColor Color = bKeysEditable ? Palette::Key : Palette::KeyReadOnly;
				float Scale = 1.f;

				if (bSelected)
				{
					Color = Palette::KeySelected;
				}
				if (bHovered)
				{
					Color = bSelected ? Palette::KeySelected * 1.2f : (bKeysEditable ? Palette::KeyHovered : Palette::Key);
					Scale = 1.15f;
				}
				if (bPressed)
				{
					Color = (bSelected ? Palette::KeySelected : Palette::Key) * 0.7f;
					Scale = 0.95f;
				}
				Color.A = 1.f;

				if (bSelected)
				{
					Diamond(X, KeyY, KeyDiamond * Scale + 5.f, Palette::KeySelectedGlow, KeyGlowLayer);
				}
				Diamond(X, KeyY, KeyDiamond * Scale + 2.f, Palette::KeyOutline, KeyOutlineLayer);
				Diamond(X, KeyY, KeyDiamond * Scale, Color, KeyFillLayer);
			}
		}
	}

	// Where the drag snapped to, and where the mouse is in time - faintly, which makes lining a key up against
	// another line easier.
	if (const TOptional<float> Snap = Owner->GetSnapIndicator(); Snap.IsSet())
	{
		Box(TimeToX(Snap.GetValue(), Size.X), RulerHeight, 1.f, Size.Y - RulerHeight, Palette::SnapLine, RangeLayer);
	}
	if (HoverX >= 0.f && Drag == EDrag::None && HoverRowEdge == INDEX_NONE)
	{
		Box(HoverX, RulerHeight, 1.f, Size.Y - RulerHeight, Palette::HoverTimeLine, RangeLayer);
	}

	// The selection box being dragged out.
	if (Drag == EDrag::Marquee)
	{
		const FBox2f Marquee = GetMarqueeRect();
		const FVector2f MarqueeSize = Marquee.Max - Marquee.Min;
		Box(Marquee.Min.X, Marquee.Min.Y, MarqueeSize.X, MarqueeSize.Y, Palette::MarqueeFill, RangeLayer);

		// The outline as four 1px edges, a layer above the fill. Not a rounded outline brush: MakeBox tints a whole
		// rounded box - its inside included - by the color it is drawn with, so the outline's own opacity covered
		// the fill and the fill's opacity changed nothing.
		Box(Marquee.Min.X, Marquee.Min.Y, MarqueeSize.X, 1.f, Palette::MarqueeOutline, RangeLayer + 1);
		Box(Marquee.Min.X, Marquee.Max.Y - 1.f, MarqueeSize.X, 1.f, Palette::MarqueeOutline, RangeLayer + 1);
		Box(Marquee.Min.X, Marquee.Min.Y, 1.f, MarqueeSize.Y, Palette::MarqueeOutline, RangeLayer + 1);
		Box(Marquee.Max.X - 1.f, Marquee.Min.Y, 1.f, MarqueeSize.Y, Palette::MarqueeOutline, RangeLayer + 1);
	}

	OutDrawElements.PopClip();

	// ---- Ruler, pinned over the top -------------------------------------------------------------------------

	Box(0.f, 0.f, Size.X, RulerHeight, HeaderColor(), RulerLayer);
	Box(0.f, RulerHeight - 1.f, Size.X, 1.f, SeparatorColor, RulerLayer);

	const FSlateFontInfo RulerFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FLinearColor RulerText = WithAlpha(ForegroundColor(), 0.65f);
	const FLinearColor RulerTick = WithAlpha(ForegroundColor(), 0.35f);

	for (int64 TickIndex = FirstTick; TickIndex * MinorStep <= ViewMax; ++TickIndex)
	{
		const float Tick = TickIndex * MinorStep;
		const float X = TimeToX(Tick, Size.X);
		const bool bMajor = ((TickIndex % MinorPerMajor) + MinorPerMajor) % MinorPerMajor == 0;

		if (bMajor)
		{
			Box(X, RulerHeight - 9.f, 1.f, 9.f, RulerTick, RulerLayer + 1);
			Text(FormatSeconds(FMath::IsNearlyZero(Tick, MinorStep * 0.25f) ? 0.f : Tick, Step), X + 4.f, 4.f, RulerFont, RulerText, RulerLayer + 1);
		}
		else
		{
			Box(X, RulerHeight - 4.f, 1.f, 4.f, WithAlpha(RulerTick, 0.2f), RulerLayer + 1);
		}
	}

	// Start and end of the animation: green and red, through the ruler and the lines, as in the native editor.
	Box(StartX, 0.f, 1.f, Size.Y, Palette::RangeStart, RangeLayer);
	Box(EndX, 0.f, 1.f, Size.Y, Palette::RangeEnd, RangeLayer);
	Box(StartX, RulerHeight - 10.f, 5.f, 2.f, Palette::RangeStart, RangeLayer);
	Box(EndX - 4.f, RulerHeight - 10.f, 5.f, 2.f, Palette::RangeEnd, RangeLayer);

	if (const TOptional<float> Snap = Owner->GetSnapIndicator(); Snap.IsSet())
	{
		Box(TimeToX(Snap.GetValue(), Size.X) - 3.f, RulerHeight - 6.f, 7.f, 6.f, Palette::SnapLine, RangeLayer, RoundedFill(2.f));
	}

	// The playhead: a line through everything, and a red tag in the ruler reading the time - Sequencer's.
	const float PlayheadSeconds = Owner->GetPlayheadSeconds();
	const float PlayheadX = TimeToX(PlayheadSeconds, Size.X);
	Box(PlayheadX, RulerHeight, 1.f, Size.Y - RulerHeight, Palette::Playhead, PlayheadLayer);

	const FSlateFontInfo TagFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FString TagText = FString::Printf(TEXT("%.2f"), PlayheadSeconds);
	const FVector2f TagTextSize = MeasureText(TagText, TagFont);
	const float TagWidth = TagTextSize.X + 10.f;
	const float TagHeight = TagTextSize.Y + 3.f;
	const float TagX = FMath::Clamp(PlayheadX - TagWidth * 0.5f, 0.f, FMath::Max(Size.X - TagWidth, 0.f));
	const float TagY = RulerHeight - TagHeight - 3.f;
	Box(TagX, TagY, TagWidth, TagHeight, Palette::Playhead, PlayheadLayer, RoundedFill(2.f));
	Box(PlayheadX, TagY + TagHeight, 1.f, RulerHeight - (TagY + TagHeight), Palette::Playhead, PlayheadLayer);
	Text(TagText, TagX + 5.f, TagY + 1.5f, TagFont, Palette::PlayheadText, PlayheadLayer + 1);

	// When the lines run past the bottom: a slim bar down the right edge showing where they are scrolled to.
	const float MaxScroll = GetMaxScrollY(Size.Y);
	if (MaxScroll > 0.f && !(Owner && Owner->IsInline()))
	{
		const float TrackHeight = Size.Y - RulerHeight - 4.f;
		const float ContentHeight = TrackHeight + MaxScroll;
		const float ThumbHeight = FMath::Max(TrackHeight * TrackHeight / ContentHeight, 16.f);
		const float ThumbY = RulerHeight + 2.f + (TrackHeight - ThumbHeight) * (ScrollY / MaxScroll);
		Box(Size.X - 5.f, ThumbY, 3.f, ThumbHeight, Palette::ScrollIndicator, PlayheadLayer + 1, RoundedFill(2.f));
	}

	return PlayheadLayer + 1;
}

FReply SWidgetAnimTimelineTracks::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A new press: the edge scrolling waits for this drag's own first move.
	LastDragLocal.Reset();

	using FLineKind = FTrackLine::EKind;

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	if (!Owner)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Width = MyGeometry.GetLocalSize().X;
	PressPosition = Local;
	bCollapseSelectionOnRelease = false;
	bCollapseKeySelectionOnRelease = false;

	// Whatever the press does, the tooltip is in the way of it.
	FSlateApplication::Get().CloseToolTip();

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		// A right press becomes a pan if it moves, a context menu if it doesn't; a middle press is always a pan.
		Drag = MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton ? EDrag::Pan : EDrag::RightPending;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);

	if (Local.Y < SWidgetAnimTimeline::RulerHeight)
	{
		Drag = EDrag::Scrub;
		Owner->ScrubToSeconds(XToTime(Local.X, Width));
		return Reply;
	}

	// On a line's edge: resize every line, by dragging.
	const int32 Edge = RowEdgeAt(Local.Y);
	if (Edge != INDEX_NONE)
	{
		Drag = EDrag::ResizeRows;
		ResizeEdge = Edge;
		ResizeStartY = Local.Y;
		ResizeStartHeight = Owner->GetRowHeight();
		return Reply;
	}

	Drag = EDrag::None;
	const FHit Hit = HitTest(Local, Width);
	const FTrackLine* Line = GetLine(Hit.Line);

	const bool bRange = MouseEvent.IsShiftDown();
	const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();

	// Empty space - below the lines, or an empty stretch of a track line: a selection box, once the mouse moves.
	// Let go without moving, it was a click on nothing, which picks the animation itself.
	if (!Line || (Line->Kind == FLineKind::Lane && Hit.Layer == INDEX_NONE))
	{
		MarqueeStart = Local;
		MarqueeEnd = Local;
		bMarqueeAdd = bRange;
		bMarqueeToggle = bToggle;
		Drag = EDrag::MarqueePending;
		return Reply;
	}

	if (Line->Kind == FLineKind::Group)
	{
		if (Hit.bOnFold)
		{
			Owner->ToggleTrackGroupCollapsed(Line->Group);
		}
		else
		{
			Owner->SelectGroup(Line->Group);
		}
		return Reply;
	}

	// A key with Shift or Ctrl / Cmd: one more key, or one fewer - keys of any tracks, picked together.
	if (Hit.Key != INDEX_NONE && (bRange || bToggle))
	{
		Owner->SelectKeyWithModifiers(Hit.Layer, Hit.Key, bToggle);
		return Reply;
	}

	// Shift and Ctrl / Cmd pick layers the way a file browser picks files, and do nothing else.
	if (bRange || bToggle)
	{
		Owner->SelectLayerWithModifiers(Hit.Layer, bRange, bToggle);
		return Reply;
	}

	PressLayer = Hit.Layer;
	PressTime = XToTime(Local.X, Width);

	// An edge: resize. Whatever is selected resizes together, if this layer is part of it.
	if (Hit.Edge != EEdge::None)
	{
		if (!Owner->IsLayerSelected(Hit.Layer))
		{
			Owner->SelectLayer(Hit.Layer);
		}

		PendingLayerDrag = Hit.Edge == EEdge::Start ? ELayerDrag::ResizeStart : ELayerDrag::ResizeEnd;
		Drag = EDrag::LayerPending;
		return Reply;
	}

	// A key: select it, and drag it if it can be. One of several selected keys keeps them all selected, so they
	// can all be dragged together - unless it turns out to be a click.
	if (Hit.Key != INDEX_NONE)
	{
		PressKey = Hit.Key;

		if (Owner->IsKeyMultiSelection() && Owner->IsKeySelected(Hit.Layer, Hit.Key))
		{
			bCollapseKeySelectionOnRelease = true;
		}
		else
		{
			Owner->SelectKey(Hit.Layer, Hit.Key);
		}

		if (Owner->CanEditKeys(Hit.Layer))
		{
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(Hit.Layer, KeyTimes);

			PressKeyOffset = KeyTimes.IsValidIndex(Hit.Key)
				? (GetLayerSpan(Hit.Layer).X + KeyTimes[Hit.Key]) - XToTime(Local.X, Width)
				: 0.f;
			Drag = EDrag::KeyPending;
		}
		return Reply;
	}

	// The section itself: select it, and move it if it can be. One of several selected layers keeps the others
	// selected, so they can all be dragged together - unless it turns out to be a click.
	if (Owner->IsMultiSelection() && Owner->IsLayerSelected(Hit.Layer))
	{
		bCollapseSelectionOnRelease = true;
	}
	else
	{
		Owner->SelectLayer(Hit.Layer);
	}

	if (Hit.bOnSection && Owner->CanMoveLayer(Hit.Layer))
	{
		PendingLayerDrag = ELayerDrag::Move;
		Drag = EDrag::LayerPending;
	}

	return Reply;
}

void SWidgetAnimTimelineTracks::ApplyDragAt(const FVector2f& Local, float Width)
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	if (!Owner)
	{
		return;
	}

	switch (Drag)
	{
	case EDrag::Scrub:
		Owner->ScrubToSeconds(XToTime(Local.X, Width));
		break;

	case EDrag::Key:
		Owner->UpdateKeyDrag(XToTime(Local.X, Width) + PressKeyOffset - GetLayerSpan(PressLayer).X, GetSnapThresholdSeconds(Width));
		break;

	case EDrag::Layer:
	{
		// Up and down a whole line at a time, once the mouse is more than half a line away.
		const int32 DeltaLines = PendingLayerDrag == ELayerDrag::Move
			? FMath::RoundToInt((Local.Y - PressPosition.Y) / FMath::Max(GetRowHeight(), 1.f))
			: 0;
		Owner->UpdateLayerDrag(XToTime(Local.X, Width) - PressTime, DeltaLines, GetSnapThresholdSeconds(Width));
		break;
	}

	default:
		break;
	}
}

FReply SWidgetAnimTimelineTracks::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Width = MyGeometry.GetLocalSize().X;

	// Hover follows the mouse whenever nothing is being dragged: a line edge, then whatever HitTest finds.
	if (Drag == EDrag::None)
	{
		HoverRowEdge = RowEdgeAt(Local.Y);
		Hover = HoverRowEdge == INDEX_NONE ? HitTest(Local, Width) : FHit();
		HoverX = Local.Y >= SWidgetAnimTimeline::RulerHeight ? Local.X : -1.f;

		// The tooltip is about a key, or a track's body - not its edges, which are for resizing.
		if (Hover.Key != INDEX_NONE)
		{
			SetToolTipTarget(Hover.Layer, Hover.Key);
		}
		else if (Hover.bOnSection && Hover.Edge == EEdge::None)
		{
			SetToolTipTarget(Hover.Layer, INDEX_NONE);
		}
		else
		{
			SetToolTipTarget(INDEX_NONE, INDEX_NONE);
		}
	}

	if (!Owner || Drag == EDrag::None || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const bool bPastThreshold = FVector2f::Distance(Local, PressPosition) > WidgetAnimTimeline::DragThreshold;

	switch (Drag)
	{
	case EDrag::Scrub:
	case EDrag::Key:
	case EDrag::Layer:
		LastDragLocal = Local;
		ApplyDragAt(Local, Width);
		break;

	case EDrag::KeyPending:
		if (bPastThreshold)
		{
			// A drag, not a click: every selected key stays selected and moves together.
			bCollapseKeySelectionOnRelease = false;
			Owner->BeginKeyDrag(PressLayer, PressKey);
			Drag = Owner->IsDraggingKey() ? EDrag::Key : EDrag::None;
		}
		break;


	case EDrag::LayerPending:
		if (bPastThreshold)
		{
			// A drag, not a click: whatever was selected stays selected and moves together.
			bCollapseSelectionOnRelease = false;
			Drag = Owner->BeginLayerDrag(PressLayer, PendingLayerDrag) ? EDrag::Layer : EDrag::None;
		}
		break;

	case EDrag::RightPending:
		if (bPastThreshold)
		{
			Drag = EDrag::Pan;
		}
		break;

	case EDrag::MarqueePending:
		if (bPastThreshold)
		{
			Drag = EDrag::Marquee;
			MarqueeEnd = Local;
		}
		break;

	case EDrag::Marquee:
		MarqueeEnd = Local;
		break;

	case EDrag::ResizeRows:
	{
		// The dragged edge follows the mouse: the lines above it share the distance moved between them.
		const float NewHeight = ResizeStartHeight + (Local.Y - ResizeStartY) / FMath::Max(ResizeEdge, 1);
		Owner->SetRowHeight(NewHeight);
		break;
	}

	case EDrag::Pan:
	{
		// Along the timeline and down the lines, the way a canvas is dragged around.
		const float DeltaX = MouseEvent.GetCursorDelta().X;
		const float DeltaY = MouseEvent.GetCursorDelta().Y;
		const float SecondsPerPixel = (ViewMax - ViewMin) / FMath::Max(Width, 1.f);
		const float Shift = -DeltaX * SecondsPerPixel;
		ViewMin += Shift;
		ViewMax += Shift;
		bUserView = true;

		ScrollY -= DeltaY;
		ClampScroll(MyGeometry.GetLocalSize().Y);
		break;
	}

	default:
		break;
	}

	return FReply::Handled();
}

FReply SWidgetAnimTimelineTracks::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	LastDragLocal.Reset();

	using FLineKind = FTrackLine::EKind;

	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	const EDrag Ended = Drag;
	Drag = EDrag::None;

	if (Owner && Ended == EDrag::Key)
	{
		Owner->EndKeyDrag();
	}

	if (Owner && Ended == EDrag::Layer)
	{
		Owner->EndLayerDrag();
	}

	// Pressed on one of several selected layers and let go without dragging: a click, which selects it alone.
	if (Owner && bCollapseSelectionOnRelease && (Ended == EDrag::LayerPending || Ended == EDrag::None))
	{
		Owner->SelectLayer(PressLayer);
	}
	bCollapseSelectionOnRelease = false;

	// The same for one of several selected keys.
	if (Owner && bCollapseKeySelectionOnRelease && (Ended == EDrag::KeyPending || Ended == EDrag::None))
	{
		Owner->SelectKey(PressLayer, PressKey);
	}
	bCollapseKeySelectionOnRelease = false;

	// A selection box let go: whatever it took in. A press on empty space let go in place picks nothing - the
	// animation itself - unless it was meant to add to what is picked.
	if (Owner && Ended == EDrag::Marquee)
	{
		TArray<SWidgetAnimTimeline::FKeyRef> BoxKeys;
		TArray<int32> BoxLayers;
		CollectMarquee(MyGeometry.GetLocalSize().X, BoxKeys, BoxLayers);
		Owner->SelectInBox(BoxKeys, BoxLayers, bMarqueeAdd, bMarqueeToggle);
	}
	else if (Owner && Ended == EDrag::MarqueePending && !bMarqueeAdd && !bMarqueeToggle)
	{
		Owner->ClearSelection();
	}

	if (Owner && Ended == EDrag::RightPending)
	{
		const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float Width = MyGeometry.GetLocalSize().X;

		if (Local.Y >= SWidgetAnimTimeline::RulerHeight)
		{
			const FHit Hit = HitTest(Local, Width);
			const FTrackLine* Line = GetLine(Hit.Line);

			// Where it was clicked, so a track added from the menu starts right there.
			const TOptional<float> ClickSeconds(FMath::Max(XToTime(Local.X, Width), 0.f));

			if (!Line)
			{
				PushMenu(MouseEvent, Owner->MakeAddMenu(INDEX_NONE, /*bWithinGroup=*/false, ClickSeconds));
			}
			else if (Line->Kind == FLineKind::Group)
			{
				Owner->SelectGroup(Line->Group);
				PushMenu(MouseEvent, Owner->MakeGroupContextMenu(Line->Group, ClickSeconds));
			}
			else if (Hit.Layer == INDEX_NONE)
			{
				// An empty stretch of a group's line: add to that group, and only that group.
				PushMenu(MouseEvent, Owner->MakeAddMenu(Line->Group, /*bWithinGroup=*/true, ClickSeconds));
			}
			else
			{
				// A right-click on one of several selected layers - or keys - keeps them all, so the menu acts on all of them.
				if (Hit.Key != INDEX_NONE)
				{
					if (!Owner->IsKeySelected(Hit.Layer, Hit.Key))
					{
						Owner->SelectKey(Hit.Layer, Hit.Key);
					}
				}
				else if (!Owner->IsLayerSelected(Hit.Layer))
				{
					Owner->SelectLayer(Hit.Layer);
				}

				const TOptional<float> LocalSeconds = Hit.bOnSection
					? TOptional<float>(XToTime(Local.X, Width) - GetLayerSpan(Hit.Layer).X)
					: TOptional<float>();
				PushMenu(MouseEvent, Owner->MakeLayerContextMenu(Hit.Layer, Hit.Key, LocalSeconds, ClickSeconds));
			}
		}
	}

	PressKey = INDEX_NONE;
	PressLayer = INDEX_NONE;

	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
}

FReply SWidgetAnimTimelineTracks::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	if (!Owner || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Width = MyGeometry.GetLocalSize().X;
	const float Seconds = XToTime(Local.X, Width);

	// Anywhere: the playhead goes to that moment. On a keyed track's body, a new key goes there too. A group's
	// fold arrow has already done its job on the first click.
	// A click on the fold arrow soon after another click arrives here rather than as a press - which is what
	// made folding take two clicks - so it folds here too.
	const FHit Hit = HitTest(Local, Width);
	if (Hit.bOnFold)
	{
		if (const FTrackLine* Line = GetLine(Hit.Line))
		{
			Owner->ToggleTrackGroupCollapsed(Line->Group);
		}
		return FReply::Handled();
	}

	if (Hit.bOnSection && Hit.Key == INDEX_NONE && Hit.Edge == EEdge::None && Owner->CanEditKeys(Hit.Layer))
	{
		Owner->AddKey(Hit.Layer, Seconds - GetLayerSpan(Hit.Layer).X);
	}

	Owner->ScrubToSeconds(Seconds);
	return FReply::Handled();
}

FReply SWidgetAnimTimelineTracks::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Not mid-drag: the drag measures against the view it started in.
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
	if (Owner && Owner->IsDragging())
	{
		return FReply::Handled();
	}

	FSlateApplication::Get().CloseToolTip();

	// Shift + wheel: down and up the lines.
	if (MouseEvent.IsShiftDown())
	{
		ScrollY -= MouseEvent.GetWheelDelta() * GetRowHeight() * 1.5f;
		ClampScroll(MyGeometry.GetLocalSize().Y);
		return FReply::Handled();
	}

	// Ctrl + wheel zooms, everywhere. Inline, in a Details panel, the plain wheel is left to the Details, so they
	// scroll past the tracks rather than getting stuck on them; in the full panel it zooms too.
	if (!MouseEvent.IsControlDown() && Owner && Owner->IsInline())
	{
		return FReply::Unhandled();
	}

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Width = MyGeometry.GetLocalSize().X;

	// Zoom about the time under the cursor, so what is being looked at stays put.
	const float Pivot = XToTime(Local.X, Width);
	const float Factor = MouseEvent.GetWheelDelta() > 0.f ? 0.85f : 1.f / 0.85f;

	const float NewMin = Pivot - (Pivot - ViewMin) * Factor;
	const float NewMax = Pivot + (ViewMax - Pivot) * Factor;
	if (NewMax - NewMin > 0.01f && NewMax - NewMin < 600.f)
	{
		ViewMin = NewMin;
		ViewMax = NewMax;
		bUserView = true;
	}

	return FReply::Handled();
}

void SWidgetAnimTimelineTracks::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SLeafWidget::OnMouseLeave(MouseEvent);

	if (Drag == EDrag::None)
	{
		ClearHover();
	}
}

FCursorReply SWidgetAnimTimelineTracks::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();

	switch (Drag)
	{
	case EDrag::Key:
	case EDrag::KeyPending:
	case EDrag::Pan:
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);

	case EDrag::LayerPending:
	case EDrag::Layer:
		return FCursorReply::Cursor(PendingLayerDrag == ELayerDrag::Move ? EMouseCursor::GrabHandClosed : EMouseCursor::ResizeLeftRight);

	case EDrag::Scrub:
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);

	case EDrag::ResizeRows:
		return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);

	case EDrag::Marquee:
		return FCursorReply::Cursor(EMouseCursor::Crosshairs);

	default:
		break;
	}

	if (HoverRowEdge != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
	}

	// A section edge that can be dragged offers to resize.
	if (Hover.Edge != EEdge::None)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	// An editable key offers to be grabbed; a read-only one just reads as clickable. So does a fold arrow.
	if (Owner && Hover.Key != INDEX_NONE)
	{
		return FCursorReply::Cursor(Owner->CanEditKeys(Hover.Layer) ? EMouseCursor::GrabHand : EMouseCursor::Hand);
	}
	if (Hover.bOnFold)
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}

	const FVector2f Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	if (Local.Y < SWidgetAnimTimeline::RulerHeight)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	return FCursorReply::Unhandled();
}

// =====================================================================================================
// The Selection panel's layout
// =====================================================================================================

/**
 * Lays out the Selection panel for whatever is selected - one key, one layer, one group, or the animation
 * itself - out of the edited object's real properties, so every edit goes through the property system as it
 * would anywhere else: undo, the object's PostEditChangeProperty, the key jump.
 *
 * Registered on the panel's own details view only, for both kinds of edited object. Every category they
 * would normally show is hidden, and only the selected part is added back.
 */
class FWidgetAnimSelectionDetails : public IDetailCustomization
{
public:

	explicit FWidgetAnimSelectionDetails(TWeakPtr<SWidgetAnimTimeline> InPanel)
		: Panel(InPanel)
	{
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		IWidgetAnimPanelSource* Source = Owner ? Owner->GetSource() : nullptr;
		if (!Owner || !Source)
		{
			return;
		}

		TArray<FName> CategoryNames;
		DetailBuilder.GetCategoryNames(CategoryNames);
		for (const FName CategoryName : CategoryNames)
		{
			DetailBuilder.HideCategory(CategoryName);
		}

		// CallInEditor buttons (the asset's View Animation Preview) come in a category of their own, added
		// after the property categories were listed above. Nothing here previews through the builder.
		DetailBuilder.HideCategory("Preview");

		// The builder's Class Defaults, or an asset - a builder edits an asset in place while it previews one - or
		// whatever object holds a plain list of tracks. Its properties are looked up on the class that declares
		// them, so a Blueprint subclass of a builder finds them just the same.
		const UObject* Edited = Source->GetEditedObject();
		const FName GroupsName = Source->GetGroupsPropertyName();
		const FName LayersName = Source->GetLayersPropertyName();
		const FProperty* LayersProperty = Edited ? FindFProperty<FProperty>(Edited->GetClass(), LayersName) : nullptr;
		const UClass* PropertyClass = LayersProperty ? LayersProperty->GetOwnerClass() : (Edited ? Edited->GetClass() : nullptr);
		const SWidgetAnimTimeline::FSelection Selection = Owner->GetSelection();

		// Read-only - the panel locked, or the track - still shows the selection: its fields are only grayed out,
		// by the details view's property-editing delegate. See SWidgetAnimTimeline::IsSelectionEditable.
		if ((Selection.Group == INDEX_NONE && Selection.Layer == INDEX_NONE) || !Owner->HasAnimation() || !PropertyClass)
		{
			CustomizeAnimation(DetailBuilder, Edited);
			return;
		}

		// A group: its row in LayerGroups - where the source keeps groups at all.
		if (Selection.Layer == INDEX_NONE)
		{
			const TSharedPtr<IPropertyHandle> GroupsHandle = GroupsName.IsNone()
				? TSharedPtr<IPropertyHandle>()
				: TSharedPtr<IPropertyHandle>(DetailBuilder.GetProperty(GroupsName, PropertyClass));
			const TSharedPtr<IPropertyHandle> GroupHandle = GroupsHandle.IsValid() && GroupsHandle->IsValidHandle() ? GroupsHandle->GetChildHandle(static_cast<uint32>(Selection.Group)) : nullptr;
			if (GroupHandle.IsValid() && GroupHandle->IsValidHandle())
			{
				CustomizeGroup(DetailBuilder, GroupHandle.ToSharedRef(), *Source);
			}
			else
			{
				CustomizeAnimation(DetailBuilder, Edited);
			}
			return;
		}

		// A layer or a key: its row in the single list of Layers.
		const TSharedRef<IPropertyHandle> LayersHandle = DetailBuilder.GetProperty(LayersName, PropertyClass);
		const TSharedPtr<IPropertyHandle> LayerHandle = LayersHandle->IsValidHandle() ? LayersHandle->GetChildHandle(static_cast<uint32>(Selection.Layer)) : nullptr;
		if (!LayerHandle.IsValid() || !LayerHandle->IsValidHandle())
		{
			CustomizeAnimation(DetailBuilder, Edited);
			return;
		}

		const TArray<FWidgetAnimLayer>* Layers = Owner->GetLayers();
		const bool bIsTransform = Layers
			&& Layers->IsValidIndex(Selection.Layer)
			&& (*Layers)[Selection.Layer].AnimType == EWidgetAnimType::Transform;
		const bool bIsValueKeyed = Layers
			&& Layers->IsValidIndex(Selection.Layer)
			&& WidgetAnimTypeSupportsValueKeys((*Layers)[Selection.Layer].AnimType);

		if (Selection.Key != INDEX_NONE && bIsTransform)
		{
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(Selection.Layer, KeyTimes);
			CustomizeKey(DetailBuilder, LayerHandle.ToSharedRef(), Selection.Key, KeyTimes.Num(), (*Layers)[Selection.Layer].IsSpringTimed());
		}
		else if (Selection.Key != INDEX_NONE && bIsValueKeyed)
		{
			CustomizeValueKey(DetailBuilder, LayerHandle.ToSharedRef(), Selection.Layer, Selection.Key, (*Layers)[Selection.Layer]);
		}
		else
		{
			CustomizeLayer(DetailBuilder, LayerHandle.ToSharedRef(), bIsTransform, Selection.Layer);
		}
	}

private:

	static void AddChildren(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& Parent, TFunctionRef<bool(FName)> Filter, bool bAutoExpand)
	{
		uint32 NumChildren = 0;
		Parent->GetNumChildren(NumChildren);

		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Child = Parent->GetChildHandle(Index);
			const FProperty* ChildProperty = Child.IsValid() ? Child->GetProperty() : nullptr;
			if (!ChildProperty || !Filter(ChildProperty->GetFName()))
			{
				continue;
			}

			Category.AddProperty(Child).ShouldAutoExpand(bAutoExpand);
		}
	}

	/**
	 * Nothing selected: the animation itself. A builder's name, folder and preview asset; an asset's description. A
	 * plain list of tracks has no settings of its own, so it says how to get to its tracks'.
	 */
	static void CustomizeAnimation(IDetailLayoutBuilder& DetailBuilder, const UObject* Edited)
	{
		const bool bEditingAsset = Edited && Edited->IsA<UWidgetAnimPresetAsset>();
		const bool bEditingBuilder = Edited && Edited->IsA<UWidgetAnimBuilder>();
		if (!bEditingAsset && !bEditingBuilder)
		{
			IDetailCategoryBuilder& Hint = DetailBuilder.EditCategory("WidgetAnimSelection_Hint", LOCTEXT("SelectionHintCategory", "Selection"));
			AddNoteRow(Hint, WidgetAnimTips::CompactNothingSelected());
			return;
		}

		IDetailCategoryBuilder& Animation = DetailBuilder.EditCategory("WidgetAnimSelection_Animation", LOCTEXT("AnimationCategory", "Animation"));

		if (!bEditingAsset)
		{
			const UClass* BuilderClass = UWidgetAnimBuilder::StaticClass();
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationName), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Description), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationSpeedMultiplier), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, SaveFolder), BuilderClass));

			// No Preview Animation row: which asset is edited is the header's Edit Existing Asset picker.
		}
		else
		{
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Description), UWidgetAnimPresetAsset::StaticClass()));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, AnimationSpeedMultiplier), UWidgetAnimPresetAsset::StaticClass()));
		}
	}

	/** A group: its name, and the widget it animates here - picked from this Widget Blueprint's own widgets. */
	void CustomizeGroup(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& GroupHandle, const IWidgetAnimPanelSource& Source)
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Group", LOCTEXT("GroupCategory", "Layer Group"));

		const TSharedPtr<IPropertyHandle> NameHandle = GroupHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayerGroup, GroupName));
		if (NameHandle.IsValid())
		{
			Category.AddProperty(NameHandle);
		}

		const TSharedPtr<IPropertyHandle> TargetHandle = GroupHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayerGroup, PreviewTarget));
		if (!TargetHandle.IsValid())
		{
			return;
		}

		// Options from the designer's preview of this Widget Blueprint, as paths - the same list, in the same
		// form, an entry's Target Widget offers. Kept on this customization, which lives as long as the rows.
		FName Current;
		TargetHandle->GetValue(Current);
		const FString CurrentText = Current.IsNone() ? FString(TEXT("None")) : Current.ToString();

		TargetOptions = MakeShared<TArray<TSharedPtr<FString>>>();
		TSharedPtr<FString> Initial;
		for (const FString& Option : Source.GetTargetOptions())
		{
			TSharedPtr<FString> Item = MakeShared<FString>(Option);
			if (Option == CurrentText)
			{
				Initial = Item;
			}
			TargetOptions->Add(Item);
		}

		// A path this Widget Blueprint doesn't have - an asset built against another one - still shows what it is.
		if (!Initial.IsValid())
		{
			Initial = MakeShared<FString>(CurrentText);
			TargetOptions->Add(Initial);
		}

		const TSharedRef<IPropertyHandle> Target = TargetHandle.ToSharedRef();
		Category.AddProperty(TargetHandle).CustomWidget()
			.NameContent()
			[
				Target->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(220.f)
			[
				SNew(STextComboBox)
				.IsEnabled_Lambda([WeakOwner = Panel]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEdit(); })
				.OptionsSource(TargetOptions.Get())
				.InitiallySelectedItem(Initial)
				.ToolTipText(WidgetAnimTips::PreviewTargetTooltip())
				.OnSelectionChanged_Lambda([Target](TSharedPtr<FString> Item, ESelectInfo::Type)
				{
					if (Item.IsValid())
					{
						Target->SetValue(*Item == TEXT("None") ? FName(NAME_None) : FName(**Item));
					}
				})
			];
	}

	/**
	 * A layer: which group - which widget - it moves, then its type, timing, easing and settings. Not a
	 * Transform layer's keys - those are selected on the track.
	 */
	void CustomizeLayer(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, bool bIsTransform, int32 LayerIndex)
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Layer", LOCTEXT("LayerCategory", "Track"));

		// Layer Group is not a Details property - it only means something next to the group list - so it is
		// a row of its own here, changed through the panel like a drop on a group's row.
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<FWidgetAnimLayerGroup>* Groups = Owner ? Owner->GetGroups() : nullptr;
		const bool bHasGroups = Owner && Owner->GetSource() && Owner->GetSource()->SupportsLayerGroups();
		if (Owner && Groups && Groups->Num() > 0 && bHasGroups)
		{
			GroupOptions = MakeShared<TArray<TSharedPtr<FString>>>();
			const int32 CurrentGroup = Owner->GetLayerGroup(LayerIndex);
			TSharedPtr<FString> Initial;
			for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
			{
				TSharedPtr<FString> Item = MakeShared<FString>((*Groups)[GroupIndex].GetDisplayName(GroupIndex));
				if (GroupIndex == CurrentGroup)
				{
					Initial = Item;
				}
				GroupOptions->Add(Item);
			}

			const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Owner;
			const TWeakPtr<TArray<TSharedPtr<FString>>> WeakOptions = GroupOptions;
			Category.AddCustomRow(LOCTEXT("LayerGroupRow", "Layer Group"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LayerGroupLabel", "Layer Group"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[
					SNew(STextComboBox)
					.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
					.OptionsSource(GroupOptions.Get())
					.InitiallySelectedItem(Initial)
					.ToolTipText(WidgetAnimTips::LayerGroupTooltip())
					.OnSelectionChanged_Lambda([WeakOwner, WeakOptions, LayerIndex](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
					{
						const TSharedPtr<SWidgetAnimTimeline> Panel = WeakOwner.Pin();
						const TSharedPtr<TArray<TSharedPtr<FString>>> Options = WeakOptions.Pin();
						if (!Panel || !Options || !Item.IsValid() || SelectInfo == ESelectInfo::Direct)
						{
							return;
						}

						const int32 GroupIndex = Options->IndexOfByKey(Item);
						if (GroupIndex != INDEX_NONE)
						{
							Panel->SetLayerGroup(LayerIndex, GroupIndex);
						}
					})
				];
		}

		// On a plain list the track names the widget it moves itself - not a Details property either, so a row of its
		// own like Layer Group's, set through the panel. See FWidgetAnimLayer::TargetWidget.
		IWidgetAnimPanelSource* PanelSource = Owner ? Owner->GetSource() : nullptr;
		const TArray<FWidgetAnimLayer>* TargetLayers = Owner ? Owner->GetLayers() : nullptr;
		if (PanelSource && PanelSource->SupportsLayerTargets() && TargetLayers && TargetLayers->IsValidIndex(LayerIndex))
		{
			const FName CurrentTarget = (*TargetLayers)[LayerIndex].TargetWidget;
			const FString CurrentTargetText = CurrentTarget.IsNone() ? FString(TEXT("None")) : CurrentTarget.ToString();

			LayerTargetOptions = MakeShared<TArray<TSharedPtr<FString>>>();
			TSharedPtr<FString> InitialTarget;
			for (const FString& Option : PanelSource->GetLayerTargetOptions())
			{
				TSharedPtr<FString> Item = MakeShared<FString>(Option);
				if (Option == CurrentTargetText)
				{
					InitialTarget = Item;
				}
				LayerTargetOptions->Add(Item);
			}

			// A path this Widget Blueprint no longer has still shows what it is.
			if (!InitialTarget.IsValid())
			{
				InitialTarget = MakeShared<FString>(CurrentTargetText);
				LayerTargetOptions->Add(InitialTarget);
			}

			const TWeakPtr<SWidgetAnimTimeline> WeakTargetOwner = Owner;
			Category.AddCustomRow(LOCTEXT("TargetWidgetRow", "Target Widget"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TargetWidgetLabel", "Target Widget"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(180.f)
				[
					SNew(STextComboBox)
					.IsEnabled_Lambda([WeakTargetOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTargetOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
					.OptionsSource(LayerTargetOptions.Get())
					.InitiallySelectedItem(InitialTarget)
					.ToolTipText(WidgetAnimTips::LayerTargetWidgetTooltip())
					.OnSelectionChanged_Lambda([WeakTargetOwner, LayerIndex](TSharedPtr<FString> Item, ESelectInfo::Type SelectInfo)
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTargetOwner.Pin();
						if (!Pinned || !Item.IsValid() || SelectInfo == ESelectInfo::Direct)
						{
							return;
						}
						Pinned->SetLayerTarget(LayerIndex, *Item == TEXT("None") ? FName(NAME_None) : FName(**Item));
					})
				];
		}

		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform);
		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);
		const FName ValueKeysName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueKeys);
		const FName OvershootName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Overshoot);
		const FName SlideOvershootName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SlideOvershoot);

		const TArray<FWidgetAnimLayer>* CurrentLayers = Owner ? Owner->GetLayers() : nullptr;
		const bool bHasValueKeys = CurrentLayers && CurrentLayers->IsValidIndex(LayerIndex) && (*CurrentLayers)[LayerIndex].ValueKeys.Keys.Num() > 0;

		uint32 NumChildren = 0;
		LayerHandle->GetNumChildren(NumChildren);

		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Child = LayerHandle->GetChildHandle(Index);
			const FProperty* ChildProperty = Child.IsValid() ? Child->GetProperty() : nullptr;
			if (!ChildProperty)
			{
				continue;
			}

			// A track's keys are selected on the track, one at a time, never listed here.
			if (ChildProperty->GetFName() == ValueKeysName)
			{
				continue;
			}

			// Overshoot only applies to a track with no keys between its start and end - see EvaluateTrackEase - so
			// a keyed track doesn't offer it.
			if (bHasValueKeys && (ChildProperty->GetFName() == OvershootName || ChildProperty->GetFName() == SlideOvershootName))
			{
				continue;
			}

			if (ChildProperty->GetFName() == TransformName)
			{
				// Laid out flat, as the layer's own ShowOnlyInnerProperties would, and only on a Transform
				// layer: added one by one, its EditCondition on the layer type no longer hides it. Its key
				// timing curve is not a Details property at all; keys are timed on the track.
				if (bIsTransform)
				{
					AddChildren(Category, Child.ToSharedRef(), [KeysName](FName Name) { return Name != KeysName; }, false);
				}
				continue;
			}

			// Everything else hides itself by type through its own EditCondition, wherever it is added - and a Timing
			// or Value Type the type can't use is left out of its dropdown.
			Category.AddProperty(Child);
			if (CurrentLayers && CurrentLayers->IsValidIndex(LayerIndex))
			{
				WidgetAnimTimeline::RestrictLayerChoices(Child, { &(*CurrentLayers)[LayerIndex] });
			}
		}
	}

	/**
	 * One Transform key: when it happens (read-only - drag it on the track), its pose, its shape, and the curve
	 * from it to the next - laid out flat, and left out on the last key, which has no next. On a Spring track the
	 * spring carries it across every key, so a note says so in place of the curve.
	 */
	static void CustomizeKey(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, int32 KeyIndex, int32 NumKeys, bool bSpring)
	{
		const TSharedPtr<IPropertyHandle> TransformHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform));
		const TSharedPtr<IPropertyHandle> KeysHandle = TransformHandle.IsValid() ? TransformHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys)) : nullptr;
		const TSharedPtr<IPropertyHandle> KeyHandle = KeysHandle.IsValid() ? KeysHandle->GetChildHandle(static_cast<uint32>(KeyIndex)) : nullptr;

		if (!KeyHandle.IsValid() || !KeyHandle->IsValidHandle())
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Key", LOCTEXT("KeyCategory", "Key"));
		const FName CurveName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, CurveToNext);
		AddChildren(Category, KeyHandle.ToSharedRef(), [CurveName](FName Name) { return Name != CurveName; }, /*bAutoExpand=*/true);

		if (KeyIndex + 1 < NumKeys && bSpring)
		{
			AddNoteRow(Category, WidgetAnimTips::SpringKeyCurveNote());
			return;
		}

		const TSharedPtr<IPropertyHandle> CurveHandle = KeyHandle->GetChildHandle(CurveName);
		if (KeyIndex + 1 < NumKeys && CurveHandle.IsValid() && CurveHandle->IsValidHandle())
		{
			AddChildren(Category, CurveHandle.ToSharedRef(), [](FName) { return true; }, /*bAutoExpand=*/false);
		}
	}

	/** What a keyed track's values are called in the Selection panel, by type - a registered type names its own. */
	static FText GetKeyValueLabel(const FWidgetAnimLayer& Layer)
	{
		if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
		{
			return Evaluator->GetTrackValueLabel(Layer.TypeParams);
		}

		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:           return LOCTEXT("KeyValueOpacity", "Opacity");
		case EWidgetAnimType::Scale:          return LOCTEXT("KeyValueScale", "Scale");
		case EWidgetAnimType::Slide:          return LOCTEXT("KeyValueDistance", "Distance");
		case EWidgetAnimType::Rotate:         return LOCTEXT("KeyValueAngle", "Angle");
		case EWidgetAnimType::MaterialVector: return LOCTEXT("KeyValueColor", "Value");
		default:                              return LOCTEXT("KeyValueValue", "Value");
		}
	}

	/** A quiet line of explanation across the whole row. */
	static void AddNoteRow(IDetailCategoryBuilder& Category, const FText& Text)
	{
		Category.AddCustomRow(Text)
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.f, 4.f))
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(Text)
				]
			];
	}

	/**
	 * One key of a track that travels from a start value to an end value. Its first key IS the track's start
	 * value and its last key its end value, so those show the track's own Start and End properties; a key between
	 * shows its own value. Every key but the last also has the curve from it to the next one - unless the track
	 * springs, which it does across its whole run.
	 */
	void CustomizeValueKey(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, int32 LayerIndex, int32 KeyIndex, const FWidgetAnimLayer& Layer)
	{
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Key", LOCTEXT("KeyCategory", "Key"));

		const int32 LastKey = Layer.ValueKeys.NumPoints() - 1;
		const bool bSpring = Layer.IsSpringTimed();
		const FText ValueLabel = GetKeyValueLabel(Layer);

		auto AddLayerProperty = [&Category, &LayerHandle](FName Name, const FText& Label)
		{
			const TSharedPtr<IPropertyHandle> Handle = LayerHandle->GetChildHandle(Name);
			if (Handle.IsValid() && Handle->IsValidHandle())
			{
				IDetailPropertyRow& Row = Category.AddProperty(Handle);
				if (!Label.IsEmpty())
				{
					Row.DisplayName(Label);
				}
			}
		};

		// When the key is, in the track and in the animation - read here, moved by dragging it on the track.
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		Category.AddCustomRow(LOCTEXT("KeyTimeRow", "Time"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("KeyTimeLabel", "Time"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MinDesiredWidth(220.f)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(WidgetAnimTips::KeyTimeTooltip())
				.Text_Lambda([WeakOwner, LayerIndex, KeyIndex]()
				{
					const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
					TArray<float> KeyTimes;
					if (Pinned)
					{
						Pinned->GetKeyTimes(LayerIndex, KeyTimes);
					}
					return KeyTimes.IsValidIndex(KeyIndex)
						? FText::Format(LOCTEXT("KeyTimeValue", "{0} s into the track"), FText::FromString(FString::Printf(TEXT("%.3f"), KeyTimes[KeyIndex])))
						: FText::GetEmpty();
				})
			];

		if (KeyIndex == 0)
		{
			switch (Layer.AnimType)
			{
			case EWidgetAnimType::Fade:           AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, StartOpacity), FText::GetEmpty()); break;
			case EWidgetAnimType::Scale:          AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, StartScale), FText::GetEmpty()); break;
			case EWidgetAnimType::Rotate:         AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, StartAngle), FText::GetEmpty()); break;
			case EWidgetAnimType::MaterialScalar: AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, StartScalarValue), FText::GetEmpty()); break;
			case EWidgetAnimType::MaterialVector: AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, StartVectorValue), FText::GetEmpty()); break;
			case EWidgetAnimType::Slide:
				AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SlideFromDirection), FText::GetEmpty());
				AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SlideDistance), FText::GetEmpty());
				break;
			case EWidgetAnimType::Custom:
				// A registered type's start and end values are fields of its own settings, which only exist as one
				// struct here - so its settings are shown whole, start value among them.
				AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TypeParams), FText::GetEmpty());
				break;
			default:
				break;
			}
		}
		else if (KeyIndex == LastKey)
		{
			switch (Layer.AnimType)
			{
			case EWidgetAnimType::Fade:           AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, EndOpacity), FText::GetEmpty()); break;
			case EWidgetAnimType::Scale:          AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, EndScale), FText::GetEmpty()); break;
			case EWidgetAnimType::Rotate:         AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, EndAngle), FText::GetEmpty()); break;
			case EWidgetAnimType::MaterialScalar: AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, EndScalarValue), FText::GetEmpty()); break;
			case EWidgetAnimType::MaterialVector: AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, EndVectorValue), FText::GetEmpty()); break;
			case EWidgetAnimType::Slide:
				AddNoteRow(Category, WidgetAnimTips::SlideEndNote());
				break;
			case EWidgetAnimType::Custom:
				AddLayerProperty(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TypeParams), FText::GetEmpty());
				break;
			default:
				break;
			}
		}
		else
		{
			const TSharedPtr<IPropertyHandle> ValueKeysHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueKeys));
			const TSharedPtr<IPropertyHandle> KeysHandle = ValueKeysHandle.IsValid() ? ValueKeysHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKeys, Keys)) : nullptr;
			const TSharedPtr<IPropertyHandle> KeyHandle = KeysHandle.IsValid() ? KeysHandle->GetChildHandle(static_cast<uint32>(KeyIndex - 1)) : nullptr;

			if (KeyHandle.IsValid() && KeyHandle->IsValidHandle())
			{
				// What the key holds, named for what it is on this track: an opacity, a scale, a color - or, on a
				// registered type whose keys carry colors, a color of its own (and its value too, when it has one).
				const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
				const EWidgetAnimKeyContent Content = Layer.AnimType == EWidgetAnimType::MaterialVector ? EWidgetAnimKeyContent::Color
					: Evaluator ? Evaluator->GetKeyContent(Layer.TypeParams)
					: EWidgetAnimKeyContent::Value;

				auto AddKeyField = [&Category, &KeyHandle](FName Name, const FText& Label)
				{
					const TSharedPtr<IPropertyHandle> FieldHandle = KeyHandle->GetChildHandle(Name);
					if (FieldHandle.IsValid() && FieldHandle->IsValidHandle())
					{
						Category.AddProperty(FieldHandle).DisplayName(Label);
					}
				};

				if (Content != EWidgetAnimKeyContent::Color)
				{
					AddKeyField(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKey, Value), ValueLabel);
				}
				if (Content != EWidgetAnimKeyContent::Value)
				{
					const FText ColorLabel = Layer.AnimType == EWidgetAnimType::MaterialVector ? ValueLabel
						: Evaluator ? Evaluator->GetKeyColorLabel(Layer.TypeParams)
						: ValueLabel;
					AddKeyField(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKey, Color), ColorLabel);
				}
			}

			if (Layer.AnimType == EWidgetAnimType::Slide)
			{
				AddNoteRow(Category, WidgetAnimTips::SlideKeyNote());
			}
		}

		// The curve from this key to the next: the start's is kept apart from the keys, every other key carries its own.
		if (KeyIndex < LastKey)
		{
			if (bSpring)
			{
				AddNoteRow(Category, WidgetAnimTips::SpringKeyCurveNote());
				return;
			}

			const TSharedPtr<IPropertyHandle> ValueKeysHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueKeys));
			TSharedPtr<IPropertyHandle> CurveHandle;
			if (ValueKeysHandle.IsValid())
			{
				if (KeyIndex == 0)
				{
					CurveHandle = ValueKeysHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKeys, StartCurve));
				}
				else
				{
					const TSharedPtr<IPropertyHandle> KeysHandle = ValueKeysHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKeys, Keys));
					const TSharedPtr<IPropertyHandle> KeyHandle = KeysHandle.IsValid() ? KeysHandle->GetChildHandle(static_cast<uint32>(KeyIndex - 1)) : nullptr;
					CurveHandle = KeyHandle.IsValid() ? KeyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKey, CurveToNext)) : nullptr;
				}
			}

			if (CurveHandle.IsValid() && CurveHandle->IsValidHandle())
			{
				AddChildren(Category, CurveHandle.ToSharedRef(), [](FName) { return true; }, /*bAutoExpand=*/false);
			}
		}
	}

	TWeakPtr<SWidgetAnimTimeline> Panel;
	TSharedPtr<TArray<TSharedPtr<FString>>> TargetOptions;
	TSharedPtr<TArray<TSharedPtr<FString>>> GroupOptions;
	TSharedPtr<TArray<TSharedPtr<FString>>> LayerTargetOptions;
};

/**
 * The Selection panel for several layers at once, laid out over their proxies - one UWidgetAnimLayerEditProxy per
 * selected layer, shown together, which is what lets the details view merge them: a setting every layer agrees
 * on shows its value, one they don't reads Multiple Values, exactly as selecting several widgets does.
 *
 * Only what every selected layer has is shown. Type, Starts, Delay and Easing are on every layer; Duration on all
 * but Transform layers, whose keys set their length; a type's own settings only when every layer selected is of
 * that type. Keys never - each layer's keys are its own.
 */
class FWidgetAnimMultiLayerDetails : public IDetailCustomization
{
public:

	explicit FWidgetAnimMultiLayerDetails(TWeakPtr<SWidgetAnimTimeline> InPanel)
		: Panel(InPanel)
	{
	}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TArray<TWeakObjectPtr<UObject>> Objects;
		DetailBuilder.GetObjectsBeingCustomized(Objects);

		TSet<EWidgetAnimType> Types;
		TArray<const FWidgetAnimLayer*> ProxyLayers;
		for (const TWeakObjectPtr<UObject>& Object : Objects)
		{
			if (const UWidgetAnimLayerEditProxy* Proxy = Cast<UWidgetAnimLayerEditProxy>(Object.Get()))
			{
				Types.Add(Proxy->Layer.AnimType);
				ProxyLayers.Add(&Proxy->Layer);
			}
		}

		TArray<FName> CategoryNames;
		DetailBuilder.GetCategoryNames(CategoryNames);
		for (const FName CategoryName : CategoryNames)
		{
			DetailBuilder.HideCategory(CategoryName);
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Layers",
			FText::Format(LOCTEXT("LayersCategory", "{0} Tracks"), FText::AsNumber(Objects.Num())));

		const bool bSameType = Types.Num() == 1;
		const bool bAllTransform = bSameType && Types.Contains(EWidgetAnimType::Transform);
		const bool bAnyTransform = Types.Contains(EWidgetAnimType::Transform);

		Category.AddCustomRow(LOCTEXT("MultipleLayersRow", "Multiple Tracks"))
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.f, 4.f))
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::Format(bSameType
						? WidgetAnimTips::MultipleSameType()
						: WidgetAnimTips::MultipleMixedTypes(),
						FText::AsNumber(Objects.Num())))
				]
			];

		const TSharedRef<IPropertyHandle> LayerHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimLayerEditProxy, Layer));
		if (!LayerHandle->IsValidHandle())
		{
			return;
		}

		static const FName SharedNames[] =
		{
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, AnimType),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Start),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Delay),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Expander),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Easing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, CustomCurve),
		};
		const FName DurationName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration);
		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform);
		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);
		const FName ValueKeysName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueKeys);
		// Timing, and what it brings - the spring, the value settings - is on every type, so on a mix of them too.
		// Each hides itself by its EditCondition where it doesn't apply, and a Timing or Value Type that doesn't
		// apply to every selected track is left out of its dropdown.
		static const FName TimingNames[] =
		{
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Timing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Spring),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueName),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueSmoothing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadySpeed),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadyCatchUpTime),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueDrives),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueOptions),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueInput),
		};

		uint32 NumChildren = 0;
		LayerHandle->GetNumChildren(NumChildren);

		for (uint32 Index = 0; Index < NumChildren; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Child = LayerHandle->GetChildHandle(Index);
			const FProperty* ChildProperty = Child.IsValid() ? Child->GetProperty() : nullptr;
			if (!ChildProperty)
			{
				continue;
			}

			const FName Name = ChildProperty->GetFName();

			// Each layer's keys are its own.
			if (Name == ValueKeysName)
			{
				continue;
			}

			if (Name == TransformName)
			{
				// Laid out flat, as the single-layer panel does, and without the keys - they are each layer's own.
				if (bAllTransform)
				{
					uint32 NumTransformChildren = 0;
					Child->GetNumChildren(NumTransformChildren);
					for (uint32 TransformIndex = 0; TransformIndex < NumTransformChildren; ++TransformIndex)
					{
						const TSharedPtr<IPropertyHandle> TransformChild = Child->GetChildHandle(TransformIndex);
						const FProperty* TransformProperty = TransformChild.IsValid() ? TransformChild->GetProperty() : nullptr;
						if (TransformProperty && TransformProperty->GetFName() != KeysName)
						{
							Category.AddProperty(TransformChild);
						}
					}
				}
				continue;
			}

			const bool bShared = Algo::Find(SharedNames, Name) != nullptr || Algo::Find(TimingNames, Name) != nullptr;
			const bool bShow = bShared
				|| (Name == DurationName && !bAnyTransform)
				// A type's own settings, when every layer is that type. Each hides itself on other types by its
				// EditCondition, which reads the same on every proxy here.
				|| (bSameType && Name != DurationName);

			if (bShow)
			{
				Category.AddProperty(Child);
				WidgetAnimTimeline::RestrictLayerChoices(Child, ProxyLayers);
			}
		}
	}

private:

	TWeakPtr<SWidgetAnimTimeline> Panel;
};

void UWidgetAnimLayerEditProxy::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
	{
		Owner->OnProxyEdited(this, PropertyChangedEvent);
	}
}

// =====================================================================================================
// The Animation Layers list: one row per group, and under it one per layer, in the order they play
// =====================================================================================================

/**
 * One row of the Animation Layers list. A group's row selects the group, folds its layers away on a double
 * click and offers the group's menu on a right click; a layer's row selects that layer's track - with Shift
 * and Ctrl / Cmd as a file browser has them - brings it into view in the Track Editor, and offers the
 * layer's menu. Nothing here is dragged: the list is a reading of the animation, in the order it plays.
 */
class SWidgetAnimListRow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimListRow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel, int32 InGroup, int32 InLayer)
	{
		Panel = InPanel;
		Group = InGroup;
		Layer = InLayer;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	bool IsGroupRow() const { return Layer == INDEX_NONE; }

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner)
		{
			return FReply::Unhandled();
		}

		// The panel takes the keyboard, so Delete, Ctrl+A and the rest work on what was just picked here. A
		// right-press also takes the mouse, so its release - where the menu opens - comes back here.
		const bool bRight = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
		FReply Reply = FReply::Handled().SetUserFocus(Owner.ToSharedRef(), EFocusCause::Mouse);
		if (bRight)
		{
			Reply.CaptureMouse(SharedThis(this));
		}
		bMenuPending = bRight;
		bCollapseSelectionOnRelease = false;

		// A right-click only opens the menu - for the group or track under the mouse, whatever is selected. It
		// changes nothing that is selected.
		if (bRight)
		{
			return Reply;
		}

		if (IsGroupRow())
		{
			Owner->SelectGroup(Group);
			Owner->RevealGroup(Group);
			return Reply;
		}

		// Shift and Ctrl / Cmd pick layers the way a file browser picks files.
		const bool bRange = MouseEvent.IsShiftDown();
		const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();
		if (!bRight && (bRange || bToggle))
		{
			Owner->SelectLayerWithModifiers(Layer, bRange, bToggle);
			Owner->RevealLayer(Layer);
			return Reply;
		}

		// One of several selected layers keeps them all selected - for its menu. A plain click on it, released,
		// selects it alone.
		if (Owner->IsMultiSelection() && Owner->IsLayerSelected(Layer))
		{
			bCollapseSelectionOnRelease = !bRight;
		}
		else
		{
			Owner->SelectLayer(Layer);
		}
		Owner->RevealLayer(Layer);
		return Reply;
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();

		// The menu, on the release, as the Track Editor opens its own. Opened on the press, the focus the press
		// hands the panel landed a moment after the menu opened - and a menu that loses focus closes, so it was
		// gone before it was ever seen.
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && bMenuPending)
		{
			bMenuPending = false;
			if (Owner)
			{
				PushMenu(MouseEvent, IsGroupRow()
					? Owner->MakeGroupContextMenu(Group)
					: Owner->MakeLayerContextMenu(Layer, INDEX_NONE, TOptional<float>()));
			}
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (Owner && bCollapseSelectionOnRelease && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bCollapseSelectionOnRelease = false;
			Owner->SelectLayer(Layer);
			return FReply::Handled();
		}

		bCollapseSelectionOnRelease = false;
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (Owner && IsGroupRow())
		{
			Owner->ToggleListGroupCollapsed(Group);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:

	void PushMenu(const FPointerEvent& MouseEvent, const TSharedRef<SWidget>& Menu)
	{
		const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, Menu, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
	}

	TWeakPtr<SWidgetAnimTimeline> Panel;
	int32 Group = INDEX_NONE;
	int32 Layer = INDEX_NONE;

	/** Pressed on one of several selected layers: released without a drag, it selects that layer alone. */
	bool bCollapseSelectionOnRelease = false;

	/** Right-pressed here: the menu opens when the button comes up. */
	bool bMenuPending = false;
};

/**
 * The Animation Tracks list's own area, round its rows: a right-click on it anywhere a row isn't - the space below
 * the last one - offers everything that can be added, to any group. Opened on the release, as every other menu in
 * the panel is; the press takes the mouse so the release comes back here.
 */
class SWidgetAnimListArea : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimListArea) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel)
	{
		Panel = InPanel;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton)
		{
			return FReply::Unhandled();
		}
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !HasMouseCapture())
		{
			return FReply::Unhandled();
		}

		if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
		{
			const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, Owner->MakeAddMenu(INDEX_NONE), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

private:

	TWeakPtr<SWidgetAnimTimeline> Panel;
};

// =====================================================================================================
// Tooltip content - what a track or a key is doing, laid out in the panel's own colors
// =====================================================================================================

namespace WidgetAnimToolTip
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	FSlateFontInfo TitleFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }
	FSlateFontInfo BodyFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
	FSlateFontInfo ValueFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
	FSlateFontInfo SmallFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 8); }

	/** A number as short as it can be: no trailing zeros, no "-0". */
	FString Number(double Value, int32 MaxDecimals = 2)
	{
		FString Out = FString::Printf(TEXT("%.*f"), MaxDecimals, Value);
		if (Out.Contains(TEXT(".")))
		{
			while (Out.EndsWith(TEXT("0")))
			{
				Out.LeftChopInline(1);
			}
			if (Out.EndsWith(TEXT(".")))
			{
				Out.LeftChopInline(1);
			}
		}
		return Out == TEXT("-0") ? FString(TEXT("0")) : Out;
	}

	FString Seconds(float Value) { return FString::Printf(TEXT("%.2f s"), Value); }
	FString Angle(float Value) { return Number(Value, 1) + TEXT("\u00B0"); }

	/** A pair of values: one number when both are the same (a uniform scale), else both in brackets. */
	FString Pair(const FVector2D& Value, bool bCollapseUniform)
	{
		if (bCollapseUniform && FMath::IsNearlyEqual(Value.X, Value.Y, 1.e-3))
		{
			return Number(Value.X);
		}
		return FString::Printf(TEXT("(%s, %s)"), *Number(Value.X), *Number(Value.Y));
	}

	TSharedRef<SWidget> Text(const FString& String, const FLinearColor& Color, const FSlateFontInfo& Font)
	{
		return SNew(STextBlock)
			.Text(FText::FromString(String))
			.Font(Font)
			.ColorAndOpacity(Color);
	}

	TSharedRef<SWidget> Divider()
	{
		return SNew(SBox)
			.HeightOverride(1.f)
			.Padding(FMargin(0.f))
			[
				SNew(SImage)
				.Image(WidgetAnimTimeline::WhiteBrush())
				.ColorAndOpacity(Palette::TooltipDivider)
			];
	}

	TSharedRef<SWidget> Swatch(const FLinearColor& Color)
	{
		return SNew(SBox)
			.WidthOverride(12.f)
			.HeightOverride(10.f)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(WidgetAnimTimeline::RoundedFill(2.f))
				.ColorAndOpacity(Color.CopyWithNewOpacity(1.f))
			];
	}

	/**
	 * A long right arrow, drawn rather than typed: a line and an arrowhead, centered on the text beside it. The
	 * arrow characters sit on the font's baseline, and the long one isn't in every font.
	 */
	TSharedRef<SWidget> Arrow()
	{
		const FSlateBrush* Head = WidgetAnimTimeline::PickBrush({ "Icons.ChevronRight", "Icons.ArrowRight" });
		if (!Head)
		{
			return Text(TEXT("\u2192"), Palette::TooltipLabel, BodyFont());
		}

		return SNew(SBox)
			.HeightOverride(10.f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(16.f)
					.HeightOverride(1.f)
					[
						SNew(SImage)
						.Image(WidgetAnimTimeline::WhiteBrush())
						.ColorAndOpacity(Palette::TooltipLabel)
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(-5.f, 0.f, 0.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(9.f)
					.HeightOverride(9.f)
					[
						SNew(SImage)
						.Image(Head)
						.ColorAndOpacity(Palette::TooltipLabel)
					]
				]
			];
	}

	/** Start and end, side by side: the start in the animation's start green, the end in its end red. */
	TSharedRef<SWidget> StartToEnd(const FString& Start, const FString& End)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Text(Start, Palette::TooltipStartValue, ValueFont()) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 0.f))[ Arrow() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Text(End, Palette::TooltipEndValue, ValueFont()) ];
	}

	/** Label and value rows, the labels in one column so the values line up. */
	class FRows
	{
	public:

		FRows()
			: Grid(SNew(SGridPanel).FillColumn(1, 1.f))
		{
		}

		void Add(const FString& Label, const TSharedRef<SWidget>& Value)
		{
			Grid->AddSlot(0, Row)
				.Padding(FMargin(0.f, 2.f, 14.f, 2.f))
				.VAlign(VAlign_Center)
				[
					Text(Label, Palette::TooltipLabel, BodyFont())
				];
			Grid->AddSlot(1, Row)
				.Padding(FMargin(0.f, 2.f))
				.VAlign(VAlign_Center)
				[
					Value
				];
			++Row;
		}

		void Add(const FString& Label, const FString& Value)
		{
			Add(Label, Text(Value, Palette::TooltipValue, ValueFont()));
		}

		bool IsEmpty() const { return Row == 0; }
		TSharedRef<SWidget> Widget() const { return Grid; }

	private:

		TSharedRef<SGridPanel> Grid;
		int32 Row = 0;
	};

	/**
	 * The whole tooltip: a bar down the left in the track's color, a title with what it belongs to beside it and
	 * a time on the right, then the body.
	 */
	TSharedRef<SWidget> Frame(const FLinearColor& Accent, const FString& Title, const FString& Subtitle, const FString& Time, const TSharedRef<SVerticalBox>& Body)
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(8.f, 9.f, 0.f, 9.f))
			[
				SNew(SBox)
				.WidthOverride(3.f)
				[
					SNew(SImage)
					.Image(WidgetAnimTimeline::RoundedFill(2.f))
					.ColorAndOpacity(Accent.CopyWithNewOpacity(1.f))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(11.f, 11.f, 15.f, 12.f))
			[
				SNew(SBox)
				.MinDesiredWidth(200.f)
				.MaxDesiredWidth(640.f)
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						[
							Text(Title, Palette::TooltipTitle, TitleFont())
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Bottom)
						.Padding(FMargin(8.f, 0.f, 0.f, 1.f))
						[
							Text(Subtitle, Palette::TooltipLabel, SmallFont())
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Bottom)
						.Padding(FMargin(16.f, 0.f, 0.f, 0.f))
						[
							Text(Time, Palette::TooltipValue, ValueFont())
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 7.f))
					[
						Divider()
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						Body
					]
				]
			];
	}

	/** What a track needs in order to work, under everything else: bold, italic, in the animation orange. */
	TSharedRef<SWidget> Warning(const FText& String)
	{
		return SNew(SBox)
			.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text(String)
				.Font(FCoreStyle::GetDefaultFontStyle("BoldItalic", 8))
				.ColorAndOpacity(Palette::TooltipWarning)
				.AutoWrapText(true)
			];
	}

	/** A quiet line of text under everything else - a note, or why a track can't be changed. */
	TSharedRef<SWidget> Note(const FString& String)
	{
		return SNew(SBox)
			.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(String))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(Palette::TooltipLabel)
				.AutoWrapText(true)
			];
	}

	/**
	 * One channel of a Transform key's pose: its name, how it reads, and whether it is the identity - which
	 * is what "not changed" means for it.
	 */
	struct FChannel
	{
		FString Name;
		TFunction<FString(const FWidgetTransformKeyframe&)> Format;
		TFunction<bool(const FWidgetTransformKeyframe&)> IsDefault;
	};

	/** Translation, Scale, Shear, Angle - and Shape, when the layer animates a Shape Box's outline. */
	TArray<FChannel> TransformChannels(bool bAnimatesShape)
	{
		TArray<FChannel> Channels;

		Channels.Add({ TEXT("Translation"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(Key.Transform.Translation, false); },
			[](const FWidgetTransformKeyframe& Key) { return Key.Transform.Translation.IsNearlyZero(1.e-3); } });

		Channels.Add({ TEXT("Scale"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(Key.Transform.Scale, true); },
			[](const FWidgetTransformKeyframe& Key) { return Key.Transform.Scale.Equals(FVector2D(1.0, 1.0), 1.e-3); } });

		Channels.Add({ TEXT("Shear"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(Key.Transform.Shear, false); },
			[](const FWidgetTransformKeyframe& Key) { return Key.Transform.Shear.IsNearlyZero(1.e-3); } });

		Channels.Add({ TEXT("Angle"),
			[](const FWidgetTransformKeyframe& Key) { return Angle(Key.Transform.Angle); },
			[](const FWidgetTransformKeyframe& Key) { return FMath::IsNearlyZero(Key.Transform.Angle, 1.e-3f); } });

		if (bAnimatesShape)
		{
			Channels.Add({ TEXT("Shape"),
				[](const FWidgetTransformKeyframe& Key) { return Key.bUseAuthoredShape ? FString(TEXT("Authored")) : FString(TEXT("Custom")); },
				[](const FWidgetTransformKeyframe& Key) { return Key.bUseAuthoredShape; } });
		}

		return Channels;
	}

	/** How a keyed track's value reads in a tooltip, by type: an angle in degrees, a color as a swatch and hex, a number. */
	TSharedRef<SWidget> KeyValue(const FWidgetAnimLayer& Layer, int32 Point, const FLinearColor& TextColor)
	{
		// A color: a Material Vector's value, or the color a registered type's key holds (beside its value, when it has one).
		if (WidgetAnimKeyPoints::HasColorKeys(Layer))
		{
			const FLinearColor Color = WidgetAnimKeyPoints::GetColor(Layer, Point);
			const bool bWithValue = Layer.AnimType == EWidgetAnimType::Custom && WidgetAnimGetRegisteredKeyContent(Layer.TypeParams) == EWidgetAnimKeyContent::ValueAndColor;
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))[ Swatch(Color) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Text(TEXT("#") + Color.ToFColor(true).ToHex().Left(6), TextColor, ValueFont()) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 0.f, 0.f, 0.f))
				[
					Text(bWithValue ? Number(WidgetAnimKeyPoints::GetValue(Layer, Point)) : FString(), TextColor, ValueFont())
				];
		}

		const float Value = WidgetAnimKeyPoints::GetValue(Layer, Point);
		return Text(Layer.AnimType == EWidgetAnimType::Rotate ? Angle(Value) : Number(Value), TextColor, ValueFont());
	}

	/** What a keyed track's value is called, by type. */
	FString KeyValueName(EWidgetAnimType AnimType)
	{
		switch (AnimType)
		{
		case EWidgetAnimType::Fade:   return TEXT("Opacity");
		case EWidgetAnimType::Scale:  return TEXT("Scale");
		case EWidgetAnimType::Slide:  return TEXT("Distance");
		case EWidgetAnimType::Rotate: return TEXT("Angle");
		default:                      return TEXT("Value");
		}
	}

	/** Where a key's values are drawn: green at the first key, red at the last, plain between - dimmed where unchanged. */
	FLinearColor KeyValueColor(int32 KeyIndex, int32 NumKeys, bool bIsDefault)
	{
		FLinearColor Color = KeyIndex == 0 ? Palette::TooltipStartValue
			: KeyIndex == NumKeys - 1 ? Palette::TooltipEndValue
			: Palette::TooltipValue;

		if (bIsDefault)
		{
			Color.A *= 0.4f;
		}
		return Color;
	}
}

// =====================================================================================================
// SWidgetAnimTimeline
// =====================================================================================================

namespace
{
	/** Where the row height is remembered: the editor's per-project user settings. */
	const TCHAR* RowHeightConfigSection = TEXT("NameTBD.WidgetAnimations");
	const TCHAR* RowHeightConfigKey = TEXT("RowHeight");
	const TCHAR* WatcherExpandedConfigKey = TEXT("ValueWatcherExpanded");

	// How the panel's splitters divide it, per layout - see SWidgetAnimTimeline::GetSplitSize. The full panel's keys
	// are shared by the drawer and the docked tab, so a layout set in one is the layout in the other.
	const FName SplitMainKey(TEXT("Split.Main"));
	const FName SplitSelectionKey(TEXT("Split.Selection"));
	const FName SplitListKey(TEXT("Split.List"));
	const FName SplitTracksKey(TEXT("Split.Tracks"));
	const FName SplitWindowTracksKey(TEXT("Split.Window.Tracks"));
	const FName SplitWindowSelectionKey(TEXT("Split.Window.Selection"));
	const FName SplitInlineTracksKey(TEXT("Split.Inline.Tracks"));
	const FName SplitInlineSelectionKey(TEXT("Split.Inline.Selection"));
}

namespace
{
	/** A compact panel's selection and view, kept per source between one panel and the next. See SaveCompactState. */
	struct FSavedCompactState
	{
		TWeakPtr<IWidgetAnimPanelSource> Source;
		SWidgetAnimTimeline::FSelection Selection;
		TArray<int32> SelectedLayers;
		TArray<SWidgetAnimTimeline::FKeyRef> SelectedKeys;
		int32 SelectionAnchor = INDEX_NONE;
		FVector2f ViewRange = FVector2f(0.f, 1.f);
		bool bUserView = false;
		float ScrollY = 0.f;
	};

	TMap<const IWidgetAnimPanelSource*, FSavedCompactState>& GetSavedCompactStates()
	{
		static TMap<const IWidgetAnimPanelSource*, FSavedCompactState> States;
		return States;
	}
}

SWidgetAnimTimeline::SWidgetAnimTimeline() = default;

SWidgetAnimTimeline::~SWidgetAnimTimeline()
{
	if (bCompact)
	{
		SaveCompactState();
	}
}

void SWidgetAnimTimeline::SaveCompactState() const
{
	if (!Source.IsValid())
	{
		return;
	}

	TMap<const IWidgetAnimPanelSource*, FSavedCompactState>& States = GetSavedCompactStates();
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!It.Value().Source.IsValid())
		{
			It.RemoveCurrent();
		}
	}

	FSavedCompactState& Saved = States.FindOrAdd(Source.Get());
	Saved.Source = Source;
	Saved.Selection = Selection;
	Saved.SelectedLayers = SelectedLayers;
	Saved.SelectedKeys = SelectedKeys;
	Saved.SelectionAnchor = SelectionAnchor;
	if (Tracks.IsValid())
	{
		Tracks->GetViewState(Saved.ViewRange, Saved.bUserView, Saved.ScrollY);
	}
}

void SWidgetAnimTimeline::RestoreCompactState()
{
	const FSavedCompactState* Saved = Source.IsValid() ? GetSavedCompactStates().Find(Source.Get()) : nullptr;
	if (!Saved || Saved->Source.Pin() != Source)
	{
		return;
	}

	// Whatever no longer exists - undone, or deleted from the plain fields - is dropped by the validation after.
	Selection = Saved->Selection;
	SelectedLayers = Saved->SelectedLayers;
	SelectedKeys = Saved->SelectedKeys;
	SelectionAnchor = Saved->SelectionAnchor;
	Selection = Validate(Selection);
	ValidateSelectedLayers();
	ValidateSelectedKeys();

	if (Tracks.IsValid())
	{
		Tracks->SetViewState(Saved->ViewRange, Saved->bUserView, Saved->ScrollY);
	}
}

float SWidgetAnimTimeline::GetSplitSize(FName Key, float Default) const
{
	if (const float* Known = SplitSizes.Find(Key))
	{
		return *Known;
	}

	// Read once per panel; every panel after that reads what the last one dragged it to.
	float Saved = Default;
	if (!GConfig->GetFloat(RowHeightConfigSection, *Key.ToString(), Saved, GEditorPerProjectIni) || Saved <= 0.f)
	{
		Saved = Default;
	}
	SplitSizes.Add(Key, Saved);
	return Saved;
}

void SWidgetAnimTimeline::SetSplitSize(float NewSize, FName Key)
{
	// Saved as it is dragged, like the line height: in the editor's per-project settings, so the layout stays as it
	// was left through a dock or undock, a closed and reopened panel, and a restart of the editor.
	SplitSizes.Add(Key, NewSize);
	GConfig->SetFloat(RowHeightConfigSection, *Key.ToString(), NewSize, GEditorPerProjectIni);
}

TAttribute<float> SWidgetAnimTimeline::MakeSplitSize(FName Key, float Default)
{
	return TAttribute<float>::CreateSP(this, &SWidgetAnimTimeline::GetSplitSize, Key, Default);
}

SSplitter::FOnSlotResized SWidgetAnimTimeline::MakeSplitResized(FName Key)
{
	return SSplitter::FOnSlotResized::CreateSP(this, &SWidgetAnimTimeline::SetSplitSize, Key);
}

void SWidgetAnimTimeline::Construct(const FArguments& InArgs)
{
	using namespace WidgetAnimTimeline;

	Source = InArgs._Source;
	OnDockInLayout = InArgs._OnDockInLayout;
	bCompact = InArgs._Compact;
	bInline = InArgs._Inline;
	IsCollapsedAttr = InArgs._IsCollapsed;
	OnToggleCollapsed = InArgs._OnToggleCollapsed;
	CompactTitle = InArgs._Title;
	CompactTitleToolTip = InArgs._TitleToolTip;
	OnExpand = InArgs._OnExpand;

	float SavedRowHeight = DefaultRowHeight;
	if (GConfig->GetFloat(RowHeightConfigSection, RowHeightConfigKey, SavedRowHeight, GEditorPerProjectIni))
	{
		RowHeight = FMath::Clamp(SavedRowHeight, MinRowHeight, MaxRowHeight);
	}

	// The Selection panel: the edited object, laid out by FWidgetAnimSelectionDetails. No notify hook, so
	// UMG's designer never hears of an edit made here - which is what keeps it from rebuilding its preview,
	// and every Details panel with it, on every commit.
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.bLockable = false;
	DetailsArgs.bUpdatesFromSelection = false;
	DetailsArgs.bShowPropertyMatrixButton = false;
	DetailsArgs.bShowOptions = false;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.NotifyHook = nullptr;

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);
	SelectionLayout = FOnGetDetailCustomizationInstance::CreateLambda([WeakThisPtr]() -> TSharedRef<IDetailCustomization>
	{
		return MakeShared<FWidgetAnimSelectionDetails>(WeakThisPtr);
	});
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimBuilder::StaticClass(), SelectionLayout);
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimPresetAsset::StaticClass(), SelectionLayout);

	// Several layers selected: their proxies, shown together so the view merges what they share. See
	// FWidgetAnimMultiLayerDetails.
	MultiDetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
	MultiDetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimLayerEditProxy::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakThisPtr]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FWidgetAnimMultiLayerDetails>(WeakThisPtr);
		}));

	// Read-only - the panel locked, or a locked track selected - grays every field out, rather than hiding it:
	// what is selected can still be looked at.
	const FIsPropertyEditingEnabled IsEditingEnabled = FIsPropertyEditingEnabled::CreateLambda([WeakThisPtr]()
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakThisPtr.Pin();
		return Pinned && Pinned->IsSelectionEditable();
	});
	DetailsView->SetIsPropertyEditingEnabledDelegate(IsEditingEnabled);
	MultiDetailsView->SetIsPropertyEditingEnabledDelegate(IsEditingEnabled);

	// The Track Editor's horizontal scroll bar, which moves its view along the timeline - see
	// SWidgetAnimTimelineTracks::ScrollHorizontally. Always shown, so the tracks don't jump when it is needed.
	TracksHorizontalScrollBar = SNew(SScrollBar)
		.Orientation(Orient_Horizontal)
		.AlwaysShowScrollbar(true)
		.OnUserScrolled_Lambda([this](float Offset)
		{
			if (Tracks.IsValid())
			{
				Tracks->ScrollHorizontally(Offset);
			}
		});

	Tracks = SNew(SWidgetAnimTimelineTracks, WeakThisPtr, TracksHorizontalScrollBar);

	if (bCompact)
	{
		// The compact panel: the tracks, with the Selection panel beside them, under its own header strip. Folded, only
		// the strip shows. Inline, the transport is a slim row under the tracks alone, so the Selection panel runs the
		// whole height beside them; in its own window, the full transport runs under both.
		const TAttribute<EVisibility> BodyVisibility = TAttribute<EVisibility>::CreateLambda([this]()
		{
			return IsBodyCollapsed() ? EVisibility::Collapsed : EVisibility::Visible;
		});

		TracksHorizontalScrollBar->SetVisibility(IsInline() ? EVisibility::Collapsed : EVisibility::Visible);

		ChildSlot
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeCompactHeader()
			]

			+ SVerticalBox::Slot()
			.FillHeight(1.f)
			[
				SNew(SBox)
				.Visibility(BodyVisibility)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					.PhysicalSplitterHandleSize(2.f)

					+ SSplitter::Slot()
					.Value(MakeSplitSize(IsInline() ? SplitInlineTracksKey : SplitWindowTracksKey, 0.62f))
					.OnSlotResized(MakeSplitResized(IsInline() ? SplitInlineTracksKey : SplitWindowTracksKey))
					[
						SNew(SVerticalBox)

						+ SVerticalBox::Slot()
						.FillHeight(1.f)
						[
							SNew(SOverlay)

							+ SOverlay::Slot()
							[
								SNew(SVerticalBox)

								+ SVerticalBox::Slot()
								.FillHeight(1.f)
								[
									Tracks.ToSharedRef()
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									TracksHorizontalScrollBar.ToSharedRef()
								]
							]

							// Nothing to edit, or nothing yet: say what to do.
							+ SOverlay::Slot()
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							.Padding(FMargin(12.f, RulerHeight, 12.f, 0.f))
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Justification(ETextJustify::Center)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Visibility_Lambda([this]()
								{
									const TArray<FWidgetAnimLayer>* Layers = GetLayers();
									return !Layers || Layers->Num() == 0 ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
								})
								.Text_Lambda([this]()
								{
									return GetLayers() ? WidgetAnimTips::CompactNoTracks() : WidgetAnimTips::NothingToEdit();
								})
							]
						]

						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							IsInline() ? MakeInlineTransport() : SNullWidget::NullWidget
						]
					]

					+ SSplitter::Slot()
					.Value(MakeSplitSize(IsInline() ? SplitInlineSelectionKey : SplitWindowSelectionKey, 0.38f))
					.OnSlotResized(MakeSplitResized(IsInline() ? SplitInlineSelectionKey : SplitWindowSelectionKey))
					[
						MakeSelectionPanel()
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.Visibility(BodyVisibility)
				[
					IsInline() ? SNullWidget::NullWidget : MakeTransport()
				]
			]
		];

		RebuildRows();
		RestoreCompactState();
		RefreshDetails();

		RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimTimeline::OnRefreshTimer));
		return;
	}

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeHeader()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(2.f)

			+ SSplitter::Slot()
			.Value(MakeSplitSize(SplitMainKey, 0.7f))
			.OnSlotResized(MakeSplitResized(SplitMainKey))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SOverlay)

					// The Animation Layers list and the Track Editor, side by side, with a splitter between them.
					+ SOverlay::Slot()
					[
						SNew(SSplitter)
						.Orientation(Orient_Horizontal)
						.PhysicalSplitterHandleSize(2.f)

						+ SSplitter::Slot()
						.Value(MakeSplitSize(SplitListKey, 0.27f))
						.OnSlotResized(MakeSplitResized(SplitListKey))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeLayerListHeader()
							]

							// Its own scroll, with its own scroll bar when it runs past the bottom.
							+ SVerticalBox::Slot()
							.FillHeight(1.f)
							[
								// A right-click on the list anywhere but a row - the empty space below the last - offers
								// everything that can be added, to any group. See SWidgetAnimListArea.
								SNew(SWidgetAnimListArea, WeakThisPtr)
								[
									SNew(SBorder)
									.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
									.Padding(FMargin(0.f))
									[
										SNew(SScrollBox)
										.Orientation(Orient_Vertical)

										+ SScrollBox::Slot()
										[
											SAssignNew(LayerList, SVerticalBox)
										]
									]
								]
							]

							// Pinned along the bottom, whatever the list scrolls to.
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeAnimationPropertiesButton()
							]
						]

						+ SSplitter::Slot()
						.Value(MakeSplitSize(SplitTracksKey, 0.73f))
						.OnSlotResized(MakeSplitResized(SplitTracksKey))
						[
							SNew(SVerticalBox)

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeTrackEditorHeader()
							]

							+ SVerticalBox::Slot()
							.FillHeight(1.f)
							[
								Tracks.ToSharedRef()
							]

							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								TracksHorizontalScrollBar.ToSharedRef()
							]
						]
					]

					// With no animation to show - an asset editor that hasn't picked one yet - say what to do.
					+ SOverlay::Slot()
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Visibility_Lambda([this]() { return GetGroups() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
						.Text(WidgetAnimTips::NothingToEdit())
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeTransport()
				]
			]

			+ SSplitter::Slot()
			.Value(MakeSplitSize(SplitSelectionKey, 0.3f))
			.OnSlotResized(MakeSplitResized(SplitSelectionKey))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					MakeSelectionPanel()
				]

				// What one value of one widget is doing as the animation plays.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					MakeValueWatcher()
				]
			]
		]
	];

	RebuildRows();
	RefreshDetails();

	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimTimeline::OnRefreshTimer));
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeSelectionPanel()
{
	using namespace WidgetAnimTimeline;

	return SNew(SVerticalBox)

		// The Selection panel's header, on the header color and as tall as the strips naming the list and
		// the Track Editor beside it. Not inline, where room is scarce and the selected track is plain to see.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility(IsInline() ? EVisibility::Collapsed : EVisibility::Visible)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 0.f))
			[
				SNew(SBox)
				.HeightOverride(SectionHeaderHeight)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						MakeFixedIcon(PickBrush({ "LevelEditor.Tabs.Details", "Icons.Details" }), FSlateColor::UseSubduedForeground())
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([this]() { return GetDetailsTitle(); })
					]
				]
			]
		]

		// Inline, its rows at the density of the Details panel around it rather than a size up: the Details it sits in
		// already spend a row's height on every field, so it is scaled down to fit more of the track in view.
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SDPIScaler)
			.DPIScale(IsInline() ? InlineSelectionScale : 1.f)
			[
			SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]()
			{
				if (IsKeyMultiSelection())
				{
					return 2;
				}
				if (IsMultiSelection())
				{
					return 1;
				}

				// The compact panel with nothing selected has no animation settings to show - just what to do.
				const bool bNothingSelected = Selection.Group == INDEX_NONE && Selection.Layer == INDEX_NONE;
				return bCompact && bNothingSelected ? 3 : 0;
			})

			+ SWidgetSwitcher::Slot()
			[
				DetailsView.ToSharedRef()
			]

			+ SWidgetSwitcher::Slot()
			[
				MultiDetailsView.ToSharedRef()
			]

			// Several keys: the curve between them.
			+ SWidgetSwitcher::Slot()
			[
				MakeKeyCurvePanel()
			]

			// The compact panel, nothing selected.
			+ SWidgetSwitcher::Slot()
			[
				SNew(SBox)
				.Padding(FMargin(8.f, 6.f))
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this]() { return GetLayers() ? WidgetAnimTips::CompactNothingSelected() : FText::GetEmpty(); })
				]
			]
			]
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeInlineTransport()
{
	using namespace WidgetAnimTimeline;

	// Small borderless buttons - smaller than the full transport's, big enough to hit without looking.
	constexpr float InlineIconSize = 14.f;
	auto MakeSmallButton = [](TAttribute<const FSlateBrush*> Icon, const FText& Fallback, const FText& ToolTip, TAttribute<FSlateColor> Tint, TFunction<void()> OnClicked) -> TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> Content = Icon.Get()
			? MakeFixedIcon(Icon, Tint, InlineIconSize)
			: StaticCastSharedRef<SWidget>(SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8)).Text(Fallback));

		return SNew(SWidgetAnimAnchoredTip)
			.Text(ToolTip)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(3.f, 2.f))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.OnClicked_Lambda([OnClicked]() { OnClicked(); return FReply::Handled(); })
				[
					Content
				]
			];
	};

	const TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	auto Add = [&Row](const TSharedRef<SWidget>& Widget, const FMargin& Padding)
	{
		Row->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(Padding)
		[
			Widget
		];
	};

	Add(MakeSmallButton(PickBrush({ "Animation.Backward_End", "Sequencer.Transport.GoToFront.Normal" }), LOCTEXT("InlineToStart", "|<"), WidgetAnimTips::ToStartTooltip(),
		FSlateColor::UseForeground(), [this]() { ScrubToSeconds(0.f); }), FMargin(0.f));
	Add(MakeSmallButton(PickBrush({ "Animation.Backward_Step", "Sequencer.Transport.StepBackwards.Normal" }), LOCTEXT("InlinePrevKey", "<"), WidgetAnimTips::PrevKeyTooltip(),
		FSlateColor::UseForeground(), [this]() { StepToMarker(-1); }), FMargin(0.f));
	Add(MakeSmallButton(
		TAttribute<const FSlateBrush*>::CreateLambda([this]()
		{
			return IsPlaying()
				? PickBrush({ "Icons.Pause", "PlayWorld.PausePlaySession", "GenericPause" })
				: PickBrush({ "Icons.Play", "PlayWorld.ResumePlaySession", "GenericPlay" });
		}),
		LOCTEXT("InlinePlay", "Play"),
		WidgetAnimTips::PlayTooltip(),
		TAttribute<FSlateColor>::CreateLambda([this]() { return FSlateColor(IsPlaying() ? PauseTint : PlayTint); }),
		[this]() { TogglePlay(); }), FMargin(0.f));
	Add(MakeSmallButton(PickBrush({ "Animation.Forward_Step", "Sequencer.Transport.StepForward.Normal" }), LOCTEXT("InlineNextKey", ">"), WidgetAnimTips::NextKeyTooltip(),
		FSlateColor::UseForeground(), [this]() { StepToMarker(1); }), FMargin(0.f));
	Add(MakeSmallButton(PickBrush({ "Animation.Forward_End", "Sequencer.Transport.GoToEnd.Normal" }), LOCTEXT("InlineToEnd", ">|"), WidgetAnimTips::ToEndTooltip(),
		FSlateColor::UseForeground(), [this]() { ScrubToSeconds(GetPassSeconds()); }), FMargin(0.f));

	// The time, right beside the buttons.
	Add(SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2f"), GetPlayheadSeconds())); }),
		FMargin(6.f, 0.f, 0.f, 0.f));
	Add(SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT(" / %.2f s"), GetPassSeconds())); }),
		FMargin(0.f));

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(2.f, 0.f))
		[
			Row
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeHeader()
{
	using namespace WidgetAnimTimeline;

	TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);

	using EMode = IWidgetAnimPanelSource::EMode;
	auto IsEditingExisting = [this]() { return Source.IsValid() && Source->GetMode() == EMode::EditExistingAsset; };

	auto SwitchMode = [this](EMode Mode)
	{
		if (Source.IsValid() && Source->GetMode() != Mode)
		{
			Source->SetMode(Mode);
			ClearSelection();
			RebuildRows();
		}
	};

	// New Animation or Edit Existing Asset: what the panel is working on.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		SNew(SComboButton)
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ModeTooltip()))
		.OnGetMenuContent_Lambda([SwitchMode]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			Menu.AddMenuEntry(
				LOCTEXT("NewAnimation", "New Animation"),
				WidgetAnimTips::NewAnimationTooltip(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([SwitchMode]() { SwitchMode(EMode::NewAnimation); })));
			Menu.AddMenuEntry(
				LOCTEXT("EditExistingAsset", "Edit Existing Asset"),
				WidgetAnimTips::EditExistingAssetTooltip(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([SwitchMode]() { SwitchMode(EMode::EditExistingAsset); })));
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Text_Lambda([IsEditingExisting]()
			{
				return IsEditingExisting() ? LOCTEXT("EditExistingAsset", "Edit Existing Asset") : LOCTEXT("NewAnimation", "New Animation");
			})
		]
	];

	// Edit Existing Asset: which asset. A fixed width, not a capped fill - a capped fill slot leaves the rest
	// of its share of the row unused, which pushed Dock in Layout and the lock in from the right edge.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(320.f)
		.Visibility_Lambda([IsEditingExisting]() { return IsEditingExisting() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UWidgetAnimPresetAsset::StaticClass())
			.AllowClear(true)
			.DisplayThumbnail(false)
			.ObjectPath_Lambda([this]()
			{
				const UWidgetAnimPresetAsset* Asset = Source.IsValid() ? Source->GetAsset() : nullptr;
				return Asset ? Asset->GetPathName() : FString();
			})
			.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
			{
				if (Source.IsValid())
				{
					Source->SetAsset(Cast<UWidgetAnimPresetAsset>(AssetData.GetAsset()));
					ClearSelection();
					RebuildRows();
				}
			})
		]
	];

	// New Animation in a builder: the name it will be saved under, from Selection: Animation.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Visibility_Lambda([this, IsEditingExisting]() { return Source.IsValid() && Source->IsBuilder() && !IsEditingExisting() ? EVisibility::Visible : EVisibility::Collapsed; })
		.Font(FAppStyle::GetFontStyle("NormalFontBold"))
		.Text_Lambda([this]()
		{
			const UWidgetAnimBuilder* Builder = Source.IsValid() ? Source->GetBuilder() : nullptr;
			return Builder && !Builder->AnimationName.IsEmpty()
				? FText::FromString(Builder->AnimationName)
				: LOCTEXT("Title", "Widget Animation");
		})
	];

	Header->AddSlot()
	.FillWidth(1.f)
	[
		SNew(SSpacer)
	];

	// Save as Animation Asset in New Animation; Update Animation Asset in Edit Existing Asset.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.f, 0.f))
	[
		SNew(SButton)
		.ButtonColorAndOpacity(FLinearColor(NameTBD::Colors::AnimationOrange_Normal))
		.IsEnabled_Lambda([this, IsEditingExisting]()
		{
			return Source.IsValid() && (IsEditingExisting() ? Source->CanUpdateAsset() : Source->CanSaveAsNewAsset());
		})
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this, IsEditingExisting]()
		{
			if (IsEditingExisting())
			{
				return WidgetAnimTips::UpdateTooltip();
			}
			return Source.IsValid() && Source->IsBuilder()
				? WidgetAnimTips::SaveAsBuilderTooltip()
				: WidgetAnimTips::SaveAsTooltip();
		})))
		.OnClicked_Lambda([this, IsEditingExisting]()
		{
			if (!Source.IsValid())
			{
				return FReply::Handled();
			}

			if (IsEditingExisting())
			{
				Source->UpdateAsset();
			}
			else if (Source->SaveAsNewAsset())
			{
				ClearSelection();
				RebuildRows();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.ColorAndOpacity(FLinearColor::Black)
			.Text_Lambda([IsEditingExisting]()
			{
				return IsEditingExisting() ? LOCTEXT("UpdateAsset", "Update Animation Asset") : LOCTEXT("SaveAsAsset", "Save as Animation Asset");
			})
		]
	];

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.Visibility(OnDockInLayout.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::DockTooltip()))
		.OnClicked_Lambda([this]()
		{
			OnDockInLayout.ExecuteIfBound();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(PickBrush({ "Icons.Layout", "LevelEditor.Tabs.Viewports", "Icons.Toolbar.Details" }))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(LOCTEXT("DockInLayout", "Dock in Layout"))
			]
		]
	];

	// The lock: the whole animation read-only - a preview. Everything that edits it is grayed out; playing,
	// scrubbing, selecting and looking at what is selected all still work.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 2.f))
		.IsEnabled_Lambda([this]() { return HasAnimation(); })
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
		{
			return IsReadOnly()
				? WidgetAnimTips::UnlockTooltip()
				: WidgetAnimTips::LockTooltip();
		})))
		.OnClicked_Lambda([this]()
		{
			SetReadOnly(!IsReadOnly());
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeFixedIcon(
					TAttribute<const FSlateBrush*>::CreateLambda([this]()
					{
						return IsReadOnly()
							? PickBrush({ "Icons.Lock", "PropertyWindow.Locked" })
							: PickBrush({ "Icons.Unlock", "PropertyWindow.Unlocked" });
					}),
					TAttribute<FSlateColor>::CreateLambda([this]()
					{
						return IsReadOnly() ? FSlateColor(Palette::ReadOnlyLabel) : FSlateColor::UseForeground();
					}))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Visibility_Lambda([this]() { return IsReadOnly() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity(Palette::ReadOnlyLabel)
				.Text(LOCTEXT("ReadOnlyLabel", "Read Only"))
			]
		]
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.f, 4.f))
		[
			Header
		];
}

namespace
{
	/** The strip across the top of the list or the Track Editor: its name, small and quiet, and room for its buttons. */
	TSharedRef<SWidget> MakeSectionHeader(const FText& Label, const TSharedRef<SWidget>& Right)
	{
		using namespace WidgetAnimTimeline;

		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 0.f, 4.f, 0.f))
			[
				SNew(SBox)
				.HeightOverride(SectionHeaderHeight)
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text(Label)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Right
					]
				]
			];
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeCompactHeader()
{
	using namespace WidgetAnimTimeline;

	// A small text button in the strip, its icon beside its label - the strip is too narrow for the full header's
	// wide buttons, and too crowded for icons alone to say what they do.
	auto MakeLabel = [](const FSlateBrush* Icon, const FText& Label, const FSlateColor& IconColor) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (Icon)
		{
			Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(Icon, IconColor)
			];
		}
		Box->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.Text(Label)
		];
		return Box;
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(8.f, 0.f, 4.f, 0.f))
		[
			SNew(SBox)
			.HeightOverride(SectionHeaderHeight)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				// Fold or unfold - the arrow, or the name beside it.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(1.f))
					.Visibility(OnToggleCollapsed.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
					.ToolTipText(WidgetAnimTips::CompactFoldTooltip())
					.OnClicked_Lambda([this]()
					{
						OnToggleCollapsed.ExecuteIfBound();
						return FReply::Handled();
					})
					[
						MakeFixedIcon(
							TAttribute<const FSlateBrush*>::CreateLambda([this]()
							{
								return IsBodyCollapsed()
									? PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" })
									: PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" });
							}),
							FSlateColor::UseSubduedForeground(), 12.f)
					]
				]

				// What the list is. Clicking it folds or unfolds, as a Details category's name does.
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(0.f))
					.OnClicked_Lambda([this]()
					{
						OnToggleCollapsed.ExecuteIfBound();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text(CompactTitle)
						.ToolTipText(CompactTitleToolTip)
					]
				]

				// When the list starts: right away, or within one of the animations it plays alongside.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f))
				[
					SNew(SComboButton)
					.Visibility_Lambda([this]()
					{
						return Source.IsValid() && Source->SupportsStartOptions() && !IsBodyCollapsed() ? EVisibility::Visible : EVisibility::Collapsed;
					})
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.ContentPadding(FMargin(4.f, 1.f))
					.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::AdditiveStartTooltip()))
					.OnGetMenuContent_Lambda([this]() { return MakeStartMenu(); })
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
						[
							SNew(STextBlock)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(LOCTEXT("StartsLabel", "Starts"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
							.MaxDesiredWidth(170.f)
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Text_Lambda([this]() { return GetStartLabel(); })
								.ColorAndOpacity_Lambda([this]()
								{
									const bool bWithin = Source.IsValid() && Source->GetStartOption() != INDEX_NONE;
									return bWithin ? FSlateColor(Palette::StartLinkedText) : FSlateColor::UseForeground();
								})
							]
						]
					]
				]

				// + Add Track: every track type, and the animations the list can be built on.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f))
				[
					SNew(SComboButton)
					.Visibility_Lambda([this]() { return IsBodyCollapsed() ? EVisibility::Collapsed : EVisibility::Visible; })
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.IsEnabled_Lambda([this]() { return CanEdit(); })
					.HasDownArrow(false)
					.ContentPadding(FMargin(4.f, 1.f))
					.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::CompactAddTooltip()))
					.OnGetMenuContent_Lambda([this]()
					{
						return MakeAddMenu(INDEX_NONE);
					})
					.ButtonContent()
					[
						MakeLabel(FAppStyle::Get().GetBrush("Icons.Plus"), LOCTEXT("CompactAddTrack", "Add Track"), FStyleColors::AccentGreen)
					]
				]

				// Save as Animation: a WA_ asset of the list - and, where the list can hand itself over to one, an
				// entry playing it in its place.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f))
				[
					SNew(SComboButton)
					.Visibility_Lambda([this]() { return IsBodyCollapsed() ? EVisibility::Collapsed : EVisibility::Visible; })
					.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
					.IsEnabled_Lambda([this]() { return Source.IsValid() && Source->CanSaveAsNewAsset(); })
					.ContentPadding(FMargin(4.f, 1.f))
					.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::CompactSaveTooltip()))
					.OnGetMenuContent_Lambda([this]()
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
						Menu.AddMenuEntry(
							LOCTEXT("CompactSaveAsAsset", "Save as Animation Asset"),
							WidgetAnimTips::CompactSaveAsAssetTooltip(),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
							FUIAction(FExecuteAction::CreateLambda([this]() { SaveCompactList(/*bReplaceWithEntry=*/false); })));

						if (Source.IsValid() && Source->CanReplaceWithEntry())
						{
							Menu.AddMenuEntry(
								Source->GetReplaceWithEntryLabel(),
								WidgetAnimTips::CompactReplaceWithEntryTooltip(),
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([this]() { SaveCompactList(/*bReplaceWithEntry=*/true); }),
									FCanExecuteAction::CreateLambda([this]() { return CanEdit(); })));
						}
						return Menu.MakeWidget();
					})
					.ButtonContent()
					[
						MakeLabel(PickBrush({ "Icons.Save", "AssetEditor.SaveAsset" }), LOCTEXT("CompactSave", "Save as Animation"), FSlateColor(NameTBD::Colors::AnimationOrange_Normal))
					]
				]

				// Expand: the same list, in a window of its own that can be made as big as it needs.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(4.f, 1.f))
					.Visibility(OnExpand.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
					.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::CompactExpandTooltip()))
					.OnClicked_Lambda([this]()
					{
						OnExpand.ExecuteIfBound();
						return FReply::Handled();
					})
					[
						MakeLabel(PickBrush({ "Icons.Fullscreen", "Icons.Expand", "EditorViewport.ToggleMaximize", "Icons.Maximize" }), LOCTEXT("CompactExpand", "Expand"), FSlateColor::UseForeground())
					]
				]
			]
		];
}

FText SWidgetAnimTimeline::GetStartLabel() const
{
	const int32 Current = Source.IsValid() ? Source->GetStartOption() : INDEX_NONE;
	if (Current == INDEX_NONE)
	{
		return LOCTEXT("StartRightAway", "Right Away");
	}

	TArray<IWidgetAnimPanelSource::FStartOption> Options;
	Source->GetStartOptions(Options);
	const IWidgetAnimPanelSource::FStartOption* Found = Options.FindByPredicate([Current](const IWidgetAnimPanelSource::FStartOption& Option) { return Option.Id == Current; });
	return Found ? FText::Format(LOCTEXT("StartWithin", "Within {0}"), Found->Label) : LOCTEXT("StartRightAway", "Right Away");
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeStartMenu()
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	if (!Source.IsValid())
	{
		return Menu.MakeWidget();
	}

	TArray<IWidgetAnimPanelSource::FStartOption> Options;
	Source->GetStartOptions(Options);
	const int32 Current = Source->GetStartOption();

	auto AddOption = [this, &Menu, Current](const FText& Label, const FText& ToolTip, int32 Id)
	{
		Menu.AddMenuEntry(
			Label,
			ToolTip,
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Id]()
				{
					if (Source.IsValid() && CanEdit())
					{
						Source->SetStartOption(Id);
						RebuildRows();
					}
				}),
				FCanExecuteAction::CreateLambda([this]() { return CanEdit(); }),
				FIsActionChecked::CreateLambda([Current, Id]() { return Current == Id; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	};

	Menu.BeginSection("Start", LOCTEXT("StartSection", "Start"));
	AddOption(LOCTEXT("StartRightAway", "Right Away"), WidgetAnimTips::AdditiveStartRightAwayTooltip(), INDEX_NONE);
	Menu.EndSection();

	// Every animation the list could play within - its own direction's entries, then those mirrored into it.
	Menu.BeginSection("Within", LOCTEXT("StartWithinSection", "Within an Animation"));
	if (Options.Num() == 0)
	{
		Menu.AddWidget(
			SNew(SBox)
			.Padding(FMargin(8.f, 2.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(WidgetAnimTips::AdditiveStartNoAnimations())
			],
			FText::GetEmpty());
	}
	for (const IWidgetAnimPanelSource::FStartOption& Option : Options)
	{
		AddOption(Option.Label, Option.ToolTip, Option.Id);
	}
	Menu.EndSection();

	return Menu.MakeWidget();
}

void SWidgetAnimTimeline::SaveCompactList(bool bReplaceWithEntry)
{
	if (!Source.IsValid())
	{
		return;
	}

	UWidgetAnimPresetAsset* Saved = bReplaceWithEntry ? Source->SaveAndReplaceWithEntry() : Source->SaveAsNewAsset();
	if (!Saved)
	{
		return;
	}

	// The asset is selected in the Content Browser; a toast says what it is called, since nothing here changes to show it.
	FNotificationInfo Info(FText::Format(
		bReplaceWithEntry ? WidgetAnimTips::CompactReplacedNotice() : WidgetAnimTips::CompactSavedNotice(),
		FText::FromString(Saved->GetName())));
	Info.ExpireDuration = 5.f;
	FSlateNotificationManager::Get().AddNotification(Info);

	// Handed over: the list is empty now.
	if (bReplaceWithEntry)
	{
		ClearSelection();
		RebuildRows();
		RefreshDetails();
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeLayerListHeader()
{
	using namespace WidgetAnimTimeline;

	// + Add, built the way the editor's own green "+ Add" buttons are - a plus in the accent green beside the
	// label, on a plain combo button - rather than with SPositiveActionButton, which would add another module
	// to link against for one button.
	return MakeSectionHeader(LOCTEXT("AnimationLayersHeader", "Animation Tracks"),
		SNew(SComboButton)
		.IsEnabled_Lambda([this]() { return CanEdit(); })
		.HasDownArrow(false)
		.ContentPadding(FMargin(6.f, 1.f))
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::AddTooltip()))
		.OnGetMenuContent_Lambda([this]()
		{
			return MakeAddMenu(Selection.Group);
		})
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(FAppStyle::Get().GetBrush("Icons.Plus"), FStyleColors::AccentGreen)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text(LOCTEXT("Add", "Add"))
			]
		]);
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeAnimationPropertiesButton()
{
	using namespace WidgetAnimTimeline;

	// Italic, and a faint orange until hovered - a quiet way into the animation's own settings (its Speed
	// Multiplier, its description), which clicking off every track also selects.
	const TSharedRef<TWeakPtr<SWidget>> ButtonWidget = MakeShared<TWeakPtr<SWidget>>();
	const TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(8.f, 5.f))
		.HAlign(HAlign_Center)
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::AnimationPropertiesTooltip()))
		.OnClicked_Lambda([this]()
		{
			ClearSelection();
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.Text(LOCTEXT("AnimationProperties", "Animation Properties"))
			.ColorAndOpacity_Lambda([this, ButtonWidget]()
			{
				const TSharedPtr<SWidget> Pinned = ButtonWidget->Pin();
				const bool bLit = (Pinned.IsValid() && Pinned->IsHovered())
					|| (Selection.Group == INDEX_NONE && Selection.Layer == INDEX_NONE && HasAnimation());
				return FSlateColor(WithAlpha(Palette::AnimationPropertiesText, bLit ? 1.f : Palette::AnimationPropertiesText.A));
			})
		];
	*ButtonWidget = Button;

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity(SeparatorColor)
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(0.f))
			[
				Button
			]
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTrackEditorHeader()
{
	using namespace WidgetAnimTimeline;

	// Fit: the whole animation back in view - the F key, or a double-click on the ruler, does the same. The graph
	// editors' own Zoom to Fit icon, under whichever name this engine version has it; "Fit" if it has none.
	return MakeSectionHeader(LOCTEXT("TrackEditorHeader", "Track Editor"),
		MakeIconButton(
			{ "GraphEditor.ZoomToFit", "GenericCurveEditor.ZoomToFit", "CurveEditor.ZoomToFit", "Icons.ZoomToFit", "Sequencer.ZoomToFit" },
			LOCTEXT("Fit", "Fit"),
			WidgetAnimTips::FitTooltip(),
			FOnClicked::CreateLambda([this]()
			{
				if (Tracks.IsValid())
				{
					Tracks->FitToAnimation();
				}
				return FReply::Handled();
			}),
			FitIconSize));
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTransport()
{
	using namespace WidgetAnimTimeline;

	// One segment of a controls pill: an icon when there is one, else its text, lit with a rounded chip while on.
	auto MakePillToggle = [](const FText& Label, const FText& ToolTip,
		TFunction<bool()> IsOn,
		TFunction<void(bool)> SetOn,
		TFunction<bool()> IsEnabled,
		const FSlateBrush* Icon = nullptr) -> TSharedRef<SWidget>
	{
		const TSharedRef<SWidget> Content = Icon
			? MakeFixedIcon(Icon, TAttribute<FSlateColor>::CreateLambda([IsOn]() { return FSlateColor(IsOn() ? Palette::ToggleOnText : Palette::ToggleOffText); }), 14.f)
			: StaticCastSharedRef<SWidget>(SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity_Lambda([IsOn]() { return FSlateColor(IsOn() ? Palette::ToggleOnText : Palette::ToggleOffText); })
				.Text(Label));

		// Its tip pinned above it, as the transport's are.
		return SNew(SWidgetAnimAnchoredTip)
			.Text(ToolTip)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(0.f))
				.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); })
				.OnClicked_Lambda([IsOn, SetOn]()
				{
					SetOn(!IsOn());
					return FReply::Handled();
				})
				[
					SNew(SBorder)
					.BorderImage(ControlsSegmentBrush())
					.BorderBackgroundColor_Lambda([IsOn]() { return FSlateColor(IsOn() ? Palette::ToggleOnBackground : FLinearColor::Transparent); })
					.Padding(Icon ? FMargin(9.f, 3.f) : FMargin(12.f, 3.f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						Content
					]
				]
			];
	};

	// A segment of the Animation Controls pill: how the preview plays, which the source keeps. Shown as an
	// icon - the plugin's own from Resources if one has been dropped in, else the editor's - or as its text.
	auto MakeSegment = [this, MakePillToggle](const FText& Label, const FText& ToolTip,
		TFunction<bool(const IWidgetAnimPanelSource&)> Getter,
		TFunction<void(IWidgetAnimPanelSource&, bool)> Setter,
		TFunction<bool(const IWidgetAnimPanelSource&)> Enabler,
		const FSlateBrush* Icon) -> TSharedRef<SWidget>
	{
		return MakePillToggle(Label, FText::Format(WidgetAnimTips::SegmentTooltip(), Label, ToolTip),
			[this, Getter]() { return Source.IsValid() && Getter(*Source); },
			[this, Setter](bool bValue) { if (Source.IsValid()) { Setter(*Source, bValue); } },
			[this, Enabler]() { return Source.IsValid() && (!Enabler || Enabler(*Source)); },
			Icon);
	};

	// The thin line between two segments of the pill.
	auto MakeDivider = []() -> TSharedRef<SWidget>
	{
		return SNew(SBox)
			.WidthOverride(1.f)
			.HeightOverride(16.f)
			.Padding(FMargin(0.f))
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity(Palette::ControlsDivider)
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.f, 4.f))
		[
			SNew(SHorizontalBox)

			// Left third: how it plays, as an entry using it would - one pill, like a media player's controls.
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
				[
					SNew(STextBlock)
					.Visibility(bCompact ? EVisibility::Collapsed : EVisibility::Visible)
					.Text(LOCTEXT("AnimationControls", "Animation Controls"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(ControlsPillBrush())
					.Padding(FMargin(3.f, 2.f))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							MakeSegment(LOCTEXT("Loop", "Loop"),
								WidgetAnimTips::LoopTooltip(),
								[](const IWidgetAnimPanelSource& In) { return In.GetLoop(); },
								[](IWidgetAnimPanelSource& In, bool bValue) { In.SetLoop(bValue); },
								nullptr,
								FindControlIcon(TEXT("WidgetAnim_Loop"), { "Animation.Loop.Enabled", "Animation.Loop", "Icons.Loop", "GenericLoop" }))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(3.f, 0.f))
						[
							MakeDivider()
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							MakeSegment(LOCTEXT("PingPong", "Ping Pong"),
								WidgetAnimTips::PingPongTooltip(),
								[](const IWidgetAnimPanelSource& In) { return In.GetPingPong(); },
								[](IWidgetAnimPanelSource& In, bool bValue) { In.SetPingPong(bValue); },
								[](const IWidgetAnimPanelSource& In) { return In.GetLoop(); },
								FindControlIcon(TEXT("WidgetAnim_PingPong"), { "Animation.PingPong", "Icons.PingPong" }))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(3.f, 0.f))
						[
							MakeDivider()
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							MakeSegment(LOCTEXT("Invert", "Invert"),
								WidgetAnimTips::InvertTooltip(),
								[](const IWidgetAnimPanelSource& In) { return In.GetInvert(); },
								[](IWidgetAnimPanelSource& In, bool bValue) { In.SetInvert(bValue); },
								nullptr,
								FindControlIcon(TEXT("WidgetAnim_Invert"), { "Icons.Reverse", "Animation.Backward" }))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(3.f, 0.f))
						[
							MakeDivider()
						]

						// Speed, as the last segment: its label styled as the others, and a compact number beside it.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(12.f, 0.f, 6.f, 0.f))
						[
							SNew(STextBlock)
							.Visibility(bCompact ? EVisibility::Collapsed : EVisibility::Visible)
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(Palette::ToggleOffText)
							.Text(LOCTEXT("PlaybackSpeed", "Playback Speed"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
						[
							SNew(SWidgetAnimAnchoredTip)
							.Text(WidgetAnimTips::SpeedTooltip())
							[
							SNew(SBox)
							.WidthOverride(52.f)
							[
								SNew(SSpinBox<float>)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
								.MinValue(0.01f)
								.MaxValue(10.f)
								.MinSliderValue(0.1f)
								.MaxSliderValue(4.f)
								.Delta(0.05f)
								.Value_Lambda([this]() { return Source.IsValid() ? Source->GetSpeed() : 1.f; })
								.OnValueChanged_Lambda([this](float NewValue) { if (Source.IsValid()) { Source->SetSpeed(NewValue); } })
								.OnValueCommitted_Lambda([this](float NewValue, ETextCommit::Type) { if (Source.IsValid()) { Source->SetSpeed(NewValue); } })
							]
							]
						]
					]
				]
			]

			// Middle: the transport, centered under the tracks as in the native editor, and a size up from the
			// rest of the panel's icons - they are the controls used most.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Backward_End", "Sequencer.Transport.GoToFront.Normal" }, LOCTEXT("ToStart", "|<"), WidgetAnimTips::ToStartTooltip(),
						FOnClicked::CreateLambda([this]() { ScrubToSeconds(0.f); return FReply::Handled(); }), TransportIconSize)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Backward_Step", "Sequencer.Transport.StepBackwards.Normal" }, LOCTEXT("PrevKey", "<"), WidgetAnimTips::PrevKeyTooltip(),
						FOnClicked::CreateLambda([this]() { StepToMarker(-1); return FReply::Handled(); }), TransportIconSize)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(FMargin(4.f, 0.f))
				[
					SNew(SWidgetAnimAnchoredTip)
					.Text_Lambda([this]()
					{
						return IsPlaying()
							? WidgetAnimTips::PauseTooltip()
							: WidgetAnimTips::PlayTooltip();
					})
					[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(6.f, 3.f))
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Center)
					.OnClicked_Lambda([this]() { TogglePlay(); return FReply::Handled(); })
					[
						MakeFixedIcon(
							TAttribute<const FSlateBrush*>::CreateLambda([this]()
							{
								return IsPlaying()
									? PickBrush({ "Icons.Pause", "PlayWorld.PausePlaySession", "GenericPause" })
									: PickBrush({ "Icons.Play", "PlayWorld.ResumePlaySession", "GenericPlay" });
							}),
							TAttribute<FSlateColor>::CreateLambda([this]()
							{
								return FSlateColor(IsPlaying() ? PauseTint : PlayTint);
							}),
							TransportIconSize + 4.f)
					]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Forward_Step", "Sequencer.Transport.StepForward.Normal" }, LOCTEXT("NextKey", ">"), WidgetAnimTips::NextKeyTooltip(),
						FOnClicked::CreateLambda([this]() { StepToMarker(1); return FReply::Handled(); }), TransportIconSize)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Forward_End", "Sequencer.Transport.GoToEnd.Normal" }, LOCTEXT("ToEnd", ">|"), WidgetAnimTips::ToEndTooltip(),
						FOnClicked::CreateLambda([this]() { ScrubToSeconds(GetPassSeconds()); return FReply::Handled(); }), TransportIconSize)
				]
			]

			// Right third: how tracks behave when dragged, then the time in a recessed readout like the native
			// editor's current-time box.
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
				[
					SNew(STextBlock)
					.Visibility(bCompact ? EVisibility::Collapsed : EVisibility::Visible)
					.Text(LOCTEXT("TrackControls", "Track Controls"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 12.f, 0.f))
				[
					SNew(SBorder)
					.BorderImage(ControlsPillBrush())
					.Padding(FMargin(3.f, 2.f))
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							MakePillToggle(LOCTEXT("TrackSnapping", "Snapping"),
								WidgetAnimTips::TrackSnappingTooltip(),
								[]() { return UWidgetAnimEditorSettings::Get().bTrackSnapping; },
								[](bool bValue) { UWidgetAnimEditorSettings::GetMutable().SetTrackSnapping(bValue); },
								[]() { return true; },
								FindControlIcon(TEXT("WidgetAnim_Snapping"), { "Sequencer.ToggleIsSnapEnabled", "EditorViewport.LocationGridSnap", "GenericCurveEditor.ToggleSnapping", "Icons.Snap" }))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(3.f, 0.f))
						[
							MakeDivider()
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							MakePillToggle(LOCTEXT("LinkedEdges", "Linked Edges"),
								WidgetAnimTips::LinkedEdgesTooltip(),
								[]() { return UWidgetAnimEditorSettings::Get().bLinkedEdges; },
								[](bool bValue) { UWidgetAnimEditorSettings::GetMutable().SetLinkedEdges(bValue); },
								[]() { return true; },
								FindControlIcon(TEXT("WidgetAnim_LinkedEdges"), { "Icons.Link", "Icons.Chain" }))
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(FMargin(10.f, 3.f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.2f"), GetPlayheadSeconds())); })
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT(" / %.2f s"), GetPassSeconds())); })
					]
				]
				]
			]
		];
}

// -----------------------------------------------------------------------------------------------------
// Reading the animation
// -----------------------------------------------------------------------------------------------------

const TArray<FWidgetAnimLayerGroup>* SWidgetAnimTimeline::GetGroups() const
{
	return Source.IsValid() ? Source->GetGroups() : nullptr;
}

const TArray<FWidgetAnimLayer>* SWidgetAnimTimeline::GetLayers() const
{
	return Source.IsValid() ? Source->GetLayers() : nullptr;
}

int32 SWidgetAnimTimeline::GetLayerGroup(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	return Layers && Groups && Layers->IsValidIndex(LayerIndex)
		? UWidgetAnimLibrary::GetLayerGroupIndex((*Layers)[LayerIndex], Groups->Num())
		: INDEX_NONE;
}

bool SWidgetAnimTimeline::HasAnimation() const
{
	return GetGroups() && GetLayers();
}

bool SWidgetAnimTimeline::CanEdit() const
{
	return Source.IsValid() && Source->CanEdit() && !Source->IsReadOnly();
}

bool SWidgetAnimTimeline::IsReadOnly() const
{
	return Source.IsValid() && Source->IsReadOnly();
}

void SWidgetAnimTimeline::SetReadOnly(bool bReadOnly)
{
	if (!Source.IsValid() || Source->IsReadOnly() == bReadOnly)
	{
		return;
	}

	// A drag in progress ends where it is, rather than carrying on into an animation that can't be edited.
	if (IsDraggingKey())
	{
		EndKeyDrag();
	}
	if (IsDraggingLayers())
	{
		EndLayerDrag();
	}

	Source->SetReadOnly(bReadOnly);

	// The rows' grips, delete buttons and drag-to-reorder are built for the state they are in; the layout
	// signature carries the lock, so the other panel of this editor catches up by itself next frame.
	RebuildRows();
	RefreshDetails();
}

bool SWidgetAnimTimeline::IsLayerLocked(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].bLockedInEditor;
}

bool SWidgetAnimTimeline::IsLayerHidden(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].bHiddenInEditor;
}

float SWidgetAnimTimeline::GetTimeScale() const
{
	return Source.IsValid() ? 1.f / Source->GetAnimationSpeedMultiplier() : 1.f;
}

bool SWidgetAnimTimeline::CanEditLayer(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return CanEdit() && Layers && Layers->IsValidIndex(LayerIndex) && !(*Layers)[LayerIndex].bLockedInEditor;
}

bool SWidgetAnimTimeline::CanEditKeys(int32 LayerIndex) const
{
	if (!CanEditLayer(LayerIndex))
	{
		return false;
	}

	// A Curve asset's keys belong to the asset, and every animation sharing it; they're edited there.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		return Layer.Transform.Curve.ExternalCurve == nullptr;
	}

	return WidgetAnimTypeSupportsValueKeys(Layer.AnimType);
}

bool SWidgetAnimTimeline::CanMoveKey(int32 LayerIndex, int32 KeyIndex) const
{
	if (!CanEditKeys(LayerIndex))
	{
		return false;
	}

	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (!KeyTimes.IsValidIndex(KeyIndex))
	{
		return false;
	}

	// A keyed track's first and last keys are its start and its end: where the track is, not points along it.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	return Layer.AnimType == EWidgetAnimType::Transform || (KeyIndex > 0 && KeyIndex < KeyTimes.Num() - 1);
}

bool SWidgetAnimTimeline::CanDeleteKey(int32 LayerIndex, int32 KeyIndex) const
{
	if (!CanEditKeys(LayerIndex))
	{
		return false;
	}

	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (!KeyTimes.IsValidIndex(KeyIndex))
	{
		return false;
	}

	// Two keys is the least a Transform track can move between; any other track always keeps its start and end.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	return Layer.AnimType == EWidgetAnimType::Transform
		? KeyTimes.Num() > 2
		: (KeyIndex > 0 && KeyIndex < KeyTimes.Num() - 1);
}

bool SWidgetAnimTimeline::CanResizeLayer(int32 LayerIndex, bool bStartEdge) const
{
	if (!CanEditLayer(LayerIndex))
	{
		return false;
	}

	// Stretching a Transform layer stretches its keys, and a shared Curve asset's keys aren't this animation's.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	if (Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.Curve.ExternalCurve != nullptr)
	{
		return false;
	}

	// A Spring track is as long as its spring takes to settle - its spring decides that, not its edges. Not a
	// Transform's: its spring is fitted to its keys, which stretch with it as a Tween's do.
	if (Layer.IsSpringTimed() && Layer.AnimType != EWidgetAnimType::Transform)
	{
		return false;
	}

	// Nor one whose length comes from its widget - a text typed at its Typing Speed - but its start can still move.
	if (HasDerivedLength(Layer) && !bStartEdge)
	{
		return false;
	}

	// Either edge - a track that starts with the animation included: its start can be pulled in, just never
	// back past 0. UpdateLayerDrag holds it there.
	return true;
}

void SWidgetAnimTimeline::GetLayerSpans(TArray<FVector2f>& OutSpans) const
{
	OutSpans.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	// Placed exactly as playback places them - one sequence across every group, Starts and Delay, a staggered layer
	// reaching on to its last item as it does once it fans out on the preview - so the bars are where the layers
	// really run. In played seconds: the animation's Speed Multiplier applied. A span is its layer's first item; the
	// rest of a stagger is its fan - see GetLayerFanSeconds.
	TArray<float> Extras;
	GetPlacementExtras(Extras);
	TArray<float> Starts;
	UWidgetAnimLibrary::PlaceLayerStarts(*Layers, Extras, Starts);
	const float TimeScale = GetTimeScale();

	OutSpans.Reserve(Starts.Num());
	for (int32 Index = 0; Index < Starts.Num(); ++Index)
	{
		OutSpans.Add(FVector2f(Starts[Index], GetShownRunLength(Index)) * TimeScale);
	}
}

namespace WidgetAnimStaggerFans
{
	/**
	 * Seconds past its first item a layer's stagger reaches, authored, for NumItems items - the fan the preview plays.
	 * With the count unknown, a Spread Over reaches its spread, and Seconds Apart can only be guessed as nothing.
	 */
	float GetExtra(const FWidgetAnimLayer& Layer, int32 NumItems)
	{
		const FWidgetAnimExpander* Expander = Layer.Expander.GetPtr<FWidgetAnimExpander>();
		if (!Expander || Layer.IsValueDriven() || (Expander->GathersParts() && !UWidgetAnimLibrary::TypeMovesParts(Layer.AnimType)))
		{
			return 0.f;
		}

		if (NumItems == INDEX_NONE)
		{
			return Expander->Spacing == EWidgetAnimStaggerSpacing::Across ? FMath::Max(Expander->Spread, 0.f) : 0.f;
		}
		return Expander->GetFanSeconds(NumItems);
	}
}

void SWidgetAnimTimeline::UpdateStaggerFans()
{
	TrackFans.Reset();
	TrackFanCounts.Reset();
	TrackRuns.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const int32 Count = Layer.Expander.IsValid() && Source.IsValid() ? Source->GetExpandedCount(LayerIndex) : INDEX_NONE;
		TrackFanCounts.Add(Count);
		TrackFans.Add(WidgetAnimStaggerFans::GetExtra(Layer, Count));

		// A length worked out on the preview - a text typed at its Typing Speed - in place of its Duration.
		TrackRuns.Add(HasDerivedLength(Layer) && Source.IsValid() ? Source->GetPreparedRun(LayerIndex) : -1.f);
	}
}

bool SWidgetAnimTimeline::HasDerivedLength(const FWidgetAnimLayer& Layer)
{
	if (Layer.AnimType != EWidgetAnimType::Custom || Layer.IsValueDriven())
	{
		return false;
	}
	const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
	return Evaluator && Evaluator->HasDerivedLength(Layer.TypeParams);
}

float SWidgetAnimTimeline::GetShownRunLength(int32 LayerIndex) const
{
	if (TrackRuns.IsValidIndex(LayerIndex) && TrackRuns[LayerIndex] >= 0.f)
	{
		return TrackRuns[LayerIndex];
	}
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].GetRunLength() : 0.f;
}

void SWidgetAnimTimeline::GetPlacementExtras(TArray<float>& OutExtras) const
{
	// Each layer's stagger fan, plus however much longer - or shorter - the preview runs it than its Duration.
	OutExtras = TrackFans;
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}
	OutExtras.SetNumZeroed(Layers->Num());
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		if (TrackRuns.IsValidIndex(LayerIndex) && TrackRuns[LayerIndex] >= 0.f)
		{
			OutExtras[LayerIndex] += TrackRuns[LayerIndex] - (*Layers)[LayerIndex].GetRunLength();
		}
	}
}

float SWidgetAnimTimeline::GetLayerFanSeconds(int32 LayerIndex) const
{
	return TrackFans.IsValidIndex(LayerIndex) ? TrackFans[LayerIndex] * GetTimeScale() : 0.f;
}

void SWidgetAnimTimeline::FocusPreviewOnKey(int32 LayerIndex, int32 KeyIndex)
{
	if (!Source.IsValid())
	{
		return;
	}

	// Without staggers the source knows best - it mirrors a key for Invert, too. With one, only the panel knows how far
	// it reaches, so the key is found on the panel's own timeline: the layer's start plus the key's time into it.
	const bool bAnyFan = TrackFans.ContainsByPredicate([](float Fan) { return Fan > 0.f; });
	if (!bAnyFan)
	{
		Source->FocusKey(LayerIndex, KeyIndex);
		return;
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (Spans.IsValidIndex(LayerIndex) && KeyTimes.IsValidIndex(KeyIndex))
	{
		ScrubToSeconds(Spans[LayerIndex].X + KeyTimes[KeyIndex]);
	}
}

FVector2f SWidgetAnimTimeline::GetGroupSpan(int32 GroupIndex) const
{
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);

	FVector2f Range(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (int32 LayerIndex = 0; LayerIndex < Spans.Num(); ++LayerIndex)
	{
		if (GetLayerGroup(LayerIndex) == GroupIndex)
		{
			Range.X = FMath::Min(Range.X, Spans[LayerIndex].X);
			Range.Y = FMath::Max(Range.Y, Spans[LayerIndex].X + Spans[LayerIndex].Y);
		}
	}
	return Range;
}

float SWidgetAnimTimeline::GetPassSeconds() const
{
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);

	// To where the last item of a stagger ends, too.
	float End = 0.f;
	for (int32 LayerIndex = 0; LayerIndex < Spans.Num(); ++LayerIndex)
	{
		End = FMath::Max(End, Spans[LayerIndex].X + Spans[LayerIndex].Y + GetLayerFanSeconds(LayerIndex));
	}

	// Played within another animation, one pass runs until both have ended - the preview's pass is the same.
	for (const FVector2f& Span : ContextSpans)
	{
		End = FMath::Max(End, Span.X + Span.Y);
	}
	return End;
}

const TArray<FWidgetAnimLayer>* SWidgetAnimTimeline::GetContextLayers() const
{
	return Source.IsValid() ? Source->GetContextLayers() : nullptr;
}

void SWidgetAnimTimeline::GetKeyTimes(int32 LayerIndex, TArray<float>& OutTimes) const
{
	OutTimes.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return;
	}

	// In played seconds, as the spans are.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	const float TimeScale = GetTimeScale();

	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		if (const FRichCurve* Curve = Layer.Transform.GetActiveCurve())
		{
			for (const FRichCurveKey& Key : Curve->GetConstRefOfKeys())
			{
				OutTimes.Add(Key.Time * TimeScale);
			}
		}
		return;
	}

	// Its start, every key at its place along the run, and its end - the start and end values being keys too. Only
	// once it has a key between them: a track that simply goes from its start to its end has nothing to show as keys
	// but its own edges, and diamonds sitting on them would only get in the way of resizing it. Double-clicking it
	// adds its first key.
	if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.Keys.Num() > 0)
	{
		const float RunSeconds = GetShownRunLength(LayerIndex) * TimeScale;
		for (int32 Point = 0; Point < Layer.ValueKeys.NumPoints(); ++Point)
		{
			OutTimes.Add(Layer.ValueKeys.GetPointTime(Point) * RunSeconds);
		}
	}
}

const FWidgetAnimSegmentCurve* SWidgetAnimTimeline::GetSegmentCurve(int32 LayerIndex, int32 Segment) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex) || Segment < 0)
	{
		return nullptr;
	}

	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		// The last key has no next, so no segment of its own.
		return Segment + 1 < Layer.Transform.Keys.Num() ? &Layer.Transform.Keys[Segment].CurveToNext : nullptr;
	}

	if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Segment <= Layer.ValueKeys.Keys.Num())
	{
		return &Layer.ValueKeys.GetSegmentCurve(Segment);
	}
	return nullptr;
}

bool SWidgetAnimTimeline::HasSegmentCurves(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}

	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	return Layer.AnimType == EWidgetAnimType::Transform
		? Layer.Transform.HasSegmentCurves()
		: WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.HasSegmentCurves();
}

bool SWidgetAnimTimeline::UsesSegmentCurves(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}

	// A spring springs across the whole run, keys and all - a Transform's too, fitted to its keys - so the curves
	// between them wait for it to be a Tween again.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	return (Layer.AnimType == EWidgetAnimType::Transform || WidgetAnimTypeSupportsValueKeys(Layer.AnimType)) && !Layer.IsSpringTimed();
}

float SWidgetAnimTimeline::GetPlayheadSeconds() const
{
	// The preview reports a fraction of one pass, which is the same fraction at any playback speed.
	return Source.IsValid() ? Source->GetPlayheadAlpha() * GetPassSeconds() : 0.f;
}

FText SWidgetAnimTimeline::GetGroupName(int32 GroupIndex) const
{
	// A plain list's one group is simply the widget the list belongs to.
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (Groups && Groups->IsValidIndex(GroupIndex) && Source.IsValid() && !Source->SupportsLayerGroups())
	{
		return LOCTEXT("ThisWidgetGroup", "This Widget");
	}

	return Groups && Groups->IsValidIndex(GroupIndex)
		? FText::FromString((*Groups)[GroupIndex].GetDisplayName(GroupIndex))
		: FText::GetEmpty();
}

FText SWidgetAnimTimeline::GetGroupTargetLabel(int32 GroupIndex) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups || !Groups->IsValidIndex(GroupIndex))
	{
		return FText::GetEmpty();
	}

	const FName Target = (*Groups)[GroupIndex].PreviewTarget;
	if (Target.IsNone())
	{
		return LOCTEXT("TargetSelf", "(this widget)");
	}

	// A path this Widget Blueprint doesn't have plays on the widget itself - said so, rather than left to puzzle over.
	const bool bFound = Source.IsValid() && (!Source->GetPreviewRoot() || Source->ResolvePreviewTarget(Target));
	return bFound
		? FText::FromName(Target)
		: FText::Format(LOCTEXT("TargetMissing", "{0} (not in this widget)"), FText::FromName(Target));
}

void SWidgetAnimTimeline::SetRowHeight(float NewHeight)
{
	NewHeight = FMath::Clamp(FMath::RoundToFloat(NewHeight), MinRowHeight, MaxRowHeight);
	if (FMath::IsNearlyEqual(NewHeight, RowHeight))
	{
		return;
	}

	RowHeight = NewHeight;

	// Remembered for next time, per user and project, like any other editor layout preference.
	GConfig->SetFloat(RowHeightConfigSection, RowHeightConfigKey, RowHeight, GEditorPerProjectIni);

	// The Track Editor reads it live; it just needs laying out again.
	if (Tracks.IsValid())
	{
		Tracks->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWidgetAnimTimeline::ToggleTrackGroupCollapsed(int32 GroupIndex)
{
	if (TrackCollapsedGroups.Contains(GroupIndex))
	{
		TrackCollapsedGroups.Remove(GroupIndex);
	}
	else
	{
		TrackCollapsedGroups.Add(GroupIndex);
	}

	UpdateTrackLayout();
}

void SWidgetAnimTimeline::ToggleListGroupCollapsed(int32 GroupIndex)
{
	if (ListCollapsedGroups.Contains(GroupIndex))
	{
		ListCollapsedGroups.Remove(GroupIndex);
	}
	else
	{
		ListCollapsedGroups.Add(GroupIndex);
	}

	RebuildLayerList();
}

// -----------------------------------------------------------------------------------------------------
// Driving the preview
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::ScrubToSeconds(float Seconds)
{
	const float PassSeconds = GetPassSeconds();
	if (Source.IsValid())
	{
		Source->ScrubToAlpha(PassSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(Seconds / PassSeconds, 0.f, 1.f) : 0.f);
	}
}

void SWidgetAnimTimeline::TogglePlay()
{
	if (Source.IsValid())
	{
		Source->TogglePlay();
	}
}

bool SWidgetAnimTimeline::IsPlaying() const
{
	return Source.IsValid() && Source->IsPlaying();
}

void SWidgetAnimTimeline::StepToMarker(int32 Direction)
{
	struct FMarker
	{
		float Seconds;
		int32 Layer;
		int32 Key;
	};

	TArray<FMarker> Markers;
	Markers.Add({ 0.f, INDEX_NONE, INDEX_NONE });
	Markers.Add({ GetPassSeconds(), INDEX_NONE, INDEX_NONE });

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	for (int32 LayerIndex = 0; LayerIndex < Spans.Num(); ++LayerIndex)
	{
		Markers.Add({ Spans[LayerIndex].X, INDEX_NONE, INDEX_NONE });
		Markers.Add({ Spans[LayerIndex].X + Spans[LayerIndex].Y, INDEX_NONE, INDEX_NONE });

		TArray<float> KeyTimes;
		GetKeyTimes(LayerIndex, KeyTimes);
		for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
		{
			Markers.Add({ Spans[LayerIndex].X + KeyTimes[Key], LayerIndex, Key });
		}
	}

	// Keys win over a bare layer edge at the same moment, so stepping lands on - and selects - the key.
	Markers.Sort([](const FMarker& A, const FMarker& B)
	{
		return A.Seconds != B.Seconds ? A.Seconds < B.Seconds : A.Key > B.Key;
	});

	const float Now = GetPlayheadSeconds();
	constexpr float Epsilon = 0.0005f;
	const FMarker* Target = nullptr;

	if (Direction > 0)
	{
		for (const FMarker& Marker : Markers)
		{
			if (Marker.Seconds > Now + Epsilon)
			{
				Target = &Marker;
				break;
			}
		}
	}
	else
	{
		for (int32 Index = Markers.Num() - 1; Index >= 0; --Index)
		{
			if (Markers[Index].Seconds < Now - Epsilon)
			{
				int32 First = Index;
				while (First > 0 && FMath::IsNearlyEqual(Markers[First - 1].Seconds, Markers[Index].Seconds, Epsilon))
				{
					--First;
				}
				Target = &Markers[First];
				break;
			}
		}
	}

	if (!Target)
	{
		return;
	}

	if (Target->Key != INDEX_NONE)
	{
		SelectKey(Target->Layer, Target->Key);
	}
	else
	{
		ScrubToSeconds(Target->Seconds);
	}
}

// -----------------------------------------------------------------------------------------------------
// Selection
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::SelectGroup(int32 GroupIndex)
{
	FSelection NewSelection;
	NewSelection.Group = GroupIndex;
	Selection = Validate(NewSelection);
	SelectedKeys.Reset();
	SelectedLayers.Reset();
	SelectionAnchor = INDEX_NONE;
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectLayer(int32 LayerIndex)
{
	FSelection NewSelection;
	NewSelection.Layer = LayerIndex;
	Selection = Validate(NewSelection);

	SelectedKeys.Reset();
	SelectedLayers.Reset();
	if (Selection.Layer != INDEX_NONE)
	{
		SelectedLayers.Add(Selection.Layer);
	}
	SelectionAnchor = Selection.Layer;
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectLayerWithModifiers(int32 LayerIndex, bool bRange, bool bToggle)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return;
	}

	if (!bRange && !bToggle)
	{
		SelectLayer(LayerIndex);
		return;
	}

	// Ctrl adds to what is selected; Shift alone replaces it with the range, and Ctrl + Shift adds the range.
	// Picking layers drops any keys that were picked: keys and layers are never selected together.
	TArray<int32> NewLayers = (bToggle && SelectedKeys.Num() == 0) ? SelectedLayers : TArray<int32>();
	SelectedKeys.Reset();

	if (bRange)
	{
		// The layers between the last one clicked and this one, in the Animation Layers list's order - group by
		// group, each in the order it plays - wherever the click was.
		TArray<int32> Shown;
		GetListOrder(Shown);

		const int32 To = Shown.IndexOfByKey(LayerIndex);
		int32 From = Shown.IndexOfByKey(SelectionAnchor);
		if (From == INDEX_NONE)
		{
			From = To;
		}

		if (To == INDEX_NONE)
		{
			NewLayers.AddUnique(LayerIndex);
		}
		else
		{
			for (int32 Index = FMath::Min(From, To); Index <= FMath::Max(From, To); ++Index)
			{
				NewLayers.AddUnique(Shown[Index]);
			}
		}

		// The anchor stays put, so the next Shift-click ranges from the same place, as in a file browser.
		if (!Layers->IsValidIndex(SelectionAnchor))
		{
			SelectionAnchor = LayerIndex;
		}
	}
	else
	{
		if (NewLayers.Contains(LayerIndex))
		{
			NewLayers.Remove(LayerIndex);
		}
		else
		{
			NewLayers.Add(LayerIndex);
		}
		SelectionAnchor = LayerIndex;
	}

	NewLayers.Sort();
	SelectedLayers = MoveTemp(NewLayers);

	// The primary selection - what a single-layer action or the title names - is the one just clicked, or,
	// if that was just taken out, the last still selected.
	FSelection NewSelection;
	NewSelection.Layer = SelectedLayers.Contains(LayerIndex) ? LayerIndex : (SelectedLayers.Num() > 0 ? SelectedLayers.Last() : INDEX_NONE);
	Selection = Validate(NewSelection);
	ValidateSelectedLayers();
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectAllLayers()
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || Layers->Num() == 0)
	{
		return;
	}

	SelectedKeys.Reset();
	SelectedLayers.Reset();
	for (int32 Index = 0; Index < Layers->Num(); ++Index)
	{
		SelectedLayers.Add(Index);
	}

	FSelection NewSelection;
	NewSelection.Layer = Layers->IsValidIndex(Selection.Layer) ? Selection.Layer : 0;
	Selection = Validate(NewSelection);
	SelectionAnchor = Selection.Layer;
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectKey(int32 LayerIndex, int32 KeyIndex)
{
	FSelection NewSelection;
	NewSelection.Layer = LayerIndex;
	NewSelection.Key = KeyIndex;
	Selection = Validate(NewSelection);

	SelectedKeys.Reset();
	if (Selection.Key != INDEX_NONE)
	{
		SelectedKeys.Add(FKeyRef{ Selection.Layer, Selection.Key });
	}

	SelectedLayers.Reset();
	if (Selection.Layer != INDEX_NONE)
	{
		SelectedLayers.Add(Selection.Layer);
	}
	SelectionAnchor = Selection.Layer;
	RefreshDetails();

	// The preview plays the layers as the one list they are, so the layer's index is the preview's.
	if (Selection.Key != INDEX_NONE && Source.IsValid())
	{
		FocusPreviewOnKey(LayerIndex, KeyIndex);
	}
}

void SWidgetAnimTimeline::SelectKeyWithModifiers(int32 LayerIndex, int32 KeyIndex, bool bToggle)
{
	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (!KeyTimes.IsValidIndex(KeyIndex))
	{
		return;
	}

	const FKeyRef Clicked{ LayerIndex, KeyIndex };
	const bool bWasSelected = SelectedKeys.Contains(Clicked);

	if (bToggle && bWasSelected)
	{
		SelectedKeys.Remove(Clicked);
	}
	else
	{
		// To the end, whether new or already in: the key clicked last is the one the title and the preview follow.
		SelectedKeys.Remove(Clicked);
		SelectedKeys.Add(Clicked);
	}

	if (SelectedKeys.Num() == 0)
	{
		ClearSelection();
		return;
	}

	ValidateSelectedKeys();
	RefreshDetails();

	if (Source.IsValid() && SelectedKeys.Contains(Clicked))
	{
		FocusPreviewOnKey(LayerIndex, KeyIndex);
	}
}

void SWidgetAnimTimeline::SelectInBox(const TArray<FKeyRef>& Keys, const TArray<int32>& Layers, bool bAdd, bool bToggle)
{
	const bool bKeep = bAdd || bToggle;

	if (Keys.Num() > 0)
	{
		// Keys: added to the keys already picked, or flipped in and out of them - never mixed with picked layers.
		TArray<FKeyRef> NewKeys = bKeep ? SelectedKeys : TArray<FKeyRef>();
		for (const FKeyRef& Key : Keys)
		{
			if (bToggle && NewKeys.Contains(Key))
			{
				NewKeys.Remove(Key);
			}
			else
			{
				NewKeys.AddUnique(Key);
			}
		}

		if (NewKeys.Num() == 0)
		{
			ClearSelection();
			return;
		}

		SelectedKeys = MoveTemp(NewKeys);
		ValidateSelectedKeys();
		SelectionAnchor = Selection.Layer;
		RefreshDetails();
		return;
	}

	if (Layers.Num() > 0)
	{
		TArray<int32> NewLayers = (bKeep && SelectedKeys.Num() == 0) ? SelectedLayers : TArray<int32>();
		for (const int32 LayerIndex : Layers)
		{
			if (bToggle && NewLayers.Contains(LayerIndex))
			{
				NewLayers.Remove(LayerIndex);
			}
			else
			{
				NewLayers.AddUnique(LayerIndex);
			}
		}

		if (NewLayers.Num() == 0)
		{
			ClearSelection();
			return;
		}

		NewLayers.Sort();
		SelectedKeys.Reset();
		SelectedLayers = MoveTemp(NewLayers);

		FSelection NewSelection;
		NewSelection.Layer = SelectedLayers.Contains(Selection.Layer) ? Selection.Layer : SelectedLayers.Last();
		Selection = Validate(NewSelection);
		SelectionAnchor = Selection.Layer;
		ValidateSelectedLayers();
		RefreshDetails();
		return;
	}

	// An empty box picks nothing - the animation itself - unless it was only adding to what was picked.
	if (!bKeep)
	{
		ClearSelection();
	}
}

void SWidgetAnimTimeline::ValidateSelectedKeys()
{
	SelectedKeys.RemoveAll([this](const FKeyRef& Ref)
	{
		TArray<float> KeyTimes;
		GetKeyTimes(Ref.Layer, KeyTimes);
		return !KeyTimes.IsValidIndex(Ref.Key);
	});

	if (SelectedKeys.Num() == 0)
	{
		return;
	}

	// The key picked last is the one the title names and the preview follows. Its layers are selected alongside,
	// only so their tracks are lit.
	const FKeyRef& Primary = SelectedKeys.Last();
	Selection = FSelection();
	Selection.Layer = Primary.Layer;
	Selection.Key = Primary.Key;
	Selection.Group = GetLayerGroup(Primary.Layer);

	SelectedLayers.Reset();
	for (const FKeyRef& Ref : SelectedKeys)
	{
		SelectedLayers.AddUnique(Ref.Layer);
	}
	SelectedLayers.Sort();
}

void SWidgetAnimTimeline::GetSelectedSegments(TArray<FKeyRef>& OutSegments) const
{
	OutSegments.Reset();

	for (const FKeyRef& Ref : SelectedKeys)
	{
		if (CanEditKeys(Ref.Layer) && UsesSegmentCurves(Ref.Layer) && IsKeySelected(Ref.Layer, Ref.Key + 1) && GetSegmentCurve(Ref.Layer, Ref.Key))
		{
			OutSegments.AddUnique(Ref);
		}
	}

	OutSegments.Sort([](const FKeyRef& A, const FKeyRef& B)
	{
		return A.Layer != B.Layer ? A.Layer < B.Layer : A.Key < B.Key;
	});
}

namespace
{
	/** The curve of one segment of a layer, to write. Null when there is no such segment. */
	FWidgetAnimSegmentCurve* FindMutableSegmentCurve(FWidgetAnimLayer& Layer, int32 Segment)
	{
		if (Segment < 0)
		{
			return nullptr;
		}

		if (Layer.AnimType == EWidgetAnimType::Transform)
		{
			return Segment + 1 < Layer.Transform.Keys.Num() ? &Layer.Transform.Keys[Segment].CurveToNext : nullptr;
		}

		if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Segment <= Layer.ValueKeys.Keys.Num())
		{
			return &Layer.ValueKeys.GetSegmentCurve(Segment);
		}
		return nullptr;
	}
}

void SWidgetAnimTimeline::SetSegmentCurves(const TArray<FKeyRef>& Segments, const FWidgetAnimSegmentCurve& Curve)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers || Segments.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(Segments.Num() > 1
		? LOCTEXT("SetSegmentCurvesTransaction", "Set Curve Between Keys")
		: LOCTEXT("SetSegmentCurveTransaction", "Set Curve To Next Key"));
	if (!BeginEdit())
	{
		return;
	}

	bool bChanged = false;
	for (const FKeyRef& Segment : Segments)
	{
		if (!Layers->IsValidIndex(Segment.Layer) || !CanEditKeys(Segment.Layer))
		{
			continue;
		}

		FWidgetAnimSegmentCurve* Target = FindMutableSegmentCurve((*Layers)[Segment.Layer], Segment.Key);
		if (Target && *Target != Curve)
		{
			*Target = Curve;
			bChanged = true;
		}
	}

	if (bChanged)
	{
		NotifyGroupsChanged(EPropertyChangeType::ValueSet);

		// A single key's curve row, if that is what the Selection panel shows.
		if (!IsKeyMultiSelection())
		{
			RefreshDetails();
		}
	}
}

FText SWidgetAnimTimeline::GetSegmentCurveName(const FWidgetAnimSegmentCurve& Curve)
{
	// A Custom curve is a Curve asset, so it is shown by that asset's own name.
	if (Curve.Easing == EWidgetAnimSegmentEasing::Custom && Curve.CustomCurve)
	{
		return FText::FromString(Curve.CustomCurve->GetName());
	}

	const UEnum* EasingEnum = StaticEnum<EWidgetAnimSegmentEasing>();
	return EasingEnum ? EasingEnum->GetDisplayNameTextByValue(static_cast<int64>(Curve.Easing)) : FText::GetEmpty();
}

void SWidgetAnimTimeline::ClearSelection()
{
	Selection = FSelection();
	SelectedKeys.Reset();
	SelectedLayers.Reset();
	SelectionAnchor = INDEX_NONE;
	RefreshDetails();
}

SWidgetAnimTimeline::FSelection SWidgetAnimTimeline::Validate(const FSelection& InSelection) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		return FSelection();
	}

	FSelection Out = InSelection;

	// A layer carries its group, so the rows and the title can light and name it.
	if (Out.Layer != INDEX_NONE)
	{
		if (!Layers->IsValidIndex(Out.Layer))
		{
			return FSelection();
		}

		Out.Group = GetLayerGroup(Out.Layer);

		if (Out.Key != INDEX_NONE)
		{
			TArray<float> KeyTimes;
			GetKeyTimes(Out.Layer, KeyTimes);
			if (!KeyTimes.IsValidIndex(Out.Key))
			{
				Out.Key = INDEX_NONE;
			}
		}
		return Out;
	}

	Out.Key = INDEX_NONE;
	if (!Groups->IsValidIndex(Out.Group))
	{
		Out.Group = INDEX_NONE;
	}
	return Out;
}

void SWidgetAnimTimeline::ValidateSelectedLayers()
{
	// Keys picked: their layers are whatever layers they are on.
	if (SelectedKeys.Num() > 0)
	{
		ValidateSelectedKeys();
		if (SelectedKeys.Num() > 0)
		{
			return;
		}
	}

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || Selection.Layer == INDEX_NONE)
	{
		SelectedLayers.Reset();
		return;
	}

	SelectedLayers.RemoveAll([Layers](int32 Index) { return !Layers->IsValidIndex(Index); });
	SelectedLayers.AddUnique(Selection.Layer);
	SelectedLayers.Sort();

	// A key belongs to one layer; several layers selected have no key.
	if (SelectedLayers.Num() > 1)
	{
		Selection.Key = INDEX_NONE;
	}
}

bool SWidgetAnimTimeline::IsSelectionEditable() const
{
	if (!CanEdit())
	{
		return false;
	}

	for (const int32 LayerIndex : SelectedLayers)
	{
		if (IsLayerLocked(LayerIndex))
		{
			return false;
		}
	}
	return true;
}

void SWidgetAnimTimeline::RefreshDetails()
{
	if (!DetailsView.IsValid() || !MultiDetailsView.IsValid())
	{
		return;
	}

	Selection = Validate(Selection);
	ValidateSelectedLayers();

	// Several keys: the curve between them, in a panel of its own that reads the keys live - nothing to bind.
	if (IsKeyMultiSelection())
	{
		if (LayerProxies.Num() > 0)
		{
			LayerProxies.Reset();
			MultiDetailsView->SetObjects(TArray<UObject*>(), /*bForceRefresh=*/true);
		}
		return;
	}

	// Several layers: their proxies, merged. The single view is left bound where it is, hidden, so going back to
	// one layer only has to refresh it.
	if (IsMultiSelection())
	{
		RebuildLayerProxies();

		TArray<UObject*> Objects;
		for (const TStrongObjectPtr<UWidgetAnimLayerEditProxy>& Proxy : LayerProxies)
		{
			Objects.Add(Proxy.Get());
		}
		MultiDetailsView->SetObjects(Objects, /*bForceRefresh=*/true);
		return;
	}

	if (LayerProxies.Num() > 0)
	{
		LayerProxies.Reset();
		MultiDetailsView->SetObjects(TArray<UObject*>(), /*bForceRefresh=*/true);
	}

	UObject* Edited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
	if (DetailsObject.Get() != Edited)
	{
		// Any other object - one holding a plain list of tracks - is laid out by the same customization. Registered
		// for its class before it is bound; a builder and an asset have theirs from the start.
		if (Edited && !Edited->IsA<UWidgetAnimBuilder>() && !Edited->IsA<UWidgetAnimPresetAsset>())
		{
			DetailsView->RegisterInstancedCustomPropertyLayout(Edited->GetClass(), SelectionLayout);
		}

		// Binding a new object lays the view out from scratch anyway.
		DetailsObject = Edited;
		DetailsView->SetObject(Edited, /*bForceRefresh=*/true);
		return;
	}

	DetailsView->ForceRefresh();
}

void SWidgetAnimTimeline::RebuildLayerProxies()
{
	LayerProxies.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);
	for (const int32 LayerIndex : SelectedLayers)
	{
		if (!Layers->IsValidIndex(LayerIndex))
		{
			continue;
		}

		UWidgetAnimLayerEditProxy* Proxy = NewObject<UWidgetAnimLayerEditProxy>(GetTransientPackage(), NAME_None, RF_Transient);
		Proxy->Layer = (*Layers)[LayerIndex];
		Proxy->LayerIndex = LayerIndex;
		Proxy->Panel = WeakThisPtr;
		LayerProxies.Emplace(Proxy);
	}
}

void SWidgetAnimTimeline::SyncLayerProxies()
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	const UScriptStruct* LayerStruct = FWidgetAnimLayer::StaticStruct();
	for (const TStrongObjectPtr<UWidgetAnimLayerEditProxy>& Proxy : LayerProxies)
	{
		if (Proxy.IsValid() && Layers->IsValidIndex(Proxy->LayerIndex)
			&& !LayerStruct->CompareScriptStruct(&Proxy->Layer, &(*Layers)[Proxy->LayerIndex], PPF_None))
		{
			Proxy->Layer = (*Layers)[Proxy->LayerIndex];
		}
	}
}

namespace
{
	/**
	 * Copies the one property a details view edited from one layer to another. The chain runs from the proxy's
	 * Layer down to the property edited; it is walked through the layer's nested structs to that property, so
	 * nothing else of either layer is touched. False if the chain leads nowhere it can follow.
	 */
	bool CopyEditedLayerProperty(const FEditPropertyChain& Chain, const FWidgetAnimLayer& From, FWidgetAnimLayer& To)
	{
		const FEditPropertyChain::TDoubleLinkedListNode* Node = Chain.GetHead();
		while (Node && (!Node->GetValue() || Node->GetValue()->GetOwnerStruct() != FWidgetAnimLayer::StaticStruct()))
		{
			Node = Node->GetNextNode();
		}

		const void* Src = &From;
		void* Dst = &To;

		while (Node)
		{
			const FProperty* Property = Node->GetValue();
			const FEditPropertyChain::TDoubleLinkedListNode* Next = Node->GetNextNode();
			if (!Property)
			{
				return false;
			}

			// Into a nested struct, when the edit was somewhere inside it.
			if (Next)
			{
				if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
				{
					Src = StructProperty->ContainerPtrToValuePtr<void>(Src);
					Dst = StructProperty->ContainerPtrToValuePtr<void>(Dst);
					Node = Next;
					continue;
				}
			}

			// The edited property itself - or a container the edit was inside, copied whole.
			Property->CopyCompleteValue_InContainer(Dst, Src);
			return true;
		}

		return false;
	}
}

void SWidgetAnimTimeline::OnProxyEdited(UWidgetAnimLayerEditProxy* Proxy, FPropertyChangedChainEvent& PropertyChangedEvent)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Proxy || !Layers || !Layers->IsValidIndex(Proxy->LayerIndex) || !CanEditLayer(Proxy->LayerIndex))
	{
		return;
	}

	// Recorded in the transaction the details view already has open for this edit.
	if (!BeginEdit())
	{
		return;
	}

	FWidgetAnimLayer& Layer = (*Layers)[Proxy->LayerIndex];
	if (!CopyEditedLayerProperty(PropertyChangedEvent.PropertyChain, Proxy->Layer, Layer))
	{
		// Nothing to follow: the whole layer, but for what only the panel sets.
		const int32 LayerGroup = Layer.LayerGroup;
		const bool bLocked = Layer.bLockedInEditor;
		const int32 EditorLine = Layer.EditorLine;
		const bool bHidden = Layer.bHiddenInEditor;
		const bool bHiddenByFocus = Layer.bHiddenByFocus;
		const FString InsertedFrom = Layer.InsertedFrom;
		Layer = Proxy->Layer;
		Layer.LayerGroup = LayerGroup;
		Layer.bLockedInEditor = bLocked;
		Layer.EditorLine = EditorLine;
		Layer.bHiddenInEditor = bHidden;
		Layer.bHiddenByFocus = bHiddenByFocus;
		Layer.InsertedFrom = InsertedFrom;
	}

	// Every selected proxy reports the same edit; the source is told once, on the next frame. A drag in
	// progress stays Interactive unless something in the batch committed.
	const EPropertyChangeType::Type ChangeType = PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
		? EPropertyChangeType::Interactive
		: EPropertyChangeType::ValueSet;
	PendingProxyNotify = (PendingProxyNotify.IsSet() && PendingProxyNotify.GetValue() != EPropertyChangeType::Interactive)
		? PendingProxyNotify.GetValue()
		: ChangeType;
}

FText SWidgetAnimTimeline::GetDetailsTitle() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();

	bool bAnyLocked = false;
	for (const int32 LayerIndex : SelectedLayers)
	{
		bAnyLocked |= IsLayerLocked(LayerIndex);
	}

	const FText Suffix = IsReadOnly()
		? LOCTEXT("ReadOnlySuffix", "  (read only)")
		: (bAnyLocked ? LOCTEXT("LockedSuffix", "  (locked)") : FText::GetEmpty());

	if (!HasAnimation() || (Selection.Group == INDEX_NONE && Selection.Layer == INDEX_NONE))
	{
		// A plain list has no animation of its own to show - its settings are its tracks'.
		if (Source.IsValid() && !Source->SupportsLayerGroups())
		{
			return LOCTEXT("SelectionNone", "Selection");
		}
		return FText::Format(LOCTEXT("SelectionAnimation", "Selection: Animation{0}"), IsReadOnly() ? Suffix : FText::GetEmpty());
	}

	if (IsKeyMultiSelection())
	{
		return FText::Format(LOCTEXT("SelectionKeys", "Selection: {0} Keys{1}"), FText::AsNumber(SelectedKeys.Num()), Suffix);
	}

	if (IsMultiSelection())
	{
		return FText::Format(LOCTEXT("SelectionLayers", "Selection: {0} Tracks{1}"), FText::AsNumber(SelectedLayers.Num()), Suffix);
	}

	const FText GroupName = GetGroupName(Selection.Group);

	if (Selection.Layer == INDEX_NONE || !Layers || !Layers->IsValidIndex(Selection.Layer))
	{
		return FText::Format(LOCTEXT("SelectionGroup", "Selection: {0}{1}"), GroupName, IsReadOnly() ? Suffix : FText::GetEmpty());
	}

	const FText TypeName = GetLayerTypeName((*Layers)[Selection.Layer]);

	// A plain list's tracks are all on the one widget: naming it after each would only take room.
	if (Source.IsValid() && !Source->SupportsLayerGroups())
	{
		if (Selection.Key != INDEX_NONE)
		{
			TArray<float> KeyTimes;
			GetKeyTimes(Selection.Layer, KeyTimes);
			const float Time = KeyTimes.IsValidIndex(Selection.Key) ? KeyTimes[Selection.Key] : 0.f;

			return FText::Format(LOCTEXT("SelectionKeyNoGroup", "Selection: Key {0} at {1}s - {2}{3}"),
				FText::AsNumber(Selection.Key),
				FText::FromString(FString::Printf(TEXT("%.3f"), Time)),
				TypeName,
				Suffix);
		}
		return FText::Format(LOCTEXT("SelectionLayerNoGroup", "Selection: {0} track{1}"), TypeName, Suffix);
	}

	if (Selection.Key != INDEX_NONE)
	{
		TArray<float> KeyTimes;
		GetKeyTimes(Selection.Layer, KeyTimes);
		const float Time = KeyTimes.IsValidIndex(Selection.Key) ? KeyTimes[Selection.Key] : 0.f;

		return FText::Format(LOCTEXT("SelectionKey", "Selection: Key {0} at {1}s - {2} ({3}){4}"),
			FText::AsNumber(Selection.Key),
			FText::FromString(FString::Printf(TEXT("%.3f"), Time)),
			TypeName,
			GroupName,
			Suffix);
	}

	return FText::Format(LOCTEXT("SelectionLayer", "Selection: {0} track ({1}){2}"), TypeName, GroupName, Suffix);
}

// -----------------------------------------------------------------------------------------------------
// Keeping up with the data
// -----------------------------------------------------------------------------------------------------

uint32 SWidgetAnimTimeline::ComputeLayoutSignature() const
{
	uint32 Hash = PointerHash(Source.IsValid() ? Source->GetEditedObject() : nullptr);
	Hash = HashCombine(Hash, GetTypeHash(CanEdit()));

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	Hash = HashCombine(Hash, PointerHash(Groups));
	Hash = HashCombine(Hash, PointerHash(Layers));
	if (!Groups || !Layers)
	{
		return Hash;
	}

	Hash = HashCombine(Hash, GetTypeHash(Groups->Num()));

	Hash = HashCombine(Hash, GetTypeHash(Layers->Num()));
	for (const FWidgetAnimLayer& Layer : *Layers)
	{
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.AnimType)));
		Hash = HashCombine(Hash, GetTypeHash(Layer.LayerGroup));

		// A lock changes what the row offers - its grip, its delete button - and whether its fields are editable.
		Hash = HashCombine(Hash, GetTypeHash(Layer.bLockedInEditor));
		Hash = HashCombine(Hash, GetTypeHash(Layer.bHiddenInEditor));

		// Timing decides which rows a layer has - Duration and Easing, or a spring - and how many keys a keyed
		// layer has decides whether a selected one still exists.
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.Timing)));
		Hash = HashCombine(Hash, GetTypeHash(Layer.ValueKeys.Keys.Num()));

		// Which Stagger it has decides which of the stagger's own settings are shown.
		Hash = HashCombine(Hash, PointerHash(Layer.Expander.GetScriptStruct()));

		// What a value-driven Transform drives decides which Value Types it offers, and its Value Type which of
		// those it can drive.
		if (Layer.IsValueDriven())
		{
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.ValueDrives)));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.ValueOptions.Kind)));

			// Which Value Input it has decides which of the input's own settings are shown.
			Hash = HashCombine(Hash, PointerHash(Layer.ValueInput.GetScriptStruct()));
		}

		if (Layer.AnimType == EWidgetAnimType::Transform)
		{
			// Key count matters (a selected key can vanish); animate-shape decides whether Shape rows exist.
			Hash = HashCombine(Hash, GetTypeHash(Layer.Transform.Keys.Num()));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.Transform.bAnimateShape)));
			Hash = HashCombine(Hash, PointerHash(Layer.Transform.Curve.ExternalCurve.Get()));
		}
	}
	return Hash;
}

EActiveTimerReturnType SWidgetAnimTimeline::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	// Tells a source that plays the preview itself that it is wanted; it stops, and puts everything back
	// as authored, soon after this stops being called.
	if (Source.IsValid() && !IsBodyCollapsed())
	{
		Source->NotifyPanelActive();
	}

	// Edits made through the multi-selection proxies, told to the source once for every proxy that made them.
	if (PendingProxyNotify.IsSet())
	{
		const EPropertyChangeType::Type ChangeType = PendingProxyNotify.GetValue();
		PendingProxyNotify.Reset();
		NotifyGroupsChanged(ChangeType);
	}

	// The proxies follow the real layers - a drag on the tracks, an undo - so what the Selection panel shows for
	// several layers is never stale.
	if (IsMultiSelection())
	{
		SyncLayerProxies();
	}

	// The Track Editor's lines follow every edit, wherever it was made - they are cheap to work out.
	UpdateTrackLayout();

	// A drag is its own source of truth until it ends; refreshing the list or the Selection panel under it would
	// only rebuild them every frame.
	if (IsDragging())
	{
		return EActiveTimerReturnType::Continue;
	}

	// Anything that changes what the list or the Selection panel contain - made here, in the Selection panel,
	// by an undo, a compile or a different asset - is caught one frame later, never mid-edit.
	const uint32 NewSignature = ComputeLayoutSignature();
	if (NewSignature != LayoutSignature)
	{
		Selection = Validate(Selection);
		RebuildRows();
		RefreshDetails();
	}
	else if (ComputeListSignature() != ListSignature)
	{
		// Only the order things play in changed - a Delay typed in the Selection panel, say. The list follows;
		// the Selection panel is left alone, since that may be exactly where the typing is going on.
		RebuildLayerList();
	}

	return EActiveTimerReturnType::Continue;
}

void SWidgetAnimTimeline::RebuildRows()
{
	LayoutSignature = ComputeLayoutSignature();
	UpdateTrackLayout();
	RebuildLayerList();
}

void SWidgetAnimTimeline::GetListOrder(TArray<int32>& OutOrder) const
{
	OutOrder.Reset();

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		return;
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);

	// Group by group, and in each the order its layers start - the order the animation plays them - ties in
	// the order they sit in the sequence.
	for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
	{
		TArray<int32> Members;
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			if (GetLayerGroup(LayerIndex) == GroupIndex)
			{
				Members.Add(LayerIndex);
			}
		}

		Members.StableSort([&Spans](int32 A, int32 B)
		{
			const float StartA = Spans.IsValidIndex(A) ? Spans[A].X : 0.f;
			const float StartB = Spans.IsValidIndex(B) ? Spans[B].X : 0.f;
			return StartA < StartB - KINDA_SMALL_NUMBER;
		});

		OutOrder.Append(Members);
	}
}

uint32 SWidgetAnimTimeline::ComputeListSignature() const
{
	TArray<int32> Order;
	GetListOrder(Order);

	uint32 Hash = GetTypeHash(Order.Num());
	for (const int32 LayerIndex : Order)
	{
		Hash = HashCombine(Hash, GetTypeHash(LayerIndex));
	}
	return Hash;
}

FText SWidgetAnimTimeline::GetLayerTimeRange(int32 LayerIndex) const
{
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	if (!Spans.IsValidIndex(LayerIndex))
	{
		return FText::GetEmpty();
	}

	const FVector2f Span = Spans[LayerIndex];
	return FText::FromString(FString::Printf(TEXT("%.2f - %.2f s"), Span.X, Span.X + Span.Y));
}

void SWidgetAnimTimeline::RebuildLayerList()
{
	using namespace WidgetAnimTimeline;

	ListSignature = ComputeListSignature();

	if (!LayerList.IsValid())
	{
		return;
	}

	LayerList->ClearChildren();

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		return;
	}

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);

	TArray<int32> Order;
	GetListOrder(Order);

	// A thin line under every row.
	auto Separator = []()
	{
		return SNew(SBox)
			.HeightOverride(1.f)
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity(SeparatorColor)
			];
	};

	// The group's color, down the left of its row and every one of its layers' rows.
	auto Band = [](int32 GroupIndex)
	{
		return SNew(SBox)
			.WidthOverride(GroupBandWidth)
			[
				SNew(SImage)
				.Image(WhiteBrush())
				.ColorAndOpacity(GetGroupColor(GroupIndex))
			];
	};

	for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
	{
		// ---- The group: fold arrow, name, and the widget it animates -------------------------------------

		const TSharedRef<TWeakPtr<SWidget>> GroupRowWidget = MakeShared<TWeakPtr<SWidget>>();
		const TSharedRef<SWidgetAnimListRow> GroupRow = SNew(SWidgetAnimListRow, WeakThisPtr, GroupIndex, INDEX_NONE)
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListGroupRowTooltip()))
			[
				SNew(SBox)
				.HeightOverride(ListRowHeight)
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(HeaderColor())
					]

					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity_Lambda([this, GroupIndex, GroupRowWidget]()
						{
							const TSharedPtr<SWidget> Pinned = GroupRowWidget->Pin();
							if (Selection.Group == GroupIndex && Selection.Layer == INDEX_NONE)
							{
								return FSlateColor(WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha));
							}
							return FSlateColor(Pinned.IsValid() && Pinned->IsHovered() ? WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha) : FLinearColor::Transparent);
						})
					]

					+ SOverlay::Slot()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							Band(GroupIndex)
						]

						// Fold open / shut: shows or hides this group's layers in the list.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "NoBorder")
							.ContentPadding(FMargin(1.f, 0.f))
							.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListFoldTooltip()))
							.OnClicked_Lambda([this, GroupIndex]() { ToggleListGroupCollapsed(GroupIndex); return FReply::Handled(); })
							[
								SNew(SImage)
								.Image(IsListGroupCollapsed(GroupIndex)
									? PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" })
									: PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" }))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(4.f, 0.f, 6.f, 0.f))
						[
							SNew(STextBlock)
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
							.Text_Lambda([this, GroupIndex]() { return GetGroupName(GroupIndex); })
						]

						// The widget it animates, as a small pill on the right.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.MaxWidth(120.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
						[
							SNew(SBorder)
							.BorderImage(PillBrush())
							.Padding(FMargin(7.f, 1.f))
							.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::GroupTargetTooltip()))
							[
								SNew(STextBlock)
								.Text_Lambda([this, GroupIndex]() { return GetGroupTargetLabel(GroupIndex); })
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
					]

					+ SOverlay::Slot()
					.VAlign(VAlign_Bottom)
					[
						Separator()
					]
				]
			];
		*GroupRowWidget = GroupRow;

		LayerList->AddSlot()
		.AutoHeight()
		[
			GroupRow
		];

		if (IsListGroupCollapsed(GroupIndex))
		{
			continue;
		}

		// ---- Its layers, in the order they play: what, and when ----------------------------------------------

		for (const int32 LayerIndex : Order)
		{
			if (GetLayerGroup(LayerIndex) != GroupIndex)
			{
				continue;
			}

			const TSharedRef<TWeakPtr<SWidget>> RowWidget = MakeShared<TWeakPtr<SWidget>>();
			auto IsWashedOut = [this, LayerIndex]() { return IsReadOnly() || IsLayerLocked(LayerIndex) || IsLayerHidden(LayerIndex); };

			const TSharedRef<SWidgetAnimListRow> LayerRow = SNew(SWidgetAnimListRow, WeakThisPtr, GroupIndex, LayerIndex)
				.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListLayerRowTooltip()))
				[
					SNew(SBox)
					.HeightOverride(ListRowHeight)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(BackgroundColor())
						]

						// The list's own highlight: a light tint on the rows picked here or in the Track Editor, and
						// on the one under the mouse.
						+ SOverlay::Slot()
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity_Lambda([this, LayerIndex, RowWidget]()
							{
								const TSharedPtr<SWidget> Pinned = RowWidget->Pin();
								if (IsLayerSelected(LayerIndex))
								{
									return FSlateColor(WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha * 0.6f));
								}
								return FSlateColor(Pinned.IsValid() && Pinned->IsHovered() ? WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha) : FLinearColor::Transparent);
							})
						]

						+ SOverlay::Slot()
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								Band(GroupIndex)
							]

							// A slim accent beside the band while the layer is selected.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								SNew(SBox)
								.WidthOverride(2.f)
								[
									SNew(SImage)
									.Image(WhiteBrush())
									.ColorAndOpacity_Lambda([this, LayerIndex]()
									{
										return FSlateColor(IsLayerSelected(LayerIndex) ? Palette::ListSelectedAccent : FLinearColor::Transparent);
									})
								]
							]

							// Its type - and, for a track brought in with Insert Existing Widget Animation, the
							// animation it came from, in italics.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(18.f, 0.f, 0.f, 0.f))
							[
								SNew(STextBlock)
								.ColorAndOpacity_Lambda([IsWashedOut]()
								{
									return IsWashedOut() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
								})
								.Text_Lambda([this, LayerIndex]()
								{
									const TArray<FWidgetAnimLayer>* Current = GetLayers();
									return Current && Current->IsValidIndex(LayerIndex)
										? GetLayerTypeName((*Current)[LayerIndex])
										: FText::GetEmpty();
								})
							]

							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
							[
								SNew(STextBlock)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text_Lambda([this, LayerIndex]()
								{
									const TArray<FWidgetAnimLayer>* Current = GetLayers();
									return Current && Current->IsValidIndex(LayerIndex) && !(*Current)[LayerIndex].InsertedFrom.IsEmpty()
										? FText::Format(LOCTEXT("ListInsertedFrom", " \u2013 {0}"), FText::FromString((*Current)[LayerIndex].InsertedFrom))
										: FText::GetEmpty();
								})
							]

							// A hidden track's closed eye, always shown. Clicking it shows the track again.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ContentPadding(FMargin(2.f, 0.f))
								.Visibility_Lambda([this, LayerIndex]() { return IsLayerHidden(LayerIndex) ? EVisibility::Visible : EVisibility::Collapsed; })
								.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ShowHiddenTrackTooltip()))
								.OnClicked_Lambda([this, LayerIndex]()
								{
									SetLayersHidden({ LayerIndex }, /*bHidden=*/false);
									return FReply::Handled();
								})
								[
									MakeFixedIcon(PickBrush({ "Icons.Hidden", "Level.NotVisibleIcon16x" }), FSlateColor::UseSubduedForeground(), 12.f)
								]
							]

							// A locked track's lock, always shown. Clicking it unlocks the track - unless the whole panel is locked.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							[
								SNew(SButton)
								.ButtonStyle(FAppStyle::Get(), "SimpleButton")
								.ContentPadding(FMargin(2.f, 0.f))
								.Visibility_Lambda([this, LayerIndex]() { return IsLayerLocked(LayerIndex) ? EVisibility::Visible : EVisibility::Collapsed; })
								.IsEnabled_Lambda([this]() { return CanEdit(); })
								.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::UnlockTrackTooltip()))
								.OnClicked_Lambda([this, LayerIndex]()
								{
									SetLayersLocked({ LayerIndex }, /*bLocked=*/false);
									return FReply::Handled();
								})
								[
									MakeFixedIcon(PickBrush({ "Icons.Lock", "PropertyWindow.Locked" }), FSlateColor::UseSubduedForeground(), 12.f)
								]
							]

							// When it runs, from the top of the animation.
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(6.f, 0.f, 8.f, 0.f))
							[
								SNew(STextBlock)
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor::UseSubduedForeground())
								.Text_Lambda([this, LayerIndex]() { return GetLayerTimeRange(LayerIndex); })
							]
						]

						+ SOverlay::Slot()
						.VAlign(VAlign_Bottom)
						[
							Separator()
						]
					]
				];
			*RowWidget = LayerRow;

			LayerList->AddSlot()
			.AutoHeight()
			[
				LayerRow
			];
		}
	}
}

void SWidgetAnimTimeline::UpdateTrackLayout()
{
	// The context tracks: placed as their animation places them, at the speed it plays, and packed onto as few rows
	// as keep them from overlapping.
	ContextSpans.Reset();
	ContextRows.Reset();
	NumContextRows = 0;
	if (const TArray<FWidgetAnimLayer>* ContextLayers = GetContextLayers())
	{
		TArray<FWidgetAnimSettings> Placed;
		UWidgetAnimLibrary::BuildLayerSettings(*ContextLayers, Placed);
		const float ContextScale = Source.IsValid() ? Source->GetContextTimeScale() : 1.f;

		TArray<float> RowEnds;
		for (int32 Index = 0; Index < ContextLayers->Num() && Placed.IsValidIndex(Index); ++Index)
		{
			const FVector2f Span(Placed[Index].Delay * ContextScale, (*ContextLayers)[Index].GetRunLength() * ContextScale);
			ContextSpans.Add(Span);

			int32 Row = RowEnds.IndexOfByPredicate([&Span](float RowEnd) { return RowEnd <= Span.X + 0.0005f; });
			if (Row == INDEX_NONE)
			{
				Row = RowEnds.Add(0.f);
			}
			RowEnds[Row] = Span.X + Span.Y;
			ContextRows.Add(Row);
		}
		NumContextRows = RowEnds.Num();
	}

	TrackLines.Reset();
	LayerToLine.Reset();
	LayerLineOrdinals.Reset();
	GroupLineCounts.Reset();
	UpdateStaggerFans();
	GetLayerSpans(TrackSpans);

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		return;
	}

	LayerToLine.Init(INDEX_NONE, Layers->Num());
	LayerLineOrdinals.Init(0, Layers->Num());
	GroupLineCounts.Init(0, Groups->Num());

	// Where a layer asks to be. A layer with no line of its own set comes after every line that is set, in its
	// order in the sequence - which is how older animations lay out: one track per line, as they always did.
	auto LineKey = [Layers](int32 LayerIndex)
	{
		const int32 Line = (*Layers)[LayerIndex].EditorLine;
		return Line >= 0 ? Line : 1000000 + LayerIndex;
	};

	auto Overlaps = [this](int32 A, int32 B)
	{
		if (!TrackSpans.IsValidIndex(A) || !TrackSpans.IsValidIndex(B))
		{
			return false;
		}
		// A stagger's tail is part of the track: another track on its line starts after it.
		constexpr float Tolerance = 0.0005f;
		const FVector2f SpanA = TrackSpans[A];
		const FVector2f SpanB = TrackSpans[B];
		const float EndA = SpanA.X + SpanA.Y + GetLayerFanSeconds(A);
		const float EndB = SpanB.X + SpanB.Y + GetLayerFanSeconds(B);
		return SpanA.X < EndB - Tolerance && SpanB.X < EndA - Tolerance;
	};

	auto StartOf = [this](int32 LayerIndex) { return TrackSpans.IsValidIndex(LayerIndex) ? TrackSpans[LayerIndex].X : 0.f; };

	for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
	{
		TArray<int32> Members;
		TArray<int32> Keys;
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			if (GetLayerGroup(LayerIndex) == GroupIndex)
			{
				Members.Add(LayerIndex);
				Keys.AddUnique(LineKey(LayerIndex));
			}
		}
		Keys.Sort();

		// Each line the group's layers ask for, in order. On it, the tracks that stay put first - so a track
		// being dragged is the one that gives way - then by when they start. A track overlapping one already
		// placed goes on a line of its own just below.
		TArray<TArray<int32>> GroupLines;
		for (const int32 Key : Keys)
		{
			TArray<int32> OnLine = Members.FilterByPredicate([&LineKey, Key](int32 LayerIndex) { return LineKey(LayerIndex) == Key; });
			OnLine.StableSort([this, &StartOf](int32 A, int32 B)
			{
				const bool bDraggedA = IsLayerBeingDragged(A);
				const bool bDraggedB = IsLayerBeingDragged(B);
				if (bDraggedA != bDraggedB)
				{
					return !bDraggedA;
				}
				return StartOf(A) < StartOf(B);
			});

			const int32 FirstLine = GroupLines.Num();
			for (const int32 LayerIndex : OnLine)
			{
				int32 Placed = INDEX_NONE;
				for (int32 Candidate = FirstLine; Candidate < GroupLines.Num() && Placed == INDEX_NONE; ++Candidate)
				{
					const bool bFree = !GroupLines[Candidate].ContainsByPredicate([&Overlaps, LayerIndex](int32 Other) { return Overlaps(LayerIndex, Other); });
					if (bFree)
					{
						Placed = Candidate;
					}
				}

				if (Placed == INDEX_NONE)
				{
					Placed = GroupLines.AddDefaulted();
				}
				GroupLines[Placed].Add(LayerIndex);
			}
		}

		GroupLineCounts[GroupIndex] = GroupLines.Num();

		// The compact panel has no Layer Groups to show: no group's own line, and nothing folded - its tracks'
		// lines follow one another straight from the top.
		if (!bCompact)
		{
			FTrackLine& GroupLine = TrackLines.AddDefaulted_GetRef();
			GroupLine.Kind = FTrackLine::EKind::Group;
			GroupLine.Group = GroupIndex;
		}

		for (int32 Ordinal = 0; Ordinal < GroupLines.Num(); ++Ordinal)
		{
			GroupLines[Ordinal].StableSort([&StartOf](int32 A, int32 B) { return StartOf(A) < StartOf(B); });
			for (const int32 LayerIndex : GroupLines[Ordinal])
			{
				LayerLineOrdinals[LayerIndex] = Ordinal;
			}

			if (!bCompact && TrackCollapsedGroups.Contains(GroupIndex))
			{
				continue;
			}

			FTrackLine& Line = TrackLines.AddDefaulted_GetRef();
			Line.Kind = FTrackLine::EKind::Lane;
			Line.Group = GroupIndex;
			Line.Ordinal = Ordinal;
			Line.Layers = GroupLines[Ordinal];

			for (const int32 LayerIndex : Line.Layers)
			{
				LayerToLine[LayerIndex] = TrackLines.Num() - 1;
			}
		}
	}
}

void SWidgetAnimTimeline::CommitTrackLines()
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers)
	{
		return;
	}

	for (int32 LayerIndex = 0; LayerIndex < Layers->Num() && LayerLineOrdinals.IsValidIndex(LayerIndex); ++LayerIndex)
	{
		if ((*Layers)[LayerIndex].EditorLine != LayerLineOrdinals[LayerIndex])
		{
			(*Layers)[LayerIndex].EditorLine = LayerLineOrdinals[LayerIndex];
		}
	}
}

void SWidgetAnimTimeline::RevealLayer(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Tracks.IsValid() || !Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return;
	}

	// A track folded away in the Track Editor is unfolded to be shown.
	const int32 GroupIndex = GetLayerGroup(LayerIndex);
	if (IsTrackGroupCollapsed(GroupIndex))
	{
		ToggleTrackGroupCollapsed(GroupIndex);
	}
	UpdateTrackLayout();

	Tracks->RevealLine(GetLayerLine(LayerIndex));
	if (TrackSpans.IsValidIndex(LayerIndex))
	{
		Tracks->RevealTime(TrackSpans[LayerIndex].X, TrackSpans[LayerIndex].X + TrackSpans[LayerIndex].Y);
	}
}

void SWidgetAnimTimeline::RevealGroup(int32 GroupIndex)
{
	if (!Tracks.IsValid())
	{
		return;
	}

	const int32 LineIndex = TrackLines.IndexOfByPredicate([GroupIndex](const FTrackLine& Line)
	{
		return Line.Kind == FTrackLine::EKind::Group && Line.Group == GroupIndex;
	});
	Tracks->RevealLine(LineIndex);
}

FReply SWidgetAnimTimeline::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();

	// Delete or Backspace: the selected keys if there are any, else every selected layer.
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		if (IsKeyMultiSelection())
		{
			DeleteSelectedKeys();
			return FReply::Handled();
		}

		if (Selection.Key != INDEX_NONE)
		{
			// A key that can't go - a track's start or end - is still a key, not a reason to delete its track.
			if (CanDeleteKey(Selection.Layer, Selection.Key))
			{
				DeleteKey(Selection.Layer, Selection.Key);
			}
			return FReply::Handled();
		}

		if (SelectedLayers.Num() > 0 && CanEdit())
		{
			DeleteSelectedLayers();
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	// Ctrl / Cmd + A: every layer, as in a file browser.
	if (Key == EKeys::A && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()))
	{
		SelectAllLayers();
		return FReply::Handled();
	}

	// Ctrl / Cmd + C, X, V: the selected tracks, to the clipboard and back - pasted after the selected track, from
	// the playhead, into its group.
	if ((InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && (Key == EKeys::C || Key == EKeys::X || Key == EKeys::V))
	{
		if (Key == EKeys::V)
		{
			if (!CanEdit() || !CanPasteLayers())
			{
				return FReply::Unhandled();
			}
			PasteLayers(Selection.Group);
			return FReply::Handled();
		}

		const TArray<int32> Layers = GetClipboardLayers();
		if (Layers.Num() == 0)
		{
			return FReply::Unhandled();
		}
		if (Key == EKeys::C)
		{
			CopyLayers(Layers);
		}
		else
		{
			CutLayers(Layers);
		}
		return FReply::Handled();
	}

	if (Key == EKeys::Escape)
	{
		ClearSelection();
		return FReply::Handled();
	}

	if (Key == EKeys::F)
	{
		if (Tracks.IsValid())
		{
			Tracks->FitToAnimation();
		}
		return FReply::Handled();
	}

	if (Key == EKeys::SpaceBar)
	{
		TogglePlay();
		return FReply::Handled();
	}

	if (Key == EKeys::Comma || Key == EKeys::Period)
	{
		StepToMarker(Key == EKeys::Comma ? -1 : 1);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

// -----------------------------------------------------------------------------------------------------
// Editing
// -----------------------------------------------------------------------------------------------------

UObject* SWidgetAnimTimeline::BeginEdit()
{
	UObject* Edited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
	if (!Edited || !CanEdit() || !Source->GetGroups() || !Source->GetLayers())
	{
		return nullptr;
	}

	Edited->Modify();
	return Edited;
}

void SWidgetAnimTimeline::NotifyGroupsChanged(EPropertyChangeType::Type ChangeType)
{
	if (Source.IsValid())
	{
		Source->NotifyGroupsChanged(ChangeType);
	}
}

int32 SWidgetAnimTimeline::GetInsertIndex() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return 0;
	}
	return Layers->IsValidIndex(Selection.Layer) ? Selection.Layer + 1 : Layers->Num();
}

int32 SWidgetAnimTimeline::GetDefaultGroup(int32 GroupIndex) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups || Groups->Num() == 0)
	{
		return INDEX_NONE;
	}
	if (Groups->IsValidIndex(GroupIndex))
	{
		return GroupIndex;
	}
	if (Groups->IsValidIndex(Selection.Group))
	{
		return Selection.Group;
	}
	return Groups->Num() - 1;
}

void SWidgetAnimTimeline::GetAuthoredStarts(TArray<float>& OutStarts) const
{
	OutStarts.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	// Staggers reaching as far as the preview plays them, and lengths worked out there, as the bars are drawn.
	TArray<float> Extras;
	GetPlacementExtras(Extras);
	UWidgetAnimLibrary::PlaceLayerStarts(*Layers, Extras, OutStarts);
}

void SWidgetAnimTimeline::PinLayerStarts(const TArray<float>& OldStarts, const TArray<int32>& OldIndexOf)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers)
	{
		return;
	}

	// Walked in sequence, as playback places them: each layer measures from the one now above it.
	float PreviousStart = 0.f;
	float PreviousEnd = 0.f;

	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const float Anchor = Layer.Start == EWidgetAnimLayerStart::AfterPrevious ? PreviousEnd : PreviousStart;
		const int32 OldIndex = OldIndexOf.IsValidIndex(LayerIndex) ? OldIndexOf[LayerIndex] : INDEX_NONE;

		float LayerStart = 0.f;
		if (OldStarts.IsValidIndex(OldIndex))
		{
			// Was there before: exactly where it was.
			LayerStart = OldStarts[OldIndex];
			const float Delay = LayerStart - Anchor;
			if (!FMath::IsNearlyEqual(Layer.Delay, Delay, 1.e-5f))
			{
				Layer.Delay = Delay;
			}
		}
		else
		{
			// New: wherever its own Starts and Delay put it.
			LayerStart = FMath::Max(Anchor + Layer.Delay, 0.f);
		}

		// A stagger reaches on past its first item: the fan it had before the edit, found by where it was then.
		const float Fan = TrackFans.IsValidIndex(OldIndex) ? TrackFans[OldIndex] : WidgetAnimStaggerFans::GetExtra(Layer, INDEX_NONE);

		PreviousStart = LayerStart;
		const float Run = TrackRuns.IsValidIndex(OldIndex) && TrackRuns[OldIndex] >= 0.f ? TrackRuns[OldIndex] : Layer.GetRunLength();
		PreviousEnd = LayerStart + Run + Fan;
	}
}

namespace
{
	/** Where each layer was before some were taken out: every index but those removed, in order. */
	TArray<int32> OldIndicesAfterRemoving(int32 OldNum, const TArray<int32>& Removed)
	{
		TArray<int32> OldIndexOf;
		for (int32 OldIndex = 0; OldIndex < OldNum; ++OldIndex)
		{
			if (!Removed.Contains(OldIndex))
			{
				OldIndexOf.Add(OldIndex);
			}
		}
		return OldIndexOf;
	}

	/** Where each layer was before Count new ones went in at InsertAt: the new ones weren't anywhere. */
	TArray<int32> OldIndicesAfterInserting(int32 NewNum, int32 InsertAt, int32 Count)
	{
		TArray<int32> OldIndexOf;
		for (int32 NewIndex = 0; NewIndex < NewNum; ++NewIndex)
		{
			OldIndexOf.Add(NewIndex < InsertAt ? NewIndex : (NewIndex < InsertAt + Count ? INDEX_NONE : NewIndex - Count));
		}
		return OldIndexOf;
	}
}

void SWidgetAnimTimeline::AddGroup()
{
	const FScopedTransaction Transaction(LOCTEXT("AddGroupTransaction", "Add Layer Group"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	Groups.AddDefaulted();
	const int32 NewGroup = Groups.Num() - 1;

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();
	SelectGroup(NewGroup);
}

void SWidgetAnimTimeline::DuplicateGroup(int32 GroupIndex)
{
	const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
	if (!Current || !Current->IsValidIndex(GroupIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DuplicateGroupTransaction", "Duplicate Layer Group"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	TArray<int32> OldIndexOf;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		OldIndexOf.Add(LayerIndex);
	}

	// The copy goes last, so no other group's index moves. It keeps the name only if it had one - a default
	// name follows its position anyway - and says it is a copy.
	FWidgetAnimLayerGroup Copy = Groups[GroupIndex];
	if (!Copy.GroupName.TrimStartAndEnd().IsEmpty())
	{
		Copy.GroupName += TEXT(" Copy");
	}
	Groups.Add(Copy);
	const int32 NewGroup = Groups.Num() - 1;

	// Its layers copied too, each placed right after the one it copies and playing With it, so the copy
	// moves its widget at the same moments without shifting anything else in the sequence.
	for (int32 LayerIndex = Layers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		if (UWidgetAnimLibrary::GetLayerGroupIndex(Layers[LayerIndex], NewGroup) == GroupIndex)
		{
			FWidgetAnimLayer LayerCopy = Layers[LayerIndex];
			LayerCopy.LayerGroup = NewGroup;
			LayerCopy.Start = EWidgetAnimLayerStart::WithPrevious;
			LayerCopy.Delay = 0.f;
			Layers.Insert(LayerCopy, LayerIndex + 1);

			// The copy starts exactly with the track it copies.
			const int32 CopiedFrom = OldIndexOf[LayerIndex];
			OldIndexOf.Insert(CopiedFrom, LayerIndex + 1);
		}
	}

	PinLayerStarts(OldStarts, OldIndexOf);

	NotifyGroupsChanged(EPropertyChangeType::Duplicate);
	RebuildRows();
	SelectGroup(NewGroup);
}

void SWidgetAnimTimeline::DeleteGroup(int32 GroupIndex)
{
	const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
	if (!Current || !Current->IsValidIndex(GroupIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteGroupTransaction", "Delete Layer Group"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	const int32 NumGroups = Groups.Num();

	// The group's tracks go with it - every other track staying exactly where it is in time - and every later
	// group's tracks follow their group down one place.
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	TArray<int32> Removed;
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		if (UWidgetAnimLibrary::GetLayerGroupIndex(Layers[LayerIndex], NumGroups) == GroupIndex)
		{
			Removed.Add(LayerIndex);
		}
	}
	const int32 OldNum = Layers.Num();

	Layers.RemoveAll([GroupIndex, NumGroups](const FWidgetAnimLayer& Layer)
	{
		return UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups) == GroupIndex;
	});
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, Removed));
	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
		Layer.LayerGroup = InGroup > GroupIndex ? InGroup - 1 : InGroup;
	}
	Groups.RemoveAt(GroupIndex);

	// Fold state is by position, and moves the same way.
	for (TSet<int32>* Collapsed : { &TrackCollapsedGroups, &ListCollapsedGroups })
	{
		TSet<int32> Shifted;
		for (const int32 Folded : *Collapsed)
		{
			if (Folded != GroupIndex)
			{
				Shifted.Add(Folded > GroupIndex ? Folded - 1 : Folded);
			}
		}
		*Collapsed = MoveTemp(Shifted);
	}

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RebuildRows();
	ClearSelection();
}

void SWidgetAnimTimeline::MoveGroupTo(int32 GroupIndex, int32 InsertBefore)
{
	const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
	if (!Current || !Current->IsValidIndex(GroupIndex))
	{
		return;
	}

	InsertBefore = FMath::Clamp(InsertBefore, 0, Current->Num());
	if (InsertBefore == GroupIndex || InsertBefore == GroupIndex + 1)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ReorderGroupTransaction", "Reorder Layer Group"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	const int32 NumGroups = Groups.Num();
	const int32 NewIndex = InsertBefore > GroupIndex ? InsertBefore - 1 : InsertBefore;

	// Where every old position ends up, so each layer follows its group rather than staying on a number.
	TArray<int32> Order;
	for (int32 Index = 0; Index < NumGroups; ++Index)
	{
		Order.Add(Index);
	}
	Order.RemoveAt(GroupIndex);
	Order.Insert(GroupIndex, NewIndex);

	TArray<int32> NewPositionOf;
	NewPositionOf.SetNum(NumGroups);
	for (int32 NewPosition = 0; NewPosition < NumGroups; ++NewPosition)
	{
		NewPositionOf[Order[NewPosition]] = NewPosition;
	}

	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
		if (InGroup != INDEX_NONE)
		{
			Layer.LayerGroup = NewPositionOf[InGroup];
		}
	}

	const FWidgetAnimLayerGroup Moved = Groups[GroupIndex];
	Groups.RemoveAt(GroupIndex);
	Groups.Insert(Moved, NewIndex);

	for (TSet<int32>* Collapsed : { &TrackCollapsedGroups, &ListCollapsedGroups })
	{
		TSet<int32> Remapped;
		for (const int32 Folded : *Collapsed)
		{
			if (NewPositionOf.IsValidIndex(Folded))
			{
				Remapped.Add(NewPositionOf[Folded]);
			}
		}
		*Collapsed = MoveTemp(Remapped);
	}

	NotifyGroupsChanged(EPropertyChangeType::ArrayMove);
	RebuildRows();
	SelectGroup(NewIndex);
}

void SWidgetAnimTimeline::AddLayer(int32 GroupIndex, EWidgetAnimType AnimType, const FInstancedStruct* InitialTypeParams, TOptional<float> AtSeconds)
{
	const FScopedTransaction Transaction(LOCTEXT("AddLayerTransaction", "Add Animation Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();

	// A layer always lands in a group: the one asked for, else the selection's, else the last, else a new first one.
	if (Groups.Num() == 0)
	{
		Groups.AddDefaulted();
	}
	GroupIndex = GetDefaultGroup(GroupIndex);

	const int32 InsertAt = FMath::Clamp(GetInsertIndex(), 0, Layers.Num());

	FWidgetAnimLayer NewLayer;
	NewLayer.AnimType = AnimType;
	NewLayer.LayerGroup = GroupIndex;

	// A registered type is its settings struct, so a new track of one starts from the settings it was picked with.
	if (AnimType == EWidgetAnimType::Custom && InitialTypeParams)
	{
		NewLayer.TypeParams = *InitialTypeParams;

		// A type whose value is a steady pace - a text typed out - starts without an ease that would make it uneven.
		const FWidgetAnimEvaluator* Evaluator = NewLayer.FindEvaluator();
		if (Evaluator && Evaluator->PrefersLinearEasing())
		{
			NewLayer.Easing = EWidgetAnimEasing::Linear;
		}
	}

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);

	if (AtSeconds.IsSet())
	{
		// Added from a menu opened on the Track Editor: it starts where the menu was opened. With the track above it,
		// delayed from that one's start, since the start is the one thing known exactly - an After Previous delay would
		// hang on the length of the track above. The time axis is in played seconds, the Delay in authored ones.
		const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
		const float Authored = FMath::RoundToFloat(FMath::Max(AtSeconds.GetValue(), 0.f) / TimeScale * 100.f) / 100.f;
		const float AnchorStart = OldStarts.IsValidIndex(InsertAt - 1) ? OldStarts[InsertAt - 1] : 0.f;
		NewLayer.Start = EWidgetAnimLayerStart::WithPrevious;
		NewLayer.Delay = Authored - AnchorStart;
	}
	else if (InsertAt > 0)
	{
		// A new layer follows the one above it, so adding layers builds a sequence rather than a pile.
		NewLayer.Start = EWidgetAnimLayerStart::AfterPrevious;
	}

	// A line of its own - EditorLine is left unset - so it can be seen, and dragged up beside another from there.
	// Every track already there stays where it is in time, the one below the new one included.
	Layers.Insert(NewLayer, InsertAt);
	PinLayerStarts(OldStarts, OldIndicesAfterInserting(Layers.Num(), InsertAt, 1));
	TrackCollapsedGroups.Remove(GroupIndex);
	ListCollapsedGroups.Remove(GroupIndex);

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();
	SelectLayer(InsertAt);
}

void SWidgetAnimTimeline::DuplicateLayer(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DuplicateLayerTransaction", "Duplicate Animation Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	FWidgetAnimLayer Copy = Layers[LayerIndex];

	// A copy is made to be changed, so it never starts out locked or hidden. It starts exactly with the track it
	// copies, and nothing else moves.
	Copy.bLockedInEditor = false;
	Copy.bHiddenInEditor = false;
	Copy.bHiddenByFocus = false;

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	Layers.Insert(Copy, LayerIndex + 1);

	TArray<int32> OldIndexOf = OldIndicesAfterInserting(Layers.Num(), LayerIndex + 1, 1);
	OldIndexOf[LayerIndex + 1] = LayerIndex;
	PinLayerStarts(OldStarts, OldIndexOf);

	NotifyGroupsChanged(EPropertyChangeType::Duplicate);
	RebuildRows();
	SelectLayer(LayerIndex + 1);
}

void SWidgetAnimTimeline::DeleteLayer(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteLayerTransaction", "Delete Animation Track"));
	if (!BeginEdit())
	{
		return;
	}

	// Every other track stays exactly where it is in time - the one below included, which measures its Delay
	// from the track above it and would otherwise jump.
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	const int32 OldNum = Source->GetLayers()->Num();
	Source->GetLayers()->RemoveAt(LayerIndex);
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, { LayerIndex }));

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RebuildRows();
	RemapSelectionAfterRemoving({ LayerIndex });
}

void SWidgetAnimTimeline::DeleteSelectedLayers()
{
	TArray<int32> ToDelete;
	for (const int32 LayerIndex : SelectedLayers)
	{
		if (CanEditLayer(LayerIndex))
		{
			ToDelete.Add(LayerIndex);
		}
	}

	if (ToDelete.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(ToDelete.Num() == 1
		? LOCTEXT("DeleteLayerTransaction", "Delete Animation Track")
		: LOCTEXT("DeleteLayersTransaction", "Delete Animation Tracks"));
	if (!BeginEdit())
	{
		return;
	}

	// From the end, so each removal leaves the indices still to go where they were. Every track left stays
	// exactly where it is in time.
	ToDelete.Sort();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	const int32 OldNum = Layers.Num();
	for (int32 Index = ToDelete.Num() - 1; Index >= 0; --Index)
	{
		Layers.RemoveAt(ToDelete[Index]);
	}
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, ToDelete));

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RebuildRows();
	RemapSelectionAfterRemoving(ToDelete);
}

void SWidgetAnimTimeline::RemapSelectionAfterRemoving(const TArray<int32>& Removed)
{
	// Whatever stays selected keeps pointing at the same layers, which have moved up past the ones removed.
	auto Remap = [&Removed](int32 LayerIndex) -> int32
	{
		if (Removed.Contains(LayerIndex))
		{
			return INDEX_NONE;
		}

		int32 Shift = 0;
		for (const int32 RemovedIndex : Removed)
		{
			Shift += RemovedIndex < LayerIndex ? 1 : 0;
		}
		return LayerIndex - Shift;
	};

	if (Selection.Layer == INDEX_NONE)
	{
		return;
	}

	TArray<int32> Remaining;
	for (const int32 LayerIndex : SelectedLayers)
	{
		const int32 Remapped = Remap(LayerIndex);
		if (Remapped != INDEX_NONE)
		{
			Remaining.Add(Remapped);
		}
	}

	if (Remaining.Num() == 0)
	{
		ClearSelection();
		return;
	}

	const int32 Primary = Remap(Selection.Layer);
	SelectedKeys.Reset();
	Selection = FSelection();
	Selection.Layer = Primary != INDEX_NONE ? Primary : Remaining.Last();
	SelectedLayers = MoveTemp(Remaining);
	SelectionAnchor = Selection.Layer;
	RefreshDetails();
}

void SWidgetAnimTimeline::SetLayersLocked(const TArray<int32>& LayerIndices, bool bLocked)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current)
	{
		return;
	}

	TArray<int32> ToChange;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Current->IsValidIndex(LayerIndex) && (*Current)[LayerIndex].bLockedInEditor != bLocked)
		{
			ToChange.Add(LayerIndex);
		}
	}

	if (ToChange.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(bLocked
		? LOCTEXT("LockTracksTransaction", "Lock Tracks")
		: LOCTEXT("UnlockTracksTransaction", "Unlock Tracks"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : ToChange)
	{
		Layers[LayerIndex].bLockedInEditor = bLocked;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

UObject* SWidgetAnimTimeline::BeginEditIgnoringReadOnly()
{
	UObject* Edited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
	if (!Edited || !Source->CanEdit() || !Source->GetLayers())
	{
		return nullptr;
	}

	Edited->Modify();
	return Edited;
}

void SWidgetAnimTimeline::SetLayersHidden(const TArray<int32>& LayerIndices, bool bHidden)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current)
	{
		return;
	}

	TArray<int32> ToChange;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Current->IsValidIndex(LayerIndex) && (*Current)[LayerIndex].bHiddenInEditor != bHidden)
		{
			ToChange.Add(LayerIndex);
		}
	}

	if (ToChange.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(bHidden
		? LOCTEXT("HideTracksTransaction", "Hide Tracks")
		: LOCTEXT("ShowTracksTransaction", "Show Tracks"));
	if (!BeginEditIgnoringReadOnly())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : ToChange)
	{
		Layers[LayerIndex].bHiddenInEditor = bHidden;
		// Hidden or shown by hand now: Un-focus leaves it as it is set here.
		Layers[LayerIndex].bHiddenByFocus = false;
	}

	// Told as an edit to the layers, which is what has the previews rebuilt without - or with - them.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

namespace WidgetAnimTrackClipboard
{
	/**
	 * Copied tracks on the clipboard: this header line, then one line per track - its start in authored seconds, its
	 * group, and the track itself as exported text, Base64 so nothing in it can break the line apart.
	 */
	const TCHAR* Header = TEXT("NameTBD.WidgetAnimTracks.v1");

	struct FEntry
	{
		float Start = 0.f;
		int32 Group = INDEX_NONE;
		FWidgetAnimLayer Layer;
	};

	bool Read(TArray<FEntry>& OutEntries)
	{
		OutEntries.Reset();

		FString Clipboard;
		FPlatformApplicationMisc::ClipboardPaste(Clipboard);
		TArray<FString> Lines;
		Clipboard.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 2 || Lines[0].TrimStartAndEnd() != Header)
		{
			return false;
		}

		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			TArray<FString> Fields;
			Lines[LineIndex].ParseIntoArray(Fields, TEXT("\t"));
			FString Exported;
			if (Fields.Num() != 3 || !FBase64::Decode(Fields[2], Exported))
			{
				continue;
			}

			FEntry Entry;
			Entry.Start = FCString::Atof(*Fields[0]);
			Entry.Group = FCString::Atoi(*Fields[1]);
			if (FWidgetAnimLayer::StaticStruct()->ImportText(*Exported, &Entry.Layer, nullptr, PPF_Copy, GWarn, TEXT("WidgetAnimLayer")))
			{
				OutEntries.Add(MoveTemp(Entry));
			}
		}
		return OutEntries.Num() > 0;
	}
}

TArray<int32> SWidgetAnimTimeline::GetClipboardLayers() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	TArray<int32> Result;
	for (const int32 LayerIndex : SelectedLayers)
	{
		if (Layers && Layers->IsValidIndex(LayerIndex))
		{
			Result.AddUnique(LayerIndex);
		}
	}
	if (Result.Num() == 0 && Layers && Layers->IsValidIndex(Selection.Layer))
	{
		Result.Add(Selection.Layer);
	}
	Result.Sort();
	return Result;
}

void SWidgetAnimTimeline::CopyLayers(const TArray<int32>& LayerIndices)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	TArray<int32> Sorted = LayerIndices;
	Sorted.Sort();

	TArray<float> Starts;
	GetAuthoredStarts(Starts);

	FString Clipboard = WidgetAnimTrackClipboard::Header;
	int32 NumCopied = 0;
	for (const int32 LayerIndex : Sorted)
	{
		if (!Layers->IsValidIndex(LayerIndex))
		{
			continue;
		}

		FString Exported;
		FWidgetAnimLayer::StaticStruct()->ExportText(Exported, &(*Layers)[LayerIndex], nullptr, nullptr, PPF_Copy, nullptr);
		Clipboard += FString::Printf(TEXT("\n%f\t%d\t"), Starts.IsValidIndex(LayerIndex) ? Starts[LayerIndex] : 0.f, GetLayerGroup(LayerIndex));
		Clipboard += FBase64::Encode(Exported);
		++NumCopied;
	}

	if (NumCopied > 0)
	{
		FPlatformApplicationMisc::ClipboardCopy(*Clipboard);
	}
}

void SWidgetAnimTimeline::CutLayers(const TArray<int32>& LayerIndices)
{
	if (!CanEdit() || LayerIndices.Num() == 0)
	{
		return;
	}

	CopyLayers(LayerIndices);

	// One undo step for the cut, named for it. Delete Tracks acts on the selection, which is what several tracks cut
	// from a menu or the keyboard are.
	const FScopedTransaction Transaction(LayerIndices.Num() > 1
		? LOCTEXT("CutTracksTransaction", "Cut Tracks")
		: LOCTEXT("CutTrackTransaction", "Cut Track"));
	if (LayerIndices.Num() == 1)
	{
		DeleteLayer(LayerIndices[0]);
	}
	else
	{
		SelectedLayers = LayerIndices;
		DeleteSelectedLayers();
	}
}

bool SWidgetAnimTimeline::CanPasteLayers() const
{
	FString Clipboard;
	FPlatformApplicationMisc::ClipboardPaste(Clipboard);
	return Clipboard.StartsWith(WidgetAnimTrackClipboard::Header);
}

void SWidgetAnimTimeline::PasteLayers(int32 GroupIndex, TOptional<float> AtSeconds)
{
	TArray<WidgetAnimTrackClipboard::FEntry> Entries;
	if (!WidgetAnimTrackClipboard::Read(Entries))
	{
		return;
	}

	const FScopedTransaction Transaction(Entries.Num() > 1
		? LOCTEXT("PasteTracksTransaction", "Paste Tracks")
		: LOCTEXT("PasteTrackTransaction", "Paste Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	if (Groups.Num() == 0)
	{
		Groups.AddDefaulted();
	}

	// Into the group asked for - the one the menu was opened in, or the selection's - unless the tracks came from
	// several groups: then each keeps its own, where this animation has it, so a copied set pasted back reads the same.
	const bool bSupportsGroups = !Source.IsValid() || Source->SupportsLayerGroups();
	int32 TargetGroup = GroupIndex;
	if (!Groups.IsValidIndex(TargetGroup) && Layers.IsValidIndex(Selection.Layer))
	{
		TargetGroup = GetLayerGroup(Selection.Layer);
	}
	TargetGroup = GetDefaultGroup(TargetGroup);

	bool bSeveralGroups = false;
	for (const WidgetAnimTrackClipboard::FEntry& Entry : Entries)
	{
		bSeveralGroups |= Entry.Group != Entries[0].Group;
	}

	// Where the first one lands: the menu's time, else the playhead - in authored seconds, as the Delay is.
	const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
	const float PasteAt = FMath::Max(AtSeconds.IsSet() ? AtSeconds.GetValue() : GetPlayheadSeconds(), 0.f) / TimeScale;
	float FirstStart = TNumericLimits<float>::Max();
	for (const WidgetAnimTrackClipboard::FEntry& Entry : Entries)
	{
		FirstStart = FMath::Min(FirstStart, Entry.Start);
	}

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	const int32 InsertAt = FMath::Clamp(GetInsertIndex(), 0, Layers.Num());

	// Each With the one above it and delayed from its start, so it lands exactly where it was relative to the rest of
	// what was copied - the start is the one thing known exactly; an After Previous delay would hang on lengths.
	float AnchorStart = OldStarts.IsValidIndex(InsertAt - 1) ? OldStarts[InsertAt - 1] : 0.f;
	for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
	{
		FWidgetAnimLayer Layer = Entries[EntryIndex].Layer;
		const float Start = FMath::Max(PasteAt + Entries[EntryIndex].Start - FirstStart, 0.f);

		Layer.Start = EWidgetAnimLayerStart::WithPrevious;
		Layer.Delay = Start - AnchorStart;
		AnchorStart = Start;

		Layer.LayerGroup = bSupportsGroups && bSeveralGroups && Groups.IsValidIndex(Entries[EntryIndex].Group) ? Entries[EntryIndex].Group : TargetGroup;
		if (!bSupportsGroups)
		{
			Layer.LayerGroup = 0;
		}

		// Pasted to be worked on: on lines of their own, unlocked and shown.
		Layer.EditorLine = INDEX_NONE;
		Layer.bLockedInEditor = false;
		Layer.bHiddenInEditor = false;
		Layer.bHiddenByFocus = false;

		Layers.Insert(Layer, InsertAt + EntryIndex);
		TrackCollapsedGroups.Remove(Layer.LayerGroup);
		ListCollapsedGroups.Remove(Layer.LayerGroup);
	}

	PinLayerStarts(OldStarts, OldIndicesAfterInserting(Layers.Num(), InsertAt, Entries.Num()));

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();

	// What was pasted is what is selected, to move or edit straight away.
	SelectLayer(InsertAt);
	if (Entries.Num() > 1)
	{
		SelectedKeys.Reset();
		SelectedLayers.Reset();
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			SelectedLayers.Add(InsertAt + EntryIndex);
		}
		RefreshDetails();
	}
}

bool SWidgetAnimTimeline::IsFocusActive() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->ContainsByPredicate([](const FWidgetAnimLayer& Layer) { return Layer.bHiddenByFocus; });
}

void SWidgetAnimTimeline::FocusLayers(const TArray<int32>& LayerIndices)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || LayerIndices.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LayerIndices.Num() > 1
		? LOCTEXT("FocusTracksTransaction", "Focus Tracks")
		: LOCTEXT("FocusTrackTransaction", "Focus Track"));
	if (!BeginEditIgnoringReadOnly())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
	{
		FWidgetAnimLayer& Layer = Layers[LayerIndex];

		// A focus already on is lifted first, so focusing again starts from the tracks as they were set by hand.
		if (Layer.bHiddenByFocus)
		{
			Layer.bHiddenInEditor = false;
			Layer.bHiddenByFocus = false;
		}

		if (LayerIndices.Contains(LayerIndex))
		{
			// The focused tracks are the ones shown - even one that was hidden by hand.
			Layer.bHiddenInEditor = false;
		}
		else if (!Layer.bHiddenInEditor)
		{
			// Only those hidden here are marked, so Un-focus shows them and leaves the ones hidden by hand hidden.
			Layer.bHiddenInEditor = true;
			Layer.bHiddenByFocus = true;
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

void SWidgetAnimTimeline::UnfocusLayers()
{
	if (!IsFocusActive())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("UnfocusTracksTransaction", "Un-focus Tracks"));
	if (!BeginEditIgnoringReadOnly())
	{
		return;
	}

	for (FWidgetAnimLayer& Layer : *Source->GetLayers())
	{
		if (Layer.bHiddenByFocus)
		{
			Layer.bHiddenInEditor = false;
			Layer.bHiddenByFocus = false;
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

void SWidgetAnimTimeline::MoveLayerTo(int32 LayerIndex, int32 InsertBefore)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
	{
		return;
	}

	InsertBefore = FMath::Clamp(InsertBefore, 0, Current->Num());
	if (InsertBefore == LayerIndex || InsertBefore == LayerIndex + 1)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MoveLayerTransaction", "Move Animation Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);

	TArray<int32> OldIndexOf;
	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		OldIndexOf.Add(Index);
	}

	const FWidgetAnimLayer Moved = Layers[LayerIndex];
	Layers.RemoveAt(LayerIndex);
	OldIndexOf.RemoveAt(LayerIndex);

	// Everything after the removed slot shifted up one. Its place in the sequence changes; its place in time,
	// and every other track's, doesn't.
	const int32 NewIndex = InsertBefore > LayerIndex ? InsertBefore - 1 : InsertBefore;
	Layers.Insert(Moved, NewIndex);
	OldIndexOf.Insert(LayerIndex, NewIndex);
	PinLayerStarts(OldStarts, OldIndexOf);

	NotifyGroupsChanged(EPropertyChangeType::ArrayMove);
	RebuildRows();
	SelectLayer(NewIndex);
}

void SWidgetAnimTimeline::SetLayerGroup(int32 LayerIndex, int32 GroupIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Current || !Groups || !Current->IsValidIndex(LayerIndex) || !Groups->IsValidIndex(GroupIndex) || GetLayerGroup(LayerIndex) == GroupIndex || !CanEditLayer(LayerIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetLayerGroupTransaction", "Move Track to Group"));
	if (!BeginEdit())
	{
		return;
	}

	// Its line was a line of its old group's; in the new one it starts on a line of its own, at the bottom.
	FWidgetAnimLayer& Moved = (*Source->GetLayers())[LayerIndex];
	Moved.LayerGroup = GroupIndex;
	Moved.EditorLine = INDEX_NONE;
	TrackCollapsedGroups.Remove(GroupIndex);
	ListCollapsedGroups.Remove(GroupIndex);

	// No refresh here: this can come from inside the Selection panel's own dropdown, and rebuilding that
	// panel under it would pull the dropdown out from under its own callback. The group is part of the
	// layout signature, so the rows and the panel catch up next frame.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

void SWidgetAnimTimeline::SetLayerTarget(int32 LayerIndex, FName Target)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex) || (*Current)[LayerIndex].TargetWidget == Target)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetLayerTargetTransaction", "Set Track Target Widget"));
	if (!BeginEdit())
	{
		return;
	}

	(*Source->GetLayers())[LayerIndex].TargetWidget = Target;

	// Not RefreshDetails: this is called from the Selection panel's own dropdown, which already shows the new value.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

FText SWidgetAnimTimeline::GetLayerWhereName(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (Source.IsValid() && Source->SupportsLayerTargets() && Layers && Layers->IsValidIndex(LayerIndex))
	{
		const FName Target = (*Layers)[LayerIndex].TargetWidget;
		return Target.IsNone() ? LOCTEXT("ThisWidgetGroup", "This Widget") : FText::FromName(Target);
	}
	return GetGroupName(GetLayerGroup(LayerIndex));
}

void SWidgetAnimTimeline::InsertAnimation(const UWidgetAnimPresetAsset* Animation, int32 GroupIndex)
{
	if (!Animation || Animation->Layers.Num() == 0 || Animation == Source->GetEditedObject())
	{
		return;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("InsertAnimationTransaction", "Insert {0}"), FText::FromString(Animation->GetName())));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();

	// Where the inserted animation's groups land. One group - a single-widget animation - joins the group it
	// was inserted into. Several come along as groups of their own, named after the animation, so the
	// widgets it moves stay distinct.
	//
	// A plain list of tracks has one widget, so every group of the inserted animation joins it.
	TArray<int32> GroupMap;
	const int32 SourceGroups = FMath::Max(Animation->LayerGroups.Num(), 1);
	const bool bMergeGroups = SourceGroups == 1 || !Source->SupportsLayerGroups();
	if (bMergeGroups)
	{
		if (Groups.Num() == 0)
		{
			Groups.AddDefaulted();
		}
		GroupMap.Init(GetDefaultGroup(GroupIndex), SourceGroups);
	}
	else
	{
		for (int32 SourceIndex = 0; SourceIndex < SourceGroups; ++SourceIndex)
		{
			FWidgetAnimLayerGroup NewGroup = Animation->LayerGroups[SourceIndex];
			NewGroup.Layers.Reset();
			NewGroup.GroupName = FString::Printf(TEXT("%s %s"), *Animation->GetName(), *Animation->LayerGroups[SourceIndex].GetDisplayName(SourceIndex));
			GroupMap.Add(Groups.Add(NewGroup));
		}
	}

	// In the sequence exactly as they were in their own - the first following the layer they were inserted
	// after, per its own Starts - so the inserted animation plays as it always did, from that point on.
	//
	// Their lines come along too: into new groups as they were, into an existing group below the lines it has.
	const int32 InsertAt = FMath::Clamp(GetInsertIndex(), 0, Layers.Num());
	const int32 LineOffset = bMergeGroups ? GetGroupLineCount(GroupMap[0]) : 0;
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	for (int32 Index = 0; Index < Animation->Layers.Num(); ++Index)
	{
		FWidgetAnimLayer Copy = Animation->Layers[Index];
		const int32 SourceGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Copy, SourceGroups);
		Copy.LayerGroup = GroupMap.IsValidIndex(SourceGroup) ? GroupMap[SourceGroup] : GroupMap[0];
		Copy.EditorLine = Copy.EditorLine >= 0 ? Copy.EditorLine + LineOffset : INDEX_NONE;

		// Which animation it came in from, so the list can say so.
		Copy.InsertedFrom = Animation->GetName();
		Layers.Insert(Copy, InsertAt + Index);
	}

	// What was already there stays where it is in time; the inserted tracks follow on from where they went in.
	PinLayerStarts(OldStarts, OldIndicesAfterInserting(Layers.Num(), InsertAt, Animation->Layers.Num()));

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();
	SelectLayer(InsertAt);
}

void SWidgetAnimTimeline::AddKey(int32 LayerIndex, float LocalSeconds)
{
	if (!CanEditKeys(LayerIndex))
	{
		return;
	}

	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];

	if (Layer.AnimType != EWidgetAnimType::Transform)
	{
		// A keyed track's keys sit at fractions of its run, so they keep their places when it is resized.
		// LocalSeconds is played time, as the timeline shows it.
		const float RunSeconds = GetShownRunLength(LayerIndex) * GetTimeScale();
		if (RunSeconds <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		const float Fraction = FMath::Clamp(LocalSeconds / RunSeconds, 0.f, 1.f);
		FWidgetAnimValueKeys& ValueKeys = Layer.ValueKeys;
		for (int32 Point = 0; Point < ValueKeys.NumPoints(); ++Point)
		{
			if (FMath::Abs(ValueKeys.GetPointTime(Point) - Fraction) * RunSeconds < WidgetAnimTimeline::MinKeySpacing)
			{
				return;
			}
		}

		const FScopedTransaction Transaction(LOCTEXT("AddValueKeyTransaction", "Add Key"));
		if (!BeginEdit())
		{
			return;
		}

		// Which segment it splits: from point Insert to point Insert + 1. It becomes point Insert + 1 itself.
		int32 Insert = 0;
		while (Insert < ValueKeys.Keys.Num() && ValueKeys.Keys[Insert].Time < Fraction)
		{
			++Insert;
		}

		// At the value the track already has there, and with the curve of the segment it splits - each half then
		// runs that curve on its own.
		const float From = ValueKeys.GetPointTime(Insert);
		const float To = ValueKeys.GetPointTime(Insert + 1);
		const float Across = To - From > KINDA_SMALL_NUMBER ? (Fraction - From) / (To - From) : 0.f;

		// A tween is sampled where it really is at that moment. A spring's keys are points along its travel rather
		// than moments, so there the key goes on the straight line between its neighbors, which leaves the path
		// the spring travels exactly as it was.
		int32 SampleSegment = Insert;
		float ValueAlpha = Across;
		if (!Layer.IsSpringTimed())
		{
			// Sampled without an overshoot: the first key switches it off (see EvaluateTrackEase), so a key placed on
			// the overshoot's path would sit somewhere the track no longer goes.
			FWidgetAnimSettings TrackSettings = Layer.ToSettings();
			TrackSettings.Overshoot = 0.f;
			TrackSettings.SlideOvershoot = 0.f;
			UWidgetAnimLibrary::SampleValueKeys(TrackSettings, Fraction, /*bTowardStart=*/false, SampleSegment, ValueAlpha);
		}

		FWidgetAnimValueKey NewKey;
		NewKey.Time = Fraction;
		NewKey.Value = FMath::Lerp(WidgetAnimKeyPoints::GetValue(Layer, SampleSegment), WidgetAnimKeyPoints::GetValue(Layer, SampleSegment + 1), ValueAlpha);
		NewKey.Color = FMath::Lerp(WidgetAnimKeyPoints::GetColor(Layer, SampleSegment), WidgetAnimKeyPoints::GetColor(Layer, SampleSegment + 1), ValueAlpha);
		NewKey.CurveToNext = ValueKeys.GetSegmentCurve(Insert);
		ValueKeys.Keys.Insert(NewKey, Insert);

		NotifyGroupsChanged(EPropertyChangeType::ValueSet);
		SelectKey(LayerIndex, Insert + 1);
		return;
	}

	FRichCurve* Curve = Layer.Transform.Curve.GetRichCurve();
	if (!Curve)
	{
		return;
	}

	// LocalSeconds is played time, as the timeline shows it; the curve holds authored time.
	const float Time = FMath::Max(LocalSeconds, 0.f) / FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
	if (Curve->FindKey(Time, WidgetAnimTimeline::MinKeySpacing) != FKeyHandle::Invalid())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddKeyTransaction", "Add Transform Key"));
	if (!BeginEdit())
	{
		return;
	}

	// Placed on the curve where it already is, with the interpolation of the key before it, so adding a key
	// changes neither the curve's shape nor - once the keys sync - the pose at that moment.
	ERichCurveInterpMode InterpMode = RCIM_Linear;
	for (const FRichCurveKey& Existing : Curve->GetConstRefOfKeys())
	{
		if (Existing.Time < Time)
		{
			InterpMode = Existing.InterpMode;
		}
	}

	const float Value = Curve->Eval(Time);
	const FKeyHandle NewKey = Curve->AddKey(Time, Value);
	Curve->SetKeyInterpMode(NewKey, InterpMode);

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);

	// Selected, which also takes the preview there.
	const TArray<FRichCurveKey>& Keys = Curve->GetConstRefOfKeys();
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		if (FMath::IsNearlyEqual(Keys[Index].Time, Time, WidgetAnimTimeline::MinKeySpacing))
		{
			SelectKey(LayerIndex, Index);
			break;
		}
	}
}

void SWidgetAnimTimeline::DeleteKey(int32 LayerIndex, int32 KeyIndex)
{
	if (!CanDeleteKey(LayerIndex, KeyIndex))
	{
		return;
	}

	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];

	if (Layer.AnimType != EWidgetAnimType::Transform)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteValueKeyTransaction", "Delete Key"));
		if (!BeginEdit())
		{
			return;
		}

		// Key 0 is the start value, so a key in between is one further along than its place in Keys.
		Layer.ValueKeys.Keys.RemoveAt(KeyIndex - 1);

		// The last key gone, the track is back to going from its start to its end by its Easing - and a curve
		// left on that one segment would otherwise come back to life with the next key added.
		if (Layer.ValueKeys.Keys.Num() == 0)
		{
			Layer.ValueKeys.StartCurve = FWidgetAnimSegmentCurve();
		}

		NotifyGroupsChanged(EPropertyChangeType::ValueSet);
		SelectLayer(LayerIndex);
		return;
	}

	FRichCurve* Curve = Layer.Transform.Curve.GetRichCurve();
	if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(KeyIndex))
	{
		return;
	}

	const FKeyHandle Handle = Curve->FindKey(Curve->GetConstRefOfKeys()[KeyIndex].Time, KINDA_SMALL_NUMBER);
	if (Handle == FKeyHandle::Invalid())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DeleteKeyTransaction", "Delete Transform Key"));
	if (!BeginEdit())
	{
		return;
	}

	Curve->DeleteKey(Handle);

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	SelectLayer(LayerIndex);
}

void SWidgetAnimTimeline::DeleteSelectedKeys()
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers)
	{
		return;
	}

	// Last first within each layer, so deleting one never moves another still to be deleted.
	TArray<FKeyRef> Doomed;
	for (const FKeyRef& Ref : SelectedKeys)
	{
		if (CanDeleteKey(Ref.Layer, Ref.Key))
		{
			Doomed.Add(Ref);
		}
	}
	if (Doomed.Num() == 0)
	{
		return;
	}
	Doomed.Sort([](const FKeyRef& A, const FKeyRef& B)
	{
		return A.Layer != B.Layer ? A.Layer < B.Layer : A.Key > B.Key;
	});

	const FScopedTransaction Transaction(LOCTEXT("DeleteKeysTransaction", "Delete Keys"));
	if (!BeginEdit())
	{
		return;
	}

	const int32 PrimaryLayer = Selection.Layer;
	for (const FKeyRef& Ref : Doomed)
	{
		FWidgetAnimLayer& Layer = (*Layers)[Ref.Layer];
		if (Layer.AnimType != EWidgetAnimType::Transform)
		{
			if (Layer.ValueKeys.Keys.IsValidIndex(Ref.Key - 1))
			{
				Layer.ValueKeys.Keys.RemoveAt(Ref.Key - 1);
			}
			if (Layer.ValueKeys.Keys.Num() == 0)
			{
				Layer.ValueKeys.StartCurve = FWidgetAnimSegmentCurve();
			}
			continue;
		}

		// A Transform track keeps the two keys it needs, whichever of its selected keys that spares.
		FRichCurve* Curve = Layer.Transform.Curve.GetRichCurve();
		if (Curve && Curve->GetNumKeys() > 2 && Curve->GetConstRefOfKeys().IsValidIndex(Ref.Key))
		{
			const FKeyHandle Handle = Curve->FindKey(Curve->GetConstRefOfKeys()[Ref.Key].Time, KINDA_SMALL_NUMBER);
			if (Handle != FKeyHandle::Invalid())
			{
				Curve->DeleteKey(Handle);
			}
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	SelectLayer(PrimaryLayer);
}

void SWidgetAnimTimeline::SetKeyTime(int32 LayerIndex, int32 KeyIndex, float LocalSeconds)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return;
	}

	FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);

	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		FRichCurve* Curve = Layer.Transform.Curve.GetRichCurve();
		if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(KeyIndex))
		{
			return;
		}

		const float CurrentTime = Curve->GetConstRefOfKeys()[KeyIndex].Time;
		const float NewCurveTime = LocalSeconds / TimeScale;
		const FKeyHandle Handle = Curve->FindKey(CurrentTime, KINDA_SMALL_NUMBER);
		if (Handle != FKeyHandle::Invalid() && !FMath::IsNearlyEqual(CurrentTime, NewCurveTime))
		{
			Curve->SetKeyTime(Handle, NewCurveTime);
		}
		return;
	}

	// A key between a keyed track's start and end: its fraction of the run.
	const float RunSeconds = GetShownRunLength(LayerIndex) * TimeScale;
	if (Layer.ValueKeys.Keys.IsValidIndex(KeyIndex - 1) && RunSeconds > KINDA_SMALL_NUMBER)
	{
		const float Fraction = FMath::Clamp(LocalSeconds / RunSeconds, 0.f, 1.f);
		if (!FMath::IsNearlyEqual(Layer.ValueKeys.Keys[KeyIndex - 1].Time, Fraction, 1.e-6f))
		{
			Layer.ValueKeys.Keys[KeyIndex - 1].Time = Fraction;
		}
	}
}

namespace
{
	/**
	 * The nearest pairing of an edge with a snap target, if any is within Threshold: how far the edges would
	 * have to move to land on it, and where it is. Edges are where the dragged things would be right now.
	 */
	bool FindSnap(const TArray<float>& Targets, const TArray<float>& Edges, float Threshold, float& OutAdjust, float& OutTarget)
	{
		bool bFound = false;
		float BestDistance = Threshold;

		for (const float Edge : Edges)
		{
			for (const float Target : Targets)
			{
				const float Distance = FMath::Abs(Target - Edge);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					OutAdjust = Target - Edge;
					OutTarget = Target;
					bFound = true;
				}
			}
		}

		return bFound;
	}

	/** Seconds rounded to the millisecond, so values dragged by hand read cleanly in the Selection panel. */
	float RoundSeconds(float Seconds)
	{
		return FMath::RoundToFloat(Seconds * 1000.f) / 1000.f;
	}
}

void SWidgetAnimTimeline::GatherSnapTargets(const TSet<int32>& Excluded, bool bIncludeKeys, TArray<float>& OutTargets) const
{
	OutTargets.Reset();

	// The animation's own start and end, and the playhead.
	OutTargets.Add(0.f);
	OutTargets.Add(GetPassSeconds());
	OutTargets.Add(GetPlayheadSeconds());

	// The animation the list plays within: where each of its tracks starts and ends.
	for (const FVector2f& Span : ContextSpans)
	{
		OutTargets.Add(Span.X);
		OutTargets.Add(Span.X + Span.Y);
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);

	for (int32 LayerIndex = 0; LayerIndex < Spans.Num(); ++LayerIndex)
	{
		if (Excluded.Contains(LayerIndex))
		{
			continue;
		}

		OutTargets.Add(Spans[LayerIndex].X);
		OutTargets.Add(Spans[LayerIndex].X + Spans[LayerIndex].Y);

		if (bIncludeKeys)
		{
			TArray<float> KeyTimes;
			GetKeyTimes(LayerIndex, KeyTimes);
			for (const float KeyTime : KeyTimes)
			{
				OutTargets.Add(Spans[LayerIndex].X + KeyTime);
			}
		}
	}
}

TArray<int32> SWidgetAnimTimeline::GetActedOnLayers(int32 LayerIndex) const
{
	if (IsMultiSelection() && IsLayerSelected(LayerIndex))
	{
		return SelectedLayers;
	}
	return { LayerIndex };
}

void SWidgetAnimTimeline::BeginKeyDrag(int32 LayerIndex, int32 KeyIndex)
{
	if (!CanEditKeys(LayerIndex) || IsDragging())
	{
		return;
	}

	TArray<float> PressedTimes;
	GetKeyTimes(LayerIndex, PressedTimes);
	if (!PressedTimes.IsValidIndex(KeyIndex))
	{
		return;
	}

	// What moves: every selected key that can, when the one pressed is among several selected - keys of any
	// tracks, all by the same amount - else just the one pressed. A track's start or end key can be pressed to
	// drag the others along, though it stays where it is itself.
	TArray<FKeyRef> Moving;
	if (IsKeyMultiSelection() && IsKeySelected(LayerIndex, KeyIndex))
	{
		// The pressed one first, when it moves itself.
		if (CanMoveKey(LayerIndex, KeyIndex))
		{
			Moving.Add(FKeyRef{ LayerIndex, KeyIndex });
		}
		for (const FKeyRef& Ref : SelectedKeys)
		{
			if (CanMoveKey(Ref.Layer, Ref.Key))
			{
				Moving.AddUnique(Ref);
			}
		}
	}
	else if (CanMoveKey(LayerIndex, KeyIndex))
	{
		Moving.Add(FKeyRef{ LayerIndex, KeyIndex });
	}

	if (Moving.Num() == 0)
	{
		return;
	}

	DragLayer = LayerIndex;
	DragKey = KeyIndex;
	DragPressedOriginalTime = PressedTimes[KeyIndex];
	DragKeys = Moving;
	DragKeyOriginalTimes.Reset();

	TSet<int32> MovingLayers;
	for (const FKeyRef& Ref : DragKeys)
	{
		TArray<float> KeyTimes;
		GetKeyTimes(Ref.Layer, KeyTimes);
		DragKeyOriginalTimes.Add(KeyTimes.IsValidIndex(Ref.Key) ? KeyTimes[Ref.Key] : 0.f);
		MovingLayers.Add(Ref.Layer);
	}

	// Where the layer starts, which a key drag never changes, and what the key can snap to - worked out now,
	// before anything moves, so nothing the drag itself shifts is snapped to. The layers whose keys move are
	// left out: their other keys are what the moving ones are being moved between.
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	KeyDragLayerStart = Spans.IsValidIndex(LayerIndex) ? Spans[LayerIndex].X : 0.f;
	MovingLayers.Add(LayerIndex);
	GatherSnapTargets(MovingLayers, /*bIncludeKeys=*/true, KeySnapTargets);
	SnapIndicator.Reset();

	// One undo step for the whole drag, however many frames it moves through.
	KeyDragTransaction = MakeUnique<FScopedTransaction>(DragKeys.Num() > 1
		? LOCTEXT("MoveKeysTransaction", "Move Keys")
		: (*GetLayers())[LayerIndex].AnimType == EWidgetAnimType::Transform
			? LOCTEXT("MoveKeyTransaction", "Move Transform Key")
			: LOCTEXT("MoveValueKeyTransaction", "Move Key"));
	BeginEdit();
}

void SWidgetAnimTimeline::UpdateKeyDrag(float LocalSeconds, float SnapThresholdSeconds)
{
	if (!IsDraggingKey() || DragKeys.Num() == 0 || DragKeys.Num() != DragKeyOriginalTimes.Num())
	{
		return;
	}

	// Snapped first, then kept between neighbors - a snap that would pass one is not taken. All in played
	// seconds, as the timeline shows them; each layer's own stored time is worked out when it is written.
	float Wanted = RoundSeconds(LocalSeconds);
	TOptional<float> SnappedTo;
	float Adjust = 0.f;
	float Target = 0.f;
	if (SnapThresholdSeconds > 0.f && FindSnap(KeySnapTargets, { KeyDragLayerStart + LocalSeconds }, SnapThresholdSeconds, Adjust, Target))
	{
		Wanted = LocalSeconds + Adjust;
		SnappedTo = Target;
	}

	// Every key moves by the same amount, held so none passes a neighbor that isn't moving with it. Keys are
	// kept in their order: a Transform curve re-sorts its keys by time, and the pose values sit alongside them by
	// position - a key dragged past its neighbor would carry the neighbor's pose, not its own.
	constexpr float MaxSeconds = 600.f;
	float Lowest = -MaxSeconds;
	float Highest = MaxSeconds;

	for (int32 Index = 0; Index < DragKeys.Num(); ++Index)
	{
		const FKeyRef& Ref = DragKeys[Index];
		const float Original = DragKeyOriginalTimes[Index];

		TArray<float> KeyTimes;
		GetKeyTimes(Ref.Layer, KeyTimes);
		if (!KeyTimes.IsValidIndex(Ref.Key))
		{
			continue;
		}

		if (Ref.Key == 0)
		{
			Lowest = FMath::Max(Lowest, -Original);
		}
		else if (!DragKeys.Contains(FKeyRef{ Ref.Layer, Ref.Key - 1 }))
		{
			Lowest = FMath::Max(Lowest, KeyTimes[Ref.Key - 1] + WidgetAnimTimeline::MinKeySpacing - Original);
		}

		if (Ref.Key + 1 < KeyTimes.Num() && !DragKeys.Contains(FKeyRef{ Ref.Layer, Ref.Key + 1 }))
		{
			Highest = FMath::Min(Highest, KeyTimes[Ref.Key + 1] - WidgetAnimTimeline::MinKeySpacing - Original);
		}
	}

	const float WantedDelta = Wanted - DragPressedOriginalTime;
	const float Delta = Lowest <= Highest ? FMath::Clamp(WantedDelta, Lowest, Highest) : 0.f;

	SnapIndicator = SnappedTo.IsSet() && FMath::IsNearlyEqual(Delta, WantedDelta, WidgetAnimTimeline::MinKeySpacing) ? SnappedTo : TOptional<float>();

	// Written from the far end of the move first: a Transform curve finds a key by its time, so a key must never be
	// set onto the time of one that has yet to move.
	TArray<float> CurrentTimes;
	GetKeyTimes(DragKeys[0].Layer, CurrentTimes);
	const float CurrentDelta = CurrentTimes.IsValidIndex(DragKeys[0].Key) ? CurrentTimes[DragKeys[0].Key] - DragKeyOriginalTimes[0] : 0.f;
	if (FMath::IsNearlyEqual(CurrentDelta, Delta, 1.e-6f))
	{
		return;
	}
	const bool bMovingLater = Delta > CurrentDelta;

	TArray<int32> Order;
	for (int32 Index = 0; Index < DragKeys.Num(); ++Index)
	{
		Order.Add(Index);
	}
	Order.Sort([this, bMovingLater](int32 A, int32 B)
	{
		return bMovingLater ? DragKeys[A].Key > DragKeys[B].Key : DragKeys[A].Key < DragKeys[B].Key;
	});

	for (const int32 Index : Order)
	{
		SetKeyTime(DragKeys[Index].Layer, DragKeys[Index].Key, DragKeyOriginalTimes[Index] + Delta);
	}

	// Interactive, as a Details panel drag is: the preview follows live and keeps its place - on the key
	// pressed, so what it looks like at its new moment is on screen.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
	FocusPreviewOnKey(DragLayer, DragKey);

	// Moving a Transform track's last key changes how long it runs, which can change what fits on its line.
	UpdateTrackLayout();
}

void SWidgetAnimTimeline::EndKeyDrag()
{
	if (!IsDraggingKey())
	{
		return;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	KeyDragTransaction.Reset();

	DragLayer = INDEX_NONE;
	DragKey = INDEX_NONE;
	DragKeys.Reset();
	DragKeyOriginalTimes.Reset();
	SnapIndicator.Reset();

	// The key's Time row changed; the Selection panel shows it fresh.
	RefreshDetails();
}

bool SWidgetAnimTimeline::BeginLayerDrag(int32 PressedLayer, ELayerDrag Mode)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!CanEdit() || IsDragging() || !Layers || !Layers->IsValidIndex(PressedLayer))
	{
		return false;
	}

	FLayerDragState State;
	State.Mode = Mode;

	// Every layer as it is now: what the drag measures from on every frame, so it never compounds.
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	const int32 NumLayers = Spans.Num();
	State.OriginalStarts.SetNum(NumLayers);
	State.OriginalLengths.SetNum(NumLayers);
	State.OriginalKeyTimes.SetNum(NumLayers);
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		State.OriginalStarts[LayerIndex] = Spans[LayerIndex].X;
		State.OriginalLengths[LayerIndex] = Spans[LayerIndex].Y;
		GetKeyTimes(LayerIndex, State.OriginalKeyTimes[LayerIndex]);
	}
	State.OriginalEnd = GetPassSeconds();

	// What the drag takes: the whole selection if the pressed layer is part of it, less anything locked or
	// that can't be resized from that edge - a track starting at 0 keeps its start where it is.
	for (const int32 LayerIndex : GetActedOnLayers(PressedLayer))
	{
		switch (Mode)
		{
		case ELayerDrag::Move:
			if (CanMoveLayer(LayerIndex))
			{
				State.StartEdges.Add(LayerIndex);
				State.EndEdges.Add(LayerIndex);
			}
			break;

		case ELayerDrag::ResizeStart:
			if (CanResizeLayer(LayerIndex, /*bStartEdge=*/true))
			{
				State.StartEdges.Add(LayerIndex);
			}
			break;

		case ELayerDrag::ResizeEnd:
			if (CanResizeLayer(LayerIndex, /*bStartEdge=*/false))
			{
				State.EndEdges.Add(LayerIndex);
			}
			break;
		}
	}

	// The one grabbed has to be among them, or the drag would move everything but what is under the mouse.
	if (!State.StartEdges.Contains(PressedLayer) && !State.EndEdges.Contains(PressedLayer))
	{
		return false;
	}

	// A track that ends where the animation ends can be pulled past it, making the animation longer. Any other
	// resize - and every move - stays inside the animation as it is.
	if (Mode == ELayerDrag::ResizeEnd)
	{
		for (const int32 LayerIndex : State.EndEdges)
		{
			const float End = State.OriginalStarts[LayerIndex] + State.OriginalLengths[LayerIndex];
			State.bEndIsFree |= FMath::IsNearlyEqual(End, State.OriginalEnd, WidgetAnimTimeline::EdgeAlignTolerance);
		}
	}

	// Linked Edges: tracks below each resized one whose start or end lines up with the edge being dragged
	// come along - that edge of theirs moves, the other stays.
	if (Mode != ELayerDrag::Move && UWidgetAnimEditorSettings::Get().bLinkedEdges)
	{
		const bool bStartDrag = Mode == ELayerDrag::ResizeStart;
		const TArray<int32> Driving = bStartDrag ? State.StartEdges : State.EndEdges;

		for (const int32 DrivingIndex : Driving)
		{
			const float Edge = bStartDrag
				? State.OriginalStarts[DrivingIndex]
				: State.OriginalStarts[DrivingIndex] + State.OriginalLengths[DrivingIndex];

			for (int32 Below = DrivingIndex + 1; Below < NumLayers; ++Below)
			{
				if (Driving.Contains(Below))
				{
					continue;
				}

				const float BelowStart = State.OriginalStarts[Below];
				const float BelowEnd = BelowStart + State.OriginalLengths[Below];

				if (FMath::IsNearlyEqual(BelowStart, Edge, WidgetAnimTimeline::EdgeAlignTolerance) && CanResizeLayer(Below, /*bStartEdge=*/true))
				{
					State.StartEdges.AddUnique(Below);
				}
				if (FMath::IsNearlyEqual(BelowEnd, Edge, WidgetAnimTimeline::EdgeAlignTolerance) && CanResizeLayer(Below, /*bStartEdge=*/false))
				{
					State.EndEdges.AddUnique(Below);
				}
			}
		}
	}

	// Snap targets: everything the drag does not move, worked out before anything does.
	TSet<int32> Moving(State.StartEdges);
	Moving.Append(State.EndEdges);
	GatherSnapTargets(Moving, /*bIncludeKeys=*/false, State.SnapTargets);

	// Every layer's line, as stored and as shown, and how many lines each group has: what a move up or down
	// measures from. Worked out now, while nothing is being dragged.
	UpdateTrackLayout();
	State.OriginalLineOrdinals = LayerLineOrdinals;
	State.OriginalGroupLineCounts = GroupLineCounts;
	State.OriginalEditorLines.SetNum(NumLayers);
	for (int32 LayerIndex = 0; LayerIndex < NumLayers && Layers->IsValidIndex(LayerIndex); ++LayerIndex)
	{
		State.OriginalEditorLines[LayerIndex] = (*Layers)[LayerIndex].EditorLine;
	}

	LayerDrag = MoveTemp(State);
	SnapIndicator.Reset();

	// One undo step for the whole drag, however many frames it moves through.
	LayerDragTransaction = MakeUnique<FScopedTransaction>(Mode == ELayerDrag::Move
		? LOCTEXT("MoveTracksTransaction", "Move Tracks")
		: LOCTEXT("ResizeTracksTransaction", "Resize Tracks"));
	BeginEdit();
	bLayerDragChanged = false;
	return true;
}

void SWidgetAnimTimeline::UpdateLayerDrag(float DeltaSeconds, int32 DeltaLines, float SnapThresholdSeconds)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!IsDraggingLayers() || !CanEdit() || !Layers || Layers->Num() != LayerDrag.OriginalStarts.Num())
	{
		return;
	}

	const FLayerDragState& State = LayerDrag;
	constexpr float MaxSeconds = 600.f;

	// How far the edges may go, from the press: every edge moved by the same amount, so the limits are simply
	// the tightest any one of them sets.
	float Lowest = -MaxSeconds;
	float Highest = MaxSeconds;

	TSet<int32> Moving(State.StartEdges);
	Moving.Append(State.EndEdges);

	for (const int32 LayerIndex : Moving)
	{
		const bool bStart = State.StartEdges.Contains(LayerIndex);
		const bool bEnd = State.EndEdges.Contains(LayerIndex);
		const float Start = State.OriginalStarts[LayerIndex];
		const float Length = State.OriginalLengths[LayerIndex];
		const float End = Start + Length;

		// Nothing before the animation starts.
		if (bStart)
		{
			Lowest = FMath::Max(Lowest, -Start);
		}

		// Nothing shorter than the shortest a layer can be.
		if (bStart && !bEnd)
		{
			Highest = FMath::Min(Highest, Length - MinLayerSeconds);
		}
		if (bEnd && !bStart)
		{
			Lowest = FMath::Max(Lowest, MinLayerSeconds - Length);
		}

		// Nothing past the animation's end, unless a track ending there is being lengthened.
		if (bEnd && (State.Mode == ELayerDrag::Move || !State.bEndIsFree))
		{
			Highest = FMath::Min(Highest, State.OriginalEnd - End);
		}
	}

	float Delta = RoundSeconds(DeltaSeconds);
	SnapIndicator.Reset();

	// Snapping: whichever moved edge is nearest something it could land on, within the snap distance.
	if (SnapThresholdSeconds > 0.f)
	{
		TArray<float> Edges;
		for (const int32 LayerIndex : Moving)
		{
			if (State.StartEdges.Contains(LayerIndex))
			{
				Edges.Add(State.OriginalStarts[LayerIndex] + DeltaSeconds);
			}
			if (State.EndEdges.Contains(LayerIndex))
			{
				Edges.Add(State.OriginalStarts[LayerIndex] + State.OriginalLengths[LayerIndex] + DeltaSeconds);
			}
		}

		float Adjust = 0.f;
		float Target = 0.f;
		if (FindSnap(State.SnapTargets, Edges, SnapThresholdSeconds, Adjust, Target))
		{
			const float Snapped = DeltaSeconds + Adjust;
			if (Snapped >= Lowest - KINDA_SMALL_NUMBER && Snapped <= Highest + KINDA_SMALL_NUMBER)
			{
				Delta = Snapped;
				SnapIndicator = Target;
			}
		}
	}

	// Limits that contradict each other - a selection pinned at both ends - mean nothing moves.
	Delta = Lowest <= Highest ? FMath::Clamp(Delta, Lowest, Highest) : 0.f;

	TArray<float> NewStarts = State.OriginalStarts;
	TArray<float> NewLengths = State.OriginalLengths;
	for (const int32 LayerIndex : Moving)
	{
		const bool bStart = State.StartEdges.Contains(LayerIndex);
		const bool bEnd = State.EndEdges.Contains(LayerIndex);

		if (bStart)
		{
			NewStarts[LayerIndex] += Delta;
		}
		if (bStart && !bEnd)
		{
			NewLengths[LayerIndex] -= Delta;
		}
		if (bEnd && !bStart)
		{
			NewLengths[LayerIndex] += Delta;
		}
	}

	ApplyLayerSpans(NewStarts, NewLengths);
	bLayerDragChanged |= !FMath::IsNearlyZero(Delta);

	// Up and down: a move carries its tracks that many lines, each within its own group - to a line below the
	// group's last makes a new one. Every other layer keeps the line it is shown on. Where a moved track would
	// overlap one already on its new line, the Track Editor shows it on a line of its own just below; letting
	// go keeps it there. See UpdateTrackLayout and CommitTrackLines.
	if (State.Mode == ELayerDrag::Move)
	{
		TArray<FWidgetAnimLayer>& MutableLayers = *Source->GetLayers();
		for (int32 LayerIndex = 0; LayerIndex < MutableLayers.Num() && State.OriginalEditorLines.IsValidIndex(LayerIndex); ++LayerIndex)
		{
			int32 NewLine = State.OriginalEditorLines[LayerIndex];

			if (DeltaLines != 0 && State.OriginalLineOrdinals.IsValidIndex(LayerIndex))
			{
				const int32 Ordinal = State.OriginalLineOrdinals[LayerIndex];
				NewLine = Ordinal;

				if (State.StartEdges.Contains(LayerIndex))
				{
					const int32 GroupIndex = GetLayerGroup(LayerIndex);
					const int32 LineCount = State.OriginalGroupLineCounts.IsValidIndex(GroupIndex) ? State.OriginalGroupLineCounts[GroupIndex] : 0;
					NewLine = FMath::Clamp(Ordinal + DeltaLines, 0, LineCount);
					bLayerDragChanged |= NewLine != Ordinal;
				}
			}

			if (MutableLayers[LayerIndex].EditorLine != NewLine)
			{
				MutableLayers[LayerIndex].EditorLine = NewLine;
			}
		}
	}

	UpdateTrackLayout();

	// Interactive, as a Details panel drag is: the preview follows every frame of it.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
}

void SWidgetAnimTimeline::ApplyLayerSpans(const TArray<float>& NewStarts, const TArray<float>& NewLengths)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers || Layers->Num() != NewStarts.Num() || Layers->Num() != NewLengths.Num())
	{
		return;
	}

	// Where the layer above now starts and ends: what each layer's Starts measures from. Every Delay is
	// rewritten against it, so each layer lands exactly where it is wanted - the dragged ones where they were
	// dragged, every other one where it already was - whatever its Starts says. A Delay that comes out
	// negative is an overlap, which is fine: see FWidgetAnimLayer::Delay.
	//
	// NewStarts and NewLengths are played seconds, as the timeline shows them; what is written is authored
	// seconds, the animation's Speed Multiplier taken back out.
	const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
	float PreviousStart = 0.f;
	float PreviousEnd = 0.f;

	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const float PlayedLength = FMath::Max(NewLengths[LayerIndex], MinLayerSeconds);
		const float Start = FMath::Max(NewStarts[LayerIndex], 0.f) / TimeScale;
		const float Length = PlayedLength / TimeScale;

		if (Layer.AnimType == EWidgetAnimType::Transform)
		{
			// A Transform layer is as long as its last key, so its keys are stretched to the new length - from
			// where they were at the press, so a drag back and forth never drifts. A shared Curve asset is not
			// this layer's to stretch; CanResizeLayer keeps those out of resizes.
			FRichCurve* Curve = Layer.Transform.Curve.ExternalCurve == nullptr ? Layer.Transform.Curve.GetRichCurve() : nullptr;
			const TArray<float>* OriginalTimes = LayerDrag.OriginalKeyTimes.IsValidIndex(LayerIndex) ? &LayerDrag.OriginalKeyTimes[LayerIndex] : nullptr;
			const float OriginalLength = LayerDrag.OriginalLengths.IsValidIndex(LayerIndex) ? LayerDrag.OriginalLengths[LayerIndex] : 0.f;

			if (Curve && OriginalTimes && OriginalTimes->Num() == Curve->Keys.Num() && OriginalLength > KINDA_SMALL_NUMBER)
			{
				// Original times and length are played seconds; the keys hold authored ones.
				const float Scale = PlayedLength / OriginalLength;
				for (int32 Key = 0; Key < Curve->Keys.Num(); ++Key)
				{
					const float Time = (*OriginalTimes)[Key] * Scale / TimeScale;
					if (!FMath::IsNearlyEqual(Curve->Keys[Key].Time, Time, 1.e-5f))
					{
						Curve->Keys[Key].Time = Time;
					}
				}
			}
		}
		else if (!Layer.IsSpringTimed() && !HasDerivedLength(Layer) && !FMath::IsNearlyEqual(Layer.Duration, Length, 1.e-5f))
		{
			// Not on a Spring track, whose length is its spring's: its Duration is only kept for going back to a
			// Tween, and a move must not quietly overwrite it with the settle time.
			Layer.Duration = Length;
		}

		const float Anchor = Layer.Start == EWidgetAnimLayerStart::AfterPrevious ? PreviousEnd : PreviousStart;
		const float Delay = Start - Anchor;
		if (!FMath::IsNearlyEqual(Layer.Delay, Delay, 1.e-5f))
		{
			Layer.Delay = Delay;
		}

		PreviousStart = Start;
		PreviousEnd = Start + GetShownRunLength(LayerIndex) + (TrackFans.IsValidIndex(LayerIndex) ? TrackFans[LayerIndex] : 0.f);
	}
}

void SWidgetAnimTimeline::EndLayerDrag()
{
	if (!IsDraggingLayers())
	{
		return;
	}

	if (bLayerDragChanged)
	{
		// The lines exactly as they are on screen now - worked out while the drag is still on, so a track
		// shown on a line of its own because it overlapped keeps that line rather than trading places.
		UpdateTrackLayout();
		CommitTrackLines();
		NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	}
	else
	{
		// Pressed and let go without the edges actually going anywhere: no undo step for nothing.
		LayerDragTransaction->Cancel();
	}

	LayerDragTransaction.Reset();
	LayerDrag = FLayerDragState();
	SnapIndicator.Reset();
	UpdateTrackLayout();

	// Delay and Duration rows changed; the Selection panel shows them fresh.
	RefreshDetails();
}

bool SWidgetAnimTimeline::IsLayerBeingDragged(int32 LayerIndex) const
{
	return IsDraggingLayers() && (LayerDrag.StartEdges.Contains(LayerIndex) || LayerDrag.EndEdges.Contains(LayerIndex));
}

// -----------------------------------------------------------------------------------------------------
// Menus
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::AddInsertAnimationEntry(FMenuBuilder& Menu, int32 GroupIndex)
{
	Menu.AddSubMenu(
		LOCTEXT("InsertAnimation", "Insert Existing Widget Animation"),
		WidgetAnimTips::InsertAnimationTooltip(),
		FNewMenuDelegate::CreateLambda([this, GroupIndex](FMenuBuilder& SubMenu)
		{
			FAssetPickerConfig Config;
			Config.Filter.ClassPaths.Add(UWidgetAnimPresetAsset::StaticClass()->GetClassPathName());
			Config.Filter.bRecursiveClasses = true;
			Config.InitialAssetViewType = EAssetViewType::List;
			Config.bAllowNullSelection = false;
			Config.bFocusSearchBoxWhenOpened = true;
			Config.SelectionMode = ESelectionMode::Single;
			Config.OnAssetSelected = FOnAssetSelected::CreateLambda([this, GroupIndex](const FAssetData& AssetData)
			{
				FSlateApplication::Get().DismissAllMenus();
				InsertAnimation(Cast<UWidgetAnimPresetAsset>(AssetData.GetAsset()), GroupIndex);
			});

			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			SubMenu.AddWidget(
				SNew(SBox)
				.WidthOverride(320.f)
				.HeightOverride(360.f)
				[
					ContentBrowser.Get().CreateAssetPicker(Config)
				],
				FText::GetEmpty(),
				/*bNoIndent=*/true);
		}),
		/*bInOpenSubMenuOnClick=*/false,
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), "ClassIcon.WidgetAnimPresetAsset"));
}

void SWidgetAnimTimeline::AddTrackSubMenu(FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds)
{
	// Named after its group only where there are groups to tell apart.
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const bool bNameGroup = Groups && Groups->IsValidIndex(GroupIndex) && (!Source.IsValid() || Source->SupportsLayerGroups());
	const FText Label = bNameGroup
		? FText::Format(LOCTEXT("AddTrackToGroup", "Add Animation Track to {0}"), GetGroupName(GroupIndex))
		: LOCTEXT("AddTrack", "Add Animation Track");

	Menu.AddSubMenu(
		Label,
		WidgetAnimTips::AddTrackToGroupTooltip(),
		FNewMenuDelegate::CreateLambda([this, GroupIndex, AtSeconds](FMenuBuilder& SubMenu)
		{
			AddTrackTypeEntries(SubMenu, GroupIndex, AtSeconds);
		}),
		/*bInOpenSubMenuOnClick=*/false,
		// Green: the one entry that adds to the animation, picked out from the ones that change what is there.
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuAddIcon));
}

void SWidgetAnimTimeline::AddTrackTypeEntries(FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds)
{
	WidgetAnimTimeline::ForEachAnimType([this, &Menu, GroupIndex, AtSeconds](EWidgetAnimType AnimType)
	{
		Menu.AddMenuEntry(
			GetAnimTypeName(AnimType),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, AnimType, AtSeconds]() { AddLayer(GroupIndex, AnimType, nullptr, AtSeconds); })));
	});

	// Registered types after the built-in ones, under the heading each names for itself - Text for the text tracks -
	// in heading order, and those naming none under Registered Types after the rest. Each is a type in its own right,
	// whichever module it came from.
	TArray<FString> Categories;
	TMap<FString, TArray<const FWidgetAnimEvaluator*>> ByCategory;
	WidgetAnimTimeline::ForEachRegisteredType([&Categories, &ByCategory](const FWidgetAnimEvaluator& Evaluator)
	{
		const FString Category = Evaluator.GetCategory().ToString();
		if (!ByCategory.Contains(Category))
		{
			Categories.Add(Category);
		}
		ByCategory.FindOrAdd(Category).Add(&Evaluator);
	});
	Categories.Sort([](const FString& A, const FString& B)
	{
		return A.IsEmpty() != B.IsEmpty() ? B.IsEmpty() : A < B;
	});

	for (const FString& Category : Categories)
	{
		Menu.BeginSection(NAME_None, Category.IsEmpty() ? LOCTEXT("RegisteredTypesSection", "Registered Types") : FText::FromString(Category));
		for (const FWidgetAnimEvaluator* Evaluator : ByCategory.FindChecked(Category))
		{
			const UScriptStruct* ParamsStruct = Evaluator->GetParamsStruct();
			Menu.AddMenuEntry(
				Evaluator->GetDisplayName(),
				Evaluator->GetDescription(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, ParamsStruct, AtSeconds]()
				{
					const FInstancedStruct InitialParams(ParamsStruct);
					AddLayer(GroupIndex, EWidgetAnimType::Custom, &InitialParams, AtSeconds);
				})));
		}
		Menu.EndSection();
	}

	// Blueprint types last, found as assets. Loaded on pick, so the menu costs nothing for types not chosen.
	bool bFirstBlueprint = true;
	WidgetAnimTimeline::ForEachBlueprintType([this, &Menu, GroupIndex, AtSeconds, &bFirstBlueprint](const FTopLevelAssetPath& ClassPath, const FText& TypeName)
	{
		if (bFirstBlueprint)
		{
			Menu.BeginSection(NAME_None, LOCTEXT("BlueprintTypesSection", "Blueprint Types"));
			bFirstBlueprint = false;
		}

		Menu.AddMenuEntry(
			TypeName,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, ClassPath, AtSeconds]()
			{
				UClass* TypeClass = LoadObject<UClass>(nullptr, *ClassPath.ToString());
				if (TypeClass && TypeClass->IsChildOf(UWidgetAnimBlueprintType::StaticClass()))
				{
					const FInstancedStruct InitialParams = FWidgetAnimBlueprintTypeParams::MakeFor(TypeClass);
					AddLayer(GroupIndex, EWidgetAnimType::Custom, &InitialParams, AtSeconds);
				}
			})));
	});
	if (!bFirstBlueprint)
	{
		Menu.EndSection();
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeAddMenu(int32 GroupIndex, bool bWithinGroup, TOptional<float> AtSeconds)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	if (!CanEdit())
	{
		Menu.AddWidget(SNew(STextBlock).Text(IsReadOnly()
			? WidgetAnimTips::ReadOnlyAdd()
			: WidgetAnimTips::ReadOnlyAsset()), FText::GetEmpty());
		return Menu.MakeWidget();
	}

	// A plain list of tracks: its track types straight away, then the animations it can be built on. No groups.
	if (!Source->SupportsLayerGroups())
	{
		// Loose entries rather than a section of their own: the registered and Blueprint types open sections of
		// their own, and a menu section can't be opened inside another.
		AddTrackTypeEntries(Menu, GetDefaultGroup(GroupIndex), AtSeconds);

		Menu.BeginSection("Existing", LOCTEXT("ExistingSection", "Build On"));
		AddInsertAnimationEntry(Menu, GetDefaultGroup(GroupIndex));
		AddPasteEntry(Menu, GetDefaultGroup(GroupIndex), AtSeconds);
		Menu.EndSection();

		return Menu.MakeWidget();
	}

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const bool bInGroup = bWithinGroup && Groups && Groups->IsValidIndex(GroupIndex);

	// Opened within a group, the menu is about that group: a new group would be beside the point there.
	if (!bInGroup)
	{
		Menu.BeginSection("Group", LOCTEXT("GroupSection", "Layer Group"));
		Menu.AddMenuEntry(
			LOCTEXT("AddGroup", "Add Layer Group"),
			WidgetAnimTips::AddGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed"),
			FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));
		Menu.EndSection();
	}

	// Within a group, just that group's. Elsewhere every group, each with every track type in a menu of its own - so
	// a track can go in any group from there, not just the one selected.
	Menu.BeginSection("Tracks", LOCTEXT("TracksSection", "Tracks"));
	if (bInGroup)
	{
		AddTrackSubMenu(Menu, GroupIndex, AtSeconds);
	}
	else if (Groups && Groups->Num() > 0)
	{
		for (int32 Group = 0; Group < Groups->Num(); ++Group)
		{
			AddTrackSubMenu(Menu, Group, AtSeconds);
		}
	}
	else
	{
		AddTrackSubMenu(Menu, INDEX_NONE, AtSeconds);
	}
	Menu.EndSection();

	Menu.BeginSection("Existing", LOCTEXT("ExistingSection", "Build On"));
	AddInsertAnimationEntry(Menu, GetDefaultGroup(GroupIndex));
	AddPasteEntry(Menu, bInGroup ? GroupIndex : INDEX_NONE, AtSeconds);
	Menu.EndSection();

	return Menu.MakeWidget();
}

void SWidgetAnimTimeline::AddPasteEntry(FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds)
{
	Menu.AddMenuEntry(
		LOCTEXT("PasteTracks", "Paste"),
		WidgetAnimTips::PasteTracksTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste"),
		FUIAction(
			FExecuteAction::CreateLambda([this, GroupIndex, AtSeconds]() { PasteLayers(GroupIndex, AtSeconds); }),
			FCanExecuteAction::CreateLambda([this]() { return CanEdit() && CanPasteLayers(); })),
		NAME_None,
		EUserInterfaceActionType::Button,
		NAME_None,
		FText::FromString(TEXT("Ctrl+V")));
}

void SWidgetAnimTimeline::AddClipboardEntries(FMenuBuilder& Menu, const TArray<int32>& LayerIndices, int32 GroupIndex, TOptional<float> AtSeconds)
{
	const bool bSeveral = LayerIndices.Num() > 1;
	bool bAnyUnlocked = false;
	for (const int32 LayerIndex : LayerIndices)
	{
		bAnyUnlocked |= CanEditLayer(LayerIndex);
	}

	Menu.AddMenuEntry(
		bSeveral ? LOCTEXT("CutTracks", "Cut Tracks") : LOCTEXT("CutTrack", "Cut Track"),
		WidgetAnimTips::CutTracksTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Cut"),
		FUIAction(
			FExecuteAction::CreateLambda([this, LayerIndices]() { CutLayers(LayerIndices); }),
			FCanExecuteAction::CreateLambda([bAnyUnlocked]() { return bAnyUnlocked; })),
		NAME_None,
		EUserInterfaceActionType::Button,
		NAME_None,
		FText::FromString(TEXT("Ctrl+X")));

	Menu.AddMenuEntry(
		bSeveral ? LOCTEXT("CopyTracks", "Copy Tracks") : LOCTEXT("CopyTrack", "Copy Track"),
		WidgetAnimTips::CopyTracksTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndices]() { CopyLayers(LayerIndices); })),
		NAME_None,
		EUserInterfaceActionType::Button,
		NAME_None,
		FText::FromString(TEXT("Ctrl+C")));

	AddPasteEntry(Menu, GroupIndex, AtSeconds);
}

void SWidgetAnimTimeline::AddFocusEntries(FMenuBuilder& Menu, const TArray<int32>& LayerIndices)
{
	Menu.AddMenuEntry(
		LayerIndices.Num() > 1 ? LOCTEXT("FocusTracks", "Focus Tracks") : LOCTEXT("FocusTrack", "Focus Track"),
		WidgetAnimTips::FocusTracksTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Filter"),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndices]() { FocusLayers(LayerIndices); })));

	if (IsFocusActive())
	{
		Menu.AddMenuEntry(
			LOCTEXT("UnfocusTracks", "Un-focus"),
			WidgetAnimTips::UnfocusTracksTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this]() { UnfocusLayers(); })));
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeGroupContextMenu(int32 GroupIndex, TOptional<float> AtSeconds)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	AddMenuHeading(Menu, GetGroupName(GroupIndex), GetGroupColor(GroupIndex));

	// The group's tracks, to hide from the previews or show again - as a track's own menu does, for all of them.
	TArray<int32> Members;
	bool bAnyShown = false;
	bool bAnyHidden = false;
	if (const TArray<FWidgetAnimLayer>* Layers = GetLayers())
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			if (GetLayerGroup(LayerIndex) == GroupIndex)
			{
				Members.Add(LayerIndex);
				(IsLayerHidden(LayerIndex) ? bAnyHidden : bAnyShown) = true;
			}
		}
	}

	// Looking at the group - jumping to it, hiding it - is offered whatever can be edited.
	Menu.BeginSection("View", LOCTEXT("GroupViewSection", "View"));

	Menu.AddMenuEntry(
		LOCTEXT("JumpToGroup", "Jump to Group"),
		WidgetAnimTips::JumpToGroupTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(
			FExecuteAction::CreateLambda([this, GroupIndex]() { JumpToGroup(GroupIndex); }),
			FCanExecuteAction::CreateLambda([Members]() { return Members.Num() > 0; })));

	if (bAnyShown)
	{
		Menu.AddMenuEntry(
			LOCTEXT("HideGroup", "Hide Group"),
			WidgetAnimTips::HideGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Hidden"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/true); })));
	}
	if (bAnyHidden)
	{
		Menu.AddMenuEntry(
			LOCTEXT("ShowGroup", "Show Group"),
			WidgetAnimTips::ShowGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/false); })));
	}

	Menu.AddMenuEntry(
		IsTrackGroupCollapsed(GroupIndex) ? LOCTEXT("Expand", "Unfold in Track Editor") : LOCTEXT("Collapse", "Fold in Track Editor"),
		WidgetAnimTips::CollapseTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { ToggleTrackGroupCollapsed(GroupIndex); })));

	Menu.EndSection();

	if (!CanEdit())
	{
		return Menu.MakeWidget();
	}

	Menu.BeginSection("Tracks", LOCTEXT("GroupTracksSection", "Tracks"));
	AddTrackSubMenu(Menu, GroupIndex, AtSeconds);
	AddInsertAnimationEntry(Menu, GroupIndex);
	AddPasteEntry(Menu, GroupIndex, AtSeconds);
	Menu.EndSection();

	Menu.BeginSection("Group", LOCTEXT("GroupSectionContext", "Layer Group"));

	Menu.AddMenuEntry(
		LOCTEXT("DuplicateGroup", "Duplicate Group"),
		WidgetAnimTips::DuplicateGroupTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Duplicate"),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { DuplicateGroup(GroupIndex); })));

	Menu.AddMenuEntry(
		LOCTEXT("MoveGroupUp", "Move Up"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, GroupIndex]() { MoveGroupTo(GroupIndex, GroupIndex - 1); }),
			FCanExecuteAction::CreateLambda([GroupIndex]() { return GroupIndex > 0; })));

	Menu.AddMenuEntry(
		LOCTEXT("MoveGroupDown", "Move Down"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this, GroupIndex]() { MoveGroupTo(GroupIndex, GroupIndex + 2); }),
			FCanExecuteAction::CreateLambda([this, GroupIndex]()
			{
				const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
				return Groups && GroupIndex + 1 < Groups->Num();
			})));

	Menu.AddMenuEntry(
		LOCTEXT("DeleteGroup", "Delete Group"),
		WidgetAnimTips::DeleteGroupTooltip(),
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { DeleteGroup(GroupIndex); })));

	Menu.AddMenuEntry(
		LOCTEXT("AddGroupContext", "Add Layer Group"),
		WidgetAnimTips::AddGroupTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed"),
		FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));

	Menu.EndSection();
	return Menu.MakeWidget();
}

void SWidgetAnimTimeline::AddMenuHeading(FMenuBuilder& Menu, const FText& Heading, const FLinearColor& Accent)
{
	using namespace WidgetAnimTimeline;

	Menu.AddWidget(
		SNew(SBox)
		.Padding(FMargin(8.f, 4.f, 12.f, 4.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 7.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(3.f)
				.HeightOverride(14.f)
				[
					SNew(SImage)
					.Image(RoundedFill(2.f))
					.ColorAndOpacity(Accent.CopyWithNewOpacity(1.f))
				]
			]

			// Nudged down a pixel: the text's line box keeps room below the baseline for descenders, so centering the
			// box leaves the letters themselves sitting high against the bar beside them.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 1.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.Text(Heading)
			]
		],
		FText::GetEmpty(),
		/*bNoIndent=*/true);
}

void SWidgetAnimTimeline::JumpToLayer(int32 LayerIndex)
{
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	if (!Spans.IsValidIndex(LayerIndex))
	{
		return;
	}

	ScrubToSeconds(Spans[LayerIndex].X);
	RevealLayer(LayerIndex);
}

void SWidgetAnimTimeline::JumpToGroup(int32 GroupIndex)
{
	const FVector2f GroupSpan = GetGroupSpan(GroupIndex);
	if (GroupSpan.Y < GroupSpan.X)
	{
		return;
	}

	ScrubToSeconds(GroupSpan.X);
	RevealGroup(GroupIndex);
	if (Tracks.IsValid())
	{
		Tracks->RevealTime(GroupSpan.X, GroupSpan.Y);
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeLayerContextMenu(int32 LayerIndex, int32 KeyIndex, TOptional<float> LocalSeconds, TOptional<float> ClickSeconds)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const int32 GroupIndex = GetLayerGroup(LayerIndex);
	const TArray<int32> ActedOn = GetActedOnLayers(LayerIndex);

	// Where on the time axis the track was right-clicked - unset from the Animation Tracks list: a track added from
	// here starts there.
	const TOptional<float> AddAtSeconds = ClickSeconds;

	bool bAnyLocked = false;
	bool bAnyUnlocked = false;
	bool bAnyHidden = false;
	bool bAnyShown = false;
	for (const int32 Index : ActedOn)
	{
		(IsLayerLocked(Index) ? bAnyLocked : bAnyUnlocked) = true;
		(IsLayerHidden(Index) ? bAnyHidden : bAnyShown) = true;
	}

	// Hiding is a way of looking at the animation, so it is offered even while the panel is locked read-only.
	auto AddHideEntries = [this, &Menu, &ActedOn, bAnyHidden, bAnyShown]()
	{
		const bool bSeveral = ActedOn.Num() > 1;
		if (bAnyShown)
		{
			Menu.AddMenuEntry(
				bSeveral ? LOCTEXT("HideTracks", "Hide Tracks") : LOCTEXT("HideTrack", "Hide Track"),
				WidgetAnimTips::HideTrackTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Hidden"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersHidden(ActedOn, /*bHidden=*/true); })));
		}
		if (bAnyHidden)
		{
			Menu.AddMenuEntry(
				bSeveral ? LOCTEXT("ShowTracks", "Show Tracks") : LOCTEXT("ShowTrack", "Show Track"),
				WidgetAnimTips::ShowTrackTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersHidden(ActedOn, /*bHidden=*/false); })));
		}
	};

	// What the menu acts on, first: the track and its group - or, for several, just that.
	{
		const TArray<FWidgetAnimLayer>* HeadingLayers = GetLayers();
		const bool bOne = ActedOn.Num() == 1 && HeadingLayers && HeadingLayers->IsValidIndex(LayerIndex);
		AddMenuHeading(Menu,
			bOne
				? FText::Format(LOCTEXT("TrackMenuHeading", "{0}  ({1})"), GetLayerTypeName((*HeadingLayers)[LayerIndex]), GetLayerWhereName(LayerIndex))
				: LOCTEXT("MultipleTracksMenuHeading", "Multiple Tracks"),
			bOne ? GetLayerTypeColor((*HeadingLayers)[LayerIndex]) : GetGroupColor(GroupIndex));
	}

	// Jumping there only looks at the animation, so it is offered whatever can be edited.
	auto AddJumpEntry = [this, &Menu, LayerIndex]()
	{
		Menu.AddMenuEntry(
			LOCTEXT("JumpToTrack", "Jump to Track"),
			WidgetAnimTips::JumpToTrackTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { JumpToLayer(LayerIndex); })));
	};

	if (!CanEdit())
	{
		Menu.BeginSection("View", LOCTEXT("ViewSection", "Preview"));
		AddJumpEntry();
		AddHideEntries();
		AddFocusEntries(Menu, ActedOn);
		Menu.EndSection();

		// Copying only reads the animation, so it is offered while it can't be edited.
		Menu.BeginSection("Clipboard", LOCTEXT("ClipboardSection", "Clipboard"));
		Menu.AddMenuEntry(
			ActedOn.Num() > 1 ? LOCTEXT("CopyTracksReadOnly", "Copy Tracks") : LOCTEXT("CopyTrackReadOnly", "Copy Track"),
			WidgetAnimTips::CopyTracksTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { CopyLayers(ActedOn); })));
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	if (CanEditKeys(LayerIndex) && ActedOn.Num() == 1)
	{
		Menu.BeginSection("Keys", LOCTEXT("KeysSection", "Keys"));

		if (KeyIndex != INDEX_NONE)
		{
			const bool bAmongSeveral = IsKeyMultiSelection() && IsKeySelected(LayerIndex, KeyIndex);

			if (bAmongSeveral)
			{
				// Only those that can go: a track's start and end keys stay whatever is selected.
				int32 NumDeletable = 0;
				for (const FKeyRef& Ref : SelectedKeys)
				{
					NumDeletable += CanDeleteKey(Ref.Layer, Ref.Key) ? 1 : 0;
				}

				Menu.AddMenuEntry(
					FText::Format(LOCTEXT("DeleteSelectedKeys", "Delete {0} Selected {0}|plural(one=Key,other=Keys)"), FText::AsNumber(NumDeletable)),
					WidgetAnimTips::DeleteSelectedKeysTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this]() { DeleteSelectedKeys(); }),
						FCanExecuteAction::CreateLambda([NumDeletable]() { return NumDeletable > 0; })));

				// The curve between every pair of neighboring selected keys, all at once.
				TArray<FKeyRef> Segments;
				GetSelectedSegments(Segments);
				if (Segments.Num() > 0)
				{
					Menu.AddSubMenu(
						LOCTEXT("CurveBetweenSelectedKeys", "Curve Between Selected Keys"),
						WidgetAnimTips::CurveBetweenSelectedKeysTooltip(),
						FNewMenuDelegate::CreateLambda([this, Segments](FMenuBuilder& SubMenu) { AddSegmentCurveEntries(SubMenu, Segments); }));
				}
			}
			else
			{
				Menu.AddMenuEntry(
					LOCTEXT("DeleteKey", "Delete Key"),
					WidgetAnimTips::DeleteKeyTooltip(),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateLambda([this, LayerIndex, KeyIndex]() { DeleteKey(LayerIndex, KeyIndex); }),
						FCanExecuteAction::CreateLambda([this, LayerIndex, KeyIndex]() { return CanDeleteKey(LayerIndex, KeyIndex); })));

				// The stretch from this key to the next.
				if (UsesSegmentCurves(LayerIndex) && GetSegmentCurve(LayerIndex, KeyIndex))
				{
					const TArray<FKeyRef> Segments = { FKeyRef{ LayerIndex, KeyIndex } };
					Menu.AddSubMenu(
						LOCTEXT("CurveToNextKey", "Curve To Next Key"),
						WidgetAnimTips::CurveToNextKeyTooltip(),
						FNewMenuDelegate::CreateLambda([this, Segments](FMenuBuilder& SubMenu) { AddSegmentCurveEntries(SubMenu, Segments); }));
				}
			}
		}
		else if (LocalSeconds.IsSet())
		{
			// Named by where it will land in the animation - the time the ruler and the playhead read.
			const float Seconds = LocalSeconds.GetValue();
			TArray<FVector2f> MenuSpans;
			GetLayerSpans(MenuSpans);
			const float AnimationSeconds = (MenuSpans.IsValidIndex(LayerIndex) ? MenuSpans[LayerIndex].X : 0.f) + Seconds;
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("AddKeyHereAt", "Add Key Here ({0} s)"), FText::FromString(FString::Printf(TEXT("%.2f"), AnimationSeconds))),
				WidgetAnimTips::AddKeyHereTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
				FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, Seconds]() { AddKey(LayerIndex, Seconds); })));
		}
		else
		{
			// From the Animation Tracks list there is no "here" - the playhead is, when it is on the track.
			TArray<FVector2f> MenuSpans;
			GetLayerSpans(MenuSpans);
			const float Playhead = GetPlayheadSeconds();
			if (MenuSpans.IsValidIndex(LayerIndex) && Playhead > MenuSpans[LayerIndex].X && Playhead < MenuSpans[LayerIndex].X + MenuSpans[LayerIndex].Y)
			{
				const float Seconds = Playhead - MenuSpans[LayerIndex].X;
				Menu.AddMenuEntry(
					FText::Format(LOCTEXT("AddKeyAtPlayhead", "Add Key at Playhead ({0} s)"), FText::FromString(FString::Printf(TEXT("%.2f"), Playhead))),
					WidgetAnimTips::AddKeyAtPlayheadTooltip(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, Seconds]() { AddKey(LayerIndex, Seconds); })));
			}
		}

		Menu.EndSection();
	}

	// Several tracks selected, this one among them: what they can all do together.
	if (ActedOn.Num() > 1)
	{
		Menu.BeginSection("Selected", FText::Format(LOCTEXT("SelectedTracksSection", "{0} Selected Tracks"), FText::AsNumber(ActedOn.Num())));

		AddHideEntries();
		AddFocusEntries(Menu, ActedOn);

		if (bAnyUnlocked)
		{
			Menu.AddMenuEntry(
				LOCTEXT("LockSelected", "Lock Tracks"),
				WidgetAnimTips::LockSelectedTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Lock"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersLocked(ActedOn, /*bLocked=*/true); })));
		}
		if (bAnyLocked)
		{
			Menu.AddMenuEntry(
				LOCTEXT("UnlockSelected", "Unlock Tracks"),
				FText::GetEmpty(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Unlock"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersLocked(ActedOn, /*bLocked=*/false); })));
		}

		Menu.AddMenuEntry(
			LOCTEXT("DeleteSelected", "Delete Tracks"),
			WidgetAnimTips::DeleteSelectedTooltip(),
			FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { DeleteSelectedLayers(); }),
				FCanExecuteAction::CreateLambda([bAnyUnlocked]() { return bAnyUnlocked; })));

		Menu.EndSection();

		Menu.BeginSection("Clipboard", LOCTEXT("ClipboardSection", "Clipboard"));
		AddClipboardEntries(Menu, ActedOn, GroupIndex, AddAtSeconds);
		Menu.EndSection();

		Menu.BeginSection("Add", LOCTEXT("AddSectionInContext", "Add"));
		AddTrackSubMenu(Menu, GroupIndex, AddAtSeconds);
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	const bool bLocked = IsLayerLocked(LayerIndex);

	Menu.BeginSection("Track", LOCTEXT("TrackSection", "Track"));

	AddJumpEntry();
	AddHideEntries();
	AddFocusEntries(Menu, ActedOn);

	// Locking: the track stays selectable - its settings can be looked at - but nothing moves or edits it.
	Menu.AddMenuEntry(
		bLocked ? LOCTEXT("UnlockTrack", "Unlock Track") : LOCTEXT("LockTrack", "Lock Track"),
		bLocked
			? WidgetAnimTips::UnlockTrackMenuTooltip()
			: WidgetAnimTips::LockTrackMenuTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), bLocked ? "Icons.Unlock" : "Icons.Lock"),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, bLocked]() { SetLayersLocked({ LayerIndex }, !bLocked); })));

	Menu.AddMenuEntry(
		LOCTEXT("Duplicate", "Duplicate Track"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Duplicate"),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { DuplicateLayer(LayerIndex); })));

	Menu.AddMenuEntry(
		LOCTEXT("DeleteTrack", "Delete Track"),
		FText::GetEmpty(),
		// Trash can, in red: the one entry here that takes something away.
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
		FUIAction(
			FExecuteAction::CreateLambda([this, LayerIndex]() { DeleteLayer(LayerIndex); }),
			FCanExecuteAction::CreateLambda([this, LayerIndex]() { return CanEditLayer(LayerIndex); })));

	Menu.EndSection();

	Menu.BeginSection("Clipboard", LOCTEXT("ClipboardSection", "Clipboard"));
	AddClipboardEntries(Menu, ActedOn, GroupIndex, AddAtSeconds);
	Menu.EndSection();

	Menu.BeginSection("Add", LOCTEXT("AddSectionInContext", "Add"));
	AddTrackSubMenu(Menu, GroupIndex, AddAtSeconds);
	AddInsertAnimationEntry(Menu, GroupIndex);
	if (!Source.IsValid() || Source->SupportsLayerGroups())
	{
		Menu.AddMenuEntry(
			LOCTEXT("AddGroupLayerContext", "Add Layer Group"),
			WidgetAnimTips::AddGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed"),
			FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));

		// Into another group - another widget - by name, every other group listed.
		const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
		if (Groups && Groups->Num() > 1 && CanEditLayer(LayerIndex))
		{
			Menu.AddSubMenu(
				LOCTEXT("MoveToLayerGroup", "Move to Layer Group"),
				WidgetAnimTips::MoveToGroupTooltip(),
				FNewMenuDelegate::CreateLambda([this, GroupIndex, LayerIndex](FMenuBuilder& SubMenu)
				{
					const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
					for (int32 Target = 0; Current && Target < Current->Num(); ++Target)
					{
						if (Target == GroupIndex)
						{
							continue;
						}

						SubMenu.AddMenuEntry(
							GetGroupName(Target),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, Target]() { SetLayerGroup(LayerIndex, Target); })));
					}
				}),
				/*bInOpenSubMenuOnClick=*/false,
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.ArrowRight"));
		}
	}
	Menu.EndSection();

	return Menu.MakeWidget();
}

void SWidgetAnimTimeline::AddSegmentCurveEntries(FMenuBuilder& Menu, const TArray<FKeyRef>& Segments)
{
	// What the segments are set to now, when they all agree - shown ticked.
	TOptional<FWidgetAnimSegmentCurve> Shared;
	bool bAgree = true;
	for (const FKeyRef& Segment : Segments)
	{
		if (const FWidgetAnimSegmentCurve* Curve = GetSegmentCurve(Segment.Layer, Segment.Key))
		{
			if (!Shared.IsSet())
			{
				Shared = *Curve;
			}
			else if (Shared.GetValue() != *Curve)
			{
				bAgree = false;
			}
		}
	}

	const UEnum* EasingEnum = StaticEnum<EWidgetAnimSegmentEasing>();
	const int32 NumEasings = EasingEnum ? EasingEnum->NumEnums() - 1 : 0; // the last entry is the generated _MAX

	for (int32 Index = 0; Index < NumEasings; ++Index)
	{
		const EWidgetAnimSegmentEasing Easing = static_cast<EWidgetAnimSegmentEasing>(EasingEnum->GetValueByIndex(Index));
		if (Easing == EWidgetAnimSegmentEasing::Custom)
		{
			continue;
		}

		FWidgetAnimSegmentCurve Curve;
		Curve.Easing = Easing;
		const bool bCurrent = bAgree && Shared.IsSet() && Shared.GetValue().Easing == Easing;

		Menu.AddMenuEntry(
			EasingEnum->GetDisplayNameTextByIndex(Index),
			EasingEnum->GetToolTipTextByIndex(Index),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Segments, Curve]() { SetSegmentCurves(Segments, Curve); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([bCurrent]() { return bCurrent; })),
			NAME_None,
			EUserInterfaceActionType::RadioButton);
	}

	// Custom: a Curve asset, picked right here - the same picker a Custom easing's curve row has.
	const bool bCustomCurrent = bAgree && Shared.IsSet() && Shared.GetValue().Easing == EWidgetAnimSegmentEasing::Custom;
	Menu.AddSubMenu(
		bCustomCurrent && Shared.GetValue().CustomCurve
			? FText::Format(LOCTEXT("SegmentCurveCustomCurrent", "Custom Curve ({0})"), FText::FromString(Shared.GetValue().CustomCurve->GetName()))
			: LOCTEXT("SegmentCurveCustom", "Custom Curve"),
		WidgetAnimTips::SegmentCurveCustomTooltip(),
		FNewMenuDelegate::CreateLambda([this, Segments](FMenuBuilder& SubMenu)
		{
			FAssetPickerConfig Config;
			Config.Filter.ClassPaths.Add(UCurveFloat::StaticClass()->GetClassPathName());
			Config.Filter.bRecursiveClasses = true;
			Config.InitialAssetViewType = EAssetViewType::List;
			Config.bAllowNullSelection = false;
			Config.bFocusSearchBoxWhenOpened = true;
			Config.SelectionMode = ESelectionMode::Single;
			Config.OnAssetSelected = FOnAssetSelected::CreateLambda([this, Segments](const FAssetData& AssetData)
			{
				FSlateApplication::Get().DismissAllMenus();

				FWidgetAnimSegmentCurve Curve;
				Curve.Easing = EWidgetAnimSegmentEasing::Custom;
				Curve.CustomCurve = Cast<UCurveFloat>(AssetData.GetAsset());
				if (Curve.CustomCurve)
				{
					SetSegmentCurves(Segments, Curve);
				}
			});

			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
			SubMenu.AddWidget(
				SNew(SBox)
				.WidthOverride(320.f)
				.HeightOverride(360.f)
				[
					ContentBrowser.Get().CreateAssetPicker(Config)
				],
				FText::GetEmpty(),
				/*bNoIndent=*/true);
		}));
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeKeyCurvePanel()
{
	auto GetSegments = [this]()
	{
		TArray<FKeyRef> Segments;
		GetSelectedSegments(Segments);
		return Segments;
	};

	// What the selected stretches are set to, as the button reads: one name when they agree.
	auto GetCurrentName = [this, GetSegments]()
	{
		const TArray<FKeyRef> Segments = GetSegments();
		if (Segments.Num() == 0)
		{
			return WidgetAnimTips::KeyCurveNone();
		}

		const FWidgetAnimSegmentCurve* First = GetSegmentCurve(Segments[0].Layer, Segments[0].Key);
		for (const FKeyRef& Segment : Segments)
		{
			const FWidgetAnimSegmentCurve* Curve = GetSegmentCurve(Segment.Layer, Segment.Key);
			if (!First || !Curve || *Curve != *First)
			{
				return LOCTEXT("KeyCurveMultiple", "Multiple Values");
			}
		}
		return First ? GetSegmentCurveName(*First) : FText::GetEmpty();
	};

	auto GetStatus = [this, GetSegments]()
	{
		const int32 NumSegments = GetSegments().Num();

		bool bAnySpring = false;
		const TArray<FWidgetAnimLayer>* Layers = GetLayers();
		for (const int32 LayerIndex : SelectedLayers)
		{
			bAnySpring |= Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].IsSpringTimed();
		}

		FText Status = NumSegments == 0
			? WidgetAnimTips::KeyCurveNoSegments()
			: FText::Format(WidgetAnimTips::KeyCurveSegments(), FText::AsNumber(NumSegments));

		if (bAnySpring)
		{
			Status = FText::Format(WidgetAnimTips::KeyCurveSpringNote(), Status);
		}
		return Status;
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(12.f, 10.f))
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.Text_Lambda([this]()
				{
					return FText::Format(LOCTEXT("KeyCurvePanelTitle", "{0} keys selected, on {1} {1}|plural(one=track,other=tracks)"),
						FText::AsNumber(SelectedKeys.Num()), FText::AsNumber(SelectedLayers.Num()));
				})
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 6.f, 0.f, 12.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(WidgetAnimTips::KeyCurvePanelHelp())
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 10.f, 0.f))
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("KeyCurveLabel", "Curve Between Keys"))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SNew(SComboButton)
					.IsEnabled_Lambda([this, GetSegments]() { return IsSelectionEditable() && GetSegments().Num() > 0; })
					.OnGetMenuContent_Lambda([this, GetSegments]()
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
						AddSegmentCurveEntries(Menu, GetSegments());
						return Menu.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text_Lambda(GetCurrentName)
					]
				]
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda(GetStatus)
			]
		];
}

// -----------------------------------------------------------------------------------------------------
// Presentation
// -----------------------------------------------------------------------------------------------------

FText SWidgetAnimTimeline::GetAnimTypeName(EWidgetAnimType AnimType)
{
	const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
	return TypeEnum ? TypeEnum->GetDisplayNameTextByValue(static_cast<int64>(AnimType)) : FText::GetEmpty();
}

FLinearColor SWidgetAnimTimeline::GetAnimTypeColor(EWidgetAnimType AnimType)
{
	// The user's, from Editor Preferences > Plugins > Widget Animations Editor; NameTBDColors.h's by default.
	return UWidgetAnimEditorSettings::Get().GetTrackColor(AnimType);
}

FText SWidgetAnimTimeline::GetLayerTypeName(const FWidgetAnimLayer& Layer)
{
	if (Layer.AnimType != EWidgetAnimType::Custom)
	{
		return GetAnimTypeName(Layer.AnimType);
	}

	if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
	{
		return Evaluator->GetTrackDisplayName(Layer.TypeParams);
	}

	// Settings of a type whose module isn't loaded are still named - by their struct - so the track can be found and
	// dealt with; no type picked yet reads as the bare enum name.
	if (const UScriptStruct* ParamsStruct = Layer.TypeParams.GetScriptStruct())
	{
		return FText::Format(LOCTEXT("UnregisteredTypeName", "{0} (not registered)"), ParamsStruct->GetDisplayNameText());
	}
	return GetAnimTypeName(Layer.AnimType);
}

FLinearColor SWidgetAnimTimeline::GetLayerTypeColor(const FWidgetAnimLayer& Layer)
{
	// A Blueprint type is drawn in its own color when it sets one, else in the one color every Blueprint type shares -
	// so a designer's types read apart from the C++ ones, and a type that matters can stand out on its own.
	if (const FWidgetAnimBlueprintTypeParams* BlueprintParams = Layer.AnimType == EWidgetAnimType::Custom ? Layer.TypeParams.GetPtr<FWidgetAnimBlueprintTypeParams>() : nullptr)
	{
		const UWidgetAnimBlueprintType* TypeObject = BlueprintParams->GetTypeObject();
		return TypeObject && TypeObject->bOverrideTrackColor ? TypeObject->TrackColor : UWidgetAnimEditorSettings::Get().BlueprintTypeTrackColor;
	}
	return GetAnimTypeColor(Layer.AnimType);
}

FLinearColor SWidgetAnimTimeline::GetLockedColor(const FLinearColor& Color)
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	// Toward gray, then darker: still recognizably its type, plainly not something to grab.
	const float Gray = Color.GetLuminance();
	FLinearColor Washed = FMath::Lerp(Color, FLinearColor(Gray, Gray, Gray, Color.A), Palette::LockedDesaturate) * Palette::LockedBrightness;
	Washed.A = Color.A;
	return Washed;
}

FLinearColor SWidgetAnimTimeline::GetGroupColor(int32 GroupIndex)
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	if (GroupIndex < 0)
	{
		return Palette::LayerUnknown;
	}

	// Around the hue circle by the golden angle: each new group lands as far as possible from the ones before
	// it, however many there are, without a list running out. Muted - low saturation and value - so groups
	// read as a quiet cue beside the vivid layer colors rather than one more color competing with them.
	constexpr float GoldenAngle = 0.381966f;
	const float Hue = FMath::Frac(Palette::GroupHueStart + GroupIndex * GoldenAngle);

	FLinearColor HSV(Hue * 360.f, Palette::GroupSaturation, Palette::GroupValue, 1.f);
	return HSV.HSVToLinearRGB();
}

FText SWidgetAnimTimeline::GetEasingName(const FWidgetAnimLayer& Layer)
{
	// A Spring track has no easing - its spring is its motion, and its Easing is hidden - so it says so.
	if (Layer.IsSpringTimed())
	{
		return LOCTEXT("SpringEasingName", "Spring");
	}

	// A value-driven track goes where its value says, so it names the value it follows instead.
	if (Layer.IsValueDriven())
	{
		return Layer.ValueName.IsNone()
			? LOCTEXT("UnnamedValueEasingName", "Value (no name)")
			: FText::Format(LOCTEXT("ValueEasingName", "Value: {0}"), FText::FromName(Layer.ValueName));
	}

	// A Custom easing is a Curve asset - Custom Curve - so it is shown by that asset's own name.
	const UEnum* EasingEnum = StaticEnum<EWidgetAnimEasing>();
	const FText EasingName = Layer.Easing == EWidgetAnimEasing::Custom && Layer.CustomCurve
		? FText::FromString(Layer.CustomCurve->GetName())
		: (EasingEnum ? EasingEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.Easing)) : FText::GetEmpty());

	// A Tween Scale or Slide's Overshoot, while it has no keys, is named beside the easing - it is what makes the
	// track arrive early on its way past its end, and a label naming only the easing hid that.
	const float Overshoot = Layer.AnimType == EWidgetAnimType::Scale ? Layer.Overshoot
		: Layer.AnimType == EWidgetAnimType::Slide ? Layer.SlideOvershoot
		: 0.f;
	if (Overshoot > 0.f && Layer.ValueKeys.Keys.Num() == 0)
	{
		FNumberFormattingOptions NumberFormat;
		NumberFormat.SetUseGrouping(false).SetMaximumFractionalDigits(2);
		return FText::Format(LOCTEXT("OvershootEasingName", "{0} + Overshoot {1}"), EasingName, FText::AsNumber(Overshoot, &NumberFormat));
	}

	return EasingName;
}

// -----------------------------------------------------------------------------------------------------
// Tooltips
// -----------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SWidgetAnimTimeline::MakeLayerToolTip(int32 LayerIndex) const
{
	using namespace WidgetAnimToolTip;

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	if (!Layers || !Layers->IsValidIndex(LayerIndex) || !Spans.IsValidIndex(LayerIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	const FVector2f Span = Spans[LayerIndex];
	const TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// What it does - its length and easing are on the track already, so they aren't repeated here.
	FRows Rows;

	// What it does - each type its own settings, a start value in green and an end value in red.
	switch (Layer.AnimType)
	{
	case EWidgetAnimType::Fade:
		Rows.Add(TEXT("Opacity"), StartToEnd(Number(Layer.StartOpacity), Number(Layer.EndOpacity)));
		break;

	case EWidgetAnimType::Scale:
		Rows.Add(TEXT("Scale"), StartToEnd(Number(Layer.StartScale), Number(Layer.EndScale)));
		if (Layer.Overshoot > KINDA_SMALL_NUMBER)
		{
			Rows.Add(TEXT("Overshoot"), Number(Layer.Overshoot));
		}
		break;

	case EWidgetAnimType::Slide:
	{
		const UEnum* DirectionEnum = StaticEnum<EWidgetSlideDirection>();
		Rows.Add(TEXT("From"), DirectionEnum ? DirectionEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.SlideFromDirection)).ToString() : FString());
		Rows.Add(TEXT("Distance"), Number(Layer.SlideDistance));
		if (Layer.SlideOvershoot > KINDA_SMALL_NUMBER)
		{
			Rows.Add(TEXT("Overshoot"), Number(Layer.SlideOvershoot));
		}
		break;
	}

	case EWidgetAnimType::Rotate:
		Rows.Add(TEXT("Angle"), StartToEnd(Angle(Layer.StartAngle), Angle(Layer.EndAngle)));
		break;

	case EWidgetAnimType::Pulse:
		Rows.Add(TEXT("Scale Amount"), FString::Printf(TEXT("%s  (\u00B1%s%%)"), *Number(Layer.PulseScaleAmount, 3), *Number(Layer.PulseScaleAmount * 100.f, 1)));
		break;

	case EWidgetAnimType::MaterialScalar:
		Rows.Add(TEXT("Parameter"), Layer.ScalarParameterName.IsNone() ? FString(TEXT("(none)")) : Layer.ScalarParameterName.ToString());
		Rows.Add(TEXT("Value"), StartToEnd(Number(Layer.StartScalarValue), Number(Layer.EndScalarValue)));
		break;

	case EWidgetAnimType::MaterialVector:
	{
		Rows.Add(TEXT("Parameter"), Layer.VectorParameterName.IsNone() ? FString(TEXT("(none)")) : Layer.VectorParameterName.ToString());

		auto ColorChip = [](const FLinearColor& Color, const FLinearColor& TextColor)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))[ Swatch(Color) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Text(TEXT("#") + Color.ToFColor(true).ToHex().Left(6), TextColor, ValueFont()) ];
		};

		Rows.Add(TEXT("Value"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ColorChip(Layer.StartVectorValue, Palette::TooltipStartValue) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 0.f))[ Arrow() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ColorChip(Layer.EndVectorValue, Palette::TooltipEndValue) ]);
		break;
	}

	case EWidgetAnimType::Custom:
		// A registered type's start and end, under the name it gives its values. Nothing to show for one whose module
		// isn't loaded - the blurb below says so.
		if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
		{
			Rows.Add(Evaluator->GetTrackValueLabel(Layer.TypeParams).ToString(),
				StartToEnd(Number(Evaluator->GetEndpointValue(Layer.TypeParams, true)), Number(Evaluator->GetEndpointValue(Layer.TypeParams, false))));
		}
		break;

	default:
		break;
	}

	// Keys between the start and the end: what the track passes through on the way.
	if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.Keys.Num() > 0)
	{
		const TSharedRef<SHorizontalBox> Through = SNew(SHorizontalBox);
		for (int32 Point = 1; Point <= Layer.ValueKeys.Keys.Num(); ++Point)
		{
			Through->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(Point > 1 ? 8.f : 0.f, 0.f, 0.f, 0.f))
			[
				KeyValue(Layer, Point, Palette::TooltipValue)
			];
		}
		Rows.Add(TEXT("Through"), Through);
	}

	// How it moves, always - a track too short to show its easing on its section still says it here. Segments
	// shaped one by one are each named, in order; one left at Track Easing by the track's own easing.
	{
		TArray<FString> Curves;
		if (UsesSegmentCurves(LayerIndex) && HasSegmentCurves(LayerIndex))
		{
			for (int32 Segment = 0; const FWidgetAnimSegmentCurve* Curve = GetSegmentCurve(LayerIndex, Segment); ++Segment)
			{
				Curves.Add(Curve->IsTrackEasing() ? GetEasingName(Layer).ToString() : GetSegmentCurveName(*Curve).ToString());
			}
		}
		else
		{
			Curves.Add(GetEasingName(Layer).ToString());
		}
		Rows.Add(Curves.Num() > 1 ? TEXT("Curves") : TEXT("Curve"), FString::Join(Curves, TEXT(", ")));
	}

	// A stagger: what it fans out to, how its items are spaced and - when the preview has counted them - how many
	// there are there, with a note when it has nothing to fan out to or can't move what it would.
	if (const FWidgetAnimExpander* Expander = Layer.Expander.GetPtr<FWidgetAnimExpander>())
	{
		const FString Summary = Expander->GetSummary().IsEmpty() ? Layer.Expander.GetScriptStruct()->GetDisplayNameText().ToString() : Expander->GetSummary();
		FString StaggerText = FString::Printf(TEXT("%s, %s"), *Summary, *Expander->DescribeSpacing());
		const int32 NumItems = GetLayerFanCount(LayerIndex);
		if (NumItems != INDEX_NONE)
		{
			StaggerText += FString::Printf(TEXT(" (%d on the preview)"), NumItems);
		}
		Rows.Add(TEXT("Stagger"), StaggerText);

		if (Expander->GathersParts() && !UWidgetAnimLibrary::TypeMovesParts(Layer.AnimType))
		{
			Rows.Add(TEXT("Stagger Note"), WidgetAnimTips::StaggerPartsTypeNote().ToString());
		}
		else if (NumItems == 0)
		{
			Rows.Add(TEXT("Stagger Note"), (Expander->GathersParts() ? WidgetAnimTips::StaggerNoTextNote() : WidgetAnimTips::StaggerNoChildrenNote()).ToString());
		}
		else if (Layer.IsValueDriven())
		{
			Rows.Add(TEXT("Stagger Note"), WidgetAnimTips::StaggerValueNote().ToString());
		}
	}

	// A value-driven track: the value it follows, its type and how it travels to each new one - and, on a Transform,
	// what the value moves.
	if (Layer.IsValueDriven())
	{
		const UEnum* KindEnum = StaticEnum<EWidgetAnimValueKind>();
		const UEnum* SmoothingEnum = StaticEnum<EWidgetAnimValueSmoothing>();
		Rows.Add(TEXT("Value Type"), KindEnum ? KindEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.GetEffectiveValueKind())).ToString() : FString());
		Rows.Add(TEXT("Smoothing"), SmoothingEnum ? SmoothingEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.ValueSmoothing)).ToString() : FString());

		// Where the value comes from: its Value Input, or gameplay.
		const FWidgetAnimInputSource* Input = Layer.ValueInput.GetPtr<FWidgetAnimInputSource>();
		const FString InputSummary = Input ? Input->GetSummary() : FString();
		Rows.Add(TEXT("Input"), Input
			? (InputSummary.IsEmpty() ? Layer.ValueInput.GetScriptStruct()->GetDisplayNameText().ToString() : InputSummary)
			: FString(TEXT("None (set from gameplay)")));

		// An input giving another kind of value than the track takes still reaches it, only not all of it: say how.
		if (Input)
		{
			const EWidgetAnimValueKind Gives = Input->GetValueKind();
			const EWidgetAnimValueKind Takes = Layer.GetEffectiveValueKind();
			const bool bTakesOneNumber = Takes == EWidgetAnimValueKind::Float || Takes == EWidgetAnimValueKind::Integer;
			if (Gives == EWidgetAnimValueKind::Vector2D && bTakesOneNumber)
			{
				Rows.Add(TEXT("Input Note"), TEXT("The input gives X and Y; this track takes one number, so only X reaches it."));
			}
			else if (Gives == EWidgetAnimValueKind::Float && !bTakesOneNumber)
			{
				Rows.Add(TEXT("Input Note"), TEXT("The input gives one number; every channel of this track gets it."));
			}
		}
		if (Layer.AnimType == EWidgetAnimType::Transform)
		{
			const UEnum* DrivesEnum = StaticEnum<EWidgetAnimTransformValueTarget>();
			Rows.Add(TEXT("Drives"), DrivesEnum ? DrivesEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.ValueDrives)).ToString() : FString());
		}
	}

	// A Transform layer: how many keys, then what they change - only the parts of the pose that move away from
	// the identity at some key, one column each, one row per key. Many keys show the first and last few.
	TSharedPtr<SWidget> KeyTable;
	FString KeyNote;
	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		const TArray<FWidgetTransformKeyframe>& Keys = Layer.Transform.Keys;
		TArray<float> KeyTimes;
		GetKeyTimes(LayerIndex, KeyTimes);
		const int32 NumKeys = Keys.Num();

		Rows.Add(TEXT("Keys"), FString::FromInt(NumKeys));
		if (Layer.Transform.Curve.ExternalCurve)
		{
			Rows.Add(TEXT("Timed by"), Layer.Transform.Curve.ExternalCurve->GetName());
		}

		TArray<FChannel> Shown;
		for (const FChannel& Channel : TransformChannels(Layer.Transform.IsAnimatingShape()))
		{
			if (Keys.ContainsByPredicate([&Channel](const FWidgetTransformKeyframe& Key) { return !Channel.IsDefault(Key); }))
			{
				Shown.Add(Channel);
			}
		}

		if (NumKeys > 0 && Shown.Num() > 0)
		{
			const TSharedRef<SGridPanel> Table = SNew(SGridPanel);
			int32 Row = 0;

			auto Cell = [&Table](int32 Column, int32 InRow, const TSharedRef<SWidget>& Content, bool bLast)
			{
				Table->AddSlot(Column, InRow)
					.Padding(FMargin(0.f, 1.f, bLast ? 0.f : 14.f, 1.f))
					.VAlign(VAlign_Center)
					[
						Content
					];
			};

			// Heading row.
			Cell(0, Row, Text(TEXT("Key"), Palette::TooltipTableHeader, SmallFont()), false);
			Cell(1, Row, Text(TEXT("Time"), Palette::TooltipTableHeader, SmallFont()), Shown.Num() == 0);
			for (int32 Column = 0; Column < Shown.Num(); ++Column)
			{
				Cell(Column + 2, Row, Text(Shown[Column].Name, Palette::TooltipTableHeader, SmallFont()), Column == Shown.Num() - 1);
			}
			++Row;

			// Which keys get a row: all of them, or - past the most there is room for - the first and last few,
			// with how many are left out between.
			const int32 MaxRows = FMath::Max(Palette::TooltipMaxKeyRows, 2);
			TArray<int32> Listed;
			if (NumKeys <= MaxRows)
			{
				for (int32 Key = 0; Key < NumKeys; ++Key)
				{
					Listed.Add(Key);
				}
			}
			else
			{
				const int32 Head = MaxRows / 2;
				const int32 Tail = MaxRows - Head;
				for (int32 Key = 0; Key < Head; ++Key)
				{
					Listed.Add(Key);
				}
				Listed.Add(INDEX_NONE);
				for (int32 Key = NumKeys - Tail; Key < NumKeys; ++Key)
				{
					Listed.Add(Key);
				}
			}

			for (const int32 Key : Listed)
			{
				if (Key == INDEX_NONE)
				{
					Cell(0, Row, Text(FString::Printf(TEXT("\u2026  %d more keys"), NumKeys - MaxRows), Palette::TooltipLabel, SmallFont()), false);
					++Row;
					continue;
				}

				const FWidgetTransformKeyframe& Keyframe = Keys[Key];
				const float Time = KeyTimes.IsValidIndex(Key) ? KeyTimes[Key] : Keyframe.Time;

				Cell(0, Row, Text(FString::FromInt(Key + 1), Palette::TooltipLabel, ValueFont()), false);
				Cell(1, Row, Text(Seconds(Span.X + Time), KeyValueColor(Key, NumKeys, false), ValueFont()), false);
				for (int32 Column = 0; Column < Shown.Num(); ++Column)
				{
					const FChannel& Channel = Shown[Column];
					Cell(Column + 2, Row, Text(Channel.Format(Keyframe), KeyValueColor(Key, NumKeys, Channel.IsDefault(Keyframe)), ValueFont()), Column == Shown.Num() - 1);
				}
				++Row;
			}

			KeyTable = Table;
		}
		else if (NumKeys > 0)
		{
			KeyNote = WidgetAnimTips::IdentityKeysNote().ToString();
		}
	}

	if (!Rows.IsEmpty())
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Rows.Widget()
		];
	}

	if (KeyTable.IsValid())
	{
		if (!Rows.IsEmpty())
		{
			Body->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0.f, 7.f))
			[
				Divider()
			];
		}

		Body->AddSlot()
		.AutoHeight()
		[
			KeyTable.ToSharedRef()
		];
	}

	if (!KeyNote.IsEmpty())
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(KeyNote)
		];
	}

	if (Layer.IsValueDriven())
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note((Layer.AnimType == EWidgetAnimType::Pulse ? WidgetAnimTips::ValueDrivenPulseNote() : WidgetAnimTips::ValueDrivenTrackNote()).ToString())
		];
	}

	if (IsLayerHidden(LayerIndex))
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(WidgetAnimTips::HiddenTrackNote().ToString())
		];
	}

	if (IsLayerLocked(LayerIndex))
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(WidgetAnimTips::LockedTrackNote().ToString())
		];
	}

	// A line on what the less obvious types do - a Fade or a Scale says what it does by its name, a Pulse doesn't.
	FText Blurb;
	switch (Layer.AnimType)
	{
	case EWidgetAnimType::Pulse:          Blurb = WidgetAnimTips::PulseBlurbTooltip(); break;
	case EWidgetAnimType::MaterialScalar: Blurb = WidgetAnimTips::MaterialScalarBlurbTooltip(); break;
	case EWidgetAnimType::MaterialVector: Blurb = WidgetAnimTips::MaterialVectorBlurbTooltip(); break;
	case EWidgetAnimType::Transform:      Blurb = WidgetAnimTips::TransformBlurbTooltip(); break;
	case EWidgetAnimType::Custom:
	{
		// A registered type describes itself - it may come from a module this plugin knows nothing about.
		const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
		Blurb = Evaluator ? Evaluator->GetTrackDescription(Layer.TypeParams)
			: Layer.TypeParams.IsValid() ? WidgetAnimTips::UnregisteredTypeBlurbTooltip()
			: WidgetAnimTips::NoRegisteredTypeBlurbTooltip();
		break;
	}
	default:                              break;
	}

	if (!Blurb.IsEmpty())
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(Blurb.ToString())
		];
	}

	// A Blueprint type's cost: over budget this session, or bound to run Blueprint on every frame it plays. See
	// UWidgetAnimRuntimeSettings, where the budget and whether to warn at all are set.
	if (const FWidgetAnimBlueprintTypeParams* BlueprintParams = Layer.AnimType == EWidgetAnimType::Custom ? Layer.TypeParams.GetPtr<FWidgetAnimBlueprintTypeParams>() : nullptr)
	{
		const UWidgetAnimBlueprintType* TypeObject = BlueprintParams->GetTypeObject();
		if (TypeObject && UWidgetAnimRuntimeSettings::Get().bWarnAboutExpensiveBlueprintTypes)
		{
			float MsPerFrame = 0.f;
			float CallFrameShare = 0.f;
			if (WidgetAnimBlueprintTypeCost::IsFlagged(TypeObject->GetClass()) && WidgetAnimBlueprintTypeCost::GetLastWindow(TypeObject->GetClass(), MsPerFrame, CallFrameShare))
			{
				Body->AddSlot()
				.AutoHeight()
				[
					Warning(FText::Format(WidgetAnimTips::BlueprintTypeOverBudgetWarning(),
						FText::FromString(FString::Printf(TEXT("%.3f"), MsPerFrame)),
						FText::AsNumber(FMath::RoundToInt(CallFrameShare * 100.f))))
				];
			}
			else if (TypeObject->QuantizeStep <= 0.0)
			{
				Body->AddSlot()
				.AutoHeight()
				[
					Warning(WidgetAnimTips::BlueprintTypeNoQuantizeWarning())
				];
			}
		}
	}

	// Animate Shape acts through a Shape Box - see UWidgetAnimLibrary::ResolveShapeBox - and does nothing without one.
	if (Layer.AnimType == EWidgetAnimType::Transform)
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Warning(WidgetAnimTips::TransformShapeBoxWarningTooltip())
		];
	}

	return Frame(
		IsReadOnly() || IsLayerLocked(LayerIndex) || IsLayerHidden(LayerIndex) ? GetLockedColor(GetLayerTypeColor(Layer)) : GetLayerTypeColor(Layer),
		GetLayerTypeName(Layer).ToString(),
		GetLayerWhereName(LayerIndex).ToString(),
		GetLayerTimeRange(LayerIndex).ToString(),
		Body);
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeKeyToolTip(int32 LayerIndex, int32 KeyIndex) const
{
	using namespace WidgetAnimToolTip;

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	if (!Layers || !Layers->IsValidIndex(LayerIndex) || !Spans.IsValidIndex(LayerIndex))
	{
		return SNullWidget::NullWidget;
	}

	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];

	// A key of a track that travels from a start value to an end value: its first key is the start, its last the
	// end, and each between is a value on the way.
	if (Layer.AnimType != EWidgetAnimType::Transform)
	{
		TArray<float> PointTimes;
		GetKeyTimes(LayerIndex, PointTimes);
		if (!PointTimes.IsValidIndex(KeyIndex))
		{
			return MakeLayerToolTip(LayerIndex);
		}

		const int32 NumPoints = PointTimes.Num();
		const float PointTime = PointTimes[KeyIndex];

		FRows Where;
		Where.Add(TEXT("In the track"), FString::Printf(TEXT("%.3f s"), PointTime));
		Where.Add(TEXT("In the animation"), FString::Printf(TEXT("%.3f s"), Spans[LayerIndex].X + PointTime));

		FRows Values;
		Values.Add(KeyValueName(Layer.AnimType), KeyValue(Layer, KeyIndex, KeyValueColor(KeyIndex, NumPoints, false)));
		if (KeyIndex < NumPoints - 1)
		{
			const FWidgetAnimSegmentCurve* Curve = GetSegmentCurve(LayerIndex, KeyIndex);
			Values.Add(TEXT("To the next key"), Layer.IsSpringTimed()
				? FString(TEXT("Spring"))
				: (Curve ? GetSegmentCurveName(*Curve).ToString() : FString()));
		}

		const TSharedRef<SVerticalBox> PointBody = SNew(SVerticalBox);
		PointBody->AddSlot().AutoHeight()[ Where.Widget() ];
		PointBody->AddSlot().AutoHeight().Padding(FMargin(0.f, 7.f))[ Divider() ];
		PointBody->AddSlot().AutoHeight()[ Values.Widget() ];

		if (Layer.IsSpringTimed() && KeyIndex > 0 && KeyIndex < NumPoints - 1)
		{
			PointBody->AddSlot().AutoHeight()[ Note(WidgetAnimTips::SpringKeyPointNote().ToString()) ];
		}

		const FString PointTitle = FString::Printf(TEXT("Key %d of %d"), KeyIndex + 1, NumPoints);
		const FString PointWhere = KeyIndex == 0 ? TEXT("Start") : KeyIndex == NumPoints - 1 ? TEXT("End") : GetLayerTypeName(Layer).ToString();
		const FString PointSubtitle = FString::Printf(TEXT("%s  \u00B7  %s"), *PointWhere, *GetLayerWhereName(LayerIndex).ToString());

		return Frame(GetLayerTypeColor(Layer), PointTitle, PointSubtitle, Seconds(Spans[LayerIndex].X + PointTime), PointBody);
	}

	const TArray<FWidgetTransformKeyframe>& Keys = Layer.Transform.Keys;
	if (!Keys.IsValidIndex(KeyIndex))
	{
		return MakeLayerToolTip(LayerIndex);
	}

	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	const FWidgetTransformKeyframe& Keyframe = Keys[KeyIndex];
	const float LocalTime = KeyTimes.IsValidIndex(KeyIndex) ? KeyTimes[KeyIndex] : Keyframe.Time;
	const int32 NumKeys = Keys.Num();

	FRows Rows;
	Rows.Add(TEXT("In the track"), FString::Printf(TEXT("%.3f s"), LocalTime));
	Rows.Add(TEXT("In the animation"), FString::Printf(TEXT("%.3f s"), Spans[LayerIndex].X + LocalTime));
	if (KeyIndex < NumKeys - 1)
	{
		Rows.Add(TEXT("To the next key"), GetSegmentCurveName(Keyframe.CurveToNext).ToString());
	}

	// Only what this key's pose changes from the identity - the start's in green, the end's in red.
	FRows Pose;
	for (const FChannel& Channel : TransformChannels(Layer.Transform.IsAnimatingShape()))
	{
		if (!Channel.IsDefault(Keyframe))
		{
			Pose.Add(Channel.Name, Text(Channel.Format(Keyframe), KeyValueColor(KeyIndex, NumKeys, false), ValueFont()));
		}
	}

	const TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot()
	.AutoHeight()
	[
		Rows.Widget()
	];

	Body->AddSlot()
	.AutoHeight()
	.Padding(FMargin(0.f, 7.f))
	[
		Divider()
	];

	Body->AddSlot()
	.AutoHeight()
	[
		Pose.IsEmpty()
			? Note(WidgetAnimTips::IdentityPoseNote().ToString())
			: Pose.Widget()
	];

	const FString Title = FString::Printf(TEXT("Key %d of %d"), KeyIndex + 1, NumKeys);
	const FString Where = KeyIndex == 0 ? TEXT("First key") : KeyIndex == NumKeys - 1 ? TEXT("Last key") : GetLayerTypeName(Layer).ToString();
	const FString Subtitle = FString::Printf(TEXT("%s  \u00B7  %s"), *Where, *GetLayerWhereName(LayerIndex).ToString());

	return Frame(GetLayerTypeColor(Layer), Title, Subtitle, Seconds(Spans[LayerIndex].X + LocalTime), Body);
}

// -----------------------------------------------------------------------------------------------------
// Value Watcher
// -----------------------------------------------------------------------------------------------------

FText SWidgetAnimTimeline::GetWatchedValueName(EWatchedValue Value)
{
	switch (Value)
	{
	case EWatchedValue::Opacity:     return LOCTEXT("WatchOpacity", "Opacity");
	case EWatchedValue::Translation: return LOCTEXT("WatchTranslation", "Translation");
	case EWatchedValue::Scale:       return LOCTEXT("WatchScale", "Scale");
	case EWatchedValue::Shear:       return LOCTEXT("WatchShear", "Shear");
	case EWatchedValue::Rotation:    return LOCTEXT("WatchRotation", "Rotation");
	default:                         return FText::GetEmpty();
	}
}

UWidget* SWidgetAnimTimeline::GetWatchedWidget(FName Target) const
{
	if (!Source.IsValid())
	{
		return nullptr;
	}

	if (!Target.IsNone())
	{
		return Source->ResolvePreviewTarget(Target);
	}

	// No path is the widget the animation plays on when a group names none: the preview itself - or, in a
	// builder, its preview image, which is where a builder plays such a group.
	if (Source->IsBuilder())
	{
		if (UWidget* PreviewImage = Source->ResolvePreviewTarget(TEXT("PreviewImage")))
		{
			return PreviewImage;
		}
	}
	return Source->GetPreviewRoot();
}

bool SWidgetAnimTimeline::ReadWatchedValue(const FWatch& Watch, FVector2D& OutValue) const
{
	const UWidget* Widget = Watch.bPicked ? GetWatchedWidget(Watch.Target) : nullptr;
	if (!Widget)
	{
		return false;
	}

	const FWidgetTransform& Transform = Widget->GetRenderTransform();
	switch (Watch.Value)
	{
	case EWatchedValue::Opacity:     OutValue = FVector2D(Widget->GetRenderOpacity(), 0.0); break;
	case EWatchedValue::Translation: OutValue = Transform.Translation; break;
	case EWatchedValue::Scale:       OutValue = Transform.Scale; break;
	case EWatchedValue::Shear:       OutValue = Transform.Shear; break;
	case EWatchedValue::Rotation:    OutValue = FVector2D(Transform.Angle, 0.0); break;
	default:                         return false;
	}
	return true;
}

FText SWidgetAnimTimeline::FormatWatchedValue(const FWatch& Watch) const
{
	if (!Watch.bPicked)
	{
		return LOCTEXT("WatchNothing", "-");
	}

	FVector2D Value = FVector2D::ZeroVector;
	if (!ReadWatchedValue(Watch, Value))
	{
		return LOCTEXT("WatchNoPreview", "No preview of that widget");
	}

	// Three decimals where a small overshoot would otherwise read as having already arrived - a scale of 1.004 is
	// still moving, and two decimals showed it as 1.00.
	switch (Watch.Value)
	{
	case EWatchedValue::Opacity:
		return FText::FromString(FString::Printf(TEXT("%.3f"), Value.X));

	case EWatchedValue::Rotation:
		return FText::FromString(FString::Printf(TEXT("%.1f\u00B0"), Value.X));

	case EWatchedValue::Scale:
		return FText::FromString(FMath::IsNearlyEqual(Value.X, Value.Y, 1.e-4)
			? FString::Printf(TEXT("%.3f"), Value.X)
			: FString::Printf(TEXT("(%.3f, %.3f)"), Value.X, Value.Y));

	default:
		return FText::FromString(FString::Printf(TEXT("(%.1f, %.1f)"), Value.X, Value.Y));
	}
}

namespace
{
	/** Whether a track moves one of the values the Value Watcher reads - not just holds it somewhere. */
	bool DoesLayerChangeValue(const FWidgetAnimLayer& Layer, SWidgetAnimTimeline::EWatchedValue Value)
	{
		using EWatched = SWidgetAnimTimeline::EWatchedValue;

		// A Transform track changes a value when its keys don't all agree on it.
		auto TransformVaries = [&Layer](TFunctionRef<FVector2D(const FWidgetTransform&)> Channel)
		{
			const TArray<FWidgetTransformKeyframe>& Keys = Layer.Transform.Keys;
			for (int32 Key = 1; Key < Keys.Num(); ++Key)
			{
				if (!Channel(Keys[Key].Transform).Equals(Channel(Keys[0].Transform), 1.e-4))
				{
					return true;
				}
			}
			return false;
		};

		// A keyed track changes its value when its start, keys and end don't all agree - a Fade from 1 to 1 through
		// a key of 0 dips and comes back.
		const bool bPointsVary = !FMath::IsNearlyEqual(WidgetAnimKeyPoints::GetMinValue(Layer), WidgetAnimKeyPoints::GetMaxValue(Layer));

		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:
			return Value == EWatched::Opacity && bPointsVary;

		case EWidgetAnimType::Scale:
			return Value == EWatched::Scale && (bPointsVary || (Layer.Overshoot > KINDA_SMALL_NUMBER && !Layer.IsSpringTimed() && Layer.ValueKeys.Keys.Num() == 0));

		case EWidgetAnimType::Pulse:
			return Value == EWatched::Scale && Layer.PulseScaleAmount > KINDA_SMALL_NUMBER;

		case EWidgetAnimType::Slide:
			return Value == EWatched::Translation && bPointsVary;

		case EWidgetAnimType::Rotate:
			return Value == EWatched::Rotation && bPointsVary;

		case EWidgetAnimType::Transform:
			switch (Value)
			{
			case EWatched::Translation: return TransformVaries([](const FWidgetTransform& T) { return T.Translation; });
			case EWatched::Scale:       return TransformVaries([](const FWidgetTransform& T) { return T.Scale; });
			case EWatched::Shear:       return TransformVaries([](const FWidgetTransform& T) { return T.Shear; });
			case EWatched::Rotation:    return TransformVaries([](const FWidgetTransform& T) { return FVector2D(T.Angle, 0.0); });
			default:                    return false;
			}

		default:
			return false;
		}
	}
}

bool SWidgetAnimTimeline::IsWatchChanging(const FWatch& Watch) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Watch.bPicked || !Groups || !Layers)
	{
		return false;
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	const float Now = GetPlayheadSeconds();
	constexpr float Epsilon = 0.0005f;

	// Changing: a track of a group playing on the watched widget, that moves the watched value, is running
	// where the playhead is - paused on it included.
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num() && Spans.IsValidIndex(LayerIndex); ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const int32 GroupIndex = GetLayerGroup(LayerIndex);
		if (!Groups->IsValidIndex(GroupIndex) || (*Groups)[GroupIndex].PreviewTarget != Watch.Target || Layer.bHiddenInEditor)
		{
			continue;
		}

		const FVector2f Span = Spans[LayerIndex];
		if (Now >= Span.X - Epsilon && Now < Span.X + Span.Y - Epsilon && DoesLayerChangeValue(Layer, Watch.Value))
		{
			return true;
		}
	}
	return false;
}

void SWidgetAnimTimeline::RebuildWatchRows()
{
	if (!WatchRows.IsValid())
	{
		return;
	}

	WatchRows->ClearChildren();

	auto SmallText = [](const TAttribute<FText>& Text)
	{
		return SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text(Text);
	};

	for (int32 WatchIndex = 0; WatchIndex < Watches.Num(); ++WatchIndex)
	{
		// Which widget: any in this Widget Blueprint's preview, as the Layer Group target dropdown lists them.
		const TSharedRef<SWidget> WidgetPicker = SNew(SComboButton)
			.ContentPadding(FMargin(4.f, 1.f))
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::WatchWidgetTooltip()))
			.OnGetMenuContent_Lambda([this, WatchIndex]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				if (Source.IsValid())
				{
					for (const FString& Option : Source->GetTargetOptions())
					{
						const FName Path = Option == TEXT("None") ? FName(NAME_None) : FName(*Option);
						Menu.AddMenuEntry(
							Path.IsNone() ? LOCTEXT("WatchThisWidget", "(this widget)") : FText::FromString(Option),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([this, WatchIndex, Path]()
							{
								if (Watches.IsValidIndex(WatchIndex))
								{
									Watches[WatchIndex].Target = Path;
									Watches[WatchIndex].bPicked = true;
								}
							})));
					}
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SmallText(TAttribute<FText>::CreateLambda([this, WatchIndex]()
				{
					if (!Watches.IsValidIndex(WatchIndex) || !Watches[WatchIndex].bPicked)
					{
						return LOCTEXT("WatchPickWidget", "Pick a widget...");
					}
					return Watches[WatchIndex].Target.IsNone() ? LOCTEXT("WatchThisWidget", "(this widget)") : FText::FromName(Watches[WatchIndex].Target);
				}))
			];

		// Which value of it.
		const TSharedRef<SWidget> ValuePicker = SNew(SComboButton)
			.ContentPadding(FMargin(4.f, 1.f))
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::WatchValueTooltip()))
			.OnGetMenuContent_Lambda([this, WatchIndex]()
			{
				FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
				for (uint8 Index = 0; Index <= static_cast<uint8>(EWatchedValue::Rotation); ++Index)
				{
					const EWatchedValue Value = static_cast<EWatchedValue>(Index);
					Menu.AddMenuEntry(
						GetWatchedValueName(Value),
						FText::GetEmpty(),
						FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([this, WatchIndex, Value]()
						{
							if (Watches.IsValidIndex(WatchIndex))
							{
								Watches[WatchIndex].Value = Value;
							}
						})));
				}
				return Menu.MakeWidget();
			})
			.ButtonContent()
			[
				SmallText(TAttribute<FText>::CreateLambda([this, WatchIndex]()
				{
					return Watches.IsValidIndex(WatchIndex) ? GetWatchedValueName(Watches[WatchIndex].Value) : FText::GetEmpty();
				}))
			];

		// The value itself, read off the preview every frame: plain while nothing moves it, red while a track is.
		const TSharedRef<SWidget> Readout = SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity_Lambda([this, WatchIndex]()
			{
				return Watches.IsValidIndex(WatchIndex) && IsWatchChanging(Watches[WatchIndex])
					? FSlateColor(WidgetAnimTimeline::Palette::WatcherChanging)
					: FSlateColor::UseForeground();
			})
			.Text_Lambda([this, WatchIndex]()
			{
				return Watches.IsValidIndex(WatchIndex) ? FormatWatchedValue(Watches[WatchIndex]) : FText::GetEmpty();
			});

		WatchRows->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(0.45f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				WidgetPicker
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.25f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
			[
				ValuePicker
			]

			+ SHorizontalBox::Slot()
			.FillWidth(0.3f)
			.VAlign(VAlign_Center)
			[
				Readout
			]

			// Taken out - the last row stays, only emptied, so there is always one to pick a widget in.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				WidgetAnimTimeline::MakeIconButton({ "Icons.X", "Icons.Delete" }, LOCTEXT("WatchRemove", "x"),
					WidgetAnimTips::WatchRemoveTooltip(),
					FOnClicked::CreateLambda([this, WatchIndex]()
					{
						if (Watches.Num() > 1 && Watches.IsValidIndex(WatchIndex))
						{
							Watches.RemoveAt(WatchIndex);
						}
						else if (Watches.IsValidIndex(WatchIndex))
						{
							Watches[WatchIndex] = FWatch();
						}
						RebuildWatchRows();
						return FReply::Handled();
					}), 12.f)
			]
		];
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeValueWatcher()
{
	using namespace WidgetAnimTimeline;

	if (Watches.Num() == 0)
	{
		Watches.AddDefaulted();
	}

	bool bSavedExpanded = true;
	if (GConfig->GetBool(RowHeightConfigSection, WatcherExpandedConfigKey, bSavedExpanded, GEditorPerProjectIni))
	{
		bWatcherExpanded = bSavedExpanded;
	}

	// The header folds the watcher open and shut, and adds a row.
	const TSharedRef<SWidget> HeaderButtons = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 1.f))
			.Visibility_Lambda([this]() { return bWatcherExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::WatchAddTooltip()))
			.OnClicked_Lambda([this]()
			{
				Watches.AddDefaulted();
				RebuildWatchRows();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
				[
					MakeFixedIcon(PickBrush({ "Icons.Plus" }), FSlateColor::UseSubduedForeground(), 12.f)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("WatchAdd", "Value"))
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 1.f))
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::WatchFoldTooltip()))
			.OnClicked_Lambda([this]()
			{
				bWatcherExpanded = !bWatcherExpanded;
				GConfig->SetBool(RowHeightConfigSection, WatcherExpandedConfigKey, bWatcherExpanded, GEditorPerProjectIni);
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image_Lambda([this]()
				{
					return bWatcherExpanded
						? PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" })
						: PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" });
				})
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];

	const TSharedRef<SWidget> Watcher = SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("ValueWatcherHeader", "Value Watcher"), HeaderButtons)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility_Lambda([this]() { return bWatcherExpanded ? EVisibility::Visible : EVisibility::Collapsed; })
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(10.f, 6.f))
			[
				SAssignNew(WatchRows, SVerticalBox)
			]
		];

	RebuildWatchRows();
	return Watcher;
}

// -----------------------------------------------------------------------------------------------------
// Undo
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::PostUndo(bool bSuccess)
{
	// An undo puts the edited object back without telling it, so previews would keep playing what was
	// undone. Told here, as if the groups had just been edited.
	NotifyGroupsChanged(EPropertyChangeType::Unspecified);
	Selection = Validate(Selection);
	RebuildRows();
	RefreshDetails();
}

void SWidgetAnimTimeline::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

#undef LOCTEXT_NAMESPACE