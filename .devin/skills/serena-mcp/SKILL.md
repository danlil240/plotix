---
name: serena-mcp
description: Serena MCP Tools — Semantic code analysis and editing with clangd for C++20 and Python
---

# Serena MCP — Semantic Code Tools Skill

> Serena provides LSP-backed semantic tools via MCP for the Spectra project.
> This skill covers proper usage, clangd-specific behavior, and the performance rules that keep `find_symbol` fast.
> NOTE: The project switched from ccls to clangd on 2026-06-25. If you see old notes referencing `::` naming or ccls, they are obsolete.

---

## 1. Project Configuration

### Language server: clangd (`cpp`)

The project uses **clangd** for C++ symbol analysis. Configured in `.serena/project.yml`:

```yaml
languages:
  - cpp
  - python
```

- Serena uses its **own bundled clangd 19.1.2** (`~/.serena/language_servers/static/ClangdLanguageServer/clangd/clangd_19.1.2/bin/clangd`), launched with `--background-index`.
- The system also has `clangd-20`; the build compiles with `clang++-20`. The 19-vs-20 skew is harmless (only `IncludeCleaner` log noise).
- The **editor** lint is a separate clangd (the `kylin-clangd` IDE extension using `.vscode/settings.json`) — independent of Serena.

### Prerequisites

- `compile_commands.json` is auto-symlinked at the project root by CMake on every configure (see `CMakeLists.txt`); clangd also reads `build/` directly.
- After a fresh clone or major structural changes: `serena project index` (~7 min for ~618 files). This builds `.serena/cache/cpp/document_symbols.pkl` (~18 MB) and is REQUIRED for fast lookups.

---

## 2. clangd Naming Convention (Critical)

clangd returns symbols as a hierarchical `documentSymbol` tree; Serena joins the path with `/`, NOT `::`.

### C++ symbols — use `/`

```
# CORRECT
find_symbol(name_path_pattern="spectra/Figure")
find_symbol(name_path_pattern="spectra/Figure/show")
find_symbol(name_path_pattern="spectra/Figure/set_size")
find_symbol(name_path_pattern="spectra/FigureManager")

# WRONG — ccls-style, will return empty results
find_symbol(name_path_pattern="spectra::Figure")
find_symbol(name_path_pattern="spectra::Figure::show")
```

When unsure of exact node names (nested namespaces such as `spectra::adapters::ros2` may collapse into a single node), run `get_symbols_overview` on the file first.

### Python symbols — use `.`

```
find_symbol(name_path_pattern="spectra.FigureManager")
find_symbol(name_path_pattern="spectra.FigureManager.create_figure")
```

### Overloaded methods — use `[index]`

```
find_symbol(name_path_pattern="spectra/Figure/style[0]")  # first overload
find_symbol(name_path_pattern="spectra/Figure/style[1]")  # second overload
```

---

## 3. Performance Rules (read this first)

The #1 failure mode is `find_symbol` "getting stuck". It is almost never a real hang.

- `find_symbol` **without** `relative_path` calls `request_full_symbol_tree`, which walks **every** project file via `documentSymbol`. Each ROS/heavy C++ file costs a ~0.7-1.5s clangd preamble, so with no cache a global search takes ~7 min and looks stuck. (clangd itself is fast — a direct `workspace/symbol` answers in ~0.05s; Serena just does not use that path.)
- **ALWAYS pass `relative_path`** for targeted lookups → a single `documentSymbol`, instant from cache.
- Keep the cache warm with `serena project index`. Edited files re-index live on the next lookup (~1.5s each).
- If lookups stay slow right after indexing, restart/reactivate the Serena MCP so it loads the fresh on-disk cache.
- To debug a suspected hang without stalling the agent, probe clangd directly in a terminal (initialize → didOpen → documentSymbol / workspace/symbol with timeouts) instead of calling the MCP `find_symbol`.

---

## 4. Tool-by-Tool Guide

### 4.1 `get_symbols_overview` — File structure (cheapest entry point)

```
get_symbols_overview(relative_path="include/spectra/figure.hpp", depth=1)
```

clangd returns a **hierarchical** tree (Namespace → Class → Method/Field). Use this FIRST to understand a file and to discover exact `/`-joined name paths.

