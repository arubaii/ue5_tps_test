// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetConstraintComponent.h"

#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Slate/WidgetTransform.h"
#include "UObject/UObjectIterator.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"
#include "NameTBD/Widgets/WidgetEditorHooks.h"

// =====================================================================================================
// SWidgetConstraintHost
// =====================================================================================================

namespace WidgetConstraintHostPrivate
{
	bool NearlySame(const FSlateRenderTransform& A, const FSlateRenderTransform& B)
	{
		float A00, A01, A10, A11;
		float B00, B01, B10, B11;
		A.GetMatrix().GetMatrix(A00, A01, A10, A11);
		B.GetMatrix().GetMatrix(B00, B01, B10, B11);
		const FVector2f TranslationA = FVector2f(A.GetTranslation());
		const FVector2f TranslationB = FVector2f(B.GetTranslation());
		return FMath::IsNearlyEqual(A00, B00, 1e-5f) && FMath::IsNearlyEqual(A01, B01, 1e-5f) && FMath::IsNearlyEqual(A10, B10, 1e-5f)
			&& FMath::IsNearlyEqual(A11, B11, 1e-5f) && FVector2f::DistSquared(TranslationA, TranslationB) < 1e-6f;
	}
}

void SWidgetConstraintHost::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		InArgs._Content.Widget
	];
}

void SWidgetConstraintHost::SetSolved(const FSlateRenderTransform& InExtra, bool bInActive, float InOpacity)
{
	const bool bMoved = bInActive != bActive || (bInActive && !WidgetConstraintHostPrivate::NearlySame(InExtra, Extra));
	const bool bFaded = SolvedOpacity != InOpacity;
	Extra = InExtra;
	bActive = bInActive;
	SolvedOpacity = InOpacity;
	if (bMoved)
	{
		// Where its child is arranged changed: under invalidation (the designer's preview, Global Invalidation) a paint alone kept
		// the child's last geometry - a value set by a Widget Animation moved nothing until the widget was rebuilt.
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	else if (bFaded)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SWidgetConstraintHost::SetDebugSegments(TArray<FVector2f>&& InSegments)
{
	if (InSegments.Num() == 0 && DebugSegments.Num() == 0)
	{
		return;
	}
	DebugSegments = MoveTemp(InSegments);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SWidgetConstraintHost::OnArrangeChildren(const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren) const
{
	const TSharedRef<SWidget>& Child = ChildSlot.GetWidget();
	const EVisibility ChildVisibility = Child->GetVisibility();
	if (!ArrangedChildren.Accepts(ChildVisibility))
	{
		return;
	}
	if (!bActive)
	{
		ArrangedChildren.AddWidget(ChildVisibility, AllottedGeometry.MakeChild(Child, AllottedGeometry.GetLocalSize(), FSlateLayoutTransform()));
		return;
	}

	// The widget's own render transform first, then the constraints' on top, both about its pivot: its slot doesn't move.
	const TOptional<FSlateRenderTransform> Own = Child->GetRenderTransformWithRespectToFlowDirection();
	const FVector2D Pivot = Child->GetRenderTransformPivotWithRespectToFlowDirection();
	const FSlateRenderTransform Combined = Own.IsSet() ? Concatenate(Own.GetValue(), Extra) : Extra;
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	ArrangedChildren.AddWidget(ChildVisibility, FArrangedWidget(Child, AllottedGeometry.MakeChild(LocalSize, FSlateLayoutTransform(), Combined, Pivot)));
}

int32 SWidgetConstraintHost::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	FWidgetStyle PaintStyle = InWidgetStyle;
	if (bActive && SolvedOpacity < 1.f)
	{
		PaintStyle.BlendColorAndOpacityTint(FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(SolvedOpacity, 0.f, 1.f)));
	}
	int32 MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, PaintStyle, bParentEnabled);

	if (DebugSegments.Num() >= 2)
	{
		// The designer's lines: what each constraint is tied to.
		const FLinearColor LineColor(1.f, 0.35f, 0.85f, 0.85f);
		++MaxLayer;
		for (int32 Index = 0; Index + 1 < DebugSegments.Num(); Index += 2)
		{
			TArray<FVector2f> Points;
			Points.Add(DebugSegments[Index]);
			Points.Add(DebugSegments[Index + 1]);
			FSlateDrawElement::MakeLines(OutDrawElements, MaxLayer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, LineColor, true, 1.f);
		}
	}
	return MaxLayer;
}

