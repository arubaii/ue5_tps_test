// Copyright AR GameSystems 2026. All rights reserved.

#include "SWidgetAnimGraphEditor.h"

#include "SWidgetAnimTimeline.h"
#include "SWidgetAnimAnchoredTip.h"
#include "WidgetAnimPanelSource.h"
#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"

#include "NameTBD/WidgetAnimation/WidgetAnimLibrary.h"
#include "NameTBD/WidgetAnimation/WidgetAnimTransform.h"
#include "NameTBD/WidgetAnimation/WidgetAnimSpring.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimCues.h"
#include "NameTBD/WidgetAnimation/Evaluators/WidgetAnimShapeTrack.h"
#include "NameTBD/Widgets/ShapeBox.h"

#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Rendering/DrawElements.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetAnimGraphEditor"

// Everything this file needs of its own lives in WidgetAnimGraph, and is reached through it: in a unity build this file
// shares a blob with others that have their own Palette and tooltip aliases at file scope.
namespace WidgetAnimGraph
{
	namespace Palette = NameTBD::Colors::WidgetAnimEditor;
	namespace Tips = NameTBD::Tooltips::WidgetAnimEditor;

	/** The two ways the graph draws the tracks - see SWidgetAnimGraphEditor. */
	enum class EView : uint8
	{
		Values,
		Ease
	};

	/** One value a track's keys hold: what each curve of the Values view draws. */
	enum class EChannel : uint8
	{
		Value,
		ColorR,
		ColorG,
		ColorB,
		ColorA,
		TranslationX,
		TranslationY,
		ScaleX,
		ScaleY,
		ShearX,
		ShearY,
		Angle,

		/**
		 * The Ease view's curve: how far through its keys a track is - each key a step up - so its timing and easing can be
		 * seen and shaped there. In the Values view only for a Shape Box or Shape track whose keys hold no number to draw.
		 */
		Progress,

		/** A Sound cue's fades: how loud it is along its run, from its Fade In and Fade Out - 0 silent, 1 its full volume. */
		FadeEnvelope,

		/**
		 * The first of a Shape Box or Shape track's own values - FieldBase + N is the track's Nth (see FFieldChannel): each
		 * number its keys' shapes hold, a curve of its own, as a Transform track's translation and scale are.
		 */
		FieldBase = 32
	};

	/** Most of a track's shape values drawn as channels of their own - see EChannel::FieldBase. */
	constexpr int32 MaxFieldChannels = 160;

	bool IsFieldChannel(EChannel Channel) { return static_cast<uint8>(Channel) >= static_cast<uint8>(EChannel::FieldBase); }
	int32 GetFieldIndex(EChannel Channel) { return static_cast<int32>(static_cast<uint8>(Channel)) - static_cast<int32>(EChannel::FieldBase); }
	EChannel MakeFieldChannel(int32 Index) { return static_cast<EChannel>(static_cast<uint8>(EChannel::FieldBase) + static_cast<uint8>(Index)); }

	/**
	 * A Sound cue's fades as keys of the FadeEnvelope channel: where its fade in ends and where its fade out begins, dragged
	 * in time to set them. Numbered past any key a track can have.
	 */
	constexpr int32 FadeInPoint = 100000;
	constexpr int32 FadeOutPoint = 100001;

	/**
	 * One number a Shape Box or Shape track's keys hold: a setting of the key's shape (Root - by name, in a Shape track's bag),
	 * and down through the structs inside it (Tail) to the number - a corner's Size X, a fill color's R.
	 */
	struct FFieldChannel
	{
		FText Name;
		FName RootName;
		const FProperty* RootProperty = nullptr;
		TArray<const FProperty*> Tail;

		const FNumericProperty* GetLeaf() const
		{
			return CastField<FNumericProperty>(Tail.Num() > 0 ? Tail.Last() : RootProperty);
		}
	};

	/** The interpolations the graph's Curve To Next Key menu sets - the segment curves, less a Curve asset (see SetCurve). */
	enum class ECurvePreset : uint8
	{
		TrackEasing,
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut,
		Hold,
		Bezier
	};

	/** A key of one channel - or, in the Ease view, of the track (Progress). */
	struct FGraphKey
	{
		int32 Layer = INDEX_NONE;
		int32 Point = INDEX_NONE;
		EChannel Channel = EChannel::Value;

		bool operator==(const FGraphKey& Other) const { return Layer == Other.Layer && Point == Other.Point && Channel == Other.Channel; }
		bool SameKey(const FGraphKey& Other) const { return Layer == Other.Layer && Point == Other.Point; }
	};

	/** A channel of a track, as one number - what hiding and the active channel are kept by. */
	uint64 ChannelId(int32 Layer, EChannel Channel)
	{
		return (static_cast<uint64>(static_cast<uint32>(Layer)) << 8) | static_cast<uint8>(Channel);
	}

	/** Pixel sizes of the canvas. */
	constexpr float RulerHeight = 22.f;
	constexpr float GutterWidth = 50.f;
	constexpr float BottomPadding = 8.f;
	constexpr float KeyRadius = 4.5f;
	constexpr float KeyHitRadius = 7.f;
	constexpr float HandleRadius = 3.5f;
	constexpr float CurveHitDistance = 8.f;
	constexpr float DragThreshold = 3.f;

	/** A handle's height is kept within this, in fractions of its segment's travel: overshoots and wind-ups, not runaways. */
	constexpr float HandleMinY = -2.f;
	constexpr float HandleMaxY = 3.f;

	/** One track as the graph shows it, read afresh every frame from the track itself. */
	struct FLayerModel
	{
		int32 Layer = INDEX_NONE;

		/** The track as it plays - what every curve is sampled from, by the runtime's own code. */
		FWidgetAnimSettings Settings;

		/** Where it starts on the panel's timeline, how long it runs, in played seconds; and played seconds per authored. */
		float SpanStart = 0.f;
		float RunSeconds = 0.f;
		float TimeScale = 1.f;

		bool bKeyframed = false;
		bool bEditable = false;
		bool bCurves = false;
		bool bKeyless = false;

		/** Each key's time, in played seconds from the track's start - the start and end values included on a keyed track. */
		TArray<float> PointTimes;

		/** How many of those have values: a Transform track's keys can be briefly out of step with its curve. */
		int32 NumValuePoints = 0;

		/** A Sound cue: its volume at its points, and its fades (EChannel::FadeEnvelope). */
		bool bSound = false;

		/** A Shape Box or Shape track's numbers - see EChannel::FieldBase - and what its keys' Use Authored read them off. */
		TArray<FFieldChannel> Fields;
		TWeakObjectPtr<const UObject> AuthoredShape;
		TOptional<FShapeBoxShape> AuthoredBox;

		TArray<EChannel> Channels;
		FLinearColor Color = FLinearColor::White;
		FText Name;
	};

	// ---------------------------------------------------------------------------------------------
	// Reading and writing a key's values
	// ---------------------------------------------------------------------------------------------

	float ReadTransform(const FWidgetTransform& Transform, EChannel Channel)
	{
		switch (Channel)
		{
		case EChannel::TranslationX: return static_cast<float>(Transform.Translation.X);
		case EChannel::TranslationY: return static_cast<float>(Transform.Translation.Y);
		case EChannel::ScaleX:       return static_cast<float>(Transform.Scale.X);
		case EChannel::ScaleY:       return static_cast<float>(Transform.Scale.Y);
		case EChannel::ShearX:       return static_cast<float>(Transform.Shear.X);
		case EChannel::ShearY:       return static_cast<float>(Transform.Shear.Y);
		case EChannel::Angle:        return Transform.Angle;
		default:                     return 0.f;
		}
	}

	void WriteTransform(FWidgetTransform& Transform, EChannel Channel, float Value)
	{
		switch (Channel)
		{
		case EChannel::TranslationX: Transform.Translation.X = Value; break;
		case EChannel::TranslationY: Transform.Translation.Y = Value; break;
		case EChannel::ScaleX:       Transform.Scale.X = Value; break;
		case EChannel::ScaleY:       Transform.Scale.Y = Value; break;
		case EChannel::ShearX:       Transform.Shear.X = Value; break;
		case EChannel::ShearY:       Transform.Shear.Y = Value; break;
		case EChannel::Angle:        Transform.Angle = Value; break;
		default: break;
		}
	}

	bool IsColorChannel(EChannel Channel)
	{
		return Channel == EChannel::ColorR || Channel == EChannel::ColorG || Channel == EChannel::ColorB || Channel == EChannel::ColorA;
	}

	float ReadColor(const FLinearColor& Color, EChannel Channel)
	{
		switch (Channel)
		{
		case EChannel::ColorR: return Color.R;
		case EChannel::ColorG: return Color.G;
		case EChannel::ColorB: return Color.B;
		case EChannel::ColorA: return Color.A;
		default:               return 0.f;
		}
	}

	void WriteColor(FLinearColor& Color, EChannel Channel, float Value)
	{
		switch (Channel)
		{
		case EChannel::ColorR: Color.R = Value; break;
		case EChannel::ColorG: Color.G = Value; break;
		case EChannel::ColorB: Color.B = Value; break;
		case EChannel::ColorA: Color.A = Value; break;
		default: break;
		}
	}

	// ---------------------------------------------------------------------------------------------
	// A Shape Box or Shape track's numbers - see FFieldChannel
	// ---------------------------------------------------------------------------------------------

