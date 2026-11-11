#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Strings/Format.h>
#include <Helix/Serialization/Transfer/TypeTree.h>

#include <bit>
#include <cstring>
#include <limits>

// The binary form: fields in order, little endian, numbers at their natural size (bool one byte), a Guid 16 bytes,
// strings and arrays a u32 count and their bytes or items, then padding to a multiple of 4 bytes from the start of the
// object; fixed arrays are their items without a count. Nothing names the fields: reading needs the same type, or the
// type tree the data was written with (SafeBinaryRead).
namespace helix::serialization
{
    static_assert(std::endian::native == std::endian::little, "the binary form is written by copying little endian memory");

    class BinaryWrite
    {
    public:
        static constexpr bool kReading = false;
        static constexpr bool kText = false;

        explicit BinaryWrite(Array<u8>& out, ReferenceTable* references = nullptr) : out_(out), base_(out.Size()), references_(references) {}

        template <class T>
        void Field(StringView, T& value)
        {
            Value(value);
        }

        i32 Version() const { return version_; }

        // Object references go through the table (the raw value is the fileID without one).
        void SetReferences(ReferenceTable* table) { references_ = table; }
        void ObjectReference(u64& object) { TransferObjectReference(*this, references_, object); }

        template <class T>
        void Value(T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (kind == ValueKind::Bool)
                out_.Push(u8(value ? 1 : 0));
            else if constexpr (IsNumeric(kind) || kind == ValueKind::Guid)
                Raw(&value, sizeof(T));
            else if constexpr (kind == ValueKind::String)
            {
                Count(value.Size());
                Raw(value.Data(), value.Size());
                Align();
            }
            else if constexpr (kind == ValueKind::Array)
            {
                using Item = typename detail::IsArray<T>::Item;
                Count(value.Size());
                if constexpr ((std::is_arithmetic_v<Item> && !std::is_same_v<Item, bool>) || std::is_enum_v<Item>)
                    Raw(value.Data(), value.Size() * sizeof(Item));
                else
                    for (Item& item : value)
                        Value(item);
                Align();
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                for (auto& item : value)
                    Value(item);
            }
            else
            {
                i32 saved = version_;
                version_ = VersionOf<T>();
                value.Transfer(*this);
                version_ = saved;
            }
        }

    private:
        void Raw(const void* data, usize size)
        {
            if (size > 0)
                std::memcpy(out_.AppendUninitialized(size), data, size);
        }
        void Count(usize count)
        {
            u32 c = u32(count);
            Raw(&c, 4);
        }
        void Align()
        {
            while ((out_.Size() - base_) % 4 != 0)
                out_.Push(u8(0));
        }

        Array<u8>& out_;
        usize base_;
        ReferenceTable* references_ = nullptr;
        i32 version_ = 1;
    };

    // Reads the binary form of the same type. Every read is checked against the end of the data; the first problem stops
    // the reading and is kept in Error().
    class BinaryRead
    {
    public:
        static constexpr bool kReading = true;
        static constexpr bool kText = false;

        explicit BinaryRead(Span<const u8> data, ReferenceTable* references = nullptr) : data_(data), references_(references) {}

        template <class T>
        void Field(StringView name, T& value)
        {
            if (!failed_)
            {
                name_ = name;
                Value(value);
            }
        }

        i32 Version() const { return version_; }
        bool Failed() const { return failed_; }
        const String& Error() const { return error_; }
        usize Position() const { return pos_; }

        // Object references go through the table (the raw value is the fileID without one).
        void SetReferences(ReferenceTable* table) { references_ = table; }
        void ObjectReference(u64& object) { TransferObjectReference(*this, references_, object); }

        template <class T>
        void Value(T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (kind == ValueKind::Bool)
            {
                u8 b = 0;
                if (Raw(&b, 1))
                    value = b != 0;
            }
            else if constexpr (IsNumeric(kind) || kind == ValueKind::Guid)
                Raw(&value, sizeof(T));
            else if constexpr (kind == ValueKind::String)
            {
                u32 count;
                if (!Count(count, 1))
                    return;
                value = StringView(reinterpret_cast<const char*>(data_.Data() + pos_), count);
                pos_ += count;
                Align();
            }
            else if constexpr (kind == ValueKind::Array)
            {
                using Item = typename detail::IsArray<T>::Item;
                constexpr bool raw = (std::is_arithmetic_v<Item> && !std::is_same_v<Item, bool>) || std::is_enum_v<Item>;
                u32 count;
                if (!Count(count, raw ? sizeof(Item) : 1))
                    return;
                value.Resize(count);
                if constexpr (raw)
                    Raw(value.Data(), usize(count) * sizeof(Item));
                else
                    for (u32 i = 0; i < count && !failed_; i++)
                        Value(value[i]);
                Align();
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                for (auto& item : value)
                    if (!failed_)
                        Value(item);
            }
            else
            {
                i32 saved = version_;
                version_ = VersionOf<T>();
                value.Transfer(*this);
                version_ = saved;
            }
        }

