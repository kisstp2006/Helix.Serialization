#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Serialization/Yaml/Tree.h>

namespace helix::serialization::yaml
{
    // Writes the Helix YAML dialect as a stream of calls:
    //
    //   Emitter e(out);
    //   e.Directives();
    //   e.BeginDocument(1, 123);
    //   e.BeginMapping();
    //   e.Key("GameObject");
    //   e.BeginMapping();
    //   e.Key("m_Name");
    //   e.Scalar("Cube");
    //   e.Key("m_Component");
    //   e.BeginSequence();
    //   ...
    //
    // Block mappings indent their entries by two spaces, block sequences stand at the indentation of their key and a
    // mapping in a sequence item starts on the item's line. Empty block containers are written as {} and []. Scalars are
    // plain unless they would read back as something else; then single quoted, or double quoted when they hold line breaks
    // or control characters.
    class Emitter
    {
    public:
        explicit Emitter(String& out) : out_(out) {}

        // %YAML 1.1 and %TAG !h! tag:helixengine,2026:
        void Directives();
        void Directive(StringView text);
        void BeginDocument(i64 classID = -1, i64 fileID = 0, bool stripped = false);

        void BeginMapping(bool flow = false);
        void EndMapping();
        void BeginSequence(bool flow = false);
        void EndSequence();
        void Key(StringView key);
        void Scalar(StringView text);
        void Scalar(StringView text, ScalarStyle style);
        void Int(i64 value);
        void UInt(u64 value);
        // The shortest text that reads back as the same double (or float, for the float overload).
        void Float(double value);
        void Float(float value);

        // Whether a plain scalar with this text would read back unchanged.
        static bool PlainIsSafe(StringView text);

    private:
        struct Level
        {
            bool mapping;
            bool flow;
            u32 indent;
            u32 count;
            // A block container whose first line has not been written (it may still be empty).
            bool open;
        };

        void BeforeValue();
        void BeginContainer(bool mapping, bool flow);
        void EndContainer();
        void Text(StringView text, ScalarStyle style);
        void Indent(u32 spaces) { out_.Append(' ', spaces); }

        String& out_;
        Array<Level> levels_;
        bool keyPending_ = false;
        bool inDocument_ = false;
    };

    // The tree as text: its directives, documents and nodes, flow containers as flow, scalars in their style where the
    // emitter allows it.
    void Write(const Tree& tree, String& out);
}
