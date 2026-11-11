#include <Support/Types.h>

#include <Helix/Core/Strings/Format.h>
#include <Helix/Serialization/Json/Parser.h>
#include <Helix/Serialization/Json/Writer.h>
#include <Helix/Serialization/Transfer/Binary.h>
#include <Helix/Serialization/Transfer/Yaml.h>
#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::serialization::yaml;
namespace json = helix::serialization::json;
namespace ser = helix::serialization;

namespace
{
    // A scene of `objects` game objects, each with a transform and a script, about 1 KB of text per object.
    const String& Scene()
    {
        static const String text = [] {
            String s;
            Emitter e(s);
            e.Directives();
            for (i64 i = 0; i < 32768; i++)
            {
                i64 id = 1000000 + i * 3;
                e.BeginDocument(1, id);
                e.BeginMapping();
                e.Key("GameObject");
                e.BeginMapping();
                e.Key("m_ObjectHideFlags");
                e.Int(0);
                e.Key("serializedVersion");
                e.Int(6);
                e.Key("m_Component");
                e.BeginSequence();
                for (i64 c = 1; c <= 2; c++)
                {
                    e.BeginMapping();
                    e.Key("component");
                    e.BeginMapping(true);
                    e.Key("fileID");
                    e.Int(id + c);
                    e.EndMapping();
                    e.EndMapping();
                }
                e.EndSequence();
                e.Key("m_Layer");
                e.Int(0);
                e.Key("m_Name");
                String name = Format("Object {}", i);
                e.Scalar(name);
                e.Key("m_TagString");
                e.Scalar("Untagged");
                e.Key("m_IsActive");
                e.Int(1);
                e.EndMapping();
                e.EndMapping();

                e.BeginDocument(4, id + 1);
                e.BeginMapping();
                e.Key("Transform");
                e.BeginMapping();
                e.Key("m_GameObject");
                e.BeginMapping(true);
                e.Key("fileID");
                e.Int(id);
                e.EndMapping();
                for (const char* key : {"m_LocalRotation", "m_LocalPosition", "m_LocalScale"})
                {
                    e.Key(key);
                    e.BeginMapping(true);
                    e.Key("x");
                    e.Float(float(i) * 0.25f);
                    e.Key("y");
                    e.Float(1.5f);
                    e.Key("z");
                    e.Float(-float(i % 97) * 0.125f);
                    e.EndMapping();
                }
                e.Key("m_Children");
                e.BeginSequence();
                e.EndSequence();
                e.EndMapping();
                e.EndMapping();

                e.BeginDocument(114, id + 2);
                e.BeginMapping();
                e.Key("MonoBehaviour");
                e.BeginMapping();
                e.Key("m_Script");
                e.BeginMapping(true);
                e.Key("fileID");
                e.Int(11500000);
                e.Key("guid");
                e.Scalar("4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e");
                e.Key("type");
                e.Int(3);
                e.EndMapping();
                e.Key("speed");
                e.Float(3.75f);
                e.Key("label");
                e.Scalar("It's moving: fast");
                e.Key("waypoints");
                e.BeginSequence();
                for (int w = 0; w < 3; w++)
                {
                    e.BeginMapping(true);
                    e.Key("x");
                    e.Int(w);
                    e.Key("y");
                    e.Int(w * 2);
                    e.EndMapping();
                }
                e.EndSequence();
                e.EndMapping();
                e.EndMapping();
            }
            return s;
        }();
        return text;
    }
}

// The first parse into a new tree, its memory touched for the first time.
HX_BENCH(YamlParseSceneCold)
{
    const String& text = Scene();
    state.SetItemsPerIteration(text.Size());
    while (state.KeepRunning())
    {
        Tree tree;
        String error;
        Parse(text, tree, error);
        testing::DoNotOptimize(tree.nodes.Data());
    }
}

// Parsing again into a tree whose memory is in use already, as a loader that reads one file after another.
HX_BENCH(YamlParseScene)
{
    const String& text = Scene();
    state.SetItemsPerIteration(text.Size());
    Tree tree;
    String error;
    Parse(text, tree, error);
    while (state.KeepRunning())
    {
        Parse(text, tree, error);
        testing::DoNotOptimize(tree.nodes.Data());
    }
}

HX_BENCH(YamlWriteScene)
{
    const String& text = Scene();
    Tree tree;
    String error;
    Parse(text, tree, error);
    state.SetItemsPerIteration(text.Size());
    while (state.KeepRunning())
    {
        String out;
        Write(tree, out);
        testing::DoNotOptimize(out.CStr());
    }
}

namespace
{
    constexpr u32 kObjects = 16384;

    Array<ser::testing::Behaviour>& Objects()
    {
        static Array<ser::testing::Behaviour> objects = [] {
            Array<ser::testing::Behaviour> o;
            for (u32 i = 0; i < kObjects; i++)
                o.Push(ser::testing::FilledBehaviour(i32(i)));
            return o;
        }();
        return objects;
    }

    // The objects one after another, each at a multiple of 8 bytes, and where each starts.
    struct Packed
    {
        Array<u8> bytes;
        Array<usize> starts;
    };

