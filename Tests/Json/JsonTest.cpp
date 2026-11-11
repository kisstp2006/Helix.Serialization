#include <Helix/Serialization/Json/Parser.h>
#include <Helix/Serialization/Json/Writer.h>
#include <Helix/Testing/Test.h>

#include <cmath>
#include <cstdio>

using namespace helix;
using namespace helix::serialization::json;

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

    bool Refused(StringView text, StringView where = StringView())
    {
        Tree tree;
        String error;
        if (Parse(text, tree, error))
        {
            std::printf("    accepted: %.*s\n", int(text.Size()), text.Data());
            return false;
        }
        if (!where.Empty() && !error.View().StartsWith(where))
        {
            std::printf("    %s (expected at %.*s)\n", error.CStr(), int(where.Size()), where.Data());
            return false;
        }
        return true;
    }

    String Compact(StringView text)
    {
        Tree tree;
        String out;
        if (ParseText(text, tree))
            Write(tree, out);
        return out;
    }

    bool SameNode(const Tree& a, u32 x, const Tree& b, u32 y)
    {
        const Node& n = a[x];
        const Node& m = b[y];
        if (n.kind != m.kind || n.Count() != m.Count())
            return false;
        if (n.kind != NodeKind::Object && n.kind != NodeKind::Array)
            return n.Text() == m.Text();
        for (u32 i = 0; i < n.Count(); i++)
        {
            if (n.kind == NodeKind::Object)
            {
                if (a.Text(a.Key(x, i)) != b.Text(b.Key(y, i)) || !SameNode(a, a.Value(x, i), b, b.Value(y, i)))
                    return false;
            }
            else if (!SameNode(a, a.Item(x, i), b, b.Item(y, i)))
                return false;
        }
        return true;
    }

    constexpr const char* kSample = R"({
    "name": "Main Camera",
    "enabled": true,
    "parent": null,
    "layer": 0,
    "position": {"x": 0, "y": 1.5, "z": -10},
    "tags": ["a", "b\n\"c\"", "\u00e9\u4e2d\ud83d\ude00", ""],
    "empty": {},
    "none": [],
    "big": 18446744073709551615,
    "neg": -9223372036854775808,
    "exp": 2.5E+3,
    "tiny": 1e-400,
    "huge": -1e400
})";
}

HX_TEST(Json, ReadsEveryKindOfValue)
{
    Tree t;
    HX_REQUIRE(ParseText(kSample, t));
    u32 r = t.root;
    HX_REQUIRE(t.IsObject(r) && t.Count(r) == 13u);
    HX_CHECK(t.Text(t.Find(r, "name")) == StringView("Main Camera"));
    bool b = false;
    HX_CHECK(t.AsBool(t.Find(r, "enabled"), b) && b);
    HX_CHECK(t.Is(t.Find(r, "parent"), NodeKind::Null));
    u32 position = t.Find(r, "position");
    double d = 0;
    HX_CHECK(t.AsDouble(t.Find(position, "y"), d) && d == 1.5);
    i64 i = 0;
    HX_CHECK(t.AsInt(t.Find(position, "z"), i) && i == -10);
    u32 tags = t.Find(r, "tags");
    HX_REQUIRE(t.IsArray(tags) && t.Count(tags) == 4u);
    HX_CHECK(t.Text(t.Item(tags, 1)) == StringView("b\n\"c\""));
    HX_CHECK(t.Text(t.Item(tags, 2)) == StringView("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80"));
    HX_CHECK(t.Text(t.Item(tags, 3)).Empty() && t.IsString(t.Item(tags, 3)));
    HX_CHECK(t.IsObject(t.Find(r, "empty")) && t.Count(t.Find(r, "empty")) == 0u);
    HX_CHECK(t.IsArray(t.Find(r, "none")) && t.Count(t.Find(r, "none")) == 0u);
    u64 u = 0;
    HX_CHECK(t.AsUInt(t.Find(r, "big"), u) && u == 18446744073709551615ull);
    HX_CHECK(!t.AsInt(t.Find(r, "big"), i));
    HX_CHECK(t.AsInt(t.Find(r, "neg"), i) && i == i64(-9223372036854775807ll - 1));
    HX_CHECK(t.AsInt(t.Find(r, "exp"), i) && i == 2500);
    HX_CHECK(t.AsDouble(t.Find(r, "tiny"), d) && d == 0.0);
    HX_CHECK(t.AsDouble(t.Find(r, "huge"), d) && std::isinf(d) && d < 0);
    HX_CHECK(t[t.Find(r, "position")].line == 6u && t[t.root].line == 1u);
    // Plain strings are views into the text.
    StringView name = t.Text(t.Find(r, "name"));
    HX_CHECK(name.Data() > kSample && name.Data() < kSample + 400);
}