	/** Every number inside Property's value, down through its structs - a corner's Size X and Y, a color's R, G, B and A. */
	void CollectFields(const FProperty* Root, FName RootName, const FProperty* Property, TArray<const FProperty*>& Tail, const FString& Label, int32 Depth, TArray<FFieldChannel>& OutFields)
	{
		if (OutFields.Num() >= MaxFieldChannels)
		{
			return;
		}
		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			// A choice from a list is no number to draw.
			if (!Numeric->IsEnum())
			{
				FFieldChannel& Field = OutFields.AddDefaulted_GetRef();
				Field.Name = FText::FromString(Label);
				Field.RootName = RootName;
				Field.RootProperty = Root;
				Field.Tail = Tail;
			}
			return;
		}
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (!StructProperty || Depth >= 3 || StructProperty->Struct == FSlateBrush::StaticStruct())
		{
			return;
		}
		for (TFieldIterator<FProperty> It(StructProperty->Struct); It; ++It)
		{
			if (It->ArrayDim != 1)
			{
				continue;
			}
			Tail.Push(*It);
			CollectFields(Root, RootName, *It, Tail, Label + TEXT(" ") + It->GetDisplayNameText().ToString(), Depth + 1, OutFields);
			Tail.Pop();
		}
	}

	/** A Shape Box track's numbers, from its outline's settings; a Shape track's, from its first key's - every key is laid out alike. */
	void BuildFields(const FWidgetAnimLayer& Layer, TArray<FFieldChannel>& OutFields)
	{
		OutFields.Reset();
		TArray<const FProperty*> Tail;
		if (Layer.AnimType == EWidgetAnimType::ShapeBox)
		{
			for (TFieldIterator<FProperty> It(FShapeBoxShape::StaticStruct()); It; ++It)
			{
				if (It->ArrayDim == 1)
				{
					CollectFields(*It, It->GetFName(), *It, Tail, It->GetDisplayNameText().ToString(), 0, OutFields);
				}
			}
			return;
		}

		const UPropertyBag* Layout = Layer.Transform.Keys.Num() > 0 ? Layer.Transform.Keys[0].ShapeValues.GetPropertyBagStruct() : nullptr;
		if (!Layout)
		{
			return;
		}
		for (const FPropertyBagPropertyDesc& Desc : Layout->GetPropertyDescs())
		{
			if (Desc.CachedProperty)
			{
				CollectFields(Desc.CachedProperty, Desc.Name, Desc.CachedProperty, Tail, Desc.CachedProperty->GetDisplayNameText().ToString(), 0, OutFields);
			}
		}
	}

	/** What the key at Point's Use Authored reads its shape off: the shape as designed in the Widget Blueprint. Null when not known. */
	const void* GetAuthoredRoot(const FLayerModel& Model, const FFieldChannel& Field)
	{
		if (Model.AuthoredBox.IsSet())
		{
			return Field.RootProperty->ContainerPtrToValuePtr<void>(&Model.AuthoredBox.GetValue());
		}
		const UObject* Shape = Model.AuthoredShape.Get();
		const FProperty* OnShape = Shape ? FindFProperty<FProperty>(Shape->GetClass(), Field.RootName) : nullptr;
		return OnShape && OnShape->SameType(Field.RootProperty) ? OnShape->ContainerPtrToValuePtr<void>(Shape) : nullptr;
	}

	/** Where the key at Point keeps Field's setting - its own, or with bAuthored the shape as designed - or null. */
	const void* GetFieldRoot(const FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, const FFieldChannel& Field, bool bAuthored)
	{
		if (!Layer.Transform.Keys.IsValidIndex(Point) || !Field.RootProperty)
		{
			return nullptr;
		}
		const FWidgetTransformKeyframe& Key = Layer.Transform.Keys[Point];
		if (bAuthored)
		{
			if (const void* Authored = GetAuthoredRoot(Model, Field))
			{
				return Authored;
			}
		}
		if (Layer.AnimType == EWidgetAnimType::ShapeBox)
		{
			return Field.RootProperty->ContainerPtrToValuePtr<void>(&Key.Shape);
		}
		const FPropertyBagPropertyDesc* Desc = Key.ShapeValues.FindPropertyDescByName(Field.RootName);
		const FConstStructView Values = Key.ShapeValues.GetValue();
		return Desc && Desc->CachedProperty && Values.GetMemory() && Desc->CachedProperty->SameType(Field.RootProperty)
			? Desc->CachedProperty->ContainerPtrToValuePtr<void>(Values.GetMemory())
			: nullptr;
	}

	/** Down through Field's structs from its root to the number itself. */
	const void* WalkField(const FFieldChannel& Field, const void* Root)
	{
		const void* At = Root;
		for (const FProperty* Member : Field.Tail)
		{
			if (!At)
			{
				return nullptr;
			}
			At = Member->ContainerPtrToValuePtr<void>(At);
		}
		return At;
	}

	float ReadField(const FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, int32 FieldIndex)
	{
		const FFieldChannel* Field = Model.Fields.IsValidIndex(FieldIndex) ? &Model.Fields[FieldIndex] : nullptr;
		const FNumericProperty* Leaf = Field ? Field->GetLeaf() : nullptr;
		const int32 Clamped = FMath::Clamp(Point, 0, Model.NumValuePoints - 1);
		const bool bAuthored = Layer.Transform.Keys.IsValidIndex(Clamped) && Layer.Transform.Keys[Clamped].bUseAuthoredShape;
		const void* Value = Field ? WalkField(*Field, GetFieldRoot(Layer, Model, Clamped, *Field, bAuthored)) : nullptr;
		if (!Leaf || !Value)
		{
			return 0.f;
		}
		return Leaf->IsFloatingPoint() ? static_cast<float>(Leaf->GetFloatingPointPropertyValue(Value)) : static_cast<float>(Leaf->GetSignedIntPropertyValue(Value));
	}

	void WriteField(FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, int32 FieldIndex, float NewValue)
	{
		const FFieldChannel* Field = Model.Fields.IsValidIndex(FieldIndex) ? &Model.Fields[FieldIndex] : nullptr;
		const FNumericProperty* Leaf = Field ? Field->GetLeaf() : nullptr;
		if (!Leaf || !Layer.Transform.Keys.IsValidIndex(Point) || Layer.Transform.Keys[Point].bUseAuthoredShape)
		{
			return;
		}
		void* Value = const_cast<void*>(WalkField(*Field, GetFieldRoot(Layer, Model, Point, *Field, /*bAuthored=*/false)));
		if (!Value)
		{
			return;
		}
		if (Leaf->IsFloatingPoint())
		{
			Leaf->SetFloatingPointPropertyValue(Value, static_cast<double>(NewValue));
		}
		else
		{
			Leaf->SetIntPropertyValue(Value, static_cast<int64>(FMath::RoundToDouble(NewValue)));
		}
	}

	/** Whether a Sound cue's fade envelope - see EChannel::FadeEnvelope. */
	const FWidgetAnimSoundCueParams* GetSoundParams(const FWidgetAnimLayer& Layer)
	{
		return Layer.AnimType == EWidgetAnimType::Custom ? Layer.TypeParams.GetPtr<FWidgetAnimSoundCueParams>() : nullptr;
	}

	/** A fade's volume a share T of the way through it, along the Fade Curve - as the track draws its fades. */
	float FadeShape(EWidgetAnimSoundFadeCurve Curve, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		switch (Curve)
		{
		case EWidgetAnimSoundFadeCurve::Logarithmic: return FMath::LogX(10.f, 1.f + 9.f * T);
		case EWidgetAnimSoundFadeCurve::SCurve:      return FMath::SmoothStep(0.f, 1.f, T);
		case EWidgetAnimSoundFadeCurve::Sine:        return FMath::Sin(T * HALF_PI);
		default:                                     return T;
		}
	}

	/** A Sound cue's fade in and out, in played seconds, held inside its run. */
	void GetFades(const FWidgetAnimLayer& Layer, const FLayerModel& Model, float& OutFadeIn, float& OutFadeOut)
	{
		const FWidgetAnimSoundCueParams* Sound = GetSoundParams(Layer);
		OutFadeIn = Sound ? FMath::Clamp(Sound->FadeIn, 0.f, Model.RunSeconds) : 0.f;
		OutFadeOut = Sound ? FMath::Clamp(Sound->FadeOut, 0.f, Model.RunSeconds) : 0.f;
	}

	/** How loud a Sound cue is LocalSeconds into its run, as its fades have it: 0 silent, 1 its full volume. */
	float EnvelopeAt(const FWidgetAnimLayer& Layer, const FLayerModel& Model, float LocalSeconds)
	{
		const FWidgetAnimSoundCueParams* Sound = GetSoundParams(Layer);
		if (!Sound)
		{
			return 1.f;
		}
		float FadeIn = 0.f;
		float FadeOut = 0.f;
		GetFades(Layer, Model, FadeIn, FadeOut);
		float Level = 1.f;
		if (FadeIn > KINDA_SMALL_NUMBER && LocalSeconds < FadeIn)
		{
			Level = FMath::Min(Level, FadeShape(Sound->FadeCurve, LocalSeconds / FadeIn));
		}
		if (FadeOut > KINDA_SMALL_NUMBER && LocalSeconds > Model.RunSeconds - FadeOut)
		{
			Level = FMath::Min(Level, FadeShape(Sound->FadeCurve, (Model.RunSeconds - LocalSeconds) / FadeOut));
		}
		return FMath::Clamp(Level, 0.f, 1.f);
	}

	/** Point Point's value of Channel: on a keyed track, point 0 is its start value and the last its end value. */
	float GetPointValue(const FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, EChannel Channel)
	{
		const int32 NumPoints = Model.NumValuePoints;
		if (Channel == EChannel::Progress)
		{
			return NumPoints > 1 ? static_cast<float>(FMath::Clamp(Point, 0, NumPoints - 1)) / static_cast<float>(NumPoints - 1) : 0.f;
		}
		if (Channel == EChannel::FadeEnvelope)
		{
			return 1.f;
		}
		if (IsFieldChannel(Channel))
		{
			return ReadField(Layer, Model, Point, GetFieldIndex(Channel));
		}
		if (Model.bKeyframed)
		{
			// A key that Uses Authored Transform is the identity - the widget as designed.
			const int32 Clamped = FMath::Clamp(Point, 0, NumPoints - 1);
			return Layer.Transform.Keys.IsValidIndex(Clamped) ? ReadTransform(Layer.Transform.GetKeyTransform(Clamped), Channel) : 0.f;
		}
		if (IsColorChannel(Channel))
		{
			return ReadColor(WidgetAnimKeyPoints::GetColor(Layer, Point), Channel);
		}
		// A Scale's X and Y; every other track's one value.
		return Channel == EChannel::ScaleY ? WidgetAnimKeyPoints::GetValueY(Layer, Point) : WidgetAnimKeyPoints::GetValue(Layer, Point);
	}

	/**
	 * Whether a point's value of Channel can be set here. A registered type's start and end come from its own settings and
	 * a Slide always ends where its widget sits, so those are the Selection panel's, or nobody's, to change - but for a
	 * Sound's, which are its Volume. A key that uses the authored transform or shape holds no values of its own to set.
	 */
	bool CanSetPointValue(const FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, EChannel Channel)
	{
		if (!Model.bEditable || Channel == EChannel::Progress || Channel == EChannel::FadeEnvelope || Point < 0 || Point >= Model.NumValuePoints)
		{
			return false;
		}
		if (Model.bKeyframed)
		{
			if (!Layer.Transform.Keys.IsValidIndex(Point))
			{
				return false;
			}
			const FWidgetTransformKeyframe& Key = Layer.Transform.Keys[Point];
			return IsFieldChannel(Channel) ? !Key.bUseAuthoredShape : !Key.bUseAuthoredTransform;
		}

		const bool bEndpoint = Point == 0 || Point == Model.NumValuePoints - 1;
		if (!bEndpoint)
		{
			return true;
		}
		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:
		case EWidgetAnimType::Scale:
		case EWidgetAnimType::Rotate:
		case EWidgetAnimType::MaterialScalar:
		case EWidgetAnimType::MaterialVector:
			return true;
		case EWidgetAnimType::Slide:
			return Point == 0;
		case EWidgetAnimType::Custom:
			return GetSoundParams(Layer) != nullptr;
		default:
			return false;
		}
	}

	void SetPointValue(FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Point, EChannel Channel, float Value)
	{
		if (IsFieldChannel(Channel))
		{
			WriteField(Layer, Model, Point, GetFieldIndex(Channel), Value);
			return;
		}
		if (Model.bKeyframed)
		{
			if (Layer.Transform.Keys.IsValidIndex(Point) && !Layer.Transform.Keys[Point].bUseAuthoredTransform)
			{
				WriteTransform(Layer.Transform.Keys[Point].Transform, Channel, Value);
			}
			return;
		}

		const int32 Last = Model.NumValuePoints - 1;
		if (Point > 0 && Point < Last && Layer.ValueKeys.Keys.IsValidIndex(Point - 1))
		{
			FWidgetAnimValueKey& Key = Layer.ValueKeys.Keys[Point - 1];
			if (IsColorChannel(Channel))
			{
				WriteColor(Key.Color, Channel, Value);
			}
			else if (Channel == EChannel::ScaleY)
			{
				Key.ValueY = Value;
			}
			else
			{
				Key.Value = Value;
			}
			return;
		}

		const bool bStart = Point <= 0;
		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:           (bStart ? Layer.StartOpacity : Layer.EndOpacity) = Value; break;
		case EWidgetAnimType::Scale:
		{
			FVector2D& Scale = bStart ? Layer.StartScale : Layer.EndScale;
			(Channel == EChannel::ScaleY ? Scale.Y : Scale.X) = Value;
			break;
		}
		case EWidgetAnimType::Rotate:         (bStart ? Layer.StartAngle : Layer.EndAngle) = Value; break;
		case EWidgetAnimType::MaterialScalar: (bStart ? Layer.StartScalarValue : Layer.EndScalarValue) = Value; break;
		case EWidgetAnimType::MaterialVector: WriteColor(bStart ? Layer.StartVectorValue : Layer.EndVectorValue, Channel, Value); break;
		// A Slide's Distance is how far away it starts - never less than nothing.
		case EWidgetAnimType::Slide:          if (bStart) { Layer.SlideDistance = FMath::Max(Value, 0.f); } break;
		// A Sound's start and end are its Volume - one setting, which both follow.
		case EWidgetAnimType::Custom:
			if (FWidgetAnimSoundCueParams* Sound = Layer.TypeParams.GetMutablePtr<FWidgetAnimSoundCueParams>())
			{
				Sound->Volume = FMath::Max(Value, 0.f);
			}
			break;
		default: break;
		}
	}

	/** What a channel is called in the list and on its curve - short, as the track it is on is named beside it: "B", "X". */
	FText GetChannelName(const FWidgetAnimLayer& Layer, const FLayerModel& Model, EChannel Channel)
	{
		if (IsFieldChannel(Channel))
		{
			return Model.Fields.IsValidIndex(GetFieldIndex(Channel)) ? Model.Fields[GetFieldIndex(Channel)].Name : FText::GetEmpty();
		}
		if (Layer.AnimType == EWidgetAnimType::Scale && (Channel == EChannel::ScaleX || Channel == EChannel::ScaleY))
		{
			return Channel == EChannel::ScaleX ? LOCTEXT("ChannelX", "X") : LOCTEXT("ChannelY", "Y");
		}
		switch (Channel)
		{
		case EChannel::ColorR:       return LOCTEXT("ChannelR", "R");
		case EChannel::ColorG:       return LOCTEXT("ChannelG", "G");
		case EChannel::ColorB:       return LOCTEXT("ChannelB", "B");
		case EChannel::ColorA:       return LOCTEXT("ChannelA", "A");
		case EChannel::TranslationX: return LOCTEXT("ChannelTranslationX", "Translation X");
		case EChannel::TranslationY: return LOCTEXT("ChannelTranslationY", "Translation Y");
		case EChannel::ScaleX:       return LOCTEXT("ChannelScaleX", "Scale X");
		case EChannel::ScaleY:       return LOCTEXT("ChannelScaleY", "Scale Y");
		case EChannel::ShearX:       return LOCTEXT("ChannelShearX", "Shear X");
		case EChannel::ShearY:       return LOCTEXT("ChannelShearY", "Shear Y");
		case EChannel::Angle:        return LOCTEXT("ChannelAngle", "Angle");
		case EChannel::Progress:     return LOCTEXT("ChannelProgress", "Through Its Keys");
		case EChannel::FadeEnvelope: return LOCTEXT("ChannelFade", "Fade");
		default: break;
		}

		switch (Layer.AnimType)
		{
		case EWidgetAnimType::Fade:           return LOCTEXT("ChannelOpacity", "Opacity");
		case EWidgetAnimType::Scale:          return LOCTEXT("ChannelScale", "Scale");
		case EWidgetAnimType::Slide:          return LOCTEXT("ChannelDistance", "Distance");
		case EWidgetAnimType::Rotate:         return LOCTEXT("ChannelRotateAngle", "Angle");
		case EWidgetAnimType::MaterialScalar:
			return Layer.ScalarParameterName.IsNone() ? LOCTEXT("ChannelParameter", "Parameter") : FText::FromName(Layer.ScalarParameterName);
		case EWidgetAnimType::Custom:
			if (const FWidgetAnimEvaluator* Evaluator = Layer.FindEvaluator())
			{
				const FText Label = Evaluator->GetTrackValueLabel(Layer.TypeParams);
				if (!Label.IsEmpty())
				{
					return Label;
				}
			}
			return LOCTEXT("ChannelValue", "Value");
		default:
			return LOCTEXT("ChannelValue", "Value");
		}
	}

	/** The second of a pair - Translation Y, Scale Y, Shear Y - is drawn dashed, so the two read apart in one color. */
	bool IsDashed(EChannel Channel)
	{
		return Channel == EChannel::TranslationY || Channel == EChannel::ScaleY || Channel == EChannel::ShearY || Channel == EChannel::ColorG || Channel == EChannel::ColorA;
	}

	/**
	 * The track's own color for every curve of it, a channel told apart by shade: the first as the track is drawn, then lighter
	 * and darker in turn - Translation and Scale, X and Y, each their own shade of the one color.
	 */
	FLinearColor GetChannelColor(const FLinearColor& TrackColor, int32 ChannelIndex)
	{
		if (ChannelIndex <= 0)
		{
			return TrackColor;
		}
		FLinearColor HSV = TrackColor.LinearRGBToHSV();
		const int32 Step = (ChannelIndex + 1) / 2;
		const bool bLighter = (ChannelIndex % 2) == 1;
		const float Amount = FMath::Min(Palette::GraphChannelShadeStep * Step, 0.6f);
		if (bLighter)
		{
			HSV.G = FMath::Max(HSV.G * (1.f - Amount), 0.f);
			HSV.B = FMath::Min(HSV.B + Amount * 0.5f, 1.f);
		}
		else
		{
			HSV.B = FMath::Max(HSV.B * (1.f - Amount), 0.12f);
		}
		FLinearColor Out = HSV.HSVToLinearRGB();
		Out.A = TrackColor.A;
		return Out;
	}

	// ---------------------------------------------------------------------------------------------
	// Segment curves
	// ---------------------------------------------------------------------------------------------

	const FWidgetAnimSegmentCurve* GetSegmentCurve(const FWidgetAnimLayer& Layer, int32 Segment)
	{
		if (Segment < 0)
		{
			return nullptr;
		}
		if (WidgetAnimTypeIsKeyframed(Layer.AnimType))
		{
			return Segment + 1 < Layer.Transform.Keys.Num() ? &Layer.Transform.Keys[Segment].CurveToNext : nullptr;
		}
		return Segment <= Layer.ValueKeys.Keys.Num() ? &Layer.ValueKeys.GetSegmentCurve(Segment) : nullptr;
	}

	FWidgetAnimSegmentCurve* GetMutableSegmentCurve(FWidgetAnimLayer& Layer, int32 Segment)
	{
		return const_cast<FWidgetAnimSegmentCurve*>(GetSegmentCurve(Layer, Segment));
	}

	/** A Tween Scale or Slide's Overshoot, in force while it has no keys - which a pair of handles can't draw exactly. */
	bool HasOvershoot(const FWidgetAnimLayer& Layer)
	{
		return Layer.Timing == EWidgetAnimTiming::Tween && Layer.ValueKeys.Keys.Num() == 0
			&& ((Layer.AnimType == EWidgetAnimType::Scale && Layer.Overshoot > 0.f) || (Layer.AnimType == EWidgetAnimType::Slide && Layer.SlideOvershoot > 0.f));
	}

	/**
	 * The handles a segment is drawn with. A Bezier segment's own; Linear, Ease In, Ease Out and Ease In Out - its own or the
	 * track's Easing it is left to - as the Bezier they are (bOutExact). Anything else - Hold, a Curve asset, an Overshoot - is
	 * shown with handles where a Bezier would start, hollow, to say that dragging one gives the segment handles of its own.
	 * False for a segment that is never shaped: a Spring track's, a cue's.
	 */
	bool GetSegmentHandles(const FWidgetAnimLayer& Layer, const FLayerModel& Model, int32 Segment, FVector2D& OutLeave, FVector2D& OutArrive, bool& bOutExact)
	{
		bOutExact = false;
		OutLeave = FVector2D(0.42, 0.0);
		OutArrive = FVector2D(0.58, 1.0);

		const FWidgetAnimSegmentCurve* Curve = Model.bCurves ? GetSegmentCurve(Layer, Segment) : nullptr;
		if (!Curve)
		{
			return false;
		}
		if (Curve->IsBezier())
		{
			OutLeave = Curve->LeaveHandle;
			OutArrive = Curve->ArriveHandle;
			bOutExact = true;
			return true;
		}

		// A track with no keys moves by its Easing unless its one segment is a Bezier: whatever else is left on that segment
		// isn't used.
		EWidgetAnimEasing Easing = EWidgetAnimEasing::Custom;
		if (Curve->IsTrackEasing() || Model.bKeyless)
		{
			if (Layer.IsValueDriven())
			{
				Easing = EWidgetAnimEasing::Linear;
			}
			else if (!(Model.bKeyless && HasOvershoot(Layer)))
			{
				Easing = Layer.Easing;
			}
		}
		else
		{
			switch (Curve->Easing)
			{
			case EWidgetAnimSegmentEasing::Linear:    Easing = EWidgetAnimEasing::Linear; break;
			case EWidgetAnimSegmentEasing::EaseIn:    Easing = EWidgetAnimEasing::EaseIn; break;
			case EWidgetAnimSegmentEasing::EaseOut:   Easing = EWidgetAnimEasing::EaseOut; break;
			case EWidgetAnimSegmentEasing::EaseInOut: Easing = EWidgetAnimEasing::EaseInOut; break;
			default: break;
			}
		}

		bOutExact = WidgetAnimEasing::GetBezierHandles(Easing, OutLeave, OutArrive);
		return true;
	}

	// ---------------------------------------------------------------------------------------------
	// Sampling - the runtime's own code, so what is drawn is what plays
	// ---------------------------------------------------------------------------------------------

	/** Where a track is at LocalSeconds (played, from its start): which segment, and how far across it - as it plays. */
	void Locate(const FLayerModel& Model, float LocalSeconds, int32& OutSegment, float& OutAlpha)
	{
		OutSegment = 0;
		OutAlpha = 0.f;
		const FWidgetAnimSettings& Settings = Model.Settings;

		if (Model.bKeyframed)
		{
			// As ApplyAnimationContribution walks a keyframed track: its keys in authored time, each segment eased by its own
			// curve or the track's Easing - or, spring timed, the spring walking the keys straight, overshooting past the last.
			const float Duration = Settings.Transform.GetDuration();
			float LocalTime = LocalSeconds / FMath::Max(Model.TimeScale, KINDA_SMALL_NUMBER);
			const bool bSpring = Settings.IsSpringTimed();
			if (bSpring && Duration > KINDA_SMALL_NUMBER)
			{
				LocalTime = UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Settings.Spring, FMath::Clamp(LocalTime / Duration, 0.f, 1.f), /*bTowardStart=*/false) * Duration;
			}
			auto EaseSegment = [&Settings](float Alpha) { return UWidgetAnimLibrary::EvaluateEasedAlpha(Settings, Alpha); };
			const TFunctionRef<float(float)> TrackEase(EaseSegment);
			UWidgetAnimTransformLibrary::FindSegment(Settings.Transform, LocalTime, bSpring ? nullptr : &TrackEase, /*bExtrapolate=*/bSpring, OutSegment, OutAlpha);
			return;
		}

		// A Sound cue fires at its points; between them its volume is drawn straight, as nothing eases it there.
		if (Model.bSound)
		{
			const int32 Last = Model.PointTimes.Num() - 1;
			while (OutSegment < Last - 1 && LocalSeconds >= Model.PointTimes[OutSegment + 1])
			{
				++OutSegment;
			}
			if (Last >= 1)
			{
				const float From = Model.PointTimes[OutSegment];
				const float To = Model.PointTimes[OutSegment + 1];
				OutAlpha = To - From > KINDA_SMALL_NUMBER ? FMath::Clamp((LocalSeconds - From) / (To - From), 0.f, 1.f) : 1.f;
			}
			return;
		}

		const float Alpha = Model.RunSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(LocalSeconds / Model.RunSeconds, 0.f, 1.f) : 1.f;
		UWidgetAnimLibrary::SampleValueKeys(Settings, Alpha, /*bTowardStart=*/false, OutSegment, OutAlpha);
	}

	/** A channel's value between two points, as the track blends it. */
	float ValueAt(const FWidgetAnimLayer& Layer, const FLayerModel& Model, EChannel Channel, int32 Segment, float Alpha)
	{
		if (Channel == EChannel::Progress)
		{
			const int32 Last = Model.NumValuePoints - 1;
			return Last > 0 ? FMath::Clamp((static_cast<float>(Segment) + Alpha) / static_cast<float>(Last), -1.f, 2.f) : 0.f;
		}
		if (Channel == EChannel::FadeEnvelope)
		{
			// Back to a moment on the run - a Sound's segments are straight in time - and its fades there.
			const float SegmentStart = Model.PointTimes.IsValidIndex(Segment) ? Model.PointTimes[Segment] : 0.f;
			const float SegmentEnd = Model.PointTimes.IsValidIndex(Segment + 1) ? Model.PointTimes[Segment + 1] : SegmentStart;
			return EnvelopeAt(Layer, Model, FMath::Lerp(SegmentStart, SegmentEnd, Alpha));
		}
		const float From = GetPointValue(Layer, Model, Segment, Channel);
		const float To = GetPointValue(Layer, Model, Segment + 1, Channel);
		return FMath::Lerp(From, To, Alpha);
	}

	/** How far along a Spring track's run its spring is at LocalSeconds - one curve for its whole run, its keys points along it. */
	float SpringProgress(const FLayerModel& Model, float LocalSeconds)
	{
		const float Alpha = Model.RunSeconds > KINDA_SMALL_NUMBER ? FMath::Clamp(LocalSeconds / Model.RunSeconds, 0.f, 1.f) : 1.f;
		return Model.bKeyframed
			? UWidgetAnimSpringLibrary::EvaluateDirectedProgress(Model.Settings.Spring, Alpha, /*bTowardStart=*/false)
			: UWidgetAnimLibrary::EvaluateTrackEase(Model.Settings, Alpha);
	}

	/** A step between grid lines near Approx, from 1, 2 and 5 times a power of ten. */
	float NiceStep(float Approx)
	{
		if (Approx <= KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}
		const float Power = FMath::Pow(10.f, FMath::FloorToFloat(FMath::LogX(10.f, Approx)));
		const float Fraction = Approx / Power;
		const float Nice = Fraction <= 1.f ? 1.f : Fraction <= 2.f ? 2.f : Fraction <= 5.f ? 5.f : 10.f;
		return Nice * Power;
	}

	/** A value as the graph reads it: no more places than its size needs. */
	FString FormatValue(float Value)
	{
		const float Abs = FMath::Abs(Value);
		FString Text = Abs >= 100.f ? FString::Printf(TEXT("%.1f"), Value) : Abs >= 1.f ? FString::Printf(TEXT("%.2f"), Value) : FString::Printf(TEXT("%.3f"), Value);
		if (Text.Contains(TEXT(".")))
		{
			while (Text.EndsWith(TEXT("0")))
			{
				Text.LeftChopInline(1);
			}
			if (Text.EndsWith(TEXT(".")))
			{
				Text.LeftChopInline(1);
			}
		}
		return Text == TEXT("-0") ? FString(TEXT("0")) : Text;
	}

	/** A value rounded as a drag leaves it, so values set by hand read cleanly in the Selection panel. */
	float RoundValue(float Value)
	{
		return FMath::RoundToFloat(Value * 1000.f) / 1000.f;
	}

	const FSlateBrush* WhiteBrush()
	{
		return FAppStyle::GetBrush("WhiteBrush");
	}

	const FSlateBrush* CircleBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 6.f);
		return &Brush;
	}

	const FSlateBrush* RingBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::Transparent, 6.f, FLinearColor::White, 1.5f);
		return &Brush;
	}

	const FSlateBrush* ReadoutBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::TooltipBackground, 4.f, Palette::TooltipOutline, 1.f);
		return &Brush;
	}

	/** The graph's icon: the editor's own Curve Editor icon, under whichever name this engine version has it, else the plugin's. */
	const FSlateBrush* GraphIcon()
	{
		const ISlateStyle& Style = FAppStyle::Get();
		for (const FName Name : { FName("GenericCurveEditor.TabIcon"), FName("CurveEditor.TabIcon"), FName("ClassIcon.CurveFloat"), FName("ClassIcon.CurveBase") })
		{
			if (const FSlateBrush* Brush = Style.GetOptionalBrush(Name, nullptr, nullptr))
			{
				return Brush;
			}
		}
		return FNameTBDStyle::FindIcon(FNameTBDStyle::GraphEditorIcon);
	}
}

