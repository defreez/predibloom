# Predibloom MCP Server

MCP (Model Context Protocol) server that provides GUI automation tools for predibloom.

## Installation

```bash
cd mcp-server
pip install -e .
```

## Usage

### 1. Start predibloom with control mode enabled

```bash
cd build
./predibloom --control &
```

This opens the GUI and listens on `/tmp/predibloom.sock` for automation commands.

### 2. Configure Claude Desktop

Add to your `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "predibloom": {
      "command": "python",
      "args": ["/absolute/path/to/predibloom/mcp-server/predibloom_mcp.py"]
    }
  }
}
```

### 3. Use in Claude

Claude will have access to these tools:

- `predibloom_screenshot` - Capture GUI screenshots
- `predibloom_click` - Simulate clicks at coordinates
- `predibloom_scroll` - Simulate scrolling
- `predibloom_get_state` - Get current app state
- `predibloom_wait` - Wait for frames to render

## Manual Testing

You can test the socket manually with netcat:

```bash
# Get current state
echo '{"cmd":"get_state"}' | nc -U /tmp/predibloom.sock

# Take screenshot
echo '{"cmd":"screenshot","path":".output/test.png"}' | nc -U /tmp/predibloom.sock

# Click at coordinates
echo '{"cmd":"click","x":200,"y":100}' | nc -U /tmp/predibloom.sock

# Scroll
echo '{"cmd":"scroll","delta":-3}' | nc -U /tmp/predibloom.sock

# Wait 10 frames
echo '{"cmd":"wait","frames":10}' | nc -U /tmp/predibloom.sock
```

## Architecture

```
┌─────────────────┐    Unix Socket    ┌──────────────────┐
│  GUI App        │◄─────────────────►│  MCP Server      │
│  (predibloom    │  JSON commands     │  (Python)        │
│   --control)    │  JSON responses    │                  │
└─────────────────┘                    └──────────────────┘
                                               ▲
                                               │
                                         Claude calls
                                         MCP tools
```

## Example Workflow

```python
# Claude's workflow:

# 1. Take initial screenshot
predibloom_screenshot(path=".output/screenshots/initial.png")

# 2. Click on a market (coordinates based on layout)
predibloom_click(x=200, y=100)
predibloom_wait(frames=5)

# 3. Take screenshot of detail view
predibloom_screenshot(path=".output/screenshots/detail.png")

# 4. Check state
predibloom_get_state()
# Returns: {"selected_market_idx": 0, "scroll_offset": 0, ...}

# 5. Scroll down
predibloom_scroll(delta=-3)
predibloom_wait(frames=2)
predibloom_screenshot(path=".output/screenshots/after_scroll.png")
```
