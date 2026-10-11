#!/usr/bin/env python3
# Copyright AR GameSystems 2026. All rights reserved.
"""
Material eval: how often does a model make a working UI material on its first try?

Runs each prompt of material_prompts.json through a model (Anthropic, OpenAI, an OpenAI-compatible endpoint such as
Ollama / LM Studio, or Gemini) with the same map and tools the in-editor chat uses, executing the model's tool calls in the
running Unreal Editor over the NameTBD MCP server - so every graph goes through the real validator and, unless
--validate-only, the real build and shader compile. Per prompt it records the path taken (master instance, master
extended, new graph), whether the first material call worked, whether it ended with a working material, calls and tokens.

The editor also logs every material call per model in <project>/Saved/NameTBDAI/stats.json (this script names itself
"eval:<provider>:<model>" to the server), so runs of several models can be compared there too.

Needs: the project open in the editor with the MCP server started and "MCP Allow Writes" on, and the provider's key in
an environment variable (ANTHROPIC_API_KEY, OPENAI_API_KEY, GEMINI_API_KEY, or --key-env). Assets go to the Output
Folder's Eval/<model> (delete the folder afterwards). Costs are the provider's normal API prices; --only limits the run.

Standard library only. Examples:
    python run_material_eval.py --project "C:/Proj" --provider anthropic --model claude-sonnet-5-5
    python run_material_eval.py --project "C:/Proj" --provider openai-compatible --base-url http://localhost:11434/v1 --model qwen3:14b
    python run_material_eval.py --project "C:/Proj" --provider gemini --model gemini-2.5-flash --only scanlines,crt --validate-only
"""

import argparse
import datetime
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))

# Tools a material request needs; the others only cost tokens here.
MATERIAL_TOOLS = ["list_options", "search_docs", "get_docs", "create_material_instance", "set_material_parameters",
                  "validate_material", "build_ui_material"]

_LOCAL = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # the editor is on this machine: never a proxy


def log(text):
    sys.stderr.write(text + "\n")
    sys.stderr.flush()


# ---- MCP (the editor) ----------------------------------------------------------------------------------------------

class Editor:
    def __init__(self, project, client_name):
        self.project = project
        self.session = None
        self.next_id = 1
        result = self.call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                          "clientInfo": {"name": client_name, "version": "1"}})
        self.instructions = result.get("instructions", "")
        self.notify("notifications/initialized")

    def _post(self, payload):
        path = os.path.join(self.project, "Saved", "NameTBDAI", "mcp.json")
        with open(path, "r", encoding="utf-8") as handle:
            connection = json.load(handle)
        request = urllib.request.Request("http://127.0.0.1:%d/mcp" % int(connection["port"]),
                                         data=json.dumps(payload).encode("utf-8"), method="POST",
                                         headers={"Content-Type": "application/json", "Accept": "application/json",
                                                  "Authorization": "Bearer " + connection["token"]})
        if self.session:
            request.add_header("Mcp-Session-Id", self.session)
        with _LOCAL.open(request, timeout=900) as response:
            self.session = response.headers.get("Mcp-Session-Id") or self.session
            body = response.read().decode("utf-8")
        return json.loads(body) if body.strip() else None

    def call(self, method, params=None):
        self.next_id += 1
        reply = self._post({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}})
        if "error" in reply:
            raise RuntimeError("%s: %s" % (method, reply["error"].get("message")))
        return reply["result"]

    def notify(self, method):
        self._post({"jsonrpc": "2.0", "method": method})

    def tool(self, name, arguments):
        result = self.call("tools/call", {"name": name, "arguments": arguments})
        text = "".join(part.get("text", "") for part in result.get("content", []) if part.get("type") == "text")
        return text, bool(result.get("isError"))


# ---- Providers (non-streaming; each keeps the conversation in its own form) -------------------------------------------

def http_json(url, payload, headers, retries=4):
    data = json.dumps(payload).encode("utf-8")
    for attempt in range(retries + 1):
        request = urllib.request.Request(url, data=data, method="POST", headers=dict(headers, **{"Content-Type": "application/json"}))
        try:
            with urllib.request.urlopen(request, timeout=600) as response:
                return json.loads(response.read().decode("utf-8"))
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", "replace")[:500]
            if error.code in (429, 500, 502, 503, 529) and attempt < retries:
                time.sleep(2 ** (attempt + 1))
                continue
            raise RuntimeError("HTTP %d: %s" % (error.code, body))


