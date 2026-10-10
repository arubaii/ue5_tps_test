// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "NameTBDAISettings.generated.h"

class UNameTBDAITheme;

/** Who answers the in-editor chat. Every provider is reached with the user's own key, straight from this editor. */
UENUM()
enum class ENameTBDAIProvider : uint8
{
	Anthropic UMETA(DisplayName = "Anthropic (Claude)"),
	OpenAI UMETA(DisplayName = "OpenAI"),
	Gemini UMETA(DisplayName = "Google Gemini"),

	/** Any server speaking OpenAI's Chat Completions: Ollama, LM Studio, vLLM, most local servers and gateways. */
	OpenAICompatible UMETA(DisplayName = "OpenAI-Compatible Endpoint")
};

/** How hard the model thinks (Anthropic's effort). Lower is faster and cheaper; higher is better at long builds. */
UENUM()
enum class ENameTBDAIEffort : uint8
{
	Default UMETA(DisplayName = "Model Default"),
	Low,
	Medium,
	High,
	XHigh UMETA(DisplayName = "Extra High")
};

/** What a model costs, in US dollars per million tokens - for the chat's per-request estimate only. */
USTRUCT()
struct FNameTBDAIModelPrice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Price", meta = (ClampMin = "0"))
	float Input = 0.f;

	UPROPERTY(EditAnywhere, Category = "Price", meta = (ClampMin = "0"))
	float Output = 0.f;

	/** Input read from the provider's prompt cache. */
	UPROPERTY(EditAnywhere, Category = "Price", meta = (ClampMin = "0"))
	float CacheRead = 0.f;

	/** Input written to the provider's prompt cache. */
	UPROPERTY(EditAnywhere, Category = "Price", meta = (ClampMin = "0"))
	float CacheWrite = 0.f;
};

/**
 * The AI Assistant: Editor Preferences > Plugins > NameTBD AI.
 *
 * Per user and per project (EditorPerProjectUserSettings) - never written to a Default*.ini, so a key is never committed with
 * the project. The plugin ships no key and runs no server: the chat calls the provider picked here with the user's own key,
 * and the MCP server runs on this machine only (127.0.0.1), for assistants the user already runs.
 */
