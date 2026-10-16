# Repository Agent Guidelines

## Tech Stack & Language Directives

1. **Required Standard:** C++23 is the required language for all new and modified NightBuild core implementation code.

2. **Prohibited Languages in NightBuild Itself:**
   - **No C:** Do not implement NightBuild's own code in C. Use C++23 features, types, and safety guarantees instead.
   - **No Objective-C / Objective-C++:** Do not implement NightBuild itself in Objective-C or Objective-C++.
   - **No Assembly:** Do not write inline or standalone assembly. Use standard C++23 facilities and compiler intrinsics where appropriate.

3. **Explicit Exceptions:**
   - These restrictions apply to NightBuild's own implementation, not third-party dependencies, development scripts, or code-generation tools.
   - NightBuild must support building projects written in other languages, including C, Objective-C, Objective-C++, assembly, Rust, Swift, and others as appropriate.
   - Third-party dependencies may be implemented in any language. Do not reject or rewrite a dependency solely because of its implementation language.

## Priority Code & PR Review Constraints

NightBuild is a low-level build system. During code and PR reviews, carefully scrutinize the following areas.

### 1. Memory Safety & Resource Management

Flag:
- Dangling pointers, null-pointer dereferences, and uninitialized variables.
- Out-of-bounds array, vector, or span access.
- Dynamic allocations without appropriate RAII and cleanup.
- Resource leaks, including unclosed file descriptors.
- Unbounded cache growth and uncontrolled memory consumption.

### 2. Instruction & CPU Hazards

Flag:
- Unsupported or non-portable instruction assumptions across architectures without appropriate feature checks.
- Function-pointer corruption and invalid function-pointer conversions.
- Unsafe bit casts or invalid reinterpretations across type and ABI boundaries.

### 3. Performance & Binary Size

Flag:
- Synchronous or blocking I/O in performance-critical execution loops.
- Redundant filesystem traversals and inefficient O(n²) dependency-graph resolution.
- Unnecessary external dependencies, binary-size inflation, and avoidable runtime overhead.

## Review Output Format

- Focus on correctness, memory safety, resource management, performance, and compliance with the language directives.
- Ignore minor formatting and style issues handled by automated formatters.
- Summarize identified risks concisely and explain their practical impact.
- When a review passes, include explicit terminal commands for testing and merging the branch.