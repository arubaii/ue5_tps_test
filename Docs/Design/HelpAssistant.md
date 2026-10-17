# Help Assistant (planned - not built)

A free, local, **read-only** chatbot in the editor that answers questions about NameTBD, the user's own project and Unreal Engine, the way help bots on documentation websites do. It is separate from the AI Assistant (`AIAssistant.md`): it has no tools that change anything, needs no API key, and sends nothing off the machine. Build it once the plugin is finished. Nothing in this file is implemented yet.

> **Standing expectation (the user's own words):** "There could be more use cases that users desire that I haven't thought of, but would be expected in a production-grade application."

## Approach: retrieval, not training
No model is trained or fine-tuned. A small ready-made local model answers from passages looked up at question time (retrieval-augmented generation, RAG). The reasons:
- A fine-tuned model goes stale with every plugin change and still makes things up. An index is rebuilt in seconds and the answer can cite where it came from.
- The project and engine are different for every user, so they can only be indexed on the user's machine anyway.

Pipeline: question -> **query rewrite** (expands names and pulls in recent chat context) -> **hybrid search** (BM25 keywords, which match exact class and property names, plus embedding vectors for meaning, merged by reciprocal rank) -> optional rerank -> top chunks, within a token budget, go to the **local model** -> answer with **citations** (clickable).

## Knowledge sources
Each chunk keeps its source, scope (Plugin / Project / Engine), path, line range, symbol name and content hash.

| Scope | Source | When indexed | Notes |
|---|---|---|---|
| Plugin | `Resources/AI/Docs/*.md`, `Docs/Design/*.md`, reflection of the plugin's classes / structs / enums with their comments, `NameTBDTooltips.h` | Prebuilt when the plugin is released, shipped in the plugin | Reuses `NameTBDAIDocs`, which already builds the topics and a BM25 index. Plugin answers rank first. |
| Project: C++ | `Source/**/*.h/.cpp` of the open project and of the project's own plugins | On the user's machine, in the background, incrementally | Chunks follow code structure (class, function, UPROPERTY with its comment), not a fixed size. Adds a symbol table: who derives from what, where a name is declared. |
| Project: assets | Asset Registry: Widget Blueprints (widget tree, variables, functions, events), Blueprints (parents, components, graphs as text summaries), Data Assets, Info Card / Context Menu / Menu Style / Settings Schema assets, config `.ini` | Background, kept current through Asset Registry and package-saved events | Read through the existing read tools' code (`NameTBDAITools_Read.cpp`: describe_widget_blueprint, get_asset...) used in a read-only mode. Never loads assets in bulk on the game thread. |
| Engine: reflection | Reflection of engine classes, structs, enums, properties and functions with their tooltips | On the user's machine, once per engine version, shared by every project on that version | Small and exact for the installed version. On by default. |
| Engine: headers | Engine and engine plugin public headers (`Engine/Source/**/Public`, `Engine/Plugins/**/Public`) | Opt-in, on the user's machine, once per engine version | Large (hundreds of thousands of chunks, about 1 GB). Adds exact signatures, `meta` specifiers and undocumented APIs. **Never shipped, uploaded or shared** (see "Legal"). |
| Engine: online docs | Epic's documentation on dev.epicgames.com | Opt-in, live at question time only, pending Epic's answer (see "Legal") | A few pages per question, rate-limited, used for that answer only and not stored. The answer links to each page. Fallback with no network request: a "Search Epic's docs for this" button that opens Epic's docs search in the user's browser (`FPlatformProcess::LaunchURL`) with a query the assistant fills in. |
| Live context | Selected asset, open editor, selected widget, current Output Log errors | At question time | "Why is this widget not showing?" works on what the user is looking at. |

**Can it read the project, C++ and UE? Yes.** It all happens on the user's machine. The code it reads never leaves the machine and is never part of any training. Costs: the first project index takes minutes, and a full engine-header index takes much longer and roughly 1 GB of disk. That's why engine headers are opt-in and indexed in the background with progress in a notification.

**Source priority** (ranking and what is on by default):
1. Plugin docs and the user's project (always on).
2. UE reflection data (always on).
3. Epic's online docs (opt-in, live, cited with links; the browser button is always available).
4. Full engine headers (opt-in, for C++ and source-build users).

## Legal
- **Engine headers:** the UE EULA restricts redistributing Engine Code and Engine Tools; distribution is only allowed through Fab or a fork of Epic's GitHub. Indexing the user's own licensed install on their own machine redistributes nothing. Rule: the plugin never ships a prebuilt engine index, and never uploads or shares one.
- **Epic's online docs:** no explicit permission for automated access was found. Fab's terms ban scrapers, and Epic's Online Services Acceptable Use Policy only allows access by means Epic expressly authorizes. Rules: never crawl, bulk-download, cache or ship the docs. Live fetch stays off by default and is only enabled if Epic allows it in writing. Until then, only the browser button ships.
- **Request sent** (Unreal Engine contact form, licensing team) asking about both. Record Epic's written answer here when it comes, and follow it.

## Model runtime
- **Planned runtime:** llama.cpp (MIT), vendored under `Source/ThirdParty/llama` like nanosvg, run in a separate helper process (`NameTBDHelpHost`). A crash or out-of-memory error in the model can't take the editor down, and the editor never waits on generation.
- **Models:** an instruct model of about 1.5-4B parameters (GGUF, 4-bit, about 1-2.5 GB) plus a small embedding model (about 30-150 MB). Choose the defaults by licence (Apache/MIT only, redistributable) and by the eval below. Weights are **not** in the plugin download. "Download assistant model" in settings fetches them with a checksum, and a custom GGUF path is allowed.
- **Hardware tiers:** GPU offload when one is available, otherwise CPU. Below a RAM threshold, fall back to Search-only mode. Unload the model after it has been idle for a while.
- **Existing local servers:** a user who already runs Ollama or LM Studio can point the helper at it. The OpenAI-Compatible provider of the AI Assistant already speaks that protocol, so share that code.

## Modes
1. **Search only** (always available, no model): ranked passages with jump links. Instant, and it never invents anything.
2. **Answer:** the local model writes an answer from the passages, citing each claim. If the passages don't contain the answer, it says so and shows the closest passages instead of guessing.
3. **Hand-off:** when the AI Assistant is configured, "Do this for me" sends the question and the found context to it. That's the only way anything gets changed.

## Editor integration (where it fits)
- **Module:** a new Editor module `NameTBDHelp`, with the helper process as its own program target. It depends on `NameTBDAI` only for the shared docs index (`NameTBDAIDocs`) and the OpenAI-compatible client, or moves those into a small shared module. It must work without any AI Assistant settings filled in.
- **UI:** a dockable "NameTBD Help" tab (Window menu, plus a toolbar button). A "?" next to plugin Details categories and a right-click "Ask Help about this" on assets, widgets and C++ symbols, which opens the tab with the context attached.
- **Citations open things:** a doc section, an asset (Content Browser sync / open editor), a C++ file and line (through the editor's source-code accessor, so it opens in the user's IDE), or a class in the Details panel.
- **Settings:** Editor Preferences > Plugins > NameTBD Help (per-user). Covers scopes to index (Plugin / Project / Engine), included and excluded folders (respecting `.gitignore`), model paths, GPU layers, context size, and a privacy note.
- **AI-facing docs:** the existing `Resources/AI/Docs` stay the main plugin source. Gaps the eval finds are fixed there, which helps both assistants.

## Beyond the request
Added for the production-grade expectation:
- Citations with clickable sources, and an "I don't know" rule instead of guessing.
- Search-only mode for machines that can't run a model, and as an instant first answer.
- Live editor context (selection, open asset, recent log errors) attached to the question.
- Incremental re-indexing on file and asset save. The engine index is shared between projects of the same engine version.
- Index scope controls and exclusions, so private folders, generated code and `Intermediate/` / `Saved/` are skipped by default.
- Answers labelled by version ("NameTBD 1.x, UE 5.7"). The plugin index is matched to the installed plugin version.
- Thumbs up / down on answers, saved locally only, as material for the eval. Copy-answer and copy-code buttons.
- Chat history per project, with a "clear" action.
- Localisation: the model answers in the editor's language. The docs stay in English.
- Accessibility: keyboard-only use, and text that scales with the editor's font settings.

## Still open
- Epic's answer on live documentation fetching and on indexing engine headers (see "Legal"). Also check by hand: `dev.epicgames.com/robots.txt`, the docs site's footer terms, and the current EULA at `unrealengine.com/eula`.
- Choosing the default instruct and embedding models: licence check, and the eval below on CPU-only and low-RAM machines.
- Eval set: about 100-200 real questions (plugin "how do I", project-specific, engine API), each with the source it should cite. Measure retrieval hit rate and the share of answers that are correct and cited.
- Vector store format (a flat file with brute-force cosine is enough for the plugin and project. The engine scope may need an approximate-nearest-neighbour index such as HNSW).
- Blueprint graphs as text: how much of a graph to summarise into a chunk.
- Fab / Marketplace rules on shipping a helper executable and an optional download. Code signing on macOS and Windows.
- Linux and macOS builds of the helper. Metal and Vulkan offload.
- Whether `NameTBDAIDocs` moves to a shared module or `NameTBDHelp` depends on `NameTBDAI`.

## Unverified
- Model speed, answer quality, memory use and engine-index size are estimates, not measured.
- Whether reflection metadata alone (without engine headers) is enough for most engine questions.
- Whether llama.cpp builds cleanly as a UE ThirdParty module on all target platforms (the separate-process design avoids most linking issues).