UCLASS(config = EditorPerProjectUserSettings, meta = (DisplayName = "NameTBD AI"))
class NAMETBDAI_API UNameTBDAISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	UNameTBDAISettings();

	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("NameTBDAI"); }

	// ---- What is sent ------------------------------------------------------------------------------------------

	/**
	 * Read this first. The AI features are optional and editor-only; the rest of the plugin works without them.
	 *
	 * Sent to the provider picked below, with your key, on each chat request: a fixed map of the plugin (~2k tokens), the tool
	 * definitions, your messages, and what the tools return - the settings of the widgets or assets a tool was asked about, and
	 * doc topics. Never the whole project, never files the tools weren't asked to read. Usage is billed by that provider to you.
	 * A local model (Ollama, LM Studio) through the OpenAI-Compatible Endpoint sends nothing off this machine.
	 *
	 * The MCP server sends nothing anywhere: it answers assistants running on this machine (Claude Desktop, Claude Code,
	 * Cursor...) that you connect to it, and they send what they send under your account with them.
	 */
	UPROPERTY(VisibleAnywhere, Transient, Category = "About", meta = (MultiLine = true))
	FText WhatIsSent;

	// ---- Provider ----------------------------------------------------------------------------------------------

	UPROPERTY(config, EditAnywhere, Category = "Provider")
	ENameTBDAIProvider Provider = ENameTBDAIProvider::Anthropic;

	/** Your own API key for the provider. Stored in your per-user editor settings on this machine, never in the project. */
	UPROPERTY(config, EditAnywhere, Category = "Provider", meta = (PasswordField = true))
	FString ApiKey;

	/**
	 * The server to call. Empty: the provider's own (api.anthropic.com, api.openai.com, generativelanguage.googleapis.com).
	 * For the OpenAI-Compatible Endpoint, its base URL - e.g. http://localhost:11434/v1 for Ollama,
	 * http://localhost:1234/v1 for LM Studio. "/chat/completions" is added.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	FString Endpoint;

	/**
	 * The model that builds and edits: widgets, animations, materials. The stronger one.
	 * Anthropic: claude-opus-5-5 (default), claude-sonnet-5-5. OpenAI / Gemini / local: that provider's model id.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	FString GenerationModel = TEXT("claude-opus-5-5");

	/**
	 * The model for questions about the plugin and quick looks at the selection: the cheaper one. Used when Ask Mode is picked
	 * in the chat. Anthropic: claude-haiku-5-5 (default).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	FString QuestionModel = TEXT("claude-haiku-5-5");

	/** Anthropic only: how hard the generation model thinks. Model Default leaves it to the model. */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	ENameTBDAIEffort GenerationEffort = ENameTBDAIEffort::Medium;

	/** Anthropic only: how hard the question model thinks. */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	ENameTBDAIEffort QuestionEffort = ENameTBDAIEffort::Low;

	/** The longest reply a request may have, in tokens. Replies are streamed, so long ones don't time out. */
	UPROPERTY(config, EditAnywhere, Category = "Provider", meta = (ClampMin = "1024", ClampMax = "128000"))
	int32 MaxOutputTokens = 32000;

	/**
	 * Anthropic only: when the model's safety classifier declines a request (it can, rarely, on harmless UI work), the request is
	 * run again on Anthropic's recommended fallback model in the same call, instead of the chat just stopping. Applies to models
	 * that support it (Opus 5 / 5.5, Sonnet 5.5, Fable); ignored for others.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	bool bRefusalFallbacks = true;

	/** Show replies as they are written. Off: each reply appears once it is complete (some local servers stream badly). */
	UPROPERTY(config, EditAnywhere, Category = "Provider")
	bool bStreamReplies = true;

	// ---- Chat ----------------------------------------------------------------------------------------------------

	/**
	 * What the chat builds is applied at once - you see it in the editor as it is made - and each request ends on an Apply /
	 * Discard card: Discard takes all of it back (one undo step, and the assets it made are deleted). Off: nothing to confirm;
	 * Ctrl+Z still takes a request's changes back in one step.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Chat")
	bool bConfirmChanges = true;

	/** Save what a request made or changed when it is applied (or, with Confirm Changes off, when the request ends). */
	UPROPERTY(config, EditAnywhere, Category = "Chat")
	bool bSaveOnApply = true;

	/** The most tool calls one request may make before it stops and asks you to continue. */
	UPROPERTY(config, EditAnywhere, Category = "Chat", meta = (ClampMin = "1", ClampMax = "200"))
	int32 MaxToolCallsPerRequest = 60;

	/**
	 * Lets the model render the widget it built to an image and look at it - to catch text over a glow, poor contrast, things
	 * off the card. Off by default: each look is an image sent to the provider (roughly 1-2k tokens).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Chat")
	bool bAllowPreviewCapture = false;

	/**
	 * OpenAI / Gemini / local only: tool results older than this many requests are sent as one-line summaries, to keep long
	 * chats cheap. (Anthropic chats keep their history as it was - the provider clears old tool results itself.)
	 */
	UPROPERTY(config, EditAnywhere, Category = "Chat", meta = (ClampMin = "1", ClampMax = "20"))
	int32 CollapseToolResultsAfter = 3;

	/** Dollars per million tokens, by model id, for the cost shown under each request. Models not listed show tokens only. */
	UPROPERTY(config, EditAnywhere, Category = "Chat")
	TMap<FString, FNameTBDAIModelPrice> ModelPrices;

	// ---- Building ------------------------------------------------------------------------------------------------

	/** The house style every tool builds with. None: the built-in theme. */
	UPROPERTY(config, EditAnywhere, Category = "Building")
	TSoftObjectPtr<UNameTBDAITheme> Theme;

	/** Where new Widget Blueprints, animations, materials and particle systems go when a request doesn't say. */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ContentDir))
	FDirectoryPath OutputFolder;

	/**
	 * Where the library the tools pick from is built the first time it is needed - the master UI materials, Material Functions
	 * and Niagara UI templates (from the definitions in the plugin's Resources/AI) - unless the plugin ships them already built.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Building", meta = (ContentDir))
	FDirectoryPath LibraryFolder;

	// ---- MCP server ----------------------------------------------------------------------------------------------

	/**
	 * Runs the MCP server while the editor is open, so Claude Desktop, Claude Code, Cursor and other MCP clients on this machine
	 * can use the same tools on the open project. Listens on 127.0.0.1 only, and every request must carry this session's token.
	 */
	UPROPERTY(config, EditAnywhere, Category = "MCP Server")
	bool bStartMcpServer = false;

	UPROPERTY(config, EditAnywhere, Category = "MCP Server", meta = (ClampMin = "1024", ClampMax = "65535"))
	int32 McpPort = 47917;

	/** Let MCP clients change things (each change one undo step). Off: they can only read and search. */
	UPROPERTY(config, EditAnywhere, Category = "MCP Server")
	bool bMcpAllowWrites = true;

	/**
	 * The token MCP clients must send (Authorization: Bearer ...). Made the first time the server starts and kept in your
	 * per-user settings, so a client set up once keeps working; Regenerate Token cuts every client off until set up again.
	 */
	UPROPERTY(config, VisibleAnywhere, Category = "MCP Server", meta = (PasswordField = true))
	FString McpToken;

	/** Save what an MCP tool call made or changed, right after the call. */
	UPROPERTY(config, EditAnywhere, Category = "MCP Server")
	bool bMcpSaveAfterWrites = true;

	/**
	 * The command that connects Claude Code to this editor (over HTTP, with the token) - select it and copy (Ctrl+C), or use
	 * Copy Claude Code Command. Shown once the token exists (Start Server or either Copy button makes it).
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "MCP Server", meta = (DisplayName = "Claude Code Command"))
	FString ClaudeCodeCommandLine;

	/**
	 * The JSON for claude_desktop_config.json - select it and copy, or open Saved/NameTBDAI/claude_desktop_config.json,
	 * which the server writes when it starts (with claude_code_command.txt next to it).
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = "MCP Server", meta = (DisplayName = "Claude Desktop Config", MultiLine = true))
	FString ClaudeDesktopConfigText;

	/** Copies the Claude Code command that connects it to this editor (over HTTP, with this session's token). */
	UFUNCTION(CallInEditor, Category = "MCP Server", meta = (DisplayName = "Copy Claude Code Command"))
	void CopyClaudeCodeCommand();

	/**
	 * Copies the JSON for claude_desktop_config.json (or any stdio MCP client): it runs the plugin's small bridge script with
	 * the engine's own Python, which forwards to this editor.
	 */
	UFUNCTION(CallInEditor, Category = "MCP Server", meta = (DisplayName = "Copy Claude Desktop Config"))
	void CopyClaudeDesktopConfig();

	UFUNCTION(CallInEditor, Category = "MCP Server", meta = (DisplayName = "Regenerate Token"))
	void RegenerateMcpToken();

	/** Starts (or restarts) the MCP server now. */
	UFUNCTION(CallInEditor, Category = "MCP Server", meta = (DisplayName = "Start Server"))
	void StartMcpServerNow();

	UFUNCTION(CallInEditor, Category = "MCP Server", meta = (DisplayName = "Stop Server"))
	void StopMcpServerNow();

	/** Builds the library (materials, Material Functions, Niagara UI templates) now rather than when first needed. */
	UFUNCTION(CallInEditor, Category = "Building", meta = (DisplayName = "Build Library Now"))
	void BuildLibraryNow();

	// ---- Helpers -------------------------------------------------------------------------------------------------

	static const UNameTBDAISettings& Get() { return *GetDefault<UNameTBDAISettings>(); }

	/** OutputFolder as a package path, "/Game/NameTBD_AI" by default. */
	FString GetOutputFolder() const;

	/** LibraryFolder as a package path, "/Game/NameTBD_AI/Library" by default. */
	FString GetLibraryFolder() const;

	/** The MCP token, made (and saved) if there is none yet. */
	FString GetOrCreateMcpToken();

	/** The Claude Code command for this port and token (empty while there's no token). */
	FString MakeClaudeCodeCommandLine() const;

	/** The claude_desktop_config.json block (the bridge run with the engine's Python). */
	FString MakeClaudeDesktopConfig() const;

	/** Fills the two fields above and writes them to Saved/NameTBDAI/ (claude_code_command.txt, claude_desktop_config.json). */
	void WriteClientSetupFiles();

	virtual void PostInitProperties() override;

	/** The price of a model: the table above, else built-in figures for Anthropic's current models, else null. */
	const FNameTBDAIModelPrice* FindPrice(const FString& Model) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
