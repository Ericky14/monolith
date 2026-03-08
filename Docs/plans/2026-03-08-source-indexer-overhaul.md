# Source Indexer Overhaul — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix the Python tree-sitter source indexer so class hierarchy (ancestors) and `read_source members_only` work correctly, producing a high-quality index for all future Monolith and Leviathan development.

**Architecture:** The root cause is that UE macros (`ENGINE_API`, `UCLASS()`, `GENERATED_BODY()`) confuse tree-sitter into producing ERROR nodes with unreliable spans and missing `base_class_clause` extraction. The fix is a **text preprocessor** that replaces UE-specific tokens with same-length whitespace before feeding to tree-sitter, preserving line numbers while giving the parser clean C++. We also add a `--clean` flag to ensure fresh DB creation, and diagnostic counters to validate the results.

**Tech Stack:** Python 3, tree-sitter, tree-sitter-cpp, SQLite3

---

## Context for Implementer

### File Locations (all under `D:/Unreal Projects/Leviathan/Plugins/Monolith/`)

| File | Purpose |
|------|---------|
| `Scripts/source_indexer/__main__.py` | CLI entry point — `python -m source_indexer --source PATH --db PATH` |
| `Scripts/source_indexer/indexer/cpp_parser.py` | Tree-sitter C++ parser (784 lines) |
| `Scripts/source_indexer/indexer/pipeline.py` | Orchestrator: file discovery, DB insertion, inheritance resolution (328 lines) |
| `Scripts/source_indexer/indexer/reference_builder.py` | Second-pass cross-reference extraction (307 lines) |
| `Scripts/source_indexer/indexer/shader_parser.py` | Regex HLSL parser (166 lines) — **no changes needed** |
| `Scripts/source_indexer/db/schema.py` | SQLite DDL (129 lines) — **no changes needed** |
| `Scripts/source_indexer/db/queries.py` | Insert/query helpers (448 lines) — **no changes needed** |
| `Source/MonolithSource/Private/MonolithSourceSubsystem.cpp` | C++ side that launches the Python indexer |
| `Source/MonolithSource/Private/MonolithSourceActions.cpp` | C++ MCP actions (read_source, get_class_hierarchy, etc.) |

### What's Broken and Why

1. **`get_class_hierarchy` returns no ancestors** — The inheritance table is empty for most UE classes because:
   - `ENGINE_API` (and similar `*_API` export macros) cause tree-sitter to produce ERROR nodes instead of clean `class_specifier` nodes
   - ERROR node recovery uses regex that works for name extraction but the ERROR node's span may not encompass the full `base_class_clause`
   - Without `base_class_clause`, `_get_base_classes()` returns `[]`, so no `_bases_*` entries are created
   - `_resolve_inheritance()` has nothing to resolve

2. **`read_source members_only` deferred** — Actually works fine (reads from disk, not DB spans). But forward-declaration-only symbols can't be filtered properly. Will benefit from better spans.

3. **DB accumulation on re-index** — `init_db()` uses `CREATE TABLE IF NOT EXISTS`. Old data persists. Must delete DB file before reindexing.

### How the Preprocessor Fix Works

Replace UE tokens with same-length whitespace **before** tree-sitter parsing. This preserves exact line numbers and column positions while giving tree-sitter valid C++ input.

Before:
```cpp
UCLASS(BlueprintType)
class ENGINE_API AActor : public UObject
{
    GENERATED_BODY()
```

After preprocessing (conceptual — actual bytes are spaces):
```cpp

class            AActor : public UObject
{

```

Tree-sitter now sees a clean `class_specifier` with `base_class_clause` → `_get_base_classes()` returns `["UObject"]` → inheritance table gets populated.

### Testing Strategy

Since this is a Python-only change (no C++ modifications), we can test entirely via command line:
1. Run the indexer with `--clean` on a small test set first
2. Query the DB directly with sqlite3 to verify inheritance
3. Run full engine reindex
4. Test via MCP actions in the editor

---

## Task 1: Add `--clean` Flag to CLI

**Files:**
- Modify: `Scripts/source_indexer/__main__.py`

**Step 1: Add the --clean argument and DB deletion logic**

In `__main__.py`, add a `--clean` argument that deletes the existing DB file before creating a new connection:

```python
def main():
    parser = argparse.ArgumentParser(description="Index Unreal Engine C++ source into SQLite")
    parser.add_argument("--source", required=True, help="UE Engine/Source path")
    parser.add_argument("--db", required=True, help="Output SQLite DB path")
    parser.add_argument("--shaders", default="", help="UE Shaders path")
    parser.add_argument("--clean", action="store_true", help="Delete existing DB before indexing")
    args = parser.parse_args()

    db_path = Path(args.db)
    if args.clean and db_path.exists():
        db_path.unlink()
        print(f"Deleted existing DB: {db_path}")

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row
    init_db(conn)
    # ... rest unchanged
```

**Step 2: Verify it works**

Run: `python -m source_indexer --help` from `Scripts/` directory
Expected: Shows `--clean` in help output

---

## Task 2: Create UE Macro Preprocessor

**Files:**
- Create: `Scripts/source_indexer/indexer/ue_preprocessor.py`

This is the core fix. A module that strips UE-specific tokens from C++ source bytes before tree-sitter parsing.

**Step 1: Write the preprocessor module**

```python
"""Preprocess UE C++ source to help tree-sitter parse cleanly.

Replaces UE-specific macros/tokens with same-length whitespace so that
tree-sitter sees valid C++ while preserving exact line and column positions.
"""

from __future__ import annotations

import re

# Pattern: UCLASS(...), USTRUCT(...), UENUM(...), UINTERFACE(...)
# These appear as standalone lines before class/struct/enum declarations.
# Must handle nested parentheses: UCLASS(meta=(Key="Value"))
_UE_CLASS_MACRO_RE = re.compile(
    rb'\b(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\([^)]*(?:\([^)]*\)[^)]*)*\)',
    re.DOTALL,
)

# Pattern: GENERATED_BODY(), GENERATED_UCLASS_BODY(), GENERATED_USTRUCT_BODY()
_GENERATED_BODY_RE = re.compile(
    rb'\bGENERATED_(?:UCLASS_|USTRUCT_)?BODY\s*\(\s*\)',
)

# Pattern: *_API export macros in class declarations (ENGINE_API, CORE_API, etc.)
# Only match when between 'class/struct' and the class name identifier
_API_MACRO_RE = re.compile(
    rb'\b[A-Z][A-Z0-9]*_API\b',
)


def _replace_with_spaces(match: re.Match[bytes]) -> bytes:
    """Replace matched text with spaces, preserving newlines for line count."""
    text = match.group(0)
    return bytes(b'\n'[0] if b == ord(b'\n') else b' '[0] for b in text)


def preprocess_ue_source(source_bytes: bytes) -> bytes:
    """Strip UE macros from C++ source, preserving line/column positions.

    Replaces:
    - UCLASS(...), USTRUCT(...), UENUM(...), UINTERFACE(...) → spaces
    - GENERATED_BODY() and variants → spaces
    - *_API export macros (ENGINE_API, CORE_API, etc.) → spaces

    All replacements preserve byte length and newline positions so that
    tree-sitter line numbers remain accurate.
    """
    result = _UE_CLASS_MACRO_RE.sub(_replace_with_spaces, source_bytes)
    result = _GENERATED_BODY_RE.sub(_replace_with_spaces, result)
    result = _API_MACRO_RE.sub(_replace_with_spaces, result)
    return result
```

**Step 2: Verify regex patterns manually**

Test the following inputs produce correct output (spaces instead of macros, newlines preserved):

```
Input:  b"UCLASS(BlueprintType)\nclass ENGINE_API AActor : public UObject\n{"
Output: b"                     \nclass            AActor : public UObject\n{"
```

The key invariant: `output.count(b'\n') == input.count(b'\n')` — line count must be identical.

---

## Task 3: Integrate Preprocessor into CppParser

**Files:**
- Modify: `Scripts/source_indexer/indexer/cpp_parser.py`

**Step 1: Import and apply preprocessor before tree-sitter parse**

At the top of `cpp_parser.py`, add the import:
```python
from .ue_preprocessor import preprocess_ue_source
```

In `parse_file()` (line 52-70), apply preprocessing before parsing:

