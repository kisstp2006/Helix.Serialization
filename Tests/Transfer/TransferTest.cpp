#include <Support/Types.h>

#include <Helix/Serialization/Transfer/Binary.h>
#include <Helix/Serialization/Transfer/Property.h>
#include <Helix/Serialization/Transfer/Yaml.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Test.h>

#include <cstdio>

using namespace helix;
using namespace helix::serialization;
using namespace helix::serialization::testing;

namespace
{
    String WriteDocument(Behaviour& b)
    {
        String out;
        yaml::Emitter e(out);
        e.Directives();
        e.BeginDocument(114, 5);
        WriteYaml(e, b);
        return out;
    }

    bool Has(StringView text, StringView line)
    {
        if (text.Find(line) != StringView::npos)
            return true;
        std::printf("    missing '%.*s' in:\n%.*s\n", int(line.Size()), line.Data(), int(text.Size()), text.Data());
        return false;
    }

    bool ReadDocument(StringView text, Behaviour& b, Array<String>& errors)
    {
        yaml::Tree tree;
        String error;
        if (!yaml::Parse(text, tree, error))
        {
            std::printf("    %s\n", error.CStr());
            return false;
        }
        return ReadYaml(tree, tree.documents[0].root, b, errors);
    }

    // The layout Behaviour had before version 2: m_Label was m_Text, m_Speed an integer, m_Layer an int32, m_Count a
    // field that was removed, m_Position missing.
    struct BehaviourV1
    {
        static constexpr StringView kTypeName = "Behaviour";

        i32 m_Speed = 0;
        String m_Text;
        u16 m_Count = 0;
        i32 m_Layer = 0;
        Array<Waypoint> m_Waypoints;
        Array<i32> m_Values;
        Guid m_Id;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Speed", m_Speed);
            v.Field("m_Text", m_Text);
            v.Field("m_Count", m_Count);
            v.Field("m_Layer", m_Layer);
            v.Field("m_Waypoints", m_Waypoints);
            v.Field("m_Values", m_Values);
            v.Field("m_Id", m_Id);
        }
    };

    struct BehaviourV2
    {
        static constexpr StringView kTypeName = "Behaviour";
        static constexpr i32 kVersion = 2;

        Guid m_Id;
        String m_Label;
        float m_Speed = -1;
        u8 m_Layer = 99;
        Vector3 m_Position = {4, 5, 6};
        Array<Waypoint> m_Waypoints;
        Array<double> m_Values;
        i32 m_ReadVersion = 0;

        template <class V>
        void Transfer(V& v)
        {
            if constexpr (V::kReading)
                m_ReadVersion = v.Version();
            v.Field("m_Id", m_Id);
            // Renamed in version 2.
            v.Field(v.Version() < 2 ? "m_Text" : "m_Label", m_Label);
            v.Field("m_Speed", m_Speed);
            v.Field("m_Layer", m_Layer);
            v.Field("m_Position", m_Position);
            v.Field("m_Waypoints", m_Waypoints);
            v.Field("m_Values", m_Values);
        }
    };

    struct Small
    {
        static constexpr StringView kTypeName = "Small";

        u8 a = 1;
        String s;
        i16 b = 2;
        Array<u8> bytes;
        i32 c = 3;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("a", a);
            v.Field("s", s);
            v.Field("b", b);
            v.Field("bytes", bytes);
            v.Field("c", c);
        }
    };
}

HX_TEST(Transfer, YamlWritesTheDialect)
{
    Behaviour b = FilledBehaviour();
    String text = WriteDocument(b);
    HX_CHECK(Has(text, "--- !h!114 &5\nBehaviour:\n  serializedVersion: 2\n"));
    HX_CHECK(Has(text, "\n  m_Script: {fileID: 11500000, guid: 4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e, type: 3}\n"));
    HX_CHECK(Has(text, "\n  m_GameObject: {fileID: 1053261458}\n"));
    HX_CHECK(Has(text, "\n  m_Enabled: 0\n  m_Layer: 31\n  m_Small: -32768\n  m_Mask: 2147483649\n"));
    HX_CHECK(Has(text, "\n  m_Big: -9223372036854775808\n  m_Huge: 18446744073709551615\n  m_Speed: 3.75\n  m_Precise: 0.1\n  m_Mode: 2\n"));
    HX_CHECK(Has(text, "\n  m_Label: 'It''s moving: fast'\n  m_Id: 00112233445566778899aabbccddeeff\n"));
    HX_CHECK(Has(text, "\n  m_Position: {x: 1, y: 2.5, z: -3}\n"));
    HX_CHECK(Has(text, "\n  m_Waypoints:\n  - {x: 0, y: 0}\n  - {x: 1, y: 2}\n  - {x: 2, y: 4}\n  m_Bytes: 007fff10\n"));
    HX_CHECK(Has(text, "\n  m_Refs:\n  - {fileID: 0}\n  - {fileID: 42}\n"));

    Behaviour plain;
    String empty = WriteDocument(plain);
    HX_CHECK(Has(empty, "\n  m_Waypoints: []\n  m_Bytes: \n"));
}

