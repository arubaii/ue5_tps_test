// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuButton.h"

#include "NameTBD/Cards/InfoCardWidget.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/App.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Slate/SObjectWidget.h"
#include "Widgets/SWidget.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/RetainerBox.h"
#include "Components/ButtonSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "Containers/Ticker.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/GameplayStatics.h"

// ==================================================================
// Local helpers
// ==================================================================

namespace ContextMenuButtonShape
{
	constexpr float PixelTolerance = 0.05f;
	constexpr float UnitTolerance = 0.001f;
	constexpr float AngleTolerance = 0.01f;
	constexpr float ColorTolerance = 0.002f;

	float EvaluateCurve(EContextMenuInterpolationCurve Curve, float Alpha, float Overshoot)
	{
		Alpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		switch (Curve)
		{
		case EContextMenuInterpolationCurve::Linear:
			return Alpha;
		case EContextMenuInterpolationCurve::EaseIn:
			return FMath::InterpEaseIn(0.0f, 1.0f, Alpha, 2.0f);
		case EContextMenuInterpolationCurve::EaseOut:
			return FMath::InterpEaseOut(0.0f, 1.0f, Alpha, 2.0f);
		case EContextMenuInterpolationCurve::EaseInOut:
			return FMath::InterpEaseInOut(0.0f, 1.0f, Alpha, 2.0f);
		case EContextMenuInterpolationCurve::Click:
		{
			// Standard BackOut evaluation
			float t = Alpha - 1.0f;
			return (t * t * ((Overshoot + 1.0f) * t + Overshoot) + 1.0f);
		}
		default: return Alpha;
		}
	}

	/** Steps Current based on explicit Start and Target blended by CurveAlpha. */
	bool InterpVector(FVector2D& Current, const FVector2D& Start, const FVector2D& Target, float CurveAlpha, float RawAlpha, bool bInstant)
	{
		if (bInstant || RawAlpha >= 1.0f)
		{
			Current = Target;
			return true;
		}
		Current = FMath::Lerp(Start, Target, CurveAlpha);
		return false;
	}

	bool InterpAngle(float& Current, float Start, float Target, float CurveAlpha, float RawAlpha, bool bInstant)
	{
		if (bInstant || RawAlpha >= 1.0f)
		{
			Current = Target;
			return true;
		}
		Current = FMath::Lerp(Start, Target, CurveAlpha);
		return false;
	}

	bool InterpWidgetTransform(FWidgetTransform& Current, const FWidgetTransform& Start, const FWidgetTransform& Target, float CurveAlpha, float RawAlpha, bool bInstant)
	{
		// Evaluated separately (not chained with &&) so every component advances every tick.
		const bool bTranslationDone = InterpVector(Current.Translation, Start.Translation, Target.Translation, CurveAlpha, RawAlpha, bInstant);
		const bool bScaleDone = InterpVector(Current.Scale, Start.Scale, Target.Scale, CurveAlpha, RawAlpha, bInstant);
		const bool bShearDone = InterpVector(Current.Shear, Start.Shear, Target.Shear, CurveAlpha, RawAlpha, bInstant);
		const bool bAngleDone = InterpAngle(Current.Angle, Start.Angle, Target.Angle, CurveAlpha, RawAlpha, bInstant);
		return bTranslationDone && bScaleDone && bShearDone && bAngleDone;
	}

	bool IsIdentityTransform(const FWidgetTransform& Transform)
	{
		return Transform.Translation.IsNearlyZero(PixelTolerance)
			&& Transform.Scale.Equals(FVector2D(1.0, 1.0), UnitTolerance)
			&& Transform.Shear.IsNearlyZero(UnitTolerance)
			&& FMath::IsNearlyZero(Transform.Angle, AngleTolerance);
	}

	bool TransformsEqual(const FWidgetTransform& A, const FWidgetTransform& B)
	{
		return A.Translation.Equals(B.Translation, KINDA_SMALL_NUMBER)
			&& A.Scale.Equals(B.Scale, KINDA_SMALL_NUMBER)
			&& A.Shear.Equals(B.Shear, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(A.Angle, B.Angle, KINDA_SMALL_NUMBER);
	}

	bool IsZeroMargin(const FMargin& Margin)
	{
		return FMath::IsNearlyZero(Margin.Left, PixelTolerance)
			&& FMath::IsNearlyZero(Margin.Top, PixelTolerance)
			&& FMath::IsNearlyZero(Margin.Right, PixelTolerance)
			&& FMath::IsNearlyZero(Margin.Bottom, PixelTolerance);
	}

	/**
	 * Turns Extrude/Inset vectors into per-edge offsets (positive = edge moves outward).
	 * Slate's Y axis points down, so +Y (up) maps to the Top edge.
	 */
	FMargin ComputeEdgeOffsets(const FVector2D& Extrude, const FVector2D& Inset)
	{
		auto Positive = [](double Value) { return static_cast<float>(FMath::Max(Value, 0.0)); };

		FMargin Offsets;
		Offsets.Right  = Positive(Extrude.X)  - Positive(Inset.X);
		Offsets.Left   = Positive(-Extrude.X) - Positive(-Inset.X);
		Offsets.Top    = Positive(Extrude.Y)  - Positive(Inset.Y);
		Offsets.Bottom = Positive(-Extrude.Y) - Positive(-Inset.Y);
		return Offsets;
	}

	/** The smallest layout size the shape math will divide by. Below this the button has nothing to shape. */
	constexpr double MinShapeLocalSize = 0.01;

	// FWidgetTransform applies Shear -> Rotate -> Scale -> Translate, so a transform with only one field set is just
	// that one operation. Building the shape out of these keeps the Slate transform types out of this file.
	FSlateRenderTransform MakeTranslation(const FVector2D& Translation)
	{
		FWidgetTransform Transform;
		Transform.Translation = Translation;
		return Transform.ToSlateRenderTransform();
	}

	FSlateRenderTransform MakeScale(const FVector2D& Scale)
	{
		FWidgetTransform Transform;
		Transform.Scale = Scale;
		return Transform.ToSlateRenderTransform();
	}

	/** Rewrites Transform so it pivots on Pivot (in local pixels) instead of the local origin. */
	FSlateRenderTransform FoldPivot(const FSlateRenderTransform& Transform, const FVector2D& Pivot)
	{
		return MakeTranslation(-Pivot).Concatenate(Transform).Concatenate(MakeTranslation(Pivot));
	}

	FLinearColor InvertColor(const FLinearColor& Color)
	{
		return FLinearColor(1.f - Color.R, 1.f - Color.G, 1.f - Color.B, Color.A);
	}
}

namespace ContextMenuButtonTree
{
	struct FTargetableWidget
	{
		UWidget* Widget = nullptr;
		FString Path;
	};

	/**
	 * Depth-first walk of Widget and everything beneath it, regardless of Is Variable.
	 * Widgets in the same WidgetTree are identified by their plain name (names are unique per tree).
	 * If bDescendIntoUserWidgets, widgets inside a nested UserWidget are identified as
	 * "NestedUserWidgetName/InnerWidgetName" since names are only unique within their own tree.
	 */
	void Gather(UWidget* Widget, const FString& TreePrefix, bool bDescendIntoUserWidgets, TArray<FTargetableWidget>& Out, int32 Depth = 0)
	{
		static constexpr int32 MaxDepth = 128;
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		const FString Path = TreePrefix.IsEmpty() ? Widget->GetName() : (TreePrefix + TEXT("/") + Widget->GetName());

		FTargetableWidget& Entry = Out.AddDefaulted_GetRef();
		Entry.Widget = Widget;
		Entry.Path = Path;

		// UContentWidget (Button, Border, SizeBox, ...) derives from UPanelWidget, so this covers both.
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				Gather(Panel->GetChildAt(Index), TreePrefix, bDescendIntoUserWidgets, Out, Depth + 1);
			}
		}
		else if (bDescendIntoUserWidgets)
		{
			if (const UUserWidget* NestedUserWidget = Cast<UUserWidget>(Widget))
			{
				if (NestedUserWidget->WidgetTree)
				{
					Gather(NestedUserWidget->WidgetTree->RootWidget, Path, bDescendIntoUserWidgets, Out, Depth + 1);
				}
			}
		}
	}
}

const FContextMenuWidgetDesign& FContextMenuDesignTarget::GetDesignForState(EContextMenuState State, bool bEnabledMatchesHover) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return Hovered;
	case EContextMenuState::Pressed:  return Pressed;
	case EContextMenuState::Disabled: return Disabled;
	case EContextMenuState::Enabled:  return bEnabledMatchesHover ? Hovered : Enabled;
	case EContextMenuState::Normal:
	default:                          return Normal;
	}
}

namespace ContextMenuButtonDesign
{
	EContextMenuState PreviewToState(EContextMenuDesignPreview Preview)
	{
		switch (Preview)
		{
		case EContextMenuDesignPreview::Hovered:  return EContextMenuState::Hovered;
		case EContextMenuDesignPreview::Pressed:  return EContextMenuState::Pressed;
		case EContextMenuDesignPreview::Disabled: return EContextMenuState::Disabled;
		case EContextMenuDesignPreview::Enabled:  return EContextMenuState::Enabled;
		case EContextMenuDesignPreview::Normal:
		default:                                  return EContextMenuState::Normal;
		}
	}

	/** Steps Current toward Goal, or snaps. Returns true once it has arrived. */
	bool InterpScalar(float& Current, float Goal, float DeltaTime, float Speed, bool bInstant, float Tolerance)
	{
		if (bInstant || Speed <= 0.0f)
		{
			Current = Goal;
			return true;
		}

		Current = FMath::FInterpTo(Current, Goal, DeltaTime, Speed);
		if (FMath::IsNearlyEqual(Current, Goal, Tolerance))
		{
			Current = Goal;
			return true;
		}
		return false;
	}

	bool InterpColor(FLinearColor& Current, const FLinearColor& Goal, float DeltaTime, float Speed, bool bInstant, float Tolerance)
	{
		if (bInstant || Speed <= 0.0f)
		{
			Current = Goal;
			return true;
		}

		Current = FMath::CInterpTo(Current, Goal, DeltaTime, Speed);
		if (Current.Equals(Goal, Tolerance))
		{
			Current = Goal;
			return true;
		}
		return false;
	}
}

namespace ContextMenuButtonEnableGroups
{
	/**
	 * The widget itself, then every ancestor up to the top of its screen. Where a widget tree ends (the root of a User
	 * Widget), it carries on from that User Widget, so a button inside W_TabButton continues into whatever panel the
	 * W_TabButton instance was placed in.
	 */
	void BuildAncestorChain(const UWidget* Widget, TArray<const UObject*>& OutChain)
	{
		OutChain.Reset();

		const UWidget* Current = Widget;
		for (int32 Depth = 0; Current && Depth < 256; ++Depth)
		{
			OutChain.Add(Current);

			if (const UPanelWidget* Parent = Current->GetParent())
			{
				Current = Parent;
				continue;
			}

			const UWidgetTree* Tree = Cast<UWidgetTree>(Current->GetOuter());
			const UUserWidget* TreeOwner = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
			Current = (TreeOwner && TreeOwner != Current) ? TreeOwner : nullptr;
		}
	}
}

// ==================================================================
// SContextMenuSlateButton
// ==================================================================

