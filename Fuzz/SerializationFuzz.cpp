#include <Support/Samples.h>
#include <Support/Types.h>

#include <Helix/Serialization/Json/Parser.h>
#include <Helix/Serialization/Json/Writer.h>
#include <Helix/Serialization/SerializedFile/SerializedFile.h>
#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Fuzz.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace helix;
using namespace helix::serialization::yaml;
namespace json = helix::serialization::json;

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

namespace
{
    // Mutated bytes of `seed`, with room to grow.
    Array<u8> Mutated(helix::testing::FuzzRound& round, const Array<u8>& seed)
    {
        Array<u8> bytes = seed;
        bytes.Resize(seed.Size() + 64);
        bytes.Resize(round.Mutate(bytes.Data(), seed.Size(), bytes.Size()));
        return bytes;
    }

    Array<u8> Bytes(StringView text)
    {
        Array<u8> b;
        b.Append(reinterpret_cast<const u8*>(text.Data()), text.Size());
        return b;
    }

    const Array<u8>& BehaviourBytes()
    {
        static const Array<u8> bytes = [] {
            Array<u8> b;
            serialization::testing::Behaviour behaviour = serialization::testing::FilledBehaviour();
            serialization::WriteBinary(behaviour, b);
            return b;
        }();
        return bytes;
    }

    const Array<u8>& BehaviourTree()
    {
        static const Array<u8> bytes = [] {
            Array<u8> b;
            serialization::TypeTreeOf<serialization::testing::Behaviour>().Write(b);
            return b;
        }();
        return bytes;
    }
}

// Mutated JSON: the parser never crashes, and text it accepts writes out, reads back and writes out again the same.
HX_FUZZ(Json)
{
    static const Array<u8> seed = Bytes(serialization::testing::kJson);
    Array<u8> bytes = Mutated(round, seed);
    StringView text(reinterpret_cast<const char*>(bytes.Data()), bytes.Size());
    json::Tree tree;
    String error;
    round.Step("parse");
    if (!json::Parse(text, tree, error))
        return 0;
    for (bool pretty : {false, true})
    {
        String first, second;
        round.Step("write");
        json::Write(tree, first, pretty);
        json::Tree again;
        round.Step("parse what was written");
        if (!json::Parse(first, again, error))
        {
            std::fprintf(stderr, "written JSON does not parse: %s\n%s\n", error.CStr(), first.CStr());
            std::abort();
        }
        json::Write(again, second, pretty);
        if (!(first == StringView(second)))
        {
            std::fprintf(stderr, "JSON written twice changed:\n%s\n---- and then ----\n%s\n", first.CStr(), second.CStr());
            std::abort();
        }
    }
    return 1;
}

// Mutated binary data of a type read by the direct reader.
HX_FUZZ(Binary)
{
    Array<u8> bytes = Mutated(round, BehaviourBytes());
    serialization::testing::Behaviour b;
    String error;
    round.Step("read");
    return serialization::ReadBinary(Span<const u8>(bytes), b, error) ? 1 : 0;
}

// A mutated type tree, or mutated data under the right tree, read into a type of another layout.
HX_FUZZ(SafeBinary)
{
    bool mutateTree = round.Below(2) == 0;
    Array<u8> treeBytes = mutateTree ? Mutated(round, BehaviourTree()) : BehaviourTree();
    Array<u8> data = mutateTree ? BehaviourBytes() : Mutated(round, BehaviourBytes());
    serialization::TypeTree tree;
    String error;
    round.Step("read the type tree");
    if (!serialization::TypeTree::Read(treeBytes, tree, error))
        return 0;
    round.Step("read the data");
    serialization::testing::Behaviour b;
    serialization::testing::Waypoint other;
    bool ok = serialization::ReadBinary(tree, Span<const u8>(data), b, error);
    serialization::ReadBinary(tree, Span<const u8>(data), other, error);
    return ok ? 1 : 0;
}

// A mutated serialized file: opening checks everything, reading every object then stays inside the file.
HX_FUZZ(SerializedFile)
{
    static const Array<u8> seed = [] {
        serialization::SerializedFileWriter w(true);
        serialization::testing::Behaviour b = serialization::testing::FilledBehaviour();
        serialization::testing::Waypoint p{3, 4};
        w.Add(10, 114, b);
        w.Add(20, 4, p);
        w.AddExternal(b.m_Script.guid, 3, "Assets/Mover.cs");
        Array<u8> out;
        String error;
        w.Finish(out, error);
        return out;
    }();
    Array<u8> bytes = Mutated(round, seed);
    serialization::SerializedFile f;
    String error;
    round.Step("open");
    if (!f.Open(bytes, error))
        return 0;
    round.Step("read the objects");
    for (u32 i = 0; i < f.Objects().Size(); i++)
    {
        serialization::testing::Behaviour b;
        serialization::testing::Waypoint p;
        f.Read(i, b, error);
        f.Read(i, p, error);
    }
    return 1;
}
