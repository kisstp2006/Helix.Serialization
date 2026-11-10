#include <Helix/Serialization/Yaml/Emitter.h>

#include <Helix/Core/Strings/Format.h>

#include <charconv>
#include <cmath>

namespace helix::serialization::yaml
{
    void Emitter::Directives()
    {
        Directive("YAML 1.1");
        Directive("TAG !h! tag:helixengine,2026:");
    }

    void Emitter::Directive(StringView text)
    {
        out_.Append('%');
        out_.Append(text);
        out_.Append('\n');
    }

    void Emitter::BeginDocument(i64 classID, i64 fileID, bool stripped)
    {
        out_.Append("---");
        if (classID >= 0)
            FormatAppend(out_, " !h!{}", classID);
        if (fileID != 0)
            FormatAppend(out_, " &{}", fileID);
        if (stripped)
            out_.Append(" stripped");
        out_.Append('\n');
        levels_.Clear();
        keyPending_ = false;
        inDocument_ = true;
    }

    bool Emitter::PlainIsSafe(StringView text)
    {
        if (text.Empty())
            return true;
        static constexpr StringView kIndicators = "?:,[]{}#&*!|>'\"%@`";
        char first = text[0];
        if (kIndicators.Find(first) != StringView::npos)
            return false;
        if (first == '-' && (text.Size() == 1 || text[1] == ' '))
            return false;
        if (first == ' ' || first == '\t' || text[text.Size() - 1] == ' ' || text[text.Size() - 1] == '\t' || text[text.Size() - 1] == ':')
            return false;
        for (usize i = 0; i < text.Size(); i++)
        {
            u8 c = u8(text[i]);
            if (c < 0x20 || c == 0x7F)
                return false;
            if (c == ':' && i + 1 < text.Size() && text[i + 1] == ' ')
                return false;
            if (c == '#' && i > 0 && (text[i - 1] == ' ' || text[i - 1] == '\t'))
                return false;
        }
        // A document marker at the start of a line.
        return !(text.StartsWith("---") || text.StartsWith("..."));
    }

    void Emitter::Text(StringView text, ScalarStyle style)
    {
        bool inFlow = !levels_.Empty() && levels_.Back().flow;
        bool control = false, flowIndicator = false;
        for (char ch : text)
        {
            u8 c = u8(ch);
            control = control || c < 0x20 || c == 0x7F;
            flowIndicator = flowIndicator || ch == ',' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
        }
        if (style == ScalarStyle::Plain && PlainIsSafe(text) && !(inFlow && flowIndicator))
        {
            out_.Append(text);
            return;
        }
        if (style != ScalarStyle::DoubleQuoted && !control)
        {
            out_.Append('\'');
            for (char ch : text)
            {
                if (ch == '\'')
                    out_.Append('\'');
                out_.Append(ch);
            }
            out_.Append('\'');
            return;
        }
        out_.Append('"');
        for (char ch : text)
        {
            u8 c = u8(ch);
            switch (ch)
            {
            case '"': out_.Append("\\\""); break;
            case '\\': out_.Append("\\\\"); break;
            case '\n': out_.Append("\\n"); break;
            case '\t': out_.Append("\\t"); break;
            case '\r': out_.Append("\\r"); break;
            case '\0': out_.Append("\\0"); break;
            default:
                if (c < 0x20 || c == 0x7F)
                {
                    static constexpr char kHex[] = "0123456789ABCDEF";
                    out_.Append("\\x");
                    out_.Append(kHex[c >> 4]);
                    out_.Append(kHex[c & 15]);
                }
                else
                    out_.Append(ch);
                break;
            }
        }
        out_.Append('"');
    }

    // What comes before a scalar or a flow container in the current context.
    void Emitter::BeforeValue()
    {
        if (levels_.Empty())
            return;
        Level& l = levels_.Back();
        if (l.mapping)
        {
            out_.Append(' ');
            return;
        }
        if (l.flow)
        {
            if (l.count > 0)
                out_.Append(", ");
        }
        else
        {
            if (l.open)
            {
                out_.Append('\n');
                l.open = false;
            }
            Indent(l.indent);
            out_.Append("- ");
        }
        l.count++;
    }

    void Emitter::Scalar(StringView text) { Scalar(text, ScalarStyle::Plain); }

    void Emitter::Scalar(StringView text, ScalarStyle style)
    {
        BeforeValue();
        Text(text, style);
        if (levels_.Empty() || !levels_.Back().flow)
            out_.Append('\n');
        keyPending_ = false;
    }

