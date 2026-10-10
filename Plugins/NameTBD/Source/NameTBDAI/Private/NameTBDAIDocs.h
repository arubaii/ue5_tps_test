// Copyright AR GameSystems 2026. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Docs for the model, searched locally - no service, works offline, free.
 *
 * Three sources, merged into one index of small topics (a few hundred tokens each):
 *   - hand-written topics in the plugin's Resources/AI/Docs/*.md ("# Title" then text; "## " splits long files into topics);
 *   - generated from the code's own reflection data as it is now - every class, struct and enum of the plugin's modules, with
 *     its comment and its editable fields' comments (the tooltips the Details panel shows) - so the docs never drift from code;
 *   - the editor panels' help strings in NameTBDTooltips.h, read from the plugin's source when it's there.
 * Searched with BM25 over titles (weighted) and text. Built on first use.
 *
 * The map (Resources/AI/Map.md) is separate: the fixed, cached system prompt that says what each system is and which tool
 * answers what. It never changes within a version, so providers' prompt caching applies to it.
 */
namespace NameTBDAIDocs
{
	struct FTopic
	{
		FString Id;
		FString Title;
		FString Text;
		/** "guide", "class", "struct", "enum", "editor help". */
		FString Kind;
	};

	struct FHit
	{
		const FTopic* Topic = nullptr;
		float Score = 0.f;
		FString Snippet;
	};

	/** The plugin's Resources/AI folder. */
	FString GetResourcesDir();

	/** The map: the fixed system prompt. Byte-for-byte stable across chats. */
	const FString& GetMap();

	/** Builds the index if it isn't yet. */
	void EnsureIndex();

	/** The best topics for Query, best first. */
	TArray<FHit> Search(const FString& Query, int32 MaxHits = 6);

	/** A topic by id, or by a title close enough to one. Null if none. */
	const FTopic* Find(const FString& IdOrTitle);

	/** Every topic (for the MCP resource list). */
	void GetTopics(TArray<const FTopic*>& OutTopics);

	/** A text file of Resources/AI (prompts, definitions), empty if missing. */
	FString LoadResourceText(const FString& RelativePath);
}
