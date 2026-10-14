// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetPathConnectTool.h"

#include "NameTBDColors.h"
#include "NameTBDTooltips.h"
#include "WidgetShapeEditorUI.h"
#include "NameTBD/Widgets/Paths/WidgetPathLayer.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "WidgetBlueprint.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "WidgetPathConnectTool"

namespace WidgetPathConnectImpl
{
	namespace Palette = NameTBD::Colors::ShapeEditor;
	namespace Tips = NameTBD::Tooltips::WidgetPathTools;

	constexpr float ToolbarLift = 6.f;

	const FSlateBrush* ToolbarBrush()
	{
		static const FSlateRoundedBoxBrush Brush(Palette::ToolbarBackground, 5.f, Palette::ToolbarOutline, 1.f);
		return &Brush;
	}

	/** The first Widget Path Layer in a widget tree. */
	UWidgetPathLayer* FindLayerIn(const UWidgetTree* Tree)
	{
		UWidgetPathLayer* Found = nullptr;
		if (Tree)
		{
			Tree->ForEachWidget([&Found](UWidget* Widget)
			{
				if (!Found)
				{
					Found = Cast<UWidgetPathLayer>(Widget);
				}
			});
		}
		return Found;
	}

	UWidgetTree* TreeOf(const UWidget* Widget)
	{
		return Widget ? Widget->GetTypedOuter<UWidgetTree>() : nullptr;
	}

	/** The selection's widgets (Widget Blueprint templates and their previews), in order along the way they are spread. */
	struct FPicked
	{
		UWidget* Template = nullptr;
		UWidget* Preview = nullptr;
		FVector2D Center = FVector2D::ZeroVector;
	};

