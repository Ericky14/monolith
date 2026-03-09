---
name: unreal-cpp
description: Use when writing or debugging Unreal Engine C++ code via Monolith MCP — engine API lookup, signature verification, include paths, source reading, class hierarchies, config resolution. Triggers on C++, header, include, UCLASS, UFUNCTION, UPROPERTY, Build.cs, linker error.
---

# Unreal C++ Development Workflows

You have access to **Monolith** with 10 source actions via `source.query()` and 6 config actions via `config.query()`.

## Discovery

```
monolith.discover({ namespace: "source" })
monolith.discover({ namespace: "config" })
```

## Source Actions (10)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `search_source` | `query` | Find symbols (classes, functions, structs) across engine source |
| `read_source` | `symbol` | Read actual engine source code for a symbol |
| `get_class_hierarchy` | `symbol` | Class inheritance tree |
| `find_callers` | `symbol` | Who calls this function in engine code |
| `find_callees` | `symbol` | What does this function call |
| `find_references` | `symbol` | All references to a symbol |
| `get_module_info` | `symbol` | Module dependencies, build type |
| `get_symbol_context` | `symbol` | Get a symbol's definition and surrounding context |
| `read_file` | `file_path` | Read raw engine source file by path |
| `trigger_reindex` | — | Trigger a re-index of engine source |

## Key Parameter Names

- `symbol` — the symbol to look up (e.g., `"ACharacter"`, `"UGameplayStatics::ApplyDamage"`)
- `query` — search query string (for `search_source`)
- `file_path` — file path for `read_file`

## Common Workflows

### Find and read an API
```
source.query({ action: "search_source", params: { query: "ApplyDamage" } })
source.query({ action: "read_source", params: { symbol: "UGameplayStatics::ApplyDamage" } })
```

### Get symbol context (definition + surrounding code)
```
source.query({ action: "get_symbol_context", params: { symbol: "UCharacterMovementComponent::PhysWalking" } })
```

### Understand how Epic uses an API
```
source.query({ action: "find_callers", params: { symbol: "UPrimitiveComponent::SetCollisionEnabled" } })
```

### Explore a class hierarchy
```
source.query({ action: "get_class_hierarchy", params: { symbol: "ACharacter" } })
```

### Read engine implementation details
```
source.query({ action: "read_source", params: { symbol: "UCharacterMovementComponent::PhysWalking" } })
```

### Resolve config/CVar values
```
config.query({ action: "resolve_setting", params: { file: "DefaultEngine", section: "/Script/Engine.RendererSettings", key: "r.Lumen.TraceMeshSDFs" } })
config.query({ action: "explain_setting", params: { setting: "r.DefaultFeature.AntiAliasing" } })
```

## Build.cs Gotchas

Common linker errors and their fixes:

| Error | Fix |
|-------|-----|
| `LNK2019` unresolved external for `UDeveloperSettings` | Add `"DeveloperSettings"` to Build.cs — it's a separate module from `Engine` |
| `LNK2019` for any UE type | Check module with `source.query("get_module_info", ...)` and add to Build.cs |
| Missing `#include` | Use `source.query("search_source", ...)` to find the correct header — never guess include paths |
| Template instantiation errors | Check if the type needs explicit export (`_API` macro) |

## UE 5.7 API Notes

- `FSkinWeightInfo` uses `uint16` for `InfluenceWeights` (not `uint8`) and `FBoneIndexType` for bones
- `CreatePackage` with same path returns existing in-memory package — use unique names
- Live Coding only handles `.cpp` body changes — header changes require editor restart + full UBT build

## Tips

- **Never guess** `#include` paths or function signatures — always verify with `source.query`
- The search action is `search_source` (not `search`)
- The source index covers engine Runtime, Editor, Developer modules + plugins + shaders (1M+ symbols)
- Use `find_callers` to learn idiomatic usage patterns from Epic's own code
- Use `get_symbol_context` for quick definition lookup without reading the full source
- Combine `source.query` (engine) with project-level search for full picture
- Use `config.query("explain_setting")` before changing any unfamiliar CVar
- Non-existent actions: `get_include_path`, `get_function_signature`, `get_deprecation_warnings` — these do NOT exist
