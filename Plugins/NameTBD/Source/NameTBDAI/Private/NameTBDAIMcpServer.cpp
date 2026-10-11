// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAIMcpServer.h"

#include "HAL/FileManager.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "IPAddress.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NameTBDAIDocs.h"
#include "NameTBDAIJson.h"
#include "NameTBDAISettings.h"
#include "NameTBDAITools.h"

using namespace NameTBDAIJson;

namespace
{
	/** Protocol versions this server speaks, newest first. */
	const TCHAR* SupportedVersions[] = { TEXT("2025-06-18"), TEXT("2025-03-26"), TEXT("2024-11-05") };

	/**
	 * The route's handler, as whichever form this engine's HTTP server takes - a delegate (CreateLambda) or a TFunction -
	 * decided at compile time.
	 */
	template <typename HandlerType, typename LambdaType>
	HandlerType MakeRouteHandler(LambdaType&& Lambda)
	{
		if constexpr (requires { HandlerType::CreateLambda(Lambda); })
		{
			return HandlerType::CreateLambda(Forward<LambdaType>(Lambda));
		}
		else
		{
			return HandlerType(Forward<LambdaType>(Lambda));
		}
	}

	FString FindHeader(const FHttpServerRequest& Request, const FString& Name)
	{
		for (const TPair<FString, TArray<FString>>& Pair : Request.Headers)
		{
			if (Pair.Key.Equals(Name, ESearchCase::IgnoreCase) && Pair.Value.Num() > 0)
			{
				return Pair.Value[0];
			}
		}
		return FString();
	}

	TUniquePtr<FHttpServerResponse> MakeResponse(const FString& Body, EHttpServerResponseCodes Code)
	{
		TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
		Response->Code = Code;
		return Response;
	}

	TSharedPtr<FJsonObject> MakeRpcError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message)
	{
		TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
		Reply->SetField(TEXT("id"), Id.IsValid() ? Id : MakeShared<FJsonValueNull>());
		TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetNumberField(TEXT("code"), Code);
		Error->SetStringField(TEXT("message"), Message);
		Reply->SetObjectField(TEXT("error"), Error);
		return Reply;
	}

	/** Makes sure this port's listener binds to 127.0.0.1 only, whatever the engine's default bind address is. */
	void RequestLoopbackBinding(int32 Port)
	{
		const TCHAR* Section = TEXT("HTTPServer.Listeners");
		TArray<FString> Overrides;
		GConfig->GetArray(Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);
		const FString Ours = FString::Printf(TEXT("(Port=%d,BindAddress=127.0.0.1)"), Port);
		Overrides.RemoveAll([Port](const FString& Entry) { return Entry.Contains(FString::Printf(TEXT("Port=%d,"), Port)) || Entry.Contains(FString::Printf(TEXT("Port=%d)"), Port)); });
		Overrides.Add(Ours);
		GConfig->SetArray(Section, TEXT("ListenerOverrides"), Overrides, GEngineIni);
	}

	/** Prompts/*.md: "# Title", "> description", "Arguments: name - what; name - what", then the text with {{name}}. */
	struct FPromptFile
	{
		FString Name;
		FString Title;
		FString Description;
		TArray<TPair<FString, FString>> Arguments;
		FString Text;
	};

	TArray<FPromptFile> LoadPrompts()
	{
		TArray<FPromptFile> Prompts;
		const FString Folder = NameTBDAIDocs::GetResourcesDir() / TEXT("Prompts");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.md")), true, false);
		Files.Sort();
		for (const FString& File : Files)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *(Folder / File)))
			{
				continue;
			}
			FPromptFile Prompt;
			Prompt.Name = FPaths::GetBaseFilename(File);
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			for (const FString& Line : Lines)
			{
				if (Line.StartsWith(TEXT("# ")) && Prompt.Title.IsEmpty())
				{
					Prompt.Title = Line.Mid(2).TrimStartAndEnd();
				}
				else if (Line.StartsWith(TEXT("> ")) && Prompt.Description.IsEmpty())
				{
					Prompt.Description = Line.Mid(2).TrimStartAndEnd();
				}
				else if (Line.StartsWith(TEXT("Arguments:")) && Prompt.Arguments.Num() == 0)
				{
					TArray<FString> Parts;
					Line.Mid(10).ParseIntoArray(Parts, TEXT(";"));
					for (const FString& Part : Parts)
					{
						FString ArgName, ArgDescription;
						if (!Part.Split(TEXT(" - "), &ArgName, &ArgDescription))
						{
							ArgName = Part;
						}
						Prompt.Arguments.Add({ ArgName.TrimStartAndEnd(), ArgDescription.TrimStartAndEnd() });
					}
				}
				else
				{
					Prompt.Text += Line + TEXT("\n");
				}
			}
			Prompt.Text.TrimStartAndEndInline();
			Prompts.Add(MoveTemp(Prompt));
		}
		return Prompts;
	}
}

