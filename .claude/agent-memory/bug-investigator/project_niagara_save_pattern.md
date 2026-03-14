---
name: Niagara system save pattern
description: MarkPackageDirty alone is not enough for Niagara — must SavePackage to disk or the Niagara editor shows empty system
type: project
---

When creating a new UNiagaraSystem or adding emitters via MCP, `MarkPackageDirty()` alone is NOT sufficient. The Niagara editor will open the asset and show an empty system because it reloads from disk (which has no file, or the pre-modification file).

**Why:** The Niagara editor initializes its view model from the asset's disk state when opened. Unlike Blueprint or Material editors which work on the already-loaded in-memory object, Niagara may re-read from disk. If the `.uasset` doesn't exist yet (newly created system) or hasn't been updated (after add_emitter), the editor shows the stale/empty state.

**How to apply:** After any Niagara create or structural write operation (create_system, add_emitter, remove_emitter), always call `UPackage::SavePackage` immediately. Use this pattern:

```cpp
UPackage* Pkg = Object->GetPackage(); // or the Pkg used in CreatePackage
FString PackageFilename;
if (FPackageName::TryConvertLongPackageNameToFilename(Pkg->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
{
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.Error = GError;
    UPackage::SavePackage(Pkg, Object, *PackageFilename, SaveArgs);
}
```

Required includes: `"Misc/PackageName.h"` and `"UObject/SavePackage.h"`.

Note: Other Monolith modules (Material, Animation) only use `MarkPackageDirty()` for write ops on existing assets — this is fine because those editors don't reload from disk on open. Niagara is the exception.

Fixed in `MonolithNiagaraActions.cpp`: `HandleCreateSystem` and `HandleAddEmitter`.
