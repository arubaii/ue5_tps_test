// Copyright AR GameSystems 2026. All rights reserved.

#include "SettingsMenu.h"

#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/Text/STextBlock.h"
#include "NameTBD/Menu/MenuManagerComponent.h"
#include "NameTBD/Menu/Settings/MenuSettingsDefaults.h"
#include "NameTBD/Menu/Settings/MenuSettingsSchema.h"
#include "NameTBD/Menu/Settings/MenuSettingsSubsystem.h"
#include "MenuStyle.h"
#include "SMenuSettingRow.h"
#include "SMenuSettingsPanel.h"

#define LOCTEXT_NAMESPACE "SettingsMenu"

/** What a single Setting Control's row needs, without a whole screen around it. */
class FMenuStandaloneRowHost : public IMenuSettingsRowHost, public TSharedFromThis<FMenuStandaloneRowHost>
{
public:
	TWeakObjectPtr<UMenuSettingsSubsystem> Settings;
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;
	FMenuStyleRef StyleHolder = MakeShared<FMenuStyleHolder>();
	TWeakPtr<SMenuSettingRow> Row;
	TMap<FName, FString> PreviewValues;
	FDelegateHandle ChangedHandle;

	virtual ~FMenuStandaloneRowHost() override
	{
		if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
		{
			Subsystem->OnSettingChangedNative().Remove(ChangedHandle);
		}
	}

	void Bind()
	{
		if (UMenuSettingsSubsystem* Subsystem = Settings.Get())
		{
			ChangedHandle = Subsystem->OnSettingChangedNative().AddSPLambda(this, [this](FName, const FString&)
			{
				if (const TSharedPtr<SMenuSettingRow> Pinned = Row.Pin())
				{
					Pinned->Refresh();
				}
			});
		}
	}

	virtual UMenuSettingsSubsystem* GetSettings() const override { return Settings.Get(); }
	virtual FMenuStyleRef GetStyle() const override { return StyleHolder; }

	virtual FMenuSettingContext GetContext() const override
	{
		FMenuSettingContext Context;
		Context.LocalPlayer = LocalPlayer.Get();
		Context.Subsystem = Settings.Get();
		return Context;
	}

	virtual int32 GetUserIndex() const override
	{
		if (const ULocalPlayer* Player = LocalPlayer.Get())
		{
			if (const TSharedPtr<const FSlateUser> User = Player->GetSlateUser())
			{
				return User->GetUserIndex();
			}
		}
		return 0;
	}

	virtual FString GetPreviewValue(const UMenuSetting& Setting) const override
	{
		const FString* Found = PreviewValues.Find(Setting.Id);
		return Found ? *Found : Setting.GetDefaultValue(GetContext());
	}

	virtual void SetPreviewValue(const UMenuSetting& Setting, const FString& Value) override
	{
		PreviewValues.Add(Setting.Id, Value);
	}
};

namespace SettingsMenuLocal
{
	/** The designer's pages: the built-in ones (if the project keeps them), the project's schemas and Extra. */
	void MakePreviewPages(UObject* Outer, TObjectPtr<UMenuSettingsSchema>& InOutBuiltIn, const TArray<TObjectPtr<UMenuSettingsSchema>>& Extra, TArray<FMenuSettingsPage>& OutPages)
	{
		if (!InOutBuiltIn)
		{
			InOutBuiltIn = MenuSettingsDefaults::MakeBuiltInSchema(Outer);
		}
		TArray<UMenuSettingsSchema*> ExtraSchemas;
		for (UMenuSettingsSchema* Schema : Extra)
		{
			ExtraSchemas.Add(Schema);
		}
		TArray<const UMenuSettingsSchema*> Schemas;
		MenuSettingsSchemaUtils::GatherSchemas(InOutBuiltIn, ExtraSchemas, Schemas);
		MenuSettingsSchemaUtils::Merge(Schemas, MenuSettingsSchemaUtils::MakeProjectRules(), OutPages);
	}
}

// =====================================================================================================
//  Settings Menu
// =====================================================================================================

