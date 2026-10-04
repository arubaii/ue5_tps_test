// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetAnimMachineEditorStyle.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "NameTBDColors.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace WidgetAnimMachineStyle
{
	namespace
	{
		namespace Palette = NameTBD::Colors::WidgetAnimEditor;

		/**
		 * The tooltip itself: empty - never shown - while there is nothing to say, and its content made as it opens, so it
		 * always says what is true now.
		 */
		class SWidgetAnimMachineToolTip : public SToolTip
		{
		public:

			SLATE_BEGIN_ARGS(SWidgetAnimMachineToolTip) {}
			SLATE_END_ARGS()

			void Construct(const FArguments& InArgs, TFunction<TSharedPtr<SWidget>()> InMakeContent)
			{
				MakeContent = MoveTemp(InMakeContent);
				SToolTip::Construct(SToolTip::FArguments()
					.TextMargin(FMargin(0.f))
					.BorderImage(ToolTipBrush()));
			}

			virtual bool IsEmpty() const override
			{
				// Asked before every open: made here, kept for OnOpening.
				Pending = MakeContent ? MakeContent() : nullptr;
				return !Pending.IsValid();
			}

			virtual void OnOpening() override
			{
				if (!Pending.IsValid() && MakeContent)
				{
					Pending = MakeContent();
				}
				if (Pending.IsValid())
				{
					SetContentWidget(Pending.ToSharedRef());
				}
				Pending.Reset();
			}

		private:

			TFunction<TSharedPtr<SWidget>()> MakeContent;
			mutable TSharedPtr<SWidget> Pending;
		};

		TSharedRef<SWidget> MakeText(const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Color, float WrapAt = 360.f)
		{
			return SNew(STextBlock)
				.Text(Text)
				.Font(Font)
				.ColorAndOpacity(Color)
				.WrapTextAt(WrapAt);
		}

		/** "Name: what it does" as two lines - see MakeToolTip. */
		TSharedPtr<SWidget> MakeNamedTip(const FText& Text)
		{
			constexpr int32 MaxNameLength = 40;
			const FString Whole = Text.ToString();
			if (Whole.TrimStartAndEnd().IsEmpty())
			{
				return nullptr;
			}

			FString Name;
			FString Description = Whole;
			int32 Colon = INDEX_NONE;
			if (Whole.FindChar(TEXT(':'), Colon) && Colon > 0 && Colon <= MaxNameLength && Whole.Mid(Colon + 1, 1) == TEXT(" ") && !Whole.Left(Colon).Contains(TEXT("\n")))
			{
				Name = Whole.Left(Colon).TrimStartAndEnd();
				Description = Whole.Mid(Colon + 1).TrimStartAndEnd();
			}
			else if (Whole.Len() <= 32 && !Whole.Contains(TEXT("\n")))
			{
				Name = Whole;
				Description.Reset();
			}

			FTipBuilder Builder;
			if (!Name.IsEmpty())
			{
				Builder.Title(FText::FromString(Name), Palette::TooltipControlName);
			}
			if (!Description.IsEmpty())
			{
				Builder.Paragraph(FText::FromString(Description));
			}
			return Builder.Build();
		}
	}

	TSharedPtr<IToolTip> MakeToolTip(const TAttribute<FText>& Text)
	{
		return MakeDynamicToolTip([Text]() { return MakeNamedTip(Text.Get()); });
	}

	TSharedPtr<IToolTip> MakeDynamicToolTip(TFunction<TSharedPtr<SWidget>()> MakeContent)
	{
		return SNew(SWidgetAnimMachineToolTip, MoveTemp(MakeContent));
	}

	// -----------------------------------------------------------------------------------------------------
	// FTipBuilder
	// -----------------------------------------------------------------------------------------------------

	FTipBuilder& FTipBuilder::Title(const FText& Text, const FLinearColor& Color)
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Title;
		Line.Value = Text;
		Line.Color = Color;
		return *this;
	}

	FTipBuilder& FTipBuilder::Kind(const FText& Text)
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Kind;
		Line.Value = Text;
		return *this;
	}

	FTipBuilder& FTipBuilder::Row(const FText& Label, const FText& Value, const FLinearColor& ValueColor)
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Row;
		Line.Label = Label;
		Line.Value = Value;
		Line.Color = ValueColor;
		return *this;
	}

	FTipBuilder& FTipBuilder::Row(const FText& Label, const FText& Value)
	{
		return Row(Label, Value, Palette::TooltipValue);
	}

	FTipBuilder& FTipBuilder::Row(const FText& Label, const FText& Value, const FText& Bold, const FLinearColor& ValueColor)
	{
		Row(Label, Value, ValueColor);
		Lines.Last().Bold = Bold;
		return *this;
	}

	FTipBuilder& FTipBuilder::Paragraph(const FText& Text)
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Paragraph;
		Line.Value = Text;
		return *this;
	}

	FTipBuilder& FTipBuilder::Hint(const FText& Text)
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Hint;
		Line.Value = Text;
		return *this;
	}

	FTipBuilder& FTipBuilder::Divider()
	{
		FLine& Line = Lines.AddDefaulted_GetRef();
		Line.Type = FLine::EType::Divider;
		return *this;
	}

	TSharedRef<SWidget> FTipBuilder::Build() const
	{
		const TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

		// The rows' labels in one column, as wide as the widest, so their values line up.
		TSharedPtr<SHorizontalBox> RowsBox;
		TSharedPtr<SVerticalBox> LabelColumn;
		TSharedPtr<SVerticalBox> ValueColumn;

		auto CloseRows = [&]()
		{
			RowsBox.Reset();
			LabelColumn.Reset();
			ValueColumn.Reset();
		};

		bool bFirst = true;
		for (const FLine& Line : Lines)
		{
			const float Gap = bFirst ? 0.f : 4.f;
			bFirst = false;

			if (Line.Type == FLine::EType::Row)
			{
				if (!RowsBox.IsValid())
				{
					SAssignNew(RowsBox, SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(0.f, 0.f, 12.f, 0.f))
					[
						SAssignNew(LabelColumn, SVerticalBox)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SAssignNew(ValueColumn, SVerticalBox)
					];
					Box->AddSlot()
						.AutoHeight()
						.Padding(FMargin(0.f, Gap, 0.f, 0.f))
						[
							RowsBox.ToSharedRef()
						];
				}
				LabelColumn->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, 1.f))
					[
						MakeText(Line.Label, BodyFont(), Palette::TooltipLabel)
					];
				if (Line.Bold.IsEmpty())
				{
					ValueColumn->AddSlot()
						.AutoHeight()
						.Padding(FMargin(0.f, 1.f))
						[
							MakeText(Line.Value, BodyFont(), Line.Color, 300.f)
						];
				}
				else
				{
					ValueColumn->AddSlot()
						.AutoHeight()
						.Padding(FMargin(0.f, 1.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								MakeText(Line.Value, BodyFont(), Line.Color, 300.f)
							]
							+ SHorizontalBox::Slot()
							.AutoWidth()
							[
								MakeText(Line.Bold, BodyBoldFont(), Line.Color, 300.f)
							]
						];
				}
				continue;
			}

			CloseRows();
			switch (Line.Type)
			{
			case FLine::EType::Title:
				Box->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, Gap, 0.f, 0.f))
					[
						MakeText(Line.Value, TitleFont(), Line.Color)
					];
				break;
			case FLine::EType::Kind:
				Box->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, 0.f, 0.f, 2.f))
					[
						MakeText(Line.Value, SmallFont(), Palette::TooltipLabel)
					];
				break;
			case FLine::EType::Hint:
				Box->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, Gap + 2.f, 0.f, 0.f))
					[
						MakeText(Line.Value, SmallFont(), Palette::TooltipLabel)
					];
				break;
			case FLine::EType::Divider:
				Box->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, Gap + 2.f, 0.f, 2.f))
					[
						SNew(SBox)
						.HeightOverride(1.f)
						[
							SNew(SImage)
							.Image(FAppStyle::GetBrush(TEXT("WhiteBrush")))
							.ColorAndOpacity(Palette::TooltipDivider)
						]
					];
				break;
			default:
				Box->AddSlot()
					.AutoHeight()
					.Padding(FMargin(0.f, Gap, 0.f, 0.f))
					[
						MakeText(Line.Value, BodyFont(), Palette::TooltipValue)
					];
				break;
			}
		}

		// Never hit-testable: a tip under the cursor would take the hover from what it is for.
		return SNew(SBox)
			.Visibility(EVisibility::HitTestInvisible)
			.Padding(FMargin(12.f, 9.f))
			.MaxDesiredWidth(440.f)
			[
				Box
			];
	}

	// -----------------------------------------------------------------------------------------------------
	// Brushes and fonts
	// -----------------------------------------------------------------------------------------------------

	const FSlateBrush* ToolTipBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::TooltipBackground, 5.f, Palette::TooltipOutline, 1.f);
		return &Brush;
	}

	FSlateFontInfo TitleFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 10); }
	FSlateFontInfo BodyFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 9); }
	FSlateFontInfo BodyBoldFont() { return FCoreStyle::GetDefaultFontStyle("Bold", 9); }
	FSlateFontInfo SmallFont() { return FCoreStyle::GetDefaultFontStyle("Regular", 8); }

	const FSlateBrush* RoundBrush()
	{
		// No radius given: rounded by half its height, whatever size it is drawn - so square, it is a circle.
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White);
		return &Brush;
	}

	const FSlateBrush* CircleBrush(float Diameter)
	{
		// Rounded right round: a box whose corners are half its size is a disc.
		static TMap<int32, TUniquePtr<FSlateRoundedBoxBrush>> Brushes;
		const int32 Key = FMath::Max(FMath::RoundToInt32(Diameter * 4.f), 4);
		TUniquePtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(Key);
		if (!Brush.IsValid())
		{
			Brush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::White, Key / 8.f);
		}
		return Brush.Get();
	}

	const FSlateBrush* RingBrush(float Diameter, float Width)
	{
		static TMap<TPair<int32, int32>, TUniquePtr<FSlateRoundedBoxBrush>> Brushes;
		const TPair<int32, int32> Key(FMath::Max(FMath::RoundToInt32(Diameter * 4.f), 4), FMath::Max(FMath::RoundToInt32(Width * 4.f), 1));
		TUniquePtr<FSlateRoundedBoxBrush>& Brush = Brushes.FindOrAdd(Key);
		if (!Brush.IsValid())
		{
			Brush = MakeUnique<FSlateRoundedBoxBrush>(FLinearColor::Transparent, Key.Key / 8.f, FLinearColor::White, Key.Value / 4.f);
		}
		return Brush.Get();
	}

	const FSlateBrush* RoundedBrush(float Radius)
	{
		static const FSlateRoundedBoxBrush Brush3(FLinearColor::White, 3.f);
		static const FSlateRoundedBoxBrush Brush5(FLinearColor::White, 5.f);
		static const FSlateRoundedBoxBrush Brush8(FLinearColor::White, 8.f);
		if (Radius <= 4.f) { return &Brush3; }
		if (Radius <= 6.5f) { return &Brush5; }
		return &Brush8;
	}

	const FSlateBrush* SharpBoxBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 0.f, FLinearColor::Black, 1.f);
		return &Brush;
	}

	const FSlateBrush* RoundedOutlineBrush(float Radius)
	{
		static const FSlateRoundedBoxBrush Brush5(FLinearColor::Transparent, 5.f, FLinearColor::White, 1.5f);
		static const FSlateRoundedBoxBrush Brush8(FLinearColor::Transparent, 8.f, FLinearColor::White, 1.5f);
		return Radius <= 6.5f ? &Brush5 : &Brush8;
	}

	void DrawStripesInCircle(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const FVector2f& Center, float Radius,
		const FLinearColor& Color, float Width, float Spacing)
	{
		if (Radius <= 1.f || Spacing <= 0.f)
		{
			return;
		}

		// Bands running down-left to up-right, each the chord of the circle on the line x + y = C (in Center's frame), pulled
		// in by half the band's width so its ends stay inside.
		const float Inner = FMath::Max(Radius - Width * 0.5f, 0.f);
		const float Reach = Inner * UE_SQRT_2;
		const float Step = Spacing * UE_SQRT_2;
		for (float C = -FMath::FloorToFloat(Reach / Step) * Step; C <= Reach; C += Step)
		{
			// On x + y = C the point nearest the center is (C/2, C/2), at C / sqrt(2) from it.
			const float Distance = FMath::Abs(C) / UE_SQRT_2;
			if (Distance >= Inner)
			{
				continue;
			}
			const float HalfChord = FMath::Sqrt(Inner * Inner - Distance * Distance);
			const FVector2f Mid(C * 0.5f, C * 0.5f);
			const FVector2f Along(UE_INV_SQRT_2, -UE_INV_SQRT_2);
			TArray<FVector2f> Points;
			Points.Add(Center + Mid - Along * HalfChord);
			Points.Add(Center + Mid + Along * HalfChord);
			FSlateDrawElement::MakeLines(OutDrawElements, Layer, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, /*bAntialias=*/true, Width);
		}
	}

	FString FormatSeconds(float Seconds)
	{
		FString Out = FString::Printf(TEXT("%.2f"), FMath::Max(Seconds, 0.f));
		while (Out.Contains(TEXT(".")) && Out.EndsWith(TEXT("0")))
		{
			Out.LeftChopInline(1);
		}
		if (Out.EndsWith(TEXT(".")))
		{
			Out.LeftChopInline(1);
		}
		return Out + TEXT("s");
	}
}
