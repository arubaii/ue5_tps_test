// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIChatSession.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "NameTBDAIJson.h"
#include "NameTBDAISettings.h"
#include "ObjectTools.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "NameTBDAIChat"

namespace
{
	FString Shorten(const FString& Text, int32 Max)
	{
		FString Clean = Text.Replace(TEXT("\n"), TEXT(" "));
		return Clean.Len() > Max ? Clean.Left(Max - 3) + TEXT("...") : Clean;
	}

	FString FormatTokens(int64 Tokens)
	{
		return Tokens >= 1000 ? FString::Printf(TEXT("%.1fk"), Tokens / 1000.0) : FString::Printf(TEXT("%lld"), Tokens);
	}

	/** "add_widgets (4 widgets)", "set_widget Title" - what a call did, in a few words. */
	FString DescribeCall(const FNameTBDAIToolCall& Call)
	{
		const TSharedPtr<FJsonObject>& Args = Call.Input;
		for (const TCHAR* Key : { TEXT("name"), TEXT("widget"), TEXT("template"), TEXT("master"), TEXT("recipe"), TEXT("query"), TEXT("kind"), TEXT("asset"), TEXT("animation"), TEXT("material") })
		{
			const FString Value = NameTBDAIJson::GetString(Args, Key);
			if (!Value.IsEmpty())
			{
				return FString::Printf(TEXT("%s %s"), *Call.Name, *Shorten(Value, 48));
			}
		}
		const int32 Widgets = NameTBDAIJson::GetArray(Args, TEXT("widgets")).Num();
		if (Widgets > 0)
		{
			return FString::Printf(TEXT("%s (%d)"), *Call.Name, Widgets);
		}
		return Call.Name;
	}
}

FNameTBDAIChatSession::FNameTBDAIChatSession()
{
	NameTBDAITools::RegisterAll();
}

FNameTBDAIChatSession::~FNameTBDAIChatSession()
{
	if (Provider.IsValid())
	{
		Provider->Cancel();
	}
	CloseTransaction();
}

void FNameTBDAIChatSession::Changed()
{
	OnChanged.Broadcast();
}

void FNameTBDAIChatSession::AddItem(FNameTBDAIChatItem Item)
{
	Items.Add(MoveTemp(Item));
	Changed();
}

void FNameTBDAIChatSession::Send(const FString& InText, bool bAsk, bool bAttachSelection)
{
	const FString Text = InText.TrimStartAndEnd();
	if (bBusy || Text.IsEmpty())
	{
		return;
	}
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();

	// Iterating on what was just made means keeping it.
	ApplyOpenCards();

	if (Provider.IsValid() && ProviderKind != Settings.Provider)
	{
		Provider->Cancel();
		Provider.Reset();
		AddItem({ FNameTBDAIChatItem::EKind::Info, TEXT("The provider changed in the settings: this continues as a new conversation.") });
	}
	if (!Provider.IsValid())
	{
		FString Error;
		Provider = NameTBDAIProviders::Make(Settings, Error);
		ProviderKind = Settings.Provider;
		if (!Provider.IsValid())
		{
			AddItem({ FNameTBDAIChatItem::EKind::Error, Error });
			return;
		}
	}

	FString Message = Text;
	if (bAttachSelection)
	{
		FNameTBDAIChangeSet Ignored;
		FNameTBDAIToolContext Context;
		Context.Changes = &Ignored;
		Context.bAllowWrites = false;
		const FNameTBDAIToolResult Selection = NameTBDAITools::Execute(TEXT("get_context"), MakeShared<FJsonObject>(), Context);
		Message += TEXT("\n\n[What I have selected in the editor]\n") + Selection.ToText();
	}

	FNameTBDAIChatItem UserItem;
	UserItem.Kind = FNameTBDAIChatItem::EKind::User;
	UserItem.Text = Text;
	if (bAttachSelection)
	{
		UserItem.Detail = TEXT("with the selection attached");
	}
	AddItem(MoveTemp(UserItem));

	Current = FRequest();
	Current.Id = NextRequestId++;
	Current.bAsk = bAsk;
	Current.Model = bAsk ? Settings.QuestionModel : Settings.GenerationModel;
	if (!bAsk && GEditor)
	{
		// The whole request is one transaction - one step of undo, which Discard takes back.
		Current.TransactionTitle = FText::Format(LOCTEXT("RequestTransaction", "NameTBD AI: {0}"), FText::FromString(Shorten(Text, 60)));
		GEditor->BeginTransaction(TEXT("NameTBDAI"), Current.TransactionTitle, nullptr);
		Current.bTransactionOpen = true;
	}

	Provider->AddUserText(Message);
	SendToModel();
}

