// Copyright AR GameSystems 2026. All rights reserved.

#include "StyledButtons.h"
#include "StyledButtonTheme.h"

#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "StyledButtons"

namespace StyledButtonParts
{
	const FName Label(TEXT("Label"));
	const FName Icon(TEXT("Icon"));
	const FName Knob(TEXT("Knob"));
	const FName KnobBox(TEXT("KnobBox"));
	const FName Check(TEXT("Check"));
	const FName CheckBox(TEXT("CheckBox"));
	const FName Dot(TEXT("Dot"));
	const FName DotBox(TEXT("DotBox"));
	const FName Bar(TEXT("Bar"));
	const FName BarBox(TEXT("BarBox"));
	const FName RingTrack(TEXT("RingTrack"));
	const FName Progress(TEXT("Progress"));
	const FName RingBox(TEXT("RingBox"));
	const FName Rarity(TEXT("Rarity"));
	const FName RarityBox(TEXT("RarityBox"));
	const FName TileBox(TEXT("TileBox"));
	const FName IconBox(TEXT("IconBox"));
}

// =====================================================================================================
// Base
// =====================================================================================================

UStyledButtonBase::UStyledButtonBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// A menu's button doesn't close the card it is on unless asked to.
	bShouldDestroyCard = false;
	bButtonCanBeEnabled = false;
	AccentColor = StyledButtonTheme::Accent();
}

#if WITH_EDITOR
const FText UStyledButtonBase::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD Buttons");
}

void UStyledButtonBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// A Style setting makes the looks again - before the Context Menu Button lays out the Looks and seeds its overrides.
	const FProperty* Changed = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty : PropertyChangedEvent.Property;
	const bool bStyleSetting = Changed && Changed->GetMetaData(TEXT("Category")).StartsWith(TEXT("Style"));
	if (bStyleSetting && bUseStyle && HasAnyContent())
	{
		ApplyStyle();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UStyledButtonBase::BuildDefaultContent()
{
	ShapeTargets.Reset();
	DesignTargets.Reset();
	PartNames.Reset();
	BuildParts();
	ApplyStyle();
}

void UStyledButtonBase::BeginStyle()
{
	ShapeTargets.Reset();
	DesignTargets.Reset();
	LookBaselines.Reset();
}

void UStyledButtonBase::RefreshStyle()
{
	if (!bUseStyle)
	{
		return;
	}
	ApplyStyle();
	RefreshShapeTargetLooks();
	RefreshButtonState(/*bInstant=*/true);
}

void UStyledButtonBase::SetLabel(const FText& Text)
{
	if (UTextBlock* LabelBlock = ResolveButtonTextBlock())
	{
		LabelBlock->SetText(Text);
	}
}

FText UStyledButtonBase::GetLabel()
{
	const UTextBlock* LabelBlock = ResolveButtonTextBlock();
	return LabelBlock ? LabelBlock->GetText() : FText::GetEmpty();
}

void UStyledButtonBase::RememberPart(FName Role, const UWidget* Part)
{
	if (Part)
	{
		PartNames.Add(Role, Part->GetFName());
	}
}

UWidget* UStyledButtonBase::GetPart(FName Role)
{
	const FName* Name = PartNames.Find(Role);
	return Name ? FindWidgetInButtonTree(*Name) : nullptr;
}

UWidget* UStyledButtonBase::FindPart(FName Role)
{
	UWidget* Part = GetPart(Role);
	if (Part)
	{
		// Inside an edit's transaction, the change the style makes to it is undone with the edit.
		Part->Modify();
	}
	return Part;
}

UWidgetTree* UStyledButtonBase::GetBuildTree() const
{
	return GetTypedOuter<UWidgetTree>();
}

UWidget* UStyledButtonBase::MakePart(UClass* Class, FName BaseName)
{
	UWidgetTree* Tree = GetBuildTree();
	UObject* Outer = Tree ? static_cast<UObject*>(Tree) : static_cast<UObject*>(this);

	// The plain name while it is free (Label), else the next one (Label_1). A copy of the button renames its parts the
	// same way, and its targets still find them - see UContextMenuButton::FindWidgetInButtonTree.
	const FName PartName = StaticFindObjectFast(UObject::StaticClass(), Outer, BaseName) ? MakeUniqueObjectName(Outer, Class, BaseName) : BaseName;
	if (Tree)
	{
		return Tree->ConstructWidget<UWidget>(Class, PartName);
	}
	return NewObject<UWidget>(Outer, Class, PartName, RF_Transactional);
}

UTextBlock* UStyledButtonBase::MakeLabel(FName BaseName, const FText& Text, int32 Size)
{
	UTextBlock* LabelBlock = MakePart<UTextBlock>(BaseName);
	LabelBlock->SetText(Text);
	FSlateFontInfo LabelFont = LabelBlock->GetFont();
	LabelFont.Size = Size;
	LabelBlock->SetFont(LabelFont);
	LabelBlock->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	return LabelBlock;
}

void UStyledButtonBase::SetButtonContent(UWidget* Content)
{
	SetContent(Content);
}

void UStyledButtonBase::SetContentLayout(const FMargin& Padding, EHorizontalAlignment HAlign, EVerticalAlignment VAlign)
{
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(GetPartsSlot()))
	{
		ContentSlot->Modify();
		ContentSlot->SetPadding(Padding);
		ContentSlot->SetHorizontalAlignment(HAlign);
		ContentSlot->SetVerticalAlignment(VAlign);
	}
}

