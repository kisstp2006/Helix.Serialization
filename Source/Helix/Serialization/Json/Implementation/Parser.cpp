#include <Helix/Serialization/Json/Parser.h>

#include <Helix/Core/Strings/Format.h>

namespace helix::serialization::json
{
    namespace
    {
        // Characters a string can hold without a closer look: everything but '"', '\\', controls and non-ASCII bytes.
        struct Plain
        {
            bool table[256];
            constexpr Plain() : table()
            {
                for (int c = 0x20; c < 0x80; c++)
                    table[c] = c != '"' && c != '\\';
            }
        };
        constexpr Plain kPlain;

        bool IsDigit(char c) { return c >= '0' && c <= '9'; }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        void AppendUtf8(String& out, u32 cp)
        {
            if (cp < 0x80)
                out.Append(char(cp));
            else if (cp < 0x800)
            {
                out.Append(char(0xC0 | (cp >> 6)));
                out.Append(char(0x80 | (cp & 63)));
            }
            else if (cp < 0x10000)
            {
                out.Append(char(0xE0 | (cp >> 12)));
                out.Append(char(0x80 | ((cp >> 6) & 63)));
                out.Append(char(0x80 | (cp & 63)));
            }
            else
            {
                out.Append(char(0xF0 | (cp >> 18)));
                out.Append(char(0x80 | ((cp >> 12) & 63)));
                out.Append(char(0x80 | ((cp >> 6) & 63)));
                out.Append(char(0x80 | (cp & 63)));
            }
        }

        class Parser
        {
        public:
            Parser(StringView text, Tree& tree, String& error) : p_(text.Data()), end_(text.Data() + text.Size()), tree_(tree), error_(error) {}

            bool Run()
            {
                tree_.Clear();
                tree_.nodes.Reserve(usize(end_ - p_) / 8 + 16);
                tree_.children.Reserve(usize(end_ - p_) / 8 + 16);
                if (end_ - p_ >= 3 && u8(p_[0]) == 0xEF && u8(p_[1]) == 0xBB && u8(p_[2]) == 0xBF)
                    p_ += 3;
                lineStart_ = p_;
                Space();
                if (p_ == end_)
                    return Fail("no value");
                tree_.root = Value(0);
                if (failed_)
                    return false;
                Space();
                if (p_ != end_)
                    return Fail("text after the value");
                return true;
            }

        private:
            bool Fail(StringView what)
            {
                if (!failed_)
                {
                    failed_ = true;
                    error_ = Format("line {}, column {}: {}", line_ + 1, usize(p_ - lineStart_) + 1, what);
                }
                return false;
            }

            u32 FailNode(StringView what)
            {
                Fail(what);
                return kNoNode;
            }

            void Space()
            {
                while (p_ < end_)
                {
                    char c = *p_;
                    if (c == ' ' || c == '\t' || c == '\r')
                        p_++;
                    else if (c == '\n')
                    {
                        p_++;
                        line_++;
                        lineStart_ = p_;
                    }
                    else
                        break;
                }
            }

            u32 Add(NodeKind kind, StringView text = StringView())
            {
                Node n;
                n.kind = kind;
                n.line = line_ + 1;
                n.SetText(text);
                return tree_.Add(n);
            }

            bool Word(StringView word)
            {
                if (usize(end_ - p_) < word.Size() || !(StringView(p_, word.Size()) == word))
                    return Fail("an unknown word");
                p_ += word.Size();
                return true;
            }

            u32 Value(u32 depth)
            {
                switch (*p_)
                {
                case '{': return Container(depth, true);
                case '[': return Container(depth, false);
                case '"': return Str();
                case 't': return Word("true") ? Add(NodeKind::True) : kNoNode;
                case 'f': return Word("false") ? Add(NodeKind::False) : kNoNode;
                case 'n': return Word("null") ? Add(NodeKind::Null) : kNoNode;
                default:
                    if (*p_ == '-' || IsDigit(*p_))
                        return Number();
                    return FailNode("not a value");
                }
            }

