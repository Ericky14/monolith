# Monolith — TODO

Last updated: 2026-03-09

---

## Bugs (fix these first)

### Critical

None! All critical bugs resolved.

### Moderate

None! All moderate bugs resolved.

### Minor

- [x] **Material `validate_material` false positive islands** — FIXED (2026-03-09). Added MP_MaterialAttributes + 6 missing properties to AllMaterialProperties, seeded BFS from UMaterialExpressionMaterialAttributeLayers. 0 false positives on standard materials. Layer-blend materials still have a known limitation (implicit layer system connections not traversable via pin graph).
- [x] **Blueprint `get_execution_flow` matches comments before events** — FIXED (2026-03-09). Two-pass FindEntryNode: Pass 1 checks events/functions (prefers K2Node_Event, K2Node_FunctionEntry), Pass 2 is fuzzy fallback that skips EdGraphNode_Comment.

---

## Unimplemented Features (stubs in code)

- [ ] **Niagara `create_module_from_hlsl`** — BLOCKED (Epic APIs). Returns error: "HLSL script creation requires Python bridge (NiagaraEditor internal APIs not exported)." Would need either Epic to export APIs or a Python subprocess workaround.
  - **File:** `Source/MonolithNiagara/Private/MonolithNiagaraActions.cpp`

- [ ] **Niagara `create_function_from_hlsl`** — BLOCKED (Epic APIs). Same as above. Both delegate to `CreateScriptFromHLSL` which always returns error.

- [ ] **SSE streaming** — DEFERRED. `MonolithHttpServer.cpp` SSE endpoint returns a single event and closes. Comment: "Full SSE streaming will be implemented when we need server-initiated notifications."
  - **File:** `Source/MonolithCore/Private/MonolithHttpServer.cpp` (~line 232)

- [x] **Python indexer: capture full class/struct definitions** — FIXED (2026-03-08). Added UE macro preprocessor that strips UCLASS/USTRUCT/UENUM/UINTERFACE, *_API, GENERATED_BODY() before tree-sitter parsing. 62,059 definitions now captured (was near-zero).

- [x] **Source index: ancestor traversal** — FIXED (2026-03-08). Inheritance table now has 37,010 entries across 34,444 classes. AActor→UObject, APawn→AActor, ACharacter→APawn all working.

---

## Feature Improvements

### Platform

- [ ] **Mac/Linux support** — DEFERRED (Windows-only project). All build-related actions are `#if PLATFORM_WINDOWS` guarded. Live Coding is Windows-only. Update system is Windows-only.

---

## Documentation

- [ ] **CI pipeline** — Per Phase 6 plan

---

## Completed

