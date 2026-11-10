#include <Helix/Core/Strings/Format.h>
#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Bench.h>

using namespace helix;
using namespace helix::serialization::yaml;

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