// =====================================================================================================
// Solver
// =====================================================================================================

namespace WidgetConstraintSolver
{
	TArray<TWeakObjectPtr<UWidgetConstraintComponent>>& GetComponents()
	{
		static TArray<TWeakObjectPtr<UWidgetConstraintComponent>> Components;
		return Components;
	}

	FDelegateHandle& GetTickHandle()
	{
		static FDelegateHandle Handle;
		return Handle;
	}

	/** Scale (per axis) then a turn of Degrees clockwise, as Slate's row-vector matrices write it. */
	FMatrix2x2 MakeLinear(const FVector2f& Scale, float Degrees)
	{
		float Sin = 0.f;
		float Cos = 1.f;
		FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(Degrees));
		return FMatrix2x2(Scale.X * Cos, Scale.X * Sin, -Scale.Y * Sin, Scale.Y * Cos);
	}

	/** Whether a Render Transform is away from the authored one (a Widget Animation has moved, turned or scaled it). */
	bool IsOffAuthored(const FWidgetTransform& Own, const FWidgetTransform& Authored)
	{
		return !(Own.Translation - Authored.Translation).IsNearlyZero(0.01)
			|| !FMath::IsNearlyEqual(Own.Angle, Authored.Angle, 0.01f)
			|| !(Own.Scale - Authored.Scale).IsNearlyZero(0.0001);
	}

	/** A layout transform (scale and offset) as a render transform. */
	FSlateRenderTransform ToRender(const FSlateLayoutTransform& Layout)
	{
		const float LayoutScale = Layout.GetScale();
		return FSlateRenderTransform(FMatrix2x2(LayoutScale, 0.f, 0.f, LayoutScale), Layout.TransformPoint(FVector2f::ZeroVector));
	}

	/**
	 * Where a widget is drawn with its Render Transform at Pose - its local space to desktop space, and its size: its box where it
	 * is laid out, posed about its pivot, through its parent's place on screen. With Pose its current transform this is where it
	 * is drawn this frame (its last paint's geometry would be a frame behind a widget a Widget Animation moves); with another,
	 * where it would be (a motion path's sample).
	 */
	bool ReadPosedFrame(const UWidget* Widget, const FWidgetTransform& Pose, FSlateRenderTransform& OutLocalToDesktop, FVector2f& OutSize)
	{
		const TSharedPtr<SWidget> Slate = Widget ? Widget->GetCachedWidget() : nullptr;
		const TSharedPtr<SWidget> Above = Slate.IsValid() ? Slate->GetParentWidget() : nullptr;
		if (!Slate.IsValid() || !Above.IsValid())
		{
			return false;
		}
		const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
		{
			return false;
		}
		OutSize = FVector2f(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
		const FVector2D PivotFraction = Widget->GetRenderTransformPivot();
		const FVector2f Pivot = OutSize * FVector2f(static_cast<float>(PivotFraction.X), static_cast<float>(PivotFraction.Y));
		const FSlateRenderTransform Posed = Concatenate(Concatenate(FSlateRenderTransform(-Pivot), Pose.ToSlateRenderTransform()), FSlateRenderTransform(Pivot));
		const FGeometry& AboveGeometry = Above->GetTickSpaceGeometry();
		const FSlateRenderTransform LaidOutInAbove = Concatenate(ToRender(Geometry.GetAccumulatedLayoutTransform()), ToRender(AboveGeometry.GetAccumulatedLayoutTransform().Inverse()));
		OutLocalToDesktop = Concatenate(Concatenate(Posed, LaidOutInAbove), AboveGeometry.GetAccumulatedRenderTransform());
		return true;
	}

	bool IsIdentity(const FSlateRenderTransform& Transform)
	{
		float A, B, C, D;
		Transform.GetMatrix().GetMatrix(A, B, C, D);
		const FVector2f Translation = FVector2f(Transform.GetTranslation());
		return FMath::IsNearlyEqual(A, 1.f, 1e-4f) && FMath::IsNearlyZero(B, 1e-4f) && FMath::IsNearlyZero(C, 1e-4f) && FMath::IsNearlyEqual(D, 1.f, 1e-4f)
			&& FMath::IsNearlyZero(Translation.X, 1e-3f) && FMath::IsNearlyZero(Translation.Y, 1e-3f);
	}
}

UWidgetConstraintComponent::UWidgetConstraintComponent()
{
}

