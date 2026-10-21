#!/usr/bin/env python3
"""Local stdio MCP query server over an explicitly selected immutable Perfetto trace."""
import argparse
import json
from pathlib import Path
import sys
from profiling_tools import DEFAULT_CACHE, query


def serve(trace, cache):
    """Handle newline-delimited MCP JSON-RPC; stdout is protocol only."""
    initialized = False
    for line in sys.stdin:
        request = None
        try:
            if len(line) > 1 << 20:
                raise ValueError("Request exceeds 1 MiB")
            request = json.loads(line)
            method, identity = request.get("method"), request.get("id")
            if request.get("jsonrpc") != "2.0":
                raise ValueError("Expected JSON-RPC 2.0")
            if method == "notifications/initialized":
                continue
            if method == "initialize":
                initialized = True
                result = {"protocolVersion": "2024-11-05", "capabilities": {"tools": {}},
                          "serverInfo": {"name": "monsterengine-perfetto", "version": "1.0.0"}}
            elif not initialized:
                raise ValueError("initialize is required")
            elif method == "ping":
                result = {}
            elif method == "tools/list":
                result = {"tools": [{"name": "perfetto_query", "description": "Bounded read-only SELECT on the selected local trace. Rows are strings; ns remain exact.",
                         "inputSchema": {"type": "object", "properties": {"sql": {"type": "string"},
                                          "maxRows": {"type": "integer", "minimum": 1, "maximum": 250000}},
                                          "required": ["sql"], "additionalProperties": False}}]}
            elif method == "tools/call":
                params = request.get("params", {})
                if params.get("name") != "perfetto_query":
                    raise ValueError("Unknown tool")
                arguments = params.get("arguments", {})
                try:
                    value = query(trace, arguments["sql"], cache, arguments.get("maxRows", 10000))
                    result = {"content": [{"type": "text", "text": json.dumps(value)}], "isError": False}
                except (OSError, KeyError, TypeError, ValueError, RuntimeError) as error:
                    result = {"content": [{"type": "text", "text": str(error)}], "isError": True}
            else:
                raise ValueError("Unsupported method")
            if identity is not None:
                print(json.dumps({"jsonrpc": "2.0", "id": identity, "result": result}), flush=True)
        except (ValueError, TypeError, AttributeError) as error:
            print(json.dumps({"jsonrpc": "2.0", "id": request.get("id") if isinstance(request, dict) else None,
                              "error": {"code": -32600, "message": str(error)}}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", required=True, type=Path)
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    args = parser.parse_args()
    try:
        if not args.trace.is_file():
            raise ValueError("Trace must be an existing local file")
        serve(args.trace, args.cache)
        return 0
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
