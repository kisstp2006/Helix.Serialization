#pragma once

// YAML text of the Helix dialect used by the tests, the fuzz targets and the benchmarks.
namespace helix::serialization::testing
{
    // A small scene: a game object with a transform, a script with data of every kind, a stripped prefab instance with a
    // modification. The empty m_Name keeps its space after the colon, as the writer of the format leaves it.
    inline constexpr const char kScene[] = R"(%YAML 1.1
%TAG !h! tag:helixengine,2026:
--- !h!1 &1053261458
GameObject:
  m_ObjectHideFlags: 0
  m_CorrespondingSourceObject: {fileID: 0}
  serializedVersion: 6
  m_Component:
  - component: {fileID: 1053261461}
  - component: {fileID: 1053261460}
  m_Layer: 0
  m_Name: Main Camera
  m_TagString: MainCamera
  m_Icon: {fileID: 0}
  m_IsActive: 1
--- !h!4 &1053261461
Transform:
  m_GameObject: {fileID: 1053261458}
  serializedVersion: 2
  m_LocalRotation: {x: 0, y: 0, z: 0, w: 1}
  m_LocalPosition: {x: 0, y: 1, z: -10}
  m_LocalScale: {x: 1, y: 1, z: 1}
  m_Children: []
  m_Father: {fileID: 0}
--- !h!114 &11400000
MonoBehaviour:
  m_Script: {fileID: 11500000, guid: 4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e, type: 3}
  m_Name:
  m_Text: 'It''s a test: with a colon'
  m_Multi: "First line\nSecond\ttabbed \"quoted\" é"
  m_Folded: 'a long string
    that goes on

    over lines'
  m_Plain: a plain value
    that folds  # and a comment
  values:
  - 1
  - -2.5
  - 3e-07
  - 0x1F
  nested:
    list:
    - name: first
      weight: 0.25
    - name: second
      weight: 0.75
    - - inner
      - [1, 2, {deep: [3, 4]}]
    empty: {}
    none:
--- !h!1001 &700000 stripped
PrefabInstance:
  m_Modification:
    m_Modifications:
    - target: {fileID: 400000, guid: 0123456789abcdef0123456789abcdef, type: 3}
      propertyPath: m_LocalPosition.x
      value: 5
      objectReference: {fileID: 0}
    m_RemovedComponents: []
)";

    // JSON with every kind of value, escapes, nesting and number forms.
    inline constexpr const char kJson[] = R"({
    "name": "Main Camera",
    "enabled": true,
    "parent": null,
    "position": {"x": 0, "y": 1.5, "z": -10},
    "tags": ["a", "b\n\"c\"", "é中😀", "", "\u0001\/"],
    "matrix": [[1, 0], [0, 1e-3], []],
    "empty": {},
    "big": 18446744073709551615,
    "neg": -9223372036854775808,
    "exp": 2.5E+3,
    "nested": {"list": [{"weight": 0.75, "on": false}, "inner", -0.0]}
})";
}
