#include <Support/Samples.h>

#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Fuzz.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace helix;
using namespace helix::serialization::yaml;

// A mutated scene: the parser never crashes, and text it accepts writes out, reads back and writes out again the same.
HX_FUZZ(Yaml)
{
    usize size = std::strlen(serialization::testing::kScene);
    Array<u8> bytes;
    bytes.Append(reinterpret_cast<const u8*>(serialization::testing::kScene), size);
    bytes.Resize(size + 64);
    bytes.Resize(round.Mutate(bytes.Data(), size, bytes.Size()));
    StringView text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    Tree tree;
    String error;
    round.Step("parse");
    if (!Parse(text, tree, error))
        return 0;
    String first, second;
    round.Step("write");
    Write(tree, first);
    Tree again;
    round.Step("parse what was written");
    if (!Parse(first, again, error))
    {
        std::fprintf(stderr, "written text does not parse: %s\n%s\n", error.CStr(), first.CStr());
        std::abort();
    }
    Write(again, second);
    if (!(first == StringView(second)))
    {
        std::fprintf(stderr, "written twice the text changed:\n%s\n---- and then ----\n%s\n", first.CStr(), second.CStr());
        std::abort();
    }
    return 1;
}