const FSlateBrush* SWidgetAnimGraphEditor::GetGraphIcon()
{
	return WidgetAnimGraph::GraphIcon();
}

/** What the graph and its channel list share: the view, which channels are shown, and the keys selected. */
struct FWidgetAnimGraphState
{
	WidgetAnimGraph::EView View = WidgetAnimGraph::EView::Values;

	/** Several channels shown together are drawn each fitted to the same height - see SWidgetAnimGraphEditor. */
	bool bNormalize = true;

	TSet<uint64> HiddenChannels;

	/** The channel last picked in the list: drawn on top and brighter. */
	uint64 ActiveChannel = MAX_uint64;

	TArray<WidgetAnimGraph::FGraphKey> SelectedKeys;

	/**
	 * The tracks whose shape values have had their first showing: only those the keys change, the rest hidden - a Shape Box
	 * holds dozens, most of them never keyed. Once a track, until the graph is shown other tracks.
	 */
	TSet<int32> FieldDefaultsApplied;

	bool IsChannelShown(int32 Layer, WidgetAnimGraph::EChannel Channel) const
	{
		return !HiddenChannels.Contains(WidgetAnimGraph::ChannelId(Layer, Channel));
	}
};

// =====================================================================================================
// The canvas: the curves themselves
// =====================================================================================================

class SWidgetAnimGraphCanvas : public SLeafWidget
{
public:

	SLATE_BEGIN_ARGS(SWidgetAnimGraphCanvas) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InOwner, TSharedRef<FWidgetAnimGraphState> InState)
	{
		Owner = InOwner;
		State = InState;
	}

	const TArray<WidgetAnimGraph::FLayerModel>& GetModels() const { return Models; }

	void RequestFit() { bFitPending = true; }

	/** A channel's value at the playhead, for the channel list. False while the playhead is outside the track. */
	bool GetValueAtPlayhead(int32 LayerIndex, WidgetAnimGraph::EChannel Channel, float& OutValue) const;

	/** The segments the Curve To Next Key menu acts on: leaving each selected key, on a track that shapes them. */
	TArray<SWidgetAnimTimeline::FKeyRef> GetSelectedSegments() const;

	/** The Curve To Next Key menu, for the selected keys. */
	TSharedRef<SWidget> MakeCurveMenu();

	/** What the selected segments are set to, as the toolbar reads it. */
	FText GetSelectedCurveName() const;

	/** Every key of every shown channel selected - Ctrl+A. */
	void SelectAllKeys();

	void DeleteSelectedKeys();

	// SWidget
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(240.0, 140.0); }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

private:

	using EChannel = WidgetAnimGraph::EChannel;
	using FGraphKey = WidgetAnimGraph::FGraphKey;
	using FLayerModel = WidgetAnimGraph::FLayerModel;

	/** How a channel's values map to the graph's height while several are normalized: Base + (Value - Offset) / Scale. */
	struct FNorm
	{
		float Offset = 0.f;
		float Scale = 1.f;
		float Base = 0.f;
	};

	/** A key where it is drawn this frame. */
	struct FKeyHit
	{
		FGraphKey Key;
		int32 Model = INDEX_NONE;
		FVector2f Position;
		bool bTimeMovable = false;
		bool bValueEditable = false;
	};

	/** A segment's handle where it is drawn this frame, and the segment's two ends, for dragging it. */
	struct FHandleHit
	{
		int32 Model = INDEX_NONE;
		int32 Segment = INDEX_NONE;
		bool bArrive = false;
		EChannel Channel = EChannel::Value;
		FVector2f Position;
		FVector2f Anchor;
		bool bExact = false;
		bool bEditable = false;
	};

	/** One curve as drawn this frame. */
	struct FCurve
	{
		int32 Model = INDEX_NONE;
		EChannel Channel = EChannel::Value;
		TArray<FVector2f> Points;
		FLinearColor Color;
		bool bDashed = false;
		bool bActive = false;

		/** Faint lines holding its first and last values out to the edges of the view. */
		TArray<FVector2f> HoldBefore;
		TArray<FVector2f> HoldAfter;

		FVector2f LabelAt;
		FText Label;
	};

	enum class EDrag : uint8
	{
		None,
		Scrub,
		KeyPending,
		Keys,
		HandlePending,
		Handle,
		MarqueePending,
		Marquee,
		RightPending,
		Pan
	};

	// Reading the tracks
	void RebuildModels();
	void UpdateNorms();
	void UpdateFrame();
	void Fit();
	bool IsNormalized() const { return bNormalizedNow; }
	const FWidgetAnimLayer* GetLayer(int32 LayerIndex) const;
	FWidgetAnimLayer* GetMutableLayer(int32 LayerIndex) const;
	int32 FindModel(int32 LayerIndex) const;

	// Mapping
	float TimeToX(float Seconds) const;
	float XToTime(float X) const;
	float DisplayToY(float Display) const;
	float YToDisplay(float Y) const;
	float ToDisplay(int32 LayerIndex, EChannel Channel, float Value) const;
	float FromDisplay(int32 LayerIndex, EChannel Channel, float Display) const;
	float PlotWidth() const { return FMath::Max(LastSize.X - WidgetAnimGraph::GutterWidth, 1.f); }
	float PlotHeight() const { return FMath::Max(LastSize.Y - WidgetAnimGraph::RulerHeight - WidgetAnimGraph::BottomPadding, 1.f); }

	// Hit testing
	const FKeyHit* FindKeyAt(const FVector2f& Local) const;
	const FHandleHit* FindHandleAt(const FVector2f& Local) const;
	const FCurve* FindCurveAt(const FVector2f& Local) const;
	bool IsKeySelected(const FGraphKey& Key) const;

	// Selection
	void SelectKey(const FGraphKey& Key, bool bAdd, bool bToggle);
	void MirrorSelectionToPanel();

	// Editing
	void BeginKeyDrag();
	void UpdateKeyDrag(const FVector2f& Local, bool bShift);
	void BeginHandleDrag();
	void UpdateHandleDrag(const FVector2f& Local);
	void EndDrag();
	TSharedRef<SWidget> MakeContextMenu(const FVector2f& Local);
	void SetCurve(const TArray<SWidgetAnimTimeline::FKeyRef>& Segments, WidgetAnimGraph::ECurvePreset Preset);
	void PushMenu(const FPointerEvent& MouseEvent, const TSharedRef<SWidget>& Menu);

	TWeakPtr<SWidgetAnimTimeline> Owner;
	TSharedPtr<FWidgetAnimGraphState> State;

	TArray<FLayerModel> Models;
	TMap<uint64, FNorm> Norms;
	bool bNormsFrozen = false;
	bool bNormalizedNow = false;

	TArray<FCurve> Curves;
	TArray<FKeyHit> KeyHits;
	TArray<FHandleHit> HandleHits;

	/** The view: the seconds across, and the height up (values, normalized values, or 0-1 of a segment's ease). */
	FVector2f ViewTime = FVector2f(0.f, 1.f);
	FVector2f ViewValue = FVector2f(-0.1f, 1.1f);
	FVector2f LastSize = FVector2f(400.f, 200.f);
	bool bFitPending = true;
	WidgetAnimGraph::EView LastView = WidgetAnimGraph::EView::Values;
	bool bLastNormalized = false;
	int32 LastNumShown = INDEX_NONE;

	// What a press and its drag are doing.
	EDrag Drag = EDrag::None;
	FVector2f PressLocal = FVector2f::ZeroVector;
	FVector2f LastMouse = FVector2f::ZeroVector;
	bool bHasMouse = false;
	FGraphKey PressKey;
	FHandleHit PressHandle;
	TUniquePtr<FScopedTransaction> DragTransaction;

	/** A key drag: every key it moves and where each was - its time once per key, its value per channel. */
	struct FMovingKey
	{
		FGraphKey Key;
		int32 Model = INDEX_NONE;
		float OriginalTime = 0.f;
		float OriginalDisplay = 0.f;
		bool bTime = false;
		bool bValue = false;
	};
	TArray<FMovingKey> Moving;
	float PressTime = 0.f;
	float PressDisplay = 0.f;

	/** A handle drag: its segment's handles at the press, the other one kept where it was. */
	FVector2D PressLeave;
	FVector2D PressArrive;
	float PressSegmentStart = 0.f;
	float PressSegmentEnd = 1.f;
	float PressD0 = 0.f;
	float PressD1 = 1.f;

	/** The selection box, in local pixels, and the keys selected when it began (kept with Shift or Ctrl). */
	FVector2f MarqueeEnd = FVector2f::ZeroVector;
	TArray<FGraphKey> MarqueeBase;
};

const FWidgetAnimLayer* SWidgetAnimGraphCanvas::GetLayer(int32 LayerIndex) const
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Panel ? Panel->GetLayers() : nullptr;
	return Layers && Layers->IsValidIndex(LayerIndex) ? &(*Layers)[LayerIndex] : nullptr;
}

FWidgetAnimLayer* SWidgetAnimGraphCanvas::GetMutableLayer(int32 LayerIndex) const
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	IWidgetAnimPanelSource* Source = Panel ? Panel->GetSource() : nullptr;
	TArray<FWidgetAnimLayer>* Layers = Source ? Source->GetLayers() : nullptr;
	return Layers && Layers->IsValidIndex(LayerIndex) ? &(*Layers)[LayerIndex] : nullptr;
}

int32 SWidgetAnimGraphCanvas::FindModel(int32 LayerIndex) const
{
	return Models.IndexOfByPredicate([LayerIndex](const FLayerModel& Model) { return Model.Layer == LayerIndex; });
}

void SWidgetAnimGraphCanvas::RebuildModels()
{
	using namespace WidgetAnimGraph;

	Models.Reset();
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Panel ? Panel->GetLayers() : nullptr;
	if (!Panel || !Layers)
	{
		return;
	}

	TArray<FVector2f> Spans;
	Panel->GetLayerSpans(Spans);
	const float TimeScale = FMath::Max(Panel->GetTimeScale(), KINDA_SMALL_NUMBER);

	for (const int32 LayerIndex : Panel->GetGraphLayers())
	{
		if (!Layers->IsValidIndex(LayerIndex) || !Panel->CanGraphLayer(LayerIndex))
		{
			continue;
		}

		const FWidgetAnimLayer& Layer = (*Layers)[LayerIndex];
		FLayerModel& Model = Models.AddDefaulted_GetRef();
		Model.Layer = LayerIndex;
		Model.Settings = Layer.ToSettings();
		Model.SpanStart = Spans.IsValidIndex(LayerIndex) ? Spans[LayerIndex].X : 0.f;
		Model.TimeScale = TimeScale;
		Model.bKeyframed = WidgetAnimTypeIsKeyframed(Layer.AnimType);
		Model.bEditable = Panel->CanEditKeys(LayerIndex);
		Model.bCurves = Panel->UsesSegmentCurves(LayerIndex);
		Model.Color = SWidgetAnimTimeline::GetLayerTypeColor(Layer);
		Model.Name = SWidgetAnimTimeline::GetLayerDisplayName(Layer);

		if (Model.bKeyframed)
		{
			if (const FRichCurve* Curve = Layer.Transform.GetActiveCurve())
			{
				for (const FRichCurveKey& Key : Curve->GetConstRefOfKeys())
				{
					Model.PointTimes.Add(Key.Time * TimeScale);
				}
			}
			Model.NumValuePoints = FMath::Min(Model.PointTimes.Num(), Layer.Transform.Keys.Num());
			Model.PointTimes.SetNum(Model.NumValuePoints);
			Model.RunSeconds = Model.PointTimes.Num() > 0 ? Model.PointTimes.Last() : 0.f;

			if (Layer.AnimType == EWidgetAnimType::Transform)
			{
				Model.Channels = { EChannel::TranslationX, EChannel::TranslationY, EChannel::ScaleX, EChannel::ScaleY, EChannel::ShearX, EChannel::ShearY, EChannel::Angle };
				continue;
			}

			// A Shape Box or Shape track: every number its keys' shapes hold, each a channel of its own - and, for a key that
			// Uses Authored Shape, the shape as it was designed in the Widget Blueprint to read them off.
			BuildFields(Layer, Model.Fields);
			IWidgetAnimPanelSource* Source = Panel->GetSource();
			UWidget* Preview = Source ? Source->GetLayerPreviewWidget(LayerIndex) : nullptr;
			UWidget* Target = Layer.AnimType == EWidgetAnimType::ShapeBox
				? static_cast<UWidget*>(UWidgetAnimLibrary::ResolveShapeBox(Preview))
				: static_cast<UWidget*>(WidgetAnimShapeTrack::ResolveShape(Preview, Layer.Transform.ShapeKind.Get()));
			const UWidgetBlueprint* WidgetBlueprint = Source ? Source->GetWidgetBlueprint() : nullptr;
			const UWidget* Designed = Target && WidgetBlueprint && WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->FindWidget(Target->GetFName()) : nullptr;
			if (const UShapeBox* DesignedBox = Cast<UShapeBox>(Designed))
			{
				Model.AuthoredBox = DesignedBox->GetShape();
			}
			else if (Designed)
			{
				Model.AuthoredShape = Designed;
			}

			for (int32 FieldIndex = 0; FieldIndex < Model.Fields.Num(); ++FieldIndex)
			{
				Model.Channels.Add(MakeFieldChannel(FieldIndex));
			}
			if (Model.Channels.Num() == 0)
			{
				Model.Channels = { EChannel::Progress };
			}
			continue;
		}

		// A keyed track's points always: its start value, every key between, its end value - even before it has a key, which
		// is where a graph editor is most often used: shaping the one move a track makes.
		Model.bKeyless = Layer.ValueKeys.Keys.Num() == 0;
		Model.RunSeconds = Panel->GetShownRunLength(LayerIndex) * TimeScale;
		Model.NumValuePoints = Layer.ValueKeys.NumPoints();
		for (int32 Point = 0; Point < Model.NumValuePoints; ++Point)
		{
			Model.PointTimes.Add(Layer.ValueKeys.GetPointTime(Point) * Model.RunSeconds);
		}

		// A Sound cue: its volume at each point it fires, and its fades.
		Model.bSound = GetSoundParams(Layer) != nullptr;
		if (Model.bSound)
		{
			Model.Channels = { EChannel::Value, EChannel::FadeEnvelope };
			continue;
		}

		// A Scale: its X and its Y.
		if (WidgetAnimKeyPoints::HasTwoValues(Layer))
		{
			Model.Channels = { EChannel::ScaleX, EChannel::ScaleY };
			continue;
		}

		const EWidgetAnimKeyContent Content = Layer.AnimType == EWidgetAnimType::Custom ? WidgetAnimGetRegisteredKeyContent(Layer.TypeParams) : EWidgetAnimKeyContent::Value;
		if (Layer.AnimType != EWidgetAnimType::MaterialVector && Content != EWidgetAnimKeyContent::Color)
		{
			Model.Channels.Add(EChannel::Value);
		}
		if (WidgetAnimKeyPoints::HasColorKeys(Layer))
		{
			Model.Channels.Append({ EChannel::ColorR, EChannel::ColorG, EChannel::ColorB, EChannel::ColorA });
		}
	}
}

