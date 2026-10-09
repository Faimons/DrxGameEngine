/**
 * DRX Engine - Resource Manager Implementation
 */

#include "Resource/Public/ResourceManager.h"
#include "Core/Public/Log.h"

#include <fstream>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <filesystem>

#include <stb_image.h>
#include <stb_image_write.h>

namespace DRX::Resource {

// ============================================================================
// ResourceManager Implementation
// ============================================================================

ResourceManager& ResourceManager::Instance() {
    static ResourceManager instance;
    return instance;
}

bool ResourceManager::Initialize(const String& asset_root) {
    asset_root_ = asset_root;

    // Start loader threads
    running_ = true;
    for (uint32 i = 0; i < num_loader_threads_; i++) {
        loader_threads_.emplace_back(&ResourceManager::LoaderThread, this);
    }

    // Scan assets
    ScanAssets();

    DRX_LOG_INFO("ResourceManager initialized with root: {}", asset_root_);
    return true;
}

void ResourceManager::Shutdown() {
    running_ = false;
    queue_cv_.notify_all();

    for (auto& thread : loader_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    loader_threads_.clear();

    UnloadAll();
    loaders_.clear();
    asset_database_.clear();

    DRX_LOG_INFO("ResourceManager shutdown");
}

void ResourceManager::Update() {
    if (hot_reload_enabled_) {
        CheckForChanges();
    }
}

SharedPtr<IResource> ResourceManager::LoadResource(const String& path, ResourceType type) {
    String resolved_path = ResolvePath(path);

    // Check cache
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = cache_.find(resolved_path);
        if (it != cache_.end()) {
            it->second.last_access = std::chrono::steady_clock::now().time_since_epoch().count();
            it->second.ref_count++;

            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.cache_hits++;

            return it->second.resource;
        }
    }

    // Load resource
    IResourceLoader* loader = GetLoader(type);
    if (!loader) {
        loader = GetLoaderForExtension(std::filesystem::path(path).extension().string());
    }

    if (!loader) {
        DRX_LOG_ERROR("No loader found for resource: {}", path);
        return nullptr;
    }

    auto resource = loader->Load(resolved_path);
    if (!resource) {
        DRX_LOG_ERROR("Failed to load resource: {}", path);
        return nullptr;
    }

    // Add to cache
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        CacheEntry entry;
        entry.resource = resource;
        entry.last_access = std::chrono::steady_clock::now().time_since_epoch().count();
        entry.ref_count = 1;
        cache_[resolved_path] = entry;
    }

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.cache_misses++;
        stats_.loaded_resources++;
        stats_.total_memory += resource->GetSize();
    }

    return resource;
}

void ResourceManager::LoadResourceAsync(const String& path, ResourceType type,
                                         std::function<void(SharedPtr<IResource>)> callback) {
    ResourceLoadRequest request;
    request.path = path;
    request.type = type;
    request.callback = callback;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        load_queue_.push(request);
    }
    queue_cv_.notify_one();

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.pending_loads++;
    }
}

void ResourceManager::Unload(const String& path) {
    String resolved_path = ResolvePath(path);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(resolved_path);
    if (it != cache_.end()) {
        it->second.ref_count--;
        if (it->second.ref_count == 0) {
            uint64 size = it->second.resource->GetSize();
            cache_.erase(it);

            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.loaded_resources--;
            stats_.total_memory -= size;
        }
    }
}

void ResourceManager::UnloadUnused() {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.begin();
    while (it != cache_.end()) {
        if (it->second.ref_count == 0) {
            uint64 size = it->second.resource->GetSize();
            it = cache_.erase(it);

            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.loaded_resources--;
            stats_.total_memory -= size;
        } else {
            ++it;
        }
    }
}

void ResourceManager::UnloadAll() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    cache_.clear();

    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.loaded_resources = 0;
    stats_.total_memory = 0;
}

void ResourceManager::EnableHotReload(bool enable) {
    hot_reload_enabled_ = enable;

    if (enable) {
        // Record current timestamps
        for (const auto& [path, entry] : cache_) {
            try {
                auto time = std::filesystem::last_write_time(path);
                file_timestamps_[path] = time.time_since_epoch().count();
            } catch (...) {}
        }
    }
}

void ResourceManager::CheckForChanges() {
    for (auto& [path, timestamp] : file_timestamps_) {
        try {
            auto current_time = std::filesystem::last_write_time(path);
            uint64 current_ts = current_time.time_since_epoch().count();

            if (current_ts != timestamp) {
                timestamp = current_ts;
                ReloadResource(path);
            }
        } catch (...) {}
    }
}

