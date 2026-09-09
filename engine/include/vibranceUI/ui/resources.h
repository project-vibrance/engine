#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

inline bool ui_directory_exists(const std::filesystem::path& path)
{
    std::error_code error;
    return !path.empty() &&
        std::filesystem::exists(path, error) &&
        std::filesystem::is_directory(path, error);
}

inline bool ui_path_exists(const std::filesystem::path& path)
{
    std::error_code error;
    return !path.empty() && std::filesystem::exists(path, error);
}

inline std::filesystem::path ui_absolute_path(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {};
    }

    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error);
    if (!error && !canonical.empty())
    {
        return canonical;
    }
    return std::filesystem::absolute(path, error);
}

inline std::filesystem::path ui_env_path(const char* name)
{
    if (!name)
    {
        return {};
    }
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0')
    {
        return {};
    }
    return ui_absolute_path(std::filesystem::path(value));
}

inline void ui_append_unique_path(
    std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& path)
{
    if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
    {
        paths.push_back(path);
    }
}

struct UiResourceDirectories
{
    // Packaged defaults are immutable fallback layers. User roots contain only
    // overrides and are never populated by copying packaged files.
    std::filesystem::path packagedAssetsDirectory {};
    std::filesystem::path packagedConfigDirectory {};
    std::filesystem::path userAssetsDirectory {};
    std::filesystem::path userConfigDirectory {};

    // Candidate files are ordered from highest priority to packaged fallback.
    // Loaders should try each candidate until one validates/decodes.
    std::vector<std::filesystem::path> asset_candidates(
        const std::filesystem::path& relativePath) const
    {
        return file_candidates(relativePath, userAssetsDirectory, packagedAssetsDirectory);
    }

    std::vector<std::filesystem::path> config_candidates(
        const std::filesystem::path& relativePath) const
    {
        return file_candidates(relativePath, userConfigDirectory, packagedConfigDirectory);
    }

    // File/directory layers are ordered from packaged fallback to user
    // priority, suitable for parsers that merge valid content successively.
    std::vector<std::filesystem::path> asset_file_layers(
        const std::filesystem::path& relativePath) const
    {
        return file_layers(relativePath, packagedAssetsDirectory, userAssetsDirectory);
    }

    std::vector<std::filesystem::path> config_file_layers(
        const std::filesystem::path& relativePath) const
    {
        return file_layers(relativePath, packagedConfigDirectory, userConfigDirectory);
    }

    std::vector<std::filesystem::path> asset_directory_layers(
        const std::filesystem::path& relativePath = {}) const
    {
        return directory_layers(relativePath, packagedAssetsDirectory, userAssetsDirectory);
    }

    std::vector<std::filesystem::path> config_directory_layers(
        const std::filesystem::path& relativePath = {}) const
    {
        return directory_layers(relativePath, packagedConfigDirectory, userConfigDirectory);
    }

    std::filesystem::path writable_asset(const std::filesystem::path& relativePath = {}) const
    {
        return userAssetsDirectory.empty()
            ? std::filesystem::path {}
            : userAssetsDirectory / relativePath;
    }

    std::filesystem::path writable_config(const std::filesystem::path& relativePath = {}) const
    {
        return userConfigDirectory.empty()
            ? std::filesystem::path {}
            : userConfigDirectory / relativePath;
    }

private:
    static void append_unique(
        std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& path)
    {
        if (path.empty())
        {
            return;
        }
        const std::filesystem::path absolutePath = ui_absolute_path(path);
        if (std::find(paths.begin(), paths.end(), absolutePath) == paths.end())
        {
            paths.push_back(absolutePath);
        }
    }

    static std::vector<std::filesystem::path> file_candidates(
        const std::filesystem::path& relativePath,
        const std::filesystem::path& userRoot,
        const std::filesystem::path& packagedRoot)
    {
        if (relativePath.is_absolute())
        {
            return { relativePath };
        }

        std::vector<std::filesystem::path> candidates;
        if (!userRoot.empty() && ui_path_exists(userRoot / relativePath))
        {
            append_unique(candidates, userRoot / relativePath);
        }
        if (!packagedRoot.empty() && ui_path_exists(packagedRoot / relativePath))
        {
            append_unique(candidates, packagedRoot / relativePath);
        }

        // Keep one diagnostic path when nothing exists so loaders can report
        // the expected packaged/user location.
        if (candidates.empty())
        {
            if (!packagedRoot.empty())
            {
                append_unique(candidates, packagedRoot / relativePath);
            }
            else if (!userRoot.empty())
            {
                append_unique(candidates, userRoot / relativePath);
            }
        }
        return candidates;
    }

