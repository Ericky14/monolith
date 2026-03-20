# Monolith

**One plugin. Every Unreal domain. Zero dependencies.**

[![UE 5.7+](https://img.shields.io/badge/Unreal-5.7%2B-blue)](https://unrealengine.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![MCP](https://img.shields.io/badge/Protocol-MCP-purple)](https://modelcontextprotocol.io)

---

## What is Monolith?

Monolith is an Unreal Engine editor plugin that gives your AI full read/write access to your project through the [Model Context Protocol (MCP)](https://modelcontextprotocol.io). Install one plugin, point your AI client at one endpoint, and it can work with Blueprints, Materials, Animation, Niagara, project configuration, and more.

It works with **Claude Code**, **Cursor**, or any MCP-compatible client. If your AI tool speaks MCP, it speaks Monolith.

> **Platform:** Windows only. Mac and Linux support is coming soon.

## Why Monolith?

Most MCP integrations register every action as a separate tool, which floods the AI's context window and buries the actually useful stuff. Monolith uses a **namespace dispatch pattern** instead: each domain exposes a single `{namespace}_query(action, params)` tool, and a central `monolith_discover()` call lists everything available. Small tool list (12 tools), massive capability (291 actions across nine domains). The AI gets oriented fast and spends its context on your actual problem.

## What Can It Actually Do?

**Blueprint** — The AI has full read/write access to every part of a Blueprint. It can read graph topology, trace execution flow, inspect variables, components, functions, and interfaces. It can create new Blueprints from any parent class, add/remove/rename variables and components, build out functions and event dispatchers, wire up nodes, set pin defaults, implement interfaces, reparent, compile, and validate. Hand it a description and it'll build the whole Blueprint. Or give it an existing one and it'll surgically change what you need.

**Material** — Create materials and instances from scratch, add and connect expression nodes, set scalar/vector/texture parameters, build full PBR graphs programmatically, recompile, validate for errors, and read compiled shader stats. "Give me a worn leather material with edge wear and a normal map input" is a reasonable thing to ask.

**Animation** — Inspect and modify animation sequences, montages, blend spaces, Animation Blueprints, state machines, skeletons, and PoseSearch databases. Add/remove notifies, edit montage sections, create blend space samples, trace ABP state transitions. The whole animation pipeline is in scope.

**Niagara** — Create particle systems from specs, add/remove emitters and modules, set module inputs and bindings, configure data interfaces and renderers, edit parameters, read compiled GPU HLSL, and batch-execute multiple operations atomically. "Build me a blood splatter system that responds to hit direction" — genuinely doable.

**Editor** — Trigger full UBT builds or Live Coding compiles, read build errors and compiler output, search editor logs, get crash context after failures, query editor state. The AI can compile your code and diagnose the failure without you touching the editor.

**Config** — Read, search, and diff INI files with full resolution chain awareness (Base → Platform → Project → User). Ask what a setting does, where it's overridden, and what the effective value is. Good for performance tuning sessions where you want the AI to just sort out the INIs.

**Project** — Full-text search across every indexed asset. Find assets by name, type, path, or content. Trace references between assets. The index updates automatically as assets change and covers marketplace/Fab plugin content too.

**Source** — Look up any Unreal Engine C++ API: read function implementations, search engine source, get class hierarchies, trace call graphs, verify include paths. The native C++ indexer runs automatically — no Python, no setup. The AI never has to guess at a function signature.

---

## Features

- **Full Blueprint read/write/CRUD** — Create Blueprints, edit variables/components/functions/nodes, compile and validate. Read CDO properties from any Blueprint or DataAsset.
- **Material graph editing** — Read, build, and validate material graphs with preview support
- **Animation coverage** — Montages, blend spaces, ABP state machines, skeletons, bone tracks
- **Niagara particle systems** — Create and edit systems, emitters, modules, parameters, renderers, and HLSL
- **Editor integration** — Build triggers, log capture, compile output, crash context, Live Coding support
- **Config management** — INI resolution, diff, search, and explain
- **Deep project search** — SQLite FTS5 full-text search across all indexed assets, including marketplace and Fab plugin content. Configurable additional content paths.
- **Engine source intelligence** — Native C++ indexer (no Python required) with call graphs, class hierarchy, and cross-references. Optional project C++ source indexing for richer results.
- **Auto-updater** — Checks GitHub Releases on editor startup, one-click update
- **Claude Code skills** — Domain-specific workflow guides bundled with the plugin
- **Pure C++** — Direct UE API access, embedded Streamable HTTP server, zero external dependencies

---

## Installation

### Prerequisites

- **Unreal Engine 5.7+** — Launcher or source build ([unrealengine.com](https://unrealengine.com))

> **Platform:** Windows only. Mac and Linux support is coming soon.

- **Claude Code, Cursor, or another MCP client** — Any tool that supports the Model Context Protocol
- **(Optional) Python 3.10+** — Only needed if you want to index your own project's C++ source. Engine source indexing is built-in and needs no Python.

### Step 1: Drop Monolith into your project

Every Unreal project has a `Plugins/` folder. If yours doesn't have one yet, create it next to your `.uproject` file:

```
YourProject/
  YourProject.uproject
  Content/
  Source/
  Plugins/          <-- here
    Monolith/
```

**Option A: Git clone (recommended)**

```bash
cd YourProject/Plugins
git clone https://github.com/tumourlove/monolith.git Monolith
```

**Option B: Download ZIP**

Grab the latest release from [GitHub Releases](https://github.com/tumourlove/monolith/releases), extract it, and drop the folder at `YourProject/Plugins/Monolith/`. The release ZIP includes precompiled DLLs — Blueprint-only projects can open the editor immediately without rebuilding. C++ projects should rebuild first.

**Option C: Let your AI do it**

If you're already in a Claude Code, Cursor, or Cline session, just say:

> "Install the Monolith plugin from https://github.com/tumourlove/monolith into my project's Plugins folder"

It'll clone the repo, create `.mcp.json`, and configure everything. After it's done, **restart your AI session** so it picks up the new `.mcp.json`, then skip to [Step 3](#step-3-open-the-editor).

### Step 2: Create `.mcp.json`

This file tells your AI client where to find Monolith's MCP server. Create it in your **project root** — same directory as your `.uproject`:

```
YourProject/
  YourProject.uproject
  .mcp.json          <-- create this
  Plugins/
    Monolith/
```

**For Cursor / Cline:**

```json
{
  "mcpServers": {
    "monolith": {
      "type": "streamableHttp",
      "url": "http://localhost:9316/mcp"
    }
  }
}
```

**For Claude Code:**

```json
{
  "mcpServers": {
    "monolith": {
      "type": "http",
      "url": "http://localhost:9316/mcp"
    }
  }
}
```

> **Heads up:** Claude Code uses `"http"`, Cursor and Cline use `"streamableHttp"`. Wrong type = connection failure. It's a common gotcha.

Or just copy the template that ships with Monolith:

```bash
cp Plugins/Monolith/Templates/.mcp.json.example .mcp.json
```

### Step 3: Open the editor

Open your `.uproject` as normal. On first launch:

1. Monolith auto-indexes your project (30-60 seconds depending on size — go get a coffee)
2. Open the **Output Log** (Window > Developer Tools > Output Log)
3. Filter for `LogMonolith` — you'll see the server start up and the index complete

When you see `Monolith MCP server listening on port 9316`, you're in business.

### Step 4: Connect your AI

1. Open **Claude Code** (or your MCP client) from your project directory — the one with `.mcp.json`
2. Claude Code auto-detects `.mcp.json` on startup and connects to Monolith
3. Sanity check: ask *"What Monolith tools do you have?"*

You should get back a list of namespace tools (`blueprint_query`, `material_query`, etc.). If you do, everything's working.

### Step 5: (Optional) Index your project's C++ source

Engine source indexing is automatic — `source_query` works immediately with no setup.

If you also want your AI to search your **own project's C++ source** (find callers, callees, and class hierarchies across your own code):

1. Install **Python 3.10+**
2. Run `python Plugins/Monolith/Scripts/index_project.py` from your project root
3. Your project source gets indexed into `EngineSource.db` alongside engine symbols
4. To re-run the indexer without leaving the editor: `source_query("trigger_project_reindex")`

### Verify it's alive

With the editor running, hit this from any terminal:

```bash
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

You'll get a JSON response listing all Monolith tools. If you get "connection refused", the editor isn't running or something went sideways — check the Output Log for `LogMonolith` errors.

### (Optional) Install Claude Code skills

Monolith ships domain-specific workflow skills for Claude Code:

```bash
cp -r Plugins/Monolith/Skills/* ~/.claude/skills/
```

---

## Architecture

```
Monolith.uplugin
  MonolithCore          — HTTP server, tool registry, discovery, auto-updater (4 actions)
  MonolithBlueprint     — Blueprint read/write, variable/component/graph CRUD, node operations, compile, CDO reader (67 actions)
  MonolithMaterial      — Material inspection + graph editing + CRUD (47 actions)
  MonolithAnimation     — Animation sequences, montages, ABPs, PoseSearch (74 actions)
  MonolithNiagara       — Niagara particle systems (64 actions)
  MonolithEditor        — Build triggers, log capture, compile output, crash context (17 actions)
  MonolithConfig        — Config/INI resolution and search (6 actions)
  MonolithIndex         — SQLite FTS5 deep project indexer, marketplace content, 15 asset indexers (5 actions)
  MonolithSource        — Native C++ engine source indexer, call graphs, class hierarchy (10 actions)
```

**291 actions total across 9 modules, exposed through 12 MCP tools.**

### Tool Reference

| Namespace | Tool | Actions | Description |
|-----------|------|---------|-------------|
| `monolith` | `monolith_discover` | — | List available actions per namespace |
| `monolith` | `monolith_status` | — | Server health, version, index status |
| `monolith` | `monolith_reindex` | — | Trigger full project re-index |
| `monolith` | `monolith_update` | — | Check or install updates |
| `blueprint` | `blueprint_query` | 67 | Full Blueprint CRUD — read/write graphs, variables, components, functions, nodes, compile, CDO properties |
| `material` | `material_query` | 47 | Inspection, editing, graph building, previews, validation, CRUD |
| `animation` | `animation_query` | 74 | Montages, blend spaces, ABPs, skeletons, bone tracks, PoseSearch, IKRig, Control Rig |
| `niagara` | `niagara_query` | 64 | Systems, emitters, modules, parameters, renderers, HLSL, dynamic inputs, simulation stages |
| `editor` | `editor_query` | 17 | Build triggers, error logs, compile output, crash context, scene capture, texture import |
| `config` | `config_query` | 6 | INI resolution, explain, diff, search |
| `project` | `project_query` | 5 | Deep project search — FTS5 across all indexed assets including marketplace plugins |
| `source` | `source_query` | 10 | Native C++ engine source lookup, call graphs, class hierarchy, project reindex |

---

## Auto-Updater

Monolith checks for new versions on editor startup so you don't have to babysit GitHub.

1. **On editor startup** — Checks GitHub Releases for a newer version
2. **Downloads and stages** — If an update is found, it downloads and stages the new version
3. **Auto-swaps on exit** — The plugin is replaced when you close the editor
4. **Manual check** — `monolith_update` tool to check anytime
5. **Disable** — Toggle off in **Editor Preferences > Plugins > Monolith**

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **Plugin doesn't appear in editor** | Verify the folder is at `YourProject/Plugins/Monolith/` and contains `Monolith.uplugin`. Check you're on UE 5.7+. |
| **MCP connection refused** | Make sure the editor is open and running. Check Output Log for port conflicts. Verify `.mcp.json` is in your project root. |
| **Index shows 0 assets** | Run `monolith_reindex` or restart the editor. Check Output Log for indexing errors. |
| **Source tools return empty results** | Run `monolith_reindex()` and wait for completion, then retry. Engine source indexing is built-in — if results are still empty, check the Output Log for `LogMonolith` errors. |
| **Claude can't find any tools** | Check `.mcp.json` transport type: Claude Code uses `"http"`, Cursor/Cline use `"streamableHttp"`. Restart your AI client after creating the file. |
| **Tools fail on first try** | Restart Claude Code to refresh the MCP connection. Known quirk with initial connection timing. |
| **Port 9316 already in use** | Change the port in Editor Preferences > Plugins > Monolith, then update `.mcp.json` to match. |
| **Mac/Linux not working** | Windows only for now. Mac and Linux are planned. |

---

## Configuration

Settings live at **Editor Preferences > Plugins > Monolith**:

| Setting | Default | Description |
|---------|---------|-------------|
| MCP Server Port | `9316` | Port for the embedded HTTP server |
| Auto-Update | `On` | Check GitHub Releases on editor startup |
| Module Toggles | All enabled | Enable/disable individual domain modules |
| Database Path | Project-local | Override SQLite database storage location |
| Index Marketplace Plugins | `On` | Index content from installed marketplace/Fab plugins |
| Index Data Assets | `On` | Deep-index DataAsset subclasses (15 indexers) |
| Additional Content Paths | `[]` | Extra content paths to include in the project index |

---

## Skills

Monolith bundles 9 Claude Code skills in `Skills/` — domain-specific workflow guides that give your AI the right mental model for each area:

| Skill | Description |
|-------|-------------|
| `unreal-blueprints` | Full Blueprint CRUD — read, create, edit variables/components/functions/nodes, compile |
| `unreal-materials` | PBR setup, graph building, validation |
| `unreal-animation` | Montages, ABP state machines, blend spaces |
| `unreal-niagara` | Particle system creation, HLSL modules, scalability |
| `unreal-debugging` | Build errors, log search, crash context |
| `unreal-performance` | Config auditing, shader stats, INI tuning |
| `unreal-project-search` | FTS5 search syntax, reference tracing |
| `unreal-cpp` | API lookup, include paths, Build.cs gotchas |
| `unreal-build` | Smart build decision-making, Live Coding vs full rebuild |

---

## Documentation

- [API_REFERENCE.md](Docs/API_REFERENCE.md) — Full action reference with parameters
- [SPEC.md](Docs/SPEC.md) — Technical specification and design decisions
- [CONTRIBUTING.md](CONTRIBUTING.md) — Dev setup, coding conventions, PR process
- [CHANGELOG.md](CHANGELOG.md) — Version history and release notes
- [Wiki](https://github.com/tumourlove/monolith/wiki) — Installation guides, test status, FAQ

---

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for dev environment setup, coding conventions, how to add new actions, and the PR process.

---

## License

[MIT](LICENSE) — See [ATTRIBUTION.md](ATTRIBUTION.md) for credits.