UWidget* UStyledButtonBase::Sized(UWidget* Content, FName BaseName)
{
	USizeBox* Box = MakePart<USizeBox>(BaseName);
	Box->SetContent(Content);
	return Box;
}

void UStyledButtonBase::SetBoxSize(UWidget* SizeBox, const FVector2D& Size)
{
	USizeBox* Box = Cast<USizeBox>(SizeBox);
	if (!Box)
	{
		return;
	}
	if (Size.X > 0.0)
	{
		Box->SetWidthOverride(static_cast<float>(Size.X));
	}
	else
	{
		Box->ClearWidthOverride();
	}
	if (Size.Y > 0.0)
	{
		Box->SetHeightOverride(static_cast<float>(Size.Y));
	}
	else
	{
		Box->ClearHeightOverride();
	}
}

FInstancedPropertyBag UStyledButtonBase::MakeLookFrom(const UShapeWidget& From, TFunctionRef<void(UShapeWidget&)> Change)
{
	// A scratch shape of the same kind holding From's settings, changed, then read into a Look.
	UShapeWidget* Scratch = NewObject<UShapeWidget>(GetTransientPackage(), From.GetClass());
	TArray<const FProperty*> Properties;
	WidgetShapeAnimation::GetProperties(From.GetClass(), Properties);
	for (const FProperty* Property : Properties)
	{
		Property->CopyCompleteValue_InContainer(Scratch, &From);
	}
	Change(*Scratch);

	FInstancedPropertyBag Look;
	WidgetShapeAnimation::LayOut(Look, Scratch->GetClass(), Scratch);
	return Look;
}

void UStyledButtonBase::SetBackgroundFrom(const UShapeWidget& Model)
{
	BackgroundShape = Model.GetClass();
	BackgroundLook.Reset();
	WidgetShapeAnimation::LayOut(BackgroundLook, Model.GetClass(), &Model);
	bHasBackgroundShape = true;
}

void UStyledButtonBase::SetCorners(URectangleShape& Shape, EStyledButtonCorners Corners, float Rounding)
{
	Shape.bIndividualCorners = false;
	Shape.bFullyRounded = Corners == EStyledButtonCorners::Pill;
	Shape.CornerRounding = Corners == EStyledButtonCorners::Rounded ? FMath::Max(Rounding, 0.f) : 0.f;
}

FName UStyledButtonBase::TargetNameOf(const UWidget* Part) const
{
	return (!Part || Part == this) ? SelfTargetName : Part->GetFName();
}

FContextMenuShapeTarget& UStyledButtonBase::AddShapeTarget(UWidget* Part)
{
	FContextMenuShapeTarget& Target = ShapeTargets.AddDefaulted_GetRef();
	Target.TargetWidgetName = TargetNameOf(Part);
	Target.ShapeSource = EContextMenuButtonSettingSource::Custom;
	Target.bSeparateTransformAndSize = false;

	switch (Motion)
	{
	case EStyledButtonMotion::Snappy:
		Target.TransformInterpolationSpeed = 22.f;
		Target.TransformInterpolationCurve = EContextMenuInterpolationCurve::EaseOut;
		break;
	case EStyledButtonMotion::Bouncy:
		Target.TransformInterpolationSpeed = 8.f;
		Target.TransformInterpolationCurve = EContextMenuInterpolationCurve::Click;
		Target.TransformOvershoot = 2.f;
		break;
	case EStyledButtonMotion::Gentle:
		Target.TransformInterpolationSpeed = 5.f;
		Target.TransformInterpolationCurve = EContextMenuInterpolationCurve::EaseInOut;
		break;
	case EStyledButtonMotion::Instant:
		Target.TransformInterpolationSpeed = 0.f;
		Target.TransformInterpolationCurve = EContextMenuInterpolationCurve::Linear;
		break;
	case EStyledButtonMotion::Smooth:
	default:
		Target.TransformInterpolationSpeed = 12.f;
		Target.TransformInterpolationCurve = EContextMenuInterpolationCurve::EaseOut;
		break;
	}

	// Enter / Exit Duration over Motion's pace: the way in at the Enter speed, back to Normal at the Exit speed.
	if (Motion != EStyledButtonMotion::Instant && (EnterDuration > 0.f || ExitDuration > 0.f))
	{
		const float MotionSpeed = Target.TransformInterpolationSpeed;
		Target.TransformInterpolationSpeed = GetShapeSpeed(EnterDuration);
		Target.bSeparateExitSpeed = true;
		Target.ExitInterpolationSpeed = ExitDuration > 0.f ? GetShapeSpeed(ExitDuration) : MotionSpeed;
	}

	// No Look on the Base Shape: the base is the part as it is. Each state given a Look below holds only what is used.
	Target.BaseShape.bIsVectorShape = Part == this ? BackgroundShape != nullptr : (Part && Part->IsA<UShapeWidget>());
	return Target;
}

