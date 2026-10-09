// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimTimeline.h"

#include "SWidgetAnimAnchoredTip.h"

#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimLayerEditProxy.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "CoreGlobals.h"
#include "Layout/Children.h"
#include "Widgets/SPanel.h"
#include "WidgetAnimPanelSource.h"
#include "WidgetAnimTargetOption.h"
#include "WidgetAnimScripting.h"
#include "WidgetAnimTrackPresets.h"
#include "WidgetAnimBuilderToolbar.h"
#include "SWidgetAnimScriptPanel.h"
#include "SWidgetAnimGraphEditor.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimBlueprintType.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimBlueprintCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimCue.h"
#include "NameTBD/WidgetAnimation/WidgetAnimExpander.h"
#include "NameTBD/WidgetAnimation/Text/WidgetAnimTextHost.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimCues.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimShapeTrack.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimDesignTrack.h"
#include "NameTBD/WidgetAnimation/WidgetDesignAnimation.h"
#include "NameTBD/Widgets/Shapes/ShapeWidget.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"
#include "NameTBD/WidgetAnimation/WidgetAnimRuntimeSettings.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"

#include "Algo/AllOf.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/Base64.h"
#include "Algo/Find.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Algo/StableSort.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
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
#include "Framework/Application/IInputProcessor.h"
#include "Containers/Ticker.h"
#include "Widgets/SWindow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "InstancedStructDetails.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "Rendering/DrawElements.h"
#include "Rendering/RenderingCommon.h"
#include "Rendering/SlateRenderer.h"
#include "ScopedTransaction.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Curves/CurveFloat.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Interfaces/IPluginManager.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditor.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "StatusBarSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "Misc/MessageDialog.h"
#include "HAL/PlatformProcess.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Views/SListView.h"
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
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "WidgetAnimNameField.h"
#include "IDetailGroup.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Misc/ScopeExit.h"
#include "WidgetAnimTypePill.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTimeline"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimScriptDock, Log, All);

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

	/** How far a folder's groups sit in from its row in the Animation Tracks list - the width of the folder's strip beside them. */
	constexpr float ListFolderIndent = 12.f;

	/** Height of the strips naming the Animation Layers list and the Track Editor. */
	constexpr float SectionHeaderHeight = 28.f;

	/** Header icons are drawn at this size whatever size their brush was authored at; the transport's larger. */
	constexpr float IconSize = 16.f;
	constexpr float TransportIconSize = 18.f;

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
	 * Transform or Shape Box, a Value Drives with only one axis while it takes a Vector 2D, and the fields of the other of
	 * the two - a Shape Box's settings on a Transform track, a transform's on a Shape Box track. A choice already made still shows as it is; one
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
		if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, AnimType))
		{
			// User-defined Track is not a type of its own to switch to - each registered type is added from the Add Track menu,
			// as itself - so it is offered only while every track edited already is one.
			const bool bAllRegistered = Algo::AllOf(Layers, [](const FWidgetAnimLayer* Layer) { return Layer->AnimType == EWidgetAnimType::Custom; });
			HideEnumValues(Child, StaticEnum<EWidgetAnimType>(), [bAllRegistered](int64 Value)
			{
				return bAllRegistered || static_cast<EWidgetAnimType>(Value) != EWidgetAnimType::Custom;
			});
		}
		else if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Timing))
		{
			HideEnumValues(Child, StaticEnum<EWidgetAnimTiming>(), [&Layers](int64 Value)
			{
				return Algo::AllOf(Layers, [Value](const FWidgetAnimLayer* Layer)
				{
					// A cue fires at points in time, so has nothing for a spring to carry: Tween or Value only.
					const EWidgetAnimTiming Timing = static_cast<EWidgetAnimTiming>(Value);
					const bool bCue = Layer->AnimType == EWidgetAnimType::Custom && WidgetAnimIsRegisteredCue(Layer->TypeParams);
					return WidgetAnimTimingAllowed(Layer->AnimType, Timing) && !(bCue && Timing == EWidgetAnimTiming::Spring);
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
				const EWidgetAnimTransformPart Part = WidgetAnimValue::GetTransformPart(Target, bX, bY);
				const bool bShapeField = WidgetAnimValue::IsShapePart(Part);
				const bool bWholePath = Part == EWidgetAnimTransformPart::WholePath;
				return Algo::AllOf(Layers, [Target, bShapeField, bWholePath](const FWidgetAnimLayer* Layer)
				{
					// A Transform track drives its transform's fields, a Shape Box track its outline's.
					const bool bFieldOfType = bWholePath || (bShapeField == (Layer->AnimType == EWidgetAnimType::ShapeBox));
					return (Layer->ValueOptions.Kind != EWidgetAnimValueKind::Vector2D || WidgetAnimValue::TransformTargetTakesTwoAxes(Target))
						&& bFieldOfType;
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

	/**
	 * A filled triangle through A, B and C - in Geometry's local space - drawn as one, from three vertices: a real triangle
	 * with straight edges at any size and DPI, where one stacked from a row of boxes per pixel came out stepped and lopsided.
	 */
	void MakeTriangle(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, const FVector2f& A, const FVector2f& B, const FVector2f& C, const FLinearColor& Color)
	{
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush());
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const FColor VertexColor = Color.ToFColor(/*bSRGB=*/true);

		TArray<FSlateVertex> Vertices;
		Vertices.Reserve(3);
		for (const FVector2f& Point : { A, B, C })
		{
			Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Point, FVector2f(0.5f, 0.5f), VertexColor));
		}
		TArray<SlateIndex> Indices = { 0, 1, 2 };
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId, Handle, Vertices, Indices, nullptr, 0, 0);
	}

	/**
	 * Where a vertical line and a head centered on it go - a start or end marker's triangle, the playhead's tag and notch -
	 * on whole physical pixels, in a geometry's local units. Worked out together so the line keeps the same width and stays
	 * on the head's middle at every zoom and DPI: placed each on its own, the two were rounded to the screen's pixels
	 * separately, and could land a pixel apart, or the line come out a pixel wider or narrower than set.
	 */
	struct FPixelColumn
	{
		float LineLeft = 0.f;
		float LineWidth = 1.f;
		float HeadLeft = 0.f;
		float HeadWidth = 1.f;
	};

	float PixelScale(const FGeometry& Geometry)
	{
		return FMath::Max(Geometry.GetAccumulatedLayoutTransform().GetScale(), UE_KINDA_SMALL_NUMBER);
	}

	/** CenterX is the line's middle; LineThickness and HeadWidth in local units, each made a whole number of pixels. */
	FPixelColumn SnapColumn(const FGeometry& Geometry, float CenterX, float LineThickness, float HeadWidth)
	{
		const float Scale = PixelScale(Geometry);
		const float Origin = Geometry.GetAccumulatedLayoutTransform().GetTranslation().X;
		const int32 LinePixels = FMath::Max(1, FMath::RoundToInt(LineThickness * Scale));
		int32 HeadPixels = FMath::Max(LinePixels, FMath::RoundToInt(HeadWidth * Scale));
		// As many pixels of head either side of the line - an odd number over would leave one side a pixel wider.
		if ((HeadPixels - LinePixels) % 2 != 0)
		{
			++HeadPixels;
		}
		const float LineLeftPixel = FMath::RoundToFloat(Origin + CenterX * Scale - LinePixels * 0.5f);
		const float HeadLeftPixel = LineLeftPixel - static_cast<float>((HeadPixels - LinePixels) / 2);

		FPixelColumn Column;
		Column.LineLeft = (LineLeftPixel - Origin) / Scale;
		Column.LineWidth = LinePixels / Scale;
		Column.HeadLeft = (HeadLeftPixel - Origin) / Scale;
		Column.HeadWidth = HeadPixels / Scale;
		return Column;
	}

	/** X, in local units, moved onto the nearest whole physical pixel. */
	float SnapX(const FGeometry& Geometry, float X)
	{
		const float Scale = PixelScale(Geometry);
		const float Origin = Geometry.GetAccumulatedLayoutTransform().GetTranslation().X;
		return (FMath::RoundToFloat(Origin + X * Scale) - Origin) / Scale;
	}

	/** Y, in local units, moved onto the nearest whole physical pixel. */
	float SnapY(const FGeometry& Geometry, float Y)
	{
		const float Scale = PixelScale(Geometry);
		const float Origin = Geometry.GetAccumulatedLayoutTransform().GetTranslation().Y;
		return (FMath::RoundToFloat(Origin + Y * Scale) - Origin) / Scale;
	}

	/**
	 * A head pointing down at its line - the plugin's smooth vector triangle, tinted; three plain vertices with their
	 * slanted edges traced anti-aliased if that file is missing.
	 */
	void MakeDownTriangle(FSlateWindowElementList& OutDrawElements, int32 LayerId, const FGeometry& Geometry, float Left, float Top, float Width, float Height, const FLinearColor& Color)
	{
		if (const FSlateBrush* Brush = FNameTBDStyle::FindIcon(FNameTBDStyle::RangeMarkerIcon))
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				Geometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(Left, Top))),
				Brush, ESlateDrawEffect::None, Color);
			return;
		}
		const FVector2f TopLeft(Left, Top);
		const FVector2f TopRight(Left + Width, Top);
		const FVector2f Point(Left + Width * 0.5f, Top + Height);
		MakeTriangle(OutDrawElements, LayerId, Geometry, TopLeft, TopRight, Point, Color);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, Geometry.ToPaintGeometry(),
			TArray<FVector2f>{ TopLeft, Point, TopRight }, ESlateDrawEffect::None, Color, /*bAntialias=*/true, 1.f);
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
	 * An animation timeline's icon, wherever one is named: the editor's own Blueprint Timeline icon - the closest thing
	 * it has to an animation kept inside a Blueprint - else a sequence's.
	 */
	const FSlateBrush* TimelineIcon()
	{
		static const FSlateBrush* Icon = PickBrush({ "GraphEditor.Timeline_16x", "ClassIcon.TimelineTemplate", "ClassIcon.LevelSequence" });
		return Icon;
	}

	/** A Widget Animation asset's icon, as the Content Browser shows it. */
	const FSlateBrush* AnimationAssetIcon()
	{
		const FSlateBrush* Own = FNameTBDStyle::Get().GetOptionalBrush("ClassIcon.WidgetAnimPresetAsset", nullptr, nullptr);
		return Own ? Own : PickBrush({ "ClassIcon.DataAsset" });
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
				.ContentPadding(Size > IconSize ? FMargin(4.f, 2.f) : FMargin(4.f, 2.f))
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

	/**
	 * A tick step for the ruler in frames at FrameRate - a whole number of frames, 1, 2, 5, 10, 15, 30, 60 and on - that leaves
	 * at least MinPixels between labels. In seconds, as every step is.
	 */
	float ChooseFrameTickStep(float PixelsPerSecond, float MinPixels, float FrameRate)
	{
		static const float FrameSteps[] = { 1.f, 2.f, 5.f, 10.f, 15.f, 30.f, 60.f, 120.f, 300.f, 600.f, 1800.f, 3600.f };
		const float Rate = FMath::Max(FrameRate, 1.f);
		for (const float Frames : FrameSteps)
		{
			if (Frames / Rate * PixelsPerSecond >= MinPixels)
			{
				return Frames / Rate;
			}
		}
		return 3600.f / Rate;
	}

	FString FormatSeconds(float Seconds, float Step, bool bPercent = false, float FrameRate = 0.f)
	{
		// In frames: a frame number, whole on a whole-frame step.
		if (FrameRate > 0.f)
		{
			return SWidgetAnimTimeline::FormatFrames(Seconds, FrameRate);
		}

		// On a percentage timeline 100% is one second: a tick a hundredth of a second apart is a whole percent.
		if (bPercent)
		{
			const float PercentStep = Step * 100.f;
			const int32 PercentDecimals = PercentStep >= 1.f ? 0 : (PercentStep >= 0.1f ? 1 : 2);
			return FString::Printf(TEXT("%.*f%%"), PercentDecimals, Seconds * 100.f);
		}
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

	/** Every kind of shape a Shape track can key, by name: Ellipse, Triangle, Star, N-gon, Custom Shape, and any Blueprint shapes. */
	void GetShapeKinds(TArray<UClass*>& OutKinds)
	{
		OutKinds.Reset();
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->IsChildOf(UShapeWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Hidden))
			{
				continue;
			}
			const FString Name = Class->GetName();
			if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			OutKinds.Add(Class);
		}
		OutKinds.Sort([](const UClass& A, const UClass& B) { return A.GetDisplayNameText().CompareTo(B.GetDisplayNameText()) < 0; });
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
	 * Every Blueprint class deriving from BaseClass in the project - a Blueprint track type, or a Blueprint cue - in name
	 * order, by its class's path: found through the asset registry, so ones never opened are listed too without loading
	 * them. Loaded only when one is picked. GetName names a loaded one by its class default object.
	 */
	void ForEachBlueprintClass(const UClass* BaseClass, TFunctionRef<FText(const UObject&)> GetName, TFunctionRef<void(const FTopLevelAssetPath&, const FText&)> Visit)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TSet<FTopLevelAssetPath> DerivedPaths;
		AssetRegistry.GetDerivedClassNames({ BaseClass->GetClassPathName() }, TSet<FTopLevelAssetPath>(), DerivedPaths);

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
				Name = GetName(*LoadedClass->GetDefaultObject());
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

	/** Every Blueprint track type in the project - see ForEachBlueprintClass. */
	void ForEachBlueprintType(TFunctionRef<void(const FTopLevelAssetPath&, const FText&)> Visit)
	{
		ForEachBlueprintClass(UWidgetAnimBlueprintType::StaticClass(),
			[](const UObject& Default) { return CastChecked<UWidgetAnimBlueprintType>(&Default)->GetTypeDisplayName(); },
			Visit);
	}

	/** Every Blueprint cue in the project - see ForEachBlueprintClass. */
	void ForEachBlueprintCue(TFunctionRef<void(const FTopLevelAssetPath&, const FText&)> Visit)
	{
		ForEachBlueprintClass(UWidgetAnimBlueprintCue::StaticClass(),
			[](const UObject& Default) { return CastChecked<UWidgetAnimBlueprintCue>(&Default)->GetCueDisplayName(); },
			Visit);
	}

	/**
	 * A layer's key times, in played seconds from its own start, for any layer - one of this animation's, or one of an
	 * animation inserted as a track: a Transform layer's curve keys at KeyTimeScale (played seconds per authored one), or
	 * any other keyed layer's start, keys and end along RunSeconds - once it has a key between its start and end. See
	 * SWidgetAnimTimeline::GetKeyTimes.
	 */
	void GetLayerKeyTimes(const FWidgetAnimLayer& Layer, float RunSeconds, float KeyTimeScale, TArray<float>& OutTimes)
	{
		OutTimes.Reset();
		if (Layer.IsAnimationTrack())
		{
			return;
		}

		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
		{
			if (const FRichCurve* Curve = Layer.Transform.GetActiveCurve())
			{
				for (const FRichCurveKey& Key : Curve->GetConstRefOfKeys())
				{
					OutTimes.Add(Key.Time * KeyTimeScale);
				}
			}
			return;
		}

		if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.Keys.Num() > 0)
		{
			for (int32 Point = 0; Point < Layer.ValueKeys.NumPoints(); ++Point)
			{
				OutTimes.Add(Layer.ValueKeys.GetPointTime(Point) * RunSeconds);
			}
		}
	}

	/** The curve of segment Segment of a layer - see SWidgetAnimTimeline::GetSegmentCurve. */
	const FWidgetAnimSegmentCurve* GetLayerSegmentCurve(const FWidgetAnimLayer& Layer, int32 Segment)
	{
		if (Segment < 0)
		{
			return nullptr;
		}
		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
		{
			return Segment + 1 < Layer.Transform.Keys.Num() ? &Layer.Transform.Keys[Segment].CurveToNext : nullptr;
		}
		if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Segment <= Layer.ValueKeys.Keys.Num())
		{
			return &Layer.ValueKeys.GetSegmentCurve(Segment);
		}
		return nullptr;
	}

	/** Whether a layer's segments are shaped at all, and whether any has a curve of its own - see SWidgetAnimTimeline. */
	bool LayerUsesSegmentCurves(const FWidgetAnimLayer& Layer)
	{
		return (WidgetAnimTypeIsKeyframed(Layer.AnimType) || WidgetAnimTypeSupportsValueKeys(Layer.AnimType))
			&& !Layer.IsSpringTimed() && !SWidgetAnimTimeline::IsCueLayer(Layer)
			&& !(WidgetAnimTypeIsKeyframed(Layer.AnimType) && Layer.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack);
	}

	bool LayerHasSegmentCurves(const FWidgetAnimLayer& Layer)
	{
		return WidgetAnimTypeIsKeyframed(Layer.AnimType)
			? Layer.Transform.HasSegmentCurves()
			: WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.HasSegmentCurves();
	}

	/**
	 * Whether a registered type's settings struct makes a User-defined Track rather than one of the plugin's own: a Blueprint
	 * type or cue, which wraps what a project made, or a type another module registers. Every type this plugin registers -
	 * Sound, Event, Count Up, the text tracks - is a track type of its own, shown like a built-in one.
	 */
	bool IsUserDefinedTrackType(const UScriptStruct* ParamsStruct)
	{
		if (!ParamsStruct)
		{
			return false;
		}
		if (ParamsStruct->IsChildOf(FWidgetAnimBlueprintTypeParams::StaticStruct()) || ParamsStruct->IsChildOf(FWidgetAnimBlueprintCueParams::StaticStruct()))
		{
			return true;
		}
		// The plugin's modules are all NameTBD-something: /Script/NameTBD, /Script/NameTBDCommonUI...
		return !ParamsStruct->GetPackage()->GetName().StartsWith(TEXT("/Script/NameTBD"));
	}

	/**
	 * A registered type's settings, field by field, with no Type Params row of their own - the stock instanced struct's
	 * children, laid out where they are added. On a Blueprint type's or cue's track, its Type and its Settings are read-only -
	 * the Settings follow the type's Settings Struct (see SWidgetAnimTimeline::SyncBlueprintTypeSettings) - each with a button
	 * beside it that finds its asset in the Content Browser.
	 *
	 * A field of a type's settings may also say when it is shown and when it can be edited, from its fields before it:
	 *   meta = (ShowWhen = "Action=BurstAtTarget|Play", EnableWhen = "bUseWidgetSystem=false")
	 * - terms joined by commas, all of which must hold; each a field and the values it may have, by '|' (a bool's true / false,
	 * an enum's value name). For what an EditCondition can't be put on: a property bag under one came back read-only.
	 */
	class FTypeParamsDetails : public FInstancedStructDataDetails
	{
	public:
		explicit FTypeParamsDetails(TSharedPtr<IPropertyHandle> InStructProperty)
			: FInstancedStructDataDetails(InStructProperty)
		{
		}

		virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override
		{
			const TSharedPtr<IPropertyHandle> Handle = ChildRow.GetPropertyHandle();
			const FProperty* Property = Handle.IsValid() ? Handle->GetProperty() : nullptr;
			if (Property)
			{
				// The fields come in order: those a later field's ShowWhen / EnableWhen read are already here.
				FieldHandles.Add(Property->GetFName(), Handle);
				ApplyFieldConditions(ChildRow, *Property);
			}
			const UStruct* Owner = Property ? Property->GetOwnerStruct() : nullptr;
			const bool bBlueprintType = Owner == FWidgetAnimBlueprintTypeParams::StaticStruct();
			const bool bBlueprintCue = Owner == FWidgetAnimBlueprintCueParams::StaticStruct();
			if (!bBlueprintType && !bBlueprintCue)
			{
				return;
			}

			// The same two fields on a Blueprint type and a Blueprint cue.
			const FName Name = Property->GetFName();
			const bool bSettings = bBlueprintType ? Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimBlueprintTypeParams, Settings) : Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimBlueprintCueParams, Settings);
			const bool bType = bBlueprintType ? Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimBlueprintTypeParams, Type) : Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimBlueprintCueParams, Type);
			if (!bSettings && !bType)
			{
				return;
			}

			// The asset each names: the Settings' struct (a Blueprint Structure), or the Blueprint the type's class is made by.
			auto FindAsset = [Handle, bSettings]() -> UObject*
			{
				if (!Handle.IsValid() || !Handle->IsValidHandle())
				{
					return nullptr;
				}
				if (bSettings)
				{
					TArray<void*> RawData;
					Handle->AccessRawData(RawData);
					const FInstancedStruct* Settings = RawData.Num() == 1 ? static_cast<const FInstancedStruct*>(RawData[0]) : nullptr;
					UScriptStruct* SettingsStruct = Settings ? const_cast<UScriptStruct*>(Settings->GetScriptStruct()) : nullptr;
					return SettingsStruct && SettingsStruct->IsAsset() ? SettingsStruct : nullptr;
				}
				UObject* Value = nullptr;
				const UClass* TypeClass = Handle->GetValue(Value) == FPropertyAccess::Success ? Cast<UClass>(Value) : nullptr;
				return TypeClass ? TypeClass->ClassGeneratedBy.Get() : nullptr;
			};

			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			ChildRow.GetDefaultWidgets(NameWidget, ValueWidget, /*bAddWidgetDecoration=*/true);
			if (!NameWidget.IsValid() || !ValueWidget.IsValid())
			{
				return;
			}

			ChildRow.CustomWidget(/*bShowChildren=*/bSettings)
				.NameContent()
				[
					NameWidget.ToSharedRef()
				]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.IsEnabled(false)
						.ToolTipText(bSettings ? WidgetAnimTips::BlueprintSettingsStructTooltip() : WidgetAnimTips::TypeNameTooltip())
						[
							ValueWidget.ToSharedRef()
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
					[
						PropertyCustomizationHelpers::MakeBrowseButton(
							FSimpleDelegate::CreateLambda([FindAsset]()
							{
								if (UObject* Asset = FindAsset(); Asset && GEditor)
								{
									TArray<UObject*> Objects;
									Objects.Add(Asset);
									GEditor->SyncBrowserToObjects(Objects);
								}
							}),
							bSettings ? WidgetAnimTips::BrowseToSettingsStructTooltip() : WidgetAnimTips::BrowseToBlueprintTypeTooltip(),
							TAttribute<bool>::CreateLambda([FindAsset]() { return FindAsset() != nullptr; }))
					]
				];
		}

	private:
		/** Whether every term of a ShowWhen / EnableWhen holds for the fields as they are now. True when one can't be read. */
		static bool AreTermsMet(const TArray<TPair<TSharedPtr<IPropertyHandle>, TArray<FString>>>& Terms)
		{
			for (const TPair<TSharedPtr<IPropertyHandle>, TArray<FString>>& Term : Terms)
			{
				FString Value;
				if (!Term.Key.IsValid() || !Term.Key->IsValidHandle() || Term.Key->GetValueAsFormattedString(Value, PPF_None) != FPropertyAccess::Success)
				{
					continue;
				}
				const bool bAny = Term.Value.ContainsByPredicate([&Value](const FString& Allowed) { return Allowed.Equals(Value, ESearchCase::IgnoreCase); });
				if (!bAny)
				{
					return false;
				}
			}
			return true;
		}

		/** A ShowWhen / EnableWhen read against the fields already added. */
		TArray<TPair<TSharedPtr<IPropertyHandle>, TArray<FString>>> ParseTerms(const FString& Text) const
		{
			TArray<TPair<TSharedPtr<IPropertyHandle>, TArray<FString>>> Terms;
			TArray<FString> Parts;
			Text.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				FString FieldName;
				FString Values;
				if (!Part.Split(TEXT("="), &FieldName, &Values))
				{
					continue;
				}
				const TSharedPtr<IPropertyHandle>* Field = FieldHandles.Find(FName(*FieldName.TrimStartAndEnd()));
				if (!Field)
				{
					continue;
				}
				TArray<FString> Allowed;
				Values.TrimStartAndEnd().ParseIntoArray(Allowed, TEXT("|"));
				Terms.Emplace(*Field, MoveTemp(Allowed));
			}
			return Terms;
		}

		void ApplyFieldConditions(IDetailPropertyRow& ChildRow, const FProperty& Property) const
		{
			static const FName ShowWhenName(TEXT("ShowWhen"));
			static const FName EnableWhenName(TEXT("EnableWhen"));
			if (Property.HasMetaData(ShowWhenName))
			{
				ChildRow.Visibility(TAttribute<EVisibility>::CreateLambda([Terms = ParseTerms(Property.GetMetaData(ShowWhenName))]()
				{
					return AreTermsMet(Terms) ? EVisibility::Visible : EVisibility::Collapsed;
				}));
			}
			if (Property.HasMetaData(EnableWhenName))
			{
				ChildRow.IsEnabled(TAttribute<bool>::CreateLambda([Terms = ParseTerms(Property.GetMetaData(EnableWhenName))]()
				{
					return AreTermsMet(Terms);
				}));
			}
		}

		/** The settings' fields added so far, by name. */
		TMap<FName, TSharedPtr<IPropertyHandle>> FieldHandles;
	};

	/**
	 * The Save / Update Animation Asset button: a dimmed animation orange, flat, with black text, lighting up to the full
	 * orange under the mouse - not the editor's dark button tinted orange, which reads brown, nor the full orange at rest,
	 * which shouts.
	 */
	const FButtonStyle& OrangeButtonStyle()
	{
		static const FButtonStyle Style = []()
		{
			FButtonStyle Out = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
			Out.SetNormal(FSlateRoundedBoxBrush(Palette::SaveButtonNormal, 4.f));
			Out.SetHovered(FSlateRoundedBoxBrush(Palette::SaveButtonHovered, 4.f));
			Out.SetPressed(FSlateRoundedBoxBrush(Palette::SaveButtonPressed, 4.f));
			Out.SetDisabled(FSlateRoundedBoxBrush(Palette::SaveButtonDisabled, 4.f));
			Out.SetNormalForeground(FSlateColor(FLinearColor::Black));
			Out.SetHoveredForeground(FSlateColor(FLinearColor::Black));
			Out.SetPressedForeground(FSlateColor(FLinearColor::Black));
			Out.SetDisabledForeground(FSlateColor(Palette::SaveButtonDisabledText));
			Out.SetNormalPadding(FMargin(10.f, 2.f));
			Out.SetPressedPadding(FMargin(10.f, 3.f, 10.f, 1.f));
			return Out;
		}();
		return Style;
	}

	/** A faint rounded chip behind a button's icon - so it reads as something to press, not as a toggle. */
	const FSlateBrush* ChipBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.f);
		return &Brush;
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

		// Its content from the start, not only once it opens: a tooltip shown before its content was made was measured
		// empty, and showed as a bare box.
		if (HasContent && HasContent() && MakeContent)
		{
			SetContentWidget(MakeContent());
		}
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

		SetToolTip(MakeTrackToolTip());
	}

	/** A tooltip for whatever is under the mouse now - a track's, or a key's. Made afresh each time that changes. */
	TSharedPtr<IToolTip> MakeTrackToolTip()
	{
		return SNew(SWidgetAnimTrackToolTip,
			[this]() { return HasToolTipTarget(); },
			[this]() { return MakeToolTipContent(); });
	}

	/** Whether the view shows less than the whole stretch the scroll bar covers - when there is anything to scroll to. */
	bool IsHorizontalScrollNeeded() const { return bHorizontalScrollNeeded; }

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

		// A pinned line is always in view: it doesn't scroll.
		if (LineIndex < GetNumPinned())
		{
			return;
		}

		const float RowHeight = Owner->GetRowHeight();
		const float ViewHeight = FMath::Max(LastSize.Y - GetPinnedBottom(), RowHeight);
		const float Top = (GetNumContextRows() + LineIndex - GetNumPinned()) * RowHeight;

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
		Marquee,

		/** The animation's start or end, pressed in the ruler - dragged once it moves, else a click that scrubs. */
		RangePending,
		Range
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
		const FVector2f GroupSpan = Owner ? Owner->GetShownGroupSpan(GroupIndex) : FVector2f(0.f, -1.f);
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

		/** On a collapsed track - Layer is its head. */
		bool bCollapsed = false;
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

	/** How many of the first lines are pinned tracks' - drawn under the ruler, where they stay while the rest scroll. */
	int32 GetNumPinned() const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner ? FMath::Min(Owner->GetNumPinnedLines(), GetNumLines()) : 0;
	}

	/** Where the pinned lines end and the scrolling ones begin, in local pixels. The ruler's bottom when none are pinned. */
	float GetPinnedBottom() const
	{
		return SWidgetAnimTimeline::RulerHeight + GetNumPinned() * GetRowHeight();
	}

	/** Where a context row's top is drawn, scrolled with the lines below it. */
	float GetContextRowTop(int32 Row) const
	{
		return GetPinnedBottom() + Row * GetRowHeight() - ScrollY;
	}

	/** Where a line's top is drawn, in local pixels: a pinned one under the ruler, the rest scrolled - under any context rows. */
	float GetLineTop(int32 LineIndex) const
	{
		const int32 NumPinned = GetNumPinned();
		if (LineIndex < NumPinned)
		{
			return SWidgetAnimTimeline::RulerHeight + LineIndex * GetRowHeight();
		}
		return GetPinnedBottom() + (GetNumContextRows() + LineIndex - NumPinned) * GetRowHeight() - ScrollY;
	}

	/** How far down the lines can be scrolled: to the last line, and one line's room below it. The pinned ones don't scroll. */
	float GetMaxScrollY(float Height) const
	{
		const float Content = (GetNumContextRows() + GetNumLines() - GetNumPinned() + 1) * GetRowHeight();
		return FMath::Max(Content - (Height - GetPinnedBottom()), 0.f);
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

		// The pinned lines first, where they stay; below them the rest, scrolled.
		const int32 NumPinned = GetNumPinned();
		const float PinnedBottom = GetPinnedBottom();
		if (Y < PinnedBottom)
		{
			const int32 Pinned = FMath::FloorToInt((Y - SWidgetAnimTimeline::RulerHeight) / GetRowHeight());
			return Pinned >= 0 && Pinned < NumPinned ? Pinned : INDEX_NONE;
		}

		const int32 Line = FMath::FloorToInt((Y - PinnedBottom + ScrollY) / GetRowHeight()) - GetNumContextRows() + NumPinned;
		return Line >= NumPinned && Line < GetNumLines() ? Line : INDEX_NONE;
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

		return Edge >= 1 && EdgeY > GetPinnedBottom()
			&& FMath::Abs(Y - EdgeY) <= WidgetAnimTimeline::RowEdgeGrabDistance
			? Edge
			: INDEX_NONE;
	}

	const FTrackLine* GetLine(int32 LineIndex) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner && Owner->GetTrackLines().IsValidIndex(LineIndex) ? &Owner->GetTrackLines()[LineIndex] : nullptr;
	}

	/**
	 * A layer's start and length, in seconds from the top, as it is drawn - a collapsed track's head spanning all of its
	 * tracks. Zero when it has none.
	 */
	FVector2f GetLayerSpan(int32 LayerIndex) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner ? Owner->GetShownSpan(LayerIndex) : FVector2f::ZeroVector;
	}

	/** Whether a layer is drawn as a collapsed track here - its head. See SWidgetAnimTimeline::IsCollapsedHead. */
	bool IsCollapsedBar(int32 LayerIndex) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		return Owner && Owner->IsCollapsedHeadShown(LayerIndex);
	}

	/** How far beyond its triangle, in pixels, the mouse can be and still grab the animation's start or end. */
	static constexpr float RangeGrabDistance = 2.f;

	/**
	 * The animation's start (false) or end (true) under a point in the ruler, if either can be dragged there: only in the
	 * strip along the ruler's bottom, on its triangle - the rest of the ruler is the playhead's, so it can be grabbed and
	 * scrubbed wherever it sits, start and end included, and a click on a triangle never scrubs.
	 */
	TOptional<bool> RangeEdgeAt(const FVector2f& Local, float Width) const
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const float StripTop = SWidgetAnimTimeline::RulerHeight - SWidgetAnimTimeline::RangeStripHeight;
		if (!Owner || !Owner->CanEdit() || Local.Y >= SWidgetAnimTimeline::RulerHeight || Local.Y < StripTop - 1.f || !Owner->GetLayers() || Owner->GetLayers()->Num() == 0)
		{
			return TOptional<bool>();
		}

		// Each triangle is centered on its line. With the two together - nothing played yet - the side pressed picks: the end
		// to the right, which is the one that can be pulled out.
		const float EndX = TimeToX(Owner->GetPassSeconds(), Width);
		const float StartX = TimeToX(0.f, Width);
		const float HalfWidth = UWidgetAnimEditorSettings::Get().RangeTriangleWidth * 0.5f + RangeGrabDistance;
		const bool bOnEnd = FMath::Abs(Local.X - (EndX + 0.5f)) <= HalfWidth;
		const bool bOnStart = FMath::Abs(Local.X - (StartX + 0.5f)) <= HalfWidth;
		if (bOnEnd && bOnStart)
		{
			return TOptional<bool>(Local.X >= (StartX + EndX) * 0.5f);
		}
		if (bOnEnd)
		{
			return TOptional<bool>(true);
		}
		if (bOnStart)
		{
			return TOptional<bool>(false);
		}
		return TOptional<bool>();
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

	/** The arrow opening an animation track: a size up from a group's, so it reads as the track's own control. */
	static constexpr float AnimationFoldArrowSize = 13.f;

	/**
	 * The arrow that opens an animation track, at the start of its section - SectionRect, from GetSectionRect. Centered on
	 * the section's body, where its text is, not on the whole section, whose frame shows as a lip along its bottom.
	 */
	FBox2f GetAnimationFoldRect(const FBox2f& SectionRect) const
	{
		namespace Palette = NameTBD::Colors::WidgetAnimEditor;
		const float Height = SectionRect.Max.Y - SectionRect.Min.Y;
		const float Lip = FMath::Clamp(Height * Palette::SectionLipRatio, Palette::SectionLipMin, Palette::SectionLipMax);
		const float BodyTop = SectionRect.Min.Y + Palette::SectionFrameThickness;
		const float BodyHeight = Height - Palette::SectionFrameThickness - Lip;
		const float CenterY = BodyTop + BodyHeight * 0.5f;
		const float Size = FMath::Min(AnimationFoldArrowSize, FMath::Max(BodyHeight, 6.f));
		const float X = SectionRect.Min.X + 5.f;
		return FBox2f(FVector2f(X, CenterY - Size * 0.5f), FVector2f(X + Size, CenterY + Size * 0.5f));
	}

	/** Where an open animation track's nested group header draws its fold arrow: just inside where the track starts. */
	float GetNestedFoldX(const FTrackLine& Line, float Width) const
	{
		return FMath::Max(TimeToX(GetLayerSpan(Line.AnimationLayer).X, Width) + 4.f, WidgetAnimTimeline::GroupBandWidth + 6.f);
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

		// An open animation track's lines: only its groups' fold arrows do anything there. What is on them is shown only.
		if (Line->IsNested())
		{
			if (Line->Kind == FTrackLine::EKind::NestedGroup)
			{
				const float FoldX = GetNestedFoldX(*Line, Width);
				Hit.bOnFold = Local.X >= FoldX - 4.f && Local.X <= FoldX + FoldArrowSize + 4.f;
			}
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

			// A collapsed track is grabbed whole: no keys, edges or arrows of its own. Double-clicked, it opens.
			if (IsCollapsedBar(LayerIndex))
			{
				Hit.bCollapsed = true;
				return Hit;
			}

			// An animation track's arrow opens it; it has no keys or edges to grab.
			if (const TArray<FWidgetAnimLayer>* Layers = Owner->GetLayers(); Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].IsAnimationTrack())
			{
				Hit.bOnFold = Local.X <= GetAnimationFoldRect(Rect).Max.X + 3.f;
				return Hit;
			}
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
				if (IsCollapsedBar(LayerIndex))
				{
					continue;
				}
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

	/**
	 * The selection box, corners in order however it was dragged. Its first corner is held where it was pressed in the
	 * timeline - its time, and its line scrolled with the lines - so the box grows as the view scrolls after the mouse.
	 */
	FBox2f GetMarqueeRect() const
	{
		FVector2f Start = MarqueeStart;
		if (LastSize.X > 1.f)
		{
			Start.X = TimeToX(MarqueeStartTime, LastSize.X);
		}
		if (!bMarqueeStartPinned)
		{
			Start.Y = MarqueeStart.Y - (ScrollY - MarqueeStartScrollY);
		}
		return FBox2f(
			FVector2f(FMath::Min(Start.X, MarqueeEnd.X), FMath::Min(Start.Y, MarqueeEnd.Y)),
			FVector2f(FMath::Max(Start.X, MarqueeEnd.X), FMath::Max(Start.Y, MarqueeEnd.Y)));
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

				// A collapsed track has no keys of its own to box.
				if (IsCollapsedBar(LayerIndex))
				{
					continue;
				}

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
		if (TipKey != INDEX_NONE)
		{
			return Owner->MakeKeyToolTip(TipLayer, TipKey);
		}
		return IsCollapsedBar(TipLayer) ? Owner->MakeCollapsedToolTip(TipLayer) : Owner->MakeLayerToolTip(TipLayer);
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

		// A new tooltip rather than the open one given new content: one tooltip reused across a track and its keys kept the
		// size it had been measured at, and a key's opened as an empty box.
		SetToolTip(MakeTrackToolTip());
	}

	void ClearHover()
	{
		Hover = FHit();
		HoverRowEdge = INDEX_NONE;
		HoverX = -1.f;
		HoverRange.Reset();
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

	/** Whether the view is narrower than what the horizontal scroll bar covers, worked out each tick - see IsHorizontalScrollNeeded. */
	bool bHorizontalScrollNeeded = false;

	/** A scrub, key drag or track drag carried to the mouse at Local - on a move, or after the view scrolled under it. */
	void ApplyDragAt(const FVector2f& Local, float Width);

	/** What a press on a section will do once it moves far enough to be a drag. */
	ELayerDrag PendingLayerDrag = ELayerDrag::Move;

	/** Which of the animation's edges a range drag has: its end, or its start. */
	bool bRangeEnd = true;

	/** The animation's start (false) or end (true) under the mouse in the ruler, while nothing is dragged - lit, to be grabbed. */
	TOptional<bool> HoverRange;

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

	/** Where the selection box was pressed in the timeline: its time, the scroll then, and whether on a pinned line. */
	float MarqueeStartTime = 0.f;
	float MarqueeStartScrollY = 0.f;
	bool bMarqueeStartPinned = false;
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
	// A selection box does the same, and up and down too: held against the top or bottom, the lines scroll under it.
	const bool bEdgeScrolling = (Drag == EDrag::Scrub || Drag == EDrag::Key || Drag == EDrag::Layer || Drag == EDrag::Range || Drag == EDrag::Marquee) && HasMouseCapture() && LastDragLocal.IsSet();
	if (bEdgeScrolling && Drag == EDrag::Marquee && LastSize.Y > 1.f)
	{
		constexpr float EdgeZoneY = 20.f;
		const float MouseY = LastDragLocal->Y;
		const float TopEdge = GetPinnedBottom();
		float PushY = 0.f;
		if (MouseY < TopEdge + EdgeZoneY)
		{
			PushY = -FMath::Min((TopEdge + EdgeZoneY - MouseY) / EdgeZoneY, 4.f);
		}
		else if (MouseY > LastSize.Y - EdgeZoneY)
		{
			PushY = FMath::Min((MouseY - (LastSize.Y - EdgeZoneY)) / EdgeZoneY, 4.f);
		}

		if (PushY != 0.f)
		{
			// Eight lines a second at the edge itself, more past it.
			const float OldScroll = ScrollY;
			ScrollY += GetRowHeight() * 8.f * PushY * InDeltaTime;
			ClampScroll(LastSize.Y);
			if (!FMath::IsNearlyEqual(OldScroll, ScrollY))
			{
				ApplyDragAt(LastDragLocal.GetValue(), LastSize.X);
			}
		}
	}
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
		const float ThumbFraction = FMath::Clamp((ViewMax - ViewMin) / Range, 0.f, 1.f);
		HorizontalScrollBar->SetState((ViewMin - RangeMin) / Range, ThumbFraction);
		bHorizontalScrollNeeded = ThumbFraction < 0.999f;
	}
}

int32 SWidgetAnimTimelineTracks::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimTrackEditorPaint);
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
	// The range's lines through the tracks: over the sections, under the track names and keys - a key on the start or end
	// is never hidden.
	const int32 RangeLineLayer = TextShadowLayer;
	const int32 KeyGlowLayer = LayerId + 14;
	const int32 KeyOutlineLayer = LayerId + 15;
	const int32 KeyFillLayer = LayerId + 16;
	const int32 LabelLayer = LayerId + 17;
	const int32 RulerLayer = LayerId + 19;
	const int32 RangeLayer = LayerId + 21;
	const int32 PlayheadLayer = LayerId + 24;

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

	// ---- Lines ---------------------------------------------------------------------------------------------------
	//
	// Drawn in two passes, each clipped to its own band: the lines that scroll, under the pinned tracks' lines; then the
	// pinned tracks' lines, under the ruler, where they stay whatever the rest are scrolled to.

	const int32 NumPinned = GetNumPinned();
	const float PinnedBottom = GetPinnedBottom();

	// The grid's steps, stepped by index rather than by adding floats, so it never drifts off the ruler's labels.
	const float PixelsPerSecond = Size.X / FMath::Max(ViewMax - ViewMin, KINDA_SMALL_NUMBER);
	// In frames, the steps are whole frames; a step of a frame or two has no fifths of a frame worth drawing.
	const bool bFrameTime = Owner->IsFrameTime();
	const float FrameRate = SWidgetAnimTimeline::GetFrameRate();
	const float Step = bFrameTime ? ChooseFrameTickStep(PixelsPerSecond, 72.f, FrameRate) : ChooseTickStep(PixelsPerSecond, 72.f);
	const int32 MinorPerMajor = bFrameTime ? FMath::Clamp(FMath::RoundToInt32(Step * FrameRate), 1, 5) : 5;
	const float MinorStep = Step / MinorPerMajor;
	const int64 FirstTick = FMath::FloorToInt64(ViewMin / MinorStep);

	const FSlateFontInfo GroupFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo GroupTargetFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* LockBrush = PickBrush({ "Icons.Lock", "PropertyWindow.Locked" });
	const FSlateBrush* HiddenBrush = PickBrush({ "Icons.Hidden", "Level.NotVisibleIcon16x" });
	const FSlateBrush* PinBrush = PickBrush({ "Icons.Pinned", "Icons.Pin", "PropertyWindow.Favorites_Enabled" });
	const FSlateBrush* CollapsedBrush = PickBrush({ "GraphEditor.SubGraph_16x", "Icons.FolderClosed" });
	const float TimeScale = Owner->GetTimeScale();

	/**
	 * How one section is drawn: a track of this animation, one of an animation inserted as a track (shown only), or a
	 * collapsed track standing for several (Layer null, its look given outright).
	 */
	struct FSectionPaint
	{
		const FWidgetAnimLayer* Layer = nullptr;

		/** This animation's layer, for its selection, hover, lit edges and keys. INDEX_NONE: shown only. */
		int32 LayerIndex = INDEX_NONE;

		FBox2f Rect;
		FVector2f Span = FVector2f::ZeroVector;
		TArray<float> KeyTimes;
		float FanSeconds = 0.f;
		int32 FanCount = INDEX_NONE;

		bool bSelected = false;
		bool bLocked = false;
		bool bHidden = false;
		bool bPinned = false;
		bool bWashedOut = false;
		bool bKeysEditable = false;
		bool bInverted = false;
		bool bNamesTarget = false;

		/** Drawn as a collapsed graph node is - a blank body whose color fades in from its edges - not as a track. */
		bool bCollapsedLook = false;

		/** A collapsed track's look, when Layer is null: its color, its title and the text on its right, its glyph. */
		FLinearColor Color = FLinearColor::White;
		FString Title;
		FString Secondary;
		const FSlateBrush* Glyph = nullptr;

	};

	auto DrawSection = [&](const FSectionPaint& Paint)
	{
		const FWidgetAnimLayer* Layer = Paint.Layer;
		const int32 LayerIndex = Paint.LayerIndex;
		const bool bInteractive = LayerIndex != INDEX_NONE;
		const FVector2f Span = Paint.Span;
		const float BarX0 = Paint.Rect.Min.X;
		const float BarTop = Paint.Rect.Min.Y;
		const float BarWidth = Paint.Rect.Max.X - Paint.Rect.Min.X;
		const float BarHeight = Paint.Rect.Max.Y - Paint.Rect.Min.Y;

		// A stagger reaches on past its first item, to where its last item ends: a tail drawn after the section.
		const float TailRight = Paint.FanSeconds > 0.f ? TimeToX(Span.X + Span.Y + Paint.FanSeconds, Size.X) : BarX0 + BarWidth;

		// Nowhere near the view: nothing of it to draw.
		if (BarX0 > Size.X + 8.f || FMath::Max(BarX0 + BarWidth, TailRight) < -8.f)
		{
			return;
		}

		// A hidden track's text is in italics - the font's italic typeface, or slanted where it has none.
		FSlateFontInfo LayerFont = SectionFont;
		// So is a collapsed track's name, as a collapsed graph node's is.
		const bool bSlantLayerText = (Paint.bHidden || Paint.bCollapsedLook) && !ItalicOf(SectionFont, LayerFont);
		const bool bWashedOut = Paint.bWashedOut;
		const bool bBarHovered = bInteractive && Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.Key == INDEX_NONE && Hover.Edge == EEdge::None && Hover.bOnSection;

		FLinearColor TypeColor = Layer ? SWidgetAnimTimeline::GetLayerTypeColor(*Layer) : Paint.Color;
		if (bWashedOut)
		{
			TypeColor = SWidgetAnimTimeline::GetLockedColor(TypeColor);
		}

		// The stagger's tail: a thinner band of the track's color from the end of its first item to the end of its
		// last, with a tick where each item after the first starts - when the preview has counted them. The ticks
		// are left out once they would crowd into a smear.
		if (Layer && TailRight > BarX0 + BarWidth + 0.5f)
		{
			const float TailHeight = BarHeight * Palette::SectionStaggerTailHeight;
			const float TailTop = BarTop + (BarHeight - TailHeight) * 0.5f;
			const float TailLeft = BarX0 + BarWidth;
			Box(TailLeft, TailTop, TailRight - TailLeft, TailHeight, WithAlpha(TypeColor, Palette::SectionStaggerTailOpacity), BackingLayer, RoundedFill(2.f));

			const FWidgetAnimExpander* Expander = Layer->Expander.GetPtr<FWidgetAnimExpander>();
			const int32 NumItems = Paint.FanCount;
			const float PixelsPerItem = (TailRight - BarX0) / FMath::Max(NumItems, 1);
			if (Expander && NumItems > 1 && PixelsPerItem >= 3.f)
			{
				TArray<float> Offsets;
				Expander->ComputeOffsets(NumItems, Offsets);
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
				const bool bSlantTail = Paint.bHidden && !ItalicOf(FCoreStyle::GetDefaultFontStyle("Regular", 8), TailFont);
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
		// A collapsed track's body sits inside an even frame, rounded at every corner, as a collapsed graph node's does; every
		// other track's frame shows only as a lip along its bottom.
		const float Lip = Paint.bCollapsedLook
			? Palette::CollapsedNodeBorder
			: FMath::Clamp(BarHeight * Palette::SectionLipRatio, Palette::SectionLipMin, Palette::SectionLipMax);
		const float Frame = Paint.bCollapsedLook ? Palette::CollapsedNodeBorder : Palette::SectionFrameThickness;
		const float InnerX = BarX0 + Frame;
		const float InnerTop = BarTop + Frame;
		const float InnerWidth = BarWidth - Frame * 2.f;
		const float InnerHeight = BarHeight - Frame - Lip;

		if (Paint.bSelected)
		{
			const float Rim = Palette::SectionSelectedOutlineSize;
			const float Halo = Rim + Palette::SectionSelectedHaloSize;
			Box(BarX0 - Halo, BarTop - Halo, BarWidth + Halo * 2.f, BarHeight + Halo * 2.f, Palette::SectionSelectedHalo, HaloLayer, RoundedFill(3.f + Halo));
			Box(BarX0 - Rim, BarTop - Rim, BarWidth + Rim * 2.f, BarHeight + Rim * 2.f, Palette::SectionSelectedOutline, OutlineLayer, RoundedFill(3.f + Rim));
		}

		Box(BarX0 + Palette::SectionBleedWidth, BarTop + Palette::SectionBleedWidth,
			BarWidth - Palette::SectionBleedWidth * 2.f, BarHeight - Palette::SectionBleedWidth * 2.f,
			Palette::SectionBacking, BackingLayer, RoundedFill());

		Box(BarX0, BarTop, BarWidth, BarHeight, Paint.bCollapsedLook ? Palette::CollapsedNodeFrame : Palette::SectionFrame, FrameLayer, RoundedFill(Paint.bCollapsedLook ? Palette::CollapsedNodeRadius : 3.f));

		const bool bValueDriven = Layer && Layer->IsValueDriven();
		const bool bDerivedLength = Layer && SWidgetAnimTimeline::HasDerivedLength(*Layer);
		const bool bStriped = bValueDriven || bDerivedLength;
		if (InnerWidth > 0.f && InnerHeight > 0.f)
		{
			const FLinearColor Spill = WithAlpha(bBarHovered && !bWashedOut ? TypeColor * Palette::SectionHoverLift : TypeColor, bStriped ? Palette::SectionValueSpillOpacity : 1.f);

			if (Paint.bCollapsedLook)
			{
				// A collapsed track: a facsimile of the Blueprint collapsed graph node - a dark, blank body, rounded at every
				// corner, with a soft gray glow in its middle fading out toward its edges. No gloss, sheen or half-tone line:
				// those are a node title's, and a collapsed node is all body.
				const float BodyRadius = FMath::Max(Palette::CollapsedNodeRadius - Frame, 1.f);
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, bWashedOut ? SWidgetAnimTimeline::GetLockedColor(Palette::CollapsedNodeBody) : Palette::CollapsedNodeBody, BodyLayer, RoundedFill(BodyRadius));

				// The glow, a row of pixels at a time: across each row, faded in from both ends over CollapsedNodeGlowReach of
				// the width; down the rows, strongest in the middle and gone at the top and bottom. Clipped to the body.
				FLinearColor Glow = bBarHovered && !bWashedOut ? Palette::CollapsedNodeGlow * Palette::SectionHoverLift : Palette::CollapsedNodeGlow;
				if (bWashedOut)
				{
					Glow = SWidgetAnimTimeline::GetLockedColor(Glow);
				}
				Glow.A = Palette::CollapsedNodeGlow.A;

				const FGeometry CollapsedGeometry = AllottedGeometry.MakeChild(FVector2f(InnerWidth, InnerHeight), FSlateLayoutTransform(FVector2f(InnerX, InnerTop)));
				OutDrawElements.PushClip(FSlateClippingZone(CollapsedGeometry));

				// Only the stretch in view is drawn - a collapsed track can run far past either side of it.
				const float GlowLeft = FMath::Max(InnerX, -8.f);
				const float GlowRight = FMath::Min(InnerX + InnerWidth, Size.X + 8.f);
				// Never so far in that the two ends' stops cross, however narrow the track is.
				const float Reach = FMath::Min(FMath::Clamp(InnerWidth * Palette::CollapsedNodeGlowReach, 4.f, Palette::CollapsedNodeGlowMaxReach), InnerWidth / 4.5f);
				const int32 NumRows = FMath::Max(FMath::CeilToInt(InnerHeight), 1);
				if (GlowRight > GlowLeft)
				{
					for (int32 Row = 0; Row < NumRows; ++Row)
					{
						const float Down = (Row + 0.5f) / NumRows;
						const float Strength = FMath::Pow(FMath::Sin(Down * PI), Palette::CollapsedNodeGlowFalloff);
						if (Strength <= 0.01f)
						{
							continue;
						}

						// Its stops in the row's own space, which starts at the body's left edge.
						auto At = [&](float BodyX, float Alpha)
						{
							return FSlateGradientStop(FVector2D(BodyX - GlowLeft, 0.0), Glow.CopyWithNewOpacity(Glow.A * Strength * Alpha));
						};
						const float BodyLeft = InnerX;
						const float BodyRight = InnerX + InnerWidth;
						TArray<FSlateGradientStop> GlowStops;
						// Slow out of the dark at each end and only full in the middle, so the edges stay near black.
						GlowStops.Add(At(BodyLeft, 0.f));
						GlowStops.Add(At(BodyLeft + Reach * 0.5f, 0.15f));
						GlowStops.Add(At(BodyLeft + Reach, 0.45f));
						GlowStops.Add(At(BodyLeft + Reach * 1.6f, 0.8f));
						GlowStops.Add(At(BodyLeft + Reach * 2.2f, 1.f));
						GlowStops.Add(At(BodyRight - Reach * 2.2f, 1.f));
						GlowStops.Add(At(BodyRight - Reach * 1.6f, 0.8f));
						GlowStops.Add(At(BodyRight - Reach, 0.45f));
						GlowStops.Add(At(BodyRight - Reach * 0.5f, 0.15f));
						GlowStops.Add(At(BodyRight, 0.f));
						FSlateDrawElement::MakeGradient(OutDrawElements, SpillLayer,
							AllottedGeometry.ToPaintGeometry(FVector2f(GlowRight - GlowLeft, 1.f), FSlateLayoutTransform(FVector2f(GlowLeft, InnerTop + Row))),
							MoveTemp(GlowStops), Orient_Vertical, ESlateDrawEffect::None);
					}
				}
				OutDrawElements.PopClip();
			}
			else
			{
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Palette::SectionBody, BodyLayer, RoundedFill(2.f));
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, bWashedOut ? SWidgetAnimTimeline::GetLockedColor(FLinearColor::White) : FLinearColor::White, GlossLayer, NodeTitleGloss());
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Spill, SpillLayer, NodeColorSpill());
				Box(InnerX, InnerTop, InnerWidth, FMath::Min(InnerHeight, Palette::SectionSheenHeight), FLinearColor::White, SheenLayer, NodeTitleHighlight());
			}

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
			if (!Paint.bCollapsedLook && Palette::SectionHalfTone.A > 0.f && HalfToneWidth >= 1.f && HalfToneHeight > 0.f)
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

			// A track playing backward - inverted on its own, or the preview's Invert on - wears a row of faint chevrons
			// pointing back along it, so which way it runs reads at a glance. Clipped to the body; only those in view.
			if (Paint.bInverted && InnerHeight >= 8.f)
			{
				const FGeometry BodyGeometry = AllottedGeometry.MakeChild(FVector2f(InnerWidth, InnerHeight), FSlateLayoutTransform(FVector2f(InnerX, InnerTop)));
				OutDrawElements.PushClip(FSlateClippingZone(BodyGeometry));

				const float Arm = FMath::Clamp(InnerHeight * 0.22f, 3.f, 6.f);
				const float Stroke = 1.5f;
				const float StrokeLength = Arm * UE_SQRT_2;
				const float CenterY = InnerTop + InnerHeight * 0.5f;
				const FLinearColor ChevronColor = bWashedOut ? SWidgetAnimTimeline::GetLockedColor(Palette::InvertChevron) : Palette::InvertChevron;
				// From the section's start, skipping those left of the view.
				const float Spacing = Palette::InvertChevronSpacing;
				float TipX = InnerX + Spacing * 0.5f;
				if (TipX < -Spacing)
				{
					TipX += FMath::CeilToFloat((-Spacing - TipX) / Spacing) * Spacing;
				}
				for (; TipX < FMath::Min(InnerX + InnerWidth - Arm, Size.X + Arm); TipX += Spacing)
				{
					// A "<": two short strokes meeting at its tip on the left, each turned 45 degrees about its middle.
					for (const float Sign : { -1.f, 1.f })
					{
						FSlateDrawElement::MakeBox(
							OutDrawElements,
							SectionOverlayLayer,
							AllottedGeometry.ToPaintGeometry(
								FVector2f(StrokeLength, Stroke),
								FSlateLayoutTransform(FVector2f(TipX + Arm * 0.5f - StrokeLength * 0.5f, CenterY + Sign * Arm * 0.5f - Stroke * 0.5f)),
								FSlateRenderTransform(FQuat2f(FMath::DegreesToRadians(Sign * 45.f))),
								FVector2f(0.5f, 0.5f)),
							Flat,
							ESlateDrawEffect::None,
							ChevronColor);
					}
				}

				OutDrawElements.PopClip();
			}
		}

		// The resize handle: a lit strip along the edge under the mouse, or the one being dragged.
		const EEdge LitEdge = bInteractive ? GetLitEdge(LayerIndex) : EEdge::None;
		if (LitEdge != EEdge::None)
		{
			const float HandleX = LitEdge == EEdge::Start ? BarX0 : BarX0 + BarWidth - Palette::SectionEdgeHandleWidth;
			Box(HandleX, BarTop, Palette::SectionEdgeHandleWidth, BarHeight, Palette::SectionEdgeHandle, SectionOverlayLayer);
		}

		// A hidden track wears a closed eye at its start, a locked one a lock, a pinned one a pin - any of them, if it is
		// more than one - and its text moves along to make room. A collapsed track wears its own glyph first.
		const float Padding = Palette::SectionTextPadding;
		float TextLeft = BarX0 + Padding;
		float GlyphX = BarX0 + Padding * 0.5f;

		// An animation track opens from an arrow at its start - see GetAnimationFoldRect - and everything else on it
		// moves along to make room.
		if (Layer && Layer->IsAnimationTrack() && bInteractive)
		{
			const FBox2f FoldRect = GetAnimationFoldRect(Paint.Rect);
			const bool bArrowHovered = Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.bOnFold;
			if (const FSlateBrush* Arrow = Owner->IsAnimationTrackExpanded(LayerIndex)
				? PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" })
				: PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" }))
			{
				if (FoldRect.Max.X <= BarX0 + BarWidth)
				{
					Box(FoldRect.Min.X, FoldRect.Min.Y, FoldRect.Max.X - FoldRect.Min.X, FoldRect.Max.Y - FoldRect.Min.Y, WithAlpha(FLinearColor::White, bArrowHovered ? 1.f : 0.8f), SectionOverlayLayer, Arrow);
				}
			}
			GlyphX = FoldRect.Max.X + 4.f;
			TextLeft = GlyphX + Padding * 0.5f;
		}
		for (const FSlateBrush* Glyph : { Paint.Glyph, Paint.bHidden ? HiddenBrush : nullptr, Paint.bLocked ? LockBrush : nullptr, Paint.bPinned ? PinBrush : nullptr })
		{
			if (Glyph && GlyphX - BarX0 + Palette::LockedGlyphSize + Padding <= BarWidth)
			{
				const float GlyphSize = FMath::Min(Palette::LockedGlyphSize, InnerHeight);
				Box(GlyphX, InnerTop + (InnerHeight - GlyphSize) * 0.5f, GlyphSize, GlyphSize, Palette::LockedGlyph, SectionOverlayLayer, Glyph);
				GlyphX += GlyphSize + 3.f;
				TextLeft = GlyphX + Padding * 0.5f;
			}
		}

		// An Event track's name follows the editor's own event icon - the one on a Blueprint's event nodes - and a Sound
		// track's a sound's. Under whichever name this engine version has it; none drawn if it has none.
		if (Layer && Layer->AnimType == EWidgetAnimType::Custom)
		{
			static const FSlateBrush* EventIcon = PickBrush({ "GraphEditor.Event_16x", "Graph.Event_16x", "Icons.Event", "GraphEditor.CustomEvent_16x" });
			static const FSlateBrush* SoundIcon = PickBrush({ "ClassIcon.SoundWave", "ClassIcon.SoundCue", "ClassIcon.SoundBase", "Icons.Audio" });
			const FSlateBrush* CueIcon = Layer->TypeParams.GetPtr<FWidgetAnimEventCueParams>() ? EventIcon
				: Layer->TypeParams.GetPtr<FWidgetAnimSoundCueParams>() ? SoundIcon
				: nullptr;
			const UWidgetAnimEditorSettings& Look = UWidgetAnimEditorSettings::Get();
			const float IconSizing = FMath::Min(Look.CueTrackIconSize, InnerHeight);
			if (CueIcon && GlyphX - BarX0 + IconSizing + Padding <= BarWidth)
			{
				const FLinearColor IconTint = bWashedOut ? SWidgetAnimTimeline::GetLockedColor(Look.CueTrackIconTint) : Look.CueTrackIconTint;
				Box(GlyphX, InnerTop + (InnerHeight - IconSizing) * 0.5f, IconSizing, IconSizing, IconTint, SectionOverlayLayer, CueIcon);
				GlyphX += IconSizing + 3.f;
				TextLeft = GlyphX + Padding * 0.5f;
			}
		}

		const TArray<float>& KeyTimes = Paint.KeyTimes;

		// A track some of whose segments have curves of their own has no one easing to name: each segment names
		// its own curve instead, small, in the middle of its stretch - see below.
		const bool bLabelSegments = Layer && WidgetAnimTimeline::LayerUsesSegmentCurves(*Layer) && WidgetAnimTimeline::LayerHasSegmentCurves(*Layer) && KeyTimes.Num() >= 2;

		// Inside it: its type and length on the left, its easing - or Spring - on the right, in the graph node
		// title's own text style. Whichever doesn't fit is dropped, the easing first, so text never spills out.
		// On a plain list, a track moving another widget than the list's own says which.
		FString Label = Paint.Title;
		FString EasingLabel = Paint.Secondary;
		const bool bCue = Layer && SWidgetAnimTimeline::IsCueLayer(*Layer);
		if (Layer)
		{
			// A value-driven track says so in place of its length, which only times its preview sweep.
			// One whose length its widget decides says what decides it, beside the length the preview works out.
			FString LengthLabel = bValueDriven
				? LOCTEXT("ValueDrivenSectionLength", "(Value Driven)").ToString()
				: (Owner->IsPercentTime() ? SWidgetAnimTimeline::FormatPercent(Span.Y)
					: Owner->IsFrameTime() ? SWidgetAnimTimeline::FormatFrames(Span.Y, SWidgetAnimTimeline::GetFrameRate()) + TEXT("f")
					: FString::Printf(TEXT("%.2fs"), Span.Y));
			if (bDerivedLength)
			{
				const FWidgetAnimEvaluator* Evaluator = Layer->FindEvaluator();
				const FText DerivedLabel = Evaluator ? Evaluator->GetDerivedLengthLabel(Layer->TypeParams) : FText::GetEmpty();
				if (!DerivedLabel.IsEmpty())
				{
					LengthLabel = FString::Printf(TEXT("%s (%s)"), *LengthLabel, *DerivedLabel.ToString());
				}
			}
			// A track limited to part of its text names the words it moves.
			FString TitleLabel = SWidgetAnimTimeline::GetLayerDisplayName(*Layer).ToString();
			if (Layer->TextTarget.IsSet())
			{
				TitleLabel = FString::Printf(TEXT("%s \u201C%s\u201D"), *TitleLabel, *Layer->TextTarget.Text);
			}
			Label = Paint.bNamesTarget
				? FString::Printf(TEXT("%s \u2192 %s  %s"), *TitleLabel, *Layer->TargetWidget.ToString(), *LengthLabel)
				: FString::Printf(TEXT("%s  %s"), *TitleLabel, *LengthLabel);

			// Animation Parameters: the parameters its fields play, after the title - "Slide 0.3s  ‹Distance, Side›".
			if (Layer->ParameterBindings.Num() > 0 || Layer->PlaysWhen.IsSet())
			{
				TArray<FString> Bound;
				for (const FWidgetAnimParameterBinding& Binding : Layer->ParameterBindings)
				{
					Bound.AddUnique(Binding.Parameter.ToString());
				}
				if (Layer->PlaysWhen.IsSet())
				{
					Bound.AddUnique(Layer->PlaysWhen.Parameter.ToString());
				}
				Label = FString::Printf(TEXT("%s  \u2039%s\u203A"), *Label, *FString::Join(Bound, TEXT(", ")));
			}

			// A cue has no easing - it fires at points, not along a curve - so it says what it does there instead.
			const FWidgetAnimEvaluator* CueEvaluator = bCue ? Layer->FindEvaluator() : nullptr;
			EasingLabel = CueEvaluator
				? CueEvaluator->GetTrackSummary(Layer->TypeParams).ToString()
				: SWidgetAnimTimeline::GetEasingName(*Layer).ToString();

			// An animation track says what its animation is for - its Description's first line - when it has one; one following
			// a float value always names the value instead, which is what moves it.
			if (Layer->IsAnimationTrack() && !Layer->IsValueDriven())
			{
				FString Description = Layer->InsertedAnimation->Description.ToString().TrimStartAndEnd();
				int32 LineEnd = INDEX_NONE;
				if (Description.FindChar(TEXT('\n'), LineEnd))
				{
					Description = Description.Left(LineEnd).TrimEnd();
				}
				if (!Description.IsEmpty())
				{
					EasingLabel = Description;
				}
			}
		}

		const FVector2f EasingSize = MeasureText(EasingLabel, LayerFont);
		const float TextOpacity = bWashedOut ? Palette::LockedBrightness + 0.2f : 1.f;
		const FLinearColor TitleColor = (Paint.bCollapsedLook ? Palette::CollapsedNodeText : NodeTitleStyle.ColorAndOpacity.GetSpecifiedColor()) * FLinearColor(1.f, 1.f, 1.f, TextOpacity);

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
			const bool bSlantSegmentText = Paint.bHidden && !ItalicOf(FCoreStyle::GetDefaultFontStyle("Regular", 9), SegmentFont);

			// On the title's baseline, so the two read as one line of text. Centering each by its measured height
			// doesn't: a line's height includes the room below the baseline for descenders, so a smaller font
			// centered that way sits visibly higher than a larger one beside it.
			const float TitleBaselineY = FullTextY + Measure->GetMaxCharacterHeight(LayerFont) + Measure->GetBaseline(LayerFont);
			const float SegmentAscent = Measure->GetMaxCharacterHeight(SegmentFont) + Measure->GetBaseline(SegmentFont);
			const float TitleRight = bTitleShown ? TextLeft + LabelSize.X + 8.f : TextLeft;
			const float KeyClearance = KeyDiamond * 0.5f + 4.f;

			for (int32 Segment = 0; Segment + 1 < KeyTimes.Num(); ++Segment)
			{
				const FWidgetAnimSegmentCurve* Curve = WidgetAnimTimeline::GetLayerSegmentCurve(*Layer, Segment);
				if (!Curve)
				{
					continue;
				}

				const FString CurveName = Curve->IsTrackEasing()
					? SWidgetAnimTimeline::GetEasingName(*Layer).ToString()
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

		// Only beside a title at its full size: once the title has to shrink, the easing gives way to it. An animation
		// track's - "Widget Animation, 3 tracks" - sits a little further in from its end, where its font's last letters
		// otherwise touch the section's rounded edge.
		const float RightPadding = Layer && Layer->IsAnimationTrack() ? Padding + Palette::AnimationTrackTextRightPadding : Padding;
		if (!bLabelSegments && bTitleFullSize && !EasingLabel.IsEmpty() && TextLeft - BarX0 + LabelSize.X + EasingSize.X + Padding + RightPadding + 12.f <= BarWidth)
		{
			TrackText(EasingLabel, BarX0 + BarWidth - RightPadding - EasingSize.X, TextY, LayerFont, TitleColor * FLinearColor(1.f, 1.f, 1.f, Palette::SectionSecondaryTextOpacity), bSlantLayerText);
		}

		// Keys: diamonds, button-like - normal, hovered (lighter, bigger), pressed (darker), selected (orange,
		// with a glow). A key that can't be edited is drawn grayed.
		const bool bKeysEditable = Paint.bKeysEditable;
		const float KeyY = InnerTop + InnerHeight * 0.5f;

		// A cue's points it fires at: a line down through the section under each one's key. A point it doesn't fire
		// at - its end, unless Fire At End - keeps its key, faded, so it can still be dragged to set the length.
		TArray<TPair<int32, float>, TInlineAllocator<8>> CuePoints;
		if (bCue)
		{
			WidgetAnimCues::GetFiringPoints(Layer->TypeParams, Layer->ValueKeys, CuePoints);
			const FLinearColor MarkerColor = bWashedOut ? SWidgetAnimTimeline::GetLockedColor(Palette::CueMarker) : Palette::CueMarker;
			for (const TPair<int32, float>& CuePoint : CuePoints)
			{
				if (KeyTimes.IsValidIndex(CuePoint.Key))
				{
					const float MarkerX = TimeToX(Span.X + KeyTimes[CuePoint.Key], Size.X);
					Box(MarkerX - Palette::CueMarkerWidth * 0.5f, InnerTop, Palette::CueMarkerWidth, InnerHeight, MarkerColor, SectionOverlayLayer);
				}
			}
		}

		for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
		{
			const float X = TimeToX(Span.X + KeyTimes[Key], Size.X);
			const bool bSilentCueKey = bCue && !CuePoints.ContainsByPredicate([Key](const TPair<int32, float>& CuePoint) { return CuePoint.Key == Key; });

			const bool bSelected = bInteractive && Owner->IsKeySelected(LayerIndex, Key);
			const bool bPressed = bInteractive && bKeysEditable && IsKeyPressed(LayerIndex, Key);
			const bool bHovered = bInteractive && !bPressed && Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.Key == Key;

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
			Color.A = bSilentCueKey && !bSelected ? Palette::CueSilentKeyOpacity : 1.f;

			if (bSelected)
			{
				Diamond(X, KeyY, KeyDiamond * Scale + 5.f, Palette::KeySelectedGlow, KeyGlowLayer);
			}
			Diamond(X, KeyY, KeyDiamond * Scale + 2.f, Palette::KeyOutline, KeyOutlineLayer);
			Diamond(X, KeyY, KeyDiamond * Scale, Color, KeyFillLayer);
		}
	};

	auto PaintLines = [&](bool bPinnedPass)
	{
		const float ClipTop = bPinnedPass ? RulerHeight : PinnedBottom;
		const float ClipBottom = bPinnedPass ? PinnedBottom : Size.Y;
		if (ClipBottom <= ClipTop)
		{
			return;
		}

		const FGeometry ClipGeometry = AllottedGeometry.MakeChild(FVector2f(Size.X, ClipBottom - ClipTop), FSlateLayoutTransform(FVector2f(0.f, ClipTop)));
		OutDrawElements.PushClip(FSlateClippingZone(ClipGeometry));

		// Where this pass's lines end: the bottom of its last line, or of the band.
		const float PassBottom = bPinnedPass ? PinnedBottom : FMath::Min(RowsBottom, Size.Y);

		auto IsLineVisible = [&](int32 LineIndex)
		{
			if ((LineIndex < NumPinned) != bPinnedPass)
			{
				return false;
			}
			const float Top = GetLineTop(LineIndex);
			return Top + RowHeight >= ClipTop && Top <= ClipBottom;
		};

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			if (!IsLineVisible(LineIndex))
			{
				continue;
			}

			const FTrackLine& Line = Lines[LineIndex];
			const float Y = GetLineTop(LineIndex);
			const bool bGroupLine = Line.Kind == FTrackLine::EKind::Group || Line.Kind == FTrackLine::EKind::NestedGroup;

			// Group lines read as headers, like Sequencer's folder rows; track lines alternate very gently. Pinned and loose
			// lines alternate by where they are, since they come from any group.
			const int32 Parity = Line.bPinned || Line.bLoose ? LineIndex : Line.Ordinal;
			const FLinearColor Base = bGroupLine
				? HeaderColor()
				: (Parity % 2 == 0 ? BackgroundColor() : FMath::Lerp(BackgroundColor(), RecessedColor(), 0.5f));
			Box(0.f, Y, Size.X, RowHeight, Base, RowLayer);

			// A pinned line is lifted a shade, so the band reads as apart from what scrolls under it.
			if (Line.bPinned)
			{
				Box(0.f, Y, Size.X, RowHeight, Palette::PinnedLineTint, RowLayer);
			}

			// An open animation track's lines are another animation's: tinted its orange, with an orange strip beside the
			// group's band, so they never read as this animation's own.
			if (Line.IsNested())
			{
				Box(0.f, Y, Size.X, RowHeight, Line.Kind == FTrackLine::EKind::NestedGroup ? Palette::NestedHeaderTint : Palette::NestedTint, RowLayer);
				Box(GroupBandWidth, Y, Palette::NestedEdgeWidth, RowHeight, Palette::NestedEdge, GridLayer);
			}

			const bool bGroupSelected = Line.Kind == FTrackLine::EKind::Group && Owner->IsGroupSelected(Line.Group);
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

			// A thin line under every line, as between Sequencer's tracks, and the group's color down its left edge - a loose
			// collapsed track's own color, since its group isn't shown.
			Box(0.f, Y + RowHeight - 1.f, Size.X, 1.f, SeparatorColor, RowLayer);
			Box(0.f, Y, GroupBandWidth, RowHeight, Line.bLoose ? Palette::LayerCollapsed : SWidgetAnimTimeline::GetGroupColor(Line.Group), GridLayer);
		}

		// The animation the list plays within: its tracks, grayed and out of reach, on rows of their own above the list's,
		// so the list's tracks can be placed against it. Nothing here can be selected, dragged or edited.
		const int32 NumContextRows = GetNumContextRows();
		const TArray<FWidgetAnimLayer>* ContextLayers = Owner->GetContextLayers();
		if (!bPinnedPass && NumContextRows > 0 && ContextLayers)
		{
			for (int32 Row = 0; Row < NumContextRows; ++Row)
			{
				const float Y = GetContextRowTop(Row);
				if (Y + RowHeight >= ClipTop && Y <= Size.Y)
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
				if (Y + RowHeight < ClipTop || Y > Size.Y)
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

				const FString ContextLabel = SWidgetAnimTimeline::GetLayerDisplayName(ContextLayer).ToString();
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
		const FLinearColor OutOfRangeShade = UWidgetAnimEditorSettings::Get().OutOfRangeShade;
		Box(0.f, ClipTop, StartX, PassBottom - ClipTop, OutOfRangeShade, GridLayer);
		Box(EndX, ClipTop, Size.X - EndX, PassBottom - ClipTop, OutOfRangeShade, GridLayer);

		// A percentage timeline ends at 100% - one second as stored - however far its tracks reach: past it is never played.
		if (Owner->IsPercentTime() && PassSeconds > 1.f + KINDA_SMALL_NUMBER)
		{
			const float HundredX = TimeToX(1.f, Size.X);
			Box(HundredX, ClipTop, EndX - HundredX, PassBottom - ClipTop, Palette::PercentPastEndShade, GridLayer);
		}

		for (int64 TickIndex = FirstTick; TickIndex * MinorStep <= ViewMax; ++TickIndex)
		{
			const float X = TimeToX(TickIndex * MinorStep, Size.X);
			const bool bMajor = ((TickIndex % MinorPerMajor) + MinorPerMajor) % MinorPerMajor == 0;
			Box(X, ClipTop, 1.f, PassBottom - ClipTop, bMajor ? UWidgetAnimEditorSettings::Get().GridMajorColor : UWidgetAnimEditorSettings::Get().GridMinorColor, GridLayer);
		}

		// ---- Group lines: name, span and key summary ----------------------------------------------------------

		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FTrackLine& Line = Lines[LineIndex];
			if (Line.Kind != FTrackLine::EKind::Group || !IsLineVisible(LineIndex))
			{
				continue;
			}

			const float LineTop = GetLineTop(LineIndex);
			const float CenterY = LineTop + RowHeight * 0.5f;
			const FVector2f GroupSpan = Owner->GetShownGroupSpan(Line.Group);
			const FLinearColor GroupColor = SWidgetAnimTimeline::GetGroupColor(Line.Group);

			if (GroupSpan.Y >= GroupSpan.X)
			{
				// The stretch the group is moving, as a parent row summarizes its children in Sequencer: a quiet band in its
				// (muted) color, edged above and below with a hairline of it, and closed at each end with a firmer cap - so
				// where the group starts and stops reads at a glance, and the keys over it stand out.
				const float X0 = TimeToX(GroupSpan.X, Size.X);
				const float X1 = TimeToX(GroupSpan.Y, Size.X);
				const float BandWidth = FMath::Max(X1 - X0, 4.f);
				const float BandHeight = FMath::Clamp(RowHeight * Palette::GroupBandHeightRatio, 8.f, 14.f);
				const float BandTop = FMath::RoundToFloat(CenterY - BandHeight * 0.5f);
				const FLinearColor BandColor = bReadOnly ? SWidgetAnimTimeline::GetLockedColor(GroupColor) : GroupColor;
				Box(X0, BandTop, BandWidth, BandHeight, WithAlpha(BandColor, Palette::GroupBandFillOpacity), FrameLayer, RoundedFill(2.f));
				Box(X0 + 1.f, BandTop, BandWidth - 2.f, 1.f, WithAlpha(BandColor, Palette::GroupBandEdgeOpacity), BodyLayer);
				Box(X0 + 1.f, BandTop + BandHeight - 1.f, BandWidth - 2.f, 1.f, WithAlpha(BandColor, Palette::GroupBandEdgeOpacity), BodyLayer);
				Box(X0, BandTop, Palette::GroupBandCapWidth, BandHeight, BandColor, BodyLayer, RoundedFill(2.f));
				Box(X0 + BandWidth - Palette::GroupBandCapWidth, BandTop, Palette::GroupBandCapWidth, BandHeight, BandColor, BodyLayer, RoundedFill(2.f));

				// Every key of the group's layers shown here, light and dark-outlined so they read on any group color. Only
				// the group's own layers, as this frame's layout has them - not every layer asked whether it is the group's.
				const bool bLayoutCurrent = Owner->IsTrackLayoutCurrent();
				const SWidgetAnimTimeline::FLayoutIndex& ShownIndex = Owner->GetTrackLayoutIndex();
				TArray<int32> AllLayers;
				if (!bLayoutCurrent && Layers)
				{
					for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
					{
						if (Owner->GetLayerGroup(LayerIndex) == Line.Group && Owner->IsLayerInView(LayerIndex))
						{
							AllLayers.Add(LayerIndex);
						}
					}
				}
				static const TArray<int32> NoLayers;
				const TArray<int32>& GroupLayers = !bLayoutCurrent ? AllLayers
					: ShownIndex.GroupMembers.IsValidIndex(Line.Group) ? ShownIndex.GroupMembers[Line.Group]
					: NoLayers;

				TArray<float> KeyTimes;
				for (const int32 LayerIndex : GroupLayers)
				{
					if (!Spans.IsValidIndex(LayerIndex) || (bLayoutCurrent && !ShownIndex.InView[LayerIndex]))
					{
						continue;
					}

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

			// While its target is being picked in the designer, it says so where the target is named.
			const bool bPicking = Owner->IsPickingTarget(Line.Group);
			// A group in a folder (see FWidgetAnimLayerGroup::EditorFolder) is named with it, as "Folder / Group".
			const FString GroupFolder = Owner->GetGroupFolder(Line.Group);
			const FString Name = GroupFolder.IsEmpty()
				? Owner->GetGroupName(Line.Group).ToString()
				: FString::Printf(TEXT("%s / %s"), *GroupFolder, *Owner->GetGroupName(Line.Group).ToString());
			const FString Target = bPicking
				? FString::Printf(TEXT("  %s"), *Owner->GetPickingTargetText().ToString())
				: FString::Printf(TEXT("  %s"), *Owner->GetGroupTargetLabel(Line.Group).ToString());
			const FVector2f NameSize = MeasureText(Name, GroupFont);
			const FVector2f TargetSize = MeasureText(Target, GroupTargetFont);
			const float NameX = GroupSpan.Y >= GroupSpan.X
				? TimeToX(GroupSpan.Y, Size.X) + 8.f
				: GetFoldX(Line.Group, Size.X) + FoldArrowSize + 4.f;
			Text(Name, NameX, CenterY - NameSize.Y * 0.5f, GroupFont, WithAlpha(ForegroundColor(), 0.7f), LabelLayer + 1);
			Text(Target, NameX + NameSize.X, CenterY - TargetSize.Y * 0.5f, GroupTargetFont, bPicking ? Palette::PickingTargetText : WithAlpha(ForegroundColor(), 0.4f), LabelLayer + 1);
		}

		// ---- Track lines: sections ----------------------------------------------------------------------------

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
				const bool bTrackLocked = Owner->IsLayerLocked(LayerIndex);
				const bool bTrackHidden = Owner->IsLayerHidden(LayerIndex);

				FSectionPaint Paint;
				Paint.LayerIndex = LayerIndex;
				Paint.Rect = GetSectionRect(LayerIndex, Size.X);
				Paint.Span = GetLayerSpan(LayerIndex);
				Paint.bSelected = Owner->IsLayerSelected(LayerIndex);
				Paint.bLocked = bTrackLocked;
				Paint.bHidden = bTrackHidden;
				Paint.bPinned = Owner->IsLayerPinned(LayerIndex);
				Paint.bWashedOut = bReadOnly || bTrackLocked || bTrackHidden;

				// A collapsed track: its name and how many tracks it holds, across their stretch - plain, so it doesn't clutter.
				if (IsCollapsedBar(LayerIndex))
				{
					const FName CollapsedName = Owner->GetLayerCollapsedName(LayerIndex);
					const SWidgetAnimTimeline::FCollapsedState Collapsed = Owner->GetCollapsedState(CollapsedName);

					Paint.Color = Palette::LayerCollapsed;
					Paint.bCollapsedLook = true;
					Paint.Glyph = CollapsedBrush;
					Paint.bSelected = Collapsed.bAnySelected;
					Paint.bHidden = Collapsed.bAllHidden;
					Paint.bLocked = Collapsed.bAllLocked;
					Paint.bWashedOut = bReadOnly || Collapsed.bAllHidden || Collapsed.bAllLocked;
					Paint.Title = Owner->IsPercentTime()
						? FString::Printf(TEXT("%s  %s"), *CollapsedName.ToString(), *SWidgetAnimTimeline::FormatPercent(Paint.Span.Y))
						: Owner->IsFrameTime() ? FString::Printf(TEXT("%s  %sf"), *CollapsedName.ToString(), *SWidgetAnimTimeline::FormatFrames(Paint.Span.Y, SWidgetAnimTimeline::GetFrameRate()))
						: FString::Printf(TEXT("%s  %.2fs"), *CollapsedName.ToString(), Paint.Span.Y);
					Paint.Secondary = FText::Format(LOCTEXT("CollapsedSectionCount", "{0} {0}|plural(one=track,other=tracks)"), FText::AsNumber(Collapsed.NumMembers)).ToString();
					DrawSection(Paint);
					continue;
				}

				Paint.Layer = &Layer;
				Owner->GetKeyTimes(LayerIndex, Paint.KeyTimes);
				Paint.FanSeconds = Owner->GetLayerFanSeconds(LayerIndex);
				Paint.FanCount = Owner->GetLayerFanCount(LayerIndex);
				Paint.bKeysEditable = Owner->CanEditKeys(LayerIndex);
				Paint.bInverted = Owner->IsLayerInverted(LayerIndex);
				Paint.bNamesTarget = Owner->GetSource() && Owner->GetSource()->SupportsLayerTargets() && !Layer.TargetWidget.IsNone();
				DrawSection(Paint);
			}
		}

		// ---- An open animation track's lines: its animation's groups and tracks, shown only ------------------------

		const FSlateFontInfo NestedGroupFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FTrackLine& Line = Lines[LineIndex];
			const TArray<FVector2f>* NestedSpans = Line.IsNested() ? Owner->GetNestedSpans(Line.AnimationLayer) : nullptr;
			const UWidgetAnimPresetAsset* Animation = Line.IsNested() ? Owner->GetInsertedAnimation(Line.AnimationLayer) : nullptr;
			if (!NestedSpans || !Animation || !IsLineVisible(LineIndex))
			{
				continue;
			}

			const float LineTop = GetLineTop(LineIndex);
			const float CenterY = LineTop + RowHeight * 0.5f;

			if (Line.Kind == FTrackLine::EKind::NestedGroup)
			{
				// Its fold arrow, and its name - the animation's own name for it - where the track starts, over a slim bar
				// of the stretch the group moves, in the animation orange.
				FVector2f GroupSpan(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
				for (int32 Index = 0; Index < Animation->Layers.Num() && NestedSpans->IsValidIndex(Index); ++Index)
				{
					if (UWidgetAnimLibrary::GetLayerGroupIndex(Animation->Layers[Index], Animation->LayerGroups.Num()) == Line.NestedGroup || Animation->LayerGroups.Num() == 0)
					{
						GroupSpan.X = FMath::Min(GroupSpan.X, (*NestedSpans)[Index].X);
						GroupSpan.Y = FMath::Max(GroupSpan.Y, (*NestedSpans)[Index].X + (*NestedSpans)[Index].Y);
					}
				}
				if (GroupSpan.Y >= GroupSpan.X)
				{
					const float X0 = TimeToX(GroupSpan.X, Size.X);
					const float X1 = TimeToX(GroupSpan.Y, Size.X);
					Box(X0, CenterY + RowHeight * 0.25f, FMath::Max(X1 - X0, 4.f), 3.f, WithAlpha(Palette::LayerAnimation, 0.45f), FrameLayer, RoundedFill(1.5f));
				}

				const float FoldX = GetNestedFoldX(Line, Size.X);
				const bool bFoldHovered = Hover.Line == LineIndex && Hover.bOnFold && Drag == EDrag::None;
				if (const FSlateBrush* Arrow = Owner->IsNestedGroupFolded(Line.AnimationLayer, Line.NestedGroup)
					? PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" })
					: PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" }))
				{
					Box(FoldX, CenterY - FoldArrowSize * 0.5f, FoldArrowSize, FoldArrowSize, WithAlpha(ForegroundColor(), bFoldHovered ? 0.95f : 0.45f), LabelLayer + 1, Arrow);
				}

				const FString GroupName = Animation->LayerGroups.IsValidIndex(Line.NestedGroup)
					? Animation->LayerGroups[Line.NestedGroup].GetDisplayName(Line.NestedGroup)
					: FWidgetAnimLayerGroup::MakeDefaultName(Line.NestedGroup);
				const FString Label = FString::Printf(TEXT("%s \u203A %s"), *Animation->GetName(), *GroupName);
				const FVector2f LabelSize = MeasureText(Label, NestedGroupFont);
				Text(Label, FoldX + FoldArrowSize + 4.f, CenterY - LabelSize.Y * 0.5f, NestedGroupFont, WithAlpha(Palette::LayerAnimation, 0.85f), LabelLayer + 1);
				continue;
			}

			// Its tracks: drawn as any track is - name, length, easing, keys - but grayed, and with nothing to grab: they are
			// the inserted animation's, edited there, or here once the track is unraveled.
			const TArray<FWidgetAnimLayer>* OwnerLayers = Owner->GetLayers();
			const float NestedKeyScale = TimeScale / (OwnerLayers && OwnerLayers->IsValidIndex(Line.AnimationLayer)
				? UWidgetAnimLibrary::GetInsertedPlaySpeed((*OwnerLayers)[Line.AnimationLayer])
				: FMath::Max(Animation->AnimationSpeedMultiplier, 0.01f));
			for (const int32 Index : Line.NestedLayers)
			{
				if (!Animation->Layers.IsValidIndex(Index) || !NestedSpans->IsValidIndex(Index))
				{
					continue;
				}

				const FWidgetAnimLayer& NestedLayer = Animation->Layers[Index];
				const FVector2f Span = (*NestedSpans)[Index];
				const float Top = LineTop + 3.f;
				const float Left = TimeToX(Span.X, Size.X);
				const float Right = Left + FMath::Max(TimeToX(Span.X + Span.Y, Size.X) - Left, 4.f);

				FSectionPaint Paint;
				Paint.Layer = &NestedLayer;
				Paint.Rect = FBox2f(FVector2f(Left, Top), FVector2f(Right, Top + RowHeight - 7.f));
				Paint.Span = Span;
				Paint.bHidden = NestedLayer.bHiddenInEditor;
				Paint.bWashedOut = true;
				Paint.bInverted = NestedLayer.bInvertTrack || (Owner->GetSource() && Owner->GetSource()->GetInvert());
				WidgetAnimTimeline::GetLayerKeyTimes(NestedLayer, Span.Y, NestedKeyScale, Paint.KeyTimes);
				DrawSection(Paint);
			}
		}

		OutDrawElements.PopClip();
	};

	PaintLines(/*bPinnedPass=*/false);
	PaintLines(/*bPinnedPass=*/true);

	// The pinned band's lower edge, so what scrolls under it reads as going under it.
	if (NumPinned > 0)
	{
		Box(0.f, PinnedBottom - 1.f, Size.X, 1.f, Palette::PinnedDivider, RangeLayer);
	}

	// Everything drawn over the lines from here on stays below the ruler.
	const FGeometry RowsGeometry = AllottedGeometry.MakeChild(
		FVector2f(Size.X, FMath::Max(Size.Y - RulerHeight, 0.f)),
		FSlateLayoutTransform(FVector2f(0.f, RulerHeight)));
	OutDrawElements.PushClip(FSlateClippingZone(RowsGeometry));

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

	// A collapsed track open on its own: the edges darkened toward the frame, and a thin line of its color around it - a
	// nested editor, as a collapsed graph reads as one. Over the tracks, under the playhead.
	if (Owner->IsInCollapsedScope())
	{
		const float Reach = FMath::Min(Palette::ScopeVignetteWidth, FMath::Min(Size.X, Size.Y - RulerHeight) * 0.5f);
		if (Reach > 1.f)
		{
			const FLinearColor Dark = Palette::ScopeVignette;
			const FLinearColor Clear = Dark.CopyWithNewOpacity(0.f);
			const float FieldHeight = Size.Y - RulerHeight;

			// One gradient per edge, darkest at the edge. Orient_Vertical runs the stops along X, Orient_Horizontal along Y.
			auto Edge = [&](float X, float Y, float W, float H, EOrientation Orientation, bool bDarkFirst)
			{
				const float Length = Orientation == Orient_Vertical ? W : H;
				TArray<FSlateGradientStop> Stops;
				Stops.Add(FSlateGradientStop(FVector2D(0.0, 0.0), bDarkFirst ? Dark : Clear));
				Stops.Add(FSlateGradientStop(Orientation == Orient_Vertical ? FVector2D(Length, 0.0) : FVector2D(0.0, Length), bDarkFirst ? Clear : Dark));
				FSlateDrawElement::MakeGradient(
					OutDrawElements,
					LabelLayer + 2,
					AllottedGeometry.ToPaintGeometry(FVector2f(W, H), FSlateLayoutTransform(FVector2f(X, Y))),
					MoveTemp(Stops),
					Orientation,
					ESlateDrawEffect::None);
			};
			Edge(0.f, RulerHeight, Reach, FieldHeight, Orient_Vertical, /*bDarkFirst=*/true);
			Edge(Size.X - Reach, RulerHeight, Reach, FieldHeight, Orient_Vertical, /*bDarkFirst=*/false);
			Edge(0.f, RulerHeight, Size.X, Reach, Orient_Horizontal, /*bDarkFirst=*/true);
			Edge(0.f, Size.Y - Reach, Size.X, Reach, Orient_Horizontal, /*bDarkFirst=*/false);
		}

		Box(0.f, RulerHeight, Size.X, 1.f, Palette::ScopeFrame, LabelLayer + 3);
		Box(0.f, Size.Y - 1.f, Size.X, 1.f, Palette::ScopeFrame, LabelLayer + 3);
		Box(0.f, RulerHeight, 1.f, Size.Y - RulerHeight, Palette::ScopeFrame, LabelLayer + 3);
		Box(Size.X - 1.f, RulerHeight, 1.f, Size.Y - RulerHeight, Palette::ScopeFrame, LabelLayer + 3);
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
			Text(FormatSeconds(FMath::IsNearlyZero(Tick, MinorStep * 0.25f) ? 0.f : Tick, Step, Owner->IsPercentTime(), bFrameTime ? FrameRate : 0.f), X + 4.f, 4.f, RulerFont, RulerText, RulerLayer + 1);
		}
		else
		{
			Box(X, RulerHeight - 4.f, 1.f, 4.f, WithAlpha(RulerTick, 0.2f), RulerLayer + 1);
		}
	}

	// With no animation - no timeline made yet, no asset picked - there is nothing to scrub or bound: no playhead, no start
	// and end.
	if (Owner->HasAnimation())
	{
		// Start and end of the animation, as Godot marks a time: a solid line down through the tracks - pure green at the start,
		// pure red at the end - under an upside-down triangle in the strip along the ruler's bottom, the only place either is
		// grabbed, lit while the mouse is on it or it is being dragged. Below the playhead's head, so the playhead sitting on
		// either never covers its triangle, and the two never take each other's clicks - see RangeEdgeAt.
		const UWidgetAnimEditorSettings& Look = UWidgetAnimEditorSettings::Get();
		const float LineThickness = FMath::Max(Look.RangeLineThickness, 1.f);
		if (Owner->IsPercentTime() && PassSeconds > 1.f + KINDA_SMALL_NUMBER)
		{
			Box(TimeToX(1.f, Size.X), RulerHeight, 1.f, Size.Y - RulerHeight, Palette::PercentEndLine, RangeLineLayer);
		}
		{
			const bool bCanDragRange = Owner->CanEdit() && Layers && Layers->Num() > 0;
			const bool bDraggingRange = Drag == EDrag::Range || Drag == EDrag::RangePending;
			const bool bStartLit = bDraggingRange ? !bRangeEnd : (Drag == EDrag::None && HoverRange.IsSet() && !HoverRange.GetValue());
			const bool bEndLit = bDraggingRange ? bRangeEnd : (Drag == EDrag::None && HoverRange.IsSet() && HoverRange.GetValue());

			// One shape: the triangle - its flat side along the top, its point where the tracks start - and the line, which
			// runs up into its middle and down through the tracks. Laid out together on whole pixels (SnapColumn), so at any
			// zoom the line is the same width and right under the triangle's point. A fixed range - the panel locked, or
			// nothing to bound - gets a smaller triangle. Lit - line and all - a little bigger and lighter.
			auto Marker = [&](float LineX, bool bEnd, bool bLit)
			{
				const FLinearColor Base = bEnd ? Look.RangeEndColor : Look.RangeStartColor;
				const FLinearColor Color = bLit ? FMath::Lerp(Base, FLinearColor::White, Look.RangeHoverLighten) : Base;
				const float Scale = bCanDragRange ? (bLit ? 1.15f : 1.f) : 0.6f;
				const float Height = FMath::Min(Look.RangeTriangleHeight * Scale, SWidgetAnimTimeline::RangeStripHeight);
				const WidgetAnimTimeline::FPixelColumn Column = WidgetAnimTimeline::SnapColumn(AllottedGeometry, LineX + 0.5f, LineThickness, Look.RangeTriangleWidth * Scale);
				const float Point = WidgetAnimTimeline::SnapY(AllottedGeometry, RulerHeight);
				const float Top = WidgetAnimTimeline::SnapY(AllottedGeometry, RulerHeight - Height);
				const float LineTop = WidgetAnimTimeline::SnapY(AllottedGeometry, (Top + Point) * 0.5f);

				// Into the triangle over the ruler; through the tracks under the keys.
				const FLinearColor LineColor = WithAlpha(Color, Color.A * Look.RangeLineOpacity);
				Box(Column.LineLeft, LineTop, Column.LineWidth, FMath::Max(Point - LineTop, 0.f), LineColor, RangeLayer);
				Box(Column.LineLeft, Point, Column.LineWidth, Size.Y - Point, LineColor, RangeLineLayer);
				WidgetAnimTimeline::MakeDownTriangle(OutDrawElements, RangeLayer + 1, AllottedGeometry, Column.HeadLeft, Top, Column.HeadWidth, Point - Top, Color);
			};
			Marker(StartX, /*bEnd=*/false, bStartLit);
			Marker(EndX, /*bEnd=*/true, bEndLit);
		}

		if (const TOptional<float> Snap = Owner->GetSnapIndicator(); Snap.IsSet())
		{
			Box(TimeToX(Snap.GetValue(), Size.X) - 3.f, RulerHeight - 6.f, 7.f, 6.f, Palette::SnapLine, RangeLayer, RoundedFill(2.f));
		}

		// The playhead: its tag in the top of the ruler reading the time, and its line down through the tracks, fading and
		// tapering as it goes - drawn the same in the Graph Editor.
		const float PlayheadSeconds = Owner->GetPlayheadSeconds();
		SWidgetAnimTimeline::PaintPlayhead(OutDrawElements, AllottedGeometry, PlayheadLayer, TimeToX(PlayheadSeconds, Size.X), RulerHeight, Size.Y, 0.f, Size.X, Owner->FormatPlayheadTag(PlayheadSeconds));
	}

	// When the lines run past the bottom: a slim bar down the right edge showing where they are scrolled to.
	const float MaxScroll = GetMaxScrollY(Size.Y);
	if (MaxScroll > 0.f && !(Owner && Owner->IsInline()) && UWidgetAnimEditorSettings::Get().bShowVerticalScrollBarWhenNeeded)
	{
		const float TrackHeight = Size.Y - PinnedBottom - 4.f;
		const float ContentHeight = TrackHeight + MaxScroll;
		const float ThumbHeight = FMath::Max(TrackHeight * TrackHeight / ContentHeight, 16.f);
		const float ThumbY = PinnedBottom + 2.f + (TrackHeight - ThumbHeight) * (ScrollY / MaxScroll);
		Box(Size.X - 5.f, ThumbY, 3.f, ThumbHeight, Palette::ScrollIndicator, PlayheadLayer + 4, RoundedFill(2.f));
	}

	return PlayheadLayer + 4;
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
		// On the animation's start or end: drag it - once the mouse moves. Let go in place, it was a click, which scrubs.
		const TOptional<bool> RangeEdge = RangeEdgeAt(Local, Width);
		if (RangeEdge.IsSet())
		{
			bRangeEnd = RangeEdge.GetValue();
			Drag = EDrag::RangePending;
			return Reply;
		}

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
		MarqueeStartTime = XToTime(Local.X, Width);
		MarqueeStartScrollY = ScrollY;
		bMarqueeStartPinned = Local.Y < GetPinnedBottom();
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
			// Shift and Ctrl / Cmd pick groups as the list does.
			Owner->SelectGroupWithModifiers(Line->Group, bRange, bToggle);
		}
		return Reply;
	}

	// An open animation track's lines: a group's arrow folds it; anywhere else picks the animation track they belong to.
	if (Line->IsNested())
	{
		if (Hit.bOnFold)
		{
			Owner->ToggleNestedGroupFolded(Line->AnimationLayer, Line->NestedGroup);
		}
		else
		{
			Owner->SelectLayer(Line->AnimationLayer);
		}
		return Reply;
	}

	// A collapsed track: picked whole - every one of its tracks - and moved whole. Shift or Ctrl / Cmd adds it or takes it
	// out; a plain press on it while it is part of a larger selection keeps that, to drag it all - unless it is a click.
	if (Hit.bCollapsed && Hit.Layer != INDEX_NONE)
	{
		const FName CollapsedName = Owner->GetLayerCollapsedName(Hit.Layer);
		if (bRange || bToggle)
		{
			Owner->SelectCollapsed(CollapsedName, /*bToggle=*/true);
			return Reply;
		}

		PressLayer = Hit.Layer;
		PressTime = XToTime(Local.X, Width);
		if (Owner->IsMultiSelection() && Owner->IsLayerSelected(Hit.Layer))
		{
			bCollapseSelectionOnRelease = true;
		}
		else
		{
			Owner->SelectCollapsed(CollapsedName);
		}

		if (Owner->CanMoveLayer(Hit.Layer))
		{
			PendingLayerDrag = ELayerDrag::Move;
			Drag = EDrag::LayerPending;
		}
		return Reply;
	}

	// An animation track's arrow: open it or close it, and nothing else.
	if (Hit.bOnFold && Hit.Layer != INDEX_NONE && !bRange && !bToggle)
	{
		Owner->ToggleAnimationTrackExpanded(Hit.Layer);
		return Reply;
	}

	// A key with Shift or Ctrl / Cmd: one more key, or one fewer - keys of any tracks, picked together. Shift on a key while
	// tracks are what is picked ranges over the tracks instead: a short track is nearly all keys, and a Shift-click meant
	// for it landed on one.
	if (Hit.Key != INDEX_NONE && (bToggle || (bRange && Owner->GetSelectedKeys().Num() > 0)))
	{
		Owner->SelectKeyWithModifiers(Hit.Layer, Hit.Key, bToggle);
		return Reply;
	}

	// Shift and Ctrl / Cmd pick layers the way a file browser picks files, and do nothing else - Shift ranging over the
	// tracks as they are shown here, from the one clicked before down (or up) to this one.
	if (bRange || bToggle)
	{
		TArray<int32> Order;
		Owner->GetTrackEditorOrder(Order);
		Owner->SelectLayerWithModifiers(Hit.Layer, bRange, bToggle, &Order);
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

	case EDrag::Range:
		Owner->UpdateRangeDrag(XToTime(Local.X, Width), GetSnapThresholdSeconds(Width));
		break;

	case EDrag::Marquee:
		MarqueeEnd = Local;
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
		HoverRange = RangeEdgeAt(Local, Width);

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
	case EDrag::Range:
		LastDragLocal = Local;
		ApplyDragAt(Local, Width);
		break;

	case EDrag::RangePending:
		if (bPastThreshold)
		{
			// Can't be dragged - nothing to trim, or read-only - so it scrubs as the ruler does anywhere else.
			if (Owner->BeginRangeDrag(bRangeEnd))
			{
				Drag = EDrag::Range;
				LastDragLocal = Local;
				ApplyDragAt(Local, Width);
			}
			else
			{
				Drag = EDrag::Scrub;
				Owner->ScrubToSeconds(XToTime(Local.X, Width));
			}
		}
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
		LastDragLocal = Local;
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

	if (Owner && (Ended == EDrag::Layer || Ended == EDrag::Range))
	{
		Owner->EndLayerDrag();
	}

	// The lines' new height, remembered once they are let go rather than on every step of the drag.
	if (Owner && Ended == EDrag::ResizeRows)
	{
		Owner->SaveRowHeight();
	}

	// The animation's start or end pressed and let go in place: a click on the ruler, which scrubs there.
	if (Owner && Ended == EDrag::RangePending)
	{
		Owner->ScrubToSeconds(XToTime(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()).X, MyGeometry.GetLocalSize().X));
	}

	// Pressed on one of several selected layers and let go without dragging: a click, which selects it alone - or, on a
	// collapsed track, every one of its tracks.
	if (Owner && bCollapseSelectionOnRelease && (Ended == EDrag::LayerPending || Ended == EDrag::None))
	{
		if (IsCollapsedBar(PressLayer))
		{
			Owner->SelectCollapsed(Owner->GetLayerCollapsedName(PressLayer));
		}
		else
		{
			Owner->SelectLayer(PressLayer);
		}
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
				// One of several selected groups keeps them all, for a menu about all of them.
				if (!Owner->IsGroupSelected(Line->Group))
				{
					Owner->SelectGroup(Line->Group);
				}
				PushMenu(MouseEvent, Owner->MakeGroupContextMenu(Line->Group, ClickSeconds));
			}
			else if (Line->IsNested())
			{
				// An open animation track's lines are the track's: its menu, Unravel among it.
				if (!Owner->IsLayerSelected(Line->AnimationLayer))
				{
					Owner->SelectLayer(Line->AnimationLayer);
				}
				PushMenu(MouseEvent, Owner->MakeLayerContextMenu(Line->AnimationLayer, INDEX_NONE, TOptional<float>(), ClickSeconds));
			}
			else if (Hit.Layer == INDEX_NONE)
			{
				// An empty stretch of a group's line: add to that group, and only that group.
				PushMenu(MouseEvent, Owner->MakeAddMenu(Line->Group, /*bWithinGroup=*/true, ClickSeconds));
			}
			else
			{
				// A right-click on one of several selected layers - or keys - keeps them all, so the menu acts on all of them.
				// A collapsed track's is about every one of its tracks.
				if (Hit.Key != INDEX_NONE)
				{
					if (!Owner->IsKeySelected(Hit.Layer, Hit.Key))
					{
						Owner->SelectKey(Hit.Layer, Hit.Key);
					}
				}
				else if (Hit.bCollapsed)
				{
					if (!Owner->IsLayerSelected(Hit.Layer))
					{
						Owner->SelectCollapsed(Owner->GetLayerCollapsedName(Hit.Layer));
					}
				}
				else if (!Owner->IsLayerSelected(Hit.Layer))
				{
					Owner->SelectLayer(Hit.Layer);
				}

				const TOptional<float> LocalSeconds = Hit.bOnSection && !Hit.bCollapsed
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

	// Anywhere: the playhead goes to that moment - and only that: a key is added from a track's menu, never by a
	// double-click, which is for moving the playhead. A group's fold arrow has already done its job on the first click.
	// A click on the fold arrow soon after another click arrives here rather than as a press - which is what
	// made folding take two clicks - so it folds here too.
	const FHit Hit = HitTest(Local, Width);

	// A collapsed track opens on its own - only its tracks, and the preview only playing them - as a collapsed graph does.
	if (Hit.bCollapsed && Hit.Layer != INDEX_NONE)
	{
		Owner->EnterCollapsed(Owner->GetLayerCollapsedName(Hit.Layer));
		return FReply::Handled();
	}

	if (Hit.bOnFold)
	{
		if (const FTrackLine* Line = GetLine(Hit.Line))
		{
			if (Line->Kind == FTrackLine::EKind::NestedGroup)
			{
				Owner->ToggleNestedGroupFolded(Line->AnimationLayer, Line->NestedGroup);
			}
			else if (Line->Kind == FTrackLine::EKind::Lane && Hit.Layer != INDEX_NONE)
			{
				Owner->ToggleAnimationTrackExpanded(Hit.Layer);
			}
			else
			{
				Owner->ToggleTrackGroupCollapsed(Line->Group);
			}
		}
		return FReply::Handled();
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
	case EDrag::Range:
	case EDrag::RangePending:
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

	// A collapsed track opens on a double-click: the pointing hand says it can be.
	if (Hover.bCollapsed && Hover.Layer != INDEX_NONE)
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

// =====================================================================================================
// Material tracks, checked against the preview: the parameters of the widget's material, and what keeps a track from
// driving one
// =====================================================================================================

namespace WidgetAnimMaterialCheck
{
	struct FParameter
	{
		FName Name;
		float Scalar = 0.f;
		FLinearColor Vector = FLinearColor::Black;
	};

	/** Where the defaults are: under a dynamic instance - such as the one the preview made when it posed the widget - its parent. */
	UMaterialInterface* GetDefaultsMaterial(UMaterialInterface* Material)
	{
		while (const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material))
		{
			if (!Dynamic->Parent)
			{
				break;
			}
			Material = Dynamic->Parent;
		}
		return Material;
	}

	/** Material's scalar or vector parameters, by name, each with its default value. */
	void GatherParameters(UMaterialInterface* Material, bool bVector, TArray<FParameter>& OutParameters)
	{
		OutParameters.Reset();
		Material = GetDefaultsMaterial(Material);
		if (!Material)
		{
			return;
		}

		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		if (bVector)
		{
			Material->GetAllVectorParameterInfo(Infos, Ids);
		}
		else
		{
			Material->GetAllScalarParameterInfo(Infos, Ids);
		}

		for (const FMaterialParameterInfo& Info : Infos)
		{
			// The material's own parameters: a material layer's are named through the layer, which a track can't do.
			if (Info.Association != EMaterialParameterAssociation::GlobalParameter
				|| OutParameters.ContainsByPredicate([&Info](const FParameter& Existing) { return Existing.Name == Info.Name; }))
			{
				continue;
			}

			FParameter& Parameter = OutParameters.AddDefaulted_GetRef();
			Parameter.Name = Info.Name;
			const FHashedMaterialParameterInfo Hashed(Info);
			if (bVector)
			{
				Material->GetVectorParameterValue(Hashed, Parameter.Vector);
			}
			else
			{
				Material->GetScalarParameterValue(Hashed, Parameter.Scalar);
			}
		}

		OutParameters.Sort([](const FParameter& A, const FParameter& B) { return A.Name.LexicalLess(B.Name); });
	}

	FText GetSlotName(EWidgetAnimMaterialSlot Slot)
	{
		const UEnum* SlotEnum = StaticEnum<EWidgetAnimMaterialSlot>();
		return SlotEnum ? SlotEnum->GetDisplayNameTextByValue(static_cast<int64>(Slot)) : FText::GetEmpty();
	}

	/**
	 * What keeps Layer - a material track - from driving anything on Widget, the widget it plays on in the preview: no such
	 * material on that kind of widget, none set, no parameter picked, or one the material doesn't have. Empty when nothing
	 * does, for any other kind of track, and with no preview to check against (Widget null).
	 */
	FText Check(const UWidget* Widget, const FWidgetAnimLayer& Layer)
	{
		const bool bVector = Layer.AnimType == EWidgetAnimType::MaterialVector;
		if ((!bVector && Layer.AnimType != EWidgetAnimType::MaterialScalar) || !Widget)
		{
			return FText::GetEmpty();
		}

		const FText WidgetName = FText::FromString(Widget->GetName());
		const FText SlotName = GetSlotName(Layer.MaterialSlot);
		if (!UWidgetAnimLibrary::WidgetHasMaterialSlot(Widget, Layer.MaterialSlot))
		{
			return FText::Format(WidgetAnimTips::MaterialNoSlotNote(), WidgetName, Widget->GetClass()->GetDisplayNameText(), SlotName);
		}

		UMaterialInterface* Material = UWidgetAnimLibrary::FindWidgetMaterial(Widget, Layer.MaterialSlot);
		if (!Material)
		{
			return FText::Format(WidgetAnimTips::MaterialNotSetNote(), WidgetName, SlotName);
		}

		TArray<FParameter> Parameters;
		GatherParameters(Material, bVector, Parameters);
		const FText MaterialName = FText::FromString(GetDefaultsMaterial(Material)->GetName());
		const FText Kind = bVector ? LOCTEXT("MaterialVectorKind", "vector") : LOCTEXT("MaterialScalarKind", "scalar");
		if (Parameters.Num() == 0)
		{
			return FText::Format(WidgetAnimTips::MaterialNoParametersNote(), MaterialName, Kind);
		}

		const FName Parameter = bVector ? Layer.VectorParameterName : Layer.ScalarParameterName;
		if (Parameter.IsNone())
		{
			return FText::Format(WidgetAnimTips::MaterialNoParameterNote(), MaterialName);
		}
		if (!Parameters.ContainsByPredicate([Parameter](const FParameter& Existing) { return Existing.Name == Parameter; }))
		{
			return FText::Format(WidgetAnimTips::MaterialMissingParameterNote(), MaterialName, Kind, FText::FromName(Parameter));
		}
		return FText::GetEmpty();
	}
}

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

		// The animation's parameters, under whatever is selected - see CustomizeParameters.
		ON_SCOPE_EXIT
		{
			CustomizeParameters(DetailBuilder);
		};

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
				// Several groups: a word that the settings shown are one of them, and where to act on all of them.
				const int32 NumSelectedGroups = Owner->GetSelectedGroups().Num();
				if (NumSelectedGroups > 1)
				{
					IDetailCategoryBuilder& Note = DetailBuilder.EditCategory("WidgetAnimSelection_Groups", LOCTEXT("GroupsCategory", "Layer Groups"));
					AddNoteRow(Note, FText::Format(WidgetAnimTips::MultipleGroupsSelectedNote(), FText::AsNumber(NumSelectedGroups), Owner->GetGroupName(Selection.Group)));
				}
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
		const TSharedPtr<IPropertyHandle> LayerHandle = LayersHandle->IsValidHandle() ? LayersHandle->GetChildHandle(static_cast<uint32>(Owner->GetDetailsLayerIndex())) : nullptr;
		if (!LayerHandle.IsValid() || !LayerHandle->IsValidHandle())
		{
			CustomizeAnimation(DetailBuilder, Edited);
			return;
		}

		const TArray<FWidgetAnimLayer>* Layers = Owner->GetLayers();
		const bool bIsTransform = Layers
			&& Layers->IsValidIndex(Selection.Layer)
			&& WidgetAnimTypeIsKeyframed((*Layers)[Selection.Layer].AnimType);
		const bool bIsValueKeyed = Layers
			&& Layers->IsValidIndex(Selection.Layer)
			&& WidgetAnimTypeSupportsValueKeys((*Layers)[Selection.Layer].AnimType);

		if (Selection.Key != INDEX_NONE && bIsTransform)
		{
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(Selection.Layer, KeyTimes);
			CustomizeKey(DetailBuilder, LayerHandle.ToSharedRef(), Selection.Layer, Selection.Key, KeyTimes.Num(), (*Layers)[Selection.Layer]);
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

	/**
	 * Animation Parameters: the bind chip at the end of a track field's row, after its value - when the field can take a
	 * parameter (see SWidgetAnimTimeline::MakeParameterBindChip). The row keeps its own name and value widgets, and its
	 * children.
	 */
	static void AddBindChip(IDetailPropertyRow& Row, const TSharedRef<IPropertyHandle>& Handle)
	{
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		FDetailWidgetRow DefaultRow;
		Row.GetDefaultWidgets(NameWidget, ValueWidget, DefaultRow);
		if (!NameWidget.IsValid() || !ValueWidget.IsValid())
		{
			return;
		}
		const TSharedPtr<SWidget> Chip = SWidgetAnimTimeline::MakeParameterBindChip(Handle, &ValueWidget);
		if (!Chip.IsValid())
		{
			return;
		}
		Row.CustomWidget(/*bShowChildren=*/true)
			.NameContent()
			.MinDesiredWidth(DefaultRow.NameWidget.MinWidth)
			.MaxDesiredWidth(DefaultRow.NameWidget.MaxWidth)
			[
				NameWidget.ToSharedRef()
			]
			.ValueContent()
			.MinDesiredWidth(DefaultRow.ValueWidget.MinWidth)
			.MaxDesiredWidth(TOptional<float>())
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					ValueWidget.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					Chip.ToSharedRef()
				]
			];
	}

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

			IDetailPropertyRow& ChildRow = Category.AddProperty(Child);
			ChildRow.ShouldAutoExpand(bAutoExpand);
			AddBindChip(ChildRow, Child.ToSharedRef());
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
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, TimeUnit), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, Progress), BuilderClass)).ShouldAutoExpand(true);
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationSpeedMultiplier), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, MinimumLength), BuilderClass));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, SaveFolder), BuilderClass));

			// No Preview Animation row: which asset is edited is the header's Edit Existing Asset picker.
		}
		else
		{
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Description), UWidgetAnimPresetAsset::StaticClass()));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, TimeUnit), UWidgetAnimPresetAsset::StaticClass()));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Progress), UWidgetAnimPresetAsset::StaticClass())).ShouldAutoExpand(true);
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, AnimationSpeedMultiplier), UWidgetAnimPresetAsset::StaticClass()));
			Animation.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, MinimumLength), UWidgetAnimPresetAsset::StaticClass()));
		}
	}

	/**
	 * A dropdown of widget paths with a search box at its top: typing narrows the list to the paths containing what is typed
	 * (any case), Enter picks the first one left, and the list scrolls once there are more than fit. What a Widget Blueprint
	 * with many widgets needs to find the one it wants. Options is kept alive by the dropdown.
	 */
	static TSharedRef<SWidget> MakeSearchablePicker(const TSharedRef<TArray<TSharedPtr<FString>>>& Options, const TAttribute<FText>& Current, TFunction<void(const FString&)> OnPicked, const TAttribute<bool>& Enabled, const FText& ToolTip)
	{
		struct FPickerState
		{
			TArray<TSharedPtr<FString>> Filtered;
			TSharedPtr<SListView<TSharedPtr<FString>>> List;
			TWeakPtr<SComboButton> Combo;
		};
		const TSharedRef<FPickerState> State = MakeShared<FPickerState>();

		auto Pick = [State, OnPicked](const TSharedPtr<FString>& Item)
		{
			if (Item.IsValid())
			{
				OnPicked(*Item);
			}
			if (const TSharedPtr<SComboButton> Combo = State->Combo.Pin())
			{
				Combo->SetIsOpen(false);
			}
		};

		auto Filter = [State, Options](const FString& Text)
		{
			State->Filtered.Reset();
			const FString Needle = Text.TrimStartAndEnd();
			for (const TSharedPtr<FString>& Option : *Options)
			{
				if (Option.IsValid() && (Needle.IsEmpty() || Option->Contains(Needle, ESearchCase::IgnoreCase)))
				{
					State->Filtered.Add(Option);
				}
			}
			if (State->List.IsValid())
			{
				State->List->RequestListRefresh();
			}
		};

		const TSharedRef<SComboButton> Combo = SNew(SComboButton)
			.IsEnabled(Enabled)
			.ToolTipText(ToolTip)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(Current)
			];

		Combo->SetOnGetMenuContent(FOnGetContent::CreateLambda([State, Filter, Pick, Current]() -> TSharedRef<SWidget>
		{
			Filter(FString());

			const TSharedRef<SSearchBox> Search = SNew(SSearchBox)
				.HintText(LOCTEXT("SearchWidgetsHint", "Search widgets"))
				.OnTextChanged_Lambda([Filter](const FText& Text) { Filter(Text.ToString()); })
				.OnTextCommitted_Lambda([State, Pick](const FText&, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter && State->Filtered.Num() > 0)
					{
						Pick(State->Filtered[0]);
					}
				});

			const FString CurrentText = Current.Get().ToString();
			State->List = SNew(SListView<TSharedPtr<FString>>)
				.ListItemsSource(&State->Filtered)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow_Lambda([CurrentText](TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& Owner)
				{
					const bool bCurrent = Item.IsValid() && *Item == CurrentText;
					return SNew(STableRow<TSharedPtr<FString>>, Owner)
						.Padding(FMargin(6.f, 2.f))
						[
							SNew(STextBlock)
							.Font(bCurrent ? IDetailLayoutBuilder::GetDetailFontBold() : IDetailLayoutBuilder::GetDetailFont())
							.Text(FText::FromString(Item.IsValid() ? *Item : FString()))
						];
				})
				.OnMouseButtonClick_Lambda([Pick](TSharedPtr<FString> Item) { Pick(Item); });

			if (const TSharedPtr<SComboButton> Pinned = State->Combo.Pin())
			{
				Pinned->SetMenuContentWidgetToFocus(Search);
			}

			return SNew(SBox)
				.MinDesiredWidth(240.f)
				.MaxDesiredHeight(360.f)
				.Padding(FMargin(4.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.f, 0.f, 0.f, 4.f))
					[
						Search
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.f)
					[
						State->List.ToSharedRef()
					]
				];
		}));
		State->Combo = Combo;
		return Combo;
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

		// The folder it is listed under - the editor's only. Not on a plain list, whose one group is the widget itself.
		const TSharedPtr<IPropertyHandle> FolderHandle = GroupHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayerGroup, EditorFolder));
		if (FolderHandle.IsValid() && Source.SupportsLayerGroups())
		{
			Category.AddProperty(FolderHandle);
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
		const FString CurrentText = WidgetAnimTargetOption::FromPath(Current);

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
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		const TSharedPtr<SWidgetAnimTimeline> PickOwner = Panel.Pin();
		const int32 GroupIndex = PickOwner ? PickOwner->GetSelection().Group : INDEX_NONE;
		Category.AddProperty(TargetHandle).CustomWidget()
			.NameContent()
			[
				Target->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(220.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					MakeSearchablePicker(
						TargetOptions.ToSharedRef(),
						TAttribute<FText>::CreateLambda([Target]()
						{
							FName Value;
							Target->GetValue(Value);
							return FText::FromString(WidgetAnimTargetOption::FromPath(Value));
						}),
						[Target](const FString& Picked)
						{
							Target->SetValue(WidgetAnimTargetOption::ToPath(Picked));
						},
						TAttribute<bool>::CreateLambda([WeakOwner]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEdit(); }),
						WidgetAnimTips::PreviewTargetTooltip())
				]

				// Pick it by clicking the widget in the designer instead: lit while it waits for that click.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(3.f, 2.f))
					.Visibility_Lambda([WeakOwner]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanPickTargetInDesigner() ? EVisibility::Visible : EVisibility::Collapsed; })
					.IsEnabled_Lambda([WeakOwner]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEdit(); })
					.ToolTipText(WidgetAnimTips::PickTargetInDesignerTooltip())
					.OnClicked_Lambda([WeakOwner, GroupIndex]()
					{
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
						{
							if (Pinned->IsPickingTarget(GroupIndex))
							{
								Pinned->CancelTargetPick();
							}
							else
							{
								Pinned->BeginTargetPick(GroupIndex);
							}
						}
						return FReply::Handled();
					})
					[
						WidgetAnimTimeline::MakeFixedIcon(
							WidgetAnimTimeline::PickBrush({ "Icons.EyeDropper", "ColorPicker.EyeDropper", "Icons.Picker", "Icons.Search" }),
							TAttribute<FSlateColor>::CreateLambda([WeakOwner, GroupIndex]()
							{
								const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
								return Pinned && Pinned->IsPickingTarget(GroupIndex)
									? FSlateColor(NameTBD::Colors::AnimationOrange_Normal)
									: FSlateColor::UseForeground();
							}))
					]
				]
			];

		// While a pick waits for its click, say where to click.
		Category.AddCustomRow(WidgetAnimTips::PickingTargetNote())
			.Visibility(TAttribute<EVisibility>::CreateLambda([WeakOwner, GroupIndex]()
			{
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
				return Pinned && Pinned->IsPickingTarget(GroupIndex) ? EVisibility::Visible : EVisibility::Collapsed;
			}))
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.f, 4.f))
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					.ColorAndOpacity(FSlateColor(NameTBD::Colors::AnimationOrange_Normal))
					.Text(WidgetAnimTips::PickingTargetNote())
				]
			];
	}

	/**
	 * A layer: which group - which widget - it moves, then its type, timing, easing and settings. Not a
	 * Transform layer's keys - those are selected on the track.
	 */
	void CustomizeLayer(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, bool bIsTransform, int32 LayerIndex)
	{
		// Two categories: Track Properties - what every track has, from its Layer Group down to its Easing and timing - and
		// Track Values, what only this kind of track has: a Fade's opacities, a Shape's fields, a registered type's settings.
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Layer", LOCTEXT("TrackPropertiesCategory", "Track Properties"));
		IDetailCategoryBuilder& Values = DetailBuilder.EditCategory("WidgetAnimSelection_TrackValues", LOCTEXT("TrackValuesCategory", "Track Values"));

		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();

		// Its name, here as well as on the track's right-click Rename - as the State Machine editor names a state in its Details.
		// Empty shows its type's name, as the track does.
		{
			const TWeakPtr<SWidgetAnimTimeline> WeakNamer = Owner;
			Category.AddCustomRow(LOCTEXT("TrackNameRow", "Name"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TrackNameLabel", "Name"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(WidgetAnimTips::TrackNameTooltip())
					.IsEnabled_Lambda([WeakNamer, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNamer.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
					.Text_Lambda([WeakNamer, LayerIndex]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNamer.Pin();
						const TArray<FWidgetAnimLayer>* Named = Pinned ? Pinned->GetLayers() : nullptr;
						return Named && Named->IsValidIndex(LayerIndex) ? SWidgetAnimTimeline::GetLayerDisplayName((*Named)[LayerIndex]) : FText::GetEmpty();
					})
					.OnTextCommitted_Lambda([WeakNamer, LayerIndex](const FText& NewText, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnCleared)
						{
							return;
						}
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNamer.Pin())
						{
							Pinned->RenameLayer(LayerIndex, NewText.ToString().TrimStartAndEnd());
						}
					})
				];
		}

		// Layer Group is not a Details property - it only means something next to the group list - so it is
		// a row of its own here, changed through the panel like a drop on a group's row.
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
			const FString CurrentTargetText = WidgetAnimTargetOption::FromPath(CurrentTarget);

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
					MakeSearchablePicker(
						LayerTargetOptions.ToSharedRef(),
						TAttribute<FText>::CreateLambda([WeakTargetOwner, LayerIndex]()
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTargetOwner.Pin();
							const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
							const FName Value = Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].TargetWidget : FName(NAME_None);
							return FText::FromString(WidgetAnimTargetOption::FromPath(Value));
						}),
						[WeakTargetOwner, LayerIndex](const FString& Picked)
						{
							if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTargetOwner.Pin())
							{
								Pinned->SetLayerTarget(LayerIndex, WidgetAnimTargetOption::ToPath(Picked));
							}
						},
						TAttribute<bool>::CreateLambda([WeakTargetOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTargetOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); }),
						WidgetAnimTips::LayerTargetWidgetTooltip())
				];
		}

		// Its name - given here, or with Rename in its menu. Empty, it is called by its type, shown as the hint.
		if (Owner && TargetLayers && TargetLayers->IsValidIndex(LayerIndex))
		{
			const TWeakPtr<SWidgetAnimTimeline> WeakNameOwner = Owner;
			auto GetLayer = [WeakNameOwner, LayerIndex]() -> const FWidgetAnimLayer*
			{
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNameOwner.Pin();
				const TArray<FWidgetAnimLayer>* LayerArray = Pinned ? Pinned->GetLayers() : nullptr;
				return LayerArray && LayerArray->IsValidIndex(LayerIndex) ? &(*LayerArray)[LayerIndex] : nullptr;
			};
			Category.AddCustomRow(LOCTEXT("TrackNameRow", "Name"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TrackNameLabel", "Name"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(WidgetAnimTips::RenameTrackTooltip())
				]
				.ValueContent()
				.MinDesiredWidth(180.f)
				[
					SNew(SEditableTextBox)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(WidgetAnimTips::RenameTrackTooltip())
					.IsReadOnly_Lambda([WeakNameOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNameOwner.Pin(); return !Pinned || !Pinned->CanEditLayer(LayerIndex); })
					.Text_Lambda([GetLayer]() { const FWidgetAnimLayer* Named = GetLayer(); return Named ? FText::FromString(Named->TrackName) : FText::GetEmpty(); })
					.HintText_Lambda([GetLayer]() { const FWidgetAnimLayer* Named = GetLayer(); return Named ? SWidgetAnimTimeline::GetLayerTypeName(*Named) : FText::GetEmpty(); })
					.OnTextCommitted_Lambda([WeakNameOwner, LayerIndex](const FText& NewText, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnCleared)
						{
							return;
						}
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakNameOwner.Pin())
						{
							Pinned->RenameLayer(LayerIndex, NewText.ToString());
						}
					})
				];
		}

		// Plays When: some of a Choice parameter's options, or a Bool's - only when the animation has one.
		if (Owner && TargetLayers && TargetLayers->IsValidIndex(LayerIndex))
		{
			AddPlaysWhenRow(Category, LayerIndex);
		}

		// Presets: a saved track's settings put on this one in one pick - or this one's saved under a name.
		if (Owner && TargetLayers && TargetLayers->IsValidIndex(LayerIndex) && !(*TargetLayers)[LayerIndex].IsAnimationTrack())
		{
			const TWeakPtr<SWidgetAnimTimeline> WeakPresetOwner = Owner;
			Category.AddCustomRow(LOCTEXT("TrackPresetRow", "Preset"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TrackPresetLabel", "Preset"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(WidgetAnimTips::TrackPresetsTooltip())
				]
				.ValueContent()
				.MinDesiredWidth(180.f)
				[
					SNew(SComboButton)
					.ToolTipText(WidgetAnimTips::TrackPresetsTooltip())
					.OnGetMenuContent_Lambda([WeakPresetOwner, LayerIndex]() -> TSharedRef<SWidget>
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPresetOwner.Pin())
						{
							Pinned->AddPresetEntries(Menu, { LayerIndex }, /*bApply=*/true);
						}
						return Menu.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(LOCTEXT("TrackPresetButton", "Apply or Save a Preset..."))
					]
				];
		}

		// An animation track plays another animation whole: only when it starts is its own to set here.
		if (TargetLayers && TargetLayers->IsValidIndex(LayerIndex) && (*TargetLayers)[LayerIndex].IsAnimationTrack())
		{
			CustomizeAnimationTrack(Category, LayerHandle, (*TargetLayers)[LayerIndex], LayerIndex);
			return;
		}

		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform);
		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);
		const FName ValueKeysName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueKeys);
		const FName OvershootName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Overshoot);
		const FName SlideOvershootName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SlideOvershoot);
		const FName ExpanderName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Expander);
		const FName ShakeName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Shake);
		const FName TypeParamsName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TypeParams);
		const FName AnimTypeName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, AnimType);
		const FName DurationName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration);
		const FName DelayName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Delay);

		const TArray<FWidgetAnimLayer>* CurrentLayers = Owner ? Owner->GetLayers() : nullptr;
		const FWidgetAnimLayer* CurrentLayer = CurrentLayers && CurrentLayers->IsValidIndex(LayerIndex) ? &(*CurrentLayers)[LayerIndex] : nullptr;
		const bool bIsShake = CurrentLayer && CurrentLayer->AnimType == EWidgetAnimType::Shake;
		const bool bHasValueKeys = CurrentLayer && CurrentLayer->ValueKeys.Keys.Num() > 0;

		// A cue fires at points in time - no easing, curve or spring shapes anything on it.
		const bool bIsCue = CurrentLayer && SWidgetAnimTimeline::IsCueLayer(*CurrentLayer);
		const FName CueHiddenNames[] =
		{
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Easing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, CustomCurve),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Spring),
		};

		// What every track has, shown under Track Properties - everything else is this kind of track's own, under Track Values.
		static const FName TrackPropertyNames[] =
		{
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, AnimType),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Start),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Timing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Delay),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Expander),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TextTarget),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, bInvertTrack),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Easing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, CustomCurve),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Spring),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueName),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueSmoothing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadySpeed),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadyCatchUpTime),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueDrives),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueOptions),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueInput),
		};

		// A material track's Parameter: picked from the widget's material, and checked against it.
		const EWidgetAnimType CurrentType = CurrentLayer ? CurrentLayer->AnimType : EWidgetAnimType::Fade;
		const bool bIsMaterial = CurrentType == EWidgetAnimType::MaterialScalar || CurrentType == EWidgetAnimType::MaterialVector;
		const bool bIsMaterialVector = CurrentType == EWidgetAnimType::MaterialVector;
		const FName MaterialParameterName = bIsMaterialVector
			? GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, VectorParameterName)
			: GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ScalarParameterName);

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

			const FName ChildName = ChildProperty->GetFName();
			IDetailCategoryBuilder& Target = Algo::Find(TrackPropertyNames, ChildName) != nullptr ? Category : Values;

			// A track's keys are selected on the track, one at a time, never listed here.
			if (ChildName == ValueKeysName)
			{
				continue;
			}

			// A Stagger only on a text track - see SWidgetAnimTimeline::CanLayerStagger.
			if (ChildName == ExpanderName && Owner && !Owner->CanLayerStagger(LayerIndex))
			{
				continue;
			}

			// A registered type's track is that type, picked when it was added - a Sound, a Count Up, a Blueprint type - so
			// its type is named, not offered as a choice: the one Type every registered type shares means nothing to pick.
			if (ChildName == AnimTypeName && CurrentLayer && CurrentLayer->AnimType == EWidgetAnimType::Custom)
			{
				AddTypeNameRow(Target, Child.ToSharedRef(), *CurrentLayer);
				continue;
			}

			// Part of the Text only on an animated text widget, on a type it can limit - and under it, where it is in the text.
			if (ChildName == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TextTarget))
			{
				if (Owner && Owner->CanLayerTargetText(LayerIndex))
				{
					Target.AddProperty(Child).ShouldAutoExpand(true);
					const TWeakPtr<SWidgetAnimTimeline> WeakTextOwner = Owner;
					Target.AddCustomRow(LOCTEXT("TextTargetNoteRow", "Part of the Text"))
						.Visibility(TAttribute<EVisibility>::CreateLambda([WeakTextOwner, LayerIndex]()
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTextOwner.Pin();
							return Pinned && !Pinned->GetTextTargetNote(LayerIndex).IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
						}))
						.WholeRowContent()
						[
							SNew(SBox)
							.Padding(FMargin(0.f, 2.f))
							[
								SNew(STextBlock)
								.AutoWrapText(true)
								.Font(IDetailLayoutBuilder::GetDetailFontItalic())
								.ColorAndOpacity_Lambda([WeakTextOwner, LayerIndex]()
								{
									const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTextOwner.Pin();
									return Pinned && Pinned->IsTextTargetMissing(LayerIndex) ? FSlateColor(WidgetAnimTimeline::Palette::TextTargetMissing) : FSlateColor::UseSubduedForeground();
								})
								.Text_Lambda([WeakTextOwner, LayerIndex]()
								{
									const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakTextOwner.Pin();
									return Pinned ? Pinned->GetTextTargetNote(LayerIndex) : FText::GetEmpty();
								})
							]
						];
				}
				continue;
			}

			// A Shake's settings laid out flat, as Transform's are, and only on a Shake track.
			if (ChildName == ShakeName)
			{
				if (bIsShake)
				{
					AddChildren(Values, Child.ToSharedRef(), [](FName) { return true; }, false);
				}
				continue;
			}

			if (bIsCue && Algo::Find(CueHiddenNames, ChildName) != nullptr)
			{
				continue;
			}

			// Overshoot only applies to a track with no keys between its start and end - see EvaluateTrackEase - so
			// a keyed track doesn't offer it.
			if (bHasValueKeys && (ChildName == OvershootName || ChildName == SlideOvershootName))
			{
				continue;
			}

			if (ChildName == TransformName)
			{
				// Laid out flat, as the layer's own ShowOnlyInnerProperties would, and only on a Transform
				// layer: added one by one, its EditCondition on the layer type no longer hides it. Its key
				// timing curve is not a Details property at all; keys are timed on the track.
				if (bIsTransform)
				{
					AddChildren(Values, Child.ToSharedRef(), [KeysName](FName Name) { return Name != KeysName; }, false);
				}
				continue;
			}

			// A registered type's own settings: shown as a built-in type's are, or under its Type Params - see AddTypeParamsRows.
			if (ChildName == TypeParamsName)
			{
				if (CurrentLayer && CurrentLayer->AnimType == EWidgetAnimType::Custom)
				{
					AddTypeParamsRows(Values, Child.ToSharedRef(), *CurrentLayer);
				}
				continue;
			}

			if (bIsMaterial && ChildName == MaterialParameterName)
			{
				AddMaterialParameterRow(Values, Child.ToSharedRef(), LayerIndex, bIsMaterialVector);
				continue;
			}

			// Duration on every track: read-only, showing how long it really runs, where something else times it.
			if (ChildName == DurationName)
			{
				AddDurationRow(Target, Child.ToSharedRef(), LayerIndex);
				continue;
			}

			// On a percentage timeline a track's delay is a percentage of it.
			if (Owner && Owner->IsPercentTime() && ChildName == DelayName)
			{
				AddPercentTimeRow(Target, Child.ToSharedRef(), LayerIndex);
				continue;
			}

			// Everything else hides itself by type through its own EditCondition, wherever it is added - and a Type, Timing
			// or Value Type the type can't use is left out of its dropdown.
			IDetailPropertyRow& FieldRow = Target.AddProperty(Child);
			AddBindChip(FieldRow, Child.ToSharedRef());
			if (CurrentLayer)
			{
				WidgetAnimTimeline::RestrictLayerChoices(Child, { CurrentLayer });
			}
		}

		if (bIsMaterial)
		{
			AddMaterialWarningRow(Values, LayerIndex);
		}

		// A Shape track: which kind of shape its keys are for - followed from its target, picked for (this widget), or
		// why no fields apply.
		if (CurrentType == EWidgetAnimType::Shape)
		{
			bool bApplies = true;
			AddShapeTrackRows(Values, LayerIndex, /*bKey=*/false, bApplies);
		}

		// A Design track: which kind of widget its keys hold the look of - followed from the widget it plays on.
		if (CurrentType == EWidgetAnimType::Design && CurrentLayer)
		{
			const UClass* Kind = CurrentLayer->Transform.DesignKind.Get();
			Values.AddCustomRow(LOCTEXT("DesignKindRow", "Design Of"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DesignKindLabel", "Design Of"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ToolTipText(WidgetAnimTips::DesignBlurbTooltip())
				]
				.ValueContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(Kind ? Kind->GetDisplayNameText() : LOCTEXT("DesignKindNone", "No widget yet"))
				];
		}
	}

	/** A registered type's track's Type: named, read-only - the type it was added as. See CustomizeLayer. */
	void AddTypeNameRow(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& TypeHandle, const FWidgetAnimLayer& Layer)
	{
		Category.AddProperty(TypeHandle).CustomWidget()
			.NameContent()
			[
				TypeHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(180.f)
			[
				SNew(SComboButton)
				.IsEnabled(false)
				.HasDownArrow(true)
				.ToolTipText(WidgetAnimTips::TypeNameTooltip())
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(SWidgetAnimTimeline::GetLayerTypeName(Layer))
				]
			];
	}

	/**
	 * A registered type's own settings. A type the plugin itself registers - Sound, Event, Count Up, the text tracks - is a
	 * track type like any built-in one, so its settings are shown as a built-in type's are: field by field, with no Type
	 * Params around them. A User-defined Track - a Blueprint type or cue, or a type another module registers - keeps its Type
	 * Params, whose type can't be changed: it is the track's type, picked when it was added. A Blueprint type's Settings
	 * follow its Settings Struct, read-only, with a button beside them that finds the struct in the Content Browser.
	 */
	void AddTypeParamsRows(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& TypeParamsHandle, const FWidgetAnimLayer& Layer)
	{
		const UScriptStruct* ParamsStruct = Layer.TypeParams.GetScriptStruct();
		if (!ParamsStruct)
		{
			Category.AddProperty(TypeParamsHandle);
			return;
		}

		if (WidgetAnimTimeline::IsUserDefinedTrackType(ParamsStruct))
		{
			Category.AddCustomRow(LOCTEXT("TypeParamsRow", "Type Params"))
				.NameContent()
				[
					TypeParamsHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(180.f)
				[
					SNew(SComboButton)
					.IsEnabled(false)
					.HasDownArrow(true)
					.ToolTipText(WidgetAnimTips::TypeNameTooltip())
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(ParamsStruct->GetDisplayNameText())
					]
				];
		}
		Category.AddCustomBuilder(MakeShared<WidgetAnimTimeline::FTypeParamsDetails>(TypeParamsHandle));
	}

	/**
	 * A Shape track's rows about its shape. On a shape: which kind it keys, followed from that shape. On (this widget): the
	 * kind, picked here. On anything else: that no shape fields apply, and how to fix it. Returns whether the keys' shape
	 * fields apply; bOutApplies is false when the target can't take a Shape track at all.
	 */
	bool AddShapeTrackRows(IDetailCategoryBuilder& Category, int32 LayerIndex, bool bKey, bool& bOutApplies)
	{
		bOutApplies = true;
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
		if (!Owner || !Layers || !Layers->IsValidIndex(LayerIndex))
		{
			return false;
		}

		const UClass* Kind = (*Layers)[LayerIndex].Transform.ShapeKind.Get();
		UShapeWidget* TargetShape = nullptr;
		switch (Owner->GetShapeTrackTarget(LayerIndex, &TargetShape))
		{
		case SWidgetAnimTimeline::EShapeTrackTarget::NotAShape:
		{
			IWidgetAnimPanelSource* PanelSource = Owner->GetSource();
			const UWidget* Target = PanelSource ? PanelSource->GetLayerPreviewWidget(LayerIndex) : nullptr;
			AddNoteRow(Category, FText::Format(WidgetAnimTips::ShapeTrackNoFieldsNote(), Target ? FText::FromString(Target->GetName()) : FText::GetEmpty()));
			bOutApplies = false;
			return false;
		}

		case SWidgetAnimTimeline::EShapeTrackTarget::Shape:
			if (!bKey && TargetShape)
			{
				AddNoteRow(Category, FText::Format(WidgetAnimTips::ShapeTrackMatchedNote(), TargetShape->GetClass()->GetDisplayNameText()));
			}
			return Kind != nullptr;

		case SWidgetAnimTimeline::EShapeTrackTarget::OwnWidget:
		default:
			break;
		}

		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Owner;
		Category.AddCustomRow(LOCTEXT("ShapeKindRow", "Shape Fields"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ShapeKindLabel", "Shape Fields"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(WidgetAnimTips::ShapeTrackKindTooltip())
			]
			.ValueContent()
			.MinDesiredWidth(180.f)
			[
				SNew(SComboButton)
				.ToolTipText(WidgetAnimTips::ShapeTrackKindTooltip())
				.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
				.OnGetMenuContent_Lambda([WeakOwner, LayerIndex]() -> TSharedRef<SWidget>
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					TArray<UClass*> Kinds;
					WidgetAnimTimeline::GetShapeKinds(Kinds);
					for (UClass* ShapeClass : Kinds)
					{
						const TWeakObjectPtr<UClass> WeakClass = ShapeClass;
						Menu.AddMenuEntry(
							ShapeClass->GetDisplayNameText(),
							ShapeClass->GetToolTipText(),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([WeakOwner, LayerIndex, WeakClass]()
								{
									if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
									{
										Pinned->SetShapeTrackKind(LayerIndex, WeakClass.Get());
									}
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([WeakOwner, LayerIndex, WeakClass]()
								{
									const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
									const TArray<FWidgetAnimLayer>* PinnedLayers = Pinned ? Pinned->GetLayers() : nullptr;
									return PinnedLayers && PinnedLayers->IsValidIndex(LayerIndex) && (*PinnedLayers)[LayerIndex].Transform.ShapeKind.Get() == WeakClass.Get();
								})),
							NAME_None,
							EUserInterfaceActionType::RadioButton);
					}
					return Menu.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([WeakOwner, LayerIndex]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						const TArray<FWidgetAnimLayer>* PinnedLayers = Pinned ? Pinned->GetLayers() : nullptr;
						const UClass* Current = PinnedLayers && PinnedLayers->IsValidIndex(LayerIndex) ? (*PinnedLayers)[LayerIndex].Transform.ShapeKind.Get() : nullptr;
						return Current ? Current->GetDisplayNameText() : WidgetAnimTips::ShapeTrackKindNone();
					})
				]
			];

		if (!Kind && bKey)
		{
			AddNoteRow(Category, WidgetAnimTips::ShapeTrackPickKindNote());
		}
		return Kind != nullptr;
	}

	/**
	 * A track's Duration, on every track: typed in where it times the track, and read-only where something else does - its
	 * keys, its spring, a sound's length - showing how long the track really runs, kept up to date as that changes. A
	 * percentage of the timeline on a percentage timeline. See SWidgetAnimTimeline::GetTrackDurationInfo.
	 */
	void AddDurationRow(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& Handle, int32 LayerIndex)
	{
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		auto IsEditable = [WeakOwner, LayerIndex]()
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			bool bEditable = false;
			float Seconds = 0.f;
			FText Why;
			return Pinned && Pinned->GetTrackDurationInfo(LayerIndex, bEditable, Seconds, Why) && bEditable && Pinned->CanEditLayer(LayerIndex);
		};

		Category.AddProperty(Handle).CustomWidget()
			.NameContent()
			[
				Handle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(120.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<float>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.AllowSpin(false)
					.MinValue(0.01f)
					.IsEnabled_Lambda(IsEditable)
					.ToolTipText_Lambda([WeakOwner, LayerIndex, Handle]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						bool bEditable = false;
						float Seconds = 0.f;
						FText Why;
						return Pinned && Pinned->GetTrackDurationInfo(LayerIndex, bEditable, Seconds, Why) && !bEditable ? Why : Handle->GetToolTipText();
					})
					.Value_Lambda([WeakOwner, LayerIndex]() -> TOptional<float>
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						bool bEditable = false;
						float Seconds = 0.f;
						FText Why;
						if (!Pinned || !Pinned->GetTrackDurationInfo(LayerIndex, bEditable, Seconds, Why))
						{
							return TOptional<float>();
						}
						// Rounded as everywhere else a time is shown: thousandths of a second, or hundredths of a percent.
						return Pinned->IsPercentTime()
							? FMath::RoundHalfFromZero(Seconds * 10000.f) / 100.f
							: FMath::RoundHalfFromZero(Seconds * 1000.f) / 1000.f;
					})
					.OnValueCommitted_Lambda([WeakOwner, Handle](float Value, ETextCommit::Type)
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						const float Seconds = Pinned && Pinned->IsPercentTime() ? Value / 100.f : Value;
						Handle->SetValue(FMath::Max(Seconds, 0.01f));
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([WeakOwner]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						return Pinned && Pinned->IsPercentTime() ? LOCTEXT("DurationPercentUnit", "%") : LOCTEXT("DurationSecondsUnit", "s");
					})
				]
			];
	}

	/**
	 * A track's Duration or Delay on a percentage timeline, shown and typed as a percentage of it - 100% is one second as
	 * stored. Shown where the stock row would be: Duration only where it times the track, as its EditCondition says.
	 */
	void AddPercentTimeRow(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& Handle, int32 LayerIndex)
	{
		const bool bDuration = Handle->GetProperty() && Handle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration);
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;

		Category.AddProperty(Handle).CustomWidget()
			.Visibility(TAttribute<EVisibility>::CreateLambda([WeakOwner, LayerIndex, bDuration]()
			{
				if (!bDuration)
				{
					return EVisibility::Visible;
				}

				// Duration's own EditCondition: it times a Tween (not a Transform, whose keys do), a Pulse, a value's ease -
				// or an animation track stretched across it.
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
				const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
				if (!Layers || !Layers->IsValidIndex(LayerIndex))
				{
					return EVisibility::Collapsed;
				}
				const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
				const bool bShown = Layer.FitsAnimationToTrack()
					|| (!WidgetAnimTypeIsKeyframed(Layer.AnimType) && Layer.Timing != EWidgetAnimTiming::Spring)
					|| Layer.AnimType == EWidgetAnimType::Pulse
					|| (Layer.Timing == EWidgetAnimTiming::Value && Layer.ValueSmoothing == EWidgetAnimValueSmoothing::Tween);
				return bShown ? EVisibility::Visible : EVisibility::Collapsed;
			}))
			.NameContent()
			[
				Handle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(120.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SNumericEntryBox<float>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.AllowSpin(false)
					.MinValue(bDuration ? TOptional<float>(0.1f) : TOptional<float>())
					.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
					.Value_Lambda([Handle]() -> TOptional<float>
					{
						float Seconds = 0.f;
						// Rounded to two places, as everywhere else a percentage is shown.
						return Handle->GetValue(Seconds) == FPropertyAccess::Success ? TOptional<float>(FMath::RoundHalfFromZero(Seconds * 10000.f) / 100.f) : TOptional<float>();
					})
					.OnValueCommitted_Lambda([Handle](float Percent, ETextCommit::Type)
					{
						Handle->SetValue(Percent / 100.f);
					})
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("PercentUnit", "%"))
				]
			];
	}

	/**
	 * A material track's Parameter: its name, typed - and beside it a dropdown of the parameters of the material it drives on
	 * the preview, each with its default value. Read when it opens, so it always lists the material as it is now.
	 */
	void AddMaterialParameterRow(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& ParameterHandle, int32 LayerIndex, bool bVector)
	{
		using namespace WidgetAnimTimeline;

		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		Category.AddProperty(ParameterHandle).CustomWidget()
			.NameContent()
			[
				ParameterHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(220.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.ToolTipText(WidgetAnimTips::MaterialParameterTooltip())
					[
						ParameterHandle->CreatePropertyValueWidget()
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
				[
					SNew(SComboButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.HasDownArrow(true)
					.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
					.ToolTipText_Lambda([WeakOwner, LayerIndex]()
					{
						UMaterialInterface* Material = nullptr;
						const UWidget* Widget = FindMaterialWidget(WeakOwner, LayerIndex, Material);
						return Material && Widget
							? FText::Format(WidgetAnimTips::MaterialParameterPickTooltip(), FText::FromString(WidgetAnimMaterialCheck::GetDefaultsMaterial(Material)->GetName()), FText::FromString(Widget->GetName()))
							: WidgetAnimTips::MaterialParameterNoMaterialTooltip();
					})
					.OnGetMenuContent_Lambda([WeakOwner, LayerIndex, bVector, ParameterHandle]() -> TSharedRef<SWidget>
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

						UMaterialInterface* Material = nullptr;
						FindMaterialWidget(WeakOwner, LayerIndex, Material);
						TArray<WidgetAnimMaterialCheck::FParameter> Parameters;
						WidgetAnimMaterialCheck::GatherParameters(Material, bVector, Parameters);

						if (!Material || Parameters.Num() == 0)
						{
							Menu.AddWidget(
								SNew(SBox)
								.Padding(FMargin(8.f, 4.f))
								.MaxDesiredWidth(320.f)
								[
									SNew(STextBlock)
									.AutoWrapText(true)
									.Font(IDetailLayoutBuilder::GetDetailFontItalic())
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
									.Text(Material
										? FText::Format(WidgetAnimTips::MaterialNoParametersNote(), FText::FromString(WidgetAnimMaterialCheck::GetDefaultsMaterial(Material)->GetName()),
											bVector ? LOCTEXT("MaterialVectorKindMenu", "vector") : LOCTEXT("MaterialScalarKindMenu", "scalar"))
										: WidgetAnimTips::MaterialParameterNoMaterialTooltip())
								],
								FText::GetEmpty(), /*bNoIndent=*/true);
							return Menu.MakeWidget();
						}

						Menu.BeginSection("Parameters", FText::FromString(WidgetAnimMaterialCheck::GetDefaultsMaterial(Material)->GetName()));
						for (const WidgetAnimMaterialCheck::FParameter& Parameter : Parameters)
						{
							// The default beside the name: a number, or a rounded square of its color.
							TSharedRef<SWidget> Default = SNullWidget::NullWidget;
							if (bVector)
							{
								Default = SNew(SBox)
									.WidthOverride(14.f)
									.HeightOverride(14.f)
									[
										SNew(SOverlay)
										+ SOverlay::Slot()
										[
											SNew(SImage)
											.Image(RoundedFill(3.f))
											.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
										]
										+ SOverlay::Slot()
										.Padding(FMargin(1.f))
										[
											SNew(SImage)
											.Image(RoundedFill(2.f))
											.ColorAndOpacity(FLinearColor(Parameter.Vector.R, Parameter.Vector.G, Parameter.Vector.B, 1.f))
										]
									];
							}
							else
							{
								Default = SNew(STextBlock)
									.Font(IDetailLayoutBuilder::GetDetailFont())
									.ColorAndOpacity(FSlateColor::UseSubduedForeground())
									.Text(FText::AsNumber(Parameter.Scalar));
							}

							const FName Name = Parameter.Name;
							Menu.AddMenuEntry(
								FUIAction(FExecuteAction::CreateLambda([ParameterHandle, Name]() { ParameterHandle->SetValue(Name); })),
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot()
								.FillWidth(1.f)
								.VAlign(VAlign_Center)
								.Padding(FMargin(0.f, 0.f, 16.f, 0.f))
								[
									SNew(STextBlock)
									.Text(FText::FromName(Name))
								]
								+ SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
								[
									Default
								]);
						}
						Menu.EndSection();
						return Menu.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text(LOCTEXT("PickMaterialParameter", "Pick"))
					]
				]
			];
	}

	/**
	 * Under a material track's settings, while it can't drive anything on the preview, what is wrong - see
	 * WidgetAnimMaterialCheck::Check. Read at most a few times a second: listing a material's parameters isn't free.
	 */
	void AddMaterialWarningRow(IDetailCategoryBuilder& Category, int32 LayerIndex)
	{
		struct FCache
		{
			double LastCheck = -1.0;
			FText Text;
		};
		const TSharedRef<FCache> Cache = MakeShared<FCache>();
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		auto GetWarning = [Cache, WeakOwner, LayerIndex]() -> FText
		{
			const double Now = FPlatformTime::Seconds();
			if (Cache->LastCheck < 0.0 || Now - Cache->LastCheck > 0.25)
			{
				Cache->LastCheck = Now;
				Cache->Text = FText::GetEmpty();
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
				IWidgetAnimPanelSource* Source = Pinned ? Pinned->GetSource() : nullptr;
				const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
				if (Source && Layers && Layers->IsValidIndex(LayerIndex))
				{
					Cache->Text = WidgetAnimMaterialCheck::Check(Source->GetLayerPreviewWidget(LayerIndex), (*Layers)[LayerIndex]);
				}
			}
			return Cache->Text;
		};

		Category.AddCustomRow(LOCTEXT("MaterialWarningRow", "Material"))
			.Visibility(TAttribute<EVisibility>::CreateLambda([GetWarning]() { return GetWarning().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }))
			.WholeRowContent()
			[
				SNew(SBox)
				.Padding(FMargin(0.f, 4.f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(FMargin(0.f, 1.f, 6.f, 0.f))
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Warning"))
						.DesiredSizeOverride(FVector2D(16.f, 16.f))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text_Lambda(GetWarning)
					]
				]
			];
	}

	/** The widget a material track plays on in the preview, and - in OutMaterial - its material in the track's slot. */
	static const UWidget* FindMaterialWidget(const TWeakPtr<SWidgetAnimTimeline>& WeakOwner, int32 LayerIndex, UMaterialInterface*& OutMaterial)
	{
		OutMaterial = nullptr;
		const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
		IWidgetAnimPanelSource* Source = Pinned ? Pinned->GetSource() : nullptr;
		const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
		if (!Source || !Layers || !Layers->IsValidIndex(LayerIndex))
		{
			return nullptr;
		}
		const UWidget* Widget = Source->GetLayerPreviewWidget(LayerIndex);
		OutMaterial = UWidgetAnimLibrary::FindWidgetMaterial(Widget, (*Layers)[LayerIndex].MaterialSlot);
		return Widget;
	}

	/**
	 * One Transform key: when it happens (read-only - drag it on the track), its pose, its shape, and the curve
	 * from it to the next - laid out flat, and left out on the last key, which has no next. On a Spring track the
	 * spring carries it across every key, so a note says so in place of the curve.
	 */
	void CustomizeKey(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, int32 LayerIndex, int32 KeyIndex, int32 NumKeys, const FWidgetAnimLayer& Layer)
	{
		const bool bSpring = Layer.IsSpringTimed();
		const TSharedPtr<IPropertyHandle> TransformHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform));
		const TSharedPtr<IPropertyHandle> KeysHandle = TransformHandle.IsValid() ? TransformHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys)) : nullptr;
		const TSharedPtr<IPropertyHandle> KeyHandle = KeysHandle.IsValid() ? KeysHandle->GetChildHandle(static_cast<uint32>(KeyIndex)) : nullptr;

		if (!KeyHandle.IsValid() || !KeyHandle->IsValidHandle())
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Key", LOCTEXT("KeyCategory", "Key"));
		const FName CurveName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, CurveToNext);

		// A Shape track's key: its shape's fields only when they apply - a shape it targets, or a kind picked for the widget
		// it is put on - and only a note saying why when they don't.
		bool bShapeFields = true;
		bool bShapeApplies = true;
		if (Layer.AnimType == EWidgetAnimType::Shape)
		{
			bShapeFields = AddShapeTrackRows(Category, LayerIndex, /*bKey=*/true, bShapeApplies);
		}
		// A Design track's key: its widget's look, once there is a widget to read it off.
		const bool bDesign = Layer.AnimType == EWidgetAnimType::Design;
		if (bDesign && !Layer.Transform.DesignKind)
		{
			AddNoteRow(Category, WidgetAnimTips::DesignNoWidgetNote());
			bShapeFields = false;
		}

		const FName ShapeValuesName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, ShapeValues);
		const FName AuthoredName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, bUseAuthoredShape);
		const FName AuthoredTransformName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, bUseAuthoredTransform);
		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, Transform);
		const FName ShapeName = GET_MEMBER_NAME_CHECKED(FWidgetTransformKeyframe, Shape);

		// Its Use Authored, named for what the track keys - Use Authored Transform, Shape Box, Shape or Design.
		const FText AuthoredLabel = Layer.AnimType == EWidgetAnimType::ShapeBox ? LOCTEXT("UseAuthoredShapeBox", "Use Authored Shape Box")
			: Layer.AnimType == EWidgetAnimType::Shape ? LOCTEXT("UseAuthoredShape", "Use Authored Shape")
			: bDesign ? LOCTEXT("UseAuthoredDesign", "Use Authored Design")
			: LOCTEXT("UseAuthoredTransform", "Use Authored Transform");

		// While it uses the authored look, the fields it stands in for aren't shown at all, rather than left to an edit
		// condition: shown that way, they came back read-only when it was turned off, until the key was picked again. Turning
		// it on or off lays the panel out afresh instead (see ComputeLayoutSignature).
		const FWidgetTransformKeyframe* Keyframe = Layer.Transform.Keys.IsValidIndex(KeyIndex) ? &Layer.Transform.Keys[KeyIndex] : nullptr;
		const bool bAuthored = Keyframe && (Layer.AnimType == EWidgetAnimType::Transform ? Keyframe->bUseAuthoredTransform : Keyframe->bUseAuthoredShape);

		uint32 NumKeyChildren = 0;
		KeyHandle->GetNumChildren(NumKeyChildren);
		for (uint32 ChildIndex = 0; ChildIndex < NumKeyChildren; ++ChildIndex)
		{
			const TSharedPtr<IPropertyHandle> Child = KeyHandle->GetChildHandle(ChildIndex);
			const FProperty* ChildProperty = Child.IsValid() ? Child->GetProperty() : nullptr;
			if (!ChildProperty)
			{
				continue;
			}
			const FName Name = ChildProperty->GetFName();
			if (Name == CurveName || (!bShapeFields && Name == ShapeValuesName) || (!bShapeApplies && Name == AuthoredName))
			{
				continue;
			}
			if (bAuthored && (Name == TransformName || Name == ShapeName || Name == ShapeValuesName))
			{
				continue;
			}

			IDetailPropertyRow& Row = Category.AddProperty(Child);
			Row.ShouldAutoExpand(true);
			if (Name == AuthoredName || Name == AuthoredTransformName)
			{
				Row.DisplayName(AuthoredLabel);
			}
			else if (Name == ShapeValuesName && bDesign)
			{
				Row.DisplayName(LOCTEXT("DesignKeyValues", "Design"));
			}
		}

		if (KeyIndex + 1 < NumKeys && bSpring)
		{
			AddNoteRow(Category, WidgetAnimTips::SpringKeyCurveNote());
			return;
		}
		if (KeyIndex + 1 < NumKeys && Layer.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack)
		{
			AddNoteRow(Category, WidgetAnimTips::WholeTrackKeyCurveNote());
			return;
		}

		const TSharedPtr<IPropertyHandle> CurveHandle = KeyHandle->GetChildHandle(CurveName);
		if (KeyIndex + 1 < NumKeys && CurveHandle.IsValid() && CurveHandle->IsValidHandle())
		{
			AddChildren(Category, CurveHandle.ToSharedRef(), [](FName) { return true; }, /*bAutoExpand=*/false);
		}
	}

	/**
	 * An animation track: its Starts and Delay, which are its own, then the animation it plays - named, with how long it
	 * runs and a button that finds it in the Content Browser - and how to edit its tracks. Everything else about it is the
	 * inserted animation's, edited there.
	 */
	void CustomizeAnimationTrack(IDetailCategoryBuilder& Category, const TSharedRef<IPropertyHandle>& LayerHandle, const FWidgetAnimLayer& Layer, int32 LayerIndex)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const bool bPercent = Owner && Owner->IsPercentTime();
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;

		for (const FName Name : { GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Start), GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Delay) })
		{
			const TSharedPtr<IPropertyHandle> Child = LayerHandle->GetChildHandle(Name);
			if (Child.IsValid() && Child->IsValidHandle())
			{
				if (bPercent && Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Delay))
				{
					AddPercentTimeRow(Category, Child.ToSharedRef(), LayerIndex);
				}
				else
				{
					Category.AddProperty(Child);
				}
			}
		}

		// Stretched across the track - always for a percentage animation, or in a percentage timeline - or played at its own
		// length. Stretched, its length is the track's own, resized here or by its edges.
		const bool bInsertsPercent = Layer.InsertedAnimation && Layer.InsertedAnimation->IsPercentTime();
		const bool bFitForced = bInsertsPercent || bPercent || Layer.IsValueDriven();
		Category.AddCustomRow(LOCTEXT("AnimationTrackFitRow", "Stretch Across Track"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationTrackFitLabel", "Stretch Across Track"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(WidgetAnimTips::AnimationTrackFitTooltip())
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.ToolTipText(WidgetAnimTips::AnimationTrackFitTooltip())
				.IsEnabled_Lambda([WeakOwner, LayerIndex, bFitForced]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return !bFitForced && Pinned && Pinned->CanEditLayer(LayerIndex); })
				.IsChecked_Lambda([WeakOwner, LayerIndex]()
				{
					const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
					const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
					return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].FitsAnimationToTrack() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([WeakOwner, LayerIndex](ECheckBoxState State)
				{
					if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
					{
						Pinned->SetAnimationTrackFit(LayerIndex, State == ECheckBoxState::Checked);
					}
				})
			];

		if (Layer.FitsAnimationToTrack())
		{
			const TSharedPtr<IPropertyHandle> DurationHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration));
			if (DurationHandle.IsValid() && DurationHandle->IsValidHandle())
			{
				if (bPercent)
				{
					AddPercentTimeRow(Category, DurationHandle.ToSharedRef(), LayerIndex);
				}
				else
				{
					Category.AddProperty(DurationHandle);
				}
			}
		}

		// What moves it along the track: the timeline passing it, or a float value of its own.
		Category.AddCustomRow(LOCTEXT("AnimationTrackDriveRow", "Moved By"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationTrackDriveLabel", "Moved By"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(WidgetAnimTips::AnimationTrackDriveTooltip())
			]
			.ValueContent()
			.MinDesiredWidth(180.f)
			[
				SNew(SComboButton)
				.ToolTipText(WidgetAnimTips::AnimationTrackDriveTooltip())
				.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
				.OnGetMenuContent_Lambda([WeakOwner, LayerIndex]() -> TSharedRef<SWidget>
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					for (const bool bValue : { false, true })
					{
						Menu.AddMenuEntry(
							bValue ? LOCTEXT("AnimationTrackDriveValue", "Float Value") : LOCTEXT("AnimationTrackDriveClock", "Across the Track"),
							WidgetAnimTips::AnimationTrackDriveTooltip(),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([WeakOwner, LayerIndex, bValue]()
								{
									if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
									{
										Pinned->SetAnimationTrackValueDriven(LayerIndex, bValue);
									}
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([WeakOwner, LayerIndex, bValue]()
								{
									const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
									const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
									return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].IsValueDriven() == bValue;
								})),
							NAME_None,
							EUserInterfaceActionType::RadioButton);
					}
					return Menu.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(Layer.IsValueDriven() ? LOCTEXT("AnimationTrackDriveValue", "Float Value") : LOCTEXT("AnimationTrackDriveClock", "Across the Track"))
				]
			];

		// Following a value: its name, how it travels and what feeds it - the value-track settings, as any value track has them.
		if (Layer.IsValueDriven())
		{
			static const FName ValueNames[] =
			{
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueName),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueSmoothing),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Spring),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadySpeed),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, SteadyCatchUpTime),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueOptions),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, ValueInput),
			};
			for (const FName Name : ValueNames)
			{
				const TSharedPtr<IPropertyHandle> Child = LayerHandle->GetChildHandle(Name);
				if (Child.IsValid() && Child->IsValidHandle())
				{
					Category.AddProperty(Child);
					WidgetAnimTimeline::RestrictLayerChoices(Child, { &Layer });
				}
			}
			AddNoteRow(Category, WidgetAnimTips::AnimationTrackValueNote());
		}

		const TWeakObjectPtr<UWidgetAnimPresetAsset> WeakAnimation = Layer.InsertedAnimation.Get();
		const FText AnimationName = Layer.InsertedAnimation ? FText::FromString(Layer.InsertedAnimation->GetName()) : LOCTEXT("AnimationTrackMissing", "(missing)");
		FNumberFormattingOptions Seconds;
		Seconds.MinimumFractionalDigits = 2;
		Seconds.MaximumFractionalDigits = 2;
		Category.AddCustomRow(LOCTEXT("AnimationTrackRow", "Animation"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("AnimationTrackLabel", "Animation"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MinDesiredWidth(220.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(NameTBD::Colors::AnimationOrange_Normal))
					.Text(Layer.InsertedAnimation && Layer.InsertedAnimation->IsPercentTime()
						? FText::Format(LOCTEXT("AnimationTrackValuePercent", "{0}  (0 - 100%)"), AnimationName)
						: FText::Format(LOCTEXT("AnimationTrackValue", "{0}  ({1} s)"), AnimationName, FText::AsNumber(UWidgetAnimLibrary::GetAnimationPassSeconds(Layer.InsertedAnimation.Get()), &Seconds)))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(2.f, 0.f))
					.ToolTipText(WidgetAnimTips::AnimationTrackBrowseTooltip())
					.IsEnabled_Lambda([WeakAnimation]() { return WeakAnimation.IsValid(); })
					.OnClicked_Lambda([WeakAnimation]()
					{
						UWidgetAnimPresetAsset* Animation = WeakAnimation.Get();
						if (Animation && GEditor)
						{
							TArray<UObject*> Objects;
							Objects.Add(Animation);
							GEditor->SyncBrowserToObjects(Objects);
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.Search"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			];

		// What the animation is for, as its Description says - then how the track is edited.
		if (Layer.InsertedAnimation && !Layer.InsertedAnimation->Description.IsEmpty())
		{
			Category.AddCustomRow(LOCTEXT("AnimationTrackDescriptionRow", "Description"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AnimationTrackDescriptionLabel", "Description"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(220.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(Layer.InsertedAnimation->Description)
				];
		}
		AddNoteRow(Category, WidgetAnimTips::AnimationTrackNote());
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

	/** A parameter's type, as the Widget Animations panel shows it: as a Blueprint variable of the type reads. See WidgetAnimTypePill. */
	static WidgetAnimTypePill::EKind GetParameterKind(EWidgetAnimParameterType Type)
	{
		return WidgetAnimTypePill::FromParameterType(Type);
	}

	/** The parameter type the type picker's kind stands for. */
	static EWidgetAnimParameterType ParameterTypeOf(WidgetAnimTypePill::EKind Kind)
	{
		switch (Kind)
		{
		case WidgetAnimTypePill::EKind::Integer:  return EWidgetAnimParameterType::Integer;
		case WidgetAnimTypePill::EKind::Bool:     return EWidgetAnimParameterType::Bool;
		case WidgetAnimTypePill::EKind::Color:    return EWidgetAnimParameterType::Color;
		case WidgetAnimTypePill::EKind::Vector2D: return EWidgetAnimParameterType::Vector2D;
		case WidgetAnimTypePill::EKind::Choice:   return EWidgetAnimParameterType::Choice;
		default:                                  return EWidgetAnimParameterType::Float;
		}
	}

	/**
	 * The animation's parameters, under every selection: a row each laid out as a Blueprint's variables are - its name, its type
	 * (click the pill to change it), its default, which is what the preview plays - and, expanded, its description, options and
	 * range. + adds one; right-click a row to rename or delete it. Edited through the shadow like any setting of the asset;
	 * renames, type changes and deletes through the panel, so the tracks' bindings follow.
	 */
	void CustomizeParameters(IDetailLayoutBuilder& DetailBuilder)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<FWidgetAnimParameter>* Declared = Owner ? Owner->GetParameters() : nullptr;
		if (!Declared)
		{
			return;
		}
		const TSharedRef<IPropertyHandle> ParametersHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Parameters), UWidgetAnimPresetAsset::StaticClass());
		if (!ParametersHandle->IsValidHandle())
		{
			return;
		}

		const FText CategoryTitle = LOCTEXT("ParametersCategory", "Animation Parameters");
		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Parameters", CategoryTitle);
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Owner;

		// Its title - the header is replaced whole, so it says it itself - then + : one of each type, as the State Machine
		// editor adds inputs.
		Category.HeaderContent(
			SNew(SHorizontalBox)
			.ToolTipText(WidgetAnimTips::ParametersSectionTooltip())
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(CategoryTitle)
				.Font(FAppStyle::GetFontStyle(TEXT("DetailsView.CategoryFontStyle")))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				// + : a type picker, as the Blueprint editor's variable types (and the State Machine editor's inputs) are picked.
				WidgetAnimTypePill::MakeTypePickerButton(
					{ WidgetAnimTypePill::EKind::Bool, WidgetAnimTypePill::EKind::Float, WidgetAnimTypePill::EKind::Integer,
						WidgetAnimTypePill::EKind::Choice, WidgetAnimTypePill::EKind::Vector2D, WidgetAnimTypePill::EKind::Color },
					[WeakOwner](WidgetAnimTypePill::EKind Kind)
					{
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
						{
							Pinned->AddParameter(ParameterTypeOf(Kind));
						}
					},
					[](WidgetAnimTypePill::EKind Kind)
					{
						const UEnum* Types = StaticEnum<EWidgetAnimParameterType>();
						return Types->GetToolTipTextByIndex(Types->GetIndexByValue(static_cast<int64>(ParameterTypeOf(Kind))));
					},
					WidgetAnimTips::AddParameterTooltip(),
					TAttribute<bool>::CreateLambda([WeakOwner]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && !Pinned->IsReadOnly(); }))
			],
			/*bWholeRowContent=*/true);

		uint32 NumParameters = 0;
		ParametersHandle->GetNumChildren(NumParameters);
		if (NumParameters == 0)
		{
			AddNoteRow(Category, WidgetAnimTips::NoParametersNote());
			return;
		}

		for (uint32 Index = 0; Index < NumParameters; ++Index)
		{
			const TSharedPtr<IPropertyHandle> Element = ParametersHandle->GetChildHandle(Index);
			const int32 ParameterIndex = static_cast<int32>(Index);
			if (!Element.IsValid() || !Declared->IsValidIndex(ParameterIndex))
			{
				continue;
			}
			const FWidgetAnimParameter& Parameter = (*Declared)[ParameterIndex];
			const EWidgetAnimParameterType Type = Parameter.Type;

			// Its default, edited in place: the field of its type - a Choice's as a dropdown of its options.
			TSharedRef<SWidget> DefaultWidget = SNullWidget::NullWidget;
			if (Type == EWidgetAnimParameterType::Choice)
			{
				const TSharedPtr<IPropertyHandle> OptionHandle = Element->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, DefaultOption));
				DefaultWidget = SNew(SComboButton)
					.OnGetMenuContent_Lambda([WeakOwner, ParameterIndex, OptionHandle]()
					{
						FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
						const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
						const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
						if (Live && Live->IsValidIndex(ParameterIndex) && OptionHandle.IsValid())
						{
							for (const FName Option : (*Live)[ParameterIndex].Options)
							{
								Menu.AddMenuEntry(FText::FromName(Option), FText::GetEmpty(), FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([OptionHandle, Option]() { OptionHandle->SetValue(Option); })));
							}
						}
						return Menu.MakeWidget();
					})
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.Text_Lambda([WeakOwner, ParameterIndex]()
						{
							const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
							const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
							return Live && Live->IsValidIndex(ParameterIndex) ? FText::FromName((*Live)[ParameterIndex].GetDefaultOption()) : FText::GetEmpty();
						})
					];
			}
			else
			{
				FName DefaultName = GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, DefaultFloat);
				switch (Type)
				{
				case EWidgetAnimParameterType::Integer:  DefaultName = GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, DefaultInteger); break;
				case EWidgetAnimParameterType::Bool:     DefaultName = GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, bDefaultBool); break;
				case EWidgetAnimParameterType::Color:    DefaultName = GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, DefaultColor); break;
				case EWidgetAnimParameterType::Vector2D: DefaultName = GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, DefaultVector2D); break;
				default: break;
				}
				const TSharedPtr<IPropertyHandle> DefaultHandle = Element->GetChildHandle(DefaultName);
				if (DefaultHandle.IsValid())
				{
					DefaultWidget = WidgetAnimTypePill::MakeValueEditor(DefaultHandle.ToSharedRef());
				}
			}

			// Its type: the pill, a dropdown of the types - as a Blueprint variable's type is changed.
			const TSharedRef<SWidget> TypeWidget = SNew(SComboButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HasDownArrow(false)
				.ContentPadding(FMargin(0.f))
				.ToolTipText(LOCTEXT("ParameterTypeTip", "Its type - click to change it. Bindings of fields that can't take the new type are let go of."))
				.ButtonContent()
				[
					WidgetAnimTypePill::MakePill(GetParameterKind(Type))
				]
				.OnGetMenuContent_Lambda([WeakOwner, ParameterIndex, Type]()
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					for (const EWidgetAnimParameterType Candidate : { EWidgetAnimParameterType::Bool, EWidgetAnimParameterType::Float, EWidgetAnimParameterType::Integer,
						EWidgetAnimParameterType::Choice, EWidgetAnimParameterType::Vector2D, EWidgetAnimParameterType::Color })
					{
						Menu.AddMenuEntry(
							WidgetAnimTypePill::GetLabel(GetParameterKind(Candidate)),
							StaticEnum<EWidgetAnimParameterType>()->GetToolTipTextByIndex(static_cast<int32>(Candidate)),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([WeakOwner, ParameterIndex, Candidate]()
								{
									if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
									{
										Pinned->SetParameterType(ParameterIndex, Candidate);
									}
								}),
								FCanExecuteAction(),
								FIsActionChecked::CreateLambda([Type, Candidate]() { return Type == Candidate; })),
							NAME_None, EUserInterfaceActionType::RadioButton);
					}
					return Menu.MakeWidget();
				});

			// Its name: double-click (or Rename on the row's right-click) to rename - the tracks' bindings follow.
			const TSharedRef<SInlineEditableTextBlock> NameText = SNew(SInlineEditableTextBlock)
				.Text(FText::FromName(Parameter.Name))
				.ToolTipText(Parameter.Description.IsEmpty() ? WidgetAnimTips::ParametersSectionTooltip() : FText::FromString(Parameter.Description))
				.OnTextCommitted_Lambda([WeakOwner, ParameterIndex](const FText& NewText, ETextCommit::Type)
				{
					if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
					{
						Pinned->RenameParameter(ParameterIndex, NewText.ToString());
					}
				});
			const TWeakPtr<SInlineEditableTextBlock> WeakNameText = NameText;

			IDetailGroup& Group = Category.AddGroup(FName(*FString::Printf(TEXT("Parameter_%d"), ParameterIndex)), FText::FromName(Parameter.Name));
			Group.HeaderRow()
				.NameContent()
				[
					NameText
				]
				.ValueContent()
				.MinDesiredWidth(WidgetAnimTypePill::TypeColumnWidth + WidgetAnimTypePill::ValueColumnWidth + 8.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
					[
						SNew(SBox)
						.WidthOverride(WidgetAnimTypePill::TypeColumnWidth)
						[
							TypeWidget
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						DefaultWidget
					]
				]
				.AddCustomContextMenuAction(
					FUIAction(FExecuteAction::CreateLambda([WeakNameText]()
					{
						if (const TSharedPtr<SInlineEditableTextBlock> Pinned = WeakNameText.Pin())
						{
							Pinned->EnterEditingMode();
						}
					})),
					LOCTEXT("RenameParameterMenu", "Rename"),
					LOCTEXT("RenameParameterMenuTip", "Renames the parameter - the tracks' bindings follow it."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Rename")))
				.AddCustomContextMenuAction(
					FUIAction(FExecuteAction::CreateLambda([WeakOwner, ParameterIndex]()
					{
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
						{
							Pinned->RemoveParameter(ParameterIndex);
						}
					})),
					LOCTEXT("DeleteParameterMenu", "Delete"),
					LOCTEXT("DeleteParameterMenuTip", "Deletes the parameter and lets go of every field bound to it - those play their own values again."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Delete")));

			// Preview: a value tried out in the preview - the default stays as it is. Unticked, the preview plays the default.
			AddPreviewRow(Group, ParameterIndex);

			// Expanded: what the rest of it says - shown for the types they are for.
			static const FName DetailNames[] =
			{
				GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, Description),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, Options),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, bUseRange),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, Min),
				GET_MEMBER_NAME_CHECKED(FWidgetAnimParameter, Max),
			};
			for (const FName DetailName : DetailNames)
			{
				const TSharedPtr<IPropertyHandle> Child = Element->GetChildHandle(DetailName);
				if (Child.IsValid())
				{
					Group.AddPropertyRow(Child.ToSharedRef());
				}
			}
		}
	}

	/** A track's Plays When - see FWidgetAnimLayer::PlaysWhen: Always, or some options of a Choice parameter, or a Bool's true or false. */
	void AddPlaysWhenRow(IDetailCategoryBuilder& Category, int32 LayerIndex)
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<FWidgetAnimParameter>* Declared = Owner ? Owner->GetParameters() : nullptr;
		if (!Declared || !Declared->ContainsByPredicate([](const FWidgetAnimParameter& Parameter)
			{
				return Parameter.Type == EWidgetAnimParameterType::Choice || Parameter.Type == EWidgetAnimParameterType::Bool;
			}))
		{
			return;
		}

		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Owner;
		auto GetCondition = [WeakOwner, LayerIndex]() -> FWidgetAnimParameterCondition
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			const TArray<FWidgetAnimLayer>* Layers = Pinned ? Pinned->GetLayers() : nullptr;
			return Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].PlaysWhen : FWidgetAnimParameterCondition();
		};

		Category.AddCustomRow(LOCTEXT("PlaysWhenRow", "Plays When"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("PlaysWhenLabel", "Plays When"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ToolTipText(WidgetAnimTips::PlaysWhenTooltip())
			]
			.ValueContent()
			.MinDesiredWidth(180.f)
			[
				SNew(SComboButton)
				.ToolTipText(WidgetAnimTips::PlaysWhenTooltip())
				.IsEnabled_Lambda([WeakOwner, LayerIndex]() { const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin(); return Pinned && Pinned->CanEditLayer(LayerIndex); })
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([GetCondition]()
					{
						const FWidgetAnimParameterCondition Condition = GetCondition();
						if (!Condition.IsSet())
						{
							return LOCTEXT("PlaysAlways", "Always");
						}
						TArray<FString> Options;
						for (const FName Option : Condition.Options)
						{
							Options.Add(Option.ToString());
						}
						return FText::Format(LOCTEXT("PlaysWhenSummary", "{0} is {1}"), FText::FromName(Condition.Parameter),
							Options.Num() > 0 ? FText::FromString(FString::Join(Options, TEXT(" or "))) : LOCTEXT("PlaysWhenTrue", "True"));
					})
				]
				.OnGetMenuContent_Lambda([WeakOwner, LayerIndex, GetCondition]()
				{
					// Not closed on a pick: a Choice's options are ticked one by one.
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/false, nullptr);
					const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
					const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
					if (!Live)
					{
						return Menu.MakeWidget();
					}

					Menu.AddMenuEntry(LOCTEXT("PlaysAlwaysEntry", "Always"), WidgetAnimTips::PlaysWhenTooltip(), FSlateIcon(),
						FUIAction(
							FExecuteAction::CreateLambda([WeakOwner, LayerIndex]()
							{
								if (const TSharedPtr<SWidgetAnimTimeline> Owned = WeakOwner.Pin())
								{
									Owned->SetLayerPlaysWhen(LayerIndex, FWidgetAnimParameterCondition());
								}
							}),
							FCanExecuteAction(),
							FIsActionChecked::CreateLambda([GetCondition]() { return !GetCondition().IsSet(); })),
						NAME_None, EUserInterfaceActionType::RadioButton);

					for (const FWidgetAnimParameter& Parameter : *Live)
					{
						const FName ParameterName = Parameter.Name;
						if (Parameter.Type == EWidgetAnimParameterType::Bool)
						{
							Menu.BeginSection(NAME_None, FText::FromName(ParameterName));
							for (const bool bWhileTrue : { true, false })
							{
								Menu.AddMenuEntry(
									FText::Format(LOCTEXT("PlaysWhenBool", "{0} is {1}"), FText::FromName(ParameterName), bWhileTrue ? LOCTEXT("TrueWord", "True") : LOCTEXT("FalseWord", "False")),
									WidgetAnimTips::PlaysWhenTooltip(), FSlateIcon(),
									FUIAction(
										FExecuteAction::CreateLambda([WeakOwner, LayerIndex, ParameterName, bWhileTrue]()
										{
											FWidgetAnimParameterCondition Condition;
											Condition.Parameter = ParameterName;
											if (!bWhileTrue)
											{
												Condition.Options.Add(FName(TEXT("False")));
											}
											if (const TSharedPtr<SWidgetAnimTimeline> Owned = WeakOwner.Pin())
											{
												Owned->SetLayerPlaysWhen(LayerIndex, Condition);
											}
										}),
										FCanExecuteAction(),
										FIsActionChecked::CreateLambda([GetCondition, ParameterName, bWhileTrue]()
										{
											const FWidgetAnimParameterCondition Condition = GetCondition();
											return Condition.Parameter == ParameterName && Condition.Options.Contains(FName(TEXT("False"))) != bWhileTrue;
										})),
									NAME_None, EUserInterfaceActionType::RadioButton);
							}
							Menu.EndSection();
						}
						else if (Parameter.Type == EWidgetAnimParameterType::Choice)
						{
							// Its options ticked on and off: the track plays for those ticked.
							Menu.BeginSection(NAME_None, FText::FromName(ParameterName));
							for (const FName Option : Parameter.Options)
							{
								Menu.AddMenuEntry(
									FText::Format(LOCTEXT("PlaysWhenChoice", "{0} is {1}"), FText::FromName(ParameterName), FText::FromName(Option)),
									WidgetAnimTips::PlaysWhenTooltip(), FSlateIcon(),
									FUIAction(
										FExecuteAction::CreateLambda([WeakOwner, LayerIndex, ParameterName, Option, GetCondition]()
										{
											FWidgetAnimParameterCondition Condition = GetCondition();
											if (Condition.Parameter != ParameterName)
											{
												Condition = FWidgetAnimParameterCondition();
												Condition.Parameter = ParameterName;
											}
											if (Condition.Options.Contains(Option))
											{
												Condition.Options.Remove(Option);
											}
											else
											{
												Condition.Options.Add(Option);
											}
											if (Condition.Options.Num() == 0)
											{
												Condition = FWidgetAnimParameterCondition();
											}
											if (const TSharedPtr<SWidgetAnimTimeline> Owned = WeakOwner.Pin())
											{
												Owned->SetLayerPlaysWhen(LayerIndex, Condition);
											}
										}),
										FCanExecuteAction(),
										FIsActionChecked::CreateLambda([GetCondition, ParameterName, Option]()
										{
											const FWidgetAnimParameterCondition Condition = GetCondition();
											return Condition.Parameter == ParameterName && Condition.Options.Contains(Option);
										})),
									NAME_None, EUserInterfaceActionType::ToggleButton);
							}
							Menu.EndSection();
						}
					}
					return Menu.MakeWidget();
				})
			];
	}

	/**
	 * A parameter's Preview row: ticked, the preview plays the value beside it instead of the default (SWidgetAnimTimeline::
	 * SetPreviewParameter). The value's editor is the type's own - a number, a box, a dropdown of options, X and Y, a swatch.
	 */
	void AddPreviewRow(IDetailGroup& Group, int32 ParameterIndex)
	{
		const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Panel;
		// The parameter as it is now, and the value the preview plays for it now - tried, or its default.
		auto Declared = [WeakOwner, ParameterIndex]() -> const FWidgetAnimParameter*
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
			return Live && Live->IsValidIndex(ParameterIndex) ? &(*Live)[ParameterIndex] : nullptr;
		};
		auto IsTried = [WeakOwner, Declared]()
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			const FWidgetAnimParameter* Parameter = Declared();
			FWidgetAnimParameterValue Tried;
			return Pinned && Parameter && Pinned->GetPreviewParameter(Parameter->Name, Tried);
		};
		auto Current = [WeakOwner, Declared]()
		{
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			const FWidgetAnimParameter* Parameter = Declared();
			FWidgetAnimParameterValue Value;
			if (Parameter && !(Pinned && Pinned->GetPreviewParameter(Parameter->Name, Value)))
			{
				Value = FWidgetAnimParameterValue::FromDefault(*Parameter);
			}
			return Value;
		};
		auto Try = [WeakOwner](const FWidgetAnimParameterValue& Value)
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
			{
				Pinned->SetPreviewParameter(Value, /*bTry=*/true);
			}
		};

		const FWidgetAnimParameter* Parameter = Declared();
		if (!Parameter)
		{
			return;
		}
		TSharedRef<SWidget> Editor = SNullWidget::NullWidget;
		auto Number = [Current, Try](int32 Part, bool bInteger) -> TSharedRef<SWidget>
		{
			return SNew(SNumericEntryBox<float>)
				.AllowSpin(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Value_Lambda([Current, Part, bInteger]() -> TOptional<float>
				{
					const FWidgetAnimParameterValue Value = Current();
					return bInteger ? static_cast<float>(Value.Integer) : static_cast<float>(Value.AsVector()[Part]);
				})
				.OnValueChanged_Lambda([Current, Try, Part, bInteger](float NewValue)
				{
					FWidgetAnimParameterValue Value = Current();
					if (bInteger)
					{
						Value.Integer = FMath::RoundToInt32(NewValue);
					}
					else
					{
						FVector4 Parts = Value.AsVector();
						Parts[Part] = NewValue;
						Value.SetFromVector(Parts);
					}
					Try(Value);
				});
		};
		switch (Parameter->Type)
		{
		case EWidgetAnimParameterType::Bool:
			Editor = SNew(SCheckBox)
				.IsChecked_Lambda([Current]() { return Current().bBool ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([Current, Try](ECheckBoxState State)
				{
					FWidgetAnimParameterValue Value = Current();
					Value.bBool = State == ECheckBoxState::Checked;
					Try(Value);
				});
			break;
		case EWidgetAnimParameterType::Integer:
			Editor = Number(0, /*bInteger=*/true);
			break;
		case EWidgetAnimParameterType::Vector2D:
			Editor = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 2.f, 0.f))[Number(0, false)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(2.f, 0.f, 0.f, 0.f))[Number(1, false)];
			break;
		case EWidgetAnimParameterType::Color:
			Editor = SNew(SColorBlock)
				.Color_Lambda([Current]() { return Current().Color; })
				.ShowBackgroundForAlpha(true)
				.OnMouseButtonDown_Lambda([Current, Try](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					FColorPickerArgs Args;
					Args.bUseAlpha = true;
					Args.bIsModal = false;
					Args.InitialColor = Current().Color;
					Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda([Current, Try](FLinearColor Picked)
					{
						FWidgetAnimParameterValue Value = Current();
						Value.Color = Picked;
						Try(Value);
					});
					OpenColorPicker(Args);
					return FReply::Handled();
				});
			break;
		case EWidgetAnimParameterType::Choice:
			Editor = SNew(SComboButton)
				.OnGetMenuContent_Lambda([Declared, Current, Try]()
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					if (const FWidgetAnimParameter* Live = Declared())
					{
						for (const FName Option : Live->Options)
						{
							Menu.AddMenuEntry(FText::FromName(Option), FText::GetEmpty(), FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([Current, Try, Option]()
								{
									FWidgetAnimParameterValue Value = Current();
									Value.Option = Option;
									Try(Value);
								})));
						}
					}
					return Menu.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([Current]() { return FText::FromName(Current().Option); })
				];
			break;
		default:
			Editor = Number(0, /*bInteger=*/false);
			break;
		}

		Group.AddWidgetRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				.ToolTipText(WidgetAnimTips::ParameterPreviewTooltip())
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([IsTried]() { return IsTried() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([WeakOwner, Current](ECheckBoxState State)
					{
						if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
						{
							Pinned->SetPreviewParameter(Current(), State == ECheckBoxState::Checked);
						}
					})
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("ParameterPreviewLabel", "Preview"))
				]
			]
			.ValueContent()
			.MinDesiredWidth(WidgetAnimTypePill::ValueColumnWidth)
			[
				SNew(SBox)
				.IsEnabled_Lambda([IsTried]() { return IsTried(); })
				[
					Editor
				]
			];
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
		auto AddLayerTypeParams = [this, &Category, &LayerHandle, &Layer]()
		{
			const TSharedPtr<IPropertyHandle> Handle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TypeParams));
			if (Handle.IsValid() && Handle->IsValidHandle())
			{
				AddTypeParamsRows(Category, Handle.ToSharedRef(), Layer);
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
						? FText::Format(LOCTEXT("KeyTimeValueFormatted", "{0} into the track"), FText::FromString(Pinned->FormatTime(KeyTimes[KeyIndex], 3)))
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
				// struct here - so its settings are shown whole, start value among them, as the track shows them.
				AddLayerTypeParams();
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
				AddLayerTypeParams();
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

				if (Layer.AnimType == EWidgetAnimType::Scale)
				{
					// A Scale's key: its X and its Y.
					AddKeyField(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKey, Value), LOCTEXT("KeyValueScaleX", "Scale X"));
					AddKeyField(GET_MEMBER_NAME_CHECKED(FWidgetAnimValueKey, ValueY), LOCTEXT("KeyValueScaleY", "Scale Y"));
				}
				else if (Content != EWidgetAnimKeyContent::Color)
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

		// The curve from this key to the next: the start's is kept apart from the keys, every other key carries its own. A
		// cue has none - it fires at its keys, it doesn't travel between them.
		if (KeyIndex < LastKey && !SWidgetAnimTimeline::IsCueLayer(Layer))
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
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		const TArray<FWidgetAnimLayer>* RealLayers = Owner ? Owner->GetLayers() : nullptr;
		bool bAllCanStagger = true;
		bool bAllCanTargetText = true;
		bool bRepresented = false;
		int32 NumTracks = 0;
		for (const TWeakObjectPtr<UObject>& Object : Objects)
		{
			if (const UWidgetAnimLayerEditProxy* Proxy = Cast<UWidgetAnimLayerEditProxy>(Object.Get()))
			{
				// One proxy standing in for a large selection: what is shown is decided by every track it stands for.
				if (Proxy->RepresentedLayers.Num() > 0 && RealLayers)
				{
					bRepresented = true;
					for (const int32 LayerIndex : Proxy->RepresentedLayers)
					{
						if (RealLayers->IsValidIndex(LayerIndex))
						{
							Types.Add((*RealLayers)[LayerIndex].AnimType);
							ProxyLayers.Add(&(*RealLayers)[LayerIndex]);
							bAllCanStagger = bAllCanStagger && Owner->CanLayerStagger(LayerIndex);
							bAllCanTargetText = bAllCanTargetText && Owner->CanLayerTargetText(LayerIndex);
							++NumTracks;
						}
					}
					continue;
				}

				Types.Add(Proxy->Layer.AnimType);
				ProxyLayers.Add(&Proxy->Layer);
				bAllCanStagger = bAllCanStagger && (!Owner || Owner->CanLayerStagger(Proxy->LayerIndex));
				bAllCanTargetText = bAllCanTargetText && Owner && Owner->CanLayerTargetText(Proxy->LayerIndex);
				++NumTracks;
			}
		}

		TArray<FName> CategoryNames;
		DetailBuilder.GetCategoryNames(CategoryNames);
		for (const FName CategoryName : CategoryNames)
		{
			DetailBuilder.HideCategory(CategoryName);
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Layers",
			FText::Format(LOCTEXT("LayersCategory", "{0} Tracks"), FText::AsNumber(NumTracks)));

		// What only tracks of one type have - when every track selected is that type - apart, as one track's are.
		IDetailCategoryBuilder& Values = DetailBuilder.EditCategory("WidgetAnimSelection_TrackValues", LOCTEXT("MultiTrackValuesCategory", "Track Values"));

		const bool bSameType = Types.Num() == 1;
		const bool bAllTransform = bSameType && Algo::AllOf(Types, [](EWidgetAnimType Type) { return WidgetAnimTypeIsKeyframed(Type); });
		const bool bAnyTransform = !Algo::AllOf(Types, [](EWidgetAnimType Type) { return !WidgetAnimTypeIsKeyframed(Type); });

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
					.Text(FText::Format(bRepresented
						? WidgetAnimTips::MultipleShowsFirstTrack()
						: bSameType
						? WidgetAnimTips::MultipleSameType()
						: WidgetAnimTips::MultipleMixedTypes(),
						FText::AsNumber(NumTracks)))
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
		const FName ExpanderName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Expander);
		const FName ShakeName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Shake);
		const bool bAllShake = bSameType && Types.Contains(EWidgetAnimType::Shake);
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

			// A Stagger only where every selected track can fan out - see SWidgetAnimTimeline::CanLayerStagger.
			if (Name == ExpanderName && !bAllCanStagger)
			{
				continue;
			}

			// Part of the Text only where every selected track is on an animated text widget - see CanLayerTargetText.
			if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TextTarget))
			{
				if (bAllCanTargetText)
				{
					Category.AddProperty(Child);
				}
				continue;
			}

			// A Shake's settings flat, as the single-track panel lays them out, when every track selected is a Shake.
			if (Name == ShakeName)
			{
				if (bAllShake)
				{
					uint32 NumShakeChildren = 0;
					Child->GetNumChildren(NumShakeChildren);
					for (uint32 ShakeIndex = 0; ShakeIndex < NumShakeChildren; ++ShakeIndex)
					{
						Values.AddProperty(Child->GetChildHandle(ShakeIndex));
					}
				}
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
							Values.AddProperty(TransformChild);
						}
					}
				}
				continue;
			}

			// A registered type's settings, when every track is of one plugin type, field by field as one track shows them -
			// see FWidgetAnimSelectionDetails::AddTypeParamsRows. Otherwise its Type Params, whose type can't be changed.
			if (Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, TypeParams))
			{
				if (bSameType && Types.Contains(EWidgetAnimType::Custom))
				{
					const UScriptStruct* ParamsStruct = ProxyLayers.Num() > 0 ? ProxyLayers[0]->TypeParams.GetScriptStruct() : nullptr;
					const bool bOneStruct = ParamsStruct && Algo::AllOf(ProxyLayers, [ParamsStruct](const FWidgetAnimLayer* Layer) { return Layer->TypeParams.GetScriptStruct() == ParamsStruct; });
					if (bOneStruct && !WidgetAnimTimeline::IsUserDefinedTrackType(ParamsStruct))
					{
						Values.AddCustomBuilder(MakeShared<WidgetAnimTimeline::FTypeParamsDetails>(Child));
					}
					else
					{
						Values.AddProperty(Child);
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
				const bool bTrackProperty = bShared || Name == DurationName || Name == GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, bInvertTrack);
				IDetailCategoryBuilder& Target = bTrackProperty ? Category : Values;
				Target.AddProperty(Child);
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

void UWidgetAnimPresetSelectionShadow::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	// UObject's, by name: the asset's own PreEditChange takes a property, and hides this one.
	UObject::PreEditChange(PropertyAboutToChange);

	if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
	{
		Owner->OnShadowPreEdit(this, PropertyAboutToChange);
	}
}

void UWidgetAnimPresetSelectionShadow::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// The real asset is given the edit and does what an edit to it does. The stand-in itself only says it changed: run on
	// its one track, the asset's own handling would only work out what the real asset is about to tell it anyway.
	if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
	{
		Owner->OnShadowEdited(this, PropertyChangedEvent);
	}
	UObject::PostEditChangeProperty(PropertyChangedEvent);
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
/**
 * A row of the Animation Tracks list that rests while it is out of view.
 *
 * The list is a plain box of rows, not a virtual list: with a big collapsed track open it holds a row for each of hundreds of
 * tracks, and Slate measures every row - reading every color, text and icon bound on it - every frame, seen or not. A row
 * that wasn't drawn last frame hands Slate nothing inside it to measure and keeps the size it last had; the frame it is
 * drawn again it is measured first, so it is never drawn stale.
 */
class SWidgetAnimRestingRow : public SPanel
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimRestingRow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	SWidgetAnimRestingRow()
		: Children(this)
	{
	}

	void Construct(const FArguments& InArgs)
	{
		Content = InArgs._Content.Widget;
		Children.Add(InArgs._Content.Widget);
	}

	virtual FChildren* GetChildren() override
	{
		if (IsResting())
		{
			return &FNoChildren::NoChildrenInstance;
		}
		return &Children;
	}

	virtual void OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const override
	{
		if (Content.IsValid())
		{
			ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(Content.ToSharedRef(), AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()));
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		// Back in view: measured now, before it is laid out and drawn.
		if (IsResting() && Content.IsValid())
		{
			Content->SlatePrepass(AllottedGeometry.Scale);
		}
		LastPaintFrame = GFrameCounter;
		return SPanel::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

protected:

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override
	{
		if (!IsResting() && Content.IsValid())
		{
			LastDesiredSize = FVector2D(Content->GetDesiredSize());
			bMeasured = true;
		}
		return LastDesiredSize;
	}

private:

	/** Measured once, and not drawn last frame - so out of view. */
	bool IsResting() const
	{
		return bMeasured && GFrameCounter > LastPaintFrame + 1;
	}

	TSlotlessChildren<SWidget> Children;

	/** The row itself - Children's one widget, kept to reach from the const passes. */
	TSharedPtr<SWidget> Content;
	mutable uint64 LastPaintFrame = 0;
	mutable FVector2D LastDesiredSize = FVector2D::ZeroVector;
	mutable bool bMeasured = false;
};

class SWidgetAnimListRow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimListRow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	/** InCollapsed: the row stands for a collapsed track - InLayer its head - and picks every one of its tracks. */
	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel, int32 InGroup, int32 InLayer, FName InCollapsed = NAME_None)
	{
		Panel = InPanel;
		Group = InGroup;
		Layer = InLayer;
		Collapsed = InCollapsed;

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
			// Shift and Ctrl / Cmd pick groups as they pick tracks. A plain press on one of several selected groups keeps
			// them all until the release, which selects it alone.
			const bool bGroupRange = MouseEvent.IsShiftDown();
			const bool bGroupToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();
			if (bGroupRange || bGroupToggle)
			{
				Owner->SelectGroupWithModifiers(Group, bGroupRange, bGroupToggle);
			}
			else if (Owner->GetSelectedGroups().Num() > 1 && Owner->IsGroupSelected(Group))
			{
				bCollapseSelectionOnRelease = true;
			}
			else
			{
				Owner->SelectGroup(Group);
			}
			Owner->RevealGroup(Group);
			return Reply;
		}

		// Shift and Ctrl / Cmd pick layers the way a file browser picks files.
		const bool bRange = MouseEvent.IsShiftDown();
		const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();

		// A collapsed track's row picks every one of its tracks - or, with a modifier, adds them or takes them out.
		if (!Collapsed.IsNone())
		{
			Owner->SelectCollapsed(Collapsed, /*bToggle=*/bRange || bToggle);
			Owner->RevealLayer(Layer);
			return Reply;
		}
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
				// A collapsed track's menu is about every one of its tracks.
				if (!Collapsed.IsNone() && !Owner->IsLayerSelected(Layer))
				{
					Owner->SelectCollapsed(Collapsed);
				}
				PushMenu(MouseEvent, IsGroupRow()
					? Owner->MakeGroupContextMenu(Group)
					: Owner->MakeLayerContextMenu(Layer, INDEX_NONE, TOptional<float>()));
			}
			return FReply::Handled().ReleaseMouseCapture();
		}
		if (Owner && bCollapseSelectionOnRelease && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bCollapseSelectionOnRelease = false;
			if (IsGroupRow())
			{
				Owner->SelectGroup(Group);
			}
			else
			{
				Owner->SelectLayer(Layer);
			}
			return FReply::Handled();
		}

		bCollapseSelectionOnRelease = false;
		return FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override
	{
		// A collapsed track's row opens it on a double-click, as its bar in the Track Editor does.
		return Collapsed.IsNone() ? FCursorReply::Unhandled() : FCursorReply::Cursor(EMouseCursor::Hand);
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (Owner && IsGroupRow())
		{
			Owner->ToggleListGroupCollapsed(Group);
			return FReply::Handled();
		}

		// A collapsed track opens on its own, as it does from the Track Editor.
		if (Owner && !Collapsed.IsNone())
		{
			Owner->EnterCollapsed(Collapsed);
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

	/** The collapsed track this row stands for, or None. */
	FName Collapsed;

	/** Pressed on one of several selected layers: released without a drag, it selects that layer alone. */
	bool bCollapseSelectionOnRelease = false;

	/** Right-pressed here: the menu opens when the button comes up. */
	bool bMenuPending = false;
};

/**
 * A folder's row in the Animation Tracks list (see FWidgetAnimLayerGroup::EditorFolder): a click selects its groups - with
 * Ctrl / Cmd or Shift, adds them - a double click folds it, and a right click offers its menu, on the release as every menu here.
 */
class SWidgetAnimListFolderRow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimListFolderRow) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InPanel, const FString& InFolder)
	{
		Panel = InPanel;
		Folder = InFolder;
		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
		if (!Owner)
		{
			return FReply::Unhandled();
		}

		FReply Reply = FReply::Handled().SetUserFocus(Owner.ToSharedRef(), EFocusCause::Mouse);
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			bMenuPending = true;
			return Reply.CaptureMouse(SharedThis(this));
		}

		TArray<int32> Members;
		Owner->GetFolderGroups(Folder, Members);
		if (MouseEvent.IsControlDown() || MouseEvent.IsCommandDown() || MouseEvent.IsShiftDown())
		{
			for (const int32 Selected : Owner->GetSelectedGroups())
			{
				Members.AddUnique(Selected);
			}
		}
		if (Members.Num() > 0)
		{
			Owner->SelectGroups(Members, Members[0]);
		}
		return Reply;
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::RightMouseButton || !bMenuPending)
		{
			return FReply::Unhandled();
		}
		bMenuPending = false;
		if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
		{
			const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
			FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, Owner->MakeFolderContextMenu(Folder), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		}
		return FReply::Handled().ReleaseMouseCapture();
	}

	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin())
		{
			Owner->ToggleListFolderCollapsed(Folder);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:

	TWeakPtr<SWidgetAnimTimeline> Panel;
	FString Folder;
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
	 * A color, as a small rounded square of it - the way a color reads at a glance, where a hex code has to be decoded. A
	 * thin dark rim keeps a color close to the tooltip's own from vanishing into it.
	 */
	TSharedRef<SWidget> ColorSquare(const FLinearColor& Color)
	{
		return SNew(SBox)
			.WidthOverride(14.f)
			.HeightOverride(14.f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WidgetAnimTimeline::RoundedFill(3.f))
					.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.6f))
				]
				+ SOverlay::Slot()
				.Padding(FMargin(1.f))
				[
					SNew(SImage)
					.Image(WidgetAnimTimeline::RoundedFill(2.f))
					.ColorAndOpacity(Color.CopyWithNewOpacity(1.f))
				]
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

	/** A Transform key's pose as it plays: the identity - the widget as designed - when it Uses Authored Transform. */
	FWidgetTransform KeyPose(const FWidgetTransformKeyframe& Key)
	{
		return Key.bUseAuthoredTransform ? FWidgetTransform() : Key.Transform;
	}

	/** A Transform track's Translation, Scale, Shear and Angle - or a Shape Box or Shape track's one Shape. */
	TArray<FChannel> TransformChannels(EWidgetKeyframeContent Content)
	{
		TArray<FChannel> Channels;

		if (Content != EWidgetKeyframeContent::Transform)
		{
			Channels.Add({ Content == EWidgetKeyframeContent::Design ? TEXT("Design") : TEXT("Shape"),
				[](const FWidgetTransformKeyframe& Key) { return Key.bUseAuthoredShape ? FString(TEXT("Authored")) : FString(TEXT("Custom")); },
				[](const FWidgetTransformKeyframe& Key) { return Key.bUseAuthoredShape; } });
			return Channels;
		}

		Channels.Add({ TEXT("Translation"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(KeyPose(Key).Translation, false); },
			[](const FWidgetTransformKeyframe& Key) { return KeyPose(Key).Translation.IsNearlyZero(1.e-3); } });

		Channels.Add({ TEXT("Scale"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(KeyPose(Key).Scale, true); },
			[](const FWidgetTransformKeyframe& Key) { return KeyPose(Key).Scale.Equals(FVector2D(1.0, 1.0), 1.e-3); } });

		Channels.Add({ TEXT("Shear"),
			[](const FWidgetTransformKeyframe& Key) { return Pair(KeyPose(Key).Shear, false); },
			[](const FWidgetTransformKeyframe& Key) { return KeyPose(Key).Shear.IsNearlyZero(1.e-3); } });

		Channels.Add({ TEXT("Angle"),
			[](const FWidgetTransformKeyframe& Key) { return Angle(KeyPose(Key).Angle); },
			[](const FWidgetTransformKeyframe& Key) { return FMath::IsNearlyZero(KeyPose(Key).Angle, 1.e-3f); } });

		return Channels;
	}

	/** How a keyed track's value reads in a tooltip, by type: an angle in degrees, a color as a rounded square of it, a number. */
	TSharedRef<SWidget> KeyValue(const FWidgetAnimLayer& Layer, int32 Point, const FLinearColor& TextColor)
	{
		// A color: a Material Vector's value, or the color a registered type's key holds (beside its value, when it has one).
		if (WidgetAnimKeyPoints::HasColorKeys(Layer))
		{
			const FLinearColor Color = WidgetAnimKeyPoints::GetColor(Layer, Point);
			const bool bWithValue = Layer.AnimType == EWidgetAnimType::Custom && WidgetAnimGetRegisteredKeyContent(Layer.TypeParams) == EWidgetAnimKeyContent::ValueAndColor;
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ColorSquare(Color) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(bWithValue ? 6.f : 0.f, 0.f, 0.f, 0.f))
				[
					Text(bWithValue ? Number(WidgetAnimKeyPoints::GetValue(Layer, Point)) : FString(), TextColor, ValueFont())
				];
		}

		const float Value = WidgetAnimKeyPoints::GetValue(Layer, Point);
		if (WidgetAnimKeyPoints::HasTwoValues(Layer))
		{
			// A Scale: X and Y - one number when they agree.
			return Text(Pair(FVector2D(Value, WidgetAnimKeyPoints::GetValueY(Layer, Point)), true), TextColor, ValueFont());
		}
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
	/**
	 * Where the panel's layout is remembered - its splitters, the Value Watcher - in the editor's per-project user settings.
	 * (The row height, once kept here too, is Default Track Height in UWidgetAnimEditorSettings now.)
	 */
	const TCHAR* RowHeightConfigSection = TEXT("NameTBD.WidgetAnimations");
	const TCHAR* WatcherExpandedConfigKey = TEXT("ValueWatcherExpanded");
	const TCHAR* ScriptOpenConfigKey = TEXT("ScriptOpen");

	// How the panel's splitters divide it, per layout - see SWidgetAnimTimeline::GetSplitSize. The full panel's keys
	// are shared by the drawer and the docked tab, so a layout set in one is the layout in the other.
	const FName SplitMainKey(TEXT("Split.Main"));
	const FName SplitSelectionKey(TEXT("Split.Selection"));
	const FName SplitListKey(TEXT("Split.List"));
	const FName SplitTracksKey(TEXT("Split.Tracks"));
	const FName SplitScriptTracksKey(TEXT("Split.Script.Tracks"));
	const FName SplitScriptKey(TEXT("Split.Script"));
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
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectChangedHandle);

	if (bCompact)
	{
		SaveCompactState();
	}

	// A pick waiting in the designer stops watching the mouse.
	EndTargetPick(/*bReleaseMouse=*/false);

	// The Script area's window goes with the panel it scripts. A docked tab stays: it belongs to the editor, and finds
	// whichever panel is there to run on.
	if (const TSharedPtr<SWindow> Window = ScriptWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}

	// A collapsed track open on its own - or the Graph Editor - leaves every track playing again once the panel goes.
	if (IsInCollapsedScope() || IsGraphOpen() || bScopeMutesApplied)
	{
		ActiveCollapsed = NAME_None;
		GraphLayers.Reset();
		SyncScopeMutes();
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

	// Edits made anywhere - here, in the Selection panel, a script, an undo - tell the Track Editor its lines are due.
	ObjectChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SWidgetAnimTimeline::OnObjectChangedForLayout);

	// The Selection panel: the edited object, laid out by FWidgetAnimSelectionDetails. No notify hook, so
	// UMG's designer never hears of an edit made here - which is what keeps it from rebuilding its preview,
	// and every Details panel with it, on every commit.
	FDetailsViewArgs DetailsArgs;
	// A search box over the fields, as the State Machine editor's Selection panel has.
	DetailsArgs.bAllowSearch = true;
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

	// The Graph Editor takes the Track Editor's place while it is open - only its place: the list, the Selection panel and
	// the transport stay. See OpenGraph.
	GraphEditor = SNew(SWidgetAnimGraphEditor, WeakThisPtr);
	auto MakeTrackArea = [this]() -> TSharedRef<SWidget>
	{
		return SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return IsGraphOpen() ? 1 : 0; })

			+ SWidgetSwitcher::Slot()
			[
				Tracks.ToSharedRef()
			]

			+ SWidgetSwitcher::Slot()
			[
				GraphEditor.ToSharedRef()
			];
	};

	if (bCompact)
	{
		// The compact panel: the tracks, with the Selection panel beside them, under its own header strip. Folded, only
		// the strip shows. Inline, the transport is a slim row under the tracks alone, so the Selection panel runs the
		// whole height beside them; in its own window, the full transport runs under both.
		const TAttribute<EVisibility> BodyVisibility = TAttribute<EVisibility>::CreateLambda([this]()
		{
			return IsBodyCollapsed() ? EVisibility::Collapsed : EVisibility::Visible;
		});

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
									MakeTrackArea()
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									MakeHorizontalScrollBarSlot()
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

	// The Script area under the Track Editor starts closed: the header has no button to open it any more - scripts are
	// written in the editor's Widget Animation Scripting drawer, or the tab it docks into - so one left open last session
	// isn't brought back with no way to bring it back again once closed.
	bScriptOpen = false;

	// No header row of its own: what the panel works on, and its buttons, sit in the Track Editor's strip - so the tracks
	// start right under it. The transport sits under the Animation Tracks list, and the Track Editor runs to the bottom.
	ChildSlot
	[
		SNew(SVerticalBox)

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

							// The transport and the controls, under the list.
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeTransport(/*bStacked=*/true)
							]
						]

						+ SSplitter::Slot()
						.Value(MakeSplitSize(SplitTracksKey, 0.73f))
						.OnSlotResized(MakeSplitResized(SplitTracksKey))
						[
							// The Track Editor - and under it, opened from its strip, the Script area.
							SNew(SSplitter)
							.Orientation(Orient_Vertical)
							.PhysicalSplitterHandleSize(2.f)

							+ SSplitter::Slot()
							.Value(MakeSplitSize(SplitScriptTracksKey, 0.62f))
							.OnSlotResized(MakeSplitResized(SplitScriptTracksKey))
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
									SNew(SOverlay)

									+ SOverlay::Slot()
									[
										MakeTrackArea()
									]

									// With no animation to show - no asset picked yet, or no timeline made yet - say what to do,
									// centered in the Track Editor under its ruler. A Widget Blueprint with no timelines gets the
									// button that makes its first.
									+ SOverlay::Slot()
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									.Padding(FMargin(16.f, RulerHeight, 16.f, 0.f))
									[
										SNew(SVerticalBox)
										.Visibility_Lambda([this]() { return GetGroups() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Center)
										[
											SNew(STextBlock)
											.AutoWrapText(true)
											.Justification(ETextJustify::Center)
											.Text_Lambda([this]()
											{
												return Source.IsValid() && Source->IsEditingTimeline() ? WidgetAnimTips::NoTimelineYet() : WidgetAnimTips::NothingToEdit();
											})
											.ColorAndOpacity(FSlateColor::UseSubduedForeground())
										]

										+ SVerticalBox::Slot()
										.AutoHeight()
										.HAlign(HAlign_Center)
										.Padding(FMargin(0.f, 8.f, 0.f, 0.f))
										[
											SNew(SButton)
											.ButtonStyle(&OrangeButtonStyle())
											.ContentPadding(FMargin(10.f, 3.f))
											.Visibility_Lambda([this]() { return Source.IsValid() && Source->IsEditingTimeline() ? EVisibility::Visible : EVisibility::Collapsed; })
											.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::NewTimelineButtonTooltip()))
											.OnClicked_Lambda([this]()
											{
												if (Source.IsValid())
												{
													SwitchEditedAnimation([this]() { Source->CreateTimeline(FString()); });
												}
												return FReply::Handled();
											})
											[
												SNew(SHorizontalBox)

												+ SHorizontalBox::Slot()
												.AutoWidth()
												.VAlign(VAlign_Center)
												.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
												[
													WidgetAnimTimeline::MakeFixedIcon(WidgetAnimTimeline::TimelineIcon(), FSlateColor::UseForeground())
												]

												+ SHorizontalBox::Slot()
												.AutoWidth()
												.VAlign(VAlign_Center)
												[
													SNew(STextBlock)
													.Font(FAppStyle::GetFontStyle("NormalFontBold"))
													.Text(LOCTEXT("MakeFirstTimeline", "New Timeline"))
												]
											]
										]
									]
								]

								+ SVerticalBox::Slot()
								.AutoHeight()
								[
									MakeHorizontalScrollBarSlot()
								]
							]

							+ SSplitter::Slot()
							.Value(MakeSplitSize(SplitScriptKey, 0.38f))
							.OnSlotResized(MakeSplitResized(SplitScriptKey))
							[
								SNew(SBox)
								.Visibility_Lambda([this]() { return bScriptOpen ? EVisibility::Visible : EVisibility::Collapsed; })
								[
									SNew(SWidgetAnimScriptPanel)
									.Panel(WeakThisPtr)
									.ScriptKey(GetScriptKey())
									.OnClose_Lambda([this]() { SetScriptOpen(false); })
									.OnDock_Lambda([this]() { DockScript(); })
								]
							]
						]
					]
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

	// The panel scripts from the console reach until another is used - see WidgetAnimScripting::NotePanelUsed.
	WidgetAnimScripting::NotePanelUsed(SharedThis(this));

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
		FSlateColor::UseForeground(), [this]() { GoToStart(); }), FMargin(0.f));
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
		FSlateColor::UseForeground(), [this]() { GoToEnd(); }), FMargin(0.f));

	// The time, right beside the buttons.
	Add(SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.Text_Lambda([this]() { return FText::FromString(FormatPlayheadTag(GetPlayheadSeconds())); }),
		FMargin(6.f, 0.f, 0.f, 0.f));
	Add(SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([this]() { return FText::FromString(TEXT(" / ") + FormatTime(GetPassSeconds())); }),
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
			SwitchEditedAnimation([this, Mode]() { Source->SetMode(Mode); });
		}
	};

	// Animation Timeline or Edit Existing Asset: what the panel is working on.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(4.f, 0.f))
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ModeTooltip()))
		.OnGetMenuContent_Lambda([SwitchMode]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			// Each with the icon the button shows for it.
			Menu.AddMenuEntry(
				LOCTEXT("AnimationTimeline", "Animation Timeline"),
				WidgetAnimTips::TimelineModeTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), FAppStyle::Get().GetOptionalBrush("GraphEditor.Timeline_16x", nullptr, nullptr) ? FName("GraphEditor.Timeline_16x") : FName("ClassIcon.TimelineTemplate")),
				FUIAction(FExecuteAction::CreateLambda([SwitchMode]() { SwitchMode(EMode::Timeline); })));
			Menu.AddMenuEntry(
				LOCTEXT("EditExistingAsset", "Edit Existing Asset"),
				WidgetAnimTips::EditExistingAssetTooltip(),
				FNameTBDStyle::Get().GetOptionalBrush("ClassIcon.WidgetAnimPresetAsset", nullptr, nullptr)
					? FSlateIcon(FNameTBDStyle::GetStyleSetName(), "ClassIcon.WidgetAnimPresetAsset")
					: FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.DataAsset"),
				FUIAction(FExecuteAction::CreateLambda([SwitchMode]() { SwitchMode(EMode::EditExistingAsset); })));
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(TAttribute<const FSlateBrush*>::CreateLambda([IsEditingExisting]()
				{
					return IsEditingExisting() ? AnimationAssetIcon() : TimelineIcon();
				}))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([IsEditingExisting]()
				{
					return IsEditingExisting() ? LOCTEXT("EditExistingAsset", "Edit Existing Asset") : LOCTEXT("AnimationTimeline", "Animation Timeline");
				})
			]
		]
	];

	// Animation Timeline in a Widget Blueprint: which of its timelines - and making, renaming and deleting them.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
	[
		MakeTimelinePicker()
	];

	// Edit Existing Asset: which asset. A fixed width, not a capped fill - a capped fill slot leaves the rest
	// of its share of the row unused, which pushed Dock in Layout and the lock in from the right edge.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(260.f)
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
					UWidgetAnimPresetAsset* Picked = Cast<UWidgetAnimPresetAsset>(AssetData.GetAsset());
					SwitchEditedAnimation([this, Picked]() { Source->SetAsset(Picked); });
				}
			})
		]
	];

	// Animation Timeline in a builder: the name it will be saved under (Animation Name), as a field - double-click to rename it,
	// as a timeline's name is.
	{
		const TSharedRef<SWidgetAnimNameField> BuilderNameText = SNew(SWidgetAnimNameField)
			.Text_Lambda([this]()
			{
				const UWidgetAnimBuilder* Builder = Source.IsValid() ? Source->GetBuilder() : nullptr;
				return Builder && !Builder->AnimationName.IsEmpty()
					? FText::FromString(Builder->AnimationName)
					: LOCTEXT("Title", "Widget Animation");
			})
			.IsReadOnly_Lambda([this]() { return !Source.IsValid() || !Source->GetBuilder(); })
			.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
			{
				UWidgetAnimBuilder* Builder = Source.IsValid() ? Source->GetBuilder() : nullptr;
				const FString Typed = NewText.ToString().TrimStartAndEnd();
				if (!Builder || Typed.IsEmpty() || Typed == Builder->AnimationName)
				{
					return;
				}
				// The Class Defaults' Animation Name, as its Details would set it - one undo step.
				const FScopedTransaction Transaction(LOCTEXT("RenameBuilderAnimation", "Rename Widget Animation"));
				Builder->Modify();
				Builder->AnimationName = Typed;
				FProperty* Property = FindFProperty<FProperty>(UWidgetAnimBuilder::StaticClass(), GET_MEMBER_NAME_CHECKED(UWidgetAnimBuilder, AnimationName));
				FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
				Builder->PostEditChangeProperty(Event);
				Builder->MarkPackageDirty();
			});
		const TWeakPtr<SWidgetAnimNameField> WeakBuilderNameText = BuilderNameText;

		Header->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FStyleColors::Input)
			.Padding(FMargin(6.f, 2.f))
			.Visibility_Lambda([this, IsEditingExisting]() { return Source.IsValid() && Source->IsBuilder() && !IsEditingExisting() ? EVisibility::Visible : EVisibility::Collapsed; })
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::BuilderAnimationNameTooltip()))
			.OnMouseDoubleClick_Lambda([WeakBuilderNameText](const FGeometry&, const FPointerEvent& MouseEvent)
			{
				const TSharedPtr<SWidgetAnimNameField> Pinned = WeakBuilderNameText.Pin();
				if (!Pinned.IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Unhandled();
				}
				Pinned->EnterEditingMode();
				return FReply::Handled();
			})
			[
				SNew(SBox)
				.MinDesiredWidth(120.f)
				[
					BuilderNameText
				]
			]
		];
	}

	// Inside a collapsed track or the Graph Editor: where the panel is, and the way back out.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
	[
		MakeCollapsedBreadcrumb()
	];

	// The Graph Editor, for the tracks selected.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
	[
		MakeGraphButton()
	];

	// Everything after it sits at the right - Time In and Fit first (see below), then Save, Dock, the lock.
	const int32 RightGroupIndex = Header->NumSlots() + 1;
	Header->AddSlot()
	.FillWidth(1.f)
	[
		SNew(SSpacer)
	];

	// Save, as a plain editor button: the editor's own save icon and the word. Editing an asset it saves that asset; a timeline
	// already saved as one updates it; a timeline or a builder's tracks not yet saved as one, Save Animation To asks where -
	// the Default Folder, under a name asked for, or a Custom Location picked in the editor's Save Asset As window.
	auto SaveContent = [](TAttribute<FText> Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Save"))
				.DesiredSizeOverride(FVector2D(16.f, 16.f))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Label)
			];
	};
	auto SaveNew = [this](bool bChooseLocation)
	{
		if (!Source.IsValid())
		{
			return;
		}
		// The builder goes on to edit what it saved; a Widget Blueprint's timeline stays open as it was.
		const UObject* Before = Source->GetEditedObject();
		if (Source->SaveAsNewAsset(bChooseLocation) && Source->GetEditedObject() != Before)
		{
			SwitchEditedAnimation([]() {});
		}
	};
	auto MakeSaveToMenu = [this, SaveNew]() -> TSharedRef<SWidget>
	{
		FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
		const FString Folder = Source.IsValid() ? Source->GetDefaultSaveFolder() : FString();
		Menu.AddMenuEntry(
			FUIAction(FExecuteAction::CreateLambda([SaveNew]() { SaveNew(/*bChooseLocation=*/false); })),
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(WidgetAnimTips::SaveToDefaultFolder())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("SmallFont"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(Folder.IsEmpty() ? FString(TEXT("/Game")) : Folder))
			],
			NAME_None,
			FText::Format(WidgetAnimTips::SaveToDefaultFolderTooltip(), FText::FromString(Folder.IsEmpty() ? FString(TEXT("/Game")) : Folder)));
		Menu.AddMenuEntry(
			WidgetAnimTips::SaveToCustomLocation(),
			WidgetAnimTips::SaveToCustomLocationTooltip(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([SaveNew]() { SaveNew(/*bChooseLocation=*/true); })));
		return Menu.MakeWidget();
	};
	// A timeline already saved as an asset: Save updates that asset.
	auto HasSavedAsset = [this]() { return Source.IsValid() && Source->GetMode() == EMode::Timeline && Source->GetSavedAsset() != nullptr; };

	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 2.f))
		.Visibility_Lambda([IsEditingExisting]() { return IsEditingExisting() ? EVisibility::Visible : EVisibility::Collapsed; })
		.IsEnabled_Lambda([this]() { return Source.IsValid() && Source->CanUpdateAsset(); })
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
		{
			const UWidgetAnimPresetAsset* Asset = Source.IsValid() ? Source->GetAsset() : nullptr;
			return Asset ? FText::Format(LOCTEXT("SaveAssetNamed", "Save: save the changes made to {0}. Every widget using it plays them."), FText::FromString(Asset->GetName())) : WidgetAnimTips::UpdateTooltip();
		})))
		.OnClicked_Lambda([this]()
		{
			if (Source.IsValid())
			{
				Source->UpdateAsset();
			}
			return FReply::Handled();
		})
		[
			SaveContent(LOCTEXT("Save", "Save"))
		]
	];

	// A timeline saved before: Save, which updates its asset, and beside it Save Animation To, to make another.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 2.f))
		.Visibility_Lambda([IsEditingExisting, HasSavedAsset]() { return !IsEditingExisting() && HasSavedAsset() ? EVisibility::Visible : EVisibility::Collapsed; })
		.IsEnabled_Lambda([this]() { return Source.IsValid() && Source->CanSaveAsNewAsset(); })
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
		{
			const UWidgetAnimPresetAsset* Saved = Source.IsValid() ? Source->GetSavedAsset() : nullptr;
			return Saved
				? FText::Format(LOCTEXT("SaveTimelineToAssetTip", "Save: put this timeline into {0}, the animation it was saved as, and save it. Every widget using it plays the change."), FText::FromString(Saved->GetName()))
				: FText::GetEmpty();
		})))
		.OnClicked_Lambda([this]()
		{
			if (Source.IsValid())
			{
				Source->SaveToSavedAsset();
			}
			return FReply::Handled();
		})
		[
			SaveContent(LOCTEXT("Save", "Save"))
		]
	];
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(1.f, 0.f, 4.f, 0.f))
	[
		SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(2.f, 2.f))
		.HasDownArrow(true)
		.Visibility_Lambda([IsEditingExisting, HasSavedAsset]() { return !IsEditingExisting() && HasSavedAsset() ? EVisibility::Visible : EVisibility::Collapsed; })
		.IsEnabled_Lambda([this]() { return Source.IsValid() && Source->CanSaveAsNewAsset(); })
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
		{
			return Source.IsValid() && Source->IsBuilder() ? WidgetAnimTips::SaveAsBuilderTooltip() : WidgetAnimTips::SaveAsTooltip();
		})))
		.OnGetMenuContent_Lambda(MakeSaveToMenu)
	];

	// Never saved: Save Animation To.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(FMargin(4.f, 0.f))
	[
		SNew(SComboButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 2.f))
		.Visibility_Lambda([IsEditingExisting, HasSavedAsset]() { return IsEditingExisting() || HasSavedAsset() ? EVisibility::Collapsed : EVisibility::Visible; })
		.IsEnabled_Lambda([this]() { return Source.IsValid() && Source->CanSaveAsNewAsset(); })
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
		{
			return Source.IsValid() && Source->IsBuilder() ? WidgetAnimTips::SaveAsBuilderTooltip() : WidgetAnimTips::SaveAsTooltip();
		})))
		.OnGetMenuContent_Lambda(MakeSaveToMenu)
		.ButtonContent()
		[
			SaveContent(LOCTEXT("SaveAnimationTo", "Save Animation To"))
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

	// Time In: Seconds or Percent - an animation that follows a progress value instead of time. Only where the animation
	// keeps one: an asset or a timeline, the builder; never a component's plain list. In the animation orange while in
	// Percent, so a percentage timeline reads as one at a glance.
	{
		auto TimeUnitName = [](EWidgetAnimTimeUnit Unit)
		{
			return Unit == EWidgetAnimTimeUnit::Percent ? LOCTEXT("TimeInPercent", "Percent") : LOCTEXT("TimeInSeconds", "Seconds");
		};

		// First of the buttons on the right, with Fit right of it; the lock is then the last thing on the right.
		Header->InsertSlot(RightGroupIndex)
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
		[
			SNew(SWidgetAnimAnchoredTip)
			.Text(WidgetAnimTips::TimeUnitTooltip())
			.Visibility_Lambda([this]() { return !bCompact && HasAnimation() && Source.IsValid() && Source->CanSetTimeUnit() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SComboButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(4.f, 1.f))
				.IsEnabled_Lambda([this]() { return CanEdit(); })
				.OnGetMenuContent_Lambda([this, TimeUnitName]() -> TSharedRef<SWidget>
				{
					FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
					Menu.BeginSection("TimeIn", LOCTEXT("TimeInHeading", "Time In"));
					for (const EWidgetAnimTimeUnit Unit : { EWidgetAnimTimeUnit::Seconds, EWidgetAnimTimeUnit::Percent })
					{
						Menu.AddMenuEntry(
							TimeUnitName(Unit),
							Unit == EWidgetAnimTimeUnit::Percent ? WidgetAnimTips::TimeInPercentTooltip() : WidgetAnimTips::TimeInSecondsTooltip(),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([this, Unit]()
								{
									SetTimeUnit(Unit);
									UWidgetAnimEditorSettings::GetMutable().SetShowTimeInFrames(false);
								}),
								FCanExecuteAction::CreateLambda([this]() { return CanEdit(); }),
								FIsActionChecked::CreateLambda([this, Unit]()
								{
									return Source.IsValid() && Source->GetTimeUnit() == Unit && !(Unit == EWidgetAnimTimeUnit::Seconds && IsFrameTime());
								})),
							NAME_None,
							EUserInterfaceActionType::RadioButton);
					}

					// Frames (Preview Only): seconds, shown as frames at a target frame rate - see IsFrameTime. Only how the panel
					// reads time, so it is picked even while the animation can't be edited, unless that would mean leaving Percent.
					// Its target frame rate is typed in the entry itself, beside its name: a click in the box types there, and leaves
					// the menu open; a click anywhere else on the entry picks it.
					Menu.AddMenuEntry(
						FUIAction(
							FExecuteAction::CreateLambda([this]()
							{
								if (IsPercentTime())
								{
									SetTimeUnit(EWidgetAnimTimeUnit::Seconds);
								}
								if (!IsPercentTime())
								{
									UWidgetAnimEditorSettings::GetMutable().SetShowTimeInFrames(true);
								}
							}),
							FCanExecuteAction::CreateLambda([this]() { return !IsPercentTime() || CanEdit(); }),
							FIsActionChecked::CreateLambda([this]() { return IsFrameTime(); })),
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TimeInFrames", "Frames (Preview Only)"))
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(8.f, 0.f, 0.f, 0.f))
						[
							SNew(SBox)
							.WidthOverride(58.f)
							.ToolTipText(WidgetAnimTips::TimeInFramesRateTooltip())
							[
								SNew(SNumericEntryBox<float>)
								.AllowSpin(true)
								.MinValue(1.f)
								.MaxValue(1000.f)
								.MinSliderValue(12.f)
								.MaxSliderValue(240.f)
								.Delta(1.f)
								.Value_Lambda([]() { return TOptional<float>(SWidgetAnimTimeline::GetFrameRate()); })
								.OnValueChanged_Lambda([](float NewValue) { UWidgetAnimEditorSettings::GetMutable().SetPreviewFrameRate(FMath::RoundToFloat(NewValue)); })
								.OnValueCommitted_Lambda([](float NewValue, ETextCommit::Type) { UWidgetAnimEditorSettings::GetMutable().SetPreviewFrameRate(FMath::RoundToFloat(NewValue)); })
							]
						]

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
						[
							SNew(STextBlock)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(LOCTEXT("TimeInFramesRateUnit", "fps"))
						],
						NAME_None,
						WidgetAnimTips::TimeInFramesTooltip(),
						EUserInterfaceActionType::RadioButton);
					Menu.EndSection();
					return Menu.MakeWidget();
				})
				.ButtonContent()
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity_Lambda([this]() { return IsPercentTime() ? FSlateColor(NameTBD::Colors::AnimationOrange_Normal) : FSlateColor::UseSubduedForeground(); })
					.Text_Lambda([this]()
					{
						if (IsPercentTime())
						{
							return LOCTEXT("TimeInPercentShort", "0 - 100%");
						}
						return IsFrameTime()
							? FText::Format(LOCTEXT("TimeInFramesShort", "Frames @ {0} fps"), FText::AsNumber(FMath::RoundToInt(GetFrameRate())))
							: LOCTEXT("TimeInSecondsShort", "Seconds");
					})
				]
			]
		];
	}

	// Fit: the whole animation back in view - the F key, or a double-click on the ruler, does the same. On a faint chip,
	// so it reads as a button to press - not a toggle, which the lit chips of the controls pill are. The graph editors'
	// own Zoom to Fit icon, under whichever name this engine version has it; "Fit" if it has none.
	{
		const FSlateBrush* FitIcon = PickBrush({ "GraphEditor.ZoomToFit", "GenericCurveEditor.ZoomToFit", "CurveEditor.ZoomToFit", "Icons.ZoomToFit", "Sequencer.ZoomToFit" });
		const TSharedRef<TWeakPtr<SWidget>> FitWidget = MakeShared<TWeakPtr<SWidget>>();
		const TSharedRef<SButton> FitButton = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this]()
			{
				if (IsGraphOpen() && GraphEditor.IsValid())
				{
					GraphEditor->FitView();
				}
				else if (Tracks.IsValid())
				{
					Tracks->FitToAnimation();
				}
				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(ChipBrush())
				.BorderBackgroundColor_Lambda([FitWidget]()
				{
					const TSharedPtr<SWidget> Pinned = FitWidget->Pin();
					return FSlateColor(Pinned.IsValid() && Pinned->IsHovered() ? Palette::FitButtonChipHovered : Palette::FitButtonChip);
				})
				.Padding(FMargin(5.f, 2.f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					FitIcon
						? MakeFixedIcon(FitIcon, FSlateColor::UseForeground(), FitIconSize)
						: StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("Fit", "Fit")))
				]
			];
		*FitWidget = FitButton;

		Header->InsertSlot(RightGroupIndex + 1)
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0.f, 0.f, 8.f, 0.f))
		[
			SNew(SWidgetAnimAnchoredTip)
			.Text(WidgetAnimTips::FitTooltip())
			[
				FitButton
			]
		];
	}

	// The Track Editor's strip - one slim strip, level with the Animation Tracks list's.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(6.f, 0.f, 4.f, 0.f))
		[
			SNew(SBox)
			.HeightOverride(SectionHeaderHeight)
			.VAlign(VAlign_Center)
			[
				Header
			]
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

				// Inside a collapsed track or the Graph Editor: where the panel is, and the way back out.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f))
				[
					MakeCollapsedBreadcrumb()
				]

				// The Graph Editor, for the tracks selected.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(4.f, 0.f))
				[
					MakeGraphButton()
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

TSharedRef<SWidget> SWidgetAnimTimeline::MakeHorizontalScrollBarSlot()
{
	// Shown only while the view shows less than the whole animation, and only with Show Horizontal Scroll Bar When
	// Necessary on in the editor preferences. Never inside a Details panel, where the panel is too short to spare it.
	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const bool bShown = !IsInline() && !IsGraphOpen()
				&& UWidgetAnimEditorSettings::Get().bShowHorizontalScrollBarWhenNeeded
				&& Tracks.IsValid() && Tracks->IsHorizontalScrollNeeded();
			return bShown ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			TracksHorizontalScrollBar.ToSharedRef()
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeAnimationPropertiesButton()
{
	using namespace WidgetAnimTimeline;

	// Italic, and a faint orange until hovered - a quiet way into the animation's own settings (its Speed
	// Multiplier, its description), which clicking off every track also selects. A real button, edges and all, so it
	// reads as one to press rather than as a label.
	const TSharedRef<TWeakPtr<SWidget>> ButtonWidget = MakeShared<TWeakPtr<SWidget>>();
	const TSharedRef<SButton> Button = SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "Button")
		.ContentPadding(FMargin(8.f, 2.f))
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
			.Padding(FMargin(6.f, 4.f))
			[
				Button
			]
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTrackEditorHeader()
{
	using namespace WidgetAnimTimeline;

	// What the panel works on, a collapsed track's way back out, Save, Dock in Layout, the lock and Fit - all in one strip.
	return MakeHeader();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeCollapsedBreadcrumb()
{
	using namespace WidgetAnimTimeline;

	// As a collapsed graph's tab reads: the whole animation, then the collapsed track open on its own, then - in the Graph
	// Editor - the tracks it shows. Each step is the way back to it: the back arrow and All Tracks to the whole animation,
	// the collapsed track's name back to it from the graph. Escape goes back one step.
	auto Chevron = []()
	{
		return MakeFixedIcon(PickBrush({ "Icons.ChevronRight", "TreeArrow_Collapsed" }), FSlateColor::UseSubduedForeground(), 10.f);
	};

	// The graph's step names its track - or, for several, how many - in the color the curves are drawn in.
	auto GetGraphColor = [this]()
	{
		const TArray<FWidgetAnimLayer>* Layers = GetLayers();
		return GraphLayers.Num() > 0 && Layers && Layers->IsValidIndex(GraphLayers[0])
			? FSlateColor(GetLayerTypeColor((*Layers)[GraphLayers[0]]))
			: FSlateColor::UseForeground();
	};

	return SNew(SHorizontalBox)
		.Visibility_Lambda([this]() { return IsInCollapsedScope() || IsGraphOpen() ? EVisibility::Visible : EVisibility::Collapsed; })

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(3.f, 1.f))
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this]()
			{
				return IsGraphOpen() ? WidgetAnimTips::GraphAllTracksTooltip() : WidgetAnimTips::LeaveCollapsedTooltip();
			})))
			.OnClicked_Lambda([this]()
			{
				ExitGraph();
				ExitCollapsed();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					MakeFixedIcon(PickBrush({ "Icons.ArrowLeft", "Icons.ChevronLeft", "GenericCommands.Undo" }), FSlateColor::UseSubduedForeground(), 12.f)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(LOCTEXT("BreadcrumbAllTracks", "All Tracks"))
				]
			]
		]

		// The collapsed track open on its own - from the graph, the way back to it.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return IsInCollapsedScope() ? EVisibility::Visible : EVisibility::Collapsed; })

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(3.f, 0.f))
			[
				Chevron()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(2.f, 1.f))
				.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::LeaveGraphTooltip()))
				.OnClicked_Lambda([this]()
				{
					ExitGraph();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						MakeFixedIcon(PickBrush({ "GraphEditor.SubGraph_16x", "Icons.FolderClosed" }), FSlateColor(Palette::LayerCollapsed), 12.f)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.MaxWidth(220.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.ColorAndOpacity(Palette::LayerCollapsed)
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text_Lambda([this]() { return FText::FromName(ActiveCollapsed); })
					]
				]
			]
		]

		// The Graph Editor: the curve icon and the track it shows.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this]() { return IsGraphOpen() ? EVisibility::Visible : EVisibility::Collapsed; })

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(3.f, 0.f))
			[
				Chevron()
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(SWidgetAnimGraphEditor::GetGraphIcon(), TAttribute<FSlateColor>::CreateLambda(GetGraphColor), 12.f)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.MaxWidth(260.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle("NormalFontBold"))
				.ColorAndOpacity_Lambda(GetGraphColor)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([this]()
				{
					const TArray<FWidgetAnimLayer>* Layers = GetLayers();
					if (GraphLayers.Num() == 1 && Layers && Layers->IsValidIndex(GraphLayers[0]))
					{
						return GetLayerDisplayName((*Layers)[GraphLayers[0]]);
					}
					return FText::Format(LOCTEXT("BreadcrumbGraphTracks", "{0} Tracks"), FText::AsNumber(GraphLayers.Num()));
				})
			]
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeGraphButton()
{
	using namespace WidgetAnimTimeline;

	// The curve icon and its name, quiet until hovered - shown only while a track the graph can show is selected.
	return SNew(SWidgetAnimAnchoredTip)
		.Text(WidgetAnimTips::GraphEditorButtonTooltip())
		.Visibility_Lambda([this]()
		{
			return !IsGraphOpen() && HasAnimation() && GetGraphableLayers(SelectedLayers).Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 1.f))
			.OnClicked_Lambda([this]()
			{
				OpenGraph(SelectedLayers);
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[
					MakeFixedIcon(SWidgetAnimGraphEditor::GetGraphIcon(), FSlateColor::UseForeground(), 14.f)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.Text(LOCTEXT("GraphEditorButton", "Graph Editor"))
				]
			]
		];
}

bool SWidgetAnimTimeline::CanGraphLayer(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}

	// Something with values along a curve: not a whole animation inserted as a track, not a cue - which fires at points and
	// moves nothing - and not a Pulse or Shake, which move about where the widget is with no keys to shape.
	// Not a Design track either - a look is no one number to draw. A Sound cue is, for its volume and fades.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	if (Layer.IsAnimationTrack() || Layer.AnimType == EWidgetAnimType::Design)
	{
		return false;
	}
	if (IsCueLayer(Layer))
	{
		return Layer.TypeParams.GetPtr<FWidgetAnimSoundCueParams>() != nullptr;
	}
	return WidgetAnimTypeIsKeyframed(Layer.AnimType) || WidgetAnimTypeSupportsValueKeys(Layer.AnimType);
}

TArray<int32> SWidgetAnimTimeline::GetGraphableLayers(const TArray<int32>& LayerIndices) const
{
	TArray<int32> Out;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Out.Num() >= MaxGraphLayers)
		{
			break;
		}
		if (CanGraphLayer(LayerIndex))
		{
			Out.AddUnique(LayerIndex);
		}
	}
	return Out;
}

void SWidgetAnimTimeline::OpenGraph(const TArray<int32>& LayerIndices)
{
	const TArray<int32> Graphable = GetGraphableLayers(LayerIndices);
	if (Graphable.Num() == 0 || !GraphEditor.IsValid())
	{
		return;
	}

	if (IsDragging())
	{
		IsDraggingKey() ? EndKeyDrag() : EndLayerDrag();
	}

	GraphLayers = Graphable;
	GraphEditor->OnLayersShown();
	SyncScopeMutes();

	// The graph takes the keyboard, so F, Delete and Escape are its own from the start.
	if (const TSharedPtr<SWidget> Focus = GraphEditor->GetFocusTarget())
	{
		FSlateApplication::Get().SetKeyboardFocus(Focus, EFocusCause::SetDirectly);
	}
}

void SWidgetAnimTimeline::ExitGraph()
{
	if (!IsGraphOpen())
	{
		return;
	}

	const TArray<int32> Was = GraphLayers;
	GraphLayers.Reset();
	SyncScopeMutes();

	// Back on the Track Editor, on the tracks the graph showed.
	if (Was.Num() > 0 && GetLayers() && GetLayers()->IsValidIndex(Was[0]))
	{
		RevealLayer(Was[0]);
	}
	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
}

void SWidgetAnimTimeline::SyncGraphLayers()
{
	// Tracks gone - an undo, a delete - or turned into something with no curves drop out of it; with none left, it closes.
	const TArray<int32> Valid = GetGraphableLayers(GraphLayers);
	if (Valid.Num() == 0)
	{
		ExitGraph();
		return;
	}
	if (Valid != GraphLayers)
	{
		GraphLayers = Valid;
	}

	// Picking other tracks in the list shows them instead - as the graph's own key picks only ever select its own tracks,
	// a selection reaching past them is one made elsewhere.
	const bool bReachesPast = SelectedLayers.ContainsByPredicate([this](int32 LayerIndex) { return !GraphLayers.Contains(LayerIndex); });
	if (bReachesPast)
	{
		const TArray<int32> Selected = GetGraphableLayers(SelectedLayers);
		if (Selected.Num() > 0 && Selected != GraphLayers)
		{
			GraphLayers = Selected;
			if (GraphEditor.IsValid())
			{
				GraphEditor->OnLayersShown();
			}
		}
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTransport(bool bStacked)
{
	using namespace WidgetAnimTimeline;

	// One segment of the controls pill: an icon when there is one, else its text, lit with a rounded chip while on.
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
					.Padding(Icon ? FMargin(7.f, 3.f) : FMargin(10.f, 3.f))
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						Content
					]
				]
			];
	};

	// A segment for how the preview plays, which the source keeps. Shown as an icon - the plugin's own from Resources
	// if one has been dropped in, else the editor's - or as its text.
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

	// ---- One pill of controls: how the preview plays - Loop, Ping Pong, Invert, Speed - then how tracks behave when
	// dragged - Snapping, Linked Edges - and the designer's Motion Path. ------------------------------------------------

	const TSharedRef<SHorizontalBox> PillRow = SNew(SHorizontalBox);
	auto AddToPill = [&PillRow](const TSharedRef<SWidget>& Widget, const FMargin& Padding = FMargin(0.f))
	{
		PillRow->AddSlot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(Padding)
		[
			Widget
		];
	};

	AddToPill(MakeSegment(LOCTEXT("Loop", "Loop"),
		WidgetAnimTips::LoopTooltip(),
		[](const IWidgetAnimPanelSource& In) { return In.GetLoop(); },
		[](IWidgetAnimPanelSource& In, bool bValue) { In.SetLoop(bValue); },
		nullptr,
		FindControlIcon(TEXT("WidgetAnim_Loop"), { "Animation.Loop.Enabled", "Animation.Loop", "Icons.Loop", "GenericLoop" })));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));
	AddToPill(MakeSegment(LOCTEXT("PingPong", "Ping Pong"),
		WidgetAnimTips::PingPongTooltip(),
		[](const IWidgetAnimPanelSource& In) { return In.GetPingPong(); },
		[](IWidgetAnimPanelSource& In, bool bValue) { In.SetPingPong(bValue); },
		[](const IWidgetAnimPanelSource& In) { return In.GetLoop(); },
		FindControlIcon(TEXT("WidgetAnim_PingPong"), { "Animation.PingPong", "Icons.PingPong" })));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));
	AddToPill(MakeSegment(LOCTEXT("Invert", "Invert"),
		WidgetAnimTips::InvertTooltip(),
		[](const IWidgetAnimPanelSource& In) { return In.GetInvert(); },
		[](IWidgetAnimPanelSource& In, bool bValue) { In.SetInvert(bValue); },
		nullptr,
		FindControlIcon(TEXT("WidgetAnim_Invert"), { "Icons.Reverse", "Animation.Backward" })));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));

	// Speed: its label styled as the segments are, and a compact number beside it - both tipped "Preview Speed".
	AddToPill(
		SNew(SWidgetAnimAnchoredTip)
		.Text(WidgetAnimTips::SpeedTooltip())
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(Palette::ToggleOffText)
			.Text(LOCTEXT("Speed", "Speed"))
		],
		FMargin(8.f, 0.f, 5.f, 0.f));
	AddToPill(
		SNew(SWidgetAnimAnchoredTip)
		.Text(WidgetAnimTips::SpeedTooltip())
		[
			SNew(SBox)
			.WidthOverride(48.f)
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
		],
		FMargin(0.f, 0.f, 4.f, 0.f));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));
	AddToPill(MakePillToggle(LOCTEXT("TrackSnapping", "Snapping"),
		WidgetAnimTips::TrackSnappingTooltip(),
		[]() { return UWidgetAnimEditorSettings::Get().bTrackSnapping; },
		[](bool bValue) { UWidgetAnimEditorSettings::GetMutable().SetTrackSnapping(bValue); },
		[]() { return true; },
		FindControlIcon(TEXT("WidgetAnim_Snapping"), { "Sequencer.ToggleIsSnapEnabled", "EditorViewport.LocationGridSnap", "GenericCurveEditor.ToggleSnapping", "Icons.Snap" })));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));
	AddToPill(MakePillToggle(LOCTEXT("LinkedEdges", "Linked Edges"),
		WidgetAnimTips::LinkedEdgesTooltip(),
		[]() { return UWidgetAnimEditorSettings::Get().bLinkedEdges; },
		[](bool bValue) { UWidgetAnimEditorSettings::GetMutable().SetLinkedEdges(bValue); },
		[]() { return true; },
		FindControlIcon(TEXT("WidgetAnim_LinkedEdges"), { "Icons.Link", "Icons.Chain" })));
	AddToPill(MakeDivider(), FMargin(2.f, 0.f));
	AddToPill(MakePillToggle(LOCTEXT("MotionPaths", "Motion Path"),
		WidgetAnimTips::MotionPathsTooltip(),
		[]() { return UWidgetAnimEditorSettings::Get().bShowMotionPaths; },
		[](bool bValue) { UWidgetAnimEditorSettings::GetMutable().SetShowMotionPaths(bValue); },
		[]() { return true; },
		FindControlIcon(TEXT("WidgetAnim_MotionPath"), { "Icons.Spline", "CurveEditor.Curve" })));

	const TSharedRef<SWidget> Pill = SNew(SBorder)
		.BorderImage(ControlsPillBrush())
		.Padding(FMargin(3.f, 2.f))
		[
			PillRow
		];

	// ---- The transport: Go to Start, the previous key, Play, the next key, Go to End - tight together. -------------------

	const TSharedRef<SWidget> Buttons = SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton({ "Animation.Backward_End", "Sequencer.Transport.GoToFront.Normal" }, LOCTEXT("ToStart", "|<"), WidgetAnimTips::ToStartTooltip(),
				FOnClicked::CreateLambda([this]() { GoToStart(); return FReply::Handled(); }), TransportIconSize)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton({ "Animation.Backward_Step", "Sequencer.Transport.StepBackwards.Normal" }, LOCTEXT("PrevKey", "<"), WidgetAnimTips::PrevKeyTooltip(),
				FOnClicked::CreateLambda([this]() { StepToMarker(-1); return FReply::Handled(); }), TransportIconSize)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(2.f, 0.f))
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
				.ContentPadding(FMargin(4.f, 2.f))
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
						TransportIconSize + 2.f)
				]
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton({ "Animation.Forward_Step", "Sequencer.Transport.StepForward.Normal" }, LOCTEXT("NextKey", ">"), WidgetAnimTips::NextKeyTooltip(),
				FOnClicked::CreateLambda([this]() { StepToMarker(1); return FReply::Handled(); }), TransportIconSize)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			MakeIconButton({ "Animation.Forward_End", "Sequencer.Transport.GoToEnd.Normal" }, LOCTEXT("ToEnd", ">|"), WidgetAnimTips::ToEndTooltip(),
				FOnClicked::CreateLambda([this]() { GoToEnd(); return FReply::Handled(); }), TransportIconSize)
		];

	// ---- The time, in a recessed readout like the native editor's current-time box. ------------------------------------

	const TSharedRef<SWidget> Time = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(FMargin(8.f, 2.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.Text_Lambda([this]() { return FText::FromString(FormatPlayheadTag(GetPlayheadSeconds())); })
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([this]() { return FText::FromString(TEXT(" / ") + FormatTime(GetPassSeconds())); })
			]
		];

	// Under the Animation Tracks list: the transport and the time on one row, the pill under them - so the Track Editor
	// runs all the way down beside them. The two rows share one width, centered: the buttons line up with the pill's
	// left end and the time with its right, rather than the time pushing the buttons off center.
	if (bStacked)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
			.Padding(FMargin(6.f, 3.f, 6.f, 5.f))
			.HAlign(HAlign_Center)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Buttons
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(SSpacer)
						.Size(FVector2D(6.f, 0.f))
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Time
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
				[
					Pill
				]
			];
	}

	// Along the bottom: the pill on the left, the transport in the middle, the time on the right.
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(8.f, 3.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				Pill
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(8.f, 0.f))
			[
				Buttons
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				Time
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

bool SWidgetAnimTimeline::IsPercentTime() const
{
	return Source.IsValid() && Source->GetTimeUnit() == EWidgetAnimTimeUnit::Percent;
}

FString SWidgetAnimTimeline::FormatTime(float Seconds, int32 Decimals) const
{
	if (IsPercentTime())
	{
		return FormatPercent(Seconds);
	}
	if (IsFrameTime())
	{
		return FormatFrames(Seconds, GetFrameRate()) + TEXT(" f");
	}
	return FString::Printf(TEXT("%.*f s"), Decimals, Seconds);
}

FString SWidgetAnimTimeline::FormatSpan(float Start, float End) const
{
	if (IsPercentTime())
	{
		FString Text = FormatPercent(Start);
		Text.LeftChopInline(1);
		return Text + TEXT(" - ") + FormatPercent(End);
	}
	if (IsFrameTime())
	{
		const float FrameRate = GetFrameRate();
		return FString::Printf(TEXT("%s - %s f"), *FormatFrames(Start, FrameRate), *FormatFrames(End, FrameRate));
	}
	return FString::Printf(TEXT("%.2f - %.2f s"), Start, End);
}

bool SWidgetAnimTimeline::IsFrameTime() const
{
	return UWidgetAnimEditorSettings::Get().bShowTimeInFrames && !IsPercentTime();
}

float SWidgetAnimTimeline::GetFrameRate()
{
	return FMath::Max(UWidgetAnimEditorSettings::Get().PreviewFrameRate, 1.f);
}

FString SWidgetAnimTimeline::FormatFrames(float Seconds, float FrameRate)
{
	// Rounded to two places, then trailing zeros - and a point left with nothing after it - dropped: 30, 12.5, 7.25.
	const double Frames = FMath::RoundHalfFromZero(static_cast<double>(Seconds) * FMath::Max(FrameRate, 1.f) * 100.0) / 100.0;
	FString Text = FString::Printf(TEXT("%.2f"), Frames);
	while (Text.EndsWith(TEXT("0")))
	{
		Text.LeftChopInline(1);
	}
	if (Text.EndsWith(TEXT(".")))
	{
		Text.LeftChopInline(1);
	}
	return Text == TEXT("-0") ? FString(TEXT("0")) : Text;
}

int32 SWidgetAnimTimeline::PaintPlayhead(FSlateWindowElementList& OutDrawElements, const FGeometry& Geometry, int32 LayerId, float X, float RulerWidgetHeight, float Bottom, float Left, float Right, const FString& TagText)
{
	using namespace WidgetAnimTimeline;
	const UWidgetAnimEditorSettings& Look = UWidgetAnimEditorSettings::Get();
	const FLinearColor Color = Look.PlayheadColor;

	// The head, in the top of the ruler: the time on the playhead's color in a compact rounded tag - as the playheads of
	// Premiere, Resolve and Sequencer read - with a notch under its middle pointing down at the line.
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo TagFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FVector2f TextSize = FVector2f(Measure->Measure(TagText, TagFont));
	const float LineThickness = FMath::Max(Look.PlayheadLineThickness, 1.f);

	// Laid out on whole pixels, the tag and the notch each centered on the line (SnapColumn): at any zoom the line is the
	// same width and runs out of the notch's point, which sits under the tag's middle.
	const FPixelColumn TagColumn = SnapColumn(Geometry, X + 0.5f, LineThickness, TextSize.X + 10.f);
	const float TagY = SnapY(Geometry, 2.f);
	const float TagBottom = SnapY(Geometry, 2.f + TextSize.Y + 2.f);
	const float TagX = SnapX(Geometry, FMath::Clamp(TagColumn.HeadLeft, Left, FMath::Max(Right - TagColumn.HeadWidth, Left)));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
		Geometry.ToPaintGeometry(FVector2f(TagColumn.HeadWidth, TagBottom - TagY), FSlateLayoutTransform(FVector2f(TagX, TagY))),
		RoundedFill(3.f), ESlateDrawEffect::None, Color);

	// The notch: the start and end markers' triangle, small, tucked a pixel under the tag so no seam shows between them.
	const FPixelColumn NotchColumn = SnapColumn(Geometry, X + 0.5f, LineThickness, 8.f);
	const float NotchTop = SnapY(Geometry, TagBottom - 1.f);
	const float NotchPoint = SnapY(Geometry, TagBottom + 4.f);
	MakeDownTriangle(OutDrawElements, LayerId + 1, Geometry, NotchColumn.HeadLeft, NotchTop, NotchColumn.HeadWidth, NotchPoint - NotchTop, Color);

	// The line: from inside the notch to the bottom - solid, no glow or fade, so it reads the same over the ruler, the start
	// and end markers and every track.
	const float LineTop = SnapY(Geometry, (NotchTop + NotchPoint) * 0.5f);
	if (Bottom > LineTop)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			Geometry.ToPaintGeometry(FVector2f(NotchColumn.LineWidth, Bottom - LineTop), FSlateLayoutTransform(FVector2f(NotchColumn.LineLeft, LineTop))),
			WhiteBrush(), ESlateDrawEffect::None, Color.CopyWithNewOpacity(Color.A * Look.PlayheadLineOpacity));
	}

	// The time, centered in the tag.
	FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
		Geometry.ToPaintGeometry(FVector2f(TextSize.X + 2.f, TextSize.Y), FSlateLayoutTransform(FVector2f(TagX + (TagColumn.HeadWidth - TextSize.X) * 0.5f, TagY + 1.f))),
		TagText, TagFont, ESlateDrawEffect::None, Palette::PlayheadText);

	return LayerId + 2;
}

FString SWidgetAnimTimeline::FormatPlayheadTag(float Seconds) const
{
	if (IsPercentTime())
	{
		return FormatPercent(Seconds);
	}
	if (IsFrameTime())
	{
		return FormatFrames(Seconds, GetFrameRate());
	}
	return FString::Printf(TEXT("%.2f"), Seconds);
}

FString SWidgetAnimTimeline::FormatPercent(float Seconds)
{
	// Rounded to two places, then trailing zeros - and a point left with nothing after it - dropped: 25%, 12.5%, 33.33%.
	const double Percent = FMath::RoundHalfFromZero(static_cast<double>(Seconds) * 10000.0) / 100.0;
	FString Text = FString::Printf(TEXT("%.2f"), Percent);
	Text.TrimToNullTerminator();
	while (Text.EndsWith(TEXT("0")))
	{
		Text.LeftChopInline(1);
	}
	if (Text.EndsWith(TEXT(".")))
	{
		Text.LeftChopInline(1);
	}
	if (Text == TEXT("-0"))
	{
		Text = TEXT("0");
	}
	return Text + TEXT("%");
}

void SWidgetAnimTimeline::SetTimeUnit(EWidgetAnimTimeUnit Unit)
{
	if (!Source.IsValid() || !Source->CanSetTimeUnit() || Source->GetTimeUnit() == Unit || !CanEdit())
	{
		return;
	}

	const FScopedTransaction Transaction(Unit == EWidgetAnimTimeUnit::Percent
		? LOCTEXT("TimeInPercentTransaction", "Time In Percent")
		: LOCTEXT("TimeInSecondsTransaction", "Time In Seconds"));
	if (!BeginEdit())
	{
		return;
	}

	// Every track retimed at once, as the Details panel's Time In does - the playhead and view follow the new length.
	Source->SetTimeUnit(Unit);
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	ScrubToSeconds(0.f);
	RefreshDetails();
	RebuildRows();
	if (Tracks.IsValid())
	{
		Tracks->FitToAnimation();
	}
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

	// A Curve asset's keys belong to the asset, and every animation sharing it; they're edited there. An animation
	// track has none of its own.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	if (Layer.IsAnimationTrack())
	{
		return false;
	}
	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
	return WidgetAnimTypeIsKeyframed(Layer.AnimType) || (KeyIndex > 0 && KeyIndex < KeyTimes.Num() - 1);
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
	return WidgetAnimTypeIsKeyframed(Layer.AnimType)
		? KeyTimes.Num() > 2
		: (KeyIndex > 0 && KeyIndex < KeyTimes.Num() - 1);
}

bool SWidgetAnimTimeline::CanResizeLayer(int32 LayerIndex, bool bStartEdge) const
{
	if (!CanEditLayer(LayerIndex))
	{
		return false;
	}

	// An animation track is as long as the animation it plays: dragged along, never stretched - unless it plays its
	// animation stretched across itself, a percentage animation's always does. A collapsed track is as long as its tracks:
	// each is resized on its own, inside it.
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	if ((Layer.IsAnimationTrack() && !Layer.FitsAnimationToTrack()) || IsCollapsedHead(LayerIndex))
	{
		return false;
	}
	if (Layer.IsAnimationTrack())
	{
		return true;
	}

	// Stretching a Transform layer stretches its keys, and a shared Curve asset's keys aren't this animation's.
	if (WidgetAnimTypeIsKeyframed(Layer.AnimType) && Layer.Transform.Curve.ExternalCurve != nullptr)
	{
		return false;
	}

	// A Spring track is as long as its spring takes to settle - its spring decides that, not its edges. Not a
	// Transform's: its spring is fitted to its keys, which stretch with it as a Tween's do.
	if (Layer.IsSpringTimed() && !WidgetAnimTypeIsKeyframed(Layer.AnimType))
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

	// GetShownRunLength, from the layers already in hand: asked through it, every layer asked the source for them again.
	OutSpans.Reserve(Starts.Num());
	for (int32 Index = 0; Index < Starts.Num(); ++Index)
	{
		const float Run = TrackRuns.IsValidIndex(Index) && TrackRuns[Index] >= 0.f
			? TrackRuns[Index]
			: (Layers->IsValidIndex(Index) ? (*Layers)[Index].GetRunLength() : 0.f);
		OutSpans.Add(FVector2f(Starts[Index], Run) * TimeScale);
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
		if (!Expander || Layer.IsValueDriven() || (Expander->GathersParts() && !UWidgetAnimLibrary::TypeTakesParts(Layer.AnimType, Layer.TypeParams)))
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

bool SWidgetAnimTimeline::GetTrackDurationInfo(int32 LayerIndex, bool& bOutEditable, float& OutSeconds, FText& OutWhy) const
{
	bOutEditable = false;
	OutSeconds = 0.f;
	OutWhy = FText::GetEmpty();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];

	// What times the track instead of its Duration, if anything: an animation track stretched across it is timed by it, and
	// a value-driven track that tweens to its values eases over it, whatever its type.
	const bool bValueTween = !Layer.IsAnimationTrack() && Layer.IsValueDriven() && Layer.ValueSmoothing == EWidgetAnimValueSmoothing::Tween;
	if (bValueTween)
	{
		bOutEditable = true;
		OutSeconds = Layer.Duration;
		return true;
	}
	if (Layer.IsAnimationTrack() && !Layer.FitsAnimationToTrack())
	{
		OutWhy = WidgetAnimTips::AnimationTrackFitTooltip();
	}
	else if (!Layer.IsAnimationTrack() && WidgetAnimTypeIsKeyframed(Layer.AnimType))
	{
		OutWhy = WidgetAnimTips::DurationFromKeysTooltip();
	}
	else if (Layer.IsSpringTimed())
	{
		OutWhy = WidgetAnimTips::DurationFromSpringTooltip();
	}
	else if (HasDerivedLength(Layer))
	{
		const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
		const FText What = Evaluator ? Evaluator->GetDerivedLengthLabel(Layer.TypeParams) : FText::GetEmpty();
		OutWhy = FText::Format(WidgetAnimTips::DurationDerivedTooltip(), What.IsEmpty() ? LOCTEXT("DurationDerivedWhat", "settings") : What);
	}
	else
	{
		bOutEditable = true;
		OutSeconds = Layer.Duration;
		return true;
	}

	// How long it really runs, as the bars are drawn: worked out on the preview where it is - a sound's length - else its own.
	OutSeconds = GetShownRunLength(LayerIndex);
	if (!(TrackRuns.IsValidIndex(LayerIndex) && TrackRuns[LayerIndex] >= 0.f))
	{
		// No preview has measured it yet: a sound says how long it is without one.
		if (const FWidgetAnimSoundCueParams* Sound = Layer.AnimType == EWidgetAnimType::Custom ? Layer.TypeParams.GetPtr<FWidgetAnimSoundCueParams>() : nullptr)
		{
			const float SoundSeconds = Sound->bLengthFromSound ? FWidgetAnimSoundCueEvaluator::GetSoundLength(*Sound) : -1.f;
			if (SoundSeconds > 0.f)
			{
				OutSeconds = SoundSeconds;
			}
		}
	}
	return true;
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

	// The layers and groups read once, rather than through GetLayerGroup and IsLayerInView for every layer.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	FVector2f Range(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max());
	for (int32 LayerIndex = 0; Layers && LayerIndex < Spans.Num() && LayerIndex < Layers->Num(); ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const int32 LayerGroup = Groups ? UWidgetAnimLibrary::GetLayerGroupIndex(Layer, Groups->Num()) : INDEX_NONE;
		if (LayerGroup == GroupIndex && (!IsInCollapsedScope() || Layer.CollapsedTrack == ActiveCollapsed))
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

	// To where the last item of a stagger ends, too - GetLayerFanSeconds, with the time scale read once.
	const float TimeScale = GetTimeScale();
	float End = 0.f;
	for (int32 LayerIndex = 0; LayerIndex < Spans.Num(); ++LayerIndex)
	{
		const float Fan = TrackFans.IsValidIndex(LayerIndex) ? TrackFans[LayerIndex] * TimeScale : 0.f;
		End = FMath::Max(End, Spans[LayerIndex].X + Spans[LayerIndex].Y + Fan);
	}

	// Played within another animation, one pass runs until both have ended - the preview's pass is the same.
	for (const FVector2f& Span : ContextSpans)
	{
		End = FMath::Max(End, Span.X + Span.Y);
	}

	// And on past its last track to its Minimum Length, when the animation's end was pulled out there.
	if (Source.IsValid())
	{
		End = FMath::Max(End, Source->GetMinimumLength() * TimeScale);
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

	// An animation track's keys are its animation's tracks', not its own.
	if (Layer.IsAnimationTrack())
	{
		return;
	}

	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
	// but its own edges, and diamonds sitting on them would only get in the way of resizing it. Its menu's Add Key adds
	// its first key.
	// In the Graph Editor, its start and end are keys from the start - points on its curve, picked there and shown in the
	// Selection panel as keys, as any key is - since the Track Editor, where they would be in the way, isn't shown.
	const bool bGraphed = IsGraphOpen() && GraphLayers.Contains(LayerIndex);
	if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && (Layer.ValueKeys.Keys.Num() > 0 || bGraphed))
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
	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
	return WidgetAnimTypeIsKeyframed(Layer.AnimType)
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
	// between them wait for it to be a Tween again. So does a keyframed track eased across its whole track.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	return (WidgetAnimTypeIsKeyframed(Layer.AnimType) || WidgetAnimTypeSupportsValueKeys(Layer.AnimType)) && !Layer.IsSpringTimed() && !IsCueLayer(Layer)
		&& !(WidgetAnimTypeIsKeyframed(Layer.AnimType) && Layer.Transform.EasingSpan == EWidgetKeyEasingSpan::WholeTrack);
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

FString SWidgetAnimTimeline::GetGroupFolder(int32 GroupIndex) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups || !Groups->IsValidIndex(GroupIndex) || !Source.IsValid() || !Source->SupportsLayerGroups())
	{
		return FString();
	}
	return (*Groups)[GroupIndex].EditorFolder.TrimStartAndEnd();
}

void SWidgetAnimTimeline::GetGroupListOrder(TArray<int32>& OutGroups) const
{
	OutGroups.Reset();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups)
	{
		return;
	}

	// Each folder where its first group is, with every group in it gathered under it in order; the rest in place. Each
	// group's folder is read once (see GetGroupFolder) - read again for every group, hundreds of groups in folders cost the
	// square of that, every frame.
	const bool bFolders = Source.IsValid() && Source->SupportsLayerGroups();
	TArray<FString> Folders;
	Folders.SetNum(Groups->Num());
	TMap<FString, TArray<int32>> FolderGroups;
	for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
	{
		if (bFolders)
		{
			Folders[GroupIndex] = (*Groups)[GroupIndex].EditorFolder.TrimStartAndEnd();
		}
		if (!Folders[GroupIndex].IsEmpty())
		{
			FolderGroups.FindOrAdd(Folders[GroupIndex]).Add(GroupIndex);
		}
	}

	OutGroups.Reserve(Groups->Num());
	for (int32 GroupIndex = 0; GroupIndex < Groups->Num(); ++GroupIndex)
	{
		const FString& Folder = Folders[GroupIndex];
		if (Folder.IsEmpty())
		{
			OutGroups.Add(GroupIndex);
			continue;
		}
		if (const TArray<int32>* InFolder = FolderGroups.Find(Folder))
		{
			OutGroups.Append(*InFolder);
			FolderGroups.Remove(Folder);
		}
	}
}

void SWidgetAnimTimeline::GetFolders(TArray<FString>& OutFolders) const
{
	OutFolders.Reset();
	TArray<int32> Order;
	GetGroupListOrder(Order);
	for (const int32 GroupIndex : Order)
	{
		const FString Folder = GetGroupFolder(GroupIndex);
		if (!Folder.IsEmpty())
		{
			OutFolders.AddUnique(Folder);
		}
	}
}

void SWidgetAnimTimeline::GetFolderGroups(const FString& Folder, TArray<int32>& OutGroups) const
{
	OutGroups.Reset();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	for (int32 GroupIndex = 0; Groups && !Folder.IsEmpty() && GroupIndex < Groups->Num(); ++GroupIndex)
	{
		if (GetGroupFolder(GroupIndex) == Folder)
		{
			OutGroups.Add(GroupIndex);
		}
	}
}

void SWidgetAnimTimeline::ToggleListFolderCollapsed(const FString& Folder)
{
	if (ListCollapsedFolders.Contains(Folder))
	{
		ListCollapsedFolders.Remove(Folder);
	}
	else
	{
		ListCollapsedFolders.Add(Folder);
	}
	RebuildLayerList();
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

float SWidgetAnimTimeline::GetRowHeight() const
{
	return FMath::Clamp(FMath::RoundToFloat(UWidgetAnimEditorSettings::Get().DefaultTrackHeight), MinRowHeight, MaxRowHeight);
}

void SWidgetAnimTimeline::SetRowHeight(float NewHeight)
{
	NewHeight = FMath::Clamp(FMath::RoundToFloat(NewHeight), MinRowHeight, MaxRowHeight);
	if (FMath::IsNearlyEqual(NewHeight, GetRowHeight()))
	{
		return;
	}

	// The preference itself, so every open panel follows; written to disk once the drag ends - see SaveRowHeight.
	UWidgetAnimEditorSettings::GetMutable().DefaultTrackHeight = NewHeight;

	// The Track Editor reads it live; it just needs laying out again.
	if (Tracks.IsValid())
	{
		Tracks->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

void SWidgetAnimTimeline::SaveRowHeight()
{
	UWidgetAnimEditorSettings::GetMutable().SaveConfig();
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
	// The playhead moved by hand: Play plays forward again, and a backward play walked by the panel stops.
	bPlayBackwardNext = false;
	bPanelPlayingBackward = false;

	const float PassSeconds = GetPassSeconds();
	if (Source.IsValid())
	{
		Source->ScrubToAlpha(PassSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(Seconds / PassSeconds, 0.f, 1.f) : 0.f);
	}
}

void SWidgetAnimTimeline::TogglePlay()
{
	if (!Source.IsValid())
	{
		return;
	}

	// Playing backward, walked by the panel: stops where it is.
	if (bPanelPlayingBackward)
	{
		bPanelPlayingBackward = false;
		return;
	}

	// After Go to End, Play plays the animation from its end back to its start - by the source where it can, else walked
	// by the panel itself, a frame at a time. See OnRefreshTimer.
	if (bPlayBackwardNext && !Source->IsPlaying())
	{
		bPlayBackwardNext = false;
		if (!Source->PlayBackward())
		{
			const float Alpha = Source->GetPlayheadAlpha();
			PanelBackwardAlpha = Alpha > KINDA_SMALL_NUMBER ? Alpha : 1.f;
			bPanelPlayingBackward = true;
		}
		return;
	}

	bPlayBackwardNext = false;
	Source->TogglePlay();
}

bool SWidgetAnimTimeline::IsPlaying() const
{
	return bPanelPlayingBackward || (Source.IsValid() && Source->IsPlaying());
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
	SelectedGroups.Reset();
	if (Selection.Group != INDEX_NONE)
	{
		SelectedGroups.Add(Selection.Group);
	}
	GroupSelectionAnchor = Selection.Group;
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectGroups(const TArray<int32>& GroupIndices, int32 Primary)
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	TArray<int32> Valid;
	for (const int32 GroupIndex : GroupIndices)
	{
		if (Groups && Groups->IsValidIndex(GroupIndex))
		{
			Valid.AddUnique(GroupIndex);
		}
	}
	if (Valid.Num() == 0)
	{
		ClearSelection();
		return;
	}
	Valid.Sort();

	FSelection NewSelection;
	NewSelection.Group = Valid.Contains(Primary) ? Primary : Valid.Last();
	Selection = Validate(NewSelection);
	SelectedKeys.Reset();
	SelectedLayers.Reset();
	SelectionAnchor = INDEX_NONE;
	SelectedGroups = MoveTemp(Valid);
	if (!SelectedGroups.Contains(GroupSelectionAnchor))
	{
		GroupSelectionAnchor = Selection.Group;
	}
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectGroupWithModifiers(int32 GroupIndex, bool bRange, bool bToggle)
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups || !Groups->IsValidIndex(GroupIndex))
	{
		return;
	}

	if (!bRange && !bToggle)
	{
		SelectGroup(GroupIndex);
		return;
	}

	// As for tracks: Ctrl adds to the groups selected; Shift alone replaces them with the range, Ctrl + Shift adds it.
	const TArray<int32> Current = GetSelectedGroups();
	TArray<int32> NewGroups = bToggle ? Current : TArray<int32>();
	const int32 Anchor = Current.Contains(GroupSelectionAnchor) ? GroupSelectionAnchor : (Current.Num() > 0 ? Current.Last() : INDEX_NONE);

	if (bRange)
	{
		// From the last one clicked to this one, in the order the list shows them - folders gathered.
		TArray<int32> Shown;
		GetGroupListOrder(Shown);
		const int32 To = Shown.IndexOfByKey(GroupIndex);
		const int32 From = Anchor != INDEX_NONE && Shown.Contains(Anchor) ? Shown.IndexOfByKey(Anchor) : To;
		if (To == INDEX_NONE)
		{
			NewGroups.AddUnique(GroupIndex);
		}
		else
		{
			for (int32 Index = FMath::Min(From, To); Index <= FMath::Max(From, To); ++Index)
			{
				NewGroups.AddUnique(Shown[Index]);
			}
		}

		// The anchor stays put, so the next Shift-click ranges from the same place.
		GroupSelectionAnchor = Anchor != INDEX_NONE ? Anchor : GroupIndex;
	}
	else
	{
		if (NewGroups.Contains(GroupIndex))
		{
			NewGroups.Remove(GroupIndex);
		}
		else
		{
			NewGroups.Add(GroupIndex);
		}
		GroupSelectionAnchor = GroupIndex;
	}

	const int32 Anchored = GroupSelectionAnchor;
	SelectGroups(NewGroups, NewGroups.Contains(GroupIndex) ? GroupIndex : INDEX_NONE);
	if (SelectedGroups.Contains(Anchored))
	{
		GroupSelectionAnchor = Anchored;
	}
}

bool SWidgetAnimTimeline::IsGroupSelected(int32 GroupIndex) const
{
	// Only while the selection is groups: a track or key selected since leaves what was there unread.
	if (GroupIndex == INDEX_NONE || Selection.Layer != INDEX_NONE || Selection.Group == INDEX_NONE)
	{
		return false;
	}
	return GroupIndex == Selection.Group || SelectedGroups.Contains(GroupIndex);
}

TArray<int32> SWidgetAnimTimeline::GetSelectedGroups() const
{
	TArray<int32> Out;
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!Groups || Selection.Layer != INDEX_NONE || !Groups->IsValidIndex(Selection.Group))
	{
		return Out;
	}

	Out.Add(Selection.Group);
	for (const int32 GroupIndex : SelectedGroups)
	{
		if (Groups->IsValidIndex(GroupIndex))
		{
			Out.AddUnique(GroupIndex);
		}
	}
	Out.Sort();
	return Out;
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

void SWidgetAnimTimeline::GetTrackEditorOrder(TArray<int32>& OutOrder) const
{
	OutOrder.Reset();
	for (const FTrackLine& Line : TrackLines)
	{
		if (Line.Kind != FTrackLine::EKind::Lane)
		{
			continue;
		}
		for (const int32 LayerIndex : Line.Layers)
		{
			// A collapsed track is every one of its tracks.
			if (IsCollapsedHeadShown(LayerIndex))
			{
				TArray<int32> Members;
				GetCollapsedMembers(GetLayerCollapsedName(LayerIndex), Members);
				for (const int32 Member : Members)
				{
					OutOrder.AddUnique(Member);
				}
			}
			else
			{
				OutOrder.AddUnique(LayerIndex);
			}
		}
	}
}

void SWidgetAnimTimeline::SelectLayerWithModifiers(int32 LayerIndex, bool bRange, bool bToggle, const TArray<int32>* RangeOrder)
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
		// group, each in the order it plays - or, clicked in the Track Editor, in its order, top to bottom.
		TArray<int32> Shown;
		if (RangeOrder)
		{
			Shown = *RangeOrder;
		}
		else
		{
			GetListOrder(Shown);
		}

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
		// Inside a collapsed track, every one of its tracks - the ones shown.
		if (IsLayerInView(Index))
		{
			SelectedLayers.Add(Index);
		}
	}
	if (SelectedLayers.Num() == 0)
	{
		return;
	}

	FSelection NewSelection;
	NewSelection.Layer = SelectedLayers.Contains(Selection.Layer) ? Selection.Layer : SelectedLayers[0];
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
		// A collapsed track taken in is every one of its tracks.
		TArray<int32> Taken;
		for (const int32 LayerIndex : Layers)
		{
			if (IsCollapsedHead(LayerIndex))
			{
				TArray<int32> Members;
				GetCollapsedMembers(GetLayerCollapsedName(LayerIndex), Members);
				for (const int32 Member : Members)
				{
					Taken.AddUnique(Member);
				}
			}
			else
			{
				Taken.AddUnique(LayerIndex);
			}
		}

		TArray<int32> NewLayers = (bKeep && SelectedKeys.Num() == 0) ? SelectedLayers : TArray<int32>();
		for (const int32 LayerIndex : Taken)
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

		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
	SelectedGroups.Reset();
	GroupSelectionAnchor = INDEX_NONE;
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

	// Asked by every field of the Selection panel, every frame - so answered once a frame: each asking walked the whole
	// selection, hundreds of tracks of it with a big collapsed track picked.
	if (EditableAnswerFrame == GFrameCounter && EditableAnswerCount == SelectedLayers.Num())
	{
		return bEditableAnswer;
	}

	bool bEditable = true;
	for (const int32 LayerIndex : SelectedLayers)
	{
		if (IsLayerLocked(LayerIndex))
		{
			bEditable = false;
			break;
		}
	}
	EditableAnswerFrame = GFrameCounter;
	EditableAnswerCount = SelectedLayers.Num();
	bEditableAnswer = bEditable;
	return bEditable;
}

void SWidgetAnimTimeline::RefreshDetails()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimPanelRefreshDetails);
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

	// An animation asset is shown through its stand-in - its settings, its groups, and only the selected track - never
	// whole. See UWidgetAnimPresetSelectionShadow.
	UObject* Shown = Edited;
	if (UObject* Shadow = UpdateSelectionShadow())
	{
		Shown = Shadow;
	}

	if (DetailsObject.Get() != Shown)
	{
		// Any other object - one holding a plain list of tracks - is laid out by the same customization. Registered
		// for its class before it is bound; a builder and an asset (and the asset's stand-in, one) have theirs from the start.
		if (Edited && Shown == Edited && !Edited->IsA<UWidgetAnimBuilder>() && !Edited->IsA<UWidgetAnimPresetAsset>())
		{
			DetailsView->RegisterInstancedCustomPropertyLayout(Edited->GetClass(), SelectionLayout);
		}

		// Binding a new object lays the view out from scratch anyway.
		DetailsObject = Shown;
		DetailsView->SetObject(Shown, /*bForceRefresh=*/true);
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

	// Past a few dozen, one proxy stands in for them all - the primary track's, an edit to it copied onto every one. One
	// proxy each, shown together, makes the details view read every field of every one of them every frame to tell
	// whether they agree, and copy every one into each edit's undo step: a big collapsed track picked whole made the
	// panel crawl for as long as it stayed selected.
	constexpr int32 MaxProxiedLayers = 64;
	if (SelectedLayers.Num() > MaxProxiedLayers)
	{
		TArray<int32> Represented;
		Represented.Reserve(SelectedLayers.Num());
		for (const int32 LayerIndex : SelectedLayers)
		{
			if (Layers->IsValidIndex(LayerIndex))
			{
				Represented.Add(LayerIndex);
			}
		}
		if (Represented.Num() > 0)
		{
			const int32 Primary = Represented.Contains(Selection.Layer) ? Selection.Layer : Represented[0];
			UWidgetAnimLayerEditProxy* Proxy = NewObject<UWidgetAnimLayerEditProxy>(GetTransientPackage(), NAME_None, RF_Transient);
			Proxy->Layer = (*Layers)[Primary];
			Proxy->LayerIndex = Primary;
			Proxy->RepresentedLayers = MoveTemp(Represented);
			Proxy->Panel = WeakThisPtr;
			LayerProxies.Emplace(Proxy);
		}
		return;
	}

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

	// Each compare is a whole track's, property by property. With hundreds of tracks selected - a big collapsed track,
	// picked whole - comparing every one every frame cost more than the rest of the panel together, so past a few dozen
	// they are compared a few dozen a frame, in turn: each is caught up within a few frames, and anything that changes
	// which tracks are selected, or what they are, makes them afresh (RefreshDetails).
	constexpr int32 MaxSyncedPerFrame = 48;
	const int32 NumProxies = LayerProxies.Num();
	const int32 NumToSync = FMath::Min(NumProxies, MaxSyncedPerFrame);
	if (NextProxyToSync >= NumProxies)
	{
		NextProxyToSync = 0;
	}

	const UScriptStruct* LayerStruct = FWidgetAnimLayer::StaticStruct();
	for (int32 Step = 0; Step < NumToSync; ++Step)
	{
		const TStrongObjectPtr<UWidgetAnimLayerEditProxy>& Proxy = LayerProxies[(NextProxyToSync + Step) % NumProxies];
		if (Proxy.IsValid() && Layers->IsValidIndex(Proxy->LayerIndex)
			&& !LayerStruct->CompareScriptStruct(&Proxy->Layer, &(*Layers)[Proxy->LayerIndex], PPF_None))
		{
			Proxy->Layer = (*Layers)[Proxy->LayerIndex];
		}
	}
	NextProxyToSync = NumProxies > 0 ? (NextProxyToSync + NumToSync) % NumProxies : 0;
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

			// Into a nested struct, when the edit was somewhere inside it - but never into an instanced struct (Type Params,
			// Stagger, Value Input): what it holds lives behind a pointer, not inside it, so the properties under it can't be
			// reached by offset from it. Walked into, the edit was copied to and from the wrong memory - and never landed, the
			// field snapping back to what the track still held. The instanced struct is copied whole instead.
			if (Next)
			{
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				if (StructProperty && StructProperty->Struct != FInstancedStruct::StaticStruct())
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

	/**
	 * After an edit: has a registered type bring what its settings derive from something else up to date - a Particles
	 * track's User Parameters, from the Niagara System just picked (FWidgetAnimEvaluator::RefreshParams).
	 */
	void RefreshLayerParams(FWidgetAnimLayer& Layer)
	{
		if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
		{
			Evaluator->RefreshParams(Layer.TypeParams);
		}
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

	// Standing in for a whole large selection: the one property copied onto every other track it stands for, as one proxy
	// each would have copied it. A change that can't be followed to one property goes onto its own track alone - copied
	// whole onto the rest, it would give them all its timing and targets.
	for (const int32 Represented : Proxy->RepresentedLayers)
	{
		if (Represented != Proxy->LayerIndex && Layers->IsValidIndex(Represented) && CanEditLayer(Represented))
		{
			CopyEditedLayerProperty(PropertyChangedEvent.PropertyChain, Proxy->Layer, (*Layers)[Represented]);
			RefreshLayerParams((*Layers)[Represented]);
		}
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

	// What the edit derives - the track and what the Details panel shows of it alike, so the next edit carries it whole.
	RefreshLayerParams(Layer);
	RefreshLayerParams(Proxy->Layer);

	// Every selected proxy reports the same edit; the source is told once, on the next frame. A drag in
	// progress stays Interactive unless something in the batch committed.
	const EPropertyChangeType::Type ChangeType = PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive
		? EPropertyChangeType::Interactive
		: EPropertyChangeType::ValueSet;
	PendingProxyNotify = (PendingProxyNotify.IsSet() && PendingProxyNotify.GetValue() != EPropertyChangeType::Interactive)
		? PendingProxyNotify.GetValue()
		: ChangeType;
}

namespace WidgetAnimSelectionShadow
{
	/** The asset's own settings and groups, as UPROPERTYs of its class - everything but Layers, which the stand-in holds one of. */
	template<typename VisitType>
	void ForEachSetting(VisitType&& Visit)
	{
		const FName LayersName = GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Layers);
		for (TFieldIterator<FProperty> It(UWidgetAnimPresetAsset::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->GetFName() != LayersName)
			{
				Visit(*It);
			}
		}
	}

	/** Copies from Asset onto Shadow every setting that differs. */
	void CopySettings(const UWidgetAnimPresetAsset* Asset, UWidgetAnimPresetAsset* Shadow)
	{
		ForEachSetting([Asset, Shadow](const FProperty* Property)
		{
			if (!Property->Identical_InContainer(Shadow, Asset))
			{
				Property->CopyCompleteValue_InContainer(Shadow, Asset);
			}
		});
	}
}

UObject* SWidgetAnimTimeline::UpdateSelectionShadow()
{
	UWidgetAnimPresetAsset* Asset = Source.IsValid() ? Cast<UWidgetAnimPresetAsset>(Source->GetEditedObject()) : nullptr;
	if (!Asset || Source->GetLayers() != &Asset->Layers || Source->GetGroups() != &Asset->LayerGroups)
	{
		return nullptr;
	}

	if (!SelectionShadow.IsValid())
	{
		SelectionShadow.Reset(NewObject<UWidgetAnimPresetSelectionShadow>(GetTransientPackage(), NAME_None, RF_Transient));
		SelectionShadow->Panel = SharedThis(this);
	}

	UWidgetAnimPresetSelectionShadow* Shadow = SelectionShadow.Get();
	Shadow->ShadowOf = Asset;
	Shadow->LayerIndex = Asset->Layers.IsValidIndex(Selection.Layer) ? Selection.Layer : INDEX_NONE;
	Shadow->Layers.Reset();
	if (Shadow->LayerIndex != INDEX_NONE)
	{
		Shadow->Layers.Add(Asset->Layers[Shadow->LayerIndex]);
	}
	WidgetAnimSelectionShadow::CopySettings(Asset, Shadow);
	return Shadow;
}

void SWidgetAnimTimeline::SyncSelectionShadow()
{
	UWidgetAnimPresetSelectionShadow* Shadow = SelectionShadow.Get();
	if (!Shadow || DetailsObject.Get() != Shadow)
	{
		return;
	}
	const UWidgetAnimPresetAsset* Asset = Shadow->ShadowOf.Get();
	if (!Asset)
	{
		return;
	}

	// The settings and groups - a handful of properties - and the one track: cheap enough for every frame.
	WidgetAnimSelectionShadow::CopySettings(Asset, Shadow);
	if (Shadow->LayerIndex != INDEX_NONE && Shadow->Layers.Num() == 1 && Asset->Layers.IsValidIndex(Shadow->LayerIndex)
		&& !FWidgetAnimLayer::StaticStruct()->CompareScriptStruct(&Shadow->Layers[0], &Asset->Layers[Shadow->LayerIndex], PPF_None))
	{
		Shadow->Layers[0] = Asset->Layers[Shadow->LayerIndex];
	}
}

int32 SWidgetAnimTimeline::GetDetailsLayerIndex() const
{
	const UWidgetAnimPresetSelectionShadow* Shadow = SelectionShadow.Get();
	return Shadow && DetailsObject.Get() == Shadow && Shadow->LayerIndex == Selection.Layer ? 0 : Selection.Layer;
}

void SWidgetAnimTimeline::OnShadowPreEdit(UWidgetAnimPresetSelectionShadow* Shadow, FEditPropertyChain& PropertyAboutToChange)
{
	UWidgetAnimPresetAsset* Asset = Shadow ? Shadow->ShadowOf.Get() : nullptr;
	if (!Asset || !Source.IsValid() || Source->GetEditedObject() != Asset)
	{
		return;
	}

	// Recorded in the transaction the details view already has open for this edit - and readied as an edit to it would
	// ready it: its Time In remembered, for one.
	if (BeginEdit())
	{
		static_cast<UObject*>(Asset)->PreEditChange(PropertyAboutToChange);
	}
}

void SWidgetAnimTimeline::OnShadowEdited(UWidgetAnimPresetSelectionShadow* Shadow, FPropertyChangedChainEvent& PropertyChangedEvent)
{
	UWidgetAnimPresetAsset* Asset = Shadow ? Shadow->ShadowOf.Get() : nullptr;
	if (!Asset || !Source.IsValid() || Source->GetEditedObject() != Asset || !BeginEdit())
	{
		return;
	}

	const FEditPropertyChain::TDoubleLinkedListNode* MemberNode = PropertyChangedEvent.PropertyChain.GetActiveMemberNode();
	const FProperty* Member = MemberNode ? MemberNode->GetValue() : PropertyChangedEvent.MemberProperty;
	if (!Member)
	{
		return;
	}

	// Which element of each array the edit was in, as the edit had them - the track's put at its real place below.
	TArray<TMap<FString, int32>> ArrayIndices;
	TMap<FString, int32>& Indices = ArrayIndices.AddDefaulted_GetRef();
	for (const FEditPropertyChain::TDoubleLinkedListNode* Node = PropertyChangedEvent.PropertyChain.GetHead(); Node; Node = Node->GetNextNode())
	{
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Node->GetValue()))
		{
			const int32 Index = PropertyChangedEvent.GetArrayIndex(ArrayProperty->GetName());
			if (Index != INDEX_NONE)
			{
				Indices.Add(ArrayProperty->GetName(), Index);
			}
		}
	}

	if (Member->GetFName() == GET_MEMBER_NAME_CHECKED(UWidgetAnimPresetAsset, Layers))
	{
		// The shown track's edit, onto the real track: the one property, as a proxy's is.
		const int32 LayerIndex = Shadow->LayerIndex;
		if (!Shadow->Layers.IsValidIndex(0) || !Asset->Layers.IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
		{
			return;
		}

		FWidgetAnimLayer& Layer = Asset->Layers[LayerIndex];
		if (!CopyEditedLayerProperty(PropertyChangedEvent.PropertyChain, Shadow->Layers[0], Layer))
		{
			// Nothing to follow: the whole track, but for what only the panel sets.
			const int32 LayerGroup = Layer.LayerGroup;
			const bool bLocked = Layer.bLockedInEditor;
			const int32 EditorLine = Layer.EditorLine;
			const bool bHidden = Layer.bHiddenInEditor;
			const bool bHiddenByFocus = Layer.bHiddenByFocus;
			const FString InsertedFrom = Layer.InsertedFrom;
			Layer = Shadow->Layers[0];
			Layer.LayerGroup = LayerGroup;
			Layer.bLockedInEditor = bLocked;
			Layer.EditorLine = EditorLine;
			Layer.bHiddenInEditor = bHidden;
			Layer.bHiddenByFocus = bHiddenByFocus;
			Layer.InsertedFrom = InsertedFrom;
		}
		RefreshLayerParams(Layer);
		Indices.Add(Member->GetName(), LayerIndex);
	}
	else
	{
		// One of the asset's own settings, or its groups: what the stand-in holds of them is all of them.
		Member->CopyCompleteValue_InContainer(Asset, Shadow);
	}

	// Mid-drag - a slider, a spin box - only the preview follows, and the Track Editor: the full tell, to the asset and
	// through it to everything listening, waits for the drag to end, which sends one more edit, not Interactive. Told on every
	// step, it made dragging a value stutter.
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		++TrackLayoutRevision;
		Source->NotifyInteractiveEdit();
		return;
	}

	// Told as a Details edit to it would tell it - at the real track's place - so it does all it does for one.
	FPropertyChangedChainEvent AssetEvent(PropertyChangedEvent.PropertyChain, PropertyChangedEvent);
	AssetEvent.ObjectIteratorIndex = 0;
	AssetEvent.SetArrayIndexPerObject(ArrayIndices);
	Asset->PostEditChangeChainProperty(AssetEvent);
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

	const FText TypeName = GetLayerDisplayName((*Layers)[Selection.Layer]);

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

	// Which animation tracks are open, and which of their groups folded, decide the list's rows.
	// Summed, so the order a set happens to hold them in doesn't count.
	uint32 FoldHash = 0;
	for (const int32 Expanded : ExpandedAnimationTracks)
	{
		FoldHash += HashCombine(GetTypeHash(Expanded), 0x9E3779B9u);
	}
	for (const uint64 Folded : FoldedNestedGroups)
	{
		FoldHash += HashCombine(GetTypeHash(Folded), 0x85EBCA6Bu);
	}
	Hash = HashCombine(Hash, FoldHash);

	// Which collapsed track is open on its own, and which are opened in the list, decide the rows too.
	Hash = HashCombine(Hash, GetTypeHash(ActiveCollapsed));
	uint32 OpenHash = 0;
	for (const FName& Open : ListOpenCollapsed)
	{
		OpenHash += HashCombine(GetTypeHash(Open), 0xC2B2AE35u);
	}
	Hash = HashCombine(Hash, OpenHash);

	// A group's widget decides whether its tracks have anything to stagger over.
	for (const FWidgetAnimLayerGroup& Group : *Groups)
	{
		Hash = HashCombine(Hash, GetTypeHash(Group.PreviewTarget));
	}

	Hash = HashCombine(Hash, GetTypeHash(Layers->Num()));

	// Seconds or Percent decides how every time in the Selection panel is shown.
	Hash = HashCombine(Hash, GetTypeHash(IsPercentTime()));

	for (const FWidgetAnimLayer& Layer : *Layers)
	{
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.AnimType)));
		Hash = HashCombine(Hash, GetTypeHash(Layer.LayerGroup));

#if WITH_EDITORONLY_DATA
		// Folded into a collapsed track, and pinned: which rows and lines it has, and where.
		Hash = HashCombine(Hash, GetTypeHash(Layer.CollapsedTrack));
		Hash = HashCombine(Hash, GetTypeHash(Layer.bPinnedInEditor));
#endif

		// A lock changes what the row offers - its grip, its delete button - and whether its fields are editable.
		Hash = HashCombine(Hash, GetTypeHash(Layer.bLockedInEditor));
		Hash = HashCombine(Hash, GetTypeHash(Layer.bHiddenInEditor));

		// Timing decides which rows a layer has - Duration and Easing, or a spring - and how many keys a keyed
		// layer has decides whether a selected one still exists.
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.Timing)));
		Hash = HashCombine(Hash, GetTypeHash(Layer.ValueKeys.Keys.Num()));

		// Which Stagger it has decides which of the stagger's own settings are shown.
		Hash = HashCombine(Hash, PointerHash(Layer.Expander.GetScriptStruct()));

		// A registered type's settings decide its Track Values - and a Blueprint type's or cue's Settings Struct, theirs.
		if (Layer.AnimType == EWidgetAnimType::Custom)
		{
			Hash = HashCombine(Hash, PointerHash(Layer.TypeParams.GetScriptStruct()));
			if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
			{
				// Rows a type lays out from an asset - a Particles track's User Parameters.
				Hash = HashCombine(Hash, Evaluator->GetParamsLayoutHash(Layer.TypeParams));
			}
			if (const FWidgetAnimBlueprintTypeParams* BlueprintType = Layer.TypeParams.GetPtr<FWidgetAnimBlueprintTypeParams>())
			{
				Hash = HashCombine(Hash, PointerHash(BlueprintType->Settings.GetScriptStruct()));
			}
			else if (const FWidgetAnimBlueprintCueParams* BlueprintCue = Layer.TypeParams.GetPtr<FWidgetAnimBlueprintCueParams>())
			{
				Hash = HashCombine(Hash, PointerHash(BlueprintCue->Settings.GetScriptStruct()));
			}
		}

		// An animation track shows its animation's groups and tracks under it - which, and how many, decide those rows.
		if (Layer.IsAnimationTrack())
		{
			Hash = HashCombine(Hash, PointerHash(Layer.InsertedAnimation.Get()));
			Hash = HashCombine(Hash, GetTypeHash(Layer.InsertedAnimation->Layers.Num()));
			Hash = HashCombine(Hash, GetTypeHash(Layer.InsertedAnimation->LayerGroups.Num()));

			// Stretched across the track or not decides whether it has a length of its own to show.
			Hash = HashCombine(Hash, GetTypeHash(Layer.FitsAnimationToTrack()));
		}

		// What a value-driven Transform drives decides which Value Types it offers, and its Value Type which of
		// those it can drive.
		if (Layer.IsValueDriven())
		{
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.ValueDrives)));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.ValueOptions.Kind)));

			// Which Value Input it has decides which of the input's own settings are shown.
			Hash = HashCombine(Hash, PointerHash(Layer.ValueInput.GetScriptStruct()));
		}

		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
		{
			// Key count matters (a selected key can vanish); a Shape track's kind decides which fields its keys have,
			// and whether they've been laid out for it yet.
			Hash = HashCombine(Hash, GetTypeHash(Layer.Transform.Keys.Num()));
			Hash = HashCombine(Hash, PointerHash(Layer.Transform.ShapeKind.Get()));
			Hash = HashCombine(Hash, PointerHash(Layer.Transform.DesignKind.Get()));

			// Each key's Use Authored decides whether its own fields are shown - see CustomizeKey.
			uint32 AuthoredBits = 0;
			for (int32 KeyIndex = 0; KeyIndex < Layer.Transform.Keys.Num(); ++KeyIndex)
			{
				const FWidgetTransformKeyframe& Key = Layer.Transform.Keys[KeyIndex];
				AuthoredBits = HashCombine(AuthoredBits, GetTypeHash((Key.bUseAuthoredShape ? 1u : 0u) | (Key.bUseAuthoredTransform ? 2u : 0u)));
			}
			Hash = HashCombine(Hash, AuthoredBits);
			// Easing Across decides whether each key shows its Curve To Next Key.
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Layer.Transform.EasingSpan)));
			Hash = HashCombine(Hash, PointerHash(Layer.Transform.Keys.Num() > 0 ? Layer.Transform.Keys[0].ShapeValues.GetPropertyBagStruct() : nullptr));
			Hash = HashCombine(Hash, PointerHash(Layer.Transform.Curve.ExternalCurve.Get()));
		}
	}
	return Hash;
}

EActiveTimerReturnType SWidgetAnimTimeline::OnRefreshTimer(double InCurrentTime, float InDeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimPanelRefresh);
	// Tells a source that plays the preview itself that it is wanted; it stops, and puts everything back
	// as authored, soon after this stops being called.
	if (Source.IsValid() && !IsBodyCollapsed())
	{
		Source->NotifyPanelActive();
	}

	// A collapsed track open on its own that is gone - undone, or expanded elsewhere - closes; while one is open, a track
	// added from anywhere else is left out of the previews with the rest.
	if (IsInCollapsedScope())
	{
		TArray<int32> Members;
		GetCollapsedMembers(ActiveCollapsed, Members);
		if (Members.Num() == 0)
		{
			ExitCollapsed();
		}
	}
	// The Graph Editor keeps to tracks there are, and follows the tracks picked in the list.
	if (IsGraphOpen())
	{
		SyncGraphLayers();
	}
	if (IsInCollapsedScope() || IsGraphOpen() || bScopeMutesApplied)
	{
		SyncScopeMutes();
	}

	// A Preview Target being picked in the designer: the widget clicked there.
	UpdateTargetPick();

	// Worked in - hovered, or typed in - so a script run from the console edits this panel's animation.
	if (IsHovered() || HasFocusedDescendants())
	{
		WidgetAnimScripting::NotePanelUsed(SharedThis(this));
	}

	// Playing backward from the end, walked by the panel for a source that can't: a frame's worth further back each tick,
	// at the preview's speed, stopping at the start.
	if (bPanelPlayingBackward && Source.IsValid())
	{
		const float PassSeconds = GetPassSeconds();
		PanelBackwardAlpha -= PassSeconds > KINDA_SMALL_NUMBER ? InDeltaTime * FMath::Max(Source->GetSpeed(), 0.01f) / PassSeconds : 1.f;
		if (PanelBackwardAlpha <= 0.f)
		{
			PanelBackwardAlpha = 0.f;
			bPanelPlayingBackward = false;
		}
		Source->ScrubToAlpha(PanelBackwardAlpha);
	}

	// Edits made through the multi-selection proxies, told to the source once for every proxy that made them.
	if (PendingProxyNotify.IsSet())
	{
		const EPropertyChangeType::Type ChangeType = PendingProxyNotify.GetValue();
		PendingProxyNotify.Reset();

		// Mid-drag, only the preview follows - see IWidgetAnimPanelSource::NotifyInteractiveEdit.
		if (ChangeType == EPropertyChangeType::Interactive && Source.IsValid())
		{
			++TrackLayoutRevision;
			Source->NotifyInteractiveEdit();
		}
		else
		{
			NotifyGroupsChanged(ChangeType);
		}
	}

	// The proxies follow the real layers - a drag on the tracks, an undo - so what the Selection panel shows for
	// several layers is never stale.
	if (IsMultiSelection())
	{
		SyncLayerProxies();
	}

	// A Shape track keys the settings of the shape it targets: retargeted - its group's Preview Target changed, or the
	// track made a Shape track - it follows, so the Selection panel always shows that shape's fields.
	// Tried once for each track and target it is seen with, never while an undo or redo is applied: undoing the match
	// leaves it undone until the track is retargeted.
	if (Selection.Layer != INDEX_NONE && !IsMultiSelection() && !GIsTransacting)
	{
		const TArray<FWidgetAnimLayer>* Layers = GetLayers();
		const bool bShapeTrack = Layers && Layers->IsValidIndex(Selection.Layer) && (*Layers)[Selection.Layer].AnimType == EWidgetAnimType::Shape;
		UShapeWidget* TargetShape = nullptr;
		const bool bShapeTarget = bShapeTrack && GetShapeTrackTarget(Selection.Layer, &TargetShape) == EShapeTrackTarget::Shape;
		const TPair<int32, FObjectKey> MatchKey(Selection.Layer, FObjectKey(bShapeTarget ? TargetShape : nullptr));
		if (bShapeTrack && MatchKey != LastShapeMatch)
		{
			LastShapeMatch = MatchKey;
			MatchShapeTrackToTarget(Selection.Layer);
		}
		else if (!bShapeTrack)
		{
			LastShapeMatch = TPair<int32, FObjectKey>(INDEX_NONE, FObjectKey());
		}

		// A Design track likewise keys the look of the widget it plays on: retargeted, it follows that widget's kind. Tried
		// once per kind of widget, not per preview instance: the designer rebuilds its preview after every edit, and matching
		// again on each new one went round and round with the designer and its Details.
		const bool bDesignTrack = Layers && Layers->IsValidIndex(Selection.Layer) && (*Layers)[Selection.Layer].AnimType == EWidgetAnimType::Design;
		const UWidget* DesignTarget = bDesignTrack ? Source->GetLayerPreviewWidget(Selection.Layer) : nullptr;
		const TPair<int32, FObjectKey> DesignKey(Selection.Layer, FObjectKey(DesignTarget ? DesignTarget->GetClass() : nullptr));
		if (bDesignTrack && DesignKey != LastDesignMatch)
		{
			LastDesignMatch = DesignKey;
			MatchDesignTrackToTarget(Selection.Layer);
		}
		else if (!bDesignTrack)
		{
			LastDesignMatch = TPair<int32, FObjectKey>(INDEX_NONE, FObjectKey());
		}

		// A Blueprint type's or cue's Settings follow its Settings Struct - set on the type, never picked on the track.
		SyncBlueprintTypeSettings(Selection.Layer);
	}

	// The Selection panel's stand-in for the asset, likewise.
	SyncSelectionShadow();

	// The Track Editor's lines follow every edit, wherever it was made - worked out again whenever one may have changed
	// them. See IsTrackLayoutDue.
	if (IsTrackLayoutDue())
	{
		UpdateTrackLayout();
	}

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
		// Another animation altogether - a timeline opened, another asset picked: nothing selected carries over. Kept by
		// position, a selection of hundreds of tracks went on selecting whatever sat at those places in the new one, and
		// the Selection panel made a proxy for each of them before anything was shown. Only between animation assets: a
		// builder's compile hands the panel new Class Defaults of the same animation, and keeps what is selected.
		UObject* Edited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
		UObject* Previous = LastEditedObject.Get();
		LastEditedObject = Edited;
		if (Edited != Previous && Cast<UWidgetAnimPresetAsset>(Edited) && Cast<UWidgetAnimPresetAsset>(Previous))
		{
			Selection = FSelection();
			SelectedKeys.Reset();
			SelectedLayers.Reset();
			SelectionAnchor = INDEX_NONE;
			SelectedGroups.Reset();
			GroupSelectionAnchor = INDEX_NONE;
		}

		Selection = Validate(Selection);
		RebuildRows();
		RefreshDetails();
		bListCheckPending = false;
	}
	else if (bListCheckPending)
	{
		// Asked once each time the lines are worked out again - the order comes from the same spans - rather than every
		// frame, which sorted every group's tracks for nothing.
		bListCheckPending = false;
		if (ComputeListSignature(LayoutIndex, TrackSpans) != ListSignature)
		{
			// Only the order things play in changed - a Delay typed in the Selection panel, say. The list follows;
			// the Selection panel is left alone, since that may be exactly where the typing is going on.
			RebuildLayerList();
		}
	}

	return EActiveTimerReturnType::Continue;
}

bool SWidgetAnimTimeline::IsTrackLayoutDue() const
{
	// After a drag, once - its tracks give way to the rest only while it is on; a drag lays its own steps out as it makes
	// them. A quarter of a second at most otherwise, for what is read off the preview without telling anyone: how many
	// items a stagger fans out to, how long a text takes to type.
	static constexpr double TrackLayoutRefreshSeconds = 0.25;
	return (bLaidOutWhileDragging && !IsDragging())
		|| TrackLayoutRevision != LaidOutRevision
		|| !IsTrackLayoutCurrent()
		|| FPlatformTime::Seconds() - LastTrackLayoutTime >= TrackLayoutRefreshSeconds
		|| ComputeTrackViewHash() != LaidOutViewHash;
}

uint32 SWidgetAnimTimeline::ComputeTrackViewHash() const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const TArray<FWidgetAnimLayer>* ContextLayers = GetContextLayers();

	uint32 Hash = PointerHash(Layers);
	Hash = HashCombine(Hash, GetTypeHash(Layers ? Layers->Num() : INDEX_NONE));
	Hash = HashCombine(Hash, PointerHash(Groups));
	Hash = HashCombine(Hash, GetTypeHash(Groups ? Groups->Num() : INDEX_NONE));
	Hash = HashCombine(Hash, PointerHash(ContextLayers));
	Hash = HashCombine(Hash, GetTypeHash(ContextLayers ? ContextLayers->Num() : INDEX_NONE));
	Hash = HashCombine(Hash, GetTypeHash(ActiveCollapsed));
	Hash = HashCombine(Hash, GetTypeHash(bCompact));
	Hash = HashCombine(Hash, GetTypeHash(IsPercentTime()));
	Hash = HashCombine(Hash, GetTypeHash(GetTimeScale()));

	// Folded groups, open animation tracks and their folded groups. Summed, so the order a set happens to hold them in
	// doesn't count.
	uint32 FoldHash = 0;
	for (const int32 Folded : TrackCollapsedGroups)
	{
		FoldHash += HashCombine(GetTypeHash(Folded), 0x9E3779B9u);
	}
	for (const int32 Expanded : ExpandedAnimationTracks)
	{
		FoldHash += HashCombine(GetTypeHash(Expanded), 0x85EBCA6Bu);
	}
	for (const uint64 Folded : FoldedNestedGroups)
	{
		FoldHash += HashCombine(GetTypeHash(Folded), 0xC2B2AE35u);
	}
	Hash = HashCombine(Hash, FoldHash);
	Hash = HashCombine(Hash, GetTypeHash(TrackCollapsedGroups.Num() + ExpandedAnimationTracks.Num() * 7 + FoldedNestedGroups.Num() * 31));
	return Hash;
}

void SWidgetAnimTimeline::OnObjectChangedForLayout(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object && (Object->IsA<UWidgetAnimPresetAsset>() || (Source.IsValid() && Object == Source->GetEditedObject())))
	{
		++TrackLayoutRevision;
	}

	if (Object && Source.IsValid() && Object == Source->GetEditedObject())
	{
		JumpToEditedKey(Event);
	}
}

void SWidgetAnimTimeline::JumpToEditedKey(const FPropertyChangedEvent& Event)
{
	if (!UWidgetAnimEditorSettings::Get().bJumpToEditedKey || bJumpingToEditedKey || IsBodyCollapsed())
	{
		return;
	}
	TGuardValue<bool> Guard(bJumpingToEditedKey, true);

	// A key shown in the Selection panel: whatever was edited there was that key.
	if (SelectedKeys.Num() > 0)
	{
		const FKeyRef& Key = SelectedKeys.Last();
		FocusPreviewOnKey(Key.Layer, Key.Key);
		return;
	}

	// A track: its start value or its end value, by name - Start Opacity, End Scale, a registered type's Start Value. The
	// slide's distance is where it starts from.
	if (SelectedLayers.Num() != 1 || !Event.Property)
	{
		return;
	}
	const FString Name = Event.Property->GetName();
	const bool bStart = (Name.StartsWith(TEXT("Start")) && Name != TEXT("Start")) || Name == TEXT("SlideDistance");
	const bool bEnd = Name.StartsWith(TEXT("End"));
	if (!bStart && !bEnd)
	{
		return;
	}
	const int32 LayerIndex = SelectedLayers[0];
	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (KeyTimes.Num() > 0)
	{
		FocusPreviewOnKey(LayerIndex, bStart ? 0 : KeyTimes.Num() - 1);
	}
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
	if (!GetGroups() || !GetLayers())
	{
		return;
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	FLayoutIndex Index;
	BuildLayoutIndex(Index);
	GetListOrder(OutOrder, Index, Spans);
}

void SWidgetAnimTimeline::GetListOrder(TArray<int32>& OutOrder, const FLayoutIndex& Index, const TArray<FVector2f>& Spans, TArray<TPair<int32, int32>>* OutGroupSlices) const
{
	OutOrder.Reset();
	if (OutGroupSlices)
	{
		OutGroupSlices->Init(TPair<int32, int32>(INDEX_NONE, 0), Index.GroupMembers.Num());
	}

	// Group by group, in the order the list shows them - folders gathered - and in each the order its layers start - the
	// order the animation plays them - ties in the order they sit in the sequence.
	TArray<int32> GroupOrder;
	GetGroupListOrder(GroupOrder);

	TArray<int32> Members;
	for (const int32 GroupIndex : GroupOrder)
	{
		if (!Index.GroupMembers.IsValidIndex(GroupIndex))
		{
			continue;
		}

		Members = Index.GroupMembers[GroupIndex];
		Members.StableSort([&Spans](int32 A, int32 B)
		{
			const float StartA = Spans.IsValidIndex(A) ? Spans[A].X : 0.f;
			const float StartB = Spans.IsValidIndex(B) ? Spans[B].X : 0.f;
			return StartA < StartB - KINDA_SMALL_NUMBER;
		});

		if (OutGroupSlices)
		{
			(*OutGroupSlices)[GroupIndex] = TPair<int32, int32>(OutOrder.Num(), Members.Num());
		}
		OutOrder.Append(Members);
	}
}

uint32 SWidgetAnimTimeline::ComputeListSignature() const
{
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	FLayoutIndex Index;
	BuildLayoutIndex(Index);
	return ComputeListSignature(Index, Spans);
}

uint32 SWidgetAnimTimeline::ComputeListSignature(const FLayoutIndex& Index, const TArray<FVector2f>& Spans) const
{
	TArray<int32> Order;
	GetListOrder(Order, Index, Spans);

	uint32 Hash = GetTypeHash(Order.Num());

	// Each track's group and collapsed track, which decide its row - and whether its group is shown at all (see
	// IsGroupAllCollapsed) - changed by a collapse, an expand, or an undo of either.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	for (const int32 LayerIndex : Order)
	{
		Hash = HashCombine(Hash, GetTypeHash(LayerIndex));
		Hash = HashCombine(Hash, GetTypeHash(Index.LayerGroups.IsValidIndex(LayerIndex) ? Index.LayerGroups[LayerIndex] : INDEX_NONE));
		Hash = HashCombine(Hash, GetTypeHash(Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].CollapsedTrack : FName()));
	}

	// The folders the groups are in, which make rows of their own - changed in the Selection panel, or by an undo. As
	// stored, rather than trimmed as GetGroupFolder shows them, so nothing is copied every frame: a change that only adds
	// a space rebuilds the list once for nothing.
	if (const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups())
	{
		const bool bFolders = Source.IsValid() && Source->SupportsLayerGroups();
		for (const FWidgetAnimLayerGroup& Group : *Groups)
		{
			Hash = HashCombine(Hash, bFolders ? GetTypeHash(Group.EditorFolder) : 0u);
		}
	}
	return Hash;
}

FText SWidgetAnimTimeline::GetLayerTimeRange(int32 LayerIndex) const
{
	// Asked by every row of the list, every frame: the spans worked out for this frame's layout, as GetShownSpan reads
	// them. Working every span out again for each row cost the square of the track count.
	FVector2f Span;
	if (TrackSpans.IsValidIndex(LayerIndex))
	{
		Span = TrackSpans[LayerIndex];
	}
	else
	{
		TArray<FVector2f> Spans;
		GetLayerSpans(Spans);
		if (!Spans.IsValidIndex(LayerIndex))
		{
			return FText::GetEmpty();
		}
		Span = Spans[LayerIndex];
	}
	return FText::FromString(FormatSpan(Span.X, Span.X + Span.Y));
}

void SWidgetAnimTimeline::RebuildLayerList()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimPanelRebuildList);
	using namespace WidgetAnimTimeline;

	// Every layer's group and collapsed track read once, and the list's order worked out from them - asked of every layer
	// for every group, a list of hundreds of tracks took the square of that to build.
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	FLayoutIndex Index;
	BuildLayoutIndex(Index);
	ListSignature = ComputeListSignature(Index, Spans);

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

	// The order, and where each group's layers are in it.
	TArray<int32> Order;
	TArray<TPair<int32, int32>> GroupSlices;
	GetListOrder(Order, Index, Spans, &GroupSlices);

	// Groups in a folder are listed under its row, in a box of their own with the folder's strip down its left: the rows of
	// each are added to that box, as LayerList, while they are made. See GetGroupListOrder.
	const TSharedPtr<SVerticalBox> ListRoot = LayerList;
	FString OpenFolder;

	// First, the collapsed tracks whose groups aren't shown - every track of those groups is in a collapsed track - each
	// a row of its own, as the Track Editor draws them loose at the top.
	for (const int32 LayerIndex : Order)
	{
		if (Index.IsLooseCollapsedHead(LayerIndex) && Index.InView[LayerIndex])
		{
			AddCollapsedListRows((*Layers)[LayerIndex].CollapsedTrack, Index.LayerGroups[LayerIndex]);
		}
	}

	TArray<int32> GroupOrder;
	GetGroupListOrder(GroupOrder);
	for (const int32 GroupIndex : GroupOrder)
	{
		if (Index.IsGroupAllCollapsed(GroupIndex) || !Index.GroupMembers.IsValidIndex(GroupIndex))
		{
			continue;
		}

		// Inside a collapsed track, only the groups its tracks are in.
		const bool bAnyInView = !IsInCollapsedScope() || Index.GroupMembers[GroupIndex].ContainsByPredicate([&Index](int32 LayerIndex)
		{
			return static_cast<bool>(Index.InView[LayerIndex]);
		});
		if (!bAnyInView)
		{
			continue;
		}

		const FString Folder = GetGroupFolder(GroupIndex);
		if (Folder != OpenFolder)
		{
			LayerList = ListRoot;
			OpenFolder = Folder;
			if (!Folder.IsEmpty())
			{
				ListRoot->AddSlot()
				.AutoHeight()
				[
					MakeListFolderRow(Folder)
				];

				if (!IsListFolderCollapsed(Folder))
				{
					const TSharedRef<SVerticalBox> FolderBody = SNew(SVerticalBox);
					ListRoot->AddSlot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(ListFolderIndent)
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
									.ColorAndOpacity(Palette::ListFolderStrip)
								]

								// A thin line down from the folder's icon, to the end of its groups.
								+ SOverlay::Slot()
								.HAlign(HAlign_Center)
								[
									SNew(SBox)
									.WidthOverride(1.f)
									[
										SNew(SImage)
										.Image(WhiteBrush())
										.ColorAndOpacity(WithAlpha(Palette::ListFolderAccent, 0.45f))
									]
								]
							]
						]

						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						[
							FolderBody
						]
					];
					LayerList = FolderBody;
				}
			}
		}
		if (!Folder.IsEmpty() && IsListFolderCollapsed(Folder))
		{
			continue;
		}

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
							if (IsGroupSelected(GroupIndex))
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
							SNew(SBox)
							.WidthOverride(GroupBandWidth)
							[
								SNew(SImage)
								.Image(WhiteBrush())
								.ColorAndOpacity(GetGroupColor(GroupIndex))
							]
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

						// The widget it animates, as a small pill on the right - or, while it is being picked in the designer, what to do.
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.MaxWidth(140.f)
						.VAlign(VAlign_Center)
						.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
						[
							SNew(SBorder)
							.BorderImage(PillBrush())
							.Padding(FMargin(7.f, 1.f))
							.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::GroupTargetTooltip()))
							[
								SNew(STextBlock)
								.Text_Lambda([this, GroupIndex]() { return IsPickingTarget(GroupIndex) ? GetPickingTargetText() : GetGroupTargetLabel(GroupIndex); })
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.ColorAndOpacity_Lambda([this, GroupIndex]() { return IsPickingTarget(GroupIndex) ? FSlateColor(Palette::PickingTargetText) : FSlateColor::UseSubduedForeground(); })
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							]
						]
					]

					+ SOverlay::Slot()
					.VAlign(VAlign_Bottom)
					[
						SNew(SBox)
						.HeightOverride(1.f)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(SeparatorColor)
						]
					]
				]
			];
		*GroupRowWidget = GroupRow;

		LayerList->AddSlot()
		.AutoHeight()
		[
			SNew(SWidgetAnimRestingRow)
			[
				GroupRow
			]
		];

		if (IsListGroupCollapsed(GroupIndex))
		{
			continue;
		}

		// ---- Its layers, in the order they play: what, and when ----------------------------------------------
		//
		// A collapsed track is one row, in the group of its first track, opening onto a row for each of its tracks; the
		// rest of its tracks aren't listed in their own groups.

		const TPair<int32, int32> Slice = GroupSlices.IsValidIndex(GroupIndex) ? GroupSlices[GroupIndex] : TPair<int32, int32>(INDEX_NONE, 0);
		for (int32 Position = FMath::Max(Slice.Key, 0); Position < Slice.Key + Slice.Value; ++Position)
		{
			const int32 LayerIndex = Order[Position];
			if (!Index.InView[LayerIndex] || Index.FoldedIntoCollapsed[LayerIndex])
			{
				continue;
			}

			if (Index.CollapsedHeads[LayerIndex])
			{
				AddCollapsedListRows((*Layers)[LayerIndex].CollapsedTrack, GroupIndex);
				continue;
			}

			LayerList->AddSlot()
			.AutoHeight()
			[
				MakeListLayerRow(LayerIndex, GroupIndex, 0.f)
			];

			AddNestedListRows(LayerIndex, GroupIndex);
		}
	}

	LayerList = ListRoot;
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeListFolderRow(const FString& Folder)
{
	using namespace WidgetAnimTimeline;

	TArray<int32> Members;
	GetFolderGroups(Folder, Members);
	const bool bCollapsed = IsListFolderCollapsed(Folder);
	const TSharedRef<TWeakPtr<SWidget>> RowWidget = MakeShared<TWeakPtr<SWidget>>();
	const FSlateBrush* FolderIcon = bCollapsed
		? PickBrush({ "Icons.FolderClosed", "ContentBrowser.AssetTreeFolderClosed" })
		: PickBrush({ "Icons.FolderOpen", "ContentBrowser.AssetTreeFolderOpen" });

	const TSharedRef<SWidgetAnimListFolderRow> Row = SNew(SWidgetAnimListFolderRow, SharedThis(this), Folder)
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListFolderRowTooltip()))
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
					.ColorAndOpacity(Palette::ListFolderStrip)
				]

				// Lit while every group in it is selected, and under the mouse.
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity_Lambda([this, Members, RowWidget]()
					{
						const bool bAllSelected = Members.Num() > 0 && Algo::AllOf(Members, [this](int32 GroupIndex) { return IsGroupSelected(GroupIndex); });
						if (bAllSelected)
						{
							return FSlateColor(WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha));
						}
						const TSharedPtr<SWidget> Pinned = RowWidget->Pin();
						return FSlateColor(Pinned.IsValid() && Pinned->IsHovered() ? WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha) : FLinearColor::Transparent);
					})
				]

				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)

					// Fold open / shut: shows or hides the folder's groups in the list.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(FMargin(1.f, 0.f))
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListFolderFoldTooltip()))
						.OnClicked_Lambda([this, Folder]() { ToggleListFolderCollapsed(Folder); return FReply::Handled(); })
						[
							SNew(SImage)
							.Image(bCollapsed
								? PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" })
								: PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" }))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(3.f, 0.f, 0.f, 0.f))
					[
						SNew(SImage)
						.Image(FolderIcon)
						.DesiredSizeOverride(FVector2D(IconSize, IconSize))
						.ColorAndOpacity(Palette::ListFolderAccent)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(5.f, 0.f, 6.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text(FText::FromString(Folder))
					]

					// How many groups it holds, as a small pill on the right.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						SNew(SBorder)
						.BorderImage(PillBrush())
						.Padding(FMargin(7.f, 1.f))
						[
							SNew(STextBlock)
							.Text(FText::Format(LOCTEXT("FolderGroupCount", "{0} {0}|plural(one=group,other=groups)"), FText::AsNumber(Members.Num())))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						]
					]
				]

				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.HeightOverride(1.f)
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(SeparatorColor)
					]
				]
			]
		];
	*RowWidget = Row;
	return Row;
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeListLayerRow(int32 LayerIndex, int32 GroupIndex, float Indent)
{
	using namespace WidgetAnimTimeline;

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);
	const TSharedRef<TWeakPtr<SWidget>> RowWidget = MakeShared<TWeakPtr<SWidget>>();
	auto IsWashedOut = [this, LayerIndex]() { return IsReadOnly() || IsLayerLocked(LayerIndex) || IsLayerHidden(LayerIndex); };
	auto IsRowHovered = [RowWidget]() { const TSharedPtr<SWidget> Pinned = RowWidget->Pin(); return Pinned.IsValid() && Pinned->IsHovered(); };
	const bool bAnimationTrack = GetInsertedAnimation(LayerIndex) != nullptr;

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

				// A track of a collapsed track, opened in the list: tinted as the collapsed track is.
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity(Indent > 0.f ? Palette::CollapsedListTint : FLinearColor::Transparent)
				]

				// The list's own highlight: a light tint on the rows picked here or in the Track Editor, and
				// on the one under the mouse.
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity_Lambda([this, LayerIndex, IsRowHovered]()
					{
						if (IsLayerSelected(LayerIndex))
						{
							return FSlateColor(WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha * 0.6f));
						}
						return FSlateColor(IsRowHovered() ? WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha) : FLinearColor::Transparent);
					})
				]

				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(GroupBandWidth)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(GetGroupColor(GroupIndex))
						]
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

					// An animation track opens here, to show its animation's groups and tracks under it.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(Indent + (bAnimationTrack ? 4.f : 0.f), 0.f, 0.f, 0.f))
					[
						SNew(SButton)
						.Visibility(bAnimationTrack ? EVisibility::Visible : EVisibility::Collapsed)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(FMargin(1.f, 0.f))
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::AnimationTrackFoldTooltip()))
						.OnClicked_Lambda([this, LayerIndex]() { ToggleAnimationTrackExpanded(LayerIndex); return FReply::Handled(); })
						[
							SNew(SImage)
							.Image(IsAnimationTrackExpanded(LayerIndex)
								? PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" })
								: PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" }))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]

					// Its type - and, for a track brought in with Insert Existing Widget Animation, the
					// animation it came from, in italics. An animation track is named for its animation, in its orange.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(bAnimationTrack ? 3.f : 18.f, 0.f, 0.f, 0.f))
					[
						SNew(STextBlock)
						.ColorAndOpacity_Lambda([IsWashedOut, bAnimationTrack]()
						{
							if (bAnimationTrack)
							{
								return FSlateColor(IsWashedOut() ? GetLockedColor(Palette::LayerAnimation) : Palette::LayerAnimation);
							}
							return IsWashedOut() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
						})
						.Text_Lambda([this, LayerIndex]()
						{
							const TArray<FWidgetAnimLayer>* Current = GetLayers();
							return Current && Current->IsValidIndex(LayerIndex)
								? GetLayerDisplayName((*Current)[LayerIndex])
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
							if (!Current || !Current->IsValidIndex(LayerIndex))
							{
								return FText::GetEmpty();
							}

							// A renamed track still says what type it is; one brought in from an animation, which.
							const FWidgetAnimLayer& Shown = (*Current)[LayerIndex];
							TArray<FString> Parts;
							if (!Shown.TrackName.TrimStartAndEnd().IsEmpty())
							{
								Parts.Add(GetLayerTypeName(Shown).ToString());
							}
							if (!Shown.InsertedFrom.IsEmpty())
							{
								Parts.Add(Shown.InsertedFrom);
							}
							return Parts.Num() > 0
								? FText::Format(LOCTEXT("ListInsertedFrom", " \u2013 {0}"), FText::FromString(FString::Join(Parts, TEXT(", "))))
								: FText::GetEmpty();
						})
					]

					// Pin: to the top of the Track Editor. Shown while the row is hovered, and always while pinned.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(2.f, 0.f))
						.Visibility_Lambda([this, LayerIndex, IsRowHovered]() { return IsLayerPinned(LayerIndex) || IsRowHovered() ? EVisibility::Visible : EVisibility::Hidden; })
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this, LayerIndex]()
						{
							return IsLayerPinned(LayerIndex) ? WidgetAnimTips::UnpinTrackTooltip() : WidgetAnimTips::PinTrackTooltip();
						})))
						.OnClicked_Lambda([this, LayerIndex]()
						{
							SetLayersPinned({ LayerIndex }, !IsLayerPinned(LayerIndex));
							return FReply::Handled();
						})
						[
							MakeFixedIcon(
								TAttribute<const FSlateBrush*>::CreateLambda([this, LayerIndex]()
								{
									return IsLayerPinned(LayerIndex)
										? PickBrush({ "Icons.Pinned", "Icons.Pin", "PropertyWindow.Favorites_Enabled" })
										: PickBrush({ "Icons.Unpinned", "Icons.Pin", "PropertyWindow.Favorites_Disabled" });
								}),
								TAttribute<FSlateColor>::CreateLambda([this, LayerIndex]()
								{
									return IsLayerPinned(LayerIndex) ? FSlateColor(Palette::ListPinnedIcon) : FSlateColor::UseSubduedForeground();
								}),
								12.f)
						]
					]

					// Show or hide: the eye, always there - open while the track plays in the previews, shut while it is hidden.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(2.f, 0.f))
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([this, LayerIndex]()
						{
							return IsLayerHidden(LayerIndex) ? WidgetAnimTips::ShowHiddenTrackTooltip() : WidgetAnimTips::HideTrackButtonTooltip();
						})))
						.OnClicked_Lambda([this, LayerIndex]()
						{
							SetLayersHidden({ LayerIndex }, !IsLayerHidden(LayerIndex));
							return FReply::Handled();
						})
						[
							MakeFixedIcon(
								TAttribute<const FSlateBrush*>::CreateLambda([this, LayerIndex]()
								{
									return IsLayerHidden(LayerIndex)
										? PickBrush({ "Icons.Hidden", "Level.NotVisibleIcon16x" })
										: PickBrush({ "Icons.Visible", "Level.VisibleIcon16x" });
								}),
								TAttribute<FSlateColor>::CreateLambda([this, LayerIndex, IsRowHovered]()
								{
									// Quiet while the track plays, so a column of open eyes doesn't shout; plain once hidden, or hovered.
									return IsLayerHidden(LayerIndex) || IsRowHovered()
										? FSlateColor::UseSubduedForeground()
										: FSlateColor(Palette::ListQuietIcon);
								}),
								12.f)
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
					SNew(SBox)
					.HeightOverride(1.f)
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(SeparatorColor)
					]
				]
			]
		];
	*RowWidget = LayerRow;
	return SNew(SWidgetAnimRestingRow)
		[
			LayerRow
		];
}

void SWidgetAnimTimeline::AddCollapsedListRows(FName Name, int32 GroupIndex)
{
	using namespace WidgetAnimTimeline;

	TArray<int32> Members;
	GetCollapsedMembers(Name, Members);
	if (Members.Num() == 0 || !LayerList.IsValid())
	{
		return;
	}

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);
	const int32 Head = Members[0];
	const bool bOpen = ListOpenCollapsed.Contains(Name);
	const TSharedRef<TWeakPtr<SWidget>> RowWidget = MakeShared<TWeakPtr<SWidget>>();
	auto IsRowHovered = [RowWidget]() { const TSharedPtr<SWidget> Pinned = RowWidget->Pin(); return Pinned.IsValid() && Pinned->IsHovered(); };

	// Read live, so a change to any of its tracks shows without the list being built again - each in one walk over the
	// layers (see GetCollapsedState), as they are asked every frame.
	auto AllSelected = [this, Name]() { return GetCollapsedState(Name).bAllSelected; };
	auto AllHidden = [this, Name]() { return GetCollapsedState(Name).bAllHidden; };
	auto AnyPinned = [this, Name]() { return GetCollapsedState(Name).bAnyPinned; };

	const TSharedRef<SWidgetAnimListRow> Row = SNew(SWidgetAnimListRow, WeakThisPtr, GroupIndex, Head, Name)
		.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::ListCollapsedRowTooltip()))
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

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity(Palette::CollapsedListTint)
				]

				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(WhiteBrush())
					.ColorAndOpacity_Lambda([AllSelected, IsRowHovered]()
					{
						if (AllSelected())
						{
							return FSlateColor(WithAlpha(SelectColor(), Palette::RowSelectedTintAlpha * 0.6f));
						}
						return FSlateColor(IsRowHovered() ? WithAlpha(HoverColor(), Palette::RowHoveredTintAlpha) : FLinearColor::Transparent);
					})
				]

				+ SOverlay::Slot()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(GroupBandWidth)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(GetGroupColor(GroupIndex))
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.WidthOverride(2.f)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity_Lambda([AllSelected]() { return FSlateColor(AllSelected() ? Palette::ListSelectedAccent : Palette::LayerCollapsed); })
						]
					]

					// Its dropdown: a row for each of its tracks, under it.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.ContentPadding(FMargin(1.f, 0.f))
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::CollapsedListFoldTooltip()))
						.OnClicked_Lambda([this, Name]()
						{
							if (ListOpenCollapsed.Contains(Name))
							{
								ListOpenCollapsed.Remove(Name);
							}
							else
							{
								ListOpenCollapsed.Add(Name);
							}
							RebuildLayerList();
							return FReply::Handled();
						})
						[
							SNew(SImage)
							.Image(bOpen
								? PickBrush({ "TreeArrow_Expanded", "Icons.ChevronDown" })
								: PickBrush({ "TreeArrow_Collapsed", "Icons.ChevronRight" }))
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						]
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(3.f, 0.f, 4.f, 0.f))
					[
						MakeFixedIcon(PickBrush({ "GraphEditor.SubGraph_16x", "Icons.FolderClosed" }), FSlateColor(Palette::LayerCollapsed), 12.f)
					]

					// Its name, bold, and how many tracks it holds.
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.ColorAndOpacity_Lambda([this, AllHidden]() { return IsReadOnly() || AllHidden() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground(); })
						.Text(FText::Format(LOCTEXT("CollapsedListName", "{0}  ({1})"), FText::FromName(Name), FText::AsNumber(Members.Num())))
					]

					// Pin every one of its tracks - or unpin them.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(2.f, 0.f))
						.Visibility_Lambda([AnyPinned, IsRowHovered]() { return AnyPinned() || IsRowHovered() ? EVisibility::Visible : EVisibility::Hidden; })
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([AnyPinned]()
						{
							return AnyPinned() ? WidgetAnimTips::UnpinTrackTooltip() : WidgetAnimTips::PinTrackTooltip();
						})))
						.OnClicked_Lambda([this, Name, AnyPinned]()
						{
							TArray<int32> Current;
							GetCollapsedMembers(Name, Current);
							SetLayersPinned(Current, !AnyPinned());
							return FReply::Handled();
						})
						[
							MakeFixedIcon(
								TAttribute<const FSlateBrush*>::CreateLambda([AnyPinned]()
								{
									return AnyPinned()
										? PickBrush({ "Icons.Pinned", "Icons.Pin", "PropertyWindow.Favorites_Enabled" })
										: PickBrush({ "Icons.Unpinned", "Icons.Pin", "PropertyWindow.Favorites_Disabled" });
								}),
								TAttribute<FSlateColor>::CreateLambda([AnyPinned]() { return AnyPinned() ? FSlateColor(Palette::ListPinnedIcon) : FSlateColor::UseSubduedForeground(); }),
								12.f)
						]
					]

					// Show or hide every one of its tracks.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ContentPadding(FMargin(2.f, 0.f))
						.ToolTip(WidgetAnimTimeline::MakePanelToolTip(TAttribute<FText>::CreateLambda([AllHidden]()
						{
							return AllHidden() ? WidgetAnimTips::ShowHiddenTrackTooltip() : WidgetAnimTips::HideTrackButtonTooltip();
						})))
						.OnClicked_Lambda([this, Name, AllHidden]()
						{
							TArray<int32> Current;
							GetCollapsedMembers(Name, Current);
							SetLayersHidden(Current, !AllHidden());
							return FReply::Handled();
						})
						[
							MakeFixedIcon(
								TAttribute<const FSlateBrush*>::CreateLambda([AllHidden]()
								{
									return AllHidden()
										? PickBrush({ "Icons.Hidden", "Level.NotVisibleIcon16x" })
										: PickBrush({ "Icons.Visible", "Level.VisibleIcon16x" });
								}),
								TAttribute<FSlateColor>::CreateLambda([AllHidden, IsRowHovered]()
								{
									return AllHidden() || IsRowHovered() ? FSlateColor::UseSubduedForeground() : FSlateColor(Palette::ListQuietIcon);
								}),
								12.f)
						]
					]

					// When it runs: from its first track's start to its last one's end.
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(6.f, 0.f, 8.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([this, Head]()
						{
							const FVector2f Span = GetShownSpan(Head);
							return FText::FromString(FormatSpan(Span.X, Span.X + Span.Y));
						})
					]
				]

				+ SOverlay::Slot()
				.VAlign(VAlign_Bottom)
				[
					SNew(SBox)
					.HeightOverride(1.f)
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(SeparatorColor)
					]
				]
			]
		];
	*RowWidget = Row;

	LayerList->AddSlot()
	.AutoHeight()
	[
		SNew(SWidgetAnimRestingRow)
		[
			Row
		]
	];

	// Open: a row for each of its tracks, in the order they play, each in its own group's color - picked one by one, to
	// see or change one; double-clicking the collapsed track opens it whole.
	if (bOpen)
	{
		for (const int32 Member : Members)
		{
			LayerList->AddSlot()
			.AutoHeight()
			[
				MakeListLayerRow(Member, GetLayerGroup(Member), 14.f)
			];
		}
	}
}

void SWidgetAnimTimeline::AddNestedListRows(int32 LayerIndex, int32 GroupIndex)
{
	using namespace WidgetAnimTimeline;

	const UWidgetAnimPresetAsset* Animation = GetInsertedAnimation(LayerIndex);
	if (!Animation || !IsAnimationTrackExpanded(LayerIndex) || !LayerList.IsValid())
	{
		return;
	}

	const TWeakPtr<SWidgetAnimTimeline> WeakThisPtr = SharedThis(this);
	const TWeakObjectPtr<const UWidgetAnimPresetAsset> WeakAnimation = Animation;

	// One row, indented under the track and tinted its orange: the group's band, then the orange edge, then Content. A
	// click on it is a click on the animation track - it selects it, and its menu is the track's.
	auto AddRow = [this, &WeakThisPtr, GroupIndex, LayerIndex](bool bHeader, const TSharedRef<SWidget>& Content)
	{
		LayerList->AddSlot()
		.AutoHeight()
		[
			SNew(SWidgetAnimListRow, WeakThisPtr, GroupIndex, LayerIndex)
			.ToolTip(WidgetAnimTimeline::MakePanelToolTip(WidgetAnimTips::NestedRowTooltip()))
			[
				SNew(SBox)
				.HeightOverride(ListRowHeight)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(bHeader ? HeaderColor() : BackgroundColor())
					]
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image(WhiteBrush())
						.ColorAndOpacity(bHeader ? Palette::NestedHeaderTint : Palette::NestedTint)
					]
					+ SOverlay::Slot()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(GroupBandWidth)
							[
								SNew(SImage)
								.Image(WhiteBrush())
								.ColorAndOpacity(GetGroupColor(GroupIndex))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(Palette::NestedEdgeWidth)
							[
								SNew(SImage)
								.Image(WhiteBrush())
								.ColorAndOpacity(Palette::NestedEdge)
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.f)
						.VAlign(VAlign_Center)
						[
							Content
						]
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Bottom)
					[
						SNew(SBox)
						.HeightOverride(1.f)
						[
							SNew(SImage)
							.Image(WhiteBrush())
							.ColorAndOpacity(SeparatorColor)
						]
					]
				]
			]
		];
	};

	// Group by group, as the animation's own panel lists them, each track in the order it starts.
	TArray<float> Starts;
	UWidgetAnimLibrary::PlaceLayerStarts(Animation->Layers, TConstArrayView<float>(), Starts);
	const int32 NumGroups = FMath::Max(Animation->LayerGroups.Num(), 1);
	for (int32 NestedGroup = 0; NestedGroup < NumGroups; ++NestedGroup)
	{
		const FString GroupName = Animation->LayerGroups.IsValidIndex(NestedGroup)
			? Animation->LayerGroups[NestedGroup].GetDisplayName(NestedGroup)
			: FWidgetAnimLayerGroup::MakeDefaultName(NestedGroup);
		const bool bFolded = IsNestedGroupFolded(LayerIndex, NestedGroup);

		AddRow(/*bHeader=*/true,
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(14.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(1.f, 0.f))
				.OnClicked_Lambda([this, LayerIndex, NestedGroup]() { ToggleNestedGroupFolded(LayerIndex, NestedGroup); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image(bFolded
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
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(FSlateColor(Palette::LayerAnimation))
				.Text(FText::FromString(GroupName))
			]);

		if (bFolded)
		{
			continue;
		}

		TArray<int32> Members;
		for (int32 Index = 0; Index < Animation->Layers.Num(); ++Index)
		{
			if (UWidgetAnimLibrary::GetLayerGroupIndex(Animation->Layers[Index], Animation->LayerGroups.Num()) == NestedGroup || Animation->LayerGroups.Num() == 0)
			{
				Members.Add(Index);
			}
		}
		Members.StableSort([&Starts](int32 A, int32 B)
		{
			return (Starts.IsValidIndex(A) ? Starts[A] : 0.f) < (Starts.IsValidIndex(B) ? Starts[B] : 0.f) - KINDA_SMALL_NUMBER;
		});

		for (const int32 Index : Members)
		{
			AddRow(/*bHeader=*/false,
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(32.f, 0.f, 6.f, 0.f))
				[
					SNew(STextBlock)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([WeakAnimation, Index]()
					{
						const UWidgetAnimPresetAsset* Current = WeakAnimation.Get();
						return Current && Current->Layers.IsValidIndex(Index) ? GetLayerDisplayName(Current->Layers[Index]) : FText::GetEmpty();
					})
				]
				// When it runs, from the top of this animation.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.f, 0.f, 8.f, 0.f))
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([this, LayerIndex, Index]()
					{
						const TArray<FVector2f>* Spans = GetNestedSpans(LayerIndex);
						if (!Spans || !Spans->IsValidIndex(Index))
						{
							return FText::GetEmpty();
						}
						const FVector2f Span = (*Spans)[Index];
						return FText::FromString(FormatSpan(Span.X, Span.X + Span.Y));
					})
				]);
		}
	}
}

void SWidgetAnimTimeline::UpdateTrackLayout()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(WidgetAnimPanelTrackLayout);

	// What these lines are worked out from, for IsTrackLayoutDue - and the list's order is to be checked against them.
	LaidOutRevision = TrackLayoutRevision;
	LaidOutViewHash = ComputeTrackViewHash();
	LastTrackLayoutTime = FPlatformTime::Seconds();
	bLaidOutWhileDragging = IsDragging();
	bListCheckPending = true;
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
	TrackGroupSpans.Reset();
	UpdateStaggerFans();
	GetLayerSpans(TrackSpans);
	UpdateNestedSpans();

	// Every layer's group, collapsed track and view, read once: everything below asks them of every layer, and of every
	// group's layers - asked one by one, that cost the square of the track count, every frame.
	BuildLayoutIndex(LayoutIndex);
	const FLayoutIndex& Index = LayoutIndex;

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		return;
	}

	const int32 NumLayers = Layers->Num();
	const int32 NumGroups = Groups->Num();
	LayerToLine.Init(INDEX_NONE, NumLayers);
	LayerLineOrdinals.Init(0, NumLayers);
	GroupLineCounts.Init(0, NumGroups);
	CollapsedSpans.Reset();
	NumPinnedLines = 0;

	// GetLayerFanSeconds, with the time scale read once.
	const float TimeScale = GetTimeScale();
	auto FanSeconds = [this, TimeScale](int32 LayerIndex)
	{
		return TrackFans.IsValidIndex(LayerIndex) ? TrackFans[LayerIndex] * TimeScale : 0.f;
	};

	// Each collapsed track's stretch: from where its first track starts to where its last one ends, staggers and all -
	// drawn as one track on its head's line, and kept clear of the tracks beside it there as one.
	{
		TMap<FName, FVector2f> Stretches;
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const FName Collapsed = (*Layers)[LayerIndex].CollapsedTrack;
			if (Collapsed.IsNone() || Collapsed == ActiveCollapsed || !TrackSpans.IsValidIndex(LayerIndex))
			{
				continue;
			}

			FVector2f* Stretch = Stretches.Find(Collapsed);
			if (!Stretch)
			{
				Stretch = &Stretches.Add(Collapsed, FVector2f(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max()));
			}
			Stretch->X = FMath::Min(Stretch->X, TrackSpans[LayerIndex].X);
			Stretch->Y = FMath::Max(Stretch->Y, TrackSpans[LayerIndex].X + TrackSpans[LayerIndex].Y + FanSeconds(LayerIndex));
		}

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const FVector2f* Stretch = Index.CollapsedHeads[LayerIndex] ? Stretches.Find((*Layers)[LayerIndex].CollapsedTrack) : nullptr;
			if (Stretch && Stretch->X <= Stretch->Y)
			{
				CollapsedSpans.Add(LayerIndex, FVector2f(Stretch->X, Stretch->Y - Stretch->X));
			}
		}
	}

	// Each group's stretch, as GetGroupSpan has it - what its line draws. See GetShownGroupSpan.
	TrackGroupSpans.Init(FVector2f(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max()), NumGroups);
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		FVector2f& Range = TrackGroupSpans[GroupIndex];
		for (const int32 LayerIndex : Index.GroupMembers[GroupIndex])
		{
			if (Index.InView[LayerIndex] && TrackSpans.IsValidIndex(LayerIndex))
			{
				Range.X = FMath::Min(Range.X, TrackSpans[LayerIndex].X);
				Range.Y = FMath::Max(Range.Y, TrackSpans[LayerIndex].X + TrackSpans[LayerIndex].Y);
			}
		}
	}

	// Pinned: at the top, a line each, above every group - in the order the list shows them. Not a track folded into a
	// collapsed track, which is shown by it.
	auto IsPinnedHere = [Layers, &Index](int32 LayerIndex)
	{
		return (*Layers)[LayerIndex].bPinnedInEditor && Index.InView[LayerIndex] && !Index.FoldedIntoCollapsed[LayerIndex];
	};

	// Whether a track is laid out in its group: shown here, not folded into a collapsed track, not pinned above, and not a
	// collapsed track drawn loose because its group isn't shown. The rest keep the lines they were given, for when they
	// are back.
	auto IsOnGroupLine = [&Index, &IsPinnedHere](int32 LayerIndex)
	{
		return Index.InView[LayerIndex] && !Index.FoldedIntoCollapsed[LayerIndex] && !IsPinnedHere(LayerIndex) && !Index.IsLooseCollapsedHead(LayerIndex);
	};

	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		if (!IsOnGroupLine(LayerIndex))
		{
			LayerLineOrdinals[LayerIndex] = (*Layers)[LayerIndex].EditorLine;
		}
	}

	{
		TArray<int32> Order;
		GetListOrder(Order, Index, TrackSpans);
		for (const int32 LayerIndex : Order)
		{
			if (!IsPinnedHere(LayerIndex))
			{
				continue;
			}

			FTrackLine& Line = TrackLines.AddDefaulted_GetRef();
			Line.Kind = FTrackLine::EKind::Lane;
			Line.Group = Index.LayerGroups[LayerIndex];
			Line.Ordinal = 0;
			Line.Layers = { LayerIndex };
			Line.bPinned = true;
			LayerToLine[LayerIndex] = TrackLines.Num() - 1;
		}
		NumPinnedLines = TrackLines.Num();

		// Under them, scrolling with the rest: the collapsed tracks whose groups aren't shown, a line each, in list order.
		for (const int32 LayerIndex : Order)
		{
			if (!Index.IsLooseCollapsedHead(LayerIndex) || !Index.InView[LayerIndex] || IsPinnedHere(LayerIndex))
			{
				continue;
			}

			FTrackLine& Line = TrackLines.AddDefaulted_GetRef();
			Line.Kind = FTrackLine::EKind::Lane;
			Line.Group = Index.LayerGroups[LayerIndex];
			Line.Ordinal = 0;
			Line.Layers = { LayerIndex };
			Line.bLoose = true;
			LayerToLine[LayerIndex] = TrackLines.Num() - 1;
		}
	}

	// Where a layer asks to be. A layer with no line of its own set comes after every line that is set, in its
	// order in the sequence - which is how older animations lay out: one track per line, as they always did.
	auto LineKey = [Layers](int32 LayerIndex)
	{
		const int32 Line = (*Layers)[LayerIndex].EditorLine;
		return Line >= 0 ? Line : 1000000 + LayerIndex;
	};

	// Where a track is drawn - a collapsed track's head across all of its tracks - and where it ends, a stagger's tail
	// included: another track on its line starts after it. Read once for every track, as the packing below asks again
	// and again.
	TArray<float> Starts;
	TArray<float> Ends;
	Starts.SetNumUninitialized(NumLayers);
	Ends.SetNumUninitialized(NumLayers);
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		if (const FVector2f* Collapsed = CollapsedSpans.Find(LayerIndex))
		{
			Starts[LayerIndex] = Collapsed->X;
			Ends[LayerIndex] = Collapsed->X + Collapsed->Y;
		}
		else if (TrackSpans.IsValidIndex(LayerIndex))
		{
			Starts[LayerIndex] = TrackSpans[LayerIndex].X;
			Ends[LayerIndex] = TrackSpans[LayerIndex].X + TrackSpans[LayerIndex].Y + FanSeconds(LayerIndex);
		}
		else
		{
			Starts[LayerIndex] = 0.f;
			Ends[LayerIndex] = 0.f;
		}
	}
	auto StartOf = [&Starts](int32 LayerIndex) { return Starts[LayerIndex]; };

	static constexpr float OverlapTolerance = 0.0005f;
	auto Overlaps = [this, &Starts, &Ends](int32 A, int32 B)
	{
		if (!TrackSpans.IsValidIndex(A) || !TrackSpans.IsValidIndex(B))
		{
			return false;
		}
		return Starts[A] < Ends[B] - OverlapTolerance && Starts[B] < Ends[A] - OverlapTolerance;
	};

	// The tracks being dragged, which give way to the rest - asked of every pair the packing sorts.
	TBitArray<> Dragged(false, NumLayers);
	if (IsDraggingLayers())
	{
		for (const TArray<int32>* Edges : { &LayerDrag.StartEdges, &LayerDrag.EndEdges })
		{
			for (const int32 LayerIndex : *Edges)
			{
				if (Dragged.IsValidIndex(LayerIndex))
				{
					Dragged[LayerIndex] = true;
				}
			}
		}
	}

	TArray<int32> Members;
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		Members.Reset();
		for (const int32 LayerIndex : Index.GroupMembers[GroupIndex])
		{
			if (IsOnGroupLine(LayerIndex))
			{
				Members.Add(LayerIndex);
			}
		}

		// Inside a collapsed track, only the groups its tracks are in - pinned ones included.
		if (IsInCollapsedScope() && Members.Num() == 0)
		{
			const bool bAnyPinned = Index.GroupMembers[GroupIndex].ContainsByPredicate([&Index](int32 LayerIndex)
			{
				return static_cast<bool>(Index.InView[LayerIndex]);
			});
			if (!bAnyPinned)
			{
				continue;
			}
		}

		// Each line the group's layers ask for, in order. On it, the tracks that stay put first - so a track
		// being dragged is the one that gives way - then by when they start. A track overlapping one already
		// placed goes on a line of its own just below. Sorted once by all three, then packed one asked-for line at a time.
		Members.StableSort([&LineKey, &Dragged, &StartOf](int32 A, int32 B)
		{
			const int32 KeyA = LineKey(A);
			const int32 KeyB = LineKey(B);
			if (KeyA != KeyB)
			{
				return KeyA < KeyB;
			}
			const bool bDraggedA = Dragged[A];
			const bool bDraggedB = Dragged[B];
			if (bDraggedA != bDraggedB)
			{
				return !bDraggedA;
			}
			return StartOf(A) < StartOf(B);
		});

		// Each line's earliest start and latest end, so a track clear of all of it is placed without checking it against
		// every track already there - only one that falls inside that stretch is.
		TArray<TArray<int32>> GroupLines;
		TArray<FVector2f> LineBounds;
		for (int32 RunStart = 0; RunStart < Members.Num();)
		{
			const int32 Key = LineKey(Members[RunStart]);
			int32 RunEnd = RunStart + 1;
			while (RunEnd < Members.Num() && LineKey(Members[RunEnd]) == Key)
			{
				++RunEnd;
			}

			const int32 FirstLine = GroupLines.Num();
			for (int32 Position = RunStart; Position < RunEnd; ++Position)
			{
				const int32 LayerIndex = Members[Position];
				int32 Placed = INDEX_NONE;
				for (int32 Candidate = FirstLine; Candidate < GroupLines.Num() && Placed == INDEX_NONE; ++Candidate)
				{
					const FVector2f Bounds = LineBounds[Candidate];
					const bool bClearOfAll = Starts[LayerIndex] >= Bounds.Y - OverlapTolerance || Ends[LayerIndex] <= Bounds.X + OverlapTolerance;
					const bool bFree = bClearOfAll || !GroupLines[Candidate].ContainsByPredicate([&Overlaps, LayerIndex](int32 Other) { return Overlaps(LayerIndex, Other); });
					if (bFree)
					{
						Placed = Candidate;
					}
				}

				if (Placed == INDEX_NONE)
				{
					Placed = GroupLines.AddDefaulted();
					LineBounds.Add(FVector2f(TNumericLimits<float>::Max(), -TNumericLimits<float>::Max()));
				}
				GroupLines[Placed].Add(LayerIndex);
				LineBounds[Placed].X = FMath::Min(LineBounds[Placed].X, Starts[LayerIndex]);
				LineBounds[Placed].Y = FMath::Max(LineBounds[Placed].Y, Ends[LayerIndex]);
			}
			RunStart = RunEnd;
		}

		GroupLineCounts[GroupIndex] = GroupLines.Num();

		// A group whose tracks are all in collapsed tracks isn't shown: its collapsed tracks are drawn loose above.
		if (Index.IsGroupAllCollapsed(GroupIndex))
		{
			continue;
		}

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

			// Under the line, each open animation track on it, in the order they start - not one folded into a collapsed
			// track, whose line is the collapsed track's.
			for (const int32 LayerIndex : GroupLines[Ordinal])
			{
				if (!Index.CollapsedHeads[LayerIndex])
				{
					AddNestedLines(LayerIndex, GroupIndex);
				}
			}
		}
	}
}

void SWidgetAnimTimeline::AddNestedLines(int32 LayerIndex, int32 GroupIndex)
{
	const TArray<FVector2f>* Spans = NestedSpans.Find(LayerIndex);
	const UWidgetAnimPresetAsset* Animation = GetInsertedAnimation(LayerIndex);
	if (!Spans || !Animation)
	{
		return;
	}

	// The inserted animation laid out as its own panel lays it out: group by group, each group's header and then its
	// tracks, packed onto as few lines as keep them from overlapping - by the lines they were given there first.
	const int32 NumGroups = FMath::Max(Animation->LayerGroups.Num(), 1);
	for (int32 NestedGroup = 0; NestedGroup < NumGroups; ++NestedGroup)
	{
		FTrackLine& Header = TrackLines.AddDefaulted_GetRef();
		Header.Kind = FTrackLine::EKind::NestedGroup;
		Header.Group = GroupIndex;
		Header.AnimationLayer = LayerIndex;
		Header.NestedGroup = NestedGroup;

		if (IsNestedGroupFolded(LayerIndex, NestedGroup))
		{
			continue;
		}

		TArray<int32> Members;
		for (int32 Index = 0; Index < Animation->Layers.Num() && Spans->IsValidIndex(Index); ++Index)
		{
			if (UWidgetAnimLibrary::GetLayerGroupIndex(Animation->Layers[Index], Animation->LayerGroups.Num()) == NestedGroup
				|| (Animation->LayerGroups.Num() == 0 && NestedGroup == 0))
			{
				Members.Add(Index);
			}
		}
		Members.StableSort([Animation, Spans](int32 A, int32 B)
		{
			const int32 LineA = Animation->Layers[A].EditorLine >= 0 ? Animation->Layers[A].EditorLine : 1000000 + A;
			const int32 LineB = Animation->Layers[B].EditorLine >= 0 ? Animation->Layers[B].EditorLine : 1000000 + B;
			return LineA != LineB ? LineA < LineB : (*Spans)[A].X < (*Spans)[B].X;
		});

		TArray<TArray<int32>> Lines;
		TArray<float> LineEnds;
		TArray<int32> LineKeys;
		for (const int32 Index : Members)
		{
			const FVector2f Span = (*Spans)[Index];
			const int32 Key = Animation->Layers[Index].EditorLine >= 0 ? Animation->Layers[Index].EditorLine : 1000000 + Index;
			int32 Placed = INDEX_NONE;
			for (int32 Candidate = 0; Candidate < Lines.Num() && Placed == INDEX_NONE; ++Candidate)
			{
				// Only onto a line of the same asked-for line, as the animation's own panel packs them.
				if (LineKeys[Candidate] == Key && LineEnds[Candidate] <= Span.X + 0.0005f)
				{
					Placed = Candidate;
				}
			}
			if (Placed == INDEX_NONE)
			{
				Placed = Lines.AddDefaulted();
				LineEnds.Add(0.f);
				LineKeys.Add(Key);
			}
			Lines[Placed].Add(Index);
			LineEnds[Placed] = FMath::Max(LineEnds[Placed], Span.X + Span.Y);
		}

		for (int32 Ordinal = 0; Ordinal < Lines.Num(); ++Ordinal)
		{
			FTrackLine& Lane = TrackLines.AddDefaulted_GetRef();
			Lane.Kind = FTrackLine::EKind::NestedLane;
			Lane.Group = GroupIndex;
			Lane.Ordinal = Ordinal;
			Lane.AnimationLayer = LayerIndex;
			Lane.NestedGroup = NestedGroup;
			Lane.NestedLayers = MoveTemp(Lines[Ordinal]);
		}
	}
}

const UWidgetAnimPresetAsset* SWidgetAnimTimeline::GetInsertedAnimation(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].InsertedAnimation.Get() : nullptr;
}

void SWidgetAnimTimeline::UpdateNestedSpans()
{
	NestedSpans.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers)
	{
		return;
	}

	// Only open ones - and only those that still are animation tracks, after an undo or an edit elsewhere.
	for (auto It = ExpandedAnimationTracks.CreateIterator(); It; ++It)
	{
		if (!GetInsertedAnimation(*It))
		{
			It.RemoveCurrent();
		}
	}

	const float TimeScale = GetTimeScale();
	for (const int32 LayerIndex : ExpandedAnimationTracks)
	{
		const UWidgetAnimPresetAsset* Animation = GetInsertedAnimation(LayerIndex);
		if (!TrackSpans.IsValidIndex(LayerIndex))
		{
			continue;
		}

		// As the animation places its own tracks, staggers as their first item - at its own speed, from where the track
		// starts, in this panel's seconds.
		TArray<float> Starts;
		UWidgetAnimLibrary::PlaceLayerStarts(Animation->Layers, TConstArrayView<float>(), Starts);
		const float Scale = TimeScale / UWidgetAnimLibrary::GetInsertedPlaySpeed((*GetLayers())[LayerIndex]);
		const float TrackStart = TrackSpans[LayerIndex].X;

		TArray<FVector2f>& Spans = NestedSpans.Add(LayerIndex);
		for (int32 Index = 0; Index < Starts.Num(); ++Index)
		{
			Spans.Add(FVector2f(TrackStart + Starts[Index] * Scale, Animation->Layers[Index].GetRunLength() * Scale));
		}
	}
}

void SWidgetAnimTimeline::ToggleAnimationTrackExpanded(int32 LayerIndex)
{
	if (!GetInsertedAnimation(LayerIndex))
	{
		return;
	}
	if (ExpandedAnimationTracks.Contains(LayerIndex))
	{
		ExpandedAnimationTracks.Remove(LayerIndex);
	}
	else
	{
		ExpandedAnimationTracks.Add(LayerIndex);
	}
	RebuildRows();
}

void SWidgetAnimTimeline::ToggleNestedGroupFolded(int32 LayerIndex, int32 NestedGroup)
{
	const uint64 Key = MakeNestedGroupKey(LayerIndex, NestedGroup);
	if (FoldedNestedGroups.Contains(Key))
	{
		FoldedNestedGroups.Remove(Key);
	}
	else
	{
		FoldedNestedGroups.Add(Key);
	}
	RebuildRows();
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

	// A track folded into a collapsed track is shown by the collapsed track.
	if (IsFoldedIntoCollapsed(LayerIndex))
	{
		TArray<int32> Members;
		GetCollapsedMembers(GetLayerCollapsedName(LayerIndex), Members);
		if (Members.Num() > 0)
		{
			LayerIndex = Members[0];
		}
	}

	// A track folded away in the Track Editor is unfolded to be shown.
	const int32 GroupIndex = GetLayerGroup(LayerIndex);
	if (IsTrackGroupCollapsed(GroupIndex))
	{
		ToggleTrackGroupCollapsed(GroupIndex);
	}
	UpdateTrackLayout();

	Tracks->RevealLine(GetLayerLine(LayerIndex));
	const FVector2f Span = GetShownSpan(LayerIndex);
	Tracks->RevealTime(Span.X, Span.X + Span.Y);
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

		// Groups - one or several - with their tracks. Not a plain list's one group, which is the widget itself.
		const TArray<int32> GroupsToDelete = GetSelectedGroups();
		if (GroupsToDelete.Num() > 0 && CanEdit() && Source.IsValid() && Source->SupportsLayerGroups())
		{
			DeleteGroups(GroupsToDelete);
			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	// Ctrl / Cmd + Z, and Ctrl / Cmd + Y or Ctrl / Cmd + Shift + Z: the editor's undo and redo, wherever the panel is docked -
	// a text box in it has the keys first, for its own typing. Not in the middle of an edit (a drag), which is still open.
	if ((Key == EKeys::Z || Key == EKeys::Y) && (InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown()) && !InKeyEvent.IsAltDown())
	{
		if (GEditor && !GEditor->IsTransactionActive())
		{
			const bool bRedo = Key == EKeys::Y || InKeyEvent.IsShiftDown();
			if (bRedo)
			{
				GEditor->RedoTransaction();
			}
			else
			{
				GEditor->UndoTransaction();
			}
		}
		return FReply::Handled();
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
		// A pick in the designer is called off first; then what is selected; then, with nothing selected, a collapsed track
		// open on its own is left, as a collapsed graph is.
		if (PickTargetGroup != INDEX_NONE)
		{
			CancelTargetPick();
			return FReply::Handled();
		}
		// The Graph Editor is left one step at a time too: the keys picked in it, then the graph itself - back to the Track
		// Editor, with the tracks it showed still selected.
		if (IsGraphOpen())
		{
			if (GraphEditor.IsValid() && GraphEditor->HasSelectedKeys())
			{
				GraphEditor->ClearSelectedKeys();
			}
			else
			{
				ExitGraph();
			}
			return FReply::Handled();
		}
		if (IsInCollapsedScope() && SelectedLayers.Num() == 0 && SelectedKeys.Num() == 0 && Selection.Group == INDEX_NONE)
		{
			ExitCollapsed();
			return FReply::Handled();
		}
		ClearSelection();
		return FReply::Handled();
	}

	if (Key == EKeys::F)
	{
		if (IsGraphOpen() && GraphEditor.IsValid())
		{
			GraphEditor->FitView();
		}
		else if (Tracks.IsValid())
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

namespace WidgetAnimCollapsed
{
	/** The name a collapsed track is shown by, made its own: numbered if another collapsed track has it. Ignore is the one being renamed. */
	FName MakeUniqueName(const TArray<FWidgetAnimLayer>& Layers, const FString& Base, FName Ignore = NAME_None, const TSet<FName>& AlsoTaken = TSet<FName>())
	{
		FString Trimmed = Base.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			Trimmed = TEXT("Collapsed Tracks");
		}

		auto IsTaken = [&Layers, &AlsoTaken, Ignore](FName Candidate)
		{
			if (Candidate == Ignore)
			{
				return false;
			}
			return AlsoTaken.Contains(Candidate) || Layers.ContainsByPredicate([Candidate](const FWidgetAnimLayer& Layer) { return Layer.CollapsedTrack == Candidate; });
		};

		FName Candidate(*Trimmed);
		for (int32 Suffix = 2; IsTaken(Candidate); ++Suffix)
		{
			Candidate = FName(*FString::Printf(TEXT("%s %d"), *Trimmed, Suffix));
		}
		return Candidate;
	}
}

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
			EnsureUniqueTrackName(Layers, LayerIndex + 1);

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

	// The groups its animation tracks play their animations in, other than this one - to go too, once nothing else uses
	// them. Where they will be once this group is gone.
	TArray<int32> InsertedGroups;
	for (const int32 InsertedGroup : GetInsertedGroupsOf(Removed))
	{
		if (InsertedGroup != GroupIndex)
		{
			InsertedGroups.Add(InsertedGroup > GroupIndex ? InsertedGroup - 1 : InsertedGroup);
		}
	}

	Layers.RemoveAll([GroupIndex, NumGroups](const FWidgetAnimLayer& Layer)
	{
		return UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups) == GroupIndex;
	});
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, Removed));
	for (FWidgetAnimLayer& Layer : Layers)
	{
		const int32 InGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
		Layer.LayerGroup = InGroup > GroupIndex ? InGroup - 1 : InGroup;

		// An animation track's groups follow theirs too; one whose group is gone plays in the track's own.
		for (int32& InsertedGroup : Layer.InsertedGroups)
		{
			InsertedGroup = InsertedGroup == GroupIndex ? INDEX_NONE : (InsertedGroup > GroupIndex ? InsertedGroup - 1 : InsertedGroup);
		}
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

	RemoveGroupsLeftEmpty(InsertedGroups);

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RebuildRows();
	ClearSelection();
}

void SWidgetAnimTimeline::DeleteGroups(TArray<int32> GroupIndices)
{
	const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
	if (!Current)
	{
		return;
	}
	GroupIndices.RemoveAll([Current](int32 GroupIndex) { return !Current->IsValidIndex(GroupIndex); });
	if (GroupIndices.Num() == 0)
	{
		return;
	}

	// Last first, so the ones still to go keep their places. One step to undo, however many go.
	const FScopedTransaction Transaction(GroupIndices.Num() > 1 ? LOCTEXT("DeleteGroupsTransaction", "Delete Layer Groups") : LOCTEXT("DeleteOneGroupTransaction", "Delete Layer Group"));
	GroupIndices.Sort([](int32 A, int32 B) { return A > B; });
	int32 Previous = INDEX_NONE;
	for (const int32 GroupIndex : GroupIndices)
	{
		if (GroupIndex != Previous)
		{
			DeleteGroup(GroupIndex);
		}
		Previous = GroupIndex;
	}
}

void SWidgetAnimTimeline::SetGroupsFolder(const TArray<int32>& GroupIndices, const FString& Folder)
{
	const TArray<FWidgetAnimLayerGroup>* Current = GetGroups();
	if (!Current || !Source.IsValid() || !Source->SupportsLayerGroups())
	{
		return;
	}

	const FString NewFolder = Folder.TrimStartAndEnd();
	bool bAnyChange = false;
	for (const int32 GroupIndex : GroupIndices)
	{
		bAnyChange |= Current->IsValidIndex(GroupIndex) && GetGroupFolder(GroupIndex) != NewFolder;
	}
	if (!bAnyChange)
	{
		return;
	}

	const FScopedTransaction Transaction(NewFolder.IsEmpty()
		? LOCTEXT("RemoveFromFolderTransaction", "Take Layer Groups Out of List Folder")
		: LOCTEXT("MoveToFolderTransaction", "Put Layer Groups in List Folder"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	for (const int32 GroupIndex : GroupIndices)
	{
		if (Groups.IsValidIndex(GroupIndex))
		{
			Groups[GroupIndex].EditorFolder = NewFolder;
		}
	}

	// Moved into a folder, they show: a folded folder opens for them.
	ListCollapsedFolders.Remove(NewFolder);

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

void SWidgetAnimTimeline::RenameFolder(const FString& Folder, const FString& NewName)
{
	const FString OldFolder = Folder.TrimStartAndEnd();
	const FString NewFolder = NewName.TrimStartAndEnd();
	if (OldFolder.IsEmpty() || NewFolder.IsEmpty() || OldFolder == NewFolder)
	{
		return;
	}

	TArray<int32> Members;
	GetFolderGroups(OldFolder, Members);
	if (Members.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameFolderTransaction", "Rename List Folder"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	for (const int32 GroupIndex : Members)
	{
		Groups[GroupIndex].EditorFolder = NewFolder;
	}

	// Folded or open as it was.
	if (ListCollapsedFolders.Remove(OldFolder) > 0)
	{
		ListCollapsedFolders.Add(NewFolder);
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
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

		// An animation track's groups follow theirs too.
		for (int32& InsertedGroup : Layer.InsertedGroups)
		{
			if (NewPositionOf.IsValidIndex(InsertedGroup))
			{
				InsertedGroup = NewPositionOf[InsertedGroup];
			}
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

void SWidgetAnimTimeline::AddLayer(int32 GroupIndex, EWidgetAnimType AnimType, const FInstancedStruct* InitialTypeParams, TOptional<float> AtSeconds, const FWidgetAnimLayer* Preset)
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

	// As long as the editor preferences' Default Track Length says - in the animation's own seconds, as Duration is.
	NewLayer.Duration = FMath::Max(UWidgetAnimEditorSettings::Get().DefaultTrackLength, MinLayerSeconds);

	// Added inside a collapsed track open on its own: one of its tracks.
	NewLayer.CollapsedTrack = ActiveCollapsed;

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

	// Made from a preset: its type and every setting - placed below exactly as any new track is - and its name.
	if (Preset)
	{
		WidgetAnimTrackPresets::ApplyTo(*Preset, NewLayer);
		NewLayer.TrackName = Preset->TrackName;
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
	EnsureUniqueTrackName(Layers, InsertAt);

	// A keyframed track's keys hold what its type says - and a Shape track's, the settings of the shape it targets, as
	// that shape has them now, so its keys start out as the shape already is; a Design track's, the look of the widget it
	// plays on; a Shape Box track's, the outline of its box.
	FWidgetAnimLayer& Added = Layers[InsertAt];
	if (WidgetAnimTypeIsKeyframed(Added.AnimType))
	{
		Added.Transform.Content = WidgetAnimKeyframeContentFor(Added.AnimType);
		UShapeWidget* TargetShape = nullptr;
		if (Added.AnimType == EWidgetAnimType::Shape && GetShapeTrackTarget(InsertAt, &TargetShape) == EShapeTrackTarget::Shape && TargetShape)
		{
			Added.Transform.SetShapeKind(TargetShape->GetClass(), TargetShape);
		}
		if (Added.AnimType == EWidgetAnimType::Design)
		{
			if (UWidget* DesignTarget = GetDesignTrackTarget(InsertAt))
			{
				Added.Transform.SetDesignKind(DesignTarget->GetClass(), DesignTarget);
			}
		}
		Added.Transform.SyncKeysToCurve();

		// Only the first key uses the widget as it was authored - Use Authored Transform, Shape Box, Shape or Design - so the
		// track starts from the widget as it is; the keys after it hold settings of their own, starting from the widget as
		// it is now, which is where it goes once they are changed. A preset keeps its own.
		if (!Preset)
		{
			const UShapeBox* TargetBox = Added.AnimType == EWidgetAnimType::ShapeBox ? UWidgetAnimLibrary::ResolveShapeBox(Source->GetLayerPreviewWidget(InsertAt)) : nullptr;
			for (int32 KeyIndex = 0; KeyIndex < Added.Transform.Keys.Num(); ++KeyIndex)
			{
				FWidgetTransformKeyframe& Key = Added.Transform.Keys[KeyIndex];
				const bool bFirst = KeyIndex == 0;
				Key.bUseAuthoredShape = bFirst;
				Key.bUseAuthoredTransform = bFirst && Added.AnimType == EWidgetAnimType::Transform;
				if (TargetBox && !bFirst)
				{
					Key.Shape = TargetBox->GetShape();
				}
			}
		}
	}
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
	Copy.bPinnedInEditor = false;

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	Layers.Insert(Copy, LayerIndex + 1);
	EnsureUniqueTrackName(Layers, LayerIndex + 1);

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
	const TArray<int32> InsertedGroups = GetInsertedGroupsOf({ LayerIndex });
	const int32 OldNum = Source->GetLayers()->Num();
	Source->GetLayers()->RemoveAt(LayerIndex);
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, { LayerIndex }));

	// An animation track's own groups - made for its animation's groups when it was inserted - go with it.
	RemoveGroupsLeftEmpty(InsertedGroups);

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
	const TArray<int32> InsertedGroups = GetInsertedGroupsOf(ToDelete);
	const int32 OldNum = Layers.Num();
	for (int32 Index = ToDelete.Num() - 1; Index >= 0; --Index)
	{
		Layers.RemoveAt(ToDelete[Index]);
	}
	PinLayerStarts(OldStarts, OldIndicesAfterRemoving(OldNum, ToDelete));

	// Animation tracks' own groups go with them.
	RemoveGroupsLeftEmpty(InsertedGroups);

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RebuildRows();
	RemapSelectionAfterRemoving(ToDelete);
}

TArray<int32> SWidgetAnimTimeline::GetInsertedGroupsOf(const TArray<int32>& LayerIndices) const
{
	// The groups animation tracks play their animations' groups in, other than the tracks' own groups.
	TArray<int32> Result;
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	for (const int32 LayerIndex : LayerIndices)
	{
		if (!Layers || !Layers->IsValidIndex(LayerIndex) || !(*Layers)[LayerIndex].IsAnimationTrack())
		{
			continue;
		}
		const int32 OwnGroup = GetLayerGroup(LayerIndex);
		for (const int32 InsertedGroup : (*Layers)[LayerIndex].InsertedGroups)
		{
			if (InsertedGroup >= 0 && InsertedGroup != OwnGroup)
			{
				Result.AddUnique(InsertedGroup);
			}
		}
	}
	return Result;
}

int32 SWidgetAnimTimeline::RemoveGroupsLeftEmpty(TArray<int32> Candidates)
{
	TArray<FWidgetAnimLayerGroup>* Groups = Source.IsValid() ? Source->GetGroups() : nullptr;
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Groups || !Layers)
	{
		return 0;
	}

	// From the last, so removing one leaves the indices of those still to look at as they were. A group still holding
	// a track, or still played in by another animation track, stays; so does the last group of all.
	Candidates.Sort(TGreater<int32>());
	int32 NumRemoved = 0;
	for (const int32 GroupIndex : Candidates)
	{
		const int32 NumGroups = Groups->Num();
		if (!Groups->IsValidIndex(GroupIndex) || NumGroups <= 1)
		{
			continue;
		}

		const bool bInUse = Layers->ContainsByPredicate([GroupIndex, NumGroups](const FWidgetAnimLayer& Layer)
		{
			return UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups) == GroupIndex || Layer.InsertedGroups.Contains(GroupIndex);
		});
		if (bInUse)
		{
			continue;
		}

		// Every later group's tracks, and every animation track's groups, follow their groups down one place - as
		// DeleteGroup moves them.
		for (FWidgetAnimLayer& Layer : *Layers)
		{
			const int32 InGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
			Layer.LayerGroup = InGroup > GroupIndex ? InGroup - 1 : InGroup;
			for (int32& InsertedGroup : Layer.InsertedGroups)
			{
				InsertedGroup = InsertedGroup > GroupIndex ? InsertedGroup - 1 : InsertedGroup;
			}
		}
		Groups->RemoveAt(GroupIndex);

		for (TSet<int32>* Folded : { &TrackCollapsedGroups, &ListCollapsedGroups })
		{
			TSet<int32> Shifted;
			for (const int32 FoldedGroup : *Folded)
			{
				if (FoldedGroup != GroupIndex)
				{
					Shifted.Add(FoldedGroup > GroupIndex ? FoldedGroup - 1 : FoldedGroup);
				}
			}
			*Folded = MoveTemp(Shifted);
		}
		if (PickTargetGroup != INDEX_NONE)
		{
			CancelTargetPick();
		}
		++NumRemoved;
	}

	if (NumRemoved > 0 && Selection.Group != INDEX_NONE && Selection.Layer == INDEX_NONE)
	{
		ClearSelection();
	}
	return NumRemoved;
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

	// A collapsed track is copied as one only when it is copied whole, from outside it: every one of its tracks, picked by
	// clicking the collapsed track. Tracks copied from inside a collapsed track open on its own - or only some of a
	// collapsed track's tracks - are copied as the tracks they are, so pasting them anywhere makes plain tracks (or, pasted
	// inside a collapsed track, that one's) rather than a new collapsed track named after where they came from.
	TSet<FName> CopiedWhole;
	if (!IsInCollapsedScope())
	{
		TSet<FName> Seen;
		for (const int32 LayerIndex : Sorted)
		{
			const FName Name = GetLayerCollapsedName(LayerIndex);
			if (Name.IsNone() || Seen.Contains(Name))
			{
				continue;
			}
			Seen.Add(Name);

			TArray<int32> Members;
			GetCollapsedMembers(Name, Members);
			if (Algo::AllOf(Members, [&Sorted](int32 Member) { return Sorted.Contains(Member); }))
			{
				CopiedWhole.Add(Name);
			}
		}
	}

	FString Clipboard = WidgetAnimTrackClipboard::Header;
	int32 NumCopied = 0;
	for (const int32 LayerIndex : Sorted)
	{
		if (!Layers->IsValidIndex(LayerIndex))
		{
			continue;
		}

		FWidgetAnimLayer Copied = (*Layers)[LayerIndex];
		if (!CopiedWhole.Contains(Copied.CollapsedTrack))
		{
			Copied.CollapsedTrack = NAME_None;
		}

		FString Exported;
		FWidgetAnimLayer::StaticStruct()->ExportText(Exported, &Copied, nullptr, nullptr, PPF_Copy, nullptr);
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

	// Pasted inside a collapsed track open on its own: its tracks. Otherwise a collapsed track copied whole comes back as
	// a collapsed track of its own - renamed, when this animation already has one by its name.
	TMap<FName, FName> CollapsedRenames;
	TSet<FName> NewNames;
	for (const WidgetAnimTrackClipboard::FEntry& Entry : Entries)
	{
		const FName Copied = Entry.Layer.CollapsedTrack;
		if (!IsInCollapsedScope() && !Copied.IsNone() && !CollapsedRenames.Contains(Copied))
		{
			const FName Renamed = WidgetAnimCollapsed::MakeUniqueName(Layers, Copied.ToString(), NAME_None, NewNames);
			CollapsedRenames.Add(Copied, Renamed);
			NewNames.Add(Renamed);
		}
	}

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

		// Pasted to be worked on: on lines of their own, unlocked, shown and unpinned.
		Layer.EditorLine = INDEX_NONE;
		Layer.bLockedInEditor = false;
		Layer.bHiddenInEditor = false;
		Layer.bHiddenByFocus = false;
		Layer.bPinnedInEditor = false;
		Layer.bMutedByEditorScope = false;
		if (IsInCollapsedScope())
		{
			Layer.CollapsedTrack = ActiveCollapsed;
		}
		else if (const FName* Renamed = CollapsedRenames.Find(Layer.CollapsedTrack))
		{
			Layer.CollapsedTrack = *Renamed;
		}

		Layers.Insert(Layer, InsertAt + EntryIndex);
		EnsureUniqueTrackName(Layers, InsertAt + EntryIndex);
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

void SWidgetAnimTimeline::SetAnimationTrackFit(int32 LayerIndex, bool bFit)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex) || !(*Current)[LayerIndex].IsAnimationTrack()
		|| (*Current)[LayerIndex].bFitToTrack == bFit)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetAnimationTrackFitTransaction", "Stretch Animation Across Track"));
	if (!BeginEdit())
	{
		return;
	}

	// Stretched, it starts out exactly as long as it played, so nothing moves until its edges are dragged.
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];
	if (bFit)
	{
		Layer.Duration = Layer.GetRunLength();
	}
	Layer.bFitToTrack = bFit;
	TArray<int32> SameIndices;
	for (int32 Index = 0; Index < OldStarts.Num(); ++Index)
	{
		SameIndices.Add(Index);
	}
	PinLayerStarts(OldStarts, SameIndices);

	// The layout signature brings the Selection panel along next frame: this comes from inside it.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
}

namespace WidgetAnimTimeline
{
	/** Whether a Shape track already keys Kind's settings, every key laid out for them. */
	bool IsShapeTrackLaidOutFor(const FWidgetTransformAnim& Anim, const UClass* Kind)
	{
		return Anim.ShapeKind.Get() == Kind && Algo::AllOf(Anim.Keys, [Kind](const FWidgetTransformKeyframe& Key)
		{
			return Kind ? WidgetShapeAnimation::IsLaidOutFor(Key.ShapeValues, Kind) : Key.ShapeValues.GetPropertyBagStruct() == nullptr;
		});
	}
}

SWidgetAnimTimeline::EShapeTrackTarget SWidgetAnimTimeline::GetShapeTrackTarget(int32 LayerIndex, UShapeWidget** OutShape) const
{
	if (OutShape)
	{
		*OutShape = nullptr;
	}

	UWidget* Target = Source.IsValid() ? Source->GetLayerPreviewWidget(LayerIndex) : nullptr;
	const bool bOwnWidget = !Target || !Source.IsValid() || Source->IsLayerOnOwnWidget(LayerIndex);

	// A shape - or, for a widget the group names, the shape inside it the track plays on, as it does at runtime. The
	// widget the animation is put on is only known when it is a shape itself; otherwise the kind is picked on the track.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const UClass* Kind = Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].Transform.ShapeKind.Get() : nullptr;
	UShapeWidget* Shape = Cast<UShapeWidget>(Target);
	if (!Shape && !bOwnWidget)
	{
		Shape = WidgetAnimShapeTrack::ResolveShape(Target, Kind);
	}
	if (Shape)
	{
		if (OutShape)
		{
			*OutShape = Shape;
		}
		return EShapeTrackTarget::Shape;
	}
	return bOwnWidget ? EShapeTrackTarget::OwnWidget : EShapeTrackTarget::NotAShape;
}

void SWidgetAnimTimeline::SetShapeTrackKind(int32 LayerIndex, UClass* Kind)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex) || (*Current)[LayerIndex].AnimType != EWidgetAnimType::Shape
		|| WidgetAnimTimeline::IsShapeTrackLaidOutFor((*Current)[LayerIndex].Transform, Kind))
	{
		return;
	}

	const FScopedTransaction Transaction(WidgetAnimTips::SetShapeTrackKindTransaction());
	if (!BeginEdit())
	{
		return;
	}

	// Values the keys don't have yet start from the shape it targets, when that is a shape of this kind.
	UShapeWidget* TargetShape = nullptr;
	GetShapeTrackTarget(LayerIndex, &TargetShape);
	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];
	Layer.Transform.Content = EWidgetKeyframeContent::Shape;
	Layer.Transform.SetShapeKind(Kind, TargetShape && Kind && TargetShape->IsA(Kind) ? TargetShape : nullptr);

	// The layout signature brings the Selection panel along next frame, with the new kind's fields.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

void SWidgetAnimTimeline::MatchShapeTrackToTarget(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (IsReadOnly() || !Current || !Current->IsValidIndex(LayerIndex) || (*Current)[LayerIndex].AnimType != EWidgetAnimType::Shape || !CanEditLayer(LayerIndex))
	{
		return;
	}

	UShapeWidget* TargetShape = nullptr;
	if (GetShapeTrackTarget(LayerIndex, &TargetShape) == EShapeTrackTarget::Shape && TargetShape)
	{
		if (!WidgetAnimTimeline::IsShapeTrackLaidOutFor((*Current)[LayerIndex].Transform, TargetShape->GetClass()))
		{
			SetShapeTrackKind(LayerIndex, TargetShape->GetClass());
		}
	}
}

UWidget* SWidgetAnimTimeline::GetDesignTrackTarget(int32 LayerIndex) const
{
	// The widget it plays on, as it finds it at runtime: its group's target - or the widget itself - or, of the kind its keys
	// were made for, the first inside it.
	UWidget* Target = Source.IsValid() ? Source->GetLayerPreviewWidget(LayerIndex) : nullptr;
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const UClass* Kind = Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].Transform.DesignKind.Get() : nullptr;
	return WidgetAnimDesignTrack::ResolveWidget(Target, Kind);
}

void SWidgetAnimTimeline::MatchDesignTrackToTarget(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (IsReadOnly() || !Current || !Current->IsValidIndex(LayerIndex) || (*Current)[LayerIndex].AnimType != EWidgetAnimType::Design || !CanEditLayer(LayerIndex))
	{
		return;
	}

	UWidget* Target = GetDesignTrackTarget(LayerIndex);
	const FWidgetTransformAnim& Anim = (*Current)[LayerIndex].Transform;
	if (!Target || (Anim.DesignKind.Get() == WidgetDesignAnimation::GetDesignKind(Target->GetClass()) && Anim.Content == EWidgetKeyframeContent::Design
		&& Algo::AllOf(Anim.Keys, [&Anim](const FWidgetTransformKeyframe& Key) { return WidgetDesignAnimation::IsLaidOutFor(Key.ShapeValues, Anim.DesignKind.Get()); })))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("MatchDesignTrackTransaction", "Match Design Track to Its Widget"));
	if (!BeginEdit())
	{
		return;
	}

	// Values the keys don't have yet start from the widget as it is now.
	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];
	Layer.Transform.Content = EWidgetKeyframeContent::Design;
	Layer.Transform.SetDesignKind(Target->GetClass(), Target);

	// The layout signature brings the Selection panel along next frame, with the new kind's fields.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

void SWidgetAnimTimeline::SyncBlueprintTypeSettings(int32 LayerIndex)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (IsReadOnly() || !Current || !Current->IsValidIndex(LayerIndex) || (*Current)[LayerIndex].AnimType != EWidgetAnimType::Custom || !CanEditLayer(LayerIndex))
	{
		return;
	}

	// Checked in place, every frame the track is selected: only a track whose Settings are out of step is copied and edited.
	const FInstancedStruct& Params = (*Current)[LayerIndex].TypeParams;
	const FWidgetAnimBlueprintTypeParams* TypeParams = Params.GetPtr<FWidgetAnimBlueprintTypeParams>();
	const FWidgetAnimBlueprintCueParams* CueParams = Params.GetPtr<FWidgetAnimBlueprintCueParams>();
	if ((!TypeParams || TypeParams->IsSettingsStructCurrent()) && (!CueParams || CueParams->IsSettingsStructCurrent()))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SyncBlueprintSettingsTransaction", "Match Track Settings to Its Blueprint Type"));
	if (!BeginEdit())
	{
		return;
	}

	FInstancedStruct& Edited = (*Source->GetLayers())[LayerIndex].TypeParams;
	if (FWidgetAnimBlueprintTypeParams* EditedType = Edited.GetMutablePtr<FWidgetAnimBlueprintTypeParams>())
	{
		EditedType->SyncSettingsStruct();
	}
	else if (FWidgetAnimBlueprintCueParams* EditedCue = Edited.GetMutablePtr<FWidgetAnimBlueprintCueParams>())
	{
		EditedCue->SyncSettingsStruct();
	}

	// The layout signature brings the Selection panel along next frame, with the struct's fields.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

void SWidgetAnimTimeline::SetAnimationTrackValueDriven(int32 LayerIndex, bool bValueDriven)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex) || !(*Current)[LayerIndex].IsAnimationTrack()
		|| (*Current)[LayerIndex].IsValueDriven() == bValueDriven)
	{
		return;
	}

	const FScopedTransaction Transaction(bValueDriven
		? LOCTEXT("AnimationTrackValueTransaction", "Animation Track Follows a Value")
		: LOCTEXT("AnimationTrackClockTransaction", "Animation Track Plays Across the Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	FWidgetAnimLayer& Layer = Layers[LayerIndex];

	// Stretched across the track either way from here on, at the length it has now.
	if (!Layer.FitsAnimationToTrack())
	{
		Layer.Duration = Layer.GetRunLength();
	}
	Layer.bFitToTrack = true;

	if (bValueDriven)
	{
		// A progress usually arrives already smooth - a hold timer, a slider: jumped to unless the track says otherwise.
		Layer.Timing = EWidgetAnimTiming::Value;
		Layer.ValueSmoothing = EWidgetAnimValueSmoothing::Immediate;
		Layer.ValueOptions.Kind = EWidgetAnimValueKind::Float;
		UWidgetAnimLibrary::AssignDefaultValueNames(Layers);
	}
	else
	{
		Layer.Timing = EWidgetAnimTiming::Tween;
	}
	TArray<int32> SameIndices;
	for (int32 Index = 0; Index < OldStarts.Num(); ++Index)
	{
		SameIndices.Add(Index);
	}
	PinLayerStarts(OldStarts, SameIndices);

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
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

void SWidgetAnimTimeline::InsertAnimation(const UWidgetAnimPresetAsset* Animation, int32 GroupIndex, TOptional<float> AtSeconds)
{
	// Never this animation, nor one that plays it - however deep - which would play itself for good.
	const UObject* Edited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
	if (!Animation || Animation->Layers.Num() == 0 || !Edited || UWidgetAnimLibrary::AnimationPlays(Animation, Edited))
	{
		if (Animation && Edited && UWidgetAnimLibrary::AnimationPlays(Animation, Edited))
		{
			FNotificationInfo Info(FText::Format(LOCTEXT("InsertAnimationLoop", "{0} can't be inserted: it plays this animation, which would then play itself."), FText::FromString(Animation->GetName())));
			Info.ExpireDuration = 5.f;
			FSlateNotificationManager::Get().AddNotification(Info);
		}
		return;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("InsertAnimationTransaction", "Insert {0}"), FText::FromString(Animation->GetName())));
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
	GroupIndex = GetDefaultGroup(GroupIndex);

	FWidgetAnimLayer Track;
	Track.AnimType = EWidgetAnimType::Custom;
	Track.InsertedAnimation = const_cast<UWidgetAnimPresetAsset*>(Animation);
	Track.LayerGroup = GroupIndex;
	Track.CollapsedTrack = ActiveCollapsed;

	// Into a percentage timeline, any animation is stretched across the track - it has no seconds to play in - and runs to
	// 100% from where it starts (a quarter of it, at the least). A percentage animation into a timeline in seconds plays over
	// one second, following a float value of its own: Set Widget Animation Value by its Value Name moves it along.
	if (IsPercentTime())
	{
		const float StartsAt = GetNewTrackStart(AtSeconds) / FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
		Track.bFitToTrack = true;
		Track.Duration = FMath::Clamp(1.f - StartsAt, 0.25f, 1.f);
	}
	else if (Animation->IsPercentTime())
	{
		Track.bFitToTrack = true;
		Track.Duration = 1.f;
		Track.Timing = EWidgetAnimTiming::Value;
		Track.ValueSmoothing = EWidgetAnimValueSmoothing::Immediate;
		Track.ValueOptions.Kind = EWidgetAnimValueKind::Float;
	}

	// Where the inserted animation's groups play. One group - a single-widget animation - plays in the group it was
	// inserted into, and follows the track to another. Several get groups of their own here, named after the
	// animation, so each widget it moves can be pointed at its own - as every use of this animation will. A plain list
	// of tracks has one widget, so every group plays on it.
	const int32 SourceGroups = Animation->LayerGroups.Num();
	if (SourceGroups > 1 && Source->SupportsLayerGroups())
	{
		for (int32 SourceIndex = 0; SourceIndex < SourceGroups; ++SourceIndex)
		{
			FWidgetAnimLayerGroup NewGroup = Animation->LayerGroups[SourceIndex];
			NewGroup.Layers.Reset();
			NewGroup.GroupName = FString::Printf(TEXT("%s %s"), *Animation->GetName(), *Animation->LayerGroups[SourceIndex].GetDisplayName(SourceIndex));
			Track.InsertedGroups.Add(Groups.Add(NewGroup));
		}
	}

	// Where a new track would go, as a track added by hand is placed: at the menu's time when it was opened on the Track
	// Editor, else following the one above it. Everything already there stays where it is in time.
	const int32 InsertAt = FMath::Clamp(GetInsertIndex(), 0, Layers.Num());
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	if (AtSeconds.IsSet())
	{
		const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
		const float Authored = FMath::RoundToFloat(FMath::Max(AtSeconds.GetValue(), 0.f) / TimeScale * 100.f) / 100.f;
		const float AnchorStart = OldStarts.IsValidIndex(InsertAt - 1) ? OldStarts[InsertAt - 1] : 0.f;
		Track.Start = EWidgetAnimLayerStart::WithPrevious;
		Track.Delay = Authored - AnchorStart;
	}
	else if (InsertAt > 0)
	{
		Track.Start = EWidgetAnimLayerStart::AfterPrevious;
	}
	Layers.Insert(Track, InsertAt);
	PinLayerStarts(OldStarts, OldIndicesAfterInserting(Layers.Num(), InsertAt, 1));
	if (Track.IsValueDriven())
	{
		UWidgetAnimLibrary::AssignDefaultValueNames(Layers);
	}
	TrackCollapsedGroups.Remove(GroupIndex);
	ListCollapsedGroups.Remove(GroupIndex);

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();
	SelectLayer(InsertAt);
}

namespace WidgetAnimUnravel
{
	/**
	 * A copy of an inserted animation's track, made to play in this animation's seconds as it played inside the track:
	 * its times divided by how fast the track played it (its Speed Multiplier, or stretched across the track) - Duration,
	 * Delay, a Transform's keys, a spring's frequency, a stagger's spacing. Keys along a track are shares of its run, and
	 * follow its Duration. See UWidgetAnimLibrary::RetimeLayer.
	 */
	void Retime(FWidgetAnimLayer& Layer, float Speed)
	{
		UWidgetAnimLibrary::RetimeLayer(Layer, Speed, SWidgetAnimTimeline::MinLayerSeconds);
	}
}

bool SWidgetAnimTimeline::CanUnravelLayer(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return CanEditLayer(LayerIndex) && (*Layers)[LayerIndex].IsAnimationTrack() && (*Layers)[LayerIndex].InsertedAnimation->Layers.Num() > 0;
}

void SWidgetAnimTimeline::UnravelAnimationTrack(int32 LayerIndex)
{
	if (!CanUnravelLayer(LayerIndex))
	{
		return;
	}

	const FWidgetAnimLayer Track = (*GetLayers())[LayerIndex];
	const UWidgetAnimPresetAsset* Animation = Track.InsertedAnimation;

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("UnravelTransaction", "Unravel {0}"), FText::FromString(Animation->GetName())));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	const int32 NumGroups = Groups.Num();
	const int32 SourceGroups = Animation->LayerGroups.Num();

	// Each of the inserted animation's groups plays where the track played it.
	auto MapGroup = [&Track, NumGroups](int32 SourceGroup)
	{
		const int32 Mapped = Track.InsertedGroups.IsValidIndex(SourceGroup) && Track.InsertedGroups[SourceGroup] >= 0 ? Track.InsertedGroups[SourceGroup] : Track.LayerGroup;
		return NumGroups > 0 ? FMath::Clamp(Mapped, 0, NumGroups - 1) : 0;
	};

	// Where the track started, and each group's lines, before anything moves: the copies start where the track did, and
	// go on lines of their own below whatever each group already has.
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	const float TrackStart = OldStarts.IsValidIndex(LayerIndex) ? OldStarts[LayerIndex] : 0.f;
	const float AnchorStart = OldStarts.IsValidIndex(LayerIndex - 1) ? OldStarts[LayerIndex - 1] : 0.f;
	TArray<int32> LineOffsets;
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		LineOffsets.Add(GetGroupLineCount(GroupIndex));
	}

	// Collapsed tracks of the animation keep to themselves here - renamed, where this animation already has one by the name.
	// Unraveled from inside a collapsed track, every copy is one of its tracks.
	TMap<FName, FName> CollapsedRenames;
	TSet<FName> NewNames;
	if (Track.CollapsedTrack.IsNone())
	{
		for (const FWidgetAnimLayer& Inner : Animation->Layers)
		{
			if (!Inner.CollapsedTrack.IsNone() && !CollapsedRenames.Contains(Inner.CollapsedTrack))
			{
				const FName Renamed = WidgetAnimCollapsed::MakeUniqueName(Layers, Inner.CollapsedTrack.ToString(), NAME_None, NewNames);
				CollapsedRenames.Add(Inner.CollapsedTrack, Renamed);
				NewNames.Add(Renamed);
			}
		}
	}

	// As fast as it played inside the track - stretched across it, or at its own Speed Multiplier.
	const float Speed = UWidgetAnimLibrary::GetInsertedPlaySpeed(Track);
	TArray<FWidgetAnimLayer> Copies;
	for (int32 Index = 0; Index < Animation->Layers.Num(); ++Index)
	{
		FWidgetAnimLayer Copy = Animation->Layers[Index];
		const int32 Group = MapGroup(UWidgetAnimLibrary::GetLayerGroupIndex(Copy, SourceGroups));
		Copy.LayerGroup = Group;
		Copy.EditorLine = Copy.EditorLine >= 0 && LineOffsets.IsValidIndex(Group) ? Copy.EditorLine + LineOffsets[Group] : INDEX_NONE;
		Copy.bLockedInEditor = Track.bLockedInEditor;
		Copy.bHiddenInEditor = Copy.bHiddenInEditor || Track.bHiddenInEditor;
		Copy.bHiddenByFocus = false;
		Copy.bPinnedInEditor = Track.bPinnedInEditor;
		Copy.bMutedByEditorScope = false;
		if (!Track.CollapsedTrack.IsNone())
		{
			Copy.CollapsedTrack = Track.CollapsedTrack;
		}
		else if (const FName* Renamed = CollapsedRenames.Find(Copy.CollapsedTrack))
		{
			Copy.CollapsedTrack = *Renamed;
		}
		Copy.InsertedFrom = Animation->GetName();
		WidgetAnimUnravel::Retime(Copy, Speed);

		// An animation inserted into this one as a track stays one, its groups now this animation's.
		for (int32& InsertedGroup : Copy.InsertedGroups)
		{
			InsertedGroup = InsertedGroup >= 0 && InsertedGroup < SourceGroups ? MapGroup(InsertedGroup) : INDEX_NONE;
		}

		Copies.Add(MoveTemp(Copy));
	}

	// The first starts with the track above, as far after it as the track started, plus its own delay into the
	// animation; the rest follow it as they followed each other.
	Copies[0].Start = EWidgetAnimLayerStart::WithPrevious;
	Copies[0].Delay = TrackStart - AnchorStart + FMath::Max(Copies[0].Delay, 0.f);

	Layers.RemoveAt(LayerIndex);
	Layers.Insert(Copies, LayerIndex);
	for (int32 Copied = 0; Copied < Copies.Num(); ++Copied)
	{
		EnsureUniqueTrackName(Layers, LayerIndex + Copied);
	}

	// Everything that was there stays exactly where it was in time; the track is gone, the copies placed by their own.
	TArray<int32> OldIndexOf;
	for (int32 NewIndex = 0; NewIndex < Layers.Num(); ++NewIndex)
	{
		OldIndexOf.Add(NewIndex < LayerIndex ? NewIndex : (NewIndex < LayerIndex + Copies.Num() ? INDEX_NONE : NewIndex - Copies.Num() + 1));
	}
	PinLayerStarts(OldStarts, OldIndexOf);
	ExpandedAnimationTracks.Reset();

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();

	TArray<int32> NewLayers;
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		NewLayers.Add(LayerIndex + Index);
	}
	SelectInBox({}, NewLayers, /*bAdd=*/false, /*bToggle=*/false);
}

void SWidgetAnimTimeline::AddKey(int32 LayerIndex, float LocalSeconds)
{
	if (!CanEditKeys(LayerIndex))
	{
		return;
	}

	FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];

	if (!WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
		NewKey.ValueY = FMath::Lerp(WidgetAnimKeyPoints::GetValueY(Layer, SampleSegment), WidgetAnimKeyPoints::GetValueY(Layer, SampleSegment + 1), ValueAlpha);
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

	const FScopedTransaction Transaction(LOCTEXT("AddKeyTransaction", "Add Key"));
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

	// Its settings now rather than at the next sync: where the track already is at that moment, held as settings of its own.
	// Only a track's first key uses the widget as authored (Use Authored Transform, Shape Box, Shape or Design) - a key added
	// after it never does, even beside a key that does: such a neighbor is read off the preview's widget instead, so the new
	// key still starts exactly where the track already was.
	Layer.Transform.SyncKeysToCurve();
	TArray<FWidgetTransformKeyframe>& Keyframes = Layer.Transform.Keys;
	const int32 AddedIndex = Keyframes.IndexOfByPredicate([Time](const FWidgetTransformKeyframe& Key) { return FMath::IsNearlyEqual(Key.Time, Time, WidgetAnimTimeline::MinKeySpacing); });
	if (AddedIndex > 0)
	{
		const int32 AfterIndex = Keyframes.IsValidIndex(AddedIndex + 1) ? AddedIndex + 1 : AddedIndex - 1;
		const FWidgetTransformKeyframe& Before = Keyframes[AddedIndex - 1];
		const FWidgetTransformKeyframe& After = Keyframes[AfterIndex];
		const float Span = After.Time - Before.Time;
		const float Alpha = Span > KINDA_SMALL_NUMBER ? FMath::Clamp((Time - Before.Time) / Span, 0.f, 1.f) : 0.f;

		FWidgetTransformKeyframe Added = Keyframes[AddedIndex];
		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Transform:
		{
			const FWidgetTransform From = Layer.Transform.GetKeyTransform(AddedIndex - 1);
			const FWidgetTransform To = Layer.Transform.GetKeyTransform(AfterIndex);
			Added.Transform.Translation = FMath::Lerp(From.Translation, To.Translation, Alpha);
			Added.Transform.Scale = FMath::Lerp(From.Scale, To.Scale, Alpha);
			Added.Transform.Shear = FMath::Lerp(From.Shear, To.Shear, Alpha);
			Added.Transform.Angle = FMath::Lerp(From.Angle, To.Angle, Alpha);
			break;
		}

		case EWidgetAnimType::ShapeBox:
		{
			const UShapeBox* Box = UWidgetAnimLibrary::ResolveShapeBox(Source->GetLayerPreviewWidget(LayerIndex));
			auto ShapeOf = [Box](const FWidgetTransformKeyframe& Key) { return Key.bUseAuthoredShape && Box ? Box->GetShape() : Key.Shape; };
			Added.Shape = FShapeBoxShape::Lerp(ShapeOf(Before), ShapeOf(After), Alpha);
			break;
		}

		case EWidgetAnimType::Shape:
		case EWidgetAnimType::Design:
		{
			UShapeWidget* TargetShape = nullptr;
			const UObject* Authored = Layer.AnimType == EWidgetAnimType::Design
				? static_cast<const UObject*>(GetDesignTrackTarget(LayerIndex))
				: (GetShapeTrackTarget(LayerIndex, &TargetShape) == EShapeTrackTarget::Shape ? TargetShape : nullptr);
			auto BagOf = [Authored](const FWidgetTransformKeyframe& Key)
			{
				FInstancedPropertyBag Bag = Key.ShapeValues;
				if (Key.bUseAuthoredShape && Authored)
				{
					WidgetShapeAnimation::Capture(Bag, Authored);
				}
				return Bag;
			};
			Added.ShapeValues = WidgetShapeAnimation::LerpBags(BagOf(Before), BagOf(After), Alpha);
			break;
		}

		default:
			break;
		}

		Added.bUseAuthoredShape = false;
		Added.bUseAuthoredTransform = false;
		Keyframes[AddedIndex] = MoveTemp(Added);
	}

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

	if (!WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
		if (!WidgetAnimTypeIsKeyframed(Layer.AnimType))
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

	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
		: WidgetAnimTypeIsKeyframed((*GetLayers())[LayerIndex].AnimType)
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

bool SWidgetAnimTimeline::CanDragKeyPosition(int32 LayerIndex, int32 KeyIndex) const
{
	if (!CanEditLayer(LayerIndex))
	{
		return false;
	}
	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	return !Layer.IsAnimationTrack()
		&& Layer.AnimType == EWidgetAnimType::Transform
		&& Layer.Transform.IsAnimatingTransform()
		&& Layer.Transform.Keys.IsValidIndex(KeyIndex);
}

bool SWidgetAnimTimeline::BeginKeyHandleDrag(int32 LayerIndex, int32 KeyIndex)
{
	if (!BeginKeyPositionDrag(LayerIndex, KeyIndex))
	{
		return false;
	}
	if (KeyPositionTransaction.IsValid())
	{
		// One drag, one undo step: named for what it does.
		KeyPositionTransaction->Cancel();
		KeyPositionTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("EditPathHandlesTransaction", "Edit Path Handles"));
		BeginEdit();
	}
	return true;
}

void SWidgetAnimTimeline::UpdateKeyHandleDrag(const FVector2D& HandleIn, const FVector2D& HandleOut)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!IsDraggingKeyPosition() || !Layers || !Layers->IsValidIndex(KeyPositionLayer))
	{
		return;
	}
	FWidgetAnimLayer& Layer = (*Layers)[KeyPositionLayer];
	if (!Layer.Transform.Keys.IsValidIndex(KeyPositionKey))
	{
		return;
	}

	auto Round = [](const FVector2D& Value) { return FVector2D(FMath::RoundToDouble(Value.X * 10.0) / 10.0, FMath::RoundToDouble(Value.Y * 10.0) / 10.0); };
	FWidgetTransformKeyframe& Key = Layer.Transform.Keys[KeyPositionKey];
	const FVector2D NewIn = Round(HandleIn);
	const FVector2D NewOut = Round(HandleOut);
	if (Key.PathShape == EWidgetKeyPathShape::Bezier && Key.PathHandleIn.Equals(NewIn, 1.e-4) && Key.PathHandleOut.Equals(NewOut, 1.e-4))
	{
		return;
	}

	Key.PathShape = EWidgetKeyPathShape::Bezier;
	Key.PathHandleIn = NewIn;
	Key.PathHandleOut = NewOut;
	bKeyPositionMoved = true;

	NotifyGroupsChanged(EPropertyChangeType::Interactive);
	FocusPreviewOnKey(KeyPositionLayer, KeyPositionKey);
}

void SWidgetAnimTimeline::SetKeyPathShape(int32 LayerIndex, const TArray<int32>& KeyIndices, EWidgetKeyPathShape Shape)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!CanEditLayer(LayerIndex) || !Layers || IsDragging())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetKeyPathTransaction", "Set Key Path"));
	if (!BeginEdit())
	{
		return;
	}

	FWidgetTransformAnim& Anim = (*Layers)[LayerIndex].Transform;

	// Every handle worked out before any key changes, as the path is now, so the keys changed together keep its shape.
	TArray<TPair<FVector2D, FVector2D>> Handles;
	for (const int32 KeyIndex : KeyIndices)
	{
		FVector2D HandleIn, HandleOut;
		Anim.GetPathHandles(KeyIndex, HandleIn, HandleOut);
		Handles.Emplace(HandleIn, HandleOut);
	}
	for (int32 Index = 0; Index < KeyIndices.Num(); ++Index)
	{
		if (!Anim.Keys.IsValidIndex(KeyIndices[Index]))
		{
			continue;
		}
		FWidgetTransformKeyframe& Key = Anim.Keys[KeyIndices[Index]];
		Key.PathShape = Shape;
		Key.PathHandleIn = Shape == EWidgetKeyPathShape::Bezier ? Handles[Index].Key : FVector2D::ZeroVector;
		Key.PathHandleOut = Shape == EWidgetKeyPathShape::Bezier ? Handles[Index].Value : FVector2D::ZeroVector;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetails();
}

void SWidgetAnimTimeline::SetLayerEasingSpan(int32 LayerIndex, EWidgetKeyEasingSpan Span)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!CanEditLayer(LayerIndex) || !Layers || IsDragging() || (*Layers)[LayerIndex].Transform.EasingSpan == Span)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetEasingAcrossTransaction", "Set Easing Across"));
	if (!BeginEdit())
	{
		return;
	}
	(*Layers)[LayerIndex].Transform.EasingSpan = Span;

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetails();
}

bool SWidgetAnimTimeline::BeginKeyPositionDrag(int32 LayerIndex, int32 KeyIndex)
{
	if (!CanDragKeyPosition(LayerIndex, KeyIndex) || IsDragging())
	{
		return false;
	}

	const FWidgetAnimLayer& Layer = (*GetLayers())[LayerIndex];
	KeyPositionLayer = LayerIndex;
	KeyPositionKey = KeyIndex;
	KeyPositionStartPose = Layer.Transform.GetKeyTransform(KeyIndex);
	KeyPositionStart = KeyPositionStartPose.Translation;
	bKeyPositionMoved = false;

	// One undo step for the whole drag, however many frames it moves through.
	KeyPositionTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveKeyPositionTransaction", "Move Key on Motion Path"));
	if (!BeginEdit())
	{
		KeyPositionTransaction->Cancel();
		KeyPositionTransaction.Reset();
		KeyPositionLayer = INDEX_NONE;
		KeyPositionKey = INDEX_NONE;
		return false;
	}

	// The key picked, and the preview held on it - what it looks like there is what the drag changes.
	SelectKey(LayerIndex, KeyIndex);
	return true;
}

void SWidgetAnimTimeline::UpdateKeyPositionDrag(const FVector2D& Delta)
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!IsDraggingKeyPosition() || !Layers || !Layers->IsValidIndex(KeyPositionLayer))
	{
		return;
	}
	FWidgetAnimLayer& Layer = (*Layers)[KeyPositionLayer];
	if (!Layer.Transform.Keys.IsValidIndex(KeyPositionKey))
	{
		return;
	}

	// Rounded to a tenth, as a value typed in would be - small enough to place anything, without long fractions in the Details.
	const FVector2D Wanted = KeyPositionStart + Delta;
	const FVector2D Rounded(FMath::RoundToDouble(Wanted.X * 10.0) / 10.0, FMath::RoundToDouble(Wanted.Y * 10.0) / 10.0);

	FWidgetTransformKeyframe& Key = Layer.Transform.Keys[KeyPositionKey];
	if (!Key.bUseAuthoredTransform && Key.Transform.Translation.Equals(Rounded, 1.e-4))
	{
		return;
	}

	// Somewhere of its own now: the rest of its pose kept as it played.
	Key.Transform = KeyPositionStartPose;
	Key.Transform.Translation = Rounded;
	Key.bUseAuthoredTransform = false;
	bKeyPositionMoved = true;

	// Interactive, as a key's time dragged is: the preview follows live, held on the key.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
	FocusPreviewOnKey(KeyPositionLayer, KeyPositionKey);
}

void SWidgetAnimTimeline::EndKeyPositionDrag()
{
	if (!IsDraggingKeyPosition())
	{
		return;
	}

	if (bKeyPositionMoved)
	{
		NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	}
	else
	{
		// A click with no move: nothing to undo.
		KeyPositionTransaction->Cancel();
	}
	KeyPositionTransaction.Reset();
	KeyPositionLayer = INDEX_NONE;
	KeyPositionKey = INDEX_NONE;
	bKeyPositionMoved = false;

	// The key's Transform row changed; the Selection panel shows it fresh.
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

		// The animation's own start or end: see BeginRangeDrag.
		default:
			break;
		}
	}

	// The one grabbed has to be among them, or the drag would move everything but what is under the mouse.
	if (!State.StartEdges.Contains(PressedLayer) && !State.EndEdges.Contains(PressedLayer))
	{
		return false;
	}

	// A track's end can be pulled out past the animation's end, making the animation longer - the animation is as long
	// as its tracks, or its Minimum Length where that is longer. (Only a track that already ended exactly there could, once: one whose stagger, or the animation it
	// plays within, reached further never counted as ending there, and couldn't be lengthened at all.) A move stays
	// inside the animation as it is.
	State.bEndIsFree = Mode == ELayerDrag::ResizeEnd;

	// A percentage timeline ends at 100%, however its tracks are pulled.
	if (IsPercentTime())
	{
		State.bEndIsFree = false;
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

	// The edges as bits, for every frame of the drag. See FLayerDragState::StartBits.
	State.StartBits.Init(false, NumLayers);
	State.EndBits.Init(false, NumLayers);
	for (const int32 LayerIndex : State.StartEdges)
	{
		if (State.StartBits.IsValidIndex(LayerIndex))
		{
			State.StartBits[LayerIndex] = true;
		}
	}
	for (const int32 LayerIndex : State.EndEdges)
	{
		if (State.EndBits.IsValidIndex(LayerIndex))
		{
			State.EndBits[LayerIndex] = true;
		}
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

	// How far back a move can go before its tracks reach the animation's start - only a limit when the start is fixed;
	// with bStartIsFree, going further moves everything else later instead.
	float StartFloor = -MaxSeconds;

	TSet<int32> Moving(State.StartEdges);
	Moving.Append(State.EndEdges);

	for (const int32 LayerIndex : Moving)
	{
		const bool bStart = State.IsStartEdge(LayerIndex);
		const bool bEnd = State.IsEndEdge(LayerIndex);
		const float Start = State.OriginalStarts[LayerIndex];
		const float Length = State.OriginalLengths[LayerIndex];
		const float End = Start + Length;

		// Nothing before the animation starts.
		if (bStart)
		{
			StartFloor = FMath::Max(StartFloor, -Start);
			if (!State.bStartIsFree)
			{
				Lowest = FMath::Max(Lowest, -Start);
			}
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

		// Nothing past the animation's end, unless a track ending there is being lengthened - or a keyboard move may make
		// the animation longer (bEndIsFree).
		if (bEnd && !State.bEndIsFree)
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
			if (State.IsStartEdge(LayerIndex))
			{
				Edges.Add(State.OriginalStarts[LayerIndex] + DeltaSeconds);
			}
			if (State.IsEndEdge(LayerIndex))
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

	// Taken before the animation's start: the moved tracks stop at it, and everything else moves later by what is left -
	// the same picture, the moved tracks ahead of the rest, with nothing before 0.
	float LeadIn = 0.f;
	if (State.bStartIsFree && Delta < StartFloor)
	{
		LeadIn = StartFloor - Delta;
		Delta = StartFloor;
	}

	TArray<float> NewStarts = State.OriginalStarts;
	TArray<float> NewLengths = State.OriginalLengths;
	if (LeadIn > 0.f)
	{
		for (int32 LayerIndex = 0; LayerIndex < NewStarts.Num(); ++LayerIndex)
		{
			if (!Moving.Contains(LayerIndex))
			{
				NewStarts[LayerIndex] += LeadIn;
			}
		}
	}

	// A Minimum Length moves later with the rest, so the animation's end stays where it was against its tracks.
	if (State.bStartIsFree && Source.IsValid() && Source->CanSetMinimumLength() && State.OriginalMinimumLength > 0.f)
	{
		Source->SetMinimumLength(State.OriginalMinimumLength + LeadIn / FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER));
	}
	for (const int32 LayerIndex : Moving)
	{
		const bool bStart = State.IsStartEdge(LayerIndex);
		const bool bEnd = State.IsEndEdge(LayerIndex);

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
		// Every layer's collapsed track, group and view read once. Asked one layer at a time, each asked about every other
		// one - which is the first of its collapsed track, whether its whole group is collapsed - so a move in an animation
		// of hundreds of tracks cost the square of that on every frame of the drag.
		FLayoutIndex Index;
		BuildLayoutIndex(Index);

		TArray<FWidgetAnimLayer>& MutableLayers = *Source->GetLayers();
		for (int32 LayerIndex = 0; LayerIndex < MutableLayers.Num() && State.OriginalEditorLines.IsValidIndex(LayerIndex); ++LayerIndex)
		{
			int32 NewLine = State.OriginalEditorLines[LayerIndex];

			// A pinned track, one folded into a collapsed track and a collapsed track drawn loose keep their lines in their
			// groups: they aren't shown there.
			const bool bIndexed = Index.InView.IsValidIndex(LayerIndex);
			const bool bOffItsLine = IsLayerPinned(LayerIndex)
				|| (bIndexed
					? (static_cast<bool>(Index.FoldedIntoCollapsed[LayerIndex]) || !static_cast<bool>(Index.InView[LayerIndex]) || Index.IsLooseCollapsedHead(LayerIndex))
					: (IsFoldedIntoCollapsed(LayerIndex) || !IsLayerInView(LayerIndex) || IsLooseCollapsedHead(LayerIndex)));
			if (DeltaLines != 0 && State.OriginalLineOrdinals.IsValidIndex(LayerIndex) && !bOffItsLine)
			{
				const int32 Ordinal = State.OriginalLineOrdinals[LayerIndex];
				NewLine = Ordinal;

				if (State.IsStartEdge(LayerIndex))
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

	// Interactive, as a Details panel drag is: the preview follows every frame of it. Told before the lines are worked out,
	// so they are worked out once for this step, not again on the next refresh - see IsTrackLayoutDue.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
	UpdateTrackLayout();
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

		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
		else if ((!Layer.IsAnimationTrack() || Layer.FitsAnimationToTrack()) && !Layer.IsSpringTimed() && !HasDerivedLength(Layer) && !FMath::IsNearlyEqual(Layer.Duration, Length, 1.e-5f))
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
	return IsDraggingLayers() && (LayerDrag.IsStartEdge(LayerIndex) || LayerDrag.IsEndEdge(LayerIndex));
}

// -----------------------------------------------------------------------------------------------------
// The animation's range, dragged on the ruler
// -----------------------------------------------------------------------------------------------------

bool SWidgetAnimTimeline::BeginRangeDrag(bool bEnd)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!CanEdit() || IsDragging() || !Layers || Layers->Num() == 0)
	{
		return false;
	}

	FLayerDragState State;
	State.Mode = bEnd ? ELayerDrag::RangeEnd : ELayerDrag::RangeStart;

	// Every track as it is now, as a track drag measures from: what each frame of the drag works from, so it never compounds.
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
	State.OriginalMinimumLength = Source.IsValid() ? Source->GetMinimumLength() * GetTimeScale() : 0.f;

	// The start or end snaps to where tracks start and end.
	GatherSnapTargets(TSet<int32>(), /*bIncludeKeys=*/false, State.SnapTargets);

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
	LayerDragTransaction = MakeUnique<FScopedTransaction>(bEnd
		? LOCTEXT("DragAnimationEndTransaction", "Change Animation End")
		: LOCTEXT("DragAnimationStartTransaction", "Change Animation Start"));
	BeginEdit();
	bLayerDragChanged = false;
	return true;
}

void SWidgetAnimTimeline::UpdateRangeDrag(float Seconds, float SnapThresholdSeconds)
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!IsDraggingRange() || !CanEdit() || !Layers || Layers->Num() != LayerDrag.OriginalStarts.Num())
	{
		return;
	}

	const FLayerDragState& State = LayerDrag;
	const bool bEnd = State.Mode == ELayerDrag::RangeEnd;
	const int32 NumLayers = Layers->Num();
	constexpr float MaxSeconds = 600.f;
	constexpr float Tolerance = 0.0005f;

	SnapIndicator.Reset();
	float Wanted = RoundSeconds(Seconds);
	float Adjust = 0.f;
	float Target = 0.f;
	if (SnapThresholdSeconds > 0.f && FindSnap(State.SnapTargets, { Seconds }, SnapThresholdSeconds, Adjust, Target))
	{
		Wanted = Seconds + Adjust;
		SnapIndicator = Target;
	}

	TArray<float> NewStarts = State.OriginalStarts;
	TArray<float> NewLengths = State.OriginalLengths;
	float Applied = 0.f;

	// The animation's Minimum Length after this frame of the drag, in played seconds: where its end is, when that is past
	// its last track. Only an animation keeps one - an asset's, the builder's - not a component's tracks.
	const bool bKeepsMinimumLength = Source.IsValid() && Source->CanSetMinimumLength();
	float NewMinimumLength = State.OriginalMinimumLength;
	auto GetTracksEnd = [this, &NewStarts, &NewLengths, NumLayers]()
	{
		float TracksEnd = 0.f;
		for (const FVector2f& Span : ContextSpans)
		{
			TracksEnd = FMath::Max(TracksEnd, Span.X + Span.Y);
		}
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			TracksEnd = FMath::Max(TracksEnd, NewStarts[LayerIndex] + NewLengths[LayerIndex] + GetLayerFanSeconds(LayerIndex));
		}
		return TracksEnd;
	};

	if (bEnd)
	{
		// Pulled in, every track the end cuts into is shortened to end there - down to the shortest a track can be, and
		// never one whose end can't move (a Spring, an animation track, a text typed at its own pace, a locked track): the
		// end stops there instead. Pulled out, only the animation gets longer - time after its last track, kept as its
		// Minimum Length - and every track stays exactly as it is. Tracks that aren't an animation (a component's) are
		// only as long as they are, so there the end goes no further out.
		float Lowest = 0.f;
		for (const FVector2f& Span : ContextSpans)
		{
			Lowest = FMath::Max(Lowest, Span.X + Span.Y);
		}
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const float Fan = GetLayerFanSeconds(LayerIndex);
			const float Start = State.OriginalStarts[LayerIndex];
			const float End = Start + State.OriginalLengths[LayerIndex] + Fan;
			Lowest = FMath::Max(Lowest, CanResizeLayer(LayerIndex, /*bStartEdge=*/false) ? Start + MinLayerSeconds + Fan : End);
		}

		const float Highest = bKeepsMinimumLength ? State.OriginalEnd + MaxSeconds : State.OriginalEnd;
		const float NewEnd = Lowest <= Highest ? FMath::Clamp(Wanted, Lowest, Highest) : Highest;
		if (SnapIndicator.IsSet() && !FMath::IsNearlyEqual(NewEnd, Wanted, Tolerance))
		{
			SnapIndicator.Reset();
		}

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const float Fan = GetLayerFanSeconds(LayerIndex);
			const float Start = State.OriginalStarts[LayerIndex];
			if (Start + State.OriginalLengths[LayerIndex] + Fan > NewEnd + Tolerance)
			{
				NewLengths[LayerIndex] = FMath::Max(NewEnd - Start - Fan, MinLayerSeconds);
			}
		}

		// Past where the tracks now end, the rest is the animation's own time; back on them, it has none of its own.
		NewMinimumLength = NewEnd > GetTracksEnd() + Tolerance ? NewEnd : 0.f;
		Applied = State.OriginalEnd - NewEnd;
	}
	else
	{
		// The start: pulled out before 0, everything moves later by that much - time in front of it all; pushed in, every
		// track starting before it is shortened to start there, and everything moves back to start at 0. A track whose
		// start can't move - or would leave it shorter than a track can be - stops it there. With a locked track, nothing
		// can move at all.
		float Lowest = -MaxSeconds;
		float Highest = MaxSeconds;
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			if (!CanEditLayer(LayerIndex))
			{
				Lowest = 0.f;
				Highest = 0.f;
				break;
			}

			const float Start = State.OriginalStarts[LayerIndex];
			const bool bCanTrim = CanResizeLayer(LayerIndex, /*bStartEdge=*/true) && !HasDerivedLength((*Layers)[LayerIndex]);
			Highest = FMath::Min(Highest, bCanTrim ? Start + State.OriginalLengths[LayerIndex] - MinLayerSeconds : Start);
		}

		const float NewStart = Lowest <= Highest ? FMath::Clamp(Wanted, Lowest, Highest) : 0.f;
		if (SnapIndicator.IsSet() && !FMath::IsNearlyEqual(NewStart, Wanted, Tolerance))
		{
			SnapIndicator.Reset();
		}

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			const float Start = State.OriginalStarts[LayerIndex];
			const float End = Start + State.OriginalLengths[LayerIndex];
			if (Start < NewStart - Tolerance)
			{
				NewStarts[LayerIndex] = 0.f;
				NewLengths[LayerIndex] = FMath::Max(End - NewStart, MinLayerSeconds);
			}
			else
			{
				NewStarts[LayerIndex] = Start - NewStart;
			}
		}

		// Time after the last track moves with everything else, so it still ends that far past the tracks - and, pushed
		// in until the tracks reach it, is gone.
		if (State.OriginalMinimumLength > 0.f)
		{
			const float Moved = State.OriginalMinimumLength - NewStart;
			NewMinimumLength = Moved > GetTracksEnd() + Tolerance ? Moved : 0.f;
		}
		Applied = NewStart;
	}

	ApplyLayerSpans(NewStarts, NewLengths);
	if (bKeepsMinimumLength)
	{
		// Kept in seconds as authored, as every track's timing is; the panel shows them as played.
		Source->SetMinimumLength(NewMinimumLength / GetTimeScale());
	}
	bLayerDragChanged |= !FMath::IsNearlyZero(Applied, Tolerance);
	UpdateTrackLayout();

	// Interactive, as a Details panel drag is: the preview follows every frame of it.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
}

// -----------------------------------------------------------------------------------------------------
// Collapsed tracks
// -----------------------------------------------------------------------------------------------------

FName SWidgetAnimTimeline::GetLayerCollapsedName(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) ? (*Layers)[LayerIndex].CollapsedTrack : NAME_None;
}

bool SWidgetAnimTimeline::IsLayerInView(int32 LayerIndex) const
{
	return !IsInCollapsedScope() || GetLayerCollapsedName(LayerIndex) == ActiveCollapsed;
}

void SWidgetAnimTimeline::GetCollapsedMembers(FName Name, TArray<int32>& OutMembers) const
{
	OutMembers.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || Name.IsNone())
	{
		return;
	}
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		if ((*Layers)[LayerIndex].CollapsedTrack == Name)
		{
			OutMembers.Add(LayerIndex);
		}
	}
}

bool SWidgetAnimTimeline::IsCollapsedHead(int32 LayerIndex) const
{
	const FName Name = GetLayerCollapsedName(LayerIndex);
	if (Name.IsNone() || Name == ActiveCollapsed)
	{
		return false;
	}

	// The first of its tracks in the sequence.
	const TArray<FWidgetAnimLayer>& Layers = *GetLayers();
	for (int32 Index = 0; Index < LayerIndex; ++Index)
	{
		if (Layers[Index].CollapsedTrack == Name)
		{
			return false;
		}
	}
	return true;
}

bool SWidgetAnimTimeline::IsFoldedIntoCollapsed(int32 LayerIndex) const
{
	const FName Name = GetLayerCollapsedName(LayerIndex);
	return !Name.IsNone() && Name != ActiveCollapsed && !IsCollapsedHead(LayerIndex);
}

bool SWidgetAnimTimeline::IsGroupAllCollapsed(int32 GroupIndex) const
{
	// Open on its own, a collapsed track shows its groups - that is how a hidden group's settings are reached.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (bCompact || IsInCollapsedScope() || !Layers)
	{
		return false;
	}

	bool bAnyTrack = false;
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		if (GetLayerGroup(LayerIndex) != GroupIndex)
		{
			continue;
		}
		if ((*Layers)[LayerIndex].CollapsedTrack.IsNone())
		{
			return false;
		}
		bAnyTrack = true;
	}
	return bAnyTrack;
}

bool SWidgetAnimTimeline::IsLooseCollapsedHead(int32 LayerIndex) const
{
	return IsCollapsedHead(LayerIndex) && IsGroupAllCollapsed(GetLayerGroup(LayerIndex));
}

void SWidgetAnimTimeline::FLayoutIndex::Reset()
{
	LayerGroups.Reset();
	CollapsedHeads.Reset();
	FoldedIntoCollapsed.Reset();
	InView.Reset();
	for (TArray<int32>& Members : GroupMembers)
	{
		Members.Reset();
	}
	GroupsAllCollapsed.Reset();
	bUngroupedAllCollapsed = false;
}

void SWidgetAnimTimeline::BuildLayoutIndex(FLayoutIndex& OutIndex) const
{
	OutIndex.Reset();

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Groups || !Layers)
	{
		OutIndex.GroupMembers.Reset();
		return;
	}

	const int32 NumLayers = Layers->Num();
	const int32 NumGroups = Groups->Num();
	const bool bInScope = IsInCollapsedScope();

	OutIndex.LayerGroups.SetNumUninitialized(NumLayers);
	OutIndex.CollapsedHeads.Init(false, NumLayers);
	OutIndex.FoldedIntoCollapsed.Init(false, NumLayers);
	OutIndex.InView.Init(false, NumLayers);
	OutIndex.GroupMembers.SetNum(NumGroups);

	// Per group - and for the layers of no group - whether it has a track at all, and one in no collapsed track.
	constexpr uint8 HasTrack = 1;
	constexpr uint8 HasLooseTrack = 2;
	TArray<uint8, TInlineAllocator<64>> GroupState;
	GroupState.SetNumZeroed(NumGroups);
	uint8 UngroupedState = 0;

	// A collapsed track's head is the first of its tracks in the sequence: the first time its name comes up.
	TSet<FName, DefaultKeyFuncs<FName>, TInlineSetAllocator<16>> SeenCollapsed;

	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const int32 GroupIndex = UWidgetAnimLibrary::GetLayerGroupIndex(Layer, NumGroups);
		OutIndex.LayerGroups[LayerIndex] = GroupIndex;

		const FName Collapsed = Layer.CollapsedTrack;
		OutIndex.InView[LayerIndex] = !bInScope || Collapsed == ActiveCollapsed;
		if (!Collapsed.IsNone() && Collapsed != ActiveCollapsed)
		{
			bool bSeen = false;
			SeenCollapsed.Add(Collapsed, &bSeen);
			OutIndex.CollapsedHeads[LayerIndex] = !bSeen;
			OutIndex.FoldedIntoCollapsed[LayerIndex] = bSeen;
		}

		uint8& State = OutIndex.GroupMembers.IsValidIndex(GroupIndex) ? GroupState[GroupIndex] : UngroupedState;
		State |= static_cast<uint8>(HasTrack | (Collapsed.IsNone() ? HasLooseTrack : 0));
		if (OutIndex.GroupMembers.IsValidIndex(GroupIndex))
		{
			OutIndex.GroupMembers[GroupIndex].Add(LayerIndex);
		}
	}

	// See IsGroupAllCollapsed: never in the compact panel, nor inside a collapsed track open on its own.
	const bool bCanHideGroups = !bCompact && !bInScope;
	OutIndex.GroupsAllCollapsed.Init(false, NumGroups);
	for (int32 GroupIndex = 0; GroupIndex < NumGroups; ++GroupIndex)
	{
		OutIndex.GroupsAllCollapsed[GroupIndex] = bCanHideGroups && GroupState[GroupIndex] == HasTrack;
	}
	OutIndex.bUngroupedAllCollapsed = bCanHideGroups && UngroupedState == HasTrack;
}

bool SWidgetAnimTimeline::IsTrackLayoutCurrent() const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	return Layers && Groups
		&& LayoutIndex.LayerGroups.Num() == Layers->Num()
		&& LayoutIndex.GroupsAllCollapsed.Num() == Groups->Num()
		&& TrackSpans.Num() == Layers->Num();
}

bool SWidgetAnimTimeline::IsCollapsedHeadShown(int32 LayerIndex) const
{
	if (IsTrackLayoutCurrent())
	{
		return LayoutIndex.CollapsedHeads.IsValidIndex(LayerIndex) && LayoutIndex.CollapsedHeads[LayerIndex];
	}
	return IsCollapsedHead(LayerIndex);
}

FVector2f SWidgetAnimTimeline::GetShownGroupSpan(int32 GroupIndex) const
{
	if (TrackGroupSpans.IsValidIndex(GroupIndex) && IsTrackLayoutCurrent())
	{
		return TrackGroupSpans[GroupIndex];
	}
	return GetGroupSpan(GroupIndex);
}

SWidgetAnimTimeline::FCollapsedState SWidgetAnimTimeline::GetCollapsedState(FName Name) const
{
	FCollapsedState State;
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || Name.IsNone())
	{
		return State;
	}

	// The selection as a set of bits, so each track's question is one lookup - a collapsed track of hundreds of tracks,
	// all of them selected, would otherwise search the selection once for every one of them.
	TBitArray<> Selected(false, Layers->Num());
	for (const int32 LayerIndex : SelectedLayers)
	{
		if (Selected.IsValidIndex(LayerIndex))
		{
			Selected[LayerIndex] = true;
		}
	}

	State.bAllHidden = true;
	State.bAllLocked = true;
	State.bAllSelected = true;
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		if (Layer.CollapsedTrack != Name)
		{
			continue;
		}

		const bool bSelected = Selected[LayerIndex];
		++State.NumMembers;
		State.bAllHidden = State.bAllHidden && Layer.bHiddenInEditor;
		State.bAllLocked = State.bAllLocked && Layer.bLockedInEditor;
		State.bAllSelected = State.bAllSelected && bSelected;
		State.bAnySelected = State.bAnySelected || bSelected;
		State.bAnyPinned = State.bAnyPinned || Layer.bPinnedInEditor;
	}

	if (State.NumMembers == 0)
	{
		State.bAllHidden = false;
		State.bAllLocked = false;
		State.bAllSelected = false;
	}
	return State;
}

FVector2f SWidgetAnimTimeline::GetShownSpan(int32 LayerIndex) const
{
	if (IsCollapsedHeadShown(LayerIndex))
	{
		if (const FVector2f* Collapsed = CollapsedSpans.Find(LayerIndex))
		{
			return *Collapsed;
		}
	}
	if (TrackSpans.IsValidIndex(LayerIndex))
	{
		return TrackSpans[LayerIndex];
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	return Spans.IsValidIndex(LayerIndex) ? Spans[LayerIndex] : FVector2f::ZeroVector;
}

bool SWidgetAnimTimeline::CanCollapseLayers(const TArray<int32>& LayerIndices) const
{
	// Not inside one: a collapsed track holds tracks, never another collapsed track.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!CanEdit() || !Layers || IsInCollapsedScope())
	{
		return false;
	}

	// Something to fold - and not just the tracks of one collapsed track, folded again as they already are.
	TSet<FName> Names;
	int32 NumValid = 0;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Layers->IsValidIndex(LayerIndex))
		{
			++NumValid;
			Names.Add((*Layers)[LayerIndex].CollapsedTrack);
		}
	}
	if (NumValid == 0)
	{
		return false;
	}
	if (Names.Num() == 1 && !Names.Contains(NAME_None))
	{
		TArray<int32> Members;
		GetCollapsedMembers(*Names.CreateConstIterator(), Members);
		return Members.Num() != NumValid;
	}
	return true;
}

void SWidgetAnimTimeline::CollapseLayers(const TArray<int32>& LayerIndices)
{
	if (!CanCollapseLayers(LayerIndices))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("CollapseTracksTransaction", "Collapse Tracks"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	const FName Name = WidgetAnimCollapsed::MakeUniqueName(Layers, TEXT("Collapsed Tracks"));
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Layers.IsValidIndex(LayerIndex))
		{
			Layers[LayerIndex].CollapsedTrack = Name;
		}
	}

	// Only how the panel shows them: the preview plays exactly as it did.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	SelectCollapsed(Name);
}

void SWidgetAnimTimeline::ExpandCollapsed(FName Name)
{
	TArray<int32> Members;
	GetCollapsedMembers(Name, Members);
	if (Members.Num() == 0 || !CanEdit())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ExpandCollapsedTransaction", "Expand Collapsed Track"));
	if (!BeginEdit())
	{
		return;
	}

	if (ActiveCollapsed == Name)
	{
		ActiveCollapsed = NAME_None;
		SyncScopeMutes();
	}
	ListOpenCollapsed.Remove(Name);

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : Members)
	{
		Layers[LayerIndex].CollapsedTrack = NAME_None;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	SelectInBox({}, Members, /*bAdd=*/false, /*bToggle=*/false);
}

void SWidgetAnimTimeline::RenameCollapsed(FName OldName, const FString& NewName)
{
	TArray<int32> Members;
	GetCollapsedMembers(OldName, Members);
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (Members.Num() == 0 || !Current || NewName.TrimStartAndEnd().IsEmpty() || !CanEdit())
	{
		return;
	}

	const FName Unique = WidgetAnimCollapsed::MakeUniqueName(*Current, NewName, OldName);
	if (Unique.ToString().Equals(OldName.ToString(), ESearchCase::CaseSensitive))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameCollapsedTransaction", "Rename Collapsed Track"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : Members)
	{
		Layers[LayerIndex].CollapsedTrack = Unique;
	}
	if (ActiveCollapsed == OldName)
	{
		ActiveCollapsed = Unique;
	}
	if (ListOpenCollapsed.Remove(OldName) > 0)
	{
		ListOpenCollapsed.Add(Unique);
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
}

void SWidgetAnimTimeline::SelectCollapsed(FName Name, bool bToggle)
{
	TArray<int32> Members;
	GetCollapsedMembers(Name, Members);
	if (Members.Num() == 0)
	{
		return;
	}

	TArray<int32> NewLayers;
	if (bToggle && SelectedKeys.Num() == 0)
	{
		// Added to what is selected - or, when every one of its tracks already is, taken out.
		NewLayers = SelectedLayers;
		const bool bAllIn = Algo::AllOf(Members, [this](int32 Member) { return SelectedLayers.Contains(Member); });
		for (const int32 Member : Members)
		{
			if (bAllIn)
			{
				NewLayers.Remove(Member);
			}
			else
			{
				NewLayers.AddUnique(Member);
			}
		}
	}
	else
	{
		NewLayers = Members;
	}

	if (NewLayers.Num() == 0)
	{
		ClearSelection();
		return;
	}

	NewLayers.Sort();
	SelectedKeys.Reset();
	SelectedLayers = MoveTemp(NewLayers);

	// Its head is the primary one - what the title and a one-track action name - while it is selected.
	FSelection NewSelection;
	NewSelection.Layer = SelectedLayers.Contains(Members[0]) ? Members[0] : SelectedLayers.Last();
	Selection = Validate(NewSelection);
	SelectionAnchor = Selection.Layer;
	ValidateSelectedLayers();
	RefreshDetails();
}

void SWidgetAnimTimeline::EnterCollapsed(FName Name)
{
	TArray<int32> Members;
	GetCollapsedMembers(Name, Members);
	if (Members.Num() == 0 || Name == ActiveCollapsed)
	{
		return;
	}

	// Where it is, to bring into view once it is open.
	const FVector2f Span = GetShownSpan(Members[0]);

	if (IsDragging())
	{
		IsDraggingKey() ? EndKeyDrag() : EndLayerDrag();
	}

	// Its tracks are shown in the Track Editor: out of the Graph Editor first.
	GraphLayers.Reset();

	ActiveCollapsed = Name;
	ClearSelection();
	SyncScopeMutes();
	RebuildRows();

	if (Tracks.IsValid() && Span.Y > 0.f)
	{
		Tracks->RevealTime(Span.X, Span.X + Span.Y);
	}
}

void SWidgetAnimTimeline::ExitCollapsed()
{
	if (!IsInCollapsedScope())
	{
		return;
	}

	if (IsDragging())
	{
		IsDraggingKey() ? EndKeyDrag() : EndLayerDrag();
	}

	const FName Was = ActiveCollapsed;
	ActiveCollapsed = NAME_None;
	SyncScopeMutes();
	RebuildRows();

	// Back out as a collapsed graph is left: on the collapsed track, selected.
	SelectCollapsed(Was);
}

void SWidgetAnimTimeline::SyncScopeMutes()
{
	TArray<FWidgetAnimLayer>* Layers = Source.IsValid() ? Source->GetLayers() : nullptr;
	if (!Layers)
	{
		bScopeMutesApplied = false;
		return;
	}

	// The tracks the Graph Editor shows, while it is open: Play plays only them.
	TBitArray<> InGraph(false, Layers->Num());
	for (const int32 GraphLayer : GraphLayers)
	{
		if (InGraph.IsValidIndex(GraphLayer))
		{
			InGraph[GraphLayer] = true;
		}
	}
	const bool bGraphScope = IsGraphOpen();

	// Not an edit: a transient flag, set and cleared as the panel goes in and out, and never saved or undone.
	bool bChanged = false;
	bool bAnyMuted = false;
	for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
	{
		FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		const bool bMuted = (IsInCollapsedScope() && Layer.CollapsedTrack != ActiveCollapsed) || (bGraphScope && !InGraph[LayerIndex]);
		bAnyMuted |= bMuted;
		if (Layer.bMutedByEditorScope != bMuted)
		{
			Layer.bMutedByEditorScope = bMuted;
			bChanged = true;
		}
	}
	bScopeMutesApplied = bAnyMuted;

	// The previews play only what is shown - as a hidden track, its place in time counted and nothing posed.
	if (bChanged)
	{
		NotifyGroupsChanged(EPropertyChangeType::Interactive);
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeCollapsedToolTip(int32 HeadLayer) const
{
	using namespace WidgetAnimToolTip;

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	const FName Name = GetLayerCollapsedName(HeadLayer);
	TArray<int32> Members;
	GetCollapsedMembers(Name, Members);
	if (!Layers || Members.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);

	// Each of its tracks, in the order they play, as its section reads on the Track Editor: its name, how long it runs,
	// and how it moves.
	Members.StableSort([&Spans](int32 A, int32 B)
	{
		return (Spans.IsValidIndex(A) ? Spans[A].X : 0.f) < (Spans.IsValidIndex(B) ? Spans[B].X : 0.f);
	});

	// The first few dozen: a collapsed track of hundreds of tracks would otherwise make a tooltip taller than the screen,
	// and thousands of widgets, every time the mouse came to rest on it.
	constexpr int32 MaxRows = 24;
	const int32 NumShown = FMath::Min(Members.Num(), MaxRows);

	const TSharedRef<SGridPanel> Table = SNew(SGridPanel).FillColumn(1, 1.f);
	int32 Row = 0;
	for (int32 Position = 0; Position < NumShown; ++Position)
	{
		const int32 Member = Members[Position];
		const FWidgetAnimLayer& Layer = (*Layers)[Member];
		const bool bWashedOut = Layer.bHiddenInEditor || Layer.bLockedInEditor;
		const FLinearColor Color = bWashedOut ? GetLockedColor(GetLayerTypeColor(Layer)) : GetLayerTypeColor(Layer);
		const float Length = Spans.IsValidIndex(Member) ? Spans[Member].Y : 0.f;

		Table->AddSlot(0, Row)
			.Padding(FMargin(0.f, 2.f, 6.f, 2.f))
			.VAlign(VAlign_Center)
			[
				Swatch(Color)
			];
		Table->AddSlot(1, Row)
			.Padding(FMargin(0.f, 2.f, 14.f, 2.f))
			.VAlign(VAlign_Center)
			[
				Text(GetLayerDisplayName(Layer).ToString(), bWashedOut ? Palette::TooltipLabel : Palette::TooltipValue, ValueFont())
			];
		Table->AddSlot(2, Row)
			.Padding(FMargin(0.f, 2.f, 14.f, 2.f))
			.VAlign(VAlign_Center)
			[
				Text(FormatTime(Length), Palette::TooltipValue, ValueFont())
			];
		Table->AddSlot(3, Row)
			.Padding(FMargin(0.f, 2.f))
			.VAlign(VAlign_Center)
			[
				Text(IsCueLayer(Layer) ? FString() : GetEasingName(Layer).ToString(), Palette::TooltipLabel, SmallFont())
			];
		++Row;
	}

	const TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot()
	.AutoHeight()
	[
		Table
	];
	if (Members.Num() > NumShown)
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(FText::Format(WidgetAnimTips::CollapsedTrackMoreNote(), FText::AsNumber(Members.Num() - NumShown)).ToString())
		];
	}
	Body->AddSlot()
	.AutoHeight()
	[
		Note(WidgetAnimTips::CollapsedTrackNote().ToString())
	];

	const FVector2f Span = GetShownSpan(HeadLayer);
	return Frame(
		Palette::LayerCollapsed,
		Name.ToString(),
		FString::Printf(TEXT("%d %s"), Members.Num(), Members.Num() == 1 ? TEXT("track") : TEXT("tracks")),
		FormatSpan(Span.X, Span.X + Span.Y),
		Body);
}

// -----------------------------------------------------------------------------------------------------
// Pinned and inverted tracks
// -----------------------------------------------------------------------------------------------------

bool SWidgetAnimTimeline::IsLayerPinned(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].bPinnedInEditor;
}

void SWidgetAnimTimeline::SetLayersPinned(const TArray<int32>& LayerIndices, bool bPinned)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current)
	{
		return;
	}

	TArray<int32> ToChange;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Current->IsValidIndex(LayerIndex) && (*Current)[LayerIndex].bPinnedInEditor != bPinned)
		{
			ToChange.Add(LayerIndex);
		}
	}
	if (ToChange.Num() == 0)
	{
		return;
	}

	// A way of looking, as hiding is: allowed while read-only, and nothing the previews play changes.
	const FScopedTransaction Transaction(bPinned
		? LOCTEXT("PinTracksTransaction", "Pin Tracks")
		: LOCTEXT("UnpinTracksTransaction", "Unpin Tracks"));
	if (!BeginEditIgnoringReadOnly())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : ToChange)
	{
		Layers[LayerIndex].bPinnedInEditor = bPinned;
	}

	RebuildRows();
}

bool SWidgetAnimTimeline::IsLayerInvertedOnItsOwn(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	return Layers && Layers->IsValidIndex(LayerIndex) && (*Layers)[LayerIndex].bInvertTrack;
}

bool SWidgetAnimTimeline::IsLayerInverted(int32 LayerIndex) const
{
	// The preview's Invert inverts every track - and never turns one inverted on its own back.
	return IsLayerInvertedOnItsOwn(LayerIndex) || (Source.IsValid() && Source->GetInvert());
}

void SWidgetAnimTimeline::SetLayersInverted(const TArray<int32>& LayerIndices, bool bInverted)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current)
	{
		return;
	}

	TArray<int32> ToChange;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (CanEditLayer(LayerIndex) && (*Current)[LayerIndex].bInvertTrack != bInverted)
		{
			ToChange.Add(LayerIndex);
		}
	}
	if (ToChange.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(bInverted
		? LOCTEXT("InvertTracksTransaction", "Invert Tracks")
		: LOCTEXT("UninvertTracksTransaction", "Turn Tracks Back"));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : ToChange)
	{
		Layers[LayerIndex].bInvertTrack = bInverted;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

// -----------------------------------------------------------------------------------------------------
// Picking a Preview Target in the designer
// -----------------------------------------------------------------------------------------------------

FWidgetBlueprintEditor* SWidgetAnimTimeline::FindWidgetBlueprintEditor() const
{
	UWidgetBlueprint* Blueprint = Source.IsValid() ? Source->GetWidgetBlueprint() : nullptr;
	UAssetEditorSubsystem* AssetEditors = GEditor && Blueprint ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	IAssetEditorInstance* Instance = AssetEditors ? AssetEditors->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false) : nullptr;
	if (!Instance || Instance->GetEditorName() != FName(TEXT("WidgetBlueprintEditor")))
	{
		return nullptr;
	}
	return static_cast<FWidgetBlueprintEditor*>(Instance);
}

UWidget* SWidgetAnimTimeline::ToPanelPreviewWidget(UWidget* DesignerWidget, FName TemplateName) const
{
	// The preview the panel poses may not be the designer's own: the same widget is found there by its name instead.
	UUserWidget* Root = Source.IsValid() ? Source->GetPreviewRoot() : nullptr;
	if (DesignerWidget && Source.IsValid() && !Source->FindTargetOptionFor(DesignerWidget).IsEmpty())
	{
		return DesignerWidget;
	}
	const FName Name = !TemplateName.IsNone() ? TemplateName : (DesignerWidget ? DesignerWidget->GetFName() : NAME_None);
	if (Root && Root->WidgetTree && !Name.IsNone())
	{
		if (UWidget* Found = Root->WidgetTree->FindWidget(Name))
		{
			return Found;
		}
	}
	return DesignerWidget;
}

UWidget* SWidgetAnimTimeline::GetDesignerSelectedWidget() const
{
	FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor();
	if (!Editor)
	{
		return nullptr;
	}

	const TSet<FWidgetReference>& Selected = Editor->GetSelectedWidgets();
	if (Selected.Num() != 1)
	{
		return nullptr;
	}

	const FWidgetReference& Reference = *Selected.CreateConstIterator();
	const UWidget* Template = Reference.GetTemplate();
	return ToPanelPreviewWidget(Reference.GetPreview(), Template ? Template->GetFName() : NAME_None);
}

bool SWidgetAnimTimeline::CanPickTargetInDesigner() const
{
	return CanEdit() && Source->SupportsLayerGroups() && Source->GetWidgetBlueprint() != nullptr && Source->GetPreviewRoot() != nullptr;
}

/**
 * While a Preview Target is being picked: Escape calls it off from anywhere, and - should the panel not hold the mouse
 * (see BeginTargetPick) - the mouse is watched here instead, the cursor kept an eyedropper and a click on a widget taken
 * as the pick. Registered only while a pick waits.
 */
class FWidgetAnimTargetPickInput : public IInputProcessor
{
public:

	explicit FWidgetAnimTargetPickInput(TWeakPtr<SWidgetAnimTimeline> InPanel)
		: Panel(InPanel)
	{
	}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
		if (Pinned && Pinned->IsPickingAnyTarget() && !Pinned->HasMouseCapture())
		{
			Cursor->SetType(EMouseCursor::EyeDropper);
		}
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
		if (Pinned && Pinned->IsPickingAnyTarget() && !Pinned->HasMouseCapture())
		{
			Pinned->UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		}
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
		if (!Pinned || !Pinned->IsPickingAnyTarget() || Pinned->HasMouseCapture() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return false;
		}
		Pinned->UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		return Pinned->CommitPickHover();
	}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = Panel.Pin();
		if (Pinned && Pinned->IsPickingAnyTarget() && InKeyEvent.GetKey() == EKeys::Escape)
		{
			Pinned->CancelTargetPick();
			return true;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("WidgetAnimTargetPick"); }

private:

	TWeakPtr<SWidgetAnimTimeline> Panel;
};

void SWidgetAnimTimeline::BeginTargetPick(int32 GroupIndex)
{
	using namespace WidgetAnimTimeline;

	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (!CanPickTargetInDesigner() || !Groups || !Groups->IsValidIndex(GroupIndex))
	{
		return;
	}

	// A pick for another group gives way to this one.
	CancelTargetPick();

	// What is selected there now doesn't count: the next widget selected does - or, under the eyedropper, the one clicked.
	PickTargetGroup = GroupIndex;
	UWidget* Current = GetDesignerSelectedWidget();
	PickStartSelection = Current;
	bPickStartedWithSelection = Current != nullptr;

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	PickInput = MakeShared<FWidgetAnimTargetPickInput>(SharedThis(this));
	FSlateApplication::Get().RegisterInputPreProcessor(PickInput);

	// The name of the widget under the mouse, in orange beside the cursor - so it is known before the click. Hidden
	// while nothing that can be picked is under it.
	PickLabelWindow = SWindow::MakeCursorDecorator();
	PickLabelWindow->SetContent(
		SNew(SBorder)
		.BorderImage(RoundedFill(3.f))
		.BorderBackgroundColor(Palette::PickHoverBackground)
		.Padding(FMargin(6.f, 2.f))
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(Palette::PickingTargetText)
			.Text_Lambda([WeakPanel = TWeakPtr<SWidgetAnimTimeline>(SharedThis(this))]()
			{
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin();
				return Pinned ? FText::FromString(Pinned->PickHoverLabel) : FText::GetEmpty();
			})
		]);
	FSlateApplication::Get().AddWindow(PickLabelWindow.ToSharedRef(), /*bShowImmediately=*/false);

	// The panel holds the mouse until the pick is made or called off, as an eyedropper does: so the cursor is its
	// eyedropper and nothing else's (the designer would set its own each frame, and it flickered between the two), the
	// mouse is followed wherever it goes, and the click lands here rather than selecting in the designer.
	CapturePickMouse();
}

void SWidgetAnimTimeline::CapturePickMouse()
{
	if (!FSlateApplication::IsInitialized() || PickTargetGroup == INDEX_NONE || HasMouseCapture())
	{
		return;
	}

	FWidgetPath PanelPath;
	if (FSlateApplication::Get().FindPathToWidget(AsShared(), PanelPath))
	{
		FSlateApplication::Get().ProcessExternalReply(PanelPath, FReply::Handled().CaptureMouse(AsShared()), 0, static_cast<int32>(FSlateApplicationBase::CursorPointerIndex));
	}
}

void SWidgetAnimTimeline::CancelTargetPick()
{
	EndTargetPick(/*bReleaseMouse=*/true);
}

void SWidgetAnimTimeline::EndTargetPick(bool bReleaseMouse)
{
	if (PickTargetGroup == INDEX_NONE)
	{
		return;
	}

	PickTargetGroup = INDEX_NONE;
	PickStartSelection.Reset();
	bPickStartedWithSelection = false;
	PickHoverWidget.Reset();
	PickHoverLabel.Reset();

	if (FSlateApplication::IsInitialized())
	{
		// The mouse goes back to whatever is under it. (Not while the panel itself is going: Slate lets go of it then.)
		if (bReleaseMouse && HasMouseCapture())
		{
			FWidgetPath PanelPath;
			if (FSlateApplication::Get().FindPathToWidget(AsShared(), PanelPath))
			{
				FSlateApplication::Get().ProcessExternalReply(PanelPath, FReply::Handled().ReleaseMouseCapture(), 0, static_cast<int32>(FSlateApplicationBase::CursorPointerIndex));
			}
		}

		// Taken off on the next tick, not now: this is often reached from inside the processor's own handling of a click,
		// while Slate walks its list of processors. Until then it does nothing - nothing is being picked.
		if (PickInput.IsValid())
		{
			const TWeakPtr<IInputProcessor> WeakInput = PickInput;
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakInput](float)
			{
				const TSharedPtr<IInputProcessor> Input = WeakInput.Pin();
				if (Input.IsValid() && FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().UnregisterInputPreProcessor(Input);
				}
				return false;
			}));
		}
		if (PickLabelWindow.IsValid())
		{
			PickLabelWindow->HideWindow();
			PickLabelWindow->RequestDestroyWindow();
		}
	}
	PickInput.Reset();
	PickLabelWindow.Reset();
}

FReply SWidgetAnimTimeline::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (PickTargetGroup != INDEX_NONE && HasMouseCapture())
	{
		UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
}

FReply SWidgetAnimTimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (PickTargetGroup != INDEX_NONE && HasMouseCapture())
	{
		// A left click on a widget of the preview picks it; any other click - elsewhere, or another button - calls it off.
		UpdatePickHover(MouseEvent.GetScreenSpacePosition());
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !CommitPickHover())
		{
			CancelTargetPick();
		}
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FCursorReply SWidgetAnimTimeline::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (PickTargetGroup != INDEX_NONE)
	{
		return FCursorReply::Cursor(EMouseCursor::EyeDropper);
	}
	return SCompoundWidget::OnCursorQuery(MyGeometry, CursorEvent);
}

void SWidgetAnimTimeline::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);

	// Taken away - another window, Alt+Tab: the pick is called off rather than left waiting without the mouse.
	if (PickTargetGroup != INDEX_NONE)
	{
		EndTargetPick(/*bReleaseMouse=*/false);
	}
}

FText SWidgetAnimTimeline::GetPickingTargetText() const
{
	return PickHoverLabel.IsEmpty()
		? WidgetAnimTips::PickingTargetLabel()
		: FText::Format(LOCTEXT("PickingTargetHovered", "Pick: {0}"), FText::FromString(PickHoverLabel));
}

UWidget* SWidgetAnimTimeline::FindPreviewWidgetAt(const FVector2D& ScreenPosition, FString& OutOption) const
{
	OutOption.Reset();
	if (!Source.IsValid() || !FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	// The preview the mouse is over is the designer's own - the editor's, when it is open; the panel's otherwise.
	UUserWidget* DesignerRoot = nullptr;
	if (FWidgetBlueprintEditor* Editor = FindWidgetBlueprintEditor())
	{
		DesignerRoot = Editor->GetPreview();
	}
	if (!DesignerRoot)
	{
		DesignerRoot = Source->GetPreviewRoot();
	}
	if (!DesignerRoot || !DesignerRoot->WidgetTree)
	{
		return nullptr;
	}

	// Never while over this panel - docked, it may sit beside or over the designer.
	FSlateApplication& Slate = FSlateApplication::Get();
	const FWidgetPath Path = Slate.LocateWindowUnderMouse(ScreenPosition, Slate.GetInteractiveTopLevelWindows(), /*bIgnoreEnabledStatus=*/true);
	for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
	{
		if (&Path.Widgets[Index].Widget.Get() == this)
		{
			return nullptr;
		}
	}

	// A widget that can be a target, as the panel names it - nothing when it can't be.
	auto Resolve = [this](UWidget* DesignerWidget, FString& OutResolvedOption) -> UWidget*
	{
		UWidget* PanelWidget = ToPanelPreviewWidget(DesignerWidget, NAME_None);
		OutResolvedOption = PanelWidget ? Source->FindTargetOptionFor(PanelWidget) : FString();
		return OutResolvedOption.IsEmpty() ? nullptr : PanelWidget;
	};

	// The widget drawn on top under the mouse: of every visible widget of the preview whose box holds the point, the last
	// in paint order - children after their parent, later siblings after earlier ones - that can be a target. Not Slate's
	// hit testing, which only finds widgets that take the mouse: a Text Block or an Image set Not Hit-Testable, the usual
	// for decoration, was skipped for the Canvas Panel or Overlay around it, or for nothing at all. A widget hidden or
	// collapsed hides everything inside it, and a Canvas Panel's children are taken in their Z Order, as it draws them.
	UWidget* Best = nullptr;
	FString BestOption;
	TFunction<void(UWidget*, int32)> Visit;
	Visit = [&](UWidget* Candidate, int32 Depth)
	{
		if (!Candidate || Depth > 64)
		{
			return;
		}
		const TSharedPtr<SWidget> CandidateSlate = Candidate->GetCachedWidget();
		if (!CandidateSlate.IsValid() || !CandidateSlate->GetVisibility().IsVisible())
		{
			return;
		}

		const FGeometry& Geometry = Candidate->GetCachedGeometry();
		const FVector2D Extent(Geometry.GetAbsoluteSize());
		if (Extent.X > 0.0 && Extent.Y > 0.0 && Geometry.IsUnderLocation(ScreenPosition))
		{
			FString CandidateOption;
			if (UWidget* Resolved = Resolve(Candidate, CandidateOption))
			{
				Best = Resolved;
				BestOption = CandidateOption;
			}
		}

		// A nested User Widget's own widgets, then a panel's children in the order they are drawn.
		if (const UUserWidget* Nested = Cast<UUserWidget>(Candidate))
		{
			if (Nested != DesignerRoot && Nested->WidgetTree)
			{
				Visit(Nested->WidgetTree->RootWidget, Depth + 1);
			}
			return;
		}
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Candidate))
		{
			TArray<UWidget*, TInlineAllocator<16>> Children;
			TArray<int32, TInlineAllocator<16>> Orders;
			for (int32 ChildIndex = 0; ChildIndex < Panel->GetChildrenCount(); ++ChildIndex)
			{
				UWidget* Child = Panel->GetChildAt(ChildIndex);
				const UCanvasPanelSlot* CanvasSlot = Child ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
				Children.Add(Child);
				Orders.Add(CanvasSlot ? CanvasSlot->GetZOrder() : 0);
			}
			TArray<int32, TInlineAllocator<16>> Sorted;
			for (int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex)
			{
				Sorted.Add(ChildIndex);
			}
			Algo::StableSort(Sorted, [&Orders](int32 A, int32 B) { return Orders[A] < Orders[B]; });
			for (const int32 ChildIndex : Sorted)
			{
				Visit(Children[ChildIndex], Depth + 1);
			}
		}
	};
	Visit(DesignerRoot->WidgetTree->RootWidget, 0);

	// The preview itself, under the mouse but nothing in it: the widget the animation plays on.
	if (!Best && DesignerRoot->GetCachedWidget().IsValid() && DesignerRoot->GetCachedGeometry().IsUnderLocation(ScreenPosition))
	{
		UUserWidget* PanelRoot = Source->GetPreviewRoot();
		BestOption = PanelRoot ? Source->FindTargetOptionFor(PanelRoot) : FString();
		Best = BestOption.IsEmpty() ? nullptr : PanelRoot;
	}

	OutOption = BestOption;
	return Best;
}

void SWidgetAnimTimeline::UpdatePickHover(const FVector2D& ScreenPosition)
{
	if (PickTargetGroup == INDEX_NONE)
	{
		return;
	}

	FString Option;
	UWidget* Hovered = FindPreviewWidgetAt(ScreenPosition, Option);
	PickHoverWidget = Hovered;
	PickHoverLabel = !Hovered ? FString() : WidgetAnimTargetOption::IsThisWidget(Option) ? Hovered->GetName() : Option;

	if (PickLabelWindow.IsValid())
	{
		if (Hovered)
		{
			PickLabelWindow->MoveWindowTo(ScreenPosition + FVector2D(18.0, 18.0));
			if (!PickLabelWindow->IsVisible())
			{
				PickLabelWindow->ShowWindow();
			}
		}
		else if (PickLabelWindow->IsVisible())
		{
			PickLabelWindow->HideWindow();
		}
	}
}

bool SWidgetAnimTimeline::CommitPickHover()
{
	UWidget* Hovered = PickHoverWidget.Get();
	if (PickTargetGroup == INDEX_NONE || !Hovered)
	{
		return false;
	}

	ApplyPickedTarget(Hovered);
	return true;
}

void SWidgetAnimTimeline::UpdateTargetPick()
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (PickTargetGroup == INDEX_NONE)
	{
		return;
	}
	if (!Groups || !Groups->IsValidIndex(PickTargetGroup) || !CanPickTargetInDesigner())
	{
		CancelTargetPick();
		return;
	}

	// The eyedropper's click is the pick (see FWidgetAnimTargetPickInput); a widget selected in the designer some other
	// way - its hierarchy, say - picks it too.
	UWidget* Now = GetDesignerSelectedWidget();
	if (bPickStartedWithSelection)
	{
		// Still what was selected when the pick began: not picked yet. Anything else - even nothing - and the next
		// selection is the pick, the first one again included.
		if (Now == PickStartSelection.Get())
		{
			return;
		}
		bPickStartedWithSelection = false;
	}
	if (Now)
	{
		ApplyPickedTarget(Now);
	}
}

void SWidgetAnimTimeline::ApplyPickedTarget(UWidget* Picked)
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const int32 GroupIndex = PickTargetGroup;
	CancelTargetPick();
	if (!Picked || !Groups || !Groups->IsValidIndex(GroupIndex) || !Source.IsValid())
	{
		return;
	}

	const FString Option = Source->FindTargetOptionFor(Picked);
	if (Option.IsEmpty())
	{
		FNotificationInfo Info(WidgetAnimTips::PickTargetNotFoundNote());
		Info.ExpireDuration = 4.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FName NewTarget = WidgetAnimTargetOption::ToPath(Option);
	if ((*Groups)[GroupIndex].PreviewTarget == NewTarget)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PickTargetTransaction", "Set Target Widget"));
	if (!BeginEdit())
	{
		return;
	}

	(*Source->GetGroups())[GroupIndex].PreviewTarget = NewTarget;
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

// -----------------------------------------------------------------------------------------------------
// Where a new track goes, and what can stagger
// -----------------------------------------------------------------------------------------------------

float SWidgetAnimTimeline::GetNewTrackStart(TOptional<float> AtSeconds) const
{
	// Added from a menu opened on the Track Editor: where the menu was opened. Otherwise after the track it follows in the
	// sequence - the selected one, or the last - as AddLayer places it, or at 0 for the first.
	if (AtSeconds.IsSet())
	{
		return FMath::Max(AtSeconds.GetValue(), 0.f);
	}

	const int32 InsertAt = GetInsertIndex();
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	if (InsertAt > 0 && Spans.IsValidIndex(InsertAt - 1))
	{
		return Spans[InsertAt - 1].X + Spans[InsertAt - 1].Y + GetLayerFanSeconds(InsertAt - 1);
	}
	return 0.f;
}

bool SWidgetAnimTimeline::CanLayerStagger(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}

	// One it already has stays in view, so it can be seen and taken off.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	if (Layer.Expander.IsValid())
	{
		return true;
	}

	// A value-driven track follows one value, an animation track staggers inside its own animation, a cue fires once.
	if (Layer.IsAnimationTrack() || Layer.IsValueDriven() || IsCueLayer(Layer))
	{
		return false;
	}

	// Only a text track: a type that moves letters (see UWidgetAnimLibrary::TypeTakesParts), played on one of the plugin's
	// animated text widgets, whose letters, words or lines it fans out over. Anywhere else it isn't offered.
	if (!UWidgetAnimLibrary::TypeTakesParts(Layer.AnimType, Layer.TypeParams))
	{
		return false;
	}

	// Nothing to look at - no preview open - so it stays offered on a type that can use it.
	IWidgetAnimPanelSource* PanelSource = Source.Get();
	if (!PanelSource || !PanelSource->GetLayerPreviewWidget(LayerIndex))
	{
		return true;
	}

	UWidget* TextHost = GetLayerTextHost(LayerIndex);
	if (!TextHost)
	{
		return false;
	}

	FWidgetAnimExpander_EachTextPart Parts;
	if (!Parts.IsReadyToExpand(TextHost))
	{
		return true;
	}
	TArray<FWidgetAnimExpandedItem> Items;
	Parts.GatherItems(TextHost, Items);
	return Items.Num() > 0;
}

UWidget* SWidgetAnimTimeline::GetLayerTextHost(int32 LayerIndex) const
{
	IWidgetAnimPanelSource* PanelSource = Source.Get();
	UWidget* Target = PanelSource ? PanelSource->GetLayerPreviewWidget(LayerIndex) : nullptr;

	// One of the plugin's animated text widgets - Animated Text Block, Animated Rich Text Block, their Common UI kin - as the
	// track's widget itself: the letters a Part of the Text is found in, and moved or changed.
	return Target && Cast<IWidgetAnimTextHost>(Target) && Cast<IWidgetAnimPartHost>(Target) ? Target : nullptr;
}

bool SWidgetAnimTimeline::CanLayerTargetText(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return false;
	}

	// One already typed stays in view, so it can be seen and cleared.
	const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
	if (Layer.TextTarget.IsSet())
	{
		return true;
	}

	// A type that moves letters, or a text track that changes them; never an animation track or a cue.
	if (Layer.IsAnimationTrack() || IsCueLayer(Layer))
	{
		return false;
	}
	const FWidgetAnimEvaluator* Evaluator = Layer.AnimType == EWidgetAnimType::Custom ? Layer.FindEvaluator() : nullptr;
	if (!UWidgetAnimLibrary::TypeMovesParts(Layer.AnimType) && !(Evaluator && Evaluator->TakesTextRange(Layer.TypeParams)))
	{
		return false;
	}
	return GetLayerTextHost(LayerIndex) != nullptr;
}

bool SWidgetAnimTimeline::IsTextTargetMissing(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	UWidget* Host = GetLayerTextHost(LayerIndex);
	if (!Layers || !Layers->IsValidIndex(LayerIndex) || !Host || !(*Layers)[LayerIndex].TextTarget.IsSet())
	{
		return false;
	}
	TArray<FIntPoint> Ranges;
	(*Layers)[LayerIndex].TextTarget.FindLetterRanges(WidgetAnimText::GetDisplayString(Host), Ranges);
	return Ranges.Num() == 0;
}

FText SWidgetAnimTimeline::GetTextTargetNote(int32 LayerIndex) const
{
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex))
	{
		return FText::GetEmpty();
	}

	const FWidgetAnimTextTarget& TextTarget = (*Layers)[LayerIndex].TextTarget;
	UWidget* Host = GetLayerTextHost(LayerIndex);
	if (!TextTarget.IsSet())
	{
		return Host ? WidgetAnimTips::TextTargetEmptyNote() : FText::GetEmpty();
	}
	if (!Host)
	{
		return WidgetAnimTips::TextTargetNoTextNote();
	}

	// Where it is in the text the preview shows, in its own words: how many places, and the text around the first.
	const FString Display = WidgetAnimText::GetDisplayString(Host);
	TArray<FIntPoint> Ranges;
	TextTarget.FindLetterRanges(Display, Ranges);
	if (Ranges.Num() == 0)
	{
		return FText::Format(WidgetAnimTips::TextTargetMissingNote(), FText::FromString(TextTarget.Text), FText::FromString(Display.Left(80)));
	}

	// Several parts joined with &&: any one of them not found is named, so a typo in one doesn't hide behind the rest.
	TArray<FString> Parts;
	TextTarget.GetParts(Parts);
	if (Parts.Num() > 1)
	{
		for (const FString& Part : Parts)
		{
			FWidgetAnimTextTarget One = TextTarget;
			One.Text = Part;
			TArray<FIntPoint> PartRanges;
			One.FindLetterRanges(Display, PartRanges);
			if (PartRanges.Num() == 0)
			{
				return FText::Format(WidgetAnimTips::TextTargetPartMissingNote(), FText::AsNumber(Ranges.Num()), FText::FromString(Part));
			}
		}
	}
	return FText::Format(WidgetAnimTips::TextTargetFoundNote(), FText::AsNumber(Ranges.Num()), FText::AsNumber(Ranges[0].Y));
}

// -----------------------------------------------------------------------------------------------------
// Playing from the end
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::GoToStart()
{
	ScrubToSeconds(0.f);
}

void SWidgetAnimTimeline::GoToEnd()
{
	ScrubToSeconds(GetPassSeconds());

	// Play goes back the way it came - from the end to the start - until the playhead is moved some other way.
	bPlayBackwardNext = true;
}

// -----------------------------------------------------------------------------------------------------
// Menus
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::AddInsertAnimationEntry(FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds)
{
	// With when it will start, as Add Animation Track says: where the menu was opened, or after the track it follows.
	Menu.AddSubMenu(
		FText::Format(LOCTEXT("InsertAnimationAt", "Insert Existing Widget Animation ({0})"), FText::FromString(FormatTime(GetNewTrackStart(AtSeconds)))),
		WidgetAnimTips::InsertAnimationTooltip(),
		FNewMenuDelegate::CreateLambda([this, GroupIndex, AtSeconds](FMenuBuilder& SubMenu)
		{
			FAssetPickerConfig Config;
			Config.Filter.ClassPaths.Add(UWidgetAnimPresetAsset::StaticClass()->GetClassPathName());
			Config.Filter.bRecursiveClasses = true;
			Config.InitialAssetViewType = EAssetViewType::List;
			Config.bAllowNullSelection = false;
			Config.bFocusSearchBoxWhenOpened = true;
			Config.SelectionMode = ESelectionMode::Single;

			// Not this animation, nor one already playing it: an animation can't play itself. Only those already loaded
			// are looked into - loading every animation to list them would be slow - and InsertAnimation turns away any
			// other that would.
			const TWeakObjectPtr<const UObject> WeakEdited = Source.IsValid() ? Source->GetEditedObject() : nullptr;
			Config.OnShouldFilterAsset = FOnShouldFilterAsset::CreateLambda([WeakEdited](const FAssetData& AssetData)
			{
				const UObject* Edited = WeakEdited.Get();
				if (!Edited)
				{
					return false;
				}
				if (AssetData.GetSoftObjectPath() == FSoftObjectPath(Edited))
				{
					return true;
				}
				const UWidgetAnimPresetAsset* Loaded = Cast<UWidgetAnimPresetAsset>(AssetData.FastGetAsset(/*bLoad=*/false));
				return Loaded && UWidgetAnimLibrary::AnimationPlays(Loaded, Edited);
			});

			Config.OnAssetSelected = FOnAssetSelected::CreateLambda([this, GroupIndex, AtSeconds](const FAssetData& AssetData)
			{
				FSlateApplication::Get().DismissAllMenus();
				InsertAnimation(Cast<UWidgetAnimPresetAsset>(AssetData.GetAsset()), GroupIndex, AtSeconds);
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

namespace WidgetAnimPresetMenu
{
	/** The name a collapsed track is given when it is made - and numbered from - which says nothing about what it does. */
	bool IsDefaultCollapsedName(const FString& Name)
	{
		const FString Default = TEXT("Collapsed Tracks");
		if (Name.Equals(Default, ESearchCase::IgnoreCase))
		{
			return true;
		}
		return Name.StartsWith(Default + TEXT(" "), ESearchCase::IgnoreCase) && Name.Mid(Default.Len() + 1).IsNumeric();
	}

	/** A preset's tracks, as the menus describe them: a single track's type, or how many a collapsed one holds. */
	FText Describe(const WidgetAnimTrackPresets::FPreset& Preset)
	{
		return Preset.bCollapsed
			? FText::Format(LOCTEXT("CollapsedPresetDescription", "Collapsed, {0} {0}|plural(one=track,other=tracks)"), FText::AsNumber(Preset.Layers.Num()))
			: SWidgetAnimTimeline::GetLayerTypeName(Preset.Layers[0]);
	}

	/** A menu row for a preset: a bar in its track's color, its name, and what it is, subdued. */
	TSharedRef<SWidget> MakeRow(const WidgetAnimTrackPresets::FPreset& Preset)
	{
		const FLinearColor Color = Preset.bCollapsed ? NameTBD::Colors::WidgetAnimEditor::LayerCollapsed : SWidgetAnimTimeline::GetLayerTypeColor(Preset.Layers[0]);
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 6.f, 0.f))
			[
				SNew(SBox)
				.WidthOverride(3.f)
				.HeightOverride(12.f)
				[
					SNew(SImage)
					.Image(WidgetAnimTimeline::WhiteBrush())
					.ColorAndOpacity(Color)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 16.f, 0.f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Preset.Name))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Describe(Preset))
			];
	}

	/** A note in a menu, where there is nothing to list. */
	TSharedRef<SWidget> MakeNote(const FText& Text)
	{
		return SNew(SBox)
			.Padding(FMargin(8.f, 2.f, 8.f, 4.f))
			.MaxDesiredWidth(300.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(Text)
			];
	}
}

void SWidgetAnimTimeline::AddTrackSubMenu(FMenuBuilder& Menu, int32 GroupIndex, TOptional<float> AtSeconds)
{
	// Named after its group only where there are groups to tell apart.
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const bool bNameGroup = Groups && Groups->IsValidIndex(GroupIndex) && (!Source.IsValid() || Source->SupportsLayerGroups());
	// With when it will start: where the menu was opened, or after the track it follows.
	const FText StartsAt = FText::FromString(FormatTime(GetNewTrackStart(AtSeconds)));
	const FText Label = bNameGroup
		? FText::Format(LOCTEXT("AddTrackToGroupAt", "Add Animation Track to {0} ({1})"), GetGroupName(GroupIndex), StartsAt)
		: FText::Format(LOCTEXT("AddTrackAt", "Add Animation Track ({0})"), StartsAt);

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
	// A saved track preset, first: a new track made from it, starting where any new track would.
	Menu.AddSubMenu(
		LOCTEXT("AddFromPreset", "From Preset"),
		WidgetAnimTips::AddTrackFromPresetTooltip(),
		FNewMenuDelegate::CreateLambda([this, GroupIndex, AtSeconds](FMenuBuilder& SubMenu)
		{
			TArray<WidgetAnimTrackPresets::FPreset> Presets;
			WidgetAnimTrackPresets::GetPresets(Presets);

			SubMenu.BeginSection("Presets", LOCTEXT("AddPresetsSection", "Presets"));
			if (Presets.Num() == 0)
			{
				SubMenu.AddWidget(WidgetAnimPresetMenu::MakeNote(WidgetAnimTips::NoTrackPresetsToAddNote()), FText::GetEmpty(), /*bNoIndent=*/true);
			}
			for (const WidgetAnimTrackPresets::FPreset& Preset : Presets)
			{
				SubMenu.AddMenuEntry(
					FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, AtSeconds, Preset]() { AddLayersFromPreset(GroupIndex, Preset, AtSeconds); })),
					WidgetAnimPresetMenu::MakeRow(Preset),
					NAME_None,
					FText::Format(Preset.bCollapsed ? WidgetAnimTips::AddCollapsedFromNamedPresetTooltip() : WidgetAnimTips::AddTrackFromNamedPresetTooltip(), FText::FromString(Preset.Name)));
			}
			SubMenu.EndSection();

			SubMenu.BeginSection("PresetFiles", LOCTEXT("AddPresetFilesSection", "Your Presets"));
			SubMenu.AddMenuEntry(
				LOCTEXT("AddOpenTrackPresetsFolder", "Open Presets Folder"),
				WidgetAnimTips::OpenTrackPresetsFolderTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FPlatformProcess::ExploreFolder(*WidgetAnimTrackPresets::GetFolder(/*bCreate=*/true));
				})));
			SubMenu.EndSection();
		}),
		/*bInOpenSubMenuOnClick=*/false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));
	Menu.AddMenuSeparator();

	// The built-in types, grouped by what they do: Standard - the everyday motions; Transformation - the keyframed poses and
	// shapes; Visual - how the widget looks. Any built-in type in none of them (one added later) follows under Other.
	struct FTypeSection
	{
		FName Name;
		FText Heading;
		TArray<EWidgetAnimType> Types;
	};
	const FTypeSection Sections[] =
	{
		{ "Standard", LOCTEXT("StandardTypesSection", "Standard"), { EWidgetAnimType::Fade, EWidgetAnimType::Scale, EWidgetAnimType::Slide, EWidgetAnimType::Rotate, EWidgetAnimType::Pulse, EWidgetAnimType::Shake } },
		{ "Transformation", LOCTEXT("TransformationTypesSection", "Transformation"), { EWidgetAnimType::Transform, EWidgetAnimType::ShapeBox, EWidgetAnimType::Shape } },
		{ "Visual", LOCTEXT("VisualTypesSection", "Visual"), { EWidgetAnimType::Design, EWidgetAnimType::MaterialScalar, EWidgetAnimType::MaterialVector } },
	};
	TArray<EWidgetAnimType> Offered;
	WidgetAnimTimeline::ForEachAnimType([&Offered](EWidgetAnimType AnimType) { Offered.Add(AnimType); });

	auto AddTypeEntry = [this, &Menu, GroupIndex, AtSeconds](EWidgetAnimType AnimType)
	{
		Menu.AddMenuEntry(
			GetAnimTypeName(AnimType),
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, AnimType, AtSeconds]() { AddLayer(GroupIndex, AnimType, nullptr, AtSeconds); })));
	};
	for (const FTypeSection& Section : Sections)
	{
		Menu.BeginSection(Section.Name, Section.Heading);
		for (const EWidgetAnimType AnimType : Section.Types)
		{
			if (Offered.Remove(AnimType) > 0)
			{
				AddTypeEntry(AnimType);
			}
		}
		Menu.EndSection();
	}
	if (Offered.Num() > 0)
	{
		Menu.BeginSection("OtherTypes", LOCTEXT("OtherTypesSection", "Other"));
		for (const EWidgetAnimType AnimType : Offered)
		{
			AddTypeEntry(AnimType);
		}
		Menu.EndSection();
	}

	// Registered types after the built-in ones, under the heading each names for itself - Text for the text tracks -
	// in heading order. One of the plugin's own naming none is a track type like the built-in ones, under Other Tracks; one
	// another module registers, naming none, is a User-defined Track, under that heading last. Each is a type in its own
	// right, whichever module it came from.
	static const FString PluginUncategorized(TEXT("@OtherTracks"));
	static const FString UserUncategorized(TEXT("@UserDefinedTracks"));
	TArray<FString> Categories;
	TMap<FString, TArray<const FWidgetAnimEvaluator*>> ByCategory;
	WidgetAnimTimeline::ForEachRegisteredType([&Categories, &ByCategory](const FWidgetAnimEvaluator& Evaluator)
	{
		FString Category = Evaluator.GetCategory().ToString();
		if (Category.IsEmpty())
		{
			Category = WidgetAnimTimeline::IsUserDefinedTrackType(Evaluator.GetParamsStruct()) ? UserUncategorized : PluginUncategorized;
		}
		if (!ByCategory.Contains(Category))
		{
			Categories.Add(Category);
		}
		ByCategory.FindOrAdd(Category).Add(&Evaluator);
	});
	Categories.Sort([](const FString& A, const FString& B)
	{
		// The two headings of no name of their own after every named one, Other Tracks before User-defined Tracks.
		const bool bUnnamedA = A == PluginUncategorized || A == UserUncategorized;
		const bool bUnnamedB = B == PluginUncategorized || B == UserUncategorized;
		return bUnnamedA != bUnnamedB ? bUnnamedB : A < B;
	});

	for (const FString& Category : Categories)
	{
		const FText Heading = Category == UserUncategorized ? LOCTEXT("UserDefinedTracksSection", "User-defined Tracks")
			: Category == PluginUncategorized ? LOCTEXT("OtherTracksSection", "Other Tracks")
			: FText::FromString(Category);
		Menu.BeginSection(NAME_None, Heading);
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

	// And Blueprint cues, found the same way.
	bool bFirstBlueprintCue = true;
	WidgetAnimTimeline::ForEachBlueprintCue([this, &Menu, GroupIndex, AtSeconds, &bFirstBlueprintCue](const FTopLevelAssetPath& ClassPath, const FText& CueName)
	{
		if (bFirstBlueprintCue)
		{
			Menu.BeginSection(NAME_None, LOCTEXT("BlueprintCuesSection", "Blueprint Cues"));
			bFirstBlueprintCue = false;
		}

		Menu.AddMenuEntry(
			CueName,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, ClassPath, AtSeconds]()
			{
				UClass* CueClass = LoadObject<UClass>(nullptr, *ClassPath.ToString());
				if (CueClass && CueClass->IsChildOf(UWidgetAnimBlueprintCue::StaticClass()))
				{
					const FInstancedStruct InitialParams = FWidgetAnimBlueprintCueParams::MakeFor(CueClass);
					AddLayer(GroupIndex, EWidgetAnimType::Custom, &InitialParams, AtSeconds);
				}
			})));
	});
	if (!bFirstBlueprintCue)
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
		// their own, and a menu section can't be opened inside another. Headed with when the track will start.
		AddMenuHeading(Menu,
			FText::Format(LOCTEXT("AddTrackHeadingAt", "Add Animation Track ({0})"), FText::FromString(FormatTime(GetNewTrackStart(AtSeconds)))),
			WidgetAnimTimeline::Palette::MenuAddIconTint);
		AddTrackTypeEntries(Menu, GetDefaultGroup(GroupIndex), AtSeconds);

		Menu.BeginSection("Existing", LOCTEXT("ExistingSection", "Build On"));
		AddInsertAnimationEntry(Menu, GetDefaultGroup(GroupIndex), AtSeconds);
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
	AddInsertAnimationEntry(Menu, GetDefaultGroup(GroupIndex), AtSeconds);
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

bool SWidgetAnimTimeline::GetPresetSource(const TArray<int32>& LayerIndices, FString& OutName, TArray<int32>& OutLayers, bool& bOutCollapsed, FText& OutWhyNot) const
{
	OutName.Reset();
	OutLayers.Reset();
	bOutCollapsed = false;

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	TArray<int32> Picked;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Layers && Layers->IsValidIndex(LayerIndex))
		{
			Picked.AddUnique(LayerIndex);
		}
	}
	if (Picked.Num() == 0)
	{
		OutWhyNot = WidgetAnimTips::PresetRenameTrackNote();
		return false;
	}
	Picked.Sort();

	for (const int32 LayerIndex : Picked)
	{
		if (!WidgetAnimTrackPresets::CanSave((*Layers)[LayerIndex]))
		{
			OutWhyNot = WidgetAnimTips::PresetAnimationTrackNote();
			return false;
		}
	}

	// Every track of one collapsed track: saved as one, under its name.
	const FName Collapsed = (*Layers)[Picked[0]].CollapsedTrack;
	bool bOneCollapsed = !Collapsed.IsNone();
	for (const int32 LayerIndex : Picked)
	{
		bOneCollapsed &= (*Layers)[LayerIndex].CollapsedTrack == Collapsed;
	}
	TArray<int32> Members;
	if (bOneCollapsed)
	{
		GetCollapsedMembers(Collapsed, Members);
		Members.Sort();
	}
	const bool bWholeCollapsed = bOneCollapsed && Members == Picked && (Collapsed != ActiveCollapsed || Picked.Num() > 1);

	if (bWholeCollapsed)
	{
		OutName = Collapsed.ToString();
		if (WidgetAnimPresetMenu::IsDefaultCollapsedName(OutName))
		{
			OutWhyNot = WidgetAnimTips::PresetRenameCollapsedNote();
			return false;
		}
		OutLayers = Members;
		bOutCollapsed = true;
		return true;
	}

	if (Picked.Num() > 1)
	{
		OutWhyNot = WidgetAnimTips::PresetSeveralTracksNote();
		return false;
	}

	OutName = (*Layers)[Picked[0]].TrackName.TrimStartAndEnd();
	if (OutName.IsEmpty())
	{
		OutWhyNot = WidgetAnimTips::PresetRenameTrackNote();
		return false;
	}
	OutLayers = Picked;
	return true;
}

void SWidgetAnimTimeline::AddPresetEntries(FMenuBuilder& Menu, const TArray<int32>& LayerIndices, bool bApply)
{
	TArray<WidgetAnimTrackPresets::FPreset> Presets;
	WidgetAnimTrackPresets::GetPresets(Presets);

	// Putting a single track's preset on the track - in the Selection panel, never in its menu, which is for saving.
	if (bApply)
	{
		const TArray<FWidgetAnimLayer>* Layers = GetLayers();
		TArray<int32> Targets;
		bool bAnyEditable = false;
		for (const int32 LayerIndex : LayerIndices)
		{
			if (Layers && Layers->IsValidIndex(LayerIndex) && !(*Layers)[LayerIndex].IsAnimationTrack())
			{
				Targets.Add(LayerIndex);
				bAnyEditable |= CanEditLayer(LayerIndex);
			}
		}

		Menu.BeginSection("Presets", LOCTEXT("ApplyPresetsSection", "Apply a Preset"));
		bool bAnyListed = false;
		for (const WidgetAnimTrackPresets::FPreset& Preset : Presets)
		{
			if (Preset.bCollapsed || Preset.Layers.Num() != 1)
			{
				continue;
			}
			bAnyListed = true;
			const FWidgetAnimLayer PresetLayer = Preset.Layers[0];
			const FString PresetName = Preset.Name;
			Menu.AddMenuEntry(
				FUIAction(
					FExecuteAction::CreateLambda([this, Targets, PresetLayer, PresetName]() { ApplyLayerPreset(Targets, PresetLayer, PresetName); }),
					FCanExecuteAction::CreateLambda([bAnyEditable]() { return bAnyEditable; })),
				WidgetAnimPresetMenu::MakeRow(Preset),
				NAME_None,
				FText::Format(WidgetAnimTips::ApplyTrackPresetTooltip(), FText::FromString(PresetName)));
		}
		if (!bAnyListed)
		{
			Menu.AddWidget(WidgetAnimPresetMenu::MakeNote(WidgetAnimTips::NoTrackPresetsNote()), FText::GetEmpty(), /*bNoIndent=*/true);
		}
		Menu.EndSection();
	}

	Menu.BeginSection("PresetFiles", LOCTEXT("PresetFilesSection", "Your Presets"));

	// Saved under the name the track - or collapsed track - was given; until it has one, the entry says to give it one.
	FString SaveName;
	TArray<int32> SaveLayers;
	bool bSaveCollapsed = false;
	FText WhyNot;
	const bool bCanSave = GetPresetSource(LayerIndices, SaveName, SaveLayers, bSaveCollapsed, WhyNot);
	Menu.AddMenuEntry(
		bCanSave ? FText::Format(LOCTEXT("SaveTrackPresetNamed", "Save as Preset \"{0}\""), FText::FromString(SaveName)) : LOCTEXT("SaveTrackPreset", "Save as Preset"),
		bCanSave ? WidgetAnimTips::SaveTrackPresetTooltip() : WhyNot,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
		FUIAction(
			FExecuteAction::CreateLambda([this, LayerIndices]() { SaveLayersAsPreset(LayerIndices); }),
			FCanExecuteAction::CreateLambda([bCanSave]() { return bCanSave; })));

	if (Presets.Num() > 0)
	{
		Menu.AddSubMenu(
			LOCTEXT("DeleteTrackPreset", "Delete Preset"),
			WidgetAnimTips::DeleteTrackPresetTooltip(),
			FNewMenuDelegate::CreateLambda([Presets](FMenuBuilder& SubMenu)
			{
				for (const WidgetAnimTrackPresets::FPreset& Preset : Presets)
				{
					SubMenu.AddMenuEntry(
						FUIAction(FExecuteAction::CreateLambda([Preset]()
						{
							const FText Question = FText::Format(LOCTEXT("DeleteTrackPresetQuestion", "Delete the track preset \"{0}\"? Tracks made from it keep their settings."), FText::FromString(Preset.Name));
							if (FMessageDialog::Open(EAppMsgType::YesNo, Question) == EAppReturnType::Yes)
							{
								WidgetAnimTrackPresets::Delete(Preset);
							}
						})),
						WidgetAnimPresetMenu::MakeRow(Preset));
				}
			}),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon));
	}

	Menu.AddMenuEntry(
		LOCTEXT("OpenTrackPresetsFolder", "Open Presets Folder"),
		WidgetAnimTips::OpenTrackPresetsFolderTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FPlatformProcess::ExploreFolder(*WidgetAnimTrackPresets::GetFolder(/*bCreate=*/true));
		})));
	Menu.EndSection();
}

void SWidgetAnimTimeline::ApplyLayerPreset(const TArray<int32>& LayerIndices, const FWidgetAnimLayer& Preset, const FString& PresetName)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	TArray<int32> Targets;
	for (const int32 LayerIndex : LayerIndices)
	{
		if (Current && Current->IsValidIndex(LayerIndex) && !(*Current)[LayerIndex].IsAnimationTrack() && CanEditLayer(LayerIndex))
		{
			Targets.Add(LayerIndex);
		}
	}
	if (Targets.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("ApplyTrackPresetTransaction", "Apply Track Preset {0}"), FText::FromString(PresetName)));
	if (!BeginEdit())
	{
		return;
	}

	TArray<FWidgetAnimLayer>& Layers = *Source->GetLayers();
	for (const int32 LayerIndex : Targets)
	{
		WidgetAnimTrackPresets::ApplyTo(Preset, Layers[LayerIndex]);
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
	RefreshDetails();
}

void SWidgetAnimTimeline::SaveLayersAsPreset(const TArray<int32>& LayerIndices)
{
	FString Name;
	TArray<int32> SaveLayers;
	bool bCollapsed = false;
	FText Error;
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !GetPresetSource(LayerIndices, Name, SaveLayers, bCollapsed, Error))
	{
		FNotificationInfo Info(Error);
		Info.ExpireDuration = 6.f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FString File = WidgetAnimTrackPresets::GetPresetFile(Name);
	if (!File.IsEmpty() && FPaths::FileExists(File))
	{
		const FText Question = FText::Format(LOCTEXT("ReplaceTrackPresetQuestion", "There is already a track preset called \"{0}\". Replace it?"), FText::FromString(FPaths::GetBaseFilename(File)));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return;
		}
	}

	// Each with its start, so a collapsed track's tracks keep their timing relative to each other.
	TArray<float> Starts;
	GetAuthoredStarts(Starts);
	TArray<FWidgetAnimLayer> PresetLayers;
	TArray<float> PresetStarts;
	for (const int32 LayerIndex : SaveLayers)
	{
		PresetLayers.Add((*Layers)[LayerIndex]);
		PresetStarts.Add(Starts.IsValidIndex(LayerIndex) ? Starts[LayerIndex] : 0.f);
	}

	const bool bSaved = WidgetAnimTrackPresets::Save(Name, PresetLayers, PresetStarts, bCollapsed, Error);
	FNotificationInfo Info(bSaved
		? FText::Format(LOCTEXT("TrackPresetSaved", "Saved the track preset \"{0}\"."), FText::FromString(FPaths::GetBaseFilename(File)))
		: Error);
	Info.ExpireDuration = bSaved ? 3.f : 6.f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void SWidgetAnimTimeline::AddLayersFromPreset(int32 GroupIndex, const WidgetAnimTrackPresets::FPreset& Preset, TOptional<float> AtSeconds)
{
	if (Preset.Layers.Num() == 0)
	{
		return;
	}

	// One track: added as any new track is, under the preset's name.
	if (!Preset.bCollapsed && Preset.Layers.Num() == 1)
	{
		const FWidgetAnimLayer& PresetLayer = Preset.Layers[0];
		AddLayer(GroupIndex, PresetLayer.AnimType, &PresetLayer.TypeParams, AtSeconds, &PresetLayer);
		return;
	}

	// Where the first one starts, worked out before anything moves: as a new track would, in authored seconds.
	const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);
	const float StartAt = FMath::RoundToFloat(GetNewTrackStart(AtSeconds) / TimeScale * 100.f) / 100.f;

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("AddPresetTracksTransaction", "Add Tracks from Preset {0}"), FText::FromString(Preset.Name)));
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
	GroupIndex = GetDefaultGroup(GroupIndex);
	const bool bSupportsGroups = Source->SupportsLayerGroups();

	// A collapsed track of the preset's name - numbered if this animation has one - or, inside one open on its own, its tracks.
	const FName CollapsedName = IsInCollapsedScope() ? ActiveCollapsed : WidgetAnimCollapsed::MakeUniqueName(Layers, Preset.Name);

	const int32 InsertAt = FMath::Clamp(GetInsertIndex(), 0, Layers.Num());
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);

	// Each With the one above it and delayed from its start, so each lands where it was relative to the first.
	float AnchorStart = OldStarts.IsValidIndex(InsertAt - 1) ? OldStarts[InsertAt - 1] : 0.f;
	for (int32 Index = 0; Index < Preset.Layers.Num(); ++Index)
	{
		FWidgetAnimLayer NewLayer = Preset.Layers[Index];
		const float Start = StartAt + (Preset.Starts.IsValidIndex(Index) ? Preset.Starts[Index] : 0.f);
		NewLayer.Start = EWidgetAnimLayerStart::WithPrevious;
		NewLayer.Delay = Start - AnchorStart;
		AnchorStart = Start;
		NewLayer.LayerGroup = bSupportsGroups ? GroupIndex : 0;
		NewLayer.EditorLine = INDEX_NONE;
		NewLayer.CollapsedTrack = CollapsedName;

		Layers.Insert(NewLayer, InsertAt + Index);
		EnsureUniqueTrackName(Layers, InsertAt + Index);
	}
	PinLayerStarts(OldStarts, OldIndicesAfterInserting(Layers.Num(), InsertAt, Preset.Layers.Num()));
	TrackCollapsedGroups.Remove(GroupIndex);
	ListCollapsedGroups.Remove(GroupIndex);

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RebuildRows();
	SelectCollapsed(CollapsedName);
}

namespace WidgetAnimFolderMenu
{
	/**
	 * A name typed into a menu - a new folder's, or a folder's new name: Enter applies it and closes the menu, Escape or
	 * clicking away leaves things as they are.
	 */
	TSharedRef<SWidget> MakeNameBox(const FText& Initial, const FText& Hint, TFunction<void(const FString&)> OnCommit, bool bAllowEmpty = false)
	{
		return SNew(SBox)
			.Padding(FMargin(8.f, 2.f, 8.f, 4.f))
			.MinDesiredWidth(200.f)
			[
				SNew(SEditableTextBox)
				.Text(Initial)
				.HintText(Hint)
				.SelectAllTextWhenFocused(true)
				.OnTextCommitted_Lambda([OnCommit, bAllowEmpty](const FText& Text, ETextCommit::Type CommitType)
				{
					// bAllowEmpty: Enter with nothing typed still commits - the caller names it.
					const FString Name = Text.ToString().TrimStartAndEnd();
					if (CommitType != ETextCommit::OnEnter || (Name.IsEmpty() && !bAllowEmpty))
					{
						return;
					}
					FSlateApplication::Get().DismissAllMenus();
					OnCommit(Name);
				})
			];
	}
}

void SWidgetAnimTimeline::AddFolderEntries(FMenuBuilder& Menu, const TArray<int32>& GroupIndices)
{
	if (!Source.IsValid() || !Source->SupportsLayerGroups() || !CanEdit() || GroupIndices.Num() == 0)
	{
		return;
	}

	bool bAnyInFolder = false;
	for (const int32 GroupIndex : GroupIndices)
	{
		bAnyInFolder |= !GetGroupFolder(GroupIndex).IsEmpty();
	}

	Menu.AddSubMenu(
		LOCTEXT("MoveToFolder", "Put in List Folder"),
		WidgetAnimTips::MoveToFolderTooltip(),
		FNewMenuDelegate::CreateLambda([this, GroupIndices](FMenuBuilder& SubMenu)
		{
			TArray<FString> Folders;
			GetFolders(Folders);
			if (Folders.Num() > 0)
			{
				SubMenu.BeginSection("Folders", LOCTEXT("FoldersSection", "List Folders"));
				for (const FString& Folder : Folders)
				{
					SubMenu.AddMenuEntry(
						FText::FromString(Folder),
						FText::GetEmpty(),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed"),
						FUIAction(
							FExecuteAction::CreateLambda([this, GroupIndices, Folder]() { SetGroupsFolder(GroupIndices, Folder); }),
							FCanExecuteAction::CreateLambda([this, GroupIndices, Folder]()
							{
								return !Algo::AllOf(GroupIndices, [this, &Folder](int32 GroupIndex) { return GetGroupFolder(GroupIndex) == Folder; });
							})));
				}
				SubMenu.EndSection();
			}

			SubMenu.BeginSection("NewFolder", LOCTEXT("NewFolderSection", "New List Folder"));
			SubMenu.AddWidget(
				WidgetAnimFolderMenu::MakeNameBox(FText::GetEmpty(), WidgetAnimTips::NewFolderHint(), [this, GroupIndices](const FString& Name) { SetGroupsFolder(GroupIndices, Name); }),
				FText::GetEmpty(), /*bNoIndent=*/true);
			SubMenu.EndSection();
		}),
		/*bInOpenSubMenuOnClick=*/false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderClosed"));

	if (bAnyInFolder)
	{
		Menu.AddMenuEntry(
			LOCTEXT("RemoveFromFolder", "Take Out of List Folder"),
			WidgetAnimTips::RemoveFromFolderTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndices]() { SetGroupsFolder(GroupIndices, FString()); })));
	}
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeGroupsContextMenu(const TArray<int32>& GroupIndices)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	AddMenuHeading(Menu,
		FText::Format(LOCTEXT("GroupsHeading", "{0} Layer Groups"), FText::AsNumber(GroupIndices.Num())),
		GetGroupColor(GroupIndices.Num() > 0 ? GroupIndices[0] : 0));

	// Every track of every one of them, to hide or show.
	TArray<int32> Members;
	bool bAnyShown = false;
	bool bAnyHidden = false;
	if (const TArray<FWidgetAnimLayer>* Layers = GetLayers())
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			if (GroupIndices.Contains(GetLayerGroup(LayerIndex)))
			{
				Members.Add(LayerIndex);
				(IsLayerHidden(LayerIndex) ? bAnyHidden : bAnyShown) = true;
			}
		}
	}
	const bool bAllFolded = Algo::AllOf(GroupIndices, [this](int32 GroupIndex) { return IsTrackGroupCollapsed(GroupIndex); });

	Menu.BeginSection("View", LOCTEXT("GroupsViewSection", "View"));
	if (bAnyShown)
	{
		Menu.AddMenuEntry(
			LOCTEXT("HideGroups", "Hide Groups"),
			WidgetAnimTips::HideGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Hidden"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/true); })));
	}
	if (bAnyHidden)
	{
		Menu.AddMenuEntry(
			LOCTEXT("ShowGroups", "Show Groups"),
			WidgetAnimTips::ShowGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/false); })));
	}
	Menu.AddMenuEntry(
		bAllFolded ? LOCTEXT("ExpandGroups", "Unfold in Track Editor") : LOCTEXT("CollapseGroups", "Fold in Track Editor"),
		WidgetAnimTips::CollapseTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndices, bAllFolded]()
		{
			for (const int32 GroupIndex : GroupIndices)
			{
				if (bAllFolded)
				{
					TrackCollapsedGroups.Remove(GroupIndex);
				}
				else
				{
					TrackCollapsedGroups.Add(GroupIndex);
				}
			}
			UpdateTrackLayout();
		})));
	Menu.EndSection();

	if (!CanEdit() || !Source.IsValid() || !Source->SupportsLayerGroups())
	{
		return Menu.MakeWidget();
	}

	Menu.BeginSection("Groups", LOCTEXT("GroupsSectionContext", "Layer Groups"));
	AddFolderEntries(Menu, GroupIndices);
	Menu.AddMenuEntry(
		LOCTEXT("DeleteGroups", "Delete Groups"),
		WidgetAnimTips::DeleteGroupsTooltip(),
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndices]() { DeleteGroups(GroupIndices); })));
	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeFolderContextMenu(const FString& Folder)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	AddMenuHeading(Menu, FText::FromString(Folder), NameTBD::Colors::WidgetAnimEditor::ListFolderAccent);

	TArray<int32> FolderGroups;
	GetFolderGroups(Folder, FolderGroups);
	TArray<int32> Members;
	bool bAnyShown = false;
	bool bAnyHidden = false;
	if (const TArray<FWidgetAnimLayer>* Layers = GetLayers())
	{
		for (int32 LayerIndex = 0; LayerIndex < Layers->Num(); ++LayerIndex)
		{
			if (FolderGroups.Contains(GetLayerGroup(LayerIndex)))
			{
				Members.Add(LayerIndex);
				(IsLayerHidden(LayerIndex) ? bAnyHidden : bAnyShown) = true;
			}
		}
	}
	const bool bAllFolded = FolderGroups.Num() > 0 && Algo::AllOf(FolderGroups, [this](int32 GroupIndex) { return IsTrackGroupCollapsed(GroupIndex); });

	Menu.BeginSection("View", LOCTEXT("FolderViewSection", "View"));
	Menu.AddMenuEntry(
		LOCTEXT("SelectFolderGroups", "Select Groups"),
		WidgetAnimTips::SelectFolderGroupsTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(
			FExecuteAction::CreateLambda([this, FolderGroups]() { SelectGroups(FolderGroups, FolderGroups[0]); }),
			FCanExecuteAction::CreateLambda([FolderGroups]() { return FolderGroups.Num() > 0; })));
	if (bAnyShown)
	{
		Menu.AddMenuEntry(
			LOCTEXT("HideFolderGroups", "Hide Groups"),
			WidgetAnimTips::HideGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Hidden"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/true); })));
	}
	if (bAnyHidden)
	{
		Menu.AddMenuEntry(
			LOCTEXT("ShowFolderGroups", "Show Groups"),
			WidgetAnimTips::ShowGroupTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this, Members]() { SetLayersHidden(Members, /*bHidden=*/false); })));
	}
	Menu.AddMenuEntry(
		IsListFolderCollapsed(Folder) ? LOCTEXT("UnfoldFolder", "Unfold List Folder") : LOCTEXT("FoldFolder", "Fold List Folder"),
		WidgetAnimTips::ListFolderFoldTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, Folder]() { ToggleListFolderCollapsed(Folder); })));
	Menu.AddMenuEntry(
		bAllFolded ? LOCTEXT("UnfoldFolderGroups", "Unfold Groups in Track Editor") : LOCTEXT("FoldFolderGroups", "Fold Groups in Track Editor"),
		WidgetAnimTips::FoldFolderGroupsTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, FolderGroups, bAllFolded]()
		{
			for (const int32 GroupIndex : FolderGroups)
			{
				if (bAllFolded)
				{
					TrackCollapsedGroups.Remove(GroupIndex);
				}
				else
				{
					TrackCollapsedGroups.Add(GroupIndex);
				}
			}
			UpdateTrackLayout();
		})));
	Menu.EndSection();

	if (!CanEdit() || !Source.IsValid() || !Source->SupportsLayerGroups())
	{
		return Menu.MakeWidget();
	}

	Menu.BeginSection("Folder", LOCTEXT("FolderSection", "List Folder"));
	Menu.AddSubMenu(
		LOCTEXT("RenameFolder", "Rename List Folder"),
		WidgetAnimTips::RenameFolderTooltip(),
		FNewMenuDelegate::CreateLambda([this, Folder](FMenuBuilder& SubMenu)
		{
			SubMenu.AddWidget(
				WidgetAnimFolderMenu::MakeNameBox(FText::FromString(Folder), WidgetAnimTips::NewFolderHint(), [this, Folder](const FString& Name) { RenameFolder(Folder, Name); }),
				FText::GetEmpty(), /*bNoIndent=*/true);
		}),
		/*bInOpenSubMenuOnClick=*/false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"));
	Menu.AddMenuEntry(
		LOCTEXT("AddGroupToFolder", "Add Layer Group to List Folder"),
		WidgetAnimTips::AddGroupToFolderTooltip(),
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuAddIcon),
		FUIAction(FExecuteAction::CreateLambda([this, Folder]()
		{
			const FScopedTransaction Transaction(LOCTEXT("AddGroupToFolderTransaction", "Add Layer Group"));
			if (!BeginEdit())
			{
				return;
			}
			TArray<FWidgetAnimLayerGroup>& Groups = *Source->GetGroups();
			Groups.AddDefaulted_GetRef().EditorFolder = Folder;
			ListCollapsedFolders.Remove(Folder);
			NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
			RebuildRows();
			SelectGroup(Groups.Num() - 1);
		})));
	Menu.AddMenuEntry(
		LOCTEXT("RemoveFolder", "Remove List Folder (Keep Groups)"),
		WidgetAnimTips::RemoveFolderTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.FolderOpen"),
		FUIAction(FExecuteAction::CreateLambda([this, FolderGroups]() { SetGroupsFolder(FolderGroups, FString()); })));
	Menu.AddMenuEntry(
		LOCTEXT("DeleteFolder", "Delete List Folder and Its Groups"),
		WidgetAnimTips::DeleteFolderTooltip(),
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
		FUIAction(FExecuteAction::CreateLambda([this, FolderGroups]() { DeleteGroups(FolderGroups); })));
	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeGroupContextMenu(int32 GroupIndex, TOptional<float> AtSeconds)
{
	// One of several selected groups: a menu about all of them.
	const TArray<int32> SelectedGroupIndices = GetSelectedGroups();
	if (SelectedGroupIndices.Num() > 1 && SelectedGroupIndices.Contains(GroupIndex))
	{
		return MakeGroupsContextMenu(SelectedGroupIndices);
	}

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
	AddInsertAnimationEntry(Menu, GroupIndex, AtSeconds);
	AddPasteEntry(Menu, GroupIndex, AtSeconds);
	Menu.EndSection();

	Menu.BeginSection("Group", LOCTEXT("GroupSectionContext", "Layer Group"));

	// Its Preview Target, by clicking the widget in the designer.
	if (CanPickTargetInDesigner())
	{
		Menu.AddMenuEntry(
			LOCTEXT("PickTargetInDesigner", "Pick Target Widget in Designer"),
			WidgetAnimTips::PickTargetInDesignerTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.EyeDropper"),
			FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { BeginTargetPick(GroupIndex); })));
	}

	Menu.AddMenuEntry(
		LOCTEXT("DuplicateGroup", "Duplicate Group"),
		WidgetAnimTips::DuplicateGroupTooltip(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Duplicate"),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { DuplicateGroup(GroupIndex); })));

	// The folder it is listed under - see FWidgetAnimLayerGroup::EditorFolder.
	AddFolderEntries(Menu, { GroupIndex });

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

	// A collapsed track - opened from its section on the Track Editor or its row in the list - is what the menu is about.
	const FName CollapsedName = GetLayerCollapsedName(LayerIndex);
	const bool bOnCollapsed = !CollapsedName.IsNone() && CollapsedName != ActiveCollapsed;

	// Pinning, like hiding, is a way of looking: offered even while the panel is locked read-only.
	bool bAnyPinned = false;
	bool bAnyUnpinned = false;
	for (const int32 Index : ActedOn)
	{
		(IsLayerPinned(Index) ? bAnyPinned : bAnyUnpinned) = true;
	}
	auto AddPinEntries = [this, &Menu, &ActedOn, bAnyPinned, bAnyUnpinned]()
	{
		const bool bSeveral = ActedOn.Num() > 1;
		if (bAnyUnpinned)
		{
			Menu.AddMenuEntry(
				bSeveral ? LOCTEXT("PinTracks", "Pin Tracks") : LOCTEXT("PinTrack", "Pin Track"),
				WidgetAnimTips::PinTrackTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Pinned"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersPinned(ActedOn, /*bPinned=*/true); })));
		}
		if (bAnyPinned)
		{
			Menu.AddMenuEntry(
				bSeveral ? LOCTEXT("UnpinTracks", "Unpin Tracks") : LOCTEXT("UnpinTrack", "Unpin Track"),
				WidgetAnimTips::UnpinTrackTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Unpinned"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersPinned(ActedOn, /*bPinned=*/false); })));
		}
	};

	// Invert Track: plays it from its end back to its start - ticked while every one acted on is inverted on its own.
	auto AddInvertEntry = [this, &Menu, &ActedOn]()
	{
		const bool bAllInverted = ActedOn.Num() > 0 && Algo::AllOf(ActedOn, [this](int32 Index) { return IsLayerInvertedOnItsOwn(Index); });
		bool bAnyEditable = false;
		for (const int32 Index : ActedOn)
		{
			bAnyEditable |= CanEditLayer(Index);
		}
		Menu.AddMenuEntry(
			ActedOn.Num() > 1 ? LOCTEXT("InvertTracks", "Invert Tracks") : LOCTEXT("InvertTrack", "Invert Track"),
			WidgetAnimTips::InvertTrackTooltip(),
			// A check box in the icon column rather than the menu's own, which sits left of it - see FNameTBDStyle::MenuCheckedIcon.
			// The menu closes on the click, so the icon is set once, here.
			FSlateIcon(FNameTBDStyle::GetStyleSetName(), bAllInverted ? FNameTBDStyle::MenuCheckedIcon : FNameTBDStyle::MenuUncheckedIcon),
			FUIAction(
				FExecuteAction::CreateLambda([this, ActedOn, bAllInverted]() { SetLayersInverted(ActedOn, !bAllInverted); }),
				FCanExecuteAction::CreateLambda([bAnyEditable]() { return bAnyEditable; })));
	};

	// Folding tracks into a collapsed track - or, on one, opening it, expanding it and renaming it.
	auto AddCollapseEntries = [this, &Menu, &ActedOn, bOnCollapsed, CollapsedName]()
	{
		if (bOnCollapsed)
		{
			Menu.AddMenuEntry(
				LOCTEXT("OpenCollapsed", "Open Collapsed Track"),
				WidgetAnimTips::OpenCollapsedTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
				FUIAction(FExecuteAction::CreateLambda([this, CollapsedName]() { EnterCollapsed(CollapsedName); })));
			Menu.AddMenuEntry(
				LOCTEXT("ExpandCollapsed", "Expand Collapsed Track"),
				WidgetAnimTips::ExpandCollapsedTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Expand"),
				FUIAction(
					FExecuteAction::CreateLambda([this, CollapsedName]() { ExpandCollapsed(CollapsedName); }),
					FCanExecuteAction::CreateLambda([this]() { return CanEdit(); })));

			// Renamed right here, as a Blueprint node's title is: typed, and taken on Enter.
			if (CanEdit())
			{
				Menu.AddWidget(
					SNew(SBox)
					.WidthOverride(180.f)
					.Padding(FMargin(0.f, 2.f, 8.f, 2.f))
					[
						SNew(SEditableTextBox)
						.Text(FText::FromName(CollapsedName))
						.SelectAllTextWhenFocused(true)
						.ToolTipText(WidgetAnimTips::RenameCollapsedTooltip())
						.OnTextCommitted_Lambda([this, CollapsedName](const FText& NewText, ETextCommit::Type CommitType)
						{
							if (CommitType == ETextCommit::OnEnter)
							{
								RenameCollapsed(CollapsedName, NewText.ToString());
								FSlateApplication::Get().DismissAllMenus();
							}
						})
					],
					LOCTEXT("RenameCollapsedLabel", "Name"));
			}
			return;
		}

		if (CanCollapseLayers(ActedOn))
		{
			Menu.AddMenuEntry(
				ActedOn.Num() > 1 ? LOCTEXT("CollapseTracks", "Collapse Tracks") : LOCTEXT("CollapseTrack", "Collapse Track"),
				WidgetAnimTips::CollapseTracksTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Collapse"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { CollapseLayers(ActedOn); })));
		}
	};

	// What the menu acts on, first: the track and its group - a collapsed track by its name - or, for several, just that.
	{
		const TArray<FWidgetAnimLayer>* HeadingLayers = GetLayers();
		const bool bOne = ActedOn.Num() == 1 && HeadingLayers && HeadingLayers->IsValidIndex(LayerIndex);
		if (bOnCollapsed)
		{
			AddMenuHeading(Menu, FText::FromName(CollapsedName), WidgetAnimTimeline::Palette::LayerCollapsed);
		}
		else
		{
			AddMenuHeading(Menu,
				bOne
					? FText::Format(LOCTEXT("TrackMenuHeading", "{0}  ({1})"), GetLayerDisplayName((*HeadingLayers)[LayerIndex]), GetLayerWhereName(LayerIndex))
					: LOCTEXT("MultipleTracksMenuHeading", "Multiple Tracks"),
				bOne ? GetLayerTypeColor((*HeadingLayers)[LayerIndex]) : GetGroupColor(GroupIndex));
		}
	}

	// Its name, first, right under the heading: renamed right here, as a collapsed track is - typed, and taken on Enter.
	// Cleared, it is named by its type again.
	if (ActedOn.Num() == 1 && !bOnCollapsed && CanEditLayer(LayerIndex))
	{
		const TArray<FWidgetAnimLayer>* NameLayers = GetLayers();
		const FText CurrentName = NameLayers && NameLayers->IsValidIndex(LayerIndex) ? GetLayerDisplayName((*NameLayers)[LayerIndex]) : FText::GetEmpty();
		Menu.AddWidget(
			SNew(SBox)
			.WidthOverride(180.f)
			.Padding(FMargin(0.f, 2.f, 8.f, 2.f))
			[
				SNew(SEditableTextBox)
				.Text(CurrentName)
				.SelectAllTextWhenFocused(true)
				.ToolTipText(WidgetAnimTips::RenameTrackTooltip())
				.OnTextCommitted_Lambda([this, LayerIndex](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnEnter)
					{
						RenameLayer(LayerIndex, NewText.ToString());
						FSlateApplication::Get().DismissAllMenus();
					}
				})
			],
			LOCTEXT("RenameTrackLabel", "Name"));
	}

	// The Graph Editor is a way of looking too - and of editing, where the tracks can be - so it is offered first, either way.
	{
		const TArray<int32> Graphable = GetGraphableLayers(ActedOn);
		if (Graphable.Num() > 0 && !IsGraphOpen())
		{
			const bool bEngineIcon = FAppStyle::Get().GetOptionalBrush("GenericCurveEditor.TabIcon", nullptr, nullptr) != nullptr;
			Menu.AddMenuEntry(
				WidgetAnimTips::OpenGraphEditor(),
				WidgetAnimTips::OpenGraphEditorTooltip(),
				bEngineIcon ? FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCurveEditor.TabIcon") : FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::GraphEditorIcon),
				FUIAction(FExecuteAction::CreateLambda([this, Graphable]() { OpenGraph(Graphable); })));
			Menu.AddSeparator();
		}
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
		AddPinEntries();
		AddFocusEntries(Menu, ActedOn);
		if (bOnCollapsed)
		{
			AddCollapseEntries();
		}
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

	// An animation track: open it to see what it plays, or unravel it into tracks of this animation's own.
	if (GetInsertedAnimation(LayerIndex) && ActedOn.Num() == 1)
	{
		Menu.BeginSection("AnimationTrack", LOCTEXT("AnimationTrackSection", "Animation Track"));
		Menu.AddMenuEntry(
			IsAnimationTrackExpanded(LayerIndex) ? LOCTEXT("CloseAnimationTrack", "Hide Its Tracks") : LOCTEXT("OpenAnimationTrack", "Show Its Tracks"),
			WidgetAnimTips::AnimationTrackFoldTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { ToggleAnimationTrackExpanded(LayerIndex); })));
		Menu.AddMenuEntry(
			LOCTEXT("UnravelAnimationTrack", "Unravel"),
			WidgetAnimTips::UnravelAnimationTrackTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Expand"),
			FUIAction(
				FExecuteAction::CreateLambda([this, LayerIndex]() { UnravelAnimationTrack(LayerIndex); }),
				FCanExecuteAction::CreateLambda([this, LayerIndex]() { return CanUnravelLayer(LayerIndex); })));
		Menu.EndSection();
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
				FText::Format(LOCTEXT("AddKeyHereAtTime", "Add Key Here ({0})"), FText::FromString(FormatTime(AnimationSeconds))),
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
					FText::Format(LOCTEXT("AddKeyAtPlayheadTime", "Add Key at Playhead ({0})"), FText::FromString(FormatTime(Playhead))),
					WidgetAnimTips::AddKeyAtPlayheadTooltip(),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
					FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, Seconds]() { AddKey(LayerIndex, Seconds); })));
			}
		}

		Menu.EndSection();
	}
	else if (ActedOn.Num() == 1 && KeyIndex == INDEX_NONE && CanEditLayer(LayerIndex) && !bOnCollapsed)
	{
		// A track whose type has no keys - a Pulse, a Shake, an animation track - says so where Add Key would be, grayed out,
		// rather than leaving a gap where the entry is on every other track.
		const TArray<FWidgetAnimLayer>* KeyLayers = GetLayers();
		const FWidgetAnimLayer* KeyLayer = KeyLayers && KeyLayers->IsValidIndex(LayerIndex) ? &(*KeyLayers)[LayerIndex] : nullptr;
		const bool bTypeHasKeys = KeyLayer && !KeyLayer->IsAnimationTrack()
			&& (WidgetAnimTypeIsKeyframed(KeyLayer->AnimType) || WidgetAnimTypeSupportsValueKeys(KeyLayer->AnimType));
		if (KeyLayer && !bTypeHasKeys)
		{
			Menu.BeginSection("Keys", LOCTEXT("KeysSection", "Keys"));
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("TrackCannotHaveKeys", "Track {0} cannot have keys"), KeyLayer->IsAnimationTrack() ? LOCTEXT("AnimationTrackTypeName", "Animation") : GetLayerTypeName(*KeyLayer)),
				WidgetAnimTips::TrackCannotHaveKeysTooltip(),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
				FUIAction(
					FExecuteAction(),
					FCanExecuteAction::CreateLambda([]() { return false; })));
			Menu.EndSection();
		}
	}

	// A collapsed track: open it, expand it, rename it - before what its tracks can do together.
	if (bOnCollapsed)
	{
		Menu.BeginSection("Collapsed", LOCTEXT("CollapsedSection", "Collapsed Track"));
		AddCollapseEntries();
		Menu.EndSection();
	}

	// Several tracks selected, this one among them: what they can all do together.
	if (ActedOn.Num() > 1)
	{
		Menu.BeginSection("Selected", FText::Format(LOCTEXT("SelectedTracksSection", "{0} Selected Tracks"), FText::AsNumber(ActedOn.Num())));

		AddHideEntries();
		AddPinEntries();
		AddFocusEntries(Menu, ActedOn);
		AddInvertEntry();
		if (!bOnCollapsed)
		{
			AddCollapseEntries();
		}

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

		Menu.AddSubMenu(
			LOCTEXT("TrackPresetsSubMenuMulti", "Track Presets"),
			WidgetAnimTips::SaveTrackPresetsMenuTooltip(),
			FNewMenuDelegate::CreateLambda([this, ActedOn](FMenuBuilder& SubMenu) { AddPresetEntries(SubMenu, ActedOn, /*bApply=*/false); }),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));

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
	AddPinEntries();
	AddFocusEntries(Menu, ActedOn);
	AddInvertEntry();
	if (!bOnCollapsed)
	{
		AddCollapseEntries();
	}

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

	// Presets: this track saved as one, under its name - adding one is Add Animation Track's From Preset.
	if (!bOnCollapsed && !GetInsertedAnimation(LayerIndex))
	{
		Menu.AddSubMenu(
			LOCTEXT("TrackPresetsSubMenu", "Track Presets"),
			WidgetAnimTips::SaveTrackPresetsMenuTooltip(),
			FNewMenuDelegate::CreateLambda([this, LayerIndex](FMenuBuilder& SubMenu) { AddPresetEntries(SubMenu, { LayerIndex }, /*bApply=*/false); }),
			/*bInOpenSubMenuOnClick=*/false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"));
	}

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
	AddInsertAnimationEntry(Menu, GroupIndex, AddAtSeconds);
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
	// An animation track is called what it plays.
	if (Layer.IsAnimationTrack())
	{
		return FText::FromString(Layer.InsertedAnimation->GetName());
	}

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

FText SWidgetAnimTimeline::GetLayerDisplayName(const FWidgetAnimLayer& Layer)
{
	const FString Name = Layer.TrackName.TrimStartAndEnd();
	return Name.IsEmpty() ? GetLayerTypeName(Layer) : FText::FromString(Name);
}

FString SWidgetAnimTimeline::MakeUniqueTrackName(const TArray<FWidgetAnimLayer>& Layers, const FString& Base, int32 IgnoreIndex)
{
	const FString Trimmed = Base.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return FString();
	}

	auto IsTaken = [&Layers, IgnoreIndex](const FString& Candidate)
	{
		for (int32 Index = 0; Index < Layers.Num(); ++Index)
		{
			if (Index != IgnoreIndex && Layers[Index].TrackName.TrimStartAndEnd().Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	};

	// "Scale 2" copied again is "Scale 3", not "Scale 2 2".
	FString Stem = Trimmed;
	int32 Suffix = 2;
	int32 Space = INDEX_NONE;
	if (Trimmed.FindLastChar(TEXT(' '), Space) && Space > 0 && Trimmed.Mid(Space + 1).IsNumeric() && IsTaken(Trimmed))
	{
		Stem = Trimmed.Left(Space);
		Suffix = FMath::Max(FCString::Atoi(*Trimmed.Mid(Space + 1)) + 1, 2);
	}

	FString Candidate = Trimmed;
	while (IsTaken(Candidate))
	{
		Candidate = FString::Printf(TEXT("%s %d"), *Stem, Suffix++);
	}
	return Candidate;
}

void SWidgetAnimTimeline::EnsureUniqueTrackName(TArray<FWidgetAnimLayer>& Layers, int32 Index)
{
	if (Layers.IsValidIndex(Index) && !Layers[Index].TrackName.IsEmpty())
	{
		Layers[Index].TrackName = MakeUniqueTrackName(Layers, Layers[Index].TrackName, Index);
	}
}

void SWidgetAnimTimeline::RenameLayer(int32 LayerIndex, const FString& NewName)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
	{
		return;
	}

	// Its type's name typed back in takes the name off, so the track follows its type again.
	FString Unique = MakeUniqueTrackName(*Current, NewName, LayerIndex);
	if (Unique.Equals(GetLayerTypeName((*Current)[LayerIndex]).ToString(), ESearchCase::CaseSensitive))
	{
		Unique.Reset();
	}
	if (Unique.Equals((*Current)[LayerIndex].TrackName, ESearchCase::CaseSensitive))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameTrackTransaction", "Rename Track"));
	if (!BeginEdit())
	{
		return;
	}
	(*Source->GetLayers())[LayerIndex].TrackName = Unique;

	// The Selection panel reads the name live, so it isn't rebuilt - it may be where the name was typed.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RebuildRows();
}

// =====================================================================================================
// Animation Parameters
// =====================================================================================================

namespace WidgetAnimParameterEditing
{
	/** A new parameter's name, by its type. */
	FString GetDefaultName(EWidgetAnimParameterType Type)
	{
		switch (Type)
		{
		case EWidgetAnimParameterType::Integer:  return TEXT("Count");
		case EWidgetAnimParameterType::Bool:     return TEXT("Enabled");
		case EWidgetAnimParameterType::Color:    return TEXT("Color");
		case EWidgetAnimParameterType::Vector2D: return TEXT("Offset");
		case EWidgetAnimParameterType::Choice:   return TEXT("Side");
		default:                                 return TEXT("Amount");
		}
	}

	/** A field of a track a parameter is never bound to: what makes the track the track it is, and the binding table itself. */
	bool IsUnbindableField(const FString& FieldPath)
	{
		static const TCHAR* Never[] =
		{
			TEXT("AnimType"), TEXT("Timing"), TEXT("Start"), TEXT("ValueName"), TEXT("ValueDrives"), TEXT("ParameterBindings"), TEXT("PlaysWhen"),
			TEXT("LayerGroup"), TEXT("TargetWidget"), TEXT("TrackName"), TEXT("InsertedAnimation"), TEXT("InsertedGroups"), TEXT("CollapsedTrack"),
		};
		for (const TCHAR* Name : Never)
		{
			if (FieldPath == Name || FieldPath.StartsWith(FString(Name) + TEXT(".")) || FieldPath.StartsWith(FString(Name) + TEXT("[")))
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * A Selection panel row's field as a path into its track - "SlideDistance", "ValueKeys.Keys[1].Value" - from its handle,
	 * whatever the row is shown on: the shadow (Layers[0].<field>) or a proxy (Layer.<field>). False for a row that isn't a
	 * track's field.
	 */
	bool MakeLayerFieldPath(const TSharedRef<IPropertyHandle>& Handle, FString& OutPath)
	{
		TArray<FString> Parts;
		TSharedPtr<IPropertyHandle> Current = Handle;
		for (int32 Guard = 0; Current.IsValid() && Current->GetProperty() && Guard < 64; ++Guard)
		{
			const TSharedPtr<IPropertyHandle> Parent = Current->GetParentHandle();
			const int32 Index = Current->GetIndexInArray();
			if (Index != INDEX_NONE && Parent.IsValid() && Parent->GetProperty() && CastField<FArrayProperty>(Parent->GetProperty()))
			{
				// An element: its index - the array adds its name on the next step.
				Parts.Insert(FString::Printf(TEXT("[%d]"), Index), 0);
			}
			else
			{
				Parts.Insert(Current->GetProperty()->GetName(), 0);
			}
			Current = Parent;
		}

		FString Path;
		for (const FString& Part : Parts)
		{
			if (!Path.IsEmpty() && !Part.StartsWith(TEXT("[")))
			{
				Path += TEXT(".");
			}
			Path += Part;
		}

		// Off the object's own property: the shadow's one track, or a proxy's.
		const FString ShadowPrefix = FString(TEXT("Layers[0]."));
		const FString ProxyPrefix = FString(TEXT("Layer."));
		if (Path.StartsWith(ShadowPrefix))
		{
			Path.RightChopInline(ShadowPrefix.Len());
		}
		else if (Path.StartsWith(ProxyPrefix))
		{
			Path.RightChopInline(ProxyPrefix.Len());
		}
		else
		{
			return false;
		}
		OutPath = Path;
		return !Path.IsEmpty();
	}

	/** Whether any type of parameter can be bound to Field. */
	bool IsBindable(const FProperty* Field)
	{
		for (const EWidgetAnimParameterType Type : { EWidgetAnimParameterType::Float, EWidgetAnimParameterType::Integer, EWidgetAnimParameterType::Bool,
			EWidgetAnimParameterType::Color, EWidgetAnimParameterType::Vector2D, EWidgetAnimParameterType::Choice })
		{
			if (WidgetAnimParameters::CanBind(Field, Type))
			{
				return true;
			}
		}
		return false;
	}

	FText GetTypeText(EWidgetAnimParameterType Type)
	{
		return StaticEnum<EWidgetAnimParameterType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
	}

	/** A field's own label, as a parameter name: "Slide Distance". */
	FString MakeNameFromField(const FProperty* Field)
	{
		FString Name = Field ? Field->GetDisplayNameText().ToString() : FString();
		return Name.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Parameter")) : Name.TrimStartAndEnd();
	}
}

void SWidgetAnimTimeline::RefreshDetailsNextTick()
{
	const TWeakPtr<SWidgetAnimTimeline> WeakPanel = SharedThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakPanel](float)
	{
		if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakPanel.Pin())
		{
			Pinned->RefreshDetails();
		}
		return false;
	}));
}

bool SWidgetAnimTimeline::GetPreviewParameter(FName Name, FWidgetAnimParameterValue& OutValue) const
{
	const TArray<FWidgetAnimParameterValue>* Tried = Source.IsValid() ? Source->GetPreviewParameters() : nullptr;
	const FWidgetAnimParameterValue* Found = Tried ? Tried->FindByPredicate([Name](const FWidgetAnimParameterValue& Value) { return Value.Name == Name; }) : nullptr;
	if (Found)
	{
		OutValue = *Found;
	}
	return Found != nullptr;
}

void SWidgetAnimTimeline::SetPreviewParameter(const FWidgetAnimParameterValue& Value, bool bTry)
{
	const TArray<FWidgetAnimParameterValue>* Tried = Source.IsValid() ? Source->GetPreviewParameters() : nullptr;
	if (!Tried || Value.Name.IsNone())
	{
		return;
	}
	TArray<FWidgetAnimParameterValue> Values = *Tried;
	Values.RemoveAll([&Value](const FWidgetAnimParameterValue& Candidate) { return Candidate.Name == Value.Name; });
	if (bTry)
	{
		Values.Add(Value);
	}
	Source->SetPreviewParameters(Values);
}

TArray<FWidgetAnimParameter>* SWidgetAnimTimeline::GetParameters() const
{
	UWidgetAnimPresetAsset* Asset = Source.IsValid() ? Cast<UWidgetAnimPresetAsset>(Source->GetEditedObject()) : nullptr;
	return Asset && Source->GetLayers() == &Asset->Layers ? &Asset->Parameters : nullptr;
}

int32 SWidgetAnimTimeline::AddParameter(EWidgetAnimParameterType Type)
{
	TArray<FWidgetAnimParameter>* Declared = GetParameters();
	if (!Declared)
	{
		return INDEX_NONE;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddParameterTransaction", "Add Animation Parameter"));
	if (!BeginEdit())
	{
		return INDEX_NONE;
	}

	FWidgetAnimParameter Added;
	Added.Type = Type;
	Added.Name = WidgetAnimParameters::MakeUniqueName(*Declared, WidgetAnimParameterEditing::GetDefaultName(Type));
	if (Type == EWidgetAnimParameterType::Choice)
	{
		Added.Options = { FName(TEXT("Left")), FName(TEXT("Right")) };
		Added.DefaultOption = Added.Options[0];
	}
	const int32 AddedIndex = Declared->Add(Added);

	NotifyGroupsChanged(EPropertyChangeType::ArrayAdd);
	RefreshDetailsNextTick();
	return AddedIndex;
}

void SWidgetAnimTimeline::RenameParameter(int32 ParameterIndex, const FString& NewName)
{
	TArray<FWidgetAnimParameter>* Declared = GetParameters();
	const FString Trimmed = NewName.TrimStartAndEnd();
	if (!Declared || !Declared->IsValidIndex(ParameterIndex) || Trimmed.IsEmpty())
	{
		return;
	}
	const FName OldName = (*Declared)[ParameterIndex].Name;
	const FName Renamed = WidgetAnimParameters::MakeUniqueName(*Declared, Trimmed, ParameterIndex);
	if (Renamed == OldName)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RenameParameterTransaction", "Rename Animation Parameter"));
	if (!BeginEdit())
	{
		return;
	}
	(*Declared)[ParameterIndex].Name = Renamed;

	// What names it follows it.
	for (FWidgetAnimLayer& Layer : *Source->GetLayers())
	{
		for (FWidgetAnimParameterBinding& Binding : Layer.ParameterBindings)
		{
			if (Binding.Parameter == OldName)
			{
				Binding.Parameter = Renamed;
			}
		}
		if (Layer.PlaysWhen.Parameter == OldName)
		{
			Layer.PlaysWhen.Parameter = Renamed;
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetailsNextTick();
}

void SWidgetAnimTimeline::SetParameterType(int32 ParameterIndex, EWidgetAnimParameterType Type)
{
	TArray<FWidgetAnimParameter>* Declared = GetParameters();
	if (!Declared || !Declared->IsValidIndex(ParameterIndex) || (*Declared)[ParameterIndex].Type == Type)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetParameterTypeTransaction", "Change Animation Parameter Type"));
	if (!BeginEdit())
	{
		return;
	}
	FWidgetAnimParameter& Parameter = (*Declared)[ParameterIndex];
	Parameter.Type = Type;
	if (Type == EWidgetAnimParameterType::Choice && Parameter.Options.Num() == 0)
	{
		Parameter.Options = { FName(TEXT("Left")), FName(TEXT("Right")) };
		Parameter.DefaultOption = Parameter.Options[0];
	}

	// A field that can't take the new type plays its own value again; Plays When tests only a Choice or a Bool.
	for (FWidgetAnimLayer& Layer : *Source->GetLayers())
	{
		Layer.ParameterBindings.RemoveAll([&Layer, &Parameter](const FWidgetAnimParameterBinding& Binding)
		{
			return Binding.Parameter == Parameter.Name
				&& !WidgetAnimParameters::CanBind(WidgetAnimParameters::FindField(FWidgetAnimLayer::StaticStruct(), Binding.FieldPath, &Layer), Parameter.Type);
		});
		if (Layer.PlaysWhen.Parameter == Parameter.Name && Type != EWidgetAnimParameterType::Choice && Type != EWidgetAnimParameterType::Bool)
		{
			Layer.PlaysWhen = FWidgetAnimParameterCondition();
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetailsNextTick();
}

void SWidgetAnimTimeline::RemoveParameter(int32 ParameterIndex)
{
	TArray<FWidgetAnimParameter>* Declared = GetParameters();
	if (!Declared || !Declared->IsValidIndex(ParameterIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveParameterTransaction", "Remove Animation Parameter"));
	if (!BeginEdit())
	{
		return;
	}
	const FName Removed = (*Declared)[ParameterIndex].Name;
	Declared->RemoveAt(ParameterIndex);

	// Its bindings go with it: those fields play their own values again.
	for (FWidgetAnimLayer& Layer : *Source->GetLayers())
	{
		Layer.ParameterBindings.RemoveAll([Removed](const FWidgetAnimParameterBinding& Binding) { return Binding.Parameter == Removed; });
		if (Layer.PlaysWhen.Parameter == Removed)
		{
			Layer.PlaysWhen = FWidgetAnimParameterCondition();
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ArrayRemove);
	RefreshDetailsNextTick();
}

void SWidgetAnimTimeline::BindLayerField(const TArray<int32>& LayerIndices, const FString& FieldPath, FName Parameter)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || FieldPath.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(Parameter.IsNone() ? LOCTEXT("UnbindFieldTransaction", "Unbind Track Field") : LOCTEXT("BindFieldTransaction", "Bind Track Field To Parameter"));
	if (!BeginEdit())
	{
		return;
	}
	for (const int32 LayerIndex : LayerIndices)
	{
		if (!Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
		{
			continue;
		}
		FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];

		// One binding a field: a new one replaces it, keeping its Scale and Offset.
		FWidgetAnimParameterBinding Kept;
		if (const FWidgetAnimParameterBinding* Existing = Layer.FindParameterBinding(FieldPath))
		{
			Kept = *Existing;
		}
		Layer.ParameterBindings.RemoveAll([&FieldPath](const FWidgetAnimParameterBinding& Binding) { return Binding.FieldPath == FieldPath; });
		if (!Parameter.IsNone())
		{
			Kept.FieldPath = FieldPath;
			Kept.Parameter = Parameter;
			Layer.ParameterBindings.Add(Kept);
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetailsNextTick();
}

void SWidgetAnimTimeline::SetLayerBindingScale(const TArray<int32>& LayerIndices, const FString& FieldPath, float BindingScale)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetBindingScaleTransaction", "Mirror Bound Field"));
	if (!BeginEdit())
	{
		return;
	}
	for (const int32 LayerIndex : LayerIndices)
	{
		if (!Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
		{
			continue;
		}
		for (FWidgetAnimParameterBinding& Binding : (*Source->GetLayers())[LayerIndex].ParameterBindings)
		{
			if (Binding.FieldPath == FieldPath)
			{
				Binding.Scale = BindingScale;
			}
		}
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetailsNextTick();
}

void SWidgetAnimTimeline::SetLayerPlaysWhen(int32 LayerIndex, const FWidgetAnimParameterCondition& Condition)
{
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Current || !Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetPlaysWhenTransaction", "Set Track Plays When"));
	if (!BeginEdit())
	{
		return;
	}
	(*Source->GetLayers())[LayerIndex].PlaysWhen = Condition;

	// Not RefreshDetails: the row's own menu set it, and reads it live.
	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
}

TSharedPtr<SWidget> SWidgetAnimTimeline::MakeParameterBindChip(const TSharedRef<IPropertyHandle>& Handle, TSharedPtr<SWidget>* InOutValueWidget)
{
	using namespace WidgetAnimParameterEditing;

	if (!Handle->IsValidHandle())
	{
		return nullptr;
	}

	// Only rows of a Widget Animations panel's Selection panel: its shadow (one track) or its proxies (several).
	TArray<UObject*> Outers;
	Handle->GetOuterObjects(Outers);
	TSharedPtr<SWidgetAnimTimeline> Owner;
	TArray<int32> LayerIndices;
	for (UObject* Outer : Outers)
	{
		if (const UWidgetAnimPresetSelectionShadow* Shadow = Cast<UWidgetAnimPresetSelectionShadow>(Outer))
		{
			Owner = Shadow->Panel.Pin();
			LayerIndices.Add(Shadow->LayerIndex);
		}
		else if (const UWidgetAnimLayerEditProxy* Proxy = Cast<UWidgetAnimLayerEditProxy>(Outer))
		{
			Owner = Proxy->Panel.Pin();
			if (Proxy->RepresentedLayers.Num() > 0)
			{
				LayerIndices.Append(Proxy->RepresentedLayers);
			}
			else
			{
				LayerIndices.Add(Proxy->LayerIndex);
			}
		}
		else
		{
			return nullptr;
		}
	}
	const TArray<FWidgetAnimParameter>* Declared = Owner ? Owner->GetParameters() : nullptr;
	const TArray<FWidgetAnimLayer>* Layers = Owner ? Owner->GetLayers() : nullptr;
	if (!Declared || !Layers || LayerIndices.Num() == 0 || LayerIndices.ContainsByPredicate([Layers](int32 Index) { return !Layers->IsValidIndex(Index); }))
	{
		return nullptr;
	}

	// A field of the track that can take a parameter - checked against the real track, so a row whose handle doesn't lead
	// back to it (a registered type's fields, laid out apart) gets no chip rather than a wrong binding.
	FString FieldPath;
	const FProperty* Field = Handle->GetProperty();
	if (!MakeLayerFieldPath(Handle, FieldPath) || IsUnbindableField(FieldPath) || !IsBindable(Field))
	{
		return nullptr;
	}
	if (WidgetAnimParameters::FindField(FWidgetAnimLayer::StaticStruct(), FieldPath, &(*Layers)[LayerIndices[0]]) != Field)
	{
		return nullptr;
	}

	// Read live, so binding or letting go shows at once: an empty pill to bind, a filled one while bound.
	const TWeakPtr<SWidgetAnimTimeline> WeakOwner = Owner;
	EWidgetAnimParameterType NaturalType = EWidgetAnimParameterType::Float;
	WidgetAnimParameters::GetNaturalType(Field, NaturalType);
	const WidgetAnimTypePill::EKind NaturalKind = WidgetAnimTypePill::FromParameterType(NaturalType);
	const int32 FirstLayer = LayerIndices[0];
	auto FindBinding = [WeakOwner, FirstLayer, FieldPath]() -> TOptional<FWidgetAnimParameterBinding>
	{
		const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
		const TArray<FWidgetAnimLayer>* Live = Pinned ? Pinned->GetLayers() : nullptr;
		const FWidgetAnimParameterBinding* Found = Live && Live->IsValidIndex(FirstLayer) ? (*Live)[FirstLayer].FindParameterBinding(FieldPath) : nullptr;
		return Found ? TOptional<FWidgetAnimParameterBinding>(*Found) : TOptional<FWidgetAnimParameterBinding>();
	};

	// Bound: the field's own value stands aside - the row reads "Distance = 40" (x -1 when mirrored), greyed.
	if (InOutValueWidget && InOutValueWidget->IsValid())
	{
		const TSharedRef<SWidget> OwnValue = InOutValueWidget->ToSharedRef();
		*InOutValueWidget = SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Visibility_Lambda([FindBinding]() { return FindBinding().IsSet() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					OwnValue
				]
			]
			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Visibility_Lambda([FindBinding]() { return FindBinding().IsSet() ? EVisibility::Visible : EVisibility::Collapsed; })
				.Font(IDetailLayoutBuilder::GetDetailFontItalic())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Lambda([WeakOwner, FindBinding]()
				{
					const TOptional<FWidgetAnimParameterBinding> Binding = FindBinding();
					if (!Binding.IsSet())
					{
						return FText::GetEmpty();
					}
					const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
					const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
					const FWidgetAnimParameter* BoundTo = Live ? Live->FindByPredicate([&Binding](const FWidgetAnimParameter& Parameter) { return Parameter.Name == Binding->Parameter; }) : nullptr;
					if (!BoundTo)
					{
						return FText::Format(LOCTEXT("BoundMissingValue", "{0} (missing)"), FText::FromName(Binding->Parameter));
					}
					FWidgetAnimParameterValue Shown;
					if (!Pinned->GetPreviewParameter(BoundTo->Name, Shown))
					{
						Shown = FWidgetAnimParameterValue::FromDefault(*BoundTo);
					}
					const FText ValueText = FText::FromString(Shown.ToDisplayString());
					const bool bScaled = !FMath::IsNearlyEqual(Binding->Scale, 1.f) || !FMath::IsNearlyZero(Binding->Offset);
					return bScaled
						? FText::Format(LOCTEXT("BoundScaledValue", "{0} = {1}  (x {2})"), FText::FromName(BoundTo->Name), ValueText, FText::AsNumber(Binding->Scale))
						: FText::Format(LOCTEXT("BoundValue", "{0} = {1}"), FText::FromName(BoundTo->Name), ValueText);
				})
			];
	}

	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(2.f, 0.f))
		.VAlign(VAlign_Center)
		.ToolTipText_Lambda([WeakOwner, FindBinding]()
		{
			const TOptional<FWidgetAnimParameterBinding> Binding = FindBinding();
			if (!Binding.IsSet())
			{
				return WidgetAnimTips::BindFieldTooltip();
			}
			const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
			const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
			const FWidgetAnimParameter* BoundTo = Live ? Live->FindByPredicate([&Binding](const FWidgetAnimParameter& Parameter) { return Parameter.Name == Binding->Parameter; }) : nullptr;
			return BoundTo
				? FText::Format(WidgetAnimTips::BoundFieldTooltip(), FText::FromName(BoundTo->Name), GetTypeText(BoundTo->Type), FText::FromString(FWidgetAnimParameterValue::FromDefault(*BoundTo).ToDisplayString()))
				: FText::Format(LOCTEXT("BoundToMissing", "Bound to {0}, which the animation no longer has: this field plays its own value. Click to bind it to another or let go."), FText::FromName(Binding->Parameter));
		})
		.OnClicked_Lambda([WeakOwner, LayerIndices, FieldPath, Field]()
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin())
			{
				Pinned->OpenBindMenu(LayerIndices, FieldPath, Field);
			}
			return FReply::Handled();
		})
		[
			// Empty: unbound, in the field's own type's color. Filled: bound, in the parameter's type's color. Split in two:
			// bound mirrored (x -1). Red: bound to a parameter the animation no longer has.
			SNew(SImage)
			.DesiredSizeOverride(FVector2D(16.0, 16.0))
			.Image_Lambda([FindBinding]()
			{
				const TOptional<FWidgetAnimParameterBinding> Binding = FindBinding();
				const FName Icon = !Binding.IsSet() ? FNameTBDStyle::ParameterBindIcon
					: (Binding->Scale < 0.f ? FNameTBDStyle::ParameterMirroredIcon : FNameTBDStyle::ParameterBoundIcon);
				const FSlateBrush* Brush = FNameTBDStyle::FindIcon(Icon);
				return Brush ? Brush : FAppStyle::GetBrush(TEXT("Icons.Link"));
			})
			.ColorAndOpacity_Lambda([WeakOwner, FindBinding, NaturalKind]()
			{
				const TOptional<FWidgetAnimParameterBinding> Binding = FindBinding();
				if (!Binding.IsSet())
				{
					return FSlateColor(WidgetAnimTypePill::GetColor(NaturalKind));
				}
				const TSharedPtr<SWidgetAnimTimeline> Pinned = WeakOwner.Pin();
				const TArray<FWidgetAnimParameter>* Live = Pinned ? Pinned->GetParameters() : nullptr;
				const FWidgetAnimParameter* BoundTo = Live ? Live->FindByPredicate([&Binding](const FWidgetAnimParameter& Parameter) { return Parameter.Name == Binding->Parameter; }) : nullptr;
				return BoundTo ? FSlateColor(WidgetAnimTypePill::GetColor(WidgetAnimTypePill::FromParameterType(BoundTo->Type))) : FSlateColor(FLinearColor(0.9f, 0.15f, 0.12f));
			})
		];
}

void SWidgetAnimTimeline::OpenBindMenu(const TArray<int32>& LayerIndices, const FString& FieldPath, const FProperty* Field)
{
	using namespace WidgetAnimParameterEditing;

	const TArray<FWidgetAnimParameter>* Declared = GetParameters();
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	if (!Declared || !Current || LayerIndices.Num() == 0 || !Current->IsValidIndex(LayerIndices[0]))
	{
		return;
	}
	const FWidgetAnimParameterBinding* Binding = (*Current)[LayerIndices[0]].FindParameterBinding(FieldPath);
	const FName BoundName = Binding ? Binding->Parameter : NAME_None;
	const float BoundScale = Binding ? Binding->Scale : 1.f;
	const bool bEditable = !IsReadOnly() && Algo::AllOf(LayerIndices, [this](int32 LayerIndex) { return CanEditLayer(LayerIndex); });

	// The menu's actions hold the panel weakly: it may be gone by the time one is picked.
	const TWeakPtr<SWidgetAnimTimeline> WeakPanel = StaticCastSharedRef<SWidgetAnimTimeline>(AsShared());
	auto MakeBind = [WeakPanel, LayerIndices, FieldPath](FName BindTo)
	{
		return FExecuteAction::CreateLambda([WeakPanel, LayerIndices, FieldPath, BindTo]()
		{
			if (const TSharedPtr<SWidgetAnimTimeline> Panel = WeakPanel.Pin())
			{
				Panel->BindLayerField(LayerIndices, FieldPath, BindTo);
			}
		});
	};

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	Menu.BeginSection(TEXT("Bind"), LOCTEXT("BindSection", "Bind To Parameter"));
	bool bAnyOffered = false;
	for (const FWidgetAnimParameter& Parameter : *Declared)
	{
		if (!WidgetAnimParameters::CanBind(Field, Parameter.Type))
		{
			continue;
		}
		bAnyOffered = true;
		const FName ParameterName = Parameter.Name;
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("BindToEntry", "{0}  ({1})"), FText::FromName(ParameterName), GetTypeText(Parameter.Type)),
			Parameter.Description.IsEmpty() ? FText::Format(LOCTEXT("BindToTip", "Plays {0}'s value here."), FText::FromName(ParameterName)) : FText::FromString(Parameter.Description),
			FSlateIcon(),
			FUIAction(
				MakeBind(ParameterName),
				FCanExecuteAction::CreateLambda([bEditable]() { return bEditable; }),
				FIsActionChecked::CreateLambda([BoundName, ParameterName]() { return BoundName == ParameterName; })),
			NAME_None, EUserInterfaceActionType::RadioButton);
	}
	if (!bAnyOffered)
	{
		Menu.AddMenuEntry(LOCTEXT("NoBindableParameters", "No parameter of a type this field takes"), WidgetAnimTips::BindFieldTooltip(), FSlateIcon(),
			FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
	}

	// A parameter made from the field: its type, its value as the default, its label as the name - bound at once.
	EWidgetAnimParameterType NaturalType = EWidgetAnimParameterType::Float;
	if (WidgetAnimParameters::GetNaturalType(Field, NaturalType))
	{
		Menu.AddMenuEntry(
			LOCTEXT("NewParameterFromField", "New Parameter From This Field"),
			LOCTEXT("NewParameterFromFieldTip", "Adds a parameter of this field's type, its value as the default and its name as the name, and binds the field to it."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
			FUIAction(
				FExecuteAction::CreateSP(this, &SWidgetAnimTimeline::AddParameterFromField, LayerIndices, FieldPath, Field),
				FCanExecuteAction::CreateLambda([bEditable]() { return bEditable; })));
	}
	Menu.EndSection();

	if (Binding)
	{
		Menu.BeginSection(TEXT("Bound"), LOCTEXT("BoundSection", "Bound"));
		const bool bNumber = CastField<FNumericProperty>(Field) != nullptr || (CastField<FStructProperty>(Field) && !WidgetAnimParameters::CanBind(Field, EWidgetAnimParameterType::Color));
		if (bNumber)
		{
			Menu.AddMenuEntry(
				LOCTEXT("MirrorBinding", "Mirrored (x -1)"),
				LOCTEXT("MirrorBindingTip", "Plays the parameter's value times -1 - the same Distance sliding the other way, the same Angle turning back."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([WeakPanel, LayerIndices, FieldPath, NewScale = BoundScale < 0.f ? 1.f : -1.f]()
					{
						if (const TSharedPtr<SWidgetAnimTimeline> Panel = WeakPanel.Pin())
						{
							Panel->SetLayerBindingScale(LayerIndices, FieldPath, NewScale);
						}
					}),
					FCanExecuteAction::CreateLambda([bEditable]() { return bEditable; }),
					FIsActionChecked::CreateLambda([BoundScale]() { return BoundScale < 0.f; })),
				NAME_None, EUserInterfaceActionType::ToggleButton);
		}
		Menu.AddMenuEntry(
			LOCTEXT("UnbindField", "Unbind"),
			LOCTEXT("UnbindFieldTip", "Lets go of the parameter: the field plays its own value again."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.X")),
			FUIAction(
				MakeBind(NAME_None),
				FCanExecuteAction::CreateLambda([bEditable]() { return bEditable; })));
		Menu.EndSection();
	}

	FSlateApplication::Get().PushMenu(AsShared(), FWidgetPath(), Menu.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

void SWidgetAnimTimeline::AddParameterFromField(TArray<int32> LayerIndices, FString FieldPath, const FProperty* Field)
{
	TArray<FWidgetAnimParameter>* Declared = GetParameters();
	const TArray<FWidgetAnimLayer>* Current = GetLayers();
	EWidgetAnimParameterType Type = EWidgetAnimParameterType::Float;
	if (!Declared || !Current || LayerIndices.Num() == 0 || !Current->IsValidIndex(LayerIndices[0]) || !WidgetAnimParameters::GetNaturalType(Field, Type))
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ParameterFromFieldTransaction", "New Parameter From Field"));
	if (!BeginEdit())
	{
		return;
	}

	FWidgetAnimParameter Added;
	Added.Type = Type;
	Added.Name = WidgetAnimParameters::MakeUniqueName(*Declared, WidgetAnimParameterEditing::MakeNameFromField(Field));

	// Its default: the field as it is now. A dropdown's options are the field's own.
	FWidgetAnimParameterValue Now;
	Now.Type = Type;
	WidgetAnimParameters::ReadField(FWidgetAnimLayer::StaticStruct(), &(*Current)[LayerIndices[0]], FieldPath, Now);
	Added.DefaultFloat = Now.Float;
	Added.DefaultInteger = Now.Integer;
	Added.bDefaultBool = Now.bBool;
	Added.DefaultColor = Now.Color;
	Added.DefaultVector2D = Now.Vector2D;
	if (Type == EWidgetAnimParameterType::Choice)
	{
		const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Field);
		const FByteProperty* ByteProperty = CastField<FByteProperty>(Field);
		const UEnum* Enum = EnumProperty ? EnumProperty->GetEnum() : (ByteProperty ? ByteProperty->Enum.Get() : nullptr);
		if (Enum)
		{
			for (int32 Index = 0; Index < Enum->NumEnums() - (Enum->ContainsExistingMax() ? 1 : 0); ++Index)
			{
				if (!Enum->HasMetaData(TEXT("Hidden"), Index))
				{
					Added.Options.Add(FName(*Enum->GetNameStringByIndex(Index)));
				}
			}
		}
		else if (!Now.Option.IsNone())
		{
			Added.Options.Add(Now.Option);
		}
		Added.DefaultOption = Now.Option;
	}
	Declared->Add(Added);

	for (const int32 LayerIndex : LayerIndices)
	{
		if (!Current->IsValidIndex(LayerIndex) || !CanEditLayer(LayerIndex))
		{
			continue;
		}
		FWidgetAnimLayer& Layer = (*Source->GetLayers())[LayerIndex];
		Layer.ParameterBindings.RemoveAll([&FieldPath](const FWidgetAnimParameterBinding& Binding) { return Binding.FieldPath == FieldPath; });
		FWidgetAnimParameterBinding& Binding = Layer.ParameterBindings.AddDefaulted_GetRef();
		Binding.FieldPath = FieldPath;
		Binding.Parameter = Added.Name;
	}

	NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	RefreshDetailsNextTick();
}

FLinearColor SWidgetAnimTimeline::GetLayerTypeColor(const FWidgetAnimLayer& Layer)
{
	// An animation track in the animation orange: a whole animation, not one more thing moving.
	if (Layer.IsAnimationTrack())
	{
		return NameTBD::Colors::WidgetAnimEditor::LayerAnimation;
	}

	// A Blueprint type is drawn in its own color when it sets one, else in the one color every Blueprint type shares -
	// so a designer's types read apart from the C++ ones, and a type that matters can stand out on its own.
	if (const FWidgetAnimBlueprintTypeParams* BlueprintParams = Layer.AnimType == EWidgetAnimType::Custom ? Layer.TypeParams.GetPtr<FWidgetAnimBlueprintTypeParams>() : nullptr)
	{
		const UWidgetAnimBlueprintType* TypeObject = BlueprintParams->GetTypeObject();
		return TypeObject && TypeObject->bOverrideTrackColor ? TypeObject->TrackColor : UWidgetAnimEditorSettings::Get().BlueprintTypeTrackColor;
	}

	// Cues share one color of their own - a Blueprint cue its own, when it sets one - so what makes something happen reads
	// apart from what moves something.
	if (IsCueLayer(Layer))
	{
		const FWidgetAnimBlueprintCueParams* BlueprintCue = Layer.TypeParams.GetPtr<FWidgetAnimBlueprintCueParams>();
		const UWidgetAnimBlueprintCue* CueObject = BlueprintCue ? BlueprintCue->GetTypeObject() : nullptr;
		return CueObject && CueObject->bOverrideTrackColor ? CueObject->TrackColor : UWidgetAnimEditorSettings::Get().CueTrackColor;
	}
	return GetAnimTypeColor(Layer.AnimType);
}

bool SWidgetAnimTimeline::IsCueLayer(const FWidgetAnimLayer& Layer)
{
	return Layer.AnimType == EWidgetAnimType::Custom && WidgetAnimIsRegisteredCue(Layer.TypeParams);
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
	// An animation track has no easing of its own - each of its animation's tracks has its own - so it says what it is.
	if (Layer.IsAnimationTrack())
	{
		// One following a float value names it: what gameplay sets to move it along.
		if (Layer.IsValueDriven())
		{
			return Layer.ValueName.IsNone()
				? LOCTEXT("AnimationTrackFloatValueUnnamed", "Float Value (no name)")
				: FText::Format(LOCTEXT("AnimationTrackFloatValue", "Float Value: {0}"), FText::FromName(Layer.ValueName));
		}

		const int32 NumTracks = Layer.InsertedAnimation->Layers.Num();
		return Layer.InsertedAnimation->IsPercentTime()
			? FText::Format(LOCTEXT("AnimationTrackPercentEasingName", "Percentage Animation, {0} {0}|plural(one=track,other=tracks)"), NumTracks)
			: FText::Format(LOCTEXT("AnimationTrackEasingName", "Widget Animation, {0} {0}|plural(one=track,other=tracks)"), NumTracks);
	}

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

	// A Shake has no easing - its Falloff is how its strength changes over its run - so it is named by that.
	if (Layer.AnimType == EWidgetAnimType::Shake)
	{
		const UEnum* FalloffEnum = StaticEnum<EWidgetAnimShakeFalloff>();
		return FalloffEnum ? FalloffEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.Shake.Falloff)) : FText::GetEmpty();
	}

	// A track with no keys shaped by handles dragged in the Graph Editor moves by them, not by its Easing.
	if (WidgetAnimTypeSupportsValueKeys(Layer.AnimType) && Layer.ValueKeys.UsesStartCurveAlone())
	{
		return GetSegmentCurveName(Layer.ValueKeys.StartCurve);
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
	if (Overshoot > 0.f && Layer.ValueKeys.Keys.Num() == 0 && Layer.Easing != EWidgetAnimEasing::Hold)
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

namespace WidgetAnimCueToolTip
{
	/** Where and when a cue track fires, in words - "At its start and 2 keys, playing forward". */
	FString DescribeCueFiring(const FWidgetAnimLayer& Layer)
	{
		TArray<TPair<int32, float>, TInlineAllocator<8>> Points;
		WidgetAnimCues::GetFiringPoints(Layer.TypeParams, Layer.ValueKeys, Points);

		const int32 NumPoints = Layer.ValueKeys.NumPoints();
		bool bStart = false;
		bool bEnd = false;
		int32 NumKeys = 0;
		for (const TPair<int32, float>& Point : Points)
		{
			bStart = bStart || Point.Key == 0;
			bEnd = bEnd || Point.Key == NumPoints - 1;
			NumKeys += (Point.Key > 0 && Point.Key < NumPoints - 1) ? 1 : 0;
		}

		TArray<FString> Where;
		if (bStart)
		{
			Where.Add(TEXT("its start"));
		}
		if (NumKeys > 0)
		{
			Where.Add(NumKeys == 1 ? FString(TEXT("1 key")) : FString::Printf(TEXT("%d keys"), NumKeys));
		}
		if (bEnd)
		{
			Where.Add(TEXT("its end"));
		}
		if (Where.Num() == 0)
		{
			return TEXT("Nowhere - Fire At Start and Fire At End are off, and it has no keys");
		}

		FString Description = TEXT("At ") + (Where.Num() == 1 ? Where[0] : FString::Join(TArrayView<const FString>(Where.GetData(), Where.Num() - 1), TEXT(", ")) + TEXT(" and ") + Where.Last());

		static const FWidgetAnimCueParams Defaults;
		const FWidgetAnimCueParams* Rules = WidgetAnimCues::GetCueParams(Layer.TypeParams);
		const FWidgetAnimCueParams& Cue = Rules ? *Rules : Defaults;
		switch (Cue.Direction)
		{
		case EWidgetAnimCueDirection::Forward:  Description += TEXT(", playing forward"); break;
		case EWidgetAnimCueDirection::Backward: Description += TEXT(", only on the way back of a Ping Pong"); break;
		case EWidgetAnimCueDirection::Both:     Description += TEXT(", either way"); break;
		}
		if (!Cue.bFireWhenInverted)
		{
			Description += TEXT("; silent when inverted");
		}
		return Description;
	}
}

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

	// A renamed track's title is its name, so what type it is goes first.
	if (!Layer.TrackName.TrimStartAndEnd().IsEmpty())
	{
		Rows.Add(TEXT("Type"), GetLayerTypeName(Layer).ToString());
	}

	// What it does - each type its own settings, a start value in green and an end value in red.
	switch (Layer.AnimType)
	{
	case EWidgetAnimType::Fade:
		Rows.Add(TEXT("Opacity"), StartToEnd(Number(Layer.StartOpacity), Number(Layer.EndOpacity)));
		break;

	case EWidgetAnimType::Scale:
		Rows.Add(TEXT("Scale"), StartToEnd(Pair(Layer.StartScale, true), Pair(Layer.EndScale, true)));
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

	case EWidgetAnimType::Shake:
	{
		Rows.Add(TEXT("Distance"), TEXT("\u00B1") + Pair(Layer.Shake.Distance, /*bCollapseUniform=*/true));
		Rows.Add(TEXT("Angle"), TEXT("\u00B1") + Angle(Layer.Shake.Angle));
		Rows.Add(TEXT("Shakes Per Second"), Number(Layer.Shake.Frequency, 1));
		if (!Layer.IsValueDriven())
		{
			const UEnum* FalloffEnum = StaticEnum<EWidgetAnimShakeFalloff>();
			Rows.Add(TEXT("Falloff"), FalloffEnum ? FalloffEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.Shake.Falloff)).ToString() : FString());
		}
		if (Layer.Shake.Seed != 0)
		{
			Rows.Add(TEXT("Seed"), FString::FromInt(Layer.Shake.Seed));
		}
		break;
	}

	case EWidgetAnimType::MaterialScalar:
		Rows.Add(TEXT("Material"), WidgetAnimMaterialCheck::GetSlotName(Layer.MaterialSlot).ToString());
		Rows.Add(TEXT("Parameter"), Layer.ScalarParameterName.IsNone() ? FString(TEXT("(none)")) : Layer.ScalarParameterName.ToString());
		Rows.Add(TEXT("Value"), StartToEnd(Number(Layer.StartScalarValue), Number(Layer.EndScalarValue)));
		break;

	case EWidgetAnimType::MaterialVector:
	{
		Rows.Add(TEXT("Material"), WidgetAnimMaterialCheck::GetSlotName(Layer.MaterialSlot).ToString());
		Rows.Add(TEXT("Parameter"), Layer.VectorParameterName.IsNone() ? FString(TEXT("(none)")) : Layer.VectorParameterName.ToString());

		// Each end as a rounded square of its color, start to end.
		Rows.Add(TEXT("Value"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ColorSquare(Layer.StartVectorValue) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 0.f))[ Arrow() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ ColorSquare(Layer.EndVectorValue) ]);
		break;
	}

	case EWidgetAnimType::Custom:
		// An animation track: what it plays, and how much of it.
		if (Layer.IsAnimationTrack())
		{
			const UWidgetAnimPresetAsset* Inserted = Layer.InsertedAnimation;
			Rows.Add(TEXT("Plays"), Inserted->GetName());
			Rows.Add(TEXT("Tracks"), FString::Printf(TEXT("%d, in %d %s"), Inserted->Layers.Num(), FMath::Max(Inserted->LayerGroups.Num(), 1), Inserted->LayerGroups.Num() > 1 ? TEXT("groups") : TEXT("group")));
			if (Inserted->IsPercentTime())
			{
				Rows.Add(TEXT("Time In"), TEXT("Percent (0 - 100%)"));
			}
			if (Layer.IsValueDriven())
			{
				Rows.Add(TEXT("Moved By"), Layer.ValueName.IsNone() ? FString(TEXT("Float Value (no name)")) : FString::Printf(TEXT("Float Value: %s"), *Layer.ValueName.ToString()));
			}
			else if (Layer.FitsAnimationToTrack())
			{
				Rows.Add(TEXT("Moved By"), TEXT("Across the Track"));
			}
			if (!FMath::IsNearlyEqual(UWidgetAnimLibrary::GetInsertedPlaySpeed(Layer), 1.f))
			{
				Rows.Add(TEXT("Speed"), Number(UWidgetAnimLibrary::GetInsertedPlaySpeed(Layer)));
			}
			break;
		}

		// A registered type's start and end, under the name it gives its values. Nothing to show for one whose module
		// isn't loaded - the blurb below says so.
		if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
		{
			// A cue: what it does, and where and when it fires, in place of values it travels between.
			if (Evaluator->IsCue())
			{
				const FText Summary = Evaluator->GetTrackSummary(Layer.TypeParams);
				if (!Summary.IsEmpty())
				{
					Rows.Add(TEXT("Does"), Summary.ToString());
				}
				Rows.Add(TEXT("Fires"), WidgetAnimCueToolTip::DescribeCueFiring(Layer));
				Rows.Add(Evaluator->GetTrackValueLabel(Layer.TypeParams).ToString(), Number(Evaluator->GetEndpointValue(Layer.TypeParams, true)));
				break;
			}

			Rows.Add(Evaluator->GetTrackValueLabel(Layer.TypeParams).ToString(),
				StartToEnd(Number(Evaluator->GetEndpointValue(Layer.TypeParams, true)), Number(Evaluator->GetEndpointValue(Layer.TypeParams, false))));
		}
		break;

	default:
		break;
	}

	// A material track that can't drive anything on the preview says why.
	if (Source.IsValid())
	{
		const FText MaterialProblem = WidgetAnimMaterialCheck::Check(Source->GetLayerPreviewWidget(LayerIndex), Layer);
		if (!MaterialProblem.IsEmpty())
		{
			Rows.Add(TEXT("Material Note"),
				SNew(SBox)
				.MaxDesiredWidth(340.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(MaterialProblem)
					.Font(ValueFont())
					.ColorAndOpacity(Palette::TooltipValue)
				]);
		}
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
	// shaped one by one are each named, in order; one left at Track Easing by the track's own easing. Not on a cue,
	// which doesn't move.
	if (!IsCueLayer(Layer))
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

		if (Expander->GathersParts() && !UWidgetAnimLibrary::TypeTakesParts(Layer.AnimType, Layer.TypeParams))
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
		if (WidgetAnimTypeHasValueDrives(Layer.AnimType))
		{
			const UEnum* DrivesEnum = StaticEnum<EWidgetAnimTransformValueTarget>();
			Rows.Add(TEXT("Drives"), DrivesEnum ? DrivesEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.ValueDrives)).ToString() : FString());
		}
	}

	// A Transform layer: how many keys, then what they change - only the parts of the pose that move away from
	// the identity at some key, one column each, one row per key. Many keys show the first and last few.
	TSharedPtr<SWidget> KeyTable;
	FString KeyNote;
	if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
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
		for (const FChannel& Channel : TransformChannels(WidgetAnimKeyframeContentFor(Layer.AnimType)))
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
				Cell(1, Row, Text(FormatTime(Span.X + Time), KeyValueColor(Key, NumKeys, false), ValueFont()), false);
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
			Note((Layer.AnimType == EWidgetAnimType::Pulse ? WidgetAnimTips::ValueDrivenPulseNote()
				: Layer.AnimType == EWidgetAnimType::Shake ? WidgetAnimTips::ValueDrivenShakeNote()
				: WidgetAnimTips::ValueDrivenTrackNote()).ToString())
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
	case EWidgetAnimType::Shake:          Blurb = WidgetAnimTips::ShakeBlurbTooltip(); break;
	case EWidgetAnimType::MaterialScalar: Blurb = WidgetAnimTips::MaterialScalarBlurbTooltip(); break;
	case EWidgetAnimType::MaterialVector: Blurb = WidgetAnimTips::MaterialVectorBlurbTooltip(); break;
	case EWidgetAnimType::Transform:      Blurb = WidgetAnimTips::TransformBlurbTooltip(); break;
	case EWidgetAnimType::ShapeBox:       Blurb = WidgetAnimTips::ShapeBoxBlurbTooltip(); break;
	case EWidgetAnimType::Shape:          Blurb = WidgetAnimTips::ShapeBlurbTooltip(); break;
	case EWidgetAnimType::Design:         Blurb = WidgetAnimTips::DesignBlurbTooltip(); break;
	case EWidgetAnimType::Custom:
	{
		// A registered type describes itself - it may come from a module this plugin knows nothing about.
		const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
		// An animation track: what its animation is for, as its Description says - or, with none, what the track is.
		Blurb = Layer.IsAnimationTrack()
			? (!Layer.InsertedAnimation->Description.IsEmpty() ? Layer.InsertedAnimation->Description : WidgetAnimTips::AnimationTrackShortNote())
			: Evaluator ? Evaluator->GetTrackDescription(Layer.TypeParams)
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

	// A Shape Box track acts through a Shape Box - see UWidgetAnimLibrary::ResolveShapeBox - and does nothing without
	// one; a Shape track through a shape. Said only when the preview shows its target isn't one.
	if (Layer.AnimType == EWidgetAnimType::ShapeBox || Layer.AnimType == EWidgetAnimType::Shape)
	{
		UWidget* Target = Source.IsValid() ? Source->GetLayerPreviewWidget(LayerIndex) : nullptr;
		const bool bShapeBox = Layer.AnimType == EWidgetAnimType::ShapeBox;
		const bool bMissing = Target && (bShapeBox
			? UWidgetAnimLibrary::ResolveShapeBox(Target) == nullptr
			: WidgetAnimShapeTrack::ResolveShape(Target, Layer.Transform.ShapeKind.Get()) == nullptr);
		if (bMissing)
		{
			Body->AddSlot()
			.AutoHeight()
			[
				Warning(bShapeBox ? WidgetAnimTips::TransformShapeBoxWarningTooltip() : WidgetAnimTips::ShapeTrackTargetWarningTooltip())
			];
		}
	}

	return Frame(
		IsReadOnly() || IsLayerLocked(LayerIndex) || IsLayerHidden(LayerIndex) ? GetLockedColor(GetLayerTypeColor(Layer)) : GetLayerTypeColor(Layer),
		GetLayerDisplayName(Layer).ToString(),
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
	TArray<float> KeyTimes;
	GetKeyTimes(LayerIndex, KeyTimes);
	if (!KeyTimes.IsValidIndex(KeyIndex))
	{
		return MakeLayerToolTip(LayerIndex);
	}

	// A small tip, as a key is a small thing: which key it is, when, and what it holds - in the panel's own dark tooltip, a
	// line in the track's color down its side. The track's own tooltip, over its body, has the rest.
	const int32 NumKeys = KeyTimes.Num();
	const float LocalTime = KeyTimes[KeyIndex];
	const bool bKeyframed = WidgetAnimTypeIsKeyframed(Layer.AnimType);
	const FString Where = KeyIndex == 0 ? TEXT("Start") : KeyIndex == NumKeys - 1 ? TEXT("End") : FString();
	const FString Title = Where.IsEmpty()
		? FString::Printf(TEXT("Key %d of %d"), KeyIndex + 1, NumKeys)
		: FString::Printf(TEXT("Key %d of %d  \u00B7  %s"), KeyIndex + 1, NumKeys, *Where);

	// What it holds: a keyed track's value; a keyframed track's pose, as what it changes - at most a few of them.
	TSharedRef<SWidget> Value = SNullWidget::NullWidget;
	if (!bKeyframed)
	{
		Value = KeyValue(Layer, KeyIndex, KeyValueColor(KeyIndex, NumKeys, false));
	}
	else if (Layer.Transform.Keys.IsValidIndex(KeyIndex))
	{
		const FWidgetTransformKeyframe& Keyframe = Layer.Transform.Keys[KeyIndex];
		const bool bAuthored = Layer.AnimType == EWidgetAnimType::Transform ? Keyframe.bUseAuthoredTransform : Keyframe.bUseAuthoredShape;
		FString Summary;
		if (bAuthored)
		{
			Summary = FString::Printf(TEXT("Authored %s"), *GetLayerTypeName(Layer).ToString());
		}
		else if (Layer.AnimType != EWidgetAnimType::Transform)
		{
			// A shape's or a design's settings are too many to list: the Selection panel shows them.
			Summary = FString::Printf(TEXT("Its own %s"), *GetLayerTypeName(Layer).ToString());
		}
		else
		{
			int32 NumChanged = 0;
			for (const FChannel& Channel : TransformChannels(WidgetAnimKeyframeContentFor(Layer.AnimType)))
			{
				if (!Channel.IsDefault(Keyframe))
				{
					if (NumChanged < 3)
					{
						Summary += FString::Printf(TEXT("%s%s %s"), Summary.IsEmpty() ? TEXT("") : TEXT(",  "), *Channel.Name, *Channel.Format(Keyframe));
					}
					++NumChanged;
				}
			}
			if (NumChanged > 3)
			{
				Summary += FString::Printf(TEXT("  +%d"), NumChanged - 3);
			}
			if (NumChanged == 0)
			{
				Summary = TEXT("No change");
			}
		}
		Value = Text(Summary, KeyValueColor(KeyIndex, NumKeys, false), ValueFont());
	}

	return SNew(SBox)
		.Padding(FMargin(8.f, 6.f, 11.f, 7.f))
		.MaxDesiredWidth(360.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(0.f, 1.f, 8.f, 1.f))
			[
				SNew(SBox)
				.WidthOverride(3.f)
				[
					SNew(SImage)
					.Image(WidgetAnimTimeline::RoundedFill(1.5f))
					.ColorAndOpacity(GetLayerTypeColor(Layer).CopyWithNewOpacity(1.f))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
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
						Text(Title, Palette::TooltipTitle, BodyFont())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					.Padding(FMargin(10.f, 0.f, 0.f, 0.f))
					[
						Text(FormatTime(Spans[LayerIndex].X + LocalTime), Palette::TooltipLabel, SmallFont())
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
				[
					Value
				]
			]
		];
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
				if (!Channel(Layer.Transform.GetKeyTransform(Key)).Equals(Channel(Layer.Transform.GetKeyTransform(0)), 1.e-4))
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

		case EWidgetAnimType::Shake:
			return (Value == EWatched::Translation && !Layer.Shake.Distance.IsNearlyZero())
				|| (Value == EWatched::Rotation && Layer.Shake.Angle > KINDA_SMALL_NUMBER);

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
						const FName Path = WidgetAnimTargetOption::ToPath(Option);
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

// -----------------------------------------------------------------------------------------------------
// The Script area
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::SetScriptOpen(bool bOpen)
{
	bScriptOpen = bOpen;
	GConfig->SetBool(RowHeightConfigSection, ScriptOpenConfigKey, bScriptOpen, GEditorPerProjectIni);
}

void SWidgetAnimTimeline::OpenScriptWindow()
{
	if (const TSharedPtr<SWindow> Existing = ScriptWindow.Pin())
	{
		Existing->BringToFront();
		return;
	}

	// Out from under the Track Editor - the script goes with it, being one for the session.
	SetScriptOpen(false);

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("ScriptWindowTitle", "Widget Animation Script"))
		.ClientSize(FVector2D(980.f, 560.f))
		.SupportsMinimize(true)
		.SupportsMaximize(true);

	const TWeakPtr<SWindow> WeakWindow = Window;
	Window->SetContent(
		SNew(SWidgetAnimScriptPanel)
		.Panel(SharedThis(this))
		.ScriptKey(GetScriptKey())
		.InWindow(true)
		.OnClose_Lambda([WeakWindow]()
		{
			if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
			{
				Pinned->RequestDestroyWindow();
			}
		})
		.OnDock_Lambda([this]() { DockScript(); }));

	// A child of the editor's window, so it stays above it, and goes with it.
	const TSharedPtr<SWindow> Parent = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (Parent.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(Window, Parent.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(Window);
	}
	ScriptWindow = Window;
}

FString SWidgetAnimTimeline::GetScriptKey() const
{
	// The Widget Blueprint the panel is part of, so its scripts are that widget's - else whatever the panel edits.
	if (!Source.IsValid())
	{
		return FString();
	}
	if (const UWidgetBlueprint* Blueprint = Source->GetWidgetBlueprint())
	{
		return SWidgetAnimScriptPanel::MakeScriptKey(Blueprint);
	}
	return SWidgetAnimScriptPanel::MakeScriptKey(Source->GetEditedObject());
}

bool SWidgetAnimTimeline::IsScriptDocked() const
{
	return FWidgetAnimBuilderToolbar::IsScriptingDocked(Source.IsValid() ? Source->GetWidgetBlueprint() : nullptr);
}

void SWidgetAnimTimeline::DockScript()
{
	// Docked by the editor, not by the panel: the Widget Animation Scripting drawer's tab, the one Dock in Layout opens from
	// the drawer too. It finds the panel it runs on for itself, so it is the same tab whichever Script area docked it.
	if (FWidgetAnimBuilderToolbar::DockScripting(Source.IsValid() ? Source->GetWidgetBlueprint() : nullptr))
	{
		// Out from under the Track Editor, and out of its window - the script goes with it, being one for the session.
		if (const TSharedPtr<SWindow> Window = ScriptWindow.Pin())
		{
			Window->RequestDestroyWindow();
			ScriptWindow.Reset();
		}
		SetScriptOpen(false);
		return;
	}

	// Not in a Widget Blueprint editor's layout (or it couldn't be placed): a window of its own, which can go anywhere.
	UE_LOG(LogWidgetAnimScriptDock, Log, TEXT("Dock in Layout: no Widget Blueprint editor to dock the script editor in - opening it in a window instead."));
	OpenScriptWindow();
}

void SWidgetAnimTimeline::OnScriptRan(int32 TracksBefore, int32 TracksAfter, int32 GroupsBefore, int32 GroupsAfter)
{
	// Folds are kept by position, which a script adding or removing groups or tracks leaves meaning something else.
	if (GroupsAfter != GroupsBefore)
	{
		TrackCollapsedGroups.Reset();
		ListCollapsedGroups.Reset();
	}
	if (TracksAfter != TracksBefore)
	{
		ExpandedAnimationTracks.Reset();
		FoldedNestedGroups.Reset();
	}

	// New tracks go in at the end, outside a collapsed track open on its own, where they would be left out of view and
	// out of the preview: back to the whole animation.
	if (IsInCollapsedScope() && TracksAfter > TracksBefore)
	{
		ActiveCollapsed = NAME_None;
		SyncScopeMutes();
	}
	if (IsGraphOpen() && TracksAfter != TracksBefore)
	{
		ExitGraph();
	}

	ClearSelection();
	RebuildRows();
	RefreshDetails();

	// What the script made, selected: tracks are only ever added at the end.
	if (TracksAfter > TracksBefore)
	{
		TArray<int32> Added;
		for (int32 LayerIndex = TracksBefore; LayerIndex < TracksAfter; ++LayerIndex)
		{
			Added.Add(LayerIndex);
		}
		SelectInBox({}, Added, /*bAdd=*/false, /*bToggle=*/false);
	}
}

// -----------------------------------------------------------------------------------------------------
// Animation timelines - a Widget Blueprint's own animations
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimTimeline::SwitchEditedAnimation(TFunctionRef<void()> Switch)
{
	// Out of a collapsed track first, while its mutes are still on the animation they were set on - the next one has
	// none, and a mute left behind would silence that track in this one's previews.
	if (IsDragging())
	{
		IsDraggingKey() ? EndKeyDrag() : EndLayerDrag();
	}
	if (IsInCollapsedScope() || IsGraphOpen())
	{
		ActiveCollapsed = NAME_None;
		GraphLayers.Reset();
		SyncScopeMutes();
	}

	Switch();

	// Fold state is kept by index, which means nothing in another animation.
	ExpandedAnimationTracks.Reset();
	FoldedNestedGroups.Reset();
	ClearSelection();
	RebuildRows();
	RefreshDetails();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTimelinePicker()
{
	using namespace WidgetAnimTimeline;

	// The open timeline's name, as a field: double-click it to rename the timeline. None open yet: read-only, in the color
	// of the thing to click - the arrow beside it.
	const TSharedRef<SWidgetAnimNameField> NameText = SNew(SWidgetAnimNameField)
		.Text_Lambda([this]() { return Source.IsValid() ? Source->GetEditedName() : FText::GetEmpty(); })
		.TextColor_Lambda([this]()
		{
			const bool bNone = Source.IsValid() && Source->GetTimelineName().IsEmpty();
			return bNone ? FSlateColor(Palette::NoTimelineText) : FSlateColor::UseForeground();
		})
		.IsReadOnly_Lambda([this]() { return !Source.IsValid() || Source->GetTimelineName().IsEmpty(); })
		.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
		{
			if (!Source.IsValid())
			{
				return;
			}
			const FString Open = Source->GetTimelineName();
			const FString Typed = NewText.ToString().TrimStartAndEnd();
			if (!Open.IsEmpty() && !Typed.IsEmpty() && Typed != Open)
			{
				Source->RenameTimeline(Open, Typed);
			}
		});
	const TWeakPtr<SWidgetAnimNameField> WeakNameText = NameText;

	// The field and, at its right, the list: every timeline, New, Rename, Duplicate, Delete.
	return SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FStyleColors::Input)
		.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
		.Visibility_Lambda([this]() { return Source.IsValid() && Source->IsEditingTimeline() ? EVisibility::Visible : EVisibility::Collapsed; })
		.ToolTip(MakePanelToolTip(WidgetAnimTips::TimelinePickerTooltip()))
		.OnMouseDoubleClick_Lambda([this, WeakNameText](const FGeometry&, const FPointerEvent& MouseEvent)
		{
			const TSharedPtr<SWidgetAnimNameField> Pinned = WeakNameText.Pin();
			if (!Pinned.IsValid() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !Source.IsValid() || Source->GetTimelineName().IsEmpty())
			{
				return FReply::Unhandled();
			}
			Pinned->EnterEditingMode();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
			[
				MakeFixedIcon(TimelineIcon())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.MinDesiredWidth(120.f)
				[
					NameText
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.HasDownArrow(true)
				.ContentPadding(FMargin(2.f, 2.f))
				.OnGetMenuContent(this, &SWidgetAnimTimeline::MakeTimelineMenu)
				.ButtonContent()
				[
					SNullWidget::NullWidget
				]
			]
		];
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeTimelineMenu()
{
	using namespace WidgetAnimTimeline;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	if (!Source.IsValid() || !Source->SupportsTimelines())
	{
		return Menu.MakeWidget();
	}

	TArray<FString> Names;
	Source->GetTimelineNames(Names);
	const FString Open = Source->GetTimelineName();

	// Every timeline of the Widget Blueprint, the open one checked.
	if (Names.Num() > 0)
	{
		Menu.BeginSection("Timelines", LOCTEXT("TimelinesSection", "Timelines in This Widget Blueprint"));
		for (const FString& Name : Names)
		{
			Menu.AddMenuEntry(
				FText::FromString(Name),
				WidgetAnimTips::OpenTimelineTooltip(),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, Name]()
					{
						if (Source.IsValid() && Source->GetTimelineName() != Name)
						{
							SwitchEditedAnimation([this, &Name]() { Source->OpenTimeline(Name); });
						}
					}),
					FCanExecuteAction(),
					FIsActionChecked::CreateLambda([this, Name]() { return Source.IsValid() && Source->GetTimelineName() == Name; })),
				NAME_None,
				EUserInterfaceActionType::RadioButton);
		}
		Menu.EndSection();
	}

	Menu.BeginSection("Timeline", LOCTEXT("TimelineSection", "Timeline"));

	// A new one, named as it is made.
	Menu.AddSubMenu(
		LOCTEXT("NewTimelineEntry", "New Timeline"),
		WidgetAnimTips::NewTimelineTooltip(),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenu)
		{
			SubMenu.AddWidget(
				WidgetAnimFolderMenu::MakeNameBox(FText::GetEmpty(), WidgetAnimTips::NewTimelineHint(), [this](const FString& Name)
				{
					if (Source.IsValid())
					{
						SwitchEditedAnimation([this, &Name]() { Source->CreateTimeline(Name); });
					}
				}, /*bAllowEmpty=*/true),
				FText::GetEmpty(), /*bNoIndent=*/true);
		}),
		false,
		FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuAddIcon));

	if (!Open.IsEmpty())
	{
		Menu.AddSubMenu(
			FText::Format(LOCTEXT("RenameTimelineEntry", "Rename \"{0}\""), FText::FromString(Open)),
			WidgetAnimTips::RenameTimelineTooltip(),
			FNewMenuDelegate::CreateLambda([this, Open](FMenuBuilder& SubMenu)
			{
				SubMenu.AddWidget(
					WidgetAnimFolderMenu::MakeNameBox(FText::FromString(Open), WidgetAnimTips::RenameTimelineHint(), [this, Open](const FString& Name)
					{
						if (Source.IsValid())
						{
							Source->RenameTimeline(Open, Name);
						}
					}),
					FText::GetEmpty(), /*bNoIndent=*/true);
			}),
			false,
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"));

		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("DuplicateTimelineEntry", "Duplicate \"{0}\""), FText::FromString(Open)),
			WidgetAnimTips::DuplicateTimelineTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Duplicate"),
			FUIAction(FExecuteAction::CreateLambda([this, Open]()
			{
				if (Source.IsValid())
				{
					SwitchEditedAnimation([this, &Open]() { Source->CreateTimeline(Open, Open); });
				}
			})));

		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("DeleteTimelineEntry", "Delete \"{0}\""), FText::FromString(Open)),
			WidgetAnimTips::DeleteTimelineTooltip(),
			FSlateIcon(FNameTBDStyle::GetStyleSetName(), FNameTBDStyle::MenuDeleteIcon),
			FUIAction(FExecuteAction::CreateLambda([this, Open]()
			{
				if (!Source.IsValid())
				{
					return;
				}

				// Asked first when it has tracks: they go with it. Undo still brings it back.
				const TArray<FWidgetAnimLayer>* Layers = Source->GetLayers();
				const int32 NumTracks = Layers ? Layers->Num() : 0;
				if (NumTracks > 0)
				{
					const FText Question = FText::Format(WidgetAnimTips::DeleteTimelineQuestion(), FText::FromString(Open), FText::AsNumber(NumTracks));
					if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
					{
						return;
					}
				}
				SwitchEditedAnimation([this, &Open]() { Source->DeleteTimeline(Open); });
			})));
	}
	Menu.EndSection();

	return Menu.MakeWidget();
}

// -----------------------------------------------------------------------------------------------------
// Moving tracks with the arrow keys
// -----------------------------------------------------------------------------------------------------

namespace WidgetAnimKeyboardMove
{
	/** A press moves the tracks this many pixels' worth of time at the zoom in force - ten times that with Shift. */
	constexpr float StepPixels = 4.f;
	constexpr float LargeStepFactor = 10.f;

	/** Held this long, the tracks start moving on their own... */
	constexpr double HoldDelaySeconds = 0.3;

	/** ...at this many pixels a second, rising to the fastest over RampSeconds - fine at first, quick once it is clear the key is staying down. */
	constexpr float SlowestPixelsPerSecond = 90.f;
	constexpr float FastestPixelsPerSecond = 900.f;
	constexpr float RampSeconds = 1.5f;
}

float SWidgetAnimTimeline::GetSecondsPerPixel() const
{
	if (!Tracks.IsValid())
	{
		return 0.01f;
	}

	FVector2f Range;
	bool bUserView = false;
	float ScrollY = 0.f;
	Tracks->GetViewState(Range, bUserView, ScrollY);
	const float Width = Tracks->GetCachedGeometry().GetLocalSize().X;
	return Width > 1.f ? FMath::Max(Range.Y - Range.X, KINDA_SMALL_NUMBER) / Width : 0.01f;
}

bool SWidgetAnimTimeline::BeginKeyboardMove(int32 Direction, bool bLarge)
{
	using namespace WidgetAnimKeyboardMove;

	// What is left of a move whose drag something else ended.
	EndKeyboardMove();

	// Tracks only: with keys picked, the arrows leave them be.
	if (!CanEdit() || IsDragging() || IsKeyMultiSelection() || Selection.Key != INDEX_NONE)
	{
		return false;
	}

	// The first selected track that can move is the one "pressed"; the drag takes every selected one that can.
	int32 Pressed = INDEX_NONE;
	for (const int32 LayerIndex : GetClipboardLayers())
	{
		if (CanMoveLayer(LayerIndex))
		{
			Pressed = LayerIndex;
			break;
		}
	}
	if (Pressed == INDEX_NONE || !BeginLayerDrag(Pressed, ELayerDrag::Move))
	{
		return false;
	}

	// Arrow Keys Can Extend the Animation: past its end the animation grows; before its start the rest moves later. Not
	// before the start when a track left behind is locked - it would be moved all the same, as a range drag refuses to.
	if (UWidgetAnimEditorSettings::Get().bArrowKeysExtendAnimation && !IsPercentTime())
	{
		LayerDrag.bEndIsFree = true;

		bool bLockedLeftBehind = false;
		const TArray<FWidgetAnimLayer>* Layers = GetLayers();
		for (int32 LayerIndex = 0; Layers && LayerIndex < Layers->Num() && !bLockedLeftBehind; ++LayerIndex)
		{
			bLockedLeftBehind = IsLayerLocked(LayerIndex) && !LayerDrag.IsStartEdge(LayerIndex);
		}
		LayerDrag.bStartIsFree = !bLockedLeftBehind;
		LayerDrag.OriginalMinimumLength = Source.IsValid() ? Source->GetMinimumLength() : 0.f;
	}

	KeyboardMoveDirection = Direction;
	bKeyboardMoveLarge = bLarge;
	KeyboardMoveStartTime = FSlateApplication::Get().GetCurrentTime();
	KeyboardMoveOffset = Direction * StepPixels * (bLarge ? LargeStepFactor : 1.f) * GetSecondsPerPixel();
	UpdateLayerDrag(KeyboardMoveOffset, 0, /*SnapThresholdSeconds=*/0.f);

	if (!KeyboardMoveTimer.IsValid())
	{
		KeyboardMoveTimer = RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SWidgetAnimTimeline::TickKeyboardMove));
	}
	return true;
}

EActiveTimerReturnType SWidgetAnimTimeline::TickKeyboardMove(double CurrentTime, float DeltaTime)
{
	using namespace WidgetAnimKeyboardMove;

	if (!IsKeyboardMoving())
	{
		KeyboardMoveDirection = 0;
		KeyboardMoveTimer.Reset();
		return EActiveTimerReturnType::Stop;
	}

	const double Held = CurrentTime - KeyboardMoveStartTime;
	if (Held < HoldDelaySeconds)
	{
		return EActiveTimerReturnType::Continue;
	}

	// Faster the longer it is held - in pixels, so it feels the same at any zoom.
	const float Ramp = FMath::Clamp(static_cast<float>(Held - HoldDelaySeconds) / RampSeconds, 0.f, 1.f);
	const float PixelsPerSecond = FMath::Lerp(SlowestPixelsPerSecond, FastestPixelsPerSecond, Ramp * Ramp) * (bKeyboardMoveLarge ? 2.f : 1.f);
	KeyboardMoveOffset += KeyboardMoveDirection * PixelsPerSecond * GetSecondsPerPixel() * DeltaTime;
	UpdateLayerDrag(KeyboardMoveOffset, 0, /*SnapThresholdSeconds=*/0.f);

	// The view follows what is moving, so a long hold doesn't carry it off screen.
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (Tracks.IsValid() && Layers && Layers->IsValidIndex(Selection.Layer))
	{
		const FVector2f Span = GetShownSpan(Selection.Layer);
		Tracks->RevealTime(Span.X, Span.X + Span.Y);
	}
	return EActiveTimerReturnType::Continue;
}

void SWidgetAnimTimeline::EndKeyboardMove()
{
	if (KeyboardMoveDirection == 0)
	{
		return;
	}

	KeyboardMoveDirection = 0;
	KeyboardMoveOffset = 0.f;
	if (KeyboardMoveTimer.IsValid())
	{
		UnRegisterActiveTimer(KeyboardMoveTimer.ToSharedRef());
		KeyboardMoveTimer.Reset();
	}
	if (IsDraggingLayers())
	{
		EndLayerDrag();
	}
}

bool SWidgetAnimTimeline::ArrowKeysMoveTracks() const
{
	// The panel itself has the focus once a track or a list row is clicked; a text box in the Selection panel keeps its arrows.
	const bool bFocusHere = HasKeyboardFocus() || (Tracks.IsValid() && Tracks->HasKeyboardFocus());
	// Anything selected is enough to keep the arrows here - BeginKeyboardMove decides whether they move it (not keys, not a
	// locked panel), and with nothing to move they simply do nothing rather than taking the focus elsewhere.
	return bFocusHere && GetClipboardLayers().Num() > 0;
}

FReply SWidgetAnimTimeline::OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bArrow = (Key == EKeys::Left || Key == EKeys::Right) && !InKeyEvent.IsControlDown() && !InKeyEvent.IsCommandDown() && !InKeyEvent.IsAltDown();
	if (!bArrow || (!IsKeyboardMoving() && !ArrowKeysMoveTracks()))
	{
		return SCompoundWidget::OnPreviewKeyDown(MyGeometry, InKeyEvent);
	}

	// Left or Right: the selected tracks move that way - a press a step, held they keep going. The key repeats the system
	// sends while it is held are the move's own: it is carried on by its timer, not by them. Handled even when nothing
	// could move (every selected track locked, say), so the key is never passed on as a move to another panel.
	const int32 Direction = Key == EKeys::Right ? 1 : -1;
	if (IsKeyboardMoving())
	{
		if (KeyboardMoveDirection == Direction)
		{
			return FReply::Handled();
		}
		EndKeyboardMove();
	}
	BeginKeyboardMove(Direction, InKeyEvent.IsShiftDown());
	return FReply::Handled();
}

FNavigationReply SWidgetAnimTimeline::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// Up and Down never take the focus out of the panel: something in it that uses them (the Selection panel's rows, a
	// text box) has them first, and an arrow nothing used is not a reason to jump to another panel of the editor.
	const EUINavigation Direction = InNavigationEvent.GetNavigationType();
	if (Direction == EUINavigation::Up || Direction == EUINavigation::Down)
	{
		return FNavigationReply::Stop();
	}
	if ((Direction == EUINavigation::Left || Direction == EUINavigation::Right) && (IsKeyboardMoving() || ArrowKeysMoveTracks()))
	{
		return FNavigationReply::Stop();
	}
	return SCompoundWidget::OnNavigation(MyGeometry, InNavigationEvent);
}

FReply SWidgetAnimTimeline::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsKeyboardMoving() && (Key == EKeys::Left || Key == EKeys::Right) && (Key == EKeys::Right) == (KeyboardMoveDirection > 0))
	{
		EndKeyboardMove();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyUp(MyGeometry, InKeyEvent);
}

void SWidgetAnimTimeline::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	EndKeyboardMove();
	SCompoundWidget::OnFocusLost(InFocusEvent);
}

#undef LOCTEXT_NAMESPACE