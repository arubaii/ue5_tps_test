// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UPackage;

/**
 * The tool layer: every tool the chat panel and the MCP server offer, in one registry. A tool is a name, a description, a
 * JSON schema of its input and a function; input is checked against the schema (and coerced where a model is loose - a number
 * as a string, an object as JSON text), then written through the plugin's own code. Data, not code generation.
 *
 * Every write tool runs inside a transaction (Ctrl+Z takes it back) and touches only the assets and widgets it is given; what
 * it makes and changes is noted in the request's change set, which the chat's Apply / Discard and the saving use.
 */

/** An image a tool hands back to the model (capture_preview). */
struct FNameTBDAIImage
{
	FString MimeType = TEXT("image/png");
	FString Base64;
	int32 Width = 0;
	int32 Height = 0;
};

struct FNameTBDAIToolResult
{
	bool bOk = true;

	/** Compact facts for the model. */
	TSharedPtr<FJsonObject> Data;

	/** Why it failed - short and actionable, for the model to correct from. */
	FString Error;

	/** Problems that didn't stop the tool (a field that didn't take, a compile warning). */
	TArray<FString> Warnings;

	TArray<FNameTBDAIImage> Images;

	static FNameTBDAIToolResult Ok(const TSharedPtr<FJsonObject>& InData = nullptr);
	static FNameTBDAIToolResult Fail(const FString& InError);

	/** What the model reads: the data as compact JSON, with "warnings" or "error". */
	FString ToText() const;
};

/** Where a request came from. */
enum class ENameTBDAIOrigin : uint8
{
	Chat,
	Mcp
};

/** What one request - a chat turn, or one MCP call - has made and changed. */
struct FNameTBDAIChangeSet
{
	/** Assets made: deleted by Discard. */
	TArray<TWeakObjectPtr<UObject>> CreatedAssets;

	/** Packages changed or made: saved on Apply. */
	TArray<TWeakObjectPtr<UPackage>> TouchedPackages;

	/** One line per change, for the Apply / Discard card: "Created WBP_GreetingCard", "Added TextBlock 'Title'". */
	TArray<FString> Lines;

	void NoteCreated(UObject* Asset, const FString& Line);
	void NoteTouched(UObject* Object, const FString& Line = FString());
	bool IsEmpty() const { return CreatedAssets.Num() == 0 && TouchedPackages.Num() == 0; }
	void Reset();

	/** Saves every touched package still dirty. Returns how many were saved. */
	int32 SaveAll() const;
};

struct FNameTBDAIToolContext
{
	ENameTBDAIOrigin Origin = ENameTBDAIOrigin::Chat;

	/** Where changes are noted. Never null while a tool runs. */
	FNameTBDAIChangeSet* Changes = nullptr;

	bool bAllowWrites = true;
	bool bAllowCapture = false;

	/** Who is asking: the chat's "provider:model", or the MCP client's name - for the per-model stats (stats.json). */
	FString Model;
};

using FNameTBDAIToolFunction = TFunction<FNameTBDAIToolResult(const TSharedPtr<FJsonObject>& /*Args*/, FNameTBDAIToolContext& /*Context*/)>;

struct FNameTBDAITool
{
	FString Name;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema;

	/** Changes something: runs in a transaction, and only where writes are allowed. */
	bool bWrites = false;

	/** Offered in the chat's Ask mode (questions only): the read tools. */
	bool bInAskMode = false;

	/** Only offered when the user turned it on (capture_preview). */
	bool bOptIn = false;

	FNameTBDAIToolFunction Run;
};

/** Builds a tool's input schema - the plain JSON Schema subset every provider accepts. */
class FNameTBDAISchema
{
public:
	FNameTBDAISchema& Str(const FString& Name, const FString& Description, bool bRequired = false, const TArray<FString>& Enum = {});
	FNameTBDAISchema& Num(const FString& Name, const FString& Description, bool bRequired = false);
	FNameTBDAISchema& Int(const FString& Name, const FString& Description, bool bRequired = false);
	FNameTBDAISchema& Bool(const FString& Name, const FString& Description, bool bRequired = false);

	/** A free-form object (fields to set). */
	FNameTBDAISchema& Obj(const FString& Name, const FString& Description, bool bRequired = false);

	/** A list of ItemType ("string", "number", "object"). */
	FNameTBDAISchema& Arr(const FString& Name, const FString& Description, const FString& ItemType, bool bRequired = false);

	TSharedPtr<FJsonObject> Build() const;

private:
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TArray<FString> Required;
};

namespace NameTBDAITools
{
	/** Registers every tool, once. */
	void RegisterAll();

	void Register(FNameTBDAITool&& Tool);
	const TArray<FNameTBDAITool>& GetTools();
	const FNameTBDAITool* Find(const FString& Name);

	/**
	 * Runs a tool: checks and coerces its input against its schema, refuses a write where writes aren't allowed, opens a
	 * transaction for a write, catches nothing - tools report failures as results. Game thread only.
	 */
	FNameTBDAIToolResult Execute(const FString& Name, const TSharedPtr<FJsonObject>& Args, FNameTBDAIToolContext& Context);

	/** The last asset a tool built or changed - "the widget just made", for follow-ups that don't name it. */
	void SetLastAsset(UObject* Asset);
	UObject* GetLastAsset();

	// Each file's tools.
	void RegisterReadTools();
	void RegisterWidgetTools();
	void RegisterAnimationTools();
	void RegisterMaterialTools();
	void RegisterNiagaraTools();
	void RegisterDataTools();
	void RegisterCaptureTools();
	void RegisterMenuTools();
}