HX_TEST(Transfer, YamlReadsBackEveryFieldKind)
{
    Behaviour b = FilledBehaviour();
    String text = WriteDocument(b);
    Behaviour read;
    read.m_Names = {String("stale")};
    Array<String> errors;
    HX_CHECK(ReadDocument(text, read, errors) && errors.Empty());
    for (const String& e : errors)
        std::printf("    %s\n", e.CStr());
    HX_CHECK(read == b);
    // Written again it is the same text.
    HX_CHECK(WriteDocument(read) == text);
}

HX_TEST(Transfer, YamlReadKeepsMissingFieldsAndReportsWrongOnes)
{
    const char* text = "--- !h!114 &5\n"
                       "Behaviour:\n"
                       "  m_Unknown: {a: 1}\n"
                       "  m_Layer: 300\n"
                       "  m_Small: {x: 1}\n"
                       "  m_Speed: 2.5\n"
                       "  m_Mode: 1\n"
                       "  m_Id: nothex\n"
                       "  m_Enabled: 2\n"
                       "  m_Fixed: [1, 2]\n"
                       "  m_Bytes: 0g\n"
                       "  m_Names: plain\n"
                       "  m_Mask: 1.0e3\n"
                       "  m_Waypoints:\n"
                       "  - {y: 5}\n";
    Behaviour b;
    Array<String> errors;
    HX_CHECK(!ReadDocument(text, b, errors));
    if (errors.Size() != 7u)
        for (const String& e : errors)
            std::printf("    %s\n", e.CStr());
    HX_REQUIRE(errors.Size() == 7u);
    HX_CHECK(errors[0] == StringView("line 9: m_Enabled: 2 is outside 0 to 1"));
    HX_CHECK(errors[1] == StringView("line 4: m_Layer: 300 is outside 0 to 255"));
    HX_CHECK(errors[2] == StringView("line 5: m_Small: a mapping where a scalar is expected"));
    HX_CHECK(errors[3] == StringView("line 8: m_Id: 'nothex' is not a guid"));
    HX_CHECK(errors[4] == StringView("line 10: m_Fixed: 2 items where 3 are expected"));
    HX_CHECK(errors[5] == StringView("line 11: m_Bytes: bytes that are not hex digits"));
    HX_CHECK(errors[6] == StringView("line 12: m_Names: a scalar where a sequence is expected"));
    // What read is set, the rest keeps its value.
    HX_CHECK(b.m_Speed == 2.5f && b.m_Mode == Mode::On && b.m_Layer == 0 && b.m_Small == -5 && b.m_Enabled);
    HX_CHECK(b.m_Fixed[0] == 1 && b.m_Fixed[1] == 2 && b.m_Fixed[2] == 0 && b.m_Mask == 1000u);
    HX_CHECK(b.m_Waypoints.Size() == 1u && b.m_Waypoints[0].x == 0 && b.m_Waypoints[0].y == 5);

    yaml::Tree tree;
    String error;
    HX_REQUIRE(yaml::Parse("--- !h!1 &1\nGameObject:\n  m_Name: x\n", tree, error));
    errors.Clear();
    HX_CHECK(!ReadYaml(tree, tree.documents[0].root, b, errors) && errors.Size() == 1u && errors[0] == StringView("line 2: no Behaviour in the document"));
}