    static std::vector<std::filesystem::path> file_layers(
        const std::filesystem::path& relativePath,
        const std::filesystem::path& packagedRoot,
        const std::filesystem::path& userRoot)
    {
        if (relativePath.is_absolute())
        {
            return ui_path_exists(relativePath)
                ? std::vector<std::filesystem::path> { relativePath }
                : std::vector<std::filesystem::path> {};
        }

        std::vector<std::filesystem::path> layers;
        if (!packagedRoot.empty() && ui_path_exists(packagedRoot / relativePath))
        {
            append_unique(layers, packagedRoot / relativePath);
        }
        if (!userRoot.empty() && ui_path_exists(userRoot / relativePath))
        {
            append_unique(layers, userRoot / relativePath);
        }
        return layers;
    }

    static std::vector<std::filesystem::path> directory_layers(
        const std::filesystem::path& relativePath,
        const std::filesystem::path& packagedRoot,
        const std::filesystem::path& userRoot)
    {
        std::vector<std::filesystem::path> layers;
        if (!packagedRoot.empty() && ui_directory_exists(packagedRoot / relativePath))
        {
            append_unique(layers, packagedRoot / relativePath);
        }
        if (!userRoot.empty() && ui_directory_exists(userRoot / relativePath))
        {
            append_unique(layers, userRoot / relativePath);
        }
        return layers;
    }
};

struct UiResourceChangeSet
{
    std::vector<std::filesystem::path> assetPaths;
    std::vector<std::filesystem::path> configPaths;

    bool any() const
    {
        return !assetPaths.empty() || !configPaths.empty();
    }

    bool assets_changed_under(const std::filesystem::path& relativePath = {}) const
    {
        return paths_changed_under(assetPaths, relativePath);
    }

    bool config_changed_under(const std::filesystem::path& relativePath = {}) const
    {
        return paths_changed_under(configPaths, relativePath);
    }

private:
    static bool has_prefix(
        const std::filesystem::path& path,
        const std::filesystem::path& prefix)
    {
        auto pathIt = path.begin();
        for (auto prefixIt = prefix.begin(); prefixIt != prefix.end(); ++prefixIt, ++pathIt)
        {
            if (pathIt == path.end() || *pathIt != *prefixIt)
            {
                return false;
            }
        }
        return true;
    }

    static bool paths_changed_under(
        const std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& relativePath)
    {
        if (relativePath.empty())
        {
            return !paths.empty();
        }
        return std::any_of(paths.begin(), paths.end(), [&relativePath](const auto& path) {
            return has_prefix(path, relativePath);
        });
    }
};

class UiResourceChangeTracker
{
public:
    explicit UiResourceChangeTracker(
        UiResourceDirectories resources = {},
        std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000)) :
        pollInterval_(std::max(pollInterval, std::chrono::milliseconds(50)))
    {
        reset(std::move(resources));
    }

    void reset(UiResourceDirectories resources)
    {
        resources_ = std::move(resources);
        snapshot_ = capture_snapshot();
        nextPoll_ = std::chrono::steady_clock::now() + pollInterval_;
        initialised_ = true;
    }

    // Runtime state often lives beside user configuration but must not be
    // treated as a hot-reloadable UI resource. Paths are relative to the
    // user/packaged config roots; directory paths ignore their whole subtree.
    void ignore_config_path(std::filesystem::path relativePath)
    {
        relativePath = relativePath.lexically_normal();
        if (relativePath.empty() || relativePath == ".")
        {
            return;
        }
        if (std::find(
                ignoredConfigPaths_.begin(),
                ignoredConfigPaths_.end(),
                relativePath) == ignoredConfigPaths_.end())
        {
            ignoredConfigPaths_.push_back(std::move(relativePath));
            snapshot_ = capture_snapshot();
        }
    }

    UiResourceChangeSet poll(bool force = false)
    {
        if (!initialised_)
        {
            reset(resources_);
            return {};
        }

        const auto now = std::chrono::steady_clock::now();
        if (!force && now < nextPoll_)
        {
            return {};
        }
        nextPoll_ = now + pollInterval_;

        Snapshot next = capture_snapshot();
        UiResourceChangeSet changes = compare_snapshots(snapshot_, next);
        snapshot_ = std::move(next);
        return changes;
    }

