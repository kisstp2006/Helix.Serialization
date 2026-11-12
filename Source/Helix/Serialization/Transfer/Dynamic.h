#pragma once

#include <Helix/Serialization/Transfer/Binary.h>
#include <Helix/Serialization/Transfer/Traits.h>
#include <Helix/Serialization/Transfer/TypeTree.h>

#include <cstring>

// Fields known at run time (the fields of scripts, C++ and C#) drive any visitor through one C ABI. A DynamicVisitor
// is a visitor behind a table of functions: a value, a string, an object reference, a struct and an array each have
// an entry, taking the field's name and address and, for strings, structs and arrays, a descriptor of functions that
// reach the field's content. The native side (DynamicAdapter<V>) turns every call back into V's own Field, with
// proxies for structs and struct arrays whose type name, flow and version are known at run time, so every visitor
// works as it does for C++ types:
//
//   serialization::DynamicVisitor d = serialization::DynamicAdapter<YamlWrite>::Make(writer);
//   script->transfer(object, &d);
//
// The C++ side of the ABI is a visitor too (DynamicFields), so generated code and the types it uses (member Transfer,
// TransferTraits) drive the table as they drive any visitor:
//
//   VisitDynamic(*visitor, [&](auto& f) { f.Field("speed", self->speed); f.Field("path", self->path); });
//
// Object references cross as EntityId values: a C++ type is one through a specialization of ObjectReferenceTraits.
// Fixed size arrays (T[N], C# fixed buffers) cross as arrays that keep their size. An opaque struct (kDynamicOpaque,
// whose type is not the same for every value: a managed reference's data) is its own type tree and binary form for
// the visitors of the binary form (V::kBinary), a struct for the others.
namespace helix::serialization
{
    struct DynamicVisitor;
    struct DynamicStruct;

    // The item kind of an array of object references (the other kinds are ValueKind's).
    inline constexpr u32 kDynamicReference = 255;

    // A string: its text, and setting it. `s` is what the field entry or the array's item function gave.
    struct DynamicString
    {
        void (*get)(const void* s, const DynamicString* type, const char** data, u32* size);
        void (*set)(void* s, const DynamicString* type, const char* data, u32 size);
        void* user;
    };

    // An object reference of an array: its EntityId value (0 for none), and setting it.
    struct DynamicReference
    {
        u64 (*get)(const void* r, const DynamicReference* type);
        void (*set)(void* r, const DynamicReference* type, u64 id);
        void* user;
    };

    inline constexpr u32 kDynamicFlow = 1;
    inline constexpr u32 kDynamicOpaque = 2;

    // A struct: its name in files, its flags (kDynamicFlow: YAML on one line; kDynamicOpaque), version, and its fields
    // through the visitor.
    struct DynamicStruct
    {
        const char* typeName;
        u32 typeNameSize;
        u32 flags;
        i32 version;
        void (*transfer)(void* object, DynamicVisitor* visitor, const DynamicStruct* type);
        void* user;
    };

    // An array: the kind of its items (a ValueKind or kDynamicReference), their descriptor, its size, resizing it,
    // the address of an item, and a default item (what the type tree describes).
    struct DynamicArray
    {
        u32 itemKind;
        const DynamicStruct* itemStruct;
        const DynamicString* itemString;
        const DynamicReference* itemReference;
        u32 (*size)(const void* array, const DynamicArray* type);
        void (*resize)(void* array, u32 count, const DynamicArray* type);
        void* (*item)(void* array, u32 index, const DynamicArray* type);
        void* (*prototype)(const DynamicArray* type);
        void* user;
    };