USettingsMenu::USettingsMenu()
{
	Title = LOCTEXT("DefaultTitle", "Settings");
	PreviousTabKeys = { EKeys::Gamepad_LeftShoulder, EKeys::Q };
	NextTabKeys = { EKeys::Gamepad_RightShoulder, EKeys::E };
	ApplyKeys = { EKeys::Gamepad_FaceButton_Left, EKeys::F };
	ResetKeys = { EKeys::Gamepad_FaceButton_Top, EKeys::R };
	BackKeys = { EKeys::Gamepad_FaceButton_Right, EKeys::Escape };
}

UMenuSettingsSubsystem* USettingsMenu::FindSettings() const
{
	if (IsDesignTime())
	{
		return nullptr;
	}
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : UMenuSettingsSubsystem::Get(this);
}

TSharedRef<SWidget> USettingsMenu::RebuildWidget()
{
	StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
	UMenuSettingsSubsystem* Settings = FindSettings();

	TArray<FMenuSettingsPage> Preview;
	if (Settings)
	{
		for (UMenuSettingsSchema* Schema : ExtraSchemas)
		{
			Settings->RegisterSchema(Schema);
		}
		OpenSession();
	}
	else
	{
		SettingsMenuLocal::MakePreviewPages(this, PreviewSchema, ExtraSchemas, Preview);
	}

	FMenuPanelKeys Keys;
	Keys.PreviousTab = PreviousTabKeys;
	Keys.NextTab = NextTabKeys;
	Keys.Apply = ApplyKeys;
	Keys.Reset = ResetKeys;
	Keys.Back = BackKeys;
	FMenuPanelTexts Texts;
	Texts.Apply = ApplyText;
	Texts.Revert = RevertText;
	Texts.Reset = ResetText;
	Texts.Back = BackText;
	Texts.Search = SearchHint;

	TWeakObjectPtr<USettingsMenu> WeakThis(this);
	Panel = SNew(SMenuSettingsPanel, StyleHolder)
		.Settings(Settings)
		.LocalPlayer(IsDesignTime() ? nullptr : GetOwningLocalPlayer())
		.PreviewPages(Preview)
		.PageFilter(Pages)
		.InitialPage(InitialPage)
		.Title(TAttribute<FText>::CreateWeakLambda(this, [this]() { return Title; }))
		.ApplyButton(ApplyPolicy == EMenuApplyPolicy::ApplyButton)
		.ConfirmUnsavedOnBack(bConfirmUnsavedOnBack)
		.ShowBackButton(bShowBackPrompt)
		.ShowResetButton(bShowResetPrompt)
		.Keys(Keys)
		.Texts(Texts)
		.SoundContext(this)
		.MakeCustomRow([WeakThis](UMenuSetting* Setting) -> TSharedPtr<SWidget>
		{
			USettingsMenu* Menu = WeakThis.Get();
			return Menu ? Menu->MakeCustomRow(Setting) : TSharedPtr<SWidget>();
		})
		.OnBack_Lambda([WeakThis]()
		{
			if (USettingsMenu* Menu = WeakThis.Get())
			{
				Menu->OnBack.Broadcast();
			}
		})
		.OnPageChanged_Lambda([WeakThis](FName PageId)
		{
			if (USettingsMenu* Menu = WeakThis.Get())
			{
				Menu->OnPageChanged.Broadcast(PageId);
			}
		})
		.OnSettingFocused_Lambda([WeakThis](FName FocusedId)
		{
			if (USettingsMenu* Menu = WeakThis.Get())
			{
				Menu->OnSettingFocused.Broadcast(FocusedId);
			}
		});

	if (!StyleHandle.IsValid())
	{
		StyleHandle = UMenuStyleAsset::OnStyleChanged().AddUObject(this, &USettingsMenu::HandleStyleChanged);
	}
	if (bFocusOnShow && Settings)
	{
		Panel->FocusFirstRow();
	}
	return Panel.ToSharedRef();
}

