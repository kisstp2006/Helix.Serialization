#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/String.h>
#include <Helix/Core/Types/Guid.h>

#include <type_traits>

// A type serializes through one member function that names its fields in order:
//
//   struct Transform
//   {
//       static constexpr StringView kTypeName = "Transform";
//       static constexpr i32 kVersion = 2;          // optional, 1 when missing
//       static constexpr bool kFlow = false;        // optional: written as {x: 0, y: 0} in YAML
//
//       Vector3 m_LocalPosition;
//       Array<i64> m_Children;
//
//       template <class V>
//       void Transfer(V& v)
//       {
//           v.Field("m_LocalPosition", m_LocalPosition);
//           v.Field("m_Children", m_Children);
//       }
//   };
//
// The same function drives every visitor: YAML write and read, binary write and read, reading through an older type
// tree, and building the type tree. A visitor tells what it does with V::kReading and V::kText, and v.Version() gives
// the version of the data being read (the type's own version when writing), for upgrading older data.
namespace helix::serialization
{
    // The kinds of values a field can hold.
    enum class ValueKind : u8
    {
        Bool,
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        String,
        Guid,
        // Array<T>: a count and the items.
        Array,
        // T[N]: N items, no count.
        FixedArray,
        // A type with Transfer.
        Struct,
    };

    inline constexpr u32 kValueKindCount = u32(ValueKind::Struct) + 1;

    // A reference to an object in the same file (guid unset) or in another asset: {fileID: 0} when empty,
    // {fileID: 11500000, guid: ..., type: 3} when external.
    struct AssetReference
    {
        static constexpr StringView kTypeName = "AssetReference";
        static constexpr bool kFlow = true;

        i64 fileID = 0;
        Guid guid;
        i32 type = 0;

        bool operator==(const AssetReference&) const = default;

        template <class V>
        void Transfer(V& v)
        {
            if constexpr (V::kReading)
                *this = AssetReference();
            v.Field("fileID", fileID);
            // Text writes the guid and type only for external references.
            bool external = true;
            if constexpr (V::kText && !V::kReading)
                external = guid.IsValid();
            if (external)
            {
                v.Field("guid", guid);
                v.Field("type", type);
            }
        }
    };

    // Turns object references into file references and back (an engine's object table). Every visitor has
    // ObjectReference(u64&), which a reference type calls from its Transfer: the reference then reads and writes as the
    // fields of an AssetReference ({fileID: N} or {fileID, guid, type}).
    class ReferenceTable
    {
    public:
        virtual ~ReferenceTable() = default;
        virtual AssetReference ToFile(u64 object) = 0;
        virtual u64 FromFile(const AssetReference& reference) = 0;
    };

    template <class V>
    void TransferObjectReference(V& v, ReferenceTable* table, u64& object)
    {
        AssetReference r;
        if constexpr (!V::kReading)
        {
            if (table)
                r = table->ToFile(object);
            else
                r.fileID = i64(object);
        }
        r.Transfer(v);
        if constexpr (V::kReading)
            object = table ? table->FromFile(r) : u64(r.fileID);
    }

    namespace detail
    {
        // Lets the traits ask whether a type has Transfer without a real visitor.
        struct TransferProbe
        {
            static constexpr bool kReading = false;
            static constexpr bool kText = false;
            template <class T>
            void Field(StringView, T&);
            i32 Version() const;
        };

        template <class T>
        struct IsArray : std::false_type
        {
        };
        template <class T>
        struct IsArray<Array<T>> : std::true_type
        {
            using Item = T;
        };
    }

    template <class T>
    concept Transferable = requires(T& t, detail::TransferProbe& v) { t.Transfer(v); };

    template <class T>
    constexpr ValueKind KindOf()
    {
        if constexpr (std::is_enum_v<T>)
            return KindOf<std::underlying_type_t<T>>();
        else if constexpr (std::is_same_v<T, bool>)
            return ValueKind::Bool;
        else if constexpr (std::is_integral_v<T>)
        {
            constexpr bool s = std::is_signed_v<T>;
            if constexpr (sizeof(T) == 1)
                return s ? ValueKind::Int8 : ValueKind::UInt8;
            else if constexpr (sizeof(T) == 2)
                return s ? ValueKind::Int16 : ValueKind::UInt16;
            else if constexpr (sizeof(T) == 4)
                return s ? ValueKind::Int32 : ValueKind::UInt32;
            else
            {
                static_assert(sizeof(T) == 8);
                return s ? ValueKind::Int64 : ValueKind::UInt64;
            }
        }
        else if constexpr (std::is_same_v<T, float>)
            return ValueKind::Float;
        else if constexpr (std::is_same_v<T, double>)
            return ValueKind::Double;
        else if constexpr (std::is_same_v<T, String>)
            return ValueKind::String;
        else if constexpr (std::is_same_v<T, Guid>)
            return ValueKind::Guid;
        else if constexpr (detail::IsArray<T>::value)
            return ValueKind::Array;
        else if constexpr (std::is_array_v<T>)
            return ValueKind::FixedArray;
        else
        {
            static_assert(Transferable<T>, "a field must be a number, bool, enum, String, Guid, Array, fixed array or a type with Transfer");
            return ValueKind::Struct;
        }
    }

    constexpr bool IsNumeric(ValueKind k) { return k <= ValueKind::Double; }
    constexpr bool IsInteger(ValueKind k) { return k >= ValueKind::Int8 && k <= ValueKind::UInt64; }

    // The name type trees use for a kind.
    StringView KindName(ValueKind kind);

    template <class T>
    constexpr i32 VersionOf()
    {
        if constexpr (requires { T::kVersion; })
            return T::kVersion;
        else
            return 1;
    }

    template <class T>
    constexpr bool FlowOf()
    {
        if constexpr (requires { T::kFlow; })
            return T::kFlow;
        else
            return false;
    }

    template <class T>
    constexpr StringView TypeNameOf()
    {
        static_assert(requires { T::kTypeName; }, "a type with Transfer names itself with static constexpr StringView kTypeName");
        return T::kTypeName;
    }
}