	TArray<FPicked> GetOrdered(const TArray<FWidgetReference>& Selection)
	{
		TArray<FPicked> Picked;
		for (const FWidgetReference& Reference : Selection)
		{
			UWidget* Template = Reference.GetTemplate();
			if (!Template || Template->IsA<UWidgetPathLayer>())
			{
				continue;
			}
			FPicked& Entry = Picked.AddDefaulted_GetRef();
			Entry.Template = Template;
			Entry.Preview = Reference.GetPreview();
			if (Entry.Preview)
			{
				const FGeometry& Geometry = Entry.Preview->GetCachedGeometry();
				Entry.Center = Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5));
			}
		}
		if (Picked.Num() < 2)
		{
			return Picked;
		}

		FVector2D Min = Picked[0].Center;
		FVector2D Max = Picked[0].Center;
		for (const FPicked& Entry : Picked)
		{
			Min = FVector2D(FMath::Min(Min.X, Entry.Center.X), FMath::Min(Min.Y, Entry.Center.Y));
			Max = FVector2D(FMath::Max(Max.X, Entry.Center.X), FMath::Max(Max.Y, Entry.Center.Y));
		}
		const bool bAcross = (Max.X - Min.X) >= (Max.Y - Min.Y);
		Picked.StableSort([bAcross](const FPicked& A, const FPicked& B)
		{
			return bAcross ? A.Center.X < B.Center.X : A.Center.Y < B.Center.Y;
		});
		return Picked;
	}

	bool IsConnected(const UWidgetPathLayer* Layer, FName A, FName B)
	{
		return Layer && Layer->Connections.ContainsByPredicate([A, B](const FWidgetPathConnection& Connection)
		{
			return (Connection.From == A && Connection.To == B) || (Connection.From == B && Connection.To == A);
		});
	}

	/** Applies an edit to the Widget Blueprint's layer and its preview, as one undo step. */
	void EditLayers(const TArray<FWidgetReference>& Selection, const FText& Description, TFunctionRef<void(UWidgetPathLayer&, const TArray<FPicked>&)> Edit)
	{
		const TArray<FPicked> Picked = GetOrdered(Selection);
		if (Picked.Num() < 2)
		{
			return;
		}
		UWidgetPathLayer* Template = FindLayerIn(TreeOf(Picked[0].Template));
		if (!Template)
		{
			return;
		}

		const FScopedTransaction Transaction(Description);
		Template->Modify();
		Edit(*Template, Picked);

		// The designer's preview of the same layer, so the lines show at once.
		const UWidgetTree* PreviewTree = TreeOf(Picked[0].Preview);
		if (UWidgetPathLayer* Preview = PreviewTree ? Cast<UWidgetPathLayer>(PreviewTree->FindWidget(Template->GetFName())) : nullptr)
		{
			Preview->Connections = Template->Connections;
			Preview->SynchronizeProperties();
		}
		if (UWidgetBlueprint* Blueprint = Template->GetTypedOuter<UWidgetBlueprint>())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}

	void Connect(const TArray<FWidgetReference>& Selection)
	{
		EditLayers(Selection, LOCTEXT("ConnectWidgets", "Connect Widgets"), [](UWidgetPathLayer& Layer, const TArray<FPicked>& Picked)
		{
			for (int32 Index = 1; Index < Picked.Num(); ++Index)
			{
				const FName From = Picked[Index - 1].Template->GetFName();
				const FName To = Picked[Index].Template->GetFName();
				if (!IsConnected(&Layer, From, To))
				{
					FWidgetPathConnection& Added = Layer.Connections.AddDefaulted_GetRef();
					Added.From = From;
					Added.To = To;
				}
			}
		});
	}

	void Disconnect(const TArray<FWidgetReference>& Selection)
	{
		EditLayers(Selection, LOCTEXT("DisconnectWidgets", "Disconnect Widgets"), [](UWidgetPathLayer& Layer, const TArray<FPicked>& Picked)
		{
			TSet<FName> Names;
			for (const FPicked& Entry : Picked)
			{
				Names.Add(Entry.Template->GetFName());
			}
			Layer.Connections.RemoveAll([&Names](const FWidgetPathConnection& Connection)
			{
				return Names.Contains(Connection.From) && Names.Contains(Connection.To);
			});
		});
	}

	bool HasConnectionsAmong(const TArray<FWidgetReference>& Selection)
	{
		const TArray<FPicked> Picked = GetOrdered(Selection);
		const UWidgetPathLayer* Layer = Picked.Num() > 0 ? FindLayerIn(TreeOf(Picked[0].Template)) : nullptr;
		if (!Layer)
		{
			return false;
		}
		TSet<FName> Names;
		for (const FPicked& Entry : Picked)
		{
			Names.Add(Entry.Template->GetFName());
		}
		return Layer->Connections.ContainsByPredicate([&Names](const FWidgetPathConnection& Connection)
		{
			return Names.Contains(Connection.From) && Names.Contains(Connection.To);
		});
	}

	TSharedRef<SWidget> MakeToolbar(const TArray<FWidgetReference>& Selection)
	{
		using WidgetShapeEditorUI::FChip;
		using WidgetShapeEditorUI::MakeChip;

		FChip ConnectChip;
		ConnectChip.Label = LOCTEXT("Connect", "Connect");
		ConnectChip.ToolTip = Tips::ConnectTooltip();
		ConnectChip.OnClick = [Selection]() { Connect(Selection); };

		FChip DisconnectChip;
		DisconnectChip.Label = LOCTEXT("Disconnect", "Disconnect");
		DisconnectChip.ToolTip = Tips::DisconnectTooltip();
		DisconnectChip.IsEnabled = [Selection]() { return HasConnectionsAmong(Selection); };
		DisconnectChip.OnClick = [Selection]() { Disconnect(Selection); };

		return SNew(SBorder)
			.BorderImage(ToolbarBrush())
			.Padding(FMargin(4.f, 3.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					MakeChip(ConnectChip)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(2.f, 0.f, 0.f, 0.f))
				[
					MakeChip(DisconnectChip)
				]
			];
	}
}

FWidgetPathConnectExtension::FWidgetPathConnectExtension()
{
	ExtensionId = FName(TEXT("WidgetPathConnect"));
}

bool FWidgetPathConnectExtension::CanExtendSelection(const TArray<FWidgetReference>& Selection) const
{
	using namespace WidgetPathConnectImpl;
	if (Selection.Num() < 2)
	{
		return false;
	}
	int32 Nodes = 0;
	for (const FWidgetReference& Reference : Selection)
	{
		const UWidget* Template = Reference.GetTemplate();
		if (Template && !Template->IsA<UWidgetPathLayer>())
		{
			++Nodes;
		}
	}
	return Nodes >= 2 && FindLayerIn(TreeOf(Selection[0].GetTemplate())) != nullptr;
}

void FWidgetPathConnectExtension::ExtendSelection(const TArray<FWidgetReference>& Selection, TArray<TSharedRef<FDesignerSurfaceElement>>& SurfaceElements)
{
	using namespace WidgetPathConnectImpl;
	if (!CanExtendSelection(Selection))
	{
		return;
	}
	// Above the first selected widget's top left: the selection can be anywhere, and its own tools (a shape's) sit centered.
	SurfaceElements.Add(MakeShared<FDesignerSurfaceElement>(MakeToolbar(Selection), EExtensionLayoutLocation::TopLeft, FVector2D(0.0, -ToolbarLift), FVector2D(0.0, 1.0)));
}

TSharedRef<FDesignerExtension> FWidgetPathConnectFactory::CreateDesignerExtension() const
{
	return MakeShared<FWidgetPathConnectExtension>();
}

#undef LOCTEXT_NAMESPACE
