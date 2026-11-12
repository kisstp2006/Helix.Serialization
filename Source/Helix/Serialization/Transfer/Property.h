#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Serialization/Transfer/Traits.h>
#include <Helix/Serialization/Yaml/Tree.h>

#include <charconv>
#include <cmath>
#include <limits>

// Property modifications (prefab overrides): one field of an object, named by a path of its serialized field names
// joined by dots ("m_LocalPosition.x", "m_Name"), arrays as "list.Array.size" and "list.Array.data[3]" (then ".x" for
// a field of the item). The value is text, as a scene file writes it (numbers, 0 or 1 for bools, strings, guids), or an
// object reference turned into the caller's object value by the ReferenceTable.
namespace helix::serialization
{
    class PropertyWrite
    {
    public:
        static constexpr bool kReading = true;
        static constexpr bool kText = true;
        // One field of an object is read, not the whole of it: types skip what they do only for whole reads.
        static constexpr bool kPartial = true;
        // Item counts a modification may set.
        static constexpr usize kMaxArraySize = usize(1) << 24;

        PropertyWrite(StringView path, StringView value, const AssetReference& reference, ReferenceTable* references)
            : rest_(path), value_(value), reference_(reference), references_(references)
        {
        }

        template <class T>
        void Field(StringView name, T& value)
        {
            if (done_)
                return;
            StringView r = rest_;
            if (!r.StartsWith(name) || (r.Size() != name.Size() && r[name.Size()] != '.'))
                return;
            rest_ = r.Size() == name.Size() ? StringView() : r.Substr(name.Size() + 1);
            Value(value);
            rest_ = r;
        }

        i32 Version() const { return version_; }

        void ObjectReference(u64& object)
        {
            if (done_ || !rest_.Empty())
                return;
            object = references_ ? references_->FromFile(reference_) : u64(reference_.fileID);
            done_ = true;
        }

        template <class T>
        void Value(T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                Value(raw);
                value = T(raw);
            }
            else if constexpr (std::is_same_v<T, AssetReference>)
            {
                if (rest_.Empty())
                    value = reference_, done_ = true;
                else
                    Fail("the path goes below a reference");
            }
            else if constexpr (kind == ValueKind::Struct)
            {
                i32 saved = version_;
                version_ = VersionOf(value);
                TransferStruct(*this, value);
                version_ = saved;
                if (!done_ && rest_.Empty())
                    Fail(Format("the path names a {}, not a value", TypeNameOf(value)));
            }
            else if constexpr (kind == ValueKind::Array)
            {
                if (rest_ == StringView("Array.size"))
                {
                    i64 n = 0;
                    if (!yaml::ParseInt(value_, n) || n < 0 || u64(n) > kMaxArraySize)
                        return Fail(Format("'{}' is not an item count", value_));
                    value.Resize(usize(n));
                    done_ = true;
                }
                else if (usize i; Item(i))
                {
                    if (i >= value.Size())
                        return Fail(Format("item {} of {}", i, value.Size()));
                    Value(value[i]);
                }
                else
                    Fail("an array path is Array.size or Array.data[i]");
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                if (usize i; Item(i))
                {
                    if (i >= std::extent_v<T>)
                        return Fail(Format("item {} of {}", i, std::extent_v<T>));
                    Value(value[i]);
                }
                else
                    Fail("an array path is Array.data[i]");
            }
            else if (!rest_.Empty())
                Fail("the path goes below a value");
            else
                Leaf(value);
        }

        bool Done() const { return done_; }
        const String& Error() const { return error_; }

    private:
        // "Array.data[i]" with what follows it.
        bool Item(usize& index)
        {
            static constexpr StringView kPrefix = "Array.data[";
            if (!rest_.StartsWith(kPrefix))
                return false;
            usize close = rest_.Find(']');
            if (close == StringView::npos)
                return false;
            StringView digits = rest_.Substr(kPrefix.Size(), close - kPrefix.Size());
            auto r = std::from_chars(digits.Data(), digits.Data() + digits.Size(), index);
            if (r.ec != std::errc() || r.ptr != digits.Data() + digits.Size() || digits.Empty())
                return false;
            StringView after = rest_.Substr(close + 1);
            if (!after.Empty() && after[0] != '.')
                return false;
            rest_ = after.Empty() ? StringView() : after.Substr(1);
            return true;
        }

        template <class T>
        void Leaf(T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (kind == ValueKind::Bool)
            {
                i64 v = 0;
                if (!yaml::ParseInt(value_, v) || (v != 0 && v != 1))
                    return Fail(Format("'{}' is not 0 or 1", value_));
                value = v != 0;
            }
            else if constexpr (kind == ValueKind::UInt64)
            {
                u64 v = 0;
                auto r = std::from_chars(value_.Data(), value_.Data() + value_.Size(), v);
                if (r.ec != std::errc() || r.ptr != value_.Data() + value_.Size() || value_.Empty())
                    return Fail(Format("'{}' is not an unsigned integer", value_));
                value = v;
            }
            else if constexpr (IsInteger(kind))
            {
                i64 v = 0;
                double d = 0;
                if (!yaml::ParseInt(value_, v))
                {
                    // Whole numbers written as floats.
                    if (!yaml::ParseFloat(value_, d) || d != std::floor(d) || d < -9.2233720368547758e18 || d >= 9.2233720368547758e18)
                        return Fail(Format("'{}' is not an integer", value_));
                    v = i64(d);
                }
                using L = std::numeric_limits<T>;
                if (v < i64(L::min()) || (v > 0 && u64(v) > u64(L::max())))
                    return Fail(Format("{} does not fit", v));
                value = T(v);
            }
            else if constexpr (kind == ValueKind::Float || kind == ValueKind::Double)
            {
                double d = 0;
                if (!yaml::ParseFloat(value_, d))
                    return Fail(Format("'{}' is not a number", value_));
                value = T(d);
            }
            else if constexpr (kind == ValueKind::String)
                value = String(value_);
            else if constexpr (kind == ValueKind::Guid)
            {
                Guid g;
                if (!value_.Empty() && !Guid::TryParse(value_, g))
                    return Fail(Format("'{}' is not a guid", value_));
                value = g;
            }
            done_ = true;
        }

