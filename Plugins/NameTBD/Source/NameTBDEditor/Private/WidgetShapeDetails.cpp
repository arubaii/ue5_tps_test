// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetShapeDetails.h"

#include "NameTBDColors.h"
#include "NameTBDStyle.h"
#include "NameTBDTooltips.h"
#include "WidgetShapeEditorUI.h"
#include "WidgetShapePathEditor.h"
#include "WidgetShapePresets.h"
#include "NameTBD/Widgets/Shapes/ShapeWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Containers/Ticker.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WidgetShapeDetails"

namespace WidgetShapeDetailsImpl
{
	namespace Tips = NameTBD::Tooltips::ShapeEditor;

	/** The class every object is, when they are all one kind - else null. */
	const UClass* CommonClass(const TArray<TWeakObjectPtr<UObject>>& Objects)
	{
		const UClass* Common = nullptr;
		for (const TWeakObjectPtr<UObject>& Object : Objects)
		{
			if (!Object.IsValid())
			{
				continue;
			}
			if (!Common)
			{
				Common = Object->GetClass();
			}
			else if (Common != Object->GetClass())
			{
				return nullptr;
			}
		}
		return Common;
	}

	/** A shape's outline from a primitive made on the spot with its default settings, tweaked by Setup. */
	template <typename TShape>
	FWidgetShapePath PathOf(TFunctionRef<void(TShape&)> Setup)
	{
		TShape* Shape = NewObject<TShape>(GetTransientPackage());
		Setup(*Shape);
		FWidgetShapePath Path;
		Shape->BuildPath(Path);
		return Path;
	}

	/** A heart: two lobes and a point, as Mirrored and Disconnected vertices. */
	FWidgetShapePath MakeHeart()
	{
		FWidgetShapePath Path;
		FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
		auto Add = [&Contour](const FVector2D& Position, EWidgetShapeVertexType Type, const FVector2D& In, const FVector2D& Out)
		{
			FWidgetShapeVertex Vertex(Position);
			Vertex.Type = Type;
			Vertex.InHandle = In;
			Vertex.OutHandle = Out;
			Contour.Vertices.Add(Vertex);
		};
		Add(FVector2D(0.5, 0.25), EWidgetShapeVertexType::Disconnected, FVector2D(-0.05, -0.15), FVector2D(0.05, -0.15));
		Add(FVector2D(1.0, 0.33), EWidgetShapeVertexType::Mirrored, FVector2D(0.0, -0.2), FVector2D(0.0, 0.2));
		Add(FVector2D(0.5, 1.0), EWidgetShapeVertexType::Disconnected, FVector2D(0.3, -0.2), FVector2D(-0.3, -0.2));
		Add(FVector2D(0.0, 0.33), EWidgetShapeVertexType::Mirrored, FVector2D(0.0, 0.2), FVector2D(0.0, -0.2));
		Path.FitToBox();
		return Path;
	}

	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 3.f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

TSharedRef<IDetailCustomization> FWidgetShapeDetails::MakeInstance()
{
	return MakeShared<FWidgetShapeDetails>();
}

FWidgetShapeDetails::~FWidgetShapeDetails()
{
	WidgetShapePathEditing::OnPathEdited().Remove(PathEditedHandle);
}

void FWidgetShapeDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	WeakBuilder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FWidgetShapeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	using namespace WidgetShapeDetailsImpl;

	DetailBuilder.GetObjectsBeingCustomized(Objects);
	ShapeClass = CommonClass(Objects);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Shape"), LOCTEXT("ShapeCategory", "Shape"), ECategoryPriority::Important);
	const FSlateFontInfo Font = IDetailLayoutBuilder::GetDetailFont();

	// ---- Preset -------------------------------------------------------------------------------------

