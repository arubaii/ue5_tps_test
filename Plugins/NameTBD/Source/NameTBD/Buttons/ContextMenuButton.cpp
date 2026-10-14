// Copyright AR GameSystems 2026. All rights reserved.

#include "ContextMenuButton.h"


#include "NameTBD/Cards/InfoCardWidget.h"
#include "NameTBD/DeveloperSettings/InfoCardDeveloperSettings.h"
#include "NameTBD/Buttons/Actions/ButtonAction.h"
#include "NameTBD/Buttons/Actions/ContextMenuInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Misc/App.h"
#include "Misc/StringBuilder.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Slate/SObjectWidget.h"
#include "Widgets/SWidget.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "NameTBD/Widgets/Shapes/WidgetShapeAnimation.h"
#include "Components/Border.h"
#include "Components/RetainerBox.h"
#include "Components/ButtonSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/PanelSlot.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture2D.h"
#include "NameTBD/Widgets/WidgetSVGImage.h"
#include "Containers/Ticker.h"
#include "Styling/SlateTypes.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SLeafWidget.h"

#if WITH_EDITOR
// Details-panel layout for the instance override slots (bottom of this file). Needs "PropertyEditor" in this
// module's editor-only dependencies.
#include "PropertyEditorModule.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "Widgets/Text/STextBlock.h"
#include "Modules/ModuleManager.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/CoreDelegates.h"
#endif

// ==================================================================
// Local helpers
// ==================================================================

namespace ContextMenuButtonOverrideNaming
{
	/**
	 * Strips the numeric suffix the engine appends when it instances a widget (MyButton -> MyButton_12).
	 *
	 * Without this, a variable deliberately named after a button stops matching the moment the widget
	 * holding it is placed inside something else - which is exactly the situation per-placement overrides
	 * exist for. A widget that declares two of these would then fall through to the "more than one
	 * candidate, refuse to guess" branch below and the placement would silently get nothing, which is the
	 * designer showing the authoring widget's look while the game shows the placement's.
	 */
	static FString StripInstanceSuffix(const FString& Name)
	{
		int32 Underscore = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < Name.Len() - 1)
		{
			if (Name.Mid(Underscore + 1).IsNumeric())
			{
				return Name.Left(Underscore);
			}
		}

		return Name;
	}

	/**
	 * Matches an Event Dispatcher against the On{ButtonName}{Event} convention - OnPillButtonClicked for a
	 * button called PillButton.
	 *
	 * Strict about order, not just contents: the name has to START with On, then the button's name, and the
	 * event word has to come after it. "OnPillButtonClicked" binds; "OnClickedPillButton" and
	 * "PillButtonClicked" do not. Loose containment would happily match OnSubmitPillButtonClickedFallback
	 * and a dozen other things, and a mis-bound dispatcher is far harder to notice than one that simply
	 * never fires.
	 */
	static bool MatchesDispatcherConvention(const FString& DispatcherName, const FString& ButtonName, const TCHAR* EventKeyword)
	{
		const FString Name = StripInstanceSuffix(DispatcherName);
		const FString Button = StripInstanceSuffix(ButtonName);

		if (Button.IsEmpty() || !Name.StartsWith(TEXT("On"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		const FString AfterOn = Name.RightChop(2);
		if (!AfterOn.StartsWith(Button, ESearchCase::IgnoreCase))
		{
			return false;
		}

		return AfterOn.RightChop(Button.Len()).Contains(EventKeyword, ESearchCase::IgnoreCase);
	}

	/** The relaxed form, for a widget with exactly one dispatcher for an event and no button named in any of them. */
	static bool MatchesDispatcherEventOnly(const FString& DispatcherName, const TCHAR* EventKeyword)
	{
		const FString Name = StripInstanceSuffix(DispatcherName);
		return Name.StartsWith(TEXT("On"), ESearchCase::IgnoreCase) && Name.Contains(EventKeyword, ESearchCase::IgnoreCase);
	}

	/** Case-insensitive and either way round, so ButtonOverrides matches Button and Button matches ButtonOverrides. */
	static bool NamesMatch(const FString& VariableName, const FString& ButtonName)
	{
		const FString Variable = StripInstanceSuffix(VariableName);
		const FString Button = StripInstanceSuffix(ButtonName);

		// One or two characters would match half the names in a project by accident, and restyling the
		// wrong button is worse than finding none at all.
		constexpr int32 MinimumMeaningfulLength = 3;
		if (Variable.Len() < MinimumMeaningfulLength || Button.Len() < MinimumMeaningfulLength)
		{
			return false;
		}

		return Variable.Contains(Button, ESearchCase::IgnoreCase) || Button.Contains(Variable, ESearchCase::IgnoreCase);
	}
}


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

	/**
	 * Steps a whole Shape Box shape based on explicit Start and Target blended by CurveAlpha. Every property blends
	 * through FShapeBoxShape::Lerp, which walks the struct by reflection, so nothing here names a single one of them.
	 */
	bool InterpShape(FShapeBoxShape& Current, const FShapeBoxShape& Start, const FShapeBoxShape& Target, float CurveAlpha, float RawAlpha, bool bInstant)
	{
		if (bInstant || RawAlpha >= 1.0f)
		{
			Current = Target;
			return true;
		}
		Current = FShapeBoxShape::Lerp(Start, Target, CurveAlpha);
		return false;
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
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				Gather(Panel->GetChildAt(i), TreePrefix, bDescendIntoUserWidgets, Out, Depth + 1);
			}

			// Parts hidden from the Hierarchy are still the button's parts.
			if (const UContextMenuButton* Button = Cast<UContextMenuButton>(Panel))
			{
				Gather(Button->GetHiddenContent(), TreePrefix, bDescendIntoUserWidgets, Out, Depth + 1);
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

FContextMenuShapeStateSettings FContextMenuShapePreset::GetShapeForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const
{
	auto GetHovered = [this]() -> const FContextMenuShapeStateSettings& {
		return bOverrideHoveredShape ? HoveredShape : BaseShape;
	};

	switch (State)
	{
	case EContextMenuState::Hovered:  return GetHovered();
	case EContextMenuState::Pressed:  return bOverridePressedShape ? PressedShape : BaseShape;
	case EContextMenuState::Disabled: return bOverrideDisabledShape ? DisabledShape : BaseShape;
	case EContextMenuState::Enabled:
		if (bEnabledMatchesHoverShape)
		{
			return GetHovered();
		}
		return bOverrideEnabledShape ? EnabledShape : BaseShape;
	case EContextMenuState::Normal:
	default:
		return bOverrideNormalShape ? NormalShape : BaseShape;
	}
}

FContextMenuResolvedInterpolation FContextMenuShapePreset::GetResolvedInterpolation() const
{
	FContextMenuResolvedInterpolation Out;

	Out.TransformSpeed     = TransformInterpolationSpeed;
	Out.TransformCurve     = TransformInterpolationCurve;
	Out.TransformOvershoot = TransformOvershoot;
	Out.TransformDelay     = TransformDelay;

	if (bSeparateTransformAndSize)
	{
		Out.SizeSpeed     = SizeInterpolationSpeed;
		Out.SizeCurve     = SizeInterpolationCurve;
		Out.SizeOvershoot = SizeOvershoot;
		Out.SizeDelay     = SizeDelay;
	}
	else
	{
		Out.SizeSpeed     = Out.TransformSpeed;
		Out.SizeCurve     = Out.TransformCurve;
		Out.SizeOvershoot = Out.TransformOvershoot;
		Out.SizeDelay     = Out.TransformDelay;
	}

	return Out;
}

FContextMenuShapeStateSettings FContextMenuShapeTarget::GetShapeForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const
{
	auto GetHovered = [this]() -> const FContextMenuShapeStateSettings& {
		return bOverrideHoveredShape ? HoveredShape : BaseShape;
	};

	switch (State)
	{
	case EContextMenuState::Hovered:  return GetHovered();
	case EContextMenuState::Pressed:  return bOverridePressedShape ? PressedShape : BaseShape;
	case EContextMenuState::Disabled: return bOverrideDisabledShape ? DisabledShape : BaseShape;
	case EContextMenuState::Enabled:
		if (bEnabledMatchesHoverShape)
		{
			return GetHovered();
		}
		return bOverrideEnabledShape ? EnabledShape : BaseShape;
	case EContextMenuState::Normal:
	default:
		return bOverrideNormalShape ? NormalShape : BaseShape;
	}
}

FContextMenuResolvedInterpolation FContextMenuShapeTarget::GetResolvedInterpolation() const
{
	FContextMenuResolvedInterpolation Out;

	Out.TransformSpeed     = TransformInterpolationSpeed;
	Out.TransformCurve     = TransformInterpolationCurve;
	Out.TransformOvershoot = TransformOvershoot;
	Out.TransformDelay     = TransformDelay;

	// Size follows transform unless it's been explicitly separated, which is what keeps the whole shape moving
	// as one piece by default instead of drifting apart because two sets of numbers got out of sync.
	if (bSeparateTransformAndSize)
	{
		Out.SizeSpeed     = SizeInterpolationSpeed;
		Out.SizeCurve     = SizeInterpolationCurve;
		Out.SizeOvershoot = SizeOvershoot;
		Out.SizeDelay     = SizeDelay;
	}
	else
	{
		Out.SizeSpeed     = Out.TransformSpeed;
		Out.SizeCurve     = Out.TransformCurve;
		Out.SizeOvershoot = Out.TransformOvershoot;
		Out.SizeDelay     = Out.TransformDelay;
	}

	return Out;
}

const FContextMenuWidgetDesign& FContextMenuDesignTarget::GetDesignForState(EContextMenuState State, bool bEnabledMatchesHover) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return bOverrideHoveredDesign ? Hovered : BaseDesign;
	case EContextMenuState::Pressed:  return bOverridePressedDesign ? Pressed : BaseDesign;
	case EContextMenuState::Disabled: return bOverrideDisabledDesign ? Disabled : BaseDesign;
	case EContextMenuState::Enabled:
		if (bEnabledMatchesHover)
		{
			return bOverrideHoveredDesign ? Hovered : BaseDesign;
		}
		return bOverrideEnabledDesign ? Enabled : BaseDesign;
	case EContextMenuState::Normal:
	default:                          return bOverrideNormalDesign ? Normal : BaseDesign;
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

/**
 * Which slot of an instance patch lines up with which slot of the button.
 *
 * A patch is laid out slot for slot like the target it patches: Base, plus one slot per state the button
 * overrides. Resolving a state therefore asks the BUTTON which of its slots that state uses, and takes the
 * patch's slot of the same name. The patch's own per-state switches are only details-panel mirrors of this,
 * so a patch that was saved against an older version of the button can never pick a different slot from the
 * one the button is actually showing.
 */
namespace ContextMenuButtonOverrideSlots
{
	/** Which states a target authors on their own rather than leaving on Base. */
	struct FStateMask
	{
		bool bNormal = false;
		bool bHovered = false;
		bool bPressed = false;
		bool bDisabled = false;
		bool bEnabled = false;

		bool Has(EContextMenuState State) const
		{
			switch (State)
			{
			case EContextMenuState::Hovered:  return bHovered;
			case EContextMenuState::Pressed:  return bPressed;
			case EContextMenuState::Disabled: return bDisabled;
			case EContextMenuState::Enabled:  return bEnabled;
			case EContextMenuState::Normal:
			default:                          return bNormal;
			}
		}
	};

	/** The state whose slot a resolve actually reads: Enabled borrows Hovered while Enabled Matches Hover is on. */
	EContextMenuState CanonicalState(EContextMenuState State, bool bEnabledMatchesHover)
	{
		return (State == EContextMenuState::Enabled && bEnabledMatchesHover) ? EContextMenuState::Hovered : State;
	}

	FStateMask MakeShapeMask(const FContextMenuShapeTarget& Target, const FContextMenuShapePreset* Preset)
	{
		FStateMask Mask;
		if (Preset)
		{
			Mask.bNormal   = Preset->bOverrideNormalShape;
			Mask.bHovered  = Preset->bOverrideHoveredShape;
			Mask.bPressed  = Preset->bOverridePressedShape;
			Mask.bDisabled = Preset->bOverrideDisabledShape;
			Mask.bEnabled  = Preset->bOverrideEnabledShape;
		}
		else
		{
			Mask.bNormal   = Target.bOverrideNormalShape;
			Mask.bHovered  = Target.bOverrideHoveredShape;
			Mask.bPressed  = Target.bOverridePressedShape;
			Mask.bDisabled = Target.bOverrideDisabledShape;
			Mask.bEnabled  = Target.bOverrideEnabledShape;
		}
		return Mask;
	}

	const FContextMenuDesignPreset* FindDesignPreset(const FContextMenuDesignTarget& Target)
	{
		if (Target.DesignSource != EContextMenuButtonSettingSource::UseProjectSettings)
		{
			return nullptr;
		}

		const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
		return Settings ? Settings->FindContextMenuDesignPreset(Target.PresetName) : nullptr;
	}

	/** A Design Preset authors every state on its own, so it has no Base to fall back to. */
	FStateMask MakeDesignMask(const FContextMenuDesignTarget& Target, const FContextMenuDesignPreset* Preset)
	{
		FStateMask Mask;
		if (Preset)
		{
			Mask.bNormal = Mask.bHovered = Mask.bPressed = Mask.bDisabled = Mask.bEnabled = true;
		}
		else
		{
			Mask.bNormal   = Target.bOverrideNormalDesign;
			Mask.bHovered  = Target.bOverrideHoveredDesign;
			Mask.bPressed  = Target.bOverridePressedDesign;
			Mask.bDisabled = Target.bOverrideDisabledDesign;
			Mask.bEnabled  = Target.bOverrideEnabledDesign;
		}
		return Mask;
	}

	/** A preset's design for one state, with no Enabled-matches-Hover substitution. */
	const FContextMenuWidgetDesign& GetPresetDesign(const FContextMenuDesignPreset& Preset, EContextMenuState State)
	{
		switch (State)
		{
		case EContextMenuState::Hovered:  return Preset.Hovered;
		case EContextMenuState::Pressed:  return Preset.Pressed;
		case EContextMenuState::Disabled: return Preset.Disabled;
		case EContextMenuState::Enabled:  return Preset.Enabled;
		case EContextMenuState::Normal:
		default:                          return Preset.Normal;
		}
	}

	/**
	 * True if at least one state the button can actually be in still falls back to Base. When every such
	 * state is overridden, Base never shows anywhere, and neither should the patch's Base slot.
	 */
	bool IsBaseReachable(const FStateMask& Mask, bool bEnabledIsOwnState)
	{
		return !Mask.bNormal || !Mask.bHovered || !Mask.bPressed || !Mask.bDisabled || (bEnabledIsOwnState && !Mask.bEnabled);
	}

	/**
	 * The patch slot lining up with the slot the button reads for State, or null if this placement has that
	 * slot switched off (then the state simply shows the button's values).
	 */
	const FContextMenuShapeOverride* SelectShapeSlot(const FContextMenuShapeTargetOverride& Patch, const FStateMask& Mask, EContextMenuState State, bool bEnabledMatchesHover)
	{
		const EContextMenuState Effective = CanonicalState(State, bEnabledMatchesHover);
		if (Mask.Has(Effective))
		{
			return Patch.IsStateSlotOverridden(Effective) ? &Patch.GetStateSlot(Effective) : nullptr;
		}
		return Patch.bOverrideBaseShape ? &Patch.BaseShape : nullptr;
	}

	const FContextMenuDesignStateOverride* SelectDesignSlot(const FContextMenuDesignTargetOverride& Patch, const FStateMask& Mask, EContextMenuState State, bool bEnabledMatchesHover)
	{
		const EContextMenuState Effective = CanonicalState(State, bEnabledMatchesHover);
		if (Mask.Has(Effective))
		{
			return Patch.IsStateSlotOverridden(Effective) ? &Patch.GetStateSlot(Effective) : nullptr;
		}
		return Patch.bOverrideBaseDesign ? &Patch.BaseDesign : nullptr;
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
// SContextMenuButtonBrush
// ==================================================================

/**
 * Paints the button's background brush and nothing else. Sits inside the button's brush layer (an SShapeBox), which
 * bends it into the button's shape. Hit-testable on purpose: its place in the hit-test grid is what makes the drawn
 * shape clickable, and it has no handlers of its own, so every event bubbles straight on to the button.
 */
class SContextMenuButtonBrush : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SContextMenuButtonBrush) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		SetCanTick(false);
	}

	/**
	 * What to paint next. The brush belongs to the owning button's style, which outlives every paint that reads it.
	 * Returns whether that looks any different from what was painted last - another brush, a brush changed in place (a
	 * design target writing its slot), another tint or effect - or may change by itself: any resource but a plain
	 * texture (a material, which can animate; a render target or media texture, which are drawn into). What decides
	 * whether a warped brush layer is drawn again (see SShapeBox::SetRedrawContentOnlyWhenChanged).
	 */
	bool SetBrush(const FSlateBrush* InBrush, ESlateDrawEffect InDrawEffects, const FLinearColor& InTint)
	{
		const UObject* Resource = InBrush ? InBrush->GetResourceObject() : nullptr;
		const bool bLivesOnItsOwn = Resource && !Resource->IsA<UTexture2D>();
		const bool bChanged = bLivesOnItsOwn
			|| !bHasPainted
			|| Brush != InBrush
			|| DrawEffects != InDrawEffects
			|| Tint != InTint
			|| (InBrush && !(PaintedBrush == *InBrush));

		Brush = InBrush;
		DrawEffects = InDrawEffects;
		Tint = InTint;
		if (bChanged)
		{
			bHasPainted = true;
			PaintedBrush = InBrush ? *InBrush : FSlateBrush();
		}
		return bChanged;
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		if (Brush && Brush->DrawAs != ESlateBrushDrawType::NoDrawType)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Brush, DrawEffects, Tint * InWidgetStyle.GetColorAndOpacityTint());
		}
		return LayerId;
	}

	virtual FVector2D ComputeDesiredSize(float) const override
	{
		return FVector2D::ZeroVector;
	}

private:
	const FSlateBrush* Brush = nullptr;
	ESlateDrawEffect DrawEffects = ESlateDrawEffect::None;
	FLinearColor Tint = FLinearColor::White;

	/** The brush as it was when last set differently - to tell a brush changed in place from one that wasn't. */
	FSlateBrush PaintedBrush;
	bool bHasPainted = false;
};

// ==================================================================
// SContextMenuSlateButton
// ==================================================================

void SContextMenuSlateButton::SetOwnerWidget(UContextMenuButton* InOwner)
{
	OwnerWidget = InOwner;

	if (!BrushLayer.IsValid())
	{
		// The layer itself is never hit: in every mode it is the painter (or, warped, the Shape Box's hit cells) that
		// lands in the grid, so the rest of the layer's rect - wherever the shape isn't drawn - lets clicks through.
		BrushPainter = SNew(SContextMenuButtonBrush);
		BrushLayer = SNew(SShapeBox)
			.Visibility(EVisibility::SelfHitTestInvisible)
			[
				BrushPainter.ToSharedRef()
			];
		BrushLayer->SetShape(ButtonShape);

		// The layer only ever shows the one brush, so it knows when it changes (SetBrush): warped, it is drawn offscreen
		// again only then, not on every paint of every button.
		BrushLayer->SetRedrawContentOnlyWhenChanged(true);
	}
}

void SContextMenuSlateButton::SetButtonShape(const FShapeBoxShape& InShape, const FWidgetTransform& InTransform)
{
	using namespace ContextMenuButtonShape;

	const bool bChanged = !ButtonShape.Equals(InShape) || !TransformsEqual(BrushTransform, InTransform);

	ButtonShape = InShape;
	BrushTransform = InTransform;
	bHasButtonShape = !ButtonShape.IsFlat() || !IsIdentityTransform(BrushTransform);

	if (BrushLayer.IsValid())
	{
		BrushLayer->SetShape(ButtonShape);
	}

	// Deliberately does not touch the render transform: the repaint is the point, and the paint it triggers is what
	// commits the new core. See CommitShapeRenderTransform.
	if (bChanged)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SContextMenuSlateButton::SetBackgroundShape(UShapeWidget* InShape)
{
	if (BackgroundShape.Get() == InShape && BackgroundPainter.IsValid() == (InShape != nullptr))
	{
		return;
	}

	BackgroundShape = InShape;
	if (InShape)
	{
		// Painted where the brush would be: inside the brush layer, so the shape's Transform, the hit area that follows it
		// and a Shape Box shape all work on it exactly as they do on the brush.
		BackgroundPainter = SNew(SWidgetShape).Shape(InShape).PaintOnly(true);
		if (BrushLayer.IsValid())
		{
			BrushLayer->SetContent(BackgroundPainter.ToSharedRef());
		}
	}
	else
	{
		BackgroundPainter.Reset();
		if (BrushLayer.IsValid() && BrushPainter.IsValid())
		{
			BrushLayer->SetContent(BrushPainter.ToSharedRef());
		}
	}

	if (BrushLayer.IsValid())
	{
		BrushLayer->MarkContentChanged();
	}
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SContextMenuSlateButton::RecordPress(const FGeometry& MyGeometry, const FPointerEvent& Event)
{
	// MyGeometry is the button's core when the shape is active; through the committed core it is the layout box again.
	FVector2f Local = FVector2f(FVector2D(MyGeometry.AbsoluteToLocal(Event.GetScreenSpacePosition())));
	if (bCommittedShapeActive)
	{
		Local = CommittedShape.TransformPoint(Local);
	}
	LastPressScreenPosition = FVector2D(Event.GetScreenSpacePosition());
	const FVector2D Size = MyGeometry.GetLocalSize();
	LastPressFraction = FVector2D(
		Size.X > 0.0 ? Local.X / Size.X : 0.5,
		Size.Y > 0.0 ? Local.Y / Size.Y : 0.5);
}

FReply SContextMenuSlateButton::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	RecordPress(MyGeometry, MouseEvent);
	return SButton::OnMouseButtonDown(MyGeometry, MouseEvent);
}

FReply SContextMenuSlateButton::OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent)
{
	RecordPress(MyGeometry, InTouchEvent);
	return SButton::OnTouchStarted(MyGeometry, InTouchEvent);
}

FReply SContextMenuSlateButton::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	LastPressFraction = FVector2D(0.5, 0.5);
	LastPressScreenPosition.Reset();
	return SButton::OnKeyDown(MyGeometry, InKeyEvent);
}

FNavigationReply SContextMenuSlateButton::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	if (UContextMenuButton* Owner = OwnerWidget.Get())
	{
		if (Owner->HandleNavigation(InNavigationEvent.GetNavigationType()))
		{
			// Taken: the focus stays on this button.
			return FNavigationReply::Stop();
		}
	}
	return SButton::OnNavigation(MyGeometry, InNavigationEvent);
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

	// The authored half is read fresh (it is what just changed) while the core is the committed one. Only the core
	// is ever canceled at paint time, so refreshing the authored half can't put the two out of step.
	CommittedAuthoredTransform = AuthoredTransform;
	CommittedAuthoredPivot = AuthoredPivot;

	WriteRenderTransform(CommittedShape, bCommittedShapeActive, CommittedLocalSize, AuthoredTransform, AuthoredPivot);
}

void SContextMenuSlateButton::WriteRenderTransform(const FSlateRenderTransform& Core, bool bShapeActive, const FVector2D& LocalSize, const FWidgetTransform& AuthoredTransform, const FVector2D& AuthoredPivot)
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
	SetRenderTransform(Core.Concatenate(Authored));
}

FSlateRenderTransform SContextMenuSlateButton::ComputeBrushTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	// Pivots on the middle of the shaped brush, wherever Extrude/Inset/... have put it.
	const FVector2D Middle = ButtonShape.Evaluate(LocalSize, FVector2D(0.5, 0.5));
	return FoldPivot(BrushTransform.ToSlateRenderTransform(), Middle);
}

FSlateRenderTransform SContextMenuSlateButton::ComputeCoreTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	const FSlateRenderTransform Brush = ComputeBrushTransform(LocalSize);

	// A parallelogram maps exactly, so the button's own rect IS the drawn shape.
	if (ButtonShape.IsAffine())
	{
		return ButtonShape.MakeLayoutToShapeTransform(LocalSize).Concatenate(Brush);
	}

	// Anything else has no rectangle equal to it. The button's rect is shrunk about the shape's middle until the
	// painted shape covers it, so it never answers clicks where nothing is drawn; the rest of the shape is covered by
	// the brush layer, which bubbles to the button anyway. Tested against the shape as just painted, in the brush
	// layer's local space (before Transform, which applies to both alike).
	const FVector2D Middle = ButtonShape.Evaluate(LocalSize, FVector2D(0.5, 0.5));

	auto IsCovered = [this, &LocalSize, &Middle](double Factor)
	{
		if (!BrushLayer.IsValid())
		{
			return false;
		}

		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				const FVector2D Offset((Column * 0.5 - 0.5) * LocalSize.X * Factor, (Row * 0.5 - 0.5) * LocalSize.Y * Factor);
				if (!BrushLayer->IsLocalPointInShape(Middle + Offset))
				{
					return false;
				}
			}
		}
		return true;
	};

	static constexpr double Factors[] = { 0.9, 0.75, 0.6, 0.45, 0.3, 0.15 };
	double Factor = 0.05;
	for (const double Candidate : Factors)
	{
		if (IsCovered(Candidate))
		{
			Factor = Candidate;
			break;
		}
	}

	return MakeScale(FVector2D(Factor, Factor))
		.Concatenate(MakeTranslation(Middle - LocalSize * (0.5 * Factor)))
		.Concatenate(Brush);
}

bool SContextMenuSlateButton::IsScreenPointInBrushShape(const FVector2D& ScreenPoint) const
{
	if (!BrushLayer.IsValid() || !bBrushLayerPainted)
	{
		return false;
	}

	const FVector2D LocalPoint = BrushLayer->GetTickSpaceGeometry().AbsoluteToLocal(ScreenPoint);
	return BrushLayer->IsLocalPointInShape(LocalPoint);
}