void SWidgetAnimGraphCanvas::UpdateNorms()
{
	using namespace WidgetAnimGraph;

	// Held still while anything is dragged: a range that followed the drag would move the very key under the mouse.
	if (bNormsFrozen)
	{
		return;
	}

	Norms.Reset();
	for (const FLayerModel& Model : Models)
	{
		const FWidgetAnimLayer* Layer = GetLayer(Model.Layer);
		if (!Layer)
		{
			continue;
		}
		for (const EChannel Channel : Model.Channels)
		{
			// The fades are a share of the volume, 0 to 1, whatever the volume.
			if (Channel == EChannel::FadeEnvelope)
			{
				FNorm Fade;
				Fade.Offset = 0.f;
				Fade.Scale = 1.f;
				Norms.Add(ChannelId(Model.Layer, Channel), Fade);
				continue;
			}

			float Min = TNumericLimits<float>::Max();
			float Max = -TNumericLimits<float>::Max();
			for (int32 Point = 0; Point < Model.NumValuePoints; ++Point)
			{
				const float Value = GetPointValue(*Layer, Model, Point, Channel);
				Min = FMath::Min(Min, Value);
				Max = FMath::Max(Max, Value);
			}
			FNorm Norm;
			if (Model.NumValuePoints == 0)
			{
				Norms.Add(ChannelId(Model.Layer, Channel), Norm);
				continue;
			}

			// A channel that never changes sits in the middle; one that does fills the height from its lowest to its highest key.
			Norm.Offset = Min;
			if (Max - Min > KINDA_SMALL_NUMBER)
			{
				Norm.Scale = Max - Min;
			}
			else
			{
				Norm.Scale = FMath::Max(FMath::Abs(Min), 1.f);
				Norm.Base = 0.5f;
			}
			Norms.Add(ChannelId(Model.Layer, Channel), Norm);
		}
	}
}

float SWidgetAnimGraphCanvas::TimeToX(float Seconds) const
{
	const float Range = FMath::Max(ViewTime.Y - ViewTime.X, KINDA_SMALL_NUMBER);
	return WidgetAnimGraph::GutterWidth + (Seconds - ViewTime.X) / Range * PlotWidth();
}

float SWidgetAnimGraphCanvas::XToTime(float X) const
{
	return ViewTime.X + (X - WidgetAnimGraph::GutterWidth) / PlotWidth() * (ViewTime.Y - ViewTime.X);
}

float SWidgetAnimGraphCanvas::DisplayToY(float Display) const
{
	const float Range = FMath::Max(ViewValue.Y - ViewValue.X, KINDA_SMALL_NUMBER);
	return WidgetAnimGraph::RulerHeight + (ViewValue.Y - Display) / Range * PlotHeight();
}

float SWidgetAnimGraphCanvas::YToDisplay(float Y) const
{
	return ViewValue.Y - (Y - WidgetAnimGraph::RulerHeight) / PlotHeight() * (ViewValue.Y - ViewValue.X);
}

float SWidgetAnimGraphCanvas::ToDisplay(int32 LayerIndex, EChannel Channel, float Value) const
{
	if (!IsNormalized())
	{
		return Value;
	}
	const FNorm* Norm = Norms.Find(WidgetAnimGraph::ChannelId(LayerIndex, Channel));
	return Norm ? Norm->Base + (Value - Norm->Offset) / Norm->Scale : Value;
}

float SWidgetAnimGraphCanvas::FromDisplay(int32 LayerIndex, EChannel Channel, float Display) const
{
	if (!IsNormalized())
	{
		return Display;
	}
	const FNorm* Norm = Norms.Find(WidgetAnimGraph::ChannelId(LayerIndex, Channel));
	return Norm ? Norm->Offset + (Display - Norm->Base) * Norm->Scale : Display;
}

void SWidgetAnimGraphCanvas::Fit()
{
	using namespace WidgetAnimGraph;

	bFitPending = false;
	if (Models.Num() == 0)
	{
		return;
	}

	float TimeMin = TNumericLimits<float>::Max();
	float TimeMax = -TNumericLimits<float>::Max();
	for (const FLayerModel& Model : Models)
	{
		TimeMin = FMath::Min(TimeMin, Model.SpanStart);
		TimeMax = FMath::Max(TimeMax, Model.SpanStart + Model.RunSeconds);
	}
	const float TimeRange = FMath::Max(TimeMax - TimeMin, 0.25f);
	ViewTime = FVector2f(TimeMin - TimeRange * 0.06f, TimeMin + TimeRange * 1.06f);

	// The value range: every shown curve, sampled as drawn, overshoots included - or, easing, 0 to 1 and any overshoot.
	float Min = TNumericLimits<float>::Max();
	float Max = -TNumericLimits<float>::Max();
	auto Include = [&Min, &Max](float Value)
	{
		Min = FMath::Min(Min, Value);
		Max = FMath::Max(Max, Value);
	};

	const bool bEase = State->View == EView::Ease;
	if (bEase)
	{
		Include(0.f);
		Include(1.f);
	}

	constexpr int32 NumSamples = 96;
	for (const FLayerModel& Model : Models)
	{
		const FWidgetAnimLayer* Layer = GetLayer(Model.Layer);
		if (!Layer || Model.RunSeconds <= 0.f)
		{
			continue;
		}
		for (int32 Sample = 0; Sample <= NumSamples; ++Sample)
		{
			const float Local = Model.RunSeconds * static_cast<float>(Sample) / NumSamples;
			int32 Segment = 0;
			float Alpha = 0.f;
			Locate(Model, Local, Segment, Alpha);
			if (bEase)
			{
				Include(Model.Settings.IsSpringTimed() ? SpringProgress(Model, Local) : Alpha);
				continue;
			}
			for (const EChannel Channel : Model.Channels)
			{
				if (State->IsChannelShown(Model.Layer, Channel))
				{
					Include(ToDisplay(Model.Layer, Channel, ValueAt(*Layer, Model, Channel, Segment, Alpha)));
				}
			}
		}
	}

	if (Min > Max)
	{
		Min = 0.f;
		Max = 1.f;
	}
	if (Max - Min < KINDA_SMALL_NUMBER)
	{
		const float Spread = FMath::Max(FMath::Abs(Min) * 0.25f, 0.5f);
		Min -= Spread;
		Max += Spread;
	}
	const float Pad = (Max - Min) * 0.12f;
	ViewValue = FVector2f(Min - Pad, Max + Pad);
}

void SWidgetAnimGraphCanvas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	LastSize = AllottedGeometry.GetLocalSize();

	RebuildModels();

	// A Shape Box or Shape track's values, the first time it is shown: those its keys change, the rest hidden until wanted -
	// or, keys changing none, its first few, so there is something to see.
	for (const FLayerModel& Model : Models)
	{
		if (Model.Fields.Num() == 0 || State->FieldDefaultsApplied.Contains(Model.Layer))
		{
			continue;
		}
		State->FieldDefaultsApplied.Add(Model.Layer);
		const FWidgetAnimLayer* Layer = GetLayer(Model.Layer);
		if (!Layer)
		{
			continue;
		}
		TArray<uint64> Unchanging;
		for (const EChannel Channel : Model.Channels)
		{
			float Min = TNumericLimits<float>::Max();
			float Max = -TNumericLimits<float>::Max();
			for (int32 Point = 0; Point < Model.NumValuePoints; ++Point)
			{
				const float Value = WidgetAnimGraph::GetPointValue(*Layer, Model, Point, Channel);
				Min = FMath::Min(Min, Value);
				Max = FMath::Max(Max, Value);
			}
			if (Max - Min <= KINDA_SMALL_NUMBER)
			{
				Unchanging.Add(WidgetAnimGraph::ChannelId(Model.Layer, Channel));
			}
		}
		const int32 NumToHide = Unchanging.Num() == Model.Channels.Num() ? FMath::Max(Unchanging.Num() - 3, 0) : Unchanging.Num();
		for (int32 Index = Unchanging.Num() - NumToHide; Index < Unchanging.Num(); ++Index)
		{
			State->HiddenChannels.Add(Unchanging[Index]);
		}
	}

	// A key or a channel no longer there - an undo, a key deleted elsewhere - drops out of the selection.
	State->SelectedKeys.RemoveAll([this](const FGraphKey& Key)
	{
		const int32 ModelIndex = FindModel(Key.Layer);
		if (ModelIndex != INDEX_NONE && Key.Channel == EChannel::FadeEnvelope)
		{
			return !Models[ModelIndex].bSound || (Key.Point != WidgetAnimGraph::FadeInPoint && Key.Point != WidgetAnimGraph::FadeOutPoint);
		}
		return ModelIndex == INDEX_NONE || Key.Point < 0 || Key.Point >= Models[ModelIndex].NumValuePoints;
	});

	// How many channels are shown decides whether they are normalized; a change - and a change of view - fits the heights again.
	int32 NumShown = 0;
	for (const FLayerModel& Model : Models)
	{
		for (const EChannel Channel : Model.Channels)
		{
			NumShown += State->IsChannelShown(Model.Layer, Channel) ? 1 : 0;
		}
	}
	bNormalizedNow = State->View == WidgetAnimGraph::EView::Values && State->bNormalize && NumShown > 1;
	UpdateNorms();

	if (Drag == EDrag::None && (State->View != LastView || bNormalizedNow != bLastNormalized || NumShown != LastNumShown))
	{
		bFitPending = true;
	}
	LastView = State->View;
	bLastNormalized = bNormalizedNow;
	LastNumShown = NumShown;

	if (bFitPending && Drag == EDrag::None)
	{
		Fit();
	}

	UpdateFrame();
}

void SWidgetAnimGraphCanvas::UpdateFrame()
{
	using namespace WidgetAnimGraph;

	Curves.Reset();
	KeyHits.Reset();
	HandleHits.Reset();

	const bool bEase = State->View == EView::Ease;
	const float PlotLeft = GutterWidth;
	const float PlotRight = LastSize.X;
	const float StepSeconds = (ViewTime.Y - ViewTime.X) / PlotWidth() * 2.f;

	for (int32 ModelIndex = 0; ModelIndex < Models.Num(); ++ModelIndex)
	{
		const FLayerModel& Model = Models[ModelIndex];
		const FWidgetAnimLayer* Layer = GetLayer(Model.Layer);
		if (!Layer || Model.NumValuePoints == 0)
		{
			continue;
		}

		// Sample times across the track's run in view - every couple of pixels, and exactly at each key, so a Hold's jump and
		// a key's corner are where they are.
		const float From = FMath::Max(0.f, ViewTime.X - Model.SpanStart);
		const float To = FMath::Min(Model.RunSeconds, ViewTime.Y - Model.SpanStart);
		TArray<float> Times;
		if (To >= From)
		{
			for (float Local = From; Local < To; Local += FMath::Max(StepSeconds, 1.e-4f))
			{
				Times.Add(Local);
			}
			Times.Add(To);
			for (const float KeyTime : Model.PointTimes)
			{
				if (KeyTime >= From && KeyTime <= To)
				{
					Times.Add(KeyTime);
					Times.Add(FMath::Max(KeyTime - 1.e-4f, 0.f));
				}
			}
			// A Sound's fades turn at their ends: sampled there too, so the corners are where they are.
			if (Model.bSound)
			{
				float FadeIn = 0.f;
				float FadeOut = 0.f;
				GetFades(*Layer, Model, FadeIn, FadeOut);
				for (const float FadeTime : { FadeIn, Model.RunSeconds - FadeOut })
				{
					if (FadeTime >= From && FadeTime <= To)
					{
						Times.Add(FadeTime);
					}
				}
			}
			Times.Sort();
		}

		if (bEase)
		{
			// The Ease view: each segment rising from 0 at its key to 1 at the next - or, for a Spring track, its one spring
			// across the whole run.
			FCurve Curve;
			Curve.Model = ModelIndex;
			Curve.Channel = EChannel::Progress;
			Curve.Color = Model.Color;
			Curve.Label = Model.Name;
			Curve.bActive = true;

			const bool bSpring = Model.Settings.IsSpringTimed();
			int32 PreviousSegment = INDEX_NONE;
			for (const float Local : Times)
			{
				int32 Segment = 0;
				float Alpha = 0.f;
				Locate(Model, Local, Segment, Alpha);
				const float Y = bSpring ? SpringProgress(Model, Local) : Alpha;

				// A new segment starts its own rise: the last one is drawn up to 1 at the key, and this one from 0.
				if (!bSpring && PreviousSegment != INDEX_NONE && Segment != PreviousSegment && Curve.Points.Num() > 0)
				{
					Curve.Points.Add(FVector2f(TimeToX(Model.SpanStart + Local), DisplayToY(1.f)));
					Curves.Add(Curve);
					Curve.Points.Reset();
					Curve.Points.Add(FVector2f(TimeToX(Model.SpanStart + Local), DisplayToY(0.f)));
				}
				PreviousSegment = Segment;
				Curve.Points.Add(FVector2f(TimeToX(Model.SpanStart + Local), DisplayToY(Y)));
			}
			if (Curve.Points.Num() > 0)
			{
				Curve.LabelAt = Curve.Points.Last();
				Curves.Add(Curve);
			}

			// Its keys along the bottom - each where its segment starts - and the last one at the top, where the run ends.
			for (int32 Point = 0; Point < Model.NumValuePoints; ++Point)
			{
				FKeyHit& Hit = KeyHits.AddDefaulted_GetRef();
				Hit.Key = FGraphKey{ Model.Layer, Point, EChannel::Progress };
				Hit.Model = ModelIndex;
				Hit.Position = FVector2f(TimeToX(Model.SpanStart + Model.PointTimes[Point]), DisplayToY(Point == Model.NumValuePoints - 1 && Point > 0 ? 1.f : 0.f));
				Hit.bTimeMovable = Model.bEditable && (Model.bKeyframed || (Point > 0 && Point < Model.NumValuePoints - 1));
			}

			// Every segment's handles: the Ease view is where they are shaped.
			for (int32 Segment = 0; Segment + 1 < Model.NumValuePoints; ++Segment)
			{
				FVector2D Leave;
				FVector2D Arrive;
				bool bExact = false;
				if (!GetSegmentHandles(*Layer, Model, Segment, Leave, Arrive, bExact))
				{
					continue;
				}
				const float T0 = Model.SpanStart + Model.PointTimes[Segment];
				const float T1 = Model.SpanStart + Model.PointTimes[Segment + 1];
				for (const bool bArrive : { false, true })
				{
					const FVector2D& Handle = bArrive ? Arrive : Leave;
					FHandleHit& Hit = HandleHits.AddDefaulted_GetRef();
					Hit.Model = ModelIndex;
					Hit.Segment = Segment;
					Hit.bArrive = bArrive;
					Hit.Channel = EChannel::Progress;
					Hit.Position = FVector2f(TimeToX(FMath::Lerp(T0, T1, static_cast<float>(Handle.X))), DisplayToY(static_cast<float>(Handle.Y)));
					Hit.Anchor = bArrive ? FVector2f(TimeToX(T1), DisplayToY(1.f)) : FVector2f(TimeToX(T0), DisplayToY(0.f));
					Hit.bExact = bExact;
					Hit.bEditable = Model.bEditable;
				}
			}
			continue;
		}

		// The Values view: a curve per shown channel.
		for (int32 ChannelIndex = 0; ChannelIndex < Model.Channels.Num(); ++ChannelIndex)
		{
			const EChannel Channel = Model.Channels[ChannelIndex];
			if (!State->IsChannelShown(Model.Layer, Channel))
			{
				continue;
			}

			FCurve Curve;
			Curve.Model = ModelIndex;
			Curve.Channel = Channel;
			Curve.Color = GetChannelColor(Model.Color, ChannelIndex);
			Curve.bDashed = IsDashed(Channel);
			Curve.bActive = State->ActiveChannel == ChannelId(Model.Layer, Channel);
			// Just the channel - "B", not "Material Vector B": the graph shows one track at a time, named in the list beside it.
			Curve.Label = GetChannelName(*Layer, Model, Channel);

			for (const float Local : Times)
			{
				int32 Segment = 0;
				float Alpha = 0.f;
				Locate(Model, Local, Segment, Alpha);
				const float Value = ValueAt(*Layer, Model, Channel, Segment, Alpha);
				Curve.Points.Add(FVector2f(TimeToX(Model.SpanStart + Local), DisplayToY(ToDisplay(Model.Layer, Channel, Value))));
			}

			// Before it starts it holds its first value, and after it ends its last - drawn faintly to the view's edges. A Sound's
			// fades are silent either side of its run.
			const bool bFade = Channel == EChannel::FadeEnvelope;
			const float FirstValue = bFade ? EnvelopeAt(*Layer, Model, 0.f) : GetPointValue(*Layer, Model, 0, Channel);
			const float LastValue = bFade ? EnvelopeAt(*Layer, Model, Model.RunSeconds) : GetPointValue(*Layer, Model, Model.NumValuePoints - 1, Channel);
			const float FirstY = DisplayToY(ToDisplay(Model.Layer, Channel, FirstValue));
			const float LastY = DisplayToY(ToDisplay(Model.Layer, Channel, LastValue));
			const float StartX = TimeToX(Model.SpanStart);
			const float EndX = TimeToX(Model.SpanStart + Model.RunSeconds);
			if (StartX > PlotLeft)
			{
				Curve.HoldBefore = { FVector2f(PlotLeft, FirstY), FVector2f(FMath::Min(StartX, PlotRight), FirstY) };
			}
			if (EndX < PlotRight)
			{
				Curve.HoldAfter = { FVector2f(FMath::Max(EndX, PlotLeft), LastY), FVector2f(PlotRight, LastY) };
			}
			Curve.LabelAt = FVector2f(FMath::Clamp(EndX, PlotLeft, PlotRight), LastY);
			Curves.Add(Curve);

			// A Sound's fades: a key where its fade in ends and one where its fade out begins, each dragged in time to set it.
			if (bFade)
			{
				float FadeIn = 0.f;
				float FadeOut = 0.f;
				GetFades(*Layer, Model, FadeIn, FadeOut);
				for (const int32 Point : { FadeInPoint, FadeOutPoint })
				{
					const float Local = Point == FadeInPoint ? FadeIn : Model.RunSeconds - FadeOut;
					FKeyHit& Hit = KeyHits.AddDefaulted_GetRef();
					Hit.Key = FGraphKey{ Model.Layer, Point, Channel };
					Hit.Model = ModelIndex;
					Hit.Position = FVector2f(TimeToX(Model.SpanStart + Local), DisplayToY(ToDisplay(Model.Layer, Channel, EnvelopeAt(*Layer, Model, Local))));
					Hit.bTimeMovable = Model.bEditable;
					Hit.bValueEditable = false;
				}
				continue;
			}

			for (int32 Point = 0; Point < Model.NumValuePoints; ++Point)
			{
				FKeyHit& Hit = KeyHits.AddDefaulted_GetRef();
				Hit.Key = FGraphKey{ Model.Layer, Point, Channel };
				Hit.Model = ModelIndex;
				Hit.Position = FVector2f(TimeToX(Model.SpanStart + Model.PointTimes[Point]),
					DisplayToY(ToDisplay(Model.Layer, Channel, GetPointValue(*Layer, Model, Point, Channel))));
				Hit.bTimeMovable = Model.bEditable && (Model.bKeyframed || (Point > 0 && Point < Model.NumValuePoints - 1));
				Hit.bValueEditable = CanSetPointValue(*Layer, Model, Point, Channel);
			}
		}

		// The handles of the segments either side of each selected key, drawn on its channel's curve - where the curve rises
		// or falls enough to have somewhere to put them.
		for (int32 Segment = 0; Segment + 1 < Model.NumValuePoints; ++Segment)
		{
			for (const FGraphKey& Selected : State->SelectedKeys)
			{
				if (Selected.Layer != Model.Layer || (Selected.Point != Segment && Selected.Point != Segment + 1)
					|| Selected.Channel == EChannel::Progress || !State->IsChannelShown(Model.Layer, Selected.Channel))
				{
					continue;
				}

				FVector2D Leave;
				FVector2D Arrive;
				bool bExact = false;
				if (!GetSegmentHandles(*Layer, Model, Segment, Leave, Arrive, bExact))
				{
					continue;
				}

				const float D0 = ToDisplay(Model.Layer, Selected.Channel, GetPointValue(*Layer, Model, Segment, Selected.Channel));
				const float D1 = ToDisplay(Model.Layer, Selected.Channel, GetPointValue(*Layer, Model, Segment + 1, Selected.Channel));
				if (FMath::Abs(DisplayToY(D1) - DisplayToY(D0)) < 6.f)
				{
					continue;
				}

				const float T0 = Model.SpanStart + Model.PointTimes[Segment];
				const float T1 = Model.SpanStart + Model.PointTimes[Segment + 1];
				for (const bool bArrive : { false, true })
				{
					const bool bAlready = HandleHits.ContainsByPredicate([ModelIndex, Segment, bArrive, &Selected](const FHandleHit& Existing)
					{
						return Existing.Model == ModelIndex && Existing.Segment == Segment && Existing.bArrive == bArrive && Existing.Channel == Selected.Channel;
					});
					if (bAlready)
					{
						continue;
					}
					const FVector2D& Handle = bArrive ? Arrive : Leave;
					FHandleHit& Hit = HandleHits.AddDefaulted_GetRef();
					Hit.Model = ModelIndex;
					Hit.Segment = Segment;
					Hit.bArrive = bArrive;
					Hit.Channel = Selected.Channel;
					Hit.Position = FVector2f(TimeToX(FMath::Lerp(T0, T1, static_cast<float>(Handle.X))), DisplayToY(FMath::Lerp(D0, D1, static_cast<float>(Handle.Y))));
					Hit.Anchor = bArrive ? FVector2f(TimeToX(T1), DisplayToY(D1)) : FVector2f(TimeToX(T0), DisplayToY(D0));
					Hit.bExact = bExact;
					Hit.bEditable = Model.bEditable;
				}
			}
		}
	}

	// The active channel drawn last, on top of the rest.
	Curves.StableSort([](const FCurve& A, const FCurve& B) { return !A.bActive && B.bActive; });
}

