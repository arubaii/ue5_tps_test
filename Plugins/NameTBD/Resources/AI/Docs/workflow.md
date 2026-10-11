# Working with the AI Assistant

## The chat panel
Window > NameTBD AI opens a dockable chat. Pick a provider and paste your own key in Editor Preferences > Plugins > NameTBD AI (Anthropic, OpenAI, Google Gemini, or any OpenAI-compatible server such as Ollama or LM Studio). Two models: the Generation model builds and edits (Build mode), the cheaper Question model answers questions (Ask mode - read-only tools). The plugin ships no key and runs no server; usage is billed by your provider to you.

## Apply and Discard
Everything a request builds appears in the editor as it is made. When the request ends, a card lists the changes with Apply and Discard. Discard takes all of it back - one undo step - and deletes the assets it created. Apply keeps it (and saves, with Save On Apply). With Confirm Changes off there is no card: Ctrl+Z still takes the whole request back in one step.

## Context
"Attach selection" adds the designer's selected widgets (their non-default settings) to your next message. Without it the model asks for them itself with get_context when you say "this" or "the selected". The assistant remembers the asset it built last, so follow-ups ("make the glow purpler", "move the text to the center", "add a pill button that says Continue") edit it directly.

## Cost
Each request shows its tokens and an estimate (from the Model Prices table; Anthropic's current prices are built in). The fixed part of every request - the map of the plugin and the tool definitions - is cached by the provider, so after the first request it costs about a tenth. Start a new chat for a new task: history is what grows a request.

## MCP server (Claude Desktop, Claude Code, Cursor)
Turn on Start MCP Server in the settings (or press Start Server). It listens on 127.0.0.1 only and needs this project's token. Claude Code: press Copy Claude Code Command and run it. Claude Desktop and other stdio clients: press Copy Claude Desktop Config and merge it into the client's MCP config - it runs the plugin's bridge script with the engine's own Python. The same tools, docs and prompts are offered; each tool call is one undo step, saved right after (MCP Save After Writes).

## Measuring models (materials)
Every validate_material / build_ui_material call is counted per model in Saved/NameTBDAI/stats.json: materials built, how many worked on the first try, the repairs made and which mistakes each model makes. Resources/AI/Evals/run_material_eval.py runs a fixed set of 40 material prompts through any provider against the open editor (MCP server on, Allow Writes on) and reports the first-try rate; --validate-only checks graphs without building them.

## What is sent
Only to the provider you picked: the map, tool definitions, your messages and what the tools return (settings of the widgets or assets asked about, doc topics). Never the whole project. Preview capture (off by default) sends an image of the widget built.