FNameTBDAIMcpServer& FNameTBDAIMcpServer::Get()
{
	static FNameTBDAIMcpServer Server;
	return Server;
}

bool FNameTBDAIMcpServer::Start(int32 Port, FString& OutError)
{
	Stop();
	RequestLoopbackBinding(Port);

	FHttpServerModule& HttpServer = FHttpServerModule::Get();
	Router = HttpServer.GetHttpRouter(static_cast<uint32>(Port), /*bFailOnBindFailure=*/true);
	if (!Router.IsValid())
	{
		OutError = FString::Printf(TEXT("NameTBD MCP: port %d is taken - pick another MCP Port."), Port);
		return false;
	}

	RouteHandle = Router->BindRoute(FHttpPath(TEXT("/mcp")), EHttpServerRequestVerbs::VERB_POST | EHttpServerRequestVerbs::VERB_GET | EHttpServerRequestVerbs::VERB_DELETE,
		MakeRouteHandler<FHttpRequestHandler>([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
		{
			// Loopback callers only (the listener is bound to 127.0.0.1 as well).
			if (Request.PeerAddress.IsValid())
			{
				const FString Peer = Request.PeerAddress->ToString(false);
				if (Peer != TEXT("127.0.0.1") && Peer != TEXT("::1") && !Peer.StartsWith(TEXT("::ffff:127.")))
				{
					OnComplete(MakeResponse(TEXT("{\"error\":\"local connections only\"}"), EHttpServerResponseCodes::Forbidden));
					return true;
				}
			}

			// The token, from Authorization: Bearer ... - so neither a web page nor another user can drive the editor.
			const FString Expected = GetMutableDefault<UNameTBDAISettings>()->GetOrCreateMcpToken();
			const FString Authorization = FindHeader(Request, TEXT("Authorization"));
			if (!Authorization.Equals(TEXT("Bearer ") + Expected, ESearchCase::CaseSensitive))
			{
				OnComplete(MakeResponse(TEXT("{\"error\":\"missing or wrong token (Editor Preferences > Plugins > NameTBD AI)\"}"), EHttpServerResponseCodes::Denied));
				return true;
			}

			if (Request.Verb == EHttpServerRequestVerbs::VERB_GET)
			{
				// No server-to-client stream: every answer comes back on its POST.
				OnComplete(MakeResponse(TEXT(""), EHttpServerResponseCodes::BadMethod));
				return true;
			}
			if (Request.Verb == EHttpServerRequestVerbs::VERB_DELETE)
			{
				OnComplete(MakeResponse(TEXT(""), EHttpServerResponseCodes::Ok));
				return true;
			}

			FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Request.Body.GetData()), Request.Body.Num());
			const FString Body(Converter.Length(), Converter.Get());
			const FString Reply = HandleMessage(Body);

			TUniquePtr<FHttpServerResponse> Response = MakeResponse(Reply, Reply.IsEmpty() ? EHttpServerResponseCodes::Accepted : EHttpServerResponseCodes::Ok);
			Response->Headers.Add(TEXT("Mcp-Session-Id"), TArray<FString>{ SessionId });
			OnComplete(MoveTemp(Response));
			return true;
		}));

	HttpServer.StartAllListeners();
	bRunning = true;
	BoundPort = Port;
	SessionId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	WriteConnectionFile();
	UE_LOG(LogTemp, Log, TEXT("NameTBD MCP server listening on http://127.0.0.1:%d/mcp"), Port);
	return true;
}

