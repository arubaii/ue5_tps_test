// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineRuntime.h"

#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "NameTBD/Buttons/ContextMenuButton.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "WidgetAnimPreset.h"
#include "WidgetAnimSpring.h"
#include "NameTBD/Widgets/ShapeBox.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetAnimStateMachine, Log, All);

// =====================================================================================================
// Channels and poses
// =====================================================================================================

uint8 WidgetAnimMachineChannels::GetChannels(const FWidgetAnimSettings& Settings)
{
	if (Settings.bMuted)
	{
		return 0;
	}

	switch (Settings.AnimType)
	{
	case EWidgetAnimType::Fade:
		return Opacity;
	case EWidgetAnimType::Scale:
	case EWidgetAnimType::Pulse:
		return Scale;
	case EWidgetAnimType::Slide:
		return Translation;
	case EWidgetAnimType::Rotate:
		return Angle;
	case EWidgetAnimType::Shake:
		return Translation | Angle;
	case EWidgetAnimType::Transform:
		return Scale | Translation | Shear | Angle;
	default:
		// Material parameters, Shape Box and Shape tracks, registered types and cues write no transform; their channels
		// are their own.
		return 0;
	}
}

int32 FWidgetAnimMachinePose::FindTransform(const UWidget* Widget, const FWidgetAnimPartKey& Part) const
{
	return Result.TargetedTransforms.IndexOfByPredicate([Widget, &Part](const FWidgetAnimTargetedTransform& Transform)
	{
		return Transform.TargetWidget == Widget && Transform.Part == Part;
	});
}

int32 FWidgetAnimMachinePose::FindOrAddTransform(UWidget* Widget, const FWidgetAnimPartKey& Part)
{
	const int32 Found = FindTransform(Widget, Part);
	if (Found != INDEX_NONE)
	{
		return Found;
	}

	FWidgetAnimTargetedTransform& Added = Result.TargetedTransforms.AddDefaulted_GetRef();
	Added.TargetWidget = Widget;
	Added.Part = Part;
	Masks.SetNumZeroed(Result.TargetedTransforms.Num());
	return Result.TargetedTransforms.Num() - 1;
}

void FWidgetAnimMachinePose::DetachParams()
{
	for (FWidgetAnimAuthoritativeWrite& Write : Result.AuthoritativeWrites)
	{
		Write.DetachParams();
	}
	// Cues are fired where they are passed; a pose kept for later has none.
	Result.CueFires.Reset();
}

namespace WidgetAnimStateMachines
{
	namespace
	{
		const FWidgetAnimBaselineSet& GetEmptyBaselines()
		{
			static const FWidgetAnimBaselineSet Empty;
			return Empty;
		}

		const FWidgetAnimMachineLayer* GetLayerDef(const FWidgetAnimMachineInstance& Instance, int32 LayerIndex)
		{
			const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
			return Machine && Machine->Layers.IsValidIndex(LayerIndex) ? &Machine->Layers[LayerIndex] : nullptr;
		}

		// -------------------------------------------------------------------------------------------------
		// Channels of one widget, for blending
		// -------------------------------------------------------------------------------------------------

		/** The eight numbers of a widget's pose, in the order a spring channel's Component counts them. */
		void ToComponents(const FWidgetAnimTargetedTransform& Transform, float OutValues[8])
		{
			OutValues[0] = Transform.Opacity;
			OutValues[1] = static_cast<float>(Transform.Scale.X);
			OutValues[2] = static_cast<float>(Transform.Scale.Y);
			OutValues[3] = static_cast<float>(Transform.Translation.X);
			OutValues[4] = static_cast<float>(Transform.Translation.Y);
			OutValues[5] = static_cast<float>(Transform.Shear.X);
			OutValues[6] = static_cast<float>(Transform.Shear.Y);
			OutValues[7] = Transform.Angle;
		}

		void FromComponents(const float Values[8], FWidgetAnimTargetedTransform& OutTransform)
		{
			OutTransform.Opacity = Values[0];
			OutTransform.Scale = FVector2D(Values[1], Values[2]);
			OutTransform.Translation = FVector2D(Values[3], Values[4]);
			OutTransform.Shear = FVector2D(Values[5], Values[6]);
			OutTransform.Angle = Values[7];
		}

		/** Which channel bit component Component belongs to. */
		uint8 ComponentChannel(int32 Component)
		{
			switch (Component)
			{
			case 0: return WidgetAnimMachineChannels::Opacity;
			case 1: case 2: return WidgetAnimMachineChannels::Scale;
			case 3: case 4: return WidgetAnimMachineChannels::Translation;
			case 5: case 6: return WidgetAnimMachineChannels::Shear;
			default: return WidgetAnimMachineChannels::Angle;
			}
		}

		/** How close a spring on Component has to be to count as arrived, in that component's own units. */
		float ComponentRestDistance(int32 Component)
		{
			return Component <= 2 ? 0.001f : 0.05f;
		}

		/** The rest value of a material parameter: what it held before anything animated it, when that was captured. */
		bool FindRestScalar(const FWidgetAnimMachineContext& Context, const FWidgetAnimScalarTarget& Target, float& OutValue)
		{
			const FWidgetAnimMaterialBaseline* Baseline = Context.RestBaselines ? Context.RestBaselines->FindMaterialBaseline(Target.TargetWidget, Target.Slot, Target.ParameterName, /*bVector=*/false) : nullptr;
			if (Baseline)
			{
				OutValue = Baseline->Scalar;
			}
			return Baseline != nullptr;
		}

		bool FindRestVector(const FWidgetAnimMachineContext& Context, const FWidgetAnimVectorTarget& Target, FLinearColor& OutValue)
		{
			const FWidgetAnimMaterialBaseline* Baseline = Context.RestBaselines ? Context.RestBaselines->FindMaterialBaseline(Target.TargetWidget, Target.Slot, Target.ParameterName, /*bVector=*/true) : nullptr;
			if (Baseline)
			{
				OutValue = Baseline->Vector;
			}
			return Baseline != nullptr;
		}

		/**
		 * Every write in Result aimed at its widget: null - "the widget the result is applied to" - becomes Host, and two
		 * entries that turn out to be the same widget become one, combined as tracks combine. Then two results can be
		 * folded entry by entry without one widget ending up written twice.
		 */
		void NormalizeTargets(FWidgetAnimResult& Result, UWidget* Host)
		{
			// Entries are re-aimed and merged in place below: the result's lookup of them can't be trusted after.
			Result.InvalidateTransformLookup();

			TArray<FWidgetAnimTargetedTransform>& Transforms = Result.TargetedTransforms;
			for (FWidgetAnimTargetedTransform& Transform : Transforms)
			{
				if (!Transform.TargetWidget)
				{
					Transform.TargetWidget = Host;
				}
			}
			for (int32 Index = 0; Index < Transforms.Num(); ++Index)
			{
				for (int32 Other = Transforms.Num() - 1; Other > Index; --Other)
				{
					if (Transforms[Other].TargetWidget == Transforms[Index].TargetWidget && Transforms[Other].Part == Transforms[Index].Part)
					{
						FWidgetAnimTargetedTransform& Kept = Transforms[Index];
						const FWidgetAnimTargetedTransform& Merged = Transforms[Other];
						Kept.Opacity *= Merged.Opacity;
						Kept.Scale *= Merged.Scale;
						Kept.Translation += Merged.Translation;
						Kept.Shear += Merged.Shear;
						Kept.Angle += Merged.Angle;
						Transforms.RemoveAt(Other);
					}
				}
			}

			// Parameters, texts and outlines: the later write wins, unless it is only a baseline and an earlier one is real.
			auto Dedupe = [](auto& Entries, auto SameTarget)
			{
				for (int32 Index = 0; Index < Entries.Num(); ++Index)
				{
					for (int32 Other = Entries.Num() - 1; Other > Index; --Other)
					{
						if (!SameTarget(Entries[Index], Entries[Other]))
						{
							continue;
						}
						if (!(Entries[Other].bIsBaseline && !Entries[Index].bIsBaseline))
						{
							Entries[Index] = Entries[Other];
						}
						Entries.RemoveAt(Other);
					}
				}
			};

			for (FWidgetAnimScalarTarget& Target : Result.ScalarParameters)
			{
				if (!Target.TargetWidget)
				{
					Target.TargetWidget = Host;
				}
			}
			Dedupe(Result.ScalarParameters, [](const FWidgetAnimScalarTarget& A, const FWidgetAnimScalarTarget& B)
			{
				return A.TargetWidget == B.TargetWidget && A.Slot == B.Slot && A.ParameterName == B.ParameterName;
			});

			for (FWidgetAnimVectorTarget& Target : Result.VectorParameters)
			{
				if (!Target.TargetWidget)
				{
					Target.TargetWidget = Host;
				}
			}
			Dedupe(Result.VectorParameters, [](const FWidgetAnimVectorTarget& A, const FWidgetAnimVectorTarget& B)
			{
				return A.TargetWidget == B.TargetWidget && A.Slot == B.Slot && A.ParameterName == B.ParameterName;
			});

			// Keys changed in place and entries removed: see FWidgetAnimMaterialLookup.
			Result.ScalarLookup.Reset();
			Result.VectorLookup.Reset();

			for (FWidgetAnimAuthoritativeWrite& Write : Result.AuthoritativeWrites)
			{
				if (!Write.TargetWidget)
				{
					Write.TargetWidget = Host;
				}
			}
			Dedupe(Result.AuthoritativeWrites, [](const FWidgetAnimAuthoritativeWrite& A, const FWidgetAnimAuthoritativeWrite& B)
			{
				return A.TargetWidget == B.TargetWidget && A.Channel == B.Channel;
			});

			UShapeBox* HostBox = nullptr;
			bool bHostBoxResolved = false;
			for (FWidgetAnimShapeTarget& Target : Result.ShapeTargets)
			{
				if (!Target.TargetBox)
				{
					if (!bHostBoxResolved)
					{
						HostBox = UWidgetAnimLibrary::ResolveShapeBox(Host);
						bHostBoxResolved = true;
					}
					Target.TargetBox = HostBox;
				}
			}
			Dedupe(Result.ShapeTargets, [](const FWidgetAnimShapeTarget& A, const FWidgetAnimShapeTarget& B)
			{
				return A.TargetBox == B.TargetBox;
			});
		}

		/** Which channels of each widget Pose's layer animates, from the settings its player is playing. */
		void ComputeMasks(const FWidgetAnimPlayer& Player, UWidget* Host, FWidgetAnimMachinePose& Pose)
		{
			Pose.Masks.Reset();
			Pose.Masks.SetNumZeroed(Pose.Result.TargetedTransforms.Num());
			for (const FWidgetAnimTrack& Track : Player.Tracks)
			{
				if (Track.bFinished && !Track.bHoldWhenFinished)
				{
					continue;
				}
				for (const FWidgetAnimSettings& Settings : Track.Settings)
				{
					const uint8 Bits = WidgetAnimMachineChannels::GetChannels(Settings);
					if (Bits == 0)
					{
						continue;
					}
					UWidget* Widget = Settings.TargetWidget ? Settings.TargetWidget.Get() : Host;
					const int32 Index = Pose.FindTransform(Widget, Settings.Part);
					if (Pose.Masks.IsValidIndex(Index))
					{
						Pose.Masks[Index] |= Bits;
					}
				}
			}
		}