```python
def parse_file(self, path: str | Path) -> ParseResult:
    path = Path(path)
    source_bytes = path.read_bytes()
    # Preprocess: strip UE macros so tree-sitter gets clean C++
    clean_bytes = preprocess_ue_source(source_bytes)
    source_text = source_bytes.decode("utf-8", errors="replace")  # Keep original for source_lines
    source_lines = source_text.splitlines()

    tree = self._parser.parse(clean_bytes)  # Parse the cleaned version
    root = tree.root_node

    result = ParseResult(
        path=str(path),
        source_lines=source_lines,  # Store ORIGINAL lines (for FTS and display)
    )

    result.includes = self._extract_includes(root)
    self._extract_symbols(root, source_lines, result)

    return result
```

**Critical:** `source_lines` must come from the ORIGINAL bytes (for FTS source search and `read_source` display). Only tree-sitter gets the cleaned version.

**Step 2: Update the `is_ue_macro` detection**

Since `UCLASS()` etc. are now stripped, the `_try_get_ue_macro()` lookahead won't find them anymore. We need to detect them from the original source text instead.

Replace the lookahead-based UE macro detection in `_extract_symbols()`. Instead, check the original source lines above a class/struct node to determine if a UE macro was present:

```python
def _has_ue_macro_above(self, node, source_lines: list[str]) -> str | None:
    """Check original source lines above this node for a UE macro."""
    line_idx = node.start_point[0] - 1  # Line above the node (0-indexed)
    while line_idx >= 0:
        line = source_lines[line_idx].strip()
        if not line:
            line_idx -= 1
            continue
        for macro in UE_MACROS:
            if line.startswith(macro + "(") or line.startswith(macro):
                return macro
        break  # Non-empty, non-macro line found — stop
    return None
```

Update `_extract_symbols()` to use this for class/struct nodes. For member-level macros (UPROPERTY, UFUNCTION), these are smaller and tree-sitter handles them as expression_statements even after preprocessing — actually wait, those are ALSO stripped by `_API_MACRO_RE`? No — UPROPERTY and UFUNCTION don't match `*_API`. But they DO match `_UE_CLASS_MACRO_RE`... wait no, that regex only matches UCLASS/USTRUCT/UENUM/UINTERFACE.

Actually, re-reading the preprocessor: it strips UCLASS/USTRUCT/UENUM/UINTERFACE and *_API and GENERATED_BODY. It does NOT strip UPROPERTY/UFUNCTION. Those still exist in the cleaned source and tree-sitter will still see them as expression_statements. The existing `_try_get_ue_macro()` and `_try_get_ue_macro_field()` will still work for UPROPERTY/UFUNCTION.

So the change to `_extract_symbols()` is: for class-level macro detection only, use `_has_ue_macro_above()` on the original source instead of relying on the previous sibling node being an expression_statement. The loop logic simplifies:

```python
def _extract_symbols(self, root, source_lines: list[str], result: ParseResult) -> None:
    children = list(root.children)
    i = 0
    while i < len(children):
        node = children[i]

        # UPROPERTY/UFUNCTION still exist in cleaned source — handle as before
        ue_macro = self._try_get_ue_macro(node)
        if ue_macro and i + 1 < len(children):
            next_node = children[i + 1]
            if next_node.type in ("class_specifier", "struct_specifier", "enum_specifier"):
                self._extract_class_or_struct_or_enum(next_node, source_lines, result, ue_macro=ue_macro)
                i += 2
                continue
            elif next_node.type == "function_definition":
                self._extract_misparse_class_or_function(next_node, source_lines, result, ue_macro=ue_macro)
                i += 2
                continue
            elif next_node.type in ("ERROR", "declaration"):
                self._extract_class_from_error_node(next_node, source_lines, result, ue_macro=ue_macro)
                i += 2
                continue

        # After preprocessing, most UCLASS'd classes should now be clean class_specifier nodes
        if node.type in ("class_specifier", "struct_specifier", "enum_specifier"):
            # Check original source for UE macro above this node
            ue_above = self._has_ue_macro_above(node, source_lines)
            self._extract_class_or_struct_or_enum(node, source_lines, result, ue_macro=ue_above)
            i += 1
            continue

        if node.type == "function_definition":
            self._extract_misparse_class_or_function(node, source_lines, result)
            i += 1
            continue

        if node.type == "ERROR":
            self._extract_class_from_error_node(node, source_lines, result)
            i += 1
            continue

        i += 1
```