void SContextMenuSlateButton::SetOwnerWidget(UContextMenuButton* InOwner)
{
	OwnerWidget = InOwner;
}

void SContextMenuSlateButton::SetButtonShape(const FMargin& InEdgeOffsets, const FWidgetTransform& InTransform)
{
	const bool bChanged = !(EdgeOffsets == InEdgeOffsets) || !ContextMenuButtonShape::TransformsEqual(BrushTransform, InTransform);

	EdgeOffsets = InEdgeOffsets;
	BrushTransform = InTransform;
	bHasButtonShape = !ContextMenuButtonShape::IsZeroMargin(EdgeOffsets) || !ContextMenuButtonShape::IsIdentityTransform(BrushTransform);

	// Deliberately does not touch the render transform: the repaint is the point, and the paint it triggers is what
	// commits this shape. See CommitShapeRenderTransform.
	if (bChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SContextMenuSlateButton::SetVisualState(EContextMenuState InVisualState, bool bInDesignOwnsBrush)
{
	if (!bHasVisualState || VisualState != InVisualState || bDesignOwnsBrush != bInDesignOwnsBrush)
	{
		bHasVisualState = true;
		VisualState = InVisualState;
		bDesignOwnsBrush = bInDesignOwnsBrush;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SContextMenuSlateButton::RefreshShapeRenderTransform()
{
	if (!bHasCommittedShape)
	{
		return;
	}

	UContextMenuButton* Owner = OwnerWidget.Get();
	const FWidgetTransform AuthoredTransform = Owner ? Owner->GetRenderTransform() : FWidgetTransform();
	const FVector2D AuthoredPivot = Owner ? Owner->GetRenderTransformPivot() : FVector2D(0.5, 0.5);

	// The authored half is read fresh (it is what just changed) while the shape is the committed one. Only the shape
	// is ever cancelled at paint time, so refreshing the authored half can't put the two out of step.
	CommittedAuthoredTransform = AuthoredTransform;
	CommittedAuthoredPivot = AuthoredPivot;

	WriteRenderTransform(CommittedShape, bCommittedShapeActive, CommittedLocalSize, AuthoredTransform, AuthoredPivot);
}

void SContextMenuSlateButton::WriteRenderTransform(const FSlateRenderTransform& Shape, bool bShapeActive, const FVector2D& LocalSize, const FWidgetTransform& AuthoredTransform, const FVector2D& AuthoredPivot)
{
	using namespace ContextMenuButtonShape;

	if (!bShapeActive)
	{
		// Nothing to shape: leave the widget exactly as UMG would have left it, unset transform included.
		SetRenderTransformPivot(AuthoredPivot);
		if (IsIdentityTransform(AuthoredTransform))
		{
			SetRenderTransform(TOptional<FSlateRenderTransform>());
		}
		else
		{
			SetRenderTransform(AuthoredTransform.ToSlateRenderTransform());
		}
		return;
	}

	// Both halves have their own pivot, so each is folded down to the local origin before they're composed and the
	// widget's pivot is zeroed out.
	const FSlateRenderTransform Authored = FoldPivot(AuthoredTransform.ToSlateRenderTransform(), AuthoredPivot * LocalSize);

	SetRenderTransformPivot(FVector2D::ZeroVector);
	SetRenderTransform(Shape.Concatenate(Authored));
}

FVector2D SContextMenuSlateButton::GetShapedSize(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	return FVector2D(
		FMath::Max<double>(MinShapeLocalSize, static_cast<double>(LocalSize.X) + EdgeOffsets.Left + EdgeOffsets.Right),
		FMath::Max<double>(MinShapeLocalSize, static_cast<double>(LocalSize.Y) + EdgeOffsets.Top + EdgeOffsets.Bottom));
}

FSlateRenderTransform SContextMenuSlateButton::ComputeEdgeTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	const FVector2D ShapedSize = GetShapedSize(LocalSize);

	// p -> p * Scale + Offset: the layout box mapped onto the extruded/inset box.
	FWidgetTransform EdgeTransform;
	EdgeTransform.Scale = FVector2D(
		LocalSize.X > MinShapeLocalSize ? ShapedSize.X / LocalSize.X : 1.0,
		LocalSize.Y > MinShapeLocalSize ? ShapedSize.Y / LocalSize.Y : 1.0);
	EdgeTransform.Translation = FVector2D(-EdgeOffsets.Left, -EdgeOffsets.Top);

	return EdgeTransform.ToSlateRenderTransform();
}

FSlateRenderTransform SContextMenuSlateButton::ComputeShapeTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	const FVector2D ShapedSize = GetShapedSize(LocalSize);

	// ButtonTransform pivots on the center of the shaped box, which is where the brush paint used to pivot it.
	const FVector2D ShapedCenter = FVector2D(-EdgeOffsets.Left, -EdgeOffsets.Top) + ShapedSize * 0.5;
	const FSlateRenderTransform Shaped = FoldPivot(BrushTransform.ToSlateRenderTransform(), ShapedCenter);

	return ComputeEdgeTransform(LocalSize).Concatenate(Shaped);
}

void SContextMenuSlateButton::CommitShapeRenderTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	UContextMenuButton* Owner = OwnerWidget.Get();

	// Whatever the widget itself is transformed by (authored in UMG, animated, or set from Blueprint) is the base the
	// shape is composed onto, so shaping a button never throws its own render transform away. It's read from the owner
	// rather than off the Slate widget because UWidget pushes it back down on every SynchronizeProperties, which would
	// otherwise fold an already-composed transform into itself.
	const FWidgetTransform AuthoredTransform = Owner ? Owner->GetRenderTransform() : FWidgetTransform();
	const FVector2D AuthoredPivot = Owner ? Owner->GetRenderTransformPivot() : FVector2D(0.5, 0.5);

	const bool bShapeActive = bHasButtonShape && LocalSize.X > MinShapeLocalSize && LocalSize.Y > MinShapeLocalSize;

	if (bHasCommittedShape
		&& bCommittedShapeActive == bShapeActive
		&& CommittedLocalSize.Equals(LocalSize, PixelTolerance)
		&& CommittedEdgeOffsets == EdgeOffsets
		&& TransformsEqual(CommittedBrushTransform, BrushTransform)
		&& TransformsEqual(CommittedAuthoredTransform, AuthoredTransform)
		&& CommittedAuthoredPivot.Equals(AuthoredPivot, UnitTolerance))
	{
		return;
	}

	bHasCommittedShape = true;
	bCommittedShapeActive = bShapeActive;
	CommittedLocalSize = LocalSize;
	CommittedEdgeOffsets = EdgeOffsets;
	CommittedBrushTransform = BrushTransform;
	CommittedAuthoredTransform = AuthoredTransform;
	CommittedAuthoredPivot = AuthoredPivot;

	CommittedShape = bShapeActive ? ComputeShapeTransform(LocalSize) : FSlateRenderTransform();
	CommittedShapeInverse = bShapeActive ? CommittedShape.Inverse() : FSlateRenderTransform();
	CommittedBrushSize = bShapeActive ? GetShapedSize(LocalSize) : LocalSize;
	CommittedEdgeScale = bShapeActive
		? FVector2D(CommittedBrushSize.X / LocalSize.X, CommittedBrushSize.Y / LocalSize.Y)
		: FVector2D(1.0, 1.0);

	// Writing the transform invalidates this widget, so the next paint is handed geometry built from it. Slate is
	// fine with a paint-time invalidation; it just queues the widget for the next frame.
	SContextMenuSlateButton* MutableThis = const_cast<SContextMenuSlateButton*>(this);
	MutableThis->WriteRenderTransform(CommittedShape, bShapeActive, LocalSize, AuthoredTransform, AuthoredPivot);
}

int32 SContextMenuSlateButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	UContextMenuButton* Owner = OwnerWidget.Get();

	// Is Enabled changed since the last paint (SetIsEnabled, a binding, the designer...). Unreal has no callback
	// for this, so notify the owner here, deferred to the next core tick: recoloring widgets or registering
	// timers in the middle of a paint pass is not safe.
	const bool bSelfEnabled = IsEnabled();
	if (bSelfEnabled != bLastPaintedSelfEnabled)
	{
		bLastPaintedSelfEnabled = bSelfEnabled;

		if (Owner)
		{
			TWeakObjectPtr<UContextMenuButton> WeakOwner(Owner);
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakOwner](float)
			{
				if (UContextMenuButton* StrongOwner = WeakOwner.Get())
				{
					StrongOwner->HandleSlateEnabledStateChanged();
				}
				return false; // one-shot
			}));
		}
	}

	if (!Owner)
	{
		return SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);

	// Same brush selection SButton uses: a disabled brush with DrawAs = NoDrawType means
	// "draw the normal brushes with the disabled effect" instead of a dedicated disabled brush.
	const FButtonStyle& Style = Owner->GetStyle();
	const bool bShowDisabledEffect = Style.Disabled.DrawAs == ESlateBrushDrawType::NoDrawType;

	// What stock SButton would paint, straight off Slate's own hover/press flags.
	const FSlateBrush* NativeBrush = &Style.Normal;
	if (!bEnabled && !bShowDisabledEffect)
	{
		NativeBrush = &Style.Disabled;
	}
	else if (IsPressed())
	{
		NativeBrush = &Style.Pressed;
	}
	else if (IsHovered())
	{
		NativeBrush = &Style.Hovered;
	}

	// What the owner's state machine says to paint. Once a state has been reported it decides the brush outright, so
	// the button's own state settings replace the native style rather than being painted on top of it. Pressed included:
	// Slate's press flag used to win here, which is why a Pressed design (or a Pressed shape, or a button that skips
	// Pressed because it can be enabled) could never get rid of the style's native pressed brush.
	const FSlateBrush* Brush = NativeBrush;
	if (bHasVisualState)
	{
		if (!bEnabled && !bShowDisabledEffect)
		{
			Brush = &Style.Disabled;
		}
		else if (bDesignOwnsBrush)
		{
			// A Design Target drives this button's own tint/brush/material, so the design is the whole look and the
			// style's per-state brushes would only fight it. Everything paints from the slot the design writes into.
			Brush = &Style.Normal;
		}
		else
		{
			switch (VisualState)
			{
			case EContextMenuState::Pressed:  Brush = &Style.Pressed; break;
			case EContextMenuState::Hovered:  Brush = &Style.Hovered; break;
			case EContextMenuState::Disabled: Brush = bShowDisabledEffect ? &Style.Normal : &Style.Disabled; break;
			default:                          Brush = &Style.Normal; break;
			}
		}
	}

	// Nothing to take over: paint exactly like a stock SButton, and commit so a shape that has just gone back to
	// identity comes off the render transform.
	if (!bHasButtonShape && !bCommittedShapeActive && Brush == NativeBrush)
	{
		const int32 StockLayer = SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		CommitShapeRenderTransform(AllottedGeometry.GetLocalSize());
		return StockLayer;
	}

	// AllottedGeometry is already the shaped box, because the shape rides on this widget's render transform (that is
	// what makes the hit area follow Extrude/Inset). So both draws go back through the inverse of it: the brush ends up
	// resized in unshaped local space, exactly as it was painted before, and the content ends up back where layout
	// put it, with its own hit area unmoved.
	//
	// Everything here comes from the shape the previous paint committed, never from the current Extrude/Inset. That
	// committed shape is the one on the render transform, so it is the one this geometry was built from; cancelling
	// anything else leaves the content off by however much the shape has moved on since, which is the content
	// fidgeting mid-blend. The new shape is committed at the bottom of this function, for the next paint.
	const FGeometry ContentGeometry = bCommittedShapeActive
		? AllottedGeometry.MakeChild(
			AllottedGeometry.GetLocalSize(),
			FSlateLayoutTransform(),
			CommittedShapeInverse,
			FVector2D::ZeroVector)
		: AllottedGeometry;

	if (Brush->DrawAs != ESlateBrushDrawType::NoDrawType)
	{
		FGeometry BrushGeometry = AllottedGeometry;
		if (bCommittedShapeActive)
		{
			// Undoing only the edge scale leaves ButtonTransform applied, and leaves the brush a BrushSize-sized box
			// so a Box/Border brush keeps its authored margins instead of stretching them.
			BrushGeometry = AllottedGeometry.MakeChild(
				CommittedBrushSize,
				FSlateLayoutTransform(),
				ContextMenuButtonShape::MakeScale(FVector2D(1.0 / CommittedEdgeScale.X, 1.0 / CommittedEdgeScale.Y)),
				FVector2D::ZeroVector);
		}

		const ESlateDrawEffect DrawEffects = (bShowDisabledEffect && !bEnabled) ? ESlateDrawEffect::DisabledEffect : ESlateDrawEffect::None;
		const FLinearColor Tint = Brush->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint() * Owner->GetBackgroundColor();

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, BrushGeometry.ToPaintGeometry(), Brush, DrawEffects, Tint);
	}

	// Skip SButton/SBorder's own brush draw and paint only the content, with unshaped geometry.
	// SCompoundWidget::OnPaint applies ColorAndOpacity/foreground color and paints children at LayerId + 1.
	const int32 ResultLayer = SCompoundWidget::OnPaint(Args, ContentGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bEnabled);

	// Last thing in the pass, and the only place the render transform is written: the shape for the next paint.
	CommitShapeRenderTransform(AllottedGeometry.GetLocalSize());

	return ResultLayer;
}