bool SWidgetAnimGraphCanvas::GetValueAtPlayhead(int32 LayerIndex, EChannel Channel, float& OutValue) const
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const int32 ModelIndex = FindModel(LayerIndex);
	const FWidgetAnimLayer* Layer = GetLayer(LayerIndex);
	if (!Panel || ModelIndex == INDEX_NONE || !Layer)
	{
		return false;
	}

	const FLayerModel& Model = Models[ModelIndex];
	const float Local = FMath::Clamp(Panel->GetPlayheadSeconds() - Model.SpanStart, 0.f, Model.RunSeconds);
	int32 Segment = 0;
	float Alpha = 0.f;
	WidgetAnimGraph::Locate(Model, Local, Segment, Alpha);
	OutValue = WidgetAnimGraph::ValueAt(*Layer, Model, Channel, Segment, Alpha);
	return true;
}

// -----------------------------------------------------------------------------------------------------
// Painting
// -----------------------------------------------------------------------------------------------------

int32 SWidgetAnimGraphCanvas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace WidgetAnimGraph;

	const FVector2f Size = AllottedGeometry.GetLocalSize();
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo SmallFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const bool bEase = State->View == EView::Ease;

	auto Box = [&](int32 Layer, const FVector2f& Position, const FVector2f& BoxSize, const FSlateBrush* Brush, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)), Brush, ESlateDrawEffect::None, Color);
	};
	// On whole pixels: text placed between two pixels is drawn smeared across both.
	auto Text = [&](int32 Layer, const FString& String, const FVector2f& Position, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		const FVector2f At(FMath::RoundToFloat(Position.X), FMath::RoundToFloat(Position.Y));
		FSlateDrawElement::MakeText(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2f(600.f, 20.f), FSlateLayoutTransform(At)), String, Font, ESlateDrawEffect::None, Color);
	};
	auto Lines = [&](int32 Layer, const TArray<FVector2f>& Points, const FLinearColor& Color, float Thickness)
	{
		if (Points.Num() > 1)
		{
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Thickness);
		}
	};

	// Background, and the plot's frame.
	Box(LayerId, FVector2f::ZeroVector, Size, WhiteBrush(), FStyleColors::Recessed.GetSpecifiedColor());

	const int32 GridLayer = LayerId + 1;
	const int32 CurveLayer = LayerId + 3;
	const int32 HandleLayer = LayerId + 5;
	const int32 KeyLayer = LayerId + 7;
	const int32 OverlayLayer = LayerId + 9;

	if (Models.Num() == 0)
	{
		const FString Message = Tips::GraphNoTracks().ToString();
		const FVector2f MessageSize = FVector2f(Measure->Measure(Message, LabelFont));
		Text(OverlayLayer, Message, (Size - MessageSize) * 0.5f, LabelFont, FStyleColors::Foreground.GetSpecifiedColor().CopyWithNewOpacity(0.55f));
		return OverlayLayer;
	}

	// Each track's run, faintly in its color, behind everything.
	for (const FLayerModel& Model : Models)
	{
		const float X0 = FMath::Max(TimeToX(Model.SpanStart), GutterWidth);
		const float X1 = FMath::Min(TimeToX(Model.SpanStart + Model.RunSeconds), Size.X);
		if (X1 > X0)
		{
			Box(GridLayer, FVector2f(X0, RulerHeight), FVector2f(X1 - X0, Size.Y - RulerHeight), WhiteBrush(), Model.Color.CopyWithNewOpacity(Palette::GraphTrackRunTint));
		}
	}

	// Time grid and ruler.
	const float SecondsPerPixel = (ViewTime.Y - ViewTime.X) / PlotWidth();
	// In frames (the Time In dropdown's Frames), the steps are whole frames.
	const bool bFrameTime = Panel && Panel->IsFrameTime();
	const float FrameRate = SWidgetAnimTimeline::GetFrameRate();
	const float TimeStep = bFrameTime
		? FMath::Max(FMath::RoundToFloat(NiceStep(SecondsPerPixel * 90.f * FrameRate)), 1.f) / FrameRate
		: NiceStep(SecondsPerPixel * 90.f);
	Box(GridLayer, FVector2f::ZeroVector, FVector2f(Size.X, RulerHeight), WhiteBrush(), FStyleColors::Header.GetSpecifiedColor());
	for (float Mark = FMath::CeilToFloat(ViewTime.X / TimeStep) * TimeStep; Mark <= ViewTime.Y; Mark += TimeStep)
	{
		const float X = TimeToX(Mark);
		if (X < GutterWidth)
		{
			continue;
		}
		Box(GridLayer, FVector2f(X, RulerHeight), FVector2f(1.f, Size.Y - RulerHeight), WhiteBrush(), Palette::GridMajor);
		Box(GridLayer, FVector2f(X, RulerHeight - 6.f), FVector2f(1.f, 6.f), WhiteBrush(), Palette::GraphRulerTick);
		const FString Label = Panel ? Panel->FormatTime(FMath::Abs(Mark) < TimeStep * 0.01f ? 0.f : Mark, TimeStep < 0.1f ? 2 : 1) : FString::SanitizeFloat(Mark);
		Text(GridLayer + 1, Label, FVector2f(X + 3.f, 3.f), SmallFont, Palette::GraphRulerText);
	}
	for (float Mark = FMath::FloorToFloat(ViewTime.X / TimeStep) * TimeStep + TimeStep * 0.5f; Mark <= ViewTime.Y; Mark += TimeStep)
	{
		const float X = TimeToX(Mark);
		if (X >= GutterWidth)
		{
			Box(GridLayer, FVector2f(X, RulerHeight), FVector2f(1.f, Size.Y - RulerHeight), WhiteBrush(), Palette::GridMinor);
		}
	}

	// Value grid and gutter: values in their own units; normalized, or easing, as a share of the way.
	Box(GridLayer, FVector2f(0.f, RulerHeight), FVector2f(GutterWidth, Size.Y - RulerHeight), WhiteBrush(), Palette::GraphGutter);
	const float ValuePerPixel = (ViewValue.Y - ViewValue.X) / PlotHeight();
	const float ValueStep = NiceStep(ValuePerPixel * 40.f);
	for (float Mark = FMath::CeilToFloat(ViewValue.X / ValueStep) * ValueStep; Mark <= ViewValue.Y; Mark += ValueStep)
	{
		const float Y = DisplayToY(Mark);
		if (Y < RulerHeight)
		{
			continue;
		}
		const bool bZero = FMath::Abs(Mark) < ValueStep * 0.01f;
		Box(GridLayer, FVector2f(GutterWidth, Y), FVector2f(Size.X - GutterWidth, 1.f), WhiteBrush(), bZero ? Palette::GraphZeroLine : Palette::GridMajor);
		const FString Label = bEase || IsNormalized()
			? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Mark * 100.f))
			: FormatValue(bZero ? 0.f : Mark);
		Text(GridLayer + 1, Label, FVector2f(4.f, Y - 7.f), SmallFont, Palette::GraphRulerText);
	}

	// Normalized, the gutter says so - its numbers are a share of each channel's own range, not values.
	if (IsNormalized())
	{
		Text(OverlayLayer, Tips::GraphNormalizedLabel().ToString(), FVector2f(GutterWidth + 6.f, RulerHeight + 4.f), SmallFont, Palette::GraphRulerText);
	}

	// Curves: their holds faint and dashed, then each curve - the active one thicker, on top.
	for (const FCurve& Curve : Curves)
	{
		const FLinearColor HoldColor = Curve.Color.CopyWithNewOpacity(Palette::GraphHoldOpacity);
		Lines(CurveLayer, Curve.HoldBefore, HoldColor, 1.f);
		Lines(CurveLayer, Curve.HoldAfter, HoldColor, 1.f);

		const float Thickness = Curve.bActive ? 2.25f : 1.5f;
		if (!Curve.bDashed)
		{
			Lines(CurveLayer, Curve.Points, Curve.Color, Thickness);
		}
		else
		{
			// Dashed by length along the line: 7 pixels drawn, 4 left out.
			TArray<FVector2f> Dash;
			float Along = 0.f;
			for (int32 Index = 0; Index + 1 < Curve.Points.Num(); ++Index)
			{
				const FVector2f A = Curve.Points[Index];
				const FVector2f B = Curve.Points[Index + 1];
				const float Length = (B - A).Size();
				const bool bOn = FMath::Fmod(Along, 11.f) < 7.f;
				if (bOn)
				{
					if (Dash.Num() == 0)
					{
						Dash.Add(A);
					}
					Dash.Add(B);
				}
				else if (Dash.Num() > 0)
				{
					Lines(CurveLayer, Dash, Curve.Color, Thickness);
					Dash.Reset();
				}
				Along += Length;
			}
			Lines(CurveLayer, Dash, Curve.Color, Thickness);
		}
	}

	// Each curve named where it ends, in its own color, while there is more than one to tell apart. Curves ending on one
	// another - R, G and B all at 1 - would write their names over each other into one unreadable glyph, so names that would
	// overlap are written once, together: "R, G, B", each in its own color.
	if (Curves.Num() > 1)
	{
		struct FLabel
		{
			FVector2f At;
			TArray<int32> Curves;
		};
		TArray<FLabel> Labels;
		const float LabelHeight = FVector2f(Measure->Measure(TEXT("Xg"), SmallFont)).Y;
		for (int32 Index = 0; Index < Curves.Num(); ++Index)
		{
			const FCurve& Curve = Curves[Index];
			if (Curve.Label.IsEmpty())
			{
				continue;
			}
			const FVector2f At(Curve.LabelAt.X + 6.f, Curve.LabelAt.Y - LabelHeight - 2.f);
			FLabel* Near = Labels.FindByPredicate([&At, LabelHeight](const FLabel& Existing)
			{
				return FMath::Abs(Existing.At.Y - At.Y) < LabelHeight && FMath::Abs(Existing.At.X - At.X) < 40.f;
			});
			if (Near)
			{
				Near->Curves.Add(Index);
			}
			else
			{
				Labels.Add(FLabel{ At, { Index } });
			}
		}

		const FString Separator(TEXT(", "));
		const float SeparatorWidth = FVector2f(Measure->Measure(Separator, SmallFont)).X;
		for (const FLabel& Label : Labels)
		{
			float Width = 0.f;
			for (int32 Part = 0; Part < Label.Curves.Num(); ++Part)
			{
				Width += FVector2f(Measure->Measure(Curves[Label.Curves[Part]].Label.ToString(), SmallFont)).X + (Part > 0 ? SeparatorWidth : 0.f);
			}
			FVector2f At(FMath::Clamp(Label.At.X, GutterWidth + 2.f, FMath::Max(Size.X - Width - 4.f, GutterWidth + 2.f)), FMath::Clamp(Label.At.Y, RulerHeight + 2.f, Size.Y - LabelHeight - 2.f));
			for (int32 Part = 0; Part < Label.Curves.Num(); ++Part)
			{
				if (Part > 0)
				{
					Text(CurveLayer + 1, Separator, At, SmallFont, Palette::GraphRulerText);
					At.X += SeparatorWidth;
				}
				const FCurve& Curve = Curves[Label.Curves[Part]];
				const FString Name = Curve.Label.ToString();
				Text(CurveLayer + 1, Name, At, SmallFont, Curve.Color.CopyWithNewOpacity(Curve.bActive ? 1.f : 0.85f));
				At.X += FVector2f(Measure->Measure(Name, SmallFont)).X;
			}
		}
	}

	// Handles: a line from its key, and a dot - hollow where the segment has no handles of its own yet.
	for (const FHandleHit& Handle : HandleHits)
	{
		const FLinearColor LineColor = Handle.bEditable ? Palette::GraphHandleLine : Palette::GraphHandleLine.CopyWithNewOpacity(0.2f);
		Lines(HandleLayer, { Handle.Anchor, Handle.Position }, LineColor, 1.f);
		const FVector2f HandleSize(HandleRadius * 2.f, HandleRadius * 2.f);
		const bool bPressed = (Drag == EDrag::Handle || Drag == EDrag::HandlePending) && PressHandle.Model == Handle.Model && PressHandle.Segment == Handle.Segment && PressHandle.bArrive == Handle.bArrive;
		const FLinearColor Color = !Handle.bEditable ? Palette::KeyReadOnly : bPressed ? Palette::KeySelected : Palette::GraphHandle;
		Box(HandleLayer + 1, Handle.Position - HandleSize * 0.5f, HandleSize, Handle.bExact ? CircleBrush() : RingBrush(), Color);
	}

	// Keys: a dot on each curve, white when hovered, orange when selected, gray when the track can't be edited.
	const FKeyHit* Hovered = bHasMouse && Drag == EDrag::None ? FindKeyAt(LastMouse) : nullptr;
	for (const FKeyHit& Hit : KeyHits)
	{
		if (Hit.Position.X < GutterWidth - KeyRadius || Hit.Position.X > Size.X + KeyRadius)
		{
			continue;
		}
		const bool bSelected = IsKeySelected(Hit.Key);
		const FLayerModel& Model = Models[Hit.Model];
		const float Radius = bSelected ? KeyRadius + 1.f : KeyRadius;
		const FVector2f KeySize(Radius * 2.f, Radius * 2.f);
		const FLinearColor Fill = !Model.bEditable ? Palette::KeyReadOnly
			: bSelected ? Palette::KeySelected
			: &Hit == Hovered ? Palette::KeyHovered
			: Model.Color;
		const FVector2f OutlineSize = KeySize + FVector2f(2.f, 2.f);
		Box(KeyLayer, Hit.Position - OutlineSize * 0.5f, OutlineSize, CircleBrush(), Palette::KeyOutline);
		Box(KeyLayer + 1, Hit.Position - KeySize * 0.5f, KeySize, CircleBrush(), Fill);
	}

	// The playhead, through everything, with its tag in the ruler - exactly as the Track Editor draws it.
	if (Panel)
	{
		const float PlayheadSeconds = Panel->GetPlayheadSeconds();
		const float X = TimeToX(PlayheadSeconds);
		if (X >= GutterWidth && X <= Size.X)
		{
			SWidgetAnimTimeline::PaintPlayhead(OutDrawElements, AllottedGeometry, OverlayLayer, X, RulerHeight, Size.Y, GutterWidth, Size.X, Panel->FormatPlayheadTag(PlayheadSeconds));
		}
	}

	// The selection box.
	if (Drag == EDrag::Marquee)
	{
		const FVector2f Min(FMath::Min(PressLocal.X, MarqueeEnd.X), FMath::Min(PressLocal.Y, MarqueeEnd.Y));
		const FVector2f Max(FMath::Max(PressLocal.X, MarqueeEnd.X), FMath::Max(PressLocal.Y, MarqueeEnd.Y));
		Box(OverlayLayer, Min, Max - Min, WhiteBrush(), Palette::MarqueeFill);
		Lines(OverlayLayer + 1, { Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), Min }, Palette::MarqueeOutline, 1.f);
	}

	// What the key under the mouse - or being dragged - is: its curve, value and time.
	const FKeyHit* Readout = Hovered;
	if (!Readout && Drag == EDrag::Keys)
	{
		Readout = KeyHits.FindByPredicate([this](const FKeyHit& Hit) { return Hit.Key == PressKey; });
	}
	if (Readout && Panel)
	{
		const FLayerModel& Model = Models[Readout->Model];
		const FWidgetAnimLayer* Layer = GetLayer(Model.Layer);
		if (Layer && Readout->Key.Channel == EChannel::FadeEnvelope)
		{
			// A Sound's fade key: how long its fade is.
			float FadeIn = 0.f;
			float FadeOut = 0.f;
			GetFades(*Layer, Model, FadeIn, FadeOut);
			const bool bIn = Readout->Key.Point == FadeInPoint;
			const FString Line = FString::Printf(TEXT("%s: %s"), bIn ? TEXT("Fade In") : TEXT("Fade Out"), *Panel->FormatTime(bIn ? FadeIn : FadeOut));
			const FVector2f LineSize = FVector2f(Measure->Measure(Line, SmallFont));
			const FVector2f BoxSize = LineSize + FVector2f(12.f, 6.f);
			const FVector2f At(FMath::Clamp(Readout->Position.X + 10.f, 0.f, FMath::Max(Size.X - BoxSize.X, 0.f)), FMath::Clamp(Readout->Position.Y - BoxSize.Y - 8.f, 0.f, FMath::Max(Size.Y - BoxSize.Y, 0.f)));
			Box(OverlayLayer + 4, At, BoxSize, ReadoutBrush(), FLinearColor::White);
			Text(OverlayLayer + 5, Line, At + FVector2f(6.f, 3.f), SmallFont, Palette::TooltipValue);
		}
		else if (Layer && Model.PointTimes.IsValidIndex(Readout->Key.Point))
		{
			const float Seconds = Model.SpanStart + Model.PointTimes[Readout->Key.Point];
			const FString Line = Readout->Key.Channel == EChannel::Progress
				? FString::Printf(TEXT("%s  @ %s"), *Model.Name.ToString(), *Panel->FormatTime(Seconds))
				: FString::Printf(TEXT("%s: %s  @ %s"), *GetChannelName(*Layer, Model, Readout->Key.Channel).ToString(),
					*FormatValue(GetPointValue(*Layer, Model, Readout->Key.Point, Readout->Key.Channel)), *Panel->FormatTime(Seconds));
			const FVector2f LineSize = FVector2f(Measure->Measure(Line, SmallFont));
			const FVector2f BoxSize = LineSize + FVector2f(12.f, 6.f);
			const FVector2f At(FMath::Clamp(Readout->Position.X + 10.f, 0.f, FMath::Max(Size.X - BoxSize.X, 0.f)), FMath::Clamp(Readout->Position.Y - BoxSize.Y - 8.f, 0.f, FMath::Max(Size.Y - BoxSize.Y, 0.f)));
			Box(OverlayLayer + 4, At, BoxSize, ReadoutBrush(), FLinearColor::White);
			Text(OverlayLayer + 5, Line, At + FVector2f(6.f, 3.f), SmallFont, Palette::TooltipValue);
		}
	}

	return OverlayLayer + 5;
}