**Step 3: Update signature extraction to use original source**

In `_extract_class_or_struct_or_enum()` at line 155, the signature is extracted from `node.text`. But after preprocessing, `node.text` has the cleaned version (no API macros). We should extract the signature from the original source lines instead:

```python
# Replace:
signature = node.text.decode().split("{")[0].strip() if node.text else ""
# With:
sig_start = node.start_point[0]
sig_end = node.end_point[0]
sig_lines = source_lines[sig_start:sig_end + 1]
signature = " ".join(line.strip() for line in sig_lines).split("{")[0].strip()
```

This preserves the original `ENGINE_API` etc. in stored signatures for display purposes.

---

## Task 4: Integrate Preprocessor into ReferenceBuilder

**Files:**
- Modify: `Scripts/source_indexer/indexer/reference_builder.py`

The reference builder re-parses every file with tree-sitter (line 35). It needs the same preprocessing for consistent results.

**Step 1: Add preprocessing to extract_references()**

```python
from .ue_preprocessor import preprocess_ue_source

# In extract_references(), line 30-35:
def extract_references(self, path: Path, file_id: int) -> int:
    try:
        source_bytes = path.read_bytes()
    except OSError:
        return 0

    clean_bytes = preprocess_ue_source(source_bytes)
    tree = self._parser.parse(clean_bytes)
    # ... rest unchanged
```

This ensures the reference builder sees the same AST structure as the main parser.

---

## Task 5: Add Diagnostic Counters to Pipeline

**Files:**
- Modify: `Scripts/source_indexer/indexer/pipeline.py`

**Step 1: Add counters to track parsing quality**

Add a stats dict to `IndexingPipeline.__init__()`:

```python
self._diag = {
    "classes_clean": 0,       # Parsed via clean class_specifier
    "classes_error": 0,       # Parsed via ERROR node recovery
    "classes_misparsed": 0,   # Parsed via misparsed function_definition
    "forward_decls": 0,       # Forward declarations (span=0)
    "with_base_classes": 0,   # Classes/structs with base classes extracted
    "inheritance_resolved": 0,  # Successful inheritance links
    "inheritance_failed": 0,    # Failed lookups (parent not in class map)
}
```

**Step 2: Increment counters in CppParser**

Pass the diag dict through to the parser, or have the pipeline inspect ParseResult after parsing. The simplest approach: count in the pipeline's `_index_cpp_file()` after parsing:

```python
for sym in result.symbols:
    if sym.kind in ("class", "struct"):
        if sym.line_end == sym.line_start:
            self._diag["forward_decls"] += 1
        if sym.base_classes:
            self._diag["with_base_classes"] += 1
```

**Step 3: Count in _resolve_inheritance()**

```python
def _resolve_inheritance(self) -> None:
    keys_to_process = [k for k in self._symbol_name_to_id if k.startswith("_bases_")]
    for key in keys_to_process:
        child_name = key[len("_bases_"):]
        base_classes = self._symbol_name_to_id[key]
        child_id = self._class_name_to_id.get(child_name)
        if child_id is None:
            self._diag["inheritance_failed"] += len(base_classes)
            continue
        for parent_name in base_classes:
            parent_id = self._class_name_to_id.get(parent_name)
            if parent_id is not None:
                try:
                    insert_inheritance(self._conn, child_id=child_id, parent_id=parent_id)
                    self._diag["inheritance_resolved"] += 1
                except sqlite3.IntegrityError:
                    pass
            else:
                self._diag["inheritance_failed"] += 1
                logger.debug("Inheritance failed: %s -> %s (parent not found)", child_name, parent_name)
```

**Step 4: Print diagnostic summary**

In `__main__.py`, after indexing completes, print the diagnostics:

```python
stats = pipeline.index_engine(...)
print(f"Done: {stats['files_processed']} files, {stats['symbols_extracted']} symbols, {stats['errors']} errors")

# Print diagnostics
diag = pipeline.diagnostics
print(f"\nDiagnostics:")
print(f"  Classes (clean parse):  {diag['classes_clean']}")
print(f"  Classes (error recovery): {diag['classes_error']}")
print(f"  Forward declarations:   {diag['forward_decls']}")
print(f"  With base classes:      {diag['with_base_classes']}")
print(f"  Inheritance resolved:   {diag['inheritance_resolved']}")
print(f"  Inheritance failed:     {diag['inheritance_failed']}")
```

