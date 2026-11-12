#include <Helix/Serialization/SerializedFile/SerializedFile.h>
#include <Helix/Serialization/Transfer/Binary.h>
#include <Helix/Serialization/Transfer/Dynamic.h>
#include <Helix/Serialization/Transfer/Property.h>
#include <Helix/Serialization/Transfer/TypeTree.h>
#include <Helix/Serialization/Transfer/Yaml.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::serialization;

namespace dyn
{
    struct Vec2
    {
        static constexpr StringView kTypeName = "Vector2f";
        static constexpr bool kFlow = true;
        f32 x = 0, y = 0;
        template <class V>
        void Transfer(V& v)
        {
            v.Field("x", x);
            v.Field("y", y);
        }
    };

    struct Waypoint
    {
        static constexpr StringView kTypeName = "Waypoint";
        Vec2 at;
        Array<i32> marks = Array<i32>();
        template <class V>
        void Transfer(V& v)
        {
            v.Field("at", at);
            v.Field("marks", marks);
        }
    };

    struct Ref
    {
        u64 id = 0;
    };

    enum class Mode : u8
    {
        Idle,
        Run,
    };

    // A script's fields, visited through the ABI (what generated code does).
    struct Script
    {
        i32 hp = 10;
        f32 speed = 1.5f;
        bool alive = true;
        Mode mode = Mode::Run;
        Guid id;
        String title = String("hero");
        Array<i32> scores = Array<i32>();
        Array<String> tags = Array<String>();
        Array<bool> flags = Array<bool>();
        i32 corners[3] = {0, 0, 0};
        Vec2 position;
        Array<Waypoint> path = Array<Waypoint>();
        Ref target;
        Array<Ref> targets = Array<Ref>();
        Waypoint boxed;
        // Leaves the two last fields out (an older layout).
        bool old = false;
    };

    void TransferScript(void* object, DynamicVisitor* visitor)
    {
        Script& s = *static_cast<Script*>(object);
        VisitDynamic(*visitor, [&s](auto& f) {
            f.Field("hp", s.hp);
            f.Field("speed", s.speed);
            f.Field("alive", s.alive);
            f.Field("mode", s.mode);
            f.Field("id", s.id);
            f.Field("title", s.title);
            f.Field("scores", s.scores);
            f.Field("tags", s.tags);
            f.Field("flags", s.flags);
            f.Field("corners", s.corners);
            f.Field("position", s.position);
            f.Field("path", s.path);
            if (!s.old)
            {
                f.Field("target", s.target);
                f.Field("targets", s.targets);
            }
        });
        // An opaque struct (a managed reference's data): bytes to the binary form, a struct to YAML.
        static const DynamicStruct opaque = [] {
            DynamicStruct d = DynamicStructOf<Waypoint>();
            d.flags |= kDynamicOpaque;
            return d;
        }();
        if (!s.old)
            visitor->table->structure(visitor->self, "boxed", 5, &s.boxed, &opaque);
    }

    // The engine side: its own fields, then the script's.
    struct Host
    {
        static constexpr StringView kTypeName = "MonoBehaviour";
        u8 enabled = 1;
        Script* script = nullptr;
        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Enabled", enabled);
            DynamicVisitor d = DynamicAdapter<V>::Make(v);
            TransferScript(script, &d);
        }
    };

    // Objects 1..n are fileIDs 10..10n.
    struct Table : ReferenceTable
    {
        AssetReference ToFile(u64 object) override
        {
            AssetReference r;
            r.fileID = i64(object) * 10;
            return r;
        }
        u64 FromFile(const AssetReference& r) override { return u64(r.fileID / 10); }
    };

    Script Sample()
    {
        Script s;
        s.hp = 42;
        s.mode = Mode::Idle;
        Guid::TryParse("0123456789abcdef0123456789abcdef", s.id);
        s.scores.Push(3);
        s.scores.Push(5);
        s.tags.Push(String("a"));
        s.tags.Push(String("b c"));
        s.flags.Push(true);
        s.flags.Push(false);
        s.position = {1, 2};
        s.corners[0] = 4;
        s.corners[2] = 6;
        Waypoint w;
        w.at = {3, 4};
        w.marks.Push(7);
        s.path.Push(w);
        w.at = {5, 6};
        s.path.Push(w);
        s.target.id = 2;
        s.targets.Push(Ref{1});
        s.targets.Push(Ref{3});
        s.boxed.at = {7, 8};
        s.boxed.marks.Push(9);
        return s;
    }

    bool Same(const Script& a, const Script& b)
    {
        bool same = a.hp == b.hp && a.speed == b.speed && a.alive == b.alive && a.mode == b.mode && a.id == b.id && a.title == b.title &&
                    a.scores.Size() == b.scores.Size() && a.tags.Size() == b.tags.Size() && a.flags.Size() == b.flags.Size() &&
                    a.position.x == b.position.x && a.corners[0] == b.corners[0] && a.corners[2] == b.corners[2] && a.path.Size() == b.path.Size() && a.target.id == b.target.id && a.targets.Size() == b.targets.Size();
        for (usize i = 0; same && i < a.tags.Size(); i++)
            same = a.tags[i] == b.tags[i];
        for (usize i = 0; same && i < a.flags.Size(); i++)
            same = a.flags[i] == b.flags[i];
        for (usize i = 0; same && i < a.path.Size(); i++)
            same = a.path[i].at.y == b.path[i].at.y && a.path[i].marks.Size() == b.path[i].marks.Size();
        for (usize i = 0; same && i < a.targets.Size(); i++)
            same = a.targets[i].id == b.targets[i].id;
        same = same && a.boxed.at.y == b.boxed.at.y && a.boxed.marks.Size() == b.boxed.marks.Size();
        return same;
    }
}

