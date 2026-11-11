#include <Helix/Serialization/SerializedFile/SerializedFile.h>

#include <algorithm>

namespace helix::serialization
{
    namespace
    {
        constexpr char kMagic[4] = {'H', 'X', 'S', 'F'};
        constexpr u32 kFlagTypeTrees = 1;

        struct Header
        {
            char magic[4];
            u32 version;
            u32 flags;
            u32 typeCount;
            u32 objectCount;
            u32 externalCount;
            u64 metadataSize;
            u64 dataOffset;
            u64 dataSize;
            u64 fileSize;
            u64 reserved;
        };
        struct TypeEntry
        {
            i32 classID;
            u32 treeOffset;
            u32 treeSize;
            u32 reserved;
            u8 hash[16];
        };
        struct ObjectEntry
        {
            i64 fileID;
            u32 type;
            u32 reserved;
            u64 offset;
            u64 size;
        };
        struct ExternalEntry
        {
            u8 guid[16];
            i32 type;
            u32 pathOffset;
            u32 pathSize;
            u32 reserved;
        };
        static_assert(sizeof(Header) == 64 && sizeof(TypeEntry) == 32 && sizeof(ObjectEntry) == 32 && sizeof(ExternalEntry) == 32);

        template <class T>
        void Put(Array<u8>& out, const T& value)
        {
            std::memcpy(out.AppendUninitialized(sizeof(T)), &value, sizeof(T));
        }

        template <class T>
        T Get(const u8* at)
        {
            T value;
            std::memcpy(&value, at, sizeof(T));
            return value;
        }
    }

    u32 SerializedFileWriter::Type(i32 classID, const TypeTree& tree, const Md5Digest& hash)
    {
        for (u32 i = 0; i < types_.Size(); i++)
            if (types_[i].classID == classID && types_[i].hash == hash)
                return i;
        SerializedType& t = types_.Emplace();
        t.classID = classID;
        t.hash = hash;
        if (typeTrees_)
            t.tree = tree;
        return u32(types_.Size() - 1);
    }

    void SerializedFileWriter::AddExternal(const Guid& guid, i32 type, StringView path) { externals_.Push({guid, type, String(path)}); }

    bool SerializedFileWriter::Finish(Array<u8>& out, String& error)
    {
        Array<SerializedObject> objects = objects_;
        std::sort(objects.begin(), objects.end(), [](const SerializedObject& a, const SerializedObject& b) { return a.fileID < b.fileID; });
        for (usize i = 1; i < objects.Size(); i++)
            if (objects[i].fileID == objects[i - 1].fileID)
                return error = Format("two objects with fileID {}", objects[i].fileID), false;

        Array<u8> blob;
        Array<TypeEntry> types;
        for (const SerializedType& t : types_)
        {
            TypeEntry e = {};
            e.classID = t.classID;
            std::memcpy(e.hash, t.hash.bytes, 16);
            if (typeTrees_)
            {
                e.treeOffset = u32(blob.Size());
                t.tree.Write(blob);
                e.treeSize = u32(blob.Size() - e.treeOffset);
                while (blob.Size() % 4 != 0)
                    blob.Push(u8(0));
            }
            types.Push(e);
        }
        Array<ExternalEntry> externals;
        for (const ExternalReference& r : externals_)
        {
            ExternalEntry e = {};
            std::memcpy(e.guid, r.guid.bytes, 16);
            e.type = r.type;
            e.pathOffset = u32(blob.Size());
            e.pathSize = u32(r.path.Size());
            blob.Append(reinterpret_cast<const u8*>(r.path.Data()), r.path.Size());
            externals.Push(e);
        }

        Header h = {};
        std::memcpy(h.magic, kMagic, 4);
        h.version = kSerializedFileVersion;
        h.flags = typeTrees_ ? kFlagTypeTrees : 0;
        h.typeCount = u32(types.Size());
        h.objectCount = u32(objects.Size());
        h.externalCount = u32(externals.Size());
        h.metadataSize = sizeof(Header) + 32 * (u64(h.typeCount) + h.objectCount + h.externalCount) + blob.Size();
        h.dataOffset = (h.metadataSize + 15) & ~u64(15);
        h.dataSize = data_.Size();
        h.fileSize = h.dataOffset + h.dataSize;

        out.Clear();
        out.Reserve(usize(h.fileSize));
        Put(out, h);
        for (const TypeEntry& e : types)
            Put(out, e);
        for (const SerializedObject& o : objects)
            Put(out, ObjectEntry{o.fileID, o.type, 0, o.offset, o.size});
        for (const ExternalEntry& e : externals)
            Put(out, e);
        out.Append(blob.Data(), blob.Size());
        while (out.Size() < h.dataOffset)
            out.Push(u8(0));
        out.Append(data_.Data(), data_.Size());
        return true;
    }