private:
    enum class ResourceKind
    {
        eAsset,
        eConfig
    };

    struct FileStamp
    {
        ResourceKind kind = ResourceKind::eAsset;
        std::filesystem::path relativePath;
        std::uintmax_t size = 0u;
        std::filesystem::file_time_type writeTime {};
    };

    using Snapshot = std::unordered_map<std::string, FileStamp>;

    static void append_changed_path(
        std::vector<std::filesystem::path>& paths,
        const std::filesystem::path& path)
    {
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }

    static bool stamp_changed(const FileStamp& left, const FileStamp& right)
    {
        return left.size != right.size || left.writeTime != right.writeTime;
    }

    static void record_change(UiResourceChangeSet& changes, const FileStamp& stamp)
    {
        if (stamp.kind == ResourceKind::eAsset)
        {
            append_changed_path(changes.assetPaths, stamp.relativePath);
        }
        else
        {
            append_changed_path(changes.configPaths, stamp.relativePath);
        }
    }

    static UiResourceChangeSet compare_snapshots(
        const Snapshot& previous,
        const Snapshot& next)
    {
        UiResourceChangeSet changes = {};
        for (const auto& [key, stamp] : next)
        {
            const auto old = previous.find(key);
            if (old == previous.end() || stamp_changed(old->second, stamp))
            {
                record_change(changes, stamp);
            }
        }
        for (const auto& [key, stamp] : previous)
        {
            if (next.find(key) == next.end())
            {
                record_change(changes, stamp);
            }
        }
        return changes;
    }

    static bool path_has_prefix(
        const std::filesystem::path& path,
        const std::filesystem::path& prefix)
    {
        auto pathIt = path.begin();
        for (auto prefixIt = prefix.begin();
            prefixIt != prefix.end(); ++prefixIt, ++pathIt)
        {
            if (pathIt == path.end() || *pathIt != *prefixIt)
            {
                return false;
            }
        }
        return true;
    }

    bool config_path_ignored(
        const std::filesystem::path& relativePath) const
    {
        return std::any_of(
            ignoredConfigPaths_.begin(),
            ignoredConfigPaths_.end(),
            [&relativePath](const std::filesystem::path& ignored) {
                return path_has_prefix(relativePath, ignored);
            });
    }

    void scan_root(
        Snapshot& snapshot,
        const std::filesystem::path& root,
        ResourceKind kind,
        std::string_view layerName) const
    {
        if (!ui_directory_exists(root))
        {
            return;
        }

        std::error_code error;
        std::filesystem::recursive_directory_iterator iterator(
            root,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end)
        {
            const std::filesystem::directory_entry entry = *iterator;
            if (entry.is_regular_file(error) && !error)
            {
                std::filesystem::path relativePath = std::filesystem::relative(entry.path(), root, error);
                if (error)
                {
                    error.clear();
                    relativePath = entry.path().lexically_relative(root);
                }

                if (kind == ResourceKind::eConfig &&
                    config_path_ignored(relativePath))
                {
                    iterator.increment(error);
                    if (error)
                    {
                        error.clear();
                    }
                    continue;
                }

                FileStamp stamp = {};
                stamp.kind = kind;
                stamp.relativePath = relativePath;
                stamp.size = entry.file_size(error);
                if (error)
                {
                    error.clear();
                    stamp.size = 0u;
                }
                stamp.writeTime = entry.last_write_time(error);
                if (error)
                {
                    error.clear();
                    stamp.writeTime = {};
                }

                const std::string key =
                    (kind == ResourceKind::eAsset ? "asset|" : "config|") +
                    std::string(layerName) +
                    "|" +
                    relativePath.generic_string();
                snapshot[key] = std::move(stamp);
            }
            iterator.increment(error);
            if (error)
            {
                error.clear();
            }
        }
    }

    Snapshot capture_snapshot() const
    {
        Snapshot snapshot;
        scan_root(
            snapshot,
            resources_.packagedAssetsDirectory,
            ResourceKind::eAsset,
            "packaged");
        scan_root(
            snapshot,
            resources_.userAssetsDirectory,
            ResourceKind::eAsset,
            "user");
        scan_root(
            snapshot,
            resources_.packagedConfigDirectory,
            ResourceKind::eConfig,
            "packaged");
        scan_root(
            snapshot,
            resources_.userConfigDirectory,
            ResourceKind::eConfig,
            "user");
        return snapshot;
    }

    UiResourceDirectories resources_ {};
    std::vector<std::filesystem::path> ignoredConfigPaths_ {};
    Snapshot snapshot_ {};
    std::chrono::milliseconds pollInterval_ { 500 };
    std::chrono::steady_clock::time_point nextPoll_ {};
    bool initialised_ = false;
};

inline std::filesystem::path ui_first_existing_directory(
    const std::vector<std::filesystem::path>& candidates,
    std::filesystem::path fallback)
{
    for (const std::filesystem::path& candidate : candidates)
    {
        if (ui_directory_exists(candidate))
        {
            return ui_absolute_path(candidate);
        }
    }
    return ui_absolute_path(std::move(fallback));
}

