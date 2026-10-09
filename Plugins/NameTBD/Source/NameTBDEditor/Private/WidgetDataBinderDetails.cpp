// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetDataBinderDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Misc/App.h"
#include "NameTBDTooltips.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WidgetDataBinderComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetDataBinderDetails"

namespace WidgetDataBinderDetailsLocal
{
	namespace Tips = NameTBD::Tooltips::DataPreview;

	/** How long the list of designer copies is trusted before it is looked up again. */
	constexpr double PreviewLookupSeconds = 0.5;

	TSharedRef<SWidget> MakeButton(const FText& Label, const FText& Tip, FSimpleDelegate OnClick, TAttribute<bool> Enabled = true)
	{
		return SNew(SButton)
			.ContentPadding(FMargin(6.f, 1.f))
			.ToolTipText(Tip)
			.IsEnabled(Enabled)
			.OnClicked_Lambda([OnClick]()
			{
				OnClick.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Label)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

TSharedRef<IDetailCustomization> FWidgetDataBinderDetails::MakeInstance()
{
	return MakeShared<FWidgetDataBinderDetails>();
}

FWidgetDataBinderDetails::~FWidgetDataBinderDetails()
{
	if (PlayHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlayHandle);
	}
	if (RebindHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RebindHandle);
	}
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

TArray<UWidgetDataBinderComponent*> FWidgetDataBinderDetails::GetPreviews() const
{
	const double Now = FApp::GetCurrentTime();
	if (CachedAt < 0.0 || Now - CachedAt > WidgetDataBinderDetailsLocal::PreviewLookupSeconds)
	{
		CachedAt = Now;
		CachedPreviews.Reset();
		if (const UWidgetDataBinderComponent* Binder = Template.Get())
		{
			for (UWidgetDataBinderComponent* Preview : Binder->FindDesignerPreviews())
			{
				CachedPreviews.Add(Preview);
			}
		}
	}

	TArray<UWidgetDataBinderComponent*> Previews;
	for (const TWeakObjectPtr<UWidgetDataBinderComponent>& Cached : CachedPreviews)
	{
		if (UWidgetDataBinderComponent* Preview = Cached.Get())
		{
			if (Preview->IsDesignBound())
			{
				Previews.Add(Preview);
			}
		}
	}
	return Previews;
}

UWidgetDataBinderComponent* FWidgetDataBinderDetails::GetFirstPreview() const
{
	const TArray<UWidgetDataBinderComponent*> Previews = GetPreviews();
	return Previews.Num() > 0 ? Previews[0] : nullptr;
}

void FWidgetDataBinderDetails::ShowSet(int32 SetIndex)
{
	for (UWidgetDataBinderComponent* Preview : GetPreviews())
	{
		Preview->SetPreviewSampleSet(SetIndex);
	}
}

void FWidgetDataBinderDetails::Step(int32 Delta)
{
	const UWidgetDataBinderComponent* Binder = Template.Get();
	const UWidgetDataBinderComponent* First = GetFirstPreview();
	const int32 Count = Binder ? Binder->SampleSets.Num() : 0;
	if (Count == 0 || !First)
	{
		return;
	}
	const int32 Current = First->GetPreviewSampleSet();
	const int32 Next = Current == INDEX_NONE ? (Delta > 0 ? 0 : Count - 1) : ((Current + Delta) % Count + Count) % Count;
	ShowSet(Next);
}

void FWidgetDataBinderDetails::TogglePlay()
{
	if (PlayHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PlayHandle);
		PlayHandle.Reset();
		return;
	}
	PlayElapsed = 0.f;
	Step(1);
	PlayHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FWidgetDataBinderDetails::TickPlay));
}

bool FWidgetDataBinderDetails::TickPlay(float DeltaTime)
{
	const UWidgetDataBinderComponent* Binder = Template.Get();
	if (!Binder || Binder->SampleSets.Num() == 0)
	{
		PlayHandle.Reset();
		return false;
	}
	PlayElapsed += DeltaTime;
	if (PlayElapsed >= FMath::Max(Binder->SampleSetInterval, 0.25f))
	{
		PlayElapsed = 0.f;
		Step(1);
	}
	return true;
}

