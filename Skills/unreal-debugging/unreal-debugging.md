---
name: unreal-debugging
description: Use when debugging Unreal Engine issues via Monolith MCP — build errors, editor log searching, crash context, Live Coding builds, and common UE error patterns. Triggers on build error, compile error, crash, log, debug, stack trace, assertion.
---

# Unreal Debugging Workflows

**13 editor diagnostic actions** via `editor_query()`. Discover with `monolith_discover({ namespace: "editor" })`.

## Action Reference

| Action | Purpose |
|--------|---------|
| `trigger_build` / `live_compile` | Live Coding compile. `live_compile` accepts `wait` (bool) |
| `get_build_errors` | Compile errors/warnings. Params: `since`, `category`, `compile_only` |
| `get_build_status` | Build in progress / succeeded / failed |
| `get_build_summary` | Stats across recent builds |
| `search_build_output` | Search build output by pattern |
| `get_recent_logs` | N most recent log entries |
| `search_logs` | Search by pattern, category, verbosity |
| `tail_log` | Latest log entries (like `tail -f`) |
| `get_log_categories` | All active log categories |
| `get_log_stats` | Error/warning counts by category |
| `get_compile_output` | Structured compile report: result, time, errors, patch status |
| `get_crash_context` | Crash dump, stack trace, system info |

## Workflows

### After modifying C++
```
editor_query({ action: "trigger_build", params: {} })
// Wait ~10s for Live Coding
editor_query({ action: "get_build_errors", params: {} })
```

### Investigate a crash
```
editor_query({ action: "get_crash_context", params: {} })
editor_query({ action: "search_logs", params: { pattern: "Fatal", limit: 20 } })
```

### Find specific log output
```
editor_query({ action: "search_logs", params: { pattern: "MyActor", category: "LogTemp", verbosity: "Warning" } })
```

## Common Error Patterns

- **LNK2019/LNK2001:** Missing module in `.Build.cs`. `DeveloperSettings` is separate from `Engine`.
- **Include path errors:** Use `source_query("search_source", ...)` to find correct header. Note: `get_include_path` does NOT exist as an action.
- **Live Coding limits:** Header changes (new members, class layout) require editor restart + UBT build. Only `.cpp` body changes work.
- **Package errors:** `CreatePackage` with same path returns existing in-memory package.

## Tips

- Log buffer: 10,000 entries, 5 build histories
- Use `search_logs` with category filters to reduce noise
- `get_build_summary` shows trends -- useful for spotting regressions
- Combine with `source_query` for engine internal errors