class Anthropic:
    def __init__(self, model, key, base_url, system, tools):
        self.url = (base_url or "https://api.anthropic.com") + "/v1/messages"
        self.model, self.key = model, key
        self.system = [{"type": "text", "text": system, "cache_control": {"type": "ephemeral"}}]
        self.tools = [{"name": t["name"], "description": t["description"], "input_schema": t["inputSchema"]} for t in tools]
        self.messages = []

    def start(self, text):
        self.messages = [{"role": "user", "content": text}]

    def step(self):
        reply = http_json(self.url, {"model": self.model, "max_tokens": 16000, "system": self.system, "tools": self.tools,
                                     "messages": self.messages},
                          {"x-api-key": self.key, "anthropic-version": "2023-06-01"})
        content = reply.get("content", [])
        self.messages.append({"role": "assistant", "content": content})
        usage = reply.get("usage", {})
        calls = [(block["id"], block["name"], block.get("input") or {}) for block in content if block.get("type") == "tool_use"]
        text = "".join(block.get("text", "") for block in content if block.get("type") == "text")
        return calls, text, usage.get("input_tokens", 0) + usage.get("cache_read_input_tokens", 0) + usage.get("cache_creation_input_tokens", 0), usage.get("output_tokens", 0)

    def results(self, outcomes):
        self.messages.append({"role": "user", "content": [
            {"type": "tool_result", "tool_use_id": call_id, "content": text, "is_error": is_error} for call_id, _, text, is_error in outcomes]})


class OpenAI:
    def __init__(self, model, key, base_url, system, tools):
        self.url = (base_url or "https://api.openai.com/v1").rstrip("/") + "/chat/completions"
        self.model, self.key = model, key
        self.system = system
        self.tools = [{"type": "function", "function": {"name": t["name"], "description": t["description"], "parameters": t["inputSchema"]}} for t in tools]
        self.messages = []

    def start(self, text):
        self.messages = [{"role": "system", "content": self.system}, {"role": "user", "content": text}]

    def step(self):
        headers = {"Authorization": "Bearer " + self.key} if self.key else {}
        reply = http_json(self.url, {"model": self.model, "messages": self.messages, "tools": self.tools}, headers)
        message = reply["choices"][0]["message"]
        self.messages.append({key: value for key, value in message.items() if key in ("role", "content", "tool_calls")})
        calls = []
        for call in message.get("tool_calls") or []:
            try:
                arguments = json.loads(call["function"].get("arguments") or "{}")
            except ValueError:
                arguments = {"__invalid_json__": call["function"].get("arguments")}
            calls.append((call["id"], call["function"]["name"], arguments))
        usage = reply.get("usage", {})
        return calls, message.get("content") or "", usage.get("prompt_tokens", 0), usage.get("completion_tokens", 0)

    def results(self, outcomes):
        for call_id, _, text, _ in outcomes:
            self.messages.append({"role": "tool", "tool_call_id": call_id, "content": text})


def gemini_schema(schema):
    """Gemini's schema subset: upper-case types, and free-form objects as JSON text (the editor parses it back)."""
    out = {}
    kind = schema.get("type", "string")
    if kind == "object" and not schema.get("properties"):
        return {"type": "STRING", "description": (schema.get("description", "") + " (JSON text)").strip()}
    out["type"] = kind.upper()
    if "description" in schema:
        out["description"] = schema["description"]
    if "enum" in schema:
        out["enum"] = schema["enum"]
    if kind == "object":
        out["properties"] = {name: gemini_schema(field) for name, field in schema["properties"].items()}
        if schema.get("required"):
            out["required"] = schema["required"]
    if kind == "array":
        out["items"] = gemini_schema(schema.get("items", {"type": "string"}))
    return out