HX_TEST(Transfer, TypeTreeDescribesTheLayout)
{
    const TypeTree& t = TypeTreeOf<Small>();
    String dump = t.Dump();
    HX_CHECK(dump == StringView("Base Small (-1)\n"
                                "  a uint8 (1)\n"
                                "  s string (-1)\n"
                                "  b int16 (2)\n"
                                "  bytes Array (-1)\n"
                                "    data uint8 (1)\n"
                                "  c int32 (4)\n"));
    if (!(dump == StringView(TypeTreeOf<Small>().Dump())))
        std::printf("%s", dump.CStr());
    String behaviour = TypeTreeOf<Behaviour>().Dump();
    HX_CHECK(Has(behaviour, "Base Behaviour (-1) v2\n  m_Script AssetReference (28) flow\n    fileID int64 (8)\n    guid Guid (16)\n    type int32 (4)\n"));
    HX_CHECK(Has(behaviour, "\n  m_Position Vector3 (12) flow\n    x float (4)\n"));
    HX_CHECK(Has(behaviour, "\n  m_Fixed FixedArray (12) x3\n    data int32 (4)\n"));
    HX_CHECK(Has(behaviour, "\n  m_Grid Array (-1)\n    data Array (-1)\n      data int32 (4)\n"));
    HX_CHECK(Has(behaviour, "\n  m_Mode int32 (4)\n"));

    HX_CHECK(TypeHashOf<BehaviourV1>() != TypeHashOf<BehaviourV2>());
    HX_CHECK(TypeHashOf<Behaviour>() == TypeTreeOf<Behaviour>().Hash());

    Array<u8> bytes;
    TypeTreeOf<Behaviour>().Write(bytes);
    TypeTree back;
    String error;
    HX_REQUIRE(TypeTree::Read(bytes, back, error));
    HX_CHECK(back.Dump() == StringView(TypeTreeOf<Behaviour>().Dump()) && back.Hash() == TypeHashOf<Behaviour>());
}

HX_TEST(Transfer, TypeTreeRefusesDamage)
{
    Array<u8> good;
    TypeTreeOf<Small>().Write(good);
    auto refused = [&](auto change, StringView what) {
        Array<u8> bytes = good;
        change(bytes);
        TypeTree t;
        String error;
        bool ok = TypeTree::Read(bytes, t, error);
        if (ok || !error.View().StartsWith(what))
        {
            std::printf("    %s (expected %.*s)\n", ok ? "accepted" : error.CStr(), int(what.Size()), what.Data());
            return false;
        }
        return true;
    };
    constexpr usize kNodes = 8;
    // Node 1 is "a", node 4 "bytes", node 5 its item.
    HX_CHECK(refused([](Array<u8>& b) { b.Resize(b.Size() - 1); }, "a type tree of 7 nodes"));
    HX_CHECK(refused([](Array<u8>& b) { b.Resize(4); }, "a type tree shorter"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 1] = 200; }, "type tree node 1: kind 200"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 1 + 1] = 3; }, "type tree node 1: level 3 after 0"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 0 + 1] = 1; }, "type tree node 0: level 1"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 5 + 1] = 1; }, "type tree node 4: an array needs exactly one item node"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 2 + 1] = 2; }, "type tree node 1: a uint8 with fields"));
    HX_CHECK(refused([](Array<u8>& b) { b[kNodes + 24 * 1 + 20] = 0xFF; }, "type tree node 1: a name outside"));
    HX_CHECK(refused([](Array<u8>& b) { b.Back() = 'x'; }, "a type tree whose names do not end"));
}

HX_TEST(Transfer, BinaryRoundTripAndExactLayout)
{
    Behaviour b = FilledBehaviour();
    Array<u8> bytes;
    WriteBinary(b, bytes);
    Behaviour read;
    String error;
    HX_CHECK(ReadBinary(Span<const u8>(bytes), read, error));
    HX_CHECK(read == b);

    Small s;
    s.s = "xyz";
    s.bytes = {9};
    Array<u8> small;
    WriteBinary(s, small);
    const u8 expected[] = {1, 3, 0, 0, 0, 'x', 'y', 'z', 2, 0, 1, 0, 0, 0, 9, 0, 3, 0, 0, 0};
    HX_REQUIRE(small.Size() == sizeof(expected));
    HX_CHECK(std::memcmp(small.Data(), expected, sizeof(expected)) == 0);

    // Every cut of the data is an error, never a read past the end.
    for (usize n = 0; n < bytes.Size(); n++)
    {
        Behaviour cut;
        String e;
        if (ReadBinary(Span<const u8>(bytes.Data(), n), cut, e))
        {
            std::printf("    a cut at %zu read\n", n);
            HX_CHECK(false);
            break;
        }
    }
    bytes.Push(u8(0));
    HX_CHECK(!ReadBinary(Span<const u8>(bytes), read, error) && error == StringView("1 bytes after the object"));
    Array<u8> huge = small;
    huge[1] = 0xFF, huge[2] = 0xFF, huge[3] = 0xFF, huge[4] = 0x7F;
    HX_CHECK(!ReadBinary(Span<const u8>(huge), s, error) && error.View().StartsWith("a count of 2147483647 with 15 bytes left at byte 5 (s)"));
}