		/**
		 * From blended toward To by Alpha, channel by channel - a widget one of them doesn't pose is at rest there - into Out.
		 * Material parameters blend from or to where they rest; texts and outlines change hands halfway.
		 */
		void BlendPoses(const FWidgetAnimMachinePose& From, const FWidgetAnimMachinePose& To, float Alpha, const FWidgetAnimMachineContext& Context, FWidgetAnimMachinePose& Out)
		{
			Out.Reset();

			// Transforms: every widget either poses, each owned where either owns it.
			for (int32 Index = 0; Index < From.Result.TargetedTransforms.Num(); ++Index)
			{
				const FWidgetAnimTargetedTransform& Source = From.Result.TargetedTransforms[Index];
				const int32 OutIndex = Out.FindOrAddTransform(Source.TargetWidget, Source.Part);
				Out.Masks[OutIndex] |= From.Masks.IsValidIndex(Index) ? From.Masks[Index] : 0;
			}
			for (int32 Index = 0; Index < To.Result.TargetedTransforms.Num(); ++Index)
			{
				const FWidgetAnimTargetedTransform& Source = To.Result.TargetedTransforms[Index];
				const int32 OutIndex = Out.FindOrAddTransform(Source.TargetWidget, Source.Part);
				Out.Masks[OutIndex] |= To.Masks.IsValidIndex(Index) ? To.Masks[Index] : 0;
			}
			for (FWidgetAnimTargetedTransform& Blended : Out.Result.TargetedTransforms)
			{
				const int32 FromIndex = From.FindTransform(Blended.TargetWidget, Blended.Part);
				const int32 ToIndex = To.FindTransform(Blended.TargetWidget, Blended.Part);
				const FWidgetAnimTargetedTransform Rest;
				const FWidgetAnimTargetedTransform& A = FromIndex != INDEX_NONE ? From.Result.TargetedTransforms[FromIndex] : Rest;
				const FWidgetAnimTargetedTransform& B = ToIndex != INDEX_NONE ? To.Result.TargetedTransforms[ToIndex] : Rest;
				Blended.Opacity = FMath::Lerp(A.Opacity, B.Opacity, Alpha);
				Blended.Scale = FMath::Lerp(A.Scale, B.Scale, static_cast<double>(Alpha));
				Blended.Translation = FMath::Lerp(A.Translation, B.Translation, static_cast<double>(Alpha));
				Blended.Shear = FMath::Lerp(A.Shear, B.Shear, static_cast<double>(Alpha));
				Blended.Angle = FMath::Lerp(A.Angle, B.Angle, Alpha);
			}

			// Scalar parameters: from one value to the other; one only written by one side goes from or to its rest.
			for (const FWidgetAnimScalarTarget& Source : From.Result.ScalarParameters)
			{
				const FWidgetAnimScalarTarget* Other = To.Result.ScalarParameters.FindByPredicate([&Source](const FWidgetAnimScalarTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				float EndValue = Source.Value;
				if (Other)
				{
					EndValue = Other->Value;
				}
				else if (!FindRestScalar(Context, Source, EndValue) && Alpha >= 1.f)
				{
					// Nowhere to go back to and done: left where it was, as a track let go leaves it.
					continue;
				}
				FWidgetAnimScalarTarget& Blended = Out.Result.ScalarParameters.Add_GetRef(Source);
				Blended.Value = FMath::Lerp(Source.Value, EndValue, Alpha);
				Blended.bIsBaseline = Source.bIsBaseline && (!Other || Other->bIsBaseline);
			}
			for (const FWidgetAnimScalarTarget& Source : To.Result.ScalarParameters)
			{
				const bool bDone = Out.Result.ScalarParameters.ContainsByPredicate([&Source](const FWidgetAnimScalarTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				if (bDone)
				{
					continue;
				}
				float StartValue = Source.Value;
				FindRestScalar(Context, Source, StartValue);
				FWidgetAnimScalarTarget& Blended = Out.Result.ScalarParameters.Add_GetRef(Source);
				Blended.Value = FMath::Lerp(StartValue, Source.Value, Alpha);
			}

			// Vector parameters the same way.
			for (const FWidgetAnimVectorTarget& Source : From.Result.VectorParameters)
			{
				const FWidgetAnimVectorTarget* Other = To.Result.VectorParameters.FindByPredicate([&Source](const FWidgetAnimVectorTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				FLinearColor EndValue = Source.Value;
				if (Other)
				{
					EndValue = Other->Value;
				}
				else if (!FindRestVector(Context, Source, EndValue) && Alpha >= 1.f)
				{
					continue;
				}
				FWidgetAnimVectorTarget& Blended = Out.Result.VectorParameters.Add_GetRef(Source);
				Blended.Value = FMath::Lerp(Source.Value, EndValue, Alpha);
				Blended.bIsBaseline = Source.bIsBaseline && (!Other || Other->bIsBaseline);
			}
			for (const FWidgetAnimVectorTarget& Source : To.Result.VectorParameters)
			{
				const bool bDone = Out.Result.VectorParameters.ContainsByPredicate([&Source](const FWidgetAnimVectorTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				if (bDone)
				{
					continue;
				}
				FLinearColor StartValue = Source.Value;
				FindRestVector(Context, Source, StartValue);
				FWidgetAnimVectorTarget& Blended = Out.Result.VectorParameters.Add_GetRef(Source);
				Blended.Value = FMath::Lerp(StartValue, Source.Value, Alpha);
			}

			// Outlines can't be mixed: the next state's shows from halfway, or once the leaving one has nothing to show.
			for (const FWidgetAnimShapeTarget& Source : To.Result.ShapeTargets)
			{
				const FWidgetAnimShapeTarget* Leaving = From.Result.ShapeTargets.FindByPredicate([&Source](const FWidgetAnimShapeTarget& Candidate) { return Candidate.TargetBox == Source.TargetBox; });
				Out.Result.ShapeTargets.Add((Leaving && Alpha < 0.5f) ? *Leaving : Source);
			}
			for (const FWidgetAnimShapeTarget& Source : From.Result.ShapeTargets)
			{
				const bool bDone = Out.Result.ShapeTargets.ContainsByPredicate([&Source](const FWidgetAnimShapeTarget& Candidate) { return Candidate.TargetBox == Source.TargetBox; });
				if (!bDone && Alpha < 1.f)
				{
					Out.Result.ShapeTargets.Add(Source);
				}
			}

			// Registered types: a value of the same type blends - a count counts on from where it was - anything else changes
			// hands halfway. One only the leaving state wrote is kept until the blend is done.
			for (const FWidgetAnimAuthoritativeWrite& Source : To.Result.AuthoritativeWrites)
			{
				const FWidgetAnimAuthoritativeWrite* Leaving = From.Result.AuthoritativeWrites.FindByPredicate([&Source](const FWidgetAnimAuthoritativeWrite& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Channel == Source.Channel;
				});
				if (!Leaving)
				{
					Out.Result.AuthoritativeWrites.Add(Source);
					continue;
				}
				if (Leaving->Evaluator == Source.Evaluator && Source.Evaluator && Source.Evaluator->BlendsWriteValues() && !Leaving->bIsBaseline && !Source.bIsBaseline)
				{
					FWidgetAnimAuthoritativeWrite& Blended = Out.Result.AuthoritativeWrites.Add_GetRef(Source);
					Blended.Value = FMath::Lerp(Leaving->Value, Source.Value, static_cast<double>(Alpha));
					if (Leaving->bHasColor && Source.bHasColor)
					{
						Blended.Color = FMath::Lerp(Leaving->Color, Source.Color, Alpha);
					}
					continue;
				}
				Out.Result.AuthoritativeWrites.Add(Alpha < 0.5f ? *Leaving : Source);
			}
			for (const FWidgetAnimAuthoritativeWrite& Source : From.Result.AuthoritativeWrites)
			{
				const bool bDone = Out.Result.AuthoritativeWrites.ContainsByPredicate([&Source](const FWidgetAnimAuthoritativeWrite& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Channel == Source.Channel;
				});
				if (!bDone && Alpha < 1.f)
				{
					Out.Result.AuthoritativeWrites.Add(Source);
				}
			}
		}

		// -------------------------------------------------------------------------------------------------
		// Inputs
		// -------------------------------------------------------------------------------------------------

		/** Reads the built-in inputs as layer LayerIndex does - off its Widget Input Targets - while it lives. */
		struct FReadingLayerScope
		{
			FReadingLayerScope(FWidgetAnimMachineInstance& InInstance, int32 LayerIndex)
				: Owner(InInstance)
				, Was(InInstance.ReadingLayer)
			{
				Owner.ReadingLayer = LayerIndex;
			}

			~FReadingLayerScope()
			{
				Owner.ReadingLayer = Was;
			}

			FWidgetAnimMachineInstance& Owner;
			int32 Was;
		};

		/** Whether a built-in input is true - as the layer being worked out reads it, off its Widget Input Targets. */
		bool GetBuiltInValue(const FWidgetAnimMachineInstance& Instance, FName Name)
		{
			if (Name == WidgetAnimMachineInputs::Closing)
			{
				return Instance.bClosing;
			}
			const FWidgetAnimMachineWidgetState& WidgetState = Instance.GetReadWidgetState();
			if (Name == WidgetAnimMachineInputs::Hovered)
			{
				return WidgetState.bHovered;
			}
			if (Name == WidgetAnimMachineInputs::Pressed)
			{
				return WidgetState.bPressed;
			}
			if (Name == WidgetAnimMachineInputs::Focused)
			{
				return WidgetState.bFocused;
			}
			if (Name == WidgetAnimMachineInputs::Selected)
			{
				return WidgetState.bSelected;
			}
			if (Name == WidgetAnimMachineInputs::Disabled)
			{
				return WidgetState.bDisabled;
			}
			if (Name == WidgetAnimMachineInputs::Clicked)
			{
				return WidgetState.bClicked;
			}
			// The Events and the Gamepad buttons: Fired this frame.
			return (WidgetState.Fired & WidgetAnimMachineInputs::GetBuiltInBit(Name)) != 0;
		}

		/** The number a condition reads off a Vector 2D. */
		float GetVectorPart(const FVector2f& Vector, EWidgetAnimMachineVectorPart Part)
		{
			switch (Part)
			{
			case EWidgetAnimMachineVectorPart::X: return Vector.X;
			case EWidgetAnimMachineVectorPart::Y: return Vector.Y;
			default:                               return Vector.Size();
			}
		}

		/**
		 * An input as a number - a Bool or a Trigger as 0 or 1, a Choice as its option's number, a Vector 2D as Part of it (its
		 * Length by default) - and whether it is "true". bPrevious: as the machine last ran, for Increased, Decreased and Changed.
		 * False when there is no input of that name.
		 */
		bool ReadAsNumber(const FWidgetAnimMachineInstance& Instance, FName Name, float& OutNumber, bool& OutTruthy,
			EWidgetAnimMachineVectorPart Part = EWidgetAnimMachineVectorPart::Length, bool bPrevious = false)
		{
			// A machine's own input of the same name (one made before that built-in was added) wins over it.
			if (WidgetAnimMachineInputs::IsBuiltIn(Name) && !Instance.FindInput(Name))
			{
				OutTruthy = GetBuiltInValue(Instance, Name);
				OutNumber = OutTruthy ? 1.f : 0.f;
				return true;
			}

			const FWidgetAnimMachineInputState* Input = Instance.FindInput(Name);
			if (!Input)
			{
				return false;
			}
			switch (Input->Type)
			{
			case EWidgetAnimMachineInputType::Bool:
				OutTruthy = Input->bValue;
				OutNumber = Input->bValue ? 1.f : 0.f;
				break;
			case EWidgetAnimMachineInputType::Trigger:
				OutTruthy = Input->bFired;
				OutNumber = Input->bFired ? 1.f : 0.f;
				break;
			case EWidgetAnimMachineInputType::Hold:
				// Truthy while being held toward its Hold Time; as a number, how far through it.
				OutTruthy = Input->bValue;
				OutNumber = bPrevious ? Input->PreviousNumber : Input->Number;
				break;
			case EWidgetAnimMachineInputType::Vector2D:
				OutNumber = GetVectorPart(bPrevious ? Input->PreviousVector : Input->Vector, Part);
				OutTruthy = !(bPrevious ? Input->PreviousVector : Input->Vector).IsNearlyZero();
				break;
			default:
				// Float, Integer, Choice.
				OutNumber = bPrevious ? Input->PreviousNumber : Input->Number;
				OutTruthy = !FMath::IsNearlyZero(OutNumber);
				break;
			}
			return true;
		}

		/** A Choice's option now, by name: None when it has none, or Input isn't a Choice. */
		FName GetChoiceOption(const FWidgetAnimMachineInputState* Input)
		{
			if (!Input || Input->Type != EWidgetAnimMachineInputType::Choice)
			{
				return NAME_None;
			}
			const int32 Index = FMath::RoundToInt32(Input->Number);
			return Input->Options.IsValidIndex(Index) ? Input->Options[Index] : FName();
		}

		/** Whether Vector points Direction - screen space, Up being negative Y - and is at least DeadZone long. */
		bool PointsTo(const FVector2f& Vector, EWidgetAnimMachineDirection Direction, float DeadZone)
		{
			if (Vector.Size() < FMath::Max(DeadZone, KINDA_SMALL_NUMBER))
			{
				return false;
			}
			// Four quarters about the axes: a diagonal exactly between two goes to the side.
			const float Across = FMath::Abs(Vector.X);
			const float Down = FMath::Abs(Vector.Y);
			switch (Direction)
			{
			case EWidgetAnimMachineDirection::Up:    return Vector.Y < 0.f && Down > Across;
			case EWidgetAnimMachineDirection::Down:  return Vector.Y > 0.f && Down > Across;
			case EWidgetAnimMachineDirection::Left:  return Vector.X < 0.f && Across >= Down;
			default:                                  return Vector.X > 0.f && Across >= Down;
			}
		}

		/**
		 * A condition on another layer's state: against every layer as it was before any moved this frame (LayerStatesNow), and
		 * the frame before (LayerStatesBefore) for Entered and Exited. A layer or state the machine doesn't have never holds.
		 */
		bool EvaluateLayerStateCondition(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineCondition& Condition)
		{
			const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
			const int32 LayerIndex = Machine ? Machine->FindLayerIndex(Condition.Layer) : INDEX_NONE;
			if (!Machine || !Machine->Layers.IsValidIndex(LayerIndex))
			{
				return false;
			}
			const int32 StateIndex = Machine->Layers[LayerIndex].FindStateIndexByName(Condition.State);
			if (StateIndex == INDEX_NONE)
			{
				return false;
			}
			const int32 Now = Instance.LayerStatesNow.IsValidIndex(LayerIndex) ? Instance.LayerStatesNow[LayerIndex] : INDEX_NONE;
			const int32 Before = Instance.LayerStatesBefore.IsValidIndex(LayerIndex) ? Instance.LayerStatesBefore[LayerIndex] : INDEX_NONE;
			// In a sub-machine is in it, whichever of its states the layer is in.
			const FWidgetAnimMachineLayer& Other = Machine->Layers[LayerIndex];
			const bool bInNow = Other.IsWithin(Now, StateIndex);
			const bool bInBefore = Other.IsWithin(Before, StateIndex);
			switch (Condition.LayerTest)
			{
			case EWidgetAnimMachineLayerStateTest::IsNotIn: return !bInNow;
			case EWidgetAnimMachineLayerStateTest::Entered: return bInNow && !bInBefore;
			case EWidgetAnimMachineLayerStateTest::Exited:  return !bInNow && bInBefore;
			default:                                         return bInNow;
			}
		}

		bool EvaluateCondition(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineCondition& Condition)
		{
			if (Condition.TestsLayerState())
			{
				return EvaluateLayerStateCondition(Instance, Condition);
			}

			const FWidgetAnimMachineInputState* Input = Instance.FindInput(Condition.Input);
			const bool bAgainstInput = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input;

			if (Input)
			{
				switch (Input->Type)
				{
				case EWidgetAnimMachineInputType::Hold:
					// Fired the frame it completes; Is Let Go the frame it is let go - a pulse, as a Trigger is, so it is spent
					// once something takes it rather than holding for as long as nothing is pressed. Never the frame it
					// completes (it is still held then), so the way back from the state it held is never taken as the hold
					// completes, whatever order the two transitions are in.
					if (Condition.Compare == EWidgetAnimMachineCompare::Fired)
					{
						return Input->bFired;
					}
					if (Condition.Compare == EWidgetAnimMachineCompare::IsFalse)
					{
						return Input->bLetGo;
					}
					break;

				case EWidgetAnimMachineInputType::Choice:
				{
					// By name: the option it has against the one typed - or another Choice's.
					if (Condition.Compare == EWidgetAnimMachineCompare::Changed)
					{
						return FMath::RoundToInt32(Input->Number) != FMath::RoundToInt32(Input->PreviousNumber);
					}
					const FName Mine = GetChoiceOption(Input);
					const FName Theirs = bAgainstInput ? GetChoiceOption(Instance.FindInput(Condition.OtherInput)) : Condition.Option;
					if (bAgainstInput && Theirs.IsNone())
					{
						return false;
					}
					if (Condition.Compare == EWidgetAnimMachineCompare::Equal)
					{
						return Mine == Theirs;
					}
					if (Condition.Compare == EWidgetAnimMachineCompare::NotEqual)
					{
						return Mine != Theirs;
					}
					return false;
				}

				case EWidgetAnimMachineInputType::Vector2D:
					if (Condition.Compare == EWidgetAnimMachineCompare::Points)
					{
						return PointsTo(Input->Vector, Condition.Direction, Condition.DeadZone);
					}
					if (Condition.Compare == EWidgetAnimMachineCompare::Changed)
					{
						return !Input->Vector.Equals(Input->PreviousVector, 1e-4f);
					}
					break;

				default:
					break;
				}

				// The frame it moved: against the value as the machine last ran.
				if (Condition.Compare == EWidgetAnimMachineCompare::Increased || Condition.Compare == EWidgetAnimMachineCompare::Decreased
					|| Condition.Compare == EWidgetAnimMachineCompare::Changed)
				{
					float Now = 0.f;
					float Was = 0.f;
					bool bUnused = false;
					ReadAsNumber(Instance, Condition.Input, Now, bUnused, Condition.Part);
					ReadAsNumber(Instance, Condition.Input, Was, bUnused, Condition.Part, /*bPrevious=*/true);
					switch (Condition.Compare)
					{
					case EWidgetAnimMachineCompare::Increased: return Now > Was + 1e-5f;
					case EWidgetAnimMachineCompare::Decreased: return Now < Was - 1e-5f;
					default:                                    return !FMath::IsNearlyEqual(Now, Was, 1e-5f);
					}
				}
			}

			float Number = 0.f;
			bool bTruthy = false;
			if (!ReadAsNumber(Instance, Condition.Input, Number, bTruthy, Condition.Part))
			{
				// An input the machine doesn't have - removed since the condition was made - never holds.
				return false;
			}

			// What it is compared against: the value typed, or another input as it is now.
			float Against = Condition.Value;
			if (bAgainstInput)
			{
				bool bOtherTruthy = false;
				if (!ReadAsNumber(Instance, Condition.OtherInput, Against, bOtherTruthy, Condition.OtherPart))
				{
					return false;
				}
			}

			switch (Condition.Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:         return bTruthy;
			case EWidgetAnimMachineCompare::IsFalse:        return !bTruthy;
			case EWidgetAnimMachineCompare::Fired:          return bTruthy;
			case EWidgetAnimMachineCompare::Equal:          return FMath::IsNearlyEqual(Number, Against, 1e-4f);
			case EWidgetAnimMachineCompare::NotEqual:       return !FMath::IsNearlyEqual(Number, Against, 1e-4f);
			case EWidgetAnimMachineCompare::Less:           return Number < Against;
			case EWidgetAnimMachineCompare::LessOrEqual:    return Number <= Against;
			case EWidgetAnimMachineCompare::Greater:        return Number > Against;
			case EWidgetAnimMachineCompare::GreaterOrEqual: return Number >= Against;
			default:                                         return false;
			}
		}

		bool ConditionsHold(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineTransition& Transition)
		{
			for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
			{
				if (!EvaluateCondition(Instance, Condition))
				{
					return false;
				}
			}
			return true;
		}

		/** Whether Widget, or any widget around it, is disabled. */
		bool IsDisabledInTree(const UWidget* Widget)
		{
			static constexpr int32 MaxDepth = 64;
			const UWidget* Search = Widget;
			for (int32 Depth = 0; Search && Depth < MaxDepth; ++Depth)
			{
				if (!Search->GetIsEnabled())
				{
					return true;
				}
				const UWidget* Parent = Search->GetParent();
				Search = Parent ? Parent : Search->GetTypedOuter<UUserWidget>();
			}
			return false;
		}

		/** The built-in inputs of a plain widget: Slate's hover and focus, a Button's press, a Check Box's check. */
		void ReadPlainWidgetState(FWidgetAnimMachineWidgetReader& Reader, uint64 Used, FWidgetAnimInputContext& InputContext, FWidgetAnimMachineWidgetState& OutState)
		{
			FVector4 Value;
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Hovered))
			{
				OutState.bHovered = Reader.HoverReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Pressed))
			{
				OutState.bPressed = Reader.PressReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Focused))
			{
				OutState.bFocused = Reader.FocusReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (const UCheckBox* CheckBox = Cast<UCheckBox>(InputContext.MeasuredWidget))
			{
				OutState.bSelected = CheckBox->IsChecked();
			}
			else if (UContextMenuButton* Button = Cast<UContextMenuButton>(InputContext.MeasuredWidget))
			{
				// A Context Menu Button that can be enabled: Selected while it is - toggled on, or the one picked in its group.
				OutState.bSelected = Button->GetEnabledState();

				// Clicked: its action went off since last read - on press or on release, as it is set, and every repeat of a
				// Hold button. Its own count, so a press dragged off, or one during a cooldown, isn't a click.
				const uint32 Count = Button->GetActionCount();
				OutState.bClicked = Reader.bHasClickCount && Count != Reader.LastClickCount;
				Reader.LastClickCount = Count;
				Reader.bHasClickCount = true;
			}
			OutState.bDisabled = IsDisabledInTree(InputContext.MeasuredWidget);
		}

		/** The built-in pulses read off counted input events rather than the widget's state: a mask of them. */
		uint64 GetCountedEventPulses()
		{
			static const uint64 Mask = []()
			{
				uint64 Pulses = 0;
				for (const FName Name : WidgetAnimMachineInputs::GetBuiltIns())
				{
					if (Name == WidgetAnimMachineInputs::DoubleClicked || Name == WidgetAnimMachineInputs::RightClicked || Name == WidgetAnimMachineInputs::Accept
						|| Name == WidgetAnimMachineInputs::Back || WidgetAnimMachineInputs::GetBuiltInKey(Name).IsValid())
					{
						Pulses |= WidgetAnimMachineInputs::GetBuiltInBit(Name);
					}
				}
				return Pulses;
			}();
			return Mask;
		}

		/**
		 * Double Clicked, Right Clicked, Accept, Back and the Gamepad buttons, from the input events counted as Slate saw them
		 * (WidgetAnimInput::StartCountingEvents): a count changed since the machine last looked is a new event. The mouse ones
		 * where they happened over Widget; the buttons while Widget, or something inside it, has focus (InOutState.bFocused - read
		 * whenever any of them is). Into InOutState.Fired.
		 */
		void ReadCountedEvents(FWidgetAnimMachineWidgetReader& Reader, uint64 UsedBuiltIns, UWidget* Widget, FWidgetAnimMachineWidgetState& InOutState)
		{
			const uint64 Used = UsedBuiltIns & GetCountedEventPulses();
			if (Used == 0)
			{
				return;
			}
			WidgetAnimInput::StartCountingEvents();

			// A count other than the one last seen: an event since. The first look only notes the count - what happened before the
			// machine started never fires.
			auto IsNew = [&Reader](FName Name, uint32 Count)
			{
				uint32* Last = Reader.LastEventCounts.Find(Name);
				if (!Last)
				{
					Reader.LastEventCounts.Add(Name, Count);
					return false;
				}
				const bool bNew = *Last != Count;
				*Last = Count;
				return bNew;
			};
			auto Reads = [Used](FName Name) { return (Used & WidgetAnimMachineInputs::GetBuiltInBit(Name)) != 0; };
			auto Fire = [&InOutState](FName Name) { InOutState.Fired |= WidgetAnimMachineInputs::GetBuiltInBit(Name); };

			if (Reads(WidgetAnimMachineInputs::DoubleClicked))
			{
				const WidgetAnimInput::FPointerEventCount DoubleClick = WidgetAnimInput::GetDoubleClickCount();
				if (IsNew(WidgetAnimMachineInputs::DoubleClicked, DoubleClick.Count) && WidgetAnimInput::IsPointUnder(Widget, DoubleClick.Position))
				{
					Fire(WidgetAnimMachineInputs::DoubleClicked);
				}
			}

			if (Reads(WidgetAnimMachineInputs::RightClicked))
			{
				// Pressed over the widget, then let go over it - as a click is - so a right press dragged off is no click.
				static const FName RightPressCountName(TEXT("Right Pressed (counted)"));
				const WidgetAnimInput::FPointerEventCount RightPress = WidgetAnimInput::GetRightPressCount();
				const WidgetAnimInput::FPointerEventCount RightRelease = WidgetAnimInput::GetRightReleaseCount();
				if (IsNew(RightPressCountName, RightPress.Count))
				{
					Reader.bRightPressOver = WidgetAnimInput::IsPointUnder(Widget, RightPress.Position);
				}
				if (IsNew(WidgetAnimMachineInputs::RightClicked, RightRelease.Count))
				{
					if (Reader.bRightPressOver && WidgetAnimInput::IsPointUnder(Widget, RightRelease.Position))
					{
						Fire(WidgetAnimMachineInputs::RightClicked);
					}
					Reader.bRightPressOver = false;
				}
			}

			// The buttons: noted whatever the focus, so a press made elsewhere never fires once focus arrives.
			for (const FName Name : WidgetAnimMachineInputs::GetBuiltIns())
			{
				if (!Reads(Name) || Name == WidgetAnimMachineInputs::DoubleClicked || Name == WidgetAnimMachineInputs::RightClicked)
				{
					continue;
				}
				const uint32 Count = Name == WidgetAnimMachineInputs::Accept ? WidgetAnimInput::GetNavigationPressCount(/*bAccept=*/true)
					: Name == WidgetAnimMachineInputs::Back ? WidgetAnimInput::GetNavigationPressCount(/*bAccept=*/false)
					: WidgetAnimInput::GetKeyPressCount(WidgetAnimMachineInputs::GetBuiltInKey(Name));
				if (IsNew(Name, Count) && InOutState.bFocused)
				{
					Fire(Name);
				}
			}
		}

		/**
		 * Reads the built-in inputs off Widget into Reader.State - the ones Used names, the Common UI module answering for its
		 * buttons. True when anything read differently from last time.
		 */
		bool ReadWidgetBuiltIns(FWidgetAnimMachineWidgetReader& Reader, uint64 Used, UWidget* Widget, FWidgetAnimInputContext& InputContext)
		{
			FWidgetAnimMachineWidgetState State;
			const bool bProviderAnswered = WidgetAnimMachineWidgetStates::ReadFromProviders(Widget, State);
			if (!bProviderAnswered)
			{
				InputContext.MeasuredWidget = Widget;
				ReadPlainWidgetState(Reader, Used, InputContext, State);
			}

			// Selected by what it sits in - a Flow List's selected item, and everything inside it - where the widget has no
			// selection of its own: a provider's answer, or a Check Box's checked state, stands.
			if (!State.bSelected && !bProviderAnswered && !Cast<UCheckBox>(Widget))
			{
				State.bSelected = WidgetAnimMachineWidgetStates::IsSelectedByContainer(Widget);
			}
			const FWidgetAnimMachineWidgetState& Was = Reader.State;

			// Clicked, where nothing said: a press let go while the widget is still hovered or focused - as a Button counts a
			// click. A press dragged off and let go elsewhere isn't one.
			if (!State.bClicked && !Reader.bHasClickCount && Was.bPressed && !State.bPressed && (State.bHovered || State.bFocused))
			{
				State.bClicked = true;
			}

			// The Events: each Fired the frame its state turns - whoever read the state, a provider or the widget itself.
			auto FireIf = [&State, Used](FName Name, bool bHappened)
			{
				const uint64 Bit = WidgetAnimMachineInputs::GetBuiltInBit(Name);
				if (bHappened && (Used & Bit) != 0)
				{
					State.Fired |= Bit;
				}
			};
			FireIf(WidgetAnimMachineInputs::HoverEntered, State.bHovered && !Was.bHovered);
			FireIf(WidgetAnimMachineInputs::HoverExited, !State.bHovered && Was.bHovered);
			FireIf(WidgetAnimMachineInputs::PressStarted, State.bPressed && !Was.bPressed);
			FireIf(WidgetAnimMachineInputs::Released, !State.bPressed && Was.bPressed);
			FireIf(WidgetAnimMachineInputs::FocusGained, State.bFocused && !Was.bFocused);
			FireIf(WidgetAnimMachineInputs::FocusLost, !State.bFocused && Was.bFocused);

			// Double Clicked, Right Clicked, Accept, Back and the Gamepad buttons, from the input Slate saw.
			ReadCountedEvents(Reader, Used, Widget, State);

			if (State.bHovered != Was.bHovered || State.bPressed != Was.bPressed || State.bFocused != Was.bFocused || State.bSelected != Was.bSelected
				|| State.bDisabled != Was.bDisabled || State.bClicked != Was.bClicked || State.Fired != Was.Fired)
			{
				Reader.State = State;
				return true;
			}
			return false;
		}

		/**
		 * The widget a layer names - in its Widget Input Targets or its Animations Affect: the widget the machine plays on for
		 * (This Widget) or (Widget); one of the machine's targets, where this use of it puts that target; else a widget path, in
		 * the User Widget the machine plays in. Null when it isn't there.
		 */
		UWidget* ResolveLayerWidget(const FWidgetAnimMachineInstance& Instance, FName Name, const FWidgetAnimMachineContext& Context)
		{
			if (UWidgetAnimStateMachineAsset::NamesHostWidget(Name))
			{
				return Context.Host;
			}
			const FName* TargetPath = Instance.TargetPaths.Find(Name);
			if (!TargetPath)
			{
				TargetPath = Instance.TargetPaths.Find(FName(*FWidgetAnimLayerGroup::UpgradeLegacyName(Name.ToString())));
			}
			if (TargetPath)
			{
				return TargetPath->IsNone() ? Context.Host : (Context.ResolvePath ? Context.ResolvePath(*TargetPath) : nullptr);
			}
			return Context.ResolvePath ? Context.ResolvePath(Name) : nullptr;
		}

		/**
		 * The built-in inputs off the widgets the layers' Widget Input Targets name, and each such layer's, as its widgets read
		 * together: true when any of them is, Fired when any of them fired. A widget that isn't there reads nothing. bHostChanged:
		 * the widget the machine plays on read differently this frame - a layer may name it among others.
		 */
		void ReadLayerWidgets(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, FWidgetAnimInputContext& InputContext, bool bHostChanged)
		{
			bool bAnyRead = bHostChanged;
			for (FWidgetAnimMachineWidgetReader& Reader : Instance.TargetReaders)
			{
				UWidget* Widget = Reader.Widget.Get();
				if (!Widget)
				{
					Widget = ResolveLayerWidget(Instance, Reader.Name, Context);
					Reader.Widget = Widget;
				}
				if (Widget)
				{
					bAnyRead |= ReadWidgetBuiltIns(Reader, Instance.UsedBuiltIns, Widget, InputContext);
				}
				else if (Reader.State.bHovered || Reader.State.bPressed || Reader.State.bFocused || Reader.State.bSelected || Reader.State.bDisabled)
				{
					Reader.State = FWidgetAnimMachineWidgetState();
					bAnyRead = true;
				}
			}
			if (!bAnyRead && Instance.LayerWidgetStates.Num() == Instance.LayerReaders.Num())
			{
				return;
			}

			Instance.LayerWidgetStates.SetNum(Instance.LayerReaders.Num());
			for (int32 LayerIndex = 0; LayerIndex < Instance.LayerReaders.Num(); ++LayerIndex)
			{
				FWidgetAnimMachineWidgetState Combined;
				for (const int32 ReaderIndex : Instance.LayerReaders[LayerIndex])
				{
					const FWidgetAnimMachineWidgetState& Read = Instance.TargetReaders.IsValidIndex(ReaderIndex) ? Instance.TargetReaders[ReaderIndex].State : Instance.HostReader.State;
					Combined.bHovered |= Read.bHovered;
					Combined.bPressed |= Read.bPressed;
					Combined.bFocused |= Read.bFocused;
					Combined.bSelected |= Read.bSelected;
					Combined.bDisabled |= Read.bDisabled;
					Combined.bClicked |= Read.bClicked;
					Combined.Fired |= Read.Fired;
				}
				Instance.LayerWidgetStates[LayerIndex] = Combined;
			}
			Instance.bInputsChanged = true;
		}

		/** Reads every input that has somewhere to be read from, and the built-in ones. Runtime only. */
		void ReadInputs(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, float DeltaTime)
		{
			if (Context.bDesignTime || !Context.Host)
			{
				return;
			}

			FWidgetAnimInputContext InputContext;
			InputContext.HostWidget = Context.Host;
			InputContext.DeltaTime = DeltaTime;
			InputContext.bHasPointer = WidgetAnimInput::GetPointerPosition(InputContext.PointerPosition);

			for (FWidgetAnimMachineInputState& Input : Instance.Inputs)
			{
				FWidgetAnimInputSource* Source = Input.Source.GetMutablePtr<FWidgetAnimInputSource>();
				if (!Source)
				{
					continue;
				}

				UWidget* Measured = Input.MeasuredOnPath.IsNone() || !Context.ResolvePath ? nullptr : Context.ResolvePath(Input.MeasuredOnPath);
				InputContext.MeasuredWidget = Measured ? Measured : Context.Host;

				FVector4 Value;
				if (!Source->Sample(InputContext, Value))
				{
					continue;
				}
				const float Sample = static_cast<float>(Value.X);

				switch (Input.Type)
				{
				case EWidgetAnimMachineInputType::Vector2D:
				{
					// Both parts: a Pointer Position or a Drag gives X and Y.
					const FVector2f Now(static_cast<float>(Value.X), static_cast<float>(Value.Y));
					if (!Now.Equals(Input.Vector, 1e-5f))
					{
						Input.Vector = Now;
						Instance.bInputsChanged = true;
					}
					break;
				}
				case EWidgetAnimMachineInputType::Integer:
				case EWidgetAnimMachineInputType::Choice:
				{
					// A whole number - for a Choice, its option's, held to the options it has.
					float Whole = static_cast<float>(FMath::RoundToInt32(Sample));
					if (Input.Type == EWidgetAnimMachineInputType::Choice)
					{
						Whole = FMath::Clamp(Whole, 0.f, static_cast<float>(FMath::Max(Input.Options.Num() - 1, 0)));
					}
					if (Whole != Input.Number)
					{
						Input.Number = Whole;
						Instance.bInputsChanged = true;
					}
					break;
				}
				case EWidgetAnimMachineInputType::Bool:
				{
					const bool bNow = Sample > 0.5f;
					if (bNow != Input.bValue)
					{
						Input.bValue = bNow;
						Instance.bInputsChanged = true;
					}
					break;
				}
				case EWidgetAnimMachineInputType::Trigger:
					// Fires as the value rises past 0.5 - a press, not every frame of it held.
					if (Input.bHasSample && Input.LastSample <= 0.5f && Sample > 0.5f)
					{
						Input.bFired = true;
						Instance.bInputsChanged = true;
					}
					break;
				case EWidgetAnimMachineInputType::Hold:
					// Held while the value is over 0.5 - timed in UpdateHolds.
					Input.bHoldSourceDown = Sample > 0.5f;
					break;
				default:
					if (Sample != Input.Number)
					{
						Input.Number = Sample;
						Instance.bInputsChanged = true;
					}
					break;
				}
				Input.LastSample = Sample;
				Input.bHasSample = true;
			}

			// The built-in ones, only when the machine reads any - the Common UI module answers for its buttons. Off the widget the
			// machine plays on, and off each widget a layer's Widget Input Targets name, for that layer.
			if ((Instance.UsedBuiltIns & ~WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Closing)) != 0)
			{
				const bool bHostChanged = ReadWidgetBuiltIns(Instance.HostReader, Instance.UsedBuiltIns, Context.Host, InputContext);
				if (bHostChanged)
				{
					Instance.bInputsChanged = true;
				}
				if (Instance.TargetReaders.Num() > 0)
				{
					ReadLayerWidgets(Instance, Context, InputContext, bHostChanged);
				}
			}
		}

		/**
		 * Times every Press And Hold: held down - by its Driven By, else by the widget's own pressed state, or from Blueprint
		 * or the Inputs panel - it counts up to its Hold Time, and fires once it gets there; let go, it starts again. Runs at
		 * design time too, where the Inputs panel's Pressed (or the input's own Hold button) holds it, and not at all while the
		 * preview is paused (DeltaTime 0).
		 */
		void UpdateHolds(FWidgetAnimMachineInstance& Instance, float DeltaTime)
		{
			for (FWidgetAnimMachineInputState& Input : Instance.Inputs)
			{
				if (Input.Type != EWidgetAnimMachineInputType::Hold)
				{
					continue;
				}

				const bool bSourced = Input.Source.IsValid() && Input.bHasSample;
				const bool bDown = Input.bHoldSetDown || (bSourced ? Input.bHoldSourceDown : Instance.HostReader.State.bPressed);
				if (!bDown)
				{
					// Let go - complete or not: ready for the next press. A press that had started is let go this frame
					// (Is Let Go), once.
					if (Input.bValue || Input.bHoldDone || Input.HoldElapsed > 0.f || Input.Number != 0.f)
					{
						Input.bLetGo = Input.bValue || Input.bHoldDone || Input.HoldElapsed > 0.f;
						Input.bValue = false;
						Input.bHoldDone = false;
						Input.HoldElapsed = 0.f;
						Input.Number = 0.f;
						Instance.bInputsChanged = true;
					}
					continue;
				}
				if (Input.bHoldDone)
				{
					// Held all the way already: nothing more until it is let go.
					continue;
				}

				Input.HoldElapsed += FMath::Max(DeltaTime, 0.f);
				const float HoldTime = FMath::Max(Input.HoldTime, 0.05f);
				if (Input.HoldElapsed >= HoldTime)
				{
					Input.bHoldDone = true;
					Input.bFired = true;
					Input.bValue = false;
					Input.Number = 1.f;
					Instance.bInputsChanged = true;
					continue;
				}

				const float Progress = Input.HoldElapsed / HoldTime;
				if (!Input.bValue || Progress != Input.Number)
				{
					Input.bValue = true;
					Input.Number = Progress;
					Instance.bInputsChanged = true;
				}
			}
		}

		// -------------------------------------------------------------------------------------------------
		// Settings
		// -------------------------------------------------------------------------------------------------

		/** An animation's settings as a machine plays it: its groups on the targets' widgets, played as the state says. */
		void BuildAnimationSettings(UWidgetAnimPresetAsset* Animation, const TMap<FName, FName>& TargetPaths, const FWidgetAnimMachineContext& Context,
			EWidgetAnimMachineStatePlayback Playback, float Speed, bool bFollowsProgress, TArray<FWidgetAnimSettings>& OutSettings,
			TConstArrayView<FWidgetAnimParameterValue> ParameterValues = TConstArrayView<FWidgetAnimParameterValue>())
		{
			if (!Animation)
			{
				return;
			}

			FWidgetAnimEntry Use;
			Use.Animation = Animation;
			Use.Parameters.Append(ParameterValues.GetData(), ParameterValues.Num());
			Use.bLoop = Playback == EWidgetAnimMachineStatePlayback::Loop || Playback == EWidgetAnimMachineStatePlayback::PingPong;
			Use.bPingPong = Playback == EWidgetAnimMachineStatePlayback::PingPong;
			Use.LoopCount = 0;
			Use.PlaybackSpeed = FMath::Max(Speed, 0.01f);

			// A percentage animation follows its Progress Input, or else plays from 0% to 100% over a second.
			Use.ProgressAnimationDuration = bFollowsProgress ? 0.f : 1.f;

			Use.BuildSettings(OutSettings, [Animation, &TargetPaths, &Context](int32 GroupIndex) -> UWidget*
			{
				if (!Animation->LayerGroups.IsValidIndex(GroupIndex))
				{
					return nullptr;
				}
				const FName TargetName(*Animation->LayerGroups[GroupIndex].GetDisplayName(GroupIndex));
				const FName* Path = TargetPaths.Find(TargetName);
				return Path && !Path->IsNone() && Context.ResolvePath ? Context.ResolvePath(*Path) : nullptr;
			});
		}

		/** The id a state's track goes by in its layer's player: its name. What a progress is set by. */
		FName GetStateTrackId(const FWidgetAnimMachineState& State)
		{
			return State.Name.IsNone() ? FName(TEXT("State")) : State.Name;
		}

		FName GetTransitionTrackId(const FWidgetAnimMachineLayer& Layer, const FWidgetAnimMachineTransition& Transition)
		{
			const FWidgetAnimMachineState* From = Layer.FindState(Transition.From);
			const FWidgetAnimMachineState* To = Layer.FindState(Transition.To);
			return FName(*FString::Printf(TEXT("%s > %s"), From ? *From->Name.ToString() : TEXT("?"), To ? *To->Name.ToString() : TEXT("?")));
		}

		// -------------------------------------------------------------------------------------------------
		// Layers
		// -------------------------------------------------------------------------------------------------

		/**
		 * Drops every track of the layer. The pose it showed is copied out of them first: its registered writes point at the
		 * tracks' settings, and it is still what this frame shows until the layer is worked out again.
		 */
		void StopTracks(FWidgetAnimMachineLayerInstance& Layer)
		{
			Layer.Output.DetachParams();
			UWidgetAnimLibrary::StopWidgetAnimTracks(Layer.Player, TArray<FName>(), /*bRemoveTracks=*/true);
			for (FWidgetAnimPlayer& BlendPlayer : Layer.BlendPlayers)
			{
				UWidgetAnimLibrary::StopWidgetAnimTracks(BlendPlayer, TArray<FName>(), /*bRemoveTracks=*/true);
			}
			Layer.BlendPlayers.Reset();
			Layer.BlendWeights.Reset();
		}

		/** Every player of the layer: its own, and a blend state's samples'. */
		template <typename TVisit>
		void ForEachPlayer(FWidgetAnimMachineLayerInstance& Layer, TVisit&& Visit)
		{
			Visit(Layer.Player);
			for (FWidgetAnimPlayer& BlendPlayer : Layer.BlendPlayers)
			{
				Visit(BlendPlayer);
			}
		}

		template <typename TVisit>
		void ForEachPlayer(const FWidgetAnimMachineLayerInstance& Layer, TVisit&& Visit)
		{
			Visit(Layer.Player);
			for (const FWidgetAnimPlayer& BlendPlayer : Layer.BlendPlayers)
			{
				Visit(BlendPlayer);
			}
		}

		/** Where a blend state's input is now: a number in X for Blend 1D, the point for Blend 2D. */
		FVector2f ReadBlendPoint(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineState& State)
		{
			const FWidgetAnimMachineInputState* Input = Instance.FindInput(State.BlendInput);
			if (!Input)
			{
				return FVector2f::ZeroVector;
			}
			if (Input->Type == EWidgetAnimMachineInputType::Vector2D)
			{
				return State.BlendSpace == EWidgetAnimMachineBlendSpace::TwoD ? Input->Vector : FVector2f(Input->Vector.Size(), 0.f);
			}
			float Number = 0.f;
			bool bTruthy = false;
			ReadAsNumber(Instance, State.BlendInput, Number, bTruthy);
			return FVector2f(Number, 0.f);
		}

		/** A blend state's weights from where its input is now, into the layer. */
		void UpdateBlendWeights(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineState& State, FWidgetAnimMachineLayerInstance& Layer)
		{
			Layer.BlendPoint = ReadBlendPoint(Instance, State);
			ComputeBlendWeights(State, Layer.BlendPoint, Layer.BlendWeights);
		}

		/** An action in words, for the editor's debugging. */
		FString DescribeAction(const FWidgetAnimMachineAction& Action)
		{
			switch (Action.Kind)
			{
			case EWidgetAnimMachineActionKind::FireTrigger: return FString::Printf(TEXT("Fired %s"), *Action.Input.ToString());
			case EWidgetAnimMachineActionKind::FireEvent:   return FString::Printf(TEXT("Event %s"), *Action.EventName.ToString());
			default:                                         return FString::Printf(TEXT("Set %s"), *Action.Input.ToString());
			}
		}

		/** Queues actions to run once the frame's transitions are settled. */
		void QueueActions(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, FName State, const TArray<FWidgetAnimMachineAction>& Actions)
		{
			for (const FWidgetAnimMachineAction& Action : Actions)
			{
				FWidgetAnimMachinePendingAction& Pending = Instance.PendingActions.AddDefaulted_GetRef();
				Pending.Action = Action;
				Pending.LayerIndex = LayerIndex;
				Pending.State = State;
			}
		}

		/** Runs the queued actions: inputs set and triggers fired for the next frame, events sent out. */
		void RunPendingActions(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context)
		{
			if (Instance.PendingActions.Num() == 0)
			{
				return;
			}
			const TArray<FWidgetAnimMachinePendingAction> Actions = MoveTemp(Instance.PendingActions);
			Instance.PendingActions.Reset();

			for (const FWidgetAnimMachinePendingAction& Pending : Actions)
			{
				const FWidgetAnimMachineAction& Action = Pending.Action;
				const FWidgetAnimMachineInputState* Input = Instance.FindInput(Action.Input);
				switch (Action.Kind)
				{
				case EWidgetAnimMachineActionKind::SetInput:
					if (Input)
					{
						switch (Input->Type)
						{
						case EWidgetAnimMachineInputType::Bool:
						case EWidgetAnimMachineInputType::Hold:
							SetBool(Instance, Action.Input, Action.bBoolValue);
							break;
						case EWidgetAnimMachineInputType::Trigger:
							FireTrigger(Instance, Action.Input);
							break;
						case EWidgetAnimMachineInputType::Choice:
							SetChoice(Instance, Action.Input, Action.Option);
							break;
						case EWidgetAnimMachineInputType::Vector2D:
							SetVector2D(Instance, Action.Input, Action.VectorValue);
							break;
						default:
							SetNumber(Instance, Action.Input, Action.NumberValue);
							break;
						}
					}
					break;

				case EWidgetAnimMachineActionKind::FireTrigger:
					if (Input && Input->Type == EWidgetAnimMachineInputType::Trigger)
					{
						FireTrigger(Instance, Action.Input);
					}
					else if (Input)
					{
						// A Press And Hold pressed or let go; anything else set as if a trigger had pulsed it on.
						SetFromNumber(Instance, Action.Input, Input->Type == EWidgetAnimMachineInputType::Hold ? (Action.bBoolValue ? 1.0 : 0.0) : 1.0);
					}
					break;

				case EWidgetAnimMachineActionKind::FireEvent:
					if (Context.OnStateEvent && !Context.bDesignTime && !Action.EventName.IsNone())
					{
						const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
						FWidgetAnimStateEventInfo Info;
						Info.Machine = Instance.Machine;
						Info.Layer = Machine && Machine->Layers.IsValidIndex(Pending.LayerIndex) ? Machine->Layers[Pending.LayerIndex].Name : FName();
						Info.State = Pending.State;
						Info.EventName = Action.EventName;
						Info.bEntered = true;
						Info.Host = Context.Host;
						Context.OnStateEvent(Info);
					}
					break;
				}

				// What it did, for the editor's debugging: the last few.
				if (Instance.Layers.IsValidIndex(Pending.LayerIndex))
				{
					TArray<FString>& Recent = Instance.Layers[Pending.LayerIndex].LastActions;
					Recent.Add(DescribeAction(Action));
					if (Recent.Num() > 4)
					{
						Recent.RemoveAt(0);
					}
				}
			}
		}

		/** The state's progress, from its Progress Input, set on its track when it changed. */
		void UpdateProgress(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, FWidgetAnimMachineLayerInstance& Layer, bool bSnap)
		{
			if (!Def.States.IsValidIndex(Layer.Current) || !Layer.bStateTrackStarted)
			{
				return;
			}
			const FWidgetAnimMachineState& State = Def.States[Layer.Current];
			if (State.ProgressInput.IsNone() || !State.Animation || !State.Animation->IsPercentTime())
			{
				return;
			}
			const FWidgetAnimMachineInputState* Input = Instance.FindInput(State.ProgressInput);
			if (!Input)
			{
				return;
			}

			// Only a change is sent: sending sets the track running again, and a settled layer should stay settled.
			const float Progress = FMath::Clamp(Input->Number, 0.f, 1.f);
			if (!bSnap && Layer.bHasProgress && Layer.LastProgress == Progress)
			{
				return;
			}
			Layer.LastProgress = Progress;
			Layer.bHasProgress = true;
			Layer.bSettled = false;
			UWidgetAnimLibrary::SetWidgetAnimTrackValue(Layer.Player, FWidgetAnimValueId::Progress(GetStateTrackId(State)), WidgetAnimValue::FromFloat(Progress), bSnap);
		}

		/** The values a state's Parameters From Inputs give its animation's parameters, from the inputs as they are now. */
		void GatherParameterInputs(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineState& State, TArray<FWidgetAnimParameterValue>& OutValues,
			const UWidgetAnimPresetAsset* Played)
		{
			if (!Played)
			{
				return;
			}
			for (const FWidgetAnimMachineParameterBinding& Binding : State.ParameterInputs)
			{
				const FWidgetAnimParameter* Declared = Played->FindParameter(Binding.Parameter);
				if (!Declared || Binding.Input.IsNone())
				{
					continue;
				}
				FWidgetAnimParameterValue Value = FWidgetAnimParameterValue::FromDefault(*Declared);
				const FWidgetAnimMachineInputState* Input = Instance.FindInput(Binding.Input);
				if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
				{
					// Both parts; a single number its length.
					Value.SetFromVector(Declared->Type == EWidgetAnimParameterType::Vector2D
						? FVector4(Input->Vector.X, Input->Vector.Y, 0.0, 0.0)
						: FVector4(Input->Vector.Size(), 0.0, 0.0, 0.0), &Declared->Options);
				}
				else if (Input && Input->Type == EWidgetAnimMachineInputType::Choice && Declared->Type == EWidgetAnimParameterType::Choice)
				{
					// By name, as the options read.
					const FName Option = GetChoiceOption(Input);
					if (Declared->Options.Contains(Option))
					{
						Value.Option = Option;
					}
				}
				else
				{
					float Number = 0.f;
					bool bTruthy = false;
					if (!ReadAsNumber(Instance, Binding.Input, Number, bTruthy))
					{
						continue;
					}
					if (Declared->Type == EWidgetAnimParameterType::Bool)
					{
						Value.bBool = bTruthy;
					}
					else
					{
						Value.SetFromVector(FVector4(Number, Number, Number, Number), &Declared->Options);
					}
				}
				Value.bOverride = true;
				OutValues.Add(Value);
			}
		}

		/** Starts the track of the state the layer is in, if it plays anything. */
		void StartStateTrack(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, FWidgetAnimMachineLayerInstance& Layer, const FWidgetAnimMachineContext& Context)
		{
			Layer.bStateTrackStarted = true;
			Layer.bPoseSettled = false;
			Layer.bHasProgress = false;
			Layer.TimeInState = 0.f;
			Layer.PassSeconds = 0.f;
			Layer.bStatePlayed = false;
			Layer.PlayedAnimation = nullptr;
			Layer.PlayedParameterInputs.Reset();

			if (!Def.States.IsValidIndex(Layer.Current))
			{
				return;
			}
			const FWidgetAnimMachineState& State = Def.States[Layer.Current];

			// A blend state: every sample on a player of its own, all at the state's speed and playback, mixed as it is posed.
			if (State.IsBlendState())
			{
				Layer.BlendPlayers.Reset();
				Layer.BlendPlayers.SetNum(State.BlendSamples.Num());
				Layer.PlayedParameterInputs.SetNum(State.BlendSamples.Num());
				for (int32 SampleIndex = 0; SampleIndex < State.BlendSamples.Num(); ++SampleIndex)
				{
					const FWidgetAnimMachineBlendSample& Sample = State.BlendSamples[SampleIndex];
					if (!Sample.Animation)
					{
						continue;
					}
					TArray<FWidgetAnimParameterValue> SampleParameters;
					GatherParameterInputs(Instance, State, SampleParameters, Sample.Animation);
					Layer.PlayedParameterInputs[SampleIndex] = SampleParameters;
					TArray<FWidgetAnimSettings> SampleSettings;
					BuildAnimationSettings(Sample.Animation, Instance.TargetPaths, Context, State.Playback, State.PlaybackSpeed, /*bFollowsProgress=*/false, SampleSettings, SampleParameters);
					if (SampleSettings.Num() > 0)
					{
						UWidgetAnimLibrary::StartWidgetAnimTrack(Layer.BlendPlayers[SampleIndex], GetStateTrackId(State), /*SourceIndex=*/0, SampleSettings, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);
					}
				}
				UpdateBlendWeights(Instance, State, Layer);
				const bool bAnyTrack = Layer.BlendPlayers.ContainsByPredicate([](const FWidgetAnimPlayer& BlendPlayer) { return BlendPlayer.Tracks.Num() > 0; });
				if (!bAnyTrack)
				{
					Layer.bStatePlayed = true;
				}
				return;
			}

			// Its animation - or, with Random Variations, one of them picked now by its chance.
			UWidgetAnimPresetAsset* Played = State.RandomVariations.Num() > 0 ? State.PickRandomAnimation(FMath::FRand()) : State.Animation.Get();
			if (State.Kind != EWidgetAnimMachineNodeKind::State || !Played)
			{
				Layer.bStatePlayed = true;
				return;
			}

			const bool bFollowsProgress = !State.ProgressInput.IsNone() && Played->IsPercentTime();
			TArray<FWidgetAnimParameterValue> ParameterValues;
			GatherParameterInputs(Instance, State, ParameterValues, Played);
			Layer.PlayedAnimation = Played;
			Layer.PlayedParameterInputs.Add(ParameterValues);
			TArray<FWidgetAnimSettings> Settings;
			BuildAnimationSettings(Played, Instance.TargetPaths, Context, State.Playback, State.PlaybackSpeed, bFollowsProgress, Settings, ParameterValues);
			if (Settings.Num() == 0)
			{
				Layer.bStatePlayed = true;
				return;
			}

			UWidgetAnimLibrary::StartWidgetAnimTrack(Layer.Player, GetStateTrackId(State), /*SourceIndex=*/0, Settings, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

			// Shown where its progress already is, rather than sweeping up to it from 0%.
			UpdateProgress(Instance, Def, Layer, /*bSnap=*/true);
		}

		/** Whether the node at Index is the layer's own Exit - a sub-machine's Exit only finishes that sub-machine. */
		bool IsLayerExit(const FWidgetAnimMachineLayer& Def, int32 Index)
		{
			return Def.States.IsValidIndex(Index) && Def.States[Index].Kind == EWidgetAnimMachineNodeKind::Exit && !Def.States[Index].ParentMachine.IsValid();
		}

		/** The nodes a layer at Index is in, outermost first: the sub-machines round it, then the node itself. */
		void GetActiveChain(const FWidgetAnimMachineLayer& Def, int32 Index, TArray<int32, TInlineAllocator<4>>& OutChain)
		{
			OutChain.Reset();
			for (int32 Step = 0; Def.States.IsValidIndex(Index) && Step <= Def.States.Num(); ++Step)
			{
				OutChain.Insert(Index, 0);
				Index = Def.FindParentIndex(Index);
			}
		}

		/**
		 * Where a layer going to the node at ToIndex ends up: there - or, for a sub-machine, in through its Entry, along the first
		 * of the Entry's transitions whose conditions hold, as deep as that leads. Stops at a sub-machine's Entry when none holds:
		 * the layer waits there, inside it. OutEntered: every node entered on the way, outermost first, the last one where it is.
		 */
		void ResolveEntered(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, int32 ToIndex, TArray<int32, TInlineAllocator<4>>& OutEntered,
			TArray<int32, TInlineAllocator<4>>& OutEntryTransitions);

		/** Finishes the running transition: the layer is in its state, nothing left blending. */
		void FinishTransition(const FWidgetAnimMachineLayer& Def, FWidgetAnimMachineLayerInstance& Layer)
		{
			Layer.Phase = EWidgetAnimMachineBlendPhase::None;
			Layer.Transition = INDEX_NONE;
			Layer.Previous = INDEX_NONE;
			Layer.BlendElapsed = 0.f;
			Layer.FromPose.Reset();
			Layer.Springs.Reset();
			Layer.bExited = IsLayerExit(Def, Layer.Current);
		}

		void ResolveEntered(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, int32 ToIndex, TArray<int32, TInlineAllocator<4>>& OutEntered,
			TArray<int32, TInlineAllocator<4>>& OutEntryTransitions)
		{
			OutEntered.Reset();
			OutEntryTransitions.Reset();
			int32 At = ToIndex;
			for (int32 Step = 0; Def.States.IsValidIndex(At) && Step <= Def.States.Num(); ++Step)
			{
				OutEntered.Add(At);
				if (!Def.States[At].IsSubMachine())
				{
					return;
				}
				const int32 EntryIndex = Def.FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry, Def.States[At].Id);
				if (EntryIndex == INDEX_NONE)
				{
					return;
				}
				OutEntered.Add(EntryIndex);
				const FGuid EntryId = Def.States[EntryIndex].Id;
				int32 Taken = INDEX_NONE;
				for (int32 Index = 0; Index < Def.Transitions.Num(); ++Index)
				{
					const FWidgetAnimMachineTransition& FromEntry = Def.Transitions[Index];
					if (FromEntry.bEnabled && FromEntry.From == EntryId && ConditionsHold(Instance, FromEntry))
					{
						Taken = Index;
						break;
					}
				}
				if (Taken == INDEX_NONE)
				{
					// Nothing to go to yet: inside, at its Entry, until a way on holds.
					return;
				}
				OutEntryTransitions.Add(Taken);
				// Past the Entry: it is gone through, not stayed in.
				OutEntered.Pop();
				At = Def.FindStateIndex(Def.Transitions[Taken].To);
			}
		}

		/** A state left or entered, as the State Entered / State Exited events tell it. */
		void SendStateEvent(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, int32 StateIndex, int32 OtherIndex, bool bEntered, const FWidgetAnimMachineContext& Context)
		{
			if (!Context.OnStateEvent || Context.bDesignTime || !Def.States.IsValidIndex(StateIndex) || Def.States[StateIndex].Kind != EWidgetAnimMachineNodeKind::State)
			{
				return;
			}
			const bool bOtherState = Def.States.IsValidIndex(OtherIndex) && Def.States[OtherIndex].Kind == EWidgetAnimMachineNodeKind::State;
			FWidgetAnimStateEventInfo Info;
			Info.Machine = Instance.Machine;
			Info.Layer = Def.Name;
			Info.Host = Context.Host;
			Info.State = Def.States[StateIndex].Name;
			Info.OtherState = bOtherState ? Def.States[OtherIndex].Name : FName();
			Info.bEntered = bEntered;
			Context.OnStateEvent(Info);
		}

		/** Starts Transition: from the pose the layer shows now, to where it points. */
		void StartTransition(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, int32 TransitionIndex, const FWidgetAnimMachineContext& Context)
		{
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			const FWidgetAnimMachineTransition& Transition = Def.Transitions[TransitionIndex];
			const int32 TargetIndex = Def.FindStateIndex(Transition.To);
			if (TargetIndex == INDEX_NONE)
			{
				return;
			}

			// What is left: the state the layer is in, and each sub-machine round it up to the graph the transition is in - a
			// transition of a sub-machine's own leaves all of it. Innermost first.
			const int32 FromIndex = Def.FindStateIndex(Transition.From);
			const FGuid TransitionScope = Def.States.IsValidIndex(FromIndex) ? Def.States[FromIndex].ParentMachine : FGuid();
			TArray<int32, TInlineAllocator<4>> LeftChain;
			for (int32 At = Layer.Current, Step = 0; Def.States.IsValidIndex(At) && Step <= Def.States.Num(); At = Def.FindParentIndex(At), ++Step)
			{
				LeftChain.Add(At);
				if (Def.States[At].ParentMachine == TransitionScope)
				{
					break;
				}
			}

			// Where it ends up - in through a sub-machine's Entry, as deep as that leads.
			TArray<int32, TInlineAllocator<4>> Entered;
			TArray<int32, TInlineAllocator<4>> EntryTransitions;
			ResolveEntered(Instance, Def, TargetIndex, Entered, EntryTransitions);
			const int32 ToIndex = Entered.Num() > 0 ? Entered.Last() : TargetIndex;
			const int32 Left = Layer.Current;

			// The events and actions of the way: each state left (innermost first) and entered (outermost first), the transitions
			// taken - all run once every layer has moved.
			auto QueueTheWay = [&]()
			{
				for (const int32 LeftIndex : LeftChain)
				{
					SendStateEvent(Instance, Def, LeftIndex, ToIndex, /*bEntered=*/false, Context);
					if (Def.States[LeftIndex].Kind == EWidgetAnimMachineNodeKind::State)
					{
						QueueActions(Instance, LayerIndex, Def.States[LeftIndex].Name, Def.States[LeftIndex].OnExit);
					}
				}
				QueueActions(Instance, LayerIndex, NAME_None, Transition.OnTaken);
				for (const int32 EntryTransition : EntryTransitions)
				{
					QueueActions(Instance, LayerIndex, NAME_None, Def.Transitions[EntryTransition].OnTaken);
				}
				for (const int32 EnteredIndex : Entered)
				{
					SendStateEvent(Instance, Def, EnteredIndex, Left, /*bEntered=*/true, Context);
					if (Def.States[EnteredIndex].Kind == EWidgetAnimMachineNodeKind::State)
					{
						QueueActions(Instance, LayerIndex, Def.States[EnteredIndex].Name, Def.States[EnteredIndex].OnEnter);
					}
				}
			};

			// A sub-machine's Exit: the sub-machine is done, and holds where it got to - its last state's tracks play on as they
			// were, nothing blends - until a transition of its own takes the layer on.
			if (Def.States.IsValidIndex(ToIndex) && Def.States[ToIndex].Kind == EWidgetAnimMachineNodeKind::Exit && Def.States[ToIndex].ParentMachine.IsValid())
			{
				Layer.Previous = Layer.Current;
				Layer.Current = ToIndex;
				Layer.Transition = INDEX_NONE;
				Layer.Phase = EWidgetAnimMachineBlendPhase::None;
				Layer.BlendElapsed = 0.f;
				Layer.bSettled = false;
				Layer.bStateTrackStarted = true;
				Layer.bStatePlayed = true;
				FinishTransition(Def, Layer);
				QueueTheWay();
				return;
			}

			// The pose the widget is in - wherever a transition already running had got to - is where this one starts.
			// Copied out of the tracks it was worked out from, which go now.
			const bool bWasSpringing = Layer.Phase == EWidgetAnimMachineBlendPhase::Blending && Def.Transitions.IsValidIndex(Layer.Transition) && Def.Transitions[Layer.Transition].Blend == EWidgetAnimMachineBlend::Spring;
			StopTracks(Layer);
			Layer.FromPose = Layer.Output;

			Layer.Previous = Layer.Current;
			Layer.Current = ToIndex;
			Layer.Transition = TransitionIndex;
			Layer.BlendElapsed = 0.f;
			Layer.TransitionAnimationSeconds = 0.f;
			Layer.bExited = false;
			Layer.bSettled = false;
			Layer.bStateTrackStarted = false;
			Layer.TimeInState = 0.f;
			Layer.PassSeconds = 0.f;
			Layer.bStatePlayed = false;

			// A spring interrupted by a spring keeps its channels' speeds; anything else starts its springs from the pose.
			if (Transition.Blend != EWidgetAnimMachineBlend::Spring || !bWasSpringing)
			{
				Layer.Springs.Reset();
			}

			if (Transition.Blend == EWidgetAnimMachineBlend::Animation && Transition.TransitionAnimation)
			{
				TArray<FWidgetAnimSettings> Settings;
				// At its own length - or sped up or slowed down to fit the Animation Duration it is given.
				BuildAnimationSettings(Transition.TransitionAnimation, Instance.TargetPaths, Context, EWidgetAnimMachineStatePlayback::Once, Transition.GetTransitionAnimationSpeed(), /*bFollowsProgress=*/false, Settings);
				if (Settings.Num() > 0)
				{
					Layer.TransitionAnimationSeconds = UWidgetAnimLibrary::GetStackPassDuration(Settings);
					UWidgetAnimLibrary::StartWidgetAnimTrack(Layer.Player, GetTransitionTrackId(Def, Transition), /*SourceIndex=*/1, Settings, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);
					Layer.Phase = EWidgetAnimMachineBlendPhase::TransitionAnimation;
				}
			}

			if (Layer.Phase != EWidgetAnimMachineBlendPhase::TransitionAnimation || Layer.Player.Tracks.Num() == 0)
			{
				StartStateTrack(Instance, Def, Layer, Context);
				const bool bInstant = Transition.Blend == EWidgetAnimMachineBlend::Cut
					|| (Transition.Blend != EWidgetAnimMachineBlend::Spring && Transition.GetTweenSeconds() <= KINDA_SMALL_NUMBER);
				Layer.Phase = bInstant ? EWidgetAnimMachineBlendPhase::None : EWidgetAnimMachineBlendPhase::Blending;
			}

			if (Layer.Phase == EWidgetAnimMachineBlendPhase::None)
			{
				FinishTransition(Def, Layer);
			}

			QueueTheWay();
		}

		/**
		 * Whether the node at LevelIndex - the state the layer is in, or a sub-machine round it - has played enough for Transition
		 * to leave it. A sub-machine has played once the layer is at its Exit.
		 */
		bool ExitTimeAllows(const FWidgetAnimMachineLayer& Def, const FWidgetAnimMachineLayerInstance& Layer, const FWidgetAnimMachineTransition& Transition, int32 LevelIndex)
		{
			if (Transition.ExitTime == EWidgetAnimMachineExitTime::Immediately || !Def.States.IsValidIndex(Layer.Current) || !Def.States.IsValidIndex(LevelIndex))
			{
				return true;
			}
			if (LevelIndex != Layer.Current)
			{
				const FWidgetAnimMachineState& Leaf = Def.States[Layer.Current];
				return Leaf.Kind == EWidgetAnimMachineNodeKind::Exit && Leaf.ParentMachine == Def.States[LevelIndex].Id;
			}
			if (Def.States[Layer.Current].Kind != EWidgetAnimMachineNodeKind::State)
			{
				return true;
			}
			if (!Layer.bStateTrackStarted)
			{
				// Still on the way in, through a transition animation: nothing of the state has played.
				return false;
			}
			if (Transition.ExitTime == EWidgetAnimMachineExitTime::WhenFinished)
			{
				return Layer.bStatePlayed;
			}
			return Layer.bStatePlayed || (Layer.PassSeconds > KINDA_SMALL_NUMBER && Layer.TimeInState >= Transition.ExitAtProgress * Layer.PassSeconds);
		}

		/**
		 * The transition the layer takes now, or INDEX_NONE. Outermost first: for each node the layer is in - each sub-machine round
		 * its state, then the state - that graph's Any State's, then the node's own, each in priority order. So a sub-machine's
		 * own transitions can be taken from anywhere inside it, before anything in there.
		 */
		int32 FindTransitionToTake(const FWidgetAnimMachineInstance& Instance, int32 LayerIndex)
		{
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			const FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			if (!Def.States.IsValidIndex(Layer.Current))
			{
				return INDEX_NONE;
			}

			// A transition that can't be interrupted finishes first.
			if (Def.Transitions.IsValidIndex(Layer.Transition) && !Def.Transitions[Layer.Transition].bInterruptible)
			{
				return INDEX_NONE;
			}

			TArray<int32, TInlineAllocator<4>> Chain;
			GetActiveChain(Def, Layer.Current, Chain);
			for (const int32 LevelIndex : Chain)
			{
				const FWidgetAnimMachineState& Level = Def.States[LevelIndex];

				// A Random transition that could be taken: one of the Random ones from the same node that could be, by weight.
				auto PickRandom = [&Instance, &Def, &Layer, &Level, LevelIndex](int32 Found) -> int32
				{
					const FWidgetAnimMachineTransition& First = Def.Transitions[Found];
					if (!First.bRandom)
					{
						return Found;
					}
					TArray<int32, TInlineAllocator<8>> Pool;
					float Total = 0.f;
					for (int32 Index = 0; Index < Def.Transitions.Num(); ++Index)
					{
						const FWidgetAnimMachineTransition& Candidate = Def.Transitions[Index];
						if (Candidate.bEnabled && Candidate.bRandom && Candidate.From == First.From && Candidate.To != Level.Id
							&& Candidate.RandomWeight > 0.f && ConditionsHold(Instance, Candidate) && ExitTimeAllows(Def, Layer, Candidate, LevelIndex))
						{
							Pool.Add(Index);
							Total += Candidate.RandomWeight;
						}
					}
					if (Pool.Num() == 0 || Total <= 0.f)
					{
						return Found;
					}
					float Roll = FMath::FRandRange(0.f, Total);
					for (const int32 Index : Pool)
					{
						Roll -= Def.Transitions[Index].RandomWeight;
						if (Roll <= 0.f)
						{
							return Index;
						}
					}
					return Pool.Last();
				};

				// At an Exit the layer waits - at its own, to be brought back in through Entry; at a sub-machine's, for a transition
				// of the sub-machine's own, already looked at. Nothing leaves an Exit.
				if (Level.Kind == EWidgetAnimMachineNodeKind::Exit)
				{
					return INDEX_NONE;
				}

				// That graph's Any State's, unless the layer is at that graph's Entry, which picks the first state on its own. Never
				// back into the node the layer is in or heading to: a held condition would start it again every frame.
				if (Level.Kind != EWidgetAnimMachineNodeKind::Entry)
				{
					const int32 AnyIndex = Def.FindSpecialIndex(EWidgetAnimMachineNodeKind::AnyState, Level.ParentMachine);
					const FGuid AnyId = Def.States.IsValidIndex(AnyIndex) ? Def.States[AnyIndex].Id : FGuid();
					for (int32 Index = 0; AnyId.IsValid() && Index < Def.Transitions.Num(); ++Index)
					{
						const FWidgetAnimMachineTransition& Transition = Def.Transitions[Index];
						if (Transition.bEnabled && Transition.From == AnyId && Transition.To != Level.Id && ConditionsHold(Instance, Transition) && ExitTimeAllows(Def, Layer, Transition, LevelIndex))
						{
							return PickRandom(Index);
						}
					}
				}

				for (int32 Index = 0; Index < Def.Transitions.Num(); ++Index)
				{
					const FWidgetAnimMachineTransition& Transition = Def.Transitions[Index];
					if (Transition.bEnabled && Transition.From == Level.Id && ConditionsHold(Instance, Transition) && ExitTimeAllows(Def, Layer, Transition, LevelIndex))
					{
						return PickRandom(Index);
					}
				}
			}
			return INDEX_NONE;
		}

		/** Takes what transitions the inputs allow. Instant ones - cuts - chain on within the frame, a few deep. */
		void TakeTransitions(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, const FWidgetAnimMachineContext& Context)
		{
			static constexpr int32 MaxHopsPerFrame = 8;
			TArray<int32, TInlineAllocator<MaxHopsPerFrame + 1>> Visited;
			Visited.Add(Instance.Layers[LayerIndex].Current);
			for (int32 Hop = 0; Hop < MaxHopsPerFrame; ++Hop)
			{
				const int32 Chosen = FindTransitionToTake(Instance, LayerIndex);
				if (Chosen == INDEX_NONE)
				{
					return;
				}

				// Instant hops going round in a circle (two Cuts whose conditions both hold): stop where it would come back.
				const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
				if (Hop > 0 && Def.Transitions.IsValidIndex(Chosen))
				{
					const int32 ToIndex = Def.FindStateIndex(Def.Transitions[Chosen].To);
					if (Visited.Contains(ToIndex))
					{
						return;
					}
				}
				const int32 TargetIndex = Def.Transitions.IsValidIndex(Chosen) ? Def.FindStateIndex(Def.Transitions[Chosen].To) : INDEX_NONE;
				StartTransition(Instance, LayerIndex, Chosen, Context);
				if (Instance.Layers[LayerIndex].Phase != EWidgetAnimMachineBlendPhase::None)
				{
					return;
				}
				// Where it went, and - for a sub-machine - where inside it that led.
				Visited.Add(TargetIndex);
				Visited.Add(Instance.Layers[LayerIndex].Current);
			}
		}

		/** Puts a layer at its Entry and takes Entry's transitions. */
		void EnterLayer(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, const FWidgetAnimMachineContext& Context)
		{
			const FReadingLayerScope Reading(Instance, LayerIndex);
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			StopTracks(Layer);
			Layer.Current = Def.FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry);
			Layer.Previous = INDEX_NONE;
			Layer.Transition = INDEX_NONE;
			Layer.Phase = EWidgetAnimMachineBlendPhase::None;
			Layer.BlendElapsed = 0.f;
			Layer.bExited = false;
			Layer.bSettled = false;
			Layer.bStateTrackStarted = true;
			Layer.bPoseSettled = false;
			Layer.bStatePlayed = true;
			Layer.TimeInState = 0.f;
			Layer.PassSeconds = 0.f;
			Layer.Springs.Reset();
			TakeTransitions(Instance, LayerIndex, Context);
		}

		/** Every channel of Target followed by a spring from where the layer shows it now. True once all have arrived. */
		bool AdvanceSprings(FWidgetAnimMachineLayerInstance& Layer, const FWidgetAnimMachinePose& Target, const FWidgetSpringParams& Params, float DeltaTime, FWidgetAnimMachinePose& Out)
		{
			// The union of what the layer was showing and what it is going to: a widget only one of them poses springs to or
			// from rest.
			FWidgetAnimMachinePose Union;
			BlendPoses(Layer.FromPose, Target, 1.f, FWidgetAnimMachineContext(), Union);
			for (const FWidgetAnimTargetedTransform& Transform : Layer.FromPose.Result.TargetedTransforms)
			{
				Union.FindOrAddTransform(Transform.TargetWidget, Transform.Part);
			}

			bool bAllAtRest = true;
			for (int32 Index = 0; Index < Union.Result.TargetedTransforms.Num(); ++Index)
			{
				FWidgetAnimTargetedTransform& Transform = Union.Result.TargetedTransforms[Index];
				UWidget* Widget = Transform.TargetWidget;

				const int32 TargetIndex = Target.FindTransform(Widget, Transform.Part);
				const int32 FromIndex = Layer.FromPose.FindTransform(Widget, Transform.Part);
				const FWidgetAnimTargetedTransform Rest;
				float Goals[8];
				float Starts[8];
				ToComponents(TargetIndex != INDEX_NONE ? Target.Result.TargetedTransforms[TargetIndex] : Rest, Goals);
				ToComponents(FromIndex != INDEX_NONE ? Layer.FromPose.Result.TargetedTransforms[FromIndex] : Rest, Starts);

				float Values[8];
				for (int32 Component = 0; Component < 8; ++Component)
				{
					FWidgetAnimMachineSpringChannel* Channel = Layer.Springs.FindByPredicate([Widget, &Transform, Component](const FWidgetAnimMachineSpringChannel& Candidate)
					{
						return Candidate.Widget.Get() == Widget && Candidate.Part == Transform.Part && Candidate.Component == Component;
					});
					if (!Channel)
					{
						Channel = &Layer.Springs.AddDefaulted_GetRef();
						Channel->Widget = Widget;
						Channel->Part = Transform.Part;
						Channel->Component = static_cast<uint8>(Component);
						Channel->State.Value = Starts[Component];
						Channel->State.Velocity = 0.f;
					}

					const float RestDistance = ComponentRestDistance(Component);
					Channel->bAtRest = UWidgetAnimSpringLibrary::AdvanceSpring(Params, Goals[Component], Channel->State, DeltaTime, RestDistance, RestDistance * 10.f);
					bAllAtRest = bAllAtRest && Channel->bAtRest;
					Values[Component] = Channel->State.Value;
				}
				FromComponents(Values, Transform);
			}

			Out = MoveTemp(Union);
			return bAllAtRest;
		}

		/** Moves one layer DeltaTime on and works out its pose. */
		void TickLayer(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, const FWidgetAnimMachineContext& Context, float DeltaTime)
		{
			const FReadingLayerScope Reading(Instance, LayerIndex);
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			const FWidgetAnimBaselineSet& Baselines = Context.Baselines ? *Context.Baselines : GetEmptyBaselines();

			// Its weight: fixed, or following a Number input.
			float Weight = FMath::Clamp(Def.Weight, 0.f, 1.f);
			if (const FWidgetAnimMachineInputState* WeightInput = Def.WeightInput.IsNone() ? nullptr : Instance.FindInput(Def.WeightInput))
			{
				Weight = FMath::Clamp(WeightInput->Number, 0.f, 1.f);
			}

			// What the inputs allow first, so this frame already shows the way to where they point.
			if (Instance.bInputsChanged || !Layer.bSettled || Layer.Phase != EWidgetAnimMachineBlendPhase::None)
			{
				TakeTransitions(Instance, LayerIndex, Context);
			}

			// The state's speed, following its Speed Input - on its track (or its samples' tracks), as it plays.
			const FWidgetAnimMachineState* CurrentState = Def.States.IsValidIndex(Layer.Current) ? &Def.States[Layer.Current] : nullptr;
			Layer.SpeedScale = 1.f;
			if (CurrentState && !CurrentState->SpeedInput.IsNone())
			{
				float SpeedNumber = 1.f;
				bool bSpeedTruthy = false;
				if (ReadAsNumber(Instance, CurrentState->SpeedInput, SpeedNumber, bSpeedTruthy))
				{
					Layer.SpeedScale = FMath::Max(SpeedNumber, 0.f);
				}
			}
			if (CurrentState && Layer.bStateTrackStarted)
			{
				const FName StateTrackId = GetStateTrackId(*CurrentState);
				const float StateSpeed = Layer.SpeedScale;
				ForEachPlayer(Layer, [StateTrackId, StateSpeed](FWidgetAnimPlayer& StatePlayer)
				{
					for (FWidgetAnimTrack& Track : StatePlayer.Tracks)
					{
						if (Track.AnimId == StateTrackId && Track.SourceIndex == 0)
						{
							Track.Speed = StateSpeed;
						}
					}
				});
			}

			// A blend state's mix, from where its input is now.
			if (CurrentState && CurrentState->IsBlendState() && Layer.BlendPlayers.Num() > 0 && Instance.bInputsChanged)
			{
				UpdateBlendWeights(Instance, *CurrentState, Layer);
			}

			// Closing, and waiting for a way to Exit that never comes - its conditions can't hold - would hold the screen open
			// for good. A few seconds is long enough for any exit worth waiting for.
			if (Instance.bClosing && !Layer.bExited && Def.HasExitPath() && !(Def.Transitions.IsValidIndex(Layer.Transition) && IsLayerExit(Def, Layer.Current)))
			{
				Layer.ClosingWait += DeltaTime;
				if (Layer.ClosingWait > 3.f)
				{
					UE_LOG(LogWidgetAnimStateMachine, Warning, TEXT("%s: layer '%s' found no transition to Exit it could take while closing - the widget closes without it. Give the transitions to Exit the condition Closing Is True."),
						*GetNameSafe(Instance.Machine), *Def.Name.ToString());
					Layer.bExited = true;
				}
			}
			else
			{
				Layer.ClosingWait = 0.f;
			}

			UpdateProgress(Instance, Def, Layer, /*bSnap=*/false);

			// Nothing moving and nothing asked of it: last frame's pose stands.
			const bool bCanReuse = Layer.bSettled && !Instance.bInputsChanged && Layer.Phase == EWidgetAnimMachineBlendPhase::None && Weight == Layer.Weight;
			if (bCanReuse)
			{
				Layer.TimeInState += DeltaTime * Layer.SpeedScale;
				return;
			}

			// Worked out again below: what the machine poses may differ from what its host last applied.
			Instance.bPoseChanged = true;

			// A pose state shows its end pose: its track is played to its end at once - once its staggers have fanned out,
			// which a zero-time evaluation sees to first.
			if (Layer.bStateTrackStarted && !Layer.bPoseSettled && Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Playback == EWidgetAnimMachineStatePlayback::Pose)
			{
				// Once: a track following a Progress Input never finishes, and is left to follow it from its end pose. A blend
				// state's samples each to theirs.
				Layer.bPoseSettled = true;
				ForEachPlayer(Layer, [&Context, &Baselines](FWidgetAnimPlayer& PosePlayer)
				{
					for (FWidgetAnimTrack& Track : PosePlayer.Tracks)
					{
						if (!Track.bFinished)
						{
							if (Track.bNeedsExpansion)
							{
								FWidgetAnimResult Discarded;
								TArray<FName> Ignored;
								UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Context.Host, PosePlayer, Baselines, 0.f, Ignored, Discarded);
							}
							UWidgetAnimLibrary::SettleWidgetAnimTrack(Track, Baselines);
						}
					}
				});
			}

			FWidgetAnimMachinePose Evaluated;
			TArray<FName> JustFinished;
			UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Context.Host, Layer.Player, Baselines, DeltaTime, JustFinished, Evaluated.Result);
			NormalizeTargets(Evaluated.Result, Context.Host);
			ComputeMasks(Layer.Player, Context.Host, Evaluated);

			// A blend state: each sample worked out on its own player, then mixed by their weights - a running weighted mean,
			// each sample lerped in by its share of the weight so far. Every sample's clock moves on, weighted or not.
			if (Layer.BlendPlayers.Num() > 0)
			{
				FWidgetAnimMachinePose Mixed;
				float WeightSoFar = 0.f;
				for (int32 SampleIndex = 0; SampleIndex < Layer.BlendPlayers.Num(); ++SampleIndex)
				{
					FWidgetAnimMachinePose Sample;
					TArray<FName> SampleFinished;
					UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Context.Host, Layer.BlendPlayers[SampleIndex], Baselines, DeltaTime, SampleFinished, Sample.Result);
					NormalizeTargets(Sample.Result, Context.Host);
					ComputeMasks(Layer.BlendPlayers[SampleIndex], Context.Host, Sample);
					const float SampleWeight = Layer.BlendWeights.IsValidIndex(SampleIndex) ? Layer.BlendWeights[SampleIndex] : 0.f;
					if (SampleWeight <= 0.f)
					{
						continue;
					}
					WeightSoFar += SampleWeight;
					if (WeightSoFar <= SampleWeight)
					{
						Mixed = MoveTemp(Sample);
						continue;
					}
					FWidgetAnimMachinePose Blended;
					BlendPoses(Mixed, Sample, SampleWeight / WeightSoFar, Context, Blended);
					Mixed = MoveTemp(Blended);
				}
				Evaluated = MoveTemp(Mixed);
			}

			// How far through its state the layer is, for Exit Time.
			const FWidgetAnimTrack* StateTrack = nullptr;
			const bool bWasPlayed = Layer.bStatePlayed;
			const EWidgetAnimMachineBlendPhase PhaseBefore = Layer.Phase;
			if (Layer.bStateTrackStarted && Def.States.IsValidIndex(Layer.Current) && Layer.BlendPlayers.Num() > 0)
			{
				// A blend state has played once its longest sample has - or every sample has finished.
				const FName TrackId = GetStateTrackId(Def.States[Layer.Current]);
				Layer.TimeInState += DeltaTime * Layer.SpeedScale;
				bool bAllDone = true;
				bool bAnyPending = false;
				float LongestPass = 0.f;
				for (const FWidgetAnimPlayer& BlendPlayer : Layer.BlendPlayers)
				{
					const FWidgetAnimTrack* SampleTrack = BlendPlayer.Tracks.FindByPredicate([TrackId](const FWidgetAnimTrack& Track) { return Track.AnimId == TrackId && Track.SourceIndex == 0; });
					if (!SampleTrack)
					{
						continue;
					}
					bAllDone = bAllDone && (SampleTrack->bFinished || SampleTrack->bSettled);
					bAnyPending = bAnyPending || SampleTrack->bNeedsExpansion;
					LongestPass = FMath::Max(LongestPass, UWidgetAnimLibrary::GetStackPassDuration(SampleTrack->Settings));
				}
				if (!bAnyPending && Layer.PassSeconds <= 0.f)
				{
					Layer.PassSeconds = LongestPass;
				}
				Layer.bStatePlayed = bAllDone || (Layer.PassSeconds > KINDA_SMALL_NUMBER && Layer.TimeInState >= Layer.PassSeconds);
			}
			else if (Layer.bStateTrackStarted && Def.States.IsValidIndex(Layer.Current))
			{
				const FName TrackId = GetStateTrackId(Def.States[Layer.Current]);
				StateTrack = Layer.Player.Tracks.FindByPredicate([TrackId](const FWidgetAnimTrack& Track) { return Track.AnimId == TrackId && Track.SourceIndex == 0; });
				Layer.TimeInState += DeltaTime * Layer.SpeedScale;
				if (StateTrack && !StateTrack->bNeedsExpansion && Layer.PassSeconds <= 0.f)
				{
					// At the state's own speed - its Speed Input moves TimeInState instead.
					Layer.PassSeconds = UWidgetAnimLibrary::GetStackPassDuration(StateTrack->Settings);
				}
				Layer.bStatePlayed = !StateTrack
					|| StateTrack->bFinished
					|| StateTrack->bSettled
					|| (Layer.PassSeconds > KINDA_SMALL_NUMBER && Layer.TimeInState >= Layer.PassSeconds);
			}

			const FWidgetAnimMachineTransition* Transition = Def.Transitions.IsValidIndex(Layer.Transition) ? &Def.Transitions[Layer.Transition] : nullptr;
			switch (Layer.Phase)
			{
			case EWidgetAnimMachineBlendPhase::Blending:
			case EWidgetAnimMachineBlendPhase::BlendingAfterAnimation:
			{
				if (!Transition)
				{
					Layer.Output = MoveTemp(Evaluated);
					FinishTransition(Def, Layer);
					break;
				}

				bool bDone = false;
				if (Transition->Blend == EWidgetAnimMachineBlend::Spring && Layer.Phase == EWidgetAnimMachineBlendPhase::Blending)
				{
					// Transforms follow springs; everything else blends over the spring's visual duration.
					Layer.BlendElapsed += DeltaTime;
					const float MixAlpha = FMath::Clamp(Layer.BlendElapsed / FMath::Max(Transition->Spring.VisualDuration, 0.01f), 0.f, 1.f);
					FWidgetAnimMachinePose Mixed;
					BlendPoses(Layer.FromPose, Evaluated, MixAlpha, Context, Mixed);
					FWidgetAnimMachinePose Sprung;
					const bool bSprungAtRest = AdvanceSprings(Layer, Evaluated, Transition->Spring, DeltaTime, Sprung);
					Mixed.Result.TargetedTransforms = MoveTemp(Sprung.Result.TargetedTransforms);
					Mixed.Masks = MoveTemp(Sprung.Masks);
					Layer.Output = MoveTemp(Mixed);
					bDone = bSprungAtRest && MixAlpha >= 1.f;
				}
				else
				{
					Layer.BlendElapsed += DeltaTime;
					const float Duration = FMath::Max(Transition->GetTweenSeconds(), KINDA_SMALL_NUMBER);
					const float Linear = FMath::Clamp(Layer.BlendElapsed / Duration, 0.f, 1.f);
					const float Alpha = WidgetAnimEasing::Evaluate(Transition->Easing, Transition->CustomCurve, Linear);
					FWidgetAnimMachinePose Blended;
					BlendPoses(Layer.FromPose, Evaluated, Alpha, Context, Blended);
					Layer.Output = MoveTemp(Blended);
					bDone = Linear >= 1.f;
				}

				if (bDone)
				{
					// Exactly the state from here on.
					Layer.Output = MoveTemp(Evaluated);
					FinishTransition(Def, Layer);
				}
				break;
			}

			case EWidgetAnimMachineBlendPhase::TransitionAnimation:
			{
				Layer.BlendElapsed += DeltaTime;
				const float BlendIn = Transition ? Transition->GetTweenSeconds() : 0.f;
				const float Linear = BlendIn > KINDA_SMALL_NUMBER ? FMath::Clamp(Layer.BlendElapsed / BlendIn, 0.f, 1.f) : 1.f;
				const float Alpha = Transition ? WidgetAnimEasing::Evaluate(Transition->Easing, Transition->CustomCurve, Linear) : 1.f;
				FWidgetAnimMachinePose Blended;
				BlendPoses(Layer.FromPose, Evaluated, Alpha, Context, Blended);
				Layer.Output = MoveTemp(Blended);

				const bool bAnimationDone = Layer.Player.Tracks.Num() == 0 || !Layer.Player.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return !Track.bFinished; });
				if (bAnimationDone && Linear >= 1.f)
				{
					const bool bToExit = IsLayerExit(Def, Layer.Current);
					if (bToExit)
					{
						// Exit holds the transition animation's end pose: Anim Out stays out.
						Layer.Output = MoveTemp(Evaluated);
						FinishTransition(Def, Layer);
						Layer.bExited = true;
					}
					else
					{
						// On into the state, blending out of where the animation ended.
						StopTracks(Layer);
						Layer.FromPose = Layer.Output;
						StartStateTrack(Instance, Def, Layer, Context);
						Layer.BlendElapsed = 0.f;
						Layer.Phase = Transition && Transition->GetTweenSeconds() > KINDA_SMALL_NUMBER ? EWidgetAnimMachineBlendPhase::BlendingAfterAnimation : EWidgetAnimMachineBlendPhase::None;
						if (Layer.Phase == EWidgetAnimMachineBlendPhase::None)
						{
							FinishTransition(Def, Layer);
						}
					}
				}
				break;
			}

