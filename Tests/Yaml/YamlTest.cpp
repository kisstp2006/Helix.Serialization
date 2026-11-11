#include <Support/Samples.h>

#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Parser.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace helix;
using namespace helix::serialization::yaml;
using namespace helix::serialization::testing;

namespace
{
    bool ParseText(StringView text, Tree& tree)
    {
        String error;
        bool ok = Parse(text, tree, error);
        if (!ok)
            std::printf("    %s\n", error.CStr());
        return ok;
    }

    // The node below `root` along keys and sequence indices ("m_Component/0/component").
    u32 At(const Tree& t, u32 root, StringView path)
    {
        u32 node = root;
        usize at = 0;
        while (at <= path.Size() && node != kNoNode)
        {
            usize slash = path.Find('/', at);
            StringView part = path.Substr(at, (slash == StringView::npos ? path.Size() : slash) - at);
            if (t.IsSequence(node))
            {
                u32 index = 0;
                for (char c : part)
                    index = index * 10 + u32(c - '0');
                node = index < t.Count(node) ? t.Item(node, index) : kNoNode;
            }
            else
                node = t.Find(node, part);
            if (slash == StringView::npos)
                break;
            at = slash + 1;
        }
        return node;
    }

    bool SameNode(const Tree& a, u32 x, const Tree& b, u32 y)
    {
        const Node& n = a[x];
        const Node& m = b[y];
        if (n.kind != m.kind || n.Count() != m.Count())
            return false;
        if (n.kind == NodeKind::Scalar)
            return n.Text() == m.Text();
        if (n.flow != m.flow)
            return false;
        for (u32 i = 0; i < n.Count(); i++)
        {
            if (n.kind == NodeKind::Mapping)
            {
                if (a.Text(a.Key(x, i)) != b.Text(b.Key(y, i)) || !SameNode(a, a.Value(x, i), b, b.Value(y, i)))
                    return false;
            }
            else if (!SameNode(a, a.Item(x, i), b, b.Item(y, i)))
                return false;
        }
        return true;
    }

    bool SameTree(const Tree& a, const Tree& b)
    {
        if (a.documents.Size() != b.documents.Size() || a.directives.Size() != b.directives.Size())
            return false;
        for (usize i = 0; i < a.documents.Size(); i++)
        {
            const Document& d = a.documents[i];
            const Document& e = b.documents[i];
            if (d.classID != e.classID || d.fileID != e.fileID || d.stripped != e.stripped || !SameNode(a, d.root, b, e.root))
                return false;
        }
        return true;
    }

    i64 Int(const Tree& t, u32 node)
    {
        i64 v = -12345;
        t.AsInt(node, v);
        return v;
    }

    double Float(const Tree& t, u32 node)
    {
        double v = -12345;
        t.AsFloat(node, v);
        return v;
    }
}

HX_TEST(Yaml, ReadsAScene)
{
    Tree t;
    HX_REQUIRE(ParseText(kScene, t));
    HX_REQUIRE(t.documents.Size() == 4u);
    HX_CHECK(t.directives.Size() == 2u && t.directives[1] == StringView("TAG !h! tag:helixengine,2026:"));
    const Document& go = t.documents[0];
    HX_CHECK(go.classID == 1 && go.fileID == 1053261458 && !go.stripped);
    HX_CHECK(t.documents[3].classID == 1001 && t.documents[3].fileID == 700000 && t.documents[3].stripped);

    u32 root = go.root;
    HX_CHECK(t.Text(At(t, root, "GameObject/m_Name")) == StringView("Main Camera"));
    u32 components = At(t, root, "GameObject/m_Component");
    HX_REQUIRE(t.IsSequence(components) && t.Count(components) == 2u);
    u32 first = At(t, root, "GameObject/m_Component/0/component");
    HX_CHECK(t[first].flow && t.IsMapping(first));
    HX_CHECK(Int(t, At(t, root, "GameObject/m_Component/1/component/fileID")) == 1053261460);
    HX_CHECK(Int(t, At(t, root, "GameObject/m_IsActive")) == 1);

    u32 transform = t.documents[1].root;
    HX_CHECK(Float(t, At(t, transform, "Transform/m_LocalPosition/z")) == -10.0);
    u32 children = At(t, transform, "Transform/m_Children");
    HX_CHECK(t.IsSequence(children) && t.Count(children) == 0u && t[children].flow);

    u32 script = t.documents[2].root;
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Script/guid")) == StringView("4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e"));
    HX_CHECK(Int(t, At(t, script, "MonoBehaviour/m_Script/type")) == 3);
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Name")).Empty());
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Text")) == StringView("It's a test: with a colon"));
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Multi")) == StringView("First line\nSecond\ttabbed \"quoted\" \xC3\xA9"));
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Folded")) == StringView("a long string that goes on\nover lines"));
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/m_Plain")) == StringView("a plain value that folds"));
    HX_CHECK(Int(t, At(t, script, "MonoBehaviour/values/0")) == 1);
    HX_CHECK(Float(t, At(t, script, "MonoBehaviour/values/1")) == -2.5);
    HX_CHECK(Float(t, At(t, script, "MonoBehaviour/values/2")) == 3e-07);
    HX_CHECK(Int(t, At(t, script, "MonoBehaviour/values/3")) == 31);
    HX_CHECK(Float(t, At(t, script, "MonoBehaviour/nested/list/1/weight")) == 0.75);
    HX_CHECK(t.Text(At(t, script, "MonoBehaviour/nested/list/2/0")) == StringView("inner"));
    HX_CHECK(Int(t, At(t, script, "MonoBehaviour/nested/list/2/1/2/deep/1")) == 4);
    u32 empty = At(t, script, "MonoBehaviour/nested/empty");
    HX_CHECK(t.IsMapping(empty) && t.Count(empty) == 0u);
    HX_CHECK(t.IsScalar(At(t, script, "MonoBehaviour/nested/none")) && t.Text(At(t, script, "MonoBehaviour/nested/none")).Empty());

    u32 prefab = t.documents[3].root;
    HX_CHECK(t.Text(At(t, prefab, "PrefabInstance/m_Modification/m_Modifications/0/propertyPath")) == StringView("m_LocalPosition.x"));
    // Nodes know their line: the line of "value: 5" in the text.
    u32 line = 1;
    for (const char* p = kScene; p < std::strstr(kScene, "value: 5"); p++)
        line += *p == '\n';
    HX_CHECK(t[At(t, prefab, "PrefabInstance/m_Modification/m_Modifications/0/value")].line == line);
}