void SContextMenuSlateButton::ClearVisualState()
{
	bHasVisualState  = false;
	bDesignOwnsBrush = false;
	VisualState      = EContextMenuState::Normal;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SContextMenuSlateButton::CommitShapeRenderTransform(const FVector2D& LocalSize) const
{
	using namespace ContextMenuButtonShape;

	UContextMenuButton* Owner = OwnerWidget.Get();

	// Whatever the widget itself is transformed by (authored in UMG, animated, or set from Blueprint) is the base the
	// core is composed onto, so shaping a button never throws its own render transform away. It's read from the owner
	// rather than off the Slate widget because UWidget pushes it back down on every SynchronizeProperties, which would
	// otherwise fold an already-composed transform into itself.
	const FWidgetTransform AuthoredTransform = Owner ? Owner->GetRenderTransform() : FWidgetTransform();
	const FVector2D AuthoredPivot = Owner ? Owner->GetRenderTransformPivot() : FVector2D(0.5, 0.5);

	// The core of a non-parallelogram shape is measured against the painted brush layer, so without a paint of it
	// there is nothing to measure yet.
	const bool bShapeActive = bHasButtonShape
		&& LocalSize.X > MinShapeLocalSize && LocalSize.Y > MinShapeLocalSize
		&& (bBrushLayerPainted || ButtonShape.IsAffine());

	if (bHasCommittedShape
		&& bCommittedShapeActive == bShapeActive
		&& CommittedLocalSize.Equals(LocalSize, PixelTolerance)
		&& CommittedButtonShape.Equals(ButtonShape)
		&& TransformsEqual(CommittedBrushTransform, BrushTransform)
		&& TransformsEqual(CommittedAuthoredTransform, AuthoredTransform)
		&& CommittedAuthoredPivot.Equals(AuthoredPivot, UnitTolerance))
	{
		return;
	}

	bHasCommittedShape = true;
	bCommittedShapeActive = bShapeActive;
	CommittedLocalSize = LocalSize;
	CommittedButtonShape = ButtonShape;
	CommittedBrushTransform = BrushTransform;
	CommittedAuthoredTransform = AuthoredTransform;
	CommittedAuthoredPivot = AuthoredPivot;

	CommittedShape = bShapeActive ? ComputeCoreTransform(LocalSize) : FSlateRenderTransform();
	CommittedShapeInverse = bShapeActive ? CommittedShape.Inverse() : FSlateRenderTransform();

	// Writing the transform invalidates this widget, so the next paint is handed geometry built from it. Slate is
	// fine with a paint-time invalidation; it just queues the widget for the next frame.
	SContextMenuSlateButton* MutableThis = const_cast<SContextMenuSlateButton*>(this);
	MutableThis->WriteRenderTransform(CommittedShape, bShapeActive, LocalSize, AuthoredTransform, AuthoredPivot);
}

FReply SContextMenuSlateButton::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D ScreenPoint = MouseEvent.GetScreenSpacePosition();

	// SButton decides whether a touch was lifted over it from MyGeometry, which is only the button's core. Lifted
	// anywhere else on the drawn shape, it is handed a tiny geometry around the touch instead, so the same test passes
	// and everything else about the release (press method, capture, click) runs exactly as SButton does it.
	if (MouseEvent.IsTouchEvent() && !MyGeometry.IsUnderLocation(ScreenPoint) && IsScreenPointInBrushShape(ScreenPoint))
	{
		const FGeometry AroundTouch = FGeometry::MakeRoot(FVector2D(2.0, 2.0), FSlateLayoutTransform(1.0f, ScreenPoint - FVector2D(1.0, 1.0)));
		return SButton::OnMouseButtonUp(AroundTouch, MouseEvent);
	}

	return SButton::OnMouseButtonUp(MyGeometry, MouseEvent);
}

int32 SContextMenuSlateButton::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	UContextMenuButton* Owner = OwnerWidget.Get();

	bBrushLayerPainted = false;

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

	if (!Owner || !BrushLayer.IsValid())
	{
		return SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	}

	const bool bEnabled = ShouldBeEnabled(bParentEnabled);

	// Same brush selection SButton uses: a disabled brush with DrawAs = NoDrawType means
	// "draw the normal brushes with the disabled effect" instead of a dedicated disabled brush.
	const FButtonStyle& ButtonStyle = Owner->GetStyle();
	const bool bShowDisabledEffect = ButtonStyle.Disabled.DrawAs == ESlateBrushDrawType::NoDrawType;

	// What stock SButton would paint, straight off Slate's own hover/press flags.
	const FSlateBrush* NativeBrush = &ButtonStyle.Normal;
	if (!bEnabled && !bShowDisabledEffect)
	{
		NativeBrush = &ButtonStyle.Disabled;
	}
	else if (IsPressed())
	{
		NativeBrush = &ButtonStyle.Pressed;
	}
	else if (IsHovered())
	{
		NativeBrush = &ButtonStyle.Hovered;
	}

	// What the owner's state machine says to paint. Once a state has been reported it decides the brush outright, so
	// the button's own state settings replace the native style rather than being painted on top of it.
	const FSlateBrush* Brush = NativeBrush;
	if (bHasVisualState)
	{
		if (!bEnabled && !bShowDisabledEffect)
		{
			Brush = &ButtonStyle.Disabled;
		}
		else if (bDesignOwnsBrush)
		{
			// A Design Target drives this button's own tint/brush/material, so the design is the whole look and the
			// style's per-state brushes would only fight it. Everything paints from the slot the design writes into.
			Brush = &ButtonStyle.Normal;
		}
		else
		{
			switch (VisualState)
			{
			case EContextMenuState::Pressed:  Brush = &ButtonStyle.Pressed; break;
			case EContextMenuState::Hovered:  Brush = &ButtonStyle.Hovered; break;
			case EContextMenuState::Disabled: Brush = bShowDisabledEffect ? &ButtonStyle.Normal : &ButtonStyle.Disabled; break;
			default:                          Brush = &ButtonStyle.Normal; break;
			}
		}
	}

	// A vector Background Shape replaces the style's brushes outright.
	const bool bVectorBackground = BackgroundPainter.IsValid() && BackgroundShape.IsValid();

	// Nothing to take over: paint exactly like a stock SButton, and commit so a shape that has just gone back to
	// identity comes off the render transform.
	if (!bHasButtonShape && !bCommittedShapeActive && Brush == NativeBrush && !bVectorBackground)
	{
		const int32 StockLayer = SButton::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		CommitShapeRenderTransform(AllottedGeometry.GetLocalSize());
		return StockLayer;
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	// AllottedGeometry is the button's core, because the core rides on this widget's render transform (that is what
	// keeps the button's own rect inside the drawn shape). Everything is painted back through the inverse of it, which
	// is the plain layout box: the content ends up exactly where layout put it, with its own hit area unmoved.
	//
	// The inverse is the core the previous paint committed, never the current one. That committed core is the one on
	// the render transform, so it is the one this geometry was built from; canceling anything else leaves the content
	// off by however much the shape has moved on since, which is the content fidgeting mid-blend. The new core is
	// committed at the bottom of this function, for the next paint.
	const FGeometry ContentGeometry = bCommittedShapeActive
		? AllottedGeometry.MakeChild(LocalSize, FSlateLayoutTransform(), CommittedShapeInverse, FVector2D::ZeroVector)
		: AllottedGeometry;

	// The brush layer is laid out over the layout box too, and bends the brush into the current shape itself, so
	// unlike the core it can follow the shape the same frame without disturbing anything. Transform goes on top,
	// pivoting on the middle of the shaped brush.
	const FGeometry BrushGeometry = ContentGeometry.MakeChild(LocalSize, FSlateLayoutTransform(), ComputeBrushTransform(LocalSize), FVector2D::ZeroVector);

	const ESlateDrawEffect DrawEffects = (bShowDisabledEffect && !bEnabled) ? ESlateDrawEffect::DisabledEffect : ESlateDrawEffect::None;

	// The painter multiplies in the widget style's color and opacity itself (or, warped, the Shape Box does), so only
	// the brush's own tint and the button's background color go in here.
	FWidgetStyle LayerStyle = InWidgetStyle;
	if (bVectorBackground)
	{
		// The button's Background Color tints it, as it does the brush - which is what a Design Target's Tint on the button
		// drives. The shape is painted live inside the layer (never warped: a vector background takes no Shape Box shape), so
		// nothing is drawn offscreen and nothing needs marking as changed on every paint.
		LayerStyle.BlendColorAndOpacityTint(Owner->GetBackgroundColor());
	}
	else if (BrushPainter->SetBrush(Brush, DrawEffects, Brush->GetTint(InWidgetStyle) * Owner->GetBackgroundColor()))
	{
		BrushLayer->MarkContentChanged();
	}

	// Painted with this button as its parent: that is what makes a hit anywhere on the drawn shape bubble to the
	// button, even though the layer isn't one of its layout children.
	const int32 BrushLayerId = BrushLayer->Paint(Args.WithNewParent(this), BrushGeometry, MyCullingRect, OutDrawElements, LayerId, LayerStyle, bEnabled);
	bBrushLayerPainted = true;

	// Skip SButton/SBorder's own brush draw and paint only the content, with the layout geometry, above the brush.
	// SCompoundWidget::OnPaint applies ColorAndOpacity/foreground color and paints children one layer up.
	const int32 ResultLayer = SCompoundWidget::OnPaint(Args, ContentGeometry, MyCullingRect, OutDrawElements, FMath::Max(LayerId, BrushLayerId), InWidgetStyle, bEnabled);

	// Last thing in the pass, and the only place the render transform is written: the core for the next paint.
	CommitShapeRenderTransform(LocalSize);

	return ResultLayer;
}

// ==================================================================
// Small struct helpers
// ==================================================================

bool FContextMenuDesignTarget::DrivesAnything() const
{
	return DrivesTintAnywhere() || DrivesOutlineTintAnywhere() || DrivesOpacityAnywhere() || DrivesBrushTextureAnywhere() || DrivesMaterialAnywhere();
}

bool FContextMenuDesignTarget::DrivesTintAnywhere() const
{
	return BaseDesign.bDriveTint
		|| (bOverrideNormalDesign && Normal.bDriveTint)
		|| (bOverrideHoveredDesign && Hovered.bDriveTint)
		|| (bOverridePressedDesign && Pressed.bDriveTint)
		|| (bOverrideDisabledDesign && Disabled.bDriveTint)
		|| (bOverrideEnabledDesign && Enabled.bDriveTint);
}

bool FContextMenuDesignTarget::DrivesOutlineTintAnywhere() const
{
	return BaseDesign.bDriveOutlineTint
		|| (bOverrideNormalDesign && Normal.bDriveOutlineTint)
		|| (bOverrideHoveredDesign && Hovered.bDriveOutlineTint)
		|| (bOverridePressedDesign && Pressed.bDriveOutlineTint)
		|| (bOverrideDisabledDesign && Disabled.bDriveOutlineTint)
		|| (bOverrideEnabledDesign && Enabled.bDriveOutlineTint);
}

bool FContextMenuDesignTarget::DrivesOpacityAnywhere() const
{
	return BaseDesign.bDriveOpacity
		|| (bOverrideNormalDesign && Normal.bDriveOpacity)
		|| (bOverrideHoveredDesign && Hovered.bDriveOpacity)
		|| (bOverridePressedDesign && Pressed.bDriveOpacity)
		|| (bOverrideDisabledDesign && Disabled.bDriveOpacity)
		|| (bOverrideEnabledDesign && Enabled.bDriveOpacity);
}

bool FContextMenuDesignTarget::DrivesBrushTextureAnywhere() const
{
	return BaseDesign.bDriveBrushTexture
		|| (bOverrideNormalDesign && Normal.bDriveBrushTexture)
		|| (bOverrideHoveredDesign && Hovered.bDriveBrushTexture)
		|| (bOverridePressedDesign && Pressed.bDriveBrushTexture)
		|| (bOverrideDisabledDesign && Disabled.bDriveBrushTexture)
		|| (bOverrideEnabledDesign && Enabled.bDriveBrushTexture);
}

bool FContextMenuDesignTarget::DrivesMaterialAnywhere() const
{
	return BaseDesign.bDriveMaterial
		|| (bOverrideNormalDesign && Normal.bDriveMaterial)
		|| (bOverrideHoveredDesign && Hovered.bDriveMaterial)
		|| (bOverridePressedDesign && Pressed.bDriveMaterial)
		|| (bOverrideDisabledDesign && Disabled.bDriveMaterial)
		|| (bOverrideEnabledDesign && Enabled.bDriveMaterial);
}

const FUISoundSlot& FContextMenuSoundSet::GetSoundForState(EContextMenuState State) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return Hovered;
	case EContextMenuState::Pressed:  return Pressed;
	case EContextMenuState::Disabled: return Disabled;
	case EContextMenuState::Enabled:  return Enabled;
	case EContextMenuState::Normal:
	default:                          return Normal;
	}
}

void FContextMenuShapeOverride::ApplyTo(FContextMenuShapeStateSettings& Target) const
{
	if (bOverrideTransform) { Target.Transform = Transform; }
	if (bOverrideShape)     { Target.Shape = Shape; }
}

bool FContextMenuShapeTargetOverride::HasAnything() const
{
	// A slot the button doesn't have, or one this placement has switched off, isn't patching anything,
	// whatever is sitting in it.
	return (bBaseShapeApplicable && bOverrideBaseShape && BaseShape.HasAnything())
		|| (bNormalShapeApplicable && bOverrideNormalShape && NormalShape.HasAnything())
		|| (bHoveredShapeApplicable && bOverrideHoveredShape && HoveredShape.HasAnything())
		|| (bPressedShapeApplicable && bOverridePressedShape && PressedShape.HasAnything())
		|| (bDisabledShapeApplicable && bOverrideDisabledShape && DisabledShape.HasAnything())
		|| (bEnabledShapeApplicable && bOverrideEnabledShape && EnabledShape.HasAnything());
}

bool FContextMenuShapeTargetOverride::IsStateSlotOverridden(EContextMenuState State) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return bOverrideHoveredShape;
	case EContextMenuState::Pressed:  return bOverridePressedShape;
	case EContextMenuState::Disabled: return bOverrideDisabledShape;
	case EContextMenuState::Enabled:  return bOverrideEnabledShape;
	case EContextMenuState::Normal:
	default:                          return bOverrideNormalShape;
	}
}

const FContextMenuShapeOverride* FContextMenuShapeTargetOverride::GetForState(EContextMenuState State, bool bEnabledMatchesHoverShape) const
{
	const EContextMenuState Effective = (State == EContextMenuState::Enabled && bEnabledMatchesHoverShape) ? EContextMenuState::Hovered : State;

	bool bSlotExists = false;
	switch (Effective)
	{
	case EContextMenuState::Hovered:  bSlotExists = bHoveredShapeApplicable; break;
	case EContextMenuState::Pressed:  bSlotExists = bPressedShapeApplicable; break;
	case EContextMenuState::Disabled: bSlotExists = bDisabledShapeApplicable; break;
	case EContextMenuState::Enabled:  bSlotExists = bEnabledShapeApplicable; break;
	case EContextMenuState::Normal:
	default:                          bSlotExists = bNormalShapeApplicable; break;
	}

	if (bSlotExists)
	{
		return IsStateSlotOverridden(Effective) ? &GetStateSlot(Effective) : nullptr;
	}

	return bOverrideBaseShape ? &BaseShape : nullptr;
}

const FContextMenuShapeOverride& FContextMenuShapeTargetOverride::GetStateSlot(EContextMenuState State) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return HoveredShape;
	case EContextMenuState::Pressed:  return PressedShape;
	case EContextMenuState::Disabled: return DisabledShape;
	case EContextMenuState::Enabled:  return EnabledShape;
	case EContextMenuState::Normal:
	default:                          return NormalShape;
	}
}

FContextMenuShapeOverride& FContextMenuShapeTargetOverride::GetStateSlot(EContextMenuState State)
{
	return const_cast<FContextMenuShapeOverride&>(static_cast<const FContextMenuShapeTargetOverride*>(this)->GetStateSlot(State));
}

bool FContextMenuDesignTargetOverride::HasAnything() const
{
	return (bBaseDesignApplicable && bOverrideBaseDesign && BaseDesign.HasAnything())
		|| (bNormalDesignApplicable && bOverrideNormalDesign && Normal.HasAnything())
		|| (bHoveredDesignApplicable && bOverrideHoveredDesign && Hovered.HasAnything())
		|| (bPressedDesignApplicable && bOverridePressedDesign && Pressed.HasAnything())
		|| (bDisabledDesignApplicable && bOverrideDisabledDesign && Disabled.HasAnything())
		|| (bEnabledDesignApplicable && bOverrideEnabledDesign && Enabled.HasAnything());
}

bool FContextMenuDesignTargetOverride::IsStateSlotOverridden(EContextMenuState State) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return bOverrideHoveredDesign;
	case EContextMenuState::Pressed:  return bOverridePressedDesign;
	case EContextMenuState::Disabled: return bOverrideDisabledDesign;
	case EContextMenuState::Enabled:  return bOverrideEnabledDesign;
	case EContextMenuState::Normal:
	default:                          return bOverrideNormalDesign;
	}
}

void FContextMenuDesignTargetOverride::PostSerialize(const FArchive& Ar)
{
	if (!Ar.IsLoading())
	{
		return;
	}

	if (!bHasMigratedStateLayout)
	{
		bHasMigratedStateLayout = true;

		// The states used to be authored flat and always applied.
		bOverrideNormalDesign |= Normal.HasAnything();
		bOverrideHoveredDesign |= Hovered.HasAnything();
		bOverridePressedDesign |= Pressed.HasAnything();
		bOverrideDisabledDesign |= Disabled.HasAnything();
		bOverrideEnabledDesign |= Enabled.HasAnything();
	}

	if (!bHasMigratedSlotToggles)
	{
		bHasMigratedSlotToggles = true;

		// Same rule as Shape: a slot is switched on if something in it is actually patched.
		bOverrideBaseDesign     = BaseDesign.HasAnything();
		bOverrideNormalDesign   = Normal.HasAnything();
		bOverrideHoveredDesign  = Hovered.HasAnything();
		bOverridePressedDesign  = Pressed.HasAnything();
		bOverrideDisabledDesign = Disabled.HasAnything();
		bOverrideEnabledDesign  = Enabled.HasAnything();
	}
}

void FContextMenuDesignStateOverride::SyncApplicability(const FContextMenuWidgetDesign* ButtonSlot)
{
	bTintApplicable         = ButtonSlot && ButtonSlot->bDriveTint;
	bOutlineTintApplicable  = ButtonSlot && ButtonSlot->bDriveOutlineTint;
	bOpacityApplicable      = ButtonSlot && ButtonSlot->bDriveOpacity;
	bBrushTextureApplicable = ButtonSlot && ButtonSlot->bDriveBrushTexture;
	bMaterialApplicable     = ButtonSlot && ButtonSlot->bDriveMaterial;
}

void FContextMenuDesignStateOverride::ApplyTo(FContextMenuWidgetDesign& Target) const
{
	if (bOverrideTint)         { Target.Tint = Tint; }
	if (bOverrideOutlineTint)  { Target.OutlineTint = OutlineTint; }
	if (bOverrideOpacity)      { Target.Opacity = Opacity; }
	if (bOverrideBrushTexture)
	{
		Target.BrushImageSource = BrushImageSource;
		Target.BrushTexture = BrushTexture;
		Target.BrushSVG = BrushSVG;
	}
	if (bOverrideMaterial)
	{
		Target.Material = Material;

		// By name, not wholesale: the patch's list is synced from the button, but the button may have moved on
		// since (a material swapped, a parameter renamed) and a stale name should just be ignored rather than
		// pushed into a material that no longer has it.
		for (FContextMenuMaterialScalarValue& Scalar : Target.MaterialScalars)
		{
			if (const FContextMenuMaterialScalarValue* Patch = MaterialScalars.FindByPredicate(
				[&Scalar](const FContextMenuMaterialScalarValue& Candidate) { return Candidate.ParameterName == Scalar.ParameterName; }))
			{
				Scalar.Value = Patch->Value;
			}
		}

		for (FContextMenuMaterialVectorValue& Vector : Target.MaterialVectors)
		{
			if (const FContextMenuMaterialVectorValue* Patch = MaterialVectors.FindByPredicate(
				[&Vector](const FContextMenuMaterialVectorValue& Candidate) { return Candidate.ParameterName == Vector.ParameterName; }))
			{
				Vector.Value = Patch->Value;
			}
		}
	}
}

const FContextMenuDesignStateOverride* FContextMenuDesignTargetOverride::GetForState(EContextMenuState State, bool bEnabledMatchesHover) const
{
	const EContextMenuState Effective = (State == EContextMenuState::Enabled && bEnabledMatchesHover) ? EContextMenuState::Hovered : State;

	bool bSlotExists = false;
	switch (Effective)
	{
	case EContextMenuState::Hovered:  bSlotExists = bHoveredDesignApplicable; break;
	case EContextMenuState::Pressed:  bSlotExists = bPressedDesignApplicable; break;
	case EContextMenuState::Disabled: bSlotExists = bDisabledDesignApplicable; break;
	case EContextMenuState::Enabled:  bSlotExists = bEnabledDesignApplicable; break;
	case EContextMenuState::Normal:
	default:                          bSlotExists = bNormalDesignApplicable; break;
	}

	if (bSlotExists)
	{
		return IsStateSlotOverridden(Effective) ? &GetStateSlot(Effective) : nullptr;
	}

	return bOverrideBaseDesign ? &BaseDesign : nullptr;
}

const FContextMenuDesignStateOverride& FContextMenuDesignTargetOverride::GetStateSlot(EContextMenuState State) const
{
	switch (State)
	{
	case EContextMenuState::Hovered:  return Hovered;
	case EContextMenuState::Pressed:  return Pressed;
	case EContextMenuState::Disabled: return Disabled;
	case EContextMenuState::Enabled:  return Enabled;
	case EContextMenuState::Normal:
	default:                          return Normal;
	}
}

FContextMenuDesignStateOverride& FContextMenuDesignTargetOverride::GetStateSlot(EContextMenuState State)
{
	return const_cast<FContextMenuDesignStateOverride&>(static_cast<const FContextMenuDesignTargetOverride*>(this)->GetStateSlot(State));
}

bool FContextMenuButtonInstanceOverrides::HasAnything() const
{
	if (bOverrideShape)
	{
		for (const FContextMenuShapeTargetOverride& Entry : ShapePatch)
		{
			if (Entry.HasAnything())
			{
				return true;
			}
		}
	}

	if (bOverrideDesign)
	{
		for (const FContextMenuDesignTargetOverride& Entry : DesignPatch)
		{
			if (Entry.HasAnything())
			{
				return true;
			}
		}
	}

	return false;
}

// ==================================================================
// UContextMenuButton
// ==================================================================

TArray<TWeakObjectPtr<UContextMenuButton>> UContextMenuButton::EnableCapableButtons;
bool UContextMenuButton::bIsEnforcingEnableGroup = false;

UContextMenuButton::UContextMenuButton(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The stock button style pads its content a few pixels on every side, and pads Pressed a little further
	// still to fake a "push". The shape's Transform and Shape (Extrude, Inset and the rest) already give full,
	// explicit control over the button's look, so the native padding is zeroed here rather than left to
	// quietly offset every author's numbers by a few pixels nobody asked for.
	FButtonStyle Style = GetStyle();
	Style.NormalPadding = FMargin(0.0f);
	Style.PressedPadding = FMargin(0.0f);
	SetStyle(Style);
}

