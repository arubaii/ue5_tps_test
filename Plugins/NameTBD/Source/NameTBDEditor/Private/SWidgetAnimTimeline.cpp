// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimTimeline.h"

#include "SWidgetAnimAnchoredTip.h"

#include "WidgetAnimEditorSettings.h"
#include "WidgetAnimLayerEditProxy.h"
#include "WidgetAnimPanelSource.h"

#include "NameTBD/WidgetAnimation/WidgetAnimBuilder.h"
#include "NameTBD/WidgetAnimation/WidgetAnimPreset.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBDColors.h"

#include "Algo/Find.h"
#include "Components/Widget.h"
#include "Slate/WidgetTransform.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailCustomization.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
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
#include "Misc/Paths.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimTimeline"

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

	/** Two keys are never closer than this, so a drag can't push one onto its neighbour and swap them. */
	constexpr float MinKeySpacing = 0.001f;

	/** The shortest stretch of time the tracks ever show, so a one-frame animation isn't one giant diamond. */
	constexpr float MinVisibleSeconds = 0.25f;

	/** How close, in pixels, the mouse has to be to a row edge to resize the rows from it. */
	constexpr float RowEdgeGrabDistance = 3.f;

	/** Width of the colour band down the left of a group's rows, in the list and in the Track Editor. */
	constexpr float GroupBandWidth = 4.f;

	/** Height of the strips naming the Animation Layers list and the Track Editor. */
	constexpr float SectionHeaderHeight = 26.f;

	/** Header icons are drawn at this size whatever size their brush was authored at; the transport's larger. */
	constexpr float IconSize = 16.f;
	constexpr float TransportIconSize = 22.f;

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

	// Every fixed colour lives in NameTBDColors.h, under NameTBD::Colors::WidgetAnimEditor, so it can be tuned
	// in one place. Only the theme colours above come from the editor itself.
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

	/** Every layer type the Add Layer menus offer, in the enum's order. */
	void ForEachAnimType(TFunctionRef<void(EWidgetAnimType)> Visit)
	{
		const UEnum* TypeEnum = StaticEnum<EWidgetAnimType>();
		const int32 NumTypes = TypeEnum ? TypeEnum->NumEnums() - 1 : 0; // the last entry is the generated _MAX
		for (int32 Index = 0; Index < NumTypes; ++Index)
		{
			if (!TypeEnum->HasMetaData(TEXT("Hidden"), Index))
			{
				Visit(static_cast<EWidgetAnimType>(TypeEnum->GetValueByIndex(Index)));
			}
		}
	}
}

// =====================================================================================================
// The Track Editor's tooltip
// =====================================================================================================

