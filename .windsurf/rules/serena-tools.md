---
trigger: always_on
---

# Serena MCP Tools — Mandatory Usage Rules

> Serena provides semantic code tools via MCP that are faster and more token-efficient than grep/read for symbol-level operations.
> Use them **by default** for all C++/Python code exploration and editing in this project.

---

## 1. When to Use Serena (Mandatory)

**Always use Serena tools instead of grep/read_file when:**

- **Finding a symbol** (class, method, field) → `find_symbol` instead of `grep_search`
- **Getting file structure** → `get_symbols_overview` instead of `read_file` (saves 80%+ tokens)
- **Finding references** → `find_referencing_symbols` instead of `grep_search`
- **Finding implementations** → `find_implementations` instead of `grep_search`
- **Getting diagnostics** → `get_diagnostics_for_file` instead of building/compiling
- **Editing a symbol body** → `replace_symbol_body` instead of `edit` (precise, no line-number guessing)
- **Inserting code before/after a symbol** → `insert_before_symbol` / `insert_after_symbol`
- **Renaming a symbol** → `rename_symbol` instead of multi-file find/replace
- **Safe-deleting a symbol** → `safe_delete_symbol` (checks references first)
- **Searching for a pattern** → `search_for_pattern` (regex over code files, same as grep but integrated)
- **Reading memories** → `read_memory` / `list_memories` (project context, not code)

**Use grep/read_file only when:**
- You need to read raw file content (non-symbol context, comments, config files)
- Serena MCP is unavailable or transport-errored
- Searching non-code files (CMake, YAML, Markdown, .gitignore, etc.)
- You already know the exact file and line range

---

## 2. clangd Naming Convention (Critical)

This project uses **clangd** (`cpp`) as the C++ language server (switched from ccls on 2026-06-25). Serena uses its bundled **clangd 19.1.2**.

### Symbol names use `/` not `::`

```
# CORRECT — clangd naming
find_symbol("spectra/Figure")
find_symbol("spectra/Figure/show")
find_symbol("spectra/Figure/set_size")

# WRONG — ccls naming (will return empty results)
find_symbol("spectra::Figure")
find_symbol("spectra::Figure::show")
```

Name paths mirror clangd's hierarchical `documentSymbol` tree joined by `/`. When unsure of exact node names (nested namespaces can collapse into one node), run `get_symbols_overview` on the file first.

### Python symbols use `.` separator

```python
find_symbol("spectra.FigureManager")
find_symbol("spectra.FigureManager.create_figure")
```

---

## 3. Token Efficiency Guide

| Task | Naive approach | Serena approach | Token savings |
|------|---------------|-----------------|---------------|
| Understand file structure | `read_file` (full file) | `get_symbols_overview` (symbol tree) | ~80% |
| Find a class definition | `grep_search` + `read_file` | `find_symbol` (direct) | ~90% |
| Find all references | `grep_search` (many results) | `find_referencing_symbols` (structured) | ~60% |
| Edit a method | `read_file` + `edit` (line numbers) | `find_symbol` + `replace_symbol_body` | ~70% |
| Check for errors | Build + parse output | `get_diagnostics_for_file` | ~95% |
| Insert a new function | `read_file` + `edit` | `insert_after_symbol` | ~80% |

---

## 4. Known Limitations & Workarounds

### clangd behavior
- **`find_symbol` with `depth=1`** DOES return children for classes (hierarchical) — unlike ccls.
- **`find_symbol` body retrieval** returns the FULL body.
- **`get_diagnostics_for_file`** WORKS for C++ (clangd publishes diagnostics via LSP) as well as Python.
- **`find_declaration`** requires a regex with exactly **one capture group**: `(show)\(\)` not `show\(\)`.
- **`find_implementations`** works for interfaces; concrete classes may return `[]`.

### Performance (CRITICAL — root cause of "find_symbol stuck")
- **`find_symbol` WITHOUT `relative_path`** calls `request_full_symbol_tree`, which walks EVERY project file via `documentSymbol`. Each ROS/heavy C++ file costs a ~0.7-1.5s clangd preamble, so with no cache a global search takes ~7 min and LOOKS stuck (clangd itself is fast — `workspace/symbol` answers in ~0.05s). **ALWAYS pass `relative_path`.**
- **`find_referencing_symbols`** can return very large results (300+ refs for core classes). Always set `max_answer_chars` to limit output.
- **Project indexing** (`serena project index`) takes ~7 min for ~618 files and builds `.serena/cache/cpp/document_symbols.pkl` (~18 MB). Run it after a fresh clone or large structural changes; it is REQUIRED for fast `find_symbol`. Edited files re-index live on the next lookup. If lookups stay slow right after indexing, restart/reactivate the Serena MCP so it loads the fresh on-disk cache.

### MCP transport
- **Large `find_referencing_symbols` results** can crash the MCP transport. Always use `max_answer_chars` (recommend ≤2000).

---

## 5. Required Setup

- `.serena/project.yml` must have `cpp` (clangd), NOT `cpp_ccls`, in the `languages` list.
- `compile_commands.json` is auto-symlinked at the project root by CMake on every configure (see `CMakeLists.txt`); clangd also reads `build/` directly.
- clangd available: system `clangd-20` (`sudo apt install clangd-20`); Serena downloads/uses its own bundled `clangd 19.1.2` automatically.
- After a fresh clone or major structural changes, run: `serena project index`.

---

## 6. Quick Reference

```
# Get file structure (cheapest way to understand a file)
get_symbols_overview(relative_path="include/spectra/figure.hpp", depth=1)

# Find a specific symbol (ALWAYS pass relative_path)
find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)

# Find all references (ALWAYS set max_answer_chars)
find_referencing_symbols(name_path="spectra/Figure", relative_path="include/spectra/figure.hpp", max_answer_chars=2000)

# Edit a method body
find_symbol(name_path_pattern="spectra/Figure/show", relative_path="include/spectra/figure.hpp", include_body=True)
# then: replace_symbol_body(name_path="spectra/Figure/show", relative_path="include/spectra/figure.hpp", body="...")

# Insert new code after a symbol
insert_after_symbol(name_path="spectra/Figure/show", relative_path="include/spectra/figure.hpp", body="void Figure::newMethod() { ... }")

# Check Python file for errors
get_diagnostics_for_file(relative_path="python/spectra/figure.py")
```