void ResourceManager::ReloadResource(const String& path) {
    std::lock_guard<std::mutex> lock(cache_mutex_);

    auto it = cache_.find(path);
    if (it != cache_.end()) {
        it->second.resource->Reload();
        DRX_LOG_INFO("Hot-reloaded: {}", path);
    }
}

void ResourceManager::RegisterLoader(UniquePtr<IResourceLoader> loader) {
    ResourceType type = loader->GetType();

    for (const auto& ext : loader->GetSupportedExtensions()) {
        extension_map_[ext] = type;
    }

    loaders_[type] = std::move(loader);
}

IResourceLoader* ResourceManager::GetLoader(ResourceType type) const {
    auto it = loaders_.find(type);
    return it != loaders_.end() ? it->second.get() : nullptr;
}

IResourceLoader* ResourceManager::GetLoaderForExtension(const String& extension) const {
    auto it = extension_map_.find(extension);
    if (it == extension_map_.end()) return nullptr;
    return GetLoader(it->second);
}

void ResourceManager::SetCacheSize(uint64 max_bytes) {
    max_cache_size_ = max_bytes;
}

uint64 ResourceManager::GetCacheSize() const {
    return max_cache_size_;
}

uint64 ResourceManager::GetCacheUsed() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_.total_memory;
}

void ResourceManager::ClearCache() {
    UnloadUnused();
}

void ResourceManager::ScanAssets() {
    asset_database_.clear();

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(asset_root_)) {
            if (entry.is_regular_file()) {
                ResourceMetadata meta;
                meta.path = entry.path().string();
                meta.size = entry.file_size();
                meta.last_modified = entry.last_write_time().time_since_epoch().count();
                meta.type = DetectResourceType(meta.path);

                asset_database_[meta.path] = meta;
            }
        }
    } catch (const std::exception& e) {
        DRX_LOG_WARN("Error scanning assets: {}", e.what());
    }

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.total_resources = static_cast<uint32>(asset_database_.size());
    }

    DRX_LOG_INFO("Scanned {} assets", asset_database_.size());
}

Array<ResourceMetadata> ResourceManager::GetAllResources() const {
    Array<ResourceMetadata> result;
    result.reserve(asset_database_.size());
    for (const auto& [path, meta] : asset_database_) {
        result.push_back(meta);
    }
    return result;
}

Array<ResourceMetadata> ResourceManager::GetResourcesByType(ResourceType type) const {
    Array<ResourceMetadata> result;
    for (const auto& [path, meta] : asset_database_) {
        if (meta.type == type) {
            result.push_back(meta);
        }
    }
    return result;
}

bool ResourceManager::ResourceExists(const String& path) const {
    String resolved = ResolvePath(path);
    return asset_database_.find(resolved) != asset_database_.end() ||
           std::filesystem::exists(resolved);
}

String ResourceManager::ResolvePath(const String& relative_path) const {
    if (std::filesystem::path(relative_path).is_absolute()) {
        return relative_path;
    }
    return (std::filesystem::path(asset_root_) / relative_path).string();
}

String ResourceManager::GetRelativePath(const String& absolute_path) const {
    return std::filesystem::relative(absolute_path, asset_root_).string();
}

ResourceManager::Stats ResourceManager::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void ResourceManager::LoaderThread() {
    while (running_) {
        ResourceLoadRequest request;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return !load_queue_.empty() || !running_; });

            if (!running_ && load_queue_.empty()) {
                return;
            }

            request = load_queue_.front();
            load_queue_.pop();
        }

        auto resource = LoadResource(request.path, request.type);

        if (request.callback) {
            request.callback(resource);
        }

        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            stats_.pending_loads--;
        }
    }
}

ResourceType ResourceManager::DetectResourceType(const String& path) const {
    String ext = std::filesystem::path(path).extension().string();

    // Convert to lowercase
    for (auto& c : ext) c = static_cast<char>(std::tolower(c));

    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
        ext == ".tga" || ext == ".bmp" || ext == ".hdr") {
        return ResourceType::Texture;
    }
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
        return ResourceType::Mesh;
    }
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3") {
        return ResourceType::Audio;
    }
    if (ext == ".lua") {
        return ResourceType::Script;
    }
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl" || ext == ".spv") {
        return ResourceType::Shader;
    }
    if (ext == ".ttf" || ext == ".otf") {
        return ResourceType::Font;
    }
    if (ext == ".scene" || ext == ".json") {
        return ResourceType::Scene;
    }
    if (ext == ".prefab") {
        return ResourceType::Prefab;
    }
    if (ext == ".anim") {
        return ResourceType::Animation;
    }

    return ResourceType::Unknown;
}

