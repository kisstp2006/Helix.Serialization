#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Serialization/Json/Tree.h>

namespace helix::serialization::json
{
    // Containers nested deeper than this are an error.
    inline constexpr u32 kMaxDepth = 512;

    // Parses one JSON value (RFC 8259) into `out` (cleared first): whitespace around it, objects, arrays, strings with
    // every escape (\u with surrogate pairs) and checked UTF-8, numbers by the grammar of the RFC, true, false and null.
    // A UTF-8 byte order mark at the start is skipped. `text` must outlive the tree. On failure `error` gives the line and
    // column and what is wrong.
    bool Parse(StringView text, Tree& out, String& error);
}
