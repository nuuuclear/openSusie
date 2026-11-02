#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

class FileSystem {
public:
    using Path = std::filesystem::path;

    void set(const std::string& name,const Path& path);

    const Path& get(const std::string& name) const;

    Path resolve(const std::string& name,const Path& relativePath = {}) const;

private:
    std::unordered_map<std::string, Path> paths;
};