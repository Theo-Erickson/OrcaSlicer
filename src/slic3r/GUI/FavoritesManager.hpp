#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <functional>
#include <wx/wx.h>

namespace Slic3r {
namespace GUI {

// Canonical tab ordering — must match the sidebar tab order.
// The Favorites tab renders items grouped and sorted by this order.
enum class SettingsTabId : int {
    Favorites = 0,
    Quality   = 1,
    Strength  = 2,
    Speed     = 3,
    Support   = 4,
    Multimaterial,
    Others,
    Filament,
    BasicInformation,
    MachineGcode,
    // Add new tabs here; keep Favorites first.
};

// Metadata attached to every favoritable config option.
struct FavoriteKey {
    std::string  opt_key;      // e.g. "layer_height"
    SettingsTabId tab_id;      // originating tab
    std::string  section_label; // human-readable section, e.g. "Quality › Layer"
    int          sort_order;   // position within originating tab for stable sort

    bool operator==(const FavoriteKey& o) const { return opt_key == o.opt_key; }
};

struct FavoriteKeyHash {
    size_t operator()(const FavoriteKey& k) const {
        return std::hash<std::string>{}(k.opt_key);
    }
};

// Singleton that owns the favorites set and persists it to the app config.
// Other UI components call toggle() / is_favorited() and subscribe via
// on_changed to refresh their star buttons.
class FavoritesManager
{
public:
    static FavoritesManager& get();

    // Returns true if the key was added (false = removed).
    bool toggle(const FavoriteKey& key);
    bool toggle(const std::string& opt_key);
    bool is_favorited(const std::string& opt_key) const;

    // Returns favorites sorted by (tab_id, sort_order) — the canonical
    // display order used by the Favorites panel.
    std::vector<FavoriteKey> sorted_favorites() const;

    // Register a callback to be invoked after any toggle.
    // Returns an id you can pass to unsubscribe().
    using ChangedCallback = std::function<void(const std::string& opt_key, bool is_now_favorited)>;
    int  subscribe(ChangedCallback cb);
    void unsubscribe(int id);

    // Persistence — call from app init / shutdown.
    void load_from_config();
    void save_to_config() const;

    void register_key(const FavoriteKey& key);
    
private:
    FavoritesManager() = default;
    FavoritesManager(const FavoritesManager&) = delete;
    FavoritesManager& operator=(const FavoritesManager&) = delete;

    std::unordered_set<FavoriteKey, FavoriteKeyHash> m_favorites;
    std::vector<std::pair<int, ChangedCallback>>     m_subscribers;
    int m_next_id { 0 };
    std::unordered_map<std::string, FavoriteKey> m_key_registry;

    void notify(const std::string& opt_key, bool added) const;
};

} // namespace GUI
} // namespace Slic3r