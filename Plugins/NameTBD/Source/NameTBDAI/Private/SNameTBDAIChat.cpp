// Copyright AR GameSystems 2026. All rights reserved.

#include "SNameTBDAIChat.h"

#include "Framework/Application/SlateApplication.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "NameTBDAIChatSession.h"
#include "NameTBDAIModule.h"
#include "NameTBDAISettings.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNameTBDAIChat"

namespace NameTBDAIChatColors
{
	const FLinearColor UserBubble(0.10f, 0.11f, 0.16f, 1.f);
	const FLinearColor AssistantBubble(0.045f, 0.047f, 0.055f, 1.f);
	const FLinearColor Card(0.07f, 0.06f, 0.12f, 1.f);
	const FLinearColor Accent(0.49f, 0.36f, 1.f, 1.f);
	const FLinearColor Muted(0.55f, 0.57f, 0.65f, 1.f);
	const FLinearColor Error(0.95f, 0.42f, 0.42f, 1.f);
	const FLinearColor Text(0.92f, 0.93f, 0.97f, 1.f);
	const FLinearColor Code(0.80f, 0.86f, 0.98f, 1.f);
}

void SNameTBDAIChat::Construct(const FArguments& InArgs)
{
	Session = NameTBDAIModule::GetSession();
	ChangedHandle = Session->OnChanged.AddSP(this, &SNameTBDAIChat::HandleSessionChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 6.f, 6.f, 2.f)
		[
			MakeHeader()
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 0.f)
		[
			SNew(STextBlock)
			.Text(this, &SNameTBDAIChat::GetSetupWarning)
			.ColorAndOpacity(NameTBDAIChatColors::Error)
			.AutoWrapText(true)
			.Visibility_Lambda([this]() { return GetSetupWarning().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			SAssignNew(ScrollBox, SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ItemList, SVerticalBox)
			]
			+ SScrollBox::Slot()
			.Padding(10.f, 4.f)
			[
				// The reply being written.
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Session->GetStreamingText()); })
				.ColorAndOpacity(NameTBDAIChatColors::Text)
				.AutoWrapText(true)
				.Visibility_Lambda([this]() { return Session->GetStreamingText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SScrollBox::Slot()
			.Padding(10.f, 2.f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
				{
					const FString& Status = Session->GetStatus();
					const int32 Dots = static_cast<int32>(FSlateApplication::Get().GetCurrentTime() * 2.0) % 4;
					return FText::FromString(Status + FString::ChrN(Dots, TEXT('.')));
				})
				.ColorAndOpacity(NameTBDAIChatColors::Accent)
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.Visibility_Lambda([this]() { return Session->IsBusy() ? EVisibility::Visible : EVisibility::Collapsed; })
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f)
		[
			MakeFooter()
		]
	];

	RebuildList();
}

SNameTBDAIChat::~SNameTBDAIChat()
{
	if (Session.IsValid())
	{
		Session->OnChanged.Remove(ChangedHandle);
	}
}

TSharedRef<SWidget> SNameTBDAIChat::MakeHeader()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("BuildTip", "Build: the Generation model, with every tool - it makes and changes things (each request one undo step, ending on Apply / Discard)."))
			.IsChecked_Lambda([this]() { return bAskMode ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState) { bAskMode = false; })
			[
				SNew(SBox).Padding(FMargin(10.f, 2.f))
				[
					SNew(STextBlock).Text(LOCTEXT("Build", "Build"))
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2.f, 0.f, 8.f, 0.f)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
			.ToolTipText(LOCTEXT("AskTip", "Ask: the cheaper Question model with read-only tools - answers about the plugin and your widgets; changes nothing."))
			.IsChecked_Lambda([this]() { return bAskMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState) { bAskMode = true; })
			[
				SNew(SBox).Padding(FMargin(10.f, 2.f))
				[
					SNew(STextBlock).Text(LOCTEXT("Ask", "Ask"))
				]
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(this, &SNameTBDAIChat::GetModelText)
			.ColorAndOpacity(NameTBDAIChatColors::Muted)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(4.f, 0.f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("NewChatTip", "New chat: a fresh conversation (cheaper than a long one). Open changes are kept."))
			.IsEnabled_Lambda([this]() { return !Session->IsBusy(); })
			.OnClicked_Lambda([this]() { Session->Reset(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(LOCTEXT("NewChat", "New Chat"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("SettingsTip", "Provider, key, models, MCP server and what is sent: Editor Preferences > Plugins > NameTBD AI."))
			.OnClicked_Lambda([]()
			{
				FModuleManager::LoadModuleChecked<ISettingsModule>(TEXT("Settings")).ShowViewer(TEXT("Editor"), TEXT("Plugins"), TEXT("NameTBDAI"));
				return FReply::Handled();
			})
			[
				SNew(SImage).Image(FAppStyle::Get().GetBrush("Icons.Settings"))
			]
		];
}

TSharedRef<SWidget> SNameTBDAIChat::MakeFooter()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SBox)
				.MinDesiredHeight(54.f)
				.MaxDesiredHeight(200.f)
				[
					SAssignNew(InputBox, SMultiLineEditableTextBox)
					.HintText(LOCTEXT("Hint", "Describe a widget, or ask for a change... (Enter sends, Shift+Enter for a new line)"))
					.AutoWrapText(true)
					.OnKeyDownHandler(this, &SNameTBDAIChat::OnInputKeyDown)
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f, 0.f, 0.f)
			.VAlign(VAlign_Bottom)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
				.OnClicked_Lambda([this]()
				{
					if (Session->IsBusy())
					{
						Session->Stop();
					}
					else
					{
						SendInput();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text_Lambda([this]() { return Session->IsBusy() ? LOCTEXT("Stop", "Stop") : LOCTEXT("Send", "Send"); })
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.f, 4.f, 0.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]() { return bAttachSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bAttachSelection = State == ECheckBoxState::Checked; })
				.ToolTipText(LOCTEXT("AttachTip", "Sends the widgets selected in the designer (their non-default settings) with the next message. Without it the model asks for them when you say 'this'."))
				[
					SNew(STextBlock).Text(LOCTEXT("Attach", "Attach selection"))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return FText::FromString(Session->GetUsageText()); })
				.ColorAndOpacity(NameTBDAIChatColors::Muted)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ToolTipText(LOCTEXT("UsageTip", "Tokens of the last request (cached = read from the provider's prompt cache, about a tenth of the price) and the estimated cost, from Model Prices in the settings."))
			]
		];
}