class Gemini:
    def __init__(self, model, key, base_url, system, tools):
        self.url = (base_url or "https://generativelanguage.googleapis.com/v1beta") + "/models/%s:generateContent" % model
        self.key = key
        self.system = system
        self.tools = [{"functionDeclarations": [{"name": t["name"], "description": t["description"], "parameters": gemini_schema(t["inputSchema"])} for t in tools]}]
        self.contents = []

    def start(self, text):
        self.contents = [{"role": "user", "parts": [{"text": text}]}]

    def step(self):
        reply = http_json(self.url, {"systemInstruction": {"parts": [{"text": self.system}]}, "tools": self.tools, "contents": self.contents},
                          {"x-goog-api-key": self.key})
        content = (reply.get("candidates") or [{}])[0].get("content") or {"role": "model", "parts": []}
        self.contents.append(content)
        calls = [(part["functionCall"].get("id") or part["functionCall"]["name"], part["functionCall"]["name"], part["functionCall"].get("args") or {})
                 for part in content.get("parts", []) if "functionCall" in part]
        text = "".join(part.get("text", "") for part in content.get("parts", []) if "text" in part)
        usage = reply.get("usageMetadata", {})
        return calls, text, usage.get("promptTokenCount", 0), usage.get("candidatesTokenCount", 0)

    def results(self, outcomes):
        self.contents.append({"role": "user", "parts": [
            {"functionResponse": {"name": name, "response": {"content": text, "is_error": is_error}}} for _, name, text, is_error in outcomes]})


PROVIDERS = {"anthropic": (Anthropic, "ANTHROPIC_API_KEY"), "openai": (OpenAI, "OPENAI_API_KEY"),
             "openai-compatible": (OpenAI, "OPENAI_API_KEY"), "gemini": (Gemini, "GEMINI_API_KEY")}


# ---- One prompt --------------------------------------------------------------------------------------------------

def material_call_kind(name, arguments):
    """instance / extend / graph for a material-making call, else None."""
    if name == "create_material_instance":
        return "instance"
    if name in ("build_ui_material", "validate_material"):
        if arguments.get("hlsl"):
            return "hlsl"
        graph = arguments.get("graph")
        if arguments.get("from_master"):
            return "extend" if graph else "instance"
        return "graph"
    return None


def call_worked(name, text, is_error):
    if is_error:
        return False
    try:
        data = json.loads(text)
    except ValueError:
        return not is_error
    if name == "build_ui_material" and "compiled" in data:
        return bool(data["compiled"])
    if name == "validate_material":
        return bool(data.get("ok"))
    return "error" not in data


