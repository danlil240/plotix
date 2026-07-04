# Serena MCP Tools — Quick Reference

## Config
- Language server: **clangd** (`cpp`) in `.serena/project.yml` — switched from ccls (`cpp_ccls`) on 2026-06-25.
- Serena uses its **own bundled clangd 19.1.2** (`~/.serena/language_servers/static/ClangdLanguageServer/clangd/clangd_19.1.2/bin/clangd`), launched with `--background-index`. System also has `clangd-20`; build uses `clang++-20` (minor skew, harmless — only `IncludeCleaner` log noise).
- `compile_commands.json` is auto-symlinked at the project root by CMake on every configure (see `CMakeLists.txt`); clangd also reads `build/` directly.
- Editor lint is a **separate** clangd (the `kylin-clangd` IDE extension using `.vscode/settings.json`) — independent of Serena.
- Index: `serena project index` (~7 min, 618 files) builds `.serena/cache/cpp/document_symbols.pkl` (~18 MB). REQUIRED for fast `find_symbol`.
- Serena v1.5.3, LSP backend

## clangd Naming (CRITICAL — changed from ccls)
- C++ symbols use `/` NOT `::`: `spectra/Figure`, `spectra/Figure/show`.
- Python symbols use `.`: `spectra.FigureManager`.
- Name paths mirror clangd's hierarchical `documentSymbol` tree joined by `/`. Run `get_symbols_overview` first to see exact node names (nested namespaces may appear as one node).

## Performance — the #1 gotcha (root cause of "find_symbol stuck")
- `find_symbol` WITHOUT `relative_path` calls `request_full_symbol_tree`, which walks EVERY project file via `documentSymbol`. Each ROS/heavy C++ file needs a ~0.7-1.5s clangd preamble. With no cache that is ~7 min -> looks "stuck" (NOT a real hang; clangd `workspace/symbol` itself answers in ~0.05s).
- **ALWAYS pass `relative_path`** for targeted lookups -> a single `documentSymbol`, instant from cache.
- Keep the cache warm: run `serena project index` after fresh clone or large structural changes. Edited files re-index live on the next lookup.
- If `find_symbol` stays slow right after indexing, restart/reactivate the Serena MCP so it loads the fresh on-disk cache.

## Tool Cheatsheet
| Task | Tool | Key param |
|------|------|-----------|
| File structure | `get_symbols_overview` | `depth=1` |
| Find symbol | `find_symbol` | `name_path_pattern`, **`relative_path` (always!)**, `include_body` |
| Find references | `find_referencing_symbols` | `max_answer_chars=2000` (ALWAYS!) |
| Find declaration | `find_declaration` | regex with ONE capture group `(show)\(\)` |
| Find implementations | `find_implementations` | works for interfaces |
| Diagnostics | `get_diagnostics_for_file` | works for C++ and Python (clangd publishes diagnostics) |
| Pattern search | `search_for_pattern` | `restrict_search_to_code_files=True` |
| Edit symbol | `replace_symbol_body` | retrieve body first with `include_body=True` |
| Insert code | `insert_after_symbol` / `insert_before_symbol` | |
| Rename | `rename_symbol` | LSP refactoring, updates all refs |
| Safe delete | `safe_delete_symbol` | checks references first |
| File edit | `replace_content` | regex mode with `.*?` wildcards |

## clangd Behavior (vs the old ccls notes)
- `find_symbol` with `depth=1`: DOES return children for classes (hierarchical) — opposite of ccls.
- `find_symbol` `include_body=True`: returns the FULL body.
- `get_symbols_overview`: hierarchical (namespace -> class -> method).
- `get_diagnostics_for_file`: WORKS for C++ (clangd publishes diagnostics), not just Python.
- `find_declaration` / `find_implementations`: reliable with clangd.
- `find_symbol` without `relative_path`: walks all files (slow) — always provide path.

## MCP Transport Safety
- `find_referencing_symbols` can crash MCP with large results (300+ refs = ~84KB)
- ALWAYS set `max_answer_chars` ≤ 2000
- CLI fallback: `serena start-project-server --port 24283` + curl API (read-only tools only)

## Memories Available
`conventions`, `core`, `memory_maintenance`, `suggested_commands`, `task_completion`, `tech_stack`

## CLI Commands
```bash
serena tools list                     # list active tools
serena tools description <tool>       # tool help
serena memories list/read/check       # memory management
serena project health-check/index     # project management
```