    private:
        bool Fail(StringView what)
        {
            if (!failed_)
            {
                failed_ = true;
                error_ = Format("{} at byte {} ({})", what, pos_, name_);
            }
            return false;
        }
        bool Raw(void* out, usize size)
        {
            if (data_.Size() - pos_ < size)
                return Fail("the data ends");
            if (size > 0)
                std::memcpy(out, data_.Data() + pos_, size);
            pos_ += size;
            return true;
        }
        // A count, and room for that many items of at least `itemBytes` (an item of a struct array takes at least a
        // byte here; a count larger than the data is damaged).
        bool Count(u32& count, usize itemBytes)
        {
            if (!Raw(&count, 4))
                return false;
            if (u64(count) * itemBytes > data_.Size() - pos_)
                return Fail(Format("a count of {} with {} bytes left", count, data_.Size() - pos_));
            return true;
        }
        void Align()
        {
            usize aligned = (pos_ + 3) & ~usize(3);
            if (aligned > data_.Size())
                Fail("the data ends");
            else
                pos_ = aligned;
        }

        Span<const u8> data_;
        ReferenceTable* references_ = nullptr;
        usize pos_ = 0;
        i32 version_ = 1;
        bool failed_ = false;
        StringView name_ = "Base";
        String error_;
    };

    // Reads data through the type tree it was written with, into a type whose fields may have changed since: fields are
    // matched by name, a field the data lacks keeps its value, a field the type lacks is skipped, numbers are converted
    // between the numeric kinds (a value that does not fit keeps the field's value), anything else of another kind is
    // skipped. v.Version() is the version the tree has for the struct being read.
    class SafeBinaryRead
    {
    public:
        static constexpr bool kReading = true;
        static constexpr bool kText = false;

        SafeBinaryRead(const TypeTree& tree, Span<const u8> data, ReferenceTable* references = nullptr)
            : tree_(tree), data_(data), references_(references)
        {
        }

        template <class T>
        void Field(StringView name, T& value)
        {
            if (failed_ || frames_.Empty())
                return;
            const Frame& f = frames_.Back();
            u32 count = f.end - f.begin;
            for (u32 n = 0; n < count; n++)
            {
                u32 i = f.cursor + n < count ? f.cursor + n : f.cursor + n - count;
                const Child& c = children_[f.begin + i];
                if (tree_.FieldName(c.node) == name)
                {
                    frames_.Back().cursor = i + 1;
                    At(c.node, c.offset, value);
                    return;
                }
            }
        }

        i32 Version() const { return version_; }
        // Object references go through the table (the raw value is the fileID without one).
        void SetReferences(ReferenceTable* table) { references_ = table; }
        void ObjectReference(u64& object) { TransferObjectReference(*this, references_, object); }
        bool Failed() const { return failed_; }
        const String& Error() const { return error_; }

        // Reads the value of tree node `node` at byte `offset`.
        template <class T>
        void At(u32 node, usize offset, T& value)
        {
            if (failed_)
                return;
            constexpr ValueKind kind = KindOf<T>();
            const TypeTreeNode& n = tree_.nodes[node];
            if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                At(node, offset, raw);
                value = T(raw);
            }
            else if constexpr (IsNumeric(kind))
            {
                if (IsNumeric(n.kind))
                    Number(n.kind, offset, value);
            }
            else if constexpr (kind == ValueKind::String)
            {
                u32 count;
                if (n.kind == ValueKind::String && Count(offset, count, 1))
                    value = StringView(reinterpret_cast<const char*>(data_.Data() + offset + 4), count);
            }
            else if constexpr (kind == ValueKind::Guid)
            {
                if (n.kind == ValueKind::Guid && Check(offset, 16))
                    std::memcpy(&value, data_.Data() + offset, 16);
            }
            else if constexpr (kind == ValueKind::Array)
            {
                using Item = typename detail::IsArray<T>::Item;
                u32 item = node + 1;
                u32 count;
                if (n.kind != ValueKind::Array || !Count(offset, count, tree_.nodes[item].byteSize > 0 ? usize(tree_.nodes[item].byteSize) : 1))
                    return;
                value.Resize(count);
                usize at = offset + 4;
                if constexpr ((std::is_arithmetic_v<Item> && !std::is_same_v<Item, bool>) || std::is_enum_v<Item>)
                {
                    if (tree_.nodes[item].kind == KindOf<Item>())
                    {
                        std::memcpy(value.Data(), data_.Data() + at, usize(count) * sizeof(Item));
                        return;
                    }
                }
                for (u32 i = 0; i < count && !failed_; i++)
                {
                    At(item, at, value[i]);
                    at = Skip(item, at);
                }
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                if (n.kind != ValueKind::FixedArray)
                    return;
                u32 item = node + 1;
                usize at = offset;
                for (u32 i = 0; i < n.count && i < std::extent_v<T> && !failed_; i++)
                {
                    At(item, at, value[i]);
                    at = Skip(item, at);
                }
            }
            else
            {
                if (n.kind != ValueKind::Struct)
                    return;
                u32 begin = u32(children_.Size());
                usize at = offset;
                for (u32 c = node + 1; c < tree_.nodes.Size() && tree_.nodes[c].level > n.level && !failed_; c = tree_.Next(c))
                {
                    children_.Push({c, at});
                    at = Skip(c, at);
                }
                if (failed_)
                    return;
                frames_.Push({begin, u32(children_.Size()), 0});
                i32 saved = version_;
                version_ = n.version;
                value.Transfer(*this);
                version_ = saved;
                frames_.Pop();
                children_.Resize(begin);
            }
        }