HX_TEST(Yaml, WritesWhatItReads)
{
    Tree t, again;
    HX_REQUIRE(ParseText(kScene, t));
    String text;
    Write(t, text);
    HX_REQUIRE(ParseText(text, again));
    HX_CHECK(SameTree(t, again));
    String twice;
    Write(again, twice);
    HX_CHECK(text == StringView(twice));
}

HX_TEST(Yaml, EmitsTheDialect)
{
    String out;
    Emitter e(out);
    e.Directives();
    e.BeginDocument(1, 42);
    e.BeginMapping();
    e.Key("GameObject");
    e.BeginMapping();
    e.Key("m_Component");
    e.BeginSequence();
    e.BeginMapping();
    e.Key("component");
    e.BeginMapping(true);
    e.Key("fileID");
    e.Int(43);
    e.EndMapping();
    e.EndMapping();
    e.EndSequence();
    e.Key("m_Name");
    e.Scalar("Cube");
    e.Key("m_Empty");
    e.Scalar("");
    e.Key("m_Children");
    e.BeginSequence();
    e.EndSequence();
    e.Key("m_Data");
    e.BeginMapping();
    e.EndMapping();
    e.Key("m_Scale");
    e.Float(0.1f);
    e.EndMapping();
    e.EndMapping();
    e.BeginDocument(4, 43, true);
    e.BeginMapping();
    e.Key("Transform");
    e.BeginMapping();
    e.Key("m_List");
    e.BeginSequence();
    e.Int(1);
    e.BeginSequence(true);
    e.Int(2);
    e.Scalar("a, b");
    e.EndSequence();
    e.EndSequence();
    e.EndMapping();
    e.EndMapping();
    const char* expected = "%YAML 1.1\n"
                           "%TAG !h! tag:helixengine,2026:\n"
                           "--- !h!1 &42\n"
                           "GameObject:\n"
                           "  m_Component:\n"
                           "  - component: {fileID: 43}\n"
                           "  m_Name: Cube\n"
                           "  m_Empty: \n"
                           "  m_Children: []\n"
                           "  m_Data: {}\n"
                           "  m_Scale: 0.1\n"
                           "--- !h!4 &43 stripped\n"
                           "Transform:\n"
                           "  m_List:\n"
                           "  - 1\n"
                           "  - [2, 'a, b']\n";
    if (!(out == StringView(expected)))
        std::printf("%s", out.CStr());
    HX_CHECK(out == StringView(expected));
}