    void Emitter::Int(i64 value)
    {
        char buffer[32];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Scalar(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Emitter::UInt(u64 value)
    {
        char buffer[32];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Scalar(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Emitter::Float(double value)
    {
        if (std::isnan(value))
            return Scalar("NaN");
        if (std::isinf(value))
            return Scalar(value > 0 ? "Infinity" : "-Infinity");
        char buffer[64];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Scalar(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Emitter::Float(float value)
    {
        if (std::isnan(value))
            return Scalar("NaN");
        if (std::isinf(value))
            return Scalar(value > 0 ? "Infinity" : "-Infinity");
        char buffer[64];
        auto r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        Scalar(StringView(buffer, usize(r.ptr - buffer)));
    }

    void Emitter::Key(StringView key)
    {
        Level& l = levels_.Back();
        if (l.flow)
        {
            if (l.count > 0)
                out_.Append(", ");
        }
        else
        {
            // The first key of a mapping that is a sequence item stays on the item's line, after its dash.
            bool afterDash = false;
            if (l.open)
            {
                afterDash = levels_.Size() >= 2 && !levels_[levels_.Size() - 2].mapping && !levels_[levels_.Size() - 2].flow;
                if (!afterDash)
                    out_.Append('\n');
                l.open = false;
            }
            if (!afterDash)
                Indent(l.indent);
        }
        if (key.Empty())
            out_.Append("''");
        else
            Text(key, ScalarStyle::Plain);
        out_.Append(':');
        l.count++;
        keyPending_ = true;
    }

    void Emitter::BeginContainer(bool mapping, bool flow)
    {
        bool parentFlow = !levels_.Empty() && levels_.Back().flow;
        flow = flow || parentFlow;
        if (flow)
        {
            BeforeValue();
            out_.Append(mapping ? '{' : '[');
            levels_.Push({mapping, true, 0, 0, false});
            keyPending_ = false;
            return;
        }
        if (levels_.Empty())
        {
            // The root of a document: entries or items start at the first column.
            levels_.Push({mapping, false, 0, 0, false});
            keyPending_ = false;
            return;
        }
        Level& parent = levels_.Back();
        if (parent.mapping)
        {
            // A block value of a key: a mapping indents by two, a sequence stands at the key's indentation.
            u32 indent = mapping ? parent.indent + 2 : parent.indent;
            levels_.Push({mapping, false, indent, 0, true});
        }
        else
        {
            if (parent.open)
            {
                out_.Append('\n');
                parent.open = false;
            }
            Indent(parent.indent);
            out_.Append(mapping ? "- " : "-");
            parent.count++;
            levels_.Push({mapping, false, parent.indent + 2, 0, true});
        }
        keyPending_ = false;
    }

    void Emitter::EndContainer()
    {
        Level l = levels_.Back();
        levels_.Pop();
        if (l.flow)
        {
            out_.Append(l.mapping ? '}' : ']');
            if (levels_.Empty() || !levels_.Back().flow)
                out_.Append('\n');
        }
        else if (l.count == 0)
        {
            bool afterDash = l.mapping && !levels_.Empty() && !levels_.Back().mapping && !levels_.Back().flow;
            if (levels_.Empty())
                out_.Append(l.mapping ? "{}\n" : "[]\n");
            else
                out_.Append(afterDash ? "{}\n" : l.mapping ? " {}\n" : " []\n");
        }
        keyPending_ = false;
    }

    void Emitter::BeginMapping(bool flow) { BeginContainer(true, flow); }
    void Emitter::EndMapping() { EndContainer(); }
    void Emitter::BeginSequence(bool flow) { BeginContainer(false, flow); }
    void Emitter::EndSequence() { EndContainer(); }

    namespace
    {
        void WriteNode(const Tree& tree, u32 node, Emitter& e)
        {
            const Node& n = tree[node];
            switch (n.kind)
            {
            case NodeKind::Scalar: e.Scalar(n.Text(), n.style); break;
            case NodeKind::Mapping:
                e.BeginMapping(n.flow);
                for (u32 i = 0; i < n.Count(); i++)
                {
                    e.Key(tree.Text(tree.Key(node, i)));
                    WriteNode(tree, tree.Value(node, i), e);
                }
                e.EndMapping();
                break;
            case NodeKind::Sequence:
                e.BeginSequence(n.flow);
                for (u32 i = 0; i < n.Count(); i++)
                    WriteNode(tree, tree.Item(node, i), e);
                e.EndSequence();
                break;
            }
        }
    }

    void Write(const Tree& tree, String& out)
    {
        Emitter e(out);
        for (StringView d : tree.directives)
            e.Directive(d);
        for (const Document& d : tree.documents)
        {
            e.BeginDocument(d.classID, d.fileID, d.stripped);
            if (d.root != kNoNode)
                WriteNode(tree, d.root, e);
        }
    }
}
