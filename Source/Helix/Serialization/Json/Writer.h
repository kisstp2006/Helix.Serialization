#pragma once

#include <Helix/Core/Strings/String.h>
#include <Helix/Serialization/Json/Tree.h>

namespace helix::serialization::json
{
    // Writes JSON as a stream of calls:
    //
    //   Writer w(out, true);
    //   w.BeginObject();
    //   w.Key("name");
    //   w.String("Cube");
    //   w.Key("size");
    //   w.Double(1.5);
    //   w.EndObject();
    //
    // Compact output has no spaces; pretty output puts every entry on its own line, indented by four spaces, with
    // "key": value. Empty containers are {} and []. Strings escape '"', '\\' and the control characters; other text is
    // written as it is (UTF-8). Floats are the shortest text that reads back as the same value; NaN and the infinities,
    // which JSON has no number for, are written as the strings "NaN", "Infinity" and "-Infinity".
    class Writer
    {
    public:
        explicit Writer(helix::String& out, bool pretty = false) : out_(out), pretty_(pretty) {}

        void BeginObject();
        void EndObject();
        void BeginArray();
        void EndArray();
        void Key(StringView key);
        void String(StringView text);
        void Bool(bool value);
        void Null();
        void Int(i64 value);
        void UInt(u64 value);
        void Double(double value);
        void Float(float value);
        // A number as text, written unchanged (as a parsed tree keeps it); the text must follow the number grammar.
        void Number(StringView text);

    private:
        void BeforeValue();
        void Begin(char open);
        void End(char close);
        void Quoted(StringView text);
        void Line();

        helix::String& out_;
        bool pretty_;
        // Entries written so far in each open container.
        Array<u32> counts_;
        bool keyPending_ = false;
    };

    // The tree as text, compact or pretty.
    void Write(const Tree& tree, helix::String& out, bool pretty = false);
}