def run_prompt(editor, provider, item, folder, max_turns, validate_only):
    provider.start(item["prompt"] + "\n\n(Make the material only - no widget. Save it in the folder %s.)" % folder)
    record = {"id": item["id"], "expect": item.get("expect", "any"), "path": None, "first_try": None, "worked": False,
              "material_calls": 0, "tool_calls": 0, "input_tokens": 0, "output_tokens": 0, "errors": [], "fixed": []}
    for _ in range(max_turns):
        calls, text, tokens_in, tokens_out = provider.step()
        record["input_tokens"] += tokens_in
        record["output_tokens"] += tokens_out
        if not calls:
            record["final_text"] = text[:400]
            break
        outcomes = []
        for call_id, name, arguments in calls:
            record["tool_calls"] += 1
            if "__invalid_json__" in arguments:
                outcomes.append((call_id, name, json.dumps({"error": "The tool input wasn't valid JSON - send it again."}), True))
                if material_call_kind(name, {}) and record["first_try"] is None:
                    record["first_try"] = False
                continue
            if name in ("create_material_instance", "build_ui_material") and not arguments.get("material"):
                arguments.setdefault("folder", folder)
            kind = material_call_kind(name, arguments)
            run_name = name
            if validate_only and name == "build_ui_material" and kind != "instance":
                run_name = "validate_material"  # the graph checked, nothing built
            if validate_only and kind == "instance":
                text_out, is_error = json.dumps({"material": "/Game/Eval/MI_%s" % item["id"], "note": "validate-only run: no asset made"}), False
            else:
                try:
                    text_out, is_error = editor.tool(run_name, arguments)
                except Exception as error:  # the editor refused or broke: the call failed
                    text_out, is_error = json.dumps({"error": str(error)}), True
            outcomes.append((call_id, name, text_out, is_error))
            if kind:
                worked = call_worked(run_name, text_out, is_error)
                record["material_calls"] += 1
                record["path"] = record["path"] or kind
                if record["first_try"] is None:
                    record["first_try"] = worked
                if worked and name != "validate_material":  # a material was made (or, validate-only, would have been)
                    record["worked"] = True
                try:
                    data = json.loads(text_out)
                    record["errors"] += data.get("errors", []) + data.get("compile_errors", [])
                    record["fixed"] += data.get("fixed", [])
                except ValueError:
                    pass
        provider.results(outcomes)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", required=True, help="The Unreal project's folder (holding the .uproject).")
    parser.add_argument("--provider", required=True, choices=sorted(PROVIDERS))
    parser.add_argument("--model", required=True)
    parser.add_argument("--base-url", help="API base URL (openai-compatible: e.g. http://localhost:11434/v1).")
    parser.add_argument("--key-env", help="Environment variable holding the API key (default per provider).")
    parser.add_argument("--prompts", default=os.path.join(HERE, "material_prompts.json"))
    parser.add_argument("--only", help="Comma-separated prompt ids.")
    parser.add_argument("--max-turns", type=int, default=8)
    parser.add_argument("--validate-only", action="store_true", help="Check graphs with validate_material instead of building them (no assets, no shader compiles).")
    args = parser.parse_args()

    project = os.path.abspath(args.project)
    provider_class, key_env = PROVIDERS[args.provider]
    key = os.environ.get(args.key_env or key_env, "")
    if not key and args.provider != "openai-compatible":
        sys.exit("Set %s to the provider's API key." % (args.key_env or key_env))
    slug = re.sub(r"[^A-Za-z0-9]+", "_", args.model).strip("_")
    folder = "Eval/" + slug

    editor = Editor(project, "eval:%s:%s" % (args.provider, args.model))
    tools = [tool for tool in editor.call("tools/list")["tools"] if tool["name"] in MATERIAL_TOOLS]
    missing = sorted(set(MATERIAL_TOOLS) - {tool["name"] for tool in tools})
    if missing:
        sys.exit("The editor doesn't offer %s - turn on MCP Allow Writes (Editor Preferences > Plugins > NameTBD AI)." % ", ".join(missing))

    with open(args.prompts, "r", encoding="utf-8") as handle:
        prompts = json.load(handle)["prompts"]
    if args.only:
        wanted = set(args.only.split(","))
        prompts = [item for item in prompts if item["id"] in wanted]

    records = []
    for index, item in enumerate(prompts, 1):
        provider = provider_class(args.model, key, args.base_url, editor.instructions, tools)
        started = time.time()
        try:
            record = run_prompt(editor, provider, item, folder, args.max_turns, args.validate_only)
        except Exception as error:
            record = {"id": item["id"], "expect": item.get("expect"), "path": None, "first_try": False, "worked": False, "failure": str(error)}
        record["seconds"] = round(time.time() - started, 1)
        records.append(record)
        log("[%d/%d] %-16s path=%-8s first_try=%-5s worked=%-5s calls=%s" % (index, len(prompts), item["id"], record.get("path"),
            record.get("first_try"), record.get("worked"), record.get("material_calls")))

    attempted = [record for record in records if record.get("path")]
    summary = {
        "model": args.model, "provider": args.provider, "validate_only": args.validate_only,
        "prompts": len(records), "made_a_material": len(attempted),
        "first_try": sum(1 for record in attempted if record.get("first_try")),
        "worked": sum(1 for record in records if record.get("worked")),
        "paths": {path: sum(1 for record in attempted if record["path"] == path) for path in ("instance", "extend", "hlsl", "graph")},
        # a new material ("graph" in the prompts) may be written in HLSL or as a node graph
        "path_as_expected": sum(1 for record in attempted if record.get("expect") in ("any", record["path"])
                                or (record.get("expect") == "graph" and record["path"] == "hlsl")),
        "input_tokens": sum(record.get("input_tokens", 0) for record in records),
        "output_tokens": sum(record.get("output_tokens", 0) for record in records),
    }
    if attempted:
        summary["first_try_rate"] = round(summary["first_try"] / len(attempted), 3)
    out_dir = os.path.join(project, "Saved", "NameTBDAI", "evals")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "%s_%s.json" % (slug, datetime.datetime.now().strftime("%Y%m%d_%H%M%S")))
    with open(out_path, "w", encoding="utf-8") as handle:
        json.dump({"summary": summary, "results": records}, handle, indent=1)
    print(json.dumps(summary, indent=1))
    log("Full results: %s" % out_path)


if __name__ == "__main__":
    main()