FContextMenuShapeStateSettings& UStyledButtonBase::OverrideShape(FContextMenuShapeTarget& Target, EContextMenuState State)
{
	bool* bOverride = nullptr;
	FContextMenuShapeStateSettings* Settings = nullptr;
	switch (State)
	{
	case EContextMenuState::Hovered:  bOverride = &Target.bOverrideHoveredShape;  Settings = &Target.HoveredShape;  break;
	case EContextMenuState::Pressed:  bOverride = &Target.bOverridePressedShape;  Settings = &Target.PressedShape;  break;
	case EContextMenuState::Disabled: bOverride = &Target.bOverrideDisabledShape; Settings = &Target.DisabledShape; break;
	case EContextMenuState::Enabled:  bOverride = &Target.bOverrideEnabledShape;  Settings = &Target.EnabledShape;  break;
	case EContextMenuState::Normal:
	default:                          bOverride = &Target.bOverrideNormalShape;   Settings = &Target.NormalShape;   break;
	}
	if (!*bOverride)
	{
		*bOverride = true;
		*Settings = Target.BaseShape;
	}
	return *Settings;
}

FContextMenuDesignTarget& UStyledButtonBase::AddDesignTarget(UWidget* Part)
{
	FContextMenuDesignTarget& Target = DesignTargets.AddDefaulted_GetRef();
	Target.TargetWidgetName = TargetNameOf(Part);
	Target.DesignSource = EContextMenuButtonSettingSource::Custom;
	Target.BaseDesign.bDriveTint = false;
	switch (Motion)
	{
	case EStyledButtonMotion::Snappy:  Target.InterpolationSpeed = 28.f; break;
	case EStyledButtonMotion::Gentle:  Target.InterpolationSpeed = 7.f; break;
	case EStyledButtonMotion::Instant: Target.InterpolationSpeed = 0.f; break;
	default:                           Target.InterpolationSpeed = 15.f; break;
	}
	if (Motion != EStyledButtonMotion::Instant && (EnterDuration > 0.f || ExitDuration > 0.f))
	{
		const float MotionSpeed = Target.InterpolationSpeed;
		Target.InterpolationSpeed = GetDesignSpeed(EnterDuration);
		Target.bSeparateExitSpeed = true;
		Target.ExitInterpolationSpeed = ExitDuration > 0.f ? GetDesignSpeed(ExitDuration) : MotionSpeed;
	}
	return Target;
}

float UStyledButtonBase::GetShapeSpeed(float Duration) const
{
	if (Duration > 0.f)
	{
		return 1.f / FMath::Max(Duration, 0.001f);
	}
	switch (Motion)
	{
	case EStyledButtonMotion::Snappy:  return 22.f;
	case EStyledButtonMotion::Bouncy:  return 8.f;
	case EStyledButtonMotion::Gentle:  return 5.f;
	case EStyledButtonMotion::Instant: return 0.f;
	case EStyledButtonMotion::Smooth:
	default:                           return 12.f;
	}
}

float UStyledButtonBase::GetDesignSpeed(float Duration) const
{
	if (Duration > 0.f)
	{
		return 3.f / FMath::Max(Duration, 0.001f);
	}
	switch (Motion)
	{
	case EStyledButtonMotion::Snappy:  return 28.f;
	case EStyledButtonMotion::Gentle:  return 7.f;
	case EStyledButtonMotion::Instant: return 0.f;
	default:                           return 15.f;
	}
}

FContextMenuWidgetDesign& UStyledButtonBase::OverrideDesign(FContextMenuDesignTarget& Target, EContextMenuState State)
{
	bool* bOverride = nullptr;
	FContextMenuWidgetDesign* Design = nullptr;
	switch (State)
	{
	case EContextMenuState::Hovered:  bOverride = &Target.bOverrideHoveredDesign;  Design = &Target.Hovered;  break;
	case EContextMenuState::Pressed:  bOverride = &Target.bOverridePressedDesign;  Design = &Target.Pressed;  break;
	case EContextMenuState::Disabled: bOverride = &Target.bOverrideDisabledDesign; Design = &Target.Disabled; break;
	case EContextMenuState::Enabled:  bOverride = &Target.bOverrideEnabledDesign;  Design = &Target.Enabled;  break;
	case EContextMenuState::Normal:
	default:                          bOverride = &Target.bOverrideNormalDesign;   Design = &Target.Normal;   break;
	}
	if (!*bOverride)
	{
		*bOverride = true;
		*Design = Target.BaseDesign;
	}
	return *Design;
}

FContextMenuDesignTarget& UStyledButtonBase::AddTint(UWidget* Part, const FLinearColor& Base, const TArray<FStyledButtonStateTint>& States)
{
	FContextMenuDesignTarget& Target = AddDesignTarget(Part);
	Target.BaseDesign.bDriveTint = true;
	Target.BaseDesign.Tint = Base;
	for (const FStyledButtonStateTint& StateTint : States)
	{
		OverrideDesign(Target, StateTint.State).Tint = StateTint.Tint;
	}
	return Target;
}

void UStyledButtonBase::AddDisabledFade(float Opacity)
{
	FContextMenuDesignTarget& Target = AddDesignTarget(this);
	FContextMenuWidgetDesign& DisabledDesign = OverrideDesign(Target, EContextMenuState::Disabled);
	DisabledDesign.bDriveOpacity = true;
	DisabledDesign.Opacity = Opacity;
}

FLinearColor UStyledButtonBase::Lighter(const FLinearColor& Color, float Amount)
{
	FLinearColor Result = FMath::Lerp(Color, FLinearColor::White, FMath::Clamp(Amount, 0.f, 1.f));
	Result.A = Color.A;
	return Result;
}

