// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "NameTBDAISettings.h"
#include "NameTBDAITools.h"

/**
 * The chat's provider adapters: Anthropic (Messages API), OpenAI (Chat Completions) and any OpenAI-compatible endpoint, and
 * Google Gemini - thin, over Unreal's HTTP module, no SDKs. Each keeps the conversation in its own provider's native form
 * (so what a provider must get back unchanged - Anthropic's thinking blocks with their signatures, Gemini's thought
 * signatures - is sent back byte for byte), streams replies as server-sent events, and turns the reply into text and tool
 * calls the chat runs.
 *
 * Caching: the map and the tool definitions come first in every request and never change within a chat, so they are the
 * cached prefix - Anthropic with an explicit cache breakpoint after the system prompt (and automatic caching for the
 * conversation), OpenAI and Gemini by their automatic prefix caching.
 */

struct FNameTBDAIToolCall
{
	FString Id;
	FString Name;
	TSharedPtr<FJsonObject> Input;

	/** False when the model's tool input wasn't valid JSON (cut off, or malformed): the call isn't run. */
	bool bInputValid = true;
	FString RawInput;
};

struct FNameTBDAIUsage
{
	int64 Input = 0;
	int64 Output = 0;
	int64 CacheRead = 0;
	int64 CacheWrite = 0;

	void Add(const FNameTBDAIUsage& Other)
	{
		Input += Other.Input;
		Output += Other.Output;
		CacheRead += Other.CacheRead;
		CacheWrite += Other.CacheWrite;
	}
};

/** One reply of the model. */
struct FNameTBDAITurn
{
	FString Text;
	TArray<FNameTBDAIToolCall> ToolCalls;
	FString StopReason;
	FNameTBDAIUsage Usage;
	FString Model;

	bool bFailed = false;
	FString Error;

	/** The provider's safety classifier declined (Anthropic "refusal"): the partial reply was dropped. */
	bool bRefused = false;
};

/** What a tool call gave back, for the model. */
struct FNameTBDAIToolOutcome
{
	FString Id;
	FString Name;
	FString Text;
	bool bIsError = false;
	TArray<FNameTBDAIImage> Images;
};

struct FNameTBDAIStreamCallbacks
{
	/** Reply text as it arrives. */
	TFunction<void(const FString& /*Delta*/)> OnText;

	/** "Thinking", "Writing build_ui_material"... */
	TFunction<void(const FString& /*Status*/)> OnStatus;

	/** The whole reply, once it is complete (or failed). Game thread. */
	TFunction<void(const FNameTBDAITurn& /*Turn*/)> OnDone;
};

struct FNameTBDAIRequestOptions
{
	FString Model;
	ENameTBDAIEffort Effort = ENameTBDAIEffort::Default;
	int32 MaxOutputTokens = 32000;
	bool bStream = true;
	bool bRefusalFallbacks = true;
	int32 CollapseToolResultsAfter = 3;
	TArray<const FNameTBDAITool*> Tools;
};

class INameTBDAIProvider
{
public:
	virtual ~INameTBDAIProvider() = default;

	virtual void AddUserText(const FString& Text) = 0;

	/** Every result of one reply's tool calls, in one message. */
	virtual void AddToolResults(const TArray<FNameTBDAIToolOutcome>& Results) = 0;

	/** Sends the conversation. One request at a time. */
	virtual void Send(const FNameTBDAIRequestOptions& Options, FNameTBDAIStreamCallbacks Callbacks) = 0;

	/** Stops the request in flight; OnDone is not called. */
	virtual void Cancel() = 0;

	virtual bool IsBusy() const = 0;

	/** Messages in the conversation (for "new chat" hints). */
	virtual int32 GetMessageCount() const = 0;
};

namespace NameTBDAIProviders
{
	/** An adapter for the provider picked in the settings, with the key and endpoint there. Null, with OutError, if it can't be. */
	TSharedPtr<INameTBDAIProvider> Make(const UNameTBDAISettings& Settings, FString& OutError);
}