void FNameTBDAIMcpServer::Stop()
{
	if (Router.IsValid() && RouteHandle.IsValid())
	{
		Router->UnbindRoute(RouteHandle);
	}
	RouteHandle.Reset();
	Router.Reset();
	bRunning = false;
}

void FNameTBDAIMcpServer::WriteConnectionFile() const
{
	TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
	Connection->SetNumberField(TEXT("port"), BoundPort > 0 ? BoundPort : UNameTBDAISettings::Get().McpPort);
	Connection->SetStringField(TEXT("token"), GetMutableDefault<UNameTBDAISettings>()->GetOrCreateMcpToken());
	Connection->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
	FFileHelper::SaveStringToFile(ToString(Connection, true), *(FPaths::ProjectSavedDir() / TEXT("NameTBDAI") / TEXT("mcp.json")));
	GetMutableDefault<UNameTBDAISettings>()->WriteClientSetupFiles();
}

FString FNameTBDAIMcpServer::HandleMessage(const FString& Body)
{
	TSharedPtr<FJsonValue> Parsed = ParseValue(Body);
	if (!Parsed.IsValid())
	{
		return ToString(MakeRpcError(nullptr, -32700, TEXT("Parse error")));
	}
	if (Parsed->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Replies;
		for (const TSharedPtr<FJsonValue>& Each : Parsed->AsArray())
		{
			if (Each->Type == EJson::Object)
			{
				if (TSharedPtr<FJsonObject> Reply = HandleRequest(Each->AsObject()))
				{
					Replies.Add(MakeShared<FJsonValueObject>(Reply));
				}
			}
		}
		return Replies.Num() > 0 ? ToString(MakeShared<FJsonValueArray>(Replies)) : FString();
	}
	if (Parsed->Type != EJson::Object)
	{
		return ToString(MakeRpcError(nullptr, -32600, TEXT("Invalid Request")));
	}
	const TSharedPtr<FJsonObject> Reply = HandleRequest(Parsed->AsObject());
	return Reply.IsValid() ? ToString(Reply) : FString();
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleRequest(const TSharedPtr<FJsonObject>& Request)
{
	const FString Method = GetString(Request, TEXT("method"));
	const TSharedPtr<FJsonValue> Id = Request->TryGetField(TEXT("id"));
	const bool bNotification = !Id.IsValid();
	const TSharedPtr<FJsonObject> Params = GetObject(Request, TEXT("params"));

	if (bNotification)
	{
		// notifications/initialized, notifications/cancelled... nothing to answer.
		return nullptr;
	}

	TSharedPtr<FJsonObject> Result;
	FString Error;
	if (Method == TEXT("initialize"))
	{
		Result = HandleInitialize(Params);
	}
	else if (Method == TEXT("ping"))
	{
		Result = MakeShared<FJsonObject>();
	}
	else if (Method == TEXT("tools/list"))
	{
		Result = HandleToolsList();
	}
	else if (Method == TEXT("tools/call"))
	{
		Result = HandleToolsCall(Params);
	}
	else if (Method == TEXT("resources/list"))
	{
		Result = HandleResourcesList();
	}
	else if (Method == TEXT("resources/read"))
	{
		Result = HandleResourcesRead(Params, Error);
	}
	else if (Method == TEXT("resources/templates/list"))
	{
		Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("resourceTemplates"), TArray<TSharedPtr<FJsonValue>>());
	}
	else if (Method == TEXT("prompts/list"))
	{
		Result = HandlePromptsList();
	}
	else if (Method == TEXT("prompts/get"))
	{
		Result = HandlePromptsGet(Params, Error);
	}
	else
	{
		return MakeRpcError(Id, -32601, FString::Printf(TEXT("Method not found: %s"), *Method));
	}

	if (!Result.IsValid())
	{
		return MakeRpcError(Id, -32602, Error.IsEmpty() ? FString(TEXT("Invalid params")) : Error);
	}
	TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Reply->SetField(TEXT("id"), Id);
	Reply->SetObjectField(TEXT("result"), Result);
	return Reply;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleInitialize(const TSharedPtr<FJsonObject>& Params)
{
	const FString Asked = GetString(Params, TEXT("protocolVersion"));
	if (TSharedPtr<FJsonObject> Client = GetObject(Params, TEXT("clientInfo")))
	{
		ClientLabel = FString::Printf(TEXT("mcp:%s %s"), *GetString(Client, TEXT("name"), TEXT("client")), *GetString(Client, TEXT("version"))).TrimEnd();
	}
	FString Version = SupportedVersions[0];
	for (const TCHAR* Supported : SupportedVersions)
	{
		if (Asked == Supported)
		{
			Version = Supported;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), Version);
	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> Tools = MakeShared<FJsonObject>();
	Tools->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("tools"), Tools);
	Capabilities->SetObjectField(TEXT("resources"), MakeShared<FJsonObject>());
	Capabilities->SetObjectField(TEXT("prompts"), MakeShared<FJsonObject>());
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
	Info->SetStringField(TEXT("name"), TEXT("nametbd-unreal"));
	Info->SetStringField(TEXT("title"), TEXT("NameTBD for Unreal Editor"));
	Info->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Result->SetObjectField(TEXT("serverInfo"), Info);
	// The map, so the client's model knows the plugin from the first message - the same text the in-editor chat caches.
	Result->SetStringField(TEXT("instructions"), NameTBDAIDocs::GetMap());
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleToolsList()
{
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();
	TArray<TSharedPtr<FJsonValue>> Tools;
	for (const FNameTBDAITool& Tool : NameTBDAITools::GetTools())
	{
		if ((Tool.bWrites && !Settings.bMcpAllowWrites) || (Tool.bOptIn && !Settings.bAllowPreviewCapture))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Tool.Name);
		Entry->SetStringField(TEXT("description"), Tool.Description);
		Entry->SetObjectField(TEXT("inputSchema"), Tool.InputSchema);
		TSharedPtr<FJsonObject> Annotations = MakeShared<FJsonObject>();
		Annotations->SetBoolField(TEXT("readOnlyHint"), !Tool.bWrites);
		Annotations->SetBoolField(TEXT("destructiveHint"), false);
		Annotations->SetBoolField(TEXT("openWorldHint"), false);
		Entry->SetObjectField(TEXT("annotations"), Annotations);
		Tools.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), Tools);
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleToolsCall(const TSharedPtr<FJsonObject>& Params)
{
	const FString Name = GetString(Params, TEXT("name"));
	const UNameTBDAISettings& Settings = UNameTBDAISettings::Get();

	FNameTBDAIChangeSet Changes;
	FNameTBDAIToolContext Context;
	Context.Origin = ENameTBDAIOrigin::Mcp;
	Context.Changes = &Changes;
	Context.bAllowWrites = Settings.bMcpAllowWrites;
	Context.bAllowCapture = Settings.bAllowPreviewCapture;
	Context.Model = ClientLabel.IsEmpty() ? FString(TEXT("mcp")) : ClientLabel;
	const FNameTBDAIToolResult Outcome = NameTBDAITools::Execute(Name, GetObject(Params, TEXT("arguments")), Context);

	if (Settings.bMcpSaveAfterWrites && !Changes.IsEmpty())
	{
		Changes.SaveAll();
	}

	TArray<TSharedPtr<FJsonValue>> Content;
	TSharedPtr<FJsonObject> Text = MakeShared<FJsonObject>();
	Text->SetStringField(TEXT("type"), TEXT("text"));
	Text->SetStringField(TEXT("text"), Outcome.ToText());
	Content.Add(MakeShared<FJsonValueObject>(Text));
	for (const FNameTBDAIImage& Image : Outcome.Images)
	{
		TSharedPtr<FJsonObject> Picture = MakeShared<FJsonObject>();
		Picture->SetStringField(TEXT("type"), TEXT("image"));
		Picture->SetStringField(TEXT("data"), Image.Base64);
		Picture->SetStringField(TEXT("mimeType"), Image.MimeType);
		Content.Add(MakeShared<FJsonValueObject>(Picture));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("content"), Content);
	Result->SetBoolField(TEXT("isError"), !Outcome.bOk);
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleResourcesList()
{
	TArray<TSharedPtr<FJsonValue>> Resources;
	auto Add = [&Resources](const FString& Uri, const FString& Name, const FString& Description)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("uri"), Uri);
		Entry->SetStringField(TEXT("name"), Name);
		Entry->SetStringField(TEXT("description"), Description);
		Entry->SetStringField(TEXT("mimeType"), TEXT("text/markdown"));
		Resources.Add(MakeShared<FJsonValueObject>(Entry));
	};
	Add(TEXT("nametbd://map"), TEXT("NameTBD map"), TEXT("What each system of the plugin is and which tool answers what."));
	TArray<const NameTBDAIDocs::FTopic*> Topics;
	NameTBDAIDocs::GetTopics(Topics);
	for (const NameTBDAIDocs::FTopic* Topic : Topics)
	{
		Add(TEXT("nametbd://docs/") + Topic->Id, Topic->Title, Topic->Kind);
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("resources"), Resources);
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandleResourcesRead(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const FString Uri = GetString(Params, TEXT("uri"));
	FString Text;
	if (Uri == TEXT("nametbd://map"))
	{
		Text = NameTBDAIDocs::GetMap();
	}
	else if (Uri.StartsWith(TEXT("nametbd://docs/")))
	{
		if (const NameTBDAIDocs::FTopic* Topic = NameTBDAIDocs::Find(Uri.Mid(15)))
		{
			Text = FString::Printf(TEXT("# %s\n\n%s"), *Topic->Title, *Topic->Text);
		}
	}
	if (Text.IsEmpty())
	{
		OutError = FString::Printf(TEXT("No resource %s."), *Uri);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("uri"), Uri);
	Entry->SetStringField(TEXT("mimeType"), TEXT("text/markdown"));
	Entry->SetStringField(TEXT("text"), Text);
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Contents;
	Contents.Add(MakeShared<FJsonValueObject>(Entry));
	Result->SetArrayField(TEXT("contents"), Contents);
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandlePromptsList()
{
	TArray<TSharedPtr<FJsonValue>> Prompts;
	for (const FPromptFile& Prompt : LoadPrompts())
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Prompt.Name);
		Entry->SetStringField(TEXT("title"), Prompt.Title);
		Entry->SetStringField(TEXT("description"), Prompt.Description);
		TArray<TSharedPtr<FJsonValue>> Arguments;
		for (const TPair<FString, FString>& Argument : Prompt.Arguments)
		{
			TSharedPtr<FJsonObject> Arg = MakeShared<FJsonObject>();
			Arg->SetStringField(TEXT("name"), Argument.Key);
			Arg->SetStringField(TEXT("description"), Argument.Value);
			Arg->SetBoolField(TEXT("required"), true);
			Arguments.Add(MakeShared<FJsonValueObject>(Arg));
		}
		Entry->SetArrayField(TEXT("arguments"), Arguments);
		Prompts.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("prompts"), Prompts);
	return Result;
}