            u32 Number()
            {
                const char* start = p_;
                if (*p_ == '-')
                    p_++;
                if (p_ == end_ || !IsDigit(*p_))
                    return FailNode("a number without digits");
                if (*p_ == '0')
                {
                    p_++;
                    if (p_ < end_ && IsDigit(*p_))
                        return FailNode("a number with a leading zero");
                }
                else
                    while (p_ < end_ && IsDigit(*p_))
                        p_++;
                if (p_ < end_ && *p_ == '.')
                {
                    p_++;
                    if (p_ == end_ || !IsDigit(*p_))
                        return FailNode("a fraction without digits");
                    while (p_ < end_ && IsDigit(*p_))
                        p_++;
                }
                if (p_ < end_ && (*p_ == 'e' || *p_ == 'E'))
                {
                    p_++;
                    if (p_ < end_ && (*p_ == '+' || *p_ == '-'))
                        p_++;
                    if (p_ == end_ || !IsDigit(*p_))
                        return FailNode("an exponent without digits");
                    while (p_ < end_ && IsDigit(*p_))
                        p_++;
                }
                return Add(NodeKind::Number, StringView(start, usize(p_ - start)));
            }

            // One UTF-8 sequence of two to four bytes at p_, checked: no overlong forms, no surrogates, nothing above
            // U+10FFFF.
            bool Utf8()
            {
                u8 c = u8(*p_);
                usize n;
                u32 cp;
                if (c >= 0xC2 && c <= 0xDF)
                    n = 1, cp = c & 0x1F;
                else if (c >= 0xE0 && c <= 0xEF)
                    n = 2, cp = c & 0x0F;
                else if (c >= 0xF0 && c <= 0xF4)
                    n = 3, cp = c & 0x07;
                else
                    return Fail("text that is not UTF-8");
                if (usize(end_ - p_) <= n)
                    return Fail("text that is not UTF-8");
                for (usize i = 1; i <= n; i++)
                {
                    u8 d = u8(p_[i]);
                    if ((d & 0xC0) != 0x80)
                        return Fail("text that is not UTF-8");
                    cp = (cp << 6) | (d & 63);
                }
                if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) || (n == 3 && (cp < 0x10000 || cp > 0x10FFFF)))
                    return Fail("text that is not UTF-8");
                p_ += n + 1;
                return true;
            }

            bool Hex4(u32& out)
            {
                if (end_ - p_ < 4)
                    return Fail("a \\u escape without four hex digits");
                out = 0;
                for (int i = 0; i < 4; i++)
                {
                    int v = HexValue(p_[i]);
                    if (v < 0)
                        return Fail("a \\u escape without four hex digits");
                    out = out * 16 + u32(v);
                }
                p_ += 4;
                return true;
            }