void FNameTBDAIChatSession::SendToModel()
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	FNameTBDAIRequestOptions Options;
	Options.Model = Current.Model;
	Options.Effort = Current.bAsk ? Settings.QuestionEffort : Settings.GenerationEffort;
	Options.MaxOutputTokens = Settings.MaxOutputTokens;
	Options.bStream = Settings.bStreamReplies;
	Options.bRefusalFallbacks = Settings.bRefusalFallbacks;
	Options.CollapseToolResultsAfter = Settings.CollapseToolResultsAfter;
	// The same tool list, in the same order, every request of the chat: it is part of the cached prefix.
	for (const FNameTBDAITool& Tool : NameTBDAITools::GetTools())
	{
		if (Current.bAsk && (Tool.bWrites || !Tool.bInAskMode))
		{
			continue;
		}
		if (Tool.bOptIn && !Settings.bAllowPreviewCapture)
		{
			continue;
		}
		Options.Tools.Add(&Tool);
	}

	bBusy = true;
	StreamingText.Reset();
	Status = FString::Printf(TEXT("Waiting for %s"), *Current.Model);
	Changed();

	TWeakPtr<FNameTBDAIChatSession> WeakSelf = AsShared();
	FNameTBDAIStreamCallbacks Callbacks;
	Callbacks.OnText = [WeakSelf](const FString& Delta)
	{
		if (const TSharedPtr<FNameTBDAIChatSession> Self = WeakSelf.Pin())
		{
			Self->StreamingText += Delta;
			Self->Status = TEXT("Writing");
			Self->Changed();
		}
	};
	Callbacks.OnStatus = [WeakSelf](const FString& NewStatus)
	{
		if (const TSharedPtr<FNameTBDAIChatSession> Self = WeakSelf.Pin())
		{
			if (Self->Status != NewStatus)
			{
				Self->Status = NewStatus;
				Self->Changed();
			}
		}
	};
	Callbacks.OnDone = [WeakSelf](const FNameTBDAITurn& Turn)
	{
		if (const TSharedPtr<FNameTBDAIChatSession> Self = WeakSelf.Pin())
		{
			Self->HandleTurn(Turn);
		}
	};
	Provider->Send(Options, MoveTemp(Callbacks));
}

void FNameTBDAIChatSession::HandleTurn(const FNameTBDAITurn& Turn)
{
	Current.Usage.Add(Turn.Usage);
	if (!Turn.Model.IsEmpty())
	{
		LastModel = Turn.Model;
	}
	StreamingText.Reset();

	if (Turn.bFailed)
	{
		AddItem({ FNameTBDAIChatItem::EKind::Error, Turn.Error });
		FinishRequest();
		return;
	}
	if (Turn.bRefused)
	{
		AddItem({ FNameTBDAIChatItem::EKind::Error, TEXT("The provider's safety filter declined this request (it can, rarely, on harmless UI work). Rephrase it, or try another model.") });
		FinishRequest();
		return;
	}
	if (!Turn.Text.TrimStartAndEnd().IsEmpty())
	{
		AddItem({ FNameTBDAIChatItem::EKind::Assistant, Turn.Text.TrimStartAndEnd() });
	}
	if (Turn.ToolCalls.Num() > 0)
	{
		Status = FString::Printf(TEXT("Running %s"), *Turn.ToolCalls[0].Name);
		Changed();
		// Next tick, so the panel shows what runs before the editor is busy running it.
		PendingToolTurn = Turn;
		TWeakPtr<FNameTBDAIChatSession> WeakSelf = AsShared();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf](float)
		{
			if (const TSharedPtr<FNameTBDAIChatSession> Self = WeakSelf.Pin())
			{
				if (Self->bBusy && Self->PendingToolTurn.IsSet())
				{
					const FNameTBDAITurn Pending = Self->PendingToolTurn.GetValue();
					Self->PendingToolTurn.Reset();
					Self->RunTools(Pending);
				}
			}
			return false;
		}));
		return;
	}
	if (Turn.StopReason == TEXT("max_tokens") || Turn.StopReason == TEXT("length") || Turn.StopReason == TEXT("MAX_TOKENS"))
	{
		FinishRequest(TEXT("The reply reached the output limit (Max Output Tokens) - say \"continue\" to go on."));
		return;
	}
	FinishRequest();
}

