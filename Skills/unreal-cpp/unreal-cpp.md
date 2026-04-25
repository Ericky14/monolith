---
name: unreal-cpp
description: Use when writing or debugging Unreal Engine C++ code via Monolith MCP — engine API lookup, signature verification, include paths, source reading, class hierarchies, config resolution. Triggers on C++, header, include, UCLASS, UFUNCTION, UPROPERTY, Build.cs, linker error.
---

# Unreal C++ Development Workflows

**11 source actions** via `source_query()`, **6 config actions** via `config_query()`.

```
monolith_discover({ namespace: "source" })
monolith_discover({ namespace: "config" })
```

## Source Actions

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `search_source` | `query` | Find symbols across engine source |
| `read_source` | `symbol` | Read engine source for a symbol |
| `get_class_hierarchy` | `symbol` | Inheritance tree |
| `find_callers` | `symbol` | Who calls this function |
| `find_callees` | `symbol` | What this function calls |
| `find_references` | `symbol` | All references to a symbol |
| `get_module_info` | `symbol` | Module dependencies, build type |
| `get_symbol_context` | `symbol` | Definition + surrounding context |
| `read_file` | `file_path` | Raw engine source file |
| `trigger_reindex` | -- | Full engine source re-index |
| `trigger_project_reindex` | -- | Incremental project-only re-index |

## Common Workflows

```
// Find and read an API
source_query({ action: "search_source", params: { query: "ApplyDamage" } })
source_query({ action: "read_source", params: { symbol: "UGameplayStatics::ApplyDamage" } })

// Learn idiomatic usage from Epic's code
source_query({ action: "find_callers", params: { symbol: "UPrimitiveComponent::SetCollisionEnabled" } })

// Resolve config/CVar
config_query({ action: "resolve_setting", params: { file: "DefaultEngine", section: "/Script/Engine.RendererSettings", key: "r.Lumen.TraceMeshSDFs" } })
config_query({ action: "explain_setting", params: { setting: "r.DefaultFeature.AntiAliasing" } })
```

## Build.cs Gotchas

| Error | Fix |
|-------|-----|
| `LNK2019` for `UDeveloperSettings` | Add `"DeveloperSettings"` module (separate from `Engine`) |
| `LNK2019` for any UE type | Check module with `get_module_info`, add to Build.cs |
| Missing `#include` | Use `search_source` to find correct header -- never guess |
| Template instantiation | Check if type needs `_API` export macro |

## UE 5.7 Notes

- `FSkinWeightInfo`: `uint16` for `InfluenceWeights` (not uint8), `FBoneIndexType` for bones
- `CreatePackage` with same path returns existing in-memory package -- use unique names
- Live Coding: `.cpp` body changes only -- header changes require editor restart + UBT build

## Rules

- **Never guess** `#include` paths or signatures -- always verify with `source_query`
- Search action is `search_source` (not `search`)
- Source index: engine Runtime/Editor/Developer + plugins + shaders (1M+ symbols)
- Use `find_callers` for idiomatic usage, `get_symbol_context` for quick definition lookup
- Use `config_query("explain_setting")` before changing unfamiliar CVars
- Non-existent actions: `get_include_path`, `get_function_signature`, `get_deprecation_warnings`
