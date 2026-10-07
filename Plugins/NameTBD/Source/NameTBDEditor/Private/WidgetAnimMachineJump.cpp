// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimMachineJump.h"

#include "NameTBD/WidgetAnimation/WidgetAnimStateMachine.h"

#define LOCTEXT_NAMESPACE "WidgetAnimMachineJump"

namespace WidgetAnimMachineJumpLocal
{
	using namespace WidgetAnimMachineJump;

	/** Works out one step's inputs: the defaults, then what a transition needs, then what keeps others from being taken. */
	struct FSolver
	{
		const UWidgetAnimStateMachineAsset& Machine;

		/** The inputs as the step before left them - as they are now, for the first: what Increased, Decreased and Changed compare with. */
		TMap<FName, float> Previous;
		TMap<FName, FVector2D> PreviousVectors;

		FInputPlan Plan;

		/** Inputs the transition being taken tests: never changed to keep another transition from being taken. */
		TSet<FName> Required;

		FSolver(const UWidgetAnimStateMachineAsset& InMachine, const TMap<FName, float>& InCurrent, const TMap<FName, FVector2D>& InCurrentVectors)
			: Machine(InMachine), Previous(InCurrent), PreviousVectors(InCurrentVectors)
		{
		}

		/** The step is set: the next one moves from it. */
		void Commit()
		{
			for (const TPair<FName, float>& Value : Plan.Values)
			{
				Previous.Add(Value.Key, Value.Value);
			}
			for (const TPair<FName, FVector2D>& Vector : Plan.Vectors)
			{
				PreviousVectors.Add(Vector.Key, Vector.Value);
			}
		}

		void SetDefaults()
		{
			Plan = FInputPlan();
			Required.Reset();
			for (const FWidgetAnimMachineInput& Input : Machine.Inputs)
			{
				switch (Input.Type)
				{
				case EWidgetAnimMachineInputType::Bool:     Plan.Values.Add(Input.Name, Input.bDefaultBool ? 1.f : 0.f); break;
				case EWidgetAnimMachineInputType::Number:   Plan.Values.Add(Input.Name, Input.DefaultNumber); break;
				case EWidgetAnimMachineInputType::Integer:  Plan.Values.Add(Input.Name, static_cast<float>(Input.DefaultInteger)); break;
				case EWidgetAnimMachineInputType::Choice:   Plan.Values.Add(Input.Name, 0.f); break;
				case EWidgetAnimMachineInputType::Hold:     Plan.Values.Add(Input.Name, 0.f); break;
				case EWidgetAnimMachineInputType::Vector2D: Plan.Vectors.Add(Input.Name, Input.DefaultVector2D); break;
				default: break;
				}
			}
			for (const FName BuiltIn : WidgetAnimMachineInputs::GetBuiltIns())
			{
				if (BuiltIn != WidgetAnimMachineInputs::Clicked)
				{
					Plan.BuiltIns.Add(BuiltIn, false);
				}
			}
		}

		static float VectorPart(const FVector2D& Vector, EWidgetAnimMachineVectorPart Part)
		{
			switch (Part)
			{
			case EWidgetAnimMachineVectorPart::X: return static_cast<float>(Vector.X);
			case EWidgetAnimMachineVectorPart::Y: return static_cast<float>(Vector.Y);
			default:                              return static_cast<float>(Vector.Size());
			}
		}

		/** A Choice's option by its number - None when it isn't a Choice or has no such option. */
		static FName OptionOf(const FWidgetAnimMachineInput* Input, float Number)
		{
			const int32 Index = FMath::RoundToInt32(Number);
			return Input && Input->Type == EWidgetAnimMachineInputType::Choice && Input->Options.IsValidIndex(Index) ? Input->Options[Index] : NAME_None;
		}

		/** A Choice's number for an option, by name - its first when it has no such option. */
		static int32 IndexOfOption(const FWidgetAnimMachineInput* Input, FName Option)
		{
			return Input ? FMath::Max(Input->Options.IndexOfByKey(Option), 0) : 0;
		}