    bool SerializedFile::Open(Span<const u8> bytes, String& error)
    {
        types_.Clear();
        objects_.Clear();
        externals_.Clear();
        data_ = Span<const u8>();
        if (bytes.Size() < sizeof(Header))
            return error = "a file shorter than the header", false;
        Header h = Get<Header>(bytes.Data());
        if (std::memcmp(h.magic, kMagic, 4) != 0)
            return error = "not a serialized file", false;
        if (h.version != kSerializedFileVersion)
            return error = Format("format version {}, this reader knows {}", h.version, kSerializedFileVersion), false;
        if (h.flags & ~kFlagTypeTrees)
            return error = Format("unknown flags {}", h.flags), false;
        u64 tables = sizeof(Header) + 32 * (u64(h.typeCount) + h.objectCount + h.externalCount);
        if (h.fileSize != bytes.Size() || h.metadataSize < tables || h.metadataSize > h.dataOffset || h.dataOffset > h.fileSize ||
            h.dataSize != h.fileSize - h.dataOffset || h.dataOffset % 16 != 0)
            return error = "header sizes that do not fit the file", false;
        typeTrees_ = (h.flags & kFlagTypeTrees) != 0;
        const u8* blob = bytes.Data() + tables;
        u64 blobSize = h.metadataSize - tables;
        auto inBlob = [&](u32 offset, u32 size) { return u64(offset) + size <= blobSize; };

        const u8* at = bytes.Data() + sizeof(Header);
        types_.Reserve(h.typeCount);
        for (u32 i = 0; i < h.typeCount; i++, at += 32)
        {
            TypeEntry e = Get<TypeEntry>(at);
            SerializedType& t = types_.Emplace();
            t.classID = e.classID;
            std::memcpy(t.hash.bytes, e.hash, 16);
            if (typeTrees_)
            {
                if (!inBlob(e.treeOffset, e.treeSize))
                    return error = Format("type {}: the type tree is outside the metadata", i), false;
                String treeError;
                if (!TypeTree::Read(Span<const u8>(blob + e.treeOffset, e.treeSize), t.tree, treeError))
                    return error = Format("type {}: {}", i, treeError), false;
            }
            else if (e.treeSize != 0)
                return error = Format("type {}: a type tree in a file without type trees", i), false;
        }
        objects_.Reserve(h.objectCount);
        for (u32 i = 0; i < h.objectCount; i++, at += 32)
        {
            ObjectEntry e = Get<ObjectEntry>(at);
            if (e.type >= h.typeCount)
                return error = Format("object {}: type {} of {}", i, e.type, h.typeCount), false;
            if (e.offset > h.dataSize || e.size > h.dataSize - e.offset)
                return error = Format("object {}: data outside the file", i), false;
            if (i > 0 && e.fileID <= objects_.Back().fileID)
                return error = Format("object {}: fileID {} after {}", i, e.fileID, objects_.Back().fileID), false;
            objects_.Push({e.fileID, e.type, e.offset, e.size});
        }
        externals_.Reserve(h.externalCount);
        for (u32 i = 0; i < h.externalCount; i++, at += 32)
        {
            ExternalEntry e = Get<ExternalEntry>(at);
            if (!inBlob(e.pathOffset, e.pathSize))
                return error = Format("external reference {}: the path is outside the metadata", i), false;
            ExternalReference& r = externals_.Emplace();
            std::memcpy(r.guid.bytes, e.guid, 16);
            r.type = e.type;
            r.path = StringView(reinterpret_cast<const char*>(blob + e.pathOffset), e.pathSize);
        }
        data_ = Span<const u8>(bytes.Data() + h.dataOffset, usize(h.dataSize));
        return true;
    }

    u32 SerializedFile::Find(i64 fileID) const
    {
        const SerializedObject* it =
            std::lower_bound(objects_.begin(), objects_.end(), fileID, [](const SerializedObject& o, i64 id) { return o.fileID < id; });
        return it != objects_.end() && it->fileID == fileID ? u32(it - objects_.begin()) : kNoObject;
    }
}