// ============================================================================
// AssetImporter Implementation
// ============================================================================

AssetImporter& AssetImporter::Instance() {
    static AssetImporter instance;
    return instance;
}

bool AssetImporter::Initialize() {
    return true;
}

void AssetImporter::Shutdown() {
}

bool AssetImporter::ImportTexture(const String& source, const String& dest,
                                  const TextureImportSettings& settings) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load(source.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!decoded) {
        DRX_LOG_ERROR("Failed to import texture {}: {}", source, stbi_failure_reason());
        return false;
    }
    Array<uint8> pixels(decoded, decoded + static_cast<size_t>(width) * height * 4);
    stbi_image_free(decoded);

    // 2x2 box filter until the image fits
    const uint32 max_size = std::max(1u, settings.max_size);
    while (static_cast<uint32>(width) > max_size || static_cast<uint32>(height) > max_size) {
        const int w = std::max(1, width / 2), h = std::max(1, height / 2);
        Array<uint8> half(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    uint32 sum = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(width - 1, x * 2 + dx), sy = std::min(height - 1, y * 2 + dy);
                            sum += pixels[(static_cast<size_t>(sy) * width + sx) * 4 + c];
                        }
                    }
                    half[(static_cast<size_t>(y) * w + x) * 4 + c] = static_cast<uint8>((sum + 2) / 4);
                }
            }
        }
        pixels = std::move(half);
        width = w;
        height = h;
    }

    std::error_code ec;
    const auto parent = std::filesystem::path(dest).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (!stbi_write_png(dest.c_str(), width, height, 4, pixels.data(), width * 4)) {
        DRX_LOG_ERROR("Failed to write texture {}", dest);
        return false;
    }
    DRX_LOG_INFO("Imported texture: {} -> {} ({}x{})", source, dest, width, height);
    return true;
}

bool AssetImporter::ImportMesh(const String& source, const String& dest) {
    try {
        std::filesystem::copy(source, dest, std::filesystem::copy_options::overwrite_existing);
        DRX_LOG_INFO("Imported mesh: {} -> {}", source, dest);
        return true;
    } catch (const std::exception& e) {
        DRX_LOG_ERROR("Failed to import mesh: {}", e.what());
        return false;
    }
}

bool AssetImporter::ImportAudio(const String& source, const String& dest) {
    try {
        std::filesystem::copy(source, dest, std::filesystem::copy_options::overwrite_existing);
        DRX_LOG_INFO("Imported audio: {} -> {}", source, dest);
        return true;
    } catch (const std::exception& e) {
        DRX_LOG_ERROR("Failed to import audio: {}", e.what());
        return false;
    }
}

void AssetImporter::ImportDirectory(const String& source_dir, const String& dest_dir) {
    try {
        std::filesystem::create_directories(dest_dir);

        for (const auto& entry : std::filesystem::recursive_directory_iterator(source_dir)) {
            if (entry.is_regular_file()) {
                auto rel_path = std::filesystem::relative(entry.path(), source_dir);
                auto dest_path = std::filesystem::path(dest_dir) / rel_path;

                std::filesystem::create_directories(dest_path.parent_path());
                std::filesystem::copy(entry.path(), dest_path,
                                      std::filesystem::copy_options::overwrite_existing);
            }
        }

        DRX_LOG_INFO("Imported directory: {} -> {}", source_dir, dest_dir);
    } catch (const std::exception& e) {
        DRX_LOG_ERROR("Failed to import directory: {}", e.what());
    }
}

// ============================================================================
// AssetPackage Implementation
// ============================================================================