void UWidgetConstraintComponent::ShutdownSolver()
{
	FDelegateHandle& Handle = WidgetConstraintSolver::GetTickHandle();
	if (Handle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnPreTick().Remove(Handle);
	}
	Handle.Reset();
	WidgetConstraintSolver::GetComponents().Reset();
}

TSharedRef<SWidget> UWidgetConstraintComponent::RebuildWidgetWithContent(const TSharedRef<SWidget> OwnerContent)
{
	TSharedRef<SWidgetConstraintHost> Host = SNew(SWidgetConstraintHost)
	[
		OwnerContent
	];
	MyHost = Host;
	bHasSolved = false;
	Register();
	return Host;
}

void UWidgetConstraintComponent::Register()
{
	// From the moment it is initialized - the designer's preview included - so a Constraint track finds it whether or not its
	// widget has been built yet.
	WidgetConstraintSolver::GetComponents().AddUnique(this);
	FDelegateHandle& Handle = WidgetConstraintSolver::GetTickHandle();
	if (!Handle.IsValid() && FSlateApplication::IsInitialized())
	{
		// Solved before Slate ticks and paints, every frame, in dependency order.
		Handle = FSlateApplication::Get().OnPreTick().AddStatic(&UWidgetConstraintComponent::SolveAll);
	}
}

#if UE_VERSION_OLDER_THAN(5, 6, 0)
bool UWidgetConstraintComponent::OnInitialize()
{
	Register();
	return true;
}
#else
void UWidgetConstraintComponent::OnInitialize()
{
	Register();
}

void UWidgetConstraintComponent::OnPreConstruct(bool bIsDesignTime)
{
	// Some owners are initialized before the component system hands the widget over: registered again here.
	Register();
}
#endif

void UWidgetConstraintComponent::BeginDestroy()
{
	WidgetConstraintSolver::GetComponents().Remove(this);
	Super::BeginDestroy();
}

UWidget* UWidgetConstraintComponent::GetConstrainedWidget() const
{
	return GetOwner().Get();
}

UWidgetConstraintComponent* UWidgetConstraintComponent::FindConstraintComponent(const UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}
	// The one whose Slate wrapper is alive first: a component left registered for the same widget (not yet collected, between
	// rebuilds) would otherwise take a Constraint track's writes while another is the one drawn.
	UWidgetConstraintComponent* Unbuilt = nullptr;
	for (const TWeakObjectPtr<UWidgetConstraintComponent>& Weak : WidgetConstraintSolver::GetComponents())
	{
		UWidgetConstraintComponent* Component = Weak.Get();
		if (Component && Component->GetOwner().Get() == Widget)
		{
			if (Component->MyHost.IsValid())
			{
				return Component;
			}
			Unbuilt = Unbuilt ? Unbuilt : Component;
		}
	}
	return Unbuilt;
}

void UWidgetConstraintComponent::GetAllComponents(TArray<UWidgetConstraintComponent*>& OutComponents)
{
	for (const TWeakObjectPtr<UWidgetConstraintComponent>& Weak : WidgetConstraintSolver::GetComponents())
	{
		if (UWidgetConstraintComponent* Component = Weak.Get())
		{
			OutComponents.Add(Component);
		}
	}
}

void UWidgetConstraintComponent::GetTargetWidgets(TArray<const UWidget*>& OutWidgets) const
{
	FWidgetConstraintContext Context;
	Context.Owner = GetOwner().Get();
	for (const UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint && Constraint->bEnabled)
		{
			Constraint->GetDependencies(Context, OutWidgets);
		}
	}
	OutWidgets.Remove(nullptr);
	OutWidgets.Remove(Context.Owner);
}

