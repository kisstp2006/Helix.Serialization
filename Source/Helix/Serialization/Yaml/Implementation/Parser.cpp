#include <Helix/Serialization/Yaml/Parser.h>

#include <Helix/Core/Strings/Format.h>

#include <charconv>
#include <cstring>

namespace helix::serialization::yaml
{
    namespace
    {
        // Characters a scan has to stop at: in a block key (: and #), in a flow scalar (also the flow indicators).
        struct CharClasses
        {
            bool key[256] = {};
            bool flow[256] = {};
            CharClasses()
            {
                key[u8(':')] = key[u8('#')] = true;
                for (char c : {':', '#', ',', '[', ']', '{', '}'})
                    flow[u8(c)] = true;
            }
        };

        const CharClasses& Classes()
        {
            static const CharClasses classes;
            return classes;
        }

        class Parser
        {
        public:
            Parser(StringView text, Tree& tree, String& error) : src_(text.Data()), size_(text.Size()), tree_(tree), error_(error), lines_(tree.lines) {}

            bool Run()
            {
                SplitLines();
                Go(0);
                // Scenes have two to three nodes per line; text with more grows the arrays once or twice.
                tree_.nodes.Reserve(lines_.Size() * 3 + 16);
                tree_.children.Reserve(lines_.Size() * 3 + 16);
                while (!failed_)
                {
                    SkipBlank();
                    if (li_ >= lines_.Size())
                        break;
                    const Line& l = lines_[li_];
                    StringView text = LineText(li_);
                    if (l.indent == 0 && text.StartsWith("%"))
                    {
                        if (!tree_.documents.Empty())
                            return Fail(0, "a directive after the first document");
                        tree_.directives.Push(text.Substr(1).Trimmed());
                        Next();
                        continue;
                    }
                    if (l.indent == 0 && IsMarker(li_, "..."))
                    {
                        Next();
                        continue;
                    }
                    Document d;
                    d.line = li_ + 1;
                    if (l.indent == 0 && IsMarker(li_, "---"))
                    {
                        if (!Header(text.Substr(3), d))
                            return false;
                        Next();
                    }
                    d.root = BlockNode(-1);
                    if (failed_)
                        return false;
                    tree_.documents.Push(d);
                    SkipBlank();
                    if (li_ < lines_.Size() && !(lines_[li_].indent == 0 && (IsMarker(li_, "---") || IsMarker(li_, "..."))))
                        return Fail(lines_[li_].indent, "content after the end of the document's node");
                }
                return !failed_;
            }

        private:
            const char* src_;
            usize size_;
            Tree& tree_;
            String& error_;
            Array<Line>& lines_;
            Array<u32> stack_;
            u32 li_ = 0;
            u32 col_ = 0;
            bool failed_ = false;
            const char* p_ = nullptr;
            u32 n_ = 0;
            // The colon IsKey found, so that Key does not look for it again.
            u32 keyLine_ = ~0u, keyStart_ = 0, keyColon_ = 0;
            const bool* keyStops_ = Classes().key;
            const bool* flowStops_ = Classes().flow;

            u32 ScanTo(u32 k, const bool* stops) const
            {
                while (k < n_ && !stops[u8(p_[k])])
                    k++;
                return k;
            }

            void SplitLines()
            {
                usize at = 0;
                lines_.Reserve(size_ / 24 + 1);
                while (at < size_)
                {
                    usize start = at;
                    const void* nl = std::memchr(src_ + at, '\n', size_ - at);
                    usize end = nl ? usize(static_cast<const char*>(nl) - src_) : size_;
                    at = nl ? end + 1 : size_;
                    usize trimmed = end > start && src_[end - 1] == '\r' ? end - 1 : end;
                    u32 indent = 0;
                    while (start + indent < trimmed && src_[start + indent] == ' ' && indent < 0xFFFF)
                        indent++;
                    bool blank = start + indent == trimmed || src_[start + indent] == '#';
                    lines_.Push({u32(start), u32(trimmed), u16(indent), blank});
                }
            }

            StringView LineText(u32 line) const { return StringView(src_ + lines_[line].start, lines_[line].end - lines_[line].start); }
            u32 LineLength(u32 line) const { return lines_[line].end - lines_[line].start; }
            char At(u32 c) const { return c < n_ ? p_[c] : '\0'; }
            bool AtEnd(u32 c) const { return c >= n_; }

