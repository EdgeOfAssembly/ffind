/*
 @licstart  The following is the entire license notice for the JavaScript code in this file.

 The MIT License (MIT)

 Copyright (C) 1997-2020 by Dimitri van Heesch

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 and associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 @licend  The above is the entire license notice for the JavaScript code in this file
*/
var NAVTREE =
[
  [ "ffind", "index.html", [
    [ "ffind Architecture Documentation", "md_ARCHITECTURE.html", [
      [ "Table of Contents", "md_ARCHITECTURE.html#autotoc_md2", null ],
      [ "Overview", "md_ARCHITECTURE.html#autotoc_md4", null ],
      [ "System Architecture", "md_ARCHITECTURE.html#autotoc_md6", null ],
      [ "Component Descriptions", "md_ARCHITECTURE.html#autotoc_md8", [
        [ "ffind Client (ffind.cpp)", "md_ARCHITECTURE.html#autotoc_md9", null ],
        [ "ffind-daemon (ffind-daemon.cpp)", "md_ARCHITECTURE.html#autotoc_md11", [
          [ "1. Index Management", "md_ARCHITECTURE.html#autotoc_md12", null ],
          [ "2. inotify Integration", "md_ARCHITECTURE.html#autotoc_md13", null ],
          [ "3. Query Handler", "md_ARCHITECTURE.html#autotoc_md14", null ],
          [ "4. Thread Pool", "md_ARCHITECTURE.html#autotoc_md15", null ],
          [ "5. SQLite Persistence", "md_ARCHITECTURE.html#autotoc_md16", null ]
        ] ]
      ] ],
      [ "Data Flow Diagrams", "md_ARCHITECTURE.html#autotoc_md18", [
        [ "Query Processing Flow", "md_ARCHITECTURE.html#autotoc_md19", null ],
        [ "Filesystem Change Propagation", "md_ARCHITECTURE.html#autotoc_md20", null ]
      ] ],
      [ "Protocol Specification", "md_ARCHITECTURE.html#autotoc_md22", [
        [ "Client-to-Daemon Binary Protocol", "md_ARCHITECTURE.html#autotoc_md23", null ],
        [ "Daemon-to-Client Response Format", "md_ARCHITECTURE.html#autotoc_md24", null ]
      ] ],
      [ "Threading Model", "md_ARCHITECTURE.html#autotoc_md26", [
        [ "Overview", "md_ARCHITECTURE.html#autotoc_md27", null ],
        [ "Thread Safety Guarantees", "md_ARCHITECTURE.html#autotoc_md28", null ]
      ] ],
      [ "Security Architecture", "md_ARCHITECTURE.html#autotoc_md30", [
        [ "Threat Model", "md_ARCHITECTURE.html#autotoc_md31", null ],
        [ "Security Layers", "md_ARCHITECTURE.html#autotoc_md32", null ],
        [ "Security-Critical Code Sections", "md_ARCHITECTURE.html#autotoc_md33", null ]
      ] ],
      [ "Storage and Persistence", "md_ARCHITECTURE.html#autotoc_md35", [
        [ "In-Memory Index Structure", "md_ARCHITECTURE.html#autotoc_md36", null ],
        [ "SQLite Schema (Optional Persistence)", "md_ARCHITECTURE.html#autotoc_md37", null ],
        [ "Persistence Flow", "md_ARCHITECTURE.html#autotoc_md38", null ]
      ] ],
      [ "Error Handling", "md_ARCHITECTURE.html#autotoc_md40", [
        [ "Error Handling Strategy", "md_ARCHITECTURE.html#autotoc_md41", null ],
        [ "Error Categories and Responses", "md_ARCHITECTURE.html#autotoc_md42", null ],
        [ "Error Logging", "md_ARCHITECTURE.html#autotoc_md43", null ]
      ] ],
      [ "Performance Optimizations", "md_ARCHITECTURE.html#autotoc_md45", [
        [ "1. Memory-Resident Index", "md_ARCHITECTURE.html#autotoc_md46", null ],
        [ "2. Thread Pool for Content Search", "md_ARCHITECTURE.html#autotoc_md48", null ],
        [ "3. Batched Result Transmission", "md_ARCHITECTURE.html#autotoc_md50", null ],
        [ "4. Zero-Copy File Reading (mmap)", "md_ARCHITECTURE.html#autotoc_md52", null ],
        [ "5. Path Index for Directory Queries", "md_ARCHITECTURE.html#autotoc_md54", null ],
        [ "6. inotify for Real-Time Updates", "md_ARCHITECTURE.html#autotoc_md56", null ],
        [ "7. SQLite Write-Ahead Logging (WAL)", "md_ARCHITECTURE.html#autotoc_md58", null ],
        [ "Performance Benchmarks Summary", "md_ARCHITECTURE.html#autotoc_md60", null ]
      ] ],
      [ "Appendix: Build and Development", "md_ARCHITECTURE.html#autotoc_md62", [
        [ "Build Process", "md_ARCHITECTURE.html#autotoc_md63", null ],
        [ "Development Guidelines", "md_ARCHITECTURE.html#autotoc_md64", null ]
      ] ],
      [ "Glossary", "md_ARCHITECTURE.html#autotoc_md66", null ]
    ] ],
    [ "Path Component Index - Benchmark Results", "md_BENCHMARK__RESULTS.html", [
      [ "Executive Summary", "md_BENCHMARK__RESULTS.html#autotoc_md69", null ],
      [ "Test Environment", "md_BENCHMARK__RESULTS.html#autotoc_md70", null ],
      [ "Benchmark Results", "md_BENCHMARK__RESULTS.html#autotoc_md71", [
        [ "Benchmark 3: Path-Filtered Queries (PRIMARY TARGET)", "md_BENCHMARK__RESULTS.html#autotoc_md72", null ],
        [ "Full Benchmark Suite", "md_BENCHMARK__RESULTS.html#autotoc_md73", null ]
      ] ],
      [ "Implementation Details", "md_BENCHMARK__RESULTS.html#autotoc_md74", [
        [ "Path Index Structure", "md_BENCHMARK__RESULTS.html#autotoc_md75", null ],
        [ "Query Optimization Logic", "md_BENCHMARK__RESULTS.html#autotoc_md76", null ],
        [ "Memory Overhead", "md_BENCHMARK__RESULTS.html#autotoc_md77", null ]
      ] ],
      [ "Performance Analysis", "md_BENCHMARK__RESULTS.html#autotoc_md78", [
        [ "Why 4.2x instead of 10x?", "md_BENCHMARK__RESULTS.html#autotoc_md79", null ],
        [ "Performance Breakdown", "md_BENCHMARK__RESULTS.html#autotoc_md80", null ]
      ] ],
      [ "Conclusions", "md_BENCHMARK__RESULTS.html#autotoc_md81", [
        [ "Success Criteria", "md_BENCHMARK__RESULTS.html#autotoc_md82", null ],
        [ "Key Achievements", "md_BENCHMARK__RESULTS.html#autotoc_md83", null ],
        [ "Production Readiness", "md_BENCHMARK__RESULTS.html#autotoc_md84", null ],
        [ "Future Optimizations", "md_BENCHMARK__RESULTS.html#autotoc_md85", null ]
      ] ],
      [ "Recommendation", "md_BENCHMARK__RESULTS.html#autotoc_md86", null ]
    ] ],
    [ "Benchmark Execution Evidence", "md_benchmarks_2EXECUTION__EVIDENCE.html", [
      [ "Execution Date", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md88", null ],
      [ "Test Environment", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md89", null ],
      [ "Test Corpus", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md90", null ],
      [ "Actual Benchmark Results", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md91", null ],
      [ "Analysis", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md92", [
        [ "Strong Performance Areas", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md93", null ],
        [ "Areas for Improvement", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md94", null ]
      ] ],
      [ "Verification", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md95", null ],
      [ "Conclusion", "md_benchmarks_2EXECUTION__EVIDENCE.html#autotoc_md96", null ]
    ] ],
    [ "ffind", "md_README.html", [
      [ "Table of Contents", "md_README.html#autotoc_md124", null ],
      [ "What is ffind?", "md_README.html#autotoc_md126", null ],
      [ "Why use ffind?", "md_README.html#autotoc_md127", null ],
      [ "Features", "md_README.html#autotoc_md128", null ],
      [ "Comparison with Other Tools", "md_README.html#autotoc_md129", null ],
      [ "Performance Benchmarks", "md_README.html#autotoc_md130", [
        [ "Indexing Performance", "md_README.html#autotoc_md131", null ],
        [ "Benchmark Methodology", "md_README.html#autotoc_md132", null ]
      ] ],
      [ "Quick Start", "md_README.html#autotoc_md133", null ],
      [ "Requirements", "md_README.html#autotoc_md134", null ],
      [ "Build", "md_README.html#autotoc_md135", [
        [ "Install Dependencies", "md_README.html#autotoc_md136", null ],
        [ "Compile", "md_README.html#autotoc_md137", null ],
        [ "Build Cache-Flush Utility (Optional - for benchmarking)", "md_README.html#autotoc_md138", null ]
      ] ],
      [ "Install", "md_README.html#autotoc_md139", null ],
      [ "Usage", "md_README.html#autotoc_md140", [
        [ "Start the daemon", "md_README.html#autotoc_md141", null ],
        [ "SQLite Persistence (Optional)", "md_README.html#autotoc_md142", null ],
        [ "Multiple Root Directories", "md_README.html#autotoc_md143", null ],
        [ "Search examples", "md_README.html#autotoc_md144", null ],
        [ "Size units", "md_README.html#autotoc_md145", null ],
        [ "Size operators", "md_README.html#autotoc_md146", null ],
        [ "Time operators", "md_README.html#autotoc_md147", null ],
        [ "Content search methods", "md_README.html#autotoc_md148", [
          [ "Fixed string search (<span class=\"tt\">-c</span>)", "md_README.html#autotoc_md149", null ],
          [ "Regex search (<span class=\"tt\">-c</span> + <span class=\"tt\">-r</span>)", "md_README.html#autotoc_md150", null ],
          [ "Glob search (<span class=\"tt\">-g</span>)", "md_README.html#autotoc_md151", null ]
        ] ],
        [ "Context lines", "md_README.html#autotoc_md152", [
          [ "Output format", "md_README.html#autotoc_md153", null ]
        ] ],
        [ "Color output", "md_README.html#autotoc_md154", null ]
      ] ],
      [ "Directory Monitoring", "md_README.html#autotoc_md155", null ],
      [ "Service Management", "md_README.html#autotoc_md156", [
        [ "Gentoo (OpenRC)", "md_README.html#autotoc_md157", null ]
      ] ],
      [ "FAQ", "md_README.html#autotoc_md158", [
        [ "Q: How fast is it?", "md_README.html#autotoc_md159", null ],
        [ "Q: Does it use a lot of RAM?", "md_README.html#autotoc_md160", null ],
        [ "Q: What about huge directory trees?", "md_README.html#autotoc_md161", null ],
        [ "Q: How does persistence work?", "md_README.html#autotoc_md162", null ],
        [ "Q: Can I use it on network filesystems (NFS, CIFS)?", "md_README.html#autotoc_md163", null ],
        [ "Q: How do I search multiple directories?", "md_README.html#autotoc_md164", null ],
        [ "Q: What's the difference between <span class=\"tt\">-c</span> and <span class=\"tt\">-g</span>?", "md_README.html#autotoc_md165", null ]
      ] ],
      [ "License", "md_README.html#autotoc_md166", null ],
      [ "Author", "md_README.html#autotoc_md167", null ]
    ] ],
    [ "ffind TODO Summary - January 2026", "md_TODO.html", [
      [ "Implemented (current version)", "md_TODO.html#autotoc_md169", null ],
      [ "Remaining / Not Implemented", "md_TODO.html#autotoc_md170", null ],
      [ "Low Priority / Optional", "md_TODO.html#autotoc_md171", null ]
    ] ],
    [ "Classes", "annotated.html", [
      [ "Class List", "annotated.html", "annotated_dup" ],
      [ "Class Index", "classes.html", null ],
      [ "Class Members", "functions.html", [
        [ "All", "functions.html", null ],
        [ "Functions", "functions_func.html", null ],
        [ "Variables", "functions_vars.html", null ]
      ] ]
    ] ],
    [ "Files", "files.html", [
      [ "File List", "files.html", "files_dup" ],
      [ "File Members", "globals.html", [
        [ "All", "globals.html", null ],
        [ "Functions", "globals_func.html", null ],
        [ "Variables", "globals_vars.html", null ],
        [ "Enumerations", "globals_enum.html", null ]
      ] ]
    ] ]
  ] ]
];

var NAVTREEINDEX =
[
"annotated.html",
"structMappedFile.html#ad7fe23fe3b709f737aa6b42330e911c3"
];

var SYNCONMSG = 'click to disable panel synchronization';
var SYNCOFFMSG = 'click to enable panel synchronization';
var LISTOFALLMEMBERS = 'List of all members';