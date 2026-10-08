// Copyright AR GameSystems 2026. All rights reserved.

#include "ChoiceButtons.h"
#include "StyledButtonTheme.h"

#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Types/WidgetActiveTimerDelegate.h"

#define LOCTEXT_NAMESPACE "ChoiceButtons"

namespace ChoiceButtonParts
{
	const FName Label(TEXT("Label"));
	const FName ValueText(TEXT("Value"));
	const FName PreviousArrow(TEXT("PreviousArrow"));
	const FName NextArrow(TEXT("NextArrow"));
	const FName PreviousBox(TEXT("PreviousBox"));
	const FName NextBox(TEXT("NextBox"));
	const FName SelectorBox(TEXT("SelectorBox"));
	const FName Segments(TEXT("Segments"));
	const FName Highlight(TEXT("Highlight"));
	const FName HighlightBox(TEXT("HighlightBox"));
}

// =====================================================================================================
// Choice Button
// =====================================================================================================

namespace ChoiceButtonPrivate
{
	/** How far a new value slides in from the side it came from, in Slate units, and how fast it settles. */
	constexpr float KickDistance = 10.f;
	constexpr float KickSettleSpeed = 18.f;

	/** An arrow at an end the choice can't pass. */
	constexpr float DimmedArrowOpacity = 0.3f;
}

UChoiceButton::UChoiceButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Options = { LOCTEXT("Low", "Low"), LOCTEXT("Medium", "Medium"), LOCTEXT("High", "High"), LOCTEXT("Ultra", "Ultra") };
	SelectedIndex = 2;
	FillColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::Line();
	LabelColor = StyledButtonTheme::TextDim();
	ValueColor = StyledButtonTheme::Text();
	ArrowColor = StyledButtonTheme::TextDim();
}

int32 UChoiceButton::GetNumberSteps() const
{
	if (Step <= 0.f || MaxValue <= MinValue)
	{
		return 0;
	}
	return FMath::Max(0, FMath::FloorToInt((MaxValue - MinValue) / Step + KINDA_SMALL_NUMBER));
}

FText UChoiceButton::GetDisplayText() const
{
	if (Mode == EChoiceButtonMode::Number)
	{
		FNumberFormattingOptions Format;
		Format.MinimumFractionalDigits = Decimals;
		Format.MaximumFractionalDigits = Decimals;
		return FText::Format(INVTEXT("{0}{1}"), FText::AsNumber(Value, &Format), Suffix);
	}
	return Options.IsValidIndex(SelectedIndex) ? Options[SelectedIndex] : FText::GetEmpty();
}

void UChoiceButton::StepBy(int32 Steps)
{
	if (Steps == 0)
	{
		return;
	}

	if (Mode == EChoiceButtonMode::Number)
	{
		const int32 Count = GetNumberSteps() + 1;
		const int32 Current = FMath::Clamp(FMath::RoundToInt((Value - MinValue) / FMath::Max(Step, KINDA_SMALL_NUMBER)), 0, Count - 1);
		int32 Next = Current + Steps;
		Next = bWrap ? ((Next % Count) + Count) % Count : FMath::Clamp(Next, 0, Count - 1);
		if (Next == Current)
		{
			return;
		}
		Value = FMath::Min(MinValue + Next * Step, MaxValue);
		UpdateDisplay(FMath::Sign(Steps));
		OnChoiceChanged.Broadcast(Next, Value);
		return;
	}

	const int32 Count = Options.Num();
	if (Count == 0)
	{
		return;
	}
	int32 Next = SelectedIndex + Steps;
	Next = bWrap ? ((Next % Count) + Count) % Count : FMath::Clamp(Next, 0, Count - 1);
	if (Next == SelectedIndex)
	{
		return;
	}
	SelectedIndex = Next;
	UpdateDisplay(FMath::Sign(Steps));
	OnChoiceChanged.Broadcast(SelectedIndex, static_cast<float>(SelectedIndex));
}

void UChoiceButton::SelectNext()
{
	StepBy(1);
}

void UChoiceButton::SelectPrevious()
{
	StepBy(-1);
}

