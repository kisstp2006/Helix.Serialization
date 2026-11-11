#include <Helix/Serialization/Json/Tree.h>

#include <charconv>
#include <cmath>

namespace helix::serialization::json
{
    void Tree::Clear()
    {
        nodes.Clear();
        children.Clear();
        root = kNoNode;
        buffers_.Clear();
    }

    u32 Tree::Find(u32 object, StringView key) const
    {
        if (!IsObject(object))
            return kNoNode;
        const Node& o = nodes[object];
        for (u32 i = 0; i < o.Count(); i++)
            if (nodes[children[o.First() + 2 * i]].Text() == key)
                return children[o.First() + 2 * i + 1];
        return kNoNode;
    }

    bool Tree::AsBool(u32 node, bool& out) const
    {
        if (Is(node, NodeKind::True))
            return out = true, true;
        if (Is(node, NodeKind::False))
            return out = false, true;
        return false;
    }

    namespace
    {
        // Whether the number has a fraction or an exponent.
        bool HasPoint(StringView t)
        {
            for (char c : t)
                if (c == '.' || c == 'e' || c == 'E')
                    return true;
            return false;
        }
    }

    bool Tree::AsInt(u32 node, i64& out) const
    {
        if (!IsNumber(node))
            return false;
        StringView t = nodes[node].Text();
        if (!HasPoint(t))
        {
            auto r = std::from_chars(t.Data(), t.Data() + t.Size(), out);
            return r.ec == std::errc() && r.ptr == t.Data() + t.Size();
        }
        double d;
        if (!AsDouble(node, d) || d != std::floor(d) || d < -9223372036854775808.0 || d >= 9223372036854775808.0)
            return false;
        out = i64(d);
        return true;
    }

    bool Tree::AsUInt(u32 node, u64& out) const
    {
        if (!IsNumber(node))
            return false;
        StringView t = nodes[node].Text();
        if (!HasPoint(t))
        {
            auto r = std::from_chars(t.Data(), t.Data() + t.Size(), out);
            return r.ec == std::errc() && r.ptr == t.Data() + t.Size();
        }
        double d;
        if (!AsDouble(node, d) || d != std::floor(d) || d < 0 || d >= 18446744073709551616.0)
            return false;
        out = u64(d);
        return true;
    }

    bool Tree::AsDouble(u32 node, double& out) const
    {
        if (IsString(node))
        {
            StringView t = nodes[node].Text();
            if (t == StringView("NaN"))
                return out = NAN, true;
            if (t == StringView("Infinity"))
                return out = INFINITY, true;
            if (t == StringView("-Infinity"))
                return out = -INFINITY, true;
            return false;
        }
        if (!IsNumber(node))
            return false;
        StringView t = nodes[node].Text();
        auto r = std::from_chars(t.Data(), t.Data() + t.Size(), out);
        // Out of range is the infinity or zero of the right sign, as the number reads in other parsers.
        if (r.ec == std::errc::result_out_of_range)
        {
            bool negative = t[0] == '-';
            usize e = t.Find('e');
            if (e == StringView::npos)
                e = t.Find('E');
            bool small = e != StringView::npos && e + 1 < t.Size() && t[e + 1] == '-';
            out = small ? (negative ? -0.0 : 0.0) : (negative ? -INFINITY : INFINITY);
            return true;
        }
        return r.ec == std::errc() && r.ptr == t.Data() + t.Size();
    }

    StringView Tree::Store(StringView text)
    {
        if (text.Empty())
            return StringView();
        if (buffers_.Empty() || buffers_.Back().Capacity() - buffers_.Back().Size() < text.Size())
            buffers_.Emplace().Reserve(text.Size() > kBlock ? text.Size() : kBlock);
        Array<char>& block = buffers_.Back();
        usize at = block.Size();
        block.Append(text.Data(), text.Size());
        return StringView(block.Data() + at, text.Size());
    }

    u32 Tree::Add(const Node& node)
    {
        nodes.Push(node);
        return u32(nodes.Size() - 1);
    }

    void Tree::SetChildren(u32 container, const u32* entries, u32 count)
    {
        Node& n = nodes[container];
        n.SetRange(u32(children.Size()), n.kind == NodeKind::Object ? count / 2 : count);
        children.Append(entries, count);
    }
}