/**
 * One tooltip for the whole Track Editor, whose content is made fresh each time it opens for whatever is under
 * the mouse - a track, or a key - and which is empty, so never shows, over anything else. Drawn in the panel's
 * own colours rather than the editor's light tooltip: see NameTBDColors.h, Track tooltips.
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
		const float Top = LineIndex * RowHeight;

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
		ResizeRows
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

	/** Where a line's top is drawn, in local pixels, with the lines scrolled. */
	float GetLineTop(int32 LineIndex) const
	{
		return SWidgetAnimTimeline::RulerHeight + LineIndex * GetRowHeight() - ScrollY;
	}

	/** How far down the lines can be scrolled: to the last line, and one line's room below it. */
	float GetMaxScrollY(float Height) const
	{
		const float Content = (GetNumLines() + 1) * GetRowHeight();
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

		const int32 Line = FMath::FloorToInt((Y - SWidgetAnimTimeline::RulerHeight + ScrollY) / GetRowHeight());
		return Line >= 0 && Line < GetNumLines() ? Line : INDEX_NONE;
	}

	/**
	 * The line edge under a local Y, as how many lines lie above it (1 = the bottom of the first line), or
	 * INDEX_NONE. Every edge but the ruler's resizes all the lines at once.
	 */
	int32 RowEdgeAt(float Y) const
	{
		if (Y < SWidgetAnimTimeline::RulerHeight)
		{
			return INDEX_NONE;
		}

		const float RowHeight = GetRowHeight();
		const int32 Edge = FMath::RoundToInt((Y - SWidgetAnimTimeline::RulerHeight + ScrollY) / RowHeight);
		const float EdgeY = GetLineTop(Edge);

		return Edge >= 1 && Edge <= GetNumLines() && EdgeY > SWidgetAnimTimeline::RulerHeight
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

	// Track field on the recessed colour, as Sequencer's is.
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
	// copied from it: the same font, colour and drop shadow, whatever this engine version makes them.
	const FTextBlockStyle& NodeTitleStyle = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("Graph.Node.NodeTitle");
	const FSlateFontInfo& SectionFont = NodeTitleStyle.Font;

	auto NodeTitleText = [&](const FString& String, float X, float Y, const FLinearColor& Color)
	{
		const FVector2f ShadowOffset(NodeTitleStyle.ShadowOffset);
		if (!ShadowOffset.IsNearlyZero())
		{
			Text(String, X + ShadowOffset.X, Y + ShadowOffset.Y, SectionFont, NodeTitleStyle.ShadowColorAndOpacity * FLinearColor(1.f, 1.f, 1.f, Color.A), TextShadowLayer);
		}
		Text(String, X, Y, SectionFont, Color, TextShadowLayer + 1);
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

		// A thin line under every line, as between Sequencer's tracks, and the group's colour down its left edge.
		Box(0.f, Y + RowHeight - 1.f, Size.X, 1.f, SeparatorColor, RowLayer);
		Box(0.f, Y, GroupBandWidth, RowHeight, SWidgetAnimTimeline::GetGroupColor(Line.Group), GridLayer);
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
			// The stretch the group is moving: a slim rounded bar in its (muted) colour, as a parent row
			// summarizes its children in Sequencer.
			const float X0 = TimeToX(GroupSpan.X, Size.X);
			const float X1 = TimeToX(GroupSpan.Y, Size.X);
			Box(X0, CenterY - 3.f, FMath::Max(X1 - X0, 4.f), 6.f, bReadOnly ? SWidgetAnimTimeline::GetLockedColor(GroupColor) : GroupColor, FrameLayer, RoundedFill(2.f));

			// Every key of the group's layers, light and dark-outlined so they read on any group colour.
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

			// Nowhere near the view: nothing of it to draw.
			if (BarX0 > Size.X + 8.f || BarX0 + BarWidth < -8.f)
			{
				continue;
			}

			const bool bLayerSelected = Owner->IsLayerSelected(LayerIndex);
			const bool bTrackLocked = Owner->IsLayerLocked(LayerIndex);
			const bool bTrackHidden = Owner->IsLayerHidden(LayerIndex);
			const bool bWashedOut = bReadOnly || bTrackLocked || bTrackHidden;
			const bool bBarHovered = Drag == EDrag::None && Hover.Layer == LayerIndex && Hover.Key == INDEX_NONE && Hover.Edge == EEdge::None && Hover.bOnSection;

			FLinearColor TypeColor = SWidgetAnimTimeline::GetAnimTypeColor(Layer.AnimType);
			if (bWashedOut)
			{
				TypeColor = SWidgetAnimTimeline::GetLockedColor(TypeColor);
			}

			// A section, drawn the way the editor draws a graph node's title: a dark rounded frame, an opaque body,
			// and on the body the node gloss, the layer colour spilled over it, and a highlight sheen along the
			// top. The gloss and spill textures are partly transparent; the body is what shows through them, the
			// same on every section. Selection is the orange outline of a selected Blueprint node; hover lifts
			// the colour a touch.
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

			if (InnerWidth > 0.f && InnerHeight > 0.f)
			{
				const FLinearColor Spill = WithAlpha(bBarHovered && !bWashedOut ? TypeColor * Palette::SectionHoverLift : TypeColor, 1.f);

				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Palette::SectionBody, BodyLayer, RoundedFill(2.f));
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, bWashedOut ? SWidgetAnimTimeline::GetLockedColor(FLinearColor::White) : FLinearColor::White, GlossLayer, NodeTitleGloss());
				Box(InnerX, InnerTop, InnerWidth, InnerHeight, Spill, SpillLayer, NodeColorSpill());
				Box(InnerX, InnerTop, InnerWidth, FMath::Min(InnerHeight, Palette::SectionSheenHeight), FLinearColor::White, SheenLayer, NodeTitleHighlight());
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

			// Inside it: its type and length on the left, its easing on the right, in the graph node title's own
			// text style. Whichever doesn't fit is dropped, the easing first, so text never spills out.
			const FString Label = FString::Printf(TEXT("%s  %.2fs"), *SWidgetAnimTimeline::GetAnimTypeName(Layer.AnimType).ToString(), Span.Y);
			const FString EasingLabel = SWidgetAnimTimeline::GetEasingName(Layer).ToString();
			const FVector2f LabelSize = MeasureText(Label, SectionFont);
			const FVector2f EasingSize = MeasureText(EasingLabel, SectionFont);
			const float TextY = InnerTop + (InnerHeight - LabelSize.Y) * 0.5f;
			const float TextOpacity = bWashedOut ? Palette::LockedBrightness + 0.2f : 1.f;
			const FLinearColor TitleColor = NodeTitleStyle.ColorAndOpacity.GetSpecifiedColor() * FLinearColor(1.f, 1.f, 1.f, TextOpacity);

			if (TextLeft - BarX0 + LabelSize.X + Padding <= BarWidth)
			{
				NodeTitleText(Label, TextLeft, TextY, TitleColor);
			}

			if (TextLeft - BarX0 + LabelSize.X + EasingSize.X + Padding * 2.f + 12.f <= BarWidth)
			{
				NodeTitleText(EasingLabel, BarX0 + BarWidth - Padding - EasingSize.X, TextY, TitleColor * FLinearColor(1.f, 1.f, 1.f, Palette::SectionSecondaryTextOpacity));
			}

			// Keys: diamonds, button-like - normal, hovered (lighter, bigger), pressed (darker), selected (orange,
			// with a glow). A key that can't be edited is drawn greyed.
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(LayerIndex, KeyTimes);
			const bool bKeysEditable = Owner->CanEditKeys(LayerIndex);
			const float KeyY = InnerTop + InnerHeight * 0.5f;

			for (int32 Key = 0; Key < KeyTimes.Num(); ++Key)
			{
				const float X = TimeToX(Span.X + KeyTimes[Key], Size.X);

				const bool bSelected = Selection.Layer == LayerIndex && Selection.Key == Key;
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
	if (MaxScroll > 0.f)
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

	if (!Line)
	{
		Owner->ClearSelection();
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

	// Empty stretch of a track line: nothing picked - the animation itself.
	if (Hit.Layer == INDEX_NONE)
	{
		Owner->ClearSelection();
		return Reply;
	}

	// Shift and Ctrl / Cmd pick layers the way a file browser picks files, and do nothing else.
	const bool bRange = MouseEvent.IsShiftDown();
	const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();
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

	// A key: select it, and drag it if it can be.
	if (Hit.Key != INDEX_NONE)
	{
		Owner->SelectKey(Hit.Layer, Hit.Key);

		if (Owner->CanEditKeys(Hit.Layer))
		{
			TArray<float> KeyTimes;
			Owner->GetKeyTimes(Hit.Layer, KeyTimes);

			PressKey = Hit.Key;
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
		Owner->ScrubToSeconds(XToTime(Local.X, Width));
		break;

	case EDrag::KeyPending:
		if (bPastThreshold)
		{
			Owner->BeginKeyDrag(PressLayer, PressKey);
			Drag = EDrag::Key;
		}
		break;

	case EDrag::Key:
		Owner->UpdateKeyDrag(XToTime(Local.X, Width) + PressKeyOffset - GetLayerSpan(PressLayer).X, GetSnapThresholdSeconds(Width));
		break;

	case EDrag::LayerPending:
		if (bPastThreshold)
		{
			// A drag, not a click: whatever was selected stays selected and moves together.
			bCollapseSelectionOnRelease = false;
			Drag = Owner->BeginLayerDrag(PressLayer, PendingLayerDrag) ? EDrag::Layer : EDrag::None;
		}
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

	case EDrag::RightPending:
		if (bPastThreshold)
		{
			Drag = EDrag::Pan;
		}
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

	if (Owner && Ended == EDrag::RightPending)
	{
		const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const float Width = MyGeometry.GetLocalSize().X;

		if (Local.Y >= SWidgetAnimTimeline::RulerHeight)
		{
			const FHit Hit = HitTest(Local, Width);
			const FTrackLine* Line = GetLine(Hit.Line);

			if (!Line)
			{
				PushMenu(MouseEvent, Owner->MakeAddMenu(INDEX_NONE));
			}
			else if (Line->Kind == FLineKind::Group)
			{
				Owner->SelectGroup(Line->Group);
				PushMenu(MouseEvent, Owner->MakeGroupContextMenu(Line->Group));
			}
			else if (Hit.Layer == INDEX_NONE)
			{
				// An empty stretch of a group's line: add to that group.
				PushMenu(MouseEvent, Owner->MakeAddMenu(Line->Group));
			}
			else
			{
				// A right-click on one of several selected layers keeps them all, so the menu acts on all of them.
				if (Hit.Key != INDEX_NONE)
				{
					Owner->SelectKey(Hit.Layer, Hit.Key);
				}
				else if (!Owner->IsLayerSelected(Hit.Layer))
				{
					Owner->SelectLayer(Hit.Layer);
				}

				const TOptional<float> LocalSeconds = Hit.bOnSection
					? TOptional<float>(XToTime(Local.X, Width) - GetLayerSpan(Hit.Layer).X)
					: TOptional<float>();
				PushMenu(MouseEvent, Owner->MakeLayerContextMenu(Hit.Layer, Hit.Key, LocalSeconds));
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

	// Anywhere: the playhead goes to that moment. On a Transform track's body, a new key goes there too. A
	// group's fold arrow has already done its job on the first click.
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

		// The builder's Class Defaults, or an asset - a builder edits an asset in place while it previews one.
		const UObject* Edited = Source->GetEditedObject();
		const bool bEditingAsset = Edited && Edited->IsA<UWidgetAnimPresetAsset>();
		const UClass* PropertyClass = bEditingAsset ? UWidgetAnimPresetAsset::StaticClass() : UWidgetAnimBuilder::StaticClass();
		const SWidgetAnimTimeline::FSelection Selection = Owner->GetSelection();

		// Read-only - the panel locked, or the track - still shows the selection: its fields are only greyed out,
		// by the details view's property-editing delegate. See SWidgetAnimTimeline::IsSelectionEditable.
		if ((Selection.Group == INDEX_NONE && Selection.Layer == INDEX_NONE) || !Owner->HasAnimation())
		{
			CustomizeAnimation(DetailBuilder, bEditingAsset);
			return;
		}

		// A group: its row in LayerGroups.
		if (Selection.Layer == INDEX_NONE)
		{
			const TSharedRef<IPropertyHandle> GroupsHandle = DetailBuilder.GetProperty(IWidgetAnimPanelSource::GetGroupsPropertyName(), PropertyClass);
			const TSharedPtr<IPropertyHandle> GroupHandle = GroupsHandle->IsValidHandle() ? GroupsHandle->GetChildHandle(static_cast<uint32>(Selection.Group)) : nullptr;
			if (GroupHandle.IsValid() && GroupHandle->IsValidHandle())
			{
				CustomizeGroup(DetailBuilder, GroupHandle.ToSharedRef(), *Source);
			}
			else
			{
				CustomizeAnimation(DetailBuilder, bEditingAsset);
			}
			return;
		}

		// A layer or a key: its row in the single list of Layers.
		const TSharedRef<IPropertyHandle> LayersHandle = DetailBuilder.GetProperty(IWidgetAnimPanelSource::GetLayersPropertyName(), PropertyClass);
		const TSharedPtr<IPropertyHandle> LayerHandle = LayersHandle->IsValidHandle() ? LayersHandle->GetChildHandle(static_cast<uint32>(Selection.Layer)) : nullptr;
		if (!LayerHandle.IsValid() || !LayerHandle->IsValidHandle())
		{
			CustomizeAnimation(DetailBuilder, bEditingAsset);
			return;
		}

		const TArray<FWidgetAnimLayer>* Layers = Owner->GetLayers();
		const bool bIsTransform = Layers
			&& Layers->IsValidIndex(Selection.Layer)
			&& (*Layers)[Selection.Layer].AnimType == EWidgetAnimType::Transform;

		if (Selection.Key != INDEX_NONE && bIsTransform)
		{
			CustomizeKey(DetailBuilder, LayerHandle.ToSharedRef(), Selection.Key);
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

	/** Nothing selected: the animation itself. A builder's name, folder and preview asset; an asset's description. */
	static void CustomizeAnimation(IDetailLayoutBuilder& DetailBuilder, bool bEditingAsset)
	{
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
				.ToolTipText(LOCTEXT("PreviewTargetTooltip", "The widget this group animates while it is built here. Each use of the animation picks its own Target Widget per group, starting from this one."))
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
		if (Owner && Groups && Groups->Num() > 0)
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
					.ToolTipText(LOCTEXT("LayerGroupTooltip", "Which group - which widget - this track moves. Its place in time stays where it is."))
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

		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform);
		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);

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

			// Everything else hides itself by type through its own EditCondition, wherever it is added.
			Category.AddProperty(Child);
		}
	}

	/** One key: when it happens (read-only - drag it on the track), its pose, and its shape. */
	static void CustomizeKey(IDetailLayoutBuilder& DetailBuilder, const TSharedRef<IPropertyHandle>& LayerHandle, int32 KeyIndex)
	{
		const TSharedPtr<IPropertyHandle> TransformHandle = LayerHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform));
		const TSharedPtr<IPropertyHandle> KeysHandle = TransformHandle.IsValid() ? TransformHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys)) : nullptr;
		const TSharedPtr<IPropertyHandle> KeyHandle = KeysHandle.IsValid() ? KeysHandle->GetChildHandle(static_cast<uint32>(KeyIndex)) : nullptr;

		if (!KeyHandle.IsValid() || !KeyHandle->IsValidHandle())
		{
			return;
		}

		IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("WidgetAnimSelection_Key", LOCTEXT("KeyCategory", "Key"));
		AddChildren(Category, KeyHandle.ToSharedRef(), [](FName) { return true; }, /*bAutoExpand=*/true);
	}

	TWeakPtr<SWidgetAnimTimeline> Panel;
	TSharedPtr<TArray<TSharedPtr<FString>>> TargetOptions;
	TSharedPtr<TArray<TSharedPtr<FString>>> GroupOptions;
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
		for (const TWeakObjectPtr<UObject>& Object : Objects)
		{
			if (const UWidgetAnimLayerEditProxy* Proxy = Cast<UWidgetAnimLayerEditProxy>(Object.Get()))
			{
				Types.Add(Proxy->Layer.AnimType);
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
						? LOCTEXT("MultipleSameType", "{0} tracks selected. A setting they don't agree on reads Multiple Values; changing it sets it on all of them.")
						: LOCTEXT("MultipleMixedTypes", "{0} tracks of different types selected. Only the settings they all have are shown; one they don't agree on reads Multiple Values."),
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
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Easing),
			GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, CustomCurve),
		};
		const FName DurationName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Duration);
		const FName TransformName = GET_MEMBER_NAME_CHECKED(FWidgetAnimLayer, Transform);
		const FName KeysName = GET_MEMBER_NAME_CHECKED(FWidgetTransformAnim, Keys);

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

			const bool bShared = Algo::Find(SharedNames, Name) != nullptr;
			const bool bShow = bShared
				|| (Name == DurationName && !bAnyTransform)
				// A type's own settings, when every layer is that type. Each hides itself on other types by its
				// EditCondition, which reads the same on every proxy here.
				|| (bSameType && Name != DurationName);

			if (bShow)
			{
				Category.AddProperty(Child);
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

		// The panel takes the keyboard, so Delete, Ctrl+A and the rest work on what was just picked here.
		const FReply Reply = FReply::Handled().SetUserFocus(Owner.ToSharedRef(), EFocusCause::Mouse);
		const bool bRight = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
		bCollapseSelectionOnRelease = false;

		if (IsGroupRow())
		{
			Owner->SelectGroup(Group);
			Owner->RevealGroup(Group);
			if (bRight)
			{
				PushMenu(MouseEvent, Owner->MakeGroupContextMenu(Group));
			}
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

		if (bRight)
		{
			PushMenu(MouseEvent, Owner->MakeLayerContextMenu(Layer, INDEX_NONE, TOptional<float>()));
		}
		return Reply;
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<SWidgetAnimTimeline> Owner = Panel.Pin();
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
};

// =====================================================================================================
// Tooltip content - what a track or a key is doing, laid out in the panel's own colours
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
	 * A long right arrow, drawn rather than typed: a line and an arrowhead, centred on the text beside it. The
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
	 * The whole tooltip: a bar down the left in the track's colour, a title with what it belongs to beside it and
	 * a time on the right, then the body.
	 */
	TSharedRef<SWidget> Frame(const FLinearColor& Accent, const FString& Title, const FString& Subtitle, const FString& Time, const TSharedRef<SVerticalBox>& Body)
	{
		return SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(4.f, 6.f, 0.f, 6.f))
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
			.Padding(FMargin(9.f, 8.f, 12.f, 9.f))
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
}

