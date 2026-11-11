#include <Helix/Serialization/Transfer/Binary.h>

namespace helix::serialization
{
    bool SafeBinaryRead::Fail(StringView what, usize offset)
    {
        if (!failed_)
        {
            failed_ = true;
            error_ = Format("{} at byte {}", what, offset);
        }
        return false;
    }

    usize SafeBinaryRead::Skip(u32 node, usize offset)
    {
        if (failed_)
            return offset;
        const TypeTreeNode& n = tree_.nodes[node];
        if (n.byteSize >= 0)
        {
            if (!Check(offset, usize(n.byteSize)))
                return offset;
            return offset + usize(n.byteSize);
        }
        auto align = [&](usize at) {
            usize aligned = (at + 3) & ~usize(3);
            if (aligned > data_.Size())
            {
                Fail("the data ends", at);
                return at;
            }
            return aligned;
        };
        switch (n.kind)
        {
        case ValueKind::String:
        {
            u32 count;
            if (!Count(offset, count, 1))
                return offset;
            return align(offset + 4 + count);
        }
        case ValueKind::Array:
        {
            u32 item = node + 1;
            i32 itemSize = tree_.nodes[item].byteSize;
            u32 count;
            if (!Count(offset, count, itemSize > 0 ? usize(itemSize) : 1))
                return offset;
            usize at = offset + 4;
            if (itemSize >= 0)
                at += usize(count) * usize(itemSize);
            else
                for (u32 i = 0; i < count && !failed_; i++)
                    at = Skip(item, at);
            return failed_ ? offset : align(at);
        }
        case ValueKind::FixedArray:
        {
            usize at = offset;
            for (u32 i = 0; i < n.count && !failed_; i++)
                at = Skip(node + 1, at);
            return at;
        }
        case ValueKind::Struct:
        {
            usize at = offset;
            for (u32 c = node + 1; c < tree_.nodes.Size() && tree_.nodes[c].level > n.level && !failed_; c = tree_.Next(c))
                at = Skip(c, at);
            return at;
        }
        default: Fail("a value without a size", offset); return offset;
        }
    }
}