    struct DynamicVisitorTable
    {
        // A number, a bool (one byte, 0 or 1) or a Guid; `kind` is its ValueKind.
        void (*value)(void* self, const char* name, u32 nameSize, u32 kind, void* value);
        void (*string)(void* self, const char* name, u32 nameSize, void* string, const DynamicString* type);
        void (*reference)(void* self, const char* name, u32 nameSize, u64* object);
        void (*structure)(void* self, const char* name, u32 nameSize, void* object, const DynamicStruct* type);
        void (*array)(void* self, const char* name, u32 nameSize, void* array, const DynamicArray* type);
        // The visitor's Version() and ObjectReference (for reference types with a Transfer of their own).
        i32 (*version)(void* self);
        void (*objectReference)(void* self, u64* object);
    };

    inline constexpr u32 kDynamicReading = 1;
    inline constexpr u32 kDynamicText = 2;
    // A visitor that writes the text or binary form (or its type tree): OnBeforeSerialize runs before it.
    inline constexpr u32 kDynamicSerializes = 4;

    struct DynamicVisitor
    {
        void* self;
        const DynamicVisitorTable* table;
        // kDynamicReading, kDynamicText, kDynamicSerializes.
        u32 flags;
    };

    // Specialized for C++ types that are object references (engine object pointers, value handles of the C++ script
    // API): static u64 Id(const T&) and static void Set(T&, u64).
    template <class T>
    struct ObjectReferenceTraits;

    template <class T>
    concept DynamicObjectReference = requires(const T& t, T& m) {
        ObjectReferenceTraits<T>::Id(t);
        ObjectReferenceTraits<T>::Set(m, u64());
    };

    // ------------------------------------------------------------------------------------------- the native side

    template <class V>
    class DynamicAdapter;

    // An object reference as visitors see one: PPtr's layout ({fileID, guid, type}).
    struct DynamicReferenceValue
    {
        static constexpr StringView kTypeName = "PPtr";
        static constexpr bool kFlow = true;
        u64 id = 0;

        template <class V>
        void Transfer(V& v)
        {
            v.ObjectReference(id);
        }
    };

    // The binary form of an opaque struct.
    struct DynamicOpaque
    {
        static constexpr StringView kTypeName = "OpaqueData";
        Array<u8> tree = Array<u8>();
        Array<u8> data = Array<u8>();

        template <class V>
        void Transfer(V& v)
        {
            v.Field("tree", tree);
            v.Field("data", data);
        }
    };

    // A struct whose fields are known at run time.
    struct DynamicStructValue
    {
        void* object = nullptr;
        const DynamicStruct* type = nullptr;

        StringView TypeName() const { return StringView(type->typeName, type->typeNameSize); }
        bool StructFlow() const { return (type->flags & kDynamicFlow) != 0; }
        i32 StructVersion() const { return type->version; }

        template <class V>
        void Transfer(V& v)
        {
            DynamicVisitor d = DynamicAdapter<V>::Make(v);
            type->transfer(object, &d, type);
        }
    };

    // An array of such structs: resizing goes to the array, the items are proxies of its items.
    class DynamicStructArray
    {
    public:
        DynamicStructArray(void* array, const DynamicArray* type) : array_(array), type_(type) { Bind(); }

        usize Size() const { return items_.Size(); }
        void Resize(usize count)
        {
            type_->resize(array_, u32(count), type_);
            Bind();
        }
        DynamicStructValue& operator[](usize index) { return items_[index]; }
        DynamicStructValue* Data() { return items_.Data(); }
        DynamicStructValue* begin() { return items_.Data(); }
        DynamicStructValue* end() { return items_.Data() + items_.Size(); }
        DynamicStructValue Prototype() const { return DynamicStructValue{type_->prototype(type_), type_->itemStruct}; }

    private:
        void Bind()
        {
            u32 count = type_->size(array_, type_);
            items_.Resize(count);
            for (u32 i = 0; i < count; i++)
                items_[i] = DynamicStructValue{type_->item(array_, i, type_), type_->itemStruct};
        }

        void* array_;
        const DynamicArray* type_;
        Array<DynamicStructValue> items_ = Array<DynamicStructValue>();
    };

    namespace detail
    {
        template <>
        struct IsArray<DynamicStructArray> : std::true_type
        {
            using Item = DynamicStructValue;
        };
    }

