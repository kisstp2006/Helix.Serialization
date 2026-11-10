#pragma once

#include <Helix/Core/Containers/Array.h>
#include <Helix/Core/Strings/StringView.h>

namespace helix::serialization::yaml
{
    enum class NodeKind : u8
    {
        Scalar,
        Mapping,
        Sequence,
    };

    enum class ScalarStyle : u8
    {
        Plain,
        SingleQuoted,
        DoubleQuoted,
    };

    inline constexpr u32 kNoNode = ~0u;

    // 24 bytes: a scalar's text (a view into the parsed text or the tree's own buffer) or a container's range of entries in
    // Tree::children (a mapping has key, value pairs: 2 * Count() entries).
    struct Node
    {
        NodeKind kind = NodeKind::Scalar;
        ScalarStyle style = ScalarStyle::Plain;
        // Written inline as {a: 1} or [1, 2].
        bool flow = false;
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
        // The size of a scalar's text, the first entry of a container.
        u32 a_ = 0;
        u32 count_ = 0;
    };

    // A line of the parsed text: where it starts and ends (without the line break) and how far its content is indented.
    struct Line
    {
        u32 start = 0;
        u32 end = 0;
        u16 indent = 0;
        // Only spaces, or a comment.
        bool blank = false;
    };

    // One document of a stream: --- !h!<classID> &<fileID>, optionally stripped.
    struct Document
    {
        i64 classID = -1;
        i64 fileID = 0;
        bool stripped = false;
        u32 root = kNoNode;
        u32 line = 0;
    };

    // The parsed form of a YAML stream, all nodes in one array. The parsed text must outlive the tree; it cannot be copied
    // (scalars point into it and into its own buffer), only moved.
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
        Array<Document> documents;
        // The text after % of each directive line, as "YAML 1.1" or "TAG !h! tag:helixengine,2026:".
        Array<StringView> directives;
        // The lines of the parsed text; kept with the tree so that parsing into it again reuses the memory.
        Array<Line> lines;

        void Clear();

        const Node& operator[](u32 node) const { return nodes[node]; }
        u32 Count(u32 node) const { return nodes[node].Count(); }
        // Mappings.
        u32 Key(u32 mapping, u32 i) const { return children[nodes[mapping].First() + 2 * i]; }
        u32 Value(u32 mapping, u32 i) const { return children[nodes[mapping].First() + 2 * i + 1]; }
        // The value of a key, kNoNode when the mapping has none (or the node is not a mapping).
        u32 Find(u32 mapping, StringView key) const;
        // Sequences.
        u32 Item(u32 sequence, u32 i) const { return children[nodes[sequence].First() + i]; }

        StringView Text(u32 node) const { return nodes[node].Text(); }
        bool IsScalar(u32 node) const { return node != kNoNode && nodes[node].kind == NodeKind::Scalar; }
        bool IsMapping(u32 node) const { return node != kNoNode && nodes[node].kind == NodeKind::Mapping; }
        bool IsSequence(u32 node) const { return node != kNoNode && nodes[node].kind == NodeKind::Sequence; }
        // Numbers from plain scalars: decimal or 0x hexadecimal integers, floats with an exponent, inf and nan.
        bool AsInt(u32 node, i64& out) const;
        bool AsFloat(u32 node, double& out) const;

        // A copy of `text` that lives as long as the tree (for scalars the parser had to change).
        StringView Store(StringView text);

        // Building: a node, and the children of a container from a list of node indices (keys and values interleaved).
        u32 Add(const Node& node);
        void SetChildren(u32 container, const u32* entries, u32 count);

    private:
        static constexpr usize kBlock = 64 * 1024;
        Array<Array<char>> buffers_;
    };
}