### 4.2 `find_symbol` — Symbol search

```
# Exact match (ALWAYS provide relative_path)
find_symbol(name_path_pattern="spectra/Figure", relative_path="include/spectra/figure.hpp", include_body=False)

# Substring matching
find_symbol(name_path_pattern="Fig", substring_matching=True, max_matches=10)

# With full body
find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)
```

**clangd behavior:**
- `depth=1` DOES return children for classes (hierarchical) — unlike ccls.
- `include_body=True` returns the FULL body.
- Without `relative_path`, the call walks all files (slow). Always provide it when you know the file.

### 4.3 `find_referencing_symbols` — Find all references

```
find_referencing_symbols(
    name_path="spectra/Figure",
    relative_path="include/spectra/figure.hpp",
    max_answer_chars=2000  # ALWAYS set this!
)
```

**Critical:** Always set `max_answer_chars` (recommend ≤2000). Core classes have 300+ references (~84KB); without the limit the MCP transport can crash.

### 4.4 `find_declaration` — Find declaration from usage

Requires a regex with **exactly one capture group**.

```
# CORRECT — one capture group
find_declaration(regex="(show)\(\)", relative_path="include/spectra/figure.hpp")

# WRONG — no capture group
find_declaration(regex="show\(\)", relative_path="include/spectra/figure.hpp")
```

### 4.5 `find_implementations` — Find interface implementations

```
find_implementations(name_path="spectra/IRenderer", relative_path="src/render/renderer.hpp", max_answer_chars=1000)
```

Works for interfaces; concrete (non-virtual) classes may return `[]`.

### 4.6 `get_diagnostics_for_file` — File diagnostics

```
get_diagnostics_for_file(relative_path="include/spectra/figure.hpp")   # C++ — works (clangd publishes diagnostics)
get_diagnostics_for_file(relative_path="python/spectra/figure.py")     # Python — works
```

clangd publishes diagnostics via LSP, so this works for C++ as well as Python (a key improvement over ccls, which returned `{}`).

### 4.7 `search_for_pattern` — Regex search across codebase

```
search_for_pattern(
    substring_pattern="class Figure",
    restrict_search_to_code_files=True,
    context_lines_before=0,
    context_lines_after=1
)
```

Use for non-symbol searches (comments, config patterns, etc.).

### 4.8 `replace_symbol_body` — Edit a symbol's body

```
# First retrieve the current body
find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)

# Then replace it
replace_symbol_body(
    name_path="spectra/Figure/show",
    relative_path="include/spectra/figure.hpp",
    body="void show() const { /* new implementation */ }"
)
```

Only use after retrieving the current body with `include_body=True`.

### 4.9 `insert_before_symbol` / `insert_after_symbol` — Insert code

```
insert_after_symbol(
    name_path="spectra/Figure/show",
    relative_path="include/spectra/figure.hpp",
    body="void newMethod();"
)

insert_before_symbol(
    name_path="spectra/Figure",
    relative_path="include/spectra/figure.hpp",
    body="#include <new_header>"
)
```

### 4.10 `replace_content` — File-based regex replacement

```
replace_content(
    relative_path="src/core/figure.cpp",
    needle="old_pattern.*?end_of_text",
    repl="new_content",
    mode="regex"
)
```

Use regex wildcards (`.*?`) to avoid quoting entire blocks.

### 4.11 `rename_symbol` — Rename across codebase

```
rename_symbol(
    name_path="spectra/Figure/oldMethod",
    relative_path="include/spectra/figure.hpp",
    new_name="newMethod"
)
```

Uses LSP refactoring — updates all references automatically.

### 4.12 `safe_delete_symbol` — Delete with reference check

```
safe_delete_symbol(
    name_path_pattern="spectra/Figure/unusedMethod",
    relative_path="include/spectra/figure.hpp"
)
```

Returns the list of references if the symbol is still used, preventing accidental deletion.

---

## 5. Memory Tools

```
list_memories()
read_memory(memory_name="core")
write_memory(memory_name="new_topic", content="# Title\n\n...")
edit_memory(memory_name="core", needle="old text", repl="new text", mode="literal")
```