		/** The input as the plan has it, read as the runtime reads it: a number, and whether it is "true". */
		bool Read(FName Name, EWidgetAnimMachineVectorPart Part, float& OutNumber, bool& OutTruthy) const
		{
			if (WidgetAnimMachineInputs::IsBuiltIn(Name))
			{
				OutTruthy = Name == WidgetAnimMachineInputs::Clicked ? Plan.Fire.Contains(Name) : Plan.BuiltIns.FindRef(Name);
				OutNumber = OutTruthy ? 1.f : 0.f;
				return true;
			}
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Name);
			if (!Input)
			{
				return false;
			}
			switch (Input->Type)
			{
			case EWidgetAnimMachineInputType::Trigger:
				OutTruthy = Plan.Fire.Contains(Name);
				OutNumber = OutTruthy ? 1.f : 0.f;
				return true;
			case EWidgetAnimMachineInputType::Vector2D:
			{
				const FVector2D Vector = Plan.Vectors.FindRef(Name);
				OutNumber = VectorPart(Vector, Part);
				OutTruthy = !Vector.IsNearlyZero();
				return true;
			}
			default:
				OutNumber = Plan.Values.FindRef(Name);
				OutTruthy = !FMath::IsNearlyZero(OutNumber);
				return true;
			}
		}

		static bool Points(const FVector2D& Vector, EWidgetAnimMachineDirection Direction, float DeadZone)
		{
			if (Vector.Size() < FMath::Max(DeadZone, UE_KINDA_SMALL_NUMBER))
			{
				return false;
			}
			const double Across = FMath::Abs(Vector.X);
			const double Down = FMath::Abs(Vector.Y);
			switch (Direction)
			{
			case EWidgetAnimMachineDirection::Up:    return Vector.Y < 0.0 && Down > Across;
			case EWidgetAnimMachineDirection::Down:  return Vector.Y > 0.0 && Down > Across;
			case EWidgetAnimMachineDirection::Left:  return Vector.X < 0.0 && Across >= Down;
			default:                                  return Vector.X > 0.0 && Across >= Down;
			}
		}

		/** Whether a condition holds on the plan - as the runtime would judge it the frame the step is set. */
		bool Holds(const FWidgetAnimMachineCondition& Condition) const
		{
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Condition.Input);
			const bool bAgainstInput = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input;