FText SNameTBDAIChat::GetModelText() const
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	const UEnum* Enum = StaticEnum<ENameTBDAIProvider>();
	return FText::Format(LOCTEXT("ModelText", "{0} - {1}"), Enum->GetDisplayNameTextByValue(static_cast<int64>(Settings.Provider)),
		FText::FromString(bAskMode ? Settings.QuestionModel : Settings.GenerationModel));
}

FText SNameTBDAIChat::GetSetupWarning() const
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	if (Settings.Provider == ENameTBDAIProvider::OpenAICompatible)
	{
		return Settings.Endpoint.IsEmpty() ? LOCTEXT("NoEndpoint", "Set your server's Endpoint in the settings (gear) to start.") : FText::GetEmpty();
	}
	return Settings.ApiKey.IsEmpty() ? LOCTEXT("NoKey", "Add your own API key in the settings (gear) to start. The MCP server works without one.") : FText::GetEmpty();
}

FReply SNameTBDAIChat::OnInputKeyDown(const FGeometry& Geometry, const FKeyEvent& KeyEvent)
{
	if (KeyEvent.GetKey() == EKeys::Enter && !KeyEvent.IsShiftDown())
	{
		if (!Session->IsBusy())
		{
			SendInput();
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SNameTBDAIChat::SendInput()
{
	const FString Text = InputBox->GetText().ToString();
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		return;
	}
	InputBox->SetText(FText::GetEmpty());
	Session->Send(Text, bAskMode, bAttachSelection);
	bAttachSelection = false;
}

void SNameTBDAIChat::HandleSessionChanged()
{
	const TArray<FNameTBDAIChatItem>& Items = Session->GetItems();
	FString Signature;
	for (const FNameTBDAIChatItem& Item : Items)
	{
		if (Item.Kind == FNameTBDAIChatItem::EKind::Changes)
		{
			Signature += Item.bResolved ? TEXT("1") : TEXT("0");
		}
	}
	if (Items.Num() != BuiltCount || Signature != BuiltSignature)
	{
		BuiltSignature = Signature;
		RebuildList();
	}
	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
}

void SNameTBDAIChat::RebuildList()
{
	ItemList->ClearChildren();
	const TArray<FNameTBDAIChatItem>& Items = Session->GetItems();
	BuiltCount = Items.Num();
	if (Items.Num() == 0)
	{
		ItemList->AddSlot().AutoHeight()[MakeWelcome()];
		return;
	}
	for (const FNameTBDAIChatItem& Item : Items)
	{
		ItemList->AddSlot()
			.AutoHeight()
			.Padding(4.f, 3.f)
			[
				MakeItemWidget(Item)
			];
	}
}

TSharedRef<SWidget> SNameTBDAIChat::MakeWelcome()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 12.f, 8.f, 6.f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Welcome", "Describe a widget and it's built in your project - then keep changing it by asking. Every request is one undo step, and ends with Apply or Discard."))
			.AutoWrapText(true)
			.ColorAndOpacity(NameTBDAIChatColors::Text)
		];
	const TArray<FText> Examples = {
		LOCTEXT("Example1", "Create a greeting card that says \"Hello User\" at the top left with a soft aurora glow at the bottom and faint circle particle effects that bubble from the bottom and fade upward."),
		LOCTEXT("Example2", "Add a pill button that says Continue at the bottom right, and make the whole card bounce in."),
		LOCTEXT("Example3", "Make a settings menu with a volume choice, a subtitles switch and a back button."),
		LOCTEXT("Example4", "How do Flow List presets work?"),
	};
	for (const FText& Example : Examples)
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(8.f, 2.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked_Lambda([this, Example]()
				{
					InputBox->SetText(Example);
					FSlateApplication::Get().SetKeyboardFocus(InputBox);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(Example)
					.AutoWrapText(true)
					.ColorAndOpacity(NameTBDAIChatColors::Accent)
				]
			];
	}
	return Box;
}

