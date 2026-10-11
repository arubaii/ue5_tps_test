// Copyright AR GameSystems 2026. All rights reserved.

#include "NameTBDAISettings.h"

#include "Framework/Notifications/NotificationManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NameTBDAILibrary.h"
#include "NameTBDAIMcpServer.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "NameTBDAISettings"

namespace
{
	void Notify(const FText& Text, bool bSuccess = true)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** The engine's own Python, which every editor install has - so the bridge needs nothing installed. */
	FString GetEnginePython()
	{
#if PLATFORM_WINDOWS
		const FString Relative = TEXT("Binaries/ThirdParty/Python3/Win64/python.exe");
#elif PLATFORM_MAC
		const FString Relative = TEXT("Binaries/ThirdParty/Python3/Mac/bin/python3");
#else
		const FString Relative = TEXT("Binaries/ThirdParty/Python3/Linux/bin/python3");
#endif
		return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / Relative);
	}

	FString GetBridgeScript()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NameTBD"));
		const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir() : FPaths::ProjectPluginsDir() / TEXT("NameTBD");
		return FPaths::ConvertRelativePathToFull(Base / TEXT("Resources/AI/MCP/nametbd_mcp_bridge.py"));
	}

	FString JsonEscape(const FString& Text)
	{
		return Text.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	}
}

UNameTBDAISettings::UNameTBDAISettings()
{
	WhatIsSent = LOCTEXT("WhatIsSent",
		"Optional and editor-only - the rest of the plugin works without it.\n"
		"Chat: sent to the provider below, with your own key - a fixed map of the plugin, the tool definitions, your messages, and what "
		"the tools return (settings of the widgets or assets asked about, doc topics). Never the whole project. Billed to you by that "
		"provider; a local model (OpenAI-Compatible Endpoint) sends nothing off this machine.\n"
		"MCP server: listens on 127.0.0.1 only, for assistants on this machine you connect to it; it sends nothing anywhere itself.");
}

FString UNameTBDAISettings::GetOutputFolder() const
{
	FString Path = OutputFolder.Path.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		return TEXT("/Game/NameTBD_AI");
	}
	if (!Path.StartsWith(TEXT("/")))
	{
		Path = TEXT("/Game/") + Path;
	}
	return Path;
}

FString UNameTBDAISettings::GetLibraryFolder() const
{
	FString Path = LibraryFolder.Path.TrimStartAndEnd();
	if (Path.IsEmpty())
	{
		return TEXT("/Game/NameTBD_AI/Library");
	}
	if (!Path.StartsWith(TEXT("/")))
	{
		Path = TEXT("/Game/") + Path;
	}
	return Path;
}

const FNameTBDAIModelPrice* UNameTBDAISettings::FindPrice(const FString& Model) const
{
	if (const FNameTBDAIModelPrice* Own = ModelPrices.Find(Model))
	{
		return Own;
	}

	// Anthropic's current list prices (USD per million tokens), so estimates work out of the box. Edit Model Prices to change.
	static const TMap<FString, FNameTBDAIModelPrice> BuiltIn = []()
	{
		auto Make = [](float Input, float Output, float CacheRead, float CacheWrite)
		{
			FNameTBDAIModelPrice Price;
			Price.Input = Input;
			Price.Output = Output;
			Price.CacheRead = CacheRead;
			Price.CacheWrite = CacheWrite;
			return Price;
		};
		TMap<FString, FNameTBDAIModelPrice> Prices;
		Prices.Add(TEXT("claude-opus-5-5"), Make(4.f, 20.f, 0.2f, 5.f));
		Prices.Add(TEXT("claude-opus-5"), Make(5.f, 25.f, 0.5f, 6.25f));
		Prices.Add(TEXT("claude-sonnet-5-5"), Make(2.f, 10.f, 0.2f, 2.5f));
		Prices.Add(TEXT("claude-haiku-5-5"), Make(0.1f, 0.5f, 0.01f, 0.125f));
		Prices.Add(TEXT("claude-fable-5-1"), Make(10.f, 50.f, 0.25f, 12.5f));
		return Prices;
	}();
	return BuiltIn.Find(Model);
}

FString UNameTBDAISettings::GetOrCreateMcpToken()
{
	if (McpToken.Len() < 24)
	{
		McpToken = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower() + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		SaveConfig();
	}
	ClaudeCodeCommandLine = MakeClaudeCodeCommandLine();
	return McpToken;
}

FString UNameTBDAISettings::MakeClaudeCodeCommandLine() const
{
	if (McpToken.Len() < 24)
	{
		return FString();
	}
	return FString::Printf(
		TEXT("claude mcp add --transport http nametbd http://127.0.0.1:%d/mcp --header \"Authorization: Bearer %s\""), McpPort, *McpToken);
}

void UNameTBDAISettings::PostInitProperties()
{
	Super::PostInitProperties();
	ClaudeCodeCommandLine = MakeClaudeCodeCommandLine();
	ClaudeDesktopConfigText = MakeClaudeDesktopConfig();
}

void UNameTBDAISettings::CopyClaudeCodeCommand()
{
	GetOrCreateMcpToken();
	const FString Command = MakeClaudeCodeCommandLine();
	ClaudeCodeCommandLine = Command;
	FPlatformApplicationMisc::ClipboardCopy(*Command);
	Notify(FNameTBDAIMcpServer::Get().IsRunning()
		? LOCTEXT("CopiedCode", "Claude Code command copied. Run it in a terminal, in your project.")
		: LOCTEXT("CopiedCodeNotRunning", "Claude Code command copied. Start the MCP server (Start Server) before using it."));
}