void UChoiceButton::SetSelectedIndex(int32 InIndex, bool bBroadcast)
{
	const int32 Clamped = Options.Num() > 0 ? FMath::Clamp(InIndex, 0, Options.Num() - 1) : 0;
	const bool bChanged = Clamped != SelectedIndex;
	SelectedIndex = Clamped;
	UpdateDisplay(0);
	if (bChanged && bBroadcast)
	{
		OnChoiceChanged.Broadcast(SelectedIndex, static_cast<float>(SelectedIndex));
	}
}

void UChoiceButton::SetValue(float InValue, bool bBroadcast)
{
	const float Clamped = FMath::Clamp(InValue, MinValue, FMath::Max(MinValue, MaxValue));
	const bool bChanged = !FMath::IsNearlyEqual(Clamped, Value);
	Value = Clamped;
	UpdateDisplay(0);
	if (bChanged && bBroadcast)
	{
		OnChoiceChanged.Broadcast(FMath::RoundToInt((Value - MinValue) / FMath::Max(Step, KINDA_SMALL_NUMBER)), Value);
	}
}

void UChoiceButton::SetOptions(const TArray<FText>& InOptions, int32 InSelectedIndex)
{
	Options = InOptions;
	SelectedIndex = Options.Num() > 0 ? FMath::Clamp(InSelectedIndex, 0, Options.Num() - 1) : 0;
	UpdateDisplay(0);
}

bool UChoiceButton::HandleNavigation(EUINavigation Direction)
{
	if (!bLeftRightSteps || (Direction != EUINavigation::Left && Direction != EUINavigation::Right))
	{
		return false;
	}
	StepBy(Direction == EUINavigation::Right ? 1 : -1);
	return true;
}

void UChoiceButton::HandleChoiceClicked(bool bWillDestroyCard, bool bIsEnabling)
{
	// Left of the value's middle steps back; anywhere else - and a key or gamepad press, which has no place - forward.
	FVector2D Press;
	const UWidget* ShownValue = KickedValueText.IsValid() ? KickedValueText.Get() : GetPart(ChoiceButtonParts::ValueText);
	if (ShownValue && GetLastPressScreenPosition(Press))
	{
		const FGeometry& Geometry = ShownValue->GetCachedGeometry();
		if (Geometry.GetLocalSize().X > 0.0)
		{
			const FVector2D Middle = Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5));
			StepBy(Press.X < Middle.X ? -1 : 1);
			return;
		}
	}
	StepBy(GetLastPressPosition().X < 0.35 ? -1 : 1);
}

void UChoiceButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	SelectedIndex = Options.Num() > 0 ? FMath::Clamp(SelectedIndex, 0, Options.Num() - 1) : 0;
	MaxValue = FMath::Max(MaxValue, MinValue);
	Value = FMath::Clamp(Value, MinValue, MaxValue);

	if (!IsDesignTime())
	{
		OnContextClicked.AddUniqueDynamic(this, &UChoiceButton::HandleChoiceClicked);
	}
	UpdateDisplay(0);
}

void UChoiceButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	bIsKickTimerActive = false;
	KickOffset = 0.f;
}

