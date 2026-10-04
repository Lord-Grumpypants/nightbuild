#pragma once

#include <cstdint>

enum class CommandKind : std::uint32_t {
    PreAction,
    Compile,
    Object,
    Link,
    Generator,
    Action
};