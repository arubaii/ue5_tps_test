// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "NameTBDAIProviders.h"
#include "NameTBDAITools.h"

/** One thing shown in the chat. */
struct FNameTBDAIChatItem
{
	enum class EKind : uint8
	{
		User,
		Assistant,
		Tool,
		Changes,
		Error,
		Info
	};

	EKind Kind = EKind::Info;
	FString Text;

	/** A tool call: its result (shortened); a change card: one line per change. */
	FString Detail;
	bool bToolFailed = false;

	/** A change card: the request it belongs to, and whether it was applied or discarded. */
	int32 RequestId = INDEX_NONE;
	bool bResolved = false;
	FString Resolution;
};

/**
 * One chat: the conversation with the provider, and the agent loop - send, run the tools the reply calls, send their results,
 * until the model ends its turn.
 *
 * Build mode: everything one request changes is one transaction, opened at the request's start and closed at its end, so the
 * request is one step of undo; it ends on a change card - Apply keeps (and saves), Discard undoes the step and deletes the
 * assets it made. A new request applies a card still open (iterating on what was just made means keeping it).
 * Ask mode: the cheaper Question model with the read tools only; nothing changes.
 */
class FNameTBDAIChatSession : public TSharedFromThis<FNameTBDAIChatSession>
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnChanged);

	FNameTBDAIChatSession();
	~FNameTBDAIChatSession();

	/** Sends a message. bAsk: Ask mode. bAttachSelection: the designer's selection goes with it. */
	void Send(const FString& Text, bool bAsk, bool bAttachSelection);

	/** Stops the request in flight (what it changed stays, behind its change card). */
	void Stop();

	/** A new conversation (the open change card is applied first). */
	void Reset();

	void Apply(int32 RequestId);
	void Discard(int32 RequestId);

	bool IsBusy() const { return bBusy; }
	const TArray<FNameTBDAIChatItem>& GetItems() const { return Items; }

	/** The reply being written, and what the model is doing ("Thinking", "Running add_widgets"). */
	const FString& GetStreamingText() const { return StreamingText; }
	const FString& GetStatus() const { return Status; }

	/** "1.2k in (8k cached) / 640 out - $0.004" for the last request, and the chat so far. */
	FString GetUsageText() const;

	/** Something in the chat changed (items, status, streaming text): redraw. */
	FOnChanged OnChanged;

private:
	struct FRequest
	{
		int32 Id = INDEX_NONE;
		bool bAsk = false;
		FString Model;
		FText TransactionTitle;
		bool bTransactionOpen = false;
		int32 ToolCalls = 0;
		FNameTBDAIChangeSet Changes;
		FNameTBDAIUsage Usage;
	};

	struct FCard
	{
		FText TransactionTitle;
		FNameTBDAIChangeSet Changes;
		bool bResolved = false;
	};

	void SendToModel();
	void HandleTurn(const FNameTBDAITurn& Turn);
	void RunTools(const FNameTBDAITurn& Turn);
	void FinishRequest(const FString& Note = FString());
	void CloseTransaction();
	void ApplyOpenCards();
	void AddItem(FNameTBDAIChatItem Item);
	void Changed();
	double EstimateCost(const FString& Model, const FNameTBDAIUsage& Usage, bool& bOutKnown) const;

	TSharedPtr<INameTBDAIProvider> Provider;
	ENameTBDAIProvider ProviderKind = ENameTBDAIProvider::Anthropic;
	TArray<FNameTBDAIChatItem> Items;
	FString StreamingText;
	FString Status;
	bool bBusy = false;

	FRequest Current;

	/** A reply whose tool calls are about to run (next tick): answered as cancelled if the request is stopped first. */
	TOptional<FNameTBDAITurn> PendingToolTurn;
	int32 NextRequestId = 1;
	TMap<int32, FCard> Cards;

	FNameTBDAIUsage LastUsage;
	FString LastModel;
	double ChatCost = 0.0;
	bool bChatCostKnown = true;
};