// =====================================================================================================
// Generic Button
// =====================================================================================================

UGenericButton::UGenericButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FillColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::Line();
	TextColor = StyledButtonTheme::Text();
}

void UGenericButton::BuildParts()
{
	UTextBlock* LabelBlock = MakeLabel(StyledButtonParts::Label, LOCTEXT("GenericLabel", "Button"), 15);
	RememberPart(StyledButtonParts::Label, LabelBlock);
	ButtonTextWidgetName = LabelBlock->GetFName();
	SetButtonContent(LabelBlock);
}

void UGenericButton::ApplyStyle()
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
	SetContentLayout(ContentPadding);

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Fill, Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.08f);
		Shape.Stroke.Paint.Color = Accent;
	});
	FContextMenuShapeStateSettings& PressedState = OverrideShape(Self, EContextMenuState::Pressed);
	PressedState.Look = MakeLook<URectangleShape>(*Model, [Fill, Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Fill * 0.8f;
		Shape.Stroke.Paint.Color = Accent;
	});
	PressedState.Transform.Scale = FVector2D(PressScale, PressScale);

	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(StyledButtonParts::Label))
	{
		LabelBlock->SetColorAndOpacity(FSlateColor(TextColor * FLinearColor(0.75f, 0.75f, 0.8f, 1.f)));
		AddTint(LabelBlock, TextColor * FLinearColor(0.75f, 0.75f, 0.8f, 1.f), { { EContextMenuState::Hovered, TextColor }, { EContextMenuState::Pressed, TextColor } });
	}
	AddDisabledFade();
}

// =====================================================================================================
// Pill Button (switch)
// =====================================================================================================

UPillSwitchButton::UPillSwitchButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bButtonCanBeEnabled = true;
	TrackColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::LineHover();
	KnobColor = StyledButtonTheme::TextDim();
	KnobOnColor = FLinearColor::White;
}

bool UPillSwitchButton::HandleNavigation(EUINavigation Direction)
{
	if (!bLeftRightSetsValue || (Direction != EUINavigation::Left && Direction != EUINavigation::Right))
	{
		return false;
	}
	SetEnabledState(Direction == EUINavigation::Right);
	return true;
}

void UPillSwitchButton::BuildParts()
{
	UEllipseShape* KnobShape = MakePart<UEllipseShape>(StyledButtonParts::Knob);
	UWidget* KnobSizeBox = Sized(KnobShape, StyledButtonParts::KnobBox);
	RememberPart(StyledButtonParts::Knob, KnobShape);
	RememberPart(StyledButtonParts::KnobBox, KnobSizeBox);
	SetButtonContent(KnobSizeBox);
}

void UPillSwitchButton::ComputeKnob(const FVector2D& TrackExtent, float& OutKnob, float& OutTravel) const
{
	// The track is the button: the knob sits inside it, Knob Inset from its edges, and slides the track's width less its own.
	const float Inset = FMath::Max(KnobInset, 0.f);
	OutKnob = FMath::Max(static_cast<float>(TrackExtent.Y) - 2.f * Inset, 1.f);
	OutTravel = FMath::Max(static_cast<float>(TrackExtent.X) - 2.f * Inset - OutKnob, 0.f);
}

void UPillSwitchButton::FitKnob(const FVector2D& TrackExtent, bool bMarkModified)
{
	float KnobExtent = 0.f;
	float Travel = 0.f;
	ComputeKnob(TrackExtent, KnobExtent, Travel);
	const float Inset = FMath::Max(KnobInset, 0.f);
	FittedTrackSize = TrackExtent;

	// The knob's box and the padding around it: together exactly the track, so the switch asks for the size it has.
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(GetPartsSlot()))
	{
		if (bMarkModified)
		{
			ContentSlot->Modify();
		}
		ContentSlot->SetPadding(FMargin(Inset, Inset, Inset + Travel, Inset));
		ContentSlot->SetHorizontalAlignment(HAlign_Left);
		ContentSlot->SetVerticalAlignment(VAlign_Top);
	}
	SetBoxSize(bMarkModified ? FindPart(StyledButtonParts::KnobBox) : GetPart(StyledButtonParts::KnobBox), FVector2D(KnobExtent, KnobExtent));

	// The slide when on: the knob's Shape Target (the one on the knob that moves it when Enabled).
	const UWidget* KnobPart = GetPart(StyledButtonParts::Knob);
	const FName KnobTargetName = KnobPart ? TargetNameOf(KnobPart) : NAME_None;
	for (FContextMenuShapeTarget& Target : ShapeTargets)
	{
		if (!KnobTargetName.IsNone() && Target.TargetWidgetName == KnobTargetName && Target.bOverrideEnabledShape)
		{
			Target.EnabledShape.Transform.Translation = FVector2D(Travel, 0.0);
		}
	}
}

void UPillSwitchButton::HandleLayoutSizeChanged(const FVector2D& NewSize)
{
	// Only once styled, and only when the size really differs from what the knob was fitted to (no loop: fitting asks for this size).
	if (!bUseStyle || NewSize.X <= 1.0 || NewSize.Y <= 1.0 || NewSize.Equals(FittedTrackSize, 0.5) || !GetPart(StyledButtonParts::Knob))
	{
		return;
	}
	FitKnob(NewSize, /*bMarkModified=*/false);

	// Shown again at the new size: the runtime blend re-reads its goal; the designer's preview repaints.
	if (IsDesignTime())
	{
#if WITH_EDITOR
		ApplyDesignPreviews();
#endif
	}
	else
	{
		RefreshButtonState(/*bInstant=*/true);
	}
}

void UPillSwitchButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	float Knob = 0.f;
	float Travel = 0.f;
	ComputeKnob(TrackSize, Knob, Travel);

	const FLinearColor Track = TrackColor;
	const FLinearColor Edge = OutlineColor;
	const URectangleShape* Model = MakeBackground<URectangleShape>([Track, Edge](URectangleShape& Shape)
	{
		Shape.bFullyRounded = true;
		Solid(Shape, Track);
		Outline(Shape, 1.5f, Edge);
	});

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Track](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Track, 0.08f);
	});
	OverrideShape(Self, EContextMenuState::Enabled).Look = MakeLook<URectangleShape>(*Model, [Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Accent;
		Shape.Stroke.Paint.Color = Accent;
	});

	if (UEllipseShape* KnobShape = FindPart<UEllipseShape>(StyledButtonParts::Knob))
	{
		Solid(*KnobShape, KnobColor);
		KnobShape->SetRenderTransform(FWidgetTransform());

		FContextMenuShapeTarget& KnobTarget = AddShapeTarget(KnobShape);
		const FLinearColor KnobIdle = KnobColor;
		const FLinearColor KnobOn = KnobOnColor;
		OverrideShape(KnobTarget, EContextMenuState::Hovered).Look = MakeLook<UEllipseShape>(*KnobShape, [KnobIdle](UEllipseShape& Shape)
		{
			Shape.Fill.Color = Lighter(KnobIdle, 0.35f);
		});
		FContextMenuShapeStateSettings& OnState = OverrideShape(KnobTarget, EContextMenuState::Enabled);
		OnState.Transform.Translation = FVector2D(Travel, 0.0);
		OnState.Look = MakeLook<UEllipseShape>(*KnobShape, [KnobOn](UEllipseShape& Shape)
		{
			Shape.Fill.Color = KnobOn;
		});
	}

	// The knob's box, padding and slide for Track Size; laid out bigger or smaller by its slot, it is fitted again to that size.
	FitKnob(TrackSize, /*bMarkModified=*/true);
	AddDisabledFade();
}

// =====================================================================================================
// Checkbox
// =====================================================================================================

UCheckboxButton::UCheckboxButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bButtonCanBeEnabled = true;
	OutlineColor = StyledButtonTheme::TextDim();
	CheckColor = FLinearColor::White;
}

void UCheckboxButton::BuildParts()
{
	// The check mark: an open path, stroked with round ends, drawn on by its Trim End. The box is the button itself.
	UCustomShape* CheckShape = MakePart<UCustomShape>(StyledButtonParts::Check);
	FWidgetShapePath Path;
	FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
	Contour.bClosed = false;
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(0.24, 0.52)));
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(0.43, 0.70)));
	Contour.Vertices.Add(FWidgetShapeVertex(FVector2D(0.77, 0.31)));
	CheckShape->Path = Path;

	UWidget* CheckSizeBox = Sized(CheckShape, StyledButtonParts::CheckBox);
	RememberPart(StyledButtonParts::Check, CheckShape);
	RememberPart(StyledButtonParts::CheckBox, CheckSizeBox);
	SetButtonContent(CheckSizeBox);
}

void UCheckboxButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const float Rounding = CornerRounding;
	const float EdgeWidth = OutlineWidth;
	const FLinearColor Edge = OutlineColor;
	const URectangleShape* Model = MakeBackground<URectangleShape>([Rounding, EdgeWidth, Edge](URectangleShape& Shape)
	{
		Shape.CornerRounding = Rounding;
		Solid(Shape, Clear());
		Outline(Shape, EdgeWidth, Edge);
	});
	SetContentLayout(FMargin(0.f));
	SetBoxSize(FindPart(StyledButtonParts::CheckBox), FVector2D(BoxSize, BoxSize));

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Edge](URectangleShape& Shape)
	{
		Shape.Stroke.Paint.Color = Lighter(Edge, 0.5f);
		Shape.Fill.Color = RowHover();
	});
	OverrideShape(Self, EContextMenuState::Enabled).Look = MakeLook<URectangleShape>(*Model, [Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Accent;
		Shape.Stroke.Paint.Color = Accent;
	});

	if (UCustomShape* CheckShape = FindPart<UCustomShape>(StyledButtonParts::Check))
	{
		Hollow(*CheckShape, CheckWidth, CheckColor, EWidgetShapeStrokeAlign::Center);
		CheckShape->Stroke.Cap = EWidgetShapeLineCap::Round;
		CheckShape->Stroke.Join = EWidgetShapeLineJoin::Round;
		CheckShape->Trim.End = 0.f;
		CheckShape->InvalidateShape();

		FContextMenuShapeTarget& CheckTarget = AddShapeTarget(CheckShape);
		OverrideShape(CheckTarget, EContextMenuState::Enabled).Look = MakeLook<UCustomShape>(*CheckShape, [](UCustomShape& Shape)
		{
			Shape.Trim.End = 1.f;
		});
	}
	AddDisabledFade();
}

// =====================================================================================================
// Radio Button
// =====================================================================================================