            // Moves to a line; the characters of the current line are read through p_ and n_.
            void Go(u32 line)
            {
                li_ = line;
                if (li_ < lines_.Size())
                {
                    p_ = src_ + lines_[li_].start;
                    n_ = lines_[li_].end - lines_[li_].start;
                }
                else
                {
                    p_ = src_;
                    n_ = 0;
                }
            }
            void Next() { Go(li_ + 1); }

            bool IsMarker(u32 line, const char* marker) const
            {
                StringView t = LineText(line);
                return t.StartsWith(marker) && (t.Size() == 3 || t[3] == ' ' || t[3] == '\t');
            }

            bool Fail(u32 column, StringView what)
            {
                if (!failed_)
                    error_ = Format("line {}, column {}: {}", li_ + 1, column + 1, what);
                failed_ = true;
                return false;
            }

            void SkipBlank()
            {
                u32 line = li_;
                while (line < lines_.Size() && lines_[line].blank)
                    line++;
                if (line != li_)
                    Go(line);
            }

            // The end of the line's content, without a comment (a # after a space or at the start).
            bool RestIsBlank(u32 c) const
            {
                while (!AtEnd(c) && (At(c) == ' ' || At(c) == '\t'))
                    c++;
                return AtEnd(c) || At(c) == '#';
            }

            bool Header(StringView rest, Document& d)
            {
                usize at = 0;
                while (at < rest.Size())
                {
                    while (at < rest.Size() && rest[at] == ' ')
                        at++;
                    usize end = rest.Find(' ', at);
                    end = end == StringView::npos ? rest.Size() : end;
                    StringView token = rest.Substr(at, end - at);
                    at = end;
                    if (token.Empty())
                        break;
                    if (token[0] == '!')
                    {
                        usize bang = token.FindLast('!');
                        StringView number = token.Substr(bang + 1);
                        auto r = std::from_chars(number.Data(), number.Data() + number.Size(), d.classID);
                        if (r.ec != std::errc() || r.ptr != number.Data() + number.Size())
                            return Fail(3, "a document tag without a class number");
                    }
                    else if (token[0] == '&')
                    {
                        StringView number = token.Substr(1);
                        auto r = std::from_chars(number.Data(), number.Data() + number.Size(), d.fileID);
                        if (r.ec != std::errc() || r.ptr != number.Data() + number.Size())
                            return Fail(3, "a document anchor that is not a number");
                    }
                    else if (token == StringView("stripped"))
                        d.stripped = true;
                    else
                        return Fail(3, "content on the document start line");
                }
                return true;
            }

            u32 AddScalar(StringView text, ScalarStyle style, u32)
            {
                Node n;
                n.kind = NodeKind::Scalar;
                n.style = style;
                n.SetText(text);
                n.line = li_ + 1;
                return tree_.Add(n);
            }

            u32 AddContainer(NodeKind kind, bool flow, u32)
            {
                Node n;
                n.kind = kind;
                n.flow = flow;
                n.line = li_ + 1;
                return tree_.Add(n);
            }

            void Close(u32 node, usize base)
            {
                tree_.SetChildren(node, stack_.Data() + base, u32(stack_.Size() - base));
                stack_.Resize(base);
            }

            // A block node deeper than `parent`, or an empty scalar where there is none.
            u32 BlockNode(i32 parent)
            {
                SkipBlank();
                if (li_ >= lines_.Size() || i32(lines_[li_].indent) <= parent ||
                    (lines_[li_].indent == 0 && (IsMarker(li_, "---") || IsMarker(li_, "..."))))
                    return AddScalar(StringView(), ScalarStyle::Plain, 0);
                col_ = lines_[li_].indent;
                return NodeAt(parent);
            }

            bool IsSequenceItem(u32 c) const { return At(c) == '-' && (AtEnd(c + 1) || At(c + 1) == ' '); }

