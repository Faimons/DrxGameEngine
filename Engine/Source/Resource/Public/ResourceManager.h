/**
 * DRX Engine - Resource Manager
 * Centralized asset loading, caching, and hot-reloading
 */

#pragma once

#include "Core/Public/CoreTypes.h"

#include <atomic>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <typeindex>
#include <filesystem>

namespace DRX::Resource {

/**
 * Resource state
 */
enum class ResourceState : uint32 {
    Unloaded,
    Loading,
    Loaded,
    Failed
};

/**
 * Resource type identifiers
 */
enum class ResourceType : uint32 {
    Unknown,
    Texture,
    Mesh,
    Material,
    Shader,
    Audio,
    Font,
    Script,
    Scene,
    Prefab,
    Animation,
    PhysicsMaterial
};

/**
 * Base resource class
 */
class IResource {
public:
    virtual ~IResource() = default;

    virtual ResourceType GetType() const = 0;
    virtual const String& GetPath() const = 0;
    virtual ResourceState GetState() const = 0;
    virtual uint64 GetSize() const = 0;
    virtual void Reload() = 0;
    virtual void Unload() = 0;
};

/**
 * Resource handle - type-safe reference to a resource
 */
template<typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;
    ResourceHandle(SharedPtr<T> resource) : resource_(resource) {}

    bool IsValid() const { return resource_ != nullptr; }
    bool IsLoaded() const { return resource_ && resource_->GetState() == ResourceState::Loaded; }

    T* Get() const { return resource_.get(); }
    T* operator->() const { return resource_.get(); }
    T& operator*() const { return *resource_; }

    operator bool() const { return IsValid(); }

private:
    SharedPtr<T> resource_;
};

/**
 * Resource load request
 */
struct ResourceLoadRequest {
    String path;
    ResourceType type;
    std::function<void(SharedPtr<IResource>)> callback;
    int32 priority = 0;
};

/**
 * Resource metadata
 */
struct ResourceMetadata {
    String path;
    ResourceType type;
    uint64 size = 0;
    uint64 last_modified = 0;
    Array<String> dependencies;
    HashMap<String, String> properties;
};

/**
 * Resource loader interface
 */
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    virtual ResourceType GetType() const = 0;
    virtual Array<String> GetSupportedExtensions() const = 0;
    virtual SharedPtr<IResource> Load(const String& path) = 0;
    virtual bool CanLoad(const String& path) const = 0;
};

/**
 * Resource cache entry
 */
struct CacheEntry {
    SharedPtr<IResource> resource;
    uint64 last_access = 0;
    uint32 ref_count = 0;
};

/**
 * Resource Manager
 * Handles loading, caching, and lifecycle of all game resources
 */
class ResourceManager {
public:
    static ResourceManager& Instance();

    bool Initialize(const String& asset_root = "Assets");
    void Shutdown();
    void Update();

    // Resource loading
    template<typename T>
    ResourceHandle<T> Load(const String& path);

    template<typename T>
    ResourceHandle<T> LoadAsync(const String& path, std::function<void(ResourceHandle<T>)> callback = nullptr);

    template<typename T>
    ResourceHandle<T> Get(const String& path);

    // Generic loading
    SharedPtr<IResource> LoadResource(const String& path, ResourceType type);
    void LoadResourceAsync(const String& path, ResourceType type,
                           std::function<void(SharedPtr<IResource>)> callback);

    // Unloading
    void Unload(const String& path);
    void UnloadUnused();
    void UnloadAll();

    // Hot reloading
    void EnableHotReload(bool enable);
    void CheckForChanges();
    void ReloadResource(const String& path);

    // Loaders
    void RegisterLoader(UniquePtr<IResourceLoader> loader);
    IResourceLoader* GetLoader(ResourceType type) const;
    IResourceLoader* GetLoaderForExtension(const String& extension) const;

    // Cache management
    void SetCacheSize(uint64 max_bytes);
    uint64 GetCacheSize() const;
    uint64 GetCacheUsed() const;
    void ClearCache();

    // Asset database
    void ScanAssets();
    Array<ResourceMetadata> GetAllResources() const;
    Array<ResourceMetadata> GetResourcesByType(ResourceType type) const;
    bool ResourceExists(const String& path) const;