URadioButton::URadioButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// One chosen per container; clicking the chosen one again clears it (Click Only Enables makes it stay).
	bButtonCanBeEnabled = true;
	bUnenableAllOtherGroupButtonsOnEnabled = true;
	bClickOnlyEnables = false;
	RingColor = StyledButtonTheme::TextDim();
}

void URadioButton::BuildParts()
{
	// The ring is the button itself; the dot sits in it.
	UEllipseShape* DotShape = MakePart<UEllipseShape>(StyledButtonParts::Dot);
	UWidget* DotSizeBox = Sized(DotShape, StyledButtonParts::DotBox);
	RememberPart(StyledButtonParts::Dot, DotShape);
	RememberPart(StyledButtonParts::DotBox, DotSizeBox);
	SetButtonContent(DotSizeBox);
}

void URadioButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const float EdgeWidth = RingWidth;
	const FLinearColor Ring = RingColor;
	const UEllipseShape* Model = MakeBackground<UEllipseShape>([EdgeWidth, Ring](UEllipseShape& Shape)
	{
		Solid(Shape, Clear());
		Outline(Shape, EdgeWidth, Ring);
	});
	SetContentLayout(FMargin(0.f));
	SetBoxSize(FindPart(StyledButtonParts::DotBox), FVector2D(Size, Size));

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<UEllipseShape>(*Model, [Ring](UEllipseShape& Shape)
	{
		Shape.Stroke.Paint.Color = Lighter(Ring, 0.5f);
		Shape.Fill.Color = RowHover();
	});
	OverrideShape(Self, EContextMenuState::Enabled).Look = MakeLook<UEllipseShape>(*Model, [Accent](UEllipseShape& Shape)
	{
		Shape.Stroke.Paint.Color = Accent;
	});

	if (UEllipseShape* DotShape = FindPart<UEllipseShape>(StyledButtonParts::Dot))
	{
		Solid(*DotShape, Accent);
		DotShape->ShapeScale = FVector2D(0.0, 0.0);
		DotShape->InvalidateShape();

		const float Scale = DotScale;
		FContextMenuShapeTarget& DotTarget = AddShapeTarget(DotShape);
		OverrideShape(DotTarget, EContextMenuState::Enabled).Look = MakeLook<UEllipseShape>(*DotShape, [Scale](UEllipseShape& Shape)
		{
			Shape.ShapeScale = FVector2D(Scale, Scale);
		});
	}
	AddDisabledFade();
}

// =====================================================================================================
// Menu Item
// =====================================================================================================

UMenuItemButton::UMenuItemButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TextColor = StyledButtonTheme::TextDim();
	HoverTextColor = StyledButtonTheme::Text();
	HoverFillColor = StyledButtonTheme::RowHover();
}

void UMenuItemButton::BuildParts()
{
	URectangleShape* BarShape = MakePart<URectangleShape>(StyledButtonParts::Bar);
	UTextBlock* LabelBlock = MakeLabel(StyledButtonParts::Label, LOCTEXT("MenuItemLabel", "Menu Item"), 20);
	ButtonTextWidgetName = LabelBlock->GetFName();

	UHorizontalBox* Row = MakePart<UHorizontalBox>(TEXT("Row"));
	UWidget* BarSizeBox = Sized(BarShape, StyledButtonParts::BarBox);
	if (UHorizontalBoxSlot* BarSlot = Row->AddChildToHorizontalBox(BarSizeBox))
	{
		BarSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelBlock))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(14.f, 0.f, 0.f, 0.f));
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	RememberPart(StyledButtonParts::Bar, BarShape);
	RememberPart(StyledButtonParts::BarBox, BarSizeBox);
	RememberPart(StyledButtonParts::Label, LabelBlock);
	SetButtonContent(Row);
}

void UMenuItemButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const URectangleShape* Model = MakeBackground<URectangleShape>([](URectangleShape& Shape)
	{
		Shape.CornerRounding = 4.f;
		Solid(Shape, Clear());
	});
	SetContentLayout(FMargin(12.f, 10.f), HAlign_Fill);
	SetBoxSize(FindPart(StyledButtonParts::BarBox), BarSize);

	const FLinearColor HoverFill = HoverFillColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [HoverFill](URectangleShape& Shape)
	{
		Shape.Fill.Color = HoverFill;
	});

	if (URectangleShape* BarShape = FindPart<URectangleShape>(StyledButtonParts::Bar))
	{
		// The bar grows from nothing on hover (Shape Scale keeps its round ends round).
		BarShape->bFullyRounded = true;
		Solid(*BarShape, AccentColor);
		BarShape->ShapeScale = FVector2D(1.0, 0.0);
		BarShape->InvalidateShape();

		FContextMenuShapeTarget& BarTarget = AddShapeTarget(BarShape);
		OverrideShape(BarTarget, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*BarShape, [](URectangleShape& Shape)
		{
			Shape.ShapeScale = FVector2D(1.0, 1.0);
		});
		OverrideShape(BarTarget, EContextMenuState::Pressed).Look = MakeLook<URectangleShape>(*BarShape, [](URectangleShape& Shape)
		{
			Shape.ShapeScale = FVector2D(1.0, 0.7);
		});
	}

	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(StyledButtonParts::Label))
	{
		FSlateFontInfo LabelFont = LabelBlock->GetFont();
		LabelFont.Size = FontSize;
		LabelBlock->SetFont(LabelFont);
		LabelBlock->SetColorAndOpacity(FSlateColor(TextColor));
		LabelBlock->SetRenderTransform(FWidgetTransform());

		FContextMenuShapeTarget& LabelTarget = AddShapeTarget(LabelBlock);
		OverrideShape(LabelTarget, EContextMenuState::Hovered).Transform.Translation = FVector2D(HoverShift, 0.0);
		OverrideShape(LabelTarget, EContextMenuState::Pressed).Transform.Translation = FVector2D(HoverShift * 0.66, 0.0);
		AddTint(LabelBlock, TextColor, { { EContextMenuState::Hovered, HoverTextColor }, { EContextMenuState::Pressed, AccentColor } });
	}
	AddDisabledFade();
}

