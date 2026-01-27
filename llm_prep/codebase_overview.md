# LLM-Ready Codebase Overview — 2026-01-27

**Project:** ffind-1.0

## Directory Structure

```text
.
├── llm_prep
│   ├── dot_graphs_doxygen
│   ├── dot_graphs_pyreverse
│   └── codebase_structure.txt
├── ffind.1
├── ffind.cpp
├── ffind-daemon.8
├── ffind-daemon.cpp
├── Makefile
└── TODO.md

4 directories, 7 files
```

## Code Statistics

```text
github.com/AlDanial/cloc v 2.00  T=0.04 s (113.9 files/s, 12921.3 lines/s)
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C++                              2             67              2            428
Markdown                         1              3              0             30
make                             1              7              1             16
Text                             1              1              0             12
-------------------------------------------------------------------------------
SUM:                             5             78              3            486
-------------------------------------------------------------------------------
```

## Doxygen Documentation (C/C++)

- Browse: `llm_prep/doxygen_output/html/index.html`
- DOT graphs for LLM context:
  - `ffind-daemon_8cpp__incl.dot` (3 KB)
  - `ffind-daemon_8cpp_a168b2a4f368ef1066408f4976ec8431c_cgraph.dot` (1 KB)
  - `ffind-daemon_8cpp_a168b2a4f368ef1066408f4976ec8431c_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a205ebedc5089e1667f29c6e01c934afc_icgraph.dot` (1 KB)
  - `ffind-daemon_8cpp_a3c04138a5bfe5d72780bb7e82a18e627_cgraph.dot` (2 KB)
  - `ffind-daemon_8cpp_a59ef0737c0ccd8f244dee8a52d4c1e9b_cgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a59ef0737c0ccd8f244dee8a52d4c1e9b_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a869452f0c0952acd39230fcf0ef89c1e_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aa29c4f34a63bacc7d3bcd55e101133fd_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ab326a3e5e38794f3fd51fabbcb5f7ca6_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ae0c07b995ce21a76ac28db67132a5565_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aef341a13753571130f788a38d2ce3b6f_icgraph.dot` (0 KB)
  - `ffind_8cpp__incl.dot` (1 KB)
  - `graph_legend.dot` (1 KB)
  - `structEntry__coll__graph.dot` (2 KB)

## Symbol Index

- `llm_prep/tags` - ctags file for symbol navigation

## LLM Context Files

- `llm_system_prompt.md` - System prompt for LLM sessions
- `project_guidance.md` - Best practices and guidelines

## How to Use

1. Copy this file as initial context for your LLM
2. Paste relevant DOT graphs for architecture questions
3. Reference specific files when asking about code