SWidgetAnimTimeline::SWidgetAnimTimeline() = default;
SWidgetAnimTimeline::~SWidgetAnimTimeline() = default;

void SWidgetAnimTimeline::Construct(const FArguments& InArgs)
{
	using namespace WidgetAnimTimeline;

	Source = InArgs._Source;
	OnDockInLayout = InArgs._OnDockInLayout;

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
	const FOnGetDetailCustomizationInstance MakeLayout = FOnGetDetailCustomizationInstance::CreateLambda([WeakThisPtr]() -> TSharedRef<IDetailCustomization>
	{
		return MakeShared<FWidgetAnimSelectionDetails>(WeakThisPtr);
	});
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimBuilder::StaticClass(), MakeLayout);
	DetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimPresetAsset::StaticClass(), MakeLayout);

	// Several layers selected: their proxies, shown together so the view merges what they share. See
	// FWidgetAnimMultiLayerDetails.
	MultiDetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
	MultiDetailsView->RegisterInstancedCustomPropertyLayout(UWidgetAnimLayerEditProxy::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([WeakThisPtr]() -> TSharedRef<IDetailCustomization>
		{
			return MakeShared<FWidgetAnimMultiLayerDetails>(WeakThisPtr);
		}));

	// Read-only - the panel locked, or a locked track selected - greys every field out, rather than hiding it:
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
			.Value(0.7f)
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
						.Value(0.27f)
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

							// Pinned along the bottom, whatever the list scrolls to.
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								MakeAnimationPropertiesButton()
							]
						]

						+ SSplitter::Slot()
						.Value(0.73f)
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
						.Text(LOCTEXT("NothingToEdit", "Pick the Widget Animation asset to edit above, or switch to New Animation."))
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
			.Value(0.3f)
			[
				SNew(SVerticalBox)

				// The Selection panel's header, on the header colour and as tall as the strips naming the list and
				// the Track Editor beside it.
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBorder)
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

				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([this]() { return IsMultiSelection() ? 1 : 0; })

					+ SWidgetSwitcher::Slot()
					[
						DetailsView.ToSharedRef()
					]

					+ SWidgetSwitcher::Slot()
					[
						MultiDetailsView.ToSharedRef()
					]
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
		.ToolTipText(LOCTEXT("ModeTooltip", "New Animation builds a new one, saved with Save as Animation Asset. Edit Existing Asset edits a Widget Animation asset in place, saved with Update Animation Asset."))
		.OnGetMenuContent_Lambda([SwitchMode]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			Menu.AddMenuEntry(
				LOCTEXT("NewAnimation", "New Animation"),
				LOCTEXT("NewAnimationTooltip", "Build a new animation, and save it as a new Widget Animation asset when it is ready."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([SwitchMode]() { SwitchMode(EMode::NewAnimation); })));
			Menu.AddMenuEntry(
				LOCTEXT("EditExistingAsset", "Edit Existing Asset"),
				LOCTEXT("EditExistingAssetTooltip", "Pick a Widget Animation asset and edit it here, against this widget's own widgets."),
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
		.ToolTipText_Lambda([this, IsEditingExisting]()
		{
			if (IsEditingExisting())
			{
				return LOCTEXT("UpdateTooltip", "Save the changes made to this Widget Animation asset. Every widget using it plays them.");
			}
			return Source.IsValid() && Source->IsBuilder()
				? LOCTEXT("SaveAsBuilderTooltip", "Save these tracks as a new WA_ Widget Animation asset, named by Animation Name in Save Folder, and go on editing it.")
				: LOCTEXT("SaveAsTooltip", "Save this animation as a new WA_ Widget Animation asset beside this Widget Blueprint, and go on editing it.");
		})
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
		.ToolTipText(LOCTEXT("DockTooltip", "Open this panel as a tab in the editor layout."))
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

	// The lock: the whole animation read-only - a preview. Everything that edits it is greyed out; playing,
	// scrubbing, selecting and looking at what is selected all still work.
	Header->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(4.f, 2.f))
		.IsEnabled_Lambda([this]() { return HasAnimation(); })
		.ToolTipText_Lambda([this]()
		{
			return IsReadOnly()
				? LOCTEXT("UnlockTooltip", "Unlock the animation, so it can be edited again.")
				: LOCTEXT("LockTooltip", "Lock the animation so that it is read only: a preview, with everything that would edit it greyed out.");
		})
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
		.ToolTipText(LOCTEXT("AddTooltip", "Add a Layer Group - a widget for the animation to move - a track, or an existing Widget Animation's tracks."))
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
		.ToolTipText(LOCTEXT("AnimationPropertiesTooltip", "Select the animation itself, to see its own settings in the Selection panel."))
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

	// Fit: the whole animation back in view - the F key, or a double-click on the ruler, does the same.
	return MakeSectionHeader(LOCTEXT("TrackEditorHeader", "Track Editor"),
		SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "SimpleButton")
		.ContentPadding(FMargin(6.f, 1.f))
		.ToolTipText(LOCTEXT("FitTooltip", "Fit the whole animation into view (F). Double-click anywhere to move the playhead there; right-drag pans along the timeline and down the lines; the wheel zooms, Shift + wheel scrolls the lines."))
		.OnClicked_Lambda([this]()
		{
			if (Tracks.IsValid())
			{
				Tracks->FitToAnimation();
			}
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("Fit", "Fit"))
		]);
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
		return MakePillToggle(Label, FText::Format(LOCTEXT("SegmentTooltip", "{0}: {1}"), Label, ToolTip),
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
								LOCTEXT("LoopTooltip", "Play the animation looped, as an entry with Loop set would, for as long as it is left playing."),
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
								LOCTEXT("PingPongTooltip", "Play back to the start between loops instead of snapping there. Needs Loop."),
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
								LOCTEXT("InvertTooltip", "Play the animation from its last frame to its first, as an entry with Invert Animation set would. Flipping it keeps the playhead where it is."),
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
							.Text(LOCTEXT("SpeedTooltip", "How fast the preview plays, as an entry's own Playback Speed would play it - delays included. Only the preview: the animation's own Animation Speed Multiplier is in Animation Properties."))
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

			// Middle: the transport, centred under the tracks as in the native editor, and a size up from the
			// rest of the panel's icons - they are the controls used most.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Backward_End", "Sequencer.Transport.GoToFront.Normal" }, LOCTEXT("ToStart", "|<"), LOCTEXT("ToStartTooltip", "Go to the start"),
						FOnClicked::CreateLambda([this]() { ScrubToSeconds(0.f); return FReply::Handled(); }), TransportIconSize)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Backward_Step", "Sequencer.Transport.StepBackwards.Normal" }, LOCTEXT("PrevKey", "<"), LOCTEXT("PrevKeyTooltip", "Previous key or track edge (,)"),
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
							? LOCTEXT("PauseTooltip", "Pause (Space)")
							: LOCTEXT("PlayTooltip", "Play the animation from the top, or pause it (Space)");
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
					MakeIconButton({ "Animation.Forward_Step", "Sequencer.Transport.StepForward.Normal" }, LOCTEXT("NextKey", ">"), LOCTEXT("NextKeyTooltip", "Next key or track edge (.)"),
						FOnClicked::CreateLambda([this]() { StepToMarker(1); return FReply::Handled(); }), TransportIconSize)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					MakeIconButton({ "Animation.Forward_End", "Sequencer.Transport.GoToEnd.Normal" }, LOCTEXT("ToEnd", ">|"), LOCTEXT("ToEndTooltip", "Go to the end"),
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
								LOCTEXT("TrackSnappingTooltip", "Snapping: A dragged track or key snaps to the start and end of other tracks, the start and end of the animation, and the playhead. The distance is in Editor Preferences > Widget Animations Editor."),
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
								LOCTEXT("LinkedEdgesTooltip", "Linked Edges: Resizing a track also resizes the tracks below it whose start or end lines up exactly with the edge being dragged - so a sequence stays joined, and tracks that end together keep ending together."),
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
	return Layer.AnimType == EWidgetAnimType::Transform && Layer.Transform.Curve.ExternalCurve == nullptr;
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

	// Placed exactly as playback places them - one sequence across every group, Starts and Delay - so the
	// bars are where the layers really run. In played seconds: the animation's Speed Multiplier applied.
	TArray<FWidgetAnimSettings> Placed;
	UWidgetAnimLibrary::BuildLayerSettings(*Layers, Placed);
	const float TimeScale = GetTimeScale();

	OutSpans.Reserve(Placed.Num());
	for (int32 Index = 0; Index < Placed.Num() && Layers->IsValidIndex(Index); ++Index)
	{
		OutSpans.Add(FVector2f(FMath::Max(Placed[Index].Delay, 0.f), (*Layers)[Index].GetRunLength()) * TimeScale);
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

	float End = 0.f;
	for (const FVector2f& Span : Spans)
	{
		End = FMath::Max(End, Span.X + Span.Y);
	}
	return End;
}

void SWidgetAnimTimeline::GetKeyTimes(int32 LayerIndex, TArray<float>& OutTimes) const
{
	OutTimes.Reset();

	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!Layers || !Layers->IsValidIndex(LayerIndex) || (*Layers)[LayerIndex].AnimType != EWidgetAnimType::Transform)
	{
		return;
	}

	// In played seconds, as the spans are.
	const float TimeScale = GetTimeScale();
	if (const FRichCurve* Curve = (*Layers)[LayerIndex].Transform.GetActiveCurve())
	{
		for (const FRichCurveKey& Key : Curve->GetConstRefOfKeys())
		{
			OutTimes.Add(Key.Time * TimeScale);
		}
	}
}