void UChoiceButton::UpdateDisplay(int32 Direction)
{
	using namespace ChoiceButtonPrivate;

	UTextBlock* ShownValue = GetPart<UTextBlock>(ChoiceButtonParts::ValueText);
	KickedValueText = ShownValue;
	if (ShownValue)
	{
		ShownValue->SetText(GetDisplayText());
	}

	// Without Wrap, the arrow at an end the choice can't pass dims.
	bool bAtStart = false;
	bool bAtEnd = false;
	if (!bWrap)
	{
		if (Mode == EChoiceButtonMode::Number)
		{
			bAtStart = Value <= MinValue + KINDA_SMALL_NUMBER;
			bAtEnd = Value >= MaxValue - KINDA_SMALL_NUMBER;
		}
		else
		{
			bAtStart = SelectedIndex <= 0;
			bAtEnd = SelectedIndex >= Options.Num() - 1;
		}
	}
	if (UWidget* PreviousPart = GetPart(ChoiceButtonParts::PreviousArrow))
	{
		PreviousPart->SetRenderOpacity(bAtStart ? DimmedArrowOpacity : 1.f);
	}
	if (UWidget* NextPart = GetPart(ChoiceButtonParts::NextArrow))
	{
		NextPart->SetRenderOpacity(bAtEnd ? DimmedArrowOpacity : 1.f);
	}

	// The new value slides in from the side it came from (in the designer too, when its toolbar steps the preview).
	if (Direction != 0 && ShownValue && (!IsDesignTime() || bAnimateInDesigner))
	{
		KickOffset = Direction * KickDistance;
		ShownValue->SetRenderTranslation(FVector2D(KickOffset, 0.0));
		ShownValue->SetRenderOpacity(0.4f);
		const TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
		if (!bIsKickTimerActive && SlateWidget.IsValid())
		{
			bIsKickTimerActive = true;
			SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UChoiceButton::TickKick));
		}
	}
}

EActiveTimerReturnType UChoiceButton::TickKick(double InCurrentTime, float InDeltaTime)
{
	using namespace ChoiceButtonPrivate;

	KickOffset *= FMath::Exp(-InDeltaTime * KickSettleSpeed);
	const bool bSettled = FMath::Abs(KickOffset) < 0.05f;
	if (bSettled)
	{
		KickOffset = 0.f;
	}

	if (UTextBlock* ShownValue = KickedValueText.Get())
	{
		ShownValue->SetRenderTranslation(FVector2D(KickOffset, 0.0));
		ShownValue->SetRenderOpacity(1.f - 0.6f * FMath::Abs(KickOffset) / KickDistance);
	}

	if (bSettled)
	{
		bIsKickTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

#if WITH_EDITOR
int32 UChoiceButton::GetDesignerPreviewValueCount() const
{
	return Mode == EChoiceButtonMode::Number ? GetNumberSteps() + 1 : Options.Num();
}

int32 UChoiceButton::GetDesignerPreviewValueIndex() const
{
	if (Mode == EChoiceButtonMode::Number)
	{
		const int32 Count = GetNumberSteps() + 1;
		return FMath::Clamp(FMath::RoundToInt((Value - MinValue) / FMath::Max(Step, KINDA_SMALL_NUMBER)), 0, Count - 1);
	}
	return SelectedIndex;
}

FText UChoiceButton::GetDesignerPreviewValueText() const
{
	return GetDisplayText();
}

void UChoiceButton::SetDesignerPreviewValueIndex(int32 Index, bool bAnimate)
{
	const int32 Count = GetDesignerPreviewValueCount();
	if (Count <= 0)
	{
		return;
	}
	const int32 Current = GetDesignerPreviewValueIndex();
	const int32 Next = ((Index % Count) + Count) % Count;
	if (Mode == EChoiceButtonMode::Number)
	{
		Value = FMath::Min(MinValue + Next * Step, MaxValue);
	}
	else
	{
		SelectedIndex = Next;
	}

	// Stepping on past the end comes back in from the side it is heading, as a wrapping step does at runtime.
	const int32 Direction = Next == Current ? 0 : ((Index > Current) ? 1 : -1);
	bAnimateInDesigner = bAnimate;
	UpdateDisplay(Direction);
	bAnimateInDesigner = false;
}
#endif

void UChoiceButton::BuildParts()
{
	UTextBlock* LabelBlock = MakeLabel(ChoiceButtonParts::Label, LOCTEXT("ChoiceLabel", "Quality"), 15);
	ButtonTextWidgetName = LabelBlock->GetFName();

	// A Triangle points up: turned a quarter either way, it points back or on.
	UTriangleShape* PreviousShape = MakePart<UTriangleShape>(ChoiceButtonParts::PreviousArrow);
	PreviousShape->ShapeRotation = -90.f;
	UTriangleShape* NextShape = MakePart<UTriangleShape>(ChoiceButtonParts::NextArrow);
	NextShape->ShapeRotation = 90.f;

	UTextBlock* ValueBlock = MakeLabel(ChoiceButtonParts::ValueText, GetDisplayText(), 15);
	ValueBlock->SetJustification(ETextJustify::Center);

	UHorizontalBox* Selector = MakePart<UHorizontalBox>(TEXT("Selector"));
	UWidget* PreviousSizeBox = Sized(PreviousShape, ChoiceButtonParts::PreviousBox);
	if (UHorizontalBoxSlot* PreviousSlot = Selector->AddChildToHorizontalBox(PreviousSizeBox))
	{
		PreviousSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* ValueSlot = Selector->AddChildToHorizontalBox(ValueBlock))
	{
		ValueSlot->SetVerticalAlignment(VAlign_Center);
		ValueSlot->SetHorizontalAlignment(HAlign_Center);
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetPadding(FMargin(10.f, 0.f));
	}
	UWidget* NextSizeBox = Sized(NextShape, ChoiceButtonParts::NextBox);
	if (UHorizontalBoxSlot* NextSlot = Selector->AddChildToHorizontalBox(NextSizeBox))
	{
		NextSlot->SetVerticalAlignment(VAlign_Center);
	}

	UHorizontalBox* Row = MakePart<UHorizontalBox>(TEXT("Row"));
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelBlock))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f));
	}
	UWidget* SelectorSizeBox = Sized(Selector, ChoiceButtonParts::SelectorBox);
	if (UHorizontalBoxSlot* SelectorSlot = Row->AddChildToHorizontalBox(SelectorSizeBox))
	{
		SelectorSlot->SetVerticalAlignment(VAlign_Center);
		SelectorSlot->SetHorizontalAlignment(HAlign_Right);
	}

	RememberPart(ChoiceButtonParts::Label, LabelBlock);
	RememberPart(ChoiceButtonParts::ValueText, ValueBlock);
	RememberPart(ChoiceButtonParts::PreviousArrow, PreviousShape);
	RememberPart(ChoiceButtonParts::NextArrow, NextShape);
	RememberPart(ChoiceButtonParts::PreviousBox, PreviousSizeBox);
	RememberPart(ChoiceButtonParts::NextBox, NextSizeBox);
	RememberPart(ChoiceButtonParts::SelectorBox, SelectorSizeBox);
	SetButtonContent(Row);
}

void UChoiceButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const EStyledButtonCorners CornerStyle = Corners;
	const float Rounding = CornerRounding;
	const FLinearColor Fill = FillColor;
	const FLinearColor Edge = OutlineColor;
	const URectangleShape* Model = MakeBackground<URectangleShape>([CornerStyle, Rounding, Fill, Edge](URectangleShape& Shape)
	{
		SetCorners(Shape, CornerStyle, Rounding);
		Solid(Shape, Fill);
		Outline(Shape, 1.5f, Edge);
	});
	SetContentLayout(ContentPadding, HAlign_Fill);
	SetBoxSize(FindPart(ChoiceButtonParts::SelectorBox), FVector2D(SelectorWidth, 0.0));
	SetBoxSize(FindPart(ChoiceButtonParts::PreviousBox), FVector2D(ArrowSize, ArrowSize));
	SetBoxSize(FindPart(ChoiceButtonParts::NextBox), FVector2D(ArrowSize, ArrowSize));

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Fill, Edge](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.08f);
		Shape.Stroke.Paint.Color = Lighter(Edge, 0.4f);
	});
	OverrideShape(Self, EContextMenuState::Pressed).Look = MakeLook<URectangleShape>(*Model, [Fill, Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Fill * 0.8f;
		Shape.Stroke.Paint.Color = Accent;
	});

	// The arrows light with the row (a Design Target's Tint on a shape is its fill).
	for (const FName ArrowRole : { ChoiceButtonParts::PreviousArrow, ChoiceButtonParts::NextArrow })
	{
		if (UTriangleShape* ArrowShape = FindPart<UTriangleShape>(ArrowRole))
		{
			ArrowShape->CornerRounding = 1.5f;
			Solid(*ArrowShape, FLinearColor::White);
			ArrowShape->InvalidateShape();
			AddTint(ArrowShape, ArrowColor, { { EContextMenuState::Hovered, Accent }, { EContextMenuState::Pressed, Accent } });
		}
	}

	for (const FName TextRole : { ChoiceButtonParts::Label, ChoiceButtonParts::ValueText })
	{
		if (UTextBlock* TextPart = FindPart<UTextBlock>(TextRole))
		{
			FSlateFontInfo PartFont = TextPart->GetFont();
			PartFont.Size = FontSize;
			TextPart->SetFont(PartFont);
		}
	}
	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(ChoiceButtonParts::Label))
	{
		LabelBlock->SetVisibility(bShowLabel ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		LabelBlock->SetColorAndOpacity(FSlateColor(LabelColor));
		AddTint(LabelBlock, LabelColor, { { EContextMenuState::Hovered, ValueColor }, { EContextMenuState::Pressed, ValueColor } });
	}
	if (UTextBlock* ValueBlock = FindPart<UTextBlock>(ChoiceButtonParts::ValueText))
	{
		ValueBlock->SetColorAndOpacity(FSlateColor(ValueColor));
	}
	AddDisabledFade();
}

