#include <Helix/Serialization/Transfer/Yaml.h>

#include <charconv>
#include <cmath>

namespace helix::serialization
{
    StringView KindName(ValueKind kind)
    {
        static constexpr StringView kNames[kValueKindCount] = {"bool",  "int8",  "uint8",  "int16", "uint16", "int32",  "uint32", "int64",
                                                               "uint64", "float", "double", "string", "Guid",  "Array", "FixedArray", "struct"};
        return u32(kind) < kValueKindCount ? kNames[u32(kind)] : StringView("?");
    }

    void YamlWrite::Bytes(const u8* data, usize size)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        String hex;
        hex.Resize(size * 2);
        for (usize i = 0; i < size; i++)
        {
            hex[2 * i] = kHex[data[i] >> 4];
            hex[2 * i + 1] = kHex[data[i] & 15];
        }
        e_.Scalar(hex);
    }

    u32 YamlRead::FindField(StringView name)
    {
        if (!t_.IsMapping(mapping_))
            return yaml::kNoNode;
        u32 count = t_.Count(mapping_);
        for (u32 n = 0; n < count; n++)
        {
            u32 i = cursor_ + n < count ? cursor_ + n : cursor_ + n - count;
            if (t_.Text(t_.Key(mapping_, i)) == name)
            {
                cursor_ = i + 1;
                return t_.Value(mapping_, i);
            }
        }
        return yaml::kNoNode;
    }

    void YamlRead::Error(u32 node, StringView name, StringView what) { errors_.Push(Format("line {}: {}: {}", t_[node].line, name, what)); }

    bool YamlRead::Expect(u32 node, StringView name, yaml::NodeKind kind)
    {
        if (t_[node].kind == kind)
            return true;
        static constexpr StringView kNames[] = {"a scalar", "a mapping", "a sequence"};
        Error(node, name, Format("{} where {} is expected", kNames[u32(t_[node].kind)], kNames[u32(kind)]));
        return false;
    }

    bool YamlRead::Sequence(u32 node, StringView name) { return Expect(node, name, yaml::NodeKind::Sequence); }

    bool YamlRead::Integer(u32 node, StringView name, i64 min, i64 max, i64& out)
    {
        if (!Expect(node, name, yaml::NodeKind::Scalar))
            return false;
        i64 v;
        if (!t_.AsInt(node, v))
        {
            // Integers written as floats by other writers ("1.0", "1e3") still read when they are whole.
            double d;
            if (!t_.AsFloat(node, d) || d != std::floor(d) || d < -9.2233720368547758e18 || d >= 9.2233720368547758e18)
            {
                Error(node, name, Format("'{}' is not an integer", t_.Text(node)));
                return false;
            }
            v = i64(d);
        }
        if (v < min || v > max)
        {
            Error(node, name, Format("{} is outside {} to {}", v, min, max));
            return false;
        }
        out = v;
        return true;
    }

    void YamlRead::UInt64(u32 node, StringView name, u64& out)
    {
        if (!Expect(node, name, yaml::NodeKind::Scalar))
            return;
        StringView t = t_.Text(node);
        u64 v = 0;
        const char* begin = t.Data();
        const char* end = begin + t.Size();
        int base = 10;
        if (end - begin > 2 && begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X'))
            begin += 2, base = 16;
        auto r = std::from_chars(begin, end, v, base);
        if (r.ec != std::errc() || r.ptr != end || begin == end)
            return Error(node, name, Format("'{}' is not an unsigned integer", t));
        out = v;
    }

    bool YamlRead::Real(u32 node, StringView name, double& out)
    {
        if (!Expect(node, name, yaml::NodeKind::Scalar))
            return false;
        if (!t_.AsFloat(node, out))
        {
            Error(node, name, Format("'{}' is not a number", t_.Text(node)));
            return false;
        }
        return true;
    }

    void YamlRead::GuidValue(u32 node, StringView name, Guid& out)
    {
        if (!Expect(node, name, yaml::NodeKind::Scalar))
            return;
        StringView t = t_.Text(node);
        Guid g;
        if (t.Empty())
            return void(out = Guid());
        if (!Guid::TryParse(t, g))
            return Error(node, name, Format("'{}' is not a guid", t));
        out = g;
    }

    void YamlRead::Bytes(u32 node, StringView name, Array<u8>& out)
    {
        if (!Expect(node, name, yaml::NodeKind::Scalar))
            return;
        StringView t = t_.Text(node);
        if (t.Size() % 2 != 0)
            return Error(node, name, "hex bytes of odd length");
        auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        Array<u8> bytes;
        bytes.Resize(t.Size() / 2);
        for (usize i = 0; i < bytes.Size(); i++)
        {
            int hi = hex(t[2 * i]), lo = hex(t[2 * i + 1]);
            if (hi < 0 || lo < 0)
                return Error(node, name, "bytes that are not hex digits");
            bytes[i] = u8(hi * 16 + lo);
        }
        out = Move(bytes);
    }
}
