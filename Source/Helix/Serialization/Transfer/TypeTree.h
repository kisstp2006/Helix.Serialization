#pragma once

#include <Helix/Core/Basics/Span.h>
#include <Helix/Core/Hashing/Md5.h>
#include <Helix/Serialization/Transfer/Traits.h>

namespace helix::serialization
{
    // 24 bytes. The nodes of a tree are in depth first order; the children of a node are the nodes after it one level
    // deeper, up to the next node at its own level or above. An Array or FixedArray node has one child, its item, named
    // "data"; a Struct node has its fields.
    struct TypeTreeNode
    {
        ValueKind kind = ValueKind::Struct;
        u8 level = 0;
        // kFlow.
        u8 flags = 0;
        u8 reserved = 0;
        i32 version = 1;
        // Bytes in binary form, -1 when it depends on the data (strings, arrays, anything holding them).
        i32 byteSize = -1;
        // The items of a FixedArray.
        u32 count = 0;
        // Offsets into the string table.
        u32 typeName = 0;
        u32 fieldName = 0;

        static constexpr u8 kFlow = 1;
    };

    // The layout of a type's binary form: written into serialized files so that data written by an older version of a
    // type can still be read (SafeBinaryRead), and hashed as the type's identity.
    class TypeTree
    {
    public:
        Array<TypeTreeNode> nodes;
        // Names, each ended by a zero.
        Array<char> strings;

        StringView TypeName(u32 node) const { return Str(nodes[node].typeName); }
        StringView FieldName(u32 node) const { return Str(nodes[node].fieldName); }
        // The node after the subtree of `node`.
        u32 Next(u32 node) const;
        // The offset of `text` in the string table, added when missing.
        u32 AddString(StringView text);
        // Byte sizes from the leaves up.
        void ComputeSizes();

        // MD5 of the binary form.
        Md5Digest Hash() const;
        // One line per node, indented by level: "  m_LocalPosition Vector3 (12) flow".
        String Dump() const;

        // u32 node count, u32 string bytes, the nodes, the strings.
        void Write(Array<u8>& out) const;
        // Reads and checks the binary form: kinds, levels, one item per array, primitives without children, names inside
        // the string table. Sizes are computed again, not taken from the bytes.
        static bool Read(Span<const u8> bytes, TypeTree& out, String& error);

    private:
        StringView Str(u32 offset) const;
    };

    // Builds the type tree of a type from a default instance.
    class TypeTreeBuild
    {
    public:
        static constexpr bool kReading = false;
        static constexpr bool kText = false;

        explicit TypeTreeBuild(TypeTree& tree) : tree_(tree) {}

        template <class T>
        void Field(StringView name, T& value)
        {
            Node(name, value);
        }

        i32 Version() const { return version_; }
        // A reference is the fields of an AssetReference.
        void ObjectReference(u64& object) { TransferObjectReference(*this, nullptr, object); }

        template <class T>
        void Node(StringView name, T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            TypeTreeNode n;
            n.kind = kind;
            n.level = level_;
            n.fieldName = tree_.AddString(name);
            if constexpr (kind == ValueKind::Struct)
            {
                n.typeName = tree_.AddString(TypeNameOf<T>());
                n.version = VersionOf<T>();
                n.flags = FlowOf<T>() ? TypeTreeNode::kFlow : 0;
            }
            else
                n.typeName = tree_.AddString(KindName(kind));
            if constexpr (kind == ValueKind::FixedArray)
                n.count = u32(std::extent_v<T>);
            tree_.nodes.Push(n);
            level_++;
            if constexpr (kind == ValueKind::Array)
            {
                auto item = typename detail::IsArray<T>::Item();
                Node("data", item);
            }
            else if constexpr (kind == ValueKind::FixedArray)
                Node("data", value[0]);
            else if constexpr (kind == ValueKind::Struct)
            {
                i32 saved = version_;
                version_ = n.version;
                value.Transfer(*this);
                version_ = saved;
            }
            level_--;
        }

    private:
        TypeTree& tree_;
        u8 level_ = 0;
        i32 version_ = 1;
    };

    // The type tree of T, built once.
    template <class T>
    const TypeTree& TypeTreeOf()
    {
        static const TypeTree tree = [] {
            TypeTree t;
            T instance = T();
            TypeTreeBuild b(t);
            b.Node("Base", instance);
            t.ComputeSizes();
            return t;
        }();
        return tree;
    }

    // The hash of TypeTreeOf<T>(), computed once.
    template <class T>
    const Md5Digest& TypeHashOf()
    {
        static const Md5Digest hash = TypeTreeOf<T>().Hash();
        return hash;
    }
}