    // Paths
    String GetAssetRoot() const { return asset_root_; }
    String ResolvePath(const String& relative_path) const;
    String GetRelativePath(const String& absolute_path) const;

    // Statistics
    struct Stats {
        uint32 total_resources = 0;
        uint32 loaded_resources = 0;
        uint32 pending_loads = 0;
        uint64 total_memory = 0;
        uint64 cache_hits = 0;
        uint64 cache_misses = 0;
    };
    Stats GetStats() const;

private:
    ResourceManager() = default;
    ~ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void LoaderThread();
    void ProcessLoadQueue();
    ResourceType DetectResourceType(const String& path) const;

    String asset_root_;

    // Resource cache
    HashMap<String, CacheEntry> cache_;
    mutable std::mutex cache_mutex_;
    uint64 max_cache_size_ = 512 * 1024 * 1024;  // 512 MB default

    // Loaders
    HashMap<ResourceType, UniquePtr<IResourceLoader>> loaders_;
    HashMap<String, ResourceType> extension_map_;

    // Async loading
    std::queue<ResourceLoadRequest> load_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    Array<std::thread> loader_threads_;
    std::atomic<bool> running_{false};
    uint32 num_loader_threads_ = 4;

    // Hot reload
    bool hot_reload_enabled_ = false;
    HashMap<String, uint64> file_timestamps_;

    // Asset database
    HashMap<String, ResourceMetadata> asset_database_;

    // Stats
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

// Template implementations
template<typename T>
ResourceHandle<T> ResourceManager::Load(const String& path) {
    auto resource = LoadResource(path, T::StaticType());
    return ResourceHandle<T>(std::dynamic_pointer_cast<T>(resource));
}

template<typename T>
ResourceHandle<T> ResourceManager::LoadAsync(const String& path,
                                              std::function<void(ResourceHandle<T>)> callback) {
    LoadResourceAsync(path, T::StaticType(), [callback](SharedPtr<IResource> res) {
        if (callback) {
            callback(ResourceHandle<T>(std::dynamic_pointer_cast<T>(res)));
        }
    });
    return Get<T>(path);
}

template<typename T>
ResourceHandle<T> ResourceManager::Get(const String& path) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return ResourceHandle<T>(std::dynamic_pointer_cast<T>(it->second.resource));
    }
    return ResourceHandle<T>();
}

struct TextureImportSettings {
    uint32 max_size = 4096;   // larger images are halved until both sides fit
};

/**
 * Asset Importer - converts raw assets to engine format
 */
class AssetImporter {
public:
    static AssetImporter& Instance();

    bool Initialize();
    void Shutdown();

    // Import settings
    using TextureImportSettings = Resource::TextureImportSettings;

    // Decodes any stb_image format and writes an RGBA PNG
    bool ImportTexture(const String& source, const String& dest, const TextureImportSettings& settings = {});
    // Models and audio are loaded from their source formats at runtime; importing copies them
    bool ImportMesh(const String& source, const String& dest);
    bool ImportAudio(const String& source, const String& dest);

    // Batch import
    void ImportDirectory(const String& source_dir, const String& dest_dir);

private:
    AssetImporter() = default;
};

/**
 * Asset Package - bundled assets for distribution
 */
class AssetPackage {
public:
    AssetPackage() = default;
    ~AssetPackage() = default;

    // Starts a new package; files are kept in memory until Save()
    void Create(const String& path);
    bool Save();
    // Opens an existing package for reading
    bool Open(const String& path);
    void Close();

    bool AddFile(const String& name, const void* data, uint64 size);
    bool AddFile(const String& name, const String& source_path);

    bool HasFile(const String& name) const;
    Array<uint8> ReadFile(const String& name) const;
    Array<String> GetFileList() const;

    uint64 GetSize() const { return package_size_; }

private:
    struct FileEntry {
        uint64 offset = 0;
        uint64 size = 0;
    };

    String package_path_;
    HashMap<String, FileEntry> entries_;
    HashMap<String, Array<uint8>> pending_;
    uint64 package_size_ = 0;
};

} // namespace DRX::Resource