	Category.AddCustomRow(Tips::PresetRowName())
		.NameContent()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font)
			.Text(Tips::PresetRowName())
			.ToolTipText(Tips::PresetTooltip())
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.IsEnabled(ShapeClass != nullptr)
			.ToolTipText(ShapeClass ? Tips::PresetTooltip() : Tips::PresetMixedSelection())
			.OnGetMenuContent(FOnGetContent::CreateRaw(this, &FWidgetShapeDetails::MakePresetMenu))
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(Font)
				.Text(Tips::PresetComboEmpty())
			]
		];

	// ---- A Custom Shape's outline tools -------------------------------------------------------------

	if (!ShapeClass || !ShapeClass->IsChildOf(UCustomShape::StaticClass()))
	{
		return;
	}

	TWeakObjectPtr<UObject> First;
	if (Objects.Num() > 0)
	{
		First = Objects[0];
	}
	using WidgetShapeEditorUI::FChip;
	using WidgetShapeEditorUI::MakeChip;

	auto PathChip = [this, First](const FText& Label, const FText& ToolTip, TFunction<void(FWidgetShapePath&)> Change, const FText& Description)
	{
		FChip Chip;
		Chip.Label = Label;
		Chip.ToolTip = ToolTip;
		Chip.OnClick = [this, First, Change, Description]()
		{
			if (const UCustomShape* Shape = Cast<UCustomShape>(First.Get()))
			{
				FWidgetShapePath Path = Shape->Path;
				Change(Path);
				SetPath(Path, Description);
			}
		};
		return MakeChip(Chip);
	};

	FChip EditChip;
	EditChip.Icon = FNameTBDStyle::ShapeEditIcon;
	EditChip.GetLabel = [First]() { return WidgetShapePathEditing::IsEditing(First.Get()) ? Tips::EditOnCanvasActive() : Tips::EditOnCanvas(); };
	EditChip.ToolTip = Tips::EditOnCanvasTooltip();
	EditChip.IsActive = [First]() { return WidgetShapePathEditing::IsEditing(First.Get()); };
	EditChip.OnClick = [First]() { WidgetShapePathEditing::SetEditing(First.Get(), !WidgetShapePathEditing::IsEditing(First.Get())); };

	Category.AddCustomRow(Tips::ShapeToolsRow())
		.NameContent()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(Font)
			.Text(Tips::ShapeToolsRow())
			.ToolTipText_Lambda([First]()
			{
				const UCustomShape* Shape = Cast<UCustomShape>(First.Get());
				return Shape ? FText::Format(Tips::OutlineSummary(), Shape->Path.GetVertexCount(), Shape->Path.Contours.Num()) : FText::GetEmpty();
			})
		]
		.ValueContent()
		.MinDesiredWidth(280.f)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 2.f, 4.f, 2.f))
			[
				MakeChip(EditChip)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 2.f, 4.f, 2.f))
			[
				SNew(SComboButton)
				.ToolTipText(Tips::StartFromTooltip())
				.OnGetMenuContent(FOnGetContent::CreateRaw(this, &FWidgetShapeDetails::MakeStartFromMenu))
				.VAlign(VAlign_Center)
				.ButtonContent()
				[
					SNew(STextBlock).Font(Font).Text(Tips::StartFromLabel())
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 2.f, 4.f, 2.f))
			[
				PathChip(LOCTEXT("Fit", "Fit"), Tips::FitToBoxTooltip(), [](FWidgetShapePath& Path) { Path.FitToBox(); }, LOCTEXT("FitToWidget", "Fit Shape To Widget"))
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 2.f, 4.f, 2.f))
			[
				PathChip(LOCTEXT("FlipH", "Flip H"), Tips::FlipHorizontal(), [](FWidgetShapePath& Path) { Path.Flip(/*bHorizontal=*/true); }, Tips::FlipHorizontal())
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(0.f, 2.f, 0.f, 2.f))
			[
				PathChip(LOCTEXT("FlipV", "Flip V"), Tips::FlipVertical(), [](FWidgetShapePath& Path) { Path.Flip(/*bHorizontal=*/false); }, Tips::FlipVertical())
			]
		];

	// An edit on the canvas changes the vertex list under this panel: lay it out again to show it.
	TSet<FName> Names;
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (Object.IsValid())
		{
			Names.Add(Object->GetFName());
		}
	}
	WidgetShapePathEditing::OnPathEdited().Remove(PathEditedHandle);
	PathEditedHandle = WidgetShapePathEditing::OnPathEdited().AddLambda([this, Names](FName Name)
	{
		if (Names.Contains(Name))
		{
			RefreshSoon();
		}
	});
}

void FWidgetShapeDetails::RefreshSoon()
{
	const TWeakPtr<IDetailLayoutBuilder> Weak = WeakBuilder;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float)
	{
		if (const TSharedPtr<IDetailLayoutBuilder> Builder = Weak.Pin())
		{
			Builder->ForceRefreshDetails();
		}
		return false;
	}));
}

// =====================================================================================================
// Presets
// =====================================================================================================