void FNameTBDAIChatSession::RunTools(const FNameTBDAITurn& Turn)
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	TArray<FNameTBDAIToolOutcome> Outcomes;
	bool bLimitReached = false;

	for (const FNameTBDAIToolCall& Call : Turn.ToolCalls)
	{
		FNameTBDAIToolOutcome& Outcome = Outcomes.AddDefaulted_GetRef();
		Outcome.Id = Call.Id;
		Outcome.Name = Call.Name;

		FNameTBDAIChatItem Item;
		Item.Kind = FNameTBDAIChatItem::EKind::Tool;
		Item.Text = DescribeCall(Call);

		if (Current.ToolCalls >= Settings.MaxToolCallsPerRequest)
		{
			bLimitReached = true;
			Outcome.bIsError = true;
			Outcome.Text = TEXT("{\"error\":\"Tool call limit for this request reached - stop here and summarize what was done.\"}");
		}
		else if (!Call.bInputValid)
		{
			Outcome.bIsError = true;
			TSharedPtr<FJsonObject> Invalid = MakeShared<FJsonObject>();
			Invalid->SetStringField(TEXT("INVALID_JSON"), Shorten(Call.RawInput, 2000));
			Invalid->SetStringField(TEXT("error"), TEXT("The tool input wasn't valid JSON (cut off?) - send it again, smaller if it was long."));
			Outcome.Text = NameTBDAIJson::ToString(Invalid);
		}
		else
		{
			++Current.ToolCalls;
			Status = FString::Printf(TEXT("Running %s"), *Call.Name);
			FNameTBDAIToolContext Context;
			Context.Origin = ENameTBDAIOrigin::Chat;
			Context.Changes = &Current.Changes;
			Context.bAllowWrites = !Current.bAsk;
			Context.bAllowCapture = Settings.bAllowPreviewCapture;
			Context.Model = StaticEnum<ENameTBDAIProvider>()->GetNameStringByValue(static_cast<int64>(ProviderKind)) + TEXT(":") + Current.Model;
			const FNameTBDAIToolResult Result = NameTBDAITools::Execute(Call.Name, Call.Input, Context);
			Outcome.Text = Result.ToText();
			Outcome.bIsError = !Result.bOk;
			Outcome.Images = Result.Images;
		}
		Item.bToolFailed = Outcome.bIsError;
		Item.Detail = Shorten(Outcome.Text, 400);
		AddItem(MoveTemp(Item));
	}

	Provider->AddToolResults(Outcomes);
	if (bLimitReached)
	{
		FinishRequest(FString::Printf(TEXT("Stopped after %d tool calls (Max Tool Calls Per Request) - say \"continue\" to go on."), Settings.MaxToolCallsPerRequest));
		return;
	}
	SendToModel();
}