    // The table of visitor V.
    template <class V>
    class DynamicAdapter
    {
    public:
        static DynamicVisitor Make(V& v)
        {
            constexpr u32 flags = (V::kReading ? kDynamicReading : 0u) | (V::kText ? kDynamicText : 0u) |
                                  (!V::kReading && (V::kText || Binary()) ? kDynamicSerializes : 0u);
            return DynamicVisitor{&v, &kTable, flags};
        }

        // Is V a visitor of the binary form (its kBinary)?
        static constexpr bool Binary()
        {
            if constexpr (requires { V::kBinary; })
                return V::kBinary;
            else
                return false;
        }

    private:
        static V& Of(void* self) { return *static_cast<V*>(self); }

        static void Value(void* self, const char* name, u32 nameSize, u32 kind, void* value)
        {
            V& v = Of(self);
            StringView n(name, nameSize);
            switch (ValueKind(kind))
            {
            case ValueKind::Bool:
            {
                bool b = *static_cast<u8*>(value) != 0;
                v.Field(n, b);
                *static_cast<u8*>(value) = b ? 1 : 0;
                break;
            }
            case ValueKind::Int8: v.Field(n, *static_cast<i8*>(value)); break;
            case ValueKind::UInt8: v.Field(n, *static_cast<u8*>(value)); break;
            case ValueKind::Int16: Through<i16>(v, n, value); break;
            case ValueKind::UInt16: Through<u16>(v, n, value); break;
            case ValueKind::Int32: Through<i32>(v, n, value); break;
            case ValueKind::UInt32: Through<u32>(v, n, value); break;
            case ValueKind::Int64: Through<i64>(v, n, value); break;
            case ValueKind::UInt64: Through<u64>(v, n, value); break;
            case ValueKind::Float: Through<f32>(v, n, value); break;
            case ValueKind::Double: Through<f64>(v, n, value); break;
            case ValueKind::Guid: Through<Guid>(v, n, value); break;
            default: break;
            }
        }

        // Through a local: the field's address need not be aligned (C# passes what it has).
        template <class T>
        static void Through(V& v, StringView name, void* at)
        {
            T value;
            std::memcpy(static_cast<void*>(&value), at, sizeof(T));
            v.Field(name, value);
            std::memcpy(at, static_cast<const void*>(&value), sizeof(T));
        }

        static void Text(void* self, const char* name, u32 nameSize, void* s, const DynamicString* type)
        {
            String text = Load(s, type);
            Of(self).Field(StringView(name, nameSize), text);
            if constexpr (V::kReading)
                type->set(s, type, text.Data(), u32(text.Size()));
        }

        static String Load(const void* s, const DynamicString* type)
        {
            const char* data = nullptr;
            u32 size = 0;
            type->get(s, type, &data, &size);
            String text;
            text = StringView(data, size);
            return text;
        }

        static void Reference(void* self, const char* name, u32 nameSize, u64* object)
        {
            DynamicReferenceValue r{*object};
            Of(self).Field(StringView(name, nameSize), r);
            *object = r.id;
        }

        static void Structure(void* self, const char* name, u32 nameSize, void* object, const DynamicStruct* type)
        {
            DynamicStructValue s{object, type};
            if constexpr (requires { V::kBinary; })
                if (type->flags & kDynamicOpaque)
                    return Opaque(Of(self), StringView(name, nameSize), s);
            Of(self).Field(StringView(name, nameSize), s);
        }

        // An opaque struct in the binary form: its type tree and its binary form, read through the safe reader.
        static void Opaque(V& v, StringView name, DynamicStructValue& s)
        {
            ReferenceTable* references = nullptr;
            if constexpr (requires { v.References(); })
                references = v.References();
            DynamicOpaque blob;
            if constexpr (!V::kReading)
            {
                TypeTree tree;
                TypeTreeBuild(tree).Node("Base", s);
                tree.ComputeSizes();
                tree.Write(blob.tree);
                BinaryWrite w(blob.data, references);
                w.Value(s);
            }
            v.Field(name, blob);
            if constexpr (V::kReading)
            {
                TypeTree tree;
                String error;
                if (!blob.tree.Empty() && TypeTree::Read(Span<const u8>(blob.tree), tree, error))
                {
                    SafeBinaryRead r(tree, Span<const u8>(blob.data), references);
                    r.At(0, 0, s);
                }
            }
        }