TSharedRef<SWidget> FWidgetShapeDetails::MakePresetMenu()
{
	using namespace WidgetShapeDetailsImpl;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);
	const FSlateFontInfo Font = IDetailLayoutBuilder::GetDetailFont();

	TArray<WidgetShapePresets::FPreset> Presets;
	WidgetShapePresets::GetPresets(ShapeClass, Presets);

	Menu.BeginSection(NAME_None, FText::Format(LOCTEXT("PresetsHeading", "{0} Presets"), WidgetShapePresets::GetKindName(ShapeClass)));
	if (Presets.Num() == 0)
	{
		Menu.AddWidget(SNew(SBox).Padding(FMargin(8.f, 4.f)).MaxDesiredWidth(280.f)
			[
				SNew(STextBlock).Font(Font).AutoWrapText(true).Text(Tips::PresetNoneSaved())
			], FText::GetEmpty());
	}
	for (const WidgetShapePresets::FPreset& Preset : Presets)
	{
		// The name applies everything; the small buttons apply only the outline, or delete the preset.
		Menu.AddWidget(
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(6.f, 2.f))
				.ToolTipText(Tips::PresetTooltip())
				.OnClicked_Lambda([this, Preset]()
				{
					FSlateApplication::Get().DismissAllMenus();
					ApplyPreset(Preset.Name, Preset.Values, /*bOutlineOnly=*/false);
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(Font).Text(FText::FromString(Preset.Name))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(4.f, 2.f))
				.ToolTipText(Tips::PresetApplyShapeOnlyTooltip())
				.OnClicked_Lambda([this, Preset]()
				{
					FSlateApplication::Get().DismissAllMenus();
					ApplyPreset(Preset.Name, Preset.Values, /*bOutlineOnly=*/true);
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(Font).Text(LOCTEXT("OutlineOnly", "Outline"))
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(4.f, 2.f))
				.ToolTipText(Tips::PresetDeleteTooltip())
				.OnClicked_Lambda([Preset]()
				{
					FSlateApplication::Get().DismissAllMenus();
					const FText Question = FText::Format(LOCTEXT("ConfirmDelete", "Delete the shape preset \"{0}\"?"), FText::FromString(Preset.Name));
					if (FMessageDialog::Open(EAppMsgType::YesNo, Question) == EAppReturnType::Yes)
					{
						WidgetShapePresets::Delete(Preset);
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(Font).Text(LOCTEXT("DeletePresetGlyph", "X"))
				]
			],
			FText::GetEmpty(), /*bNoIndent=*/true);
	}
	Menu.EndSection();

	Menu.BeginSection(NAME_None, LOCTEXT("SaveHeading", "Save As Preset"));
	Menu.AddWidget(
		SNew(SBox)
		.MinDesiredWidth(220.f)
		.Padding(FMargin(6.f, 2.f))
		[
			SNew(SEditableTextBox)
			.Font(Font)
			.HintText(Tips::PresetSaveHint())
			.ToolTipText(Tips::PresetSaveTooltip())
			.IsEnabled(Objects.Num() > 0)
			.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
			{
				if (Commit == ETextCommit::OnEnter && !Text.IsEmptyOrWhitespace())
				{
					FSlateApplication::Get().DismissAllMenus();
					SavePreset(Text.ToString());
				}
			})
		],
		FText::GetEmpty(), /*bNoIndent=*/true);
	Menu.AddMenuEntry(
		Tips::PresetOpenFolder(),
		Tips::PresetOpenFolderTooltip(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([this]()
		{
			FPlatformProcess::ExploreFolder(*WidgetShapePresets::GetFolder(ShapeClass, /*bCreate=*/true));
		})));
	Menu.EndSection();

	return Menu.MakeWidget();
}

bool FWidgetShapeDetails::SetProperty(FName PropertyName, const UClass* Owner, const FString& Value)
{
	const TSharedPtr<IDetailLayoutBuilder> Builder = WeakBuilder.Pin();
	if (!Builder.IsValid())
	{
		return false;
	}
	const TSharedRef<IPropertyHandle> Handle = Builder->GetProperty(PropertyName, Owner);
	if (!Handle->IsValidHandle())
	{
		return false;
	}
	return Handle->SetValueFromFormattedString(Value) == FPropertyAccess::Success;
}

void FWidgetShapeDetails::ApplyPreset(const FString& Name, const TArray<TPair<FName, FString>>& Values, bool bOutlineOnly)
{
	if (!ShapeClass)
	{
		return;
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("ApplyPreset", "Apply Shape Preset \"{0}\""), FText::FromString(Name)));
	TArray<FProperty*> Properties;
	WidgetShapePresets::GetPresetProperties(ShapeClass, bOutlineOnly, Properties);
	for (const FProperty* Property : Properties)
	{
		const TPair<FName, FString>* Value = Values.FindByPredicate([Property](const TPair<FName, FString>& Pair) { return Pair.Key == Property->GetFName(); });
		if (Value)
		{
			SetProperty(Property->GetFName(), Property->GetOwnerClass(), Value->Value);
		}
	}
	RefreshSoon();
}