void UWidgetConstraintComponent::SolveAll(float DeltaTime)
{
	TArray<TWeakObjectPtr<UWidgetConstraintComponent>>& Registered = WidgetConstraintSolver::GetComponents();
	TArray<UWidgetConstraintComponent*> Live;
	TMap<const UWidget*, UWidgetConstraintComponent*> ByOwner;
	for (int32 Index = Registered.Num() - 1; Index >= 0; --Index)
	{
		UWidgetConstraintComponent* Component = Registered[Index].Get();
		if (!Component)
		{
			Registered.RemoveAtSwap(Index, 1, EAllowShrinking::No);
			continue;
		}
		// Kept registered without a Slate widget (not built yet, or between rebuilds): found, not solved.
		if (!Component->MyHost.IsValid())
		{
			continue;
		}
		if (const UWidget* OwnerWidget = Component->GetOwner().Get())
		{
			Live.Add(Component);
			ByOwner.Add(OwnerWidget, Component);
		}
	}
	if (Live.Num() == 0)
	{
		return;
	}

	// Depth first through what each depends on: a widget constrained to a constrained widget comes after it. A cycle is
	// broken where it closes (that link reads last frame's place).
	TArray<UWidgetConstraintComponent*> Order;
	Order.Reserve(Live.Num());
	TSet<UWidgetConstraintComponent*> Done;
	TSet<UWidgetConstraintComponent*> Visiting;
	TFunction<void(UWidgetConstraintComponent*)> Visit = [&](UWidgetConstraintComponent* Component)
	{
		if (Done.Contains(Component) || Visiting.Contains(Component))
		{
			return;
		}
		Visiting.Add(Component);
		TArray<const UWidget*> Depends;
		Component->GatherDependencies(Depends);
		for (const UWidget* Widget : Depends)
		{
			if (UWidgetConstraintComponent* const* Before = ByOwner.Find(Widget))
			{
				Visit(*Before);
			}
		}
		Visiting.Remove(Component);
		Done.Add(Component);
		Order.Add(Component);
	};
	for (UWidgetConstraintComponent* Component : Live)
	{
		Visit(Component);
	}

	for (UWidgetConstraintComponent* Component : Order)
	{
		Component->SolveFrame(DeltaTime, ByOwner);
	}
}

void UWidgetConstraintComponent::GatherDependencies(TArray<const UWidget*>& OutWidgets) const
{
	FWidgetConstraintContext Context;
	Context.Owner = GetOwner().Get();
	for (const UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint && Constraint->bEnabled)
		{
			Constraint->GetDependencies(Context, OutWidgets);
		}
	}
	// Solved after a constrained widget it is inside.
	for (const UWidget* Parent = Context.Owner ? static_cast<const UWidget*>(Context.Owner->GetParent()) : nullptr; Parent; Parent = static_cast<const UWidget*>(Parent->GetParent()))
	{
		OutWidgets.Add(Parent);
	}
}

bool UWidgetConstraintComponent::MakeContext(const FWidgetTransform& Own, float DeltaTime, const TMap<const UWidget*, UWidgetConstraintComponent*>* ByOwner,
	FWidgetConstraintContext& OutContext, const TMap<const UWidget*, FWidgetTransform>* PosedWidgets) const
{
	UWidget* OwnerWidget = GetOwner().Get();
	const TSharedPtr<SWidgetConstraintHost> Host = MyHost.Pin();
	if (!OwnerWidget || !Host.IsValid())
	{
		return false;
	}

	// Measured where the widget's box is laid out, before any transform of its own: the host is never transformed.
	const FGeometry& HostGeometry = Host->GetTickSpaceGeometry();
	const FVector2D LocalSize = HostGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return false;
	}

	OutContext.Owner = OwnerWidget;
	OutContext.Size = FVector2f(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	OutContext.HostToDesktop = HostGeometry.GetAccumulatedRenderTransform();
	OutContext.DesktopToHost = OutContext.HostToDesktop.Inverse();
	OutContext.DeltaTime = FMath::Min(DeltaTime, 0.1f);
	OutContext.bDesignTime = OwnerWidget->IsDesignTime();

	const FVector2D PivotFraction = OwnerWidget->GetRenderTransformPivot();
	OutContext.PivotPoint = OutContext.Size * FVector2f(static_cast<float>(PivotFraction.X), static_cast<float>(PivotFraction.Y));
	OutContext.Rest.Position = FVector2D(OutContext.PivotPoint.X, OutContext.PivotPoint.Y) + Own.Translation;
	OutContext.Rest.Angle = Own.Angle;
	OutContext.Rest.Scale = Own.Scale;
	OutContext.Rest.Opacity = 1.f;

	// Animated: away from where it was authored. The place the animation puts its pivot is what Follow the Keys follows.
	const FWidgetTransform Authored = ReadAuthoredTransform();
	OutContext.bAnimated = WidgetConstraintSolver::IsOffAuthored(Own, Authored);
	OutContext.AnimatedPoint = FVector2f(OutContext.Rest.Position);

	const FSlateRenderTransform DesktopToHost = OutContext.DesktopToHost;
	OutContext.ResolveWidget = [ByOwner, DesktopToHost, PosedWidgets](const UWidget* Widget, FSlateRenderTransform& OutLocalToHost, FVector2f& OutSize) -> bool
	{
		if (!Widget)
		{
			return false;
		}
		// Asked for at another pose (a motion path's sample): there, whatever its own constraints do.
		if (const FWidgetTransform* Posed = PosedWidgets ? PosedWidgets->Find(Widget) : nullptr)
		{
			FSlateRenderTransform LocalToDesktop;
			if (WidgetConstraintSolver::ReadPosedFrame(Widget, *Posed, LocalToDesktop, OutSize))
			{
				OutLocalToHost = Concatenate(LocalToDesktop, DesktopToHost);
				return true;
			}
		}
		// A constrained widget: where its constraints put it (this frame, when it came first).
		UWidgetConstraintComponent* Constrained = nullptr;
		if (ByOwner)
		{
			UWidgetConstraintComponent* const* Found = ByOwner->Find(Widget);
			Constrained = Found ? *Found : nullptr;
		}
		else
		{
			Constrained = FindConstraintComponent(Widget);
		}
		if (Constrained && Constrained->bHasSolved)
		{
			OutLocalToHost = Concatenate(Constrained->SolvedLocalToDesktop, DesktopToHost);
			OutSize = Constrained->SolvedSize;
			return true;
		}
		// Anything else at its Render Transform as it is now - a target a Widget Animation moves is followed this frame, not a
		// frame behind as its last paint would have it.
		{
			FSlateRenderTransform LocalToDesktop;
			if (WidgetConstraintSolver::ReadPosedFrame(Widget, Widget->GetRenderTransform(), LocalToDesktop, OutSize))
			{
				OutLocalToHost = Concatenate(LocalToDesktop, DesktopToHost);
				return true;
			}
		}
		const TSharedPtr<SWidget> Slate = Widget->GetCachedWidget();
		if (!Slate.IsValid())
		{
			return false;
		}
		const FGeometry& Geometry = Slate->GetTickSpaceGeometry();
		const FVector2D Size = Geometry.GetLocalSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0)
		{
			return false;
		}
		OutLocalToHost = Concatenate(Geometry.GetAccumulatedRenderTransform(), DesktopToHost);
		OutSize = FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y));
		return true;
	};
	return true;
}

