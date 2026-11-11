#include <Helix/Serialization/Json/Writer.h>

#include <charconv>
#include <cmath>

namespace helix::serialization::json
{
    void Writer::Line()
    {
        out_.Append('\n');
        out_.Append(' ', counts_.Size() * 4);
    }

    void Writer::BeforeValue()
    {
        if (keyPending_)
        {
            keyPending_ = false;
            return;
        }
        if (counts_.Empty())
            return;
        if (counts_.Back()++ > 0)
            out_.Append(',');
        if (pretty_)
            Line();
    }

    void Writer::Begin(char open)
    {
        BeforeValue();
        out_.Append(open);
        counts_.Push(0);
    }

    void Writer::End(char close)
    {
        u32 count = counts_.Back();
        counts_.Pop();
        if (pretty_ && count > 0)
            Line();
        out_.Append(close);
    }

    void Writer::BeginObject() { Begin('{'); }
    void Writer::EndObject() { End('}'); }
    void Writer::BeginArray() { Begin('['); }
    void Writer::EndArray() { End(']'); }

    void Writer::Key(StringView key)
    {
        BeforeValue();
        Quoted(key);
        out_.Append(pretty_ ? StringView(": ") : StringView(":"));
        keyPending_ = true;
    }

    void Writer::Quoted(StringView text)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        out_.Append('"');
        usize run = 0;
        for (usize i = 0; i < text.Size(); i++)
        {
            u8 c = u8(text[i]);
            if (c >= 0x20 && c != '"' && c != '\\')
                continue;
            out_.Append(text.Substr(run, i - run));
            run = i + 1;
            switch (c)
            {
            case '"': out_.Append("\\\""); break;
            case '\\': out_.Append("\\\\"); break;
            case '\b': out_.Append("\\b"); break;
            case '\f': out_.Append("\\f"); break;
            case '\n': out_.Append("\\n"); break;
            case '\r': out_.Append("\\r"); break;
            case '\t': out_.Append("\\t"); break;
            default:
                out_.Append("\\u00");
                out_.Append(kHex[c >> 4]);
                out_.Append(kHex[c & 15]);
                break;
            }
        }
        out_.Append(text.Substr(run, text.Size() - run));
        out_.Append('"');
    }

    void Writer::String(StringView text)
    {
        BeforeValue();
        Quoted(text);
    }

    void Writer::Bool(bool value)
    {
        BeforeValue();
        out_.Append(value ? StringView("true") : StringView("false"));
    }

    void Writer::Null()
    {
        BeforeValue();
        out_.Append("null");
    }

    void Writer::Int(i64 value)
    {
        char buffer[32];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Number(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Writer::UInt(u64 value)
    {
        char buffer[32];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Number(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Writer::Double(double value)
    {
        if (std::isnan(value))
            return String("NaN");
        if (std::isinf(value))
            return String(value > 0 ? "Infinity" : "-Infinity");
        char buffer[64];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Number(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Writer::Float(float value)
    {
        if (std::isnan(value))
            return String("NaN");
        if (std::isinf(value))
            return String(value > 0 ? "Infinity" : "-Infinity");
        char buffer[64];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Number(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Writer::Number(StringView text)
    {
        BeforeValue();
        out_.Append(text);
    }

    namespace
    {
        void WriteNode(const Tree& tree, u32 node, Writer& w)
        {
            const Node& n = tree[node];
            switch (n.kind)
            {
            case NodeKind::Null: w.Null(); break;
            case NodeKind::False: w.Bool(false); break;
            case NodeKind::True: w.Bool(true); break;
            case NodeKind::Number: w.Number(n.Text()); break;
            case NodeKind::String: w.String(n.Text()); break;
            case NodeKind::Object:
                w.BeginObject();
                for (u32 i = 0; i < n.Count(); i++)
                {
                    w.Key(tree.Text(tree.Key(node, i)));
                    WriteNode(tree, tree.Value(node, i), w);
                }
                w.EndObject();
                break;
            case NodeKind::Array:
                w.BeginArray();
                for (u32 i = 0; i < n.Count(); i++)
                    WriteNode(tree, tree.Item(node, i), w);
                w.EndArray();
                break;
            }
        }
    }

    void Write(const Tree& tree, helix::String& out, bool pretty)
    {
        if (tree.root == kNoNode)
            return;
        Writer w(out, pretty);
        WriteNode(tree, tree.root, w);
    }
}
