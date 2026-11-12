#pragma once

#include <Helix/Core/Strings/Format.h>
#include <Helix/Serialization/Transfer/Traits.h>
#include <Helix/Serialization/Yaml/Emitter.h>
#include <Helix/Serialization/Yaml/Tree.h>

#include <limits>

namespace helix::serialization
{
    // Writes a type's fields into the emitter. Bools are 0 and 1, enums their integer, a Guid 32 hex digits, Array<u8>
    // one hex string, a struct of version 2 or more starts with serializedVersion.
    class YamlWrite
    {
    public:
        static constexpr bool kReading = false;
        static constexpr bool kText = true;

        explicit YamlWrite(yaml::Emitter& emitter, ReferenceTable* references = nullptr) : e_(emitter), references_(references) {}

        template <class T>
        void Field(StringView name, T& value)
        {
            e_.Key(name);
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
            if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                Value(raw);
            }
            else if constexpr (kind == ValueKind::Bool)
                e_.Int(value ? 1 : 0);
            else if constexpr (kind == ValueKind::UInt64)
                e_.UInt(value);
            else if constexpr (IsInteger(kind))
                e_.Int(i64(value));
            else if constexpr (kind == ValueKind::Float || kind == ValueKind::Double)
                e_.Float(value);
            else if constexpr (kind == ValueKind::String)
                e_.Scalar(value);
            else if constexpr (kind == ValueKind::Guid)
            {
                char hex[33];
                value.ToChars(hex);
                e_.Scalar(StringView(hex, 32));
            }
            else if constexpr (kind == ValueKind::Array)
            {
                using Item = typename detail::IsArray<T>::Item;
                if constexpr (std::is_same_v<Item, u8>)
                    Bytes(value.Data(), value.Size());
                else
                {
                    e_.BeginSequence();
                    for (Item& item : value)
                        Value(item);
                    e_.EndSequence();
                }
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                e_.BeginSequence();
                for (auto& item : value)
                    Value(item);
                e_.EndSequence();
            }
            else
            {
                e_.BeginMapping(FlowOf(value));
                i32 saved = version_;
                version_ = VersionOf(value);
                if (version_ > 1)
                {
                    e_.Key("serializedVersion");
                    e_.Int(version_);
                }
                TransferStruct(*this, value);
                version_ = saved;
                e_.EndMapping();
            }
        }

    private:
        void Bytes(const u8* data, usize size);