        static void Sequence(void* self, const char* name, u32 nameSize, void* array, const DynamicArray* type)
        {
            V& v = Of(self);
            StringView n(name, nameSize);
            switch (type->itemKind)
            {
            case u32(ValueKind::Bool): Items<bool>(v, n, array, type); break;
            case u32(ValueKind::Int8): Items<i8>(v, n, array, type); break;
            case u32(ValueKind::UInt8): Items<u8>(v, n, array, type); break;
            case u32(ValueKind::Int16): Items<i16>(v, n, array, type); break;
            case u32(ValueKind::UInt16): Items<u16>(v, n, array, type); break;
            case u32(ValueKind::Int32): Items<i32>(v, n, array, type); break;
            case u32(ValueKind::UInt32): Items<u32>(v, n, array, type); break;
            case u32(ValueKind::Int64): Items<i64>(v, n, array, type); break;
            case u32(ValueKind::UInt64): Items<u64>(v, n, array, type); break;
            case u32(ValueKind::Float): Items<f32>(v, n, array, type); break;
            case u32(ValueKind::Double): Items<f64>(v, n, array, type); break;
            case u32(ValueKind::Guid): Items<Guid>(v, n, array, type); break;
            case u32(ValueKind::String):
            {
                u32 count = type->size(array, type);
                Array<String> items;
                items.Resize(count);
                for (u32 i = 0; i < count; i++)
                    items[i] = Load(type->item(array, i, type), type->itemString);
                v.Field(n, items);
                if constexpr (V::kReading)
                {
                    u32 kept = Kept(array, type, items.Size());
                    for (u32 i = 0; i < kept; i++)
                        type->itemString->set(type->item(array, i, type), type->itemString, items[i].Data(), u32(items[i].Size()));
                }
                break;
            }
            case u32(ValueKind::Struct):
            {
                DynamicStructArray items(array, type);
                v.Field(n, items);
                break;
            }
            case kDynamicReference:
            {
                u32 count = type->size(array, type);
                Array<DynamicReferenceValue> items;
                items.Resize(count);
                for (u32 i = 0; i < count; i++)
                    items[i].id = type->itemReference->get(type->item(array, i, type), type->itemReference);
                v.Field(n, items);
                // Readers set every item; the remap changes ids without reading.
                u32 kept = V::kReading ? Kept(array, type, items.Size()) : u32(items.Size());
                for (u32 i = 0; i < kept; i++)
                {
                    void* at = type->item(array, i, type);
                    bool set = V::kReading;
                    if (!set)
                        set = type->itemReference->get(at, type->itemReference) != items[i].id;
                    if (set)
                        type->itemReference->set(at, type->itemReference, items[i].id);
                }
                break;
            }
            default: break;
            }
        }

        // Numbers, bools and guids through an Array<T> of copies.
        template <class T>
        static void Items(V& v, StringView name, void* array, const DynamicArray* type)
        {
            u32 count = type->size(array, type);
            Array<T> items;
            items.Resize(count);
            for (u32 i = 0; i < count; i++)
                Copy(&items[i], type->item(array, i, type));
            v.Field(name, items);
            if constexpr (V::kReading)
            {
                u32 kept = Kept(array, type, items.Size());
                for (u32 i = 0; i < kept; i++)
                    Store(type->item(array, i, type), items[i]);
            }
        }

