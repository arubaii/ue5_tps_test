// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimStateMachineRuntime.h"

#include "Blueprint/UserWidget.h"
#include "Components/CheckBox.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Framework/Application/SlateApplication.h"
#include "WidgetAnimPreset.h"

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
		// Material parameters, registered types and cues write no transform; their channels are their own.
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
				if (Leaving->Evaluator == Source.Evaluator && !Leaving->bIsBaseline && !Source.bIsBaseline)
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

		/** Whether a built-in input is true. */
		bool GetBuiltInValue(const FWidgetAnimMachineInstance& Instance, FName Name)
		{
			if (Name == WidgetAnimMachineInputs::Closing)
			{
				return Instance.bClosing;
			}
			if (Name == WidgetAnimMachineInputs::Hovered)
			{
				return Instance.WidgetState.bHovered;
			}
			if (Name == WidgetAnimMachineInputs::Pressed)
			{
				return Instance.WidgetState.bPressed;
			}
			if (Name == WidgetAnimMachineInputs::Focused)
			{
				return Instance.WidgetState.bFocused;
			}
			if (Name == WidgetAnimMachineInputs::Selected)
			{
				return Instance.WidgetState.bSelected;
			}
			if (Name == WidgetAnimMachineInputs::Disabled)
			{
				return Instance.WidgetState.bDisabled;
			}
			return false;
		}

		/** An input as a number - a Bool or a Trigger as 0 or 1 - and whether there is one of that name. */
		bool ReadAsNumber(const FWidgetAnimMachineInstance& Instance, FName Name, float& OutNumber, bool& OutTruthy)
		{
			if (WidgetAnimMachineInputs::IsBuiltIn(Name))
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
			default:
				OutNumber = Input->Number;
				OutTruthy = !FMath::IsNearlyZero(Input->Number);
				break;
			}
			return true;
		}

		bool EvaluateCondition(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineCondition& Condition)
		{
			float Number = 0.f;
			bool bTruthy = false;
			if (!ReadAsNumber(Instance, Condition.Input, Number, bTruthy))
			{
				// An input the machine doesn't have - removed since the condition was made - never holds.
				return false;
			}

			switch (Condition.Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:         return bTruthy;
			case EWidgetAnimMachineCompare::IsFalse:        return !bTruthy;
			case EWidgetAnimMachineCompare::Fired:          return bTruthy;
			case EWidgetAnimMachineCompare::Equal:          return FMath::IsNearlyEqual(Number, Condition.Value, 1e-4f);
			case EWidgetAnimMachineCompare::NotEqual:       return !FMath::IsNearlyEqual(Number, Condition.Value, 1e-4f);
			case EWidgetAnimMachineCompare::Less:           return Number < Condition.Value;
			case EWidgetAnimMachineCompare::LessOrEqual:    return Number <= Condition.Value;
			case EWidgetAnimMachineCompare::Greater:        return Number > Condition.Value;
			case EWidgetAnimMachineCompare::GreaterOrEqual: return Number >= Condition.Value;
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
		void ReadPlainWidgetState(FWidgetAnimMachineInstance& Instance, FWidgetAnimInputContext& InputContext, FWidgetAnimMachineWidgetState& OutState)
		{
			FVector4 Value;
			const uint8 Used = Instance.UsedBuiltIns;
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Hovered))
			{
				OutState.bHovered = Instance.HoverReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Pressed))
			{
				OutState.bPressed = Instance.PressReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (Used & WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Focused))
			{
				OutState.bFocused = Instance.FocusReader.Sample(InputContext, Value) && Value.X > 0.5;
			}
			if (const UCheckBox* CheckBox = Cast<UCheckBox>(InputContext.MeasuredWidget))
			{
				OutState.bSelected = CheckBox->IsChecked();
			}
			OutState.bDisabled = IsDisabledInTree(InputContext.MeasuredWidget);
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
			if (FSlateApplication::IsInitialized())
			{
				const FVector2D Pointer = FSlateApplication::Get().GetCursorPos();
				InputContext.PointerPosition = FVector2f(static_cast<float>(Pointer.X), static_cast<float>(Pointer.Y));
				InputContext.bHasPointer = true;
			}

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

			// The built-in ones, only when the machine reads any - the Common UI module answers for its buttons.
			if ((Instance.UsedBuiltIns & ~WidgetAnimMachineInputs::GetBuiltInBit(WidgetAnimMachineInputs::Closing)) != 0)
			{
				FWidgetAnimMachineWidgetState State;
				if (!WidgetAnimMachineWidgetStates::ReadFromProviders(Context.Host, State))
				{
					InputContext.MeasuredWidget = Context.Host;
					ReadPlainWidgetState(Instance, InputContext, State);
				}
				const FWidgetAnimMachineWidgetState& Was = Instance.WidgetState;
				if (State.bHovered != Was.bHovered || State.bPressed != Was.bPressed || State.bFocused != Was.bFocused || State.bSelected != Was.bSelected || State.bDisabled != Was.bDisabled)
				{
					Instance.WidgetState = State;
					Instance.bInputsChanged = true;
				}
			}
		}

		// -------------------------------------------------------------------------------------------------
		// Settings
		// -------------------------------------------------------------------------------------------------

		/** An animation's settings as a machine plays it: its groups on the targets' widgets, played as the state says. */
		void BuildAnimationSettings(UWidgetAnimPresetAsset* Animation, const TMap<FName, FName>& TargetPaths, const FWidgetAnimMachineContext& Context,
			EWidgetAnimMachineStatePlayback Playback, float Speed, bool bFollowsProgress, TArray<FWidgetAnimSettings>& OutSettings)
		{
			if (!Animation)
			{
				return;
			}

			FWidgetAnimEntry Use;
			Use.Animation = Animation;
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

		/** Starts the track of the state the layer is in, if it plays anything. */
		void StartStateTrack(FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, FWidgetAnimMachineLayerInstance& Layer, const FWidgetAnimMachineContext& Context)
		{
			Layer.bStateTrackStarted = true;
			Layer.bPoseSettled = false;
			Layer.bHasProgress = false;
			Layer.TimeInState = 0.f;
			Layer.PassSeconds = 0.f;
			Layer.bStatePlayed = false;

			if (!Def.States.IsValidIndex(Layer.Current))
			{
				return;
			}
			const FWidgetAnimMachineState& State = Def.States[Layer.Current];
			if (State.Kind != EWidgetAnimMachineNodeKind::State || !State.Animation)
			{
				Layer.bStatePlayed = true;
				return;
			}

			const bool bFollowsProgress = !State.ProgressInput.IsNone() && State.Animation->IsPercentTime();
			TArray<FWidgetAnimSettings> Settings;
			BuildAnimationSettings(State.Animation, Instance.TargetPaths, Context, State.Playback, State.PlaybackSpeed, bFollowsProgress, Settings);
			if (Settings.Num() == 0)
			{
				Layer.bStatePlayed = true;
				return;
			}

			UWidgetAnimLibrary::StartWidgetAnimTrack(Layer.Player, GetStateTrackId(State), /*SourceIndex=*/0, Settings, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);

			// Shown where its progress already is, rather than sweeping up to it from 0%.
			UpdateProgress(Instance, Def, Layer, /*bSnap=*/true);
		}

		/** Finishes the running transition: the layer is in its state, nothing left blending. */
		void FinishTransition(const FWidgetAnimMachineLayer& Def, FWidgetAnimMachineLayerInstance& Layer)
		{
			Layer.Phase = EWidgetAnimMachineBlendPhase::None;
			Layer.Transition = INDEX_NONE;
			Layer.Previous = INDEX_NONE;
			Layer.BlendElapsed = 0.f;
			Layer.FromPose.Reset();
			Layer.Springs.Reset();
			Layer.bExited = Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Kind == EWidgetAnimMachineNodeKind::Exit;
		}

		void SendStateEvents(const FWidgetAnimMachineInstance& Instance, const FWidgetAnimMachineLayer& Def, int32 Left, int32 Entered, const FWidgetAnimMachineContext& Context)
		{
			if (!Context.OnStateEvent || Context.bDesignTime)
			{
				return;
			}

			const bool bLeftState = Def.States.IsValidIndex(Left) && Def.States[Left].Kind == EWidgetAnimMachineNodeKind::State;
			const bool bEnteredState = Def.States.IsValidIndex(Entered) && Def.States[Entered].Kind == EWidgetAnimMachineNodeKind::State;
			const FName LeftName = bLeftState ? Def.States[Left].Name : FName();
			const FName EnteredName = bEnteredState ? Def.States[Entered].Name : FName();

			FWidgetAnimStateEventInfo Info;
			Info.Machine = Instance.Machine;
			Info.Layer = Def.Name;
			Info.Host = Context.Host;
			if (bLeftState)
			{
				Info.State = LeftName;
				Info.OtherState = EnteredName;
				Info.bEntered = false;
				Context.OnStateEvent(Info);
			}
			if (bEnteredState)
			{
				Info.State = EnteredName;
				Info.OtherState = LeftName;
				Info.bEntered = true;
				Context.OnStateEvent(Info);
			}
		}

		/** Starts Transition: from the pose the layer shows now, to where it points. */
		void StartTransition(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, int32 TransitionIndex, const FWidgetAnimMachineContext& Context)
		{
			const FWidgetAnimMachineLayer& Def = Instance.Machine->Layers[LayerIndex];
			FWidgetAnimMachineLayerInstance& Layer = Instance.Layers[LayerIndex];
			const FWidgetAnimMachineTransition& Transition = Def.Transitions[TransitionIndex];
			const int32 ToIndex = Def.FindStateIndex(Transition.To);
			if (ToIndex == INDEX_NONE)
			{
				return;
			}

			// The pose the widget is in - wherever a transition already running had got to - is where this one starts.
			// Copied out of the tracks it was worked out from, which go now.
			const bool bWasSpringing = Layer.Phase == EWidgetAnimMachineBlendPhase::Blending && Def.Transitions.IsValidIndex(Layer.Transition) && Def.Transitions[Layer.Transition].Blend == EWidgetAnimMachineBlend::Spring;
			StopTracks(Layer);
			Layer.FromPose = Layer.Output;

			const int32 Left = Layer.Current;
			Layer.Previous = Layer.Current;
			Layer.Current = ToIndex;
			Layer.Transition = TransitionIndex;
			Layer.BlendElapsed = 0.f;
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
				BuildAnimationSettings(Transition.TransitionAnimation, Instance.TargetPaths, Context, EWidgetAnimMachineStatePlayback::Once, Transition.TransitionAnimationSpeed, /*bFollowsProgress=*/false, Settings);
				if (Settings.Num() > 0)
				{
					UWidgetAnimLibrary::StartWidgetAnimTrack(Layer.Player, GetTransitionTrackId(Def, Transition), /*SourceIndex=*/1, Settings, /*bIsOut=*/false, /*Speed=*/1.f, /*bRestartIfPlaying=*/true, /*bHoldWhenFinished=*/true);
					Layer.Phase = EWidgetAnimMachineBlendPhase::TransitionAnimation;
				}
			}

			if (Layer.Phase != EWidgetAnimMachineBlendPhase::TransitionAnimation || Layer.Player.Tracks.Num() == 0)
			{
				StartStateTrack(Instance, Def, Layer, Context);
				const bool bInstant = Transition.Blend == EWidgetAnimMachineBlend::Cut
					|| (Transition.Blend != EWidgetAnimMachineBlend::Spring && Transition.Duration <= KINDA_SMALL_NUMBER);
				Layer.Phase = bInstant ? EWidgetAnimMachineBlendPhase::None : EWidgetAnimMachineBlendPhase::Blending;
			}

			if (Layer.Phase == EWidgetAnimMachineBlendPhase::None)
			{
				FinishTransition(Def, Layer);
			}

			SendStateEvents(Instance, Def, Left, ToIndex, Context);
		}

		/** Whether the state the layer is in has played enough for Transition to leave it. */
		bool ExitTimeAllows(const FWidgetAnimMachineLayer& Def, const FWidgetAnimMachineLayerInstance& Layer, const FWidgetAnimMachineTransition& Transition)
		{
			if (Transition.ExitTime == EWidgetAnimMachineExitTime::Immediately || !Def.States.IsValidIndex(Layer.Current))
			{
				return true;
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

		/** The transition the layer takes now - Any State's first, then its state's, each in priority order - or INDEX_NONE. */
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

			const FWidgetAnimMachineState& Current = Def.States[Layer.Current];

			// At Exit the layer waits to be brought back in through Entry; nothing leaves it.
			if (Current.Kind == EWidgetAnimMachineNodeKind::Exit)
			{
				return INDEX_NONE;
			}

			// Any State's, unless the layer is at Entry, which picks the first state on its own. Never back into the state the
			// layer is in or heading to: a held condition would start it again every frame.
			if (Current.Kind != EWidgetAnimMachineNodeKind::Entry)
			{
				const int32 AnyIndex = Def.FindSpecialIndex(EWidgetAnimMachineNodeKind::AnyState);
				const FGuid AnyId = Def.States.IsValidIndex(AnyIndex) ? Def.States[AnyIndex].Id : FGuid();
				for (int32 Index = 0; AnyId.IsValid() && Index < Def.Transitions.Num(); ++Index)
				{
					const FWidgetAnimMachineTransition& Transition = Def.Transitions[Index];
					if (Transition.bEnabled && Transition.From == AnyId && Transition.To != Current.Id && ConditionsHold(Instance, Transition) && ExitTimeAllows(Def, Layer, Transition))
					{
						return Index;
					}
				}
			}

			for (int32 Index = 0; Index < Def.Transitions.Num(); ++Index)
			{
				const FWidgetAnimMachineTransition& Transition = Def.Transitions[Index];
				if (Transition.bEnabled && Transition.From == Current.Id && ConditionsHold(Instance, Transition) && ExitTimeAllows(Def, Layer, Transition))
				{
					return Index;
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
				StartTransition(Instance, LayerIndex, Chosen, Context);
				if (Instance.Layers[LayerIndex].Phase != EWidgetAnimMachineBlendPhase::None)
				{
					return;
				}
				Visited.Add(Instance.Layers[LayerIndex].Current);
			}
		}

		/** Puts a layer at its Entry and takes Entry's transitions. */
		void EnterLayer(FWidgetAnimMachineInstance& Instance, int32 LayerIndex, const FWidgetAnimMachineContext& Context)
		{
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

			// Closing, and waiting for a way to Exit that never comes - its conditions can't hold - would hold the screen open
			// for good. A few seconds is long enough for any exit worth waiting for.
			if (Instance.bClosing && !Layer.bExited && Def.HasExitPath() && !(Def.Transitions.IsValidIndex(Layer.Transition) && Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Kind == EWidgetAnimMachineNodeKind::Exit))
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
				Layer.TimeInState += DeltaTime;
				return;
			}

			// A pose state shows its end pose: its track is played to its end at once - once its staggers have fanned out,
			// which a zero-time evaluation sees to first.
			if (Layer.bStateTrackStarted && !Layer.bPoseSettled && Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Playback == EWidgetAnimMachineStatePlayback::Pose)
			{
				// Once: a track following a Progress Input never finishes, and is left to follow it from its end pose.
				Layer.bPoseSettled = true;
				for (FWidgetAnimTrack& Track : Layer.Player.Tracks)
				{
					if (!Track.bFinished)
					{
						if (Track.bNeedsExpansion)
						{
							FWidgetAnimResult Discarded;
							TArray<FName> Ignored;
							UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Context.Host, Layer.Player, Baselines, 0.f, Ignored, Discarded);
						}
						UWidgetAnimLibrary::SettleWidgetAnimTrack(Track, Baselines);
					}
				}
			}

			FWidgetAnimMachinePose Evaluated;
			TArray<FName> JustFinished;
			UWidgetAnimLibrary::EvaluateWidgetAnimPlayer(Context.Host, Layer.Player, Baselines, DeltaTime, JustFinished, Evaluated.Result);
			NormalizeTargets(Evaluated.Result, Context.Host);
			ComputeMasks(Layer.Player, Context.Host, Evaluated);

			// How far through its state the layer is, for Exit Time.
			const FWidgetAnimTrack* StateTrack = nullptr;
			const bool bWasPlayed = Layer.bStatePlayed;
			const EWidgetAnimMachineBlendPhase PhaseBefore = Layer.Phase;
			if (Layer.bStateTrackStarted && Def.States.IsValidIndex(Layer.Current))
			{
				const FName TrackId = GetStateTrackId(Def.States[Layer.Current]);
				StateTrack = Layer.Player.Tracks.FindByPredicate([TrackId](const FWidgetAnimTrack& Track) { return Track.AnimId == TrackId && Track.SourceIndex == 0; });
				Layer.TimeInState += DeltaTime;
				if (StateTrack && !StateTrack->bNeedsExpansion && Layer.PassSeconds <= 0.f)
				{
					Layer.PassSeconds = UWidgetAnimLibrary::GetStackPassDuration(StateTrack->Settings) / FMath::Max(StateTrack->Speed, 0.01f);
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
					const float Duration = FMath::Max(Transition->Duration, KINDA_SMALL_NUMBER);
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
				const float BlendIn = Transition ? FMath::Max(Transition->Duration, 0.f) : 0.f;
				const float Linear = BlendIn > KINDA_SMALL_NUMBER ? FMath::Clamp(Layer.BlendElapsed / BlendIn, 0.f, 1.f) : 1.f;
				const float Alpha = Transition ? WidgetAnimEasing::Evaluate(Transition->Easing, Transition->CustomCurve, Linear) : 1.f;
				FWidgetAnimMachinePose Blended;
				BlendPoses(Layer.FromPose, Evaluated, Alpha, Context, Blended);
				Layer.Output = MoveTemp(Blended);

				const bool bAnimationDone = Layer.Player.Tracks.Num() == 0 || !Layer.Player.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return !Track.bFinished; });
				if (bAnimationDone && Linear >= 1.f)
				{
					const bool bToExit = Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Kind == EWidgetAnimMachineNodeKind::Exit;
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
						Layer.Phase = Transition && Transition->Duration > KINDA_SMALL_NUMBER ? EWidgetAnimMachineBlendPhase::BlendingAfterAnimation : EWidgetAnimMachineBlendPhase::None;
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
			Layer.bSettled = !bJustArrived
				&& Layer.Phase == EWidgetAnimMachineBlendPhase::None
				&& !Layer.Player.Tracks.ContainsByPredicate([](const FWidgetAnimTrack& Track) { return !Track.bFinished || Track.bNeedsExpansion; })
				&& !UWidgetAnimLibrary::HasValueInputs(Layer.Player);
		}

		/** Writes Pose into Acc by Weight: over what is there (Override), or added on top (Additive). Collects the widgets it posed. */
		void ApplyLayer(FWidgetAnimMachinePose& Acc, const FWidgetAnimMachinePose& Pose, EWidgetAnimMachineLayerBlend Mode, float Weight, const FWidgetAnimMachineContext& Context,
			TArray<TPair<TWeakObjectPtr<UWidget>, FWidgetAnimPartKey>>& OutPosed)
		{
			if (Weight <= 0.f)
			{
				return;
			}

			for (int32 Index = 0; Index < Pose.Result.TargetedTransforms.Num(); ++Index)
			{
				const uint8 Mask = Pose.Masks.IsValidIndex(Index) ? Pose.Masks[Index] : 0;
				if (Mask == 0)
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
					// As a track on top: opacity and scale multiply, the rest adds - each only as strongly as the weight.
					Into.Opacity *= FMath::Lerp(1.f, Source.Opacity, Weight);
					Into.Scale *= FMath::Lerp(FVector2D(1.0, 1.0), Source.Scale, static_cast<double>(Weight));
					Into.Translation += Source.Translation * Weight;
					Into.Shear += Source.Shear * Weight;
					Into.Angle += Source.Angle * Weight;
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
				FWidgetAnimAuthoritativeWrite* Existing = Acc.Result.AuthoritativeWrites.FindByPredicate([&Source](const FWidgetAnimAuthoritativeWrite& Candidate)
				{
					return Candidate.TargetWidget == Source.TargetWidget && Candidate.Channel == Source.Channel;
				});
				if (Source.bIsBaseline && Existing)
				{
					continue;
				}
				if (Existing && Weight < 1.f && Existing->Evaluator == Source.Evaluator && !Existing->bIsBaseline)
				{
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

		/** Which built-in inputs a machine reads. */
		uint8 FindUsedBuiltIns(const UWidgetAnimStateMachineAsset& Machine)
		{
			uint8 Used = 0;
			for (const FWidgetAnimMachineLayer& Layer : Machine.Layers)
			{
				for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
				{
					for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
					{
						Used |= WidgetAnimMachineInputs::GetBuiltInBit(Condition.Input);
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
		Instance.HoverReader.State = EWidgetAnimInteractionState::Hovered;
		Instance.PressReader.State = EWidgetAnimInteractionState::Pressed;
		Instance.FocusReader.State = EWidgetAnimInteractionState::Focused;

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

		// What the widget is now - hovered already, disabled already - before Entry picks the first state.
		ReadInputs(Instance, Context, 0.f);

		for (int32 LayerIndex = 0; LayerIndex < Instance.Layers.Num(); ++LayerIndex)
		{
			EnterLayer(Instance, LayerIndex, Context);
		}
	}

	void Stop(FWidgetAnimMachineInstance& Instance)
	{
		for (FWidgetAnimMachineLayerInstance& Layer : Instance.Layers)
		{
			UWidgetAnimLibrary::StopWidgetAnimTracks(Layer.Player, TArray<FName>(), /*bRemoveTracks=*/true);
			Layer = FWidgetAnimMachineLayerInstance();
		}
		Instance.bStarted = false;
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

		// Triggers are one-frame pulses: spent whether or not anything took them.
		for (FWidgetAnimMachineInputState& Input : Instance.Inputs)
		{
			Input.bFired = false;
		}
		Instance.bInputsChanged = false;
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
					ApplyLayer(Acc, Layer.Output, Instance.Machine->Layers[LayerIndex].BlendMode, Layer.Weight, Context, Posed);
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
			UWidgetAnimLibrary::ReleaseFinishedTracks(Layer.Player);
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
			const bool bAtExit = Def.States.IsValidIndex(Layer.Current) && Def.States[Layer.Current].Kind == EWidgetAnimMachineNodeKind::Exit;
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
		if (!State || State->Type != EWidgetAnimMachineInputType::Number)
		{
			return false;
		}
		if (State->Number != Value)
		{
			State->Number = Value;
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
			return SetBool(Instance, Input, !FMath::IsNearlyZero(Value));
		case EWidgetAnimMachineInputType::Trigger:
			return FMath::IsNearlyZero(Value) || FireTrigger(Instance, Input);
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

	void SetBuiltIn(FWidgetAnimMachineInstance& Instance, FName Input, bool bValue)
	{
		FWidgetAnimMachineWidgetState& State = Instance.WidgetState;
		bool* Field = Input == WidgetAnimMachineInputs::Hovered ? &State.bHovered
			: Input == WidgetAnimMachineInputs::Pressed ? &State.bPressed
			: Input == WidgetAnimMachineInputs::Focused ? &State.bFocused
			: Input == WidgetAnimMachineInputs::Selected ? &State.bSelected
			: Input == WidgetAnimMachineInputs::Disabled ? &State.bDisabled
			: Input == WidgetAnimMachineInputs::Closing ? &Instance.bClosing
			: nullptr;
		if (Field && *Field != bValue)
		{
			*Field = bValue;
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
			const int32 Current = Instance.Layers[LayerIndex].Current;
			if (Def.States.IsValidIndex(Current) && Def.States[Current].Name == State)
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
			const int32 LayerTaken = UWidgetAnimLibrary::SetWidgetAnimTrackValue(Layer.Player, Id, Value, bSnap);
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
				if (State.Kind == EWidgetAnimMachineNodeKind::State && State.Animation)
				{
					BuildAnimationSettings(State.Animation, TargetPaths, Context, EWidgetAnimMachineStatePlayback::Once, 1.f, /*bFollowsProgress=*/false, OutSettings);
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
			Debug.PreviousState = Def.States.IsValidIndex(Layer.Previous) ? Def.States[Layer.Previous].Id : FGuid();
			Debug.ActiveTransition = Def.Transitions.IsValidIndex(Layer.Transition) ? Def.Transitions[Layer.Transition].Id : FGuid();
			if (Def.Transitions.IsValidIndex(Layer.Transition))
			{
				const float Seconds = FMath::Max(Def.Transitions[Layer.Transition].GetBlendSeconds(), KINDA_SMALL_NUMBER);
				Debug.TransitionAlpha = FMath::Clamp(Layer.BlendElapsed / Seconds, 0.f, 1.f);
			}
			Debug.StateAlpha = Layer.PassSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(Layer.TimeInState / Layer.PassSeconds, 0.f, 1.f) : (Layer.bStatePlayed ? 1.f : 0.f);
			Debug.bExited = Layer.bExited;
			Debug.Weight = Layer.Weight;
		}
	}
}
