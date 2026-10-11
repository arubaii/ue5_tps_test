// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimKeyValues.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/WidgetAnimEvaluator.h"
#include "Styling/SlateBrush.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "WidgetAnimKeyValues"

namespace WidgetAnimKeyValues
{
	namespace
	{
		/** Most numbers a Shape Box or Shape key lists - a shape has a few dozen; a struct of hundreds would only be noise. */
		constexpr int32 MaxFieldChannels = 160;

		/** How far apart two numbers may be and still be the same number - what an unchanged channel is. */
		bool IsSame(double A, double B)
		{
			return FMath::Abs(A - B) <= 1.e-4 * FMath::Max(1.0, FMath::Max(FMath::Abs(A), FMath::Abs(B)));
		}

		FChannel MakePointChannel(const FText& Label, EPointField Field, float DragStep)
		{
			FChannel Channel;
			Channel.Label = Label;
			Channel.Source = ESource::Point;
			Channel.PointField = Field;
			Channel.DragStep = DragStep;
			return Channel;
		}

		FText ColorPart(const FText& Base, EPointField Field)
		{
			const FText Part = Field == EPointField::ColorR ? LOCTEXT("ColorR", "R")
				: Field == EPointField::ColorG ? LOCTEXT("ColorG", "G")
				: Field == EPointField::ColorB ? LOCTEXT("ColorB", "B")
				: LOCTEXT("ColorA", "A");
			return FText::Format(LOCTEXT("ColorPartFormat", "{0} {1}"), Base, Part);
		}

		void AddColorChannels(const FText& Base, TArray<FChannel>& Out)
		{
			for (const EPointField Field : { EPointField::ColorR, EPointField::ColorG, EPointField::ColorB, EPointField::ColorA })
			{
				Out.Add(MakePointChannel(ColorPart(Base, Field), Field, 0.01f));
			}
		}

		/** A drag step that suits a setting by its name: hundredths for scales, opacities, alphas and colors, tenths for the rest. */
		float GuessDragStep(const FString& Label, bool bColor)
		{
			return bColor || Label.Contains(TEXT("Scale")) || Label.Contains(TEXT("Opacity")) || Label.Contains(TEXT("Alpha")) ? 0.01f : 0.1f;
		}

		/** Every number inside Property's value, down through its structs - as the Graph Editor's channels are gathered. */
		void CollectFields(const FProperty* Root, FName RootName, const FProperty* Property, TArray<const FProperty*>& Tail, const FString& Label, bool bColor, int32 Depth, TArray<FChannel>& Out)
		{
			if (Out.Num() >= MaxFieldChannels)
			{
				return;
			}
			if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
			{
				// A choice from a list is no number to type.
				if (!Numeric->IsEnum())
				{
					FChannel& Channel = Out.AddDefaulted_GetRef();
					Channel.Label = FText::FromString(Label);
					Channel.Source = ESource::Field;
					Channel.RootName = RootName;
					Channel.RootProperty = Root;
					Channel.RootClass = Root->GetClass();
					Channel.Tail = Tail;
					Channel.bInteger = !Numeric->IsFloatingPoint();
					Channel.DragStep = Channel.bInteger ? 1.f : GuessDragStep(Label, bColor);
				}
				return;
			}
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty || Depth >= 3 || StructProperty->Struct == FSlateBrush::StaticStruct())
			{
				return;
			}
			const bool bColorStruct = StructProperty->Struct == TBaseStructure<FLinearColor>::Get() || StructProperty->Struct == TBaseStructure<FColor>::Get();
			for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
			{
				if (It->ArrayDim != 1)
				{
					continue;
				}
				Tail.Push(*It);
				CollectFields(Root, RootName, *It, Tail, Label + TEXT(" ") + It->GetDisplayNameText().ToString(), bColor || bColorStruct, Depth + 1, Out);
				Tail.Pop();
			}
		}