TSharedRef<SWidget> SNameTBDAIChat::MakeRichText(const FString& Text, const FSlateColor& Color)
{
	// Fenced code blocks in a fixed-width font; the rest as it is.
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT("```"), false);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FString Part = Parts[Index];
		const bool bCode = Index % 2 == 1;
		if (bCode)
		{
			// The language name after the opening fence.
			int32 LineEnd = INDEX_NONE;
			if (Part.FindChar(TEXT('\n'), LineEnd) && LineEnd < 16 && !Part.Left(LineEnd).Contains(TEXT(" ")))
			{
				Part = Part.Mid(LineEnd + 1);
			}
		}
		Part.TrimStartAndEndInline();
		if (Part.IsEmpty())
		{
			continue;
		}
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, 2.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(bCode ? FLinearColor(0.02f, 0.02f, 0.03f, 1.f) : FLinearColor::Transparent)
				.Padding(bCode ? 6.f : 0.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Part))
					.AutoWrapText(true)
					.Font(bCode ? FCoreStyle::GetDefaultFontStyle("Mono", 9) : FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(bCode ? FSlateColor(NameTBDAIChatColors::Code) : Color)
				]
			];
	}
	return Box;
}

TSharedRef<SWidget> SNameTBDAIChat::MakeItemWidget(const FNameTBDAIChatItem& Item)
{
	using EKind = FNameTBDAIChatItem::EKind;
	switch (Item.Kind)
	{
	case EKind::User:
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.12f)
			+ SHorizontalBox::Slot()
			.FillWidth(0.88f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(NameTBDAIChatColors::UserBubble)
				.Padding(8.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeRichText(Item.Text, NameTBDAIChatColors::Text)]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Item.Detail))
						.ColorAndOpacity(NameTBDAIChatColors::Muted)
						.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
						.Visibility(Item.Detail.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				]
			];

	case EKind::Assistant:
	{
		const FString Copyable = Item.Text;
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(NameTBDAIChatColors::AssistantBubble)
			.Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MakeRichText(Item.Text, NameTBDAIChatColors::Text)]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("CopyTip", "Copy this reply."))
					.OnClicked_Lambda([Copyable]() { FPlatformApplicationMisc::ClipboardCopy(*Copyable); return FReply::Handled(); })
					[
						SNew(STextBlock).Text(LOCTEXT("Copy", "Copy")).ColorAndOpacity(NameTBDAIChatColors::Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					]
				]
			];
	}

	case EKind::Tool:
		return SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("%s  %s"), Item.bToolFailed ? TEXT("x") : TEXT(">"), *Item.Text)))
			.ColorAndOpacity(Item.bToolFailed ? NameTBDAIChatColors::Error : NameTBDAIChatColors::Muted)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
			.ToolTipText(FText::FromString(Item.Detail));

	case EKind::Changes:
	{
		const int32 RequestId = Item.RequestId;
		return SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(NameTBDAIChatColors::Card)
			.Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.Text))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(NameTBDAIChatColors::Text)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Item.Detail))
					.AutoWrapText(true)
					.ColorAndOpacity(NameTBDAIChatColors::Muted)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					Item.bResolved
					? StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(FText::FromString(Item.Resolution)).ColorAndOpacity(NameTBDAIChatColors::Accent))
					: StaticCastSharedRef<SWidget>(SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
							.ToolTipText(LOCTEXT("ApplyTip", "Keep the changes (saved, with Save On Apply)."))
							.OnClicked_Lambda([this, RequestId]() { Session->Apply(RequestId); return FReply::Handled(); })
							[
								SNew(STextBlock).Text(LOCTEXT("Apply", "Apply"))
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(6.f, 0.f)
						[
							SNew(SButton)
							.ToolTipText(LOCTEXT("DiscardTip", "Undo everything this request changed (one undo step) and delete the assets it made."))
							.IsEnabled_Lambda([this]() { return !Session->IsBusy(); })
							.OnClicked_Lambda([this, RequestId]() { Session->Discard(RequestId); return FReply::Handled(); })
							[
								SNew(STextBlock).Text(LOCTEXT("Discard", "Discard"))
							]
						])
				]
			];
	}

	case EKind::Error:
		return SNew(STextBlock)
			.Text(FText::FromString(Item.Text))
			.AutoWrapText(true)
			.ColorAndOpacity(NameTBDAIChatColors::Error);

	case EKind::Info:
	default:
		return SNew(STextBlock)
			.Text(FText::FromString(Item.Text))
			.AutoWrapText(true)
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(NameTBDAIChatColors::Muted);
	}
}

#undef LOCTEXT_NAMESPACE