// ==================================================================
// UContextMenuButton
// ==================================================================

TArray<TWeakObjectPtr<UContextMenuButton>> UContextMenuButton::EnableCapableButtons;
bool UContextMenuButton::bIsEnforcingEnableGroup = false;

void UContextMenuButton::PostLoad()
{
	Super::PostLoad();

	// Convert Color Targets into tint-only Design Targets. Also covers the older layout that stored the target as a
	// UWidget pointer rather than a name.
	for (FContextMenuColorTarget& Legacy : ColorTargets)
	{
		if (Legacy.TargetWidgetName.IsNone() && Legacy.TargetWidget)
		{
			Legacy.TargetWidgetName = Legacy.TargetWidget->GetFName();
		}
		Legacy.TargetWidget = nullptr;

		FContextMenuDesignTarget& Converted = DesignTargets.AddDefaulted_GetRef();
		Converted.TargetWidgetName = Legacy.TargetWidgetName;
		Converted.DesignSource = Legacy.ColorSource;
		Converted.PresetName = Legacy.PaletteName;
		Converted.InterpolationSpeed = Legacy.ColorInterpolationSpeed;

		// Tint only: every other channel stays off, so nothing that wasn't being written before starts being written.
		Converted.bDriveTint = true;
		Converted.Normal.Tint = Legacy.NormalColor;
		Converted.Hovered.Tint = Legacy.HoverColor;
		Converted.Pressed.Tint = Legacy.PressedColor;
		Converted.Disabled.Tint = Legacy.DisabledColor;
		Converted.Enabled.Tint = Legacy.EnabledColor;
	}

	ColorTargets.Reset();

	SyncEditConditionMirrors();
}

#if WITH_EDITOR
void UContextMenuButton::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncEditConditionMirrors();
	SyncMaterialParametersEditor();
}

void UContextMenuButton::SyncMaterialParametersEditor()
{
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		if (!Target.bDriveMaterial)
		{
			continue;
		}

		UWidget* TargetWidget = FindWidgetInButtonTree(Target.TargetWidgetName);
		UMaterialInterface* DefaultMaterial = ExtractWidgetMaterialEditor(TargetWidget);

		FContextMenuWidgetDesign* const States[] = { &Target.Normal, &Target.Hovered, &Target.Pressed, &Target.Disabled, &Target.Enabled };
		for (FContextMenuWidgetDesign* Design : States)
		{
			UMaterialInterface* MatToSync = Design->Material.Get();
			
			// Inherit from Normal state if this state didn't override the material
			if (!MatToSync && Design != &Target.Normal)
			{
				MatToSync = Target.Normal.Material.Get();
				Design->Material = MatToSync; // Auto-populate in UI
			}
			
			// Fallback to the target widget's default material
			if (!MatToSync)
			{
				MatToSync = DefaultMaterial;
			}

			SyncMaterialParameters(*Design, MatToSync);
		}
	}
}

UMaterialInterface* UContextMenuButton::ExtractWidgetMaterialEditor(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}
	
	if (UImage* Image = Cast<UImage>(Widget))
	{
		return Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
	}
	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		return Cast<UMaterialInterface>(Border->Background.GetResourceObject());
	}
	if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		return Cast<UMaterialInterface>(Text->GetFont().FontMaterial);
	}
	if (URetainerBox* Retainer = Cast<URetainerBox>(Widget))
	{
		return Retainer->GetEffectMaterial();
	}
	if (UButton* Button = Cast<UButton>(Widget))
	{
		return Cast<UMaterialInterface>(Button->GetStyle().Normal.GetResourceObject());
	}
	return nullptr;
}

void UContextMenuButton::SyncMaterialParameters(FContextMenuWidgetDesign& Design, UMaterialInterface* Material)
{
	if (!Material)
	{
		if (Design.MaterialScalars.Num() > 0 || Design.MaterialVectors.Num() > 0)
		{
			Design.MaterialScalars.Empty();
			Design.MaterialVectors.Empty();
		}
		return;
	}

	TArray<FMaterialParameterInfo> ScalarInfo;
	TArray<FGuid> ScalarGuids;
	Material->GetAllScalarParameterInfo(ScalarInfo, ScalarGuids);

	TArray<FMaterialParameterInfo> VectorInfo;
	TArray<FGuid> VectorGuids;
	Material->GetAllVectorParameterInfo(VectorInfo, VectorGuids);

	// Check if Scalars need update (prevents destroying properties panel focus if topology hasn't changed)
	bool bScalarsNeedUpdate = Design.MaterialScalars.Num() != ScalarInfo.Num();
	if (!bScalarsNeedUpdate)
	{
		for (int32 i = 0; i < ScalarInfo.Num(); ++i)
		{
			if (Design.MaterialScalars[i].ParameterName != ScalarInfo[i].Name)
			{
				bScalarsNeedUpdate = true;
				break;
			}
		}
	}

	if (bScalarsNeedUpdate)
	{
		TArray<FContextMenuMaterialScalarValue> NewScalars;
		for (const FMaterialParameterInfo& Info : ScalarInfo)
		{
			FContextMenuMaterialScalarValue NewVal;
			NewVal.ParameterName = Info.Name;
			if (FContextMenuMaterialScalarValue* Existing = Design.MaterialScalars.FindByPredicate([&](const FContextMenuMaterialScalarValue& Val) { return Val.ParameterName == Info.Name; }))
			{
				NewVal.Value = Existing->Value;
			}
			else
			{
				float DefaultVal = 0.0f;
				if (Material->GetScalarParameterDefaultValue(Info, DefaultVal))
				{
					NewVal.Value = DefaultVal;
				}
			}
			NewScalars.Add(NewVal);
		}
		Design.MaterialScalars = NewScalars;
	}

	// Check if Vectors need update
	bool bVectorsNeedUpdate = Design.MaterialVectors.Num() != VectorInfo.Num();
	if (!bVectorsNeedUpdate)
	{
		for (int32 i = 0; i < VectorInfo.Num(); ++i)
		{
			if (Design.MaterialVectors[i].ParameterName != VectorInfo[i].Name)
			{
				bVectorsNeedUpdate = true;
				break;
			}
		}
	}

	if (bVectorsNeedUpdate)
	{
		TArray<FContextMenuMaterialVectorValue> NewVectors;
		for (const FMaterialParameterInfo& Info : VectorInfo)
		{
			FContextMenuMaterialVectorValue NewVal;
			NewVal.ParameterName = Info.Name;
			if (FContextMenuMaterialVectorValue* Existing = Design.MaterialVectors.FindByPredicate([&](const FContextMenuMaterialVectorValue& Val) { return Val.ParameterName == Info.Name; }))
			{
				NewVal.Value = Existing->Value;
			}
			else
			{
				FLinearColor DefaultVal = FLinearColor::White;
				if (Material->GetVectorParameterDefaultValue(Info, DefaultVal))
				{
					NewVal.Value = DefaultVal;
				}
			}
			NewVectors.Add(NewVal);
		}
		Design.MaterialVectors = NewVectors;
	}
}
#endif

void UContextMenuButton::SyncEditConditionMirrors()
{
	// Only the Enabled state is ever greyed/hidden; the other states always apply.
	EnabledShape.bIsApplicable = bButtonCanBeEnabled && !bEnabledMatchesHoverShape;

	const bool bEnabledDesignApplicable = bButtonCanBeEnabled && !bEnabledMatchesHoverDesign;
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		Target.bEnabledDesignApplicable = bEnabledDesignApplicable;

		FContextMenuWidgetDesign* const States[] = { &Target.Normal, &Target.Hovered, &Target.Pressed, &Target.Disabled, &Target.Enabled };
		for (FContextMenuWidgetDesign* Design : States)
		{
			const bool bIsCustomDesign = Target.DesignSource == EContextMenuButtonSettingSource::Custom;
			Design->bTintApplicable = Target.bDriveTint && bIsCustomDesign;
			Design->bOpacityApplicable = Target.bDriveOpacity && bIsCustomDesign;
			Design->bBrushApplicable = Target.bDriveBrush && bIsCustomDesign;
			Design->bMaterialApplicable = Target.bDriveMaterial && bIsCustomDesign;
			Design->bSoundApplicable = Target.bDriveSound && bIsCustomDesign;
		}
	}
}

void UContextMenuButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// UWidget::SynchronizeProperties has just pushed the authored render transform onto the Slate button, which wipes
	// the shape composed into it (and with it the shaped hit area). Put it back before anything can be clicked.
	if (TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
	{
		SlateButton->RefreshShapeRenderTransform();
	}

	SyncEditConditionMirrors();

	if (!bHasBoundClickHandler)
	{
		OnHovered.AddUniqueDynamic(this, &UContextMenuButton::HandleHovered);
		OnUnhovered.AddUniqueDynamic(this, &UContextMenuButton::HandleUnhovered);
		OnClicked.AddUniqueDynamic(this, &UContextMenuButton::HandleClicked);
		OnPressed.AddUniqueDynamic(this, &UContextMenuButton::HandlePressed);
		OnReleased.AddUniqueDynamic(this, &UContextMenuButton::HandleReleased);
		bHasBoundClickHandler = true;
	}

	if (!IsDesignTime())
	{
		const bool bNewlyRegistered = UpdateEnableGroupRegistration();

		// Seed the runtime enabled state once; later rebuilds must not reset it.
		const bool bIsSeeding = !bHasInitializedEnabledState;
		if (bIsSeeding)
		{
			bRuntimeEnabledState = bDefaultEnabledState;
			bHasInitializedEnabledState = true;

			// Several buttons in an exclusive group starting enabled: the first one built keeps it. Buttons built later
			// see it already enabled here; ones not built yet aren't registered, so they can claim it first.
			if (bRuntimeEnabledState && IsAnotherExclusiveGroupMemberEnabled())
			{
				bRuntimeEnabledState = false;
			}
		}

		// A disabled button is never enabled.
		if (!GetIsEnabled())
		{
			bRuntimeEnabledState = false;
		}

		if (bIsSeeding)
		{
			bLastReportedEnabledState = GetEnabledState();
		}
		else if (bNewlyRegistered && GetEnabledState() && IsAnotherExclusiveGroupMemberEnabled())
		{
			// Rebuilt after being away (removed from its parent and re-added) while enabled, and its group moved on
			// meanwhile. The group's current choice wins.
			CommitEnabledState(false);
		}

		if (!bHasInitializedRuntimeDesignTargets)
		{
			InitializeRuntimeDesignTargets();
			bHasInitializedRuntimeDesignTargets = true;
		}

		UpdateButtonState(/*bInstant=*/true);
	}
#if WITH_EDITOR
	else
	{
		// Design time: no state machine runs, so the Show Preview dropdowns are what drive the widgets here.
		ApplyDesignPreviews();

		if (bButtonCanBeEnabled)
		{
			// Find the parent UserWidget being edited in the UMG Designer
			if (UUserWidget* ParentScreen = GetTypedOuter<UUserWidget>())
			{
				if (ParentScreen->WidgetTree && ParentScreen->WidgetTree->RootWidget)
				{
					TArray<ContextMenuButtonTree::FTargetableWidget> AllWidgets;
					ContextMenuButtonTree::Gather(ParentScreen->WidgetTree->RootWidget, FString(), true, AllWidgets);
					
					int32 CalculatedIndex = 0;
					for (const ContextMenuButtonTree::FTargetableWidget& Target : AllWidgets)
					{
						if (UContextMenuButton* OtherButton = Cast<UContextMenuButton>(Target.Widget))
						{
							if (!OtherButton->bButtonCanBeEnabled)
							{
								continue;
							}

							bool bIsSameGroup = false;

							// Named Group Check
							if (!ButtonGroup.IsNone() && OtherButton->ButtonGroup == ButtonGroup)
							{
								bIsSameGroup = true;
							}
							// Container Group Check
							else if (ButtonGroup.IsNone() && OtherButton->ButtonGroup.IsNone() && OtherButton->GetParent() == GetParent()) 
							{
								bIsSameGroup = true;
							}

							if (bIsSameGroup)
							{
								if (OtherButton == this)
								{
									GroupIndex = CalculatedIndex;
									break;
								}
								CalculatedIndex++;
							}
						}
					}
				}
			}
		}
	}
#endif

	TEnumAsByte<EMouseCursor::Type> ActualHoverCursor = ButtonHoverCursor;
	if (CursorSettingSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
		{
			ActualHoverCursor = Settings->DefaultHoverCursor;
		}
	}
	SetCursor(ActualHoverCursor);
}

TSharedRef<SWidget> UContextMenuButton::RebuildWidget()
{
	// Mirrors UButton::RebuildWidget, but builds SContextMenuSlateButton so the button brush can be
	// painted independently of the content. (Same pattern CommonUI uses for its SCommonButton.)
	TSharedRef<SContextMenuSlateButton> NewButton = SNew(SContextMenuSlateButton)
		.OnClicked(BIND_UOBJECT_DELEGATE(FOnClicked, SlateHandleClicked))
		.OnPressed(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandlePressed))
		.OnReleased(BIND_UOBJECT_DELEGATE(FSimpleDelegate, SlateHandleReleased))
		.OnHovered_UObject(this, &ThisClass::SlateHandleHovered)
		.OnUnhovered_UObject(this, &ThisClass::SlateHandleUnhovered)
		.ButtonStyle(&GetStyle())
		.ClickMethod(GetClickMethod())
		.TouchMethod(GetTouchMethod())
		.PressMethod(GetPressMethod())
		.IsFocusable(GetIsFocusable());

	NewButton->SetOwnerWidget(this);

	MyButton = NewButton;
	MyContextMenuSlateButton = NewButton;

	if (GetChildrenCount() > 0)
	{
		Cast<UButtonSlot>(GetContentSlot())->BuildSlot(NewButton);
	}

	if (!IsDesignTime())
	{
		BindSlateFocusChanging();
	}

	return NewButton;
}

void UContextMenuButton::ReleaseSlateResources(bool bReleaseChildren)
{
	StopPressDuration();
	bIsVisualTimerActive = false;
	bHasNavigationFocus = false;
	bHoverShapeLatched = false;
	bHoverDesignLatched = false;
	UnbindSlateFocusChanging();
	UnregisterFromEnableGroups();
	MyContextMenuSlateButton.Reset();

	Super::ReleaseSlateResources(bReleaseChildren);
}

void UContextMenuButton::BeginDestroy()
{
	UnregisterFromEnableGroups();
	Super::BeginDestroy();
}

void UContextMenuButton::BindSlateFocusChanging()
{
	if (!FSlateApplication::IsInitialized())
	{
		return; 
	}

	UnbindSlateFocusChanging();
	SlateFocusChangingHandle = FSlateApplication::Get().OnFocusChanging().AddUObject(this, &UContextMenuButton::HandleSlateFocusChanging);
}

void UContextMenuButton::UnbindSlateFocusChanging()
{
	if (SlateFocusChangingHandle.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnFocusChanging().Remove(SlateFocusChangingHandle);
		}
		SlateFocusChangingHandle.Reset();
	}
}

void UContextMenuButton::HandleSlateFocusChanging(const FFocusEvent& FocusEvent, const FWeakWidgetPath& OldFocusedWidgetPath, const TSharedPtr<SWidget>& OldFocusedWidget, const FWidgetPath& NewFocusedWidgetPath, const TSharedPtr<SWidget>& NewFocusedWidget)
{
	if (IsDesignTime())
	{
		return;
	}

	TSharedPtr<SWidget> MySlateWidget = GetCachedWidget();
	if (!MySlateWidget.IsValid())
	{
		return;
	}

	bool bWillBeFocused = false;
	for (TSharedPtr<SWidget> Current = NewFocusedWidget; Current.IsValid(); Current = Current->GetParentWidget())
	{
		if (Current == MySlateWidget)
		{
			bWillBeFocused = true;
			break;
		}
	}

	// Only focus that arrives by keyboard/gamepad navigation counts as hover. Clicking a button also gives it
	// focus (cause = Mouse), and counting that kept the button in its Hovered shape after the pointer left,
	// while the Slate brush (which only checks real hover) had already reverted.
	const bool bNewNavigationFocus = bWillBeFocused && FocusEvent.GetCause() == EFocusCause::Navigation;
	if (bNewNavigationFocus != bHasNavigationFocus)
	{
		bHasNavigationFocus = bNewNavigationFocus;
		UpdateButtonState();
	}
}

void UContextMenuButton::SetOwningCard(UInfoCardWidget* InCard)
{
	OwningCard = InCard;
}

UInfoCardWidget* UContextMenuButton::GetOwningCard()
{
	if (UInfoCardWidget* Cached = OwningCard.Get())
	{
		return Cached;
	}

	UInfoCardWidget* Found = ResolveOwningCard();
	if (Found)
	{
		OwningCard = Found;
	}

	return Found;
}

UInfoCardWidget* UContextMenuButton::ResolveOwningCard() const
{
	if (UInfoCardWidget* Direct = GetTypedOuter<UInfoCardWidget>())
	{
		return Direct;
	}

	{
		const UObject* Current = this;
		for (int32 Depth = 0; Depth < MaxOwnerSearchDepth; ++Depth)
		{
			UUserWidget* OuterUserWidget = Current->GetTypedOuter<UUserWidget>();
			if (!OuterUserWidget)
			{
				break;
			}

			if (UInfoCardWidget* Card = Cast<UInfoCardWidget>(OuterUserWidget))
			{
				return Card;
			}

			Current = OuterUserWidget;
		}
	}

	{
		TSharedPtr<SWidget> Current = GetCachedWidget();
		for (int32 Depth = 0; Current.IsValid() && Depth < MaxOwnerSearchDepth; ++Depth)
		{
			static const FName ObjectWidgetType(TEXT("SObjectWidget"));
			if (Current->GetType() == ObjectWidgetType)
			{
				const SObjectWidget& ObjectWidget = static_cast<const SObjectWidget&>(*Current);
				if (UInfoCardWidget* Card = Cast<UInfoCardWidget>(ObjectWidget.GetWidgetObject()))
				{
					return Card;
				}
			}

			Current = Current->GetParentWidget();
		}
	}

	return nullptr;
}

void UContextMenuButton::HandleHovered()
{
	UpdateButtonState();
}

void UContextMenuButton::HandleUnhovered()
{
	// Cursor left: the next hover may show Hovered shape/designs again.
	bHoverShapeLatched = false;
	bHoverDesignLatched = false;

	UpdateButtonState();
}

void UContextMenuButton::HandleClicked()
{
	if (IsDesignTime() || bIsOnCooldown)
	{
		return;
	}

	if (ButtonType == EContextMenuButtonType::Press)
	{
		EExecutePressAction ActualPressAction = PressAction;
		if (ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::UseProjectSettings)
		{
			if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
			{
				ActualPressAction = Settings->DefaultPressAction;
			}
		}

		if (ActualPressAction == EExecutePressAction::OnRelease)
		{
			ExecuteButtonAction();
		}
	}
}

void UContextMenuButton::HandlePressed()
{
	// New press: Enabled State may flip again. Reset before the cooldown check, so a cooldown that ends
	// between press and release can't leave the previous press's flag behind.
	bHasToggledEnablingThisPress = false;

	if (IsDesignTime() || bIsOnCooldown)
	{
		return;
	}

	UpdateButtonState();

	TEnumAsByte<EMouseCursor::Type> ActualPressCursor = ButtonPressCursor;
	if (CursorSettingSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
		{
			ActualPressCursor = Settings->DefaultPressCursor;
		}
	}
	SetCursor(ActualPressCursor);

	EExecutePressAction ActualPressAction = PressAction;
	EExecuteHoldAction ActualHoldAction = HoldAction;
	
	float ActualPressDuration = PressDurationDelegate.IsBound() ? PressDurationDelegate.Execute() : PressDuration;
	float ActualActionExecutionFrequency = ActionExecutionFrequency;

	if (ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
		{
			ActualPressAction = Settings->DefaultPressAction;
			ActualHoldAction = Settings->DefaultHoldAction;
			ActualPressDuration = Settings->DefaultPressDuration;
			ActualActionExecutionFrequency = Settings->DefaultActionExecutionFrequency;
		}
	}

	if (ButtonType == EContextMenuButtonType::Press)
	{
		if (ActualPressAction == EExecutePressAction::Immediate)
		{
			ExecuteButtonAction();
		}
	}
	else if (ButtonType == EContextMenuButtonType::Hold)
	{
		if (UWorld* World = GetWorld())
		{
			StopPressDuration();
			World->GetTimerManager().ClearTimer(HoldFrequencyTimerHandle);
			
			CurrentHoldFrequency = FMath::Max(0.01f, ActualActionExecutionFrequency);
			ActivePressDuration = FMath::Max(0.01f, ActualPressDuration);

			if (ActualHoldAction == EExecuteHoldAction::Immediate)
			{
				HoldStartTime = World->GetTimeSeconds();
				World->GetTimerManager().SetTimer(HoldFrequencyTimerHandle, this, &UContextMenuButton::OnHoldTimerTick, CurrentHoldFrequency, false);
				ExecuteButtonAction();
			}
			else if (ActualHoldAction == EExecuteHoldAction::AfterPressDuration)
			{
				BeginPressDuration();
			}
		}
	}
}