        // The offset after the value of `node` at `offset`; on damaged data Failed() is set.
        usize Skip(u32 node, usize offset);

    private:
        struct Child
        {
            u32 node;
            usize offset;
        };
        struct Frame
        {
            u32 begin, end, cursor;
        };

        bool Fail(StringView what, usize offset);
        bool Check(usize offset, usize size)
        {
            if (offset > data_.Size() || data_.Size() - offset < size)
                return Fail("the data ends", offset);
            return true;
        }
        bool Count(usize offset, u32& count, usize itemBytes)
        {
            if (!Check(offset, 4))
                return false;
            std::memcpy(&count, data_.Data() + offset, 4);
            if (u64(count) * itemBytes > data_.Size() - offset - 4)
                return Fail(Format("a count of {}", count), offset);
            return true;
        }
        template <class T>
        void Number(ValueKind stored, usize offset, T& value);

        const TypeTree& tree_;
        Span<const u8> data_;
        ReferenceTable* references_ = nullptr;
        Array<Child> children_;
        Array<Frame> frames_;
        i32 version_ = 1;
        bool failed_ = false;
        String error_;
    };

    template <class T>
    void SafeBinaryRead::Number(ValueKind stored, usize offset, T& value)
    {
        u64 bits = 0;
        static constexpr u8 kSizes[] = {1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8};
        usize size = kSizes[u32(stored)];
        if (!Check(offset, size))
            return;
        std::memcpy(&bits, data_.Data() + offset, size);
        // The stored value as one of three: signed, unsigned or floating.
        i64 s = 0;
        u64 u = 0;
        double d = 0;
        int form = 0;
        switch (stored)
        {
        case ValueKind::Bool: u = bits != 0, form = 1; break;
        case ValueKind::Int8: s = i8(bits); break;
        case ValueKind::Int16: s = i16(bits); break;
        case ValueKind::Int32: s = i32(bits); break;
        case ValueKind::Int64: s = i64(bits); break;
        case ValueKind::UInt8:
        case ValueKind::UInt16:
        case ValueKind::UInt32:
        case ValueKind::UInt64: u = bits, form = 1; break;
        case ValueKind::Float:
        {
            float f;
            u32 b = u32(bits);
            std::memcpy(&f, &b, 4);
            d = f, form = 2;
            break;
        }
        default: std::memcpy(&d, &bits, 8), form = 2; break;
        }
        if constexpr (std::is_same_v<T, bool>)
            value = form == 0 ? s != 0 : form == 1 ? u != 0 : d != 0;
        else if constexpr (std::is_floating_point_v<T>)
            value = form == 0 ? T(s) : form == 1 ? T(u) : T(d);
        else
        {
            using L = std::numeric_limits<T>;
            if (form == 0)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    if (s >= i64(L::min()) && s <= i64(L::max()))
                        value = T(s);
                }
                else if (s >= 0 && u64(s) <= u64(L::max()))
                    value = T(s);
            }
            else if (form == 1)
            {
                if (u <= u64(L::max()))
                    value = T(u);
            }
            else if (d == d && d >= double(L::min()) && d < double(L::max()) + 1.0)
                value = T(d);
        }
    }

    template <class T>
    void WriteBinary(T& object, Array<u8>& out, ReferenceTable* references = nullptr)
    {
        BinaryWrite w(out, references);
        w.Value(object);
    }

    // Reads the binary form of T; the data must end with the object.
    template <class T>
    bool ReadBinary(Span<const u8> data, T& object, String& error, ReferenceTable* references = nullptr)
    {
        BinaryRead r(data, references);
        r.Value(object);
        if (!r.Failed() && r.Position() != data.Size())
            return error = Format("{} bytes after the object", data.Size() - r.Position()), false;
        if (r.Failed())
            error = r.Error();
        return !r.Failed();
    }

    // Reads binary data written with `tree` into T.
    template <class T>
    bool ReadBinary(const TypeTree& tree, Span<const u8> data, T& object, String& error, ReferenceTable* references = nullptr)
    {
        SafeBinaryRead r(tree, data, references);
        r.At(0, 0, object);
        if (!r.Failed() && r.Skip(0, 0) != data.Size() && !r.Failed())
            return error = "the data does not end with the object", false;
        if (r.Failed())
            error = r.Error();
        return !r.Failed();
    }
}