        yaml::Emitter& e_;
        ReferenceTable* references_ = nullptr;
        i32 version_ = 1;
    };

    // Reads a type's fields from a mapping of a parsed tree. Missing fields keep their value, unknown fields are
    // skipped, a field of the wrong kind or a number that does not fit is an error (with its line) and keeps its value;
    // reading goes on after errors, so one call reports all of them.
    class YamlRead
    {
    public:
        static constexpr bool kReading = true;
        static constexpr bool kText = true;

        explicit YamlRead(const yaml::Tree& tree, ReferenceTable* references = nullptr) : t_(tree), references_(references) {}

        template <class T>
        void Field(StringView name, T& value)
        {
            u32 node = FindField(name);
            if (node != yaml::kNoNode)
                Value(node, name, value);
        }

        i32 Version() const { return version_; }

        // Object references go through the table (the raw value is the fileID without one).
        void SetReferences(ReferenceTable* table) { references_ = table; }
        void ObjectReference(u64& object) { TransferObjectReference(*this, references_, object); }
        // One line per problem, "line 12: m_Layer: ...".
        const Array<String>& Errors() const { return errors_; }

        template <class T>
        void Value(u32 node, StringView name, T& value)
        {
            constexpr ValueKind kind = KindOf<T>();
            if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                Value(node, name, raw);
                value = T(raw);
            }
            else if constexpr (kind == ValueKind::Bool)
            {
                i64 v;
                if (Integer(node, name, 0, 1, v))
                    value = v != 0;
            }
            else if constexpr (kind == ValueKind::UInt64)
                UInt64(node, name, value);
            else if constexpr (IsInteger(kind))
            {
                i64 v;
                if (Integer(node, name, i64(std::numeric_limits<T>::min()), i64(std::numeric_limits<T>::max()), v))
                    value = T(v);
            }
            else if constexpr (kind == ValueKind::Float || kind == ValueKind::Double)
            {
                double v;
                if (Real(node, name, v))
                    value = T(v);
            }
            else if constexpr (kind == ValueKind::String)
            {
                if (Expect(node, name, yaml::NodeKind::Scalar))
                    value = t_.Text(node);
            }
            else if constexpr (kind == ValueKind::Guid)
                GuidValue(node, name, value);
            else if constexpr (kind == ValueKind::Array)
            {
                using Item = typename detail::IsArray<T>::Item;
                if constexpr (std::is_same_v<Item, u8>)
                    Bytes(node, name, value);
                else if (Sequence(node, name))
                {
                    u32 count = t_.Count(node);
                    value.Resize(count);
                    for (u32 i = 0; i < count; i++)
                        Value(t_.Item(node, i), name, value[i]);
                }
            }
            else if constexpr (kind == ValueKind::FixedArray)
            {
                if (Sequence(node, name))
                {
                    constexpr usize n = std::extent_v<T>;
                    u32 count = t_.Count(node);
                    if (count != n)
                        Error(node, name, Format("{} items where {} are expected", count, n));
                    for (u32 i = 0; i < count && i < n; i++)
                        Value(t_.Item(node, i), name, value[i]);
                }
            }
            else if (Expect(node, name, yaml::NodeKind::Mapping))
            {
                u32 savedMapping = mapping_, savedCursor = cursor_;
                i32 savedVersion = version_;
                mapping_ = node;
                cursor_ = 0;
                version_ = 1;
                u32 v = FindField("serializedVersion");
                i64 version = 1;
                if (v != yaml::kNoNode && Integer(v, "serializedVersion", 1, 0x7FFFFFFF, version))
                    version_ = i32(version);
                TransferStruct(*this, value);
                mapping_ = savedMapping;
                cursor_ = savedCursor;
                version_ = savedVersion;
            }
        }

    private:
        u32 FindField(StringView name);
        void Error(u32 node, StringView name, StringView what);
        bool Expect(u32 node, StringView name, yaml::NodeKind kind);
        bool Sequence(u32 node, StringView name);
        bool Integer(u32 node, StringView name, i64 min, i64 max, i64& out);
        void UInt64(u32 node, StringView name, u64& out);
        bool Real(u32 node, StringView name, double& out);
        void GuidValue(u32 node, StringView name, Guid& out);
        void Bytes(u32 node, StringView name, Array<u8>& out);

        const yaml::Tree& t_;
        ReferenceTable* references_ = nullptr;
        u32 mapping_ = yaml::kNoNode;
        // The entry after the last field found: fields are usually read in the order they were written.
        u32 cursor_ = 0;
        i32 version_ = 1;
        Array<String> errors_;
    };

    // A document's body: "<TypeName>:" and the fields below it.
    template <class T>
    void WriteYaml(yaml::Emitter& emitter, T& object, ReferenceTable* references = nullptr)
    {
        YamlWrite w(emitter, references);
        emitter.BeginMapping();
        emitter.Key(TypeNameOf<T>());
        w.Value(object);
        emitter.EndMapping();
    }

    // Reads the body of a document (its root mapping, with the type name as the one key). False with every problem in
    // `errors` when something did not read; the fields that did are set.
    template <class T>
    bool ReadYaml(const yaml::Tree& tree, u32 root, T& object, Array<String>& errors, ReferenceTable* references = nullptr)
    {
        u32 body = tree.Find(root, TypeNameOf<T>());
        if (body == yaml::kNoNode)
        {
            errors.Push(Format("line {}: no {} in the document", root == yaml::kNoNode ? 0u : tree[root].line, TypeNameOf<T>()));
            return false;
        }
        YamlRead r(tree, references);
        r.Value(body, TypeNameOf<T>(), object);
        for (const String& e : r.Errors())
            errors.Push(e);
        return r.Errors().Empty();
    }
}