bool UWidgetConstraintComponent::AnyFollowsAnimation() const
{
	return Constraints.ContainsByPredicate([](const TObjectPtr<UWidgetConstraint>& Constraint)
	{
		return Constraint && Constraint->bEnabled && Constraint->Strength > 0.f && Constraint->WhileAnimated == EWidgetConstraintWhileAnimated::FollowKeys;
	});
}

FWidgetTransform UWidgetConstraintComponent::GetAuthoredTransform(const UWidget* Widget)
{
	if (!Widget)
	{
		return FWidgetTransform();
	}
	// In the designer, the Widget Blueprint's own widget - what was last set, compiled or not; else the widget it was made from.
	if (const UWidget* Template = WidgetEditorHooks::FindDesignerTemplate(Widget))
	{
		return Template->GetRenderTransform();
	}
	const UWidget* Archetype = Cast<UWidget>(Widget->GetArchetype());
	return Archetype && Archetype != Widget ? Archetype->GetRenderTransform() : FWidgetTransform();
}

FWidgetTransform UWidgetConstraintComponent::ReadAuthoredTransform() const
{
	const UWidget* OwnerWidget = GetOwner().Get();
	if (!OwnerWidget)
	{
		return FWidgetTransform();
	}
	if (AuthoredFor.Get() != OwnerWidget)
	{
		// Looked up once per widget: the template in the designer, else the archetype.
		AuthoredFor = OwnerWidget;
		const UWidget* Template = WidgetEditorHooks::FindDesignerTemplate(OwnerWidget);
		const UWidget* Archetype = Cast<UWidget>(OwnerWidget->GetArchetype());
		AuthoredSource = Template ? Template : (Archetype != OwnerWidget ? Archetype : nullptr);
	}
	const UWidget* Source = AuthoredSource.Get();
	return Source ? Source->GetRenderTransform() : FWidgetTransform();
}

