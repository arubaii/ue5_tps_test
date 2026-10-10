#!/usr/bin/env python3
# Copyright AR GameSystems 2026. All rights reserved.
"""
NameTBD MCP bridge: lets stdio MCP clients (Claude Desktop, Cursor, ...) talk to the NameTBD MCP server running inside the
Unreal Editor.

The editor serves MCP over HTTP on 127.0.0.1 (Editor Preferences > Plugins > NameTBD AI > MCP Server). This script reads
JSON-RPC messages from stdin, one per line, posts each to the editor with the project's token, and writes the editor's
answers to stdout, one per line. Port and token are read from <project>/Saved/NameTBDAI/mcp.json, which the editor writes
when the server starts - so the client config never holds the token, and a regenerated token is picked up by itself.

Standard library only; runs with the Python that ships with the engine. Usage:
    python nametbd_mcp_bridge.py --project "C:/Path/To/MyProject"
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request

# Never through a proxy: the server is on this machine.
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def _log(message):
    sys.stderr.write("[nametbd-mcp-bridge] %s\n" % message)
    sys.stderr.flush()


def _read_connection(project):
    path = os.path.join(project, "Saved", "NameTBDAI", "mcp.json")
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return int(data["port"]), str(data["token"])
    except (OSError, ValueError, KeyError) as error:
        raise RuntimeError(
            "No connection file at %s (%s). Open the project in the Unreal Editor and start the MCP server "
            "(Editor Preferences > Plugins > NameTBD AI > Start Server)." % (path, error))


def _error_reply(message_id, text):
    return {"jsonrpc": "2.0", "id": message_id, "error": {"code": -32000, "message": text}}


def _forward(project, body, session):
    port, token = _read_connection(project)
    request = urllib.request.Request(
        "http://127.0.0.1:%d/mcp" % port,
        data=body.encode("utf-8"),
        method="POST",
        headers={
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "Authorization": "Bearer " + token,
        },
    )
    if session.get("id"):
        request.add_header("Mcp-Session-Id", session["id"])
    # Tools can take a while (a material compiling its shaders, a Niagara system compiling).
    with _OPENER.open(request, timeout=600) as response:
        session_id = response.headers.get("Mcp-Session-Id")
        if session_id:
            session["id"] = session_id
        return response.read().decode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", required=True, help="The Unreal project's folder (holding the .uproject).")
    args = parser.parse_args()
    project = os.path.abspath(args.project)
    session = {}
    _log("forwarding to the Unreal Editor for %s" % project)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        message_id = None
        try:
            parsed = json.loads(line)
            if isinstance(parsed, dict):
                message_id = parsed.get("id")
        except ValueError:
            parsed = None

        try:
            answer = _forward(project, line, session)
        except urllib.error.HTTPError as error:
            text = error.read().decode("utf-8", "replace")
            if error.code == 401:
                text = "The editor refused the token: press Regenerate Token or restart the MCP server, then try again."
            answer = json.dumps(_error_reply(message_id, "HTTP %d: %s" % (error.code, text))) if message_id is not None else ""
        except (urllib.error.URLError, RuntimeError, OSError) as error:
            reason = getattr(error, "reason", error)
            answer = json.dumps(_error_reply(
                message_id, "The Unreal Editor's NameTBD MCP server isn't reachable (%s). Is the editor open with the server started?" % reason)) \
                if message_id is not None else ""

        answer = answer.strip()
        if answer:
            sys.stdout.write(answer.replace("\n", " ") + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
