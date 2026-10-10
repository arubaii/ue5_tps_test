// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIProviders.h"

#include "Containers/Ticker.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIJson.h"

using namespace NameTBDAIJson;

namespace NameTBDAIProviders
{
	namespace
	{
		// =============================================================================================================
		// The shared part: one streaming HTTP request at a time, its bytes split into server-sent events.
		// =============================================================================================================

		/** Bytes from the HTTP thread, taken by the game thread. */
		struct FStreamBuffer
		{
			FCriticalSection Lock;
			TArray<uint8> Bytes;
		};

		FString FirstErrorMessage(const FString& Body)
		{
			const TSharedPtr<FJsonObject> Json = ParseObject(Body);
			if (Json.IsValid())
			{
				if (const TSharedPtr<FJsonObject> Error = GetObject(Json, TEXT("error")))
				{
					const FString Message = GetString(Error, TEXT("message"));
					if (!Message.IsEmpty())
					{
						return Message;
					}
				}
				const FString Message = GetString(Json, TEXT("message"));
				if (!Message.IsEmpty())
				{
					return Message;
				}
			}
			// A JSON array of errors (Gemini sometimes), or plain text.
			const TSharedPtr<FJsonValue> Value = ParseValue(Body);
			if (Value.IsValid() && Value->Type == EJson::Array && Value->AsArray().Num() > 0 && Value->AsArray()[0]->Type == EJson::Object)
			{
				return FirstErrorMessage(ToString(Value->AsArray()[0]->AsObject()));
			}
			return Body.Left(400);
		}

		class FProviderBase : public INameTBDAIProvider, public TSharedFromThis<FProviderBase>
		{
		public:
			virtual void Cancel() override
			{
				if (Request.IsValid())
				{
					Request->OnProcessRequestComplete().Unbind();
					Request->CancelRequest();
				}
				StopTicker();
				Request.Reset();
				bBusy = false;
			}

			virtual bool IsBusy() const override { return bBusy; }

			virtual ~FProviderBase() override
			{
				Cancel();
			}

		protected:
			/** Starts the request: streamed (bytes split into events as they arrive) or whole. */
			void StartRequest(const FString& Url, const TMap<FString, FString>& Headers, const FString& Body, bool bInStream, FNameTBDAIStreamCallbacks InCallbacks)
			{
				Cancel();
				Callbacks = MoveTemp(InCallbacks);
				Turn = FNameTBDAITurn();
				RawPending.Reset();
				AllText.Reset();
				bStream = bInStream;
				bBusy = true;
				ResetTurnState();

				Request = FHttpModule::Get().CreateRequest();
				Request->SetURL(Url);
				Request->SetVerb(TEXT("POST"));
				Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
				for (const TPair<FString, FString>& Header : Headers)
				{
					Request->SetHeader(Header.Key, Header.Value);
				}
				Request->SetContentAsString(Body);
				// Long builds at high effort can take minutes; streaming keeps the connection busy meanwhile.
				Request->SetTimeout(900.f);

				Buffer = MakeShared<FStreamBuffer>();
				if (bStream)
				{
					Request->SetHeader(TEXT("Accept"), TEXT("text/event-stream"));
					TSharedPtr<FStreamBuffer> Shared = Buffer;
					Request->SetResponseBodyReceiveStreamDelegateV2(FHttpRequestStreamDelegateV2::CreateLambda([Shared](void* Data, int64& Length)
					{
						// HTTP thread: only copy.
						FScopeLock Lock(&Shared->Lock);
						Shared->Bytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
					}));
					TWeakPtr<FProviderBase> WeakSelf = AsShared();
					Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSelf](float)
					{
						if (const TSharedPtr<FProviderBase> Self = WeakSelf.Pin())
						{
							Self->Drain(false);
							return true;
						}
						return false;
					}), 1.f / 30.f);
				}

				TWeakPtr<FProviderBase> WeakSelf = AsShared();
				Request->OnProcessRequestComplete().BindLambda([WeakSelf](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
				{
					if (const TSharedPtr<FProviderBase> Self = WeakSelf.Pin())
					{
						Self->Complete(Response, bConnected);
					}
				});
				Request->ProcessRequest();
			}