void UContextMenuButton::PostLoad()
{
	Super::PostLoad();

	// Parts a ready-made button kept outside the hierarchy (Buttons fix6-fix7) go back to being its content.
	RestoreHiddenParts();

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

	// The channel switches used to live on the target, above the five states they applied to. Push them down
	// into each state's own toggles, once. Detected by the switches differing from the defaults a freshly
	// created target has, so a target authored after the move isn't "migrated" back on top of itself: those
	// defaults are the only combination the old layout and the new one can't be told apart by, and they mean
	// the same thing in both.
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		const bool bLooksLegacy = !Target.bDriveTint || Target.bDriveOpacity || Target.bDriveBrush || Target.bDriveMaterial;
		if (!bLooksLegacy)
		{
			continue;
		}

		FContextMenuWidgetDesign* const States[] = { &Target.Normal, &Target.Hovered, &Target.Pressed, &Target.Disabled, &Target.Enabled };
		for (FContextMenuWidgetDesign* Design : States)
		{
			Design->bDriveTint = Target.bDriveTint;
			Design->bDriveOpacity = Target.bDriveOpacity;
			Design->bDriveBrushTexture = Target.bDriveBrush;
			Design->bDriveMaterial = Target.bDriveMaterial;
		}

		Target.bDriveTint = true;
		Target.bDriveOpacity = false;
		Target.bDriveBrush = false;
		Target.bDriveMaterial = false;
	}

	// Sound used to be a Design Target channel. Move anything stored that way into a Sound Set, which is where
	// sound lives now, and clear the old fields so this only ever happens once.
	FContextMenuSoundSet Recovered;
	bool bRecoveredAnySound = false;
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		if (!Target.bDriveSound)
		{
			continue;
		}

		FContextMenuWidgetDesign* const States[] = { &Target.Normal, &Target.Hovered, &Target.Pressed, &Target.Disabled, &Target.Enabled };
		FUISoundSlot* const SoundSlots[] = { &Recovered.Normal, &Recovered.Hovered, &Recovered.Pressed, &Recovered.Disabled, &Recovered.Enabled };
		for (int32 i = 0; i < UE_ARRAY_COUNT(States); ++i)
		{
			// First target to name a sound for a state wins it: the old channel could be driven from several
			// targets at once, which played the same state's sound more than once anyway.
			if (States[i]->Sound && !SoundSlots[i]->Sound)
			{
				SoundSlots[i]->Sound = States[i]->Sound;
				bRecoveredAnySound = true;
			}
			States[i]->Sound = nullptr;
		}

		Target.bDriveSound = false;
	}

	if (bRecoveredAnySound)
	{
		Recovered.Settings.SetName = TEXT("Recovered Design Sounds");
		Sounds.Add(Recovered);
	}

	// Design Targets used to author Normal/Hovered/Pressed/Disabled/Enabled directly, with nothing to fall
	// back to - Base Design now fills that role, and a state with its override left off simply follows it.
	// So a state that was actually authored under the old layout needs its override forced on here, or its
	// values would go from "always applied" to "ignored in favor of a blank Base Design" the moment this
	// loads. Detected the same way as the channel-switch migration above: by differing from what a freshly
	// constructed state looks like. A state already sitting at those defaults doesn't care either way, since
	// Base Design starts at the same defaults, so there's nothing for it to lose.
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		static const FContextMenuWidgetDesign FreshDesign;
		auto DiffersFromFreshDesign = [](const FContextMenuWidgetDesign& Design) -> bool
		{
			return Design.bDriveTint != FreshDesign.bDriveTint
				|| Design.Tint != FreshDesign.Tint
				|| Design.bDriveOutlineTint != FreshDesign.bDriveOutlineTint
				|| Design.OutlineTint != FreshDesign.OutlineTint
				|| Design.bDriveOpacity != FreshDesign.bDriveOpacity
				|| Design.Opacity != FreshDesign.Opacity
				|| Design.bDriveBrushTexture != FreshDesign.bDriveBrushTexture
				|| Design.BrushTexture != FreshDesign.BrushTexture
				|| Design.BrushImageSource != FreshDesign.BrushImageSource
				|| Design.BrushSVG != FreshDesign.BrushSVG
				|| Design.bDriveMaterial != FreshDesign.bDriveMaterial
				|| Design.Material != FreshDesign.Material
				|| Design.MaterialScalars.Num() != 0
				|| Design.MaterialVectors.Num() != 0;
		};

		if (!Target.bOverrideNormalDesign && DiffersFromFreshDesign(Target.Normal))
		{
			Target.bOverrideNormalDesign = true;
		}
		if (!Target.bOverrideHoveredDesign && DiffersFromFreshDesign(Target.Hovered))
		{
			Target.bOverrideHoveredDesign = true;
		}
		if (!Target.bOverridePressedDesign && DiffersFromFreshDesign(Target.Pressed))
		{
			Target.bOverridePressedDesign = true;
		}
		if (!Target.bOverrideDisabledDesign && DiffersFromFreshDesign(Target.Disabled))
		{
			Target.bOverrideDisabledDesign = true;
		}
		if (!Target.bOverrideEnabledDesign && DiffersFromFreshDesign(Target.Enabled))
		{
			Target.bOverrideEnabledDesign = true;
		}
	}

	// The first version of the instance overrides stored whole copies rather than a patch. A copy can't be
	// turned into a patch without knowing what it was copied from, and re-applying one is what caused
	// placements to stop tracking their parent, so the old data is dropped rather than migrated.
	InstanceOverrides.DesignOverride.Reset();
	InstanceOverrides.bHasBeenSeeded = false;

	SyncEditConditionMirrors();
}

#if WITH_EDITOR
void UContextMenuButton::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Auto-fill an override to match a target's own Base Shape whenever it is first turned on, the same way a
	// Design Target's overrides do - so it needs the same array-index lookup, since Shape Targets live in an
	// array too now rather than being fields directly on the button.
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideNormalShape)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideHoveredShape)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverridePressedShape)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideDisabledShape)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideEnabledShape))
	{
		const int32 TargetIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets).ToString());
		if (ShapeTargets.IsValidIndex(TargetIndex))
		{
			FContextMenuShapeTarget& Target = ShapeTargets[TargetIndex];
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideNormalShape) && Target.bOverrideNormalShape)
			{
				Target.NormalShape = Target.BaseShape;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideHoveredShape) && Target.bOverrideHoveredShape)
			{
				Target.HoveredShape = Target.BaseShape;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverridePressedShape) && Target.bOverridePressedShape)
			{
				Target.PressedShape = Target.BaseShape;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideDisabledShape) && Target.bOverrideDisabledShape)
			{
				Target.DisabledShape = Target.BaseShape;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, bOverrideEnabledShape) && Target.bOverrideEnabledShape)
			{
				Target.EnabledShape = Target.BaseShape;
			}
		}
	}

	// Same idea, one level down: Design Targets live in an array, so which target was edited has to be read
	// back out of the array index rather than being "this" the way the button's own Shape fields are.
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideNormalDesign)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideHoveredDesign)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverridePressedDesign)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideDisabledDesign)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideEnabledDesign))
	{
		const int32 TargetIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UContextMenuButton, DesignTargets).ToString());
		if (DesignTargets.IsValidIndex(TargetIndex))
		{
			FContextMenuDesignTarget& Target = DesignTargets[TargetIndex];
			if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideNormalDesign) && Target.bOverrideNormalDesign)
			{
				Target.Normal = Target.BaseDesign;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideHoveredDesign) && Target.bOverrideHoveredDesign)
			{
				Target.Hovered = Target.BaseDesign;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverridePressedDesign) && Target.bOverridePressedDesign)
			{
				Target.Pressed = Target.BaseDesign;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideDisabledDesign) && Target.bOverrideDisabledDesign)
			{
				Target.Disabled = Target.BaseDesign;
			}
			else if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuDesignTarget, bOverrideEnabledDesign) && Target.bOverrideEnabledDesign)
			{
				Target.Enabled = Target.BaseDesign;
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	// A Shape Target aimed at a vector shape (or at Self, once the button has a Background Shape) authors that shape's Look
	// itself - a Shape Preset has none to give - so it is switched to Custom, where the Looks are.
	auto UseCustomForLooks = [this](FContextMenuShapeTarget& Target)
	{
		if (Target.ShapeSource == EContextMenuButtonSettingSource::UseProjectSettings && GetLookClass(Target))
		{
			Target.ShapeSource = EContextMenuButtonSettingSource::Custom;
		}
	};
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FContextMenuShapeTarget, TargetWidgetName)
		&& PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets))
	{
		const int32 TargetIndex = PropertyChangedEvent.GetArrayIndex(GET_MEMBER_NAME_CHECKED(UContextMenuButton, ShapeTargets).ToString());
		if (ShapeTargets.IsValidIndex(TargetIndex))
		{
			UseCustomForLooks(ShapeTargets[TargetIndex]);
		}
	}
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UContextMenuButton, BackgroundShape))
	{
		for (FContextMenuShapeTarget& Target : ShapeTargets)
		{
			if (IsSelfTargetName(Target.TargetWidgetName))
			{
				UseCustomForLooks(Target);
			}
		}
	}

	// Every target's Look laid out for the shape it aims at, before anything below reads them.
	RefreshShapeTargetLooks();

	SyncEditConditionMirrors();
	SyncMaterialParametersEditor();

#if WITH_EDITOR
	// DISCARDED, not restored. ClearDesignPreviews puts every previewed widget back from these baselines,
	// and the baselines were captured from how the widget looked BEFORE this edit - so leaving them in
	// place means the next preview pass faithfully undoes whatever was just authored. That is why editing
	// the button's own style appeared to do nothing: the slider moved, the value stuck, and a tenth of a
	// second later the preview restored the old one over the top. Dropping them makes the next pass
	// re-capture from the freshly authored widget instead.
	DesignPreviewBaselines.Reset();
#endif

	// Unconditional now that there is no Is Instance Editable box to gate it. The Details panel is reading
	// this object rather than a preview of it, so the struct has to be filled here and then - doing it
	// later during construction would never be seen, or saved. Editing the button's own Shape/Design
	// re-runs it too, which is how the Design Target list on the patch keeps up with targets being added,
	// removed or renamed.
	SeedInstanceOverrides(InstanceOverrides);

	// Which Shape Targets actually take their Shape, for the Shape Status box. Same reason as above: the Details
	// panel reads this object.
	RebuildShapeTargetStatus();

	// The Details panel is reading THIS object, not the preview, so the status string has to be rebuilt
	// here as well or it only ever reflects whatever the preview last worked out.
	RebuildEffectiveButtonAnimations();

#if WITH_EDITOR
	// A committed edit rebuilds the preview on its own. An interactive one - a slider still being dragged -
	// never reaches the preview at all, which is what made scrubbing an animation useless: nothing moved
	// until the mouse came up. Shared with every other preview host rather than reimplemented here.
	UWidgetAnimDesignerPreview::NotifyPropertyChanged(this, PropertyChangedEvent);
#endif
}


void UContextMenuButton::OnCreationFromPalette()
{
	Super::OnCreationFromPalette();

	// Nothing is built here. The designer makes the dragged widget again for every drop preview while it is dragged over the
	// canvas, and parts made in the Widget Blueprint's tree for a preview it then discards stay behind in that tree - each
	// one an ensure on every compile ("was added but did not get a GUID"). So the button only notes that it wants its parts;
	// the editor module builds them once the button has really been placed (after the compile the drop causes, with no drag
	// in progress), and gives every part its GUID - see StyledButtonsEditor.
	bPendingDefaultContent = ShouldBuildDefaultContentWhenEmpty() && !HasAnyContent();
}

namespace ContextMenuButtonParts
{
	/** Root and everything inside it - panels' children and named slots' content. */
	void GatherWidgetAndChildren(UWidget* Root, TArray<UWidget*>& OutWidgets)
	{
		if (!Root)
		{
			return;
		}
		OutWidgets.Add(Root);
		UWidgetTree::ForWidgetAndChildren(Root, [&OutWidgets](UWidget* Child)
		{
			OutWidgets.Add(Child);
		});
	}

	/** Moves Part into NewOuter, under its own name while that is free there, else a numbered one. */
	void MovePart(UWidget* Part, UObject* NewOuter)
	{
		FName NewName = Part->GetFName();
		if (StaticFindObjectFast(UObject::StaticClass(), NewOuter, NewName))
		{
			NewName = MakeUniqueObjectName(NewOuter, Part->GetClass(), NewName);
		}
		Part->Modify();
		Part->Rename(*NewName.ToString(), NewOuter, REN_DontCreateRedirectors);
	}
}

bool UContextMenuButton::SetPartsHidden(bool bHide, TArray<UWidget*>& OutMovedParts)
{
	using namespace ContextMenuButtonParts;
	OutMovedParts.Reset();

	UWidgetTree* OwnerTree = GetTypedOuter<UWidgetTree>();
	if (!OwnerTree)
	{
		return false;
	}

	if (bHide)
	{
		UPanelSlot* ListedSlot = GetContentSlot();
		UWidget* Content = ListedSlot ? ListedSlot->Content.Get() : nullptr;
		if (!Content || HiddenContentSlot)
		{
			return false;
		}
		Modify();
		if (!PartsTree)
		{
			const FName TreeName = MakeUniqueObjectName(this, UWidgetTree::StaticClass(), TEXT("PartsTree"));
			PartsTree = NewObject<UWidgetTree>(this, TreeName, RF_Transactional);
		}
		PartsTree->Modify();
		ListedSlot->Modify();

		GatherWidgetAndChildren(Content, OutMovedParts);
		for (UWidget* Part : OutMovedParts)
		{
			MovePart(Part, PartsTree);
		}

		Slots.Remove(ListedSlot);
		HiddenContentSlot = ListedSlot;
		PartsTree->RootWidget = Content;
	}
	else
	{
		if (!ArePartsHidden() || GetChildrenCount() > 0)
		{
			return false;
		}
		UWidget* Content = GetHiddenContent();
		Modify();
		OwnerTree->Modify();
		PartsTree->Modify();

		GatherWidgetAndChildren(Content, OutMovedParts);
		for (UWidget* Part : OutMovedParts)
		{
			MovePart(Part, OwnerTree);
		}

		PartsTree->RootWidget = nullptr;
		Slots.Add(HiddenContentSlot);
		HiddenContentSlot = nullptr;
	}

	// Built again onto a live button (the slot is the same; only where the button finds it changed).
	if (MyButton.IsValid())
	{
		if (UButtonSlot* PartsSlot = Cast<UButtonSlot>(GetPartsSlot()))
		{
			PartsSlot->BuildSlot(MyButton.ToSharedRef());
		}
	}
	return OutMovedParts.Num() > 0;
}

EContextMenuDesignPreview UContextMenuButton::GetEffectiveDesignPreview() const
{
	return bDesignerPreviewDriven ? DesignerPreviewState : ActiveDesignPreview;
}

void UContextMenuButton::PlayDesignerPreviewState(EContextMenuDesignPreview State, bool bAnimate)
{
	using namespace ContextMenuButtonDesign;
	if (!IsDesignTime())
	{
		return;
	}

	const EContextMenuDesignPreview Shown = GetEffectiveDesignPreview();
	bDesignerPreviewDriven = true;
	DesignerPreviewState = State;

	const TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!bAnimate || !SlateWidget.IsValid() || State == EContextMenuDesignPreview::None)
	{
		if (bIsDesignerStateTimerActive)
		{
			bIsDesignerStateTimerActive = false;
			++DesignerStateSerial;
			RestoreRuntimeDesignBaselines();
		}
		ApplyDesignPreviews();
		CacheResolvedOverridesSnapshot();
		return;
	}

	// The runtime's own blend, run on the designer's copy: its targets are set up from the widgets as authored, put at the state
	// shown now, then sent to the new one - and a pick during a blend redirects it from wherever it has got to.
	const EContextMenuState Goal = PreviewToState(State);
	if (!bIsDesignerStateTimerActive)
	{
		ClearDesignPreviews();
		InitializeRuntimeShapeTargets();
		InitializeRuntimeDesignTargets();

		const EContextMenuState From = Shown == EContextMenuDesignPreview::None ? EContextMenuState::Normal : PreviewToState(Shown);
		CurrentShapeState = From;
		CurrentDesignState = From;
		ApplyTransitions(0.f, /*bInstant=*/true);

		bIsDesignerStateTimerActive = true;
		const int32 Serial = ++DesignerStateSerial;
		SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UContextMenuButton::TickDesignerPreviewState, Serial));
	}

	CurrentShapeState = Goal;
	CurrentDesignState = Goal;
	PushVisualStateToSlate(Goal);
}

void UContextMenuButton::StopDesignerPreviewState()
{
	if (!bDesignerPreviewDriven)
	{
		return;
	}
	bDesignerPreviewDriven = false;
	if (bIsDesignerStateTimerActive)
	{
		bIsDesignerStateTimerActive = false;
		++DesignerStateSerial;
		RestoreRuntimeDesignBaselines();
	}
	ApplyDesignPreviews();
	CacheResolvedOverridesSnapshot();
}