void UWidgetConstraintComponent::SolveFrame(float DeltaTime, const TMap<const UWidget*, UWidgetConstraintComponent*>& ByOwner)
{
	UWidget* OwnerWidget = GetOwner().Get();
	const TSharedPtr<SWidgetConstraintHost> Host = MyHost.Pin();
	if (!OwnerWidget || !Host.IsValid())
	{
		return;
	}

	const bool bDesignTime = OwnerWidget->IsDesignTime();
	if (bDesignTime && !bSolveInDesigner)
	{
		Host->SetSolved(FSlateRenderTransform(), false, 1.f);
		Host->SetDebugSegments(TArray<FVector2f>());
		bHasSolved = false;
		return;
	}

	const FWidgetTransform& Own = OwnerWidget->GetRenderTransform();
	FWidgetConstraintContext Context;
	if (!MakeContext(Own, DeltaTime, &ByOwner, Context))
	{
		return;
	}

	// Animated a moment ago counts: a key passing through the authored place doesn't flick a follower's target for a frame.
	SinceAnimated = Context.bAnimated ? 0.f : SinceAnimated + Context.DeltaTime;
	Context.bAnimated = SinceAnimated < 0.25f;
	const bool bFollowing = Context.bAnimated && AnyFollowsAnimation();
	if (bFollowing)
	{
		// The keyed offset is the followers' target, not added on top: the stack starts where the widget was authored.
		const FWidgetTransform Authored = ReadAuthoredTransform();
		Context.Rest.Position = FVector2D(Context.PivotPoint.X, Context.PivotPoint.Y) + Authored.Translation;
	}

	// The pointer, in the widget's layout space - the world pointer of a Diegetic Screen too. Never in the designer.
	if (!bDesignTime)
	{
		FWidgetAnimInputContext Input;
		Input.HostWidget = OwnerWidget;
		Input.MeasuredWidget = OwnerWidget;
		WidgetAnimInput::ReadPointer(Input);
		if (Input.bHasPointer && (!Input.bWorldPointer || Input.WorldPointer.bOver))
		{
			Context.bHasPointer = true;
			Context.Pointer = Context.DesktopToHost.TransformPoint(Input.PointerPosition);
			Context.bPointerOver = Input.bWorldPointer
				? WidgetAnimInput::IsPointUnder(Input, OwnerWidget, Input.PointerPosition)
				: WidgetAnimInput::IsPointerOver(OwnerWidget, Input.PointerPosition);
		}
		const bool bDown = WidgetAnimInput::IsButtonDown(Input, EKeys::LeftMouseButton);
		if (bDown && !bWasDown)
		{
			bPressStartedOver = Context.bPointerOver;
		}
		bWasDown = bDown;
		Context.bPressed = bDown && bPressStartedOver;
	}

	FWidgetConstraintPose Pose = Context.Rest;
	TArray<FVector2f> Debug;
	const bool bDrawDebug = bDesignTime && bShowInDesigner;
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (!Constraint)
		{
			continue;
		}
		Context.bFollowAnimation = Context.bAnimated && Constraint->WhileAnimated == EWidgetConstraintWhileAnimated::FollowKeys;
		Pose = Constraint->Apply(Context, Pose, Strength);
		if (bDrawDebug && Constraint->bEnabled)
		{
			Constraint->GetDebugLines(Context, Pose, Debug);
		}
	}
	Context.bFollowAnimation = false;
	SolvedPose = Pose;

	// The extra transform, about the pivot, that takes the widget from its own pose to the solved one:
	// E(x) = T + L x, with L the change of scale and turn, and T placing the pivot where the pose says.
	const FVector2f ScaleRatio(
		FMath::Abs(Own.Scale.X) > KINDA_SMALL_NUMBER ? static_cast<float>(Pose.Scale.X / Own.Scale.X) : 1.f,
		FMath::Abs(Own.Scale.Y) > KINDA_SMALL_NUMBER ? static_cast<float>(Pose.Scale.Y / Own.Scale.Y) : 1.f);
	const FMatrix2x2 Linear = WidgetConstraintSolver::MakeLinear(ScaleRatio, Pose.Angle - Own.Angle);
	const FVector2f OwnTranslation(static_cast<float>(Own.Translation.X), static_cast<float>(Own.Translation.Y));
	const FVector2f PosePosition(static_cast<float>(Pose.Position.X), static_cast<float>(Pose.Position.Y));
	const FVector2f Translation = PosePosition - Context.PivotPoint - Linear.TransformPoint(OwnTranslation);
	const FSlateRenderTransform Extra(Linear, Translation);
	const bool bActive = !WidgetConstraintSolver::IsIdentity(Extra) || Pose.Opacity < 0.999f;
	Host->SetSolved(Extra, bActive, Pose.Opacity);
	Host->SetDebugSegments(MoveTemp(Debug));

	// What depends on it reads where it is drawn now: its local space through its own transform and the extra one, about the
	// pivot, into the host's place on the desktop.
	const FSlateRenderTransform OwnTransform = Own.ToSlateRenderTransform();
	const FSlateRenderTransform AboutPivot = Concatenate(Concatenate(FSlateRenderTransform(-Context.PivotPoint), OwnTransform), Extra);
	SolvedLocalToDesktop = Concatenate(Concatenate(AboutPivot, FSlateRenderTransform(Context.PivotPoint)), Context.HostToDesktop);
	SolvedSize = Context.Size;
	bHasSolved = true;
}

