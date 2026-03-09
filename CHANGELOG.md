# Changelog

All notable changes to Monolith will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.5.2] - 2026-03-09

Wave 2: Blueprint expansion, Material export controls, Niagara HLSL auto-compile, and discover param schemas.

### Added

- **Blueprint** — `get_graph_summary` action: lightweight graph overview (id/class/title + exec connections only, ~10KB vs 172KB)
- **Blueprint** — `get_graph_data` now accepts optional `node_class_filter` param
- **Material** — `export_material_graph` now accepts `include_properties` (bool) and `include_positions` (bool) params
- **Material** — `get_thumbnail` now accepts `save_to_file` (bool) param
- **All** — Per-action param schemas in `monolith.discover()` output — all 122 actions now self-document their params

### Fixed

- **Blueprint** — `get_variables` now reads default values from CDO (was always empty)
- **Blueprint** — BlueprintIndexer CDO fix — same default value extraction applied to indexer
- **Niagara** — `get_compiled_gpu_hlsl` auto-compiles system if HLSL not available
- **Niagara** — `User.` prefix now stripped in `get_parameter_value`, `trace_parameter_binding`, `remove_user_parameter`, `set_parameter_default`

### Changed

- **Blueprint** — Action count 5 -> 6
- **Total** — Action count 121 -> 122

## [0.5.1] - 2026-03-09

Indexer reliability, Niagara usability, and Animation accuracy fixes.

### Fixed

- **Indexer** — Auto-index deferred to `IAssetRegistry::OnFilesLoaded()` — was running too early, only indexing 193 of 9560 assets
- **Indexer** — Sanity check: if fewer than 500 assets indexed, skip writing `last_full_index` so next launch retries
- **Indexer** — `bIsIndexing` reset in `Deinitialize()` to prevent stuck flag across editor sessions
- **Indexer** — Index DB changed from WAL to DELETE journal mode
- **Niagara** — `trace_parameter_binding` missing OR fallback for `User.` prefix
- **Niagara** — `get_di_functions` reversed class name pattern — now tries `UNiagaraDataInterface<Name>`
- **Niagara** — `batch_execute` had 3 op name mismatches — old names kept as aliases
- **Animation** — State machine names stripped of `\n` suffix (clean names like "InAir" instead of "InAir\nState Machine")
- **Animation** — `get_state_info` now validates required params (`machine_name`, `state_name`)
- **Animation** — State machine matching changed from fuzzy `Contains()` to exact match

### Added

- **Niagara** — `list_emitters` action: returns emitter names, index, enabled, sim_target, renderer_count
- **Niagara** — `list_renderers` action: returns renderer class, index, enabled, material
- **Niagara** — All actions now accept `asset_path` (preferred) with `system_path` as backward-compat alias
- **Niagara** — `duplicate_emitter` accepts `emitter` as alias for `source_emitter`
- **Niagara** — `set_curve_value` accepts `module_node` as alias for `module`
- **Animation** — `get_nodes` now accepts optional `graph_name` filter (makes `get_blend_nodes` redundant for filtered queries)

### Changed

- **Niagara** — Action count 39 → 41
- **Total** — Action count 119 → 121

## [0.5.0] - 2026-03-08

Auto-updater rewrite — fixes all swap script failures on Windows.

### Fixed

- **Auto-Updater** — Swap script now polls `tasklist` for `UnrealEditor.exe` instead of a cosmetic 10-second countdown (was launching before editor fully exited)
- **Auto-Updater** — `errorlevel` check after retry rename was unreachable due to cmd.exe resetting `%ERRORLEVEL%` on closing `)` — replaced with `goto` pattern
- **Auto-Updater** — Launcher script now uses outer-double-quote trick for `cmd /c` paths with spaces (`D:\Unreal Projects\...`)
- **Auto-Updater** — Switched from `ren` (bare filename only) to `move` (full path support) for plugin folder rename
- **Auto-Updater** — Retry now cleans stale backup before re-attempting rename
- **Auto-Updater** — Rollback on failed xcopy now removes partial destination before restoring backup
- **Auto-Updater** — Added `/h` flag to primary xcopy to include hidden-attribute files
- **Auto-Updater** — Enabled `DelayedExpansion` for correct variable expansion inside `if` blocks

## [0.2.0] - 2026-03-08

Source indexer overhaul and auto-updater improvements.

### Fixed

- **Source Indexer** — UE macros (UCLASS, ENGINE_API, GENERATED_BODY) now stripped before tree-sitter parsing, fixing class hierarchy and inheritance resolution
- **Source Indexer** — Class definitions increased from ~0 to 62,059; inheritance links from ~0 to 37,010
- **Source Indexer** — `read_source members_only` now returns class members correctly
- **Source Indexer** — `get_class_hierarchy` ancestor traversal now works
- **MonolithSource** — `get_class_hierarchy` accepts both `symbol` and `class_name` params (was inconsistent)

### Added

