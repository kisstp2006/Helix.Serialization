#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/StringView.h>

namespace helix::serialization::json
{
    enum class NodeKind : u8
    {
        Null,
        False,
        True,
        Number,
        String,
        Object,
        Array,
    };

    inline constexpr u32 kNoNode = ~0u;

    // 24 bytes: the text of a number or string (a view into the parsed text or the tree's own buffer) or a container's
    // range of entries in Tree::children (an object has key, value pairs: 2 * Count() entries; keys are string nodes).
    struct Node
    {
        NodeKind kind = NodeKind::Null;
        // The line the value starts on, from 1.
        u32 line = 0;

        StringView Text() const { return StringView(data_, a_); }
        u32 First() const { return a_; }
        u32 Count() const { return count_; }
        void SetText(StringView text)
        {
            data_ = text.Data();
            a_ = u32(text.Size());
        }
        void SetRange(u32 first, u32 count)
        {
            a_ = first;
            count_ = count;
        }

    private:
        const char* data_ = nullptr;
        // The size of a text, the first entry of a container.
        u32 a_ = 0;
        u32 count_ = 0;
    };

    // The parsed form of a JSON text, all nodes in one array. The parsed text must outlive the tree; it cannot be copied
    // (nodes point into it and into its own buffer), only moved.
    class Tree
    {
    public:
        Tree() = default;
        Tree(const Tree&) = delete;
        Tree& operator=(const Tree&) = delete;
        Tree(Tree&&) = default;
        Tree& operator=(Tree&&) = default;

        Array<Node> nodes;
        Array<u32> children;
        u32 root = kNoNode;

        void Clear();

        const Node& operator[](u32 node) const { return nodes[node]; }
        u32 Count(u32 node) const { return nodes[node].Count(); }
        // Objects.
        u32 Key(u32 object, u32 i) const { return children[nodes[object].First() + 2 * i]; }
        u32 Value(u32 object, u32 i) const { return children[nodes[object].First() + 2 * i + 1]; }
        // The value of the first entry with this key, kNoNode when there is none (or the node is not an object).
        u32 Find(u32 object, StringView key) const;
        // Arrays.
        u32 Item(u32 array, u32 i) const { return children[nodes[array].First() + i]; }

        StringView Text(u32 node) const { return nodes[node].Text(); }
        NodeKind Kind(u32 node) const { return nodes[node].kind; }
        bool Is(u32 node, NodeKind kind) const { return node != kNoNode && nodes[node].kind == kind; }
        bool IsObject(u32 node) const { return Is(node, NodeKind::Object); }
        bool IsArray(u32 node) const { return Is(node, NodeKind::Array); }
        bool IsString(u32 node) const { return Is(node, NodeKind::String); }
        bool IsNumber(u32 node) const { return Is(node, NodeKind::Number); }

        bool AsBool(u32 node, bool& out) const;
        // Numbers with an integer value that fits (1e3 and 2.0 too).
        bool AsInt(u32 node, i64& out) const;
        bool AsUInt(u32 node, u64& out) const;
        // Numbers, and the strings "NaN", "Infinity" and "-Infinity" the writer uses for values JSON has no number for.
        bool AsDouble(u32 node, double& out) const;

        // A copy of `text` that lives as long as the tree (for strings the parser had to unescape).
        StringView Store(StringView text);

        // Building: a node, and the children of a container from a list of node indices (keys and values interleaved).
        u32 Add(const Node& node);
        void SetChildren(u32 container, const u32* entries, u32 count);

    private:
        static constexpr usize kBlock = 64 * 1024;
        Array<Array<char>> buffers_;
    };
}