			default:
				Layer.Output = MoveTemp(Evaluated);
				break;
			}

			Layer.Weight = Weight;

			// Settled: in its state, every track at rest, nothing read from the pointer by its tracks - and not on the very frame
			// it got there or its state finished playing, so the transitions waiting for that (When Finished, one that couldn't
			// interrupt the blend) are looked at once more before the layer rests.
			const bool bJustArrived = (PhaseBefore != EWidgetAnimMachineBlendPhase::None && Layer.Phase == EWidgetAnimMachineBlendPhase::None)
				|| (!bWasPlayed && Layer.bStatePlayed);
			bool bPlayersAtRest = true;
			ForEachPlayer(Layer, [&bPlayersAtRest](FWidgetAnimPlayer& RestPlayer)
			{
				bPlayersAtRest = bPlayersAtRest
					&& !RestPlayer.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return !Track.bFinished || Track.bNeedsExpansion; })
					&& !UWidgetAnimLibrary::HasValueInputs(RestPlayer);
			});
			Layer.bSettled = !bJustArrived
				&& Layer.Phase == EWidgetAnimMachineBlendPhase::None
				&& bPlayersAtRest;
		}

		/** Writes Pose into Acc by Weight: over what is there (Override), or added on top (Additive). Collects the widgets it posed. */
		void ApplyLayer(FWidgetAnimMachinePose& Acc, const FWidgetAnimMachinePose& Pose, EWidgetAnimMachineLayerBlend Mode, float Weight, const FWidgetAnimMachineContext& Context,
			TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>>& OutPosed, const TSet<const UObject*>* WidgetMask = nullptr, bool bMaskExcludes = false,
			int32 ChannelMask = FWidgetAnimMachineLayer::AllMaskChannels)
		{
			if (Weight <= 0.f)
			{
				return;
			}

			// The layer's widget mask (see FWidgetAnimMachineLayer::MaskMode): what it poses on a widget left out is dropped,
			// so the layers under it keep that widget.
			auto Allowed = [WidgetMask, bMaskExcludes](const UObject* Widget)
			{
				if (!WidgetMask)
				{
					return true;
				}
				const bool bListed = Widget && WidgetMask->Contains(Widget);
				return bMaskExcludes ? !bListed : bListed;
			};

			// The layer's channel mask (see FWidgetAnimMachineLayer::MaskChannels): the kinds of things it leaves to the layers under it.
			const uint8 TransformChannels = static_cast<uint8>(ChannelMask) & WidgetAnimMachineChannels::All;
			const bool bMaterials = (ChannelMask & static_cast<int32>(EWidgetAnimMachineLayerChannel::Material)) != 0;
			const bool bOutlines = (ChannelMask & static_cast<int32>(EWidgetAnimMachineLayerChannel::Outline)) != 0;
			const bool bOthers = (ChannelMask & static_cast<int32>(EWidgetAnimMachineLayerChannel::Other)) != 0;

			for (int32 Index = 0; Index < Pose.Result.TargetedTransforms.Num(); ++Index)
			{
				const uint8 Mask = (Pose.Masks.IsValidIndex(Index) ? Pose.Masks[Index] : 0) & TransformChannels;
				if (Mask == 0 || !Allowed(Pose.Result.TargetedTransforms[Index].TargetWidget))
				{
					continue;
				}

				const FWidgetAnimTargetedTransform& Source = Pose.Result.TargetedTransforms[Index];
				const int32 AccIndex = Acc.FindOrAddTransform(Source.TargetWidget, Source.Part);
				FWidgetAnimTargetedTransform& Into = Acc.Result.TargetedTransforms[AccIndex];
				Acc.Masks[AccIndex] |= Mask;
				OutPosed.AddUnique(TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>(Source.TargetWidget, Source.Part));

				if (Mode == EWidgetAnimMachineLayerBlend::Additive)
				{
					// As a track on top: opacity and scale multiply, the rest adds - each only as strongly as the weight. A channel
					// it doesn't animate is at rest (adds nothing), so only a channel its Channels leave out needs skipping.
					if (Mask & WidgetAnimMachineChannels::Opacity)
					{
						Into.Opacity *= FMath::Lerp(1.f, Source.Opacity, Weight);
					}
					if (Mask & WidgetAnimMachineChannels::Scale)
					{
						Into.Scale *= FMath::Lerp(FVector2D(1.0, 1.0), Source.Scale, static_cast<double>(Weight));
					}
					if (Mask & WidgetAnimMachineChannels::Translation)
					{
						Into.Translation += Source.Translation * Weight;
					}
					if (Mask & WidgetAnimMachineChannels::Shear)
					{
						Into.Shear += Source.Shear * Weight;
					}
					if (Mask & WidgetAnimMachineChannels::Angle)
					{
						Into.Angle += Source.Angle * Weight;
					}
					continue;
				}

				// Override: the channels it owns go to its values - by its weight - and the rest stay as the layers under it left them.
				if (Mask & WidgetAnimMachineChannels::Opacity)
				{
					Into.Opacity = FMath::Lerp(Into.Opacity, Source.Opacity, Weight);
				}
				if (Mask & WidgetAnimMachineChannels::Scale)
				{
					Into.Scale = FMath::Lerp(Into.Scale, Source.Scale, static_cast<double>(Weight));
				}
				if (Mask & WidgetAnimMachineChannels::Translation)
				{
					Into.Translation = FMath::Lerp(Into.Translation, Source.Translation, static_cast<double>(Weight));
				}
				if (Mask & WidgetAnimMachineChannels::Shear)
				{
					Into.Shear = FMath::Lerp(Into.Shear, Source.Shear, static_cast<double>(Weight));
				}
				if (Mask & WidgetAnimMachineChannels::Angle)
				{
					Into.Angle = FMath::Lerp(Into.Angle, Source.Angle, Weight);
				}
			}

			// Material parameters are assigned, as a later track's are - by the weight, from what is there or where they rest.
			for (const FWidgetAnimScalarTarget& Source : Pose.Result.ScalarParameters)
			{
				if (!bMaterials || !Allowed(Source.TargetWidget))
				{
					continue;
				}
				FWidgetAnimScalarTarget* Existing = Acc.Result.ScalarParameters.FindByPredicate([&Source](const FWidgetAnimScalarTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				float Base = Source.Value;
				if (Existing)
				{
					Base = Existing->Value;
				}
				else
				{
					FindRestScalar(Context, Source, Base);
				}
				FWidgetAnimScalarTarget& Into = Existing ? *Existing : Acc.Result.ScalarParameters.Add_GetRef(Source);
				Into.Value = FMath::Lerp(Base, Source.Value, Weight);
				Into.bIsBaseline = Source.bIsBaseline;
			}
			for (const FWidgetAnimVectorTarget& Source : Pose.Result.VectorParameters)
			{
				if (!bMaterials || !Allowed(Source.TargetWidget))
				{
					continue;
				}
				FWidgetAnimVectorTarget* Existing = Acc.Result.VectorParameters.FindByPredicate([&Source](const FWidgetAnimVectorTarget& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Slot == Source.Slot && Candidate.ParameterName == Source.ParameterName;
				});
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				FLinearColor Base = Source.Value;
				if (Existing)
				{
					Base = Existing->Value;
				}
				else
				{
					FindRestVector(Context, Source, Base);
				}
				FWidgetAnimVectorTarget& Into = Existing ? *Existing : Acc.Result.VectorParameters.Add_GetRef(Source);
				Into.Value = FMath::Lerp(Base, Source.Value, Weight);
				Into.bIsBaseline = Source.bIsBaseline;
			}

			// Outlines and registered types say what their channel IS: the later layer's, unless it is only a baseline.
			for (const FWidgetAnimShapeTarget& Source : Pose.Result.ShapeTargets)
			{
				if (!bOutlines || !Allowed(Source.TargetBox))
				{
					continue;
				}
				FWidgetAnimShapeTarget* Existing = Acc.Result.ShapeTargets.FindByPredicate([&Source](const FWidgetAnimShapeTarget& Candidate) { return Candidate.TargetBox == Source.TargetBox; });
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				if (Existing)
				{
					*Existing = Source;
				}
				else
				{
					Acc.Result.ShapeTargets.Add(Source);
				}
			}
			for (const FWidgetAnimAuthoritativeWrite& Source : Pose.Result.AuthoritativeWrites)
			{
				if (!bOthers || !Allowed(Source.TargetWidget))
				{
					continue;
				}
				FWidgetAnimAuthoritativeWrite* Existing = Acc.Result.AuthoritativeWrites.FindByPredicate([&Source](const FWidgetAnimAuthoritativeWrite& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Channel == Source.Channel;
				});
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				if (Existing && Weight < 1.f && Existing->Evaluator == Source.Evaluator && Source.Evaluator && !Existing->bIsBaseline)
				{
					// A write whose value only means something with the rest of it - a Shape track's place among its keys -
					// can't be blended by its value: the heavier layer has it.
					if (!Source.Evaluator->BlendsWriteValues())
					{
						if (Weight >= 0.5f)
						{
							*Existing = Source;
						}
						continue;
					}
					Existing->Value = FMath::Lerp(Existing->Value, Source.Value, static_cast<double>(Weight));
					continue;
				}
				if (Existing)
				{
					*Existing = Source;
				}
				else
				{
					Acc.Result.AuthoritativeWrites.Add(Source);
				}
			}
		}

		/**
		 * The widgets a layer's Animations Affect names - widget paths, its targets as this use of the machine maps them, (This
		 * Widget) the host; the host alone when it names none - and the Shape Box each is or sits in. False for a layer that
		 * affects everything.
		 */
		bool BuildLayerMask(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, const FWidgetAnimMachineContext& Context, TSet<const UObject*>& OutWidgets)
		{
			if (Def.MaskMode == EWidgetAnimMachineLayerMask::All)
			{
				return false;
			}
			static const TArray<FName> HostOnly = { FName() };
			for (const FName Target : Def.MaskTargets.Num() > 0 ? Def.MaskTargets : HostOnly)
			{
				UWidget* Widget = ResolveLayerWidget(Instance, Target, Context);
				if (Widget)
				{
					OutWidgets.Add(Widget);
					if (const UShapeBox* Box = UWidgetAnimLibrary::ResolveShapeBox(Widget))
					{
						OutWidgets.Add(Box);
					}
				}
			}
			return true;
		}

		/** The frame's other tracks on top of the machines, as tracks combine: opacity and scale multiply, the rest adds. */
		void FoldTracksOnTop(FWidgetAnimMachinePose& Acc, const FWidgetAnimResult& Tracks)
		{
			for (const FWidgetAnimTargetedTransform& Source : Tracks.TargetedTransforms)
			{
				const int32 AccIndex = Acc.FindOrAddTransform(Source.TargetWidget, Source.Part);
				FWidgetAnimTargetedTransform& Into = Acc.Result.TargetedTransforms[AccIndex];
				Into.Opacity *= Source.Opacity;
				Into.Scale *= Source.Scale;
				Into.Translation += Source.Translation;
				Into.Shear += Source.Shear;
				Into.Angle += Source.Angle;
			}

			Acc.Result.FoldMaterialParameters(Tracks);

			for (const FWidgetAnimShapeTarget& Source : Tracks.ShapeTargets)
			{
				FWidgetAnimShapeTarget* Existing = Acc.Result.ShapeTargets.FindByPredicate([&Source](const FWidgetAnimShapeTarget& Candidate) { return Candidate.TargetBox == Source.TargetBox; });
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				if (Existing)
				{
					*Existing = Source;
				}
				else
				{
					Acc.Result.ShapeTargets.Add(Source);
				}
			}
			for (const FWidgetAnimAuthoritativeWrite& Source : Tracks.AuthoritativeWrites)
			{
				FWidgetAnimAuthoritativeWrite* Existing = Acc.Result.AuthoritativeWrites.FindByPredicate([&Source](const FWidgetAnimAuthoritativeWrite& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Channel == Source.Channel;
				});
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				if (Existing)
				{
					*Existing = Source;
				}
				else
				{
					Acc.Result.AuthoritativeWrites.Add(Source);
				}
			}
		}

		/** Which built-in inputs a machine reads - not a name the machine has an input of its own for, which wins over the built-in. */
		uint64 FindUsedBuiltIns(const UWidgetAnimStateMachineAsset& Machine)
		{
			auto BitOf = [&Machine](FName Name) { return Machine.FindInput(Name) ? uint64(0) : WidgetAnimMachineInputs::GetBuiltInBit(Name); };
			uint64 Used = 0;
			for (const FWidgetAnimMachineLayer& Layer : Machine.Layers)
			{
				for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
				{
					for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
					{
						Used |= BitOf(Condition.Input);
						if (Condition.CompareTo == EWidgetAnimMachineCompareTo::Input)
						{
							Used |= BitOf(Condition.OtherInput);
						}
					}
				}
			}
			return Used;
		}
	}

	// =================================================================================================
	// Public
	// =================================================================================================

	void Initialize(FWidgetAnimMachineInstance& Instance, const FWidgetAnimStateMachineEntry& Row, int32 RowIndex)
	{
		Instance = FWidgetAnimMachineInstance();
		Instance.Machine = Row.StateMachine;
		Instance.RowIndex = RowIndex;

		UWidgetAnimStateMachineAsset* Machine = Row.StateMachine;
		if (!Machine)
		{
			return;
		}

		TArray<FName> TargetNames;
		Machine->GetTargetNames(TargetNames);
		for (const FName TargetName : TargetNames)
		{
			Instance.TargetPaths.Add(TargetName, Row.GetTargetPath(TargetName));
		}

		for (const FWidgetAnimMachineInput& Definition : Machine->Inputs)
		{
			FWidgetAnimMachineInputState& Input = Instance.Inputs.AddDefaulted_GetRef();
			Input.Name = Definition.Name;
			Input.Type = Definition.Type;

			// This use's own settings where it overrides them.
			const FWidgetAnimMachineInputOverride* Override = Row.FindInputOverride(Definition.Name);
			Input.bValue = Override ? Override->bDefaultBool : Definition.bDefaultBool;
			Input.Number = Override ? Override->DefaultNumber : Definition.DefaultNumber;
			Input.HoldTime = Override ? Override->HoldTime : Definition.HoldTime;
			Input.Options = Definition.Options;
			switch (Definition.Type)
			{
			case EWidgetAnimMachineInputType::Hold:
				// Not held, nothing done: a hold starts from nothing, whatever the defaults say.
				Input.bValue = false;
				Input.Number = 0.f;
				break;
			case EWidgetAnimMachineInputType::Integer:
				Input.Number = static_cast<float>(Override ? Override->DefaultInteger : Definition.DefaultInteger);
				break;
			case EWidgetAnimMachineInputType::Choice:
				// Its first option.
				Input.Number = 0.f;
				break;
			case EWidgetAnimMachineInputType::Vector2D:
				Input.Vector = FVector2f(Override ? Override->DefaultVector2D : Definition.DefaultVector2D);
				break;
			default:
				break;
			}
			Input.PreviousNumber = Input.Number;
			Input.PreviousVector = Input.Vector;
			Input.Source = Override ? Override->Source : Definition.Source;
			if (Override)
			{
				Input.MeasuredOnPath = FWidgetAnimGroupTarget::IsThisWidget(Override->MeasuredOnWidget) ? FName() : Override->MeasuredOnWidget;
			}
			else if (!Definition.MeasuredOn.IsNone())
			{
				Input.MeasuredOnPath = Row.GetTargetPath(Definition.MeasuredOn);
			}
		}

		Instance.UsedBuiltIns = FindUsedBuiltIns(*Machine);

		// A Press And Hold with nothing driving it is held by the widget's own pressed state: that is read too.
		if (Instance.Inputs.ContainsByPredicate([](const FWidgetAnimMachineInputState& Input) { return Input.Type == EWidgetAnimMachineInputType::Hold && !Input.Source.IsValid(); }))
		{
			Instance.UsedBuiltIns |= WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Pressed);
		}

		// Clicked is worked out from the press, hover and focus: those are read too.
		if (Instance.UsedBuiltIns & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Clicked))
		{
			Instance.UsedBuiltIns |= WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Pressed)
				| WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Hovered)
				| WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Focused);
		}

		// The Events are worked out from the states they turn, and the buttons fire only while the widget has focus: those
		// states are read too.
		auto ReadsToo = [&Instance](FName Pulse, FName StateRead)
		{
			if (Instance.UsedBuiltIns & WidgetAnimMachineInputs::GetBuiltInBit(Pulse))
			{
				Instance.UsedBuiltIns |= WidgetAnimMachineInputs::GetBuiltInBit(StateRead);
			}
		};
		ReadsToo(WidgetAnimMachineInputs::HoverEntered, WidgetAnimMachineInputs::Hovered);
		ReadsToo(WidgetAnimMachineInputs::HoverExited, WidgetAnimMachineInputs::Hovered);
		ReadsToo(WidgetAnimMachineInputs::PressStarted, WidgetAnimMachineInputs::Pressed);
		ReadsToo(WidgetAnimMachineInputs::Released, WidgetAnimMachineInputs::Pressed);
		ReadsToo(WidgetAnimMachineInputs::FocusGained, WidgetAnimMachineInputs::Focused);
		ReadsToo(WidgetAnimMachineInputs::FocusLost, WidgetAnimMachineInputs::Focused);
		for (const FName Name : WidgetAnimMachineInputs::GetBuiltIns())
		{
			if (Name == WidgetAnimMachineInputs::Accept || Name == WidgetAnimMachineInputs::Back || WidgetAnimMachineInputs::GetBuiltInKey(Name).IsValid())
			{
				ReadsToo(Name, WidgetAnimMachineInputs::Focused);
			}
		}
		auto SetUpReader = [](FWidgetAnimMachineWidgetReader& Reader)
		{
			Reader.HoverReader.State = EWidgetAnimInteractionState::Hovered;
			Reader.PressReader.State = EWidgetAnimInteractionState::Pressed;
			Reader.FocusReader.State = EWidgetAnimInteractionState::Focused;
		};
		SetUpReader(Instance.HostReader);

		// The widgets each layer's Widget Input Targets name: a reader for each, shared by the layers that name it.
		Instance.LayerReaders.SetNum(Machine->Layers.Num());
		for (int32 LayerIndex = 0; LayerIndex < Machine->Layers.Num(); ++LayerIndex)
		{
			for (const FName WidgetName : Machine->Layers[LayerIndex].WidgetInputTargets)
			{
				int32 ReaderIndex = INDEX_NONE;
				if (!UWidgetAnimStateMachineAsset::NamesHostWidget(WidgetName))
				{
					ReaderIndex = Instance.TargetReaders.IndexOfByPredicate([WidgetName](const FWidgetAnimMachineWidgetReader& Candidate) { return Candidate.Name == WidgetName; });
					if (ReaderIndex == INDEX_NONE)
					{
						ReaderIndex = Instance.TargetReaders.AddDefaulted();
						Instance.TargetReaders[ReaderIndex].Name = WidgetName;
						SetUpReader(Instance.TargetReaders[ReaderIndex]);
					}
				}
				Instance.LayerReaders[LayerIndex].AddUnique(ReaderIndex);
			}
		}

		Instance.Layers.SetNum(Machine->Layers.Num());
	}

	void Start(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context)
	{
		if (!Instance.Machine)
		{
			return;
		}

		// The machine may have changed since it was set up - an edit in the editor - so the layers are sized again.
		Instance.Layers.SetNum(Instance.Machine->Layers.Num());
		for (FWidgetAnimMachineLayerInstance& Layer : Instance.Layers)
		{
			UWidgetAnimLibrary::StopWidgetAnimTracks(Layer.Player, TArray<FName>(), /*bRemoveTracks=*/true);
			Layer = FWidgetAnimMachineLayerInstance();
		}

		Instance.bStarted = true;
		Instance.bClosing = false;
		Instance.bInputsChanged = true;
		Instance.bPoseChanged = true;
		Instance.PendingActions.Reset();

		// No layer is anywhere yet: conditions on another layer's state, as Entry picks the first states, hold for none.
		Instance.LayerStatesNow.Init(INDEX_NONE, Instance.Layers.Num());
		Instance.LayerStatesBefore.Init(INDEX_NONE, Instance.Layers.Num());

		// What the widget is now - hovered already, disabled already - before Entry picks the first state.
		ReadInputs(Instance, Context, 0.f);
		UpdateHolds(Instance, 0.f);

		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num(); ++LayerIndex)
		{
			EnterLayer(Instance, LayerIndex, Context);
		}

		// The first states' On Enter, seen from the first Tick on.
		RunPendingActions(Instance, Context);
	}

	void Stop(FWidgetAnimMachineInstance& Instance)
	{
		for (FWidgetAnimMachineLayerInstance& Layer : Instance.Layers)
		{
			UWidgetAnimLibrary::StopWidgetAnimTracks(Layer.Player, TArray<FName>(), /*bRemoveTracks=*/true);
			Layer = FWidgetAnimMachineLayerInstance();
		}
		Instance.bStarted = false;

		// What it posed goes back to rest on its host's next compose.
		Instance.bPoseChanged = true;
	}

	void Tick(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, float DeltaTime)
	{
		if (!Instance.bStarted || !Instance.Machine)
		{
			return;
		}

		// A machine edited while it plays (in the editor) gains or loses layers: those it gained start at their Entry.
		const int32 NumLayers = Instance.Machine->Layers.Num();
		if (Instance.Layers.Num() != NumLayers)
		{
			const int32 OldNum = Instance.Layers.Num();
			Instance.Layers.SetNum(NumLayers);
			for (int32 LayerIndex = OldNum; LayerIndex < NumLayers; ++LayerIndex)
			{
				EnterLayer(Instance, LayerIndex, Context);
			}
		}

		ReadInputs(Instance, Context, DeltaTime);
		UpdateHolds(Instance, DeltaTime);

		// Every layer as it is before any moves - and as it was last time: what conditions on another layer's state read.
		Instance.LayerStatesBefore = Instance.LayerStatesNow;
		Instance.LayerStatesNow.SetNum(NumLayers);
		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			Instance.LayerStatesNow[LayerIndex] = Instance.Layers[LayerIndex].Current;
		}
		if (Instance.LayerStatesBefore.Num() != NumLayers)
		{
			Instance.LayerStatesBefore = Instance.LayerStatesNow;
		}
		// A layer that moved since last time is news to the others: they look again.
		if (Instance.LayerStatesBefore != Instance.LayerStatesNow)
		{
			Instance.bInputsChanged = true;
		}

		// A playing state whose Parameters From Inputs read inputs that moved: its tracks take the new values now, rather than
		// the next time the state starts.
		if (Instance.bInputsChanged)
		{
			RefreshParameters(Instance, Context, /*bOnlyIfInputsChanged=*/true);
		}

		for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];

			// A state, transition or node removed while it played (in the editor): the layer starts again from Entry.
			if (!Def.States.IsValidIndex(Layer.Current) || (Layer.Transition != INDEX_NONE && !Def.Transitions.IsValidIndex(Layer.Transition)))
			{
				EnterLayer(Instance, LayerIndex, Context);
			}

			TickLayer(Instance, LayerIndex, Context, DeltaTime);
		}

		// Triggers are one-frame pulses: spent whether or not anything took them. And the values as they were this time, for
		// Increased, Decreased and Changed next time - which so hold for the one frame something moved.
		for (FWidgetAnimMachineInputState& Input : Instance.Inputs)
		{
			Input.bFired = false;
			Input.bLetGo = false;
			Input.PreviousNumber = Input.Number;
			Input.PreviousVector = Input.Vector;
		}
		auto SpendPulses = [](FWidgetAnimMachineWidgetState& WidgetState)
		{
			WidgetState.bClicked = false;
			WidgetState.Fired = 0;
		};
		SpendPulses(Instance.HostReader.State);
		for (FWidgetAnimMachineWidgetReader& Reader : Instance.TargetReaders)
		{
			SpendPulses(Reader.State);
		}
		for (FWidgetAnimMachineWidgetState& LayerState : Instance.LayerWidgetStates)
		{
			SpendPulses(LayerState);
		}
		Instance.bInputsChanged = false;

		// The actions the frame's transitions queued: after the inputs were spent, so what they set and fire is seen next frame -
		// never within this one, which keeps a machine from looping on itself.
		RunPendingActions(Instance, Context);
	}

	void Compose(TArrayView<FWidgetAnimMachineInstance> Instances, EWidgetAnimTracksWithMachines Mode, const FWidgetAnimMachineContext& Context, FWidgetAnimResult& InOutResult,
		TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>>& InOutLastPosed)
	{
		NormalizeTargets(InOutResult, Context.Host);

		FWidgetAnimMachinePose Acc;
		if (Mode == EWidgetAnimTracksWithMachines::UnderMachines)
		{
			// The tracks are the bottom of the stack, every channel of theirs coverable.
			Acc.Result = InOutResult;
			Acc.Masks.Init(WidgetAnimMachineChannels::All, Acc.Result.TargetedTransforms.Num());
		}

		TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>> Posed;
		for (const FWidgetAnimMachineInstance& Instance : Instances)
		{
			if (!Instance.bStarted || !Instance.Machine)
			{
				continue;
			}
			for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Instance.Machine->Layers.Num(); ++LayerIndex)
			{
				const FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
				if (!Layer.Output.IsEmpty())
				{
					const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
					TSet<const UObject*> MaskWidgets;
					const bool bMasked = BuildLayerMask(Instance, Def, Context, MaskWidgets);
					ApplyLayer(Acc, Layer.Output, Def.BlendMode, Layer.Weight, Context, Posed, bMasked ? &MaskWidgets : nullptr, Def.MaskMode == EWidgetAnimMachineLayerMask::AllButThese,
						Def.MaskChannels);
				}
			}
		}

		if (Mode == EWidgetAnimTracksWithMachines::AddOnTop)
		{
			FoldTracksOnTop(Acc, InOutResult);
		}

		// A widget the machines posed last frame and not this one goes back to rest - unless something else poses it.
		for (const TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>& Was : InOutLastPosed)
		{
			UWidget* Widget = Was.Key.Get();
			if (Widget && Acc.FindTransform(Widget, Was.Value) == INDEX_NONE)
			{
				Acc.FindOrAddTransform(Widget, Was.Value);
			}
		}
		InOutLastPosed = MoveTemp(Posed);

		InOutResult = MoveTemp(Acc.Result);
	}

	void ReleaseFinishedTracks(FWidgetAnimMachineInstance& Instance)
	{
		for (FWidgetAnimMachineLayerInstance& Layer : Instance.Layers)
		{
			ForEachPlayer(Layer, [](FWidgetAnimPlayer& LayerPlayer) { UWidgetAnimLibrary::ReleaseFinishedTracks(LayerPlayer); });
		}
	}

	void SetClosing(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, bool bClosing)
	{
		if (Instance.bClosing == bClosing)
		{
			return;
		}
		Instance.bClosing = bClosing;
		Instance.bInputsChanged = true;

		if (!Instance.bStarted || !Instance.Machine)
		{
			return;
		}

		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Instance.Machine->Layers.Num(); ++LayerIndex)
		{
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			Layer.ClosingWait = 0.f;
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			const bool bAtExit = IsLayerExit(Def, Layer.Current);
			if (!bClosing && (Layer.bExited || bAtExit))
			{
				// Back in through Entry - blending in from the pose Exit left, so nothing snaps.
				Layer.bExited = false;
				EnterLayer(Instance, LayerIndex, Context);
			}
			else if (bClosing)
			{
				TakeTransitions(Instance, LayerIndex, Context);
			}
		}
	}

	bool IsExitPending(const FWidgetAnimMachineInstance& Instance)
	{
		if (!Instance.bStarted || !Instance.Machine)
		{
			return false;
		}
		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Instance.Machine->Layers.Num(); ++LayerIndex)
		{
			if (Instance.Machine->Layers[LayerIndex].HasExitPath() && !Instance.Layers[LayerIndex].bExited)
			{
				return true;
			}
		}
		return false;
	}

	bool SetBool(FWidgetAnimMachineInstance& Instance, FName Input, bool bValue)
	{
		FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (State && State->Type == EWidgetAnimMachineInputType::Hold)
		{
			// Pressed (true) or let go (false) from outside - timed with the widget's own press, on the next Tick.
			if (State->bHoldSetDown != bValue)
			{
				State->bHoldSetDown = bValue;
				Instance.bInputsChanged = true;
			}
			return true;
		}
		if (!State || State->Type != EWidgetAnimMachineInputType::Bool)
		{
			return false;
		}
		if (State->bValue != bValue)
		{
			State->bValue = bValue;
			Instance.bInputsChanged = true;
		}
		return true;
	}

	bool SetNumber(FWidgetAnimMachineInstance& Instance, FName Input, float Value)
	{
		FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State || (State->Type != EWidgetAnimMachineInputType::Number && State->Type != EWidgetAnimMachineInputType::Integer))
		{
			return false;
		}
		// An Integer holds whole numbers only.
		const float Kept = State->Type == EWidgetAnimMachineInputType::Integer ? static_cast<float>(FMath::RoundToInt32(Value)) : Value;
		if (State->Number != Kept)
		{
			State->Number = Kept;
			Instance.bInputsChanged = true;
		}
		return true;
	}

	bool SetInteger(FWidgetAnimMachineInstance& Instance, FName Input, int32 Value)
	{
		return SetNumber(Instance, Input, static_cast<float>(Value));
	}

	/** A Choice's option, by its number: held to the options it has. */
	static bool SetChoiceIndex(FWidgetAnimMachineInstance& Instance, FWidgetAnimMachineInputState& State, int32 Index)
	{
		if (State.Options.Num() == 0)
		{
			return false;
		}
		const float Kept = static_cast<float>(FMath::Clamp(Index, 0, State.Options.Num() - 1));
		if (State.Number != Kept)
		{
			State.Number = Kept;
			Instance.bInputsChanged = true;
		}
		return true;
	}

	bool SetChoice(FWidgetAnimMachineInstance& Instance, FName Input, FName Option)
	{
		FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		const int32 Index = State && State->Type == EWidgetAnimMachineInputType::Choice ? State->Options.IndexOfByKey(Option) : INDEX_NONE;
		return Index != INDEX_NONE && SetChoiceIndex(Instance, *State, Index);
	}

	bool SetVector2D(FWidgetAnimMachineInstance& Instance, FName Input, const FVector2D& Value)
	{
		FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State || State->Type != EWidgetAnimMachineInputType::Vector2D)
		{
			return false;
		}
		const FVector2f Now(Value);
		if (!Now.Equals(State->Vector, 1e-6f))
		{
			State->Vector = Now;
			Instance.bInputsChanged = true;
		}
		return true;
	}

	bool FireTrigger(FWidgetAnimMachineInstance& Instance, FName Input)
	{
		FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State || State->Type != EWidgetAnimMachineInputType::Trigger)
		{
			return false;
		}
		State->bFired = true;
		Instance.bInputsChanged = true;
		return true;
	}

	bool SetFromNumber(FWidgetAnimMachineInstance& Instance, FName Input, double Value)
	{
		const FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State)
		{
			return false;
		}
		switch (State->Type)
		{
		case EWidgetAnimMachineInputType::Bool:
		case EWidgetAnimMachineInputType::Hold:
			return SetBool(Instance, Input, !FMath::IsNearlyZero(Value));
		case EWidgetAnimMachineInputType::Trigger:
			return FMath::IsNearlyZero(Value) || FireTrigger(Instance, Input);
		case EWidgetAnimMachineInputType::Choice:
			return SetChoiceIndex(Instance, *Instance.FindInput(Input), FMath::RoundToInt32(Value));
		case EWidgetAnimMachineInputType::Vector2D:
			return false;
		default:
			return SetNumber(Instance, Input, static_cast<float>(Value));
		}
	}

	bool GetBool(const FWidgetAnimMachineInstance& Instance, FName Input, bool& OutValue)
	{
		float Number = 0.f;
		return ReadAsNumber(Instance, Input, Number, OutValue);
	}

	bool GetNumber(const FWidgetAnimMachineInstance& Instance, FName Input, float& OutValue)
	{
		bool bTruthy = false;
		return ReadAsNumber(Instance, Input, OutValue, bTruthy);
	}

	bool GetChoice(const FWidgetAnimMachineInstance& Instance, FName Input, FName& OutOption)
	{
		const FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State || State->Type != EWidgetAnimMachineInputType::Choice)
		{
			return false;
		}
		OutOption = GetChoiceOption(State);
		return true;
	}

	bool GetVector2D(const FWidgetAnimMachineInstance& Instance, FName Input, FVector2D& OutValue)
	{
		const FWidgetAnimMachineInputState* State = Instance.FindInput(Input);
		if (!State || State->Type != EWidgetAnimMachineInputType::Vector2D)
		{
			return false;
		}
		OutValue = FVector2D(State->Vector);
		return true;
	}

	void SetBuiltIn(FWidgetAnimMachineInstance& Instance, FName Input, bool bValue)
	{
		FWidgetAnimMachineWidgetState& State = Instance.HostReader.State;
		bool* Field = Input == WidgetAnimMachineInputs::Hovered ? &State.bHovered
			: Input == WidgetAnimMachineInputs::Pressed ? &State.bPressed
			: Input == WidgetAnimMachineInputs::Focused ? &State.bFocused
			: Input == WidgetAnimMachineInputs::Selected ? &State.bSelected
			: Input == WidgetAnimMachineInputs::Disabled ? &State.bDisabled
			: Input == WidgetAnimMachineInputs::Closing ? &Instance.bClosing
			: Input == WidgetAnimMachineInputs::Clicked ? &State.bClicked
			: nullptr;
		if (Field)
		{
			if (*Field != bValue)
			{
				*Field = bValue;
				Instance.bInputsChanged = true;
			}
			return;
		}

		// The Events and the Gamepad buttons: fired (a pulse, spent at the end of the frame).
		const uint64 Bit = WidgetAnimMachineInputs::IsBuiltInPulse(Input) ? WidgetAnimMachineInputs::GetBuiltInBit(Input) : uint64(0);
		if (Bit != 0 && ((State.Fired & Bit) != 0) != bValue)
		{
			State.Fired = bValue ? (State.Fired | Bit) : (State.Fired & ~Bit);
			Instance.bInputsChanged = true;
		}
	}

	bool IsInState(const FWidgetAnimMachineInstance& Instance, FName State, FName LayerName)
	{
		if (!Instance.bStarted || !Instance.Machine || State.IsNone())
		{
			return false;
		}
		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Instance.Machine->Layers.Num(); ++LayerIndex)
		{
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			if (!LayerName.IsNone() && Def.Name != LayerName)
			{
				continue;
			}
			// In a sub-machine is in it, whichever of its states the layer is in.
			const int32 Current = Instance.Layers[LayerIndex].Current;
			if (Def.IsWithin(Current, Def.FindStateIndexByName(State)))
			{
				return true;
			}
		}
		return false;
	}

	FName GetCurrentState(const FWidgetAnimMachineInstance& Instance, FName LayerName)
	{
		if (!Instance.bStarted || !Instance.Machine)
		{
			return NAME_None;
		}
		const int32 LayerIndex = LayerName.IsNone() ? 0 : Instance.Machine->FindLayerIndex(LayerName);
		if (!Instance.Layers.IsValidIndex(LayerIndex) || !Instance.Machine->Layers.IsValidIndex(LayerIndex))
		{
			return NAME_None;
		}
		const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
		const int32 Current = Instance.Layers[LayerIndex].Current;
		return Def.States.IsValidIndex(Current) ? Def.States[Current].Name : FName();
	}

	int32 SetTrackValue(FWidgetAnimMachineInstance& Instance, const FWidgetAnimValueId& Id, const FVector4& Value, bool bSnap)
	{
		int32 Taken = 0;
		for (FWidgetAnimMachineLayerInstance& Layer : Instance.Layers)
		{
			int32 LayerTaken = 0;
			ForEachPlayer(Layer, [&LayerTaken, &Id, &Value, bSnap](FWidgetAnimPlayer& LayerPlayer)
			{
				LayerTaken += UWidgetAnimLibrary::SetWidgetAnimTrackValue(LayerPlayer, Id, Value, bSnap);
			});
			if (LayerTaken > 0)
			{
				Layer.bSettled = false;
			}
			Taken += LayerTaken;
		}
		return Taken;
	}

	void GatherAllSettings(const FWidgetAnimStateMachineEntry& Row, const FWidgetAnimMachineContext& Context, TArray<FWidgetAnimSettings>& OutSettings)
	{
		UWidgetAnimStateMachineAsset* Machine = Row.StateMachine;
		if (!Machine)
		{
			return;
		}

		TMap<FName, FName> TargetPaths;
		TArray<FName> TargetNames;
		Machine->GetTargetNames(TargetNames);
		for (const FName TargetName : TargetNames)
		{
			TargetPaths.Add(TargetName, Row.GetTargetPath(TargetName));
		}

		for (const FWidgetAnimMachineLayer& Layer : Machine->Layers)
		{
			for (const FWidgetAnimMachineState& State : Layer.States)
			{
				if (State.Kind != EWidgetAnimMachineNodeKind::State)
				{
					continue;
				}
				// Everything it can play: its animation and variations, or its blend samples.
				TArray<UWidgetAnimPresetAsset*, TInlineAllocator<8>> Played;
				if (State.IsBlendState())
				{
					for (const FWidgetAnimMachineBlendSample& Sample : State.BlendSamples)
					{
						Played.Add(Sample.Animation);
					}
				}
				else
				{
					Played.Add(State.Animation);
					for (const FWidgetAnimMachineRandomVariation& Variation : State.RandomVariations)
					{
						Played.Add(Variation.Animation);
					}
				}
				for (UWidgetAnimPresetAsset* Animation : Played)
				{
					if (Animation)
					{
						BuildAnimationSettings(Animation, TargetPaths, Context, EWidgetAnimMachineStatePlayback::Once, 1.f, /*bFollowsProgress=*/false, OutSettings);
					}
				}
			}
			for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
			{
				if (Transition.Blend == EWidgetAnimMachineBlend::Animation && Transition.TransitionAnimation)
				{
					BuildAnimationSettings(Transition.TransitionAnimation, TargetPaths, Context, EWidgetAnimMachineStatePlayback::Once, 1.f, /*bFollowsProgress=*/false, OutSettings);
				}
			}
		}
	}

	bool IsAnimating(const FWidgetAnimMachineInstance& Instance)
	{
		if (!Instance.bStarted)
		{
			return false;
		}
		return Instance.Layers.ContainsByPredicate([](const FWidgetAnimMachineLayerInstance& Layer) { return !Layer.bSettled; });
	}

	void GetDebug(const FWidgetAnimMachineInstance& Instance, TArray<FWidgetAnimMachineLayerDebug>& OutLayers)
	{
		OutLayers.Reset();
		const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
		if (!Machine)
		{
			return;
		}
		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Machine->Layers.Num(); ++LayerIndex)
		{
			const FWidgetAnimMachineLayer& Def = Machine->Layers[LayerIndex];
			const FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			FWidgetAnimMachineLayerDebug& Debug = OutLayers.AddDefaulted_GetRef();
			Debug.CurrentState = Def.States.IsValidIndex(Layer.Current) ? Def.States[Layer.Current].Id : FGuid();
			for (int32 Parent = Def.FindParentIndex(Layer.Current), Step = 0; Parent != INDEX_NONE && Step <= Def.States.Num(); Parent = Def.FindParentIndex(Parent), ++Step)
			{
				Debug.CurrentMachines.Insert(Def.States[Parent].Id, 0);
			}
			Debug.PreviousState = Def.States.IsValidIndex(Layer.Previous) ? Def.States[Layer.Previous].Id : FGuid();
			Debug.ActiveTransition = Def.Transitions.IsValidIndex(Layer.Transition) ? Def.Transitions[Layer.Transition].Id : FGuid();
			if (Def.Transitions.IsValidIndex(Layer.Transition))
			{
				// The whole way: a transition animation, then the blend out of it, read as one bar.
				const FWidgetAnimMachineTransition& Transition = Def.Transitions[Layer.Transition];
				const float Blend = Transition.Blend == EWidgetAnimMachineBlend::Spring ? UWidgetAnimSpringLibrary::GetVisualDuration(Transition.Spring) : Transition.GetTweenSeconds();
				float Elapsed = Layer.BlendElapsed;
				float Total = Blend;
				if (Layer.Phase == EWidgetAnimMachineBlendPhase::TransitionAnimation || Layer.Phase == EWidgetAnimMachineBlendPhase::BlendingAfterAnimation)
				{
					Total = Layer.TransitionAnimationSeconds + Transition.GetTweenSeconds();
					Elapsed = Layer.Phase == EWidgetAnimMachineBlendPhase::TransitionAnimation
						? FMath::Min(Layer.BlendElapsed, Layer.TransitionAnimationSeconds)
						: Layer.TransitionAnimationSeconds + Layer.BlendElapsed;
				}
				Debug.TransitionAlpha = Total > KINDA_SMALL_NUMBER ? FMath::Clamp(Elapsed / Total, 0.f, 1.f) : 1.f;
			}

			// Through the pass it is on: a looping state's bar runs again each pass, a ping-pong's back and forth.
			const FWidgetAnimMachineState* StateDef = Def.States.IsValidIndex(Layer.Current) ? &Def.States[Layer.Current] : nullptr;
			const EWidgetAnimMachineStatePlayback Playback = StateDef ? StateDef->Playback : EWidgetAnimMachineStatePlayback::Once;
			if (Layer.PassSeconds > KINDA_SMALL_NUMBER)
			{
				const float Passes = Layer.TimeInState / Layer.PassSeconds;
				switch (Playback)
				{
				case EWidgetAnimMachineStatePlayback::Loop:
					Debug.StateAlpha = FMath::Frac(Passes);
					break;
				case EWidgetAnimMachineStatePlayback::PingPong:
				{
					const float Cycle = FMath::Fmod(Passes, 2.f);
					Debug.StateAlpha = Cycle <= 1.f ? Cycle : 2.f - Cycle;
					break;
				}
				case EWidgetAnimMachineStatePlayback::Pose:
					Debug.StateAlpha = 1.f;
					break;
				default:
					Debug.StateAlpha = FMath::Clamp(Passes, 0.f, 1.f);
					break;
				}
			}
			else
			{
				Debug.StateAlpha = Layer.bStatePlayed ? 1.f : 0.f;
			}
			Debug.bStatePlaying = StateDef && StateDef->Kind == EWidgetAnimMachineNodeKind::State && StateDef->Animation
				&& Layer.bStateTrackStarted && Playback != EWidgetAnimMachineStatePlayback::Pose && Layer.PassSeconds > KINDA_SMALL_NUMBER
				&& !(Playback == EWidgetAnimMachineStatePlayback::Once && Layer.bStatePlayed);
			Debug.bExited = Layer.bExited;
			Debug.Weight = Layer.Weight;
			if (Layer.BlendPlayers.Num() > 0)
			{
				Debug.BlendWeights = Layer.BlendWeights;
				Debug.BlendPoint = Layer.BlendPoint;
			}
			Debug.LastActions = Layer.LastActions;

			// What the layer's conditions on other layers make of them now, for the graph to mark them held or not.
			for (const FWidgetAnimMachineTransition& Transition : Def.Transitions)
			{
				for (int32 ConditionIndex = 0; ConditionIndex < Transition.Conditions.Num(); ++ConditionIndex)
				{
					const FWidgetAnimMachineCondition& Condition = Transition.Conditions[ConditionIndex];
					if (Condition.TestsLayerState())
					{
						FWidgetAnimMachineLayerDebug::FLayerCondition& Entry = Debug.LayerConditions.AddDefaulted_GetRef();
						Entry.Transition = Transition.Id;
						Entry.Condition = ConditionIndex;
						Entry.bHolds = EvaluateLayerStateCondition(Instance, Condition);
					}
				}
			}
		}
	}

	bool RefreshParameters(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineContext& Context, bool bOnlyIfInputsChanged)
	{
		const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
		if (!Machine || !Instance.bStarted)
		{
			return false;
		}

		// Rebuilt settings laid over the track's own, one for one: only those that differ, and where each is in its play kept.
		auto LayOver = [](FWidgetAnimPlayer& Player, FName TrackId, TArray<FWidgetAnimSettings>& Rebuilt)
		{
			bool bChanged = false;
			for (FWidgetAnimTrack& Track : Player.Tracks)
			{
				if (Track.AnimId != TrackId || (Track.bFinished && !Track.bHoldWhenFinished))
				{
					continue;
				}
				bool bTrackChanged = false;
				const int32 Count = FMath::Min(Rebuilt.Num(), Track.Settings.Num());
				for (int32 Index = 0; Index < Count; ++Index)
				{
					if (!FWidgetAnimSettings::StaticStruct()->CompareScriptStruct(&Track.Settings[Index], &Rebuilt[Index], PPF_None))
					{
						Track.Settings[Index] = MoveTemp(Rebuilt[Index]);
						bTrackChanged = true;
					}
				}
				if (bTrackChanged)
				{
					Track.bSettled = false;
					bChanged = true;
				}
			}
			return bChanged;
		};
		auto SameValues = [](const TArray<FWidgetAnimParameterValue>& A, const TArray<FWidgetAnimParameterValue>& B)
		{
			if (A.Num() != B.Num())
			{
				return false;
			}
			for (int32 Index = 0; Index < A.Num(); ++Index)
			{
				if (A[Index].Name != B[Index].Name || !A[Index].SameValueAs(B[Index]))
				{
					return false;
				}
			}
			return true;
		};

		bool bAnyChanged = false;
		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num() && LayerIndex < Machine->Layers.Num(); ++LayerIndex)
		{
			const FWidgetAnimMachineLayer& Def = Machine->Layers[LayerIndex];
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			if (!Layer.bStateTrackStarted || !Def.States.IsValidIndex(Layer.Current))
			{
				continue;
			}
			const FReadingLayerScope Reading(Instance, LayerIndex);
			const FWidgetAnimMachineState& State = Def.States[Layer.Current];
			if (State.Kind != EWidgetAnimMachineNodeKind::State || (bOnlyIfInputsChanged && State.ParameterInputs.Num() == 0))
			{
				continue;
			}

			const FName TrackId = GetStateTrackId(State);
			bool bLayerChanged = false;
			auto Rebuild = [&](UWidgetAnimPresetAsset* Animation, FWidgetAnimPlayer& Player, int32 InputsIndex, bool bFollowsProgress)
			{
				if (!Animation || Animation->Parameters.Num() == 0)
				{
					return;
				}
				TArray<FWidgetAnimParameterValue> Values;
				GatherParameterInputs(Instance, State, Values, Animation);
				if (Layer.PlayedParameterInputs.Num() <= InputsIndex)
				{
					Layer.PlayedParameterInputs.SetNum(InputsIndex + 1);
				}
				if (bOnlyIfInputsChanged && SameValues(Values, Layer.PlayedParameterInputs[InputsIndex]))
				{
					return;
				}
				Layer.PlayedParameterInputs[InputsIndex] = Values;
				TArray<FWidgetAnimSettings> Rebuilt;
				BuildAnimationSettings(Animation, Instance.TargetPaths, Context, State.Playback, State.PlaybackSpeed, bFollowsProgress, Rebuilt, Values);
				bLayerChanged |= LayOver(Player, TrackId, Rebuilt);
			};

			if (State.IsBlendState())
			{
				for (int32 SampleIndex = 0; SampleIndex < State.BlendSamples.Num() && SampleIndex < Layer.BlendPlayers.Num(); ++SampleIndex)
				{
					Rebuild(State.BlendSamples[SampleIndex].Animation, Layer.BlendPlayers[SampleIndex], SampleIndex, /*bFollowsProgress=*/false);
				}
			}
			else if (UWidgetAnimPresetAsset* Played = Layer.PlayedAnimation)
			{
				Rebuild(Played, Layer.Player, 0, !State.ProgressInput.IsNone() && Played->IsPercentTime());
			}

			if (bLayerChanged)
			{
				// Posed again: the frame works it out anew, and a pose state is taken to its new end pose.
				Layer.bSettled = false;
				Layer.bPoseSettled = false;
				bAnyChanged = true;
			}
		}
		if (bAnyChanged)
		{
			Instance.bPoseChanged = true;
		}
		return bAnyChanged;
	}

	void ComputeBlendWeights(const FWidgetAnimMachineState& State, const FVector2f& Value, TArray<float>& OutWeights)
	{
		const int32 NumSamples = State.BlendSamples.Num();
		OutWeights.Init(0.f, NumSamples);
		if (NumSamples == 0)
		{
			return;
		}
		if (NumSamples == 1)
		{
			OutWeights[0] = 1.f;
			return;
		}

		if (State.BlendSpace != EWidgetAnimMachineBlendSpace::TwoD)
		{
			// 1D: the two samples either side of the value, mixed linearly; past either end, the end sample alone.
			int32 Below = INDEX_NONE;
			int32 Above = INDEX_NONE;
			for (int32 Index = 0; Index < NumSamples; ++Index)
			{
				const float Position = State.BlendSamples[Index].Position;
				if (Position <= Value.X && (Below == INDEX_NONE || Position > State.BlendSamples[Below].Position))
				{
					Below = Index;
				}
				if (Position >= Value.X && (Above == INDEX_NONE || Position < State.BlendSamples[Above].Position))
				{
					Above = Index;
				}
			}
			if (Below == INDEX_NONE || Above == INDEX_NONE || Below == Above)
			{
				OutWeights[Below != INDEX_NONE ? Below : Above] = 1.f;
				return;
			}
			const float From = State.BlendSamples[Below].Position;
			const float To = State.BlendSamples[Above].Position;
			const float Alpha = To - From > KINDA_SMALL_NUMBER ? FMath::Clamp((Value.X - From) / (To - From), 0.f, 1.f) : 0.f;
			OutWeights[Below] = 1.f - Alpha;
			OutWeights[Above] = Alpha;
			return;
		}

		// 2D. On a sample: that one alone.
		auto PointOf = [&State](int32 Index) { return FVector2f(State.BlendSamples[Index].Position2D); };
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			if (FVector2f::DistSquared(PointOf(Index), Value) < 1e-8f)
			{
				OutWeights[Index] = 1.f;
				return;
			}
		}

		// Inside a triangle of samples: barycentric over the smallest one around it. Samples are few, so every triangle is tried.
		float BestArea = TNumericLimits<float>::Max();
		int32 Best[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
		float BestWeights[3] = { 0.f, 0.f, 0.f };
		const int32 Tried = FMath::Min(NumSamples, 16);
		for (int32 A = 0; A < Tried; ++A)
		{
			for (int32 B = A + 1; B < Tried; ++B)
			{
				for (int32 C = B + 1; C < Tried; ++C)
				{
					const FVector2f PA = PointOf(A);
					const FVector2f PB = PointOf(B);
					const FVector2f PC = PointOf(C);
					const float Denominator = (PB.Y - PC.Y) * (PA.X - PC.X) + (PC.X - PB.X) * (PA.Y - PC.Y);
					if (FMath::Abs(Denominator) < 1e-6f)
					{
						continue;
					}
					const float WA = ((PB.Y - PC.Y) * (Value.X - PC.X) + (PC.X - PB.X) * (Value.Y - PC.Y)) / Denominator;
					const float WB = ((PC.Y - PA.Y) * (Value.X - PC.X) + (PA.X - PC.X) * (Value.Y - PC.Y)) / Denominator;
					const float WC = 1.f - WA - WB;
					const float Area = FMath::Abs(Denominator);
					if (WA >= -1e-4f && WB >= -1e-4f && WC >= -1e-4f && Area < BestArea)
					{
						BestArea = Area;
						Best[0] = A;
						Best[1] = B;
						Best[2] = C;
						BestWeights[0] = FMath::Max(WA, 0.f);
						BestWeights[1] = FMath::Max(WB, 0.f);
						BestWeights[2] = FMath::Max(WC, 0.f);
					}
				}
			}
		}
		if (Best[0] != INDEX_NONE)
		{
			const float Sum = BestWeights[0] + BestWeights[1] + BestWeights[2];
			for (int32 Corner = 0; Corner < 3; ++Corner)
			{
				OutWeights[Best[Corner]] = Sum > 0.f ? BestWeights[Corner] / Sum : 1.f / 3.f;
			}
			return;
		}

		// Outside them all (or all in a line): the nearest three, by inverse distance squared.
		TArray<int32, TInlineAllocator<16>> Order;
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			Order.Add(Index);
		}
		Order.Sort([&PointOf, &Value](int32 Left, int32 Right) { return FVector2f::DistSquared(PointOf(Left), Value) < FVector2f::DistSquared(PointOf(Right), Value); });
		float Total = 0.f;
		for (int32 Rank = 0; Rank < FMath::Min(3, Order.Num()); ++Rank)
		{
			const float Inverse = 1.f / FMath::Max(FVector2f::DistSquared(PointOf(Order[Rank]), Value), 1e-6f);
			OutWeights[Order[Rank]] = Inverse;
			Total += Inverse;
		}
		for (float& Weight : OutWeights)
		{
			Weight = Total > 0.f ? Weight / Total : 0.f;
		}
	}

	void GetBlendWeights(const FWidgetAnimMachineInstance& Instance, FName LayerName, TArray<float>& OutWeights)
	{
		OutWeights.Reset();
		const UWidgetAnimStateMachineAsset* Machine = Instance.Machine.Get();
		const int32 LayerIndex = !Machine ? INDEX_NONE : (LayerName.IsNone() ? 0 : Machine->FindLayerIndex(LayerName));
		if (Instance.Layers.IsValidIndex(LayerIndex) && Instance.Layers[LayerIndex].BlendPlayers.Num() > 0)
		{
			OutWeights = Instance.Layers[LayerIndex].BlendWeights;
		}
	}
}