inline std::filesystem::path ui_default_user_resource_root(std::string_view applicationName)
{
    if (applicationName.empty())
    {
        return {};
    }

#ifdef _WIN32
    std::filesystem::path base = ui_env_path("LOCALAPPDATA");
    if (base.empty())
    {
        if (const std::filesystem::path userProfile = ui_env_path("USERPROFILE"); !userProfile.empty())
        {
            base = userProfile / "AppData" / "Local";
        }
    }
#elif defined(__APPLE__)
    std::filesystem::path base = ui_env_path("HOME");
    if (!base.empty())
    {
        base /= "Library/Application Support";
    }
#else
    std::filesystem::path base = ui_env_path("XDG_CONFIG_HOME");
    if (base.empty())
    {
        if (const std::filesystem::path home = ui_env_path("HOME"); !home.empty())
        {
            base = home / ".config";
        }
    }
#endif

    return base.empty()
        ? std::filesystem::path {}
        : ui_absolute_path(base / std::filesystem::path(applicationName));
}

inline UiResourceDirectories ui_resource_directories_from_base(
    std::filesystem::path packagedAssetsDirectory,
    std::filesystem::path packagedConfigDirectory,
    std::filesystem::path userAssetsDirectory = {},
    std::filesystem::path userConfigDirectory = {})
{
    UiResourceDirectories directories = {};
    directories.packagedAssetsDirectory = ui_absolute_path(std::move(packagedAssetsDirectory));
    directories.packagedConfigDirectory = ui_absolute_path(std::move(packagedConfigDirectory));
    directories.userAssetsDirectory = ui_absolute_path(std::move(userAssetsDirectory));
    directories.userConfigDirectory = ui_absolute_path(std::move(userConfigDirectory));
    return directories;
}

inline UiResourceDirectories ui_discover_resource_directories(
    std::filesystem::path workingDirectory = std::filesystem::current_path(),
    std::string applicationName = {})
{
    workingDirectory = ui_absolute_path(std::move(workingDirectory));

    std::vector<std::filesystem::path> assetCandidates;
    std::vector<std::filesystem::path> configCandidates;
    // Build trees are allowed to add platform/configuration directories (for
    // example build/windows-x64/Release/app). Walk the ancestry instead of
    // assuming a fixed depth so a development executable can still reach the
    // source-tree app/assets and app/config fallbacks.
    std::vector<std::filesystem::path> sourceRoots;
    std::filesystem::path sourceRoot = workingDirectory;
    for (std::size_t depth = 0u; depth < 8u && !sourceRoot.empty(); ++depth)
    {
        ui_append_unique_path(sourceRoots, sourceRoot);
        const std::filesystem::path parent = sourceRoot.parent_path();
        if (parent.empty() || parent == sourceRoot)
        {
            break;
        }
        sourceRoot = parent;
    }

    // Installed applications may keep implementation-only resources under a
    // private .vibrance directory. Prefer that layout over legacy public
    // assets/config siblings; source-tree resources remain a development
    // fallback for build directories that intentionally do not copy data.
    for (const std::filesystem::path& root : sourceRoots)
    {
        assetCandidates.push_back(root / ".vibrance" / "assets");
        configCandidates.push_back(root / ".vibrance" / "config");
    }
    for (const std::filesystem::path& root : sourceRoots)
    {
        assetCandidates.push_back(root / "assets");
        configCandidates.push_back(root / "config");
    }
    for (const std::filesystem::path& root : sourceRoots)
    {
        assetCandidates.push_back(root / "app" / "assets");
        configCandidates.push_back(root / "app" / "config");
    }

    std::filesystem::path userRoot = ui_env_path("VIBRANCE_RESOURCE_ROOT");
    if (userRoot.empty())
    {
        userRoot = ui_default_user_resource_root(applicationName);
    }
    std::filesystem::path userAssets = ui_env_path("VIBRANCE_ASSETS_DIR");
    std::filesystem::path userConfig = ui_env_path("VIBRANCE_CONFIG_DIR");
    if (userAssets.empty() && !userRoot.empty())
    {
        userAssets = userRoot / "assets";
    }
    if (userConfig.empty() && !userRoot.empty())
    {
        userConfig = userRoot / "config";
    }

    return ui_resource_directories_from_base(
        ui_first_existing_directory(assetCandidates, workingDirectory / "assets"),
        ui_first_existing_directory(configCandidates, workingDirectory / "config"),
        std::move(userAssets),
        std::move(userConfig));
}