// -----------------------------------------------------------------------------------------------------
// Hit testing and selection
// -----------------------------------------------------------------------------------------------------

const SWidgetAnimGraphCanvas::FKeyHit* SWidgetAnimGraphCanvas::FindKeyAt(const FVector2f& Local) const
{
	// The nearest within reach - the last drawn (on top) winning a tie.
	const FKeyHit* Best = nullptr;
	float BestDistance = WidgetAnimGraph::KeyHitRadius;
	for (int32 Index = KeyHits.Num() - 1; Index >= 0; --Index)
	{
		const float Distance = (KeyHits[Index].Position - Local).Size();
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = &KeyHits[Index];
		}
	}
	return Best;
}

const SWidgetAnimGraphCanvas::FHandleHit* SWidgetAnimGraphCanvas::FindHandleAt(const FVector2f& Local) const
{
	const FHandleHit* Best = nullptr;
	float BestDistance = WidgetAnimGraph::KeyHitRadius;
	for (const FHandleHit& Hit : HandleHits)
	{
		const float Distance = (Hit.Position - Local).Size();
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = &Hit;
		}
	}
	return Best;
}

const SWidgetAnimGraphCanvas::FCurve* SWidgetAnimGraphCanvas::FindCurveAt(const FVector2f& Local) const
{
	const FCurve* Best = nullptr;
	float BestDistance = WidgetAnimGraph::CurveHitDistance;
	for (const FCurve& Curve : Curves)
	{
		for (int32 Index = 0; Index + 1 < Curve.Points.Num(); ++Index)
		{
			const FVector2f A = Curve.Points[Index];
			const FVector2f B = Curve.Points[Index + 1];
			if (Local.X < FMath::Min(A.X, B.X) - BestDistance || Local.X > FMath::Max(A.X, B.X) + BestDistance)
			{
				continue;
			}
			const FVector2f AB = B - A;
			const float LengthSquared = AB.SizeSquared();
			const float T = LengthSquared > KINDA_SMALL_NUMBER ? FMath::Clamp(FVector2f::DotProduct(Local - A, AB) / LengthSquared, 0.f, 1.f) : 0.f;
			const float Distance = (A + AB * T - Local).Size();
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Best = &Curve;
			}
		}
	}
	return Best;
}

bool SWidgetAnimGraphCanvas::IsKeySelected(const FGraphKey& Key) const
{
	// In the Ease view a key is the whole key: selected on any of its channels, it is selected there.
	if (Key.Channel == EChannel::Progress)
	{
		return State->SelectedKeys.ContainsByPredicate([&Key](const FGraphKey& Selected) { return Selected.SameKey(Key); });
	}
	return State->SelectedKeys.Contains(Key);
}

void SWidgetAnimGraphCanvas::SelectKey(const FGraphKey& Key, bool bAdd, bool bToggle)
{
	TArray<FGraphKey>& Selected = State->SelectedKeys;
	if (bToggle && IsKeySelected(Key))
	{
		Selected.RemoveAll([&Key](const FGraphKey& Existing) { return Key.Channel == EChannel::Progress ? Existing.SameKey(Key) : Existing == Key; });
	}
	else
	{
		if (!bAdd && !bToggle)
		{
			Selected.Reset();
		}
		Selected.Remove(Key);
		Selected.Add(Key);
	}

	if (Key.Channel != EChannel::Progress && Selected.Contains(Key))
	{
		State->ActiveChannel = WidgetAnimGraph::ChannelId(Key.Layer, Key.Channel);
	}
	MirrorSelectionToPanel();
}

void SWidgetAnimGraphCanvas::MirrorSelectionToPanel()
{
	// The panel's Selection panel shows what is picked here - a key's fields, its curve and handles, to type exactly; or the
	// track, for a start or end value that is the track's own (before it has keys, the Track Editor has none to select).
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel)
	{
		return;
	}

	TArray<SWidgetAnimTimeline::FKeyRef> Refs;
	TArray<int32> LayersOnly;
	for (const FGraphKey& Key : State->SelectedKeys)
	{
		TArray<float> KeyTimes;
		Panel->GetKeyTimes(Key.Layer, KeyTimes);
		if (KeyTimes.IsValidIndex(Key.Point) && Key.Channel != EChannel::FadeEnvelope)
		{
			Refs.AddUnique(SWidgetAnimTimeline::FKeyRef{ Key.Layer, Key.Point });
		}
		else
		{
			LayersOnly.AddUnique(Key.Layer);
		}
	}

	if (Refs.Num() == 1 && LayersOnly.Num() == 0)
	{
		Panel->SelectKey(Refs[0].Layer, Refs[0].Key);
	}
	else if (Refs.Num() > 0)
	{
		Panel->SelectInBox(Refs, {}, /*bAdd=*/false, /*bToggle=*/false);
	}
	else if (LayersOnly.Num() == 1)
	{
		Panel->SelectLayer(LayersOnly[0]);
	}
	else if (LayersOnly.Num() > 1)
	{
		Panel->SelectInBox({}, LayersOnly, /*bAdd=*/false, /*bToggle=*/false);
	}
}

void SWidgetAnimGraphCanvas::SelectAllKeys()
{
	State->SelectedKeys.Reset();
	for (const FKeyHit& Hit : KeyHits)
	{
		State->SelectedKeys.AddUnique(Hit.Key);
	}
	MirrorSelectionToPanel();
}

// -----------------------------------------------------------------------------------------------------
// Editing
// -----------------------------------------------------------------------------------------------------

void SWidgetAnimGraphCanvas::BeginKeyDrag()
{
	using namespace WidgetAnimGraph;

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel || !Panel->CanEdit())
	{
		Drag = EDrag::None;
		return;
	}

	Moving.Reset();
	for (const FGraphKey& Key : State->SelectedKeys)
	{
		const int32 ModelIndex = FindModel(Key.Layer);
		const FWidgetAnimLayer* Layer = GetLayer(Key.Layer);
		if (ModelIndex == INDEX_NONE || !Layer)
		{
			continue;
		}
		const FLayerModel& Model = Models[ModelIndex];
		const bool bFadeKey = Key.Channel == EChannel::FadeEnvelope;
		if (!Model.bEditable || (!bFadeKey && !Model.PointTimes.IsValidIndex(Key.Point)))
		{
			continue;
		}

		FMovingKey& Entry = Moving.AddDefaulted_GetRef();
		Entry.Key = Key;
		Entry.Model = ModelIndex;

		// A Sound's fade key moves its fade's end in time, and only that.
		if (bFadeKey)
		{
			float FadeIn = 0.f;
			float FadeOut = 0.f;
			GetFades(*Layer, Model, FadeIn, FadeOut);
			Entry.OriginalTime = Key.Point == FadeInPoint ? FadeIn : Model.RunSeconds - FadeOut;
			Entry.bTime = true;
			Entry.bValue = false;
			continue;
		}
		Entry.OriginalTime = Model.PointTimes[Key.Point];

		// A key's moment is shared by every channel, so only one entry per key moves it in time.
		const bool bTimeTaken = Moving.ContainsByPredicate([&Key](const FMovingKey& Other) { return Other.bTime && Other.Key.SameKey(Key); });
		Entry.bTime = !bTimeTaken && (Model.bKeyframed || (Key.Point > 0 && Key.Point < Model.NumValuePoints - 1));
		Entry.bValue = State->View == EView::Values && CanSetPointValue(*Layer, Model, Key.Point, Key.Channel);
		Entry.OriginalDisplay = Entry.bValue ? ToDisplay(Key.Layer, Key.Channel, GetPointValue(*Layer, Model, Key.Point, Key.Channel)) : 0.f;
	}

	if (!Moving.ContainsByPredicate([](const FMovingKey& Entry) { return Entry.bTime || Entry.bValue; }))
	{
		Drag = EDrag::None;
		Moving.Reset();
		return;
	}

	DragTransaction = MakeUnique<FScopedTransaction>(Moving.Num() > 1 ? LOCTEXT("MoveGraphKeys", "Move Keys") : LOCTEXT("MoveGraphKey", "Move Key"));
	if (!Panel->BeginEdit())
	{
		DragTransaction->Cancel();
		DragTransaction.Reset();
		Drag = EDrag::None;
		Moving.Reset();
		return;
	}

	bNormsFrozen = true;
	PressTime = XToTime(PressLocal.X);
	PressDisplay = YToDisplay(PressLocal.Y);
	Drag = EDrag::Keys;
}

void SWidgetAnimGraphCanvas::UpdateKeyDrag(const FVector2f& Local, bool bShift)
{
	using namespace WidgetAnimGraph;

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel || Moving.Num() == 0)
	{
		return;
	}

	// Shift keeps the drag to one direction - whichever it has gone further in.
	float DeltaTime = XToTime(Local.X) - PressTime;
	float DeltaDisplay = YToDisplay(Local.Y) - PressDisplay;
	if (bShift)
	{
		const FVector2f Moved = Local - PressLocal;
		(FMath::Abs(Moved.X) >= FMath::Abs(Moved.Y) ? DeltaDisplay : DeltaTime) = 0.f;
	}

	// In time: every moving key by one amount, none past a neighbor that isn't moving with it - so keys keep their order,
	// which a Transform curve depends on (its poses sit alongside its keys by position).
	constexpr float MinSpacing = 0.001f;
	float Lowest = -600.f;
	float Highest = 600.f;
	for (const FMovingKey& Entry : Moving)
	{
		if (!Entry.bTime || Entry.Key.Channel == EChannel::FadeEnvelope)
		{
			continue;
		}
		const FLayerModel& Model = Models[Entry.Model];
		const int32 Point = Entry.Key.Point;
		auto IsMoving = [this, &Entry](int32 Other)
		{
			return Moving.ContainsByPredicate([&Entry, Other](const FMovingKey& Candidate) { return Candidate.bTime && Candidate.Key.Layer == Entry.Key.Layer && Candidate.Key.Point == Other; });
		};
		if (Point == 0)
		{
			Lowest = FMath::Max(Lowest, -Entry.OriginalTime);
		}
		else if (!IsMoving(Point - 1))
		{
			Lowest = FMath::Max(Lowest, Model.PointTimes[Point - 1] + MinSpacing - Entry.OriginalTime);
		}
		if (Point + 1 < Model.PointTimes.Num() && !IsMoving(Point + 1))
		{
			Highest = FMath::Min(Highest, Model.PointTimes[Point + 1] - MinSpacing - Entry.OriginalTime);
		}
	}
	const float TimeDelta = Lowest <= Highest ? FMath::Clamp(FMath::RoundToFloat(DeltaTime * 1000.f) / 1000.f, Lowest, Highest) : 0.f;

	// Written from the far end of the move first: a Transform curve finds a key by its time, so a key must never be set onto
	// the time of one that has yet to move.
	TArray<const FMovingKey*> TimeOrder;
	for (const FMovingKey& Entry : Moving)
	{
		if (Entry.bTime && Entry.Key.Channel != EChannel::FadeEnvelope)
		{
			TimeOrder.Add(&Entry);
		}
	}
	TimeOrder.Sort([TimeDelta](const FMovingKey& A, const FMovingKey& B)
	{
		return TimeDelta > 0.f ? A.Key.Point > B.Key.Point : A.Key.Point < B.Key.Point;
	});
	for (const FMovingKey* Entry : TimeOrder)
	{
		Panel->SetKeyTime(Entry->Key.Layer, Entry->Key.Point, Entry->OriginalTime + TimeDelta);
	}

	// A Sound's fades: where the fade in ends, or the fade out begins, moved - inside its run.
	for (const FMovingKey& Entry : Moving)
	{
		if (Entry.Key.Channel != EChannel::FadeEnvelope)
		{
			continue;
		}
		FWidgetAnimLayer* Layer = GetMutableLayer(Entry.Key.Layer);
		FWidgetAnimSoundCueParams* Sound = Layer ? Layer->TypeParams.GetMutablePtr<FWidgetAnimSoundCueParams>() : nullptr;
		if (!Sound)
		{
			continue;
		}
		const float RunSeconds = Models[Entry.Model].RunSeconds;
		const float FadeTime = FMath::Clamp(Entry.OriginalTime + FMath::RoundToFloat(DeltaTime * 1000.f) / 1000.f, 0.f, RunSeconds);
		if (Entry.Key.Point == FadeInPoint)
		{
			Sound->FadeIn = FMath::RoundToFloat(FadeTime * 1000.f) / 1000.f;
		}
		else
		{
			Sound->FadeOut = FMath::RoundToFloat((RunSeconds - FadeTime) * 1000.f) / 1000.f;
		}
	}

	// In value: each key's channel by the same amount on screen - normalized, by its own channel's range.
	for (const FMovingKey& Entry : Moving)
	{
		if (!Entry.bValue)
		{
			continue;
		}
		FWidgetAnimLayer* Layer = GetMutableLayer(Entry.Key.Layer);
		if (Layer)
		{
			const float Value = RoundValue(FromDisplay(Entry.Key.Layer, Entry.Key.Channel, Entry.OriginalDisplay + DeltaDisplay));
			SetPointValue(*Layer, Models[Entry.Model], Entry.Key.Point, Entry.Key.Channel, Value);
		}
	}

	// Interactive, as a Details panel drag is: the preview follows live, on the key pressed - what it looks like there now.
	Panel->NotifyGroupsChanged(EPropertyChangeType::Interactive);
	const int32 PressedModel = FindModel(PressKey.Layer);
	if (PressedModel != INDEX_NONE)
	{
		const FMovingKey* Pressed = Moving.FindByPredicate([this](const FMovingKey& Entry) { return Entry.Key.SameKey(PressKey); });
		const float KeyTime = Pressed ? Pressed->OriginalTime + (Pressed->bTime ? TimeDelta : 0.f) : 0.f;
		Panel->ScrubToSeconds(Models[PressedModel].SpanStart + KeyTime);
	}
}

void SWidgetAnimGraphCanvas::BeginHandleDrag()
{
	using namespace WidgetAnimGraph;

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const FLayerModel* Model = Models.IsValidIndex(PressHandle.Model) ? &Models[PressHandle.Model] : nullptr;
	const FWidgetAnimLayer* Layer = Model ? GetLayer(Model->Layer) : nullptr;
	if (!Panel || !Model || !Layer || !PressHandle.bEditable || !Panel->CanEdit())
	{
		Drag = EDrag::None;
		return;
	}

	bool bExact = false;
	if (!GetSegmentHandles(*Layer, *Model, PressHandle.Segment, PressLeave, PressArrive, bExact))
	{
		Drag = EDrag::None;
		return;
	}

	PressSegmentStart = Model->SpanStart + Model->PointTimes[PressHandle.Segment];
	PressSegmentEnd = Model->SpanStart + Model->PointTimes[PressHandle.Segment + 1];
	if (State->View == EView::Ease || PressHandle.Channel == EChannel::Progress)
	{
		PressD0 = 0.f;
		PressD1 = 1.f;
	}
	else
	{
		PressD0 = ToDisplay(Model->Layer, PressHandle.Channel, GetPointValue(*Layer, *Model, PressHandle.Segment, PressHandle.Channel));
		PressD1 = ToDisplay(Model->Layer, PressHandle.Channel, GetPointValue(*Layer, *Model, PressHandle.Segment + 1, PressHandle.Channel));
	}

	DragTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("ShapeGraphCurve", "Shape Curve"));
	if (!Panel->BeginEdit())
	{
		DragTransaction->Cancel();
		DragTransaction.Reset();
		Drag = EDrag::None;
		return;
	}
	bNormsFrozen = true;
	Drag = EDrag::Handle;
}