TSharedPtr<SWidget> USettingsMenu::MakeCustomRow(UMenuSetting* Setting)
{
	if (!Setting)
	{
		return nullptr;
	}
	const TSoftClassPtr<UMenuSettingRowWidget>* Entry = StyleHolder->Style.RowWidgetClasses.Find(Setting->GetKind());
	UClass* Class = Entry ? Entry->LoadSynchronous() : nullptr;
	if (!Class)
	{
		return nullptr;
	}
	UMenuSettingRowWidget* Row = nullptr;
	if (APlayerController* Player = GetOwningPlayer())
	{
		Row = CreateWidget<UMenuSettingRowWidget>(Player, Class);
	}
	else if (UWorld* World = GetWorld())
	{
		Row = CreateWidget<UMenuSettingRowWidget>(World, Class);
	}
	if (!Row)
	{
		return nullptr;
	}
	Row->InitializeRow(Setting);
	CustomRows.Add(Row);
	return Row->TakeWidget();
}

void USettingsMenu::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
	if (Panel.IsValid() && IsDesignTime())
	{
		Panel->RebuildAll();
	}
}

void USettingsMenu::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	CloseSession(false);
	UMenuStyleAsset::OnStyleChanged().Remove(StyleHandle);
	StyleHandle.Reset();
	Panel.Reset();
	CustomRows.Reset();
}

void USettingsMenu::HandleStyleChanged(const UMenuStyleAsset* Changed)
{
	if (Changed == StyleAsset || !StyleAsset)
	{
		StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
		if (Panel.IsValid())
		{
			Panel->RebuildAll();
			Panel->Invalidate(EInvalidateWidget::LayoutAndVolatility);
		}
	}
}

void USettingsMenu::OpenSession()
{
	if (bSessionOpen)
	{
		return;
	}
	if (UMenuSettingsSubsystem* Settings = FindSettings())
	{
		Settings->RefreshKeyBindings();
		Settings->BeginEditing(ApplyPolicy == EMenuApplyPolicy::ApplyButton);
		SessionSettings = Settings;
		bSessionOpen = true;
	}
}

void USettingsMenu::CloseSession(bool bApplyPending)
{
	if (!bSessionOpen)
	{
		return;
	}
	bSessionOpen = false;
	// The subsystem the session began on (the owning player can be gone by the time the widget is released).
	if (UMenuSettingsSubsystem* Settings = SessionSettings.Get())
	{
		Settings->EndEditing(bApplyPending);
	}
	SessionSettings.Reset();
}

bool USettingsMenu::SetPage(FName PageId)
{
	return Panel.IsValid() && Panel->SetPageById(PageId);
}

void USettingsMenu::NextPage()
{
	if (Panel.IsValid())
	{
		Panel->StepPage(1);
	}
}

void USettingsMenu::PreviousPage()
{
	if (Panel.IsValid())
	{
		Panel->StepPage(-1);
	}
}

FName USettingsMenu::GetCurrentPage() const
{
	return Panel.IsValid() ? Panel->GetCurrentPageId() : NAME_None;
}

void USettingsMenu::ApplyChanges()
{
	if (Panel.IsValid())
	{
		Panel->ApplyChanges();
	}
}

void USettingsMenu::RevertChanges()
{
	if (Panel.IsValid())
	{
		Panel->RevertChanges();
	}
}

void USettingsMenu::ResetCurrentPage()
{
	if (Panel.IsValid())
	{
		Panel->ResetCurrentPage();
	}
}

bool USettingsMenu::RequestBack()
{
	if (Panel.IsValid())
	{
		return Panel->RequestBack();
	}
	OnBack.Broadcast();
	return false;
}

void USettingsMenu::FocusFirstSetting()
{
	if (Panel.IsValid())
	{
		Panel->FocusFirstRow();
	}
}

void USettingsMenu::FocusSetting(FName SettingId)
{
	if (Panel.IsValid())
	{
		Panel->FocusSetting(SettingId);
	}
}

void USettingsMenu::SetStyleAsset(UMenuStyleAsset* InStyleAsset)
{
	StyleAsset = InStyleAsset;
	HandleStyleChanged(InStyleAsset);
}

void USettingsMenu::SetSearchText(FText InText)
{
	if (Panel.IsValid())
	{
		Panel->SetSearchText(InText);
	}
}

void USettingsMenu::RefreshSettingsMenu()
{
	if (UMenuSettingsSubsystem* Settings = FindSettings())
	{
		Settings->RefreshKeyBindings();
	}
	if (Panel.IsValid())
	{
		Panel->RebuildAll();
	}
}