void FNameTBDAIChatSession::Stop()
{
	if (!bBusy)
	{
		return;
	}
	if (Provider.IsValid())
	{
		Provider->Cancel();
		// The reply asked for tools that never ran: every call still gets an answer, or the next request is refused.
		if (PendingToolTurn.IsSet())
		{
			TArray<FNameTBDAIToolOutcome> Cancelled;
			for (const FNameTBDAIToolCall& Call : PendingToolTurn->ToolCalls)
			{
				FNameTBDAIToolOutcome& Outcome = Cancelled.AddDefaulted_GetRef();
				Outcome.Id = Call.Id;
				Outcome.Name = Call.Name;
				Outcome.bIsError = true;
				Outcome.Text = TEXT("{\"error\":\"Stopped by the user before this ran.\"}");
			}
			Provider->AddToolResults(Cancelled);
		}
	}
	PendingToolTurn.Reset();
	if (!StreamingText.IsEmpty())
	{
		AddItem({ FNameTBDAIChatItem::EKind::Assistant, StreamingText + TEXT(" ...") });
		StreamingText.Reset();
	}
	FinishRequest(TEXT("Stopped."));
}

void FNameTBDAIChatSession::CloseTransaction()
{
	if (Current.bTransactionOpen && GEditor)
	{
		GEditor->EndTransaction();
	}
	Current.bTransactionOpen = false;
}

void FNameTBDAIChatSession::FinishRequest(const FString& Note)
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	bBusy = false;
	Status.Reset();
	StreamingText.Reset();
	CloseTransaction();

	LastUsage = Current.Usage;
	if (LastModel.IsEmpty())
	{
		LastModel = Current.Model;
	}
	bool bKnown = true;
	ChatCost += EstimateCost(Current.Model, Current.Usage, bKnown);
	bChatCostKnown &= bKnown;

	if (!Note.IsEmpty())
	{
		AddItem({ FNameTBDAIChatItem::EKind::Info, Note });
	}

	if (!Current.bAsk && !Current.Changes.IsEmpty())
	{
		if (Settings.bConfirmChanges)
		{
			FCard& Card = Cards.Add(Current.Id);
			Card.TransactionTitle = Current.TransactionTitle;
			Card.Changes = Current.Changes;

			FNameTBDAIChatItem Item;
			Item.Kind = FNameTBDAIChatItem::EKind::Changes;
			Item.RequestId = Current.Id;
			Item.Text = FString::Printf(TEXT("%d change%s"), FMath::Max(Current.Changes.Lines.Num(), 1), Current.Changes.Lines.Num() == 1 ? TEXT("") : TEXT("s"));
			Item.Detail = FString::Join(Current.Changes.Lines, TEXT("\n"));
			AddItem(MoveTemp(Item));
		}
		else if (Settings.bSaveOnApply)
		{
			const int32 Saved = Current.Changes.SaveAll();
			if (Saved > 0)
			{
				AddItem({ FNameTBDAIChatItem::EKind::Info, FString::Printf(TEXT("Saved %d asset%s."), Saved, Saved == 1 ? TEXT("") : TEXT("s")) });
			}
		}
	}
	Changed();
}

void FNameTBDAIChatSession::Apply(int32 RequestId)
{
	FCard* Card = Cards.Find(RequestId);
	if (!Card || Card->bResolved)
	{
		return;
	}
	Card->bResolved = true;
	int32 Saved = 0;
	if (UNameTBDAISettings::Get().bSaveOnApply)
	{
		Saved = Card->Changes.SaveAll();
	}
	for (FNameTBDAIChatItem& Item : Items)
	{
		if (Item.Kind == FNameTBDAIChatItem::EKind::Changes && Item.RequestId == RequestId)
		{
			Item.bResolved = true;
			Item.Resolution = Saved > 0 ? FString::Printf(TEXT("Applied - %d saved"), Saved) : FString(TEXT("Applied"));
		}
	}
	Changed();
}

