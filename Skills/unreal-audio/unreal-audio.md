---
name: unreal-audio
description: Use when creating, editing, or inspecting Unreal Engine audio assets via Monolith MCP. Covers Sound Cues, MetaSounds, attenuation, sound classes, submixes, batch ops, and templates. Triggers on audio, sound, metasound, sound cue, attenuation, submix, reverb.
---

# Unreal Audio Workflows

**81 audio actions** via `audio_query()`. Discover first: `monolith_discover({ namespace: "audio" })`

## Key Parameters

- `asset_path` -- audio asset path (e.g. `/Game/Audio/SA_MyAttenuation`)
- `asset_paths` -- array of paths for batch operations
- `spec` -- JSON graph spec for `build_sound_cue_from_spec` / `build_metasound_from_spec`

## Action Reference

### Asset CRUD (15)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `create_sound_attenuation` | `asset_path`, `settings?` | Create USoundAttenuation with optional settings |
| `get_attenuation_settings` | `asset_path` | Read all FSoundAttenuationSettings fields |
| `set_attenuation_settings` | `asset_path`, `settings` | Partial update any attenuation fields |
| `create_sound_class` | `asset_path`, `parent_class?`, `properties?` | Create USoundClass with hierarchy |
| `get_sound_class_properties` | `asset_path` | Read FSoundClassProperties + parent + children |
| `set_sound_class_properties` | `asset_path`, `properties`, `parent_class?` | Update class properties |
| `create_sound_mix` | `asset_path`, `eq_settings?`, `class_effects?`, `timing?` | Create USoundMix with EQ + adjusters |
| `get_sound_mix_settings` | `asset_path` | Read EQ bands, class adjusters, timing |
| `set_sound_mix_settings` | `asset_path`, `eq_settings?`, `class_effects?`, `timing?` | Update mix |
| `create_sound_concurrency` | `asset_path`, `settings?` | Create USoundConcurrency |
| `get_concurrency_settings` | `asset_path` | Read MaxCount, ResolutionRule, ducking |
| `set_concurrency_settings` | `asset_path`, `settings` | Update concurrency |
| `create_sound_submix` | `asset_path`, `parent_submix?`, `effect_chain?` | Create USoundSubmix |
| `get_submix_properties` | `asset_path` | Read effect chain, volume, hierarchy |
| `set_submix_properties` | `asset_path`, `properties` | Update submix |

### Query & Search (10)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `list_audio_assets` | `type`, `path_filter?`, `limit?` | List by type (SoundWave/SoundCue/MetaSoundSource/All/etc) |
| `search_audio_assets` | `query`, `type?`, `limit?` | FTS name search |
| `get_sound_wave_info` | `asset_path` | Duration, channels, sample rate, compression, class, attenuation |
| `get_sound_class_hierarchy` | `root_class?` | Recursive tree traversal |
| `get_submix_hierarchy` | `root_submix?` | Routing tree with effect chains |
| `find_audio_references` | `asset_path` | Bidirectional reference scan |
| `find_unused_audio` | `type?`, `path_filter?`, `limit?` | Zero-reference audio assets |
| `find_sounds_without_class` | `path_filter?` | Unassigned SoundBases |
| `find_unattenuated_sounds` | `path_filter?` | Missing attenuation |
| `get_audio_stats` | — | Counts by type, sizes, compression breakdown |

### Batch Operations (10)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `batch_assign_sound_class` | `asset_paths[]`, `sound_class` | Set class on N assets |
| `batch_assign_attenuation` | `asset_paths[]`, `attenuation` | Set attenuation on N assets |
| `batch_set_compression` | `asset_paths[]`, `quality?`, `type?` | Set compression on N SoundWaves |
| `batch_set_submix` | `asset_paths[]`, `submix` | Set submix on N assets |
| `batch_set_concurrency` | `asset_paths[]`, `concurrency` | Set concurrency on N assets |
| `batch_set_looping` | `asset_paths[]`, `looping` | Set looping flag |
| `batch_set_virtualization` | `asset_paths[]`, `mode` | Restart/PlayWhenSilent/Disabled |
| `batch_rename_audio` | `asset_paths[]`, `prefix?`, `suffix?`, `find?`, `replace?` | Rename with patterns |
| `batch_set_sound_wave_properties` | `asset_paths[]`, `properties` | Multi-property reflection set |
| `apply_audio_template` | `asset_paths[]`, `template` | Apply class+attenuation+compression+submix+concurrency in one pass |