// =====================================================================================================
// Hold Button
// =====================================================================================================

UHoldButton::UHoldButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ButtonType = EContextMenuButtonType::Hold;
	ButtonTypeBehaviorSource = EContextMenuButtonSettingSource::Custom;
	HoldAction = EExecuteHoldAction::AfterPressDuration;
	PressDuration = 1.2f;
	bRepeatWhileHeld = false;
	bPressDurationProgressResetsOnCancelled = true;
	bPressDurationProgressResetsOnCompleted = true;
	PressProgressValue = EWidgetShapeValue::TrimEnd;
	PressProgressRange = FVector2D(0.0, 1.0);
	FillColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::Line();
	TextColor = StyledButtonTheme::Text();
}

void UHoldButton::BuildParts()
{
	UEllipseShape* TrackShape = MakePart<UEllipseShape>(StyledButtonParts::RingTrack);
	UEllipseShape* ProgressShape = MakePart<UEllipseShape>(StyledButtonParts::Progress);
	PressProgressWidgetName = ProgressShape->GetFName();

	UOverlay* RingStack = MakePart<UOverlay>(TEXT("Ring"));
	for (UWidget* RingPart : { static_cast<UWidget*>(TrackShape), static_cast<UWidget*>(ProgressShape) })
	{
		if (UOverlaySlot* PartSlot = RingStack->AddChildToOverlay(RingPart))
		{
			PartSlot->SetHorizontalAlignment(HAlign_Fill);
			PartSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	UTextBlock* LabelBlock = MakeLabel(StyledButtonParts::Label, LOCTEXT("HoldLabel", "Hold to Confirm"), 15);
	ButtonTextWidgetName = LabelBlock->GetFName();

	UHorizontalBox* Row = MakePart<UHorizontalBox>(TEXT("Row"));
	UWidget* RingSizeBox = Sized(RingStack, StyledButtonParts::RingBox);
	if (UHorizontalBoxSlot* RingSlot = Row->AddChildToHorizontalBox(RingSizeBox))
	{
		RingSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(LabelBlock))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
	}
	RememberPart(StyledButtonParts::RingTrack, TrackShape);
	RememberPart(StyledButtonParts::Progress, ProgressShape);
	RememberPart(StyledButtonParts::RingBox, RingSizeBox);
	RememberPart(StyledButtonParts::Label, LabelBlock);
	SetButtonContent(Row);
}

void UHoldButton::ApplyStyle()
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
	SetContentLayout(FMargin(16.f, 9.f, 24.f, 9.f));
	SetBoxSize(FindPart(StyledButtonParts::RingBox), FVector2D(RingSize, RingSize));

	if (UEllipseShape* TrackShape = FindPart<UEllipseShape>(StyledButtonParts::RingTrack))
	{
		Hollow(*TrackShape, RingWidth, FLinearColor(1.f, 1.f, 1.f, 0.18f));
		TrackShape->InvalidateShape();
	}
	if (UEllipseShape* ProgressShape = FindPart<UEllipseShape>(StyledButtonParts::Progress))
	{
		// Its Trim End is the Progress Widget's value: 0 to 1 while held.
		Hollow(*ProgressShape, RingWidth, AccentColor);
		ProgressShape->Stroke.Cap = EWidgetShapeLineCap::Round;
		ProgressShape->Trim.End = 0.f;
		ProgressShape->InvalidateShape();
	}

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	OverrideShape(Self, EContextMenuState::Hovered).Look = MakeLook<URectangleShape>(*Model, [Fill, Edge](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.08f);
		Shape.Stroke.Paint.Color = Lighter(Edge, 0.4f);
	});
	FContextMenuShapeStateSettings& PressedState = OverrideShape(Self, EContextMenuState::Pressed);
	PressedState.Look = MakeLook<URectangleShape>(*Model, [Fill, Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Fill * 0.8f;
		Shape.Stroke.Paint.Color = Accent;
	});
	PressedState.Transform.Scale = FVector2D(0.98, 0.98);

	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(StyledButtonParts::Label))
	{
		const FLinearColor Idle = TextColor * FLinearColor(0.75f, 0.75f, 0.8f, 1.f);
		LabelBlock->SetColorAndOpacity(FSlateColor(Idle));
		AddTint(LabelBlock, Idle, { { EContextMenuState::Hovered, TextColor }, { EContextMenuState::Pressed, TextColor } });
	}
	AddDisabledFade();
}

// =====================================================================================================
// Tile Button
// =====================================================================================================