// =====================================================================================================
// Segmented Choice
// =====================================================================================================

namespace SegmentedChoicePrivate
{
	/** Ticks the slide waits for the track to be laid out before giving up (a widget never shown). */
	constexpr int32 MaxUnplacedTicks = 240;
}

USegmentedChoiceButton::USegmentedChoiceButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Options = { LOCTEXT("SegmentLow", "Low"), LOCTEXT("SegmentMedium", "Medium"), LOCTEXT("SegmentHigh", "High") };
	SelectedIndex = 1;
	FillColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::Line();
	TextColor = StyledButtonTheme::TextDim();
	SelectedTextColor = StyledButtonTheme::Text();
}

FText USegmentedChoiceButton::GetSelectedOption() const
{
	return Options.IsValidIndex(SelectedIndex) ? Options[SelectedIndex] : FText::GetEmpty();
}

float USegmentedChoiceButton::GetSlideSpeed() const
{
	switch (Motion)
	{
	case EStyledButtonMotion::Snappy:  return 30.f;
	case EStyledButtonMotion::Bouncy:  return 14.f;
	case EStyledButtonMotion::Gentle:  return 7.f;
	case EStyledButtonMotion::Instant: return 0.f;
	case EStyledButtonMotion::Smooth:
	default:                           return 18.f;
	}
}

void USegmentedChoiceButton::SetSelectedIndex(int32 InIndex, bool bBroadcast)
{
	const int32 Clamped = Options.Num() > 0 ? FMath::Clamp(InIndex, 0, Options.Num() - 1) : 0;
	if (Clamped == SelectedIndex)
	{
		return;
	}
	SelectedIndex = Clamped;
	StartSlide();
	if (bBroadcast)
	{
		OnChoiceChanged.Broadcast(SelectedIndex, static_cast<float>(SelectedIndex));
	}
}

void USegmentedChoiceButton::SetOptions(const TArray<FText>& InOptions, int32 InSelectedIndex)
{
	Options = InOptions;
	SelectedIndex = Options.Num() > 0 ? FMath::Clamp(InSelectedIndex, 0, Options.Num() - 1) : 0;
	RebuildSegments();
	HighlightPosition = static_cast<float>(SelectedIndex);
	StartSlide();
}

bool USegmentedChoiceButton::HandleNavigation(EUINavigation Direction)
{
	if (!bLeftRightSteps || (Direction != EUINavigation::Left && Direction != EUINavigation::Right))
	{
		return false;
	}
	SetSelectedIndex(SelectedIndex + (Direction == EUINavigation::Right ? 1 : -1), /*bBroadcast=*/true);
	return true;
}