bool UWidgetConstraintComponent::PreviewTransform(const FWidgetTransform& OwnTransform, FWidgetTransform& OutTransform)
{
	return PreviewTransform(OwnTransform, OutTransform, nullptr);
}

bool UWidgetConstraintComponent::PreviewTransform(const FWidgetTransform& OwnTransform, FWidgetTransform& OutTransform, const TMap<const UWidget*, FWidgetTransform>* PosedWidgets)
{
	FWidgetConstraintContext Context;
	if (Constraints.Num() == 0 || !MakeContext(OwnTransform, 0.f, nullptr, Context, PosedWidgets) || (Context.bDesignTime && !bSolveInDesigner))
	{
		return false;
	}
	Context.bStateless = true;
	if (Context.bAnimated && AnyFollowsAnimation())
	{
		const FWidgetTransform Authored = ReadAuthoredTransform();
		Context.Rest.Position = FVector2D(Context.PivotPoint.X, Context.PivotPoint.Y) + Authored.Translation;
	}

	FWidgetConstraintPose Pose = Context.Rest;
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (!Constraint)
		{
			continue;
		}
		Context.bFollowAnimation = Context.bAnimated && Constraint->WhileAnimated == EWidgetConstraintWhileAnimated::FollowKeys;
		Pose = Constraint->ApplyStateless(Context, Pose, Strength);
	}

	// As a Render Transform: the pivot where the pose puts it, turned and scaled as it says (its shear kept).
	OutTransform = OwnTransform;
	OutTransform.Translation = Pose.Position - FVector2D(Context.PivotPoint.X, Context.PivotPoint.Y);
	OutTransform.Angle = Pose.Angle;
	OutTransform.Scale = Pose.Scale;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

UWidgetConstraint* UWidgetConstraintComponent::FindConstraint(FName InName) const
{
	// By its Name first; then by its kind's name ("Follow Path") for one left unnamed - what a track typed by what the Details
	// show means.
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint && (InName.IsNone() || Constraint->ConstraintName == InName))
		{
			return Constraint;
		}
	}
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint && Constraint->GetDisplayName() == InName)
		{
			return Constraint;
		}
	}
	return nullptr;
}

TArray<FName> UWidgetConstraintComponent::GetConstraintNames() const
{
	TArray<FName> Names;
	for (const UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint)
		{
			Names.AddUnique(Constraint->GetDisplayName());
		}
	}
	return Names;
}

UWidgetConstraint* UWidgetConstraintComponent::GetConstraintAt(int32 Index) const
{
	return Constraints.IsValidIndex(Index) ? Constraints[Index].Get() : nullptr;
}

UWidgetConstraint* UWidgetConstraintComponent::AddConstraint(TSubclassOf<UWidgetConstraint> ConstraintClass, FName InName)
{
	if (!ConstraintClass || ConstraintClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}
	UWidgetConstraint* Added = NewObject<UWidgetConstraint>(this, ConstraintClass);
	Added->ConstraintName = InName;
	Constraints.Add(Added);
	return Added;
}

bool UWidgetConstraintComponent::RemoveConstraint(FName InName)
{
	const int32 Index = Constraints.IndexOfByPredicate([InName](const TObjectPtr<UWidgetConstraint>& Constraint)
	{
		return Constraint && (InName.IsNone() || Constraint->ConstraintName == InName);
	});
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Constraints.RemoveAt(Index);
	return true;
}

bool UWidgetConstraintComponent::SetConstraintStrength(FName InName, float InStrength)
{
	UWidgetConstraint* Constraint = FindConstraint(InName);
	if (!Constraint)
	{
		return false;
	}
	Constraint->SetStrength(InStrength);
	return true;
}

