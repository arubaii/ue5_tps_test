// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetConstraintComponent.h"

#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/DrawElements.h"
#include "Slate/WidgetTransform.h"
#include "NameTBD/WidgetAnimation/WidgetAnimInput.h"

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
	const bool bChanged = bInActive != bActive || SolvedOpacity != InOpacity || (bInActive && !WidgetConstraintHostPrivate::NearlySame(InExtra, Extra));
	Extra = InExtra;
	bActive = bInActive;
	SolvedOpacity = InOpacity;
	if (bChanged)
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

	WidgetConstraintSolver::GetComponents().AddUnique(this);
	FDelegateHandle& Handle = WidgetConstraintSolver::GetTickHandle();
	if (!Handle.IsValid() && FSlateApplication::IsInitialized())
	{
		// Solved before Slate ticks and paints, every frame, in dependency order.
		Handle = FSlateApplication::Get().OnPreTick().AddStatic(&UWidgetConstraintComponent::SolveAll);
	}
	return Host;
}

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
	for (const TWeakObjectPtr<UWidgetConstraintComponent>& Weak : WidgetConstraintSolver::GetComponents())
	{
		UWidgetConstraintComponent* Component = Weak.Get();
		if (Component && Component->GetOwner().Get() == Widget)
		{
			return Component;
		}
	}
	return nullptr;
}

void UWidgetConstraintComponent::SolveAll(float DeltaTime)
{
	TArray<TWeakObjectPtr<UWidgetConstraintComponent>>& Registered = WidgetConstraintSolver::GetComponents();
	TArray<UWidgetConstraintComponent*> Live;
	TMap<const UWidget*, UWidgetConstraintComponent*> ByOwner;
	for (int32 Index = Registered.Num() - 1; Index >= 0; --Index)
	{
		UWidgetConstraintComponent* Component = Registered[Index].Get();
		if (!Component || !Component->MyHost.IsValid())
		{
			Registered.RemoveAtSwap(Index, 1, EAllowShrinking::No);
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

	// Measured where the widget's box is laid out, before any transform of its own: the host is never transformed.
	const FGeometry& HostGeometry = Host->GetTickSpaceGeometry();
	const FVector2D LocalSize = HostGeometry.GetLocalSize();
	if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
	{
		return;
	}

	FWidgetConstraintContext Context;
	Context.Owner = OwnerWidget;
	Context.Size = FVector2f(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y));
	Context.HostToDesktop = HostGeometry.GetAccumulatedRenderTransform();
	Context.DesktopToHost = Context.HostToDesktop.Inverse();
	Context.DeltaTime = FMath::Min(DeltaTime, 0.1f);
	Context.bDesignTime = bDesignTime;

	const FWidgetTransform& Own = OwnerWidget->GetRenderTransform();
	const FVector2D PivotFraction = OwnerWidget->GetRenderTransformPivot();
	Context.PivotPoint = Context.Size * FVector2f(static_cast<float>(PivotFraction.X), static_cast<float>(PivotFraction.Y));
	Context.Rest.Position = FVector2D(Context.PivotPoint.X, Context.PivotPoint.Y) + Own.Translation;
	Context.Rest.Angle = Own.Angle;
	Context.Rest.Scale = Own.Scale;
	Context.Rest.Opacity = 1.f;

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

	const FSlateRenderTransform DesktopToHost = Context.DesktopToHost;
	Context.ResolveWidget = [&ByOwner, DesktopToHost](const UWidget* Widget, FSlateRenderTransform& OutLocalToHost, FVector2f& OutSize) -> bool
	{
		if (!Widget)
		{
			return false;
		}
		// A constrained widget: where its constraints put it (this frame, when it came first).
		if (UWidgetConstraintComponent* const* Constrained = ByOwner.Find(Widget))
		{
			if ((*Constrained)->bHasSolved)
			{
				OutLocalToHost = Concatenate((*Constrained)->SolvedLocalToDesktop, DesktopToHost);
				OutSize = (*Constrained)->SolvedSize;
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

	FWidgetConstraintPose Pose = Context.Rest;
	TArray<FVector2f> Debug;
	const bool bDrawDebug = bDesignTime && bShowInDesigner;
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (!Constraint)
		{
			continue;
		}
		Pose = Constraint->Apply(Context, Pose, Strength);
		if (bDrawDebug && Constraint->bEnabled)
		{
			Constraint->GetDebugLines(Context, Pose, Debug);
		}
	}
	SolvedPose = Pose;

	// The extra transform, about the pivot, that takes the widget from its own pose to the solved one:
	// E(x) = T + L x, with L the change of scale and turn, and T placing the pivot where the pose says.
	const FVector2f ScaleRatio(
		FMath::Abs(Context.Rest.Scale.X) > KINDA_SMALL_NUMBER ? static_cast<float>(Pose.Scale.X / Context.Rest.Scale.X) : 1.f,
		FMath::Abs(Context.Rest.Scale.Y) > KINDA_SMALL_NUMBER ? static_cast<float>(Pose.Scale.Y / Context.Rest.Scale.Y) : 1.f);
	const FMatrix2x2 Linear = WidgetConstraintSolver::MakeLinear(ScaleRatio, Pose.Angle - Context.Rest.Angle);
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

// ---------------------------------------------------------------------------------------------
// Blueprint
// ---------------------------------------------------------------------------------------------

UWidgetConstraint* UWidgetConstraintComponent::FindConstraint(FName InName) const
{
	for (UWidgetConstraint* Constraint : Constraints)
	{
		if (Constraint && (InName.IsNone() || Constraint->ConstraintName == InName))
		{
			return Constraint;
		}
	}
	return nullptr;
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
	return Constraint && Constraint->SetChannel(Channel, Value);
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