HX_TEST(Transfer, SafeBinaryReadsOlderLayouts)
{
    BehaviourV1 old;
    old.m_Speed = 12;
    old.m_Text = "renamed";
    old.m_Count = 7;
    old.m_Layer = 1000;
    old.m_Waypoints = {{1, 2}, {3, 4}};
    old.m_Values = {-1, 2, 1 << 30};
    Guid::TryParse("00112233445566778899aabbccddeeff", old.m_Id);
    Array<u8> bytes;
    WriteBinary(old, bytes);

    BehaviourV2 now;
    String error;
    HX_REQUIRE(ReadBinary(TypeTreeOf<BehaviourV1>(), Span<const u8>(bytes), now, error));
    HX_CHECK(now.m_ReadVersion == 1);
    HX_CHECK(now.m_Label == StringView("renamed") && now.m_Speed == 12.0f && now.m_Id == old.m_Id);
    // 1000 does not fit a u8: the field keeps its value. A field the data lacks keeps its value too.
    HX_CHECK(now.m_Layer == 99 && now.m_Position == Vector3({4, 5, 6}));
    HX_CHECK(Same(now.m_Waypoints, old.m_Waypoints));
    HX_CHECK(now.m_Values.Size() == 3u && now.m_Values[0] == -1.0 && now.m_Values[2] == double(1 << 30));

    // The same type through its own tree reads as the direct reader does.
    Behaviour b = FilledBehaviour();
    Array<u8> direct;
    WriteBinary(b, direct);
    Behaviour safe;
    HX_CHECK(ReadBinary(TypeTreeOf<Behaviour>(), Span<const u8>(direct), safe, error) && safe == b);

    // Damaged data is an error.
    for (usize n = 0; n < bytes.Size(); n++)
    {
        BehaviourV2 cut;
        String e;
        if (ReadBinary(TypeTreeOf<BehaviourV1>(), Span<const u8>(bytes.Data(), n), cut, e))
        {
            std::printf("    a cut at %zu read\n", n);
            HX_CHECK(false);
            break;
        }
    }
}

namespace
{
    // A reference type as an engine has one: an object handle that files keep as {fileID, guid, type}.
    struct Handle
    {
        static constexpr StringView kTypeName = "PPtr";
        static constexpr bool kFlow = true;
        u64 object = 0;
        template <class V>
        void Transfer(V& v)
        {
            v.ObjectReference(object);
        }
    };

    struct Holder
    {
        static constexpr StringView kTypeName = "Holder";
        Handle near;
        Handle far;
        Array<Handle> list;
        template <class V>
        void Transfer(V& v)
        {
            v.Field("near", near);
            v.Field("far", far);
            v.Field("list", list);
        }
    };

    // Objects 1000 + n are fileID n of this file; 9000 is an object of another asset.
    struct Table final : ReferenceTable
    {
        Guid other;
        AssetReference ToFile(u64 object) override
        {
            AssetReference r;
            if (object == 9000)
                r.fileID = 11400000, r.guid = other, r.type = 2;
            else if (object)
                r.fileID = i64(object - 1000);
            return r;
        }
        u64 FromFile(const AssetReference& r) override
        {
            if (r.guid.IsValid())
                return r.guid == other && r.fileID == 11400000 ? 9000 : 0;
            return r.fileID ? u64(r.fileID) + 1000 : 0;
        }
    };
}

HX_TEST(Transfer, ObjectReferencesGoThroughTheTable)
{
    Table table;
    HX_REQUIRE(Guid::TryParse("0123456789abcdef0123456789abcdef", table.other));
    Holder h;
    h.near.object = 1005;
    h.far.object = 9000;
    h.list.Push(Handle{1007});
    h.list.Push(Handle{0});
    String text;
    yaml::Emitter e(text);
    e.Directives();
    e.BeginDocument(1, 1);
    WriteYaml(e, h, &table);
    HX_CHECK(Has(text, "near: {fileID: 5}") && Has(text, "far: {fileID: 11400000, guid: 0123456789abcdef0123456789abcdef, type: 2}"));
    HX_CHECK(Has(text, "- {fileID: 7}") && Has(text, "- {fileID: 0}"));
    yaml::Tree tree;
    String error;
    HX_REQUIRE(yaml::Parse(text, tree, error));
    Holder back;
    Array<String> errors;
    HX_CHECK(ReadYaml(tree, tree.documents[0].root, back, errors, &table));
    HX_CHECK(back.near.object == 1005 && back.far.object == 9000 && back.list.Size() == 2u && back.list[0].object == 1007 && back.list[1].object == 0);
    // Binary, directly and through the type tree, which names the reference's fields.
    Array<u8> bytes;
    WriteBinary(h, bytes, &table);
    Holder direct, safe;
    HX_CHECK(ReadBinary(Span<const u8>(bytes), direct, error, &table) && direct.far.object == 9000 && direct.list[0].object == 1007);
    HX_CHECK(ReadBinary(TypeTreeOf<Holder>(), Span<const u8>(bytes), safe, error, &table) && safe.near.object == 1005);
    const TypeTree& tt = TypeTreeOf<Holder>();
    bool named = false;
    for (u32 n = 0; n < tt.nodes.Size(); n++)
        named = named || tt.FieldName(n) == StringView("guid");
    HX_CHECK(named);
    // Without a table the raw value is the fileID.
    String raw;
    yaml::Emitter e2(raw);
    WriteYaml(e2, h);
    HX_CHECK(Has(raw, "near: {fileID: 1005}"));
}