float SWidgetAnimTimeline::GetPlayheadSeconds() const
{
	// The preview reports a fraction of one pass, which is the same fraction at any playback speed.
	return Source.IsValid() ? Source->GetPlayheadAlpha() * GetPassSeconds() : 0.f;
}

FText SWidgetAnimTimeline::GetGroupName(int32 GroupIndex) const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
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
	SelectedLayers.Reset();
	SelectionAnchor = INDEX_NONE;
	RefreshDetails();
}

void SWidgetAnimTimeline::SelectLayer(int32 LayerIndex)
{
	FSelection NewSelection;
	NewSelection.Layer = LayerIndex;
	Selection = Validate(NewSelection);

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
	TArray<int32> NewLayers = bToggle ? SelectedLayers : TArray<int32>();

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
		Source->FocusKey(LayerIndex, KeyIndex);
	}
}

void SWidgetAnimTimeline::ClearSelection()
{
	Selection = FSelection();
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
		Layer = Proxy->Layer;
		Layer.LayerGroup = LayerGroup;
		Layer.bLockedInEditor = bLocked;
		Layer.EditorLine = EditorLine;
		Layer.bHiddenInEditor = bHidden;
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
		return FText::Format(LOCTEXT("SelectionAnimation", "Selection: Animation{0}"), IsReadOnly() ? Suffix : FText::GetEmpty());
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

	const FText TypeName = GetAnimTypeName((*Layers)[Selection.Layer].AnimType);

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
	if (Source.IsValid())
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

	// The group's colour, down the left of its row and every one of its layers' rows.
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
			.ToolTipText(LOCTEXT("ListGroupRowTooltip", "Click to select the group - its name and target widget - and bring its line into view. Double-click to fold its tracks away here."))
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
							.ToolTipText(LOCTEXT("ListFoldTooltip", "Show or hide this group's tracks in the list. They still play."))
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
							.ToolTipText(LOCTEXT("GroupTargetTooltip", "The widget this group animates here. Change it by selecting the group."))
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
				.ToolTipText(LOCTEXT("ListLayerRowTooltip", "Click to select this track and bring it into view - Shift or Ctrl / Cmd to select several. Right-click for its menu."))
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

							+ SHorizontalBox::Slot()
							.FillWidth(1.f)
							.VAlign(VAlign_Center)
							.Padding(FMargin(18.f, 0.f, 6.f, 0.f))
							[
								SNew(STextBlock)
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.ColorAndOpacity_Lambda([IsWashedOut]()
								{
									return IsWashedOut() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
								})
								.Text_Lambda([this, LayerIndex]()
								{
									const TArray<FWidgetAnimLayer>* Current = GetLayers();
									return Current && Current->IsValidIndex(LayerIndex)
										? GetAnimTypeName((*Current)[LayerIndex].AnimType)
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
								.ToolTipText(LOCTEXT("ShowHiddenTrackTooltip", "This track is hidden: the previews play without it. Click to show it again."))
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
								.ToolTipText(LOCTEXT("UnlockTrackTooltip", "This track is locked: it can be selected, but not moved, resized or edited. Click to unlock it."))
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
	TrackLines.Reset();
	LayerToLine.Reset();
	LayerLineOrdinals.Reset();
	GroupLineCounts.Reset();
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
		constexpr float Tolerance = 0.0005f;
		const FVector2f SpanA = TrackSpans[A];
		const FVector2f SpanB = TrackSpans[B];
		return SpanA.X < SpanB.X + SpanB.Y - Tolerance && SpanB.X < SpanA.X + SpanA.Y - Tolerance;
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

		FTrackLine& GroupLine = TrackLines.AddDefaulted_GetRef();
		GroupLine.Kind = FTrackLine::EKind::Group;
		GroupLine.Group = GroupIndex;

		for (int32 Ordinal = 0; Ordinal < GroupLines.Num(); ++Ordinal)
		{
			GroupLines[Ordinal].StableSort([&StartOf](int32 A, int32 B) { return StartOf(A) < StartOf(B); });
			for (const int32 LayerIndex : GroupLines[Ordinal])
			{
				LayerLineOrdinals[LayerIndex] = Ordinal;
			}

			if (TrackCollapsedGroups.Contains(GroupIndex))
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

	// Delete or Backspace: the selected key if there is one, else every selected layer.
	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		if (Selection.Key != INDEX_NONE && CanEditKeys(Selection.Layer))
		{
			DeleteKey(Selection.Layer, Selection.Key);
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

	TArray<FWidgetAnimSettings> Placed;
	UWidgetAnimLibrary::BuildLayerSettings(*Layers, Placed);
	for (const FWidgetAnimSettings& Settings : Placed)
	{
		OutStarts.Add(FMath::Max(Settings.Delay, 0.f));
	}
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

		PreviousStart = LayerStart;
		PreviousEnd = LayerStart + Layer.GetRunLength();
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

void SWidgetAnimTimeline::AddLayer(int32 GroupIndex, EWidgetAnimType AnimType)
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

	// A new layer follows the one above it, so adding layers builds a sequence rather than a pile.
	if (InsertAt > 0)
	{
		NewLayer.Start = EWidgetAnimLayerStart::AfterPrevious;
	}

	// A line of its own - EditorLine is left unset - so it can be seen, and dragged up beside another from there.
	// Every track already there stays where it is in time, the one below the new one included.
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
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
	}

	// Told as an edit to the layers, which is what has the previews rebuilt without - or with - them.
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
	TArray<int32> GroupMap;
	const int32 SourceGroups = FMath::Max(Animation->LayerGroups.Num(), 1);
	if (SourceGroups == 1)
	{
		if (Groups.Num() == 0)
		{
			Groups.AddDefaulted();
		}
		GroupMap.Add(GetDefaultGroup(GroupIndex));
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
	const int32 LineOffset = SourceGroups == 1 ? GetGroupLineCount(GroupMap[0]) : 0;
	TArray<float> OldStarts;
	GetAuthoredStarts(OldStarts);
	for (int32 Index = 0; Index < Animation->Layers.Num(); ++Index)
	{
		FWidgetAnimLayer Copy = Animation->Layers[Index];
		const int32 SourceGroup = UWidgetAnimLibrary::GetLayerGroupIndex(Copy, SourceGroups);
		Copy.LayerGroup = GroupMap.IsValidIndex(SourceGroup) ? GroupMap[SourceGroup] : GroupMap[0];
		Copy.EditorLine = Copy.EditorLine >= 0 ? Copy.EditorLine + LineOffset : INDEX_NONE;
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

	FRichCurve* Curve = (*Source->GetLayers())[LayerIndex].Transform.Curve.GetRichCurve();
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
	if (!CanEditKeys(LayerIndex))
	{
		return;
	}

	FRichCurve* Curve = (*Source->GetLayers())[LayerIndex].Transform.Curve.GetRichCurve();
	if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(KeyIndex) || Curve->GetNumKeys() <= 2)
	{
		// Two keys is the least a Transform layer can move between.
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

	DragLayer = LayerIndex;
	DragKey = KeyIndex;

	// Where the layer starts, which a key drag never changes, and what the key can snap to - worked out now,
	// before anything moves, so nothing the drag itself shifts is snapped to. The key's own layer is left
	// out: its other keys are what it is being moved between.
	TArray<FVector2f> Spans;
	GetLayerSpans(Spans);
	KeyDragLayerStart = Spans.IsValidIndex(LayerIndex) ? Spans[LayerIndex].X : 0.f;
	GatherSnapTargets({ LayerIndex }, /*bIncludeKeys=*/true, KeySnapTargets);
	SnapIndicator.Reset();

	// One undo step for the whole drag, however many frames it moves through.
	KeyDragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveKeyTransaction", "Move Transform Key"));
	BeginEdit();
}

void SWidgetAnimTimeline::UpdateKeyDrag(float LocalSeconds, float SnapThresholdSeconds)
{
	if (!IsDraggingKey() || !CanEditKeys(DragLayer))
	{
		return;
	}

	FRichCurve* Curve = (*Source->GetLayers())[DragLayer].Transform.Curve.GetRichCurve();
	if (!Curve || !Curve->GetConstRefOfKeys().IsValidIndex(DragKey))
	{
		return;
	}

	const TArray<FRichCurveKey>& Keys = Curve->GetConstRefOfKeys();
	const float TimeScale = FMath::Max(GetTimeScale(), KINDA_SMALL_NUMBER);

	// Snapped first, then kept between its neighbours - a snap that would pass one is not taken. All in played
	// seconds, as the timeline shows them; the curve's own authored time is worked out at the end.
	float Wanted = RoundSeconds(LocalSeconds);
	TOptional<float> SnappedTo;
	float Adjust = 0.f;
	float Target = 0.f;
	if (SnapThresholdSeconds > 0.f && FindSnap(KeySnapTargets, { KeyDragLayerStart + LocalSeconds }, SnapThresholdSeconds, Adjust, Target))
	{
		Wanted = LocalSeconds + Adjust;
		SnappedTo = Target;
	}

	// Kept between its neighbours. The curve re-sorts keys by time, and the pose values sit alongside the
	// keys by position - a key dragged past its neighbour would carry the neighbour's pose, not its own.
	const float Lower = DragKey > 0 ? Keys[DragKey - 1].Time * TimeScale + WidgetAnimTimeline::MinKeySpacing : 0.f;
	const float Upper = DragKey + 1 < Keys.Num() ? Keys[DragKey + 1].Time * TimeScale - WidgetAnimTimeline::MinKeySpacing : TNumericLimits<float>::Max();
	const float NewTime = FMath::Clamp(Wanted, Lower, FMath::Max(Lower, Upper));

	SnapIndicator = SnappedTo.IsSet() && FMath::IsNearlyEqual(NewTime, Wanted, WidgetAnimTimeline::MinKeySpacing) ? SnappedTo : TOptional<float>();

	const float NewCurveTime = NewTime / TimeScale;
	const FKeyHandle Handle = Curve->FindKey(Keys[DragKey].Time, KINDA_SMALL_NUMBER);
	if (Handle == FKeyHandle::Invalid() || FMath::IsNearlyEqual(Keys[DragKey].Time, NewCurveTime))
	{
		return;
	}

	Curve->SetKeyTime(Handle, NewCurveTime);

	// Interactive, as a Details panel drag is: the preview follows live and keeps its place - on the key
	// being moved, so what it looks like at its new moment is on screen.
	NotifyGroupsChanged(EPropertyChangeType::Interactive);
	Source->FocusKey(DragLayer, DragKey);

	// Moving the last key changes how long the layer runs, which can change what fits on its line.
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
		else if (!FMath::IsNearlyEqual(Layer.Duration, Length, 1.e-5f))
		{
			Layer.Duration = Length;
		}

		const float Anchor = Layer.Start == EWidgetAnimLayerStart::AfterPrevious ? PreviousEnd : PreviousStart;
		const float Delay = Start - Anchor;
		if (!FMath::IsNearlyEqual(Layer.Delay, Delay, 1.e-5f))
		{
			Layer.Delay = Delay;
		}

		PreviousStart = Start;
		PreviousEnd = Start + Layer.GetRunLength();
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
		LOCTEXT("InsertAnimationTooltip", "Unwrap another Widget Animation's tracks into this one, where a new track would go - after the selected track, or at the end - to build on it."),
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
		}));
}

void SWidgetAnimTimeline::AddTrackSubMenu(FMenuBuilder& Menu, int32 GroupIndex)
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const FText Label = Groups && Groups->IsValidIndex(GroupIndex)
		? FText::Format(LOCTEXT("AddTrackToGroup", "Add Animation Track to {0}"), GetGroupName(GroupIndex))
		: LOCTEXT("AddTrack", "Add Animation Track");

	Menu.AddSubMenu(
		Label,
		LOCTEXT("AddTrackToGroupTooltip", "A new track in this group, after the selected track - or at the end - on a line of its own."),
		FNewMenuDelegate::CreateLambda([this, GroupIndex](FMenuBuilder& SubMenu)
		{
			WidgetAnimTimeline::ForEachAnimType([this, &SubMenu, GroupIndex](EWidgetAnimType AnimType)
			{
				SubMenu.AddMenuEntry(
					GetAnimTypeName(AnimType),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, GroupIndex, AnimType]() { AddLayer(GroupIndex, AnimType); })));
			});
		}),
		/*bInOpenSubMenuOnClick=*/false,
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"));
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeAddMenu(int32 GroupIndex)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	if (!CanEdit())
	{
		Menu.AddWidget(SNew(STextBlock).Text(IsReadOnly()
			? LOCTEXT("ReadOnlyAdd", "The animation is locked. Unlock it to add to it.")
			: LOCTEXT("ReadOnlyAsset", "Pick the Widget Animation asset to edit, or switch to New Animation.")), FText::GetEmpty());
		return Menu.MakeWidget();
	}

	Menu.BeginSection("Group", LOCTEXT("GroupSection", "Layer Group"));
	Menu.AddMenuEntry(
		LOCTEXT("AddGroup", "Add Layer Group"),
		LOCTEXT("AddGroupTooltip", "A new group: another widget for this animation to move."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));
	Menu.EndSection();

	// Every group, each with every track type in a menu of its own - so a track can go in any group from here,
	// not just the one selected.
	Menu.BeginSection("Tracks", LOCTEXT("TracksSection", "Tracks"));
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (Groups && Groups->Num() > 0)
	{
		for (int32 Group = 0; Group < Groups->Num(); ++Group)
		{
			AddTrackSubMenu(Menu, Group);
		}
	}
	else
	{
		AddTrackSubMenu(Menu, INDEX_NONE);
	}
	Menu.EndSection();

	Menu.BeginSection("Existing", LOCTEXT("ExistingSection", "Build On"));
	AddInsertAnimationEntry(Menu, GetDefaultGroup(GroupIndex));
	Menu.EndSection();

	return Menu.MakeWidget();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeGroupContextMenu(int32 GroupIndex)
{
	if (!CanEdit())
	{
		return MakeAddMenu(GroupIndex);
	}

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	Menu.BeginSection("Tracks", LOCTEXT("GroupTracksSection", "Tracks"));
	AddTrackSubMenu(Menu, GroupIndex);
	AddInsertAnimationEntry(Menu, GroupIndex);
	Menu.EndSection();

	Menu.BeginSection("Group", LOCTEXT("GroupSectionContext", "Layer Group"));

	Menu.AddMenuEntry(
		IsTrackGroupCollapsed(GroupIndex) ? LOCTEXT("Expand", "Show Its Tracks") : LOCTEXT("Collapse", "Hide Its Tracks"),
		LOCTEXT("CollapseTooltip", "Fold this group's tracks away in the Track Editor, leaving its own line. They still play."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { ToggleTrackGroupCollapsed(GroupIndex); })));

	Menu.AddMenuEntry(
		LOCTEXT("DuplicateGroup", "Duplicate Group"),
		LOCTEXT("DuplicateGroupTooltip", "A copy of this group and its tracks, playing at the same moments - to point at another widget."),
		FSlateIcon(),
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
		LOCTEXT("DeleteGroupTooltip", "Delete this group and every track in it."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, GroupIndex]() { DeleteGroup(GroupIndex); })));

	Menu.AddMenuEntry(
		LOCTEXT("AddGroupContext", "Add Layer Group"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));

	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeLayerContextMenu(int32 LayerIndex, int32 KeyIndex, TOptional<float> LocalSeconds)
{
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const int32 GroupIndex = GetLayerGroup(LayerIndex);
	const TArray<int32> ActedOn = GetActedOnLayers(LayerIndex);

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
				LOCTEXT("HideTrackTooltip", "Play the previews without this track - every other track stays where it is - to judge the rest on their own. A game still plays it."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Hidden"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersHidden(ActedOn, /*bHidden=*/true); })));
		}
		if (bAnyHidden)
		{
			Menu.AddMenuEntry(
				bSeveral ? LOCTEXT("ShowTracks", "Show Tracks") : LOCTEXT("ShowTrack", "Show Track"),
				LOCTEXT("ShowTrackTooltip", "Play this track in the previews again."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visible"),
				FUIAction(FExecuteAction::CreateLambda([this, ActedOn]() { SetLayersHidden(ActedOn, /*bHidden=*/false); })));
		}
	};

	if (!CanEdit())
	{
		Menu.BeginSection("View", LOCTEXT("ViewSection", "Preview"));
		AddHideEntries();
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	if (CanEditKeys(LayerIndex) && ActedOn.Num() == 1)
	{
		Menu.BeginSection("Keys", LOCTEXT("KeysSection", "Keys"));

		if (KeyIndex != INDEX_NONE)
		{
			Menu.AddMenuEntry(
				LOCTEXT("DeleteKey", "Delete Key"),
				LOCTEXT("DeleteKeyTooltip", "Delete this key. A Transform track keeps at least two."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateLambda([this, LayerIndex, KeyIndex]() { DeleteKey(LayerIndex, KeyIndex); }),
					FCanExecuteAction::CreateLambda([this, LayerIndex]()
					{
						TArray<float> KeyTimes;
						GetKeyTimes(LayerIndex, KeyTimes);
						return KeyTimes.Num() > 2;
					})));
		}
		else if (LocalSeconds.IsSet())
		{
			const float Seconds = LocalSeconds.GetValue();
			Menu.AddMenuEntry(
				LOCTEXT("AddKeyHere", "Add Key Here"),
				LOCTEXT("AddKeyHereTooltip", "Add a key at this moment, starting at the pose the animation already has there. Double-clicking the track does the same."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, Seconds]() { AddKey(LayerIndex, Seconds); })));
		}

		Menu.EndSection();
	}

	// Several tracks selected, this one among them: what they can all do together.
	if (ActedOn.Num() > 1)
	{
		Menu.BeginSection("Selected", FText::Format(LOCTEXT("SelectedTracksSection", "{0} Selected Tracks"), FText::AsNumber(ActedOn.Num())));

		AddHideEntries();

		if (bAnyUnlocked)
		{
			Menu.AddMenuEntry(
				LOCTEXT("LockSelected", "Lock Tracks"),
				LOCTEXT("LockSelectedTooltip", "Lock these tracks: they can still be selected and looked at, but not moved, resized or edited."),
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
			LOCTEXT("DeleteSelectedTooltip", "Delete every selected track that isn't locked."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { DeleteSelectedLayers(); }),
				FCanExecuteAction::CreateLambda([bAnyUnlocked]() { return bAnyUnlocked; })));

		Menu.EndSection();

		Menu.BeginSection("Add", LOCTEXT("AddSectionInContext", "Add"));
		AddTrackSubMenu(Menu, GroupIndex);
		Menu.EndSection();
		return Menu.MakeWidget();
	}

	const bool bLocked = IsLayerLocked(LayerIndex);

	Menu.BeginSection("Track", LOCTEXT("TrackSection", "Track"));

	AddHideEntries();

	// Locking: the track stays selectable - its settings can be looked at - but nothing moves or edits it.
	Menu.AddMenuEntry(
		bLocked ? LOCTEXT("UnlockTrack", "Unlock Track") : LOCTEXT("LockTrack", "Lock Track"),
		bLocked
			? LOCTEXT("UnlockTrackMenuTooltip", "Let this track be moved, resized and edited again.")
			: LOCTEXT("LockTrackMenuTooltip", "Lock this track: it can still be selected and looked at, but not moved, resized or edited."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), bLocked ? "Icons.Unlock" : "Icons.Lock"),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, bLocked]() { SetLayersLocked({ LayerIndex }, !bLocked); })));

	Menu.AddMenuEntry(
		LOCTEXT("Duplicate", "Duplicate Track"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this, LayerIndex]() { DuplicateLayer(LayerIndex); })));

	// Into another group - another widget - by name.
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	if (Groups && Groups->Num() > 1 && CanEditLayer(LayerIndex))
	{
		Menu.AddSubMenu(
			LOCTEXT("MoveToGroup", "Move to Group"),
			LOCTEXT("MoveToGroupTooltip", "Make this track move another group's widget. Its place in time stays where it is."),
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
			}));
	}

	Menu.AddMenuEntry(
		LOCTEXT("DeleteTrack", "Delete Track"),
		FText::GetEmpty(),
		// Trash can
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(
			FExecuteAction::CreateLambda([this, LayerIndex]() { DeleteLayer(LayerIndex); }),
			FCanExecuteAction::CreateLambda([this, LayerIndex]() { return CanEditLayer(LayerIndex); })));

	Menu.EndSection();

	Menu.BeginSection("Add", LOCTEXT("AddSectionInContext", "Add"));
	AddTrackSubMenu(Menu, GroupIndex);
	AddInsertAnimationEntry(Menu, GroupIndex);
	Menu.AddMenuEntry(
		LOCTEXT("AddGroupLayerContext", "Add Layer Group"),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { AddGroup(); })));
	Menu.EndSection();

	return Menu.MakeWidget();
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

