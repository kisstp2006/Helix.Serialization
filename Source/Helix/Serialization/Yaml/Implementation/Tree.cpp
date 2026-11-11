#include <Helix/Serialization/Yaml/Tree.h>

#include <charconv>
#include <cmath>
#include <cstring>

namespace helix::serialization::yaml
{
    void Tree::Clear()
    {
        nodes.Clear();
        children.Clear();
        documents.Clear();
        directives.Clear();
        lines.Clear();
        buffers_.Clear();
    }

    u32 Tree::Find(u32 mapping, StringView key) const
    {
        if (!IsMapping(mapping))
            return kNoNode;
        const Node& m = nodes[mapping];
        for (u32 i = 0; i < m.Count(); i++)
            if (nodes[children[m.First() + 2 * i]].Text() == key)
                return children[m.First() + 2 * i + 1];
        return kNoNode;
    }

    bool Tree::AsInt(u32 node, i64& out) const
    {
        if (!IsScalar(node))
            return false;
        StringView t = nodes[node].Text();
        const char* begin = t.Data();
        const char* end = begin + t.Size();
        bool negative = begin < end && *begin == '-';
        const char* digits = begin + (begin < end && (*begin == '-' || *begin == '+') ? 1 : 0);
        if (end - digits > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X'))
        {
            u64 v = 0;
            auto r = std::from_chars(digits + 2, end, v, 16);
            if (r.ec != std::errc() || r.ptr != end)
                return false;
            out = negative ? -i64(v) : i64(v);
            return true;
        }
        if (digits < end && *begin == '+')
            begin++;
        auto r = std::from_chars(begin, end, out);
        return r.ec == std::errc() && r.ptr == end && begin < end;
    }

    bool Tree::AsFloat(u32 node, double& out) const
    {
        if (!IsScalar(node))
            return false;
        StringView t = nodes[node].Text();
        if (t == StringView("inf") || t == StringView(".inf") || t == StringView("Infinity"))
            return out = INFINITY, true;
        if (t == StringView("-inf") || t == StringView("-.inf") || t == StringView("-Infinity"))
            return out = -INFINITY, true;
        if (t == StringView("nan") || t == StringView(".nan") || t == StringView("NaN"))
            return out = NAN, true;
        const char* begin = t.Data();
        const char* end = begin + t.Size();
        // IEEE 754 bits in hex, as scene files may write floats: 0x3F800000, optionally with the decimal value in
        // parentheses after it (only the bits count). 8 digits are a float, 16 a double, others an integer.
        if (end - begin > 2 && begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X'))
        {
            const char* digits = begin + 2;
            const char* stop = digits;
            while (stop < end && *stop != '(')
                stop++;
            if (stop < end && end[-1] != ')')
                return false;
            u64 bits = 0;
            auto r = std::from_chars(digits, stop, bits, 16);
            if (r.ec != std::errc() || r.ptr != stop)
                return false;
            if (stop - digits == 8)
            {
                u32 b = u32(bits);
                float f;
                std::memcpy(&f, &b, 4);
                out = f;
            }
            else if (stop - digits == 16)
                std::memcpy(&out, &bits, 8);
            else
                out = double(bits);
            return true;
        }
        if (begin < end && *begin == '+')
            begin++;
        auto r = std::from_chars(begin, end, out);
        return r.ec == std::errc() && r.ptr == end && begin < end;
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
        n.SetRange(u32(children.Size()), n.kind == NodeKind::Mapping ? count / 2 : count);
        children.Append(entries, count);
    }
}