HX_TEST(Yaml, QuotesOnlyWhatNeedsIt)
{
    const char* plain[] = {"Cube", "Main Camera", "m_LocalPosition.x", "-5", "0.25", "a-b", "a:b", "a#b", "\xC3\xA9t\xC3\xA9", "x, y"};
    for (const char* s : plain)
        HX_CHECK(Emitter::PlainIsSafe(s));
    const char* quoted[] = {" lead", "trail ", "a: b", "a #b", "#x", "-", "- x", "---", "'q'", "\"q\"", "{x}", "[x]", "*alias", "&anchor",
                            "!tag", "line\nbreak", "tab\tx", "\x01", "ends:", "%d", "@x", "`x", "|", ">"};
    for (const char* s : quoted)
        HX_CHECK(!Emitter::PlainIsSafe(s));
    // Every one of them reads back as itself, as a block value and inside a flow mapping.
    for (int flow = 0; flow < 2; flow++)
        for (const char* s : quoted)
        {
            String out;
            Emitter e(out);
            e.BeginDocument();
            e.BeginMapping();
            e.Key("k");
            e.BeginMapping(flow != 0);
            e.Key(s);
            e.Scalar(s);
            e.EndMapping();
            e.EndMapping();
            Tree t;
            bool ok = ParseText(out, t);
            u32 inner = ok ? t.Find(t.documents[0].root, "k") : kNoNode;
            bool same = inner != kNoNode && t.Count(inner) == 1 && t.Text(t.Key(inner, 0)) == StringView(s) && t.Text(t.Value(inner, 0)) == StringView(s);
            if (!same)
                std::printf("    '%s' does not read back:\n%s", s, out.CStr());
            HX_CHECK(same);
        }
}

HX_TEST(Yaml, NumbersAndFloats)
{
    Tree t;
    HX_REQUIRE(ParseText("a: 0xFF\nb: -12\nc: +7\nd: Infinity\ne: -inf\nf: NaN\ng: 1.5e3\nh: text\n", t));
    u32 r = t.documents[0].root;
    HX_CHECK(Int(t, t.Find(r, "a")) == 255 && Int(t, t.Find(r, "b")) == -12 && Int(t, t.Find(r, "c")) == 7);
    HX_CHECK(std::isinf(Float(t, t.Find(r, "d"))) && Float(t, t.Find(r, "e")) < 0 && std::isnan(Float(t, t.Find(r, "f"))));
    HX_CHECK(Float(t, t.Find(r, "g")) == 1500.0);
    i64 v;
    HX_CHECK(!t.AsInt(t.Find(r, "h"), v));
    // Floats are written with the fewest digits that read back as the same value.
    for (float f : {0.1f, 1.0f / 3.0f, 1e-7f, 123456.789f, -0.0f})
    {
        String out;
        Emitter e(out);
        e.BeginDocument();
        e.Float(f);
        Tree back;
        HX_REQUIRE(ParseText(out, back));
        double d;
        HX_CHECK(back.AsFloat(back.documents[0].root, d) && float(d) == f);
    }
}

HX_TEST(Yaml, FloatsAsTheirBitsInHex)
{
    // The reference files write floats as their IEEE 754 bits, the decimal value in parentheses for reading.
    Tree t;
    HX_REQUIRE(ParseText("a: 0x3F800000(1)\nb: 0x3f800000\nc: 0xBF000000(-0.5)\nd: 0x3FF8000000000000\ne: 0x10\nf: 0x3F80000(1\n", t));
    u32 r = t.documents[0].root;
    HX_CHECK(Float(t, t.Find(r, "a")) == 1.0 && Float(t, t.Find(r, "b")) == 1.0 && Float(t, t.Find(r, "c")) == -0.5);
    HX_CHECK(Float(t, t.Find(r, "d")) == 1.5 && Float(t, t.Find(r, "e")) == 16.0 && Float(t, t.Find(r, "f")) == -12345);
}

HX_TEST(Yaml, LineEndingsCommentsAndBlankLines)
{
    Tree t;
    HX_REQUIRE(ParseText("# a comment\r\n\r\na:\r\n  # inside\r\n  b: 1 # trailing\r\n\r\n  c: [1,\r\n    2]\r\nd: 'x'\r\n", t));
    u32 r = t.documents[0].root;
    HX_CHECK(Int(t, At(t, r, "a/b")) == 1);
    HX_CHECK(Int(t, At(t, r, "a/c/1")) == 2);
    HX_CHECK(t.Text(t.Find(r, "d")) == StringView("x"));
}

HX_TEST(Yaml, DamagedTextIsAnErrorWithItsPlace)
{
    struct Case
    {
        const char* text;
        const char* where;
    };
    const Case cases[] = {
        {"a: [1, 2\n", "line 1"},
        {"a: {b: 1\n", "line 1"},
        {"a: 'open\nb: 2\n", "line 1"},
        {"a: \"bad \\q escape\"\n", "line 1"},
        {"--- !h!x &1\na: 1\n", "line 1"},
        {"a: 1\n    b: 2\n", "line 2"},
        {"a: 1\nb\n", "line 2"},
        {"a: {b: 1 c: 2}\n", "line 1"},
        {"a: [1,, 2]\n", "line 1"},
        {"a: |\n  text\n", "line 1"},
        {"a: 'x' y\n", "line 1"},
    };
    for (const Case& c : cases)
    {
        Tree t;
        String error;
        bool ok = Parse(c.text, t, error);
        HX_CHECK(!ok);
        bool placed = StringView(error).StartsWith(c.where);
        if (ok || !placed)
            std::printf("    %s -> %s\n", c.text, error.CStr());
        HX_CHECK(placed);
    }
}