FLinearColor SWidgetAnimTimeline::GetLockedColor(const FLinearColor& Color)
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;

	// Towards grey, then darker: still recognisably its type, plainly not something to grab.
	const float Grey = Color.GetLuminance();
	FLinearColor Washed = FMath::Lerp(Color, FLinearColor(Grey, Grey, Grey, Color.A), Palette::LockedDesaturate) * Palette::LockedBrightness;
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

	// Round the hue circle by the golden angle: each new group lands as far as possible from the ones before
	// it, however many there are, without a list running out. Muted - low saturation and value - so groups
	// read as a quiet cue beside the vivid layer colours rather than one more colour competing with them.
	constexpr float GoldenAngle = 0.381966f;
	const float Hue = FMath::Frac(Palette::GroupHueStart + GroupIndex * GoldenAngle);

	FLinearColor HSV(Hue * 360.f, Palette::GroupSaturation, Palette::GroupValue, 1.f);
	return HSV.HSVToLinearRGB();
}

FText SWidgetAnimTimeline::GetEasingName(const FWidgetAnimLayer& Layer)
{
	// A Custom easing is a Curve asset - Custom Curve - so it is shown by that asset's own name.
	if (Layer.Easing == EWidgetAnimEasing::Custom && Layer.CustomCurve)
	{
		return FText::FromString(Layer.CustomCurve->GetName());
	}

	const UEnum* EasingEnum = StaticEnum<EWidgetAnimEasing>();
	return EasingEnum ? EasingEnum->GetDisplayNameTextByValue(static_cast<int64>(Layer.Easing)) : FText::GetEmpty();
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

		auto Colour = [](const FLinearColor& Color, const FLinearColor& TextColor)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))[ Swatch(Color) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Text(TEXT("#") + Color.ToFColor(true).ToHex().Left(6), TextColor, ValueFont()) ];
		};

		Rows.Add(TEXT("Value"),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Colour(Layer.StartVectorValue, Palette::TooltipStartValue) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(7.f, 0.f))[ Arrow() ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[ Colour(Layer.EndVectorValue, Palette::TooltipEndValue) ]);
		break;
	}

	default:
		break;
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
			KeyNote = TEXT("Every key is at the identity pose - this track doesn't move anything yet.");
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

	if (IsLayerHidden(LayerIndex))
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(TEXT("Hidden - the previews play without it. A game still plays it."))
		];
	}

	if (IsLayerLocked(LayerIndex))
	{
		Body->AddSlot()
		.AutoHeight()
		[
			Note(TEXT("Locked - it can be selected and looked at, but not moved, resized or edited."))
		];
	}

	return Frame(
		IsReadOnly() || IsLayerLocked(LayerIndex) || IsLayerHidden(LayerIndex) ? GetLockedColor(GetAnimTypeColor(Layer.AnimType)) : GetAnimTypeColor(Layer.AnimType),
		GetAnimTypeName(Layer.AnimType).ToString(),
		GetGroupName(GetLayerGroup(LayerIndex)).ToString(),
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
			? Note(TEXT("Identity pose - nothing moved at this key."))
			: Pose.Widget()
	];

	const FString Title = FString::Printf(TEXT("Key %d of %d"), KeyIndex + 1, NumKeys);
	const FString Where = KeyIndex == 0 ? TEXT("First key") : KeyIndex == NumKeys - 1 ? TEXT("Last key") : GetAnimTypeName(Layer.AnimType).ToString();
	const FString Subtitle = FString::Printf(TEXT("%s  \u00B7  %s"), *Where, *GetGroupName(GetLayerGroup(LayerIndex)).ToString());

	return Frame(GetAnimTypeColor(Layer.AnimType), Title, Subtitle, Seconds(Spans[LayerIndex].X + LocalTime), Body);
}