EActiveTimerReturnType UContextMenuButton::TickDesignerPreviewState(double InCurrentTime, float InDeltaTime, int32 Serial)
{
	if (Serial != DesignerStateSerial || !bIsDesignerStateTimerActive)
	{
		return EActiveTimerReturnType::Stop;
	}
	if (!IsDesignTime() || !GetCachedWidget().IsValid())
	{
		bIsDesignerStateTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}
	if (ApplyTransitions(InDeltaTime, /*bInstant=*/false))
	{
		FinishDesignerPreviewState();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void UContextMenuButton::FinishDesignerPreviewState()
{
	// The blend has arrived where the preview's own pass paints the state at once: handed back to it, so edits made from now on
	// repaint as before. The design channels go back to as authored first, for its baselines.
	bIsDesignerStateTimerActive = false;
	RestoreRuntimeDesignBaselines();
	ApplyDesignPreviews();
	CacheResolvedOverridesSnapshot();
}

void UContextMenuButton::RestoreRuntimeDesignBaselines()
{
	for (const FContextMenuRuntimeDesignTarget& Target : RuntimeDesignTargets)
	{
		UWidget* Widget = Target.Widget.Get();
		if (!Widget || !Target.bHasBaseline)
		{
			continue;
		}
		ApplyColorToWidget(Widget, Target.BaselineTint);
		ApplyOutlineTintToWidget(Widget, Target.BaselineOutlineTint);
		Widget->SetRenderOpacity(Target.BaselineOpacity);
		StopSVGOnWidget(Widget);
		if (UWidgetSVG* BaselineSVGAsset = Target.BaselineSVG.Get())
		{
			ShowSVGOnWidget(Widget, BaselineSVGAsset);
		}
		else if (UTexture2D* BaselineTexture = Target.BaselineBrushTexture.Get())
		{
			ApplyBrushTextureToWidget(Widget, BaselineTexture);
		}
		if (UMaterialInterface* BaselineMaterialObject = Target.BaselineMaterial.Get())
		{
			ApplyMaterialToWidget(Widget, BaselineMaterialObject);
		}
	}
	RuntimeShapeTargets.Reset();
	RuntimeDesignTargets.Reset();
}

bool UContextMenuButton::ConsumePendingDefaultContent()
{
	const bool bWasPending = bPendingDefaultContent;
	bPendingDefaultContent = false;
	return bWasPending;
}

void UContextMenuButton::BuildDefaultContentForEditor(TArray<UWidget*>& OutParts)
{
	OutParts.Reset();

	// Given content by hand meanwhile: that is the content.
	if (HasAnyContent())
	{
		return;
	}

	BuildDefaultContent();
	RefreshShapeTargetLooks();
	SyncEditConditionMirrors();
	RebuildShapeTargetStatus();

	TArray<ContextMenuButtonTree::FTargetableWidget> Built;
	ContextMenuButtonTree::Gather(this, FString(), /*bDescendIntoUserWidgets=*/false, Built);
	for (const ContextMenuButtonTree::FTargetableWidget& Entry : Built)
	{
		if (Entry.Widget && Entry.Widget != this)
		{
			OutParts.Add(Entry.Widget);
		}
	}
}

void UContextMenuButton::SyncMaterialParametersEditor()
{
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		UWidget* TargetWidget = FindWidgetInButtonTree(Target.TargetWidgetName);
		UMaterialInterface* DefaultMaterial = ExtractWidgetMaterial(TargetWidget);

		FContextMenuWidgetDesign* const States[] = { &Target.BaseDesign, &Target.Normal, &Target.Hovered, &Target.Pressed, &Target.Disabled, &Target.Enabled };
		for (FContextMenuWidgetDesign* Design : States)
		{
			UMaterialInterface* MatToSync = Design->Material.Get();
			
			// Inherit from Base Design if this state didn't override the material
			if (!MatToSync && Design != &Target.BaseDesign)
			{
				MatToSync = Target.BaseDesign.Material.Get();
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

bool UContextMenuButton::ArePartsHidden() const
{
	const UWidget* Content = GetHiddenContent();
	return Content && PartsTree && Content->IsIn(PartsTree);
}

bool UContextMenuButton::RestoreHiddenParts()
{
	if (!HiddenContentSlot)
	{
		return false;
	}

	// Hidden in the button's own tree: kept. Only the links between the button, the slot and the content are put right.
	if (ArePartsHidden())
	{
		UWidget* OwnContent = GetHiddenContent();
		bool bChanged = false;
		if (HiddenContentSlot->Parent.Get() != this)
		{
			HiddenContentSlot->Parent = this;
			bChanged = true;
		}
		if (OwnContent->Slot.Get() != HiddenContentSlot.Get())
		{
			OwnContent->Slot = HiddenContentSlot;
			bChanged = true;
		}
		if (PartsTree->RootWidget.Get() != OwnContent)
		{
			PartsTree->RootWidget = OwnContent;
			bChanged = true;
		}
		return bChanged;
	}

	UPanelSlot* KeptSlot = HiddenContentSlot;
	HiddenContentSlot = nullptr;

	// The parts are this button's own while they are in its tree and no other widget lists them as its content.
	UWidget* Content = KeptSlot->Content;
	const UPanelSlot* ClaimedBy = Content ? Content->Slot.Get() : nullptr;
	const bool bListedElsewhere = ClaimedBy && ClaimedBy != KeptSlot && ClaimedBy->Parent && ClaimedBy->Parent->GetChildIndex(Content) != INDEX_NONE;
	const bool bOwnParts = IsValid(Content) && Content->GetOuter() == GetOuter() && !bListedElsewhere && GetChildrenCount() == 0;
	if (bOwnParts)
	{
		KeptSlot->Parent = this;
		Content->Slot = KeptSlot;
		Slots.Add(KeptSlot);
		if (MyButton.IsValid())
		{
			if (UButtonSlot* PartsSlot = Cast<UButtonSlot>(KeptSlot))
			{
				PartsSlot->BuildSlot(MyButton.ToSharedRef());
			}
		}
		return true;
	}

#if WITH_EDITORONLY_DATA
	// Not its own: built again, like a button just dropped from the Palette.
	if (GetChildrenCount() == 0)
	{
		bPendingDefaultContent = ShouldBuildDefaultContentWhenEmpty();
	}
#endif
	return true;
}

bool UContextMenuButton::IsVisibilityInteractive(ESlateVisibility Visibility)
{
	// Hit-test invisible counts as gone for this purpose. A button the player cannot reach is not in any
	// state worth latching, and coming back from it is the same fresh start as coming back from Collapsed.
	return Visibility == ESlateVisibility::Visible || Visibility == ESlateVisibility::SelfHitTestInvisible;
}

void UContextMenuButton::SetVisibility(ESlateVisibility InVisibility)
{
	const bool bWillBeInteractive = IsVisibilityInteractive(InVisibility);

	Super::SetVisibility(InVisibility);

	// Only the transition back into view. Re-asserting Visible on an already-visible button is not a
	// reappearance and must not clear a latch mid-hover.
	if (bWillBeInteractive && !bWasVisibilityInteractive)
	{
		FUISoundPlayer::ResetEventLatches(RuntimeSounds);

		// Not re-seeded here: the next real state change is what should sound, and with
		// bSilentOnFirstEvent that first one is recorded quietly, exactly as it is on construct.
	}

	bWasVisibilityInteractive = bWillBeInteractive;
}

const TCHAR* UContextMenuButton::GetDispatcherKeywordForEvent(EContextMenuButtonEvent Event)
{
	switch (Event)
	{
	case EContextMenuButtonEvent::EnabledStateChanged:   return TEXT("Enabled");
	case EContextMenuButtonEvent::HoverChanged:          return TEXT("Hover");
	case EContextMenuButtonEvent::PressDurationCompleted:return TEXT("Press");
	case EContextMenuButtonEvent::Clicked:
	default:                                             return TEXT("Click");
	}
}

int32 UContextMenuButton::GetDispatcherParamCountForEvent(EContextMenuButtonEvent Event)
{
	switch (Event)
	{
	case EContextMenuButtonEvent::Clicked:                return 2; // bShouldDestroyCard, bEnabling
	case EContextMenuButtonEvent::EnabledStateChanged:    return 1; // bIsEnabled
	case EContextMenuButtonEvent::HoverChanged:           return 1; // bHovered
	case EContextMenuButtonEvent::PressDurationCompleted:
	default:                                              return 0;
	}
}

bool UContextMenuButton::IsUsableDispatcherSignature(const UFunction* Signature, int32 MaxBoolParams, int32& OutParamCount)
{
	OutParamCount = 0;

	if (!Signature)
	{
		return false;
	}

	int32 BoolCount = 0;

	for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		// A return value would mean this isn't an Event Dispatcher signature at all.
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			return false;
		}

		if (!CastField<FBoolProperty>(*It))
		{
			return false;
		}

		++BoolCount;
	}

	// No parameters is always fine and is what most placements want - they care that the button was
	// clicked, not what it reported. Anything else has to match the event exactly, because a dispatcher
	// taking one bool where the event offers two would silently receive the wrong one.
	if (BoolCount != 0 && BoolCount != MaxBoolParams)
	{
		return false;
	}

	OutParamCount = BoolCount;
	return true;
}

void UContextMenuButton::ResolveEventDispatcherBindings()
{
	bHasResolvedDispatcherBindings = true;
	CachedDispatcherBindings.Reset();
	EventDispatcherBindingStatus.Reset();

	const FString ButtonName = GetName();

	TArray<FString> Report;
	TArray<FString> Mismatches;

	static const EContextMenuButtonEvent AllEvents[] =
	{
		EContextMenuButtonEvent::Clicked,
		EContextMenuButtonEvent::EnabledStateChanged,
		EContextMenuButtonEvent::HoverChanged,
		EContextMenuButtonEvent::PressDurationCompleted
	};

	// Outwards from the button. The nearest owner is the widget that contains it; the ones past it are the
	// widgets that merely placed that widget, which is the case this whole mechanism exists for.
	UObject* Search = GetOuter();
	int32 Depth = 0;

	while (Search && Depth < MaxOwnerSearchDepth)
	{
		++Depth;

		UUserWidget* Owner = Cast<UUserWidget>(Search);
		if (!Owner)
		{
			Search = Search->GetOuter();
			continue;
		}

		for (const EContextMenuButtonEvent Event : AllEvents)
		{
			const TCHAR* Keyword = GetDispatcherKeywordForEvent(Event);
			const int32 MaxParams = GetDispatcherParamCountForEvent(Event);

			FMulticastDelegateProperty* NameMatched = nullptr;
			int32 NameMatchedParams = 0;

			FMulticastDelegateProperty* SoleKeywordMatch = nullptr;
			int32 SoleKeywordParams = 0;
			int32 KeywordMatchCount = 0;

			for (TFieldIterator<FMulticastDelegateProperty> It(Owner->GetClass()); It; ++It)
			{
				FMulticastDelegateProperty* Prop = *It;
				const FString PropName = Prop->GetName();

				// Every candidate has to at least look like On...<Event>. Anything else isn't addressed to
				// this system and shouldn't be reported on either way.
				if (!ContextMenuButtonOverrideNaming::MatchesDispatcherEventOnly(PropName, Keyword))
				{
					continue;
				}

				int32 ParamCount = 0;
				const bool bSignatureOk = IsUsableDispatcherSignature(Prop->SignatureFunction, MaxParams, ParamCount);

				const bool bMatchesButton = ContextMenuButtonOverrideNaming::MatchesDispatcherConvention(PropName, ButtonName, Keyword);

				if (!bSignatureOk)
				{
					// Only worth complaining about when the name says it was meant for this button. Every
					// other dispatcher in the project happens to contain "Click" too.
					if (bMatchesButton)
					{
						Mismatches.Add(FString::Printf(
							TEXT("'%s' on %s is named correctly but its signature doesn't fit. Give it no parameters at all, or exactly %d bool(s)."),
							*PropName, *Owner->GetClass()->GetName(), MaxParams));
					}
					continue;
				}

				if (bMatchesButton)
				{
					NameMatched = Prop;
					NameMatchedParams = ParamCount;
					break;
				}

				SoleKeywordMatch = Prop;
				SoleKeywordParams = ParamCount;
				++KeywordMatchCount;
			}

			// Named after the button wins outright. Failing that, exactly one candidate for this event is
			// unambiguous; several are not, and picking one would wire up the wrong button silently.
			FMulticastDelegateProperty* Chosen = NameMatched;
			int32 ChosenParams = NameMatchedParams;

			if (!Chosen && KeywordMatchCount == 1)
			{
				Chosen = SoleKeywordMatch;
				ChosenParams = SoleKeywordParams;
			}

			if (!Chosen)
			{
				if (KeywordMatchCount > 1)
				{
					Mismatches.Add(FString::Printf(
						TEXT("%s has %d dispatchers for '%s' and none follows the convention for this button - none used. Rename one to 'On%s%s'."),
						*Owner->GetClass()->GetName(), KeywordMatchCount, Keyword, *ButtonName, Keyword));
				}
				continue;
			}

			FContextMenuButtonDispatcherBinding& Binding = CachedDispatcherBindings.AddDefaulted_GetRef();
			Binding.Owner = Owner;
			Binding.Property = Chosen;
			Binding.Event = Event;
			Binding.ParamCount = ChosenParams;

			Report.Add(FString::Printf(TEXT("%s -> '%s' on %s"),
				Keyword, *Chosen->GetName(), *Owner->GetClass()->GetName()));

			// Always continues on to every ancestor above this one for the remaining events - a wrapper
			// with its own dispatcher for this event never shields the screen that placed it from also
			// having one.
		}

		Search = Search->GetOuter();
	}

	if (Report.Num() == 0 && Mismatches.Num() == 0)
	{
		EventDispatcherBindingStatus = FString::Printf(
			TEXT("No Event Dispatchers found. Add one to the widget that contains this button, named On{Button}{Event} - e.g. 'On%sClicked'."),
			*ButtonName);
		return;
	}

	EventDispatcherBindingStatus = FString::Join(Report, TEXT("\n"));

	if (Mismatches.Num() > 0)
	{
		if (!EventDispatcherBindingStatus.IsEmpty())
		{
			EventDispatcherBindingStatus += TEXT("\n\n");
		}
		EventDispatcherBindingStatus += FString::Join(Mismatches, TEXT("\n"));
	}
}

void UContextMenuButton::BroadcastToOwnerDispatchers(EContextMenuButtonEvent Event, bool bArg0, bool bArg1)
{
	if (!bHasResolvedDispatcherBindings)
	{
		ResolveEventDispatcherBindings();
	}

	for (const FContextMenuButtonDispatcherBinding& Binding : CachedDispatcherBindings)
	{
		if (Binding.Event != Event || !Binding.Property)
		{
			continue;
		}

		UObject* Owner = Binding.Owner.Get();
		if (!Owner)
		{
			continue;
		}

		const void* ValuePtr = Binding.Property->ContainerPtrToValuePtr<void>(Owner);
		const FMulticastScriptDelegate* Delegate = Binding.Property->GetMulticastDelegate(ValuePtr);
		if (!Delegate || !Delegate->IsBound())
		{
			continue;
		}

		UFunction* Signature = Binding.Property->SignatureFunction;

		if (Binding.ParamCount == 0 || !Signature || Signature->ParmsSize == 0)
		{
			Delegate->ProcessMulticastDelegate<UObject>(nullptr);
		}
		else
		{
			// Built through the signature's own properties rather than as a packed struct: a Blueprint bool
			// parameter is a bitfield, and writing one by hand would be guessing at its mask.
			void* Params = FMemory_Alloca(Signature->ParmsSize);
			FMemory::Memzero(Params, Signature->ParmsSize);

			int32 Index = 0;
			for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				if (FBoolProperty* BoolProp = CastField<FBoolProperty>(*It))
				{
					BoolProp->SetPropertyValue_InContainer(Params, Index == 0 ? bArg0 : bArg1);
				}
				++Index;
			}

			Delegate->ProcessMulticastDelegate<UObject>(Params);
		}
	}
}

void UContextMenuButton::SyncEditConditionMirrors()
{
	// Only the Enabled state is ever grayed/hidden; the other states always apply. Every target shares the same
	// mirror, since Enabled Matches Hover Shape is a button-wide setting.
	const bool bEnabledShapeApplicable = bButtonCanBeEnabled && !bEnabledMatchesHoverShape;
	for (FContextMenuShapeTarget& Target : ShapeTargets)
	{
		Target.EnabledShape.bIsApplicable = bEnabledShapeApplicable;
	}

	for (FContextMenuSoundSet& Set : Sounds)
	{
		Set.bEnabledSoundApplicable = bButtonCanBeEnabled;
	}

	const bool bEnabledDesignApplicable = bButtonCanBeEnabled && !bEnabledMatchesHoverDesign;
	for (FContextMenuDesignTarget& Target : DesignTargets)
	{
		// The only mirror left. Every other field now carries its own inline toggle, and a preset-driven
		// target hides its states outright rather than graying each field from up here.
		Target.bEnabledDesignApplicable = bEnabledDesignApplicable;
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

	// The Background Shape as authored, before any state's Look is written over it below.
	SyncBackgroundShape();

	// Before anything reads Shape or Design below. This only works out which patch is in force; the merge
	// itself happens at read time, so both the runtime state machine and the designer previews further down
	// go through the same resolution and can't end up showing different things.
	ResolveInstanceOverrides();

	FUISoundPlayer::ResizeRuntimes(this, RuntimeSounds, Sounds.Num());

	SyncEditConditionMirrors();

	// Design time included, deliberately: setting Button Tag should show you the label it resolves to in
	// the viewport, and fill in Action Status, without a compile. ApplyButtonData does not instantiate
	// actions at design time - see there.
	if (!bHasResolvedButtonData || IsDesignTime())
	{
		ApplyButtonData();
	}

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

		if (!bHasInitializedRuntimeShapeTargets)
		{
			InitializeRuntimeShapeTargets();
			bHasInitializedRuntimeShapeTargets = true;
		}

		if (!bHasInitializedRuntimeDesignTargets)
		{
			InitializeRuntimeDesignTargets();
			bHasInitializedRuntimeDesignTargets = true;
		}

		UpdateButtonState(/*bInstant=*/true);

		RebuildEffectiveButtonAnimations();

		// Captured, not re-captured: the first pass records the authored pose, and every pass after that
		// would be reading back whatever frame the animation happens to be on.
		UWidgetAnimLibrary::CaptureWidgetAnimBaselines(this, FlattenedAnimIn, FlattenedAnimOut, ButtonAnimBaselines);

		if (!bHasPlayedAnimInOnce && bPlayAnimInOnConstruct && FlattenedAnimIn.Num() > 0)
		{
			bHasPlayedAnimInOnce = true;
			PlayButtonAnimIn();
		}
	}
#if WITH_EDITOR
	else
	{
		// Design time: no state machine runs, so the Show Preview dropdowns are what drive the widgets here.
		// Resolved again immediately beforehand: in a parent designer the placement's override values can land
		// on the owning widget after this button's tree was first built, and the preview has to see them.
		ResolveInstanceOverrides();
		ApplyDesignPreviews();
		CacheResolvedOverridesSnapshot();
		RebuildShapeTargetStatus();

		// Belt and braces for the case this whole comment is about: the designer does not reliably
		// re-synchronize a button that lives inside a nested widget when only that nested widget's
		// exposed variable changed, so from here the button watches for the change itself.
		StartDesignPreviewWatcher();

		// Registers with WidgetAnimDesignerSync, rebuilds the stacks, re-reads the baselines and starts or
		// stops the preview. The same call every animation host makes; EnsurePreviewTimer is overridden on
		// this class so it reuses StartButtonAnimTimer rather than registering a timer of its own.
		UWidgetAnimDesignerPreview::Synchronize(this);

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

	// A ready-made button built at runtime with nothing in it builds what it is made of first, so the slot below has it.
	if (!IsDesignTime() && !HasAnyContent() && ShouldBuildDefaultContentWhenEmpty())
	{
		BuildDefaultContent();
		RefreshShapeTargetLooks();
	}

	MyButton = NewButton;
	MyContextMenuSlateButton = NewButton;

	// The Background Shape's widget is only ever made here, once there is a Slate button to paint it - never on the
	// Widget Blueprint's own template.
	SyncBackgroundShape();

	// Parts hidden from the Hierarchy live in the button's own tree, which the designer doesn't walk when it marks its widgets
	// as being designed: they take the button's flags from it, before they are built.
#if WITH_EDITOR
	if (UWidget* HiddenRoot = GetHiddenContent())
	{
		const EWidgetDesignFlags DesignFlags = GetDesignerFlags();
		HiddenRoot->SetDesignerFlags(DesignFlags);
		UWidgetTree::ForWidgetAndChildren(HiddenRoot, [DesignFlags](UWidget* Part)
		{
			Part->SetDesignerFlags(DesignFlags);
		});
	}
#endif

	// The content slot - or, with the parts hidden from the Hierarchy, the slot kept for them - builds the content.
	if (UButtonSlot* PartsSlot = Cast<UButtonSlot>(GetPartsSlot()))
	{
		PartsSlot->BuildSlot(NewButton);
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
	bIsButtonAnimTimerActive = false;
	bIsTargetSVGTimerActive = false;
#if WITH_EDITOR
	bIsDesignPreviewWatcherActive = false;
#endif
	bHasNavigationFocus = false;
	bHoverShapeLatched = false;
	bHoverDesignLatched = false;
	UnbindSlateFocusChanging();
	UnregisterFromEnableGroups();
	MyContextMenuSlateButton.Reset();
	if (BackgroundShapeWidget)
	{
		BackgroundShapeWidget->SetExternalPainter(nullptr);
	}

	// Parts hidden from the Hierarchy aren't among the panel's slots, so the panel doesn't release them itself.
	if (bReleaseChildren && HiddenContentSlot)
	{
		HiddenContentSlot->ReleaseSlateResources(bReleaseChildren);
	}

	Super::ReleaseSlateResources(bReleaseChildren);
}

void UContextMenuButton::OnWidgetRebuilt()
{
	Super::OnWidgetRebuilt();

	// SVGs drawn into Image / Border / Button targets keep following their size on the new Slate widget.
	StartTargetSVGTimer();

	ResolveInstanceOverrides();

	// The chain above the button is only fully established once it has been built into a tree, and it can
	// change between builds, so the matched dispatchers are thrown away and found again on next use.
	bHasResolvedDispatcherBindings = false;
	CachedDispatcherBindings.Reset();

#if WITH_EDITOR
	if (IsDesignTime())
	{
		ApplyDesignPreviews();
		CacheResolvedOverridesSnapshot();
		StartDesignPreviewWatcher();

		if (ShouldRunDesignerAnimPreview())
		{
			StartButtonAnimTimer();
		}
	}
#endif

	// A rebuild replaces the Slate widget the previous timer was registered against, so anything still
	// mid-animation needs a new one.
	if (ButtonAnimPlayer.Tracks.Num() > 0)
	{
		StartButtonAnimTimer();
	}
}

void UContextMenuButton::BeginDestroy()
{
	UnregisterFromEnableGroups();
	CancelPendingSounds();
	UWidgetAnimDesignerPreview::Shutdown(this);
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

	BroadcastToOwnerDispatchers(EContextMenuButtonEvent::HoverChanged, /*bHovered=*/true);
}

void UContextMenuButton::HandleUnhovered()
{
	// Cursor left: the next hover may show Hovered shape/designs again.
	bHoverShapeLatched = false;
	bHoverDesignLatched = false;

	UpdateButtonState();

	BroadcastToOwnerDispatchers(EContextMenuButtonEvent::HoverChanged, /*bHovered=*/false);
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
				if (bRepeatWhileHeld)
				{
					World->GetTimerManager().SetTimer(HoldFrequencyTimerHandle, this, &UContextMenuButton::OnHoldTimerTick, CurrentHoldFrequency, false);
				}
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
			// Back to empty for whatever shows the progress. (Only the progress: Press Duration, the time a press takes, is
			// the button's setting and stays as it is - zeroing it made every press after the first complete at once.)
			if (bPressDurationProgressResetsOnCancelled)
			{
				BroadcastPressProgress(0.0f);
			}

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

	// Found again on every press, so renaming or replacing the Progress Widget is picked up.
	CachedPressProgressWidget = !PressProgressWidgetName.IsNone() ? FindWidgetInButtonTree(PressProgressWidgetName) : nullptr;

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

	BroadcastPressProgress(0.0f);
}

void UContextMenuButton::BroadcastPressProgress(float Progress)
{
	OnContextPressDurationProgress.Broadcast(Progress);

	UWidget* Widget = CachedPressProgressWidget.Get();
	if (UProgressBar* Bar = Cast<UProgressBar>(Widget))
	{
		Bar->SetPercent(Progress);
	}
	else if (UShapeWidget* Shape = GetLookWidget(Widget))
	{
		Shape->SetShapeValue(PressProgressValue, FMath::Lerp(static_cast<float>(PressProgressRange.X), static_cast<float>(PressProgressRange.Y), Progress));
		InvalidateLookWidget(Shape);
	}
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

	BroadcastPressProgress(GetPressDurationProgress());
	return true;
}

void UContextMenuButton::StartHoldFrequencyTimer()
{
	StopPressDuration();
	BroadcastPressProgress(1.0f);
	
	OnContextPressDurationCompleted.Broadcast();

	// Back to empty once it has completed - the progress only, as on cancel.
	if (bPressDurationProgressResetsOnCompleted)
	{
		BroadcastPressProgress(0.0f);
	}

	BroadcastToOwnerDispatchers(EContextMenuButtonEvent::PressDurationCompleted);

	UWorld* World = GetWorld();
	if (World && bRepeatWhileHeld)
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

	// Counted for a state machine playing on the button: its built-in Clicked fires as this goes up.
	++ActionCount;

	UInfoCardWidget* Card = bShouldDestroyCard ? GetOwningCard() : nullptr;

	// Flip before broadcasting, so bEnabling (and Get Enabled State inside the event) is the state being entered.
	bool bEnabling = false;
	if (bButtonCanBeEnabled)
	{
		if (!bHasToggledEnablingThisPress)
		{
			bHasToggledEnablingThisPress = true;
			CommitEnabledState(bClickOnlyEnables ? true : !GetEnabledState());
		}
		bEnabling = GetEnabledState();
	}

	// After the enabled flip, so a Blueprint listening on the event and the sound agree about the new state.
	PlayClickSounds();

	// Before the delegate rather than after it, and before the card is closed further down: an action is
	// this button's own behavior, and anything bound to On Context Clicked is a reaction to the button
	// having done its job. A card that closes on click would otherwise be tearing down the widget the
	// actions are still reading payloads from.
	ExecuteTaggedActions();

	OnContextClicked.Broadcast(bShouldDestroyCard, bEnabling);

	// Broadcast as well, never instead: a widget that already binds OnContextClicked keeps working untouched.
	BroadcastToOwnerDispatchers(EContextMenuButtonEvent::Clicked, bShouldDestroyCard, bEnabling);

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

// ==================================================================
// Action
// ==================================================================

const FContextMenuButtonData* UContextMenuButton::FindButtonData(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	return Settings ? Settings->ButtonActions.Find(Tag) : nullptr;
}

FString UContextMenuButton::DescribeButtonTag(const FGameplayTag& Tag)
{
	if (!Tag.IsValid())
	{
		return TEXT("No tag set.");
	}

	const FContextMenuButtonData* Data = FindButtonData(Tag);
	if (!Data)
	{
		// Not an error worth a warning on its own - a tag can be picked before its entry is added - but it
		// is the single most likely reason a button "does nothing", so it says so plainly.
		return FString::Printf(TEXT("%s\n  Not found in Project Settings > Info Card > Button Actions."), *Tag.ToString());
	}

	TStringBuilder<512> Builder;
	Builder.Append(*Tag.ToString());

	
	const FString ResolvedText_Left   = Data->ButtonText_Left.ToString();
	const FString ResolvedText_Center = Data->ButtonText_Center.ToString();
	const FString ResolvedText_Right  = Data->ButtonText_Right.ToString();
	
	TArray<FString> StringPieces;

	if (!ResolvedText_Left.IsEmpty())   { StringPieces.Add(ResolvedText_Left); }
	if (!ResolvedText_Center.IsEmpty()) { StringPieces.Add(ResolvedText_Center); }
	if (!ResolvedText_Right.IsEmpty())  { StringPieces.Add(ResolvedText_Right); }

	
	const FString ResolvedText = FString::Join(StringPieces, TEXT(", "));

	Builder.Append(TEXT("\n  Text: "));
	Builder.Append(ResolvedText.IsEmpty() ? TEXT("(empty)") : *ResolvedText);

	Builder.Append(TEXT("\n  Actions: "));
	if (Data->Actions.IsEmpty())
	{
		Builder.Append(TEXT("(none)"));
	}
	else
	{
		for (int32 i = 0; i < Data->Actions.Num(); ++i)
		{
			if (i > 0)
			{
				Builder.Append(TEXT(", "));
			}
			Builder.Append(Data->Actions[i] ? *Data->Actions[i]->GetName() : TEXT("(empty)"));
		}
	}

	Builder.Append(TEXT("\n  Expected payloads: "));
	if (Data->ExpectedPayloads.IsEmpty())
	{
		Builder.Append(TEXT("(none)"));
	}
	else
	{
		for (int32 i = 0; i < Data->ExpectedPayloads.Num(); ++i)
		{
			Builder.Appendf(TEXT("\n    %d. %s"), i,
				Data->ExpectedPayloads[i] ? *Data->ExpectedPayloads[i]->GetName() : TEXT("(empty)"));
		}
	}

	return Builder.ToString();
}

void UContextMenuButton::InitializeFromButtonTag(FGameplayTag InButtonTag)
{
	ButtonTag = InButtonTag;

	// Cleared rather than left for ApplyButtonData to overwrite, so re-initializing a pooled button with a
	// different tag can never leave the previous tag's action instances running alongside the new ones.
	ResolvedActions.Reset();
	bHasResolvedButtonData = false;

	ApplyButtonData();
}

void UContextMenuButton::RefreshButtonAction()
{
	bHasResolvedButtonData = false;
	ApplyButtonData();
}

void UContextMenuButton::ApplyButtonData()
{
	ExpectedPayloads.Reset();
	ResolvedActions.Reset();

	if (const FContextMenuButtonData* Data = FindButtonData(ButtonTag))
	{
		ExpectedPayloads = Data->ExpectedPayloads;

		// Never at design time. A UButtonAction is a class the project wrote, and constructing one against
		// an editor preview - which is built and thrown away constantly - is not something a project should
		// have to defend against. The label is all the designer needs to see.
		if (!IsDesignTime())
		{
			ResolvedActions.Reserve(Data->Actions.Num());
			for (const TSubclassOf<UButtonAction>& ActionClass : Data->Actions)
			{
				if (!ActionClass)
				{
					continue;
				}

				if (UButtonAction* Action = NewObject<UButtonAction>(this, ActionClass))
				{
					ResolvedActions.Add(Action);
				}
			}
		}
		
		// 1. Gather all non-empty text pieces into an array
		TArray<FText> TextPieces;

		if (!Data->ButtonText_Left.IsEmpty())
			TextPieces.Add(Data->ButtonText_Left);

		if (!Data->ButtonText_Center.IsEmpty())
			TextPieces.Add(Data->ButtonText_Center);
		
		if (!Data->ButtonText_Right.IsEmpty())
			TextPieces.Add(Data->ButtonText_Right);
		
		FText Delimiter = FText::FromString(TEXT(", "));

		FText ResolvedText = FText::Join(Delimiter, TextPieces);
	}

	bHasResolvedButtonData = true;
	RebuildButtonActionStatus();
}

UTextBlock* UContextMenuButton::ResolveButtonTextBlock()
{
	if (!ButtonTextWidgetName.IsNone())
	{
		return Cast<UTextBlock>(FindWidgetInButtonTree(ButtonTextWidgetName));
	}

	// Unset means "work it out", which is only safe when the answer is unambiguous. One text block under
	// the button is the ordinary shape of a context menu button and covers almost every case; two or more
	// and guessing would silently overwrite the wrong one, so the designer is asked to name it instead.
	TArray<ContextMenuButtonTree::FTargetableWidget> All;
	ContextMenuButtonTree::Gather(this, FString(), /*bDescendIntoUserWidgets=*/true, All);

	UTextBlock* Found = nullptr;
	for (const ContextMenuButtonTree::FTargetableWidget& Candidate : All)
	{
		if (UTextBlock* Text = Cast<UTextBlock>(Candidate.Widget))
		{
			if (Found)
			{
				return nullptr;
			}
			Found = Text;
		}
	}

	return Found;
}

void UContextMenuButton::ApplyButtonTextFromData(const FText& ButtonText)
{
	if (ButtonText.IsEmpty())
	{
		// A tag that supplies only actions has no opinion about the label, and stamping an empty FText
		// over authored text would silently blank every button using it.
		return;
	}

	if (UTextBlock* TextBlock = ResolveButtonTextBlock())
	{
		TextBlock->SetText(ButtonText);
	}
}

TArray<UContextButtonPayload*> UContextMenuButton::ResolveActionPayloads() const
{
	TArray<UContextButtonPayload*> Resolved;
	Resolved.Reserve(ExpectedPayloads.Num());

	const UInfoCardWidget* Card = const_cast<UContextMenuButton*>(this)->GetOwningCard();

	for (const TSubclassOf<UContextButtonPayload>& Expected : ExpectedPayloads)
	{
		UContextButtonPayload* Match = nullptr;

		if (Expected && Card)
		{
			for (const TObjectPtr<UNameTBD_Payload>& Payload : Card->Payloads)
			{
				// IsA rather than an exact class match, so a project that subclasses
				// UContextButtonPayloadText to add fields still satisfies an expectation of the base.
				if (Payload && Payload->IsA(Expected))
				{
					Match = Cast<UContextButtonPayload>(Payload.Get());
					if (Match)
					{
						break;
					}
				}
			}
		}

		// Added even when null. An action is written against Expected Payloads by position, and skipping a
		// missing one would shift every slot after it onto the wrong class - far worse than a null to check.
		Resolved.Add(Match);
	}

	return Resolved;
}

void UContextMenuButton::ExecuteTaggedActions()
{
	if (ResolvedActions.IsEmpty())
	{
		return;
	}

	const TArray<UContextButtonPayload*> Payloads = ResolveActionPayloads();

	for (int32 i = 0; i < Payloads.Num(); ++i)
	{
		if (!Payloads[i] && ExpectedPayloads.IsValidIndex(i) && ExpectedPayloads[i])
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Context Menu Button '%s' (%s) expected a %s payload on its card and found none. The action is still run, with that slot null."),
				*GetName(), *ButtonTag.ToString(), *ExpectedPayloads[i]->GetName());
		}
	}

	// A copy, because an action is free to do something that rebuilds this button - closing the card is
	// the obvious one - and iterating the live array while that happens is how you get a crash nobody can
	// reproduce.
	TArray<TObjectPtr<UButtonAction>> ActionsToRun = ResolvedActions;

	for (const TObjectPtr<UButtonAction>& Action : ActionsToRun)
	{
		if (UButtonAction* LiveAction = Action.Get())
		{
			IContextMenuInterface::Execute_ExecuteAction(LiveAction, this, Payloads);
		}
	}

	// Which payloads were actually found is the interesting half of the status, and it is only knowable
	// once something has asked for them.
	RebuildButtonActionStatus();
}

void UContextMenuButton::RebuildButtonActionStatus()
{
	if (!ButtonTag.IsValid())
	{
		ButtonActionStatus = TEXT("No Button Tag set. This button clicks and broadcasts normally; it just has no actions of its own.");
		return;
	}

	TStringBuilder<768> Builder;
	Builder.Append(*DescribeButtonTag(ButtonTag));

	if (IsDesignTime())
	{
		// Nothing to report about a card that does not exist yet, and saying so is better than an empty
		// section a designer reads as "no payloads are needed".
		Builder.Append(TEXT("\n  Payloads on card: (not resolved at design time)"));
		ButtonActionStatus = Builder.ToString();
		return;
	}

	Builder.Appendf(TEXT("\n  Actions instantiated: %d"), ResolvedActions.Num());

	if (!ExpectedPayloads.IsEmpty())
	{
		Builder.Append(TEXT("\n  Payloads on card:"));

		const TArray<UContextButtonPayload*> Resolved = ResolveActionPayloads();
		for (int32 i = 0; i < ExpectedPayloads.Num(); ++i)
		{
			const UClass* Expected = ExpectedPayloads[i];
			const bool bFound = Resolved.IsValidIndex(i) && Resolved[i] != nullptr;

			Builder.Appendf(TEXT("\n    %d. %s - %s"),
				i,
				Expected ? *Expected->GetName() : TEXT("(empty)"),
				bFound ? TEXT("found") : TEXT("MISSING"));
		}
	}

	ButtonActionStatus = Builder.ToString();
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

float UContextMenuButton::GetPressDurationProgress() const
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
		// An instant update is a re-seed (first build, a refresh, a property change), not something the player
		// did, so the state is recorded without being announced.
		HandleStateSounds(NewDesignState, /*bSuppressPlayback=*/true);

		CurrentShapeState = NewShapeState;
		CurrentDesignState = NewDesignState;
		ApplyTransitions(0.0f, /*bInstant=*/true);
		return;
	}

	HandleStateSounds(NewDesignState, /*bSuppressPlayback=*/false);

	if (CurrentShapeState != NewShapeState || CurrentDesignState != NewDesignState)
	{
		// Starting the blend from wherever each target currently sits, and re-arming its delay, now happens
		// per target inside ApplyShapeTarget - it has to, since every target can carry its own speed/curve/
		// delay. All this has to do is record the new state and make sure a frame actually runs to chase it.
		CurrentShapeState = NewShapeState;
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

	const bool bIsStateEnabled = GetEnabledState();

	if (IsHovered() || bHasNavigationFocus)
	{
		const bool bAllowHoverWhileEnabled = bForDesign ? bHoverDesignWhileEnabled : bHoverShapeWhileEnabled;
		const bool bLatched = bForDesign ? bHoverDesignLatched : bHoverShapeLatched;

		if (!bLatched && (!bIsStateEnabled || bAllowHoverWhileEnabled))
		{
			return EContextMenuState::Hovered;
		}
	}

	return bIsStateEnabled ? EContextMenuState::Enabled : EContextMenuState::Normal;
}

void UContextMenuButton::RefreshButtonState(bool bInstant)
{
	if (IsDesignTime())
	{
		return;
	}

	InitializeRuntimeShapeTargets();
	bHasInitializedRuntimeShapeTargets = true;

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

// ------------------------------------------------------------------
// Sound
// ------------------------------------------------------------------
//
// The mechanics - repeat limiting, pitch jitter, the delay timer - all live in FUISoundPlayer, shared with
// the info card system. What's left here is only the part that's actually about buttons: which slot a state
// maps to, and when a state counts as entered.

void UContextMenuButton::PlaySoundSlot(int32 SetIndex, const FUISoundSlot& SoundSlot)
{
	if (!Sounds.IsValidIndex(SetIndex))
	{
		return;
	}

	FUISoundPlayer::ResizeRuntimes(this, RuntimeSounds, Sounds.Num());
	FUISoundPlayer::Play(this, RuntimeSounds[SetIndex], Sounds[SetIndex].Settings, SoundSlot);
}

void UContextMenuButton::HandleStateSounds(EContextMenuState State, bool bSuppressPlayback)
{
	if (Sounds.Num() == 0)
	{
		return;
	}

	FUISoundPlayer::ResizeRuntimes(this, RuntimeSounds, Sounds.Num());

	for (int32 i = 0; i < Sounds.Num(); ++i)
	{
		const FContextMenuSoundSet& Set = Sounds[i];
		FUISoundPlayer::PlayOnEventChange(
			this,
			RuntimeSounds[i],
			Set.Settings,
			Set.GetSoundForState(State),
			static_cast<int32>(State),
			bSuppressPlayback);
	}
}

void UContextMenuButton::PlayClickSounds()
{
	// Not routed through PlayOnEventChange: a click is an event in its own right, not a state the button
	// settles into, so two clicks in a row have to sound twice.
	for (int32 i = 0; i < Sounds.Num(); ++i)
	{
		PlaySoundSlot(i, Sounds[i].Clicked);
	}
}

void UContextMenuButton::PlayButtonSound(USoundBase* Sound, float VolumeMultiplier, float PitchMultiplier)
{
	FUISoundPlayer::PlayOneShot(this, Sound, VolumeMultiplier, PitchMultiplier);
}

void UContextMenuButton::CancelPendingSounds()
{
	FUISoundPlayer::CancelAll(this, RuntimeSounds);
	RuntimeSounds.Reset();
}

// ------------------------------------------------------------------
// Instance overrides
// ------------------------------------------------------------------

namespace ContextMenuButtonOverrides
{
	/**
	 * Rebuilds Patch to hold exactly the parameters Source has, in Source's order. With bKeepEditedValues off
	 * the values come straight from Source; with it on, a parameter that was already in Patch keeps its value
	 * and only genuinely new ones are seeded. Works for both the scalar and the vector value types, which
	 * differ only in what Value is.
	 */
	template <typename ValueType>
	void SyncParameters(TArray<ValueType>& Patch, const TArray<ValueType>& Source, bool bKeepEditedValues)
	{
		TArray<ValueType> Rebuilt;
		Rebuilt.Reserve(Source.Num());

		for (const ValueType& SourceParam : Source)
		{
			ValueType Entry = SourceParam;

			if (bKeepEditedValues)
			{
				if (const ValueType* Existing = Patch.FindByPredicate(
					[&SourceParam](const ValueType& Candidate) { return Candidate.ParameterName == SourceParam.ParameterName; }))
				{
					Entry.Value = Existing->Value;
				}
			}

			Rebuilt.Add(MoveTemp(Entry));
		}

		Patch = MoveTemp(Rebuilt);
	}
}

void UContextMenuButton::SyncOverrideParameters(TArray<FContextMenuMaterialScalarValue>& Patch, const TArray<FContextMenuMaterialScalarValue>& Source, bool bKeepEditedValues)
{
	ContextMenuButtonOverrides::SyncParameters(Patch, Source, bKeepEditedValues);
}

void UContextMenuButton::SyncOverrideParameters(TArray<FContextMenuMaterialVectorValue>& Patch, const TArray<FContextMenuMaterialVectorValue>& Source, bool bKeepEditedValues)
{
	ContextMenuButtonOverrides::SyncParameters(Patch, Source, bKeepEditedValues);
}

void UContextMenuButton::SeedInstanceOverrides(FContextMenuButtonInstanceOverrides& Overrides) const
{
	using namespace ContextMenuButtonOverrideSlots;

	// Enabled only exists as a slot of its own while the button can be enabled and doesn't borrow Hovered's
	// look for it. Past that it is a state like any other: it appears if the button overrides it.
	const bool bEnabledShapeIsOwnState = bButtonCanBeEnabled && !bEnabledMatchesHoverShape;
	const bool bEnabledDesignIsOwnState = bButtonCanBeEnabled && !bEnabledMatchesHoverDesign;

	// Slot order used everywhere below and by SlotDefaults: Base, then the five states.
	const EContextMenuState SlotStates[] = { EContextMenuState::Normal, EContextMenuState::Hovered, EContextMenuState::Pressed, EContextMenuState::Disabled, EContextMenuState::Enabled };
	constexpr int32 NumSlots = 6;

	// --- Shape ---
	// Rebuilt against the button's Shape Targets rather than edited in place: a target added to the button
	// appears here, one removed disappears, and the entries stay in the button's order - so the list itself is
	// never the placement's to change. What the placement did inside an entry is carried over by name.
	TArray<FContextMenuShapeTargetOverride> RebuiltShapes;
	RebuiltShapes.Reserve(ShapeTargets.Num());

	for (const FContextMenuShapeTarget& Authored : ShapeTargets)
	{
		FContextMenuShapeTargetOverride Entry;
		if (const FContextMenuShapeTargetOverride* Existing = Overrides.ShapePatch.FindByPredicate(
			[&Authored](const FContextMenuShapeTargetOverride& Candidate) { return Candidate.TargetWidgetName == Authored.TargetWidgetName; }))
		{
			Entry = *Existing;
		}

		const FContextMenuShapePreset* Preset = FindActiveShapePreset(Authored);
		const FStateMask Mask = MakeShapeMask(Authored, Preset);

		Entry.TargetWidgetName = Authored.TargetWidgetName;

		// Which slots the button has. The placement's own switches (bOverride...) are never touched here.
		bool* const Applicable[NumSlots] = { &Entry.bBaseShapeApplicable, &Entry.bNormalShapeApplicable, &Entry.bHoveredShapeApplicable, &Entry.bPressedShapeApplicable, &Entry.bDisabledShapeApplicable, &Entry.bEnabledShapeApplicable };
		*Applicable[0] = IsBaseReachable(Mask, bEnabledShapeIsOwnState);
		*Applicable[1] = Mask.bNormal;
		*Applicable[2] = Mask.bHovered;
		*Applicable[3] = Mask.bPressed;
		*Applicable[4] = Mask.bDisabled;
		*Applicable[5] = Mask.bEnabled && bEnabledShapeIsOwnState;

		bool* const Switches[NumSlots] = { &Entry.bOverrideBaseShape, &Entry.bOverrideNormalShape, &Entry.bOverrideHoveredShape, &Entry.bOverridePressedShape, &Entry.bOverrideDisabledShape, &Entry.bOverrideEnabledShape };
		FContextMenuShapeOverride* const PatchSlots[NumSlots] = { &Entry.BaseShape, &Entry.NormalShape, &Entry.HoveredShape, &Entry.PressedShape, &Entry.DisabledShape, &Entry.EnabledShape };

		Entry.SlotDefaults.SetNum(NumSlots);

		for (int32 i = 0; i < NumSlots; ++i)
		{
			// Each slot follows the button's matching slot, unpatched: Base from Base, a state from that state.
			// Enabled Matches Hover is ignored on purpose - this is the button's own Enabled slot, which only
			// exists here when that setting is off anyway.
			const FContextMenuShapeStateSettings Source = (i == 0)
				? (Preset ? Preset->BaseShape : Authored.BaseShape)
				: (Preset ? Preset->GetShapeForState(SlotStates[i - 1], /*bEnabledMatchesHoverShape=*/false)
				          : Authored.GetShapeForState(SlotStates[i - 1], /*bEnabledMatchesHoverShape=*/false));

			// The slot untouched: nothing switched on, everything the button's.
			FContextMenuShapeOverride& Default = Entry.SlotDefaults[i];
			Default = FContextMenuShapeOverride();
			Default.Transform = Source.Transform;
			Default.Shape = Source.Shape;

			FContextMenuShapeOverride& PatchSlot = *PatchSlots[i];

			// A slot that's switched off, or that the button no longer has, isn't being overridden - so it
			// holds exactly the button's values rather than whatever was last typed into it.
			if (!*Applicable[i])
			{
				*Switches[i] = false;
			}

			if (!*Switches[i])
			{
				PatchSlot = Default;
				continue;
			}

			// Switched on: a half that is on belongs to whoever switched it on, so only the halves that are off
			// keep following the button. Shape is one half, whole: every Shape Box property in it moves together.
			if (!PatchSlot.bOverrideTransform) { PatchSlot.Transform = Source.Transform; }
			if (!PatchSlot.bOverrideShape)     { PatchSlot.Shape = Source.Shape; }
		}

		RebuiltShapes.Add(MoveTemp(Entry));
	}

	Overrides.ShapePatch = MoveTemp(RebuiltShapes);

	// --- Design ---
	// Same treatment. On top of the slots, each channel inside a slot is only offered if the button drives
	// that channel in that slot.
	TArray<FContextMenuDesignTargetOverride> Rebuilt;
	Rebuilt.Reserve(DesignTargets.Num());

	for (const FContextMenuDesignTarget& Authored : DesignTargets)
	{
		FContextMenuDesignTargetOverride Entry;
		if (const FContextMenuDesignTargetOverride* Existing = Overrides.DesignPatch.FindByPredicate(
			[&Authored](const FContextMenuDesignTargetOverride& Candidate) { return Candidate.TargetWidgetName == Authored.TargetWidgetName; }))
		{
			Entry = *Existing;
		}

		Entry.bHasMigratedStateLayout = true;
		Entry.bHasMigratedSlotToggles = true;

		const FContextMenuDesignPreset* Preset = FindDesignPreset(Authored);
		const FStateMask Mask = MakeDesignMask(Authored, Preset);

		Entry.TargetWidgetName = Authored.TargetWidgetName;

		bool* const Applicable[NumSlots] = { &Entry.bBaseDesignApplicable, &Entry.bNormalDesignApplicable, &Entry.bHoveredDesignApplicable, &Entry.bPressedDesignApplicable, &Entry.bDisabledDesignApplicable, &Entry.bEnabledDesignApplicable };
		*Applicable[0] = !Preset && IsBaseReachable(Mask, bEnabledDesignIsOwnState);
		*Applicable[1] = Mask.bNormal;
		*Applicable[2] = Mask.bHovered;
		*Applicable[3] = Mask.bPressed;
		*Applicable[4] = Mask.bDisabled;
		*Applicable[5] = Mask.bEnabled && bEnabledDesignIsOwnState;

		bool* const Switches[NumSlots] = { &Entry.bOverrideBaseDesign, &Entry.bOverrideNormalDesign, &Entry.bOverrideHoveredDesign, &Entry.bOverridePressedDesign, &Entry.bOverrideDisabledDesign, &Entry.bOverrideEnabledDesign };
		FContextMenuDesignStateOverride* const PatchSlots[NumSlots] = { &Entry.BaseDesign, &Entry.Normal, &Entry.Hovered, &Entry.Pressed, &Entry.Disabled, &Entry.Enabled };

		Entry.SlotDefaults.SetNum(NumSlots);

		for (int32 i = 0; i < NumSlots; ++i)
		{
			// The button's matching slot, unpatched. Null when the button has no such slot.
			const FContextMenuWidgetDesign* Source = nullptr;
			if (*Applicable[i])
			{
				if (i == 0)
				{
					Source = &Authored.BaseDesign;
				}
				else
				{
					const EContextMenuState State = SlotStates[i - 1];
					Source = Preset ? &GetPresetDesign(*Preset, State) : &Authored.GetDesignForState(State, /*bEnabledMatchesHover=*/false);
				}
			}

			FContextMenuDesignStateOverride& Default = Entry.SlotDefaults[i];
			Default = FContextMenuDesignStateOverride();
			Default.SyncApplicability(Source);
			if (Source)
			{
				Default.Tint = Source->Tint;
				Default.OutlineTint = Source->OutlineTint;
				Default.Opacity = Source->Opacity;
				Default.BrushImageSource = Source->BrushImageSource;
				Default.BrushTexture = Source->BrushTexture;
				Default.BrushSVG = Source->BrushSVG;
				Default.Material = Source->Material;
				Default.MaterialScalars = Source->MaterialScalars;
				Default.MaterialVectors = Source->MaterialVectors;
			}

			FContextMenuDesignStateOverride& PatchSlot = *PatchSlots[i];

			if (!*Applicable[i])
			{
				*Switches[i] = false;
			}

			if (!*Switches[i])
			{
				PatchSlot = Default;
				continue;
			}

			// Switched on: which channels are offered always tracks the button; values only follow the button
			// for channels that are off.
			PatchSlot.SyncApplicability(Source);

			if (!PatchSlot.bOverrideTint)         { PatchSlot.Tint = Source->Tint; }
			if (!PatchSlot.bOverrideOutlineTint)  { PatchSlot.OutlineTint = Source->OutlineTint; }
			if (!PatchSlot.bOverrideOpacity)      { PatchSlot.Opacity = Source->Opacity; }
			if (!PatchSlot.bOverrideBrushTexture)
			{
				PatchSlot.BrushImageSource = Source->BrushImageSource;
				PatchSlot.BrushTexture = Source->BrushTexture;
				PatchSlot.BrushSVG = Source->BrushSVG;
			}
			if (!PatchSlot.bOverrideMaterial)     { PatchSlot.Material = Source->Material; }

			// The parameter LIST always tracks the button - a material swapped or a parameter renamed has to
			// show up here or the patch would be editing names that no longer exist. The VALUES are kept by
			// name while the channel is on, so adding a parameter doesn't reset the ones already tuned.
			SyncOverrideParameters(PatchSlot.MaterialScalars, Source->MaterialScalars, PatchSlot.bOverrideMaterial);
			SyncOverrideParameters(PatchSlot.MaterialVectors, Source->MaterialVectors, PatchSlot.bOverrideMaterial);
		}

		Rebuilt.Add(MoveTemp(Entry));
	}

	Overrides.DesignPatch = MoveTemp(Rebuilt);
}


FContextMenuButtonInstanceOverrides* UContextMenuButton::FindOwnerInstanceOverrides(FString* OutVariableName, FString* OutDiagnostic, UUserWidget** OutOwnerWidget) const
{
	if (OutOwnerWidget)
	{
		*OutOwnerWidget = nullptr;
	}

	const UStruct* OverrideStruct = FContextMenuButtonInstanceOverrides::StaticStruct();
	const FString ButtonName = GetName();

	// Outwards from the widget that authors this button. Nearest wins, so a button wrapped in its own little
	// User Widget still reaches the variable on that wrapper rather than one further up the screen.
	UObject* Search = GetOuter();
	int32 Depth = 0;

	while (Search && Depth < MaxOwnerSearchDepth)
	{
		++Depth;

		if (UUserWidget* Owner = Cast<UUserWidget>(Search))
		{
			FContextMenuButtonInstanceOverrides* SoleMatch = nullptr;
			FString SoleMatchName;
			int32 MatchCount = 0;

			for (TFieldIterator<FStructProperty> It(Owner->GetClass()); It; ++It)
			{
				FStructProperty* StructProp = *It;
				if (!StructProp || StructProp->Struct != OverrideStruct)
				{
					continue;
				}

				FContextMenuButtonInstanceOverrides* Value = StructProp->ContainerPtrToValuePtr<FContextMenuButtonInstanceOverrides>(Owner);

				// An explicit Target Button Name beats everything. Set by Add Overrides Variable, it makes
				// this an exact match rather than an inference, so the variable can be called anything and a
				// widget can hold one per button without a naming convention anyone has to remember.
				if (!Value->TargetButtonName.IsNone())
				{
					if (ContextMenuButtonOverrideNaming::NamesMatch(Value->TargetButtonName.ToString(), ButtonName))
					{
						if (OutVariableName) { *OutVariableName = StructProp->GetName(); }
						if (OutOwnerWidget) { *OutOwnerWidget = Owner; }
						return Value;
					}

					// Claimed by a different button. Not a candidate for this one at all, so it must not fall
					// through to the name guessing below and be picked up as the "single unambiguous" match.
					continue;
				}

				// A variable named after the button wins next, so a widget holding several buttons can give
				// each one its own overrides just by naming the variables after them.
				if (ContextMenuButtonOverrideNaming::NamesMatch(StructProp->GetName(), ButtonName))
				{
					if (OutVariableName) { *OutVariableName = StructProp->GetName(); }
					if (OutOwnerWidget) { *OutOwnerWidget = Owner; }
					return Value;
				}

				SoleMatch = Value;
				SoleMatchName = StructProp->GetName();
				++MatchCount;
			}

			// Exactly one unnamed candidate is unambiguous. Several are not, and guessing would silently
			// restyle the wrong button, so nothing is used until one of them is named after a button.
			if (MatchCount == 1)
			{
				if (OutVariableName) { *OutVariableName = SoleMatchName; }
				if (OutOwnerWidget) { *OutOwnerWidget = Owner; }
				return SoleMatch;
			}

			if (MatchCount > 1)
			{
				// Said out loud rather than failing quietly: from the outside this looks identical to the
				// variable not existing, and the two are fixed in completely different ways.
				if (OutDiagnostic)
				{
					*OutDiagnostic = FString::Printf(
						TEXT("'%s' declares %d Context Menu Button Instance Overrides variables and none is named after this button ('%s'), so none was used. Rename one to include the button's name."),
						*Owner->GetClass()->GetName(),
						MatchCount,
						*ButtonName);
				}

				return nullptr;
			}
		}

		Search = Search->GetOuter();
	}

	return nullptr;
}

void UContextMenuButton::ResolveInstanceOverrides(bool bSeedSource)
{
	const FContextMenuButtonInstanceOverrides* Source = nullptr;

	FString SourceDescription;
	FString Diagnostic;

	// The button's own dropdown unless a placement below says otherwise.
	ActiveDesignPreview = ShowPreview;

	if (bInstanceOverridesAppliedManually)
	{
		// Blueprint has pushed its own patch in; a rebuild must not quietly replace it with the designer's.
		Source = &ManualInstanceOverrides;
		SourceDescription = TEXT("Apply Instance Overrides (Blueprint)");
	}
	else
	{
		// A variable on a containing widget is the per-placement path and takes priority; the button's own
		// struct is the fallback for the common case of one shared look. No longer gated on a checkbox:
		// declaring the variable is the switch, and a second box that also had to be ticked somewhere else
		// was only ever a way for a correct setup to silently do nothing.
		FString OwnerVariableName;
		UUserWidget* VariableOwner = nullptr;
		if (FContextMenuButtonInstanceOverrides* OwnerOverrides = FindOwnerInstanceOverrides(&OwnerVariableName, &Diagnostic, &VariableOwner))
		{
			// Skipped when this is a poll rather than a rebuild: seeding WRITES into the struct it found,
			// and a timer doing that a few times a second would be quietly mutating somebody's widget.
			if (bSeedSource)
			{
				SeedInstanceOverrides(*OwnerOverrides);
			}

			Source = OwnerOverrides;
			SourceDescription = FString::Printf(TEXT("Variable '%s' on the containing widget"), *OwnerVariableName);

			// A placement's own preview dropdown wins, but only for an actual placement: the widget holding
			// the variable has itself been dropped into another widget's tree. In the widget that authors
			// the button that same variable is just its default, and the button's own Show Preview - the one
			// being edited right there - has to stay in charge.
			if (IsDesignTime())
			{
				if (VariableOwner && Cast<UWidgetTree>(VariableOwner->GetOuter()))
				{
					ActiveDesignPreview = OwnerOverrides->ShowPreview;
				}
			}
		}
		else
		{
			if (bSeedSource)
			{
				SeedInstanceOverrides(InstanceOverrides);
			}

			Source = &InstanceOverrides;
			SourceDescription = TEXT("This button's own Instance Overrides (no variable of type Context Menu Button Instance Overrides found on any widget above it, so this is shared by every placement)");
		}
	}

	if (!Source)
	{
		ActiveShapePatch.Reset();
		ActiveDesignPatch.Reset();
		InstanceOverrideStatus = TEXT("Is Instance Editable is off; nothing is patched.");
		return;
	}

	ActiveShapePatch = Source->bOverrideShape ? Source->ShapePatch : TArray<FContextMenuShapeTargetOverride>();
	ActiveDesignPatch = Source->bOverrideDesign ? Source->DesignPatch : TArray<FContextMenuDesignTargetOverride>();

	int32 PatchedShapeTargets = 0;
	for (const FContextMenuShapeTargetOverride& Entry : ActiveShapePatch)
	{
		PatchedShapeTargets += Entry.HasAnything() ? 1 : 0;
	}

	int32 PatchedTargets = 0;
	for (const FContextMenuDesignTargetOverride& Entry : ActiveDesignPatch)
	{
		PatchedTargets += Entry.HasAnything() ? 1 : 0;
	}

	const UEnum* PreviewEnum = StaticEnum<EContextMenuDesignPreview>();
	InstanceOverrideStatus = FString::Printf(
		TEXT("%s\nShape patch: %d of %d target(s) changed\nDesign patch: %d of %d target(s) changed\nDesigner preview: %s"),
		*SourceDescription,
		PatchedShapeTargets,
		ActiveShapePatch.Num(),
		PatchedTargets,
		ActiveDesignPatch.Num(),
		PreviewEnum ? *PreviewEnum->GetNameStringByValue(static_cast<int64>(ActiveDesignPreview)) : TEXT("?"));

	if (!Diagnostic.IsEmpty())
	{
		InstanceOverrideStatus += TEXT("\n\n") + Diagnostic;
	}
}

FContextMenuButtonInstanceOverrides UContextMenuButton::GetAuthoredInstanceOverrides() const
{
	FContextMenuButtonInstanceOverrides Authored;
	SeedInstanceOverrides(Authored);
	return Authored;
}

void UContextMenuButton::ApplyInstanceOverrides(FContextMenuButtonInstanceOverrides& Overrides)
{
	SeedInstanceOverrides(Overrides);

	ManualInstanceOverrides = Overrides;
	bInstanceOverridesAppliedManually = true;
	ResolveInstanceOverrides();

	// RefreshButtonState no-ops at design time by design (it drives the runtime state machine, which doesn't
	// run in the designer). That's fine here: the designer re-synchronizes every widget on its own, and that
	// pass picks the patch up through SynchronizeProperties -> ApplyDesignPreviews. This call is what makes
	// the change visible immediately at runtime, where nothing else re-synchronizes for you.
	RefreshButtonState(/*bInstant=*/true);
}

void UContextMenuButton::ClearInstanceOverrides()
{
	bInstanceOverridesAppliedManually = false;
	ManualInstanceOverrides = FContextMenuButtonInstanceOverrides();
	ResolveInstanceOverrides();
	RefreshButtonState(/*bInstant=*/true);
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

const FContextMenuShapePreset* UContextMenuButton::FindActiveShapePreset(const FContextMenuShapeTarget& Target) const
{
	if (Target.ShapeSource != EContextMenuButtonSettingSource::UseProjectSettings)
	{
		return nullptr;
	}

	const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
	return Settings ? Settings->FindContextMenuShapePreset(Target.PresetName) : nullptr;
}

FContextMenuShapeStateSettings UContextMenuButton::ResolveShapeForState(const FContextMenuShapeTarget& Authored, EContextMenuState State) const
{
	using namespace ContextMenuButtonOverrideSlots;

	const FContextMenuShapePreset* Preset = FindActiveShapePreset(Authored);

	FContextMenuShapeStateSettings Result = Preset
		? Preset->GetShapeForState(State, bEnabledMatchesHoverShape)
		: Authored.GetShapeForState(State, bEnabledMatchesHoverShape);

	// A Shape Preset carries Transforms and Shape Box shapes, never a vector shape's Look: that stays the target's own.
	if (Preset)
	{
		const FContextMenuShapeStateSettings Own = Authored.GetShapeForState(State, bEnabledMatchesHoverShape);
		Result.Look = Own.Look;
		Result.bHasLook = Own.bHasLook;
		Result.bIsVectorShape = Own.bIsVectorShape;
	}

	// Matched by widget name, so a Shape Target the patch doesn't mention is left exactly as authored and a
	// patch entry for a target that has since been deleted or renamed is quietly dropped. The slot is the one
	// lining up with the slot the button itself just read from: a state the button leaves on Base is patched
	// by the patch's Base, a state it overrides by the patch's slot for that state.
	if (const FContextMenuShapeTargetOverride* Patch = ActiveShapePatch.FindByPredicate(
		[&Authored](const FContextMenuShapeTargetOverride& Candidate) { return Candidate.TargetWidgetName == Authored.TargetWidgetName; }))
	{
		if (const FContextMenuShapeOverride* PatchSlot = SelectShapeSlot(*Patch, MakeShapeMask(Authored, Preset), State, bEnabledMatchesHoverShape))
		{
			PatchSlot->ApplyTo(Result);
		}
	}

	return Result;
}

FContextMenuWidgetDesign UContextMenuButton::ResolveDesignForState(const FContextMenuDesignTarget& Authored, EContextMenuState State) const
{
	using namespace ContextMenuButtonOverrideSlots;

	const FContextMenuDesignPreset* Preset = FindDesignPreset(Authored);

	FContextMenuWidgetDesign Resolved = Preset
		? GetPresetDesign(*Preset, CanonicalState(State, bEnabledMatchesHoverDesign))
		: Authored.GetDesignForState(State, bEnabledMatchesHoverDesign);

	// Matched by widget name, so a Design Target the patch doesn't mention is left exactly as authored and a
	// patch entry for a target that has since been deleted or renamed is quietly dropped. Slot chosen the same
	// way as for Shape. A patched channel the button doesn't drive in this slot changes a value nobody reads:
	// the button's own Drive toggle is carried through untouched, so it simply isn't applied.
	if (const FContextMenuDesignTargetOverride* Patch = ActiveDesignPatch.FindByPredicate(
		[&Authored](const FContextMenuDesignTargetOverride& Candidate) { return Candidate.TargetWidgetName == Authored.TargetWidgetName; }))
	{
		if (const FContextMenuDesignStateOverride* PatchSlot = SelectDesignSlot(*Patch, MakeDesignMask(Authored, Preset), State, bEnabledMatchesHoverDesign))
		{
			PatchSlot->ApplyTo(Resolved);
		}
	}

	return Resolved;
}

bool UContextMenuButton::ApplyTransitions(float DeltaTime, bool bInstant)
{
	// --- Shape targets (this button's own shape, plus any other widget shaped alongside it) ---
	bool bShapesDone = true;
	for (FContextMenuRuntimeShapeTarget& Target : RuntimeShapeTargets)
	{
		bShapesDone &= ApplyShapeTarget(Target, CurrentShapeState, DeltaTime, bInstant);
	}

	// --- Design targets (tint, opacity, brush, material, sounds, parameters) ---
	bool bDesignsDone = true;
	for (FContextMenuRuntimeDesignTarget& Target : RuntimeDesignTargets)
	{
		bDesignsDone &= ApplyDesignTarget(Target, CurrentDesignState, DeltaTime, bInstant);
	}

	return bShapesDone && bDesignsDone;
}

bool UContextMenuButton::ApplyShapeTarget(FContextMenuRuntimeShapeTarget& Target, EContextMenuState State, float DeltaTime, bool bInstant)
{
	using namespace ContextMenuButtonShape;

	UWidget* Widget = Target.Widget.Get();
	if (!Widget || !ShapeTargets.IsValidIndex(Target.AuthoredIndex))
	{
		return true;
	}

	// Read live, so editing a shape (or a preset) during PIE is picked up on the next transition. Resolved once per
	// state change (or snap), not on every frame of the blend: it copies the state's whole settings, Look included.
	const FContextMenuShapeTarget& Authored = ShapeTargets[Target.AuthoredIndex];
	const FContextMenuShapePreset* Preset = FindActiveShapePreset(Authored);
	const bool bStateChanged = !Target.bHasInitializedState || Target.LastShapeState != State;
	if (bStateChanged || bInstant || !Target.bHasResolvedGoal)
	{
		Target.ResolvedGoal = ResolveShapeForState(Authored, State);
		Target.bHasResolvedGoal = true;
	}
	const FContextMenuShapeStateSettings& TargetShape = Target.ResolvedGoal;
	const FContextMenuResolvedInterpolation Interp = Preset ? Preset->GetResolvedInterpolation() : Authored.GetResolvedInterpolation();

	// A state change resets the blend from wherever this target currently sits, and re-arms the delay. Done
	// here, per target, rather than once for the whole button, since every target can carry its own speed,
	// curve and delay now - the same reason a Design Target's channels blend at their own InterpolationSpeed.
	if (bStateChanged)
	{
		Target.StartTransform = Target.CurrentTransform;
		Target.StartShape = Target.CurrentShape;
		Target.TransformTransitionAlpha = 0.0f;
		Target.SizeTransitionAlpha = 0.0f;

		// Re-armed from scratch, so a state change during a wait replaces it rather than queueing behind it: a
		// cursor skimming across the button doesn't build up a backlog of pending moves.
		Target.TransformDelayRemaining = FMath::Max(0.0f, Interp.TransformDelay);
		Target.SizeDelayRemaining = FMath::Max(0.0f, Interp.SizeDelay);

		Target.LastShapeState = State;
		Target.bHasInitializedState = true;

		// A vector shape's Look blends from wherever it stands now (only when a state changes anything on it).
		UShapeWidget* StartLookWidget = Target.LookProperties.Num() > 0 ? Target.LookWidget.Get() : nullptr;
		if (StartLookWidget)
		{
			WidgetShapeAnimation::LayOut(Target.StartLook, StartLookWidget->GetClass(), StartLookWidget);
			WidgetShapeAnimation::Capture(Target.StartLook, StartLookWidget);
		}
	}

	if (!bInstant)
	{
		// A delay holds the channel exactly where it is rather than slowing it down, so the shape sits still
		// and then moves at full speed. Each channel counts down on its own, which is what lets a separated
		// Shape lag behind the transform (or the other way round). The "Size" channel is the whole Shape.
		if (Target.TransformDelayRemaining > 0.0f)
		{
			Target.TransformDelayRemaining = FMath::Max(0.0f, Target.TransformDelayRemaining - DeltaTime);
		}
		else if (Interp.TransformSpeed > 0.0f)
		{
			Target.TransformTransitionAlpha = FMath::Clamp(Target.TransformTransitionAlpha + (DeltaTime * Interp.TransformSpeed), 0.0f, 1.0f);
		}
		else
		{
			Target.TransformTransitionAlpha = 1.0f;
		}

		if (Target.SizeDelayRemaining > 0.0f)
		{
			Target.SizeDelayRemaining = FMath::Max(0.0f, Target.SizeDelayRemaining - DeltaTime);
		}
		else if (Interp.SizeSpeed > 0.0f)
		{
			Target.SizeTransitionAlpha = FMath::Clamp(Target.SizeTransitionAlpha + (DeltaTime * Interp.SizeSpeed), 0.0f, 1.0f);
		}
		else
		{
			Target.SizeTransitionAlpha = 1.0f;
		}
	}
	else
	{
		// Snapping ignores the delays outright: an instant update is a re-seed, not an animation.
		Target.TransformDelayRemaining = 0.0f;
		Target.SizeDelayRemaining = 0.0f;
		Target.TransformTransitionAlpha = 1.0f;
		Target.SizeTransitionAlpha = 1.0f;
	}

	const float TCurveAlpha = EvaluateCurve(Interp.TransformCurve, Target.TransformTransitionAlpha, Interp.TransformOvershoot);
	const float SCurveAlpha = EvaluateCurve(Interp.SizeCurve, Target.SizeTransitionAlpha, Interp.SizeOvershoot);

	const bool bTransformDone = InterpWidgetTransform(Target.CurrentTransform, Target.StartTransform, TargetShape.Transform, TCurveAlpha, Target.TransformTransitionAlpha, bInstant);
	const bool bShapeDone = InterpShape(Target.CurrentShape, Target.StartShape, TargetShape.Shape, SCurveAlpha, Target.SizeTransitionAlpha, bInstant);

	ApplyShapeToTargetWidget(Widget, Target.CurrentShape, Target.CurrentTransform);

	// A vector shape's Look, at the Shape channel's pace: only the settings some state changes.
	bool bLookDone = true;
	UShapeWidget* LookWidget = Target.LookWidget.Get();
	if (LookWidget && Target.LookProperties.Num() > 0)
	{
		const float LookAlpha = (bInstant || Target.SizeTransitionAlpha >= 1.0f) ? 1.0f : SCurveAlpha;
		bool bLookChanged = false;
		for (const FProperty* Property : Target.LookProperties)
		{
			// A state with no Look of its own shows the shape as authored.
			const void* Goal = WidgetShapeAnimation::FindValue(TargetShape.Look, Property);
			if (!Goal)
			{
				Goal = WidgetShapeAnimation::FindValue(Target.BaselineLook, Property);
			}
			bLookChanged |= WidgetShapeAnimation::WriteBlend(LookWidget, Property,
				WidgetShapeAnimation::FindValue(Target.StartLook, Property), Goal, LookAlpha);
		}
		if (bLookChanged)
		{
			InvalidateLookWidget(LookWidget);
		}
		bLookDone = LookAlpha >= 1.0f;
	}

	const bool bDelaysDone = (Target.TransformDelayRemaining <= 0.0f) && (Target.SizeDelayRemaining <= 0.0f);

	return bDelaysDone && bTransformDone && bShapeDone && bLookDone;
}

void UContextMenuButton::ApplyShapeToTargetWidget(UWidget* Widget, const FShapeBoxShape& Shape, const FWidgetTransform& Transform)
{
	if (!Widget)
	{
		return;
	}

	if (Widget == this)
	{
		// Self-targeting: the button's own brush layer, which bends the brush and carries the hit area with it. A vector
		// Background Shape takes a Look instead of a Shape Box shape (the Details show no Shape for it), so none is applied.
		PushButtonShapeToSlate(BackgroundShapeWidget ? FShapeBoxShape() : Shape, Transform);
		return;
	}

	// A vector shape takes its Look (ApplyShapeTarget) and its Transform, on itself - never the Shape Box it may sit in.
	if (Widget->IsA<UShapeWidget>())
	{
		Widget->SetRenderTransform(Transform);
		return;
	}

	// A Shape Box - the target itself, or the one it sits alone in - takes the whole shape. Written as one struct,
	// so whatever the Shape Box grows later arrives with it.
	if (UShapeBox* ShapeBox = FindDrivenShapeBox(Widget))
	{
		ShapeBox->SetShape(Shape);
		ShapeBox->SetRenderTransform(Transform);
		return;
	}

	// Anything else has no way to take a shape without a Shape Box around it, so only the Transform applies (Shape
	// Status says so in the Details panel).
	Widget->SetRenderTransform(Transform);
}

UShapeBox* UContextMenuButton::FindDrivenShapeBox(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (UShapeBox* ShapeBox = Cast<UShapeBox>(Widget))
	{
		return ShapeBox;
	}

	// A Shape Box is a content widget, so a widget whose parent is one is necessarily its only child.
	return Cast<UShapeBox>(Widget->GetParent());
}

// ------------------------------------------------------------------
// Vector shapes: Background Shape and Looks
// ------------------------------------------------------------------

UShapeWidget* UContextMenuButton::GetLookWidget(UWidget* Widget) const
{
	if (!Widget)
	{
		return nullptr;
	}
	if (Widget == this)
	{
		return BackgroundShapeWidget;
	}
	return Cast<UShapeWidget>(Widget);
}

UClass* UContextMenuButton::GetLookClass(const FContextMenuShapeTarget& Target)
{
	UWidget* Widget = FindWidgetInButtonTree(Target.TargetWidgetName);
	if (!Widget)
	{
		return nullptr;
	}
	if (Widget == this)
	{
		UClass* Class = BackgroundShape.Get();
		return (Class && !Class->HasAnyClassFlags(CLASS_Abstract)) ? Class : nullptr;
	}
	return Widget->IsA<UShapeWidget>() ? Widget->GetClass() : nullptr;
}

void UContextMenuButton::SyncBackgroundShape()
{
	UClass* Class = BackgroundShape.Get();
	if (Class && Class->HasAnyClassFlags(CLASS_Abstract))
	{
		Class = nullptr;
	}
	bHasBackgroundShape = Class != nullptr;

	const TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin();
	if (!Class)
	{
		if (BackgroundShapeWidget)
		{
			BackgroundShapeWidget->SetExternalPainter(nullptr);
			BackgroundShapeWidget = nullptr;
		}
		if (SlateButton.IsValid())
		{
			SlateButton->SetBackgroundShape(nullptr);
		}
		return;
	}

	// Kept as it is when it already holds this kind of shape's settings.
	WidgetShapeAnimation::LayOut(BackgroundLook, Class, nullptr);

	// Only made once there is a Slate button to paint it: a Widget Blueprint's template never builds one, so it never
	// carries this widget around. In the transient package, so nothing ever saves or duplicates it with the button.
	if (!SlateButton.IsValid())
	{
		return;
	}
	if (!BackgroundShapeWidget || BackgroundShapeWidget->GetClass() != Class)
	{
		BackgroundShapeWidget = NewObject<UShapeWidget>(GetTransientPackage(), Class, NAME_None, RF_Transient);
	}

	TArray<const FProperty*> All;
	WidgetShapeAnimation::GetProperties(Class, All);
	WriteLook(BackgroundShapeWidget, BackgroundLook, All);

	BackgroundShapeWidget->SetExternalPainter(SlateButton);
	SlateButton->SetBackgroundShape(BackgroundShapeWidget);
}

const FInstancedPropertyBag* UContextMenuButton::FindLookBaseline(UShapeWidget* Shape)
{
	if (!Shape)
	{
		return nullptr;
	}
	if (Shape == BackgroundShapeWidget)
	{
		return &BackgroundLook;
	}

	LookBaselines.RemoveAll([](const FContextMenuLookBaseline& Entry) { return !Entry.Widget.IsValid(); });
	if (const FContextMenuLookBaseline* Found = LookBaselines.FindByPredicate([Shape](const FContextMenuLookBaseline& Entry) { return Entry.Widget.Get() == Shape; }))
	{
		return &Found->Look;
	}

	// The first time a Look reaches this shape, before anything is written onto it: the shape as authored.
	FContextMenuLookBaseline& Added = LookBaselines.AddDefaulted_GetRef();
	Added.Widget = Shape;
	WidgetShapeAnimation::LayOut(Added.Look, Shape->GetClass(), Shape);
	return &Added.Look;
}

void UContextMenuButton::GatherLookProperties(const FContextMenuShapeTarget& Authored, UShapeWidget* Shape, TArray<const FProperty*>& OutProperties)
{
	OutProperties.Reset();
	const FInstancedPropertyBag* Baseline = FindLookBaseline(Shape);
	if (!Shape || !Baseline)
	{
		return;
	}

	const EContextMenuState States[] = { EContextMenuState::Normal, EContextMenuState::Hovered, EContextMenuState::Pressed, EContextMenuState::Disabled, EContextMenuState::Enabled };
	TArray<FContextMenuShapeStateSettings, TInlineAllocator<5>> Resolved;
	for (const EContextMenuState State : States)
	{
		Resolved.Add(ResolveShapeForState(Authored, State));
	}

	// A setting is the states' to write once any state's Look differs from the shape as authored - so one a state changes
	// blends, and one none of them touch keeps whatever the shape itself is given.
	TArray<const FProperty*> All;
	WidgetShapeAnimation::GetProperties(Shape->GetClass(), All);
	for (const FProperty* Property : All)
	{
		const void* AsAuthored = WidgetShapeAnimation::FindValue(*Baseline, Property);
		if (!AsAuthored)
		{
			continue;
		}
		for (const FContextMenuShapeStateSettings& Settings : Resolved)
		{
			const void* Value = WidgetShapeAnimation::FindValue(Settings.Look, Property);
			if (Value && !Property->Identical(Value, AsAuthored))
			{
				OutProperties.Add(Property);
				break;
			}
		}
	}
}

void UContextMenuButton::WriteLook(UShapeWidget* Shape, const FInstancedPropertyBag& Look, const TArray<const FProperty*>& Properties, const FInstancedPropertyBag* Fallback)
{
	if (!Shape)
	{
		return;
	}
	bool bChanged = false;
	for (const FProperty* Property : Properties)
	{
		const void* Value = WidgetShapeAnimation::FindValue(Look, Property);
		if (!Value && Fallback)
		{
			Value = WidgetShapeAnimation::FindValue(*Fallback, Property);
		}
		bChanged |= WidgetShapeAnimation::WriteBlend(Shape, Property, nullptr, Value, 1.0f);
	}
	if (bChanged)
	{
		InvalidateLookWidget(Shape);
	}
}

void UContextMenuButton::InvalidateLookWidget(UShapeWidget* Shape)
{
	if (!Shape)
	{
		return;
	}
	// Repaints its own widget, and whatever paints it besides (this button, for the Background Shape).
	Shape->InvalidateShape();
}

void UContextMenuButton::RefreshShapeTargetLooks()
{
	SyncBackgroundShape();

	for (FContextMenuShapeTarget& Target : ShapeTargets)
	{
		// Found once per target: what it aims at decides whether it has Looks at all.
		UWidget* Widget = FindWidgetInButtonTree(Target.TargetWidgetName);
		UClass* Class = nullptr;
		if (Widget == this)
		{
			Class = (BackgroundShape && !BackgroundShape->HasAnyClassFlags(CLASS_Abstract)) ? BackgroundShape.Get() : nullptr;
		}
		else if (Widget && Widget->IsA<UShapeWidget>())
		{
			Class = Widget->GetClass();
		}
		const UShapeWidget* Source = (Widget && Widget != this) ? Cast<UShapeWidget>(Widget) : nullptr;

		// Only the states the target overrides carry a Look; the Base Shape never does (the base is the shape as authored).
		// A Look nobody uses is emptied, so it costs nothing - not in the Details panel, not in every copy of the button.
		struct FStateLook
		{
			FContextMenuShapeStateSettings* Settings;
			bool bOverridden;
		};
		const FStateLook States[] = {
			{ &Target.BaseShape, false },
			{ &Target.NormalShape, Target.bOverrideNormalShape },
			{ &Target.HoveredShape, Target.bOverrideHoveredShape },
			{ &Target.PressedShape, Target.bOverridePressedShape },
			{ &Target.DisabledShape, Target.bOverrideDisabledShape },
			{ &Target.EnabledShape, Target.bOverrideEnabledShape } };
		for (const FStateLook& Entry : States)
		{
			FContextMenuShapeStateSettings* State = Entry.Settings;
			const bool bWantsLook = Class != nullptr && Entry.bOverridden;
			State->bIsVectorShape = Class != nullptr;
			State->bHasLook = bWantsLook;

			if (!bWantsLook)
			{
				if (State->Look.GetPropertyBagStruct())
				{
					State->Look.Reset();
				}
				continue;
			}

			// A Look for another kind of shape (or none yet) is laid out again, keeping what the two kinds share; one already
			// right is left exactly as authored. Started from the shape as it is - the Background Look, for Self.
			if (WidgetShapeAnimation::IsLaidOutFor(State->Look, Class))
			{
				continue;
			}
			if (!Source && WidgetShapeAnimation::IsLaidOutFor(BackgroundLook, Class))
			{
				State->Look = BackgroundLook;
			}
			else
			{
				WidgetShapeAnimation::LayOut(State->Look, Class, Source);
			}
		}
	}
}

void UContextMenuButton::RebuildShapeTargetStatus()
{
	if (ShapeTargets.Num() == 0)
	{
		ShapeTargetStatus = TEXT("No Shape Targets.");
		return;
	}

	const EContextMenuState States[] = { EContextMenuState::Normal, EContextMenuState::Hovered, EContextMenuState::Pressed, EContextMenuState::Disabled, EContextMenuState::Enabled };

	TStringBuilder<1024> Builder;
	for (int32 i = 0; i < ShapeTargets.Num(); ++i)
	{
		const FContextMenuShapeTarget& Authored = ShapeTargets[i];

		if (i > 0)
		{
			Builder.Append(TEXT("\n"));
		}
		Builder.Appendf(TEXT("%d. %s - "), i, Authored.TargetWidgetName.IsNone() ? TEXT("(none)") : *Authored.TargetWidgetName.ToString());

		if (Authored.TargetWidgetName.IsNone())
		{
			Builder.Append(TEXT("no Target Widget picked."));
			continue;
		}

		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			Builder.Append(TEXT("not found in this button's tree."));
			continue;
		}

		if (Widget == this)
		{
			if (BackgroundShape)
			{
				Builder.Appendf(TEXT("shapes the button's Background Shape, a %s (Look and Transform)."), *BackgroundShape->GetDisplayNameText().ToString());
			}
			else
			{
				Builder.Append(TEXT("shapes the button's brush (Shape and Transform)."));
			}
			continue;
		}

		if (Widget->IsA<UShapeWidget>())
		{
			Builder.Appendf(TEXT("drives this %s (Look and Transform)."), *Widget->GetClass()->GetDisplayNameText().ToString());
			continue;
		}

		if (const UShapeBox* ShapeBox = FindDrivenShapeBox(Widget))
		{
			if (ShapeBox == Widget)
			{
				Builder.Append(TEXT("drives this Shape Box (Shape and Transform)."));
			}
			else
			{
				Builder.Appendf(TEXT("drives the Shape Box around it, %s (Shape and Transform)."), *ShapeBox->GetName());
			}
			continue;
		}

		// Only worth shouting about when a state actually authors a shape that is going to be dropped.
		bool bAuthorsShape = false;
		for (const EContextMenuState State : States)
		{
			if (!ResolveShapeForState(Authored, State).Shape.IsFlat())
			{
				bAuthorsShape = true;
				break;
			}
		}

		Builder.Append(bAuthorsShape
			? TEXT("Transform only: its Shape is IGNORED. Wrap it in a Shape Box (or target a Shape Box) to use Shape.")
			: TEXT("Transform only. Wrap it in a Shape Box to give it a Shape."));
	}

	ShapeTargetStatus = Builder.ToString();
}

void UContextMenuButton::PushButtonShapeToSlate(const FShapeBoxShape& Shape, const FWidgetTransform& Transform)
{
	if (TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
	{
		SlateButton->SetButtonShape(Shape, Transform);
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

bool UContextMenuButton::DoesTargetDriveBrush(const FContextMenuDesignTarget& Target)
{
	// Any state driving any of the four brush-ish channels is enough: the question this answers is whether
	// the design owns the button's look at all, not whether it happens to be doing so this frame. Delegating
	// to these keeps this in step with Base Design: a channel Base Design drives applies to every state that
	// hasn't overridden it, so it counts here too.
	return Target.DrivesTintAnywhere() || Target.DrivesOutlineTintAnywhere() || Target.DrivesBrushTextureAnywhere() || Target.DrivesMaterialAnywhere();
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

		if (DoesTargetDriveBrush(DesignTargets[Target.AuthoredIndex]))
		{
			return true;
		}
	}

	// Design time (and before the targets are resolved): match on the stored name instead. A target on the button
	// itself is stored as the button's own name, since it's the root of the gathered tree.
	if (RuntimeDesignTargets.Num() == 0)
	{
		for (const FContextMenuDesignTarget& Authored : DesignTargets)
		{
			if (DoesTargetDriveBrush(Authored) && IsSelfTargetName(Authored.TargetWidgetName))
			{
				return true;
			}
		}
	}

	return false;
}

// ------------------------------------------------------------------
// Shape
// ------------------------------------------------------------------

void UContextMenuButton::InitializeRuntimeShapeTargets()
{
	RuntimeShapeTargets.Reset();

	for (int32 i = 0; i < ShapeTargets.Num(); ++i)
	{
		const FContextMenuShapeTarget& Authored = ShapeTargets[i];

		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			continue;
		}

		FContextMenuRuntimeShapeTarget& Entry = RuntimeShapeTargets.AddDefaulted_GetRef();
		Entry.Widget = Widget;
		Entry.AuthoredIndex = i;

		// A vector shape: which of its settings the states change, worked out once against the shape as authored.
		if (UShapeWidget* LookWidget = GetLookWidget(Widget))
		{
			Entry.LookWidget = LookWidget;
			GatherLookProperties(Authored, LookWidget, Entry.LookProperties);
			if (Entry.LookProperties.Num() > 0)
			{
				if (const FInstancedPropertyBag* Baseline = FindLookBaseline(LookWidget))
				{
					Entry.BaselineLook = *Baseline;
				}
			}
		}

		// Everything else starts at identity/zero, so the very first resolve blends out of "unshaped" toward
		// whatever the current state's shape is - exactly what a freshly built button always did.
	}
}

// ------------------------------------------------------------------
// Colors / Design
// ------------------------------------------------------------------

void UContextMenuButton::InitializeRuntimeDesignTargets()
{
	using namespace ContextMenuButtonDesign;
	RuntimeDesignTargets.Reset();

	for (int32 i = 0; i < DesignTargets.Num(); ++i)
	{
		const FContextMenuDesignTarget& Authored = DesignTargets[i];

		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			continue;
		}

		FContextMenuRuntimeDesignTarget& Entry = RuntimeDesignTargets.AddDefaulted_GetRef();
		Entry.Widget = Widget;
		Entry.AuthoredIndex = i;

		// Each channel starts from what the widget currently shows, so the first transition blends out of the
		// authored look rather than snapping to the Normal design first.
		Entry.CurrentTint = ExtractWidgetColor(Widget);
		Entry.CurrentOutlineTint = ExtractWidgetOutlineTint(Widget);
		Entry.CurrentOpacity = Widget->GetRenderOpacity();

		// The same reading, kept rather than only used as a starting point. This is the value a state that
		// doesn't drive a channel blends back to, so it has to be captured before anything writes over it -
		// which is here, once, and never again for the life of the button.
		Entry.BaselineTint = Entry.CurrentTint;
		Entry.BaselineOutlineTint = Entry.CurrentOutlineTint;
		Entry.BaselineOpacity = Entry.CurrentOpacity;
		Entry.BaselineBrushTexture = ExtractWidgetBrushTexture(Widget);
		Entry.BaselineMaterial = ExtractWidgetMaterial(Widget);
		Entry.BaselineSVG = ExtractWidgetSVG(Widget);
		Entry.bHasBaseline = true;

		// Material parameters have no reliable "current" to read back per parameter, so they start at the
		// Normal design - resolved the same way everything else reads it, so an un-overridden Normal state
		// picks its parameters up from Base Design instead of starting blank.
		if (Authored.DrivesMaterialAnywhere())
		{
			const FContextMenuWidgetDesign& NormalDesign = Authored.GetDesignForState(EContextMenuState::Normal, bEnabledMatchesHoverDesign);
			for (const FContextMenuMaterialScalarValue& Scalar : NormalDesign.MaterialScalars)
			{
				if (!Scalar.ParameterName.IsNone())
				{
					Entry.CurrentScalars.Add(Scalar.ParameterName, Scalar.Value);
				}
			}
			for (const FContextMenuMaterialVectorValue& Vector : NormalDesign.MaterialVectors)
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
	const FContextMenuWidgetDesign Design = ResolveDesignForState(Authored, State);

	float Speed = Authored.InterpolationSpeed;
	if (Authored.DesignSource == EContextMenuButtonSettingSource::UseProjectSettings)
	{
		const UInfoCardDeveloperSettings* Settings = UInfoCardDeveloperSettings::Get();
		if (const FContextMenuDesignPreset* Preset = Settings ? Settings->FindContextMenuDesignPreset(Authored.PresetName) : nullptr)
		{
			Speed = Preset->InterpolationSpeed;
		}
	}
	bool bDone = true;

	// A channel that this state doesn't drive isn't skipped - it's driven back to the widget's own authored
	// value. Skipping would leave whatever the last state happened to write sitting there, which is what made
	// the old target-level switches all-or-nothing; blending to the baseline instead is what lets a channel be
	// switched on for one state and simply not apply to the others.
	if (Authored.DrivesTintAnywhere())
	{
		const FLinearColor DesiredTint = Design.bDriveTint ? Design.Tint : Target.BaselineTint;
		bDone &= InterpColor(Target.CurrentTint, DesiredTint, DeltaTime, Speed, bInstant, ColorTolerance);
		ApplyColorToWidget(Widget, Target.CurrentTint);
	}

	if (Authored.DrivesOutlineTintAnywhere())
	{
		const FLinearColor DesiredOutlineTint = Design.bDriveOutlineTint ? Design.OutlineTint : Target.BaselineOutlineTint;
		bDone &= InterpColor(Target.CurrentOutlineTint, DesiredOutlineTint, DeltaTime, Speed, bInstant, ColorTolerance);
		ApplyOutlineTintToWidget(Widget, Target.CurrentOutlineTint);
	}

	if (Authored.DrivesOpacityAnywhere())
	{
		const float DesiredOpacity = Design.bDriveOpacity ? Design.Opacity : Target.BaselineOpacity;
		bDone &= InterpScalar(Target.CurrentOpacity, DesiredOpacity, DeltaTime, Speed, bInstant, ColorTolerance);
		Widget->SetRenderOpacity(Target.CurrentOpacity);
	}

	// Discrete: a texture can't be blended, so it swaps once, on the frame the state changes.
	if (Authored.DrivesBrushTextureAnywhere() && (!Target.bHasAppliedBrush || Target.LastBrushState != State))
	{
		Target.bHasAppliedBrush = true;
		Target.LastBrushState = State;

		// An SVG for this state; otherwise this state's texture, or what the widget was authored with (an SVG or a texture).
		UWidgetSVG* DesiredSVG = Design.bDriveBrushTexture
			? (Design.BrushImageSource == EWidgetImageSource::SVG ? Design.BrushSVG.Get() : nullptr)
			: Target.BaselineSVG.Get();
		if (DesiredSVG)
		{
			ShowSVGOnWidget(Widget, DesiredSVG);
		}
		else
		{
			StopSVGOnWidget(Widget);
			UTexture2D* DesiredTexture = Design.bDriveBrushTexture ? Design.BrushTexture.Get() : Target.BaselineBrushTexture.Get();
			if (DesiredTexture && !(Design.bDriveBrushTexture && Design.BrushImageSource == EWidgetImageSource::SVG))
			{
				ApplyBrushTextureToWidget(Widget, DesiredTexture);
			}
		}
	}

	if (Authored.DrivesMaterialAnywhere())
	{
		// Same shape as the brush: a state that doesn't drive the material puts the authored one back, so a
		// material swap on hover undoes itself on unhover without a second entry to say so.
		UMaterialInterface* DesiredMaterial = Design.bDriveMaterial ? Design.Material.Get() : Target.BaselineMaterial.Get();
		// Discrete: material swaps once, on the frame the state changes.
		if (!Target.bHasAppliedMaterial || Target.LastMaterialState != State)
		{
			Target.bHasAppliedMaterial = true;
			Target.LastMaterialState = State;

			if (DesiredMaterial)
			{
				ApplyMaterialToWidget(Widget, DesiredMaterial);

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

		// Parameters are only pushed while this state actually drives the material. A state that doesn't has
		// no parameter values of its own to push, and the material it just restored already has its defaults.
		if (UMaterialInstanceDynamic* Material = Design.bDriveMaterial ? Target.CachedMaterial.Get() : nullptr)
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

	// Self: the button itself, whatever it is called.
	if (WidgetPath == SelfTargetName)
	{
		return this;
	}

	// A copy of a button (pasted, duplicated) has its children renamed - Label to Label_1 - while its targets still name the
	// originals. Matched again by the name without its number, among this button's own widgets only.
	if (!PathString.Contains(TEXT("/")))
	{
		const FString Stem = ContextMenuButtonOverrideNaming::StripInstanceSuffix(PathString);
		for (const ContextMenuButtonTree::FTargetableWidget& Candidate : All)
		{
			if (!Candidate.Path.Contains(TEXT("/"))
				&& ContextMenuButtonOverrideNaming::StripInstanceSuffix(Candidate.Path).Equals(Stem, ESearchCase::IgnoreCase))
			{
				return Candidate.Widget;
			}
		}
	}

	return nullptr;
}

const FName UContextMenuButton::SelfTargetName(TEXT("Self"));

bool UContextMenuButton::IsSelfTargetName(FName Name) const
{
	return !Name.IsNone() && (Name == SelfTargetName || Name == GetFName());
}

FVector2D UContextMenuButton::GetLastPressPosition() const
{
	if (const TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
	{
		return SlateButton->GetLastPressFraction();
	}
	return FVector2D(0.5, 0.5);
}

bool UContextMenuButton::GetLastPressScreenPosition(FVector2D& OutScreenPosition) const
{
	const TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin();
	if (!SlateButton.IsValid() || !SlateButton->GetLastPressScreenPosition().IsSet())
	{
		return false;
	}
	OutScreenPosition = SlateButton->GetLastPressScreenPosition().GetValue();
	return true;
}

TArray<FString> UContextMenuButton::GetColorTargetWidgetOptions() const
{
	TArray<ContextMenuButtonTree::FTargetableWidget> All;

	// Walks this button and every descendant through the panel/slot hierarchy, so Is Variable doesn't matter.
	ContextMenuButtonTree::Gather(const_cast<UContextMenuButton*>(this), FString(), /*bDescendIntoUserWidgets=*/true, All);

	// The button itself is offered as Self, which keeps working when the button is renamed or copied.
	TArray<FString> Options;
	Options.Reserve(All.Num());
	for (const ContextMenuButtonTree::FTargetableWidget& Entry : All)
	{
		Options.AddUnique(Entry.Widget == this ? SelfTargetName.ToString() : Entry.Path);
	}
	return Options;
}

UShapeWidget* UContextMenuButton::GetDesignShape(UWidget* Widget)
{
	if (const UContextMenuButton* Button = Cast<UContextMenuButton>(Widget))
	{
		return Button->BackgroundShapeWidget;
	}
	return Cast<UShapeWidget>(Widget);
}

FLinearColor UContextMenuButton::ExtractWidgetColor(UWidget* Widget)
{
	// A shape's fill. (A button's Background Shape is tinted through the button's Background Color, below, like its brush.)
	if (const UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
	{
		return Shape->Fill.Color;
	}
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
	if (UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
	{
		Shape->SetFillColor(Color);
	}
	else if (UTextBlock* Text = Cast<UTextBlock>(Widget))
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

FLinearColor UContextMenuButton::ExtractWidgetOutlineTint(UWidget* Widget)
{
	// A vector shape's outline is its stroke.
	if (const UShapeWidget* Shape = GetDesignShape(Widget))
	{
		return Shape->Stroke.Paint.Color;
	}

	// Only widgets whose look actually comes from an FSlateBrush have an outline to speak of - the same set
	// ApplyBrushTextureToWidget supports, and for the same reason (Text and RetainerBox have no brush).
	if (const UImage* Image = Cast<UImage>(Widget))
	{
		return Image->GetBrush().OutlineSettings.Color.GetSpecifiedColor();
	}
	if (const UBorder* Border = Cast<UBorder>(Widget))
	{
		return Border->Background.OutlineSettings.Color.GetSpecifiedColor();
	}
	if (const UButton* Button = Cast<UButton>(Widget))
	{
		return Button->GetStyle().Normal.OutlineSettings.Color.GetSpecifiedColor();
	}
	return FLinearColor::Transparent;
}

void UContextMenuButton::ApplyOutlineTintToWidget(UWidget* Widget, const FLinearColor& Color)
{
	// The outline is baked into the brush's own OutlineSettings rather than being multiplied in at paint
	// time the way Tint is, so coloring it means writing a modified copy of the brush back rather than
	// setting a separate multiplier - there's no equivalent of SetColorAndOpacity for it.
	if (UShapeWidget* Shape = GetDesignShape(Widget))
	{
		Shape->SetStrokeColor(Color);
	}
	else if (UImage* Image = Cast<UImage>(Widget))
	{
		FSlateBrush Brush = Image->GetBrush();
		Brush.OutlineSettings.Color = FSlateColor(Color);
		Image->SetBrush(Brush);
	}
	else if (UBorder* Border = Cast<UBorder>(Widget))
	{
		FSlateBrush Brush = Border->Background;
		Brush.OutlineSettings.Color = FSlateColor(Color);
		Border->SetBrush(Brush);
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		// Applied to every native state brush at once, exactly like ApplyBrushTextureToWidget and
		// ApplyMaterialToWidget: once a Design Target owns this button's own brush, the design is the whole
		// look, and Tint (via Background Color) is the only channel that still varies per state.
		FButtonStyle Style = Button->GetStyle();
		const FSlateColor SlateColor(Color);
		Style.Normal.OutlineSettings.Color = SlateColor;
		Style.Hovered.OutlineSettings.Color = SlateColor;
		Style.Pressed.OutlineSettings.Color = SlateColor;
		Style.Disabled.OutlineSettings.Color = SlateColor;
		Button->SetStyle(Style);
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

	if (UShapeWidget* Shape = GetDesignShape(Widget))
	{
		FWidgetShapePaint& Paint = Shape->GetImagePaint();
		Paint.Type = EWidgetShapePaintType::Image;
		Paint.ImageSource = EWidgetImageSource::Texture;
		Paint.Image.SetResourceObject(Texture);
		Shape->InvalidateShape();
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

	if (UShapeWidget* Shape = GetDesignShape(Widget))
	{
		FWidgetShapePaint& Paint = Shape->GetImagePaint();
		Paint.Type = EWidgetShapePaintType::Image;
		Paint.ImageSource = EWidgetImageSource::Texture;
		Paint.Image.SetResourceObject(Material);
		Shape->InvalidateShape();
	}
	else if (UImage* Image = Cast<UImage>(Widget))
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

UTexture2D* UContextMenuButton::ExtractWidgetBrushTexture(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (const UShapeWidget* Shape = GetDesignShape(Widget))
	{
		const FWidgetShapePaint& Paint = Shape->GetImagePaint();
		return Paint.DrawsBrush() ? Cast<UTexture2D>(Paint.Image.GetResourceObject()) : nullptr;
	}
	if (UImage* Image = Cast<UImage>(Widget))
	{
		return Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
	}
	if (UBorder* Border = Cast<UBorder>(Widget))
	{
		return Cast<UTexture2D>(Border->Background.GetResourceObject());
	}
	if (UButton* Button = Cast<UButton>(Widget))
	{
		return Cast<UTexture2D>(Button->GetStyle().Normal.GetResourceObject());
	}

	// Text and retainers have no brush texture to speak of; null means "nothing to restore", which is the
	// right answer rather than a failure.
	return nullptr;
}

UWidgetSVG* UContextMenuButton::ExtractWidgetSVG(UWidget* Widget)
{
	if (const UShapeWidget* Shape = GetDesignShape(Widget))
	{
		const FWidgetShapePaint& Paint = Shape->GetImagePaint();
		return Paint.DrawsSVG() ? Paint.SVG.Get() : nullptr;
	}
	if (const UWidgetSVGImage* SVGImage = Cast<UWidgetSVGImage>(Widget))
	{
		return SVGImage->SVG;
	}
	return nullptr;
}

void UContextMenuButton::ShowSVGOnWidget(UWidget* Widget, UWidgetSVG* SVG)
{
	if (!Widget || !SVG)
	{
		return;
	}

	// Widgets that draw SVGs themselves take it as it is.
	if (UShapeWidget* Shape = GetDesignShape(Widget))
	{
		FWidgetShapePaint& Paint = Shape->GetImagePaint();
		Paint.Type = EWidgetShapePaintType::Image;
		Paint.ImageSource = EWidgetImageSource::SVG;
		Paint.SVG = SVG;
		Shape->InvalidateShape();
		return;
	}
	if (UWidgetSVGImage* SVGImage = Cast<UWidgetSVGImage>(Widget))
	{
		SVGImage->SetSVG(SVG);
		return;
	}

	// An Image, Border or Button: its brush becomes the SVG drawn at its size, what it had kept to put back.
	UImage* Image = Cast<UImage>(Widget);
	UBorder* Border = Cast<UBorder>(Widget);
	UButton* Button = Cast<UButton>(Widget);
	if (!Image && !Border && !Button)
	{
		return;
	}

	FContextMenuTargetSVG& Entry = TargetSVGs.FindOrAdd(Widget);
	if (!Entry.BrushBefore.IsSet() && !Entry.StyleBefore.IsSet())
	{
		if (Image)
		{
			Entry.BrushBefore = Image->GetBrush();
		}
		else if (Border)
		{
			Entry.BrushBefore = Border->Background;
		}
		else
		{
			Entry.StyleBefore = Button->GetStyle();
		}
	}
	if (Entry.SVG.Get() != SVG)
	{
		Entry.SVG = SVG;
		Entry.PushedResource = NAME_None;
	}
	PushTargetSVG(Widget, Entry);
	StartTargetSVGTimer();
}

void UContextMenuButton::StopSVGOnWidget(UWidget* Widget)
{
	FContextMenuTargetSVG Entry;
	if (!Widget || !TargetSVGs.RemoveAndCopyValue(Widget, Entry))
	{
		return;
	}

	// Put back before the drawing goes (Entry, going out of scope, lets go of it).
	if (Entry.BrushBefore.IsSet())
	{
		if (UImage* Image = Cast<UImage>(Widget))
		{
			Image->SetBrush(Entry.BrushBefore.GetValue());
		}
		else if (UBorder* Border = Cast<UBorder>(Widget))
		{
			Border->SetBrush(Entry.BrushBefore.GetValue());
		}
	}
	else if (Entry.StyleBefore.IsSet())
	{
		if (UButton* Button = Cast<UButton>(Widget))
		{
			Button->SetStyle(Entry.StyleBefore.GetValue());
		}
	}
}

void UContextMenuButton::PushTargetSVG(UWidget* Widget, FContextMenuTargetSVG& Entry)
{
	const TSharedPtr<SWidget> TargetSlate = Widget ? Widget->GetCachedWidget() : nullptr;
	UWidgetSVG* SVG = Entry.SVG.Get();
	if (!TargetSlate.IsValid() || !SVG)
	{
		return;
	}

	// The widget's size on screen; before its first layout, the SVG's own size.
	const FGeometry& TargetGeometry = Widget->GetCachedGeometry();
	const FVector2D LaidOutSize = TargetGeometry.GetLocalSize();
	const bool bLaidOut = LaidOutSize.X > 0.0 && LaidOutSize.Y > 0.0;
	const FSlateBrush* Drawn = bLaidOut
		? Entry.Drawing.Get(SVG, nullptr, TargetGeometry, FVector2f(static_cast<float>(LaidOutSize.X), static_cast<float>(LaidOutSize.Y)), *TargetSlate)
		: Entry.Drawing.GetAtPixelSize(SVG, nullptr, FIntPoint(FMath::Max(FMath::CeilToInt32(SVG->SVGSize.X), 1), FMath::Max(FMath::CeilToInt32(SVG->SVGSize.Y), 1)), TargetSlate.Get());
	if (!Drawn || Drawn->GetResourceName() == Entry.PushedResource)
	{
		return;
	}
	Entry.PushedResource = Drawn->GetResourceName();

	// The drawing as the image, with the look (draw as, margin, tint, size) the widget's brush already has.
	auto WithLookOf = [Drawn](const FSlateBrush& Look)
	{
		FSlateBrush Result = *Drawn;
		Result.DrawAs = Look.DrawAs;
		Result.Tiling = Look.Tiling;
		Result.Mirroring = Look.Mirroring;
		Result.Margin = Look.Margin;
		Result.TintColor = Look.TintColor;
		Result.OutlineSettings = Look.OutlineSettings;
		Result.ImageSize = Look.ImageSize;
		return Result;
	};

	if (UImage* Image = Cast<UImage>(Widget))
	{
		Image->SetBrush(WithLookOf(Image->GetBrush()));
	}
	else if (UBorder* Border = Cast<UBorder>(Widget))
	{
		Border->SetBrush(WithLookOf(Border->Background));
	}
	else if (UButton* Button = Cast<UButton>(Widget))
	{
		FButtonStyle SVGButtonStyle = Button->GetStyle();
		SVGButtonStyle.Normal = WithLookOf(SVGButtonStyle.Normal);
		SVGButtonStyle.Hovered = WithLookOf(SVGButtonStyle.Hovered);
		SVGButtonStyle.Pressed = WithLookOf(SVGButtonStyle.Pressed);
		SVGButtonStyle.Disabled = WithLookOf(SVGButtonStyle.Disabled);
		Button->SetStyle(SVGButtonStyle);
	}
}

void UContextMenuButton::StartTargetSVGTimer()
{
	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!bIsTargetSVGTimerActive && SlateWidget.IsValid() && TargetSVGs.Num() > 0)
	{
		bIsTargetSVGTimerActive = true;
		SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UContextMenuButton::TickTargetSVGs));
	}
}

EActiveTimerReturnType UContextMenuButton::TickTargetSVGs(double InCurrentTime, float InDeltaTime)
{
	for (auto It = TargetSVGs.CreateIterator(); It; ++It)
	{
		UWidget* Widget = It.Key().Get();
		if (!Widget || !It.Value().SVG.IsValid())
		{
			It.RemoveCurrent();
			continue;
		}
		PushTargetSVG(Widget, It.Value());
	}

	if (TargetSVGs.Num() == 0)
	{
		bIsTargetSVGTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

UMaterialInterface* UContextMenuButton::ExtractWidgetMaterial(UWidget* Widget)
{
	if (!Widget)
	{
		return nullptr;
	}

	if (const UShapeWidget* Shape = GetDesignShape(Widget))
	{
		const FWidgetShapePaint& Paint = Shape->GetImagePaint();
		return Paint.Type == EWidgetShapePaintType::Image ? Cast<UMaterialInterface>(Paint.Image.GetResourceObject()) : nullptr;
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

UMaterialInstanceDynamic* UContextMenuButton::ResolveWidgetMaterial(UWidget* Widget)
{
	// GetDynamicMaterial creates the instance on first call and returns the same one afterward. It returns null
	// unless the brush's resource is already a material, which is why a plain texture brush can't be driven this way.
	if (UShapeWidget* Shape = GetDesignShape(Widget))
	{
		return Shape->GetDynamicMaterial(Shape->GetImagePaint());
	}

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

	// A toolbar transition is painting the widgets; it hands back to this once it arrives (FinishDesignerPreviewState).
	if (bIsDesignerStateTimerActive)
	{
		return;
	}

	ClearDesignPreviews();

	// The Background Shape back as authored, so a Look or a material an earlier preview wrote doesn't linger.
	SyncBackgroundShape();

	// The toolbar's state when it drives the preview; else the placement's own dropdown when previewed inside a widget that has
	// been placed somewhere, otherwise the button's. Worked out by ResolveInstanceOverrides, which every caller has just run.
	const EContextMenuDesignPreview Preview = GetEffectiveDesignPreview();
	const bool bNoPreview = (Preview == EContextMenuDesignPreview::None);
	const EContextMenuState State = bNoPreview ? EContextMenuState::Normal : PreviewToState(Preview);

	// Every Shape Target's state shape (Transform and Shape - for whichever widget it targets) always follows
	// the global preview, and falls back to Normal when there isn't one. Applied
	// directly rather than through the runtime blend, since a preview should show up immediately, not ease in.
	for (const FContextMenuShapeTarget& ShapeAuthored : ShapeTargets)
	{
		UWidget* ShapeWidget = FindWidgetInButtonTree(ShapeAuthored.TargetWidgetName);
		if (!ShapeWidget)
		{
			continue;
		}

		const FContextMenuShapeStateSettings Shape = ResolveShapeForState(ShapeAuthored, State);
		ApplyShapeToTargetWidget(ShapeWidget, Shape.Shape, Shape.Transform);

		if (UShapeWidget* LookWidget = GetLookWidget(ShapeWidget))
		{
			TArray<const FProperty*> LookProperties;
			GatherLookProperties(ShapeAuthored, LookWidget, LookProperties);
			if (LookProperties.Num() > 0)
			{
				// Copied, not pointed at: the baseline list may grow while another target is gathered.
				const FInstancedPropertyBag* Found = FindLookBaseline(LookWidget);
				const FInstancedPropertyBag Baseline = Found ? *Found : FInstancedPropertyBag();
				WriteLook(LookWidget, Shape.Look, LookProperties, &Baseline);
			}
		}
	}

	if (bNoPreview)
	{
		// Unlatch the pinned brush instead of pushing Normal through it, so the designer goes back to
		// painting Style.Hovered/Pressed/Disabled off Slate's own hover/press flags, exactly like a stock
		// SButton. Without this, bHasVisualState stays latched from the last preview and every state
		// paints Style.Normal, which is why Hovered/Pressed/Disabled edits (corner radii, tint, etc.)
		// stop visibly updating once any preview has run once.
		if (TSharedPtr<SContextMenuSlateButton> SlateButton = MyContextMenuSlateButton.Pin())
		{
			SlateButton->ClearVisualState();
		}
	}
	else
	{
		PushVisualStateToSlate(State);
	}

	// With no preview selected the designer normally leaves the target widgets showing exactly what they were
	// authored as, which is right - until a placement patches them, at which point that authored look is the
	// one thing it definitely isn't going to be. So a patched button previews its Normal design instead, and
	// the placement's own colors show up in the parent designer rather than only once you hit Play.
	// Num() alone was the wrong question: the patch array is rebuilt to hold one entry per target whether or
	// not anything in it is switched on, so a button with targets always looked "patched" and got its Normal
	// look forced onto every target even when the placement had changed nothing.
	const bool bHasAnythingPatched = ActiveShapePatch.ContainsByPredicate(
		[](const FContextMenuShapeTargetOverride& Entry) { return Entry.HasAnything(); })
		|| ActiveDesignPatch.ContainsByPredicate(
		[](const FContextMenuDesignTargetOverride& Entry) { return Entry.HasAnything(); });

	if (bNoPreview && !bHasAnythingPatched)
	{
		return;
	}


	// Now apply the corresponding state to all the design targets
	for (const FContextMenuDesignTarget& Authored : DesignTargets)
	{
		UWidget* Widget = FindWidgetInButtonTree(Authored.TargetWidgetName);
		if (!Widget)
		{
			continue;
		}

		// Same resolution the runtime uses, patch and all, so what the designer shows for a placement is what
		// that placement will actually look like. Previously the preview read the authored design directly,
		// which is why an instance override was invisible until you hit Play.
		const FContextMenuWidgetDesign Design = ResolveDesignForState(Authored, State);

		// Captured before anything is written, so the widget can be restored exactly.
		FContextMenuDesignPreviewBaseline& Baseline = DesignPreviewBaselines.AddDefaulted_GetRef();
		Baseline.Widget = Widget;
		Baseline.Tint = ExtractWidgetColor(Widget);
		Baseline.OutlineTint = ExtractWidgetOutlineTint(Widget);
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
		else if (const UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
		{
			const FWidgetShapePaint& Paint = Shape->GetImagePaint();
			UObject* Resource = Paint.DrawsBrush() ? Paint.Image.GetResourceObject() : nullptr;
			Baseline.BrushTexture = Cast<UTexture2D>(Resource);
			Baseline.Material = Cast<UMaterialInterface>(Resource);
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
		Baseline.SVG = ExtractWidgetSVG(Widget);

		// Per-state now, and read off the resolved design rather than off the target: a channel this state
		// doesn't drive is simply left showing whatever the widget was authored as, which is exactly what the
		// baseline restore does at runtime. ClearDesignPreviews at the top of this function has already put
		// the widgets back, so "leave it alone" and "restore the baseline" are the same thing here.
		if (Design.bDriveTint)
		{
			ApplyColorToWidget(Widget, Design.Tint);
		}

		if (Design.bDriveOutlineTint)
		{
			ApplyOutlineTintToWidget(Widget, Design.OutlineTint);
		}

		if (Design.bDriveOpacity)
		{
			Widget->SetRenderOpacity(Design.Opacity);
		}

		if (Design.bDriveBrushTexture && Design.BrushImageSource == EWidgetImageSource::SVG)
		{
			ShowSVGOnWidget(Widget, Design.BrushSVG);
		}
		else if (Design.bDriveBrushTexture && Design.BrushTexture)
		{
			ApplyBrushTextureToWidget(Widget, Design.BrushTexture);
		}

		if (Design.bDriveMaterial)
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
		ApplyOutlineTintToWidget(Widget, Baseline.OutlineTint);
		Widget->SetRenderOpacity(Baseline.Opacity);

		// An SVG drawn into an Image, Border or Button's brush: its own brush back first.
		StopSVGOnWidget(Widget);

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

		// A shape or SVG Image authored with an SVG gets it back.
		if (UWidgetSVG* BaselineSVGAsset = Baseline.SVG.Get())
		{
			ShowSVGOnWidget(Widget, BaselineSVGAsset);
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

	const bool bIsStateEnabled = GetEnabledState();

	// Enabled -> un-enabled while the cursor (or navigation focus) is on the button: keep hover effects off until it
	// leaves and comes back, for whichever of shape/designs was being suppressed while enabled.
	if (bWasEnabled && !bIsStateEnabled && (IsHovered() || bHasNavigationFocus))
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

	if (bIsStateEnabled && !bWasEnabled)
	{
		// Button Can Be Enabled may have been switched on at runtime after this button was built.
		UpdateEnableGroupRegistration();

		// Group-mates are un-enabled before this button reports its own change, so a Blueprint listening to both
		// sees the old choice go away before the new one arrives.
		EnforceEnableGroupExclusivity();
	}

	if (bIsStateEnabled != bLastReportedEnabledState)
	{
		bLastReportedEnabledState = bIsStateEnabled;
		OnContextEnabledStateChanged.Broadcast(bIsStateEnabled);

		BroadcastToOwnerDispatchers(EContextMenuButtonEvent::EnabledStateChanged, bIsStateEnabled);
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
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		BuildAncestorChain(Candidates[i], Chains[i]);
	}

	// Named group: every button with that name on the same screen (same top-most ancestor).
	if (!ButtonGroup.IsNone())
	{
		const UObject* ScreenRoot = Chains[0].Last();
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (Chains[i].Last() == ScreenRoot)
			{
				OutMembers.Add(Candidates[i]);
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
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		if (FindContainer(Chains[i]) == MyContainer)
		{
			OutMembers.Add(Candidates[i]);
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

// ==================================================================
// Instance override designer preview
// ==================================================================

#if WITH_EDITOR

void UContextMenuButton::StartDesignPreviewWatcher()
{
	if (bIsDesignPreviewWatcherActive || !IsDesignTime())
	{
		return;
	}

	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	bIsDesignPreviewWatcherActive = true;

	// A tenth of a second. Fast enough that dragging a color swatch in a parent designer reads as live,
	// slow enough that a screen full of these costs nothing worth measuring - and each tick is a struct
	// comparison that early-outs, not a repaint.
	SlateWidget->RegisterActiveTimer(0.1f, FWidgetActiveTimerDelegate::CreateUObject(this, &UContextMenuButton::TickDesignPreviewWatcher));
}

EActiveTimerReturnType UContextMenuButton::TickDesignPreviewWatcher(double InCurrentTime, float InDeltaTime)
{
	if (!IsDesignTime() || !GetCachedWidget().IsValid())
	{
		bIsDesignPreviewWatcherActive = false;
		return EActiveTimerReturnType::Stop;
	}

	// Never seeds. Seeding writes into whatever struct was found, and the struct found from a parent
	// designer may be a real object somebody is editing rather than a throwaway preview copy.
	ResolveInstanceOverrides(/*bSeedSource=*/false);

	if (HasResolvedOverridesChanged())
	{
		ApplyDesignPreviews();
		CacheResolvedOverridesSnapshot();
	}

	return EActiveTimerReturnType::Continue;
}

bool UContextMenuButton::HasResolvedOverridesChanged() const
{
	if (!bHasPaintedDesignPreviewOnce)
	{
		return true;
	}

	if (LastPaintedPreview != GetEffectiveDesignPreview())
	{
		return true;
	}

	// CompareScriptStruct rather than a hand-written comparison: these structs gain fields regularly, and
	// a comparison that silently stops covering a new one would show up as "the preview sometimes doesn't
	// update", which is a miserable thing to track down.
	if (ActiveShapePatch.Num() != LastPaintedShapePatch.Num())
	{
		return true;
	}

	const UScriptStruct* ShapeTargetStruct = FContextMenuShapeTargetOverride::StaticStruct();
	if (!ShapeTargetStruct)
	{
		return false;
	}

	for (int32 i = 0; i < ActiveShapePatch.Num(); ++i)
	{
		if (!ShapeTargetStruct->CompareScriptStruct(&ActiveShapePatch[i], &LastPaintedShapePatch[i], PPF_None))
		{
			return true;
		}
	}

	if (ActiveDesignPatch.Num() != LastPaintedDesignPatch.Num())
	{
		return true;
	}

	const UScriptStruct* TargetStruct = FContextMenuDesignTargetOverride::StaticStruct();
	if (!TargetStruct)
	{
		return false;
	}

	for (int32 i = 0; i < ActiveDesignPatch.Num(); ++i)
	{
		if (!TargetStruct->CompareScriptStruct(&ActiveDesignPatch[i], &LastPaintedDesignPatch[i], PPF_None))
		{
			return true;
		}
	}

	return false;
}

void UContextMenuButton::CacheResolvedOverridesSnapshot()
{
	bHasPaintedDesignPreviewOnce = true;
	LastPaintedPreview = GetEffectiveDesignPreview();
	LastPaintedShapePatch = ActiveShapePatch;
	LastPaintedDesignPatch = ActiveDesignPatch;
}

bool UContextMenuButton::ShouldRunDesignerAnimPreview() const
{
	// Kept as a named function because several places in this file read better for it, but the rule now
	// lives in one place for every host rather than once per class.
	return UWidgetAnimDesignerPreview::ShouldRun(const_cast<UContextMenuButton*>(this));
}

#endif // WITH_EDITOR

// ==================================================================
// Animation
// ==================================================================

UWidget* UContextMenuButton::ResolveAnimTarget(FName TargetWidgetName) const
{
	if (TargetWidgetName.IsNone())
	{
		return nullptr;
	}

	// Rooted at the button, not at the owning screen. An animation on a button should not be able to reach
	// out and move something on the other side of the menu - and the dropdown only offers this subtree, so
	// anything else in here came from a hand-typed name or a widget that has since been renamed.
	//
	// The button itself is in that subtree and is a perfectly good target: the shape system composes its
	// own transform into the Slate widget rather than writing UWidget::RenderTransform, so an animation
	// moving the button has nothing to collide with.
	return UWidgetAnimLibrary::FindWidgetByPath(const_cast<UContextMenuButton*>(this), TargetWidgetName);
}

TArray<FString> UContextMenuButton::GetAnimTargetWidgetOptions() const
{
	TArray<FString> Options;
	UWidgetAnimLibrary::GatherTargetableWidgetPaths(const_cast<UContextMenuButton*>(this), Options);

	// First, so there is a way back to "animate the button" once something has been picked. The button is
	// already in the list under its own name as well; None simply avoids having to go and find it.
	Options.Insert(TEXT("None"), 0);
	return Options;
}

void UContextMenuButton::RebuildEffectiveButtonAnimations()
{
	EffectiveInEntries.Reset();
	EffectiveOutEntries.Reset();

	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimIn, EffectiveInEntries, EffectiveOutEntries);
	UWidgetAnimEntryLibrary::AppendEntriesWithMirroring(AnimOut, EffectiveOutEntries, EffectiveInEntries);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	FlattenedAnimIn.Reset();
	FlattenedAnimOut.Reset();
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveInEntries, Resolver, FlattenedAnimIn);
	UWidgetAnimEntryLibrary::FlattenEntries(EffectiveOutEntries, Resolver, FlattenedAnimOut);

	// Only Material layers can be dropped, and only for want of a widget with a material of that kind to drive - an
	// Image or Border's brush, a Text Block's font: a scalar or vector parameter needs a material instance, and there is
	// nothing sensible to fall back to. Every other
	// layer is kept - a layer naming no target animates the button itself, which the shape system composes
	// into the Slate widget rather than fighting over UWidget::RenderTransform.
	auto DropUndrivableMaterialLayers = [](TArray<FWidgetAnimSettings>& Stack) -> int32
	{
		const int32 Before = Stack.Num();

		Stack.RemoveAll([](const FWidgetAnimSettings& Settings)
		{
			const bool bIsMaterialEntry = Settings.AnimType == EWidgetAnimType::MaterialScalar || Settings.AnimType == EWidgetAnimType::MaterialVector;
			return bIsMaterialEntry && !UWidgetAnimLibrary::WidgetHasMaterialSlot(Settings.GetMaterialWidget(), Settings.MaterialSlot);
		});

		return Before - Stack.Num();
	};

	const int32 DroppedIn = DropUndrivableMaterialLayers(FlattenedAnimIn);
	const int32 DroppedOut = DropUndrivableMaterialLayers(FlattenedAnimOut);

	ButtonAnimationStatus = FString::Printf(
		TEXT("Anim In: %d layer(s). Anim Out: %d layer(s)."),
		FlattenedAnimIn.Num(),
		FlattenedAnimOut.Num());

	if (DroppedIn > 0 || DroppedOut > 0)
	{
		ButtonAnimationStatus += FString::Printf(
			TEXT("\n\n%d Material layer(s) skipped: a Material Scalar or Material Vector layer needs its entry's Target Widget to name an Image, since there is no material instance to drive otherwise."),
			DroppedIn + DroppedOut);
	}
}

void UContextMenuButton::RecaptureButtonAnimBaselines()
{
	// The button is the fallback widget, so a layer naming no target gets a baseline captured for it like
	// any other - without one it would animate from an assumed identity pose rather than from how the
	// button was actually authored. That, and the restore-before-recapture the set needs, is the same
	// everywhere, so it lives on the shared harness.
	UWidgetAnimDesignerPreview::RecaptureBaselines(this);
}

void UContextMenuButton::PlayButtonAnimIn()
{
	PlayWidgetAnimationsById(TArray<FName>(), /*bReverse=*/false, /*PlaybackSpeed=*/1.f, /*bHoldFinalPose=*/true, /*bRestartIfPlaying=*/true);
}

void UContextMenuButton::PlayButtonAnimOut()
{
	PlayWidgetAnimationsById(TArray<FName>(), /*bReverse=*/true, /*PlaybackSpeed=*/1.f, /*bHoldFinalPose=*/true, /*bRestartIfPlaying=*/true);
}

void UContextMenuButton::PlayWidgetAnimationsById(const TArray<FName>& AnimIds, bool bReverse, float PlaybackSpeed, bool bHoldFinalPose, bool bRestartIfPlaying)
{
	if (FlattenedAnimIn.Num() == 0 && FlattenedAnimOut.Num() == 0)
	{
		RebuildEffectiveButtonAnimations();
	}

	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(this, FlattenedAnimIn, FlattenedAnimOut, ButtonAnimBaselines);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };

	UWidgetAnimEntryLibrary::StartEntriesById(
		ButtonAnimPlayer,
		bReverse ? EffectiveOutEntries : EffectiveInEntries,
		Resolver,
		AnimIds,
		bReverse,
		PlaybackSpeed,
		bRestartIfPlaying,
		bHoldFinalPose);

	StartButtonAnimTimer();
}

void UContextMenuButton::StopWidgetAnimationsById(const TArray<FName>& AnimIds, bool bResetToStart)
{
	UWidgetAnimLibrary::StopWidgetAnimTracks(ButtonAnimPlayer, AnimIds, /*bRemoveTracks=*/bResetToStart);

	if (bResetToStart)
	{
		// Removing the tracks takes their contribution out of the combined result, but nothing has
		// re-applied it yet, so the widgets would sit on the last frame until something else animated.
		UWidgetAnimLibrary::RestoreWidgetAnimBaselines(ButtonAnimBaselines);

		TArray<FName> JustFinished;
		UWidgetAnimLibrary::TickWidgetAnimPlayer(this, ButtonAnimPlayer, ButtonAnimBaselines, 0.f, JustFinished);
	}
}

bool UContextMenuButton::AreWidgetAnimationsPlayingById(const TArray<FName>& AnimIds) const
{
	return UWidgetAnimLibrary::AreWidgetAnimTracksPlaying(ButtonAnimPlayer, AnimIds);
}

bool UContextMenuButton::SetWidgetAnimationValues(const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap, bool bStartIfNotPlaying)
{
	// The designer previews value-driven tracks on their timelines; only a live button takes values.
	if (IsDesignTime())
	{
		return false;
	}

	if (FlattenedAnimIn.Num() == 0 && FlattenedAnimOut.Num() == 0)
	{
		RebuildEffectiveButtonAnimations();
	}

	UWidgetAnimLibrary::CaptureWidgetAnimBaselines(this, FlattenedAnimIn, FlattenedAnimOut, ButtonAnimBaselines);

	auto Resolver = [this](FName Path) { return ResolveAnimTarget(Path); };
	const int32 Taken = UWidgetAnimEntryLibrary::SetEntriesValue(ButtonAnimPlayer, EffectiveInEntries, Resolver, Id, Value, bSnap, bStartIfNotPlaying);

	// The timer stops once nothing moves, so it is started again for the value to travel - and a snap, which moves
	// nothing over time, is posed now.
	TArray<FName> JustFinished;
	UWidgetAnimLibrary::TickWidgetAnimPlayer(this, ButtonAnimPlayer, ButtonAnimBaselines, 0.f, JustFinished);
	StartButtonAnimTimer();
	return Taken > 0;
}

FVector4 UContextMenuButton::GetWidgetAnimationValues(const FWidgetAnimValueId& Id, bool bTargetValue) const
{
	FVector4 Value(0.0, 0.0, 0.0, 0.0);
	UWidgetAnimLibrary::GetWidgetAnimTrackValue(ButtonAnimPlayer, Id, bTargetValue, Value);
	return Value;
}

void UContextMenuButton::StartButtonAnimTimer()
{
	if (bIsButtonAnimTimerActive)
	{
		return;
	}

	TSharedPtr<SWidget> SlateWidget = GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		return;
	}

	// An active timer rather than a tick: it keeps the widget ticking AND forces the repaint, which is
	// what makes the animation visible in the designer viewport rather than only when something else
	// happens to redraw it. Same mechanism the visual state transitions already use.
	bIsButtonAnimTimerActive = true;
	bButtonAnimsResting = false;
	bButtonAnimsHeldLastTick = false;
	SlateWidget->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UContextMenuButton::TickButtonAnimations));
}

EActiveTimerReturnType UContextMenuButton::TickButtonAnimations(double InCurrentTime, float InDeltaTime)
{
	if (!GetCachedWidget().IsValid())
	{
		bIsButtonAnimTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

#if WITH_EDITOR
	if (IsDesignTime())
	{
		if (!ShouldRunDesignerAnimPreview())
		{
			UWidgetAnimDesignerPreview::Stop(this);
			bIsButtonAnimTimerActive = false;
			return EActiveTimerReturnType::Stop;
		}

		// TickFrame rather than Tick: this timer is the button's own and it owns the flag that says so,
		// so the harness is asked only for the frame, not for the timer bookkeeping.
		if (!UWidgetAnimDesignerPreview::TickFrame(this, InDeltaTime))
		{
			// A held pose, or a Play Once that has finished. Editing any property re-synchronizes the
			// button, which starts the timer again, so nothing is lost by stopping here. Scrub never
			// reaches this branch - it asks for frames for as long as it is the selected mode.
			bIsButtonAnimTimerActive = false;
			return EActiveTimerReturnType::Stop;
		}

		return EActiveTimerReturnType::Continue;
	}
#endif

	// At rest, following only its inputs: they are read, and the frame is worked out only if a read moved a value - the pose
	// would otherwise be the one already applied.
	if (bButtonAnimsResting && ButtonAnimRestRevision == ButtonAnimPlayer.Revision && InDeltaTime > 0.f)
	{
		UWidgetAnimLibrary::ReadValueInputs(this, ButtonAnimPlayer, InDeltaTime);
		if (ButtonAnimRestRevision == ButtonAnimPlayer.Revision)
		{
			return EActiveTimerReturnType::Continue;
		}
	}

	TArray<FName> JustFinished;
	const bool bAnyPlaying = UWidgetAnimLibrary::TickWidgetAnimPlayer(this, ButtonAnimPlayer, ButtonAnimBaselines, InDeltaTime, JustFinished);

	// A track following a Value Input - the pointer, a scroll - has to be read every frame even at rest, or it would stop
	// following the moment it caught up.
	const bool bReadsInputs = UWidgetAnimLibrary::HasValueInputs(ButtonAnimPlayer);
	if (!bAnyPlaying && !bReadsInputs)
	{
		bButtonAnimsResting = false;
		bButtonAnimsHeldLastTick = false;
		bIsButtonAnimTimerActive = false;
		return EActiveTimerReturnType::Stop;
	}

	// Resting from the next frame once two ticks in a row have found every track at rest, with nothing touching the player
	// in between.
	const bool bHolding = !bAnyPlaying && !UWidgetAnimLibrary::AnimatesAtRest(ButtonAnimPlayer);
	bButtonAnimsResting = bHolding && bButtonAnimsHeldLastTick && ButtonAnimRestRevision == ButtonAnimPlayer.Revision;
	bButtonAnimsHeldLastTick = bHolding;
	ButtonAnimRestRevision = ButtonAnimPlayer.Revision;

	return EActiveTimerReturnType::Continue;
}
// ==================================================================
// Details panel: instance override slots
// ==================================================================
//
// Why this is a customization and not just UPROPERTY metadata: every slot needs a checkbox on its own row that
// grays the row out when cleared, while the row as a whole only exists if the button has that slot. Unreal
// draws a row's checkbox from its Edit Condition, and Edit Condition Hides hides on that SAME condition - so with
// metadata alone, clearing the checkbox also made the row vanish, and it only came back once the asset was
// reopened and the button re-seeded it. Here the two are separate: which slots exist is decided when the rows are
// built, and the checkbox only ever grays.

#if WITH_EDITOR

namespace ContextMenuButtonOverrideDetails
{
	/** One slot of a target override, by property name. Index is the slot's position in SlotDefaults. */
	struct FSlotSpec
	{
		FName Slot;
		FName Switch;
		FName Applicable;
		int32 DefaultIndex = INDEX_NONE;
	};

	/** Reads a bool on the struct by name, whether or not the Details panel shows it. */
	bool ReadStructBool(const UScriptStruct* Struct, const void* Data, FName Name, bool bFallback)
	{
		const FBoolProperty* Property = (Struct && Data) ? FindFProperty<FBoolProperty>(Struct, Name) : nullptr;
		return Property ? Property->GetPropertyValue_InContainer(Data) : bFallback;
	}

	/** Pointer to SlotDefaults[Index] on the struct, or null. */
	const void* GetSlotDefault(const UScriptStruct* Struct, const void* Data, int32 Index)
	{
		const FArrayProperty* Array = (Struct && Data) ? FindFProperty<FArrayProperty>(Struct, TEXT("SlotDefaults")) : nullptr;
		if (!Array)
		{
			return nullptr;
		}

		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Data));
		return Helper.IsValidIndex(Index) ? Helper.GetRawPtr(Index) : nullptr;
	}

	/**
	 * Lays out FContextMenuShapeTargetOverride / FContextMenuDesignTargetOverride: the target's name on the
	 * header, then one row per slot the button has, each with a checkbox.
	 */
	class FContextMenuTargetOverrideCustomization : public IPropertyTypeCustomization
	{
	public:
		explicit FContextMenuTargetOverrideCustomization(TArray<FSlotSpec> InSlots)
			: Slots(MoveTemp(InSlots))
		{
		}

		virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
		{
			TSharedPtr<IPropertyHandle> TargetHandle = PropertyHandle->GetChildHandle(TEXT("TargetWidgetName"));

			HeaderRow
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				[
					SNew(STextBlock)
					.Font(CustomizationUtils.GetRegularFont())
					.Text_Lambda([TargetHandle]()
					{
						FName Name;
						return (TargetHandle.IsValid() && TargetHandle->GetValue(Name) == FPropertyAccess::Success) ? FText::FromName(Name) : FText::GetEmpty();
					})
				];
		}

		virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
		{
			if (TSharedPtr<IPropertyHandle> Target = PropertyHandle->GetChildHandle(TEXT("TargetWidgetName")))
			{
				ChildBuilder.AddProperty(Target.ToSharedRef());
			}

			const FStructProperty* StructProperty = CastField<FStructProperty>(PropertyHandle->GetProperty());
			const UScriptStruct* Struct = StructProperty ? StructProperty->Struct : nullptr;

			// Which slots exist comes from the first selected struct. Several placements selected at once
			// share a button, so they share its slots too.
			TArray<void*> RawData;
			PropertyHandle->AccessRawData(RawData);
			const void* FirstData = RawData.Num() > 0 ? RawData[0] : nullptr;


			for (const FSlotSpec& Spec : Slots)
			{
				if (!ReadStructBool(Struct, FirstData, Spec.Applicable, /*bFallback=*/true))
				{
					continue;
				}

				const TSharedPtr<IPropertyHandle> SlotHandle = PropertyHandle->GetChildHandle(Spec.Slot);
				const TSharedPtr<IPropertyHandle> SwitchHandle = PropertyHandle->GetChildHandle(Spec.Switch);
				if (!SlotHandle.IsValid() || !SwitchHandle.IsValid())
				{
					continue;
				}

				TWeakPtr<IPropertyHandle> WeakSlot = SlotHandle;
				TWeakPtr<IPropertyHandle> WeakSwitch = SwitchHandle;
				const int32 DefaultIndex = Spec.DefaultIndex;
				
				IDetailPropertyRow& Row = ChildBuilder.AddProperty(SlotHandle.ToSharedRef());
				Row.EditCondition(
					TAttribute<bool>::CreateLambda([SwitchHandle]()
					{
						bool bValue = false;
						return SwitchHandle->GetValue(bValue) == FPropertyAccess::Success && bValue;
					}),
					FOnBooleanValueChanged::CreateLambda([StructHandle = TSharedRef<IPropertyHandle>(PropertyHandle), Slot = SlotHandle.ToSharedRef(), Switch = SwitchHandle.ToSharedRef(), DefaultIndex](bool bNewValue)
					{
						// Cleared: the slot goes back to the button's values, as if it had never been
						// overridden. The row stays where it is, grayed out.
						if (!bNewValue)
						{
							ResetSlotToDefault(StructHandle, Slot, DefaultIndex);
						}

						Switch->SetValue(bNewValue);
					}));
			}
		}

	private:
		static void ResetSlotToDefault(const TSharedRef<IPropertyHandle>& StructHandle, const TSharedRef<IPropertyHandle>& SlotHandle, int32 DefaultIndex)
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(StructHandle->GetProperty());
			const FProperty* SlotProperty = SlotHandle->GetProperty();
			if (!StructProperty || !SlotProperty)
			{
				return;
			}

			TArray<void*> RawData;
			StructHandle->AccessRawData(RawData);
			if (RawData.Num() == 0)
			{
				return;
			}

			// Every selected struct has its own defaults, but a formatted-string write applies one value to
			// all of them, so the first one's defaults are used. Multi-selected placements share a button.
			const void* Default = GetSlotDefault(StructProperty->Struct, RawData[0], DefaultIndex);
			if (!Default)
			{
				return;
			}

			FString Exported;
			SlotProperty->ExportText_Direct(Exported, Default, nullptr, nullptr, PPF_None);
			SlotHandle->SetValueFromFormattedString(Exported);
		}

		TArray<FSlotSpec> Slots;
	};

	TArray<FSlotSpec> MakeShapeSlots()
	{
		return {
			{ TEXT("BaseShape"),     TEXT("bOverrideBaseShape"),     TEXT("bBaseShapeApplicable"),     0 },
			{ TEXT("NormalShape"),   TEXT("bOverrideNormalShape"),   TEXT("bNormalShapeApplicable"),   1 },
			{ TEXT("HoveredShape"),  TEXT("bOverrideHoveredShape"),  TEXT("bHoveredShapeApplicable"),  2 },
			{ TEXT("PressedShape"),  TEXT("bOverridePressedShape"),  TEXT("bPressedShapeApplicable"),  3 },
			{ TEXT("DisabledShape"), TEXT("bOverrideDisabledShape"), TEXT("bDisabledShapeApplicable"), 4 },
			{ TEXT("EnabledShape"),  TEXT("bOverrideEnabledShape"),  TEXT("bEnabledShapeApplicable"),  5 },
		};
	}

	TArray<FSlotSpec> MakeDesignSlots()
	{
		return {
			{ TEXT("BaseDesign"), TEXT("bOverrideBaseDesign"),     TEXT("bBaseDesignApplicable"),     0 },
			{ TEXT("Normal"),     TEXT("bOverrideNormalDesign"),   TEXT("bNormalDesignApplicable"),   1 },
			{ TEXT("Hovered"),    TEXT("bOverrideHoveredDesign"),  TEXT("bHoveredDesignApplicable"),  2 },
			{ TEXT("Pressed"),    TEXT("bOverridePressedDesign"),  TEXT("bPressedDesignApplicable"),  3 },
			{ TEXT("Disabled"),   TEXT("bOverrideDisabledDesign"), TEXT("bDisabledDesignApplicable"), 4 },
			{ TEXT("Enabled"),    TEXT("bOverrideEnabledDesign"),  TEXT("bEnabledDesignApplicable"),  5 },
		};
	}

	const FName PropertyEditorModuleName(TEXT("PropertyEditor"));

	void Register()
	{
		if (!GIsEditor || !FModuleManager::Get().IsModuleLoaded(PropertyEditorModuleName))
		{
			return;
		}

		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>(PropertyEditorModuleName);

		PropertyEditor.RegisterCustomPropertyTypeLayout(
			FContextMenuShapeTargetOverride::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateLambda([]()
			{
				return MakeShared<FContextMenuTargetOverrideCustomization>(MakeShapeSlots());
			}));

		PropertyEditor.RegisterCustomPropertyTypeLayout(
			FContextMenuDesignTargetOverride::StaticStruct()->GetFName(),
			FOnGetPropertyTypeCustomizationInstance::CreateLambda([]()
			{
				return MakeShared<FContextMenuTargetOverrideCustomization>(MakeDesignSlots());
			}));

		PropertyEditor.NotifyCustomizationModuleChanged();

		FCoreDelegates::OnPreExit.AddLambda([]()
		{
			if (FPropertyEditorModule* Module = FModuleManager::GetModulePtr<FPropertyEditorModule>(PropertyEditorModuleName))
			{
				Module->UnregisterCustomPropertyTypeLayout(FContextMenuShapeTargetOverride::StaticStruct()->GetFName());
				Module->UnregisterCustomPropertyTypeLayout(FContextMenuDesignTargetOverride::StaticStruct()->GetFName());
			}
		});
	}

	// Registered from here rather than from an editor module so the layout ships with the structs it lays out.
	// End of engine init: the Property Editor module is loaded by then in the editor, and not at all in a game.
	static FDelayedAutoRegisterHelper GRegisterTargetOverrideCustomization(EDelayedRegisterRunPhase::EndOfEngineInit, &Register);
}

#endif // WITH_EDITOR