namespace
{
    struct Point
    {
        static constexpr StringView kTypeName = "Point";
        static constexpr bool kFlow = true;
        float x = 0, y = 0;
        template <class V>
        void Transfer(V& v)
        {
            v.Field("x", x);
            v.Field("y", y);
        }
    };

    enum class Switch : u8
    {
        Off = 0,
        On = 1,
    };

    struct Target
    {
        static constexpr StringView kTypeName = "Target";
        String m_Name;
        bool m_IsActive = true;
        u8 m_Layer = 0;
        Switch m_Mode = Switch::Off;
        Point m_Position;
        Array<Point> m_Path;
        Handle m_Link;
        Array<Handle> m_Links;
        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Name", m_Name);
            v.Field("m_IsActive", m_IsActive);
            v.Field("m_Layer", m_Layer);
            v.Field("m_Mode", m_Mode);
            v.Field("m_Position", m_Position);
            v.Field("m_Path", m_Path);
            v.Field("m_Link", m_Link);
            v.Field("m_Links", m_Links);
        }
    };
}

HX_TEST(Transfer, PropertyPathsChangeOneField)
{
    Table table;
    Target t;
    String error;
    AssetReference none;
    HX_CHECK(ApplyProperty(t, "m_Name", "Renamed", none, &table, error) && t.m_Name == StringView("Renamed"));
    HX_CHECK(ApplyProperty(t, "m_IsActive", "0", none, &table, error) && !t.m_IsActive);
    HX_CHECK(ApplyProperty(t, "m_Layer", "5", none, &table, error) && t.m_Layer == 5);
    HX_CHECK(ApplyProperty(t, "m_Mode", "1", none, &table, error) && t.m_Mode == Switch::On);
    HX_CHECK(ApplyProperty(t, "m_Position.y", "0x3F800000(1)", none, &table, error) && t.m_Position.y == 1.0f && t.m_Position.x == 0.0f);
    HX_CHECK(ApplyProperty(t, "m_Path.Array.size", "3", none, &table, error) && t.m_Path.Size() == 3u);
    HX_CHECK(ApplyProperty(t, "m_Path.Array.data[2].x", "-2.5", none, &table, error) && t.m_Path[2].x == -2.5f);
    AssetReference five;
    five.fileID = 5;
    HX_CHECK(ApplyProperty(t, "m_Link", "", five, &table, error) && t.m_Link.object == 1005);
    HX_CHECK(ApplyProperty(t, "m_Links.Array.size", "1", none, &table, error));
    HX_CHECK(ApplyProperty(t, "m_Links.Array.data[0]", "", five, &table, error) && t.m_Links[0].object == 1005);
    // What does not fit is an error and changes nothing.
    HX_CHECK(!ApplyProperty(t, "m_Layer", "300", none, &table, error) && t.m_Layer == 5 && error.View().Contains("fit"));
    HX_CHECK(!ApplyProperty(t, "m_Nothing", "1", none, &table, error) && error.View().Contains("no field"));
    HX_CHECK(!ApplyProperty(t, "m_Path.Array.data[7].x", "1", none, &table, error));
    HX_CHECK(!ApplyProperty(t, "m_Position", "1", none, &table, error));
    HX_CHECK(!ApplyProperty(t, "m_Name.x", "1", none, &table, error));
    HX_CHECK(!ApplyProperty(t, "m_IsActive", "yes", none, &table, error) && !t.m_IsActive);
    // A field name that only starts the path's name does not match.
    HX_CHECK(!ApplyProperty(t, "m_Nam", "x", none, &table, error) && t.m_Name == StringView("Renamed"));
}
