# Predibloom Project Instructions

## GUI Testing via MCP

The GUI can be tested via MCP tools that communicate over a Unix socket.

**Setup:**
1. User starts GUI with control socket: `./predibloom --control`
2. This creates `/tmp/predibloom.sock` for MCP communication
3. MCP server (`mcp-server/predibloom_mcp.py`) connects to this socket

**Available MCP tools:**

- `predibloom_screenshot` - Capture screenshot (raylib saves to cwd, so screenshots go to repo root, e.g. `screenshot.png`)
- `predibloom_click` - Simulate click at x,y coordinates
- `predibloom_scroll` - Scroll market list (positive=up, negative=down)
- `predibloom_get_state` - Get JSON state (num_markets, selected_market_idx, scroll_offset, etc.)

**Testing workflow:**
1. Ask user to start: `./build/predibloom --control`
2. Use `predibloom_get_state` to check connection and state
3. Use `predibloom_screenshot` to see current UI
4. Use Read tool to view the screenshot file


## Build Commands

```bash
cd build && cmake .. && make
```

## Project Structure

- `src/api/` - Kalshi API client
- `src/core/` - Service layer and config
- `src/gui/` - GUI application (raylib)
- `src/cli/` - CLI tool
- `src/ui/` - UI theme/styling
- `mcp-server/` - MCP server for GUI automation

## Config

Location: `~/.config/predibloom/config.json`

```json
{
  "tracked": [
    {
      "series_ticker": "KXHORMUZNORM",
      "label": "Strait of Hormuz"
    }
  ]
}
```

The GUI loads this on startup and filters markets by the first tracked series.