            // Whether a mapping key starts at column c: a scalar followed by a colon and a space or the end of the line.
            bool IsKey(u32 c)
            {
                char first = At(c);
                if (first == '\'' || first == '"')
                {
                    u32 k = c + 1;
                    while (!AtEnd(k))
                    {
                        if (At(k) == first)
                        {
                            if (first == '\'' && At(k + 1) == '\'')
                            {
                                k += 2;
                                continue;
                            }
                            break;
                        }
                        k += first == '"' && At(k) == '\\' ? 2 : 1;
                    }
                    if (AtEnd(k))
                        return false;
                    k++;
                    while (At(k) == ' ')
                        k++;
                    return At(k) == ':' && (AtEnd(k + 1) || At(k + 1) == ' ');
                }
                if (first == '{' || first == '[' || first == '#' || IsSequenceItem(c))
                    return false;
                for (u32 k = ScanTo(c, keyStops_); k < n_; k = ScanTo(k + 1, keyStops_))
                {
                    if (p_[k] == '#' && k > c && p_[k - 1] == ' ')
                        return false;
                    if (p_[k] == ':' && (k + 1 >= n_ || p_[k + 1] == ' '))
                    {
                        keyLine_ = li_;
                        keyStart_ = c;
                        keyColon_ = k;
                        return true;
                    }
                }
                return false;
            }

            u32 NodeAt(i32 parent)
            {
                u32 c = col_;
                if (IsSequenceItem(c))
                    return BlockSequence(c);
                if (IsKey(c))
                    return BlockMapping(c);
                return Inline(parent);
            }

            u32 BlockMapping(u32 indent)
            {
                u32 node = AddContainer(NodeKind::Mapping, false, indent);
                usize base = stack_.Size();
                while (!failed_)
                {
                    u32 key = Key();
                    if (failed_)
                        break;
                    // After the colon: the value on the same line, or a block node below it.
                    u32 value;
                    if (RestIsBlank(col_))
                    {
                        Next();
                        SkipBlank();
                        if (li_ < lines_.Size() && lines_[li_].indent == indent && (col_ = indent, IsSequenceItem(indent)))
                            value = BlockSequence(indent);
                        else
                            value = BlockNode(i32(indent));
                    }
                    else
                        value = Inline(i32(indent));
                    stack_.Push(key);
                    stack_.Push(value);
                    SkipBlank();
                    if (failed_ || li_ >= lines_.Size())
                        break;
                    const Line& l = lines_[li_];
                    if (l.indent < indent || (l.indent == 0 && (IsMarker(li_, "---") || IsMarker(li_, "..."))))
                        break;
                    col_ = l.indent;
                    if (l.indent > indent)
                    {
                        Fail(l.indent, "a line indented deeper than the mapping it is in");
                        break;
                    }
                    if (IsSequenceItem(indent))
                        break;
                    if (!IsKey(indent))
                    {
                        Fail(indent, "a mapping entry without a key and a colon");
                        break;
                    }
                }
                Close(node, base);
                return node;
            }

            u32 BlockSequence(u32 indent)
            {
                u32 node = AddContainer(NodeKind::Sequence, false, indent);
                usize base = stack_.Size();
                while (!failed_)
                {
                    u32 c = indent + 1;
                    while (At(c) == ' ')
                        c++;
                    u32 item;
                    if (RestIsBlank(c))
                    {
                        Next();
                        item = BlockNode(i32(indent));
                    }
                    else
                    {
                        col_ = c;
                        item = NodeAt(i32(indent));
                    }
                    stack_.Push(item);
                    SkipBlank();
                    if (failed_ || li_ >= lines_.Size())
                        break;
                    const Line& l = lines_[li_];
                    if (l.indent != indent || (l.indent == 0 && (IsMarker(li_, "---") || IsMarker(li_, "..."))))
                        break;
                    col_ = indent;
                    if (!IsSequenceItem(indent))
                        break;
                }
                Close(node, base);
                return node;
            }

            u32 Key()
            {
                u32 c = col_;
                u32 key;
                if (At(c) == '\'' || At(c) == '"')
                {
                    key = Quoted(-1);
                    c = col_;
                    while (At(c) == ' ')
                        c++;
                }
                else
                {
                    u32 k = c;
                    if (keyLine_ == li_ && keyStart_ == c)
                        k = keyColon_;
                    else
                        while (!(At(k) == ':' && (AtEnd(k + 1) || At(k + 1) == ' ')))
                            k++;
                    u32 end = k;
                    while (end > c && At(end - 1) == ' ')
                        end--;
                    key = AddScalar(StringView(src_ + lines_[li_].start + c, end - c), ScalarStyle::Plain, c);
                    c = k;
                }
                if (At(c) != ':')
                {
                    Fail(c, "a key without a colon");
                    return key;
                }
                col_ = c + 1;
                while (At(col_) == ' ')
                    col_++;
                return key;
            }