#if WITH_EDITOR
const FText USettingsMenu::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

// =====================================================================================================
//  Setting Control
// =====================================================================================================

TSharedRef<SWidget> UMenuSettingControl::RebuildWidget()
{
	StyleHolder->Style = UMenuStyleAsset::Resolve(StyleAsset);
	Host = MakeShared<FMenuStandaloneRowHost>();
	Host->StyleHolder = StyleHolder;

	UMenuSetting* Found = nullptr;
	if (!IsDesignTime())
	{
		ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
		UMenuSettingsSubsystem* Settings = LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : UMenuSettingsSubsystem::Get(this);
		Host->Settings = Settings;
		Host->LocalPlayer = LocalPlayer;
		Found = Settings ? Settings->FindSetting(SettingId) : nullptr;
		Host->Bind();
	}
	else
	{
		TArray<FMenuSettingsPage> Preview;
		SettingsMenuLocal::MakePreviewPages(this, PreviewSchema, {}, Preview);
		for (const FMenuSettingsPage& Page : Preview)
		{
			for (const FMenuSettingsSection& Section : Page.Sections)
			{
				for (UMenuSetting* Candidate : Section.Settings)
				{
					if (Candidate && Candidate->Id == SettingId)
					{
						Found = Candidate;
					}
				}
			}
		}
	}

	if (!Found)
	{
		return SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("NoSetting", "No setting \"{0}\""), FText::FromName(SettingId)))
			.ColorAndOpacity(FLinearColor(1.f, 0.6f, 0.3f));
	}
	TSharedRef<SMenuSettingRow> Row = SNew(SMenuSettingRow, Host.Get(), Found);
	Host->Row = Row;
	return Row;
}

void UMenuSettingControl::SetSettingId(FName InSettingId)
{
	if (SettingId != InSettingId)
	{
		SettingId = InSettingId;
		InvalidateLayoutAndVolatility();
		TakeWidget();
	}
}

void UMenuSettingControl::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	Host.Reset();
}

#if WITH_EDITOR
const FText UMenuSettingControl::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "NameTBD");
}
#endif

// =====================================================================================================
//  Custom row
// =====================================================================================================

UMenuSettingsSubsystem* UMenuSettingRowWidget::GetSettings() const
{
	ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UMenuSettingsSubsystem>() : UMenuSettingsSubsystem::Get(this);
}

void UMenuSettingRowWidget::InitializeRow(UMenuSetting* InSetting)
{
	Setting = InSetting;
	if (InSetting)
	{
		SettingId = InSetting->Id;
		if (LabelText)
		{
			LabelText->SetText(InSetting->GetLabel());
		}
		if (DescriptionText)
		{
			DescriptionText->SetText(InSetting->Description);
		}
		OnSettingAssigned(InSetting);
	}
	RefreshRow();
}

void UMenuSettingRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UMenuSettingsSubsystem* Settings = GetSettings();
	if (!Setting && Settings && !SettingId.IsNone())
	{
		InitializeRow(Settings->FindSetting(SettingId));
	}
	if (Settings && !ChangedHandle.IsValid())
	{
		ChangedHandle = Settings->OnSettingChangedNative().AddUObject(this, &UMenuSettingRowWidget::HandleSettingChanged);
	}
	RefreshRow();
}

void UMenuSettingRowWidget::NativeDestruct()
{
	if (UMenuSettingsSubsystem* Settings = GetSettings())
	{
		Settings->OnSettingChangedNative().Remove(ChangedHandle);
	}
	ChangedHandle.Reset();
	Super::NativeDestruct();
}

void UMenuSettingRowWidget::HandleSettingChanged(FName ChangedId, const FString& NewValue)
{
	// Any change can change this one (a preset, a condition): refresh.
	RefreshRow();
}

void UMenuSettingRowWidget::RefreshRow()
{
	if (!Setting)
	{
		return;
	}
	const FString Current = GetValue();
	const FText Display = GetDisplayValue();
	FText Reason;
	const bool bEnabled = CanChange(Reason);
	if (ValueText)
	{
		ValueText->SetText(Display);
	}
	OnSettingRefreshed(Current, Display, bEnabled, Reason);
}