Add a `diagnostics` property to the pipeline:
```python
@property
def diagnostics(self) -> dict[str, int]:
    return dict(self._diag)
```

---

## Task 6: Handle Nested Parentheses in Preprocessor Regex

**Files:**
- Modify: `Scripts/source_indexer/indexer/ue_preprocessor.py`

The initial regex `_UE_CLASS_MACRO_RE` with `\([^)]*(?:\([^)]*\)[^)]*)*\)` handles one level of nesting. But UE macros can have deeper nesting:

```cpp
UCLASS(BlueprintType, meta=(DisplayName="Foo", Categories=(Gameplay, UI)))
```

**Step 1: Replace regex with a function-based approach for class macros**

```python
def _strip_balanced_macro(source: bytes, macro_names: tuple[bytes, ...]) -> bytes:
    """Strip macro_name(...) with balanced parentheses, replacing with spaces."""
    result = bytearray(source)
    i = 0
    while i < len(result):
        for macro in macro_names:
            if result[i:i+len(macro)] == macro:
                # Check it's a word boundary
                if i > 0 and (chr(result[i-1]).isalnum() or result[i-1] == ord('_')):
                    break
                # Find opening paren
                j = i + len(macro)
                while j < len(result) and chr(result[j]).isspace():
                    j += 1
                if j >= len(result) or result[j] != ord('('):
                    break
                # Find balanced closing paren
                depth = 1
                k = j + 1
                while k < len(result) and depth > 0:
                    if result[k] == ord('('):
                        depth += 1
                    elif result[k] == ord(')'):
                        depth -= 1
                    k += 1
                if depth == 0:
                    # Replace i..k with spaces (preserving newlines)
                    for idx in range(i, k):
                        if result[idx] != ord('\n'):
                            result[idx] = ord(' ')
                    i = k
                    break
                break
        else:
            i += 1
            continue
        i += 1
    return bytes(result)

_CLASS_MACROS = (b'UCLASS', b'USTRUCT', b'UENUM', b'UINTERFACE')

def preprocess_ue_source(source_bytes: bytes) -> bytes:
    result = _strip_balanced_macro(source_bytes, _CLASS_MACROS)
    result = _GENERATED_BODY_RE.sub(_replace_with_spaces, result)
    result = _API_MACRO_RE.sub(_replace_with_spaces, result)
    return result
```

This handles arbitrary nesting depth.

---

## Task 7: Run Full Reindex and Validate

**Files:** None (testing only)

**Step 1: Delete the existing source DB**

```bash
rm -f "D:/Unreal Projects/Leviathan/Plugins/Monolith/Saved/EngineSource.db"
```

**Step 2: Run the indexer with --clean**

```bash
cd "D:/Unreal Projects/Leviathan/Plugins/Monolith/Scripts"
python -m source_indexer \
  --source "'C:/Program Files (x86)/UE_5.7/Engine/Source'" \
  --db "D:/Unreal Projects/Leviathan/Plugins/Monolith/Saved/EngineSource.db" \
  --shaders "'C:/Program Files (x86)/UE_5.7/Engine/Shaders'" \
  --clean
```

Wait for completion. Check diagnostic output.

**Step 3: Validate inheritance with sqlite3**

```bash
sqlite3 "D:/Unreal Projects/Leviathan/Plugins/Monolith/Saved/EngineSource.db"
```

```sql
-- Check AActor has parents
SELECT s.name FROM inheritance i
JOIN symbols s ON s.id = i.parent_id
WHERE i.child_id = (SELECT id FROM symbols WHERE name='AActor' AND kind='class' ORDER BY (line_end > line_start) DESC LIMIT 1);
-- Expected: UObject (or at minimum, a non-empty result)

-- Check inheritance table has entries
SELECT COUNT(*) FROM inheritance;
-- Expected: Thousands (was likely near-zero before)

-- Check class definitions have proper spans
SELECT name, line_start, line_end, (line_end - line_start) as span
FROM symbols WHERE kind='class' AND line_end > line_start
ORDER BY span DESC LIMIT 20;
-- Expected: Large spans (100+) for major engine classes

-- Count forward declarations vs definitions
SELECT
  SUM(CASE WHEN line_end > line_start THEN 1 ELSE 0 END) as definitions,
  SUM(CASE WHEN line_end = line_start THEN 1 ELSE 0 END) as forward_decls
FROM symbols WHERE kind IN ('class', 'struct');
```

