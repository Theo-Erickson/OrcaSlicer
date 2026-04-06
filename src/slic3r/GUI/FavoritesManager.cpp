#include "FavoritesManager.hpp"
#include "GUI_App.hpp"        // wxGetApp()
#include "libslic3r/AppConfig.hpp"      // AppConfig::set / get

#include <algorithm>
#include <sstream>

namespace Slic3r {
namespace GUI {

static constexpr const char* CONFIG_SECTION = "favorites";
static constexpr const char* CONFIG_KEY     = "favorited_opts";

FavoritesManager& FavoritesManager::get()
{
    static FavoritesManager instance;
    return instance;
}

bool FavoritesManager::toggle(const FavoriteKey& key)
{
    auto it = m_favorites.find(key);
    if (it != m_favorites.end()) {
        m_favorites.erase(it);
        notify(key.opt_key, false);
        save_to_config();
        return false;
    } else {
        m_favorites.insert(key);
        notify(key.opt_key, true);
        save_to_config();
        return true;
    }
}

bool FavoritesManager::toggle(const std::string& opt_key)
{
    FavoriteKey probe;
    probe.opt_key = opt_key;
    auto it = m_key_registry.find(opt_key);
    if (it != m_key_registry.end())
        probe = it->second;

    auto fit = m_favorites.find(probe);
    if (fit != m_favorites.end()) {
        m_favorites.erase(fit);
        BOOST_LOG_TRIVIAL(info) << "FAV: removed " << opt_key 
                                << " total=" << m_favorites.size();
        notify(opt_key, false);
        save_to_config();
        return false;
    }
    m_favorites.insert(probe);
    BOOST_LOG_TRIVIAL(info) << "FAV: added " << opt_key 
                            << " total=" << m_favorites.size();
    notify(opt_key, true);
    save_to_config();
    return true;
}

bool FavoritesManager::is_favorited(const std::string& opt_key) const
{
    // FavoriteKey equality is keyed on opt_key only.
    FavoriteKey probe;
    probe.opt_key = opt_key;
    return m_favorites.count(probe) > 0;
}

std::vector<FavoriteKey> FavoritesManager::sorted_favorites() const
{
    std::vector<FavoriteKey> out(m_favorites.begin(), m_favorites.end());
    std::stable_sort(out.begin(), out.end(), [](const FavoriteKey& a, const FavoriteKey& b) {
        if (a.tab_id != b.tab_id)
            return static_cast<int>(a.tab_id) < static_cast<int>(b.tab_id);
        return a.sort_order < b.sort_order;
    });
    return out;
}

int FavoritesManager::subscribe(ChangedCallback cb)
{
    int id = m_next_id++;
    m_subscribers.emplace_back(id, std::move(cb));
    return id;
}

void FavoritesManager::unsubscribe(int id)
{
    m_subscribers.erase(
        std::remove_if(m_subscribers.begin(), m_subscribers.end(),
                       [id](const auto& p) { return p.first == id; }),
        m_subscribers.end());
}

void FavoritesManager::notify(const std::string& opt_key, bool added) const
{
    for (const auto& [id, cb] : m_subscribers)
        cb(opt_key, added);
}

// --- Persistence ---------------------------------------------------------
// We store a semicolon-delimited list of "opt_key:tab_id:sort_order:section"
// in the [favorites] section of OrcaSlicer's AppConfig (orca_slicer.conf).

void FavoritesManager::save_to_config() const
{
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return;

    std::ostringstream ss;
    bool first = true;
    for (const auto& fav : m_favorites) {
        if (!first) ss << ';';
        first = false;
        ss << fav.opt_key << ':'
           << static_cast<int>(fav.tab_id) << ':'
           << fav.sort_order << ':'
           << fav.section_label;
    }
    cfg->set(CONFIG_SECTION, CONFIG_KEY, ss.str());
    cfg->save();
}

void FavoritesManager::register_key(const FavoriteKey& key)
{
    // Just record metadata. Does not add to favorites.
    m_key_registry[key.opt_key] = key;
}

void FavoritesManager::load_from_config()
{
    // Harcoded Tester
    /*FavoritesManager::get().register_key({
        "layer_height", SettingsTabId::Quality, "Quality › Layer height", 0
    });
    FavoritesManager::get().toggle("layer_height");
    */
    AppConfig* cfg = wxGetApp().app_config;
    if (!cfg) return;

    m_favorites.clear();
    const std::string raw = cfg->get(CONFIG_SECTION, CONFIG_KEY);
    if (raw.empty()) return;

    std::istringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ';')) {
        // Format: opt_key:tab_id:sort_order:section_label
        auto p1 = token.find(':');
        auto p2 = token.find(':', p1 + 1);
        auto p3 = token.find(':', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos)
            continue;

        FavoriteKey fav;
        fav.opt_key       = token.substr(0, p1);
        fav.tab_id        = static_cast<SettingsTabId>(std::stoi(token.substr(p1 + 1, p2 - p1 - 1)));
        fav.sort_order    = std::stoi(token.substr(p2 + 1, p3 - p2 - 1));
        fav.section_label = token.substr(p3 + 1);
        m_favorites.insert(fav);
    }
}

} // namespace GUI
} // namespace Slic3r