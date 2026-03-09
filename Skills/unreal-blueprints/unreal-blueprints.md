---
name: unreal-blueprints
description: Use when working with Unreal Engine Blueprints via Monolith MCP — reading graph topology, inspecting variables, tracing execution flow, searching nodes, or understanding Blueprint architecture. Triggers on Blueprint, BP, event graph, node, variable, function graph.
---

# Unreal Blueprint Workflows

You have access to **Monolith** with 6 Blueprint introspection actions via `blueprint.query()`.

## Discovery

Always discover available actions first:
```
monolith.discover({ namespace: "blueprint" })
```

## Key Parameter Names

- `asset_path` — the Blueprint asset path (NOT `asset`)
- `graph_name` — graph name (returned by `list_graphs`)
- `entry_point` — entry point for execution flow tracing

## Action Reference

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `list_graphs` | `asset_path` | List all event/function/macro graphs in a Blueprint |
| `get_graph_summary` | `asset_path`, `graph_name` | Lightweight graph overview — node counts by type, entry points, no full payload |
| `get_graph_data` | `asset_path`, `graph_name`, `node_class_filter`? | Full node topology — pins, connections, positions. Optional `node_class_filter` to return only specific node types |
| `get_variables` | `asset_path` | Variables with types, real default values, categories, replication |
| `get_execution_flow` | `asset_path`, `graph_name`, `entry_point` | Trace execution wires from entry to terminal nodes |
| `search_nodes` | `asset_path`, `query` | Find nodes by class name, display name, or comment |

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|---------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Blueprints/BP_Player` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Blueprints/BP_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

## Common Workflows

### Understand a Blueprint's structure
```
blueprint.query({ action: "list_graphs", params: { asset_path: "/Game/Blueprints/BP_Enemy" } })
blueprint.query({ action: "get_variables", params: { asset_path: "/Game/Blueprints/BP_Enemy" } })
```

### Trace logic flow
```
blueprint.query({ action: "get_execution_flow", params: { asset_path: "/Game/Blueprints/BP_Enemy", graph_name: "EventGraph" } })
```

### Find where a function is called
```
blueprint.query({ action: "search_nodes", params: { asset_path: "/Game/Blueprints/BP_Enemy", query: "TakeDamage" } })
```

### Find Blueprints across the project
Use the project index to locate BPs before inspecting them:
```
project.query({ action: "search", params: { query: "BP_Enemy", type: "Blueprint" } })
```

## Tips

- **Use `get_graph_summary` first** to understand structure, then `get_graph_data` with `node_class_filter` for specific node types — avoids 172KB+ payloads
- **Graph names** are returned by `list_graphs` — use exact names for drill-down calls
- The primary asset param is `asset_path` (not `asset`), and graph param is `graph_name` (not `graph`)
- **Pin connections** in `get_graph_data` show both execution (white) and data (colored) wires
- **Execution flow** traces only follow white exec pins — data flow is shown in graph data
- **Variables** include replication flags (`Replicated`, `RepNotify`) and `EditAnywhere`/`BlueprintReadOnly` specifiers — `get_variables` now returns real default values
- For C++ parent class analysis, combine with `source.query("get_class_hierarchy", ...)`