**Step 4: Validate via MCP (requires editor open)**

Test these actions after restarting the editor to pick up the new DB:

```
source.query("get_class_hierarchy", {"symbol": "AActor", "direction": "both", "depth": 3})
```

Expected: Ancestors shows UObject chain. Descendants shows derived classes.

```
source.query("read_source", {"symbol": "AActor", "members_only": true})
```

Expected: Class member declarations without function bodies.

**Step 5: Record results in TESTING.md**

Update `read_source members_only` and `get_class_hierarchy` statuses.

---

## Task 8 (Optional): Improve Error Node Span Recovery

**Files:**
- Modify: `Scripts/source_indexer/indexer/cpp_parser.py`

Even with preprocessing, some edge cases may still produce ERROR nodes. Improve the fallback by using brace-counting on the original source to find the true closing `}`:

In `_extract_class_from_error_node()`, after the regex match, if the ERROR node's span seems too small (< 5 lines for a class with a body), use source lines to find the real end:

```python
# After line 347 (symbol creation), before appending:
# Validate span — if node span is suspiciously small but regex found '{', find real '}'
if '{' in text and (node.end_point[0] - node.start_point[0]) < 3:
    brace_line = node.start_point[0]
    for idx in range(node.start_point[0], min(node.start_point[0] + 5, len(source_lines))):
        if '{' in source_lines[idx]:
            brace_line = idx
            break
    real_end = self._find_closing_brace_in_source(source_lines, brace_line)
    if real_end > symbol.line_end:
        symbol.line_end = real_end

def _find_closing_brace_in_source(self, source_lines: list[str], open_line_idx: int) -> int:
    """Find matching '}' using brace counting on source lines."""
    depth = 0
    for i in range(open_line_idx, len(source_lines)):
        for ch in source_lines[i]:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0:
                    return i + 1  # 1-indexed
    return open_line_idx + 1
```

---

## Validation Checklist

After all tasks are complete, verify:

- [ ] `--clean` flag deletes old DB and creates fresh one
- [ ] Preprocessor strips UCLASS/USTRUCT/UENUM/UINTERFACE macros (with nested parens)
- [ ] Preprocessor strips *_API tokens (ENGINE_API, CORE_API, etc.)
- [ ] Preprocessor strips GENERATED_BODY() variants
- [ ] Preprocessor preserves exact line count (newlines intact)
- [ ] Tree-sitter produces `class_specifier` nodes for UE classes (not ERROR)
- [ ] `_get_base_classes()` extracts parent classes correctly
- [ ] Inheritance table has entries (was previously empty or near-empty)
- [ ] `get_class_hierarchy("AActor", direction="ancestors")` returns UObject chain
- [ ] `read_source("AActor", members_only=true)` returns member declarations
- [ ] Diagnostic output shows high `classes_clean` count and low `classes_error` count
- [ ] Diagnostic output shows `inheritance_resolved` >> `inheritance_failed`
- [ ] Source signatures in DB still contain original text (ENGINE_API etc.)
- [ ] FTS search still works (source_fts stores original lines)
- [ ] Reference builder produces cross-references (calls, types)
- [ ] Full reindex completes without errors

---

## Risk Notes

- **Regex edge cases:** Some UE macros may have unusual formatting (split across lines, embedded comments). The balanced-paren approach handles nesting but not comments inside macros. This is rare enough to ignore.
- **Reindex time:** Full engine + shaders takes several minutes. Test on a subset first if debugging.
- **DB deletion:** Once deleted, the old DB is gone. The editor will have no source index until reindex completes.
- **Tree-sitter version:** Ensure `tree-sitter` and `tree-sitter-cpp` are up to date. Older versions may have different error recovery behavior.