void UContextMenuButton::HandleReleased()
{
	UpdateButtonState();

	TEnumAsByte<EMouseCursor::Type> ActualHoverCursor = ButtonHoverCursor;
	if (CursorSettingSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
		{
			ActualHoverCursor = Settings->DefaultHoverCursor;
		}
	}
	SetCursor(ActualHoverCursor);

	if (IsDesignTime() || bIsOnCooldown)
	{
		return;
	}

	if (ButtonType == EContextMenuButtonType::Hold)
	{
		if (bIsPressDurationActive)
		{
			OnContextPressDurationCancelled.Broadcast();
		}

		StopPressDuration();

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(HoldFrequencyTimerHandle);
		}

		ActivePressDuration = 0.0f;
		HoldStartTime = 0.0f;
	}
}

void UContextMenuButton::BeginPressDuration()
{
	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	const uint32 Serial = ++PressDurationSerial;
	bIsPressDurationActive = true;
	PressDurationStartTime = FApp::GetCurrentTime();

	TWeakObjectPtr<UContextMenuButton> WeakThis(this);
	SlateWidget->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
		[WeakThis, Serial](double /*InCurrentTime*/, float /*InDeltaTime*/) -> EActiveTimerReturnType
		{
			UContextMenuButton* StrongThis = WeakThis.Get();
			return (StrongThis && StrongThis->TickPressDuration(Serial))
				? EActiveTimerReturnType::Continue
				: EActiveTimerReturnType::Stop;
		}));

	OnContextPressDurationProgress.Broadcast(0.0f);
}

void UContextMenuButton::StopPressDuration()
{
	bIsPressDurationActive = false;
	PressDurationStartTime = 0.0;
	++PressDurationSerial;
}

bool UContextMenuButton::TickPressDuration(uint32 Serial)
{
	if (!bIsPressDurationActive || Serial != PressDurationSerial)
	{
		return false;
	}

	if (GetElapsedPressTime() >= ActivePressDuration)
	{
		StartHoldFrequencyTimer();
		return false;
	}

	OnContextPressDurationProgress.Broadcast(GetPressProgress());
	return true;
}

void UContextMenuButton::StartHoldFrequencyTimer()
{
	StopPressDuration();
	OnContextPressDurationProgress.Broadcast(1.0f);
	OnContextProgressDurationCompleted.Broadcast();

	if (UWorld* World = GetWorld())
	{
		HoldStartTime = World->GetTimeSeconds();
		World->GetTimerManager().SetTimer(HoldFrequencyTimerHandle, this, &UContextMenuButton::OnHoldTimerTick, CurrentHoldFrequency, false);
	}

	ExecuteButtonAction();
}

void UContextMenuButton::OnHoldTimerTick()
{
	float ActualAcceleration = ActionExecutionFrequencyAcceleration;
	float ActualCap = ActionExecutionFrequencyCap;

	if (ButtonTypeBehaviorSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		if (const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get())
		{
			ActualAcceleration = Settings->DefaultActionExecutionFrequencyAcceleration;
			ActualCap = Settings->DefaultActionExecutionFrequencyCap;
		}
	}

	CurrentHoldFrequency = FMath::Max(ActualCap, CurrentHoldFrequency - ActualAcceleration);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HoldFrequencyTimerHandle, this, &UContextMenuButton::OnHoldTimerTick, CurrentHoldFrequency, false);
	}

	ExecuteButtonAction();
}

void UContextMenuButton::ExecuteButtonAction()
{
	if (bIsOnCooldown)
	{
		return;
	}

	UInfoCardWidget* Card = bShouldDestroyCard ? GetOwningCard() : nullptr;

	// Flip before broadcasting, so bEnabling (and Get Enabled State inside the event) is the state being entered.
	bool bEnabling = false;
	if (bButtonCanBeEnabled)
	{
		if (!bHasToggledEnablingThisPress)
		{
			bHasToggledEnablingThisPress = true;
			CommitEnabledState(!GetEnabledState());
		}
		bEnabling = GetEnabledState();
	}

	OnContextClicked.Broadcast(bShouldDestroyCard, bEnabling);

	if (bHasButtonCooldown && ButtonType == EContextMenuButtonType::Press)
	{
		bIsOnCooldown = true;
		if (bInvalidatesButton)
		{
			SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}

		StopPressDuration();
		ActivePressDuration = 0.0f;
		HoldStartTime = 0.0f;

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(HoldFrequencyTimerHandle);
			World->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UContextMenuButton::OnCooldownFinished, FMath::Max(0.01f, CooldownDuration), false);
		}
	}

	if (!bShouldDestroyCard)
	{
		return;
	}

	ActivePressDuration = 0.0f;
	HoldStartTime = 0.0f;

	StopPressDuration();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldFrequencyTimerHandle);
	}

	if (!IsValid(Card))
	{
		return;
	}

	Card->CloseCard();
}

void UContextMenuButton::OnCooldownFinished()
{
	bIsOnCooldown = false;

	if (bInvalidatesButton)
	{
		SetVisibility(ESlateVisibility::Visible);
	}
}

float UContextMenuButton::GetElapsedPressTime() const
{
	if (bIsPressDurationActive)
	{
		return static_cast<float>(FMath::Max(0.0, FApp::GetCurrentTime() - PressDurationStartTime));
	}
	return 0.0f;
}

float UContextMenuButton::GetPressProgress() const
{
	if (ActivePressDuration > 0.0f)
	{
		return FMath::Clamp(GetElapsedPressTime() / ActivePressDuration, 0.0f, 1.0f);
	}
	return 0.0f;
}

float UContextMenuButton::GetElapsedHoldTime() const
{
	if (HoldStartTime > 0.0f)
	{
		if (UWorld* World = GetWorld())
		{
			return World->GetTimeSeconds() - HoldStartTime;
		}
	}
	return 0.0f;
}

// ------------------------------------------------------------------
// Visuals
// ------------------------------------------------------------------

void UContextMenuButton::UpdateButtonState(bool bInstant)
{
	if (IsDesignTime())
	{
		return;
	}

	// Safety net for the re-hover latches: once nothing is hovering/focusing the button, they're done.
	if (!IsHovered() && !bHasNavigationFocus)
	{
		bHoverShapeLatched = false;
		bHoverDesignLatched = false;
	}

	const EContextMenuState NewShapeState = ResolveButtonState(/*bForDesign=*/false);
	const EContextMenuState NewDesignState = ResolveButtonState(/*bForDesign=*/true);

	// The style's own brushes are a shape effect too: they follow the resolved shape state, not Slate's raw
	// hover/press flags, so this button's state settings win over the native button style.
	PushVisualStateToSlate(NewShapeState);

	if (bInstant)
	{
		CurrentShapeState = NewShapeState;
		CurrentDesignState = NewDesignState;
		ApplyTransitions(0.0f, /*bInstant=*/true);
		return;
	}

	if (CurrentShapeState != NewShapeState || CurrentDesignState != NewDesignState)
	{
		if (CurrentShapeState != NewShapeState)
		{
			StartButtonTransform = CurrentButtonTransform;
			StartContentTransform = CurrentContentTransform;
			StartExtrude = CurrentExtrude;
			StartInset = CurrentInset;
			TransformTransitionAlpha = 0.0f;
			SizeTransitionAlpha = 0.0f;
			CurrentShapeState = NewShapeState;
		}

		CurrentDesignState = NewDesignState;
		StartVisualUpdateTimer();
	}
}

EContextMenuState UContextMenuButton::ResolveButtonState(bool bForDesign) const
{
	// Priority: Disabled > Pressed (buttons that can be enabled only) > Hovered > Enabled > Normal.
	// Pressed/Hovered use the same checks as the Slate brush, so content and background agree.
	if (!IsWidgetEnabledForVisuals())
	{
		return EContextMenuState::Disabled;
	}

	// A button that can be enabled skips Pressed. Its click changes the state, and passing through Pressed on the way
	// made the shapes/designs blend Hovered -> Pressed -> Enabled. Without it they blend straight to the click result.
	if (IsPressed() && !bButtonCanBeEnabled)
	{
		return EContextMenuState::Pressed;
	}

	const bool bIsEnabled = GetEnabledState();

	if (IsHovered() || bHasNavigationFocus)
	{
		const bool bAllowHoverWhileEnabled = bForDesign ? bHoverDesignWhileEnabled : bHoverShapeWhileEnabled;
		const bool bLatched = bForDesign ? bHoverDesignLatched : bHoverShapeLatched;

		if (!bLatched && (!bIsEnabled || bAllowHoverWhileEnabled))
		{
			return EContextMenuState::Hovered;
		}
	}

	return bIsEnabled ? EContextMenuState::Enabled : EContextMenuState::Normal;
}

void UContextMenuButton::RefreshButtonState(bool bInstant)
{
	if (IsDesignTime())
	{
		return;
	}

	InitializeRuntimeDesignTargets();
	bHasInitializedRuntimeDesignTargets = true;

	if (bInstant)
	{
		UpdateButtonState(/*bInstant=*/true);
	}
	else
	{
		UpdateButtonState();
		StartVisualUpdateTimer(); // state may be unchanged, but its targets may not be
	}
}

void UContextMenuButton::StartVisualUpdateTimer()
{
	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!bIsVisualTimerActive && SlateWidget.IsValid())
	{
		bIsVisualTimerActive = true;
		SlateWidget->RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateUObject(this, &UContextMenuButton::TickVisuals));
	}
}

EActiveTimerReturnType UContextMenuButton::TickVisuals(double InCurrentTime, float InDeltaTime)
{
	if (IsDesignTime() || ApplyTransitions(InDeltaTime, /*bInstant=*/false))
	{
		bIsVisualTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	return EActiveTimerReturnType::Continue;
}

const FContextMenuShapePreset* UContextMenuButton::FindActiveShapePreset() const
{
	if (ShapeSettingSource != EContextMenuButtonSettingSource::UseProjectSettings)
	{
		return nullptr;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	return Settings ? Settings->FindContextMenuShapePreset(ShapePresetName) : nullptr;
}

const FContextMenuShapeStateSettings& UContextMenuButton::GetShapeForState(EContextMenuState State, const FContextMenuShapePreset* Preset) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return Preset ? Preset->HoveredShape : HoveredShape;
	case EContextMenuState::Pressed:  return Preset ? Preset->PressedShape : PressedShape;
	case EContextMenuState::Disabled: return Preset ? Preset->DisabledShape : DisabledShape;
	case EContextMenuState::Enabled:
		if (bEnabledMatchesHoverShape)
		{
			return Preset ? Preset->HoveredShape : HoveredShape;
		}
		return Preset ? Preset->EnabledShape : EnabledShape;
	case EContextMenuState::Normal:
	default:                          return Preset ? Preset->NormalShape : NormalShape;
	}
}

