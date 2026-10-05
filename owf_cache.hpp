#pragma once

#include <cstdint>

struct TocHeader
{
	uint32_t magic;
	uint32_t version;
};

struct TocEntry
{
	uint64_t cacheOffset;
	uint64_t timestamp;
	uint32_t compressedLen;
	uint32_t length;
	uint32_t reserved;
	uint32_t parentDirIndex;
	char name[64];
};

struct TocFile
{
	TocHeader header;
	TocEntry entries[1];
};

struct CachePair
{
	TocFile* toc;
	size_t toc_size;
	const void* cache;
	size_t cache_size;

	CachePair(const std::string& base)
		: toc((TocFile*)soup::filesystem::createFileMapping(base + ".toc", toc_size)),
		  cache(soup::filesystem::createFileMapping(base + ".cache", cache_size))
	{
	}

	~CachePair()
	{
		soup::filesystem::destroyFileMapping(toc, toc_size);
		soup::filesystem::destroyFileMapping(cache, cache_size);
	}

	[[nodiscard]] uint32_t findIndex(const char* name, size_t len, uint32_t parent) const noexcept
	{
		const uint32_t num_entries = (toc_size - sizeof(TocHeader)) / sizeof(TocEntry);
		for (uint32_t i = 0; i != num_entries; ++i)
		{
			if (toc->entries[i].parentDirIndex == parent
				&& memcmp(toc->entries[i].name, name, len) == 0 && toc->entries[i].name[len] == '\0'
				&& toc->entries[i].timestamp != 0 // Ignore deleted files
				)
			{
				return 1 + i;
			}
		}
		return 0;
	}

	[[nodiscard]] uint32_t findIndex(const char* path, size_t len) const noexcept
	{
		++path; // Skip '/'
		--len;

		uint32_t parent = 0;
		for (const char* sep; (sep = strchr(path, '/')) != nullptr; path = sep + 1)
		{
			parent = findIndex(path, sep - path, parent);
			len -= (sep - path) + 1;
		}
		return findIndex(path, len, parent);
	}

	[[nodiscard]] TocEntry* findEntry(const char* path, size_t len) const noexcept
	{
		if (auto i = findIndex(path, len))
		{
			return &toc->entries[i - 1];
		}
		return nullptr;
	}
};
inline std::unordered_map<uint32_t, CachePair*> open_cache_pairs;

struct CacheManifest
{
	struct Entry
	{
		char hash[16];
		char unk[4];
	};

	std::unordered_map<std::string, Entry> entries;
	std::unordered_map<std::string, Entry> stripped_entries;
};
