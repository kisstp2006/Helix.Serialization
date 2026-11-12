#include <Helix/Serialization/SerializedFile/SerializedFile.h>
#include <Helix/Serialization/Transfer/Binary.h>
#include <Helix/Serialization/Transfer/TypeTree.h>
#include <Helix/Serialization/Transfer/Yaml.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Test.h>

using namespace helix;
using namespace helix::serialization;

// A type of another library: no member Transfer, a TransferTraits specialization instead.
namespace other
{
    struct Point
    {
        f32 x = 0, y = 0;
    };

    struct Holder
    {
        static constexpr StringView kTypeName = "Holder";
        Point point;
        Array<Point> points = Array<Point>();
        template <class V>
        void Transfer(V& v)
        {
            v.Field("point", point);
            v.Field("points", points);
        }
    };
}

template <>
struct helix::serialization::TransferTraits<other::Point>
{
    static constexpr StringView kTypeName = "Point2f";
    static constexpr bool kFlow = true;
    template <class V>
    static void Transfer(V& v, other::Point& p)
    {
        v.Field("x", p.x);
        v.Field("y", p.y);
    }
};

namespace
{
    // A struct whose layout is known at run time names its type itself.
    struct Runtime
    {
        static constexpr StringView kTypeName = "Runtime";
        i32 fields = 1;
        i32 values[3] = {1, 2, 3};
        StringView TypeName() const { return fields == 1 ? StringView("One") : StringView("Many"); }
        template <class V>
        void Transfer(V& v)
        {
            v.Field("a", values[0]);
            for (i32 i = 1; i < fields; i++)
                v.Field(i == 1 ? "b" : "c", values[i]);
        }
    };
}

HX_TEST(TransferTraits, TypesOfOtherLibrariesSerialize)
{
    other::Holder h;
    h.point = {1, 2};
    h.points.Push({3, 4});
    String text;
    yaml::Emitter e(text);
    e.BeginDocument(1, 1);
    WriteYaml(e, h);
    HX_CHECK(StringView(text).Contains("point: {x: 1, y: 2}") && StringView(text).Contains("- {x: 3, y: 4}"));
    yaml::Tree tree;
    String error;
    HX_REQUIRE(yaml::Parse(text, tree, error));
    other::Holder back;
    Array<String> errors;
    HX_CHECK(ReadYaml(tree, tree.documents[0].root, back, errors) && back.point.y == 2 && back.points.Size() == 1 && back.points[0].x == 3);
    Array<u8> bytes;
    WriteBinary(h, bytes);
    other::Holder fromBytes;
    HX_CHECK(ReadBinary(Span<const u8>(bytes), fromBytes, error) && fromBytes.points[0].y == 4);
    const TypeTree& t = TypeTreeOf<other::Holder>();
    bool named = false;
    for (u32 i = 0; i < t.nodes.Size(); i++)
        named = named || t.TypeName(i) == StringView("Point2f");
    HX_CHECK(named);
}

HX_TEST(TransferTraits, LayoutsKnownAtRunTime)
{
    Runtime one, three;
    three.fields = 3;
    TypeTree treeOne, treeThree;
    TypeTreeBuild(treeOne).Node("Base", one);
    TypeTreeBuild(treeThree).Node("Base", three);
    treeOne.ComputeSizes();
    treeThree.ComputeSizes();
    HX_CHECK(treeOne.TypeName(0) == StringView("One") && treeThree.nodes.Size() > treeOne.nodes.Size());
    SerializedFileWriter w;
    w.Add(1, 114, one, treeOne, treeOne.Hash());
    w.Add(2, 114, three, treeThree, treeThree.Hash());
    Array<u8> bytes;
    String error;
    HX_REQUIRE(w.Finish(bytes, error));
    SerializedFile f;
    HX_REQUIRE(f.Open(Span<const u8>(bytes), error));
    HX_CHECK(f.Types().Size() == 2);
    Runtime readThree;
    readThree.fields = 3;
    readThree.values[2] = 0;
    HX_CHECK(f.Read(1, readThree, treeThree.Hash(), error) && readThree.values[2] == 3);
    // Read with another layout: through the file's type tree.
    Runtime readOne;
    readOne.values[0] = 0;
    HX_CHECK(f.Read(1, readOne, treeOne.Hash(), error) && readOne.values[0] == 1);
}