void USegmentedChoiceButton::HandleSegmentClicked(bool bWillDestroyCard, bool bIsEnabling)
{
	const int32 Count = SegmentTexts.Num();
	if (Count == 0)
	{
		return;
	}

	// The choice under the press; a key or gamepad press, which has no place, steps on round.
	CacheSlideParts();
	FVector2D Press;
	const UWidget* SegmentsPart = CachedSegments.Get();
	if (SegmentsPart && GetLastPressScreenPosition(Press))
	{
		const FGeometry& Geometry = SegmentsPart->GetCachedGeometry();
		const double Width = Geometry.GetLocalSize().X;
		if (Width > 0.0)
		{
			const double Fraction = FVector2D(Geometry.AbsoluteToLocal(Press)).X / Width;
			SetSelectedIndex(FMath::Clamp(FMath::FloorToInt(Fraction * Count), 0, Count - 1), /*bBroadcast=*/true);
			return;
		}
	}
	SetSelectedIndex((SelectedIndex + 1) % Count, /*bBroadcast=*/true);
}

void USegmentedChoiceButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	SelectedIndex = Options.Num() > 0 ? FMath::Clamp(SelectedIndex, 0, Options.Num() - 1) : 0;
	bHasCachedSlideParts = false;
	RebuildSegments();

	if (!IsDesignTime())
	{
		OnContextClicked.AddUniqueDynamic(this, &USegmentedChoiceButton::HandleSegmentClicked);
	}

	// The designer shows the choice where it is, without sliding there.
	if (!bHasPlacedHighlight || IsDesignTime())
	{
		HighlightPosition = static_cast<float>(SelectedIndex);
	}
	StartSlide();
}

void USegmentedChoiceButton::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	bIsSlideTimerActive = false;
	bHasPlacedHighlight = false;
	bHasCachedSlideParts = false;
}

void USegmentedChoiceButton::CacheSlideParts()
{
	if (bHasCachedSlideParts && CachedSegments.IsValid())
	{
		return;
	}
	bHasCachedSlideParts = true;
	CachedSegments = GetPart(ChoiceButtonParts::Segments);
	CachedHighlight = GetPart(ChoiceButtonParts::Highlight);
	CachedHighlightBox = GetPart<USizeBox>(ChoiceButtonParts::HighlightBox);
}

void USegmentedChoiceButton::RebuildSegments()
{
	CacheSlideParts();
	UHorizontalBox* SegmentsBox = Cast<UHorizontalBox>(CachedSegments.Get());
	if (!SegmentsBox)
	{
		return;
	}

	TArray<FString> Wanted;
	for (const FText& Option : Options)
	{
		Wanted.Add(Option.ToString());
	}
	const bool bAllThere = SegmentTexts.Num() == Wanted.Num()
		&& !SegmentTexts.ContainsByPredicate([SegmentsBox](const TObjectPtr<UTextBlock>& Existing) { return !Existing || Existing->GetParent() != SegmentsBox; });
	if (!bAllThere || Wanted != BuiltOptions)
	{
		for (UTextBlock* Old : SegmentTexts)
		{
			if (Old)
			{
				Old->RemoveFromParent();
			}
		}
		SegmentTexts.Reset();
		BuiltOptions = Wanted;

		// Made at runtime (and on the designer's preview), never stored: the options are the source.
		for (const FText& Option : Options)
		{
			UTextBlock* SegmentText = NewObject<UTextBlock>(this, NAME_None, RF_Transient);
			SegmentText->SetText(Option);
			SegmentText->SetJustification(ETextJustify::Center);
			SegmentText->SetVisibility(ESlateVisibility::HitTestInvisible);
			SegmentsBox->AddChildToHorizontalBox(SegmentText);
			SegmentTexts.Add(SegmentText);
		}
		bHasPlacedHighlight = false;
	}

	// Styled every time: the font, padding and colors may have changed while the options didn't.
	for (UTextBlock* SegmentText : SegmentTexts)
	{
		if (!SegmentText)
		{
			continue;
		}
		if (Font.HasValidFont())
		{
			SegmentText->SetFont(Font);
		}
		else
		{
			FSlateFontInfo DefaultFont = SegmentText->GetFont();
			DefaultFont.Size = 14;
			SegmentText->SetFont(DefaultFont);
		}
		SegmentText->SetColorAndOpacity(FSlateColor(TextColor));
		if (UHorizontalBoxSlot* TextSlot = Cast<UHorizontalBoxSlot>(SegmentText->Slot))
		{
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(SegmentPadding);
		}
	}
}