TSharedPtr<FJsonObject> FNameTBDAIMcpServer::HandlePromptsGet(const TSharedPtr<FJsonObject>& Params, FString& OutError)
{
	const FString Name = GetString(Params, TEXT("name"));
	const TSharedPtr<FJsonObject> Arguments = GetObject(Params, TEXT("arguments"));
	for (const FPromptFile& Prompt : LoadPrompts())
	{
		if (Prompt.Name != Name)
		{
			continue;
		}
		FString Text = Prompt.Text;
		for (const TPair<FString, FString>& Argument : Prompt.Arguments)
		{
			Text.ReplaceInline(*FString::Printf(TEXT("{{%s}}"), *Argument.Key), *GetString(Arguments, Argument.Key));
		}
		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("type"), TEXT("text"));
		Content->SetStringField(TEXT("text"), Text);
		TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("role"), TEXT("user"));
		Message->SetObjectField(TEXT("content"), Content);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("description"), Prompt.Description);
		TArray<TSharedPtr<FJsonValue>> Messages;
		Messages.Add(MakeShared<FJsonValueObject>(Message));
		Result->SetArrayField(TEXT("messages"), Messages);
		return Result;
	}
	OutError = FString::Printf(TEXT("No prompt '%s'."), *Name);
	return nullptr;
}
