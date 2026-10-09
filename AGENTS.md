# Repository Agent Guidelines

## Tech Stack & Language Directives
1. **Preferred Standard:** C++23 is the strongly preferred language for all core build system code and modifications.
2. **Strictly Prohibited:**
   - **No Garbage-Collected or Interpreted Languages:** Zero tolerance for Python, JavaScript, Java, Go, C#, etc., in the core build system runtime.
   - **No Objective-C / Objective-C++:** Permanently forbidden across the entire repository.
   - **No Raw Assembly:** Do not write inline or standalone assembly; rely on standard C++23 abstractions or compiler intrinsics where strictly necessary.
   - **No C:** Do not use legacy C paradigms where modern C++23 idioms, types, and safety guarantees apply.

## Priority Code & PR Review Constraints
This is a low-level build system. During code or PR reviews, aggressively flag:

1. **Memory Safety & Leaks (SIGSEGV):**
   - Dangling pointers, null-pointer dereferences, uninitialized variables.
   - Out-of-bounds array, vector, or span access.
   - Dynamic allocations missing proper RAII/cleanup, unclosed file descriptors, or unbounded cache growth.

2. **Instruction & CPU Hazards (SIGILL):**
   - Unsupported or non-portable instruction assumptions across architectures without proper feature checks.
   - Function pointer corruption, unsafe standard library bit casts, or invalid reinterprets across boundaries.

3. **Performance & Bloat:**
   - Synchronous or blocking I/O operations inside hot execution loops.
   - Redundant disk/file-system traversals or inefficient $O(n^2)$ dependency graph resolution.
   - Unnecessary external library dependencies, binary size inflation, or unnecessary runtime overhead.

## Review Output Format
- Focus strictly on logic, memory safety, performance, and language compliance (ignore minor formatting/style covered by auto-formatters).
- Provide concise summaries of identified risks.
- Always include explicit terminal commands for testing and merging the branch when a review passes.
