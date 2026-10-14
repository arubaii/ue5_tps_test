// Copyright AR GameSystems 2026. All rights reserved.

#include "WidgetEditorHooks.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Blueprint.h"
#include "NameTBD/Widgets/WidgetTreeLookup.h"

namespace WidgetEditorHooks
{
	namespace
	{
		FAssetCreator& GetAssetCreator()
		{
			static FAssetCreator Creator;
			return Creator;
		}
	}

	UWidget* FindDesignerTemplate(const UWidget* PreviewWidget)
	{
#if WITH_EDITOR
		if (!PreviewWidget || !PreviewWidget->IsDesignTime())
		{
			return nullptr;
		}
		// A preview copy lives in the designer's preview User Widget; a template in the Widget Blueprint's own tree.
		const UUserWidget* Preview = PreviewWidget->GetTypedOuter<UUserWidget>();
		const UBlueprint* Blueprint = Preview ? UBlueprint::GetBlueprintFromClass(Preview->GetClass()) : nullptr;
		const UWidgetTree* Tree = Blueprint ? WidgetTreeLookup::FindWidgetTree(Blueprint) : nullptr;
		UWidget* Template = Tree ? Tree->FindWidget(PreviewWidget->GetFName()) : nullptr;
		return Template != PreviewWidget && Template && Template->GetClass() == PreviewWidget->GetClass() ? Template : nullptr;
#else
		return nullptr;
#endif
	}

	void SetAssetCreator(FAssetCreator InCreator)
	{
		GetAssetCreator() = MoveTemp(InCreator);
	}

	UObject* CreateAsset(UClass* Class, const FString& DefaultName)
	{
		const FAssetCreator& Creator = GetAssetCreator();
		return Creator && Class ? Creator(Class, DefaultName) : nullptr;
	}
}