void FWidgetDataBinderDetails::Rebind()
{
	const UWidgetDataBinderComponent* Binder = Template.Get();
	for (UWidgetDataBinderComponent* Preview : GetPreviews())
	{
		Preview->RefreshDesignerPreviewFrom(Binder);
	}
}

void FWidgetDataBinderDetails::HandleObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	// A sample row typed, a trigger added: into the preview next frame - once, however many edits the frame held.
	if (Object != Template.Get() || RebindHandle.IsValid() || Event.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}
	RebindHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		RebindHandle.Reset();
		CachedAt = -1.0;
		Rebind();
		return false;
	}));
}

FText FWidgetDataBinderDetails::GetSetLabel() const
{
	const UWidgetDataBinderComponent* Binder = Template.Get();
	const int32 Count = Binder ? Binder->SampleSets.Num() : 0;
	if (Count == 0)
	{
		return LOCTEXT("NoSets", "No sample sets - add some under Mock Data > Sample Sets");
	}
	const UWidgetDataBinderComponent* First = GetFirstPreview();
	const int32 Current = First ? First->GetPreviewSampleSet() : INDEX_NONE;
	if (!Binder->SampleSets.IsValidIndex(Current))
	{
		return FText::Format(LOCTEXT("MockOnly", "Mock Values ({0} sets)"), FText::AsNumber(Count));
	}
	const FName SetName = Binder->SampleSets[Current].Name;
	return SetName.IsNone()
		? FText::Format(LOCTEXT("SetOfUnnamed", "Set {0} of {1}"), FText::AsNumber(Current + 1), FText::AsNumber(Count))
		: FText::Format(LOCTEXT("SetOf", "Set {0} of {1}: {2}"), FText::AsNumber(Current + 1), FText::AsNumber(Count), FText::FromName(SetName));
}

void FWidgetDataBinderDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace WidgetDataBinderDetailsLocal;

	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	UWidgetDataBinderComponent* Binder = Objects.Num() == 1 ? Cast<UWidgetDataBinderComponent>(Objects[0].Get()) : nullptr;
	if (!Binder)
	{
		return;
	}
	Template = Binder;
	CachedAt = -1.0;

	if (!PropertyChangedHandle.IsValid())
	{
		PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FWidgetDataBinderDetails::HandleObjectPropertyChanged);
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Data Preview"), LOCTEXT("DataPreviewCategory", "Data Preview"), ECategoryPriority::Important);
	const FSlateFontInfo Font = IDetailLayoutBuilder::GetDetailFont();
	const TAttribute<bool> HasPreview = TAttribute<bool>::CreateLambda([this]() { return GetFirstPreview() != nullptr; });

	Category.AddCustomRow(LOCTEXT("HintFilter", "Data Preview"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFontItalic())
		.AutoWrapText(true)
		.Text_Lambda([this]()
		{
			return GetFirstPreview() ? Tips::DataPreviewHint() : Tips::NoPreviewHint();
		})
	];

	Category.AddCustomRow(LOCTEXT("SetsFilter", "Sample Set"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(Font)
		.Text(LOCTEXT("SampleSetName", "Sample Set"))
	]
	.ValueContent()
	.MinDesiredWidth(320.f)
	.MaxDesiredWidth(640.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
			[
				MakeButton(LOCTEXT("Previous", "<"), Tips::PreviousSetTooltip(), FSimpleDelegate::CreateLambda([this]() { Step(-1); }), HasPreview)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
			[
				MakeButton(LOCTEXT("Next", ">"), Tips::NextSetTooltip(), FSimpleDelegate::CreateLambda([this]() { Step(1); }), HasPreview)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(6.f, 1.f))
				.IsEnabled(HasPreview)
				.ToolTipText_Lambda([this]() { return PlayHandle.IsValid() ? Tips::StopSetsTooltip() : Tips::PlaySetsTooltip(); })
				.OnClicked_Lambda([this]()
				{
					TogglePlay();
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(Font)
					.Text_Lambda([this]() { return PlayHandle.IsValid() ? LOCTEXT("Stop", "Stop") : LOCTEXT("Play", "Play"); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 2.f, 0.f)
			[
				MakeButton(LOCTEXT("MockOnlyButton", "Mock Values"), Tips::MockOnlyTooltip(), FSimpleDelegate::CreateLambda([this]() { ShowSet(INDEX_NONE); }), HasPreview)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				MakeButton(LOCTEXT("Rebind", "Rebind"), Tips::RebindTooltip(), FSimpleDelegate::CreateLambda([this]() { CachedAt = -1.0; Rebind(); }), HasPreview)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 3.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.Font(Font)
			.Text_Lambda([this]() { return GetSetLabel(); })
		]
	];

	// A row per field the ViewModel announces: what the preview holds now, editable.
	UClass* ViewModelType = Binder->ViewModelClass.Get();
	if (!ViewModelType)
	{
		return;
	}
	for (const FString& Option : Binder->GetViewModelFieldOptions())
	{
		const FName FieldName(*Option);
		if (FieldName.IsNone())
		{
			continue;
		}
		const FProperty* Field = FindFProperty<FProperty>(ViewModelType, FieldName);
		const bool bList = CastField<FArrayProperty>(Field) != nullptr;

		auto ReadValue = [this, FieldName]()
		{
			FString Text;
			const UWidgetDataBinderComponent* First = GetFirstPreview();
			return First && First->GetViewModelFieldAsText(FieldName, Text) ? FText::FromString(Text) : FText::FromString(TEXT("-"));
		};

		TSharedRef<SWidget> ValueWidget = SNullWidget::NullWidget;
		if (bList)
		{
			auto ListButton = [this, FieldName, HasPreview](const FText& Label, const FText& Tip, EWidgetDataListEdit Edit)
			{
				return MakeButton(Label, Tip, FSimpleDelegate::CreateLambda([this, FieldName, Edit]()
				{
					for (UWidgetDataBinderComponent* Preview : GetPreviews())
					{
						Preview->EditListForPreview(FieldName, Edit);
					}
				}), HasPreview);
			};

			ValueWidget = SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(Font)
					.Text_Lambda(ReadValue)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 2.f, 0.f, 0.f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
					[
						ListButton(LOCTEXT("AddRow", "Add Row"), Tips::AddRowTooltip(), EWidgetDataListEdit::AddRow)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
					[
						ListButton(LOCTEXT("RemoveRow", "Remove Row"), Tips::RemoveRowTooltip(), EWidgetDataListEdit::RemoveRow)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
					[
						ListButton(LOCTEXT("Overtake", "Overtake"), Tips::MoveUpTooltip(), EWidgetDataListEdit::MoveOneUp)
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 2.f, 0.f)
					[
						ListButton(LOCTEXT("Shuffle", "Shuffle"), Tips::ShuffleTooltip(), EWidgetDataListEdit::Shuffle)
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						ListButton(LOCTEXT("Reverse", "Reverse"), Tips::ReverseTooltip(), EWidgetDataListEdit::Reverse)
					]
				];
		}
		else if (Field)
		{
			ValueWidget = SNew(SEditableTextBox)
				.Font(Font)
				.IsEnabled(HasPreview)
				.ToolTipText(FText::Format(Tips::FieldValueTooltip(), FText::FromName(FieldName)))
				.Text_Lambda(ReadValue)
				.OnTextCommitted_Lambda([this, FieldName](const FText& NewText, ETextCommit::Type CommitType)
				{
					if (CommitType == ETextCommit::OnCleared)
					{
						return;
					}
					for (UWidgetDataBinderComponent* Preview : GetPreviews())
					{
						Preview->SetViewModelFieldFromText(FieldName, NewText.ToString());
					}
				});
		}
		else
		{
			// A getter function: shown, not set.
			ValueWidget = SNew(STextBlock)
				.Font(Font)
				.Text_Lambda(ReadValue);
		}

		Category.AddCustomRow(FText::FromName(FieldName))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(Font)
			.Text(FText::FromName(FieldName))
		]
		.ValueContent()
		.MinDesiredWidth(240.f)
		.MaxDesiredWidth(640.f)
		[
			ValueWidget
		];
	}
}

#undef LOCTEXT_NAMESPACE