void FNameTBDAIChatSession::Discard(int32 RequestId)
{
	FCard* Card = Cards.Find(RequestId);
	if (!Card || Card->bResolved || bBusy || !GEditor || !GEditor->Trans)
	{
		return;
	}

	// The request's own step must be the last one: anything done after it would be undone in its place.
	const FTransactionContext Top = GEditor->Trans->GetUndoContext(/*bCheckWhetherUndoPossible=*/false);
	const bool bOnTop = Top.Title.EqualTo(Card->TransactionTitle);

	// A request that only made new assets may have recorded no step at all.
	bool bOnlyCreated = true;
	for (const TWeakObjectPtr<UPackage>& Package : Card->Changes.TouchedPackages)
	{
		const bool bCreated = Card->Changes.CreatedAssets.ContainsByPredicate([&Package](const TWeakObjectPtr<UObject>& Asset)
		{
			return Asset.IsValid() && Asset->GetOutermost() == Package.Get();
		});
		bOnlyCreated &= bCreated || !Package.IsValid();
	}

	if (!bOnTop && !bOnlyCreated)
	{
		AddItem({ FNameTBDAIChatItem::EKind::Error, TEXT("Other changes were made after this request, so it can't be discarded on its own - use Ctrl+Z (Edit > Undo History) to step back to it.") });
		return;
	}
	if (bOnTop)
	{
		GEditor->UndoTransaction();
	}

	TArray<UObject*> ToDelete;
	for (const TWeakObjectPtr<UObject>& Asset : Card->Changes.CreatedAssets)
	{
		if (UObject* Object = Asset.Get())
		{
			if (UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditors->CloseAllEditorsForAsset(Object);
			}
			ToDelete.Add(Object);
		}
	}
	const int32 Deleted = ToDelete.Num() > 0 ? ObjectTools::ForceDeleteObjects(ToDelete, /*ShowConfirmation=*/false) : 0;

	Card->bResolved = true;
	for (FNameTBDAIChatItem& Item : Items)
	{
		if (Item.Kind == FNameTBDAIChatItem::EKind::Changes && Item.RequestId == RequestId)
		{
			Item.bResolved = true;
			Item.Resolution = Deleted > 0 ? FString::Printf(TEXT("Discarded - %d new asset%s deleted"), Deleted, Deleted == 1 ? TEXT("") : TEXT("s")) : FString(TEXT("Discarded"));
		}
	}
	Changed();
}

void FNameTBDAIChatSession::ApplyOpenCards()
{
	TArray<int32> Open;
	for (const TPair<int32, FCard>& Pair : Cards)
	{
		if (!Pair.Value.bResolved)
		{
			Open.Add(Pair.Key);
		}
	}
	for (const int32 Id : Open)
	{
		Apply(Id);
	}
}

void FNameTBDAIChatSession::Reset()
{
	Stop();
	ApplyOpenCards();
	if (Provider.IsValid())
	{
		Provider->Cancel();
	}
	Provider.Reset();
	Items.Reset();
	Cards.Reset();
	LastUsage = FNameTBDAIUsage();
	ChatCost = 0.0;
	bChatCostKnown = true;
	Changed();
}

double FNameTBDAIChatSession::EstimateCost(const FString& Model, const FNameTBDAIUsage& Usage, bool& bOutKnown) const
{
	const FNameTBDAIModelPrice* Price = UNameTBDAISettings::Get().FindPrice(Model);
	bOutKnown = Price != nullptr;
	if (!Price)
	{
		return 0.0;
	}
	return (Usage.Input * Price->Input + Usage.Output * Price->Output + Usage.CacheRead * Price->CacheRead + Usage.CacheWrite * Price->CacheWrite) / 1000000.0;
}

FString FNameTBDAIChatSession::GetUsageText() const
{
	if (LastUsage.Input + LastUsage.Output + LastUsage.CacheRead + LastUsage.CacheWrite == 0)
	{
		return FString();
	}
	FString Text = FString::Printf(TEXT("Last request: %s in"), *FormatTokens(LastUsage.Input + LastUsage.CacheRead + LastUsage.CacheWrite));
	if (LastUsage.CacheRead > 0)
	{
		Text += FString::Printf(TEXT(" (%s cached)"), *FormatTokens(LastUsage.CacheRead));
	}
	Text += FString::Printf(TEXT(", %s out"), *FormatTokens(LastUsage.Output));
	bool bKnown = true;
	const double Cost = EstimateCost(Current.Model, LastUsage, bKnown);
	if (bKnown)
	{
		Text += FString::Printf(TEXT(" - ~$%.4f"), Cost);
	}
	if (bChatCostKnown && ChatCost > 0.0)
	{
		Text += FString::Printf(TEXT("  |  this chat ~$%.3f"), ChatCost);
	}
	return Text;
}

#undef LOCTEXT_NAMESPACE