void SWidgetAnimGraphCanvas::UpdateHandleDrag(const FVector2f& Local)
{
	using namespace WidgetAnimGraph;

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const FLayerModel* Model = Models.IsValidIndex(PressHandle.Model) ? &Models[PressHandle.Model] : nullptr;
	FWidgetAnimLayer* Layer = Model ? GetMutableLayer(Model->Layer) : nullptr;
	FWidgetAnimSegmentCurve* Curve = Layer ? GetMutableSegmentCurve(*Layer, PressHandle.Segment) : nullptr;
	if (!Panel || !Curve)
	{
		return;
	}

	// Where the mouse is, in the segment's own terms: how far along it in time (never outside it, so the curve can't turn
	// back in time) and how far along its rise - past either end for an overshoot or a wind-up.
	const float SegmentLength = FMath::Max(PressSegmentEnd - PressSegmentStart, KINDA_SMALL_NUMBER);
	const double HandleX = FMath::Clamp((XToTime(Local.X) - PressSegmentStart) / SegmentLength, 0.f, 1.f);
	const float Rise = PressD1 - PressD0;
	const double HandleY = FMath::Clamp(FMath::Abs(Rise) > KINDA_SMALL_NUMBER ? (YToDisplay(Local.Y) - PressD0) / Rise : 0.f, HandleMinY, HandleMaxY);

	// The segment takes the handles over: the one dragged where the mouse is, the other where it was drawn.
	Curve->Easing = EWidgetAnimSegmentEasing::Bezier;
	Curve->CustomCurve = nullptr;
	Curve->LeaveHandle = PressHandle.bArrive ? PressLeave : FVector2D(FMath::RoundToDouble(HandleX * 1000.0) / 1000.0, FMath::RoundToDouble(HandleY * 1000.0) / 1000.0);
	Curve->ArriveHandle = PressHandle.bArrive ? FVector2D(FMath::RoundToDouble(HandleX * 1000.0) / 1000.0, FMath::RoundToDouble(HandleY * 1000.0) / 1000.0) : PressArrive;

	Panel->NotifyGroupsChanged(EPropertyChangeType::Interactive);
}

void SWidgetAnimGraphCanvas::EndDrag()
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const bool bEdited = DragTransaction.IsValid();
	if (bEdited && Panel)
	{
		Panel->NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	}
	DragTransaction.Reset();
	Moving.Reset();
	bNormsFrozen = false;
	Drag = EDrag::None;

	// What the Selection panel shows was just changed under it.
	if (bEdited && Panel)
	{
		Panel->RefreshDetails();
	}
}

void SWidgetAnimGraphCanvas::DeleteSelectedKeys()
{
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel || !Panel->CanEdit())
	{
		return;
	}

	// Through the panel's own delete, so a track keeps what it must - a Transform its two keys, any other its start and end.
	TArray<SWidgetAnimTimeline::FKeyRef> Refs;
	for (const FGraphKey& Key : State->SelectedKeys)
	{
		if (Panel->CanDeleteKey(Key.Layer, Key.Point))
		{
			Refs.AddUnique(SWidgetAnimTimeline::FKeyRef{ Key.Layer, Key.Point });
		}
	}
	if (Refs.Num() == 0)
	{
		return;
	}
	Panel->SelectInBox(Refs, {}, /*bAdd=*/false, /*bToggle=*/false);
	Panel->DeleteSelectedKeys();
	State->SelectedKeys.Reset();
}

TArray<SWidgetAnimTimeline::FKeyRef> SWidgetAnimGraphCanvas::GetSelectedSegments() const
{
	TArray<SWidgetAnimTimeline::FKeyRef> Segments;
	for (const FGraphKey& Key : State->SelectedKeys)
	{
		const int32 ModelIndex = FindModel(Key.Layer);
		if (ModelIndex != INDEX_NONE && Models[ModelIndex].bCurves && Key.Point + 1 < Models[ModelIndex].NumValuePoints)
		{
			Segments.AddUnique(SWidgetAnimTimeline::FKeyRef{ Key.Layer, Key.Point });
		}
	}
	return Segments;
}

void SWidgetAnimGraphCanvas::SetCurve(const TArray<SWidgetAnimTimeline::FKeyRef>& Segments, WidgetAnimGraph::ECurvePreset Preset)
{
	using namespace WidgetAnimGraph;

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel || Segments.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetGraphCurve", "Set Curve To Next Key"));
	if (!Panel->BeginEdit())
	{
		return;
	}

	for (const SWidgetAnimTimeline::FKeyRef& Segment : Segments)
	{
		const int32 ModelIndex = FindModel(Segment.Layer);
		FWidgetAnimLayer* Layer = GetMutableLayer(Segment.Layer);
		FWidgetAnimSegmentCurve* Curve = Layer ? GetMutableSegmentCurve(*Layer, Segment.Key) : nullptr;
		if (ModelIndex == INDEX_NONE || !Curve || !Panel->CanEditKeys(Segment.Layer))
		{
			continue;
		}
		const FLayerModel& Model = Models[ModelIndex];

		// Handles to start a Bezier from: the curve as it is drawn now, so choosing Bezier changes nothing until dragged.
		FVector2D Leave;
		FVector2D Arrive;
		bool bExact = false;
		GetSegmentHandles(*Layer, Model, Segment.Key, Leave, Arrive, bExact);

		// A track with no keys moves by its Easing, or by its one segment's handles - nothing else on that segment is used - so
		// a preset there is its Easing.
		if (Model.bKeyless && Preset != ECurvePreset::Bezier)
		{
			*Curve = FWidgetAnimSegmentCurve();
			switch (Preset)
			{
			case ECurvePreset::Linear:    Layer->Easing = EWidgetAnimEasing::Linear; break;
			case ECurvePreset::EaseIn:    Layer->Easing = EWidgetAnimEasing::EaseIn; break;
			case ECurvePreset::EaseOut:   Layer->Easing = EWidgetAnimEasing::EaseOut; break;
			case ECurvePreset::EaseInOut: Layer->Easing = EWidgetAnimEasing::EaseInOut; break;
			default: break;
			}
			continue;
		}

		const bool bWasBezier = Curve->IsBezier();
		Curve->CustomCurve = nullptr;
		switch (Preset)
		{
		case ECurvePreset::TrackEasing: Curve->Easing = EWidgetAnimSegmentEasing::Track; break;
		case ECurvePreset::Linear:      Curve->Easing = EWidgetAnimSegmentEasing::Linear; break;
		case ECurvePreset::EaseIn:      Curve->Easing = EWidgetAnimSegmentEasing::EaseIn; break;
		case ECurvePreset::EaseOut:     Curve->Easing = EWidgetAnimSegmentEasing::EaseOut; break;
		case ECurvePreset::EaseInOut:   Curve->Easing = EWidgetAnimSegmentEasing::EaseInOut; break;
		case ECurvePreset::Hold:        Curve->Easing = EWidgetAnimSegmentEasing::Hold; break;
		case ECurvePreset::Bezier:
			Curve->Easing = EWidgetAnimSegmentEasing::Bezier;
			if (!bWasBezier)
			{
				Curve->LeaveHandle = Leave;
				Curve->ArriveHandle = Arrive;
			}
			break;
		}
	}

	Panel->NotifyGroupsChanged(EPropertyChangeType::ValueSet);
	Panel->RefreshDetails();
}

FText SWidgetAnimGraphCanvas::GetSelectedCurveName() const
{
	const TArray<SWidgetAnimTimeline::FKeyRef> Segments = GetSelectedSegments();
	if (Segments.Num() == 0)
	{
		return WidgetAnimGraph::Tips::GraphCurveNone();
	}

	TOptional<FString> Shared;
	for (const SWidgetAnimTimeline::FKeyRef& Segment : Segments)
	{
		const FWidgetAnimLayer* Layer = GetLayer(Segment.Layer);
		const FWidgetAnimSegmentCurve* Curve = Layer ? WidgetAnimGraph::GetSegmentCurve(*Layer, Segment.Key) : nullptr;
		if (!Layer || !Curve)
		{
			continue;
		}
		const bool bKeyless = !WidgetAnimTypeIsKeyframed(Layer->AnimType) && Layer->ValueKeys.Keys.Num() == 0;
		const FString Name = (Curve->IsTrackEasing() || (bKeyless && !Curve->IsBezier()))
			? SWidgetAnimTimeline::GetEasingName(*Layer).ToString()
			: SWidgetAnimTimeline::GetSegmentCurveName(*Curve).ToString();
		if (Shared.IsSet() && Shared.GetValue() != Name)
		{
			return LOCTEXT("GraphCurveMultiple", "Multiple");
		}
		Shared = Name;
	}
	return Shared.IsSet() ? FText::FromString(Shared.GetValue()) : WidgetAnimGraph::Tips::GraphCurveNone();
}

TSharedRef<SWidget> SWidgetAnimGraphCanvas::MakeCurveMenu()
{
	using namespace WidgetAnimGraph;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const TArray<SWidgetAnimTimeline::FKeyRef> Segments = GetSelectedSegments();

	// Track Easing and Hold mean nothing on a track with no keys, which moves by its Easing or its handles alone.
	bool bAnyKeyless = false;
	for (const SWidgetAnimTimeline::FKeyRef& Segment : Segments)
	{
		const int32 ModelIndex = FindModel(Segment.Layer);
		bAnyKeyless |= ModelIndex != INDEX_NONE && Models[ModelIndex].bKeyless;
	}

	struct FEntry
	{
		ECurvePreset Preset;
		EWidgetAnimSegmentEasing Easing;
	};
	const FEntry Entries[] =
	{
		{ ECurvePreset::TrackEasing, EWidgetAnimSegmentEasing::Track },
		{ ECurvePreset::Linear, EWidgetAnimSegmentEasing::Linear },
		{ ECurvePreset::EaseIn, EWidgetAnimSegmentEasing::EaseIn },
		{ ECurvePreset::EaseOut, EWidgetAnimSegmentEasing::EaseOut },
		{ ECurvePreset::EaseInOut, EWidgetAnimSegmentEasing::EaseInOut },
		{ ECurvePreset::Hold, EWidgetAnimSegmentEasing::Hold },
		{ ECurvePreset::Bezier, EWidgetAnimSegmentEasing::Bezier },
	};

	const UEnum* EasingEnum = StaticEnum<EWidgetAnimSegmentEasing>();
	Menu.BeginSection("Curve", LOCTEXT("CurveToNextKeyHeading", "Curve To Next Key"));
	for (const FEntry& Entry : Entries)
	{
		const bool bAllowed = Segments.Num() > 0 && !(bAnyKeyless && (Entry.Preset == ECurvePreset::TrackEasing || Entry.Preset == ECurvePreset::Hold));
		const ECurvePreset Preset = Entry.Preset;
		Menu.AddMenuEntry(
			EasingEnum ? EasingEnum->GetDisplayNameTextByValue(static_cast<int64>(Entry.Easing)) : FText::GetEmpty(),
			EasingEnum ? EasingEnum->GetToolTipTextByIndex(EasingEnum->GetIndexByValue(static_cast<int64>(Entry.Easing))) : FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Segments, Preset]() { SetCurve(Segments, Preset); }),
				FCanExecuteAction::CreateLambda([bAllowed]() { return bAllowed; })));
	}
	Menu.EndSection();
	return Menu.MakeWidget();
}

TSharedRef<SWidget> SWidgetAnimGraphCanvas::MakeContextMenu(const FVector2f& Local)
{
	using namespace WidgetAnimGraph;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	if (!Panel)
	{
		return Menu.MakeWidget();
	}

	if (State->SelectedKeys.Num() > 0)
	{
		Menu.BeginSection("Keys", LOCTEXT("GraphKeysSection", "Keys"));
		int32 NumDeletable = 0;
		TArray<SWidgetAnimTimeline::FKeyRef> Seen;
		for (const FGraphKey& Key : State->SelectedKeys)
		{
			const SWidgetAnimTimeline::FKeyRef Ref{ Key.Layer, Key.Point };
			if (!Seen.Contains(Ref))
			{
				Seen.Add(Ref);
				NumDeletable += Panel->CanDeleteKey(Key.Layer, Key.Point) ? 1 : 0;
			}
		}
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("GraphDeleteKeys", "Delete {0} {0}|plural(one=Key,other=Keys)"), FText::AsNumber(NumDeletable)),
			Tips::GraphDeleteKeysTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
			FUIAction(
				FExecuteAction::CreateLambda([this]() { DeleteSelectedKeys(); }),
				FCanExecuteAction::CreateLambda([NumDeletable]() { return NumDeletable > 0; })));
		if (GetSelectedSegments().Num() > 0)
		{
			Menu.AddSubMenu(
				LOCTEXT("GraphCurveSubMenu", "Curve To Next Key"),
				Tips::GraphCurveTooltip(),
				FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenu)
				{
					SubMenu.AddWidget(MakeCurveMenu(), FText::GetEmpty(), /*bNoIndent=*/true);
				}));
		}
		Menu.EndSection();
	}

	// Adding a key to any shown track the mouse is within, at the time it is at.
	const float Seconds = XToTime(Local.X);
	Menu.BeginSection("Add", LOCTEXT("GraphAddSection", "Add"));
	for (const FLayerModel& Model : Models)
	{
		const float LocalSeconds = Seconds - Model.SpanStart;
		if (!Model.bEditable || LocalSeconds < 0.f || (!Model.bKeyframed && LocalSeconds > Model.RunSeconds))
		{
			continue;
		}
		const int32 LayerIndex = Model.Layer;
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("GraphAddKeyTo", "Add Key to {0} ({1})"), Model.Name, FText::FromString(Panel->FormatTime(Seconds))),
			Tips::GraphAddKeyTooltip(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.PlusCircle"),
			FUIAction(FExecuteAction::CreateLambda([this, LayerIndex, LocalSeconds]()
			{
				const TSharedPtr<SWidgetAnimTimeline> Pinned = Owner.Pin();
				if (Pinned)
				{
					Pinned->AddKey(LayerIndex, LocalSeconds);
					const SWidgetAnimTimeline::FSelection& Added = Pinned->GetSelection();
					if (Added.Layer == LayerIndex && Added.Key != INDEX_NONE)
					{
						State->SelectedKeys = { FGraphKey{ LayerIndex, Added.Key, EChannel::Progress } };
					}
				}
			})));
	}
	Menu.EndSection();

	Menu.BeginSection("View", LOCTEXT("GraphViewSection", "View"));
	Menu.AddMenuEntry(
		LOCTEXT("GraphFit", "Fit (F)"),
		Tips::GraphFitTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]() { RequestFit(); })));
	Menu.AddMenuEntry(
		LOCTEXT("GraphShowAll", "Show Every Channel"),
		Tips::GraphShowAllTooltip(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]() { State->HiddenChannels.Reset(); RequestFit(); }),
			FCanExecuteAction::CreateLambda([this]() { return State->HiddenChannels.Num() > 0; })));
	Menu.EndSection();

	return Menu.MakeWidget();
}

void SWidgetAnimGraphCanvas::PushMenu(const FPointerEvent& MouseEvent, const TSharedRef<SWidget>& Menu)
{
	const FWidgetPath WidgetPath = MouseEvent.GetEventPath() ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, Menu, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

// -----------------------------------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------------------------------

FReply SWidgetAnimGraphCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	using namespace WidgetAnimGraph;

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	PressLocal = Local;
	LastMouse = Local;
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();

	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton || MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		Drag = MouseEvent.GetEffectingButton() == EKeys::RightMouseButton ? EDrag::RightPending : EDrag::Pan;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// The ruler scrubs.
	if (Local.Y < RulerHeight)
	{
		Drag = EDrag::Scrub;
		if (Panel)
		{
			Panel->ScrubToSeconds(XToTime(Local.X));
		}
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	const bool bShift = MouseEvent.IsShiftDown();
	const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();

	if (const FHandleHit* Handle = FindHandleAt(Local))
	{
		PressHandle = *Handle;
		Drag = EDrag::HandlePending;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	if (const FKeyHit* Hit = FindKeyAt(Local))
	{
		PressKey = Hit->Key;
		// Pressed on one of several selected keys, they all go with it; otherwise it is picked - alone, or with Shift / Ctrl.
		if (!IsKeySelected(Hit->Key) || bShift || bToggle)
		{
			SelectKey(Hit->Key, bShift, bToggle);
		}
		Drag = IsKeySelected(Hit->Key) ? EDrag::KeyPending : EDrag::None;
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	// Empty space: a selection box, once it moves; a click alone clears the selection.
	MarqueeBase = (bShift || bToggle) ? State->SelectedKeys : TArray<FGraphKey>();
	MarqueeEnd = Local;
	Drag = EDrag::MarqueePending;
	return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
}

FReply SWidgetAnimGraphCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	using namespace WidgetAnimGraph;

	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2f Previous = LastMouse;
	LastMouse = Local;
	bHasMouse = true;
	const bool bPastThreshold = (Local - PressLocal).Size() > DragThreshold;
	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();

	switch (Drag)
	{
	case EDrag::Scrub:
		if (Panel)
		{
			Panel->ScrubToSeconds(XToTime(Local.X));
		}
		return FReply::Handled();

	case EDrag::KeyPending:
		if (bPastThreshold)
		{
			BeginKeyDrag();
		}
		if (Drag == EDrag::Keys)
		{
			UpdateKeyDrag(Local, MouseEvent.IsShiftDown());
		}
		return FReply::Handled();

	case EDrag::Keys:
		UpdateKeyDrag(Local, MouseEvent.IsShiftDown());
		return FReply::Handled();

	case EDrag::HandlePending:
		if (bPastThreshold)
		{
			BeginHandleDrag();
		}
		if (Drag == EDrag::Handle)
		{
			UpdateHandleDrag(Local);
		}
		return FReply::Handled();

	case EDrag::Handle:
		UpdateHandleDrag(Local);
		return FReply::Handled();

	case EDrag::MarqueePending:
		if (bPastThreshold)
		{
			Drag = EDrag::Marquee;
		}
		[[fallthrough]];
	case EDrag::Marquee:
		if (Drag == EDrag::Marquee)
		{
			MarqueeEnd = Local;
			const FVector2f Min(FMath::Min(PressLocal.X, Local.X), FMath::Min(PressLocal.Y, Local.Y));
			const FVector2f Max(FMath::Max(PressLocal.X, Local.X), FMath::Max(PressLocal.Y, Local.Y));
			const bool bToggle = MouseEvent.IsControlDown() || MouseEvent.IsCommandDown();
			TArray<FGraphKey> Selected = MarqueeBase;
			for (const FKeyHit& Hit : KeyHits)
			{
				if (Hit.Position.X >= Min.X && Hit.Position.X <= Max.X && Hit.Position.Y >= Min.Y && Hit.Position.Y <= Max.Y)
				{
					if (bToggle && Selected.Contains(Hit.Key))
					{
						Selected.Remove(Hit.Key);
					}
					else
					{
						Selected.AddUnique(Hit.Key);
					}
				}
			}
			State->SelectedKeys = Selected;
		}
		return FReply::Handled();

	case EDrag::RightPending:
		if (bPastThreshold)
		{
			Drag = EDrag::Pan;
		}
		[[fallthrough]];
	case EDrag::Pan:
		if (Drag == EDrag::Pan)
		{
			// The view follows the mouse: what was under it stays under it.
			const FVector2f Delta = Local - Previous;
			const float SecondsPerPixel = (ViewTime.Y - ViewTime.X) / PlotWidth();
			const float ValuePerPixel = (ViewValue.Y - ViewValue.X) / PlotHeight();
			ViewTime -= FVector2f(Delta.X * SecondsPerPixel, Delta.X * SecondsPerPixel);
			ViewValue += FVector2f(Delta.Y * ValuePerPixel, Delta.Y * ValuePerPixel);
		}
		return FReply::Handled();

	default:
		break;
	}
	return FReply::Unhandled();
}

FReply SWidgetAnimGraphCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());

	switch (Drag)
	{
	case EDrag::Keys:
	case EDrag::Handle:
		EndDrag();
		break;

	case EDrag::MarqueePending:
		// A click on empty space: nothing selected.
		State->SelectedKeys.Reset();
		Drag = EDrag::None;
		break;

	case EDrag::Marquee:
		Drag = EDrag::None;
		MirrorSelectionToPanel();
		break;

	case EDrag::RightPending:
	{
		Drag = EDrag::None;
		// Right-clicked on a key not yet selected: it is, for the menu to be about.
		if (const FKeyHit* Hit = FindKeyAt(Local))
		{
			if (!IsKeySelected(Hit->Key))
			{
				SelectKey(Hit->Key, /*bAdd=*/false, /*bToggle=*/false);
			}
		}
		PushMenu(MouseEvent, MakeContextMenu(Local));
		break;
	}

	default:
		Drag = EDrag::None;
		break;
	}

	return FReply::Handled().ReleaseMouseCapture();
}