bool UContextMenuButton::ApplyTransitions(float DeltaTime, bool bInstant)
{
	using namespace ContextMenuButtonShape;

	// Resolved every step, so editing a preset in Project Settings during PIE is picked up on the next transition.
	const FContextMenuShapePreset* Preset = FindActiveShapePreset();
	const FContextMenuShapeStateSettings& TargetShape = GetShapeForState(CurrentShapeState, Preset);
	
	const float TransformSpeed = Preset ? Preset->TransformInterpolationSpeed : TransformInterpolationSpeed;
	const EContextMenuInterpolationCurve TransformCurve = Preset ? Preset->TransformInterpolationCurve : TransformInterpolationCurve;
	const float ActualTransformOvershoot = Preset ? Preset->TransformOvershoot : TransformOvershoot;

	const float SizeSpeed = Preset ? Preset->SizeInterpolationSpeed : SizeInterpolationSpeed;
	const EContextMenuInterpolationCurve SizeCurve = Preset ? Preset->SizeInterpolationCurve : SizeInterpolationCurve;
	const float ActualSizeOvershoot = Preset ? Preset->SizeOvershoot : SizeOvershoot;

	if (!bInstant)
	{
		if (TransformSpeed > 0.0f)
			TransformTransitionAlpha = FMath::Clamp(TransformTransitionAlpha + (DeltaTime * TransformSpeed), 0.0f, 1.0f);
		else
			TransformTransitionAlpha = 1.0f;

		if (SizeSpeed > 0.0f)
			SizeTransitionAlpha = FMath::Clamp(SizeTransitionAlpha + (DeltaTime * SizeSpeed), 0.0f, 1.0f);
		else
			SizeTransitionAlpha = 1.0f;
	}
	else
	{
		TransformTransitionAlpha = 1.0f;
		SizeTransitionAlpha = 1.0f;
	}

	const float TCurveAlpha = EvaluateCurve(TransformCurve, TransformTransitionAlpha, ActualTransformOvershoot);
	const float SCurveAlpha = EvaluateCurve(SizeCurve, SizeTransitionAlpha, ActualSizeOvershoot);

	// --- Button only (painted on the brush by SContextMenuSlateButton; content and layout untouched) ---
	const bool bButtonTransformDone = InterpWidgetTransform(CurrentButtonTransform, StartButtonTransform, TargetShape.ButtonTransform, TCurveAlpha, TransformTransitionAlpha, bInstant);
	const bool bExtrudeDone = InterpVector(CurrentExtrude, StartExtrude, TargetShape.Extrude, SCurveAlpha, SizeTransitionAlpha, bInstant);
	const bool bInsetDone = InterpVector(CurrentInset, StartInset, TargetShape.Inset, SCurveAlpha, SizeTransitionAlpha, bInstant);
	PushButtonShapeToSlate();

	// --- Content only ---
	bool bContentTransformDone = true;
	if (UWidget* Content = GetContent())
	{
		bContentTransformDone = InterpWidgetTransform(CurrentContentTransform, StartContentTransform, TargetShape.ContentTransform, TCurveAlpha, TransformTransitionAlpha, bInstant);
		Content->SetRenderTransform(CurrentContentTransform);
	}

	// --- Design targets (tint, opacity, brush, material, sounds, parameters) ---
	bool bDesignsDone = true;
	for (FContextMenuRuntimeDesignTarget& Target : RuntimeDesignTargets)
	{
		bDesignsDone &= ApplyDesignTarget(Target, CurrentDesignState, DeltaTime, bInstant);
	}

	return bButtonTransformDone && bExtrudeDone && bInsetDone && bContentTransformDone && bDesignsDone;
}

void UContextMenuButton::PushButtonShapeToSlate()
{
	if (TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
	{
		SlateButton->SetButtonShape(ContextMenuButtonShape::ComputeEdgeOffsets(CurrentExtrude, CurrentInset), CurrentButtonTransform);
	}
}

void UContextMenuButton::PushVisualStateToSlate(EContextMenuState ShapeState)
{
	if (TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
	{
		SlateButton->SetVisualState(GetBrushStateForShapeState(ShapeState), DoesDesignDriveButtonBrush());
	}
}

EContextMenuState UContextMenuButton::GetBrushStateForShapeState(EContextMenuState ShapeState) const
{
	// A button style has no Enabled brush, so Enabled borrows Hovered's (with Enabled Matches Hover Shape) or falls
	// back to Normal. Everything else maps one to one.
	if (ShapeState == EContextMenuState::Enabled)
	{
		return bEnabledMatchesHoverShape ? EContextMenuState::Hovered : EContextMenuState::Normal;
	}

	return ShapeState;
}

bool UContextMenuButton::DoesDesignDriveButtonBrush() const
{
	// Runtime: the targets are already resolved to widgets.
	for (const FContextMenuRuntimeDesignTarget& Target : RuntimeDesignTargets)
	{
		if (Target.Widget.Get() != this || !DesignTargets.IsValidIndex(Target.AuthoredIndex))
		{
			continue;
		}

		const FContextMenuDesignTarget& Authored = DesignTargets[Target.AuthoredIndex];
		if (Authored.bDriveTint || Authored.bDriveBrush || Authored.bDriveMaterial)
		{
			return true;
		}
	}

	// Design time (and before the targets are resolved): match on the stored name instead. A target on the button
	// itself is stored as the button's own name, since it's the root of the gathered tree.
	if (RuntimeDesignTargets.Num() == 0)
	{
		const FString MyName = GetName();
		for (const FContextMenuDesignTarget& Authored : DesignTargets)
		{
			if ((Authored.bDriveTint || Authored.bDriveBrush || Authored.bDriveMaterial)
				&& Authored.TargetWidgetName.ToString().Equals(MyName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
	}

	return false;
}

// ------------------------------------------------------------------
// Colors / Design
// ------------------------------------------------------------------

void UContextMenuButton::InitializeRuntimeDesignTargets()
{
	using namespace ContextMenuButtonDesign;
	RuntimeDesignTargets.Reset();

	for (int32 Index = 0; Index < DesignTargets.Num(); ++Index)
	{
		const FContextMenuDesignTarget& Authored = DesignTargets[Index];

		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			continue;
		}

		FContextMenuRuntimeDesignTarget& Entry = RuntimeDesignTargets.AddDefaulted_GetRef();
		Entry.Widget = Widget;
		Entry.AuthoredIndex = Index;

		// Each channel starts from what the widget currently shows, so the first transition blends out of the
		// authored look rather than snapping to the Normal design first.
		Entry.CurrentTint = ExtractWidgetColor(Widget);
		Entry.CurrentOpacity = Widget->GetRenderOpacity();

		// Material parameters have no reliable "current" to read back per parameter, so they start at the Normal design.
		if (Authored.bDriveMaterial)
		{
			for (const FContextMenuMaterialScalarValue& Scalar : Authored.Normal.MaterialScalars)
			{
				if (!Scalar.ParameterName.IsNone())
				{
					Entry.CurrentScalars.Add(Scalar.ParameterName, Scalar.Value);
				}
			}
			for (const FContextMenuMaterialVectorValue& Vector : Authored.Normal.MaterialVectors)
			{
				if (!Vector.ParameterName.IsNone())
				{
					Entry.CurrentVectors.Add(Vector.ParameterName, Vector.Value);
				}
			}
		}
	}
}

bool UContextMenuButton::ApplyDesignTarget(FContextMenuRuntimeDesignTarget& Target, EContextMenuState State, float DeltaTime, bool bInstant)
{
	using namespace ContextMenuButtonDesign;
	using namespace ContextMenuButtonShape;

	UWidget* Widget = Target.Widget.Get();
	if (!Widget)
	{
		return true;
	}

	if (!DesignTargets.IsValidIndex(Target.AuthoredIndex))
	{
		return true; // the array shrank at runtime; RefreshButtonState rebuilds these
	}

	// Read live, so editing a design (or a preset) during PIE is picked up on the next transition.
	const FContextMenuDesignTarget& Authored = DesignTargets[Target.AuthoredIndex];
	const FContextMenuWidgetDesign* DesignPtr = &Authored.GetDesignForState(State, bEnabledMatchesHoverDesign);
	float Speed = Authored.InterpolationSpeed;

	if (Authored.DesignSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
		if (const FContextMenuDesignPreset* Preset = Settings ? Settings->FindContextMenuDesignPreset(Authored.PresetName) : nullptr)
		{
			Speed = Preset->InterpolationSpeed;
			switch (State)
			{
			case EContextMenuState::Hovered:  DesignPtr = &Preset->Hovered; break;
			case EContextMenuState::Pressed:  DesignPtr = &Preset->Pressed; break;
			case EContextMenuState::Disabled: DesignPtr = &Preset->Disabled; break;
			case EContextMenuState::Enabled:  DesignPtr = bEnabledMatchesHoverDesign ? &Preset->Hovered : &Preset->Enabled; break;
			case EContextMenuState::Normal:
			default:                          DesignPtr = &Preset->Normal; break;
			}
		}
	}

	const FContextMenuWidgetDesign& Design = *DesignPtr;
	bool bDone = true;

	if (Authored.bDriveTint)
	{
		bDone &= InterpColor(Target.CurrentTint, Design.Tint, DeltaTime, Speed, bInstant, ColorTolerance);
		ApplyColorToWidget(Widget, Target.CurrentTint);
	}

	if (Authored.bDriveOpacity)
	{
		bDone &= InterpScalar(Target.CurrentOpacity, Design.Opacity, DeltaTime, Speed, bInstant, ColorTolerance);
		Widget->SetRenderOpacity(Target.CurrentOpacity);
	}

	// Discrete: a texture can't be blended, so it swaps once, on the frame the state changes.
	if (Authored.bDriveBrush && (!Target.bHasAppliedBrush || Target.LastBrushState != State))
	{
		Target.bHasAppliedBrush = true;
		Target.LastBrushState = State;

		if (Design.BrushTexture)
		{
			ApplyBrushTextureToWidget(Widget, Design.BrushTexture);
		}
	}

	// Discrete: Sound only plays exactly on the frame the state changes.
	if (Authored.bDriveSound && (!Target.bHasAppliedSound || Target.LastSoundState != State))
	{
		Target.bHasAppliedSound = true;
		Target.LastSoundState = State;

		if (Design.Sound)
		{
			if (UWorld* World = Widget->GetWorld())
			{
				UGameplayStatics::PlaySound2D(World, Design.Sound);
			}
		}
	}

	if (Authored.bDriveMaterial)
	{
		// Discrete: material swaps once, on the frame the state changes.
		if (!Target.bHasAppliedMaterial || Target.LastMaterialState != State)
		{
			Target.bHasAppliedMaterial = true;
			Target.LastMaterialState = State;

			if (Design.Material)
			{
				ApplyMaterialToWidget(Widget, Design.Material);
				
				// Force a re-resolve of the dynamic instance since the base material has just changed.
				Target.bHasResolvedMaterial = false;
			}
		}

		// Resolved once per target per material swap: creating a Dynamic Material Instance every frame would leak.
		if (!Target.bHasResolvedMaterial)
		{
			Target.bHasResolvedMaterial = true;
			Target.CachedMaterial = ResolveWidgetMaterial(Widget);
		}

		if (UMaterialInstanceDynamic* Material = Target.CachedMaterial.Get())
		{
			for (const FContextMenuMaterialScalarValue& Scalar : Design.MaterialScalars)
			{
				if (Scalar.ParameterName.IsNone())
				{
					continue;
				}

				float& Current = Target.CurrentScalars.FindOrAdd(Scalar.ParameterName, Scalar.Value);
				bDone &= InterpScalar(Current, Scalar.Value, DeltaTime, Speed, bInstant, ColorTolerance);
				Material->SetScalarParameterValue(Scalar.ParameterName, Current);
			}

			for (const FContextMenuMaterialVectorValue& Vector : Design.MaterialVectors)
			{
				if (Vector.ParameterName.IsNone())
				{
					continue;
				}

				FLinearColor& Current = Target.CurrentVectors.FindOrAdd(Vector.ParameterName, Vector.Value);
				bDone &= InterpColor(Current, Vector.Value, DeltaTime, Speed, bInstant, ColorTolerance);
				Material->SetVectorParameterValue(Vector.ParameterName, Current);
			}
		}
	}

	return bDone;
}

UWidget* UContextMenuButton::FindWidgetInButtonTree(FName WidgetPath)
{
	if (WidgetPath.IsNone())
	{
		return nullptr;
	}

	TArray<ContextMenuButtonTree::FTargetableWidget> All;
	ContextMenuButtonTree::Gather(this, FString(), /*bDescendIntoUserWidgets=*/true, All);

	const FString PathString = WidgetPath.ToString();
	for (const ContextMenuButtonTree::FTargetableWidget& Candidate : All)
	{
		if (Candidate.Path.Equals(PathString, ESearchCase::IgnoreCase))
		{
			return Candidate.Widget;
		}
	}

	return nullptr;
}

TArray<FString> UContextMenuButton::GetColorTargetWidgetOptions() const
{
	TArray<ContextMenuButtonTree::FTargetableWidget> All;

	// Walks this button and every descendant through the panel/slot hierarchy, so Is Variable doesn't matter.
	ContextMenuButtonTree::Gather(const_cast<UContextMenuButton*>(this), FString(), /*bDescendIntoUserWidgets=*/true, All);

	TArray<FString> Options;
	Options.Reserve(All.Num());
	for (const ContextMenuButtonTree::FTargetableWidget& Entry : All)
	{
		Options.AddUnique(Entry.Path);
	}
	return Options;
}

FLinearColor UContextMenuButton::ExtractWidgetColor(UWidget* Widget)
{
	if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		return Text->GetColorAndOpacity().GetSpecifiedColor();
	}
	if (UImage* Image = Cast<UImage>(Widget))
	{
		return Image->GetColorAndOpacity();
	}
	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		return Border->GetBrushColor();
	}
	if (UButton* Button = Cast<UButton>(Widget))
	{
		return Button->GetBackgroundColor();
	}
	return FLinearColor::Transparent;
}

void UContextMenuButton::ApplyColorToWidget(UWidget* Widget, const FLinearColor& Color)
{
	if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		Text->SetColorAndOpacity(FSlateColor(Color));
	}
	else if (UImage* Image = Cast<UImage>(Widget))
	{
		Image->SetColorAndOpacity(Color);
	}
	else if (UBorder* Border = Cast<UBorder>(Widget))
	{
		Border->SetBrushColor(Color);
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		Button->SetBackgroundColor(Color);
	}
}

// ------------------------------------------------------------------
// Enabled State (not the same as Is Enabled / disabled)
// ------------------------------------------------------------------

void UContextMenuButton::ApplyBrushTextureToWidget(UWidget* Widget, UTexture2D* Texture)
{
	if (!Texture)
	{
		return;
	}

	// bMatchSize = false: the brush keeps the size and draw type it was authored with, so a swap never
	// resizes the widget mid-transition.
	if (UImage* Image = Cast<UImage>(Widget))
	{
		Image->SetBrushFromTexture(Texture, /*bMatchSize=*/false);
		return;
	}

	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		Border->SetBrushFromTexture(Texture);
		return;
	}

	if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle Style = Button->GetStyle();
		Style.Normal.SetResourceObject(Texture);
		Style.Hovered.SetResourceObject(Texture);
		Style.Pressed.SetResourceObject(Texture);
		Style.Disabled.SetResourceObject(Texture);
		Button->SetStyle(Style);
	}
}