void FWidgetShapeDetails::SavePreset(const FString& Name)
{
	using namespace WidgetShapeDetailsImpl;

	const UObject* Shape = Objects.Num() > 0 ? Objects[0].Get() : nullptr;
	if (!Shape || !ShapeClass)
	{
		return;
	}

	const FString Trimmed = Name.TrimStartAndEnd();
	const FString File = WidgetShapePresets::GetPresetFile(ShapeClass, Trimmed);
	if (!File.IsEmpty() && FPaths::FileExists(File))
	{
		const FText Question = FText::Format(Tips::PresetReplaceConfirm(), WidgetShapePresets::GetKindName(ShapeClass), FText::FromString(Trimmed));
		if (FMessageDialog::Open(EAppMsgType::YesNo, Question) != EAppReturnType::Yes)
		{
			return;
		}
	}

	TArray<TPair<FName, FString>> Values;
	WidgetShapePresets::Capture(Shape, Values);
	FText Error;
	if (WidgetShapePresets::Save(ShapeClass, Trimmed, Values, Error))
	{
		Notify(FText::Format(Tips::PresetSaved(), FText::FromString(Trimmed)), /*bSuccess=*/true);
	}
	else
	{
		Notify(Error, /*bSuccess=*/false);
	}
}

// =====================================================================================================
// Start From
// =====================================================================================================

void FWidgetShapeDetails::SetPath(const FWidgetShapePath& Path, const FText& Description)
{
	FString Exported;
	FWidgetShapePath::StaticStruct()->ExportText(Exported, &Path, nullptr, nullptr, PPF_None, nullptr);
	const FScopedTransaction Transaction(Description);
	SetProperty(GET_MEMBER_NAME_CHECKED(UCustomShape, Path), UCustomShape::StaticClass(), Exported);
	RefreshSoon();
}

void FWidgetShapeDetails::CopyFromShape(const UShapeWidget* Source)
{
	if (!Source)
	{
		return;
	}

	// The outline as it is drawn in its own widget - its fit, scale and turn worked into the vertices - as fractions of
	// that widget, so a Custom Shape the same size looks exactly the same.
	FVector2D Size = Source->DesiredSize;
	if (Source->GetCachedWidget().IsValid())
	{
		const FVector2D Laid = Source->GetCachedGeometry().GetLocalSize();
		if (Laid.X > 0.0 && Laid.Y > 0.0)
		{
			Size = Laid;
		}
	}
	Size = Size.ComponentMax(FVector2D(1.0, 1.0));
	const FWidgetShapeFrame SourceFrame = Source->MakeFrame(FVector2f(static_cast<float>(Size.X), static_cast<float>(Size.Y)));

	FWidgetShapePath Path;
	Source->BuildPath(Path);
	for (FWidgetShapeContour& Contour : Path.Contours)
	{
		for (FWidgetShapeVertex& Vertex : Contour.Vertices)
		{
			const FVector2f Position(static_cast<float>(Vertex.Position.X), static_cast<float>(Vertex.Position.Y));
			const FVector2f Local = SourceFrame.ToLocal(Position);
			const FVector2f In = SourceFrame.VectorToLocal(FVector2f(static_cast<float>(Vertex.InHandle.X), static_cast<float>(Vertex.InHandle.Y)));
			const FVector2f Out = SourceFrame.VectorToLocal(FVector2f(static_cast<float>(Vertex.OutHandle.X), static_cast<float>(Vertex.OutHandle.Y)));
			Vertex.Position = FVector2D(Local.X / Size.X, Local.Y / Size.Y);
			Vertex.InHandle = FVector2D(In.X / Size.X, In.Y / Size.Y);
			Vertex.OutHandle = FVector2D(Out.X / Size.X, Out.Y / Size.Y);
		}
	}

	const FScopedTransaction Transaction(FText::Format(LOCTEXT("CopyFromShape", "Copy Shape From {0}"), FText::FromName(Source->GetFName())));
	SetPath(Path, FText::GetEmpty());

	// Its look, and a plain layout now that the layout is in the vertices.
	for (const FName Look : { GET_MEMBER_NAME_CHECKED(UShapeWidget, bFill), GET_MEMBER_NAME_CHECKED(UShapeWidget, Fill),
		GET_MEMBER_NAME_CHECKED(UShapeWidget, Stroke), GET_MEMBER_NAME_CHECKED(UShapeWidget, Trim), GET_MEMBER_NAME_CHECKED(UShapeWidget, bAntiAlias) })
	{
		if (const FProperty* Property = UShapeWidget::StaticClass()->FindPropertyByName(Look))
		{
			FString Value;
			Property->ExportTextItem_InContainer(Value, Source, nullptr, nullptr, PPF_None);
			SetProperty(Look, UShapeWidget::StaticClass(), Value);
		}
	}
	SetProperty(GET_MEMBER_NAME_CHECKED(UShapeWidget, ShapeRotation), UShapeWidget::StaticClass(), TEXT("0.0"));
	SetProperty(GET_MEMBER_NAME_CHECKED(UShapeWidget, ShapeScale), UShapeWidget::StaticClass(), TEXT("(X=1.0,Y=1.0)"));
	SetProperty(GET_MEMBER_NAME_CHECKED(UShapeWidget, Fit), UShapeWidget::StaticClass(), TEXT("Stretch"));
}

