#pragma once

#include <Helix/Serialization/Transfer/Binary.h>

// A binary serialized file: the objects of a scene or an asset in the binary form of Transfer.
//
//   header      64 bytes: "HXSF", format version, flags (bit 0: type trees), the counts of types, objects and
//               external references, the metadata size, the data offset and size, the file size
//   types       32 bytes each: class ID, type tree offset and size in the blob (0 without type trees), type hash
//   objects     32 bytes each, in fileID order: fileID, type index, data offset and size
//   externals   32 bytes each: guid, type, path offset and size in the blob
//   blob        type trees and paths
//   data        at a multiple of 16 bytes; each object at a multiple of 8 from the data start
//
// All numbers little endian. Editor files keep the type trees, so data written by an older version of a type still
// reads; player files leave them out, and a type must then have the hash it was written with.
namespace helix::serialization
{
    inline constexpr u32 kSerializedFileVersion = 1;
    inline constexpr u32 kNoObject = ~0u;

    struct SerializedType
    {
        i32 classID = 0;
        Md5Digest hash = {};
        // Empty when the file has no type trees.
        TypeTree tree;
    };

    struct SerializedObject
    {
        i64 fileID = 0;
        u32 type = 0;
        u64 offset = 0;
        u64 size = 0;
    };

    struct ExternalReference
    {
        Guid guid;
        i32 type = 0;
        String path;
    };

    class SerializedFileWriter
    {
    public:
        explicit SerializedFileWriter(bool typeTrees = true) : typeTrees_(typeTrees) {}

        template <class T>
        void Add(i64 fileID, i32 classID, T& object)
        {
            u32 type = Type(classID, TypeTreeOf<T>(), TypeHashOf<T>());
            while (data_.Size() % 8 != 0)
                data_.Push(u8(0));
            usize at = data_.Size();
            WriteBinary(object, data_);
            objects_.Push({fileID, type, at, data_.Size() - at});
        }

        void AddExternal(const Guid& guid, i32 type, StringView path);

        // The file, its objects in fileID order. Two objects with one fileID are an error.
        bool Finish(Array<u8>& out, String& error);

    private:
        u32 Type(i32 classID, const TypeTree& tree, const Md5Digest& hash);

        bool typeTrees_;
        Array<SerializedType> types_;
        Array<SerializedObject> objects_;
        Array<ExternalReference> externals_;
        Array<u8> data_;
    };

    // Reads a serialized file held in memory (the bytes must outlive the reader). Open checks every count, offset and
    // size, the order of the fileIDs and each type tree, so the objects can be read without further checks of the file.
    class SerializedFile
    {
    public:
        bool Open(Span<const u8> bytes, String& error);

        bool HasTypeTrees() const { return typeTrees_; }
        const Array<SerializedType>& Types() const { return types_; }
        const Array<SerializedObject>& Objects() const { return objects_; }
        const Array<ExternalReference>& Externals() const { return externals_; }

        // The index of the object with this fileID, kNoObject when there is none.
        u32 Find(i64 fileID) const;
        Span<const u8> Data(u32 object) const { return data_.Subspan(usize(objects_[object].offset), usize(objects_[object].size)); }

        // Reads an object into T: directly when T has the hash the object was written with, through the file's type tree
        // when it has another, an error when it has another and the file has no type trees.
        template <class T>
        bool Read(u32 object, T& out, String& error) const
        {
            const SerializedType& type = types_[objects_[object].type];
            if (type.hash == TypeHashOf<T>())
                return ReadBinary(Data(object), out, error);
            if (!typeTrees_)
                return error = Format("object {} was written with another layout of {} and the file has no type trees", objects_[object].fileID, TypeNameOf<T>()),
                       false;
            return ReadBinary(type.tree, Data(object), out, error);
        }

    private:
        bool typeTrees_ = false;
        Array<SerializedType> types_;
        Array<SerializedObject> objects_;
        Array<ExternalReference> externals_;
        Span<const u8> data_;
    };
}