FReply SWidgetAnimGraphCanvas::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	// The playhead to that moment, as a double-click on the Track Editor does - never a new key, which the menu adds.
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (FindKeyAt(Local) || FindHandleAt(Local))
	{
		return FReply::Handled();
	}
	if (const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin())
	{
		Panel->ScrubToSeconds(XToTime(Local.X));
	}
	return FReply::Handled();
}

FReply SWidgetAnimGraphCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Drag == EDrag::Keys || Drag == EDrag::Handle)
	{
		return FReply::Handled();
	}

	// About the mouse: both ways - Ctrl only in time, Shift only in value.
	const FVector2f Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const float Factor = MouseEvent.GetWheelDelta() > 0.f ? 0.85f : 1.f / 0.85f;
	const bool bTime = !MouseEvent.IsShiftDown();
	const bool bValue = !(MouseEvent.IsControlDown() || MouseEvent.IsCommandDown());

	if (bTime)
	{
		const float Pivot = XToTime(Local.X);
		ViewTime = FVector2f(Pivot + (ViewTime.X - Pivot) * Factor, Pivot + (ViewTime.Y - Pivot) * Factor);
		if (ViewTime.Y - ViewTime.X < 0.01f)
		{
			ViewTime.Y = ViewTime.X + 0.01f;
		}
	}
	if (bValue)
	{
		const float Pivot = YToDisplay(Local.Y);
		ViewValue = FVector2f(Pivot + (ViewValue.X - Pivot) * Factor, Pivot + (ViewValue.Y - Pivot) * Factor);
		if (ViewValue.Y - ViewValue.X < 1.e-4f)
		{
			ViewValue.Y = ViewValue.X + 1.e-4f;
		}
	}
	return FReply::Handled();
}

FReply SWidgetAnimGraphCanvas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bCtrl = InKeyEvent.IsControlDown() || InKeyEvent.IsCommandDown();

	if (Key == EKeys::Delete || Key == EKeys::BackSpace)
	{
		DeleteSelectedKeys();
		return FReply::Handled();
	}
	if (Key == EKeys::F && !bCtrl)
	{
		RequestFit();
		return FReply::Handled();
	}
	if (Key == EKeys::A && bCtrl)
	{
		SelectAllKeys();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape)
	{
		// Out one step at a time: the keys picked here, then the graph itself.
		if (State->SelectedKeys.Num() > 0)
		{
			State->SelectedKeys.Reset();
		}
		else if (const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin())
		{
			Panel->ExitGraph();
		}
		return FReply::Handled();
	}

	// Everything else - Space to play, undo - is the panel's, which this sits inside.
	return FReply::Unhandled();
}

void SWidgetAnimGraphCanvas::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	bHasMouse = false;
}

void SWidgetAnimGraphCanvas::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	if (Drag == EDrag::Keys || Drag == EDrag::Handle)
	{
		EndDrag();
	}
	Drag = EDrag::None;
}

FCursorReply SWidgetAnimGraphCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (Drag == EDrag::Pan)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}
	if (Drag == EDrag::Keys || Drag == EDrag::Handle)
	{
		return FCursorReply::Cursor(EMouseCursor::CardinalCross);
	}
	const FVector2f Local = MyGeometry.AbsoluteToLocal(CursorEvent.GetScreenSpacePosition());
	if (Local.Y < WidgetAnimGraph::RulerHeight)
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}
	const FHandleHit* Handle = FindHandleAt(Local);
	const FKeyHit* Key = Handle ? nullptr : FindKeyAt(Local);
	if ((Handle && Handle->bEditable) || (Key && Models.IsValidIndex(Key->Model) && Models[Key->Model].bEditable))
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}
	return FCursorReply::Unhandled();
}

// =====================================================================================================
// The editor: toolbar, channel list and canvas
// =====================================================================================================

void SWidgetAnimGraphEditor::Construct(const FArguments& InArgs, TWeakPtr<SWidgetAnimTimeline> InOwner)
{
	using namespace WidgetAnimGraph;

	Owner = InOwner;
	State = MakeShared<FWidgetAnimGraphState>();
	const TSharedRef<FWidgetAnimGraphState> StateRef = State.ToSharedRef();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			MakeToolbar()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			.PhysicalSplitterHandleSize(2.f)

			+ SSplitter::Slot()
			.Value(0.2f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
				.Padding(FMargin(0.f))
				[
					SNew(SScrollBox)
					.Orientation(Orient_Vertical)

					+ SScrollBox::Slot()
					[
						SAssignNew(ChannelList, SVerticalBox)
					]
				]
			]

			+ SSplitter::Slot()
			.Value(0.8f)
			[
				SAssignNew(Canvas, SWidgetAnimGraphCanvas, InOwner, StateRef)
			]
		]
	];
}

TSharedRef<SWidget> SWidgetAnimGraphEditor::MakeToolbar()
{
	using namespace WidgetAnimGraph;

	// A segment of the view switch: lit while its view is the one shown.
	auto MakeViewToggle = [this](EView View, const FText& Label, const FText& ToolTip) -> TSharedRef<SWidget>
	{
		return SNew(SWidgetAnimAnchoredTip)
			.Text(ToolTip)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.Padding(FMargin(8.f, 1.f))
				.IsChecked_Lambda([this, View]() { return State->View == View ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, View](ECheckBoxState) { State->View = View; })
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.Text(Label)
				]
			];
	};

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(FMargin(6.f, 3.f))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				MakeViewToggle(EView::Values, LOCTEXT("ValuesView", "Values"), Tips::GraphValuesViewTooltip())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
			[
				MakeViewToggle(EView::Ease, LOCTEXT("EaseView", "Ease"), Tips::GraphEaseViewTooltip())
			]

			// Normalize: several channels each fitted to the same height, in the Values view.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
			[
				SNew(SWidgetAnimAnchoredTip)
				.Text(Tips::GraphNormalizeTooltip())
				.Visibility_Lambda([this]() { return State->View == EView::Values ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return State->bNormalize ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) { State->bNormalize = NewState == ECheckBoxState::Checked; })
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.Text(LOCTEXT("Normalize", "Normalize"))
					]
				]
			]

			// How the selected keys move on to the next one.
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 0.f, 0.f, 0.f))
			[
				SNew(SWidgetAnimAnchoredTip)
				.Text(Tips::GraphCurveTooltip())
				[
					SNew(SComboButton)
					.ContentPadding(FMargin(6.f, 1.f))
					.IsEnabled_Lambda([this]()
					{
						const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
						return Canvas.IsValid() && Panel && Panel->CanEdit() && Canvas->GetSelectedSegments().Num() > 0;
					})
					.OnGetMenuContent_Lambda([this]() { return Canvas.IsValid() ? Canvas->MakeCurveMenu() : SNullWidget::NullWidget; })
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.Text_Lambda([this]()
						{
							return FText::Format(LOCTEXT("CurveButton", "Curve: {0}"), Canvas.IsValid() ? Canvas->GetSelectedCurveName() : FText::GetEmpty());
						})
					]
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(FMargin(12.f, 0.f))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.Text_Lambda([this]()
				{
					// A Spring track's segments aren't shaped by handles, and the hint says why there are none.
					if (Canvas.IsValid())
					{
						for (const FLayerModel& Model : Canvas->GetModels())
						{
							if (Model.Settings.IsSpringTimed())
							{
								return Tips::GraphSpringNote();
							}
						}
					}
					return State->View == EView::Ease ? Tips::GraphEaseHint() : Tips::GraphValuesHint();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SWidgetAnimAnchoredTip)
				.Text(Tips::GraphFitTooltip())
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ContentPadding(FMargin(6.f, 1.f))
					.OnClicked_Lambda([this]()
					{
						FitView();
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.Text(LOCTEXT("FitButton", "Fit"))
					]
				]
			]
		];
}

void SWidgetAnimGraphEditor::OnLayersShown()
{
	State->SelectedKeys.Reset();
	State->HiddenChannels.Reset();
	State->FieldDefaultsApplied.Reset();
	State->ActiveChannel = MAX_uint64;
	ChannelSignature = 0;
	if (Canvas.IsValid())
	{
		Canvas->RequestFit();
	}
}

bool SWidgetAnimGraphEditor::HasSelectedKeys() const
{
	return State.IsValid() && State->SelectedKeys.Num() > 0;
}

void SWidgetAnimGraphEditor::ClearSelectedKeys()
{
	if (State.IsValid())
	{
		State->SelectedKeys.Reset();
	}
}

void SWidgetAnimGraphEditor::FitView()
{
	if (Canvas.IsValid())
	{
		Canvas->RequestFit();
	}
}

TSharedPtr<SWidget> SWidgetAnimGraphEditor::GetFocusTarget() const
{
	return Canvas;
}

uint32 SWidgetAnimGraphEditor::ComputeChannelSignature() const
{
	uint32 Hash = 0x9e3779b9u;
	if (!Canvas.IsValid())
	{
		return Hash;
	}
	for (const WidgetAnimGraph::FLayerModel& Model : Canvas->GetModels())
	{
		Hash = HashCombine(Hash, GetTypeHash(Model.Layer));
		Hash = HashCombine(Hash, GetTypeHash(Model.Name.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(Model.Color.ToFColor(true)));
		for (const WidgetAnimGraph::EChannel Channel : Model.Channels)
		{
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Channel)));
		}
	}
	return Hash;
}

void SWidgetAnimGraphEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	const uint32 Signature = ComputeChannelSignature();
	if (Signature != ChannelSignature)
	{
		ChannelSignature = Signature;
		RebuildChannelList();
	}
}

void SWidgetAnimGraphEditor::RebuildChannelList()
{
	using namespace WidgetAnimGraph;

	if (!ChannelList.IsValid() || !Canvas.IsValid())
	{
		return;
	}
	ChannelList->ClearChildren();

	const TSharedPtr<SWidgetAnimTimeline> Panel = Owner.Pin();
	const TArray<FWidgetAnimLayer>* Layers = Panel ? Panel->GetLayers() : nullptr;
	if (!Layers)
	{
		return;
	}

	// An eye that shows or hides what it is beside - quiet while shown, as the Animation Tracks list's eyes are.
	auto MakeEye = [](TFunction<bool()> IsShown, TFunction<void()> Toggle, const FText& ToolTip) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.f, 0.f))
			.ToolTipText(ToolTip)
			.OnClicked_Lambda([Toggle]()
			{
				Toggle();
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(14.0, 14.0))
				.Image_Lambda([IsShown]() { return FAppStyle::GetBrush(IsShown() ? "Icons.Visible" : "Icons.Hidden"); })
				.ColorAndOpacity_Lambda([IsShown]() { return IsShown() ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground(); })
			];
	};

	// Every channel shown again, and the heights fitted to them.
	ChannelList->AddSlot()
	.AutoHeight()
	.Padding(FMargin(6.f, 4.f, 6.f, 2.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("ChannelsHeading", "CHANNELS"))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(4.f, 0.f))
			.ToolTipText(Tips::GraphShowAllTooltip())
			.IsEnabled_Lambda([this]() { return State->HiddenChannels.Num() > 0; })
			.OnClicked_Lambda([this]()
			{
				State->HiddenChannels.Reset();
				FitView();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.Text(LOCTEXT("ShowAllChannels", "Show All"))
			]
		]
	];

	for (const FLayerModel& Model : Canvas->GetModels())
	{
		if (!Layers->IsValidIndex(Model.Layer))
		{
			continue;
		}
		const FWidgetAnimLayer& Layer = (*Layers)[Model.Layer];
		const int32 LayerIndex = Model.Layer;
		const TArray<EChannel> Channels = Model.Channels;

		// The track: its color, its name, and an eye for all of its channels at once.
		ChannelList->AddSlot()
		.AutoHeight()
		.Padding(FMargin(0.f, 4.f, 0.f, 0.f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(0.f, 2.f, 4.f, 2.f))
			.ToolTipText(Tips::GraphTrackRowTooltip())
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(4.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("WhiteBrush"))
						.ColorAndOpacity(Model.Color)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.VAlign(VAlign_Center)
				.Padding(FMargin(6.f, 0.f))
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(Model.Color)
					.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					.Text(Model.Name)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeEye(
						[this, LayerIndex, Channels]() { return Channels.ContainsByPredicate([this, LayerIndex](EChannel Channel) { return State->IsChannelShown(LayerIndex, Channel); }); },
						[this, LayerIndex, Channels]()
						{
							const bool bAnyShown = Channels.ContainsByPredicate([this, LayerIndex](EChannel Channel) { return State->IsChannelShown(LayerIndex, Channel); });
							for (const EChannel Channel : Channels)
							{
								bAnyShown ? (void)State->HiddenChannels.Add(ChannelId(LayerIndex, Channel)) : (void)State->HiddenChannels.Remove(ChannelId(LayerIndex, Channel));
							}
						},
						Tips::GraphTrackEyeTooltip())
				]
			]
		];

		// Each value its keys hold: a swatch of its shade, its name, its value at the playhead, and an eye. Clicked, it is the
		// active channel - on top, brighter; double-clicked, the only one shown.
		for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			const EChannel Channel = Channels[ChannelIndex];
			const FLinearColor Color = GetChannelColor(Model.Color, ChannelIndex);
			const uint64 Id = ChannelId(LayerIndex, Channel);
			const TWeakPtr<SWidgetAnimGraphCanvas> WeakCanvas = Canvas;

			ChannelList->AddSlot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this, Id]()
				{
					return State->ActiveChannel == Id ? FSlateColor(FStyleColors::Select.GetSpecifiedColor().CopyWithNewOpacity(0.18f)) : FSlateColor(FLinearColor::Transparent);
				})
				.Padding(FMargin(14.f, 2.f, 4.f, 2.f))
				.ToolTipText(Tips::GraphChannelTooltip())
				.OnMouseButtonDown_Lambda([this, Id](const FGeometry&, const FPointerEvent& MouseEvent)
				{
					if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
					{
						return FReply::Unhandled();
					}
					State->ActiveChannel = Id;
					State->HiddenChannels.Remove(Id);
					return FReply::Handled();
				})
				.OnMouseDoubleClick_Lambda([this, Id](const FGeometry&, const FPointerEvent&)
				{
					// Solo: every other channel of every track hidden.
					State->HiddenChannels.Reset();
					if (Canvas.IsValid())
					{
						for (const FLayerModel& Other : Canvas->GetModels())
						{
							for (const EChannel OtherChannel : Other.Channels)
							{
								const uint64 OtherId = ChannelId(Other.Layer, OtherChannel);
								if (OtherId != Id)
								{
									State->HiddenChannels.Add(OtherId);
								}
							}
						}
					}
					State->ActiveChannel = Id;
					FitView();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(IsDashed(Channel) ? 6.f : 12.f)
						.HeightOverride(3.f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush("WhiteBrush"))
							.ColorAndOpacity(Color)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(FMargin(IsDashed(Channel) ? 12.f : 6.f, 0.f, 4.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
						.Text(GetChannelName(Layer, Model, Channel))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(0.f, 0.f, 4.f, 0.f))
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Text_Lambda([WeakCanvas, LayerIndex, Channel]()
						{
							float Value = 0.f;
							const TSharedPtr<SWidgetAnimGraphCanvas> Pinned = WeakCanvas.Pin();
							return Pinned && Channel != EChannel::Progress && Pinned->GetValueAtPlayhead(LayerIndex, Channel, Value)
								? FText::FromString(FormatValue(Value))
								: FText::GetEmpty();
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						MakeEye(
							[this, Id]() { return !State->HiddenChannels.Contains(Id); },
							[this, Id]()
							{
								if (State->HiddenChannels.Contains(Id))
								{
									State->HiddenChannels.Remove(Id);
								}
								else
								{
									State->HiddenChannels.Add(Id);
								}
							},
							Tips::GraphChannelEyeTooltip())
					]
				]
			];
		}
	}
}

#undef LOCTEXT_NAMESPACE