HX_TEST(Json, AcceptsWhatTheGrammarAllows)
{
    HX_CHECK(Compact("0") == StringView("0"));
    HX_CHECK(Compact("-0") == StringView("-0"));
    HX_CHECK(Compact(" \t\r\n\"x\" \n") == StringView("\"x\""));
    HX_CHECK(Compact("\xEF\xBB\xBF{}") == StringView("{}"));
    HX_CHECK(Compact("[1.25e-3,0.5,10E2,1e+1]") == StringView("[1.25e-3,0.5,10E2,1e+1]"));
    HX_CHECK(Compact("{\"a\":1,\"a\":2}") == StringView("{\"a\":1,\"a\":2}"));
    HX_CHECK(Compact("[[[[]]],{\"\":{}}]") == StringView("[[[[]]],{\"\":{}}]"));
    HX_CHECK(Compact("\"\\/\\b\\f\\r\\t\"") == StringView("\"/\\b\\f\\r\\t\""));
    HX_CHECK(Compact("\"\\u0000\\u001F\\u007f\"") == StringView("\"\\u0000\\u001f\x7F\""));
    // The largest code point and the edges of each UTF-8 length.
    HX_CHECK(Compact("\"\xF4\x8F\xBF\xBF \xC2\x80 \xE0\xA0\x80 \xF0\x90\x80\x80 \xEF\xBF\xBF\"") ==
             StringView("\"\xF4\x8F\xBF\xBF \xC2\x80 \xE0\xA0\x80 \xF0\x90\x80\x80 \xEF\xBF\xBF\""));
    HX_CHECK(Compact("\"\\udbff\\udfff\"") == StringView("\"\xF4\x8F\xBF\xBF\""));

    String deep;
    for (u32 i = 0; i < kMaxDepth; i++)
        deep.Append('[');
    for (u32 i = 0; i < kMaxDepth; i++)
        deep.Append(']');
    Tree t;
    HX_CHECK(ParseText(deep, t));
}

HX_TEST(Json, RefusesWhatTheGrammarDoesNot)
{
    HX_CHECK(Refused("", "line 1, column 1: no value"));
    HX_CHECK(Refused("   \n  ", "line 2, column 3: no value"));
    HX_CHECK(Refused("{} x", "line 1, column 4: text after the value"));
    HX_CHECK(Refused("[1,]", "line 1, column 4: not a value"));
    HX_CHECK(Refused("{\"a\":1,}", "line 1, column 8: an object key that is not a string"));
    HX_CHECK(Refused("{a:1}", "line 1, column 2"));
    HX_CHECK(Refused("{\"a\" 1}", "line 1, column 6: a key without a colon"));
    HX_CHECK(Refused("{\"a\":}", "line 1, column 6: not a value"));
    HX_CHECK(Refused("[1 2]", "line 1, column 4: array items without a comma"));
    HX_CHECK(Refused("{\"a\":1 \"b\":2}", "line 1, column 8: object entries without a comma"));
    HX_CHECK(Refused("[1", "line 1, column 3: an array without its closing bracket"));
    HX_CHECK(Refused("{\"a\":1", "line 1, column 7: an object without its closing brace"));
    HX_CHECK(Refused("\"abc", "line 1, column 5: a string without its closing quote"));
    HX_CHECK(Refused("\"a\\", "line 1, column 4"));
    HX_CHECK(Refused("\"a\tb\"", "line 1, column 3: a control character in a string"));
    HX_CHECK(Refused("\"\\x41\"", "line 1, column 3: an unknown escape"));
    HX_CHECK(Refused("\"\\u12\"", "line 1, column 4: a \\u escape without four hex digits"));
    HX_CHECK(Refused("\"\\ud800\"", "line 1, column 8: a high surrogate without a low one"));
    HX_CHECK(Refused("\"\\ud800\\u0041\"", "line 1"));
    HX_CHECK(Refused("\"\\udc00\"", "line 1, column 8: a low surrogate without a high one"));
    for (const char* bad : {"\"\x80\"", "\"\xC0\xAF\"", "\"\xC1\xBF\"", "\"\xE0\x80\xAF\"", "\"\xED\xA0\x80\"", "\"\xF0\x80\x80\xAF\"",
                            "\"\xF4\x90\x80\x80\"", "\"\xF5\x80\x80\x80\"", "\"\xFF\"", "\"\xC3\"", "\"\xE4\xB8\""})
        HX_CHECK(Refused(bad, "line 1, column 2: text that is not UTF-8"));
    for (const char* bad : {"01", "-", "-01", "1.", ".5", "1e", "1e+", "+1", "0x10", "1.e3", "Infinity", "NaN", "--1"})
        HX_CHECK(Refused(bad));
    for (const char* bad : {"tru", "True", "nul", "falsey", "nulll", "'a'", "[1]]", "{}}", "/* */ 1", "1 // x"})
        HX_CHECK(Refused(bad));
    HX_CHECK(Refused("[\n  1,\n  2,\n]", "line 4, column 1: not a value"));

    String deep;
    for (u32 i = 0; i <= kMaxDepth; i++)
        deep.Append('[');
    HX_CHECK(Refused(deep, "line 1, column 513: containers nested too deep"));
}