// -----------------------------------------------------------------------------------------------------
// Value Watcher
// -----------------------------------------------------------------------------------------------------

FText SWidgetAnimTimeline::GetWatchedValueName(EWatchedValue Value)
{
	switch (Value)
	{
	case EWatchedValue::Opacity:      return LOCTEXT("WatchOpacity", "Opacity");
	case EWatchedValue::TranslationX: return LOCTEXT("WatchTranslationX", "Translation X");
	case EWatchedValue::TranslationY: return LOCTEXT("WatchTranslationY", "Translation Y");
	case EWatchedValue::ScaleX:       return LOCTEXT("WatchScaleX", "Scale X");
	case EWatchedValue::ScaleY:       return LOCTEXT("WatchScaleY", "Scale Y");
	case EWatchedValue::ShearX:       return LOCTEXT("WatchShearX", "Shear X");
	case EWatchedValue::ShearY:       return LOCTEXT("WatchShearY", "Shear Y");
	case EWatchedValue::Angle:        return LOCTEXT("WatchAngle", "Angle");
	default:                          return FText::GetEmpty();
	}
}

UWidget* SWidgetAnimTimeline::GetWatchedWidget() const
{
	if (!bWatching || !Source.IsValid())
	{
		return nullptr;
	}

	if (!WatchedTarget.IsNone())
	{
		return Source->ResolvePreviewTarget(WatchedTarget);
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

bool SWidgetAnimTimeline::ReadWatchedValue(float& OutValue) const
{
	const UWidget* Widget = GetWatchedWidget();
	if (!Widget)
	{
		return false;
	}

	const FWidgetTransform& Transform = Widget->GetRenderTransform();
	switch (WatchedValue)
	{
	case EWatchedValue::Opacity:      OutValue = Widget->GetRenderOpacity(); break;
	case EWatchedValue::TranslationX: OutValue = Transform.Translation.X; break;
	case EWatchedValue::TranslationY: OutValue = Transform.Translation.Y; break;
	case EWatchedValue::ScaleX:       OutValue = Transform.Scale.X; break;
	case EWatchedValue::ScaleY:       OutValue = Transform.Scale.Y; break;
	case EWatchedValue::ShearX:       OutValue = Transform.Shear.X; break;
	case EWatchedValue::ShearY:       OutValue = Transform.Shear.Y; break;
	case EWatchedValue::Angle:        OutValue = Transform.Angle; break;
	default:                          return false;
	}
	return true;
}

namespace
{
	/** Whether a track moves one of the values the Value Watcher reads - not just holds it somewhere. */
	bool DoesLayerChangeValue(const FWidgetAnimLayer& Layer, SWidgetAnimTimeline::EWatchedValue Value)
	{
		using EWatched = SWidgetAnimTimeline::EWatchedValue;

		// A Transform track changes a channel when its keys don't all agree on it.
		auto TransformVaries = [&Layer](TFunctionRef<float(const FWidgetTransform&)> Channel)
		{
			const TArray<FWidgetTransformKeyframe>& Keys = Layer.Transform.Keys;
			for (int32 Key = 1; Key < Keys.Num(); ++Key)
			{
				if (!FMath::IsNearlyEqual(Channel(Keys[Key].Transform), Channel(Keys[0].Transform), 1.e-4f))
				{
					return true;
				}
			}
			return false;
		};

		const bool bHorizontalSlide = Layer.SlideFromDirection == EWidgetSlideDirection::Left || Layer.SlideFromDirection == EWidgetSlideDirection::Right;

		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:
			return Value == EWatched::Opacity && !FMath::IsNearlyEqual(Layer.StartOpacity, Layer.EndOpacity);

		case EWidgetAnimType::Scale:
			return (Value == EWatched::ScaleX || Value == EWatched::ScaleY)
				&& (!FMath::IsNearlyEqual(Layer.StartScale, Layer.EndScale) || Layer.Overshoot > KINDA_SMALL_NUMBER);

		case EWidgetAnimType::Pulse:
			return (Value == EWatched::ScaleX || Value == EWatched::ScaleY) && Layer.PulseScaleAmount > KINDA_SMALL_NUMBER;

		case EWidgetAnimType::Slide:
			return Layer.SlideDistance > KINDA_SMALL_NUMBER
				&& ((Value == EWatched::TranslationX && bHorizontalSlide) || (Value == EWatched::TranslationY && !bHorizontalSlide));

		case EWidgetAnimType::Rotate:
			return Value == EWatched::Angle && !FMath::IsNearlyEqual(Layer.StartAngle, Layer.EndAngle);

		case EWidgetAnimType::Transform:
			switch (Value)
			{
			case EWatched::TranslationX: return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Translation.X); });
			case EWatched::TranslationY: return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Translation.Y); });
			case EWatched::ScaleX:       return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Scale.X); });
			case EWatched::ScaleY:       return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Scale.Y); });
			case EWatched::ShearX:       return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Shear.X); });
			case EWatched::ShearY:       return TransformVaries([](const FWidgetTransform& T) { return static_cast<float>(T.Shear.Y); });
			case EWatched::Angle:        return TransformVaries([](const FWidgetTransform& T) { return T.Angle; });
			default:                     return false;
			}

		default:
			return false;
		}
	}
}