			if (Condition.Compare == EWidgetAnimMachineCompare::Increased || Condition.Compare == EWidgetAnimMachineCompare::Decreased
				|| Condition.Compare == EWidgetAnimMachineCompare::Changed)
			{
				// Against what the step before left: as the runtime compares with the frame before.
				float Now = 0.f;
				float Before = 0.f;
				if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
				{
					const FVector2D NowVector = Plan.Vectors.FindRef(Condition.Input);
					const FVector2D BeforeVector = PreviousVectors.FindRef(Condition.Input);
					if (Condition.Compare == EWidgetAnimMachineCompare::Changed)
					{
						return !NowVector.Equals(BeforeVector, 1e-4);
					}
					Now = VectorPart(NowVector, Condition.Part);
					Before = VectorPart(BeforeVector, Condition.Part);
				}
				else if (Input)
				{
					Now = Plan.Values.FindRef(Condition.Input);
					Before = Previous.FindRef(Condition.Input);
				}
				else
				{
					return false;
				}
				switch (Condition.Compare)
				{
				case EWidgetAnimMachineCompare::Increased: return Now > Before + 1e-4f;
				case EWidgetAnimMachineCompare::Decreased: return Now < Before - 1e-4f;
				default:                                    return !FMath::IsNearlyEqual(Now, Before, 1e-4f);
				}
			}
			if (Input && Input->Type == EWidgetAnimMachineInputType::Choice)
			{
				// By name, as the runtime compares: the option typed, or the other Choice's option.
				const FName Mine = OptionOf(Input, Plan.Values.FindRef(Condition.Input));
				const FName Theirs = bAgainstInput
					? OptionOf(Machine.FindInput(Condition.OtherInput), Plan.Values.FindRef(Condition.OtherInput))
					: Condition.Option;
				return Condition.Compare == EWidgetAnimMachineCompare::Equal ? Mine == Theirs
					: Condition.Compare == EWidgetAnimMachineCompare::NotEqual ? Mine != Theirs : false;
			}
			if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D && Condition.Compare == EWidgetAnimMachineCompare::Points)
			{
				return Points(Plan.Vectors.FindRef(Condition.Input), Condition.Direction, Condition.DeadZone);
			}

			float Number = 0.f;
			bool bTruthy = false;
			if (!Read(Condition.Input, Condition.Part, Number, bTruthy))
			{
				return false;
			}
			float Against = Condition.Value;
			if (bAgainstInput)
			{
				bool bOther = false;
				if (!Read(Condition.OtherInput, Condition.OtherPart, Against, bOther))
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

		bool AllHold(const FWidgetAnimMachineTransition& Transition) const
		{
			for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
			{
				if (!Holds(Condition))
				{
					return false;
				}
			}
			return true;
		}

		/** A step for a number on either side of a comparison: whole for an Integer, else a tenth or a tenth of its size. */
		float StepFor(const FWidgetAnimMachineInput* Input, float Around) const
		{
			if (Input && (Input->Type == EWidgetAnimMachineInputType::Integer || Input->Type == EWidgetAnimMachineInputType::Bool))
			{
				return 1.f;
			}
			return FMath::Max(0.1f, FMath::Abs(Around) * 0.1f);
		}

		/** What a comparison is made against, on the plan. */
		float AgainstOf(const FWidgetAnimMachineCondition& Condition) const
		{
			float Against = Condition.Value;
			if (Condition.CompareTo == EWidgetAnimMachineCompareTo::Input)
			{
				bool bUnused = false;
				Read(Condition.OtherInput, Condition.OtherPart, Against, bUnused);
			}
			return Against;
		}

		/** Writes a number into an input as the plan holds it - a part of a Vector 2D, or the value of anything else. */
		void WriteNumber(const FWidgetAnimMachineCondition& Condition, float Number)
		{
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Condition.Input);
			if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
			{
				FVector2D& Vector = Plan.Vectors.FindOrAdd(Condition.Input);
				switch (Condition.Part)
				{
				case EWidgetAnimMachineVectorPart::X: Vector.X = Number; break;
				case EWidgetAnimMachineVectorPart::Y: Vector.Y = Number; break;
				default: Vector = FVector2D(FMath::Max(Number, 0.f), 0.0); break;
				}
				return;
			}
			if (WidgetAnimMachineInputs::IsBuiltIn(Condition.Input))
			{
				Plan.BuiltIns.Add(Condition.Input, Number > 0.5f);
				return;
			}
			Plan.Values.Add(Condition.Input, Number);
		}

		/** Makes the conditions of a transition on one input hold together: bounds gathered, a value picked inside them. */
		void SolveNumbers(const TArray<const FWidgetAnimMachineCondition*>& Conditions)
		{
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Conditions[0]->Input);
			TOptional<float> Equal;
			TArray<float, TInlineAllocator<4>> NotEqual;
			float Low = -FLT_MAX;
			float High = FLT_MAX;
			bool bLowStrict = false;
			bool bHighStrict = false;
			for (const FWidgetAnimMachineCondition* Condition : Conditions)
			{
				const float Against = AgainstOf(*Condition);
				switch (Condition->Compare)
				{
				case EWidgetAnimMachineCompare::Equal:          Equal = Against; break;
				case EWidgetAnimMachineCompare::NotEqual:       NotEqual.Add(Against); break;
				case EWidgetAnimMachineCompare::Greater:        if (Against >= Low) { Low = Against; bLowStrict = true; } break;
				case EWidgetAnimMachineCompare::GreaterOrEqual: if (Against > Low) { Low = Against; bLowStrict = false; } break;
				case EWidgetAnimMachineCompare::Less:           if (Against <= High) { High = Against; bHighStrict = true; } break;
				case EWidgetAnimMachineCompare::LessOrEqual:    if (Against < High) { High = Against; bHighStrict = false; } break;
				default: break;
				}
			}

			float Number = 0.f;
			bool bUnused = false;
			Read(Conditions[0]->Input, Conditions[0]->Part, Number, bUnused);
			if (Equal.IsSet())
			{
				Number = Equal.GetValue();
			}
			else
			{
				const bool bHasLow = Low > -FLT_MAX;
				const bool bHasHigh = High < FLT_MAX;
				const float Step = StepFor(Input, bHasLow ? Low : (bHasHigh ? High : Number));
				const bool bInside = (!bHasLow || (bLowStrict ? Number > Low : Number >= Low)) && (!bHasHigh || (bHighStrict ? Number < High : Number <= High));
				if (!bInside)
				{
					if (bHasLow && bHasHigh)
					{
						Number = (Low + High) * 0.5f;
						if (Step >= 1.f)
						{
							Number = FMath::RoundToFloat(Number);
							if (bLowStrict && Number <= Low) { Number = Low + 1.f; }
							if (bHighStrict && Number >= High) { Number = High - 1.f; }
						}
					}
					else if (bHasLow)
					{
						Number = bLowStrict ? Low + Step : Low;
					}
					else if (bHasHigh)
					{
						Number = bHighStrict ? High - Step : High;
					}
				}
				for (int32 Guard = 0; Guard < 8 && NotEqual.ContainsByPredicate([Number](float Value) { return FMath::IsNearlyEqual(Value, Number, 1e-4f); }); ++Guard)
				{
					Number += Step;
				}
			}
			WriteNumber(*Conditions[0], Number);
		}

		/** Makes one condition hold - all but the number comparisons, which SolveNumbers takes together. */
		void MakeHold(const FWidgetAnimMachineCondition& Condition)
		{
			const FName Name = Condition.Input;
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Name);
			const bool bBuiltIn = WidgetAnimMachineInputs::IsBuiltIn(Name);

			switch (Condition.Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:
			case EWidgetAnimMachineCompare::Fired:
				if (bBuiltIn)
				{
					if (Name == WidgetAnimMachineInputs::Clicked)
					{
						Plan.Fire.Add(Name);
					}
					else
					{
						Plan.BuiltIns.Add(Name, true);
					}
				}
				else if (Input && Input->Type == EWidgetAnimMachineInputType::Trigger)
				{
					Plan.Fire.Add(Name);
				}
				else if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
				{
					Plan.Vectors.Add(Name, FVector2D(1.0, 0.0));
				}
				else
				{
					// A Bool on; a Press And Hold held - Fired once it has been held its whole Hold Time.
					Plan.Values.Add(Name, 1.f);
				}
				return;

			case EWidgetAnimMachineCompare::IsFalse:
				if (bBuiltIn)
				{
					Plan.BuiltIns.Add(Name, false);
				}
				else if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
				{
					Plan.Vectors.Add(Name, FVector2D::ZeroVector);
				}
				else if (!(Input && Input->Type == EWidgetAnimMachineInputType::Trigger))
				{
					Plan.Values.Add(Name, 0.f);
				}
				return;

			case EWidgetAnimMachineCompare::Points:
			{
				const float Length = FMath::Max(1.f, Condition.DeadZone * 1.5f);
				FVector2D Direction(1.0, 0.0);
				switch (Condition.Direction)
				{
				case EWidgetAnimMachineDirection::Up:   Direction = FVector2D(0.0, -1.0); break;
				case EWidgetAnimMachineDirection::Down: Direction = FVector2D(0.0, 1.0); break;
				case EWidgetAnimMachineDirection::Left: Direction = FVector2D(-1.0, 0.0); break;
				default: break;
				}
				Plan.Vectors.Add(Name, Direction * Length);
				return;
			}

			case EWidgetAnimMachineCompare::Increased:
			case EWidgetAnimMachineCompare::Decreased:
			case EWidgetAnimMachineCompare::Changed:
			{
				// Moved from where the step before left it, the frame the step is set.
				const float Delta = Condition.Compare == EWidgetAnimMachineCompare::Decreased ? -1.f : 1.f;
				if (Input && Input->Type == EWidgetAnimMachineInputType::Vector2D)
				{
					FVector2D Vector = PreviousVectors.FindRef(Name);
					if (Condition.Part == EWidgetAnimMachineVectorPart::Y)
					{
						Vector.Y += Delta;
					}
					else
					{
						Vector.X += Delta;
					}
					Plan.Vectors.Add(Name, Vector);
				}
				else if (Input && Input->Type == EWidgetAnimMachineInputType::Choice)
				{
					const int32 Count = FMath::Max(Input->Options.Num(), 1);
					Plan.Values.Add(Name, static_cast<float>((FMath::RoundToInt32(Previous.FindRef(Name)) + 1) % Count));
				}
				else
				{
					Plan.Values.Add(Name, Previous.FindRef(Name) + Delta);
				}
				return;
			}

			case EWidgetAnimMachineCompare::Equal:
			case EWidgetAnimMachineCompare::NotEqual:
				if (Input && Input->Type == EWidgetAnimMachineInputType::Choice)
				{
					// The option by name - another Choice's as the plan has it - then, for Is Not, the next one.
					const FName WantedOption = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input
						? OptionOf(Machine.FindInput(Condition.OtherInput), Plan.Values.FindRef(Condition.OtherInput))
						: Condition.Option;
					int32 Wanted = IndexOfOption(Input, WantedOption);
					if (Condition.Compare == EWidgetAnimMachineCompare::NotEqual)
					{
						Wanted = Input->Options.Num() > 1 ? (Wanted + 1) % Input->Options.Num() : 0;
					}
					Plan.Values.Add(Name, static_cast<float>(Wanted));
				}
				return;

			default:
				return;
			}
		}

		/** Makes every condition of Transition hold, and marks its inputs as needed. */
		void MakeTransitionHold(const FWidgetAnimMachineTransition& Transition)
		{
			// Two passes: what is compared with a fixed value first, then what is compared with another input - so the other
			// input already has its value when it is followed.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				const bool bAgainstInputs = Pass == 1;
				TMap<FName, TArray<const FWidgetAnimMachineCondition*>> Numbers;
				for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
				{
					const bool bAgainstInput = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input && !Condition.OtherInput.IsNone();
					if (Pass == 0)
					{
						// Both sides of a comparison between inputs are needed: blocking moves neither.
						Required.Add(Condition.Input);
						if (bAgainstInput)
						{
							Required.Add(Condition.OtherInput);
						}
					}
					if (bAgainstInput != bAgainstInputs)
					{
						continue;
					}
					const FWidgetAnimMachineInput* Input = Machine.FindInput(Condition.Input);
					const bool bChoice = Input && Input->Type == EWidgetAnimMachineInputType::Choice;
					const bool bComparison = Condition.Compare == EWidgetAnimMachineCompare::Equal || Condition.Compare == EWidgetAnimMachineCompare::NotEqual
						|| Condition.Compare == EWidgetAnimMachineCompare::Less || Condition.Compare == EWidgetAnimMachineCompare::LessOrEqual
						|| Condition.Compare == EWidgetAnimMachineCompare::Greater || Condition.Compare == EWidgetAnimMachineCompare::GreaterOrEqual;
					if (bComparison && !bChoice)
					{
						Numbers.FindOrAdd(Condition.Input).Add(&Condition);
					}
					else
					{
						MakeHold(Condition);
					}
				}
				for (const TPair<FName, TArray<const FWidgetAnimMachineCondition*>>& Group : Numbers)
				{
					SolveNumbers(Group.Value);
				}
			}
		}

		/** Makes one condition fail, leaving the needed inputs alone. False when it can't be. */
		bool MakeFail(const FWidgetAnimMachineCondition& Condition)
		{
			const FName Name = Condition.Input;
			if (Required.Contains(Name))
			{
				return false;
			}
			const FWidgetAnimMachineInput* Input = Machine.FindInput(Name);
			const bool bBuiltIn = WidgetAnimMachineInputs::IsBuiltIn(Name);
			const EWidgetAnimMachineInputType Type = Input ? Input->Type : EWidgetAnimMachineInputType::Bool;

			switch (Condition.Compare)
			{
			case EWidgetAnimMachineCompare::IsTrue:
			case EWidgetAnimMachineCompare::Fired:
				Plan.Fire.Remove(Name);
				if (bBuiltIn)
				{
					Plan.BuiltIns.Add(Name, false);
				}
				else if (Type == EWidgetAnimMachineInputType::Vector2D)
				{
					Plan.Vectors.Add(Name, FVector2D::ZeroVector);
				}
				else if (Type != EWidgetAnimMachineInputType::Trigger)
				{
					Plan.Values.Add(Name, 0.f);
				}
				break;
			case EWidgetAnimMachineCompare::IsFalse:
				if (bBuiltIn)
				{
					Plan.BuiltIns.Add(Name, true);
				}
				else if (Type == EWidgetAnimMachineInputType::Vector2D)
				{
					Plan.Vectors.Add(Name, FVector2D(1.0, 0.0));
				}
				else if (Type == EWidgetAnimMachineInputType::Trigger)
				{
					return false;
				}
				else
				{
					Plan.Values.Add(Name, 1.f);
				}
				break;
			case EWidgetAnimMachineCompare::Points:
				Plan.Vectors.Add(Name, FVector2D::ZeroVector);
				break;
			case EWidgetAnimMachineCompare::Increased:
			case EWidgetAnimMachineCompare::Decreased:
			case EWidgetAnimMachineCompare::Changed:
				// Left where the step before left it: no change to see.
				if (Type == EWidgetAnimMachineInputType::Vector2D)
				{
					Plan.Vectors.Add(Name, PreviousVectors.FindRef(Name));
				}
				else if (Input)
				{
					Plan.Values.Add(Name, Previous.FindRef(Name));
				}
				else
				{
					return false;
				}
				break;
			default:
			{
				if (Type == EWidgetAnimMachineInputType::Choice)
				{
					const int32 Count = Input ? Input->Options.Num() : 0;
					const FName Against = Condition.CompareTo == EWidgetAnimMachineCompareTo::Input
						? OptionOf(Machine.FindInput(Condition.OtherInput), Plan.Values.FindRef(Condition.OtherInput))
						: Condition.Option;
					const int32 Option = IndexOfOption(Input, Against);
					if (Condition.Compare == EWidgetAnimMachineCompare::Equal)
					{
						Plan.Values.Add(Name, static_cast<float>(Count > 1 ? (Option + 1) % Count : 0));
					}
					else
					{
						Plan.Values.Add(Name, static_cast<float>(Option));
					}
					break;
				}
				const float Against = AgainstOf(Condition);
				const float Step = StepFor(Input, Against);
				switch (Condition.Compare)
				{
				case EWidgetAnimMachineCompare::Equal:          WriteNumber(Condition, Against + Step); break;
				case EWidgetAnimMachineCompare::NotEqual:       WriteNumber(Condition, Against); break;
				case EWidgetAnimMachineCompare::Less:
				case EWidgetAnimMachineCompare::LessOrEqual:    WriteNumber(Condition, Against + Step); break;
				case EWidgetAnimMachineCompare::Greater:
				case EWidgetAnimMachineCompare::GreaterOrEqual: WriteNumber(Condition, Against - Step); break;
				default: return false;
				}
				break;
			}
			}
			return !Holds(Condition);
		}

		/** Keeps a transition from being taken, if it would be and can be kept from it without undoing what is needed. True if the plan changed. */
		bool Block(const FWidgetAnimMachineTransition& Transition)
		{
			if (!Transition.bEnabled || !AllHold(Transition))
			{
				return false;
			}
			for (const FWidgetAnimMachineCondition& Condition : Transition.Conditions)
			{
				const FInputPlan Before = Plan;
				if (MakeFail(Condition) && !AllHold(Transition))
				{
					return true;
				}
				Plan = Before;
			}
			return false;
		}
	};
}