bool UWidgetConstraintComponent::SetConstraintEnabled(FName InName, bool bInEnabled)
{
	UWidgetConstraint* Constraint = FindConstraint(InName);
	if (!Constraint)
	{
		return false;
	}
	Constraint->SetEnabled(bInEnabled);
	return true;
}

bool UWidgetConstraintComponent::SetConstraintValue(FName InName, EWidgetConstraintChannel Channel, float Value)
{
	UWidgetConstraint* Constraint = FindConstraint(InName);
	if (!Constraint || !Constraint->SetChannel(Channel, Value))
	{
		return false;
	}
	SolveNowInDesigner();
	return true;
}

void UWidgetConstraintComponent::SolveNowInDesigner() const
{
	// In the designer (a Widget Animation previewed, scrubbed): shown at once, not on the next tick.
	const UWidget* OwnerWidget = GetOwner().Get();
	if (OwnerWidget && OwnerWidget->IsDesignTime())
	{
		SolveAll(0.f);
	}
}

bool UWidgetConstraintComponent::SetConstraintValueByName(FName InName, FName ValueName, float Value)
{
	UWidgetConstraint* Constraint = FindConstraint(InName);
	if (!Constraint || !WidgetConstraintValues::Set(Constraint, ValueName, Value))
	{
		return false;
	}
	SolveNowInDesigner();
	return true;
}

bool UWidgetConstraintComponent::GetConstraintValueByName(FName InName, FName ValueName, float& OutValue) const
{
	return WidgetConstraintValues::Get(FindConstraint(InName), ValueName, OutValue);
}

bool UWidgetConstraintComponent::SetTrackValue(FName InName, FName ValueName, EWidgetConstraintChannel Channel, float Value, bool bSolveNow)
{
	UWidgetConstraint* Constraint = FindConstraint(InName);
	const bool bSet = Constraint && (ValueName.IsNone() ? Constraint->SetChannel(Channel, Value) : WidgetConstraintValues::Set(Constraint, ValueName, Value));
	if (bSet && bSolveNow)
	{
		SolveNowInDesigner();
	}
	return bSet;
}

bool UWidgetConstraintComponent::GetTrackValue(FName InName, FName ValueName, EWidgetConstraintChannel Channel, float& OutValue) const
{
	const UWidgetConstraint* Constraint = FindConstraint(InName);
	return Constraint && (ValueName.IsNone() ? Constraint->GetChannel(Channel, OutValue) : WidgetConstraintValues::Get(Constraint, ValueName, OutValue));
}

TArray<FString> UWidgetConstraintComponent::GetConstraintTrackNameOptions()
{
	// None (the first), the names given on the widgets open in a designer, then every kind's name (a constraint left unnamed).
	TArray<FString> Options;
	Options.Add(FName(NAME_None).ToString());
	for (const TWeakObjectPtr<UWidgetConstraintComponent>& Weak : WidgetConstraintSolver::GetComponents())
	{
		const UWidgetConstraintComponent* Component = Weak.Get();
		const UWidget* OwnerWidget = Component ? Component->GetOwner().Get() : nullptr;
		if (!OwnerWidget || !OwnerWidget->IsDesignTime())
		{
			continue;
		}
		for (const FName& Name : Component->GetConstraintNames())
		{
			Options.AddUnique(Name.ToString());
		}
	}
	TArray<FString> Kinds;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UWidgetConstraint::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			if (const UWidgetConstraint* Defaults = Cast<UWidgetConstraint>(It->GetDefaultObject()))
			{
				Kinds.AddUnique(Defaults->GetDisplayName().ToString());
			}
		}
	}
	Kinds.Sort();
	for (const FString& Kind : Kinds)
	{
		Options.AddUnique(Kind);
	}
	return Options;
}

TArray<FString> UWidgetConstraintComponent::GetConstraintTrackValueOptions()
{
	TArray<FName> Names;
	WidgetConstraintValues::GetAllNames(Names);
	TArray<FString> Options;
	Options.Add(FName(NAME_None).ToString());
	for (const FName& Name : Names)
	{
		Options.Add(Name.ToString());
	}
	return Options;
}

bool UWidgetConstraintComponent::GetConstraintValue(FName InName, EWidgetConstraintChannel Channel, float& OutValue) const
{
	const UWidgetConstraint* Constraint = FindConstraint(InName);
	return Constraint && Constraint->GetChannel(Channel, OutValue);
}

void UWidgetConstraintComponent::ResetConstraintMotion()
{
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint)
		{
			Constraint->ResetMotion();
		}
	}
}