            // The string at p_ (its opening quote) as a view, or unescaped into the tree's buffer.
            bool StringText(StringView& out)
            {
                p_++;
                const char* start = p_;
                while (true)
                {
                    while (p_ < end_ && kPlain.table[u8(*p_)])
                        p_++;
                    if (p_ == end_)
                        return Fail("a string without its closing quote");
                    char c = *p_;
                    if (c == '"')
                    {
                        out = StringView(start, usize(p_ - start));
                        p_++;
                        return true;
                    }
                    if (c == '\\')
                        break;
                    if (u8(c) < 0x20)
                        return Fail("a control character in a string");
                    if (!Utf8())
                        return false;
                }
                // Escapes: the rest goes through a scratch string.
                scratch_.Clear();
                scratch_.Append(StringView(start, usize(p_ - start)));
                while (true)
                {
                    const char* run = p_;
                    while (p_ < end_ && kPlain.table[u8(*p_)])
                        p_++;
                    scratch_.Append(StringView(run, usize(p_ - run)));
                    if (p_ == end_)
                        return Fail("a string without its closing quote");
                    char c = *p_;
                    if (c == '"')
                    {
                        p_++;
                        out = tree_.Store(scratch_);
                        return true;
                    }
                    if (u8(c) < 0x20)
                        return Fail("a control character in a string");
                    if (c != '\\')
                    {
                        const char* at = p_;
                        if (!Utf8())
                            return false;
                        scratch_.Append(StringView(at, usize(p_ - at)));
                        continue;
                    }
                    p_++;
                    if (p_ == end_)
                        return Fail("a string without its closing quote");
                    c = *p_++;
                    switch (c)
                    {
                    case '"': scratch_.Append('"'); break;
                    case '\\': scratch_.Append('\\'); break;
                    case '/': scratch_.Append('/'); break;
                    case 'b': scratch_.Append('\b'); break;
                    case 'f': scratch_.Append('\f'); break;
                    case 'n': scratch_.Append('\n'); break;
                    case 'r': scratch_.Append('\r'); break;
                    case 't': scratch_.Append('\t'); break;
                    case 'u':
                    {
                        u32 cp;
                        if (!Hex4(cp))
                            return false;
                        if (cp >= 0xDC00 && cp <= 0xDFFF)
                            return Fail("a low surrogate without a high one");
                        if (cp >= 0xD800 && cp <= 0xDBFF)
                        {
                            u32 low;
                            if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u')
                                return Fail("a high surrogate without a low one");
                            p_ += 2;
                            if (!Hex4(low))
                                return false;
                            if (low < 0xDC00 || low > 0xDFFF)
                                return Fail("a high surrogate without a low one");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        AppendUtf8(scratch_, cp);
                        break;
                    }
                    default: p_--; return Fail("an unknown escape");
                    }
                }
            }

            u32 Str()
            {
                u32 line = line_;
                StringView text;
                if (!StringText(text))
                    return kNoNode;
                u32 n = Add(NodeKind::String, text);
                tree_.nodes[n].line = line + 1;
                return n;
            }

            u32 Container(u32 depth, bool object)
            {
                if (depth >= kMaxDepth)
                    return FailNode("containers nested too deep");
                u32 node = Add(object ? NodeKind::Object : NodeKind::Array);
                p_++;
                usize mark = stack_.Size();
                Space();
                char close = object ? '}' : ']';
                if (p_ < end_ && *p_ == close)
                    p_++;
                else
                    while (true)
                    {
                        if (p_ == end_)
                            return FailNode(object ? "an object without its closing brace" : "an array without its closing bracket");
                        if (object)
                        {
                            if (*p_ != '"')
                                return FailNode("an object key that is not a string");
                            u32 key = Str();
                            if (key == kNoNode)
                                return kNoNode;
                            stack_.Push(key);
                            Space();
                            if (p_ == end_ || *p_ != ':')
                                return FailNode("a key without a colon");
                            p_++;
                            Space();
                            if (p_ == end_)
                                return FailNode("a key without a value");
                        }
                        u32 value = Value(depth + 1);
                        if (value == kNoNode)
                            return kNoNode;
                        stack_.Push(value);
                        Space();
                        if (p_ == end_)
                            return FailNode(object ? "an object without its closing brace" : "an array without its closing bracket");
                        if (*p_ == close)
                        {
                            p_++;
                            break;
                        }
                        if (*p_ != ',')
                            return FailNode(object ? "object entries without a comma" : "array items without a comma");
                        p_++;
                        Space();
                    }
                tree_.SetChildren(node, stack_.Data() + mark, u32(stack_.Size() - mark));
                stack_.Resize(mark);
                return node;
            }

            const char* p_;
            const char* end_;
            const char* lineStart_ = nullptr;
            u32 line_ = 0;
            bool failed_ = false;
            Tree& tree_;
            String& error_;
            Array<u32> stack_;
            String scratch_;
        };
    }

    bool Parse(StringView text, Tree& out, String& error)
    {
        Parser parser(text, out, error);
        return parser.Run();
    }
}
