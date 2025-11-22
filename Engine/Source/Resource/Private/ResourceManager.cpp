/**
 * DRX Engine - Resource Manager Implementation
 */

#include "Resource/Public/ResourceManager.h"
#include "Core/Public/Log.h"

#include <fstream>
#include <chrono>

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
    // In a real implementation, this would process the image
    // For now, just copy the file
    try {
        std::filesystem::copy(source, dest, std::filesystem::copy_options::overwrite_existing);
        DRX_LOG_INFO("Imported texture: {} -> {}", source, dest);
        return true;
    } catch (const std::exception& e) {
        DRX_LOG_ERROR("Failed to import texture: {}", e.what());
        return false;
    }
}

bool AssetImporter::ImportMesh(const String& source, const String& dest,
                                const MeshImportSettings& settings) {
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

AssetPackage::~AssetPackage() {
    Close();
}

bool AssetPackage::Create(const String& path) {
    package_path_ = path;
    entries_.clear();
    package_size_ = 0;
    return true;
}

bool AssetPackage::Open(const String& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    package_path_ = path;

    // Read header
    char magic[4];
    file.read(magic, 4);
    if (std::strncmp(magic, "DRXP", 4) != 0) {
        return false;
    }

    uint32 version;
    file.read(reinterpret_cast<char*>(&version), 4);

    uint32 entry_count;
    file.read(reinterpret_cast<char*>(&entry_count), 4);

    // Read entries
    for (uint32 i = 0; i < entry_count; i++) {
        uint32 name_len;
        file.read(reinterpret_cast<char*>(&name_len), 4);

        String name(name_len, '\0');
        file.read(name.data(), name_len);

        FileEntry entry;
        entry.name = name;
        file.read(reinterpret_cast<char*>(&entry.offset), 8);
        file.read(reinterpret_cast<char*>(&entry.size), 8);
        file.read(reinterpret_cast<char*>(&entry.compressed_size), 8);
        file.read(reinterpret_cast<char*>(&entry.flags), 4);

        entries_[name] = entry;
    }

    return true;
}

void AssetPackage::Close() {
    entries_.clear();
    package_path_.clear();
    package_size_ = 0;
}

bool AssetPackage::AddFile(const String& name, const void* data, uint64 size) {
    FileEntry entry;
    entry.name = name;
    entry.offset = package_size_;
    entry.size = size;
    entry.compressed_size = size;
    entry.flags = 0;

    entries_[name] = entry;
    package_size_ += size;

    return true;
}

bool AssetPackage::AddFile(const String& name, const String& source_path) {
    std::ifstream file(source_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }

    uint64 size = file.tellg();
    file.seekg(0);

    Array<uint8> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return AddFile(name, data.data(), size);
}

bool AssetPackage::HasFile(const String& name) const {
    return entries_.find(name) != entries_.end();
}

Array<uint8> AssetPackage::ReadFile(const String& name) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        return {};
    }

    std::ifstream file(package_path_, std::ios::binary);
    if (!file) {
        return {};
    }

    const FileEntry& entry = it->second;
    file.seekg(entry.offset);

    Array<uint8> data(entry.size);
    file.read(reinterpret_cast<char*>(data.data()), entry.size);

    return data;
}

Array<String> AssetPackage::GetFileList() const {
    Array<String> result;
    result.reserve(entries_.size());
    for (const auto& [name, entry] : entries_) {
        result.push_back(name);
    }
    return result;
}

} // namespace DRX::Resource