		/** Where a key keeps a Field channel's setting - its Shape Box outline's, or its bag's by name - or null. */
		const void* GetFieldRoot(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel)
		{
			if (!Layer.Transform.Keys.IsValidIndex(Key))
			{
				return nullptr;
			}
			const FWidgetTransformKeyframe& Keyframe = Layer.Transform.Keys[Key];
			if (Layer.AnimType == EWidgetAnimType::ShapeBox)
			{
				return Channel.RootProperty ? Channel.RootProperty->ContainerPtrToValuePtr<void>(&Keyframe.Shape) : nullptr;
			}

			// A bag's settings are looked up again by name: its layout is the key's own. Only the same kind of setting will do -
			// a number of the same class, or a struct of the type the members below it belong to.
			const FPropertyBagPropertyDesc* Desc = Keyframe.ShapeValues.FindPropertyDescByName(Channel.RootName);
			const FConstStructView Values = Keyframe.ShapeValues.GetValue();
			const FProperty* Found = Desc ? Desc->CachedProperty : nullptr;
			if (!Found || !Values.GetMemory() || Found->GetClass() != Channel.RootClass)
			{
				return nullptr;
			}
			if (Channel.Tail.Num() > 0)
			{
				const FStructProperty* FoundStruct = CastField<FStructProperty>(Found);
				if (!FoundStruct || FoundStruct->Struct != Channel.Tail[0]->GetOwnerStruct())
				{
					return nullptr;
				}
			}
			return Found->ContainerPtrToValuePtr<void>(Values.GetMemory());
		}

		/** Down through the channel's structs from its setting to the number itself, and that number's property. */
		const void* WalkField(const FChannel& Channel, const void* Root, const FNumericProperty*& OutLeaf)
		{
			OutLeaf = nullptr;
			const void* At = Root;
			for (const FProperty* Member : Channel.Tail)
			{
				if (!At)
				{
					return nullptr;
				}
				At = Member->ContainerPtrToValuePtr<void>(At);
			}
			// The leaf: the last member, or the setting itself - a bag's looked up again, so its own property is read.
			if (Channel.Tail.Num() > 0)
			{
				OutLeaf = CastField<FNumericProperty>(Channel.Tail.Last());
			}
			return At;
		}

		const FNumericProperty* FindRootLeaf(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel)
		{
			if (Layer.AnimType == EWidgetAnimType::ShapeBox)
			{
				return CastField<FNumericProperty>(Channel.RootProperty);
			}
			const FPropertyBagPropertyDesc* Desc = Layer.Transform.Keys.IsValidIndex(Key) ? Layer.Transform.Keys[Key].ShapeValues.FindPropertyDescByName(Channel.RootName) : nullptr;
			return Desc ? CastField<FNumericProperty>(Desc->CachedProperty) : nullptr;
		}

		double ReadTransformField(const FWidgetTransform& Transform, int32 Field)
		{
			switch (Field)
			{
			case 0: return Transform.Translation.X;
			case 1: return Transform.Translation.Y;
			case 2: return Transform.Scale.X;
			case 3: return Transform.Scale.Y;
			case 4: return Transform.Shear.X;
			case 5: return Transform.Shear.Y;
			default: return Transform.Angle;
			}
		}

		void WriteTransformField(FWidgetTransform& Transform, int32 Field, double Value)
		{
			switch (Field)
			{
			case 0: Transform.Translation.X = Value; break;
			case 1: Transform.Translation.Y = Value; break;
			case 2: Transform.Scale.X = Value; break;
			case 3: Transform.Scale.Y = Value; break;
			case 4: Transform.Shear.X = Value; break;
			case 5: Transform.Shear.Y = Value; break;
			default: Transform.Angle = static_cast<float>(Value); break;
			}
		}

		float ReadColorField(const FLinearColor& Color, EPointField Field)
		{
			switch (Field)
			{
			case EPointField::ColorR: return Color.R;
			case EPointField::ColorG: return Color.G;
			case EPointField::ColorB: return Color.B;
			default:                  return Color.A;
			}
		}

		void WriteColorField(FLinearColor& Color, EPointField Field, float Value)
		{
			switch (Field)
			{
			case EPointField::ColorR: Color.R = Value; break;
			case EPointField::ColorG: Color.G = Value; break;
			case EPointField::ColorB: Color.B = Value; break;
			default:                  Color.A = Value; break;
			}
		}

		bool IsColorField(EPointField Field)
		{
			return Field != EPointField::Value && Field != EPointField::ValueY;
		}

		// ---- The expression a value box takes - see Evaluate ------------------------------------------------------------

		/** A small recursive-descent reader: sum := product (('+'|'-') product)*, product := unary (('*'|'/'|'%') unary)*,
		 *  unary := ('-'|'+') unary | power, power := atom ('^' unary)?, atom := number | x | v | '(' sum ')' - so -2^2 is -4. */
		struct FExpressionReader
		{
			const FString& Text;
			int32 At = 0;
			double Current = 0.0;
			bool bFailed = false;

			FExpressionReader(const FString& InText, double InCurrent) : Text(InText), Current(InCurrent) {}