namespace {
constexpr char kPackageMagic[4] = {'D', 'R', 'X', 'P'};
constexpr uint32 kPackageVersion = 1;

template<typename T>
void WritePod(std::ofstream& file, const T& value) {
    file.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template<typename T>
bool ReadPod(std::ifstream& file, T& value) {
    return static_cast<bool>(file.read(reinterpret_cast<char*>(&value), sizeof(T)));
}
} // namespace

void AssetPackage::Create(const String& path) {
    Close();
    package_path_ = path;
}

bool AssetPackage::Save() {
    if (package_path_.empty()) return false;

    // Keep files of an opened package that were not replaced
    for (const auto& [name, entry] : entries_) {
        if (!pending_.count(name)) pending_[name] = ReadFile(name);
    }

    // Layout: magic, version, count, table (name_len, name, offset, size), data
    Array<String> names;
    names.reserve(pending_.size());
    for (const auto& [name, data] : pending_) names.push_back(name);
    std::sort(names.begin(), names.end());

    uint64 table_size = sizeof(kPackageMagic) + sizeof(uint32) * 2;
    for (const String& name : names) table_size += sizeof(uint32) + name.size() + sizeof(uint64) * 2;

    std::ofstream file(package_path_, std::ios::binary | std::ios::trunc);
    if (!file) {
        DRX_LOG_ERROR("Cannot write package {}", package_path_);
        return false;
    }
    file.write(kPackageMagic, sizeof(kPackageMagic));
    WritePod(file, kPackageVersion);
    WritePod(file, static_cast<uint32>(names.size()));

    entries_.clear();
    uint64 offset = table_size;
    for (const String& name : names) {
        const uint64 size = pending_[name].size();
        WritePod(file, static_cast<uint32>(name.size()));
        file.write(name.data(), static_cast<std::streamsize>(name.size()));
        WritePod(file, offset);
        WritePod(file, size);
        entries_[name] = {offset, size};
        offset += size;
    }
    for (const String& name : names) {
        const Array<uint8>& data = pending_[name];
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        DRX_LOG_ERROR("Failed while writing package {}", package_path_);
        return false;
    }
    package_size_ = offset;
    pending_.clear();
    return true;
}

bool AssetPackage::Open(const String& path) {
    Close();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto file_size = static_cast<uint64>(file.tellg());
    file.seekg(0);

    char magic[4] = {};
    uint32 version = 0, count = 0;
    if (!file.read(magic, 4) || std::memcmp(magic, kPackageMagic, 4) != 0 || !ReadPod(file, version) ||
        version != kPackageVersion || !ReadPod(file, count)) {
        DRX_LOG_ERROR("{} is not a valid DRX package", path);
        return false;
    }

    HashMap<String, FileEntry> entries;
    for (uint32 i = 0; i < count; ++i) {
        uint32 name_length = 0;
        if (!ReadPod(file, name_length) || name_length == 0 || name_length > 4096) {
            DRX_LOG_ERROR("Package {} has a corrupt file table", path);
            return false;
        }
        String name(name_length, '\0');
        FileEntry entry;
        if (!file.read(name.data(), name_length) || !ReadPod(file, entry.offset) || !ReadPod(file, entry.size) ||
            entry.offset > file_size || entry.size > file_size - entry.offset) {
            DRX_LOG_ERROR("Package {} has a corrupt file table", path);
            return false;
        }
        entries[name] = entry;
    }

    package_path_ = path;
    entries_ = std::move(entries);
    package_size_ = file_size;
    return true;
}

void AssetPackage::Close() {
    entries_.clear();
    pending_.clear();
    package_path_.clear();
    package_size_ = 0;
}

bool AssetPackage::AddFile(const String& name, const void* data, uint64 size) {
    if (name.empty() || (size > 0 && !data)) return false;
    const auto* bytes = static_cast<const uint8*>(data);
    pending_[name] = Array<uint8>(bytes, bytes + size);
    return true;
}

bool AssetPackage::AddFile(const String& name, const String& source_path) {
    std::ifstream file(source_path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto size = static_cast<uint64>(file.tellg());
    file.seekg(0);
    Array<uint8> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size))) return false;
    pending_[name] = std::move(data);
    return true;
}

bool AssetPackage::HasFile(const String& name) const {
    return pending_.count(name) > 0 || entries_.count(name) > 0;
}

Array<uint8> AssetPackage::ReadFile(const String& name) const {
    if (auto pending = pending_.find(name); pending != pending_.end()) return pending->second;

    const auto it = entries_.find(name);
    if (it == entries_.end()) return {};
    std::ifstream file(package_path_, std::ios::binary);
    if (!file) return {};

    Array<uint8> data(it->second.size);
    file.seekg(static_cast<std::streamoff>(it->second.offset));
    if (!file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return {};
    return data;
}

Array<String> AssetPackage::GetFileList() const {
    Array<String> result;
    for (const auto& [name, entry] : entries_) result.push_back(name);
    for (const auto& [name, data] : pending_) {
        if (!entries_.count(name)) result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace DRX::Resource