TSharedRef<SWidget> FWidgetShapeDetails::MakeStartFromMenu()
{
	using namespace WidgetShapeDetailsImpl;

	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection=*/true, nullptr);

	auto AddPrimitive = [this, &Menu](const FText& Label, TFunction<FWidgetShapePath()> Make)
	{
		Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Label, Make]()
		{
			SetPath(Make(), FText::Format(LOCTEXT("StartFrom", "Start Shape From {0}"), Label));
		})));
	};

	Menu.BeginSection(NAME_None, Tips::StartFromPrimitives());
	AddPrimitive(LOCTEXT("Circle", "Circle"), []() { return PathOf<UEllipseShape>([](UEllipseShape&) {}); });
	AddPrimitive(LOCTEXT("Square", "Square"), []()
	{
		FWidgetShapePath Path;
		FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
		for (const FVector2D& Corner : { FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0) })
		{
			Contour.Vertices.Add(FWidgetShapeVertex(Corner));
		}
		return Path;
	});
	AddPrimitive(LOCTEXT("RoundedSquare", "Rounded Square"), []()
	{
		FWidgetShapePath Path;
		FWidgetShapeContour& Contour = Path.Contours.AddDefaulted_GetRef();
		for (const FVector2D& Corner : { FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(1.0, 1.0), FVector2D(0.0, 1.0) })
		{
			Contour.Vertices.Add(FWidgetShapeVertex(Corner, 12.f));
		}
		return Path;
	});
	AddPrimitive(LOCTEXT("Triangle", "Triangle"), []() { return PathOf<UTriangleShape>([](UTriangleShape&) {}); });
	AddPrimitive(LOCTEXT("Star", "Star"), []() { return PathOf<UStarShape>([](UStarShape&) {}); });
	AddPrimitive(LOCTEXT("Pentagon", "Pentagon"), []() { return PathOf<UPolygonShape>([](UPolygonShape& Shape) { Shape.Sides = 5; }); });
	AddPrimitive(LOCTEXT("Hexagon", "Hexagon"), []() { return PathOf<UPolygonShape>([](UPolygonShape& Shape) { Shape.Sides = 6; }); });
	AddPrimitive(LOCTEXT("Heart", "Heart"), []() { return MakeHeart(); });
	Menu.EndSection();

	// The other shape widgets of this Widget Blueprint, exactly as they are.
	const UObject* Self = Objects.Num() > 0 ? Objects[0].Get() : nullptr;
	const UWidgetTree* Tree = Self ? Self->GetTypedOuter<UWidgetTree>() : nullptr;
	TArray<const UShapeWidget*> Shapes;
	if (Tree)
	{
		Tree->ForEachWidget([&Shapes, Self](UWidget* Widget)
		{
			if (const UShapeWidget* Shape = Cast<UShapeWidget>(Widget))
			{
				if (Shape != Self)
				{
					Shapes.Add(Shape);
				}
			}
		});
	}
	if (Shapes.Num() > 0)
	{
		Menu.BeginSection(NAME_None, Tips::StartFromWidgets());
		for (const UShapeWidget* Shape : Shapes)
		{
			const TWeakObjectPtr<const UShapeWidget> WeakShape = Shape;
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("WidgetEntry", "{0}  ({1})"), FText::FromName(Shape->GetFName()), Shape->GetClass()->GetDisplayNameText()),
				FText::GetEmpty(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([this, WeakShape]()
				{
					CopyFromShape(WeakShape.Get());
				})));
		}
		Menu.EndSection();
	}

	return Menu.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