### Sound Cue Graph (21)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `create_sound_cue` | `asset_path`, `sound_waves?[]` | Create cue, auto-Random if multiple waves |
| `get_sound_cue_graph` | `asset_path` | JSON: nodes[], connections[], first_node |
| `add_sound_cue_node` | `asset_path`, `node_type`, `node_id`, `properties?` | Add any of 22 node types |
| `remove_sound_cue_node` | `asset_path`, `node_id` | Remove node |
| `connect_sound_cue_nodes` | `asset_path`, `from_node_id`, `to_node_id`, `child_index?` | Wire nodes |
| `set_sound_cue_first_node` | `asset_path`, `node_id` | Set root output |
| `set_sound_cue_node_property` | `asset_path`, `node_id`, `property_name`, `value` | Set any node property |
| `list_sound_cue_node_types` | — | All 22 types with max_children |
| `find_sound_waves_in_cue` | `asset_path` | All WavePlayer references |
| `validate_sound_cue` | `asset_path` | Check for issues |
| `build_sound_cue_from_spec` | `asset_path`, `spec` | **Crown jewel** — declarative JSON graph |
| `create_random_sound_cue` | `asset_path`, `sound_waves[]`, `weights?[]` | Template: Random node |
| `create_layered_sound_cue` | `asset_path`, `sound_waves[]`, `volumes?[]` | Template: Mixer node |
| `create_looping_ambient_cue` | `asset_path`, `sound_waves[]`, `delay_min?`, `delay_max?` | Template: Loop+Random+Delay |
| `create_distance_crossfade_cue` | `asset_path`, `bands[]` | Template: Distance crossfade |
| `create_switch_sound_cue` | `asset_path`, `parameter_name`, `sound_waves[]` | Template: Switch node |
| `duplicate_sound_cue` | `source_path`, `dest_path` | Clone |
| `delete_audio_asset` | `asset_path` | Delete any audio asset |
| `preview_sound` | `asset_path` | Editor playback |
| `stop_preview` | — | Stop preview |
| `get_sound_cue_duration` | `asset_path` | Cached duration |