- **Source Indexer** — UE macro preprocessor (`ue_preprocessor.py`) with balanced-paren stripping for UCLASS/USTRUCT/UENUM/UINTERFACE
- **Source Indexer** — `--clean` flag for `__main__.py` to delete DB before reindexing
- **Source Indexer** — Diagnostic output after indexing (definitions, forward decls, inheritance stats)
- **Auto-Updater** — Release notes now shown in update notification toast and logged to Output Log

### Changed

- **Source Indexer** — `reference_builder.py` now preprocesses source before tree-sitter parsing

### Important

- **You MUST delete your existing source database and reindex** after updating to 0.2.0. The old database has empty class hierarchy data. Delete the `.db` file in your Saved/Monolith/ directory and run the indexer with `--clean`.

## [0.1.0] - 2026-03-07

Initial beta release. One plugin, 9 domains, 119 actions.

### Added

- **MonolithCore** — Embedded Streamable HTTP MCP server with JSON-RPC 2.0 dispatch
- **MonolithCore** — Central tool registry with discovery/dispatch pattern (~14 namespace tools instead of ~117 individual tools)
- **MonolithCore** — Plugin settings via UDeveloperSettings (port, auto-update, module toggles, DB paths)
- **MonolithCore** — Auto-updater via GitHub Releases (download, stage, notify)
- **MonolithCore** — Asset loading with 4-tier fallback (StaticLoadObject -> PackageName.ObjectName -> FindObject+_C -> ForEachObjectWithPackage)
- **MonolithBlueprint** — 6 actions: graph topology, graph summary, variables, execution flow tracing, node search
- **MonolithMaterial** — 14 actions: inspection, graph editing, build/export/import, validation, preview rendering, Custom HLSL nodes
- **MonolithAnimation** — 23 actions: montage sections, blend space samples, ABP graph reading, notify editing, bone tracks, skeleton info
- **MonolithNiagara** — 39 actions: system/emitter management, module stack operations, parameters, renderers, batch execute, declarative system builder
- **MonolithNiagara** — 6 reimplemented NiagaraEditor helpers (Epic APIs not exported)
- **MonolithEditor** — 13 actions: Live Coding build triggers, compile output capture, log ring buffer (10K entries), crash context
- **MonolithConfig** — 6 actions: INI resolution, explain (multi-layer), diff from default, search, section read
- **MonolithIndex** — SQLite FTS5 project indexer with 4 indexers (Blueprint, Material, Generic, Dependency)
- **MonolithIndex** — 5 actions: full-text search, reference tracing, type filtering, stats, asset deep inspection
- **MonolithSource** — Python tree-sitter engine source indexer (C++ and shader parsing)
- **MonolithSource** — 10 actions: source reading, call graphs, class hierarchy, symbol context, module info
- **9 Claude Code skills** — Domain-specific workflow guides for animation, blueprints, build decisions, C++, debugging, materials, Niagara, performance, project search
- **Templates** — `.mcp.json.example` and `CLAUDE.md.example` for quick project setup
- All 9 modules compiling clean on UE 5.7
- **MonolithEditor** — `get_compile_output` action for Live Coding compile result capture with time-windowed error filtering
- **MonolithEditor** — Auto hot-swap on editor exit (stages update, swaps on close)
- **MonolithEditor** — Re-index buttons in Project Settings UI
- **MonolithEditor** — Improved Live Coding integration with compile output capture, time-windowed errors, category filtering
- **unreal-build skill** — Smart build decision-making guide (Live Coding vs full rebuild)
- **Logging** — 80% reduction in Log-level noise across all modules (kept Warnings/Errors, demoted routine logs to Verbose)
- **README** — Complete rewrite with Installation for Dummies walkthrough

### Fixed

- HTTP body null-termination causing malformed JSON-RPC responses
- Niagara graph traversal crash when accessing emitter shared graphs
- Niagara emitter lookup failures — added case-insensitive matching with fallbacks
- Source DB WAL journal mode causing lock contention — switched to DELETE mode
- SQL schema creation with nested BEGIN/END depth tracking for triggers
- Reindex dispatch — switched from `FindFunctionByName` to `StartFullIndex` with UFUNCTION
- Asset loading crash from `FastGetAsset` on background thread — removed unsafe call
- Animation `remove_bone_track` — now uses `RemoveBoneCurve(FName)` per bone with child traversal
- MonolithIndex `last_full_index` — added `WriteMeta()` call, guarded with `!bShouldStop`
- Niagara `move_module` — rewires stack-flow pins only, preserves override inputs
- Editor `get_build_errors` — uses `ELogVerbosity` enum instead of substring matching
- MonolithIndex SQL injection — all 13 insert methods converted to `FSQLitePreparedStatement`
- Animation modules using `LogTemp` instead of `LogMonolith`
- Editor `CachedLogCapture` dangling pointer — added `ClearCachedLogCapture()` in `ShutdownModule`
- MonolithSource vestigial outer module — flattened structure, deleted stub
- Session expiry / reconnection issues — removed session tracking entirely (server is stateless)
- Claude tools failing on first invocation — fixed transport type in `.mcp.json` (`"http"` -> `"streamableHttp"`) and fixed MonolithSource stub not registering actions