    const Packed& Binary()
    {
        static const Packed packed = [] {
            Packed p;
            for (ser::testing::Behaviour& b : Objects())
            {
                while (p.bytes.Size() % 8 != 0)
                    p.bytes.Push(u8(0));
                p.starts.Push(p.bytes.Size());
                ser::WriteBinary(b, p.bytes);
            }
            p.starts.Push(p.bytes.Size());
            return p;
        }();
        return packed;
    }

    const String& YamlObjects()
    {
        static const String text = [] {
            String s;
            Emitter e(s);
            e.Directives();
            i64 id = 1;
            for (ser::testing::Behaviour& b : Objects())
            {
                e.BeginDocument(114, id++);
                ser::WriteYaml(e, b);
            }
            return s;
        }();
        return text;
    }

    // An array of objects like a scene saved as JSON, about 20 MB.
    const String& JsonScene()
    {
        static const String text = [] {
            String s;
            json::Writer w(s, true);
            w.BeginArray();
            for (i64 i = 0; i < 65536; i++)
            {
                w.BeginObject();
                w.Key("m_Name");
                w.String(Format("Object {}", i));
                w.Key("m_IsActive");
                w.Bool(i % 3 != 0);
                w.Key("m_Layer");
                w.Int(i % 32);
                w.Key("m_LocalPosition");
                w.BeginObject();
                w.Key("x");
                w.Float(float(i) * 0.25f);
                w.Key("y");
                w.Float(1.5f);
                w.Key("z");
                w.Float(-float(i % 97) * 0.125f);
                w.EndObject();
                w.Key("m_Children");
                w.BeginArray();
                for (i64 c = 0; c < i % 4; c++)
                    w.Int(i * 4 + c);
                w.EndArray();
                w.Key("m_Label");
                w.String("It's \"quoted\"");
                w.EndObject();
            }
            w.EndArray();
            return s;
        }();
        return text;
    }
}

HX_BENCH(JsonParseScene)
{
    const String& text = JsonScene();
    state.SetItemsPerIteration(text.Size());
    json::Tree tree;
    String error;
    json::Parse(text, tree, error);
    while (state.KeepRunning())
    {
        json::Parse(text, tree, error);
        testing::DoNotOptimize(tree.nodes.Data());
    }
}

HX_BENCH(JsonWriteScene)
{
    const String& text = JsonScene();
    json::Tree tree;
    String error;
    json::Parse(text, tree, error);
    state.SetItemsPerIteration(text.Size());
    while (state.KeepRunning())
    {
        String out;
        json::Write(tree, out, true);
        testing::DoNotOptimize(out.CStr());
    }
}

HX_BENCH(TransferBinaryWrite)
{
    const Packed& p = Binary();
    state.SetItemsPerIteration(p.bytes.Size());
    Array<u8> out;
    out.Reserve(p.bytes.Size());
    while (state.KeepRunning())
    {
        out.Clear();
        for (ser::testing::Behaviour& b : Objects())
            ser::WriteBinary(b, out);
        testing::DoNotOptimize(out.Data());
    }
}

HX_BENCH(TransferBinaryRead)
{
    const Packed& p = Binary();
    state.SetItemsPerIteration(p.bytes.Size());
    Array<ser::testing::Behaviour> read;
    read.Resize(kObjects);
    String error;
    while (state.KeepRunning())
        for (u32 i = 0; i < kObjects; i++)
            ser::ReadBinary(Span<const u8>(p.bytes.Data() + p.starts[i], p.starts[i + 1] - p.starts[i]), read[i], error);
    testing::DoNotOptimize(read.Data());
}

HX_BENCH(TransferSafeBinaryRead)
{
    const Packed& p = Binary();
    const ser::TypeTree& tree = ser::TypeTreeOf<ser::testing::Behaviour>();
    state.SetItemsPerIteration(p.bytes.Size());
    Array<ser::testing::Behaviour> read;
    read.Resize(kObjects);
    String error;
    while (state.KeepRunning())
        for (u32 i = 0; i < kObjects; i++)
            ser::ReadBinary(tree, Span<const u8>(p.bytes.Data() + p.starts[i], p.starts[i + 1] - p.starts[i]), read[i], error);
    testing::DoNotOptimize(read.Data());
}

HX_BENCH(TransferYamlWrite)
{
    const String& text = YamlObjects();
    state.SetItemsPerIteration(text.Size());
    while (state.KeepRunning())
    {
        String s;
        Emitter e(s);
        e.Directives();
        i64 id = 1;
        for (ser::testing::Behaviour& b : Objects())
        {
            e.BeginDocument(114, id++);
            ser::WriteYaml(e, b);
        }
        testing::DoNotOptimize(s.CStr());
    }
}

// Parsing the text and reading every object from the tree.
HX_BENCH(TransferYamlRead)
{
    const String& text = YamlObjects();
    state.SetItemsPerIteration(text.Size());
    Tree tree;
    String error;
    Array<ser::testing::Behaviour> read;
    read.Resize(kObjects);
    Array<String> errors;
    while (state.KeepRunning())
    {
        Parse(text, tree, error);
        for (u32 i = 0; i < kObjects; i++)
            ser::ReadYaml(tree, tree.documents[i].root, read[i], errors);
    }
    testing::DoNotOptimize(read.Data());
}