void UContextMenuButton::ApplyMaterialToWidget(UWidget* Widget, UMaterialInterface* Material)
{
	if (!Material)
	{
		return;
	}

	if (UImage* Image = Cast<UImage>(Widget))
	{
		Image->SetBrushFromMaterial(Material);
	}
	else if (UBorder* Border = Cast<UBorder>(Widget))
	{
		Border->SetBrushFromMaterial(Material);
	}
	else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		FSlateFontInfo FontInfo = Text->GetFont();
		FontInfo.FontMaterial = Material;
		Text->SetFont(FontInfo);
	}
	else if (URetainerBox* Retainer = Cast<URetainerBox>(Widget))
	{
		Retainer->SetEffectMaterial(Material);
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle Style = Button->GetStyle();
		Style.Normal.SetResourceObject(Material);
		Style.Hovered.SetResourceObject(Material);
		Style.Pressed.SetResourceObject(Material);
		Style.Disabled.SetResourceObject(Material);
		Button->SetStyle(Style);
	}
}

UMaterialInstanceDynamic* UContextMenuButton::ResolveWidgetMaterial(UWidget* Widget)
{
	// GetDynamicMaterial creates the instance on first call and returns the same one afterward. It returns null
	// unless the brush's resource is already a material, which is why a plain texture brush can't be driven this way.
	if (UImage* Image = Cast<UImage>(Widget))
	{
		return Image->GetDynamicMaterial();
	}

	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		return Border->GetDynamicMaterial();
	}

	if (UTextBlock* Text = Cast<UTextBlock>(Widget))
	{
		return Text->GetDynamicFontMaterial();
	}
	
	if (URetainerBox* Retainer = Cast<URetainerBox>(Widget))
	{
		return Retainer->GetEffectMaterial();
	}

	if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle Style = Button->GetStyle();
		if (UMaterialInterface* Mat = Cast<UMaterialInterface>(Style.Normal.GetResourceObject()))
		{
			if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mat))
			{
				return MID;
			}
			else
			{
				UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(Mat, Button);
				Style.Normal.SetResourceObject(NewMID);
				Style.Hovered.SetResourceObject(NewMID);
				Style.Pressed.SetResourceObject(NewMID);
				Style.Disabled.SetResourceObject(NewMID);
				Button->SetStyle(Style);
				return NewMID;
			}
		}
	}

	return nullptr;
}

#if WITH_EDITOR
void UContextMenuButton::ApplyDesignPreviews()
{
	using namespace ContextMenuButtonDesign;

	ClearDesignPreviews();

	if (ShowPreview == EContextMenuDesignPreview::None)
	{
		// Nothing is previewing, so the button shows its Normal shape and brush rather than keeping whatever the
		// last preview left behind.
		const FContextMenuShapeStateSettings& Shape = GetShapeForState(EContextMenuState::Normal, FindActiveShapePreset());
		CurrentButtonTransform = Shape.ButtonTransform;
		CurrentExtrude = Shape.Extrude;
		CurrentInset = Shape.Inset;
		CurrentContentTransform = Shape.ContentTransform;

		PushButtonShapeToSlate();

		if (UWidget* Content = GetContent())
		{
			Content->SetRenderTransform(CurrentContentTransform);
		}

		PushVisualStateToSlate(EContextMenuState::Normal);
		return;
	}

	const EContextMenuState State = PreviewToState(ShowPreview);

	// The button's own state shape (Button/Content Transform, Extrude, Inset) always follows the global preview
	const FContextMenuShapeStateSettings& Shape = GetShapeForState(State, FindActiveShapePreset());
	CurrentButtonTransform = Shape.ButtonTransform;
	CurrentExtrude = Shape.Extrude;
	CurrentInset = Shape.Inset;
	CurrentContentTransform = Shape.ContentTransform;

	PushButtonShapeToSlate();

	if (UWidget* Content = GetContent())
	{
		Content->SetRenderTransform(CurrentContentTransform);
	}

	PushVisualStateToSlate(State);

	// Now apply the corresponding state to all the design targets
	for (const FContextMenuDesignTarget& Authored : DesignTargets)
	{
		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			continue;
		}

		const FContextMenuWidgetDesign* DesignPtr = &Authored.GetDesignForState(State, bEnabledMatchesHoverDesign);

		if (Authored.DesignSource == EContextMenuButtonSettingSource::UseProjectSettings)
		{
			const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
			if (const FContextMenuDesignPreset* Preset = Settings ? Settings->FindContextMenuDesignPreset(Authored.PresetName) : nullptr)
			{
				switch (State)
				{
				case EContextMenuState::Hovered:  DesignPtr = &Preset->Hovered; break;
				case EContextMenuState::Pressed:  DesignPtr = &Preset->Pressed; break;
				case EContextMenuState::Disabled: DesignPtr = &Preset->Disabled; break;
				case EContextMenuState::Enabled:  DesignPtr = bEnabledMatchesHoverDesign ? &Preset->Hovered : &Preset->Enabled; break;
				case EContextMenuState::Normal:
				default:                          DesignPtr = &Preset->Normal; break;
				}
			}
		}

		const FContextMenuWidgetDesign& Design = *DesignPtr;

		// Captured before anything is written, so the widget can be restored exactly.
		FContextMenuDesignPreviewBaseline& Baseline = DesignPreviewBaselines.AddDefaulted_GetRef();
		Baseline.Widget = Widget;
		Baseline.Tint = ExtractWidgetColor(Widget);
		Baseline.Opacity = Widget->GetRenderOpacity();

		// Image only: UBorder has no brush getter that's public across every engine version, so a previewed Border
		// brush swap isn't restored here. The designer refreshing the preview widget from the asset restores it anyway.
		if (const UImage* Image = Cast<UImage>(Widget))
		{
			UObject* Resource = Image->GetBrush().GetResourceObject();
			Baseline.BrushTexture = Cast<UTexture2D>(Resource);
			Baseline.Material = Cast<UMaterialInterface>(Resource);
		}
		else if (const URetainerBox* Retainer = Cast<URetainerBox>(Widget))
		{
			Baseline.Material = Retainer->GetEffectMaterial();
		}
		else if (const UButton* Button = Cast<UButton>(Widget))
		{
			Baseline.bIsButton = true;
			Baseline.ButtonStyle = Button->GetStyle();
			UObject* Resource = Button->GetStyle().Normal.GetResourceObject();
			Baseline.BrushTexture = Cast<UTexture2D>(Resource);
			Baseline.Material = Cast<UMaterialInterface>(Resource);
		}
		else if (const UBorder* Border = Cast<UBorder>(Widget))
		{
			UObject* Resource = Border->Background.GetResourceObject();
			Baseline.BrushTexture = Cast<UTexture2D>(Resource);
			Baseline.Material = Cast<UMaterialInterface>(Resource);
		}
		else if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			Baseline.Material = Cast<UMaterialInterface>(Text->GetFont().FontMaterial);
		}

		if (Authored.bDriveTint)
		{
			ApplyColorToWidget(Widget, Design.Tint);
		}

		if (Authored.bDriveOpacity)
		{
			Widget->SetRenderOpacity(Design.Opacity);
		}

		if (Authored.bDriveBrush)
		{
			ApplyBrushTextureToWidget(Widget, Design.BrushTexture);
		}

		if (Authored.bDriveMaterial)
		{
			if (Design.Material)
			{
				ApplyMaterialToWidget(Widget, Design.Material);
			}

			// Not restored by ClearDesignPreviews: the preview widget's material instance is rebuilt from the asset
			// whenever the designer refreshes, so parameters written here don't persist into the saved Widget Blueprint.
			if (UMaterialInstanceDynamic* Material = ResolveWidgetMaterial(Widget))
			{
				for (const FContextMenuMaterialScalarValue& Scalar : Design.MaterialScalars)
				{
					if (!Scalar.ParameterName.IsNone())
					{
						Material->SetScalarParameterValue(Scalar.ParameterName, Scalar.Value);
					}
				}
				for (const FContextMenuMaterialVectorValue& Vector : Design.MaterialVectors)
				{
					if (!Vector.ParameterName.IsNone())
					{
						Material->SetVectorParameterValue(Vector.ParameterName, Vector.Value);
					}
				}
			}
		}
	}
}