### MetaSound Graph (25, requires WITH_METASOUND)

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `create_metasound_source` | `asset_path`, `format?`, `one_shot?` | Create via Builder API |
| `create_metasound_patch` | `asset_path` | Create reusable subgraph |
| `add_metasound_node` | `asset_path`, `node_class` [NS, Name, Variant] | Add from 76+ node types |
| `remove_metasound_node` | `asset_path`, `node_id_or_handle` | Remove node |
| `connect_metasound_nodes` | `asset_path`, `from_node`, `from_output`, `to_node`, `to_input` | Wire by name |
| `disconnect_metasound_nodes` | `asset_path`, `from_node`, `from_output`, `to_node`, `to_input` | Disconnect |
| `add_metasound_input` | `asset_path`, `name`, `data_type`, `default_value?` | Graph-level input |
| `add_metasound_output` | `asset_path`, `name`, `data_type` | Graph-level output |
| `set_metasound_input_default` | `asset_path`, `input_name`, `value` | Set default |
| `add_metasound_interface` | `asset_path`, `interface_name` | UE.Source, UE.OutputFormat.Mono, etc |
| `get_metasound_graph` | `asset_path` | JSON: nodes, edges, inputs, outputs |
| `list_metasound_connections` | `asset_path` | All edges |
| `list_available_metasound_nodes` | `filter?` | Enumerate registered node classes |
| `get_metasound_node_info` | `node_class` | Inputs/outputs/types for a class |
| `find_metasound_node_inputs` | `asset_path`, `node_id_or_handle` | Node inputs |
| `find_metasound_node_outputs` | `asset_path`, `node_id_or_handle` | Node outputs |
| `get_metasound_input_names` | `asset_path` | Graph inputs with types/defaults |
| `build_metasound_from_spec` | `asset_path`, `spec` | **Crown jewel** — declarative JSON graph |
| `create_metasound_preset` | `asset_path`, `reference_metasound` | Preset from existing |
| `create_oneshot_sfx` | `asset_path`, `sound_wave` | Template: WavePlayer -> Out |
| `create_looping_ambient_metasound` | `asset_path`, `sound_wave` | Template: LFO modulated |
| `create_synthesized_tone` | `asset_path`, `oscillator_type?` | Template: Osc->Filter->ADSR |
| `create_interactive_metasound` | `asset_path`, `sound_waves[]`, `parameter_name` | Template: Crossfade/Switch |
| `add_metasound_variable` | `asset_path`, `name`, `data_type`, `default_value?` | Graph variable |
| `set_metasound_node_location` | `asset_path`, `node_id_or_handle`, `x`, `y` | Editor layout |

## Common Workflows

### Create randomized footstep Sound Cue
```
audio_query("create_random_sound_cue", {
  "asset_path": "/Game/Audio/SC_Footstep_Concrete",
  "sound_waves": ["/Game/Audio/Waves/Concrete_01", "/Game/Audio/Waves/Concrete_02", "/Game/Audio/Waves/Concrete_03"],
  "weights": [1.0, 1.0, 0.5]
})
```

### Build MetaSound from spec
```
audio_query("build_metasound_from_spec", {
  "asset_path": "/Game/Audio/MS_Wind",
  "spec": {
    "type": "Source", "format": "Mono", "one_shot": false,
    "interfaces": ["UE.Source", "UE.OutputFormat.Mono"],
    "inputs": [{"name": "Intensity", "type": "Float", "default": 0.5}],
    "nodes": [
      {"id": "player", "class": ["UE", "Wave Player", "Mono"]},
      {"id": "gain", "class": ["UE", "Multiply", "Audio Float"]}
    ],
    "connections": [
      {"from": "player", "output": "Audio", "to": "gain", "input": "Audio"}
    ],
    "graph_input_connections": [
      {"input": "Intensity", "to_node": "gain", "to_pin": "Float"}
    ]
  }
})
```

### Batch configure imported sounds
```
audio_query("apply_audio_template", {
  "asset_paths": ["/Game/Audio/Waves/Gun_01", "/Game/Audio/Waves/Gun_02"],
  "template": {
    "sound_class": "/Game/Audio/Classes/SC_SFX",
    "attenuation": "/Game/Audio/Attenuation/SA_Medium",
    "compression": {"quality": 60, "type": "BinkAudio"}
  }
})
```

## Gotchas

- `USoundWave::CompressionQuality` and `SoundAssetCompressionType` are **private** -- use reflection, not direct access
- `EVirtualizationMode` values: `Restart`, `PlayWhenSilent`, `Disabled` (NOT `Pause`)
- MetaSound `AddGraphInputNode` returns `FMetaSoundBuilderNodeOutputHandle` (counterintuitive!)
- `UMetaSoundEditorSubsystem` accessed via `GEditor->GetEditorSubsystem<>()`, NOT `::Get()`
- Sound Cues need `LinkGraphNodesFromSoundNodes()` + `CacheAggregateValues()` after ANY graph mutation
- MetaSound data types: Float, Int32, Bool, String, Trigger, Time, Audio, WaveAsset
- MetaSound interfaces: "UE.Source", "UE.Source.OneShot", "UE.OutputFormat.Mono", "UE.OutputFormat.Stereo"
