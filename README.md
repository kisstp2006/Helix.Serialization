# Helix.Serialization

Serialization of the Helix engine: the YAML dialect of scenes, prefabs and assets, JSON, the Transfer system that
writes and reads types as YAML and binary, type trees, and binary serialized files. All code is own.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Serialization/Yaml/Tree.h` | `Tree`: one array of 24 byte `Node`s, one array of children, the documents and directives; lookups by key, items by index, `AsInt`, `AsFloat` |
| `Source/Helix/Serialization/Yaml/Parser.h` | `Parse(text, tree, error)` |
| `Source/Helix/Serialization/Yaml/Emitter.h` | `Emitter`, a streaming writer of the dialect, and `Write(tree, out)` |
| `Source/Helix/Serialization/Json/Tree.h`, `Parser.h`, `Writer.h` | The same flat tree for JSON, `Parse` (RFC 8259), a compact or pretty `Writer`, `Write(tree, out)` |
| `Source/Helix/Serialization/Transfer/Traits.h` | How a type declares `Transfer`, `ValueKind`, `AssetReference` |
| `Source/Helix/Serialization/Transfer/Yaml.h` | `YamlWrite`, `YamlRead`, `WriteYaml`, `ReadYaml` |
| `Source/Helix/Serialization/Transfer/Binary.h` | `BinaryWrite`, `BinaryRead`, `SafeBinaryRead`, `WriteBinary`, `ReadBinary` |
| `Source/Helix/Serialization/Transfer/TypeTree.h` | `TypeTree`, `TypeTreeOf<T>()`, `TypeHashOf<T>()` |
| `Source/Helix/Serialization/SerializedFile/SerializedFile.h` | `SerializedFileWriter`, `SerializedFile` |
| `Tests/`, `Benchmarks/`, `Fuzz/` | Scenes, prefabs, every scalar and container form, damaged input; throughput; fuzz targets for every reader |

## The dialect

```yaml
%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &1000000
GameObject:
  m_ObjectHideFlags: 0
  m_Component:
  - component: {fileID: 1000001}
  m_Name: Cube
  m_TagString: Untagged
--- !h!4 &1000001 stripped
Transform:
  m_LocalPosition: {x: 0, y: 1.5, z: -2}
  m_Children: []
```

- A stream of documents, each started by `---` with an optional `!h!<classID>` tag, an `&<fileID>` anchor and the
  word `stripped`.
- Block mappings, block sequences at the indentation of their key or deeper, sequences of mappings, flow mappings and
  sequences (nested and empty).
- Plain, single quoted and double quoted scalars, with escapes and continuation lines; comments and blank lines.
- `\n` and `\r\n` line endings.

## Reading

```cpp
using namespace helix::serialization::yaml;

Tree tree;
helix::String error;
if (!Parse(text, tree, error))
    Log(error); // "line 12, column 7: ..."
for (const Document& d : tree.documents)
{
    u32 body = tree.Value(d.root, 0);
    i64 layer = 0;
    tree.AsInt(tree.Find(body, "m_Layer"), layer);
}
```

The parser does not copy text: scalars are views into `text`, which must outlive the tree. Only scalars that change
when read (escapes, folded lines) are copied into the tree's own buffer. The children of a container are contiguous in
`Tree::children`, a mapping's keys and values interleaved. Parsing into a tree again reuses its memory.

Damaged text is an error that names the line and column; the parser never reads outside `text`.

## Writing

```cpp
helix::String out;
Emitter e(out);
e.Directives();
e.BeginDocument(1, 1000000);
e.BeginMapping();
e.Key("GameObject");
e.BeginMapping();
e.Key("m_Name");
e.Scalar("Cube");
e.EndMapping();
e.EndMapping();
```

Scalars are plain unless they would read back as something else, then single quoted, or double quoted when they hold
line breaks or control characters. Floats are written as the shortest text that reads back as the same value.
`Write(tree, out)` writes a parsed tree: its output parses back to the same tree, and a tree written twice gives the
same text.

## JSON

`json::Parse` reads one value by RFC 8259: UTF-8 is checked, `\u` escapes with surrogate pairs become UTF-8, numbers
follow the grammar and keep their text (`AsInt`, `AsUInt`, `AsDouble` convert), containers nest up to 512 deep. Errors
name the line and column. `json::Writer` writes compact text or pretty text with four spaces; NaN and the infinities,
which JSON has no number for, are written as the strings `"NaN"`, `"Infinity"` and `"-Infinity"`, which `AsDouble` reads.

## Transfer

A type names its fields once, and every visitor uses that list:

```cpp
struct Mover
{
    static constexpr helix::StringView kTypeName = "Mover";
    static constexpr helix::i32 kVersion = 2;        // optional; written as serializedVersion when above 1

    helix::serialization::AssetReference m_Target;  // {fileID: 0} or {fileID: ..., guid: ..., type: ...}
    float m_Speed = 1;
    helix::Array<helix::String> m_Tags;

    template <class V>
    void Transfer(V& v)
    {
        v.Field("m_Target", m_Target);
        v.Field(v.Version() < 2 ? "m_Velocity" : "m_Speed", m_Speed);  // renamed in version 2
        v.Field("m_Tags", m_Tags);
    }
};
```

Fields are numbers, bools, enums, `String`, `Guid`, `Array<T>`, fixed arrays and types with `Transfer`; `kFlow = true`
writes a type as `{x: 0, y: 0}`. The visitors:

| Visitor | Does |
|---|---|
| `YamlWrite` | Into the emitter: bools as 0 and 1, a `Guid` as 32 hex digits, `Array<u8>` as one hex string |
| `YamlRead` | From a mapping: missing fields keep their value, unknown ones are skipped, wrong ones are errors with their line |
| `BinaryWrite`, `BinaryRead` | Fields in order, little endian; strings and arrays as a u32 count and the data, padded to 4 bytes |
| `SafeBinaryRead` | Binary data through the type tree it was written with: fields by name, numbers converted |
| `TypeTreeBuild` | The type tree: one node per field with kind, name, size, version and flags, hashed with MD5 |

Transfer must visit the same fields whatever the values are, except that text may leave out fields (as `AssetReference`
does for local references).

## Serialized files

`SerializedFileWriter` collects objects (fileID, class ID, the object) and external references (guid, type, path) and
writes one file: a 64 byte header, tables of types, objects and externals, the type trees and paths, then the object
data. Editor files keep the type trees, so objects of a type that changed since read through `SafeBinaryRead`; player
files leave them out and need the same type hash. `SerializedFile::Open` checks every count, offset and size, the order
of the fileIDs and every type tree before anything is read.

## Speed

Release, one thread:

| Work | Speed |
|---|---|
| YAML parse, a generated scene of 32768 objects (22 MB), into a tree that is reused | about 230 MB/s (about 150 MB/s into a new tree) |
| YAML write of that scene | about 95 MB/s |
| JSON parse of a 20 MB scene | about 450 MB/s |
| JSON write, pretty | about 260 MB/s |
| Binary write / read / read through the type tree, 16384 objects with every field kind | about 890 / 320 / 100 MB/s |
| YAML transfer write / parse and read, the same objects | about 90 / 105 MB/s |

## Building

The repository builds with the presets of Helix.CMake (Windows, Web, Android); the submodules in `External/` are
Helix.CMake, Helix.Testing and Helix.Core.

## License

GPL-3.0, see `LICENSE`.