void UContextMenuButton::ClearDesignPreviews()
{
	for (const FContextMenuDesignPreviewBaseline& Baseline : DesignPreviewBaselines)
	{
		UWidget* Widget = Baseline.Widget.Get();
		if (!Widget)
		{
			continue;
		}

		ApplyColorToWidget(Widget, Baseline.Tint);
		Widget->SetRenderOpacity(Baseline.Opacity);

		if (Baseline.bIsButton)
		{
			if (UButton* Button = Cast<UButton>(Widget))
			{
				Button->SetStyle(Baseline.ButtonStyle);
			}
		}
		else if (UTexture2D* Texture = Baseline.BrushTexture.Get())
		{
			ApplyBrushTextureToWidget(Widget, Texture);
		}
		else if (UMaterialInterface* Mat = Baseline.Material.Get())
		{
			ApplyMaterialToWidget(Widget, Mat);
		}
	}

	DesignPreviewBaselines.Reset();
}
#endif

bool UContextMenuButton::IsWidgetEnabledForVisuals() const
{
	return MyButton.IsValid() ? MyButton->IsEnabled() : GetIsEnabled();
}

bool UContextMenuButton::GetEnabledState() const
{
	const bool bState = bHasInitializedEnabledState ? bRuntimeEnabledState : bDefaultEnabledState;
	return bButtonCanBeEnabled && bState && IsWidgetEnabledForVisuals();
}

void UContextMenuButton::SetEnabledState(bool bNewEnabledState)
{
	CommitEnabledState(bNewEnabledState && bButtonCanBeEnabled && IsWidgetEnabledForVisuals());
}

void UContextMenuButton::CommitEnabledState(bool bNewEnabledState)
{
	const bool bWasEnabled = GetEnabledState();

	bRuntimeEnabledState = bNewEnabledState;
	bHasInitializedEnabledState = true;

	const bool bIsEnabled = GetEnabledState();

	// Enabled -> un-enabled while the cursor (or navigation focus) is on the button: keep hover effects off until it
	// leaves and comes back, for whichever of shape/designs was being suppressed while enabled.
	if (bWasEnabled && !bIsEnabled && (IsHovered() || bHasNavigationFocus))
	{
		if (!bHoverShapeWhileEnabled)
		{
			bHoverShapeLatched = true;
		}
		if (!bHoverDesignWhileEnabled)
		{
			bHoverDesignLatched = true;
		}
	}

	UpdateButtonState();

	if (bIsEnabled && !bWasEnabled)
	{
		// Button Can Be Enabled may have been switched on at runtime after this button was built.
		UpdateEnableGroupRegistration();

		// Group-mates are un-enabled before this button reports its own change, so a Blueprint listening to both
		// sees the old choice go away before the new one arrives.
		EnforceEnableGroupExclusivity();
	}

	if (bIsEnabled != bLastReportedEnabledState)
	{
		bLastReportedEnabledState = bIsEnabled;
		OnEnabledStateChanged.Broadcast(bIsEnabled);
	}
}

void UContextMenuButton::HandleSlateEnabledStateChanged()
{
	if (IsDesignTime())
	{
		return;
	}

	// Disabling is explicit and always clears the enabled state; re-enabling does not restore it.
	if (!IsWidgetEnabledForVisuals())
	{
		CommitEnabledState(false);
		return;
	}

	UpdateButtonState();
}

// ------------------------------------------------------------------
// Enable groups
// ------------------------------------------------------------------

bool UContextMenuButton::UpdateEnableGroupRegistration()
{
	if (IsDesignTime() || HasAnyFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	if (!bButtonCanBeEnabled)
	{
		UnregisterFromEnableGroups();
		return false;
	}

	const int32 PreviousNum = EnableCapableButtons.Num();
	EnableCapableButtons.AddUnique(TWeakObjectPtr<UContextMenuButton>(this));
	return EnableCapableButtons.Num() != PreviousNum;
}

void UContextMenuButton::UnregisterFromEnableGroups()
{
	EnableCapableButtons.RemoveSwap(TWeakObjectPtr<UContextMenuButton>(this));
}

void UContextMenuButton::GatherEnableGroupMembers(TArray<UContextMenuButton*>& OutMembers)
{
	using namespace ContextMenuButtonEnableGroups;

	OutMembers.Reset();

	if (IsDesignTime() || !bButtonCanBeEnabled)
	{
		return;
	}

	// Groups are resolved on demand from the live buttons rather than stored anywhere, so buttons added, removed,
	// spawned or re-parented at runtime are always accounted for with nothing to register per container.
	const UWorld* World = GetWorld();

	TArray<UContextMenuButton*> Candidates;
	Candidates.Add(this);

	EnableCapableButtons.RemoveAll([](const TWeakObjectPtr<UContextMenuButton>& Weak) { return !Weak.IsValid(); });
	for (const TWeakObjectPtr<UContextMenuButton>& Weak : EnableCapableButtons)
	{
		UContextMenuButton* Button = Weak.Get();
		if (Button && Button != this && Button->bButtonCanBeEnabled && Button->ButtonGroup == ButtonGroup && Button->GetWorld() == World)
		{
			Candidates.Add(Button);
		}
	}

	TArray<TArray<const UObject*>> Chains;
	Chains.SetNum(Candidates.Num());
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		BuildAncestorChain(Candidates[Index], Chains[Index]);
	}

	// Named group: every button with that name on the same screen (same top-most ancestor).
	if (!ButtonGroup.IsNone())
	{
		const UObject* ScreenRoot = Chains[0].Last();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Chains[Index].Last() == ScreenRoot)
			{
				OutMembers.Add(Candidates[Index]);
			}
		}
		return;
	}

	// Container group. A button's container is its closest ancestor that also holds another candidate, i.e. the
	// closest ancestor with at least two candidates beneath it. Counting candidates per ancestor once makes that a
	// single walk up each chain, instead of comparing every pair of buttons.
	TMap<const UObject*, int32> CandidatesBeneath;
	for (const TArray<const UObject*>& Chain : Chains)
	{
		for (int32 Depth = 1; Depth < Chain.Num(); ++Depth)
		{
			++CandidatesBeneath.FindOrAdd(Chain[Depth]);
		}
	}

	auto FindContainer = [&CandidatesBeneath](const TArray<const UObject*>& Chain) -> const UObject*
	{
		for (int32 Depth = 1; Depth < Chain.Num(); ++Depth)
		{
			if (CandidatesBeneath.FindRef(Chain[Depth]) >= 2)
			{
				return Chain[Depth];
			}
		}
		return nullptr;
	};

	const UObject* MyContainer = FindContainer(Chains[0]);
	if (!MyContainer)
	{
		OutMembers.Add(this);
		return;
	}

	// Members are the buttons whose OWN container is this same one. That keeps groups symmetric: in a box holding a
	// row of two buttons plus one loose button, the row's buttons group with each other, not with the loose one.
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (FindContainer(Chains[Index]) == MyContainer)
		{
			OutMembers.Add(Candidates[Index]);
		}
	}
}

TArray<UContextMenuButton*> UContextMenuButton::GetEnableGroupButtons()
{
	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);
	return Members;
}

int32 UContextMenuButton::GetGroupIndex() const
{
	return GroupIndex;
}

TArray<UContextMenuButton*> UContextMenuButton::GetEnabledGroupButtons()
{
	TArray<UContextMenuButton*> EnabledMembers;
	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);
	
	for (UContextMenuButton* Member : Members)
	{
		if (Member && Member->GetEnabledState())
		{
			EnabledMembers.Add(Member);
		}
	}
	return EnabledMembers;
}

TArray<int32> UContextMenuButton::GetEnabledGroupIndices()
{
	TArray<int32> EnabledIndices;
	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);
	
	for (UContextMenuButton* Member : Members)
	{
		if (Member && Member->GetEnabledState())
		{
			EnabledIndices.Add(Member->GroupIndex);
		}
	}
	return EnabledIndices;
}

void UContextMenuButton::SetGroupButtonEnabledStateByIndex(int32 TargetIndex, bool bNewEnabledState)
{
	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);
	
	for (UContextMenuButton* Member : Members)
	{
		if (Member && Member->GroupIndex == TargetIndex)
		{
			Member->SetEnabledState(bNewEnabledState);
			break;
		}
	}
}

bool UContextMenuButton::IsEnableGroupExclusive(const TArray<UContextMenuButton*>& Members)
{
	return Members.ContainsByPredicate([](const UContextMenuButton* Member)
	{
		return Member && Member->bUnenableAllOtherGroupButtonsOnEnabled;
	});
}

bool UContextMenuButton::IsAnotherExclusiveGroupMemberEnabled()
{
	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);

	if (Members.Num() < 2 || !IsEnableGroupExclusive(Members))
	{
		return false;
	}

	return Members.ContainsByPredicate([this](const UContextMenuButton* Member)
	{
		return Member != this && Member->bHasInitializedEnabledState && Member->GetEnabledState();
	});
}

void UContextMenuButton::EnforceEnableGroupExclusivity()
{
	if (bIsEnforcingEnableGroup || IsDesignTime())
	{
		return;
	}

	TArray<UContextMenuButton*> Members;
	GatherEnableGroupMembers(Members);

	if (Members.Num() < 2 || !IsEnableGroupExclusive(Members))
	{
		return;
	}

	TGuardValue<bool> EnforcingGuard(bIsEnforcingEnableGroup, true);

	// Weak: un-enabling a button broadcasts On Enabled State Changed, and Blueprint handling it may remove or
	// destroy other buttons in the group before the loop reaches them.
	TArray<TWeakObjectPtr<UContextMenuButton>> Others;
	for (UContextMenuButton* Member : Members)
	{
		if (Member != this)
		{
			Others.Emplace(Member);
		}
	}

	for (const TWeakObjectPtr<UContextMenuButton>& Weak : Others)
	{
		UContextMenuButton* Other = Weak.Get();
		if (Other && Other->GetEnabledState())
		{
			Other->CommitEnabledState(false);
		}
	}
}