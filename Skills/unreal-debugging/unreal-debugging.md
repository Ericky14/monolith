---
name: unreal-debugging
description: Use when debugging Unreal Engine issues via Monolith MCP — build errors, editor log searching, crash context, Live Coding builds, and common UE error patterns. Triggers on build error, compile error, crash, log, debug, stack trace, assertion.
---

# Unreal Debugging Workflows

You have access to **Monolith** with 13 editor diagnostic actions via `editor.query()`.

## Discovery

```
monolith.discover({ namespace: "editor" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|--------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Materials/M_Rock` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Materials/M_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

**Note:** For project plugins, the path starts with the plugin name as configured in the .uplugin file's "MountPoint" — which defaults to `/<PluginName>/`. Most plugins mount their Content/ folder there directly.

## Action Reference

| Action | Purpose |
|--------|---------|
| `trigger_build` | Trigger a Live Coding compile (use instead of UBT when editor is open) |
| `live_compile` | Alias for `trigger_build`. Params: `wait` (bool, optional) — block until compile finishes |
| `get_build_errors` | Get compile errors/warnings. Params: `since` (float), `category` (string), `compile_only` (bool) |
| `get_build_status` | Check if a build is in progress / succeeded / failed |
| `get_build_summary` | Summary stats across recent builds |
| `search_build_output` | Search build output by pattern |
| `get_recent_logs` | Get the N most recent log entries |
| `search_logs` | Search logs by pattern, category, and verbosity |
| `tail_log` | Get the latest log entries (like `tail -f`) |
| `get_log_categories` | List all active log categories |
| `get_log_stats` | Error/warning/log counts by category |
| `get_compile_output` | Structured compile report: result, time, log lines, error/warning counts, patch status |
| `get_crash_context` | Get crash dump details, stack trace, and system info |

## Debugging Workflow

### After modifying C++ code
```
editor.query({ action: "trigger_build", params: {} })
// Wait ~10 seconds for Live Coding
editor.query({ action: "get_build_status", params: {} })
editor.query({ action: "get_build_errors", params: {} })
```

### Get structured compile results
```
editor.query({ action: "get_compile_output", params: {} })
```

### Investigate a crash
```
editor.query({ action: "get_crash_context", params: {} })
editor.query({ action: "search_logs", params: { pattern: "Fatal", limit: 20 } })
```

### Find specific log output
```
editor.query({ action: "search_logs", params: { pattern: "MyActor", category: "LogTemp", verbosity: "Warning" } })
```

### Check overall log health
```
editor.query({ action: "get_log_stats", params: {} })
editor.query({ action: "get_log_categories", params: {} })
```

## Common UE Error Patterns

### Linker errors (LNK2019 / LNK2001)
- Missing module dependency in `.Build.cs` — check `PublicDependencyModuleNames`
- `DeveloperSettings` is a separate module from `Engine`
- `UObject` constructors must use `ObjectInitializer` signature

### Include path errors
Use source lookup to find the correct header:
```
source.query({ action: "search", params: { query: "FMyStruct", type: "class" } })
source.query({ action: "get_include_path", params: { symbol: "FMyStruct" } })
```

### Live Coding limitations
- **Header changes** (new members, class layout changes) require editor restart + full UBT build
- Live Coding only handles `.cpp` body changes reliably
- After triggering build, wait ~10 seconds before checking status

### Package/Asset errors
- `CreatePackage` with same path returns existing in-memory package — use unique names
- Asset paths use content browser format with no file extension — see Asset Path Conventions above

## Tips

- Log buffer holds 10,000 entries and 5 build histories
- Use `search_logs` with category filters to reduce noise
- `get_build_summary` shows trends across recent builds — useful for spotting regressions
- Combine with `source.query` to look up engine internals when errors reference engine code
