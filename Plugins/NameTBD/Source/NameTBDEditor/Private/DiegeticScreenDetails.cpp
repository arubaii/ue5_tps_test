// Copyright AR GameSystems 2026. All rights reserved.

#include "DiegeticScreenDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DiegeticScreenMaterial.h"
#include "Editor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/Material.h"
#include "NameTBD/Diegetic/DiegeticScreenComponent.h"
#include "NameTBDTooltips.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DiegeticScreenDetails"

namespace DiegeticScreenDetailsPrivate
{
	/** Where Copy To Project puts the copies. */
	const TCHAR* MaterialFolder = TEXT("/Game/Diegetic");

	void Notify(const FText& Message, bool bSucceeded)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSucceeded ? 4.f : 8.f;
		Info.bUseSuccessFailIcons = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(bSucceeded ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

TSharedRef<IDetailCustomization> FDiegeticScreenDetails::MakeInstance()
{
	return MakeShared<FDiegeticScreenDetails>();
}

void FDiegeticScreenDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	namespace Tips = NameTBD::Tooltips::DiegeticScreen;

	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// The Widget Component's materials and Material Mode: Copy To Project sets them through these.
	MaterialsHandle = DetailBuilder.GetProperty(TEXT("OverrideMaterials"), UMeshComponent::StaticClass());
	MaterialModeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDiegeticScreenComponent, MaterialMode));

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Diegetic Screen"));
	Category.AddCustomRow(Tips::ScreenMaterialLabel())
		.NameContent()
		[
			SNew(STextBlock)
			.Text(Tips::ScreenMaterialLabel())
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(200.f)
		[
			SNew(SButton)
			.Text(Tips::CreateScreenMaterialButton())
			.ToolTipText(Tips::CreateScreenMaterialTooltip())
			.OnClicked(this, &FDiegeticScreenDetails::OnCreateScreenMaterial)
		];
}

FReply FDiegeticScreenDetails::OnCreateScreenMaterial()
{
	namespace Tips = NameTBD::Tooltips::DiegeticScreen;

	// The variant the first selected screen is drawn with.
	const UDiegeticScreenComponent* First = nullptr;
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		First = Cast<UDiegeticScreenComponent>(Object.Get());
		if (First)
		{
			break;
		}
	}
	if (!First)
	{
		return FReply::Handled();
	}

	FText Error;
	UMaterial* Material = DiegeticScreenMaterial::CopyToProject(First->GetMaterialVariant(), DiegeticScreenDetailsPrivate::MaterialFolder, Error);
	if (!Material)
	{
		DiegeticScreenDetailsPrivate::Notify(FText::Format(Tips::MaterialFailedNotice(), Error), false);
		return FReply::Handled();
	}

	// Through the panel: one undo step, every selected screen, a Blueprint's template and its instances alike. Material Custom,
	// so the copy is what they draw with.
	{
		const FScopedTransaction Transaction(LOCTEXT("AssignScreenMaterial", "Copy Screen Material To Project"));
		bool bAssigned = false;
		if (MaterialModeHandle.IsValid() && MaterialModeHandle->IsValidHandle())
		{
			MaterialModeHandle->SetValue(static_cast<uint8>(EDiegeticMaterialMode::Custom));
		}
		if (MaterialsHandle.IsValid() && MaterialsHandle->IsValidHandle())
		{
			if (const TSharedPtr<IPropertyHandleArray> AsArray = MaterialsHandle->AsArray())
			{
				uint32 Count = 0;
				if (AsArray->GetNumElements(Count) == FPropertyAccess::Success && Count == 0)
				{
					AsArray->AddItem();
				}
				const TSharedRef<IPropertyHandle> Element = AsArray->GetElement(0);
				bAssigned = Element->IsValidHandle() && Element->SetValue(static_cast<const UObject*>(Material)) == FPropertyAccess::Success;
			}
		}
		// Without the handles (hidden, or a mix of array sizes): straight onto each screen.
		if (!bAssigned)
		{
			for (const TWeakObjectPtr<UObject>& Object : Objects)
			{
				if (UDiegeticScreenComponent* Screen = Cast<UDiegeticScreenComponent>(Object.Get()))
				{
					Screen->Modify();
					Screen->MaterialMode = EDiegeticMaterialMode::Custom;
					Screen->SetMaterial(0, Material);
				}
			}
		}
	}

	if (GEditor)
	{
		TArray<UObject*> ToShow = { Material };
		GEditor->SyncBrowserToObjects(ToShow);
	}
	DiegeticScreenDetailsPrivate::Notify(FText::Format(Tips::MaterialCreatedNotice(), FText::FromString(Material->GetPathName())), true);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
