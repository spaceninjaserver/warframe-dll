#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <fwd.hpp>
#include <Mutex.hpp>

class owfRepo
{
protected:
	bool loadArchive(const char* data, size_t size);
public:
	void loadBuiltinArchive();
	static bool readHotfixHeader(const char* data, size_t size, uint32_t& hash);
	bool loadHotfix(const char* data, size_t size);

	const char* find(uint32_t key, size_t& out_len) const;
	uint64_t getVersionedU64(uint32_t path, uint64_t ver) const;
	int64_t getVersionedI64(uint32_t path, uint64_t ver) const;
	soup::Pattern getVersionedPattern(uint32_t path, uint64_t ver) const;
	std::unordered_map<std::string, std::string> getDict(const std::string& type, const std::string& lang) const;
	const char* /*[16]*/ getExpectedCodeVersionForManifestHash(const char manifest_hash[22]) const;

	uint8_t hotfix;
	uint32_t hash;
protected:
	std::unordered_map<uint32_t, std::string> data;
};

inline soup::Mutex g_repo_mtx;
inline owfRepo g_repo;

inline std::unordered_map<std::string, std::string> g_core_dict;
inline std::unordered_map<std::string, std::string> g_overlay_dict;
extern std::string get_string(const std::unordered_map<std::string, std::string>& dict, std::string key);
inline std::string get_core_string(std::string key) { return get_string(g_core_dict, std::move(key)); }
inline std::string get_overlay_string(std::string key) { return get_string(g_overlay_dict, std::move(key)); }