FString UMenuSettingRowWidget::GetValue() const
{
	const UMenuSettingsSubsystem* Settings = GetSettings();
	return Settings && Setting ? Settings->GetSettingValue(Setting->Id) : (Setting ? Setting->GetDefaultValue(FMenuSettingContext()) : FString());
}

FText UMenuSettingRowWidget::GetDisplayValue() const
{
	const UMenuSettingsSubsystem* Settings = GetSettings();
	if (Settings && Setting)
	{
		return Settings->GetSettingDisplayValue(Setting->Id);
	}
	return Setting ? Setting->FormatValue(GetValue(), FMenuSettingContext()) : FText::GetEmpty();
}

TArray<FMenuSettingOption> UMenuSettingRowWidget::GetOptions() const
{
	const UMenuSettingsSubsystem* Settings = GetSettings();
	return Settings && Setting ? Settings->GetSettingOptions(Setting->Id) : TArray<FMenuSettingOption>();
}

bool UMenuSettingRowWidget::CanChange(FText& Reason) const
{
	const UMenuSettingsSubsystem* Settings = GetSettings();
	return Settings && Setting && Settings->IsSettingEnabled(Setting->Id, Reason);
}

void UMenuSettingRowWidget::SetValue(const FString& NewValue)
{
	if (UMenuSettingsSubsystem* Settings = GetSettings(); Settings && Setting)
	{
		Settings->SetSettingValue(Setting->Id, NewValue);
	}
}

void UMenuSettingRowWidget::Step(int32 Direction)
{
	if (UMenuSettingsSubsystem* Settings = GetSettings(); Settings && Setting)
	{
		Settings->StepSetting(Setting->Id, Direction);
	}
}

void UMenuSettingRowWidget::ResetToDefault()
{
	if (UMenuSettingsSubsystem* Settings = GetSettings(); Settings && Setting)
	{
		Settings->ResetSetting(Setting->Id);
	}
}

void UMenuSettingRowWidget::RunAction()
{
	if (UMenuSettingsSubsystem* Settings = GetSettings(); Settings && Setting)
	{
		Settings->RunSettingAction(Setting->Id);
	}
}

// =====================================================================================================
//  Screen
// =====================================================================================================

UMenuSettingsScreen::UMenuSettingsScreen(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

void UMenuSettingsScreen::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (SettingsMenu)
	{
		SettingsMenu->OnBack.AddUniqueDynamic(this, &UMenuSettingsScreen::HandleMenuBack);
	}
}

void UMenuSettingsScreen::InitializeScreen_Implementation(UObject* MenuContext)
{
}

void UMenuSettingsScreen::OnScreenOpened_Implementation(UObject* MenuContext)
{
	if (SettingsMenu)
	{
		SettingsMenu->OpenSession();
		SettingsMenu->FocusFirstSetting();
	}
}

void UMenuSettingsScreen::OnScreenClosed_Implementation()
{
	if (SettingsMenu)
	{
		SettingsMenu->CloseSession(false);
	}
}

bool UMenuSettingsScreen::OnBackRequested_Implementation()
{
	// The menu decides: a dialog or unapplied changes keep the screen; otherwise its On Back closes it (HandleMenuBack).
	if (SettingsMenu)
	{
		SettingsMenu->RequestBack();
		return true;
	}
	return false;
}

FReply UMenuSettingsScreen::NativeOnFocusReceived(const FGeometry& InGeometry, const FFocusEvent& InFocusEvent)
{
	if (SettingsMenu)
	{
		SettingsMenu->FocusFirstSetting();
		return FReply::Handled();
	}
	return Super::NativeOnFocusReceived(InGeometry, InFocusEvent);
}

void UMenuSettingsScreen::HandleMenuBack()
{
	if (UMenuManagerComponent* Manager = UMenuManagerComponent::FindForPlayer(GetOwningPlayer()))
	{
		if (Manager->CloseScreenWidget(this))
		{
			return;
		}
	}
	// Not a Menu Manager screen: just go.
	if (SettingsMenu)
	{
		SettingsMenu->CloseSession(false);
	}
	RemoveFromParent();
}

#undef LOCTEXT_NAMESPACE