void USegmentedChoiceButton::PlaceHighlight(float Position)
{
	const int32 Count = SegmentTexts.Num();

	// Each choice colored by how near the highlight is to it, so the color slides with it.
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (UTextBlock* SegmentText = SegmentTexts[Index])
		{
			const float Nearness = FMath::Clamp(1.f - FMath::Abs(Position - Index), 0.f, 1.f);
			SegmentText->SetColorAndOpacity(FSlateColor(FMath::Lerp(TextColor, SelectedTextColor, Nearness)));
		}
	}

	CacheSlideParts();
	UWidget* HighlightPart = CachedHighlight.Get();
	USizeBox* HighlightSizeBox = CachedHighlightBox.Get();
	const UWidget* SegmentsPart = CachedSegments.Get();
	if (!HighlightPart || !SegmentsPart)
	{
		bHasPlacedHighlight = true;
		return;
	}
	if (Count == 0)
	{
		HighlightPart->SetRenderOpacity(0.f);
		bHasPlacedHighlight = true;
		return;
	}
	const double TrackWidth = SegmentsPart->GetCachedGeometry().GetLocalSize().X;
	if (TrackWidth <= 0.0)
	{
		return;
	}

	// The highlight's own box is one choice wide, at the track's left, and moves over to the choice at Position: its
	// widget sits where it is drawn - on the button, never past its ends.
	const double SegmentWidth = TrackWidth / Count;
	HighlightPart->SetRenderOpacity(1.f);
	UWidget* Moved = HighlightSizeBox ? static_cast<UWidget*>(HighlightSizeBox) : HighlightPart;
	if (HighlightSizeBox && !FMath::IsNearlyEqual(HighlightSizeBox->GetWidthOverride(), static_cast<float>(SegmentWidth), 0.01f))
	{
		HighlightSizeBox->SetWidthOverride(static_cast<float>(SegmentWidth));
	}
	Moved->SetRenderTranslation(FVector2D(Position * SegmentWidth, 0.0));
	bHasPlacedHighlight = true;
}

void USegmentedChoiceButton::StartSlide()
{
	bHasPlacedHighlight = false;
	PlaceHighlight(HighlightPosition);

	const TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!bIsSlideTimerActive && SlateWidget.IsValid())
	{
		bIsSlideTimerActive = true;
		SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &USegmentedChoiceButton::TickSlide));
	}
}