Available memories: `conventions`, `core`, `memory_maintenance`, `serena_tools`, `suggested_commands`, `task_completion`, `tech_stack`.

---

## 6. clangd Behavior (was ccls)

| Aspect | clangd (`cpp`, current) | ccls (`cpp_ccls`, removed) |
|--------|-------------------------|----------------------------|
| Symbol naming | `spectra/Figure` (slash path) | `spectra::Figure` (`::` scope) |
| `get_symbols_overview` | Hierarchical (Namespace → Class → Method) | Flat list with `::` names grouped by kind |
| `find_symbol` `depth=1` | Returns children (methods, fields) | Returned only the class |
| `find_symbol` body | Full method body | Minimal (single line) |
| `get_diagnostics_for_file` | Works for C++ and Python | Returned `{}` for C++ |
| `find_declaration` | Reliable | Often "No symbol declaration found" |
| `compile_commands.json` | Auto-symlinked at root by CMake; also reads `build/` | Needed manual root symlink |
| Global `find_symbol` (no `relative_path`) | Walks all files (~7 min uncached) → always pass `relative_path` | Same problem |
| Indexing | `serena project index` (~7 min, 618 files) → 18 MB cache | ~6.5 min |

---

## 7. Common Workflows

### Workflow 1: Understand a file
```
1. get_symbols_overview(relative_path="include/spectra/figure.hpp", depth=1)
2. find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)
3. read_file(... )  # for raw content if needed
```

### Workflow 2: Find all callers of a method
```
1. find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp")
2. find_referencing_symbols(name_path="spectra/Figure/show", relative_path="include/spectra/figure.hpp", max_answer_chars=2000)
```

### Workflow 3: Edit a method
```
1. find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)
2. replace_symbol_body(name_path="spectra/Figure/show", relative_path="include/spectra/figure.hpp", body="void show() const { /* new */ }")
```

### Workflow 4: Add a new method to a class
```
1. get_symbols_overview(relative_path="include/spectra/figure.hpp", depth=1)
2. insert_after_symbol(name_path="spectra/Figure/has_animation", relative_path="include/spectra/figure.hpp", body="void newMethod();")
```

### Workflow 5: Rename a method across codebase
```
1. find_symbol(name_path_pattern="spectra/Figure/oldName", relative_path="include/spectra/figure.hpp")
2. rename_symbol(name_path="spectra/Figure/oldName", relative_path="include/spectra/figure.hpp", new_name="newName")
```

---

## 8. Troubleshooting

### `find_symbol` is slow / "stuck"
- You almost certainly omitted `relative_path` → it is walking every file. Pass `relative_path`.
- Ensure the cache exists: run `serena project index` (terminal). Verify `.serena/cache/cpp/document_symbols.pkl` exists.
- If still slow after indexing, restart/reactivate the Serena MCP so it loads the fresh cache.

### `find_symbol` returns `[]`
- Check naming convention: use `/` not `::`.
- Run `get_symbols_overview` to see the exact name path.
- Try `substring_matching=True`.

### `find_referencing_symbols` crashes MCP transport
- Always set `max_answer_chars` (≤2000). Narrow scope with `relative_path`.

### MCP transport errors
- Restart the MCP server if tools become unavailable.
- CLI fallback: `serena start-project-server --port 24283` + curl API (read-only tools only).

---

## 9. CLI Fallback

```bash
# Start project server
serena start-project-server --port 24283 --log-level WARNING &

# Test heartbeat
curl -s http://127.0.0.1:24283/heartbeat

# Call any read-only tool (note the / naming)
curl -s -X POST http://127.0.0.1:24283/query_project \
  -H "Content-Type: application/json" \
  -d '{"project_name":"Spectra","tool_name":"find_symbol","tool_params_json":"{\"name_path_pattern\":\"spectra/Figure\",\"relative_path\":\"include/spectra/figure.hpp\"}"}'
```

**Note:** Project server only allows read-only tools. Write tools require an MCP connection.

### Useful CLI commands
```bash
serena tools list                    # List all active tools
serena tools description find_symbol # Get tool description
serena memories list                 # List memories
serena project health-check          # Health check
serena project index                 # Index project (run after major changes)
```
