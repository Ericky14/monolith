# Monolith — Testing Reference

Last updated: 2026-03-09

---

## Test Environment

- **Engine:** Unreal Engine 5.7
- **Project:** Leviathan (`D:\Unreal Projects\Leviathan\`)
- **Plugin location (dev):** `D:\Unreal Projects\Leviathan\Plugins\Monolith\`
- **MCP endpoint:** `http://localhost:9316/mcp`

---

## How to Test

### Build Test

```bash
'C:\Program Files (x86)\UE_5.7\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe' LeviathanEditor Win64 Development '-Project=D:\Unreal Projects\Leviathan\Leviathan.uproject' -waitmutex
```

### MCP Connection Test

```python
# Python http.client test (use NEW connection per request)
import http.client, json

conn = http.client.HTTPConnection("localhost", 9316)
body = json.dumps({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})
conn.request("POST", "/mcp", body, {"Content-Type": "application/json"})
resp = conn.getresponse()
print(json.loads(resp.read()))
conn.close()
```

### Action Test Template

```python
import http.client, json

def test_action(namespace, action, params=None):
    conn = http.client.HTTPConnection("localhost", 9316)
    if namespace == "monolith":
        tool_name = f"monolith_{action}"
        args = params or {}
    else:
        tool_name = f"{namespace}.query"
        args = {"action": action}
        if params:
            args["params"] = params

    body = json.dumps({
        "jsonrpc": "2.0", "id": 1,
        "method": "tools/call",
        "params": {"name": tool_name, "arguments": args}
    })
    conn.request("POST", "/mcp", body, {"Content-Type": "application/json"})
    resp = conn.getresponse()
    result = json.loads(resp.read())
    conn.close()
    return result

# Example:
# test_action("monolith", "status")
# test_action("blueprint", "list_graphs", {"asset_path": "/Game/MyBlueprint"})
```

---

## Test Matrix

### Legend
- PASS = Verified working
- FAIL = Broken, see notes
- SKIP = Not testable (stub/unimplemented)
- UNTESTED = Not yet verified

---

### MonolithCore (namespace: "monolith")

| Action | Status | Notes |
|--------|--------|-------|
| `discover` | PASS | Returned all 9 namespaces, 122 actions. Now includes per-action param schemas |
| `discover(namespace)` | PASS | Tested all 8 namespaces, correct action counts |
| `status` | PASS | Returns version 0.5.0, port 9316, 122 actions, engine 5.7, project Leviathan |
| `update(check)` | PASS | Detected v0.5.0 from GitHub, showed update dialog with release notes |
| `update(install)` | PASS | Downloaded zip, staged, swapped on exit. Retry loop handled Defender file locks. Windows tested. |
| `reindex` | PASS | Triggers successfully |

### MonolithBlueprint (namespace: "blueprint")

| Action | Status | Notes |
|--------|--------|-------|
| `list_graphs` | PASS | 700 bytes, 14 graphs. Clean overview with asset class, parent class |
| `get_graph_summary` | PASS | 29KB for EventGraph (5.7x smaller than get_graph_data). Returns all 14 graphs when graph_name empty, single graph when specified. Nodes with id/class/title/exec_targets |
| `get_graph_data` | PASS | 164KB full, 8KB with node_class_filter. Filter works well |
| `get_variables` | PASS | 8KB, 40 vars. CDO defaults confirmed populated (37/40 non-trivial) |
| `get_execution_flow` | PASS | 0.7-2KB. Two-pass FindEntryNode: Pass 1 prefers events/functions, Pass 2 fuzzy fallback skips comments. "BeginPlay" and "Tick" correctly match events |
| `search_nodes` | PASS | 3.5KB. Case-insensitive across all graphs |

### MonolithMaterial (namespace: "material")

| Action | Status | Notes |
|--------|--------|-------|
| `get_all_expressions` | PASS | 8.7K chars, 43 expressions with class, position, params |
| `get_expression_details` | PASS | Full property dump with inputs, outputs, UE properties |
| `get_full_connection_graph` | PASS | 10K chars, 60 connections. material_outputs empty on layer blends |
| `disconnect_expression` | UNTESTED | Test input disconnect vs output disconnect |
| `build_material_graph` | UNTESTED | Test full PBR setup from JSON spec |
| `begin_transaction` | UNTESTED | Test undo after transaction |
| `end_transaction` | UNTESTED | Test without begin (should error gracefully) |
| `export_material_graph` | PASS | 44K default, 13K with include_properties=false (71% reduction) |
| `import_material_graph` | UNTESTED | Test both "overwrite" and "merge" modes |
| `validate_material` | PASS | Fixed: added MP_MaterialAttributes + 6 properties, MaterialAttributeLayers BFS seed. 0 false positives (was 43). Layer-blend materials still have known limitation (implicit layer system connections) |
| `render_preview` | PASS | Saves PNG to Saved/Monolith/previews/, 68KB at 256x256 |
| `get_thumbnail` | PASS | save_to_file=true returns path (164 chars vs 91K base64). Both modes work |
| `create_custom_hlsl_node` | UNTESTED | Test with inputs, outputs, and HLSL code |
| `get_layer_info` | PASS | Correctly rejects non-layer materials with clear error |

### MonolithAnimation (namespace: "animation")

| Action | Status | Notes |
|--------|--------|-------|
| `add_montage_section` | UNTESTED | |
| `delete_montage_section` | UNTESTED | Test boundary: delete last section |
| `set_section_next` | UNTESTED | Test circular linking |
| `set_section_time` | UNTESTED | |
| `add_blendspace_sample` | UNTESTED | |
| `edit_blendspace_sample` | UNTESTED | Tests delete+re-add workaround |
| `delete_blendspace_sample` | UNTESTED | Test boundary: delete only sample |
| `get_state_machines` | PASS | 5KB. Clean names (no \n). from_type/to_type present on transitions |
| `get_state_info` | PASS | 288B. Validates required params — clear error on empty machine_name |
| `get_transitions` | PASS | 21KB. All from/to resolved (no "?"). from_type/to_type distinguishes state vs conduit. Full rule graphs |
| `get_blend_nodes` | PASS | 6KB. 27 nodes with pin connections |
| `get_linked_layers` | PASS | 295B. Found self-layer correctly |
| `get_graphs` | PASS | 7KB. 66 graphs, clean names, node/anim_node/SM counts |
| `get_nodes` | PASS | 21KB unfiltered, 8KB with graph_name, 450B with class filter. Both filters work |
| `set_notify_time` | UNTESTED | |
| `set_notify_duration` | UNTESTED | |
| `set_bone_track_keys` | UNTESTED | Test JSON array format [[x,y,z],...] |
| `add_bone_track` | UNTESTED | |
| `remove_bone_track` | UNTESTED | Fixed 2026-03-07: now uses RemoveBoneCurve per bone + child traversal |
| `add_virtual_bone` | UNTESTED | |
| `remove_virtual_bones` | UNTESTED | Test specific names vs remove all |
| `get_skeleton_info` | PASS | 27KB. 262 bones + 12 virtual bones with parent chains |
| `get_skeletal_mesh_info` | PASS | 8KB. Materials, 181 morph targets, sockets, LODs |

### MonolithNiagara (namespace: "niagara")

| Action | Status | Notes |
|--------|--------|-------|
| `add_emitter` | UNTESTED | UE 5.7 FGuid VersionGuid |
| `remove_emitter` | UNTESTED | |
| `duplicate_emitter` | UNTESTED | |
| `set_emitter_enabled` | UNTESTED | |
| `reorder_emitters` | UNTESTED | Fixed 2026-03-07: now uses PostEditChange + MarkPackageDirty for proper change notifications |
| `set_emitter_property` | UNTESTED | Test each: SimTarget, bLocalSpace, bDeterminism, etc. |
| `request_compile` | UNTESTED | |
| `create_system` | UNTESTED | Test blank + from template |
| `get_ordered_modules` | PASS | Usage filter works: spawn/update/shorthands. No usage = all stages with usage field. Invalid = error with valid list |
| `get_module_inputs` | PASS | Real types now (NiagaraBool, ENiagara_VelocityMode, etc.). No more Vector4f |
| `get_module_graph` | PASS | Full node graph with pins and links |
| `add_module` | UNTESTED | |
| `remove_module` | UNTESTED | |
| `move_module` | UNTESTED | Fixed 2026-03-07: rewires stack-flow pins only, preserves overrides |
| `set_module_enabled` | UNTESTED | |
| `set_module_input_value` | UNTESTED | Test each type: float, int, bool, vec2/3/4, color, string |
| `set_module_input_binding` | UNTESTED | |
| `set_module_input_di` | UNTESTED | Test with config JSON |
| `create_module_from_hlsl` | SKIP | Stub — always returns error |
| `create_function_from_hlsl` | SKIP | Stub — always returns error |
| `get_all_parameters` | PASS | Filtering works: scope="User" (4 params), emitter="Sparks", combined filters |
| `get_user_parameters` | PASS | Clean, compact. 4 params |
| `get_parameter_value` | PASS | Both User.X and X formats work |
| `get_parameter_type` | PASS | Returns size, primitiveness, is_di, is_enum |
| `trace_parameter_binding` | PASS | Both prefix formats work |
| `add_user_parameter` | UNTESTED | |
| `remove_user_parameter` | UNTESTED | |
| `set_parameter_default` | UNTESTED | |
| `set_curve_value` | UNTESTED | |
| `add_renderer` | UNTESTED | Test each: Sprite, Mesh, Ribbon, Light, Component |
| `remove_renderer` | UNTESTED | |
| `set_renderer_material` | UNTESTED | |
| `set_renderer_property` | UNTESTED | Test reflection types |
| `get_renderer_bindings` | PASS | Clean output: name/bound_to/type. No more raw struct dumps |
| `set_renderer_binding` | UNTESTED | Test primary + fallback ImportText format |
| `batch_execute` | UNTESTED | Test multiple ops in single transaction |
| `create_system_from_spec` | UNTESTED | Test full JSON spec with all sub-elements |
| `list_emitters` | PASS | 4 emitters with name, index, enabled, sim_target, renderer_count |
| `list_renderers` | PASS | Renderer class, index, enabled, material. Requires emitter param |
| `get_di_functions` | PASS | Simple name "CurlNoise" works |
| `get_compiled_gpu_hlsl` | PASS | Auto-compile works |

### MonolithEditor (namespace: "editor")

| Action | Status | Notes |
|--------|--------|-------|
| `trigger_build` | SKIP | Not tested to avoid disruption |
| `live_compile` | PASS | Triggered successfully, compiled 3 modules |
| `get_build_errors` | PASS | error_count + warning list |
| `get_build_status` | PASS | live_coding status fields |
| `get_build_summary` | PASS | Summary with counts |
| `search_build_output` | PASS | Pattern search works |
| `get_recent_logs` | PASS | Default returns 100, max/count params both work |
| `search_logs` | PASS | Pattern, category, verbosity filters all work |
| `tail_log` | PASS | Returns 50 formatted lines |
| `get_log_categories` | PASS | 134 categories |
| `get_log_stats` | PASS | total/fatal/error/warning/log/verbose counts |
| `get_compile_output` | PASS | Structured compile report with time-windowed log lines |
| `get_crash_context` | PASS | Returns recent_errors even without crash |

### MonolithConfig (namespace: "config")

| Action | Status | Notes |
|--------|--------|-------|
| `resolve_setting` | PASS | Works with Engine/Game/Input categories |
| `explain_setting` | PASS | Convenience + explicit modes, layer breakdown |
| `diff_from_default` | PASS | Section filter works, shows change_type |
| `search_config` | PASS | Category filter now works (was broken, fixed) |
| `get_section` | PASS | Category names now resolve (was broken, fixed) |
| `get_config_files` | PASS | 38 files, 3 hierarchy levels, category filter works |

### MonolithIndex (namespace: "project")

| Action | Status | Notes |
|--------|--------|-------|
| `search` | PASS | FTS5 queries working. Ranking correct (name > content). Match highlighting with >>>keyword<<<. Default limit 50 |
| `find_references` | PASS | Bidirectional deps/referencers verified on Blueprint, Material, RL_LWSkin. All Hard refs, semantically correct |
| `find_by_type` | PASS | Tested Blueprint, Material, NiagaraSystem, DataTable, InputAction. Pagination (limit/offset) verified. Param is `asset_type`/`asset_class` |
| `get_stats` | PASS | All 11 tables populated: 9,571 assets, 80,991 nodes, 68,366 connections, 2,184 cpp_symbols. Zero empty tables |
| `get_asset_details` | PASS | Deep data returned: AnimBP (78 nodes, 11 vars), Material (34 nodes), Blueprint (92 nodes, 8 vars). References included |

### MonolithSource (namespace: "source")

| Action | Status | Notes |
|--------|--------|-------|
| `read_source` | PASS | Returns full source, 137K chars for AActor |
| `read_source members_only` | PASS | Returns AActor members (lines 256-387), function bodies replaced with `// [body omitted]`, access specifiers preserved, original source intact (ENGINE_API etc.) |
| `find_references` | PASS | 50+ type references |
| `find_callers` | PASS | 50+ callers with file/line info |
| `find_callees` | PASS | Call graph with function names |
| `search_source` | PASS | Symbol + source line matches |
| `get_class_hierarchy` | PASS | Descendants work (60+ for AActor). Ancestors now work: AActor→UObject, APawn→AActor, ACharacter→APawn. 37,010 inheritance links (2026-03-08) |
| `get_module_info` | PASS | Path, type, file count, symbol counts, key classes |
| `get_symbol_context` | PASS | Implementation with 20 lines context |
| `read_file` | PASS | Relative paths now work via suffix matching |
| `trigger_reindex` | PASS | Triggers successfully |

---

## Integration Tests

| Test | Status | Notes |
|------|--------|-------|
| Plugin loads without errors | PASS | No LogMonolith errors on startup |
| MCP server starts on configured port | PASS | tools/list returns 12 tools |
| Project auto-indexes on first launch | PASS | Fixed 2026-03-09: deferred to OnFilesLoaded, now indexes all 9560+ assets |
| All 9 modules register their actions | PASS | discover returns all 9 namespaces |
| CORS headers present | UNTESTED | Check `Access-Control-Allow-Origin: *` |
| Stateless server (no session tracking) | PASS | No session tracking, accepts requests without session headers |
| Batch JSON-RPC requests | UNTESTED | Send array of requests |
| Invalid JSON handling | UNTESTED | Should return -32700 Parse error |
| Unknown method handling | UNTESTED | Should return -32601 Method not found |
| Undo/redo after write operations | UNTESTED | Test material build, niagara add_emitter, animation add_section |
| Module enable toggles functional | UNTESTED | Disable a module in settings, verify its actions are not registered on restart |
| Cross-platform update extraction | PASS | Windows PowerShell Expand-Archive verified. macOS/Linux untested. |
| Hot-swap plugin on editor exit | PASS | v0.4.0→v0.5.0 swap successful. tasklist polling, move retry loop (10x3s), backup cleanup, .git preservation all verified. |
| Incremental indexing | UNTESTED | Add/remove/rename an asset, verify index updates via Asset Registry callbacks |
| Deep asset indexing (game-thread) | UNTESTED | Verify deep indexing batches run on game thread without editor hitches |
| 10 new indexers register | UNTESTED | Verify Animation, Niagara, DataTable, Level, GameplayTag, Config, Cpp, UserDefinedEnum, UserDefinedStruct, InputAction indexers produce data |
| editor.live_compile action | PASS | Triggered via MCP, compiled 3 modules |
| diff_from_default 5 INI layers | UNTESTED | Test diff across Base, Default, Project, User, Saved layers |
| reorder_emitters change notifications | UNTESTED | Reorder emitters, verify PostEditChange fires and asset is marked dirty |

---

## Performance Benchmarks

| Benchmark | Status | Target | Notes |
|-----------|--------|--------|-------|
| Full project index time | UNTESTED | < 60s for 10K assets | Currently metadata-only |
| FTS5 search latency | UNTESTED | < 100ms | |
| Source DB query latency | UNTESTED | < 200ms | |
| Material graph export (complex) | UNTESTED | < 2s | |
| Niagara create_system_from_spec | UNTESTED | < 5s | |
| HTTP request/response overhead | UNTESTED | < 10ms | |
| Memory usage (idle) | UNTESTED | < 50MB | 2 SQLite DBs open |

---

## Test History

| Date | Tester | Scope | Result | Notes |
|------|--------|-------|--------|-------|
| 2026-03-06 | tumourlove + Claude | Full build | PASS | All 9 modules compile clean on UE 5.7 |
| 2026-03-07 | tumourlove + Claude | Bug fixes | PASS | HTTP body null-term, Niagara graph traversal, emitter lookup, Source DB WAL, asset loading, SQL schema creation, reindex dispatch |
| 2026-03-07 | tumourlove + Claude | 8 bug fixes | PASS | remove_bone_track (RemoveBoneTrack API), last_full_index (WriteMeta), move_module (pin rewire), get_build_errors (ELogVerbosity), SQL parameterization (13 methods), LogTemp->LogMonolith, CachedLogCapture safety, MonolithSource flatten. Build: 0 errors, 3.95s |
| 2026-03-07 | tumourlove + Claude | Session + first-call fixes | PASS | Removed session tracking from HTTP server (fully stateless). Fixed first-tool-call failures: transport type mismatch in .mcp.json ("http" → "streamableHttp") + MonolithSource stub not registering actions |
| 2026-03-07 | tumourlove + Claude | Waves 1-4 features | PASS | Module enable toggles enforced, editor.live_compile added, diff_from_default GConfig+5 layers, Niagara reorder_emitters PostEditChange+MarkPackageDirty, cross-platform update extraction, hot-swap plugin on exit, 7 new indexers (Animation/Niagara/DataTable/Level/GameplayTag/Config/Cpp), incremental indexing with Asset Registry callbacks, deep asset indexing with game-thread batching |
| 2026-03-07 | tumourlove + Claude | Project index actions | PASS | All 5 MCP actions verified: search (FTS5 ranking), find_references (bidirectional), find_by_type (5 types + pagination), get_stats (11 tables, ~211K data points), get_asset_details (deep nodes/vars/refs across BP/Material/AnimBP) |
| 2026-03-08 | tumourlove + Claude | Source indexer overhaul | PASS | UE macro preprocessor (strips UCLASS/API/GENERATED_BODY), --clean flag, diagnostic counters. Results: 1.1M symbols, 81K files, 62K class definitions, 37K inheritance links, full ancestor chains (AActor→UObject, APawn→AActor, ACharacter→APawn). DB: 1.8GB. |
| 2026-03-08 | tumourlove + Claude | Auto-updater end-to-end | PASS | Full cycle: v0.4.0→v0.5.0 via GitHub Releases. Fixed 7 bugs in swap script (tasklist polling, errorlevel fix, move retry loop 10x3s, cmd /c quoting, DelayedExpansion, xcopy /h, rollback rmdir). Defender file locks handled by retry loop. Backup + staging cleaned up. .git/.github preserved. |
| 2026-03-07 | tumourlove + Claude | Wave 1 full test | PASS | Integration (10/10), Core (4/4), Editor (11/11 +2 skip), Config (6/6), Source (9/10 +1 deferred). Bugs found and fixed: find_callers/find_callees param mismatch, read_file param mismatch + path normalization, get_recent_logs max param, search_config category filter, get_section category resolution, get_class_hierarchy forward-decl filtering, ExtractMembers brace depth rewrite, MonolithHttpServer top-level param merge, SQLite WAL→DELETE + ReadWrite, reindex absolute path. members_only deferred pending indexer improvement. |
| 2026-03-09 | tumourlove + Claude | Wave 2: Blueprint + Material + Niagara + discover schemas | PASS | **Blueprint:** NEW `get_graph_summary` (lightweight ~10KB), `get_graph_data` `node_class_filter` param, `get_variables` CDO default values fix, BlueprintIndexer CDO fix. 5->6 actions. **Material:** `export_material_graph` `include_properties`/`include_positions` params, `get_thumbnail` `save_to_file` param. **Niagara:** `get_compiled_gpu_hlsl` auto-compile, `User.` prefix stripping in 4 param actions. **All:** Per-action param schemas in `discover()` output. Total: 121->122. |
| 2026-03-09 | tumourlove + Claude | Indexer + Niagara + Animation fixes | PASS | **Phase 1 (Indexer):** Auto-index deferred to OnFilesLoaded (was 193/9560), sanity check <500 skips last_full_index, bIsIndexing reset in Deinitialize, DB WAL→DELETE. **Phase 2 (Niagara):** trace_parameter_binding OR fallback, get_di_functions reversed pattern, batch_execute 3 op name aliases, all actions accept asset_path (system_path compat), duplicate_emitter/set_curve_value param aliases, 2 NEW actions (list_emitters, list_renderers). Total: 39→41. **Phase 3 (Animation):** State machine \n stripping, get_state_info param validation, exact SM matching, get_nodes graph_name filter. |
| 2026-03-09 | tumourlove + Claude | Round 4: Final read action verification | PASS | 35/36 read actions PASS, 1 FAIL (validate_material on layer-blend materials). All 6 targeted fixes verified: (1) get_module_inputs real types (NiagaraBool, enums), (2) get_ordered_modules usage filter + shorthands + error on invalid, (3) get_renderer_bindings clean JSON, (4) get_all_parameters scope/emitter filters, (5) get_transitions from/to resolved with from_type/to_type, (6) validate_material custom output seeding (partial — still fails on layer blends). Also verified: CDO defaults, param schemas, node_class_filter, include_properties, save_to_file, graph_name filter, User. prefix both forms. |
| 2026-03-09 | tumourlove + Claude | Round 5: Final polish verification | PASS | **36/36 read actions PASS.** 3 final fixes verified: (1) `validate_material` — added MP_MaterialAttributes + 6 properties to AllMaterialProperties, MaterialAttributeLayers BFS seed → 0 false positive islands (was 43). (2) `get_execution_flow` — two-pass FindEntryNode prioritizes events over comments → "BeginPlay" and "Tick" correctly match events. (3) `get_graph_summary` — returns all 14 graphs when graph_name empty. All changes baked via UBT build. |

---

## Regression Checklist

Before any release, verify:

- [ ] All 9 modules compile clean (`UBT LeviathanEditor Win64 Development`)
- [ ] MCP server starts and responds to `tools/list`
- [ ] `monolith_discover` lists all 9 namespaces
- [ ] `monolith_status` returns correct version
- [ ] `project.search` returns results
- [ ] `source.read_source` returns code for a known class (e.g., "AActor")
- [ ] `blueprint.list_graphs` works on a test Blueprint
- [ ] `material.get_all_expressions` works on a test Material
- [ ] `editor.get_recent_logs` returns log entries
- [ ] `config.resolve_setting` returns a known setting value
- [ ] First-tool-call works on fresh session — `tools/list` returns all actions on first call, `source.query` works without retry
- [ ] MonolithSource registers all 10 actions — verify all callable: read_source, find_references, find_callers, find_callees, search_source, get_class_hierarchy, get_module_info, get_symbol_context, read_file, trigger_reindex
- [ ] Module enable toggle disables action registration when set to false
- [ ] `editor.live_compile` triggers Live Coding compile
- [ ] Incremental index updates on asset add/remove/rename
- [ ] Deep indexing produces data for Animation, Niagara, DataTable, Level, UserDefinedEnum, UserDefinedStruct, InputAction assets
- [ ] Settings UI re-index buttons appear in Editor Preferences > Plugins > Monolith
- [ ] Live Coding OnPatchComplete delegate captures compile results (last_result, timestamps, patch_applied)
- [ ] `editor.get_compile_output` returns time-windowed compile log lines with error/warning counts
- [ ] No LogMonolith errors in editor log on clean startup
- [ ] `niagara.list_emitters` returns emitter names, index, enabled, sim_target, renderer_count
- [ ] `niagara.list_renderers` returns renderer class, index, enabled, material
- [ ] All Niagara actions accept `asset_path` as primary param (backward-compat `system_path` still works)
- [ ] Project auto-index on first launch indexes >500 assets (OnFilesLoaded timing fix)
- [ ] Animation `get_state_machines` returns clean names without `\n` suffixes
- [ ] Animation `get_nodes` accepts optional `graph_name` filter
- [ ] Blueprint `get_graph_summary` returns lightweight graph overview (id/class/title + exec connections)
- [ ] Blueprint `get_graph_data` accepts `node_class_filter` param
- [ ] Blueprint `get_variables` returns CDO default values (not empty)
- [ ] Material `export_material_graph` respects `include_properties=false` and `include_positions=false`
- [ ] Material `get_thumbnail` with `save_to_file=true` writes PNG to Saved/Monolith/thumbnails/
- [ ] Niagara `get_compiled_gpu_hlsl` auto-compiles system if HLSL not cached
- [ ] Niagara param actions handle `User.` prefix transparently (get_parameter_value, trace_parameter_binding, remove_user_parameter, set_parameter_default)
- [ ] `monolith.discover()` returns per-action param schemas for all 122 actions