			/** Splits what has arrived into events ("data: ..." blocks ending in a blank line) and hands each on. */
			void Drain(bool bFinal)
			{
				if (!Buffer.IsValid())
				{
					return;
				}
				{
					FScopeLock Lock(&Buffer->Lock);
					RawPending.Append(Buffer->Bytes);
					Buffer->Bytes.Reset();
				}
				while (true)
				{
					int32 End = INDEX_NONE;
					int32 SeparatorLength = 0;
					for (int32 Index = 0; Index + 1 < RawPending.Num(); ++Index)
					{
						if (RawPending[Index] == '\n' && RawPending[Index + 1] == '\n')
						{
							End = Index;
							SeparatorLength = 2;
							break;
						}
						if (Index + 3 < RawPending.Num() && RawPending[Index] == '\r' && RawPending[Index + 1] == '\n' && RawPending[Index + 2] == '\r' && RawPending[Index + 3] == '\n')
						{
							End = Index;
							SeparatorLength = 4;
							break;
						}
					}
					if (End == INDEX_NONE)
					{
						if (bFinal && RawPending.Num() > 0)
						{
							End = RawPending.Num();
							SeparatorLength = 0;
						}
						else
						{
							break;
						}
					}
					const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(RawPending.GetData()), End);
					const FString EventText(Converter.Length(), Converter.Get());
					RawPending.RemoveAt(0, FMath::Min(RawPending.Num(), End + SeparatorLength));
					if (AllText.Len() < 65536)
					{
						AllText += EventText + TEXT("\n\n");
					}
					ParseEvent(EventText);
				}
			}

			void ParseEvent(const FString& EventText)
			{
				FString EventName;
				FString Data;
				TArray<FString> Lines;
				EventText.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Lines, TEXT("\n"), false);
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("event:")))
					{
						EventName = Line.Mid(6).TrimStartAndEnd();
					}
					else if (Line.StartsWith(TEXT("data:")))
					{
						Data += (Data.IsEmpty() ? FString() : FString(TEXT("\n"))) + Line.Mid(5).TrimStart();
					}
				}
				if (!Data.IsEmpty())
				{
					OnSseEvent(EventName, Data);
				}
			}

			void Complete(FHttpResponsePtr Response, bool bConnected)
			{
				const int32 Code = Response.IsValid() ? Response->GetResponseCode() : 0;
				FString Body;
				if (bStream)
				{
					Drain(true);
					Body = AllText;
				}
				else if (Response.IsValid())
				{
					Body = Response->GetContentAsString();
				}
				StopTicker();
				Request.Reset();
				bBusy = false;

				if (!bConnected || !Response.IsValid())
				{
					Turn.bFailed = true;
					Turn.Error = FString::Printf(TEXT("Couldn't reach %s - check the network, and the Endpoint in Editor Preferences > Plugins > NameTBD AI."), *GetHostForErrors());
				}
				else if (Code < 200 || Code >= 300)
				{
					Turn.bFailed = true;
					Turn.Error = DescribeHttpError(Code, Body);
				}
				else
				{
					if (!bStream)
					{
						OnFullBody(Body);
					}
					if (!Turn.bFailed)
					{
						FinishTurn();
					}
				}
				if (Callbacks.OnDone)
				{
					// Copied first: OnDone may start the next request, which resets Turn.
					const FNameTBDAITurn Done = Turn;
					Callbacks.OnDone(Done);
				}
			}

			FString DescribeHttpError(int32 Code, const FString& Body) const
			{
				// An error event in the stream ("data: {...error...}") or the body itself.
				FString Message = FirstErrorMessage(Body);
				if (Message.IsEmpty() || Message == Body.Left(400))
				{
					int32 DataAt = Body.Find(TEXT("data:"));
					if (DataAt != INDEX_NONE)
					{
						Message = FirstErrorMessage(Body.Mid(DataAt + 5).TrimStartAndEnd());
					}
				}
				FString Hint;
				switch (Code)
				{
				case 401:
				case 403:
					Hint = TEXT("The API key was refused - check it in Editor Preferences > Plugins > NameTBD AI.");
					break;
				case 404:
					Hint = TEXT("Model or endpoint not found - check the model names and Endpoint in the settings.");
					break;
				case 429:
					Hint = TEXT("Rate limited or out of credit - wait a moment, or check your provider account.");
					break;
				case 529:
				case 503:
				case 500:
				case 502:
					Hint = TEXT("The provider is having trouble - try again shortly.");
					break;
				default:
					if (Message.Contains(TEXT("tool"), ESearchCase::IgnoreCase) || Message.Contains(TEXT("function"), ESearchCase::IgnoreCase))
					{
						Hint = TEXT("This model may not support tool calls - pick one that does.");
					}
					break;
				}
				return FString::Printf(TEXT("HTTP %d: %s%s%s"), Code, *Message.Left(600), Hint.IsEmpty() ? TEXT("") : TEXT("\n"), *Hint);
			}

			void StopTicker()
			{
				if (Ticker.IsValid())
				{
					FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
					Ticker.Reset();
				}
			}

			void EmitText(const FString& Delta)
			{
				Turn.Text += Delta;
				if (Callbacks.OnText && !Delta.IsEmpty())
				{
					Callbacks.OnText(Delta);
				}
			}

			void EmitStatus(const FString& Status)
			{
				if (Callbacks.OnStatus)
				{
					Callbacks.OnStatus(Status);
				}
			}

			/** A tool's input JSON, parsed strictly; invalid input is marked, never run. */
			static void ParseToolInput(FNameTBDAIToolCall& Call, const FString& Raw)
			{
				Call.RawInput = Raw;
				const FString Trimmed = Raw.TrimStartAndEnd();
				Call.Input = Trimmed.IsEmpty() ? MakeShared<FJsonObject>() : ParseObject(Trimmed);
				Call.bInputValid = Call.Input.IsValid();
				if (!Call.Input.IsValid())
				{
					Call.Input = MakeShared<FJsonObject>();
				}
			}

			virtual FString GetHostForErrors() const = 0;
			virtual void ResetTurnState() = 0;
			virtual void OnSseEvent(const FString& EventName, const FString& Data) = 0;
			virtual void OnFullBody(const FString& Body) = 0;

			/** The reply is complete: build the turn's tool calls and add the reply to the conversation. */
			virtual void FinishTurn() = 0;

			FNameTBDAIStreamCallbacks Callbacks;
			FNameTBDAITurn Turn;
			bool bStream = true;
			bool bBusy = false;

		private:
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request;
			TSharedPtr<FStreamBuffer> Buffer;
			TArray<uint8> RawPending;
			FString AllText;
			FTSTicker::FDelegateHandle Ticker;
		};

		TSharedPtr<FJsonObject> TextBlock(const FString& Text)
		{
			TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
			Block->SetStringField(TEXT("type"), TEXT("text"));
			Block->SetStringField(TEXT("text"), Text);
			return Block;
		}

		// =============================================================================================================
		// Anthropic - Messages API
		// =============================================================================================================

		class FAnthropicProvider : public FProviderBase
		{
		public:
			FAnthropicProvider(const FString& InKey, const FString& InEndpoint)
				: ApiKey(InKey)
				, Endpoint(InEndpoint.IsEmpty() ? FString(TEXT("https://api.anthropic.com")) : InEndpoint)
			{
				Endpoint.RemoveFromEnd(TEXT("/"));
				Endpoint.RemoveFromEnd(TEXT("/v1/messages"));
				Endpoint.RemoveFromEnd(TEXT("/v1"));
			}

			virtual void AddUserText(const FString& Text) override
			{
				TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
				Message->SetStringField(TEXT("role"), TEXT("user"));
				Message->SetArrayField(TEXT("content"), ListOf(TextBlock(Text)));
				Messages.Add(MakeShared<FJsonValueObject>(Message));
			}

			virtual void AddToolResults(const TArray<FNameTBDAIToolOutcome>& Results) override
			{
				TArray<TSharedPtr<FJsonValue>> Content;
				for (const FNameTBDAIToolOutcome& Result : Results)
				{
					TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
					Block->SetStringField(TEXT("type"), TEXT("tool_result"));
					Block->SetStringField(TEXT("tool_use_id"), Result.Id);
					TArray<TSharedPtr<FJsonValue>> Inner;
					Inner.Add(MakeShared<FJsonValueObject>(TextBlock(Result.Text.IsEmpty() ? FString(TEXT("{}")) : Result.Text)));
					for (const FNameTBDAIImage& Image : Result.Images)
					{
						TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
						Source->SetStringField(TEXT("type"), TEXT("base64"));
						Source->SetStringField(TEXT("media_type"), Image.MimeType);
						Source->SetStringField(TEXT("data"), Image.Base64);
						TSharedPtr<FJsonObject> ImageBlock = MakeShared<FJsonObject>();
						ImageBlock->SetStringField(TEXT("type"), TEXT("image"));
						ImageBlock->SetObjectField(TEXT("source"), Source);
						Inner.Add(MakeShared<FJsonValueObject>(ImageBlock));
					}
					Block->SetArrayField(TEXT("content"), Inner);
					if (Result.bIsError)
					{
						Block->SetBoolField(TEXT("is_error"), true);
					}
					Content.Add(MakeShared<FJsonValueObject>(Block));
				}
				TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
				Message->SetStringField(TEXT("role"), TEXT("user"));
				Message->SetArrayField(TEXT("content"), Content);
				Messages.Add(MakeShared<FJsonValueObject>(Message));
			}

			virtual int32 GetMessageCount() const override { return Messages.Num(); }

			virtual void Send(const FNameTBDAIRequestOptions& Options, FNameTBDAIStreamCallbacks InCallbacks) override
			{
				const bool bOwnApi = Endpoint.Contains(TEXT("anthropic.com"));
				TArray<FString> Betas;

				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("model"), Options.Model);
				Root->SetNumberField(TEXT("max_tokens"), Options.MaxOutputTokens);
				Root->SetBoolField(TEXT("stream"), Options.bStream);

				// The map, then a cache breakpoint: tools + system are the stable, cached prefix.
				TSharedPtr<FJsonObject> System = TextBlock(NameTBDAIDocs::GetMap());
				TSharedPtr<FJsonObject> CacheControl = MakeShared<FJsonObject>();
				CacheControl->SetStringField(TEXT("type"), TEXT("ephemeral"));
				System->SetObjectField(TEXT("cache_control"), CacheControl);
				Root->SetArrayField(TEXT("system"), ListOf(System));
				// And automatic caching of the conversation so far.
				Root->SetObjectField(TEXT("cache_control"), CacheControl);

				TArray<TSharedPtr<FJsonValue>> Tools;
				for (const FNameTBDAITool* Tool : Options.Tools)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("name"), Tool->Name);
					Entry->SetStringField(TEXT("description"), Tool->Description);
					Entry->SetObjectField(TEXT("input_schema"), Tool->InputSchema);
					if (Options.bStream && bOwnApi)
					{
						// Large inputs (a whole widget tree, a material graph) stream as they are written; parsed strictly here.
						Entry->SetBoolField(TEXT("eager_input_streaming"), true);
					}
					Tools.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (Tools.Num() > 0)
				{
					Root->SetArrayField(TEXT("tools"), Tools);
				}
				Root->SetArrayField(TEXT("messages"), Messages);

				static const TMap<ENameTBDAIEffort, FString> Efforts = {
					{ ENameTBDAIEffort::Low, TEXT("low") }, { ENameTBDAIEffort::Medium, TEXT("medium") },
					{ ENameTBDAIEffort::High, TEXT("high") }, { ENameTBDAIEffort::XHigh, TEXT("xhigh") } };
				if (const FString* Effort = Efforts.Find(Options.Effort))
				{
					TSharedPtr<FJsonObject> OutputConfig = MakeShared<FJsonObject>();
					OutputConfig->SetStringField(TEXT("effort"), *Effort);
					Root->SetObjectField(TEXT("output_config"), OutputConfig);
				}

				if (bOwnApi)
				{
					// Old tool results are cleared by the API itself, so the history stays append-only on our side.
					Betas.Add(TEXT("context-management-2025-06-27"));
					TSharedPtr<FJsonObject> Edit = MakeShared<FJsonObject>();
					Edit->SetStringField(TEXT("type"), TEXT("clear_tool_uses_20250919"));
					TSharedPtr<FJsonObject> Management = MakeShared<FJsonObject>();
					Management->SetArrayField(TEXT("edits"), ListOf(Edit));
					Root->SetObjectField(TEXT("context_management"), Management);

					// A safety-classifier decline is run again on Anthropic's recommended fallback model, in the same call.
					static const TArray<FString> FallbackModels = { TEXT("claude-opus-5-5"), TEXT("claude-opus-5"), TEXT("claude-fable-5-1"), TEXT("claude-fable-5"), TEXT("claude-sonnet-5-5") };
					if (Options.bRefusalFallbacks && FallbackModels.Contains(Options.Model))
					{
						Betas.Add(TEXT("server-side-fallback-2026-07-01"));
						Root->SetStringField(TEXT("fallbacks"), TEXT("default"));
					}
				}

				TMap<FString, FString> Headers;
				Headers.Add(TEXT("x-api-key"), ApiKey);
				Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
				if (Betas.Num() > 0)
				{
					Headers.Add(TEXT("anthropic-beta"), FString::Join(Betas, TEXT(",")));
				}
				StartRequest(Endpoint + TEXT("/v1/messages"), Headers, ToString(Root), Options.bStream, MoveTemp(InCallbacks));
			}

		protected:
			struct FBlock
			{
				FString Type;
				TSharedPtr<FJsonObject> Start;
				FString Text;
				FString Thinking;
				FString Signature;
				FString PartialJson;
			};

			virtual FString GetHostForErrors() const override { return Endpoint; }

			virtual void ResetTurnState() override
			{
				Blocks.Reset();
				Order.Reset();
			}

			void StartBlock(int32 Index, const TSharedPtr<FJsonObject>& ContentBlock)
			{
				FBlock& Block = Blocks.FindOrAdd(Index);
				Block.Start = ContentBlock;
				Block.Type = GetString(ContentBlock, TEXT("type"));
				Order.AddUnique(Index);
				if (Block.Type == TEXT("thinking") || Block.Type == TEXT("redacted_thinking"))
				{
					EmitStatus(TEXT("Thinking"));
					Block.Thinking = GetString(ContentBlock, TEXT("thinking"));
					Block.Signature = GetString(ContentBlock, TEXT("signature"));
				}
				else if (Block.Type == TEXT("tool_use"))
				{
					EmitStatus(FString::Printf(TEXT("Writing %s"), *GetString(ContentBlock, TEXT("name"))));
				}
				else if (Block.Type == TEXT("text"))
				{
					const FString Initial = GetString(ContentBlock, TEXT("text"));
					Block.Text = Initial;
					EmitText(Initial);
				}
			}

			virtual void OnSseEvent(const FString& EventName, const FString& Data) override
			{
				const TSharedPtr<FJsonObject> Event = ParseObject(Data);
				if (!Event.IsValid())
				{
					return;
				}
				const FString Type = GetString(Event, TEXT("type"), EventName);
				if (Type == TEXT("message_start"))
				{
					const TSharedPtr<FJsonObject> Message = GetObject(Event, TEXT("message"));
					Turn.Model = GetString(Message, TEXT("model"));
					ReadUsage(GetObject(Message, TEXT("usage")));
				}
				else if (Type == TEXT("content_block_start"))
				{
					StartBlock(static_cast<int32>(GetNumber(Event, TEXT("index"))), GetObject(Event, TEXT("content_block")));
				}
				else if (Type == TEXT("content_block_delta"))
				{
					FBlock& Block = Blocks.FindOrAdd(static_cast<int32>(GetNumber(Event, TEXT("index"))));
					const TSharedPtr<FJsonObject> Delta = GetObject(Event, TEXT("delta"));
					const FString DeltaType = GetString(Delta, TEXT("type"));
					if (DeltaType == TEXT("text_delta"))
					{
						const FString Text = GetString(Delta, TEXT("text"));
						Block.Text += Text;
						EmitText(Text);
					}
					else if (DeltaType == TEXT("thinking_delta"))
					{
						Block.Thinking += GetString(Delta, TEXT("thinking"));
					}
					else if (DeltaType == TEXT("signature_delta"))
					{
						Block.Signature += GetString(Delta, TEXT("signature"));
					}
					else if (DeltaType == TEXT("input_json_delta"))
					{
						Block.PartialJson += GetString(Delta, TEXT("partial_json"));
					}
				}
				else if (Type == TEXT("message_delta"))
				{
					const FString Stop = GetString(GetObject(Event, TEXT("delta")), TEXT("stop_reason"));
					if (!Stop.IsEmpty())
					{
						Turn.StopReason = Stop;
					}
					const TSharedPtr<FJsonObject> Usage = GetObject(Event, TEXT("usage"));
					if (Usage.IsValid() && Usage->HasField(TEXT("output_tokens")))
					{
						Turn.Usage.Output = static_cast<int64>(GetNumber(Usage, TEXT("output_tokens")));
					}
				}
				else if (Type == TEXT("error"))
				{
					const TSharedPtr<FJsonObject> Error = GetObject(Event, TEXT("error"));
					Turn.bFailed = true;
					Turn.Error = FString::Printf(TEXT("%s: %s"), *GetString(Error, TEXT("type")), *GetString(Error, TEXT("message")));
				}
			}

			void ReadUsage(const TSharedPtr<FJsonObject>& Usage)
			{
				if (!Usage.IsValid())
				{
					return;
				}
				Turn.Usage.Input = static_cast<int64>(GetNumber(Usage, TEXT("input_tokens")));
				Turn.Usage.CacheRead = static_cast<int64>(GetNumber(Usage, TEXT("cache_read_input_tokens")));
				Turn.Usage.CacheWrite = static_cast<int64>(GetNumber(Usage, TEXT("cache_creation_input_tokens")));
				if (Usage->HasField(TEXT("output_tokens")))
				{
					Turn.Usage.Output = static_cast<int64>(GetNumber(Usage, TEXT("output_tokens")));
				}
			}

			virtual void OnFullBody(const FString& Body) override
			{
				const TSharedPtr<FJsonObject> Message = ParseObject(Body);
				if (!Message.IsValid())
				{
					Turn.bFailed = true;
					Turn.Error = TEXT("The reply wasn't JSON.");
					return;
				}
				Turn.Model = GetString(Message, TEXT("model"));
				Turn.StopReason = GetString(Message, TEXT("stop_reason"));
				ReadUsage(GetObject(Message, TEXT("usage")));
				int32 Index = 0;
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Message, TEXT("content")))
				{
					if (Value->Type != EJson::Object)
					{
						continue;
					}
					const TSharedPtr<FJsonObject> Block = Value->AsObject();
					StartBlock(Index, Block);
					FBlock& Stored = Blocks.FindOrAdd(Index);
					if (Stored.Type == TEXT("tool_use"))
					{
						if (const TSharedPtr<FJsonObject> Input = GetObject(Block, TEXT("input")))
						{
							Stored.PartialJson = ToString(Input);
						}
					}
					++Index;
				}
			}

			virtual void FinishTurn() override
			{
				if (Turn.StopReason == TEXT("refusal"))
				{
					// Declined (by the model's safety classifier): the partial reply is dropped, not continued from.
					Turn.bRefused = true;
					return;
				}

				// After a mid-reply fallback, blocks before the last fallback marker that only the declined model can read go.
				int32 LastFallback = INDEX_NONE;
				for (int32 Position = 0; Position < Order.Num(); ++Position)
				{
					if (Blocks[Order[Position]].Type == TEXT("fallback"))
					{
						LastFallback = Position;
					}
				}

				TArray<TSharedPtr<FJsonValue>> Content;
				for (int32 Position = 0; Position < Order.Num(); ++Position)
				{
					const FBlock& Block = Blocks[Order[Position]];
					const bool bBeforeFallback = Position < LastFallback;
					if (Block.Type == TEXT("fallback"))
					{
						continue;
					}
					if (bBeforeFallback && (Block.Type == TEXT("thinking") || Block.Type == TEXT("redacted_thinking") || Block.Type == TEXT("tool_use")))
					{
						continue;
					}
					if (Block.Type == TEXT("text"))
					{
						if (!Block.Text.IsEmpty())
						{
							Content.Add(MakeShared<FJsonValueObject>(TextBlock(Block.Text)));
						}
					}
					else if (Block.Type == TEXT("thinking"))
					{
						// Sent back exactly as it came, signature and all.
						TSharedPtr<FJsonObject> Thinking = MakeShared<FJsonObject>();
						Thinking->SetStringField(TEXT("type"), TEXT("thinking"));
						Thinking->SetStringField(TEXT("thinking"), Block.Thinking);
						Thinking->SetStringField(TEXT("signature"), Block.Signature);
						Content.Add(MakeShared<FJsonValueObject>(Thinking));
					}
					else if (Block.Type == TEXT("tool_use"))
					{
						FNameTBDAIToolCall& Call = Turn.ToolCalls.AddDefaulted_GetRef();
						Call.Id = GetString(Block.Start, TEXT("id"));
						Call.Name = GetString(Block.Start, TEXT("name"));
						ParseToolInput(Call, Block.PartialJson);
						TSharedPtr<FJsonObject> Use = MakeShared<FJsonObject>();
						Use->SetStringField(TEXT("type"), TEXT("tool_use"));
						Use->SetStringField(TEXT("id"), Call.Id);
						Use->SetStringField(TEXT("name"), Call.Name);
						Use->SetObjectField(TEXT("input"), Call.Input);
						Content.Add(MakeShared<FJsonValueObject>(Use));
					}
					else if (Block.Start.IsValid())
					{
						// Anything else (redacted thinking, blocks this adapter doesn't know) goes back as it came.
						Content.Add(MakeShared<FJsonValueObject>(Block.Start));
					}
				}
				if (Content.Num() > 0)
				{
					TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
					Message->SetStringField(TEXT("role"), TEXT("assistant"));
					Message->SetArrayField(TEXT("content"), Content);
					Messages.Add(MakeShared<FJsonValueObject>(Message));
				}
			}

		private:
			FString ApiKey;
			FString Endpoint;
			TArray<TSharedPtr<FJsonValue>> Messages;
			TMap<int32, FBlock> Blocks;
			TArray<int32> Order;
		};

		// =============================================================================================================
		// OpenAI Chat Completions - OpenAI itself and every compatible server (Ollama, LM Studio, vLLM, gateways)
		// =============================================================================================================

		class FOpenAIProvider : public FProviderBase
		{
		public:
			FOpenAIProvider(const FString& InKey, const FString& InEndpoint, bool bInCompatible)
				: ApiKey(InKey)
				, bCompatible(bInCompatible)
			{
				Url = InEndpoint.IsEmpty() ? FString(TEXT("https://api.openai.com/v1")) : InEndpoint;
				Url.RemoveFromEnd(TEXT("/"));
				if (!Url.EndsWith(TEXT("/chat/completions")))
				{
					Url += TEXT("/chat/completions");
				}
			}

			virtual void AddUserText(const FString& Text) override
			{
				TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
				Message->SetStringField(TEXT("role"), TEXT("user"));
				Message->SetStringField(TEXT("content"), Text);
				Messages.Add(Message);
				++UserTurns;
			}

			virtual void AddToolResults(const TArray<FNameTBDAIToolOutcome>& Results) override
			{
				TArray<TSharedPtr<FJsonValue>> Images;
				for (const FNameTBDAIToolOutcome& Result : Results)
				{
					TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
					Message->SetStringField(TEXT("role"), TEXT("tool"));
					Message->SetStringField(TEXT("tool_call_id"), Result.Id);
					Message->SetStringField(TEXT("content"), Result.Text.IsEmpty() ? FString(TEXT("{}")) : Result.Text);
					Messages.Add(Message);
					ToolMessageTurn.Add(Messages.Num() - 1, UserTurns);
					for (const FNameTBDAIImage& Image : Result.Images)
					{
						TSharedPtr<FJsonObject> ImageUrl = MakeShared<FJsonObject>();
						ImageUrl->SetStringField(TEXT("url"), FString::Printf(TEXT("data:%s;base64,%s"), *Image.MimeType, *Image.Base64));
						TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
						Part->SetStringField(TEXT("type"), TEXT("image_url"));
						Part->SetObjectField(TEXT("image_url"), ImageUrl);
						Images.Add(MakeShared<FJsonValueObject>(Part));
					}
				}
				if (Images.Num() > 0)
				{
					// Tool messages carry text only: the picture follows as a user message.
					Images.Insert(MakeShared<FJsonValueObject>(TextBlock(TEXT("The image capture_preview made:"))), 0);
					TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
					Message->SetStringField(TEXT("role"), TEXT("user"));
					Message->SetArrayField(TEXT("content"), Images);
					Messages.Add(Message);
				}
			}

			virtual int32 GetMessageCount() const override { return Messages.Num(); }

			virtual void Send(const FNameTBDAIRequestOptions& Options, FNameTBDAIStreamCallbacks InCallbacks) override
			{
				if (Options.Model.IsEmpty())
				{
					FNameTBDAITurn Failed;
					Failed.bFailed = true;
					Failed.Error = TEXT("No model is set: enter the model's id (Generation Model / Question Model) in Editor Preferences > Plugins > NameTBD AI.");
					if (InCallbacks.OnDone)
					{
						InCallbacks.OnDone(Failed);
					}
					return;
				}

				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				Root->SetStringField(TEXT("model"), Options.Model);
				Root->SetBoolField(TEXT("stream"), Options.bStream);
				Root->SetNumberField(bCompatible ? TEXT("max_tokens") : TEXT("max_completion_tokens"), Options.MaxOutputTokens);
				if (Options.bStream && !bCompatible)
				{
					TSharedPtr<FJsonObject> StreamOptions = MakeShared<FJsonObject>();
					StreamOptions->SetBoolField(TEXT("include_usage"), true);
					Root->SetObjectField(TEXT("stream_options"), StreamOptions);
				}

				// The map first, unchanging: the automatic prefix cache's part.
				TArray<TSharedPtr<FJsonValue>> Out;
				TSharedPtr<FJsonObject> System = MakeShared<FJsonObject>();
				System->SetStringField(TEXT("role"), TEXT("system"));
				System->SetStringField(TEXT("content"), NameTBDAIDocs::GetMap());
				Out.Add(MakeShared<FJsonValueObject>(System));
				for (int32 Index = 0; Index < Messages.Num(); ++Index)
				{
					const int32* ResultTurn = ToolMessageTurn.Find(Index);
					if (ResultTurn && UserTurns - *ResultTurn >= Options.CollapseToolResultsAfter)
					{
						// An old tool result, sent short.
						TSharedPtr<FJsonObject> Short = MakeShared<FJsonObject>();
						Short->Values = Messages[Index]->Values;
						const FString Full = GetString(Messages[Index], TEXT("content"));
						Short->SetStringField(TEXT("content"), Full.Len() > 160 ? Full.Left(160) + TEXT("... (older result, shortened)") : Full);
						Out.Add(MakeShared<FJsonValueObject>(Short));
						continue;
					}
					Out.Add(MakeShared<FJsonValueObject>(Messages[Index]));
				}
				Root->SetArrayField(TEXT("messages"), Out);

				TArray<TSharedPtr<FJsonValue>> Tools;
				for (const FNameTBDAITool* Tool : Options.Tools)
				{
					TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
					Function->SetStringField(TEXT("name"), Tool->Name);
					Function->SetStringField(TEXT("description"), Tool->Description);
					Function->SetObjectField(TEXT("parameters"), Tool->InputSchema);
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("type"), TEXT("function"));
					Entry->SetObjectField(TEXT("function"), Function);
					Tools.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (Tools.Num() > 0)
				{
					Root->SetArrayField(TEXT("tools"), Tools);
				}

				TMap<FString, FString> Headers;
				if (!ApiKey.IsEmpty())
				{
					Headers.Add(TEXT("Authorization"), TEXT("Bearer ") + ApiKey);
				}
				StartRequest(Url, Headers, ToString(Root), Options.bStream, MoveTemp(InCallbacks));
			}

		protected:
			struct FPendingCall
			{
				FString Id;
				FString Name;
				FString Arguments;
			};

			virtual FString GetHostForErrors() const override { return Url; }

			virtual void ResetTurnState() override
			{
				Calls.Reset();
			}

			void ReadUsage(const TSharedPtr<FJsonObject>& Usage)
			{
				if (!Usage.IsValid())
				{
					return;
				}
				const int64 Prompt = static_cast<int64>(GetNumber(Usage, TEXT("prompt_tokens")));
				const int64 Cached = static_cast<int64>(GetNumber(GetObject(Usage, TEXT("prompt_tokens_details")), TEXT("cached_tokens")));
				Turn.Usage.CacheRead = Cached;
				Turn.Usage.Input = FMath::Max<int64>(0, Prompt - Cached);
				Turn.Usage.Output = static_cast<int64>(GetNumber(Usage, TEXT("completion_tokens")));
			}

			virtual void OnSseEvent(const FString& EventName, const FString& Data) override
			{
				if (Data == TEXT("[DONE]"))
				{
					return;
				}
				const TSharedPtr<FJsonObject> Chunk = ParseObject(Data);
				if (!Chunk.IsValid())
				{
					return;
				}
				if (const TSharedPtr<FJsonObject> Error = GetObject(Chunk, TEXT("error")))
				{
					Turn.bFailed = true;
					Turn.Error = GetString(Error, TEXT("message"), Data.Left(400));
					return;
				}
				if (Turn.Model.IsEmpty())
				{
					Turn.Model = GetString(Chunk, TEXT("model"));
				}
				ReadUsage(GetObject(Chunk, TEXT("usage")));
				const TArray<TSharedPtr<FJsonValue>> Choices = GetArray(Chunk, TEXT("choices"));
				if (Choices.Num() == 0 || Choices[0]->Type != EJson::Object)
				{
					return;
				}
				const TSharedPtr<FJsonObject> Choice = Choices[0]->AsObject();
				const FString Finish = GetString(Choice, TEXT("finish_reason"));
				if (!Finish.IsEmpty())
				{
					Turn.StopReason = Finish;
				}
				const TSharedPtr<FJsonObject> Delta = GetObject(Choice, TEXT("delta"));
				FString Content;
				if (Delta.IsValid() && Delta->TryGetStringField(TEXT("content"), Content))
				{
					EmitText(Content);
				}
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Delta, TEXT("tool_calls")))
				{
					const TSharedPtr<FJsonObject> Call = Value->AsObject();
					const int32 Index = static_cast<int32>(GetNumber(Call, TEXT("index"), Calls.Num()));
					FPendingCall& Pending = Calls.FindOrAdd(Index);
					const FString Id = GetString(Call, TEXT("id"));
					if (!Id.IsEmpty())
					{
						Pending.Id = Id;
					}
					const TSharedPtr<FJsonObject> Function = GetObject(Call, TEXT("function"));
					const FString Name = GetString(Function, TEXT("name"));
					if (!Name.IsEmpty())
					{
						Pending.Name = Name;
						EmitStatus(FString::Printf(TEXT("Writing %s"), *Name));
					}
					Pending.Arguments += GetString(Function, TEXT("arguments"));
				}
			}

			virtual void OnFullBody(const FString& Body) override
			{
				const TSharedPtr<FJsonObject> Reply = ParseObject(Body);
				if (!Reply.IsValid())
				{
					Turn.bFailed = true;
					Turn.Error = TEXT("The reply wasn't JSON.");
					return;
				}
				Turn.Model = GetString(Reply, TEXT("model"));
				ReadUsage(GetObject(Reply, TEXT("usage")));
				const TArray<TSharedPtr<FJsonValue>> Choices = GetArray(Reply, TEXT("choices"));
				if (Choices.Num() == 0)
				{
					return;
				}
				const TSharedPtr<FJsonObject> Choice = Choices[0]->AsObject();
				Turn.StopReason = GetString(Choice, TEXT("finish_reason"));
				const TSharedPtr<FJsonObject> Message = GetObject(Choice, TEXT("message"));
				EmitText(GetString(Message, TEXT("content")));
				int32 Index = 0;
				for (const TSharedPtr<FJsonValue>& Value : GetArray(Message, TEXT("tool_calls")))
				{
					const TSharedPtr<FJsonObject> Call = Value->AsObject();
					FPendingCall& Pending = Calls.FindOrAdd(Index++);
					Pending.Id = GetString(Call, TEXT("id"));
					Pending.Name = GetString(GetObject(Call, TEXT("function")), TEXT("name"));
					Pending.Arguments = GetString(GetObject(Call, TEXT("function")), TEXT("arguments"));
				}
			}

			virtual void FinishTurn() override
			{
				TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
				Message->SetStringField(TEXT("role"), TEXT("assistant"));
				TArray<int32> Indices;
				Calls.GetKeys(Indices);
				Indices.Sort();
				TArray<TSharedPtr<FJsonValue>> ToolCalls;
				for (const int32 Index : Indices)
				{
					const FPendingCall& Pending = Calls[Index];
					FNameTBDAIToolCall& Call = Turn.ToolCalls.AddDefaulted_GetRef();
					Call.Id = Pending.Id.IsEmpty() ? FString::Printf(TEXT("call_%d_%d"), Messages.Num(), Index) : Pending.Id;
					Call.Name = Pending.Name;
					ParseToolInput(Call, Pending.Arguments);

					TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
					Function->SetStringField(TEXT("name"), Call.Name);
					Function->SetStringField(TEXT("arguments"), Pending.Arguments.IsEmpty() ? FString(TEXT("{}")) : Pending.Arguments);
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("id"), Call.Id);
					Entry->SetStringField(TEXT("type"), TEXT("function"));
					Entry->SetObjectField(TEXT("function"), Function);
					ToolCalls.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (Turn.Text.IsEmpty() && ToolCalls.Num() > 0)
				{
					Message->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
				}
				else
				{
					Message->SetStringField(TEXT("content"), Turn.Text);
				}
				if (ToolCalls.Num() > 0)
				{
					Message->SetArrayField(TEXT("tool_calls"), ToolCalls);
				}
				Messages.Add(Message);
			}

		private:
			FString ApiKey;
			FString Url;
			bool bCompatible = false;
			TArray<TSharedPtr<FJsonObject>> Messages;
			/** Message index of each tool result -> the user turn it belongs to, to send old ones short. */
			TMap<int32, int32> ToolMessageTurn;
			int32 UserTurns = 0;
			TMap<int32, FPendingCall> Calls;
		};

		// =============================================================================================================
		// Google Gemini - generateContent
		// =============================================================================================================

		/** Gemini takes a subset of JSON Schema, upper-case types, and no free-form objects: those become JSON text. */
		TSharedPtr<FJsonObject> ToGeminiSchema(const TSharedPtr<FJsonObject>& Schema)
		{
			TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
			if (!Schema.IsValid())
			{
				Out->SetStringField(TEXT("type"), TEXT("STRING"));
				return Out;
			}
			FString Type = GetString(Schema, TEXT("type"), TEXT("string"));
			FString Description = GetString(Schema, TEXT("description"));
			const TSharedPtr<FJsonObject> Properties = GetObject(Schema, TEXT("properties"));
			if (Type == TEXT("object") && (!Properties.IsValid() || Properties->Values.Num() == 0))
			{
				Type = TEXT("string");
				Description += TEXT(" (a JSON object, written as text)");
			}
			Out->SetStringField(TEXT("type"), Type.ToUpper());
			if (!Description.IsEmpty())
			{
				Out->SetStringField(TEXT("description"), Description);
			}
			if (const TSharedPtr<FJsonValue> Enum = Schema->TryGetField(TEXT("enum")))
			{
				Out->SetField(TEXT("enum"), Enum);
			}
			if (Type == TEXT("object"))
			{
				TSharedPtr<FJsonObject> OutProperties = MakeShared<FJsonObject>();
				for (const auto& Pair : Properties->Values)
				{
					OutProperties->SetObjectField(Pair.Key, ToGeminiSchema(Pair.Value->AsObject()));
				}
				Out->SetObjectField(TEXT("properties"), OutProperties);
				if (const TSharedPtr<FJsonValue> Required = Schema->TryGetField(TEXT("required")))
				{
					Out->SetField(TEXT("required"), Required);
				}
			}
			if (Type == TEXT("array"))
			{
				Out->SetObjectField(TEXT("items"), ToGeminiSchema(GetObject(Schema, TEXT("items"))));
			}
			return Out;
		}

		class FGeminiProvider : public FProviderBase
		{
		public:
			FGeminiProvider(const FString& InKey, const FString& InEndpoint)
				: ApiKey(InKey)
				, Base(InEndpoint.IsEmpty() ? FString(TEXT("https://generativelanguage.googleapis.com/v1beta")) : InEndpoint)
			{
				Base.RemoveFromEnd(TEXT("/"));
			}

			virtual void AddUserText(const FString& Text) override
			{
				TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
				Part->SetStringField(TEXT("text"), Text);
				AddContent(TEXT("user"), ListOf(Part));
				++UserTurns;
			}

			virtual void AddToolResults(const TArray<FNameTBDAIToolOutcome>& Results) override
			{
				TArray<TSharedPtr<FJsonValue>> ResultParts;
				for (const FNameTBDAIToolOutcome& Result : Results)
				{
					TSharedPtr<FJsonObject> Response = ParseObject(Result.Text);
					if (!Response.IsValid())
					{
						Response = MakeShared<FJsonObject>();
						Response->SetStringField(TEXT("result"), Result.Text);
					}
					TSharedPtr<FJsonObject> FunctionResponse = MakeShared<FJsonObject>();
					FunctionResponse->SetStringField(TEXT("name"), Result.Name);
					if (!Result.Id.StartsWith(TEXT("gemini_")))
					{
						FunctionResponse->SetStringField(TEXT("id"), Result.Id);
					}
					FunctionResponse->SetObjectField(TEXT("response"), Response);
					TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
					Part->SetObjectField(TEXT("functionResponse"), FunctionResponse);
					ResultParts.Add(MakeShared<FJsonValueObject>(Part));
					for (const FNameTBDAIImage& Image : Result.Images)
					{
						TSharedPtr<FJsonObject> Inline = MakeShared<FJsonObject>();
						Inline->SetStringField(TEXT("mimeType"), Image.MimeType);
						Inline->SetStringField(TEXT("data"), Image.Base64);
						TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
						ImagePart->SetObjectField(TEXT("inlineData"), Inline);
						ResultParts.Add(MakeShared<FJsonValueObject>(ImagePart));
					}
				}
				AddContent(TEXT("user"), ResultParts);
				ToolContentTurn.Add(Contents.Num() - 1, UserTurns);
			}

			virtual int32 GetMessageCount() const override { return Contents.Num(); }

			virtual void Send(const FNameTBDAIRequestOptions& Options, FNameTBDAIStreamCallbacks InCallbacks) override
			{
				TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
				TSharedPtr<FJsonObject> SystemPart = MakeShared<FJsonObject>();
				SystemPart->SetStringField(TEXT("text"), NameTBDAIDocs::GetMap());
				TSharedPtr<FJsonObject> System = MakeShared<FJsonObject>();
				System->SetArrayField(TEXT("parts"), ListOf(SystemPart));
				Root->SetObjectField(TEXT("systemInstruction"), System);

				TArray<TSharedPtr<FJsonValue>> Out;
				for (int32 Index = 0; Index < Contents.Num(); ++Index)
				{
					const int32* ResultTurn = ToolContentTurn.Find(Index);
					if (ResultTurn && UserTurns - *ResultTurn >= Options.CollapseToolResultsAfter)
					{
						// Old tool results, sent short.
						TArray<TSharedPtr<FJsonValue>> ShortParts;
						for (const TSharedPtr<FJsonValue>& Part : GetArray(Contents[Index], TEXT("parts")))
						{
							const TSharedPtr<FJsonObject> FunctionResponse = GetObject(Part->AsObject(), TEXT("functionResponse"));
							if (!FunctionResponse.IsValid())
							{
								continue; // images of old captures go
							}
							TSharedPtr<FJsonObject> ShortResponse = MakeShared<FJsonObject>();
							ShortResponse->Values = FunctionResponse->Values;
							const FString Full = ToString(GetObject(FunctionResponse, TEXT("response")));
							TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
							Summary->SetStringField(TEXT("result"), Full.Len() > 160 ? Full.Left(160) + TEXT("... (older result, shortened)") : Full);
							ShortResponse->SetObjectField(TEXT("response"), Summary);
							TSharedPtr<FJsonObject> ShortPart = MakeShared<FJsonObject>();
							ShortPart->SetObjectField(TEXT("functionResponse"), ShortResponse);
							ShortParts.Add(MakeShared<FJsonValueObject>(ShortPart));
						}
						TSharedPtr<FJsonObject> Short = MakeShared<FJsonObject>();
						Short->SetStringField(TEXT("role"), TEXT("user"));
						Short->SetArrayField(TEXT("parts"), ShortParts);
						Out.Add(MakeShared<FJsonValueObject>(Short));
						continue;
					}
					Out.Add(MakeShared<FJsonValueObject>(Contents[Index]));
				}
				Root->SetArrayField(TEXT("contents"), Out);

				TArray<TSharedPtr<FJsonValue>> Declarations;
				for (const FNameTBDAITool* Tool : Options.Tools)
				{
					TSharedPtr<FJsonObject> Declaration = MakeShared<FJsonObject>();
					Declaration->SetStringField(TEXT("name"), Tool->Name);
					Declaration->SetStringField(TEXT("description"), Tool->Description);
					Declaration->SetObjectField(TEXT("parameters"), ToGeminiSchema(Tool->InputSchema));
					Declarations.Add(MakeShared<FJsonValueObject>(Declaration));
				}
				if (Declarations.Num() > 0)
				{
					TSharedPtr<FJsonObject> ToolsEntry = MakeShared<FJsonObject>();
					ToolsEntry->SetArrayField(TEXT("functionDeclarations"), Declarations);
					Root->SetArrayField(TEXT("tools"), ListOf(ToolsEntry));
				}
				TSharedPtr<FJsonObject> Generation = MakeShared<FJsonObject>();
				Generation->SetNumberField(TEXT("maxOutputTokens"), Options.MaxOutputTokens);
				Root->SetObjectField(TEXT("generationConfig"), Generation);

				const FString Url = Options.bStream
					? FString::Printf(TEXT("%s/models/%s:streamGenerateContent?alt=sse"), *Base, *Options.Model)
					: FString::Printf(TEXT("%s/models/%s:generateContent"), *Base, *Options.Model);
				TMap<FString, FString> Headers;
				Headers.Add(TEXT("x-goog-api-key"), ApiKey);
				StartRequest(Url, Headers, ToString(Root), Options.bStream, MoveTemp(InCallbacks));
			}

		protected:
			virtual FString GetHostForErrors() const override { return Base; }

			virtual void ResetTurnState() override
			{
				Parts.Reset();
			}

			void AddContent(const FString& Role, const TArray<TSharedPtr<FJsonValue>>& InParts)
			{
				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("role"), Role);
				Content->SetArrayField(TEXT("parts"), InParts);
				Contents.Add(Content);
			}

			void ReadResponse(const TSharedPtr<FJsonObject>& Response)
			{
				if (const TSharedPtr<FJsonObject> Usage = GetObject(Response, TEXT("usageMetadata")))
				{
					const int64 Prompt = static_cast<int64>(GetNumber(Usage, TEXT("promptTokenCount")));
					const int64 Cached = static_cast<int64>(GetNumber(Usage, TEXT("cachedContentTokenCount")));
					Turn.Usage.CacheRead = Cached;
					Turn.Usage.Input = FMath::Max<int64>(0, Prompt - Cached);
					Turn.Usage.Output = static_cast<int64>(GetNumber(Usage, TEXT("candidatesTokenCount")) + GetNumber(Usage, TEXT("thoughtsTokenCount")));
				}
				if (Turn.Model.IsEmpty())
				{
					Turn.Model = GetString(Response, TEXT("modelVersion"));
				}
				const TArray<TSharedPtr<FJsonValue>> Candidates = GetArray(Response, TEXT("candidates"));
				if (Candidates.Num() == 0)
				{
					return;
				}
				const TSharedPtr<FJsonObject> Candidate = Candidates[0]->AsObject();
				const FString Finish = GetString(Candidate, TEXT("finishReason"));
				if (!Finish.IsEmpty())
				{
					Turn.StopReason = Finish;
				}
				for (const TSharedPtr<FJsonValue>& Value : GetArray(GetObject(Candidate, TEXT("content")), TEXT("parts")))
				{
					const TSharedPtr<FJsonObject> Part = Value->AsObject();
					if (!Part.IsValid())
					{
						continue;
					}
					// Every part is kept as it came - thought signatures must go back unchanged.
					Parts.Add(MakeShared<FJsonValueObject>(Part));
					if (GetBool(Part, TEXT("thought")))
					{
						EmitStatus(TEXT("Thinking"));
						continue;
					}
					FString Text;
					if (Part->TryGetStringField(TEXT("text"), Text))
					{
						EmitText(Text);
					}
					if (const TSharedPtr<FJsonObject> Call = GetObject(Part, TEXT("functionCall")))
					{
						EmitStatus(FString::Printf(TEXT("Writing %s"), *GetString(Call, TEXT("name"))));
					}
				}
			}

			virtual void OnSseEvent(const FString& EventName, const FString& Data) override
			{
				const TSharedPtr<FJsonObject> Response = ParseObject(Data);
				if (!Response.IsValid())
				{
					return;
				}
				if (const TSharedPtr<FJsonObject> Error = GetObject(Response, TEXT("error")))
				{
					Turn.bFailed = true;
					Turn.Error = GetString(Error, TEXT("message"), Data.Left(400));
					return;
				}
				ReadResponse(Response);
			}

			virtual void OnFullBody(const FString& Body) override
			{
				const TSharedPtr<FJsonObject> Response = ParseObject(Body);
				if (!Response.IsValid())
				{
					Turn.bFailed = true;
					Turn.Error = TEXT("The reply wasn't JSON.");
					return;
				}
				ReadResponse(Response);
			}

			virtual void FinishTurn() override
			{
				int32 CallIndex = 0;
				for (const TSharedPtr<FJsonValue>& Value : Parts)
				{
					const TSharedPtr<FJsonObject> Call = GetObject(Value->AsObject(), TEXT("functionCall"));
					if (!Call.IsValid())
					{
						continue;
					}
					FNameTBDAIToolCall& ToolCall = Turn.ToolCalls.AddDefaulted_GetRef();
					const FString Id = GetString(Call, TEXT("id"));
					ToolCall.Id = Id.IsEmpty() ? FString::Printf(TEXT("gemini_%d_%d"), Contents.Num(), CallIndex) : Id;
					ToolCall.Name = GetString(Call, TEXT("name"));
					const TSharedPtr<FJsonObject> Args = GetObject(Call, TEXT("args"));
					ToolCall.Input = Args.IsValid() ? Args : MakeShared<FJsonObject>();
					ToolCall.RawInput = ToString(ToolCall.Input);
					++CallIndex;
				}
				if (Turn.StopReason == TEXT("SAFETY") || Turn.StopReason == TEXT("PROHIBITED_CONTENT"))
				{
					Turn.bRefused = true;
					return;
				}
				if (Parts.Num() > 0)
				{
					AddContent(TEXT("model"), Parts);
				}
			}

		private:
			FString ApiKey;
			FString Base;
			TArray<TSharedPtr<FJsonObject>> Contents;
			TMap<int32, int32> ToolContentTurn;
			int32 UserTurns = 0;
			TArray<TSharedPtr<FJsonValue>> Parts;
		};
	}

	TSharedPtr<INameTBDAIProvider> Make(const UNameTBDAISettings& Settings, FString& OutError)
	{
		const FString Key = Settings.ApiKey.TrimStartAndEnd();
		const FString Endpoint = Settings.Endpoint.TrimStartAndEnd();
		switch (Settings.Provider)
		{
		case ENameTBDAIProvider::Anthropic:
			if (Key.IsEmpty())
			{
				OutError = TEXT("Add your Anthropic API key in Editor Preferences > Plugins > NameTBD AI (console.anthropic.com > API Keys).");
				return nullptr;
			}
			return MakeShared<FAnthropicProvider>(Key, Endpoint);
		case ENameTBDAIProvider::OpenAI:
			if (Key.IsEmpty())
			{
				OutError = TEXT("Add your OpenAI API key in Editor Preferences > Plugins > NameTBD AI.");
				return nullptr;
			}
			return MakeShared<FOpenAIProvider>(Key, Endpoint, false);
		case ENameTBDAIProvider::Gemini:
			if (Key.IsEmpty())
			{
				OutError = TEXT("Add your Google AI Studio (Gemini) API key in Editor Preferences > Plugins > NameTBD AI.");
				return nullptr;
			}
			return MakeShared<FGeminiProvider>(Key, Endpoint);
		case ENameTBDAIProvider::OpenAICompatible:
			if (Endpoint.IsEmpty())
			{
				OutError = TEXT("Set the Endpoint of your OpenAI-compatible server (e.g. http://localhost:11434/v1 for Ollama) in Editor Preferences > Plugins > NameTBD AI.");
				return nullptr;
			}
			return MakeShared<FOpenAIProvider>(Key, Endpoint, true);
		}
		OutError = TEXT("Pick a provider in Editor Preferences > Plugins > NameTBD AI.");
		return nullptr;
	}
}