UTileButton::UTileButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// One chosen per container; clicking the chosen one again clears it (Click Only Enables makes it stay).
	bButtonCanBeEnabled = true;
	bUnenableAllOtherGroupButtonsOnEnabled = true;
	bClickOnlyEnables = false;
	FillColor = StyledButtonTheme::Surface();
	OutlineColor = StyledButtonTheme::Line();
	RarityColor = StyledButtonTheme::Accent();
}

void UTileButton::BuildParts()
{
	UImage* IconImage = MakePart<UImage>(StyledButtonParts::Icon);
	UTextBlock* LabelBlock = MakeLabel(StyledButtonParts::Label, LOCTEXT("TileLabel", "Item"), 12);
	ButtonTextWidgetName = LabelBlock->GetFName();
	URectangleShape* RarityShape = MakePart<URectangleShape>(StyledButtonParts::Rarity);

	UOverlay* Stack = MakePart<UOverlay>(TEXT("Stack"));
	UWidget* IconSizeBox = Sized(IconImage, StyledButtonParts::IconBox);
	if (UOverlaySlot* IconSlot = Stack->AddChildToOverlay(IconSizeBox))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Center);
		IconSlot->SetVerticalAlignment(VAlign_Center);
		IconSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 16.f));
	}
	if (UOverlaySlot* LabelSlot = Stack->AddChildToOverlay(LabelBlock))
	{
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Bottom);
		LabelSlot->SetPadding(FMargin(6.f, 0.f, 6.f, 10.f));
	}
	UWidget* RaritySizeBox = Sized(RarityShape, StyledButtonParts::RarityBox);
	if (UOverlaySlot* RaritySlot = Stack->AddChildToOverlay(RaritySizeBox))
	{
		RaritySlot->SetHorizontalAlignment(HAlign_Fill);
		RaritySlot->SetVerticalAlignment(VAlign_Bottom);
		RaritySlot->SetPadding(FMargin(2.f, 0.f, 2.f, 2.f));
	}
	UWidget* TileSizeBox = Sized(Stack, StyledButtonParts::TileBox);

	RememberPart(StyledButtonParts::Icon, IconImage);
	RememberPart(StyledButtonParts::IconBox, IconSizeBox);
	RememberPart(StyledButtonParts::Label, LabelBlock);
	RememberPart(StyledButtonParts::Rarity, RarityShape);
	RememberPart(StyledButtonParts::RarityBox, RaritySizeBox);
	RememberPart(StyledButtonParts::TileBox, TileSizeBox);
	SetButtonContent(TileSizeBox);
}

void UTileButton::ApplyStyle()
{
	using namespace StyledButtonTheme;
	BeginStyle();

	const float Rounding = CornerRounding;
	const FLinearColor Fill = FillColor;
	const FLinearColor Edge = OutlineColor;
	const URectangleShape* Model = MakeBackground<URectangleShape>([Rounding, Fill, Edge](URectangleShape& Shape)
	{
		Shape.CornerRounding = Rounding;
		Solid(Shape, Fill);
		Outline(Shape, 2.f, Edge);
	});
	SetContentLayout(FMargin(0.f));
	SetBoxSize(FindPart(StyledButtonParts::TileBox), TileSize);
	SetBoxSize(FindPart(StyledButtonParts::IconBox), FVector2D(IconSize, IconSize));
	SetBoxSize(FindPart(StyledButtonParts::RarityBox), FVector2D(0.0, 4.0));

	if (URectangleShape* RarityShape = FindPart<URectangleShape>(StyledButtonParts::Rarity))
	{
		// Its lower corners follow the tile's, inside its outline.
		const double Inner = FMath::Max(Rounding - 2.f, 0.f);
		RarityShape->bFullyRounded = false;
		RarityShape->bIndividualCorners = true;
		RarityShape->CornerRadii = FVector4(0.0, 0.0, Inner, Inner);
		Solid(*RarityShape, RarityColor);
		RarityShape->SetVisibility(bShowRarity ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		RarityShape->InvalidateShape();
	}

	const FLinearColor Accent = AccentColor;
	FContextMenuShapeTarget& Self = AddShapeTarget(this);
	FContextMenuShapeStateSettings& HoveredState = OverrideShape(Self, EContextMenuState::Hovered);
	HoveredState.Look = MakeLook<URectangleShape>(*Model, [Fill, Edge](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.08f);
		Shape.Stroke.Paint.Color = Lighter(Edge, 0.4f);
	});
	HoveredState.Transform.Scale = FVector2D(HoverScale, HoverScale);
	OverrideShape(Self, EContextMenuState::Pressed).Transform.Scale = FVector2D(0.97, 0.97);
	OverrideShape(Self, EContextMenuState::Enabled).Look = MakeLook<URectangleShape>(*Model, [Fill, Accent](URectangleShape& Shape)
	{
		Shape.Fill.Color = Lighter(Fill, 0.08f);
		Shape.Stroke.Paint.Color = Accent;
		Shape.Stroke.Width = 3.f;
	});

	if (UTextBlock* LabelBlock = FindPart<UTextBlock>(StyledButtonParts::Label))
	{
		LabelBlock->SetVisibility(bShowLabel ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		LabelBlock->SetColorAndOpacity(FSlateColor(TextDim()));
		AddTint(LabelBlock, TextDim(), { { EContextMenuState::Hovered, Text() }, { EContextMenuState::Enabled, Text() } });
	}
	AddDisabledFade();
}

#undef LOCTEXT_NAMESPACE