        // Resizes the array to what was read: the items it holds then (a fixed size array keeps its size).
        static u32 Kept(void* array, const DynamicArray* type, usize read)
        {
            type->resize(array, u32(read), type);
            u32 size = type->size(array, type);
            return size < u32(read) ? size : u32(read);
        }

        template <class T>
        static void Copy(T* to, const void* from)
        {
            if constexpr (std::is_same_v<T, bool>)
                *to = *static_cast<const u8*>(from) != 0;
            else
                std::memcpy(static_cast<void*>(to), from, sizeof(T));
        }
        template <class T>
        static void Store(void* to, const T& from)
        {
            if constexpr (std::is_same_v<T, bool>)
                *static_cast<u8*>(to) = from ? 1 : 0;
            else
                std::memcpy(to, static_cast<const void*>(&from), sizeof(T));
        }

        static i32 Version(void* self) { return Of(self).Version(); }

        static void ObjectReference(void* self, u64* object)
        {
            if constexpr (requires(V& v, u64& o) { v.ObjectReference(o); })
                Of(self).ObjectReference(*object);
        }

        static constexpr DynamicVisitorTable kTable = {&Value, &Text, &Reference, &Structure, &Sequence, &Version, &ObjectReference};
    };

    // -------------------------------------------------------------------------------------------- the C++ side

    template <bool Reading, bool Text>
    class DynamicFields;

    // Runs `f` with the C++ visitor of `v` (reading and text known at compile time).
    template <class F>
    void VisitDynamic(DynamicVisitor& v, F&& f);

    template <class T>
    void TransferDynamicStruct(void* object, DynamicVisitor* visitor, const DynamicStruct*)
    {
        VisitDynamic(*visitor, [object](auto& f) { TransferStruct(f, *static_cast<T*>(object)); });
    }

    // The descriptor of a C++ type with Transfer (or TransferTraits).
    template <class T>
    const DynamicStruct& DynamicStructOf()
    {
        static const DynamicStruct type = {TypeNameOf<T>().Data(), u32(TypeNameOf<T>().Size()), FlowOf<T>() ? kDynamicFlow : 0u, VersionOf<T>(), &TransferDynamicStruct<T>,
                                           nullptr};
        return type;
    }

    // helix::String.
    inline const DynamicString& DynamicStringOfString()
    {
        static const DynamicString type = {
            [](const void* s, const DynamicString*, const char** data, u32* size) {
                const String& text = *static_cast<const String*>(s);
                *data = text.Data();
                *size = u32(text.Size());
            },
            [](void* s, const DynamicString*, const char* data, u32 size) { *static_cast<String*>(s) = StringView(data, size); },
            nullptr};
        return type;
    }

    template <class T>
    const DynamicReference& DynamicReferenceOf()
    {
        static const DynamicReference type = {
            [](const void* r, const DynamicReference*) -> u64 { return ObjectReferenceTraits<T>::Id(*static_cast<const T*>(r)); },
            [](void* r, const DynamicReference*, u64 id) { ObjectReferenceTraits<T>::Set(*static_cast<T*>(r), id); }, nullptr};
        return type;
    }

    template <class T>
    constexpr u32 DynamicItemKind()
    {
        if constexpr (DynamicObjectReference<T>)
            return kDynamicReference;
        else
        {
            constexpr ValueKind kind = KindOf<T>();
            static_assert(kind != ValueKind::Array && kind != ValueKind::FixedArray, "arrays of arrays are not fields of scripts");
            return u32(kind);
        }
    }

    // The descriptor of a helix::Array<T>.
    template <class T>
    const DynamicArray& DynamicArrayOf()
    {
        static const DynamicArray type = [] {
            DynamicArray a = {};
            a.itemKind = DynamicItemKind<T>();
            if constexpr (DynamicObjectReference<T>)
                a.itemReference = &DynamicReferenceOf<T>();
            else if constexpr (KindOf<T>() == ValueKind::String)
                a.itemString = &DynamicStringOfString();
            else if constexpr (KindOf<T>() == ValueKind::Struct)
                a.itemStruct = &DynamicStructOf<T>();
            a.size = [](const void* array, const DynamicArray*) { return u32(static_cast<const Array<T>*>(array)->Size()); };
            a.resize = [](void* array, u32 count, const DynamicArray*) { static_cast<Array<T>*>(array)->Resize(count); };
            a.item = [](void* array, u32 index, const DynamicArray*) -> void* { return &(*static_cast<Array<T>*>(array))[index]; };
            a.prototype = [](const DynamicArray*) -> void* {
                static T item = T();
                return &item;
            };
            return a;
        }();
        return type;
    }

