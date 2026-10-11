// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"
#include "Dom/JsonObject.h"

class IHttpRouter;

/**
 * The MCP server: the tool layer, the docs and a few task prompts, for the assistant the user already runs (Claude Desktop,
 * Claude Code, Cursor...), acting on the open project.
 *
 * Model Context Protocol over its Streamable HTTP transport: JSON-RPC 2.0 posted to http://127.0.0.1:<port>/mcp, answered
 * with one JSON response each (no server-sent events - none of this server's methods need to stream). Bound to 127.0.0.1
 * only, and every request must carry the token (Authorization: Bearer <token>) from Editor Preferences > Plugins > NameTBD AI.
 * Stdio clients go through Resources/AI/MCP/nametbd_mcp_bridge.py, which forwards each line to this server.
 *
 * Requests are handled on the game thread (the HTTP server ticks there), so tools run exactly as they do from the chat.
 */
class FNameTBDAIMcpServer
{
public:
	static FNameTBDAIMcpServer& Get();

	/** Starts listening (restarts if running). False, with OutError, when the port can't be bound. */
	bool Start(int32 Port, FString& OutError);
	void Stop();
	bool IsRunning() const { return bRunning; }
	int32 GetPort() const { return BoundPort; }

	/** Writes Saved/NameTBDAI/mcp.json (port and token) for the stdio bridge. */
	void WriteConnectionFile() const;

	/** Handles one JSON-RPC message (or batch). Empty when nothing is to be sent back (a notification). Public for tests. */
	FString HandleMessage(const FString& Body);

private:
	TSharedPtr<FJsonObject> HandleRequest(const TSharedPtr<FJsonObject>& Request);
	TSharedPtr<FJsonObject> HandleInitialize(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleToolsList();
	TSharedPtr<FJsonObject> HandleToolsCall(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleResourcesList();
	TSharedPtr<FJsonObject> HandleResourcesRead(const TSharedPtr<FJsonObject>& Params, FString& OutError);
	TSharedPtr<FJsonObject> HandlePromptsList();
	TSharedPtr<FJsonObject> HandlePromptsGet(const TSharedPtr<FJsonObject>& Params, FString& OutError);

	bool bRunning = false;
	int32 BoundPort = 0;
	TSharedPtr<IHttpRouter> Router;
	FHttpRouteHandle RouteHandle;
	FString SessionId;

	/** The client's name and version from initialize ("claude-code 2.1"), for the per-model stats. */
	FString ClientLabel;
};
