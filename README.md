# Helix.Serialization

Serialization of the Helix engine: the YAML dialect of scenes, prefabs and assets, read into a flat node tree and
written back. All code is own.

## Contents

| Path | Content |
|---|---|
| `Source/Helix/Serialization/Yaml/Tree.h` | `Tree`: one array of 24 byte `Node`s, one array of children, the documents and directives; lookups by key, items by index, `AsInt`, `AsFloat` |
| `Source/Helix/Serialization/Yaml/Parser.h` | `Parse(text, tree, error)` |
| `Source/Helix/Serialization/Yaml/Emitter.h` | `Emitter`, a streaming writer of the dialect, and `Write(tree, out)` |
| `Tests/`, `Benchmarks/`, `Fuzz/` | Scenes, prefabs, every scalar and container form, damaged text; parse and write throughput; a round trip fuzz target |

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

## Speed

On a generated scene of 32768 objects (22 MB, release, one thread): parsing about 220 MB/s into a tree that is
reused, about 150 MB/s into a new one; writing about 95 MB/s.

## Building

The repository builds with the presets of Helix.CMake (Windows, Web, Android); the submodules in `External/` are
Helix.CMake, Helix.Testing and Helix.Core.

## License

GPL-3.0, see `LICENSE`.
