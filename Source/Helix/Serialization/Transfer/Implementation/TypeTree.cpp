#include <Helix/Serialization/Transfer/TypeTree.h>

#include <Helix/Core/Strings/Format.h>

#include <cstring>

namespace helix::serialization
{
    namespace
    {
        i32 PrimitiveSize(ValueKind kind)
        {
            switch (kind)
            {
            case ValueKind::Bool:
            case ValueKind::Int8:
            case ValueKind::UInt8: return 1;
            case ValueKind::Int16:
            case ValueKind::UInt16: return 2;
            case ValueKind::Int32:
            case ValueKind::UInt32:
            case ValueKind::Float: return 4;
            case ValueKind::Int64:
            case ValueKind::UInt64:
            case ValueKind::Double: return 8;
            case ValueKind::Guid: return 16;
            default: return -1;
            }
        }

        constexpr u32 kMaxFixedCount = 1u << 20;
        constexpr usize kNodeBytes = 24;
        static_assert(sizeof(TypeTreeNode) == kNodeBytes);
    }

    StringView TypeTree::Str(u32 offset) const
    {
        if (offset >= strings.Size())
            return StringView();
        const char* s = strings.Data() + offset;
        return StringView(s, std::strlen(s));
    }

    u32 TypeTree::Next(u32 node) const
    {
        u8 level = nodes[node].level;
        u32 i = node + 1;
        while (i < nodes.Size() && nodes[i].level > level)
            i++;
        return i;
    }

    u32 TypeTree::AddString(StringView text)
    {
        usize at = 0;
        while (at < strings.Size())
        {
            usize size = std::strlen(strings.Data() + at);
            if (StringView(strings.Data() + at, size) == text)
                return u32(at);
            at += size + 1;
        }
        u32 offset = u32(strings.Size());
        strings.Append(text.Data(), text.Size());
        strings.Push('\0');
        return offset;
    }

    void TypeTree::ComputeSizes()
    {
        for (u32 i = u32(nodes.Size()); i-- > 0;)
        {
            TypeTreeNode& n = nodes[i];
            switch (n.kind)
            {
            case ValueKind::String:
            case ValueKind::Array: n.byteSize = -1; break;
            case ValueKind::FixedArray:
            {
                i32 item = i + 1 < nodes.Size() ? nodes[i + 1].byteSize : -1;
                i64 total = i64(item) * n.count;
                n.byteSize = item >= 0 && total <= 0x7FFFFFFF ? i32(total) : -1;
                break;
            }
            case ValueKind::Struct:
            {
                i64 total = 0;
                for (u32 c = i + 1; c < nodes.Size() && nodes[c].level > n.level; c = Next(c))
                {
                    if (nodes[c].byteSize < 0)
                    {
                        total = -1;
                        break;
                    }
                    total += nodes[c].byteSize;
                }
                n.byteSize = total >= 0 && total <= 0x7FFFFFFF ? i32(total) : -1;
                break;
            }
            default: n.byteSize = PrimitiveSize(n.kind); break;
            }
        }
    }

    Md5Digest TypeTree::Hash() const
    {
        Array<u8> bytes;
        Write(bytes);
        return ComputeMd5(bytes.Data(), bytes.Size());
    }

    String TypeTree::Dump() const
    {
        String out;
        for (u32 i = 0; i < nodes.Size(); i++)
        {
            const TypeTreeNode& n = nodes[i];
            out.Append(' ', usize(n.level) * 2);
            FormatAppend(out, "{} {} ({})", FieldName(i), TypeName(i), n.byteSize);
            if (n.kind == ValueKind::FixedArray)
                FormatAppend(out, " x{}", n.count);
            if (n.kind == ValueKind::Struct && n.version > 1)
                FormatAppend(out, " v{}", n.version);
            if (n.flags & TypeTreeNode::kFlow)
                out.Append(" flow");
            out.Append('\n');
        }
        return out;
    }

    void TypeTree::Write(Array<u8>& out) const
    {
        u32 header[2] = {u32(nodes.Size()), u32(strings.Size())};
        out.Append(reinterpret_cast<const u8*>(header), sizeof(header));
        out.Append(reinterpret_cast<const u8*>(nodes.Data()), nodes.Size() * kNodeBytes);
        out.Append(reinterpret_cast<const u8*>(strings.Data()), strings.Size());
    }

    bool TypeTree::Read(Span<const u8> bytes, TypeTree& out, String& error)
    {
        out.nodes.Clear();
        out.strings.Clear();
        u32 header[2];
        if (bytes.Size() < sizeof(header))
            return error = "a type tree shorter than its header", false;
        std::memcpy(header, bytes.Data(), sizeof(header));
        u32 count = header[0], stringBytes = header[1];
        if (count == 0 || u64(count) * kNodeBytes + stringBytes + sizeof(header) != bytes.Size())
            return error = Format("a type tree of {} nodes and {} string bytes in {} bytes", count, stringBytes, bytes.Size()), false;
        out.nodes.Resize(count);
        std::memcpy(static_cast<void*>(out.nodes.Data()), bytes.Data() + sizeof(header), usize(count) * kNodeBytes);
        out.strings.Append(reinterpret_cast<const char*>(bytes.Data()) + sizeof(header) + usize(count) * kNodeBytes, stringBytes);
        if (stringBytes > 0 && out.strings.Back() != '\0')
            return error = "a type tree whose names do not end", false;

        for (u32 i = 0; i < count; i++)
        {
            const TypeTreeNode& n = out.nodes[i];
            if (u32(n.kind) >= kValueKindCount)
                return error = Format("type tree node {}: kind {}", i, u32(n.kind)), false;
            if (n.typeName >= stringBytes || n.fieldName >= stringBytes)
                return error = Format("type tree node {}: a name outside the string table", i), false;
            if (i == 0 ? n.level != 0 : (n.level == 0 || n.level > out.nodes[i - 1].level + 1))
                return error = Format("type tree node {}: level {} after {}", i, n.level, i == 0 ? 0 : out.nodes[i - 1].level), false;
            bool hasChild = i + 1 < count && out.nodes[i + 1].level == n.level + 1;
            bool container = n.kind == ValueKind::Array || n.kind == ValueKind::FixedArray;
            if (container)
            {
                if (!hasChild || (out.Next(i + 1) < count && out.nodes[out.Next(i + 1)].level == n.level + 1))
                    return error = Format("type tree node {}: an array needs exactly one item node", i), false;
                if (n.kind == ValueKind::FixedArray && n.count > kMaxFixedCount)
                    return error = Format("type tree node {}: {} fixed items", i, n.count), false;
            }
            else if (n.kind != ValueKind::Struct && hasChild)
                return error = Format("type tree node {}: a {} with fields", i, KindName(n.kind)), false;
        }
        out.ComputeSizes();
        return true;
    }
}