            // A value that starts on the current line: a flow container, a quoted scalar or a plain scalar, which may go on
            // over lines indented deeper than `parent`.
            u32 Inline(i32 parent)
            {
                u32 c = col_;
                char first = At(c);
                u32 node;
                if (first == '{' || first == '[')
                    node = Flow();
                else if (first == '\'' || first == '"')
                    node = Quoted(parent);
                else if (first == '|' || first == '>')
                {
                    Fail(c, "literal and folded block scalars are not supported");
                    return AddScalar(StringView(), ScalarStyle::Plain, c);
                }
                else
                    return Plain(parent);
                if (!failed_ && !RestIsBlank(col_))
                    Fail(col_, "content after the end of a value");
                Next();
                return node;
            }

            // A plain scalar to the end of the line (without a comment), folded with following deeper lines.
            u32 Plain(i32 parent)
            {
                u32 start = col_, line = li_;
                auto lineEnd = [&](u32 from) {
                    u32 end = n_;
                    for (const char* hash = static_cast<const char*>(std::memchr(p_ + from, '#', n_ - from)); hash;
                         hash = static_cast<const char*>(std::memchr(hash + 1, '#', usize(p_ + n_ - hash - 1))))
                    {
                        u32 k = u32(hash - p_);
                        if (k > from && (p_[k - 1] == ' ' || p_[k - 1] == '\t'))
                        {
                            end = k;
                            break;
                        }
                    }
                    while (end > from && (At(end - 1) == ' ' || At(end - 1) == '\t'))
                        end--;
                    return end;
                };
                u32 end = lineEnd(start);
                StringView first(src_ + lines_[li_].start + start, end - start);
                Next();
                // Continuation lines: deeper than the parent, not starting another entry.
                u32 next = li_;
                while (next < lines_.Size() && lines_[next].blank)
                    next++;
                if (next >= lines_.Size() || i32(lines_[next].indent) <= parent || parent < 0)
                {
                    Node n;
                    n.SetText(first);
                    n.line = line + 1;
                    return tree_.Add(n);
                }
                String folded;
                folded.Append(first);
                while (li_ < lines_.Size())
                {
                    u32 blanks = 0;
                    while (li_ < lines_.Size() && lines_[li_].blank && LineLength(li_) == lines_[li_].indent)
                    {
                        blanks++;
                        Next();
                    }
                    if (li_ >= lines_.Size() || i32(lines_[li_].indent) <= parent || lines_[li_].blank)
                        break;
                    if (IsKey(lines_[li_].indent) || IsSequenceItem(lines_[li_].indent))
                    {
                        Fail(lines_[li_].indent, "a line indented deeper than the mapping it is in");
                        break;
                    }
                    if (blanks)
                        folded.Append('\n', blanks);
                    else
                        folded.Append(' ');
                    u32 from = lines_[li_].indent;
                    u32 e = lineEnd(from);
                    folded.Append(StringView(src_ + lines_[li_].start + from, e - from));
                    Next();
                }
                Node n;
                n.SetText(tree_.Store(folded));
                n.line = line + 1;
                return tree_.Add(n);
            }

            static bool Hex(char c, u32& v)
            {
                if (c >= '0' && c <= '9')
                    return v = v * 16 + u32(c - '0'), true;
                if (c >= 'a' && c <= 'f')
                    return v = v * 16 + u32(c - 'a' + 10), true;
                if (c >= 'A' && c <= 'F')
                    return v = v * 16 + u32(c - 'A' + 10), true;
                return false;
            }