template <>
struct helix::serialization::ObjectReferenceTraits<dyn::Ref>
{
    static u64 Id(const dyn::Ref& r) { return r.id; }
    static void Set(dyn::Ref& r, u64 id) { r.id = id; }
};

HX_TEST(DynamicFields, YamlRoundTrip)
{
    dyn::Script s = dyn::Sample();
    dyn::Host h{1, &s};
    dyn::Table table;
    String text;
    yaml::Emitter e(text);
    e.BeginDocument(114, 1);
    WriteYaml(e, h, &table);
    StringView t(text);
    HX_CHECK(t.Contains("  m_Enabled: 1\n  hp: 42\n  speed: 1.5\n  alive: 1\n  mode: 0\n"));
    HX_CHECK(t.Contains("  title: hero\n"));
    HX_CHECK(t.Contains("position: {x: 1, y: 2}"));
    HX_CHECK(t.Contains("target: {fileID: 20}"));
    HX_CHECK(t.Contains("- {fileID: 30}"));
    HX_CHECK(t.Contains("  boxed:\n    at: {x: 7, y: 8}\n"));
    HX_CHECK(t.Contains("  - at: {x: 3, y: 4}\n    marks: 07000000\n") || t.Contains("  - at: {x: 3, y: 4}\n    marks:\n    - 7\n"));

    yaml::Tree tree;
    String error;
    HX_REQUIRE(yaml::Parse(text, tree, error));
    dyn::Script back;
    dyn::Host hb{0, &back};
    Array<String> errors;
    HX_CHECK(ReadYaml(tree, tree.documents[0].root, hb, errors, &table));
    HX_CHECK(errors.Empty());
    HX_CHECK(hb.enabled == 1 && dyn::Same(s, back));
}

HX_TEST(DynamicFields, BinaryAndTypeTree)
{
    dyn::Script s = dyn::Sample();
    dyn::Host h{1, &s};
    TypeTree tree;
    TypeTreeBuild(tree).Node("Base", h);
    tree.ComputeSizes();
    HX_CHECK(tree.TypeName(0) == StringView("MonoBehaviour"));
    String dump = tree.Dump();
    HX_CHECK(StringView(dump).Contains("path") && StringView(dump).Contains("Waypoint") && StringView(dump).Contains("PPtr"));
    HX_CHECK(StringView(dump).Contains("OpaqueData"));

    Array<u8> bytes;
    WriteBinary(h, bytes);
    dyn::Script back;
    back.title = String();
    dyn::Host hb{0, &back};
    String error;
    HX_CHECK(ReadBinary(Span<const u8>(bytes), hb, error));
    HX_CHECK(dyn::Same(s, back));

    // An older layout read through its type tree: the fields it lacks keep their values.
    dyn::Script older = dyn::Sample();
    older.old = true;
    older.hp = 7;
    dyn::Host ho{1, &older};
    TypeTree olderTree;
    TypeTreeBuild(olderTree).Node("Base", ho);
    olderTree.ComputeSizes();
    HX_CHECK(olderTree.Hash() != tree.Hash());
    SerializedFileWriter w;
    w.Add(1, 114, ho, olderTree, olderTree.Hash());
    Array<u8> file;
    HX_REQUIRE(w.Finish(file, error));
    SerializedFile f;
    HX_REQUIRE(f.Open(Span<const u8>(file), error));
    dyn::Script now;
    now.target.id = 9;
    dyn::Host hn{0, &now};
    HX_CHECK(f.Read(0, hn, tree.Hash(), error));
    HX_CHECK(now.hp == 7 && now.target.id == 9 && now.path.Size() == 2 && now.path[1].at.x == 5);
}

HX_TEST(DynamicFields, PropertyPaths)
{
    dyn::Script s = dyn::Sample();
    dyn::Host h{1, &s};
    String error, text;
    HX_CHECK(ApplyProperty(h, "path.Array.data[1].at.x", "9", AssetReference(), nullptr, error));
    HX_CHECK(s.path[1].at.x == 9);
    HX_CHECK(ApplyProperty(h, "scores.Array.size", "4", AssetReference(), nullptr, error));
    HX_CHECK(s.scores.Size() == 4 && s.scores[0] == 3);
    HX_CHECK(ApplyProperty(h, "tags.Array.data[0]", "z", AssetReference(), nullptr, error));
    // A fixed size array keeps its size.
    HX_CHECK(ApplyProperty(h, "corners.Array.size", "5", AssetReference(), nullptr, error) && ReadProperty(h, "corners.Array.size", text, error) && text == StringView("3"));
    HX_CHECK(ApplyProperty(h, "corners.Array.data[1]", "8", AssetReference(), nullptr, error) && s.corners[1] == 8);
    HX_CHECK(s.tags[0] == StringView("z"));
    AssetReference r;
    r.fileID = 5;
    HX_CHECK(ApplyProperty(h, "target", "", r, nullptr, error));
    HX_CHECK(s.target.id == 5);
    HX_CHECK(ReadProperty(h, "title", text, error) && text == StringView("hero"));
    HX_CHECK(ReadProperty(h, "path.Array.size", text, error) && text == StringView("2"));
    HX_CHECK(!ReadProperty(h, "missing", text, error));
}