			void SkipSpaces()
			{
				while (At < Text.Len() && FChar::IsWhitespace(Text[At]))
				{
					++At;
				}
			}

			bool Accept(TCHAR Char)
			{
				SkipSpaces();
				if (At < Text.Len() && Text[At] == Char)
				{
					++At;
					return true;
				}
				return false;
			}

			bool AtEnd()
			{
				SkipSpaces();
				return At >= Text.Len();
			}

			double ReadSum()
			{
				double Value = ReadProduct();
				for (;;)
				{
					if (Accept(TEXT('+')))
					{
						Value += ReadProduct();
					}
					else if (Accept(TEXT('-')))
					{
						Value -= ReadProduct();
					}
					else
					{
						return Value;
					}
				}
			}

			double ReadProduct()
			{
				double Value = ReadUnary();
				for (;;)
				{
					if (Accept(TEXT('*')))
					{
						Value *= ReadUnary();
					}
					else if (Accept(TEXT('/')))
					{
						const double Divisor = ReadUnary();
						if (FMath::IsNearlyZero(Divisor))
						{
							bFailed = true;
							return 0.0;
						}
						Value /= Divisor;
					}
					else if (Accept(TEXT('%')))
					{
						const double Divisor = ReadUnary();
						if (FMath::IsNearlyZero(Divisor))
						{
							bFailed = true;
							return 0.0;
						}
						Value = FMath::Fmod(Value, Divisor);
					}
					else
					{
						return Value;
					}
				}
			}

			double ReadPower()
			{
				const double Base = ReadAtom();
				if (Accept(TEXT('^')))
				{
					return FMath::Pow(Base, ReadUnary());
				}
				return Base;
			}

			double ReadUnary()
			{
				if (Accept(TEXT('-')))
				{
					return -ReadUnary();
				}
				if (Accept(TEXT('+')))
				{
					return ReadUnary();
				}
				return ReadPower();
			}

			double ReadAtom()
			{
				SkipSpaces();
				if (Accept(TEXT('(')))
				{
					const double Value = ReadSum();
					if (!Accept(TEXT(')')))
					{
						bFailed = true;
					}
					return Value;
				}
				if (At < Text.Len() && (Text[At] == TEXT('x') || Text[At] == TEXT('X') || Text[At] == TEXT('v') || Text[At] == TEXT('V')))
				{
					++At;
					return Current;
				}

				// A number: digits, a point, digits, and an exponent - ".5", "2.", "1e-3". A unit typed after it (12s, 45deg,
				// 50%) is left for the caller to have taken off.
				const int32 Start = At;
				bool bDigits = false;
				while (At < Text.Len() && FChar::IsDigit(Text[At]))
				{
					++At;
					bDigits = true;
				}
				if (At < Text.Len() && Text[At] == TEXT('.'))
				{
					++At;
					while (At < Text.Len() && FChar::IsDigit(Text[At]))
					{
						++At;
						bDigits = true;
					}
				}
				if (bDigits && At < Text.Len() && (Text[At] == TEXT('e') || Text[At] == TEXT('E')))
				{
					int32 Exponent = At + 1;
					if (Exponent < Text.Len() && (Text[Exponent] == TEXT('+') || Text[Exponent] == TEXT('-')))
					{
						++Exponent;
					}
					if (Exponent < Text.Len() && FChar::IsDigit(Text[Exponent]))
					{
						At = Exponent;
						while (At < Text.Len() && FChar::IsDigit(Text[At]))
						{
							++At;
						}
					}
				}
				if (!bDigits)
				{
					bFailed = true;
					return 0.0;
				}
				return FCString::Atod(*Text.Mid(Start, At - Start));
			}
		};