void UNameTBDAISettings::CopyClaudeDesktopConfig()
{
	GetOrCreateMcpToken();
	const FString Json = MakeClaudeDesktopConfig();
	ClaudeDesktopConfigText = Json;
	FPlatformApplicationMisc::ClipboardCopy(*Json);
	FNameTBDAIMcpServer::Get().WriteConnectionFile();
	Notify(LOCTEXT("CopiedDesktop", "Config copied (also in Saved/NameTBDAI/claude_desktop_config.json). Merge it into claude_desktop_config.json (or your client's MCP config) and restart the client."));
}

FString UNameTBDAISettings::MakeClaudeDesktopConfig() const
{
	const FString Project = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	return FString::Printf(TEXT(
		"{\n"
		"  \"mcpServers\": {\n"
		"    \"nametbd\": {\n"
		"      \"command\": \"%s\",\n"
		"      \"args\": [\"%s\", \"--project\", \"%s\"]\n"
		"    }\n"
		"  }\n"
		"}\n"), *JsonEscape(GetEnginePython()), *JsonEscape(GetBridgeScript()), *JsonEscape(Project));
}

void UNameTBDAISettings::WriteClientSetupFiles()
{
	GetOrCreateMcpToken();
	ClaudeCodeCommandLine = MakeClaudeCodeCommandLine();
	ClaudeDesktopConfigText = MakeClaudeDesktopConfig();
	const FString Folder = FPaths::ProjectSavedDir() / TEXT("NameTBDAI");
	FFileHelper::SaveStringToFile(ClaudeCodeCommandLine + TEXT("\n"), *(Folder / TEXT("claude_code_command.txt")));
	FFileHelper::SaveStringToFile(ClaudeDesktopConfigText, *(Folder / TEXT("claude_desktop_config.json")));
}

void UNameTBDAISettings::RegenerateMcpToken()
{
	McpToken.Reset();
	GetOrCreateMcpToken();
	FNameTBDAIMcpServer::Get().WriteConnectionFile();
	Notify(LOCTEXT("Regenerated", "New MCP token made. Clients set up with the old one must be set up again (Claude Desktop's bridge picks it up by itself)."));
}

void UNameTBDAISettings::StartMcpServerNow()
{
	FString Error;
	if (FNameTBDAIMcpServer::Get().Start(McpPort, Error))
	{
		Notify(FText::Format(LOCTEXT("Started", "NameTBD MCP server listening on 127.0.0.1:{0}."), FText::AsNumber(McpPort, &FNumberFormattingOptions::DefaultNoGrouping())));
	}
	else
	{
		Notify(FText::FromString(Error), false);
	}
}

void UNameTBDAISettings::StopMcpServerNow()
{
	FNameTBDAIMcpServer::Get().Stop();
	Notify(LOCTEXT("Stopped", "NameTBD MCP server stopped."));
}

namespace
{
	/** Builds (bForce: rebuilds) the library; the details go to the Output Log, a summary to a notification. */
	void RunLibraryBuild(bool bForce)
	{
		TArray<FString> Report;
		NameTBDAILibrary::BuildAll(Report, bForce);
		int32 Problems = 0;
		for (const FString& Line : Report)
		{
			Problems += Line.StartsWith(TEXT("built ")) ? 0 : 1;
		}
		const FString Summary = Report.Num() == 0 ? FString(TEXT("NameTBD AI library: everything is built and current."))
			: FString::Printf(TEXT("NameTBD AI library: %d built, %d problem(s) - details in the Output Log."), Report.Num() - Problems, Problems);
		Notify(FText::FromString(Summary), Problems == 0);
	}

	// The settings page's buttons don't run on some setups; these always do (Output Log > Cmd, or the ` console).
	FAutoConsoleCommand BuildLibraryCommand(
		TEXT("NameTBDAI.BuildLibrary"),
		TEXT("Builds the NameTBD AI library (UI master materials, Material Functions, Niagara UI templates): what isn't built yet, and project copies built from an older definition."),
		FConsoleCommandDelegate::CreateLambda([]() { RunLibraryBuild(false); }));

	FAutoConsoleCommand RebuildLibraryCommand(
		TEXT("NameTBDAI.RebuildLibrary"),
		TEXT("Rebuilds every NameTBD AI library asset in the project's Library Folder from its definition (Resources/AI/Materials), whatever its state."),
		FConsoleCommandDelegate::CreateLambda([]() { RunLibraryBuild(true); }));
}

void UNameTBDAISettings::BuildLibraryNow()
{
	UE_LOG(LogTemp, Display, TEXT("NameTBD AI: Build Library Now pressed."));
	RunLibraryBuild(false);
}

#if WITH_EDITOR
void UNameTBDAISettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	ClaudeCodeCommandLine = MakeClaudeCodeCommandLine();
	if (Name == GET_MEMBER_NAME_CHECKED(UNameTBDAISettings, bStartMcpServer) || Name == GET_MEMBER_NAME_CHECKED(UNameTBDAISettings, McpPort))
	{
		if (bStartMcpServer)
		{
			StartMcpServerNow();
		}
		else if (FNameTBDAIMcpServer::Get().IsRunning())
		{
			StopMcpServerNow();
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