EActiveTimerReturnType USegmentedChoiceButton::TickSlide(double InCurrentTime, float InDeltaTime)
{
	const float Target = static_cast<float>(SelectedIndex);
	const float Speed = GetSlideSpeed();
	if ((IsDesignTime() && !bAnimateInDesigner) || Speed <= 0.f)
	{
		HighlightPosition = Target;
	}
	else
	{
		HighlightPosition += (Target - HighlightPosition) * (1.f - FMath::Exp(-InDeltaTime * Speed));
	}
	const bool bArrived = FMath::Abs(Target - HighlightPosition) < 0.001f;
	if (bArrived)
	{
		HighlightPosition = Target;
		bAnimateInDesigner = false;
	}

	bHasPlacedHighlight = false;
	PlaceHighlight(HighlightPosition);

	// Laid out yet? Until the first paint there is no width to place the highlight by.
	if (!bHasPlacedHighlight)
	{
		if (++UnplacedTicks < SegmentedChoicePrivate::MaxUnplacedTicks)
		{
			return EActiveTimerReturnType::Continue;
		}
	}
	UnplacedTicks = 0;

	if (bArrived || !bHasPlacedHighlight)
	{
		bIsSlideTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

#if WITH_EDITOR
void USegmentedChoiceButton::SetDesignerPreviewValueIndex(int32 Index, bool bAnimate)
{
	const int32 Count = Options.Num();
	if (Count <= 0)
	{
		return;
	}
	SelectedIndex = ((Index % Count) + Count) % Count;
	bAnimateInDesigner = bAnimate;
	if (!bAnimate)
	{
		HighlightPosition = static_cast<float>(SelectedIndex);
	}
	StartSlide();
}
#endif

void USegmentedChoiceButton::BuildParts()
{
	URectangleShape* HighlightShape = MakePart<URectangleShape>(ChoiceButtonParts::Highlight);
	UWidget* HighlightSizeBox = Sized(HighlightShape, ChoiceButtonParts::HighlightBox);
	UHorizontalBox* SegmentsBox = MakePart<UHorizontalBox>(ChoiceButtonParts::Segments);

	// The highlight under the choices, in a box one choice wide at the track's left (Place Highlight sizes and moves it).
	UOverlay* Track = MakePart<UOverlay>(TEXT("Track"));
	if (UOverlaySlot* HighlightSlot = Track->AddChildToOverlay(HighlightSizeBox))
	{
		HighlightSlot->SetHorizontalAlignment(HAlign_Left);
		HighlightSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UOverlaySlot* SegmentsSlot = Track->AddChildToOverlay(SegmentsBox))
	{
		SegmentsSlot->SetHorizontalAlignment(HAlign_Fill);
		SegmentsSlot->SetVerticalAlignment(VAlign_Fill);
	}

	RememberPart(ChoiceButtonParts::Highlight, HighlightShape);
	RememberPart(ChoiceButtonParts::HighlightBox, HighlightSizeBox);
	RememberPart(ChoiceButtonParts::Segments, SegmentsBox);
	SetButtonContent(Track);
}

void USegmentedChoiceButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const bool bPill = bFullyRounded;
	const float Rounding = FMath::Max(CornerRounding, 0.f);
	const float Gap = FMath::Max(Inset, 0.f);
	const FLinearColor Fill = FillColor;
	const FLinearColor Edge = OutlineColor;
	const URectangleShape* Model = MakeBackground<URectangleShape>([bPill, Rounding, Fill, Edge](URectangleShape& Shape)
	{
		Shape.bIndividualCorners = false;
		Shape.bFullyRounded = bPill;
		Shape.CornerRounding = bPill ? 0.f : Rounding;
		Solid(Shape, Fill);
		Outline(Shape, 1.5f, Edge);
	});
	SetContentLayout(FMargin(Gap), HAlign_Fill, VAlign_Fill);

	// Concentric with the track: a pill inside a pill, or the track's rounding less the gap - so at either end the
	// highlight's corners follow the track's edge rather than poking past it.
	if (URectangleShape* HighlightShape = FindPart<URectangleShape>(ChoiceButtonParts::Highlight))
	{
		HighlightShape->bIndividualCorners = false;
		HighlightShape->bFullyRounded = bPill;
		HighlightShape->CornerRounding = bPill ? 0.f : FMath::Max(Rounding - Gap, 0.f);
		HighlightShape->ShapeScale = FVector2D(1.0, 1.0);
		Solid(*HighlightShape, AccentColor);
		HighlightShape->SetRenderTransform(FWidgetTransform());
		HighlightShape->SetVisibility(ESlateVisibility::HitTestInvisible);
		HighlightShape->InvalidateShape();
	}
	if (UWidget* HighlightSizeBox = FindPart(ChoiceButtonParts::HighlightBox))
	{
		HighlightSizeBox->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	if (UWidget* SegmentsBox = FindPart(ChoiceButtonParts::Segments))
	{
		SegmentsBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}

	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Fill, Edge](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.05f);
		Shape.Stroke.Paint.Color = Lighter(Edge, 0.4f);
	});
	AddDisabledFade();

	// On a shown button (the designer's preview, or Refresh Style at runtime) the choices restyle now. The Widget Blueprint's
	// own copy never gets them: they are made while the button runs, never stored.
	bHasCachedSlideParts = false;
	if (GetCachedWidget().IsValid())
	{
		RebuildSegments();
		StartSlide();
	}
}

#undef LOCTEXT_NAMESPACE
