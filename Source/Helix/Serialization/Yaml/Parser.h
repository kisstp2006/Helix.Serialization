#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Serialization/Yaml/Tree.h>

namespace helix::serialization::yaml
{
    // Parses a YAML stream of the Helix dialect into `out` (cleared first): %YAML and %TAG directives, documents that
    // start with --- and may carry a !h!<classID> tag, an &<fileID> anchor and the word stripped, block mappings and
    // sequences, flow mappings and sequences, plain and quoted scalars. `text` must outlive the tree. On failure `error`
    // gives the line and column and what is wrong.
    bool Parse(StringView text, Tree& out, String& error);
}