    // The descriptor of a T[N]: N items, resizing keeps them (files hold it as an array, a count and the items).
    template <class T, usize N>
    const DynamicArray& DynamicFixedArrayOf()
    {
        static const DynamicArray type = [] {
            DynamicArray a = DynamicArrayOf<T>();
            a.size = [](const void*, const DynamicArray*) { return u32(N); };
            a.resize = [](void*, u32, const DynamicArray*) {};
            a.item = [](void* array, u32 index, const DynamicArray*) -> void* { return static_cast<T*>(array) + index; };
            return a;
        }();
        return type;
    }

    // The visitor generated code and C++ types drive: every field goes to the table.
    template <bool Reading, bool Text>
    class DynamicFields
    {
    public:
        static constexpr bool kReading = Reading;
        static constexpr bool kText = Text;

        explicit DynamicFields(DynamicVisitor& v) : v_(v) {}

        i32 Version() const { return v_.table->version(v_.self); }
        void ObjectReference(u64& object) { v_.table->objectReference(v_.self, &object); }

        template <class T>
        void Field(StringView name, T& value)
        {
            const char* n = name.Data();
            u32 size = u32(name.Size());
            if constexpr (DynamicObjectReference<T>)
            {
                u64 id = ObjectReferenceTraits<T>::Id(value), before = id;
                v_.table->reference(v_.self, n, size, &id);
                if (id != before)
                    ObjectReferenceTraits<T>::Set(value, id);
            }
            else if constexpr (std::is_enum_v<T>)
            {
                auto raw = std::underlying_type_t<T>(value);
                Field(name, raw);
                if constexpr (Reading)
                    value = T(raw);
            }
            else
            {
                constexpr ValueKind kind = KindOf<T>();
                if constexpr (kind == ValueKind::Bool)
                {
                    u8 b = value ? 1 : 0;
                    v_.table->value(v_.self, n, size, u32(kind), &b);
                    value = b != 0;
                }
                else if constexpr (IsNumeric(kind) || kind == ValueKind::Guid)
                    v_.table->value(v_.self, n, size, u32(kind), &value);
                else if constexpr (kind == ValueKind::String)
                    v_.table->string(v_.self, n, size, &value, &DynamicStringOfString());
                else if constexpr (kind == ValueKind::Array)
                    v_.table->array(v_.self, n, size, &value, &DynamicArrayOf<typename detail::IsArray<T>::Item>());
                else if constexpr (kind == ValueKind::FixedArray)
                    v_.table->array(v_.self, n, size, &value[0], &DynamicFixedArrayOf<std::remove_extent_t<T>, std::extent_v<T>>());
                else
                    v_.table->structure(v_.self, n, size, &value, &DynamicStructOf<T>());
            }
        }

    private:
        DynamicVisitor& v_;
    };

    template <class F>
    void VisitDynamic(DynamicVisitor& v, F&& f)
    {
        switch (v.flags & (kDynamicReading | kDynamicText))
        {
        case 0:
        {
            DynamicFields<false, false> d(v);
            f(d);
            break;
        }
        case kDynamicReading:
        {
            DynamicFields<true, false> d(v);
            f(d);
            break;
        }
        case kDynamicText:
        {
            DynamicFields<false, true> d(v);
            f(d);
            break;
        }
        default:
        {
            DynamicFields<true, true> d(v);
            f(d);
            break;
        }
        }
    }
}
