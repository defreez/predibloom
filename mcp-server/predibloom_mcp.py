#!/usr/bin/env python3
"""MCP server for predibloom GUI automation."""

import socket
import json
import asyncio
from mcp.server import Server
from mcp.types import Tool, TextContent

# Create MCP server
server = Server("predibloom")

SOCKET_PATH = "/tmp/predibloom.sock"


def send_command(cmd: dict) -> dict:
    """Send command to predibloom GUI and get response."""
    try:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(SOCKET_PATH)

        # Send command as JSON with newline
        cmd_json = json.dumps(cmd) + "\n"
        sock.send(cmd_json.encode())

        # Receive response
        response = sock.recv(4096).decode()
        sock.close()

        return json.loads(response)
    except Exception as e:
        return {"status": "error", "message": str(e)}


@server.list_tools()
async def list_tools() -> list[Tool]:
    """List available tools."""
    return [
        Tool(
            name="predibloom_screenshot",
            description="Capture screenshot of predibloom GUI window",
            inputSchema={
                "type": "object",
                "properties": {
                    "path": {
                        "type": "string",
                        "description": "Output path for screenshot (PNG)",
                        "default": ".output/screenshots/screenshot.png"
                    }
                }
            }
        ),
        Tool(
            name="predibloom_click_button",
            description="Click a button by its ID (use list_buttons to see available buttons)",
            inputSchema={
                "type": "object",
                "properties": {
                    "button_id": {
                        "type": "string",
                        "description": "Button ID (e.g., 'tab_Politics', 'tab_Climate', 'refresh')"
                    }
                },
                "required": ["button_id"]
            }
        ),
        Tool(
            name="predibloom_list_buttons",
            description="List all available buttons with their IDs",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        ),
        Tool(
            name="predibloom_scroll",
            description="Simulate mouse wheel scroll in the market list",
            inputSchema={
                "type": "object",
                "properties": {
                    "delta": {
                        "type": "number",
                        "description": "Scroll delta (positive=up, negative=down)"
                    }
                },
                "required": ["delta"]
            }
        ),
        Tool(
            name="predibloom_get_state",
            description="Get current GUI state (selected market, scroll position, etc.)",
            inputSchema={
                "type": "object",
                "properties": {}
            }
        )
    ]


@server.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Execute tool and return result."""

    # Remove "predibloom_" prefix to get command name
    cmd_name = name.replace("predibloom_", "")

    # Build command
    cmd = {"cmd": cmd_name, **arguments}

    # Send command to GUI
    result = send_command(cmd)

    # Format response
    if result.get("status") == "ok":
        if cmd_name == "screenshot":
            path = result.get("path", arguments.get("path", "unknown"))
            return [TextContent(
                type="text",
                text=f"Screenshot saved to {path}"
            )]
        elif cmd_name == "get_state":
            state = result.get("state", {})
            return [TextContent(
                type="text",
                text=json.dumps(state, indent=2)
            )]
        elif cmd_name == "list_buttons":
            buttons = result.get("buttons", [])
            lines = ["Available buttons:"]
            for btn in buttons:
                status = " (selected)" if btn.get("is_selected") else ""
                btn_type = "tab" if btn.get("is_tab") else "button"
                lines.append(f"  - {btn['id']}: {btn['label']} [{btn_type}]{status}")
            return [TextContent(
                type="text",
                text="\n".join(lines)
            )]
        else:
            return [TextContent(
                type="text",
                text=f"Command '{cmd_name}' executed successfully"
            )]
    else:
        error_msg = result.get("message", "Unknown error")
        return [TextContent(
            type="text",
            text=f"Error: {error_msg}"
        )]


async def main():
    """Run the MCP server."""
    from mcp.server.stdio import stdio_server

    async with stdio_server() as (read_stream, write_stream):
        await server.run(
            read_stream,
            write_stream,
            server.create_initialization_options()
        )


if __name__ == "__main__":
    asyncio.run(main())
