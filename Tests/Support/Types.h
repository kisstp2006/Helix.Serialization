#pragma once

#include <Helix/Serialization/Transfer/Traits.h>

// Types with Transfer for the tests, the fuzz targets and the benchmarks: one field of every kind.
namespace helix::serialization::testing
{
    enum class Mode : i32
    {
        Off,
        On,
        Auto,
    };

    struct Vector3
    {
        static constexpr StringView kTypeName = "Vector3";
        static constexpr bool kFlow = true;

        float x = 0, y = 0, z = 0;

        bool operator==(const Vector3&) const = default;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("x", x);
            v.Field("y", y);
            v.Field("z", z);
        }
    };

    struct Waypoint
    {
        static constexpr StringView kTypeName = "Waypoint";
        static constexpr bool kFlow = true;

        i32 x = 0, y = 0;

        bool operator==(const Waypoint&) const = default;

        template <class V>
        void Transfer(V& v)
        {
            v.Field("x", x);
            v.Field("y", y);
        }
    };

    template <class T>
    bool Same(const Array<T>& a, const Array<T>& b)
    {
        if (a.Size() != b.Size())
            return false;
        for (usize i = 0; i < a.Size(); i++)
        {
            if constexpr (requires { Same(a[i], b[i]); })
            {
                if (!Same(a[i], b[i]))
                    return false;
            }
            else if (!(a[i] == b[i]))
                return false;
        }
        return true;
    }

    struct Behaviour
    {
        static constexpr StringView kTypeName = "Behaviour";
        static constexpr i32 kVersion = 2;

        AssetReference m_Script;
        AssetReference m_GameObject;
        bool m_Enabled = true;
        u8 m_Layer = 0;
        i16 m_Small = -5;
        u32 m_Mask = 0xFFFFFFFF;
        i64 m_Big = 0;
        u64 m_Huge = 0;
        float m_Speed = 0;
        double m_Precise = 0;
        Mode m_Mode = Mode::Off;
        String m_Label;
        Guid m_Id;
        Vector3 m_Position;
        i32 m_Fixed[3] = {};
        Array<Waypoint> m_Waypoints;
        Array<u8> m_Bytes;
        Array<String> m_Names;
        Array<Array<i32>> m_Grid;
        Array<AssetReference> m_Refs;

        bool operator==(const Behaviour& o) const
        {
            return m_Script == o.m_Script && m_GameObject == o.m_GameObject && m_Enabled == o.m_Enabled && m_Layer == o.m_Layer &&
                   m_Small == o.m_Small && m_Mask == o.m_Mask && m_Big == o.m_Big && m_Huge == o.m_Huge && m_Speed == o.m_Speed &&
                   m_Precise == o.m_Precise && m_Mode == o.m_Mode && m_Label == o.m_Label && m_Id == o.m_Id && m_Position == o.m_Position &&
                   m_Fixed[0] == o.m_Fixed[0] && m_Fixed[1] == o.m_Fixed[1] && m_Fixed[2] == o.m_Fixed[2] && Same(m_Waypoints, o.m_Waypoints) &&
                   Same(m_Bytes, o.m_Bytes) && Same(m_Names, o.m_Names) && Same(m_Grid, o.m_Grid) && Same(m_Refs, o.m_Refs);
        }

        template <class V>
        void Transfer(V& v)
        {
            v.Field("m_Script", m_Script);
            v.Field("m_GameObject", m_GameObject);
            v.Field("m_Enabled", m_Enabled);
            v.Field("m_Layer", m_Layer);
            v.Field("m_Small", m_Small);
            v.Field("m_Mask", m_Mask);
            v.Field("m_Big", m_Big);
            v.Field("m_Huge", m_Huge);
            v.Field("m_Speed", m_Speed);
            v.Field("m_Precise", m_Precise);
            v.Field("m_Mode", m_Mode);
            v.Field("m_Label", m_Label);
            v.Field("m_Id", m_Id);
            v.Field("m_Position", m_Position);
            v.Field("m_Fixed", m_Fixed);
            v.Field("m_Waypoints", m_Waypoints);
            v.Field("m_Bytes", m_Bytes);
            v.Field("m_Names", m_Names);
            v.Field("m_Grid", m_Grid);
            v.Field("m_Refs", m_Refs);
        }
    };

    // A Behaviour with a value in every field.
    inline Behaviour FilledBehaviour(i32 seed = 0)
    {
        Behaviour b;
        b.m_Script = {11500000, {}, 3};
        Guid::TryParse("4f2b3c1d9e8a7b6c5d4e3f2a1b0c9d8e", b.m_Script.guid);
        b.m_GameObject = {1053261458 + seed, {}, 0};
        b.m_Enabled = false;
        b.m_Layer = 31;
        b.m_Small = -32768;
        b.m_Mask = 0x80000001u;
        b.m_Big = -9223372036854775807ll - 1;
        b.m_Huge = 18446744073709551615ull;
        b.m_Speed = 3.75f + float(seed);
        b.m_Precise = 0.1;
        b.m_Mode = Mode::Auto;
        b.m_Label = "It's moving: fast";
        Guid::TryParse("00112233445566778899aabbccddeeff", b.m_Id);
        b.m_Position = {1, 2.5f, -3};
        b.m_Fixed[0] = 7, b.m_Fixed[1] = -8, b.m_Fixed[2] = 9;
        b.m_Waypoints = {{0, 0}, {1, 2}, {2, 4}};
        b.m_Bytes = {0x00, 0x7F, 0xFF, 0x10};
        b.m_Names = {String("first"), String(""), String("third one")};
        b.m_Grid.Resize(2);
        b.m_Grid[0] = {1, 2, 3};
        b.m_Refs = {AssetReference{}, AssetReference{42, {}, 0}};
        return b;
    }
}