		TOptional<double> EvaluateText(const FString& Text, double Current)
		{
			FExpressionReader Reader(Text, Current);
			const double Value = Reader.ReadSum();
			if (Reader.bFailed || !Reader.AtEnd() || !FMath::IsFinite(Value))
			{
				return TOptional<double>();
			}
			return Value;
		}
	}

	void GetChannels(const FWidgetAnimLayer& Layer, TArray<FChannel>& OutChannels)
	{
		OutChannels.Reset();
		if (Layer.IsAnimationTrack())
		{
			return;
		}

		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:
			OutChannels.Add(MakePointChannel(LOCTEXT("Opacity", "Opacity"), EPointField::Value, 0.01f));
			return;
		case EWidgetAnimType::Scale:
			OutChannels.Add(MakePointChannel(LOCTEXT("ScaleX", "Scale X"), EPointField::Value, 0.01f));
			OutChannels.Add(MakePointChannel(LOCTEXT("ScaleY", "Scale Y"), EPointField::ValueY, 0.01f));
			return;
		case EWidgetAnimType::Slide:
			OutChannels.Add(MakePointChannel(LOCTEXT("Distance", "Distance"), EPointField::Value, 1.f));
			return;
		case EWidgetAnimType::Rotate:
			OutChannels.Add(MakePointChannel(LOCTEXT("Angle", "Angle"), EPointField::Value, 1.f));
			return;
		case EWidgetAnimType::MaterialScalar:
			OutChannels.Add(MakePointChannel(LOCTEXT("ScalarValue", "Value"), EPointField::Value, 0.01f));
			return;
		case EWidgetAnimType::MaterialVector:
			AddColorChannels(LOCTEXT("VectorValue", "Value"), OutChannels);
			return;
		case EWidgetAnimType::Custom:
		{
			const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator();
			if (!Evaluator)
			{
				return;
			}
			const EWidgetAnimKeyContent Content = Evaluator->GetKeyContent(Layer.TypeParams);
			if (Content != EWidgetAnimKeyContent::Color)
			{
				OutChannels.Add(MakePointChannel(Evaluator->GetTrackValueLabel(Layer.TypeParams), EPointField::Value, 0.1f));
			}
			if (Content != EWidgetAnimKeyContent::Value)
			{
				AddColorChannels(Evaluator->GetKeyColorLabel(Layer.TypeParams), OutChannels);
			}
			return;
		}
		case EWidgetAnimType::Transform:
		{
			static const FText TransformLabels[] =
			{
				LOCTEXT("TranslationX", "Translation X"),
				LOCTEXT("TranslationY", "Translation Y"),
				LOCTEXT("TransformScaleX", "Scale X"),
				LOCTEXT("TransformScaleY", "Scale Y"),
				LOCTEXT("ShearX", "Shear X"),
				LOCTEXT("ShearY", "Shear Y"),
				LOCTEXT("TransformAngle", "Angle"),
			};
			static const float TransformSteps[] = { 1.f, 1.f, 0.01f, 0.01f, 0.1f, 0.1f, 1.f };
			for (int32 Field = 0; Field < UE_ARRAY_COUNT(TransformLabels); ++Field)
			{
				FChannel& Channel = OutChannels.AddDefaulted_GetRef();
				Channel.Label = TransformLabels[Field];
				Channel.Source = ESource::Transform;
				Channel.TransformField = Field;
				Channel.DragStep = TransformSteps[Field];
			}
			return;
		}
		case EWidgetAnimType::ShapeBox:
		{
			TArray<const FProperty*> Tail;
			for (TFieldIterator<FProperty> It(FShapeBoxShape::StaticStruct()); It; ++It)
			{
				if (It->ArrayDim == 1)
				{
					CollectFields(*It, It->GetFName(), *It, Tail, It->GetDisplayNameText().ToString(), false, 0, OutChannels);
				}
			}
			return;
		}
		case EWidgetAnimType::Shape:
		case EWidgetAnimType::Design:
		{
			// Every key is laid out alike: the first that has its settings says what they are.
			const UPropertyBag* Layout = nullptr;
			for (const FWidgetTransformKeyframe& Key : Layer.Transform.Keys)
			{
				if (const UPropertyBag* KeyLayout = Key.ShapeValues.GetPropertyBagStruct())
				{
					Layout = KeyLayout;
					break;
				}
			}
			if (!Layout)
			{
				return;
			}
			TArray<const FProperty*> Tail;
			for (const FPropertyBagPropertyDesc& Desc : Layout->GetPropertyDescs())
			{
				if (Desc.CachedProperty)
				{
					CollectFields(Desc.CachedProperty, Desc.Name, Desc.CachedProperty, Tail, Desc.CachedProperty->GetDisplayNameText().ToString(), false, 0, OutChannels);
				}
			}
			return;
		}
		default:
			return;
		}
	}

	int32 GetNumKeys(const FWidgetAnimLayer& Layer)
	{
		if (Layer.IsAnimationTrack())
		{
			return 0;
		}
		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
		{
			return Layer.Transform.Keys.Num();
		}
		return WidgetAnimTypeSupportsValueKeys(Layer.AnimType) ? Layer.ValueKeys.NumPoints() : 0;
	}

	bool IsKeyAuthored(const FWidgetAnimLayer& Layer, int32 Key)
	{
		if (!WidgetAnimTypeIsKeyframed(Layer.AnimType) || !Layer.Transform.Keys.IsValidIndex(Key))
		{
			return false;
		}
		const FWidgetTransformKeyframe& Keyframe = Layer.Transform.Keys[Key];
		return Layer.AnimType == EWidgetAnimType::Transform ? Keyframe.bUseAuthoredTransform : Keyframe.bUseAuthoredShape;
	}

	TOptional<double> Read(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel)
	{
		if (Key < 0 || Key >= GetNumKeys(Layer))
		{
			return TOptional<double>();
		}

		switch (Channel.Source)
		{
		case ESource::Point:
			if (IsColorField(Channel.PointField))
			{
				return static_cast<double>(ReadColorField(WidgetAnimKeyPoints::GetColor(Layer, Key), Channel.PointField));
			}
			return static_cast<double>(Channel.PointField == EPointField::ValueY
				? WidgetAnimKeyPoints::GetValueY(Layer, Key)
				: WidgetAnimKeyPoints::GetValue(Layer, Key));

		case ESource::Transform:
		{
			// A key using the authored transform stands for the widget as designed: no change of its own - identity.
			const FWidgetTransformKeyframe& Keyframe = Layer.Transform.Keys[Key];
			return ReadTransformField(Keyframe.bUseAuthoredTransform ? FWidgetTransform() : Keyframe.Transform, Channel.TransformField);
		}

		case ESource::Field:
		{
			if (Layer.Transform.Keys[Key].bUseAuthoredShape)
			{
				return TOptional<double>();
			}
			const FNumericProperty* Leaf = nullptr;
			const void* Value = WalkField(Channel, GetFieldRoot(Layer, Key, Channel), Leaf);
			if (!Leaf)
			{
				Leaf = FindRootLeaf(Layer, Key, Channel);
			}
			if (!Leaf || !Value)
			{
				return TOptional<double>();
			}
			return Leaf->IsFloatingPoint() ? Leaf->GetFloatingPointPropertyValue(Value) : static_cast<double>(Leaf->GetSignedIntPropertyValue(Value));
		}
		}
		return TOptional<double>();
	}

	bool CanWrite(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel, FText* OutWhyNot)
	{
		auto No = [OutWhyNot](const FText& Why)
		{
			if (OutWhyNot)
			{
				*OutWhyNot = Why;
			}
			return false;
		};

		const int32 NumKeys = GetNumKeys(Layer);
		if (Key < 0 || Key >= NumKeys)
		{
			return No(FText::GetEmpty());
		}

		if (Channel.Source == ESource::Point)
		{
			const bool bEdge = Key == 0 || Key == NumKeys - 1;
			if (bEdge && Layer.AnimType == EWidgetAnimType::Custom)
			{
				return No(LOCTEXT("RegisteredEdgeWhyNot", "This track's start and end values are its own settings - set them under Track Values."));
			}
			if (Key == NumKeys - 1 && Layer.AnimType == EWidgetAnimType::Slide)
			{
				return No(LOCTEXT("SlideEndWhyNot", "A Slide always ends where the widget is: 0 along the slide."));
			}
			return true;
		}
		if (Channel.Source == ESource::Field && Layer.Transform.Keys[Key].bUseAuthoredShape)
		{
			return No(LOCTEXT("AuthoredShapeWhyNot", "This key uses the shape as authored. Turn its Use Authored off (pick the key) to give it values of its own."));
		}
		return Read(Layer, Key, Channel).IsSet() || No(FText::GetEmpty());
	}

	bool Write(FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel, double Value)
	{
		if (!CanWrite(Layer, Key, Channel) || !FMath::IsFinite(Value))
		{
			return false;
		}
		if (Channel.bInteger)
		{
			Value = FMath::RoundHalfFromZero(Value);
		}

		switch (Channel.Source)
		{
		case ESource::Point:
		{
			const int32 NumKeys = GetNumKeys(Layer);
			const float Number = static_cast<float>(Value);
			if (Key > 0 && Key < NumKeys - 1)
			{
				FWidgetAnimValueKey& ValueKey = Layer.ValueKeys.Keys[Key - 1];
				if (IsColorField(Channel.PointField))
				{
					WriteColorField(ValueKey.Color, Channel.PointField, Number);
				}
				else if (Channel.PointField == EPointField::ValueY)
				{
					ValueKey.ValueY = Number;
				}
				else
				{
					ValueKey.Value = Number;
				}
				return true;
			}

			// The start or end value: the track's own Start / End setting.
			const bool bStart = Key == 0;
			switch (Layer.AnimType)
			{
			case EWidgetAnimType::Fade:
				(bStart ? Layer.StartOpacity : Layer.EndOpacity) = Number;
				return true;
			case EWidgetAnimType::Scale:
			{
				FVector2D& Scale = bStart ? Layer.StartScale : Layer.EndScale;
				(Channel.PointField == EPointField::ValueY ? Scale.Y : Scale.X) = Value;
				return true;
			}
			case EWidgetAnimType::Slide:
				// Only its start: how far away it slides in from, never less than nothing.
				Layer.SlideDistance = FMath::Max(Number, 0.f);
				return true;
			case EWidgetAnimType::Rotate:
				(bStart ? Layer.StartAngle : Layer.EndAngle) = Number;
				return true;
			case EWidgetAnimType::MaterialScalar:
				(bStart ? Layer.StartScalarValue : Layer.EndScalarValue) = Number;
				return true;
			case EWidgetAnimType::MaterialVector:
				WriteColorField(bStart ? Layer.StartVectorValue : Layer.EndVectorValue, Channel.PointField, Number);
				return true;
			default:
				return false;
			}
		}

		case ESource::Transform:
		{
			FWidgetTransformKeyframe& Keyframe = Layer.Transform.Keys[Key];
			if (Keyframe.bUseAuthoredTransform)
			{
				// At the pose it stood for, so only the channel typed changes.
				Keyframe.Transform = FWidgetTransform();
				Keyframe.bUseAuthoredTransform = false;
			}
			WriteTransformField(Keyframe.Transform, Channel.TransformField, Value);
			return true;
		}

		case ESource::Field:
		{
			const FNumericProperty* Leaf = nullptr;
			void* At = const_cast<void*>(WalkField(Channel, GetFieldRoot(Layer, Key, Channel), Leaf));
			if (!Leaf)
			{
				Leaf = FindRootLeaf(Layer, Key, Channel);
			}
			if (!Leaf || !At)
			{
				return false;
			}
			if (Leaf->IsFloatingPoint())
			{
				Leaf->SetFloatingPointPropertyValue(At, Value);
			}
			else
			{
				Leaf->SetIntPropertyValue(At, static_cast<int64>(Value));
			}
			return true;
		}
		}
		return false;
	}

	bool IsAltered(const FWidgetAnimLayer& Layer, int32 Key, const FChannel& Channel)
	{
		const TOptional<double> Here = Read(Layer, Key, Channel);
		if (!Here.IsSet())
		{
			return false;
		}
		const int32 NumKeys = GetNumKeys(Layer);
		if (Key > 0)
		{
			const TOptional<double> Before = Read(Layer, Key - 1, Channel);
			if (Before.IsSet() && !IsSame(Before.GetValue(), Here.GetValue()))
			{
				return true;
			}
		}
		if (Key + 1 < NumKeys)
		{
			const TOptional<double> After = Read(Layer, Key + 1, Channel);
			if (After.IsSet() && !IsSame(After.GetValue(), Here.GetValue()))
			{
				return true;
			}
		}
		return false;
	}

	TOptional<double> Evaluate(const FString& Expression, double Current)
	{
		FString Text = Expression.TrimStartAndEnd();
		if (Text.IsEmpty())
		{
			return TOptional<double>();
		}

		// "+= 8", "-= 8", "*= 2", "/= 4", "%= 3", "^= 2": that done to the box's own number.
		if (Text.Len() >= 2 && Text[1] == TEXT('='))
		{
			const TCHAR Operator = Text[0];
			if (Operator == TEXT('+') || Operator == TEXT('-') || Operator == TEXT('*') || Operator == TEXT('/') || Operator == TEXT('%') || Operator == TEXT('^'))
			{
				return EvaluateText(FString::Printf(TEXT("x %c (%s)"), Operator, *Text.Mid(2)), Current);
			}
		}

		// "+8", "*2", "/4", "^2": relative too. A leading minus stays a negative number - "-8" is minus eight.
		const TCHAR First = Text[0];
		if (First == TEXT('+') || First == TEXT('*') || First == TEXT('/') || First == TEXT('^'))
		{
			return EvaluateText(FString::Printf(TEXT("x %c (%s)"), First, *Text.Mid(1)), Current);
		}
		return EvaluateText(Text, Current);
	}
}

#undef LOCTEXT_NAMESPACE