HX_TEST(Json, WritesCompactAndPretty)
{
    String out;
    Writer w(out, true);
    w.BeginObject();
    w.Key("name");
    w.String("Cube \"one\"\n");
    w.Key("size");
    w.Double(1.5);
    w.Key("count");
    w.Int(-3);
    w.Key("items");
    w.BeginArray();
    w.UInt(1);
    w.Bool(false);
    w.Null();
    w.EndArray();
    w.Key("empty");
    w.BeginArray();
    w.EndArray();
    w.Key("nested");
    w.BeginObject();
    w.EndObject();
    w.EndObject();
    HX_CHECK(out == StringView("{\n"
                               "    \"name\": \"Cube \\\"one\\\"\\n\",\n"
                               "    \"size\": 1.5,\n"
                               "    \"count\": -3,\n"
                               "    \"items\": [\n"
                               "        1,\n"
                               "        false,\n"
                               "        null\n"
                               "    ],\n"
                               "    \"empty\": [],\n"
                               "    \"nested\": {}\n"
                               "}"));

    Tree t;
    HX_REQUIRE(ParseText(out, t));
    String compact;
    Write(t, compact);
    HX_CHECK(compact == StringView("{\"name\":\"Cube \\\"one\\\"\\n\",\"size\":1.5,\"count\":-3,\"items\":[1,false,null],\"empty\":[],\"nested\":{}}"));
    String pretty;
    Write(t, pretty, true);
    HX_CHECK(pretty == out);
}

HX_TEST(Json, FloatsReadBackExactly)
{
    double values[] = {0.1, 1.0 / 3.0, 1e300, -2.2250738585072014e-308, 5e-324, 123456789.125, -0.0};
    float floats[] = {0.1f, 3.4028235e38f, 1.17549435e-38f, 16777216.0f, 0.3f};
    String out;
    Writer w(out);
    w.BeginArray();
    for (double v : values)
        w.Double(v);
    for (float v : floats)
        w.Float(v);
    w.Double(NAN);
    w.Double(INFINITY);
    w.Float(-INFINITY);
    w.EndArray();
    Tree t;
    HX_REQUIRE(ParseText(out, t));
    u32 n = 0;
    for (double v : values)
    {
        double d = 1;
        HX_CHECK(t.AsDouble(t.Item(t.root, n++), d) && d == v && std::signbit(d) == std::signbit(v));
    }
    for (float v : floats)
    {
        double d = 1;
        HX_CHECK(t.AsDouble(t.Item(t.root, n++), d) && float(d) == v);
    }
    HX_CHECK(t.Text(t.Item(t.root, 12)) == StringView("NaN"));
    double d = 0;
    HX_CHECK(t.AsDouble(t.Item(t.root, 12), d) && std::isnan(d));
    HX_CHECK(t.AsDouble(t.Item(t.root, 13), d) && d == INFINITY);
    HX_CHECK(t.AsDouble(t.Item(t.root, 14), d) && d == -INFINITY);
    HX_CHECK(out.View().StartsWith("[0.1,0.3333333333333333,1e+300,"));
}

HX_TEST(Json, WrittenTextReadsBackTheSame)
{
    Tree a;
    HX_REQUIRE(ParseText(kSample, a));
    for (bool pretty : {false, true})
    {
        String text, again;
        Write(a, text, pretty);
        Tree b;
        HX_REQUIRE(ParseText(text, b));
        HX_CHECK(SameNode(a, a.root, b, b.root));
        Write(b, again, pretty);
        HX_CHECK(text == again);
    }
    // A parsed tree can be reused for the next text.
    HX_CHECK(ParseText("[1]", a) && a.nodes.Size() == 2u && a.Count(a.root) == 1u);
}