        void Fail(String message)
        {
            error_ = Move(message);
            done_ = true;
        }

        StringView rest_;
        StringView value_;
        AssetReference reference_;
        ReferenceTable* references_;
        i32 version_ = 1;
        bool done_ = false;
        String error_;
    };

    // One field of an object as text, by its property path: the partner of PropertyWrite (numbers, 0 or 1 for bools,
    // strings, guids, enums as their integers, "Array.size" as the item count). Structs, arrays and references are
    // read through their own fields, not as one value.
    class PropertyRead
    {
    public:
        static constexpr bool kReading = false;
        static constexpr bool kText = true;
        static constexpr bool kPartial = true;

        explicit PropertyRead(StringView path) : rest_(path) {}

        template <class T>
        void Field(StringView name, T& value)
        {
            if (done_)
                return;
            StringView r = rest_;
            if (!r.StartsWith(name) || (r.Size() != name.Size() && r[name.Size()] != '.'))
                return;
            rest_ = r.Size() == name.Size() ? StringView() : r.Substr(name.Size() + 1);
            Value(value);
            rest_ = r;
        }

        i32 Version() const { return version_; }

        void ObjectReference(u64&)
        {
            if (!done_ && rest_.Empty())
                Fail("a reference is not read as text");
        }

        template <class T>
        void Value(T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                Value(raw);
            }
            else if constexpr (std::is_same_v<T, AssetReference>)
                Fail("a reference is not read as text");
            else if constexpr (kind == ValueKind::Struct)
            {
                i32 saved = version_;
                version_ = VersionOf(value);
                TransferStruct(*this, value);
                version_ = saved;
                if (!done_ && rest_.Empty())
                    Fail(Format("the path names a {}, not a value", TypeNameOf(value)));
            }
            else if constexpr (kind == ValueKind::Array)
            {
                if (rest_ == StringView("Array.size"))
                {
                    text_ = Format("{}", value.Size());
                    done_ = true;
                }
                else if (usize i; Item(i))
                {
                    if (i >= value.Size())
                        return Fail(Format("item {} of {}", i, value.Size()));
                    Value(value[i]);
                }
                else
                    Fail("an array path is Array.size or Array.data[i]");
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                if (usize i; Item(i))
                {
                    if (i >= std::extent_v<T>)
                        return Fail(Format("item {} of {}", i, std::extent_v<T>));
                    Value(value[i]);
                }
                else
                    Fail("an array path is Array.data[i]");
            }
            else if (!rest_.Empty())
                Fail("the path goes below a value");
            else
            {
                if constexpr (kind == ValueKind::Bool)
                    text_ = String(value ? "1" : "0");
                else if constexpr (kind == ValueKind::String)
                    text_ = String(value);
                else if constexpr (kind == ValueKind::Guid)
                    text_ = value.ToString();
                else if constexpr (kind == ValueKind::Float || kind == ValueKind::Double)
                    text_ = Format("{}", value);
                else
                    text_ = Format("{}", value);
                done_ = true;
            }
        }

        bool Done() const { return done_; }
        const String& Error() const { return error_; }
        const String& Text() const { return text_; }

    private:
        bool Item(usize& index)
        {
            static constexpr StringView kPrefix = "Array.data[";
            if (!rest_.StartsWith(kPrefix))
                return false;
            usize close = rest_.Find(']');
            if (close == StringView::npos)
                return false;
            StringView digits = rest_.Substr(kPrefix.Size(), close - kPrefix.Size());
            auto r = std::from_chars(digits.Data(), digits.Data() + digits.Size(), index);
            if (r.ec != std::errc() || r.ptr != digits.Data() + digits.Size() || digits.Empty())
                return false;
            StringView after = rest_.Substr(close + 1);
            if (!after.Empty() && after[0] != '.')
                return false;
            rest_ = after.Empty() ? StringView() : after.Substr(1);
            return true;
        }

        void Fail(String message)
        {
            error_ = Move(message);
            done_ = true;
        }

        StringView rest_;
        i32 version_ = 1;
        bool done_ = false;
        String text_;
        String error_;
    };

    // Reads one field as text; false with `error` when the path names no value of T.
    template <class T>
    bool ReadProperty(T& object, StringView path, String& text, String& error)
    {
        PropertyRead r(path);
        r.Value(object);
        if (!r.Done())
            return error = Format("{} has no field {}", TypeNameOf<T>(), path), false;
        if (!r.Error().Empty())
            return error = Format("{}: {}", path, r.Error()), false;
        text = r.Text();
        return true;
    }

    // Applies one modification; false with `error` when the path names no field of T or the value does not fit it.
    template <class T>
    bool ApplyProperty(T& object, StringView path, StringView value, const AssetReference& reference, ReferenceTable* references, String& error)
    {
        PropertyWrite w(path, value, reference, references);
        w.Value(object);
        if (!w.Done())
            return error = Format("{} has no field {}", TypeNameOf<T>(), path), false;
        if (!w.Error().Empty())
            return error = Format("{}: {}", path, w.Error()), false;
        return true;
    }
}