namespace WidgetAnimMachineJump
{
	bool Plan(const UWidgetAnimStateMachineAsset& Machine, int32 LayerIndex, const FGuid& From, const FGuid& Target,
		const TMap<FName, float>& CurrentValues, const TMap<FName, FVector2D>& CurrentVectors, TArray<FStep>& OutSteps, FText& OutProblem)
	{
		OutSteps.Reset();
		if (!Machine.Layers.IsValidIndex(LayerIndex))
		{
			return false;
		}
		const FWidgetAnimMachineLayer& Layer = Machine.Layers[LayerIndex];
		const int32 EntryIndex = Layer.FindSpecialIndex(EWidgetAnimMachineNodeKind::Entry);
		const int32 AnyIndex = Layer.FindSpecialIndex(EWidgetAnimMachineNodeKind::AnyState);
		const FGuid EntryId = Layer.States.IsValidIndex(EntryIndex) ? Layer.States[EntryIndex].Id : FGuid();
		const FGuid AnyId = Layer.States.IsValidIndex(AnyIndex) ? Layer.States[AnyIndex].Id : FGuid();
		const FGuid Start = Layer.FindState(From) ? From : EntryId;

		auto IsStateNode = [&Layer](const FGuid& Id)
		{
			const FWidgetAnimMachineState* State = Layer.FindState(Id);
			return State && State->Kind == EWidgetAnimMachineNodeKind::State;
		};

		// The fewest transitions from Start to Target: Any State's leave from anywhere but Entry; Entry's only from Entry.
		TArray<FGuid> Hops;
		if (Start != Target)
		{
			TMap<FGuid, TPair<FGuid, FGuid>> CameBy;
			TArray<FGuid> Queue;
			Queue.Add(Start);
			CameBy.Add(Start, TPair<FGuid, FGuid>(FGuid(), FGuid()));
			bool bFound = false;
			for (int32 Head = 0; Head < Queue.Num() && !bFound; ++Head)
			{
				const FGuid Node = Queue[Head];
				for (const FWidgetAnimMachineTransition& Transition : Layer.Transitions)
				{
					const bool bLeaves = Transition.From == Node || (Transition.From == AnyId && Node != EntryId);
					if (!Transition.bEnabled || !bLeaves || !IsStateNode(Transition.To) || CameBy.Contains(Transition.To))
					{
						continue;
					}
					CameBy.Add(Transition.To, TPair<FGuid, FGuid>(Node, Transition.Id));
					if (Transition.To == Target)
					{
						bFound = true;
						break;
					}
					Queue.Add(Transition.To);
				}
			}
			if (bFound)
			{
				for (FGuid Node = Target; Node != Start; Node = CameBy.FindChecked(Node).Key)
				{
					Hops.Insert(CameBy.FindChecked(Node).Value, 0);
				}
			}
		}

		WidgetAnimMachineJumpLocal::FSolver Solver(Machine, CurrentValues, CurrentVectors);
		auto BlockOnce = [&](const FGuid& Node, const FGuid& Except, const FGuid& Into)
		{
			// Any State's are checked first, then the node's own: none of them may be taken instead.
			bool bChanged = false;
			for (const FWidgetAnimMachineTransition& Other : Layer.Transitions)
			{
				const bool bFromHere = Other.From == Node || (Other.From == AnyId && Node != EntryId);
				const bool bBackInto = Other.From == AnyId && Other.To == Into;
				if (bFromHere && Other.Id != Except && !bBackInto)
				{
					bChanged |= Solver.Block(Other);
				}
			}
			return bChanged;
		};
		auto BlockLeaving = [&](const FGuid& Node, const FGuid& Except, const FGuid& Into)
		{
			// Keeping one transition from being taken can make one already kept from it hold again: a few passes settle it.
			for (int32 Pass = 0; Pass < 4 && BlockOnce(Node, Except, Into); ++Pass)
			{
			}
		};

		// Already there: only keep it there.
		if (Start == Target)
		{
			Solver.SetDefaults();
			BlockLeaving(Target, FGuid(), Target);
			FStep& Step = OutSteps.AddDefaulted_GetRef();
			Step.To = Target;
			Step.Plan = Solver.Plan;
			return true;
		}

		if (Hops.Num() == 0)
		{
			// No way there from where the layer is: the next best - a transition into it set to hold, from wherever it leaves.
			const FWidgetAnimMachineTransition* Into = Layer.Transitions.FindByPredicate([&Target](const FWidgetAnimMachineTransition& Transition)
			{
				return Transition.bEnabled && Transition.To == Target;
			});
			const FWidgetAnimMachineState* TargetState = Layer.FindState(Target);
			OutProblem = FText::Format(LOCTEXT("NoWay", "No enabled transitions lead from the state the layer is in to {0}."),
				FText::FromName(TargetState ? TargetState->Name : NAME_None));
			if (Into)
			{
				Solver.SetDefaults();
				Solver.MakeTransitionHold(*Into);
				FStep& Step = OutSteps.AddDefaulted_GetRef();
				Step.Transition = Into->Id;
				Step.To = Target;
				Step.Plan = Solver.Plan;
			}
			return false;
		}

		FGuid Node = Start;
		for (int32 HopIndex = 0; HopIndex < Hops.Num(); ++HopIndex)
		{
			const int32 TransitionIndex = Layer.FindTransitionIndex(Hops[HopIndex]);
			if (!Layer.Transitions.IsValidIndex(TransitionIndex))
			{
				return false;
			}
			const FWidgetAnimMachineTransition& Hop = Layer.Transitions[TransitionIndex];

			// What isn't needed goes back to its default; what is, is set; and nothing else may leave first - nor, on the way, go
			// on from where the step arrives before the next step is set. The last step keeps the layer at the target.
			Solver.SetDefaults();
			Solver.MakeTransitionHold(Hop);
			BlockLeaving(Node, Hop.Id, Hop.To);
			BlockLeaving(Hop.To, FGuid(), Hop.To);

			// Blocking leaves the needed inputs alone, but a needed input can be shared with what was blocked - say so if the
			// step's own transition stopped holding.
			if (!Solver.AllHold(Hop) && OutProblem.IsEmpty())
			{
				const FWidgetAnimMachineState* FromState = Layer.FindState(Node);
				const FWidgetAnimMachineState* ToState = Layer.FindState(Hop.To);
				OutProblem = FText::Format(LOCTEXT("StepBroken", "The transition {0} -> {1} can't be made to hold without another being taken first: the jump may stop short."),
					FText::FromName(FromState ? FromState->Name : NAME_None), FText::FromName(ToState ? ToState->Name : NAME_None));
			}

			FStep& Step = OutSteps.AddDefaulted_GetRef();
			Step.Transition = Hop.Id;
			Step.To = Hop.To;
			Step.Plan = Solver.Plan;
			Solver.Commit();
			Node = Hop.To;
		}
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