            static void Utf8(String& out, u32 cp)
            {
                if (cp < 0x80)
                    out.Append(char(cp));
                else if (cp < 0x800)
                {
                    out.Append(char(0xC0 | (cp >> 6)));
                    out.Append(char(0x80 | (cp & 0x3F)));
                }
                else if (cp < 0x10000)
                {
                    out.Append(char(0xE0 | (cp >> 12)));
                    out.Append(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.Append(char(0x80 | (cp & 0x3F)));
                }
                else
                {
                    out.Append(char(0xF0 | (cp >> 18)));
                    out.Append(char(0x80 | ((cp >> 12) & 0x3F)));
                    out.Append(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.Append(char(0x80 | (cp & 0x3F)));
                }
            }

            // A single or double quoted scalar; leaves col_ after the closing quote (on the line it ends on).
            u32 Quoted(i32 parent)
            {
                (void)parent;
                char quote = At(col_);
                u32 line = li_, column = col_;
                u32 c = col_ + 1;
                // The common case: on one line, without escapes, a view into the text.
                {
                    u32 k = c;
                    bool simple = true;
                    while (!AtEnd(k) && At(k) != quote)
                    {
                        if (quote == '"' && At(k) == '\\')
                            simple = false;
                        k++;
                    }
                    if (simple && !AtEnd(k) && !(quote == '\'' && At(k + 1) == '\''))
                    {
                        col_ = k + 1;
                        Node n;
                        n.style = quote == '\'' ? ScalarStyle::SingleQuoted : ScalarStyle::DoubleQuoted;
                        n.SetText(StringView(src_ + lines_[li_].start + c, k - c));
                        n.line = line + 1;
                        return tree_.Add(n);
                    }
                }
                String value;
                while (true)
                {
                    if (AtEnd(c))
                    {
                        // A line break inside quotes folds to a space, empty lines to line breaks; a backslash before
                        // it joins the lines.
                        while (value.Size() && (value.CStr()[value.Size() - 1] == ' ' || value.CStr()[value.Size() - 1] == '\t'))
                            value.Resize(value.Size() - 1);
                        Next();
                        u32 blanks = 0;
                        while (li_ < lines_.Size() && LineLength(li_) == lines_[li_].indent)
                        {
                            blanks++;
                            Next();
                        }
                        if (li_ >= lines_.Size())
                        {
                            Go(line);
                            Fail(column, "a quoted scalar without its closing quote");
                            return AddScalar(StringView(), ScalarStyle::Plain, column);
                        }
                        if (blanks)
                            value.Append('\n', blanks);
                        else
                            value.Append(' ');
                        c = 0;
                        while (At(c) == ' ' || At(c) == '\t')
                            c++;
                        continue;
                    }
                    char ch = At(c);
                    if (ch == quote)
                    {
                        if (quote == '\'' && At(c + 1) == '\'')
                        {
                            value.Append('\'');
                            c += 2;
                            continue;
                        }
                        break;
                    }
                    if (quote == '"' && ch == '\\')
                    {
                        if (AtEnd(c + 1))
                        {
                            // An escaped line break: the lines are joined without a space.
                            Next();
                            if (li_ >= lines_.Size())
                            {
                                Go(line);
                                Fail(column, "a quoted scalar without its closing quote");
                                return AddScalar(StringView(), ScalarStyle::Plain, column);
                            }
                            c = 0;
                            while (At(c) == ' ' || At(c) == '\t')
                                c++;
                            continue;
                        }
                        char e = At(c + 1);
                        c += 2;
                        u32 digits = e == 'x' ? 2 : e == 'u' ? 4 : e == 'U' ? 8 : 0;
                        if (digits)
                        {
                            u32 cp = 0;
                            for (u32 i = 0; i < digits; i++, c++)
                                if (!Hex(At(c), cp))
                                {
                                    Fail(c, "a bad escape in a double quoted scalar");
                                    return AddScalar(StringView(), ScalarStyle::Plain, column);
                                }
                            Utf8(value, cp);
                            continue;
                        }
                        switch (e)
                        {
                        case '0': value.Append('\0'); break;
                        case 'a': value.Append('\a'); break;
                        case 'b': value.Append('\b'); break;
                        case 't': case '\t': value.Append('\t'); break;
                        case 'n': value.Append('\n'); break;
                        case 'v': value.Append('\v'); break;
                        case 'f': value.Append('\f'); break;
                        case 'r': value.Append('\r'); break;
                        case 'e': value.Append('\x1B'); break;
                        case ' ': value.Append(' '); break;
                        case '"': value.Append('"'); break;
                        case '/': value.Append('/'); break;
                        case '\\': value.Append('\\'); break;
                        case 'N': Utf8(value, 0x85); break;
                        case '_': Utf8(value, 0xA0); break;
                        case 'L': Utf8(value, 0x2028); break;
                        case 'P': Utf8(value, 0x2029); break;
                        default:
                            Fail(c - 1, "an unknown escape in a double quoted scalar");
                            return AddScalar(StringView(), ScalarStyle::Plain, column);
                        }
                        continue;
                    }
                    value.Append(ch);
                    c++;
                }
                col_ = c + 1;
                Node n;
                n.style = quote == '\'' ? ScalarStyle::SingleQuoted : ScalarStyle::DoubleQuoted;
                n.SetText(tree_.Store(value));
                n.line = line + 1;
                return tree_.Add(n);
            }

            // Spaces, comments and line breaks between the parts of a flow container.
            bool FlowSpace()
            {
                while (true)
                {
                    while (At(col_) == ' ' || At(col_) == '\t')
                        col_++;
                    if (At(col_) == '#' || AtEnd(col_))
                    {
                        Next();
                        col_ = 0;
                        if (li_ >= lines_.Size())
                        {
                            Go(u32(lines_.Size() - 1));
                            return Fail(LineLength(li_), "a flow container without its end");
                        }
                        continue;
                    }
                    return true;
                }
            }

            static bool FlowIndicator(char c) { return c == ',' || c == '[' || c == ']' || c == '{' || c == '}'; }

            u32 FlowScalar()
            {
                char first = At(col_);
                if (first == '\'' || first == '"')
                    return Quoted(-1);
                u32 start = col_, k = ScanTo(col_, flowStops_);
                while (k < n_)
                {
                    char ch = p_[k];
                    if ((ch == ':' && !(k + 1 >= n_ || p_[k + 1] == ' ' || FlowIndicator(p_[k + 1]))) || (ch == '#' && !(k > start && p_[k - 1] == ' ')))
                    {
                        k = ScanTo(k + 1, flowStops_);
                        continue;
                    }
                    break;
                }
                u32 end = k;
                while (end > start && (At(end - 1) == ' ' || At(end - 1) == '\t'))
                    end--;
                col_ = k;
                return AddScalar(StringView(src_ + lines_[li_].start + start, end - start), ScalarStyle::Plain, start);
            }

            u32 FlowNode()
            {
                char c = At(col_);
                if (c == '{' || c == '[')
                    return Flow();
                return FlowScalar();
            }

            // A flow mapping or sequence; leaves col_ after its closing bracket.
            u32 Flow()
            {
                bool mapping = At(col_) == '{';
                char close = mapping ? '}' : ']';
                u32 node = AddContainer(mapping ? NodeKind::Mapping : NodeKind::Sequence, true, col_);
                usize base = stack_.Size();
                col_++;
                if (!FlowSpace())
                    return node;
                while (!failed_ && At(col_) != close)
                {
                    if (mapping)
                    {
                        u32 key = FlowScalar();
                        if (!FlowSpace())
                            break;
                        u32 value;
                        if (At(col_) == ':')
                        {
                            col_++;
                            if (!FlowSpace())
                                break;
                            value = At(col_) == ',' || At(col_) == close ? AddScalar(StringView(), ScalarStyle::Plain, col_) : FlowNode();
                        }
                        else
                            value = AddScalar(StringView(), ScalarStyle::Plain, col_);
                        stack_.Push(key);
                        stack_.Push(value);
                    }
                    else
                    {
                        if (At(col_) == ',')
                        {
                            Fail(col_, "an empty item in a flow sequence");
                            break;
                        }
                        stack_.Push(FlowNode());
                    }
                    if (failed_ || !FlowSpace())
                        break;
                    if (At(col_) == ',')
                    {
                        col_++;
                        if (!FlowSpace())
                            break;
                        continue;
                    }
                    if (At(col_) != close)
                    {
                        Fail(col_, mapping ? "a flow mapping entry without a comma or a closing brace" : "a flow sequence item without a comma or a closing bracket");
                        break;
                    }
                }
                col_++;
                Close(node, base);
                return node;
            }
        };
    }

    bool Parse(StringView text, Tree& out, String& error)
    {
        out.Clear();
        error.Clear();
        Parser parser(text, out, error);
        return parser.Run();
    }
}
