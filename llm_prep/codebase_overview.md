# LLM-Ready Codebase Overview — 2026-01-30

**Project:** ffind

## Directory Structure

```text
.
├── benchmarks
│   ├── cache_flush.c
│   ├── EXECUTION_EVIDENCE.md
│   ├── Makefile
│   ├── README.md
│   └── run_real_benchmarks.sh
├── llm_prep
│   ├── dot_graphs_doxygen
│   ├── dot_graphs_pyreverse
│   └── codebase_structure.txt
├── tests
│   ├── run_tests.sh
│   ├── test_multiple_roots.sh
│   └── test_persistence.sh
├── ARCHITECTURE.md
├── BENCHMARK_RESULTS.md
├── Doxyfile
├── etc-conf.d-ffind-daemon.example
├── ffind.1
├── ffind.cpp
├── ffind-daemon.8
├── ffind-daemon.cpp
├── ffind-daemon.openrc
├── LICENSE
├── Makefile
├── README.md
└── TODO.md

6 directories, 22 files
```

## Code Statistics

```text
github.com/AlDanial/cloc v 2.00  T=0.61 s (26.1 files/s, 12955.5 lines/s)
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
C++                              2            400            767           2337
Markdown                         6            495              0           1725
Bourne Shell                     4            269            250           1568
make                             2             13             11             38
Text                             1              1              0             29
C                                1              7             20             26
-------------------------------------------------------------------------------
SUM:                            16           1185           1048           5723
-------------------------------------------------------------------------------
```

## Doxygen Documentation (C/C++)

- Browse: `llm_prep/doxygen_output/html/index.html`
- DOT graphs for LLM context:
  - `cache__flush_8c__incl.dot` (1 KB)
  - `classThreadPool__coll__graph.dot` (4 KB)
  - `ffind-daemon_8cpp__incl.dot` (4 KB)
  - `ffind-daemon_8cpp_a09c579b994a755d8cbd88831609690a8_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a09d220bcc638f3d7f76e2c424b095129_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a168b2a4f368ef1066408f4976ec8431c_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a1fe291c8a4f4e82731c0266c42943dee_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a273768174833995d8ad2eb25994d3530_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a2c4f38e0541583328a2ff1fdfd663f6e_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a3c04138a5bfe5d72780bb7e82a18e627_cgraph.dot` (9 KB)
  - `ffind-daemon_8cpp_a3c6ea839f8269e1c879bece01b9d43a0_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a3e4024f4f5d79a6af4aa3ef6153bbec5_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a3ee509d80aed50ed80b0a8a044d6d8aa_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a5054c36923934387c6f7605dd1a2f3c9_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a661259a347bae2d165d1653683d41472_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a78bf8646c074be1959943f73bd7458c0_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a7b04429420875e5e02ebc2893cfa8d6f_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a869452f0c0952acd39230fcf0ef89c1e_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_a9b45a13c2b22d9b98045a6bbb3201183_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aa29c4f34a63bacc7d3bcd55e101133fd_cgraph.dot` (1 KB)
  - `ffind-daemon_8cpp_aa29c4f34a63bacc7d3bcd55e101133fd_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aa3dce2cc3e8187c974d70de730744bae_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aa4eb8c59e406fff61f4cf476db590854_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ab69d236a01aa6689b1c7709979bf0262_cgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ab69d236a01aa6689b1c7709979bf0262_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ac2acdbfe25fe356cd7052c036bce7072_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ac90ea00129fdd44112a544f6c4b8ffe8_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ac9789d3167d8e76e0a9fe8d5bc23910a_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_aca41db1c34064861e1972d04aea0271c_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_adb67f2caafe3ae5a6a2a28def0468a9f_icgraph.dot` (1 KB)
  - `ffind-daemon_8cpp_adbd569fc9bff2199fe17c1a98905233b_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ae56faec3f7affb7dbacee7c21a944169_cgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ae56faec3f7affb7dbacee7c21a944169_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_ae7ca651aefd5176935c0a94a0389e957_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_af8f154bf210b7ff6d50433da876083e6_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_afe3d8064e402d5fda40480e60546ef2f_icgraph.dot` (0 KB)
  - `ffind-daemon_8cpp_affd557bc34b0546314dc6394d0ba6219_icgraph.dot` (0 KB)
  - `ffind_8cpp__incl.dot` (1 KB)
  - `ffind_8cpp_a3c04138a5bfe5d72780bb7e82a18e627_cgraph.dot` (0 KB)
  - `ffind_8cpp_adb67f2caafe3ae5a6a2a28def0468a9f_icgraph.dot` (0 KB)
  - `graph_legend.dot` (1 KB)
  - `structConfig__coll__graph.dot` (2 KB)
  - `structEntry__coll__graph.dot` (2 KB)
  - `structMappedFile__coll__graph.dot` (1 KB)
  - `structMappedFile_ad7fe23fe3b709f737aa6b42330e911c3_cgraph.dot` (0 KB)
  - `structMappedFile_ad7fe23fe3b709f737aa6b42330e911c3_icgraph.dot` (0 KB)
  - `structPathIndex__coll__graph.dot` (5 KB)

## Symbol Index

- `llm_prep/tags` - ctags file for symbol navigation

## LLM Context Files

- `llm_system_prompt.md` - System prompt for LLM sessions
- `project_guidance.md` - Best practices and guidelines

## How to Use

1. Copy this file as initial context for your LLM
2. Paste relevant DOT graphs for architecture questions
3. Reference specific files when asking about code
