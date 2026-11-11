#include <Support/Types.h>

#include <Helix/Serialization/SerializedFile/SerializedFile.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

using namespace helix;
using namespace helix::serialization;
using namespace helix::serialization::testing;

namespace
{
    struct Named
    {
        static constexpr StringView kTypeName = "Named";

        String m_Name;
        i32 m_Value = 0;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Name", m_Name);
            v.Field("m_Value", m_Value);
        }
    };

    // Named with a field added and the value widened.
    struct NamedLater
    {
        static constexpr StringView kTypeName = "Named";
        static constexpr i32 kVersion = 2;

        i64 m_Value = -1;
        Array<String> m_Tags = {String("default")};
        String m_Name;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Value", m_Value);
            v.Field("m_Tags", m_Tags);
            v.Field("m_Name", m_Name);
        }
    };

    Array<u8> MakeFile(bool typeTrees)
    {
        SerializedFileWriter w(typeTrees);
        Named a{String("first"), 1}, c{String("third"), 3};
        Behaviour b = FilledBehaviour();
        w.Add(300, 1, c);
        w.Add(100, 1, a);
        w.Add(200, 114, b);
        Guid g;
        Guid::TryParse("4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e", g);
        w.AddExternal(g, 3, "Assets/Scripts/Mover.cs");
        Array<u8> out;
        String error;
        HX_CHECK(w.Finish(out, error));
        return out;
    }
}

HX_TEST(SerializedFile, WritesAndReadsObjects)
{
    for (bool trees : {true, false})
    {
        Array<u8> bytes = MakeFile(trees);
        SerializedFile f;
        String error;
        HX_REQUIRE(f.Open(bytes, error));
        HX_CHECK(f.HasTypeTrees() == trees);
        HX_REQUIRE(f.Objects().Size() == 3u && f.Types().Size() == 2u && f.Externals().Size() == 1u);
        HX_CHECK(f.Objects()[0].fileID == 100 && f.Objects()[1].fileID == 200 && f.Objects()[2].fileID == 300);
        HX_CHECK(f.Types()[f.Objects()[1].type].classID == 114 && f.Types()[0].hash == TypeHashOf<Named>());
        HX_CHECK(f.Types()[0].tree.nodes.Empty() == !trees);
        HX_CHECK(f.Externals()[0].path == StringView("Assets/Scripts/Mover.cs") && f.Externals()[0].type == 3);
        HX_CHECK(f.Find(200) == 1u && f.Find(150) == kNoObject && f.Find(-1) == kNoObject);
        for (const SerializedObject& o : f.Objects())
            HX_CHECK(o.offset % 8 == 0);

        Named n;
        HX_CHECK(f.Read(f.Find(300), n, error) && n.m_Name == StringView("third") && n.m_Value == 3);
        Behaviour b;
        HX_CHECK(f.Read(f.Find(200), b, error) && b == FilledBehaviour());

        NamedLater later;
        bool read = f.Read(f.Find(100), later, error);
        if (trees)
            HX_CHECK(read && later.m_Name == StringView("first") && later.m_Value == 1 && later.m_Tags.Size() == 1u);
        else
            HX_CHECK(!read && error == StringView("object 100 was written with another layout of Named and the file has no type trees"));
    }
}

HX_TEST(SerializedFile, RefusesDamagedFiles)
{
    SerializedFileWriter dup;
    Named a;
    dup.Add(5, 1, a);
    dup.Add(5, 1, a);
    Array<u8> out;
    String error;
    HX_CHECK(!dup.Finish(out, error) && error == StringView("two objects with fileID 5"));

    Array<u8> good = MakeFile(true);
    auto refused = [&](auto change, StringView what) {
        Array<u8> bytes = good;
        change(bytes);
        SerializedFile f;
        String e;
        bool ok = f.Open(bytes, e);
        if (ok || !e.View().StartsWith(what))
        {
            std::printf("    %s (expected %.*s)\n", ok ? "accepted" : e.CStr(), int(what.Size()), what.Data());
            return false;
        }
        return true;
    };
    HX_CHECK(refused([](Array<u8>& b) { b[0] = 'X'; }, "not a serialized file"));
    HX_CHECK(refused([](Array<u8>& b) { b[4] = 9; }, "format version 9"));
    HX_CHECK(refused([](Array<u8>& b) { b[8] = 3; }, "unknown flags 3"));
    HX_CHECK(refused([](Array<u8>& b) { b.Push(u8(0)); }, "header sizes"));
    HX_CHECK(refused([](Array<u8>& b) { b[12] = 200; }, "header sizes"));
    // The first type entry starts at 64: its tree offset at 68, its tree size at 72.
    HX_CHECK(refused([](Array<u8>& b) { b[73] = 0x7F; }, "type 0: the type tree is outside"));
    HX_CHECK(refused([](Array<u8>& b) { b[72] -= 4; }, "type 0: a type tree of"));
    // Objects start at 64 + 2 * 32 = 128: fileID, type at 136, offset at 144, size at 152.
    HX_CHECK(refused([](Array<u8>& b) { b[136] = 2; }, "object 0: type 2 of 2"));
    HX_CHECK(refused([](Array<u8>& b) { b[159] = 1; }, "object 0: data outside the file"));
    HX_CHECK(refused([](Array<u8>& b) { b[128 + 32] = 0, b[128 + 33] = 0; }, "object 1: fileID 0 after 100"));
    // The external reference at 224: path offset at 244, size at 248.
    HX_CHECK(refused([](Array<u8>& b) { b[249] = 1; }, "external reference 0: the path is outside"));

    // Every cut of the file is refused.
    for (usize n = 0; n < good.Size(); n += 7)
    {
        SerializedFile f;
        String e;
        if (f.Open(Span<const u8>(good.Data(), n), e))
        {
            std::printf("    a cut at %zu opened\n", n);
            HX_CHECK(false);
            break;
        }
    }
}