- [x] Core infrastructure (HTTP server, registry, settings, JSON utils, asset utils)
- [x] All 9 domain modules compiling clean on UE 5.7
- [x] SQLite FTS5 project indexer with 14 indexers (Blueprint, Material, Generic, Dependency, Animation, Niagara, DataTable, Level, GameplayTag, Config, Cpp, UserDefinedEnum, UserDefinedStruct, InputAction)
- [x] Python tree-sitter engine source indexer
- [x] Auto-updater via GitHub Releases
- [x] 9 Claude Code skills (including unreal-build)
- [x] Templates (.mcp.json, CLAUDE.md)
- [x] README, LICENSE, ATTRIBUTION
- [x] HTTP body null-termination fix
- [x] Niagara graph traversal fix (emitter shared graph)
- [x] Niagara emitter lookup hardening (case-insensitive + fallbacks)
- [x] Source DB WAL -> DELETE journal mode fix
- [x] Asset loading 4-tier fallback
- [x] SQL schema creation (BEGIN/END depth tracking for triggers)
- [x] Reindex dispatch fix (FindFunctionByName -> StartFullIndex + UFUNCTION)
- [x] Asset loading crash fix (removed FastGetAsset from background thread)
- [x] Animation `remove_bone_track` — now uses `RemoveBoneCurve(FName)` per bone + child traversal (2026-03-07)
- [x] MonolithIndex `last_full_index` — added `WriteMeta()` call, guarded with `!bShouldStop` (2026-03-07)
- [x] Niagara `move_module` — rewires stack-flow pins only, preserves override inputs (2026-03-07)
- [x] Editor `get_build_errors` — uses `ELogVerbosity` enum instead of substring matching (2026-03-07)
- [x] MonolithIndex SQL injection — all 13 insert methods converted to `FSQLitePreparedStatement` (2026-03-07)
- [x] Animation `LogTemp` -> `LogMonolith` (2026-03-07)
- [x] Editor `CachedLogCapture` dangling pointer — added `ClearCachedLogCapture()` in ShutdownModule (2026-03-07)
- [x] MonolithSource vestigial outer module — flattened structure, deleted stub (2026-03-07)
- [x] Session expiry / reconnection — Removed session tracking entirely. Sessions stored no per-session state and only caused bugs when server restarted. Server is now fully stateless. (2026-03-07)
- [x] Claude tools fail on first invocation — Fixed transport type mismatch in .mcp.json ("http" → "streamableHttp") and fixed MonolithSource stub that wasn't registering actions. (2026-03-07)
- [x] Module enable toggles — settings now checked before registering actions (2026-03-07)
- [x] MCP package CLI cleanup — removed abandoned scaffold (2026-03-07)
- [x] Material action count alignment — skill updated to match C++ reality (2026-03-07)
- [x] Animation action count alignment — skill updated to match C++ reality (2026-03-07)
- [x] Niagara action count alignment — skill updated to match C++ reality (2026-03-07)
- [x] Config action schema documentation — `explain_setting` convenience mode documented in skill (2026-03-07)
- [x] Niagara `reorder_emitters` safety — proper change notifications added (2026-03-07)
- [x] `diff_from_default` INI parsing — rewritten with GConfig/FConfigCacheIni (2026-03-07)
- [x] Config `diff_from_default` enhancement — now compares all 5 config layers (2026-03-07)
- [x] Live Coding trigger action (`editor.live_compile`) — fully implemented (2026-03-07)
- [x] Cross-platform update system — tar/unzip support added (2026-03-07)
- [x] Hot-swap plugin updates — delayed file swap mechanism implemented (2026-03-07)
- [x] Remove phase plan .md files from Source/ — moved to Docs/plans/ (2026-03-07)
- [x] AnimationIndexer — AnimSequence, AnimMontage, BlendSpace indexing (2026-03-07)
- [x] NiagaraIndexer — NiagaraSystem, NiagaraEmitter deep indexing (2026-03-07)
- [x] DataTableIndexer — DataTable row indexing (2026-03-07)
- [x] LevelIndexer — Level/World actor indexing (2026-03-07)
- [x] GameplayTagIndexer — Tag hierarchy indexing (2026-03-07)
- [x] ConfigIndexer — INI config indexing (2026-03-07)
- [x] CppIndexer — C++ symbol indexing (2026-03-07)
- [x] Deep asset indexing — safe game-thread loading strategy implemented (2026-03-07)
- [x] Incremental indexing — delta updates from file change detection (2026-03-07)
- [x] Asset change detection — hooked into Asset Registry callbacks (2026-03-07)
- [x] API reference page — auto-generated API_REFERENCE.md with 119 actions (2026-03-07)
- [x] Contribution guide — CONTRIBUTING.md created (2026-03-07)
- [x] Changelog — CHANGELOG.md created (2026-03-07)
- [x] Clean up MCP/ package — removed abandoned CLI scaffold (2026-03-07)
- [x] `find_callers` / `find_callees` param name fix — `"function"` → `"symbol"` (2026-03-07)
- [x] `read_file` param name fix — `"path"` → `"file_path"` (2026-03-07)
- [x] `read_file` path normalization — forward slash → backslash for DB suffix matching (2026-03-07)
- [x] `get_class_hierarchy` forward-declaration filtering — prefer real definitions over `class X;` (2026-03-07)
- [x] `ExtractMembers` rewrite — brace depth tracking for Allman-style UE code (2026-03-07)
- [x] `get_recent_logs` — accepts both `"max"` and `"count"` param names (2026-03-07)
- [x] `search_config` category filter — changed param read from `"file"` to `"category"` (2026-03-07)
- [x] `get_section` category name resolution — accepts `"Engine"` not just `"DefaultEngine"` (2026-03-07)
- [x] SQLite WAL → DELETE — belt-and-suspenders fix: C++ forces DELETE on open + Python indexer never sets WAL (2026-03-07)
- [x] Source DB ReadOnly → ReadWrite — WAL + ReadOnly silently returns 0 rows on Windows (2026-03-07)
- [x] Reindex absolute path — `FPaths::ConvertRelativePathToFull()` on engine source + shader paths (2026-03-07)
- [x] MonolithHttpServer top-level param merge — params alongside `action` were silently dropped, now merged (2026-03-07)
- [x] UE macro preprocessor — strips UCLASS/USTRUCT/UENUM/UINTERFACE, *_API, GENERATED_BODY() before tree-sitter parsing (2026-03-08)
- [x] Source indexer --clean flag — deletes existing DB before reindexing (2026-03-08)
- [x] Inheritance resolution — 37,010 links across 34,444 classes, full ancestor chains working (2026-03-08)
- [x] Diagnostic counters — definitions/forward_decls/with_base_classes/inheritance_resolved/failed printed after indexing (2026-03-08)
- [x] Preprocessor in ReferenceBuilder — consistent AST for cross-reference extraction (2026-03-08)
- [x] Auto-updater rewrite — tasklist polling, move retry loop 10x3s, errorlevel fix, cmd /c quoting, DelayedExpansion, xcopy /h, rollback rmdir. Windows end-to-end tested v0.4.0→v0.5.0 (2026-03-08)
- [x] Release script `Scripts/make_release.ps1` — sets `"Installed": true` in zip for BP-only users (2026-03-08)
- [x] BP-only support — release zips work without rebuild for Blueprint-only projects (2026-03-08)
- [x] GitHub Wiki — 11 pages: Installation, Tool Reference, Test Status, Auto-Updater, FAQ, Changelog, etc. (2026-03-08)
- [x] Indexer auto-index deferred to `IAssetRegistry::OnFilesLoaded()` — was running too early, only indexing 193/9560 assets (2026-03-09)
- [x] Indexer sanity check — if < 500 assets indexed, skip writing `last_full_index` so next launch retries (2026-03-09)
- [x] Indexer `bIsIndexing` reset in `Deinitialize()` to prevent stuck flag (2026-03-09)
- [x] Index DB changed from WAL to DELETE journal mode (2026-03-09)
- [x] Niagara `trace_parameter_binding` — fixed missing OR fallback for `User.` prefix (2026-03-09)
- [x] Niagara `get_di_functions` — fixed reversed class name pattern, now tries `UNiagaraDataInterface<Name>` (2026-03-09)
- [x] Niagara `batch_execute` — fixed 3 op name mismatches, old names kept as aliases (2026-03-09)
- [x] Niagara actions now accept `asset_path` (preferred) with `system_path` as backward-compat alias (2026-03-09)
- [x] Niagara `duplicate_emitter` accepts `emitter` as alias for `source_emitter` (2026-03-09)
- [x] Niagara `set_curve_value` accepts `module_node` as alias for `module` (2026-03-09)
- [x] NEW: Niagara `list_emitters` action — returns emitter names, index, enabled, sim_target, renderer_count (2026-03-09)
- [x] NEW: Niagara `list_renderers` action — returns renderer class, index, enabled, material (2026-03-09)
- [x] Animation state machine names stripped of `\n` — clean names like "InAir" not "InAir\nState Machine" (2026-03-09)
- [x] Animation `get_state_info` validates required params (machine_name, state_name) (2026-03-09)
- [x] Animation state machine matching changed from fuzzy Contains() to exact match (2026-03-09)
- [x] Animation `get_nodes` now accepts optional `graph_name` filter (2026-03-09)
- [x] NEW: Blueprint `get_graph_summary` — lightweight graph overview (id/class/title + exec connections only, ~10KB vs 172KB) (2026-03-09)
- [x] Blueprint `get_graph_data` now accepts optional `node_class_filter` param (2026-03-09)
- [x] Blueprint `get_variables` now reads default values from CDO (was always empty) (2026-03-09)
- [x] Blueprint indexer CDO fix — same default value extraction fix applied to BlueprintIndexer (2026-03-09)
- [x] Material `export_material_graph` now accepts `include_properties` and `include_positions` params (2026-03-09)
- [x] Material `get_thumbnail` now accepts `save_to_file` param (2026-03-09)
- [x] Niagara `get_compiled_gpu_hlsl` auto-compiles system if HLSL not available (2026-03-09)
- [x] Niagara `User.` prefix stripped in get_parameter_value, trace_parameter_binding, remove_user_parameter, set_parameter_default (2026-03-09)
- [x] Per-action param schemas in `monolith.discover()` output — all 122 actions now have param documentation (2026-03-09)
- [x] Niagara `get_module_inputs` — types now use `PinToTypeDefinition` instead of default Vector4f (2026-03-09)
- [x] Niagara `get_ordered_modules` — usage filter now works with shorthands ("spawn", "update"), returns error on invalid values, returns all stages when omitted (2026-03-09)
- [x] Niagara `get_renderer_bindings` — clean JSON output (name/bound_to/type) instead of raw UE struct dumps (2026-03-09)
- [x] Niagara `get_all_parameters` — added optional `emitter` and `scope` filters (2026-03-09)
- [x] Animation `get_transitions` — cast to `UAnimStateNodeBase*` instead of `UAnimStateNode*`, resolves conduit names. Added from_type/to_type fields (2026-03-09)
- [x] Material `validate_material` — seeds BFS from `UMaterialExpressionCustomOutput` subclasses + `UMaterialExpressionMaterialAttributeLayers`, added MP_MaterialAttributes + 6 missing properties. 0 false positives on standard materials (2026-03-09)
- [x] Blueprint `get_execution_flow` — two-pass FindEntryNode: Pass 1 prefers events/functions, Pass 2 fuzzy fallback skips comments (2026-03-09)
- [x] Blueprint `get_graph_summary` all-graphs mode — returns all graphs when graph_name empty, single graph when specified (2026-03-09)