bool SWidgetAnimTimeline::IsWatchedValueChanging() const
{
	const TArray<FWidgetAnimLayerGroup>* Groups = GetGroups();
	const TArray<FWidgetAnimLayer>* Layers = GetLayers();
	if (!bWatching || !Groups || !Layers)
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
		if (!Groups->IsValidIndex(GroupIndex) || (*Groups)[GroupIndex].PreviewTarget != WatchedTarget || Layer.bHiddenInEditor)
		{
			continue;
		}

		const FVector2f Span = Spans[LayerIndex];
		if (Now >= Span.X - Epsilon && Now < Span.X + Span.Y - Epsilon && DoesLayerChangeValue(Layer, WatchedValue))
		{
			return true;
		}
	}
	return false;
}

TSharedRef<SWidget> SWidgetAnimTimeline::MakeValueWatcher()
{
	using namespace WidgetAnimTimeline;

	auto Label = [](const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	};

	// Which widget: any in this Widget Blueprint's preview, as the Layer Group target dropdown lists them.
	const TSharedRef<SWidget> WidgetPicker = SNew(SComboButton)
		.ContentPadding(FMargin(4.f, 1.f))
		.ToolTipText(LOCTEXT("WatchWidgetTooltip", "The widget to watch - on the designer's preview, as the animation poses it."))
		.OnGetMenuContent_Lambda([this]()
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
						FUIAction(FExecuteAction::CreateLambda([this, Path]()
						{
							WatchedTarget = Path;
							bWatching = true;
						})));
				}
			}
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text_Lambda([this]()
			{
				if (!bWatching)
				{
					return LOCTEXT("WatchPickWidget", "Pick a widget...");
				}
				return WatchedTarget.IsNone() ? LOCTEXT("WatchThisWidget", "(this widget)") : FText::FromName(WatchedTarget);
			})
		];

	// Which value of it.
	const TSharedRef<SWidget> ValuePicker = SNew(SComboButton)
		.ContentPadding(FMargin(4.f, 1.f))
		.OnGetMenuContent_Lambda([this]()
		{
			FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
			for (uint8 Index = 0; Index <= static_cast<uint8>(EWatchedValue::Angle); ++Index)
			{
				const EWatchedValue Value = static_cast<EWatchedValue>(Index);
				Menu.AddMenuEntry(
					GetWatchedValueName(Value),
					FText::GetEmpty(),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([this, Value]() { WatchedValue = Value; })));
			}
			return Menu.MakeWidget();
		})
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.Text_Lambda([this]() { return GetWatchedValueName(WatchedValue); })
		];

	// The value itself, read off the preview every frame: plain while nothing moves it, red while a track is.
	const TSharedRef<SWidget> Readout = SNew(STextBlock)
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
		.ColorAndOpacity_Lambda([this]()
		{
			return IsWatchedValueChanging() ? FSlateColor(Palette::WatcherChanging) : FSlateColor::UseForeground();
		})
		.Text_Lambda([this]()
		{
			float Value = 0.f;
			if (!bWatching)
			{
				return LOCTEXT("WatchNothing", "-");
			}
			if (!ReadWatchedValue(Value))
			{
				return LOCTEXT("WatchNoPreview", "No preview of that widget");
			}
			return WatchedValue == EWatchedValue::Angle
				? FText::FromString(FString::Printf(TEXT("%.1f\u00B0"), Value))
				: FText::FromString(WatchedValue == EWatchedValue::Opacity
					? FString::Printf(TEXT("%.3f"), Value)
					: FString::Printf(TEXT("%.2f"), Value));
		});

	return SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeSectionHeader(LOCTEXT("ValueWatcherHeader", "Value Watcher"), SNullWidget::NullWidget)
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(10.f, 8.f))
			[
				SNew(SGridPanel)
				.FillColumn(1, 1.f)

				+ SGridPanel::Slot(0, 0).VAlign(VAlign_Center).Padding(FMargin(0.f, 2.f, 10.f, 2.f))[ Label(LOCTEXT("WatchWidgetLabel", "Widget")) ]
				+ SGridPanel::Slot(1, 0).VAlign(VAlign_Center).Padding(FMargin(0.f, 2.f))[ WidgetPicker ]

				+ SGridPanel::Slot(0, 1).VAlign(VAlign_Center).Padding(FMargin(0.f, 2.f, 10.f, 2.f))[ Label(LOCTEXT("WatchValueLabel", "Value")) ]
				+ SGridPanel::Slot(1, 1).VAlign(VAlign_Center).Padding(FMargin(0.f, 2.f))[ ValuePicker ]

				+ SGridPanel::Slot(0, 2).VAlign(VAlign_Center).Padding(FMargin(0.f, 6.f, 10.f, 0.f))[ Label(LOCTEXT("WatchNowLabel", "Now")) ]
				+ SGridPanel::Slot(1, 2).VAlign(VAlign_Center).Padding(FMargin(0.f, 6.f, 0.f, 0.f))[ Readout ]
			]
		];
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