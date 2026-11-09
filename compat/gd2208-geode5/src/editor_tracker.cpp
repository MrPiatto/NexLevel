// GD 2.2081 / Geode v5 - editor adapter.
// Detects local edits and, while in a NexLevel session, turns them into ops;
// applies remote changes to GD objects without re-sending them (m_applyingRemote guard).
// Signatures copied from geode-sdk/bindings 2.2081/GeometryDash.bro.
// Windows-inline (not hookable): EditorUI::deleteObject, LevelEditorLayer::undoLastAction/redoLastAction,
// so deletion is confirmed at end of frame and undo/redo is caught at LevelEditorLayer::handleAction.
#include <Geode/Geode.hpp>
#include <Geode/modify/CustomizeObjectLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include <nexlevel/core/sync/object_props.hpp>

#include "level_settings_adapter.hpp"
#include "nexmenu.hpp"
#include "ping_hud.hpp"
#include "presence_overlay.hpp"
#include "session_manager.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;
namespace nls = nexlevel::sync;
using nexlevel::mod::SessionManager;

namespace {

constexpr std::size_t kMaxLogLinesPerKind = 20;  // per tick, so a big paste can't flood the log

// The exit (yellow) half of a teleport portal (object 749) is owned by its entry portal (747):
// GD creates and deletes it together with the entry, and the entry keeps a pointer to it
// (TeleportPortalObject::m_orangePortal). NexLevel syncs only the entry; touching the exit on its
// own breaks that link, makes GD create another exit, and can crash.
bool isPortalCompanion(CCNode* node) {
    auto* tp = typeinfo_cast<TeleportPortalObject*>(node);
    return tp && tp->m_isYellowPortal;
}

// Editor UI previews (build tab buttons) are GameObjects too; they live under EditorUI, not in the level.
// Companion objects GD manages itself are treated the same way: never synced on their own.
bool isUnderEditorUI(CCNode* node) {
    if (isPortalCompanion(node)) return true;
    for (auto* n = node ? node->getParent() : nullptr; n; n = n->getParent()) {
        if (typeinfo_cast<EditorUI*>(n)) return true;
    }
    return false;
}

template <class F>
void forEachObject(CCArray* objects, F&& f) {
    if (!objects) return;
    for (auto* o : CCArrayExt<GameObject*>(objects)) f(o);
}

// Touches currently held on the editor (EditorUI::ccTouchBegan..Ended hooks below).
int g_editorTouches = 0;

// True while the local player is in the middle of an edit: a touch/click is held, swipe is running,
// or Shift (continuous building) is held. GD keeps raw pointers to the objects it is placing,
// dragging and swiping until the gesture ends, so remote changes that remove or replace objects
// must wait until then (doing it mid-gesture corrupts the heap), and local changes are sent as
// one batch when the gesture ends.
// Any GD popup open over the editor (trigger settings, Edit Object, colour picker, group IDs...)
// keeps pointers to the objects it edits until it closes. NexLevel's own menu doesn't count.
bool editPopupOpen() {
    auto* scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return false;
    for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
        if (!child || !child->isVisible()) continue;
        if (typeinfo_cast<FLAlertLayer*>(child) && !typeinfo_cast<nexlevel::mod::NexMenu*>(child)) return true;
    }
    return false;
}

bool editorBusy(EditorUI* ui) {
    if (g_editorTouches > 0) return true;
    if (ui && ui->m_swipeActive) return true;
    if (auto* kb = CCKeyboardDispatcher::get(); kb && kb->getShiftKeyPressed()) return true;
    return false;
}

class EditorTracker : public nexlevel::mod::IEditorBridge {
public:
    explicit EditorTracker(LevelEditorLayer* layer) : m_layer(layer) {}
    ~EditorTracker() override { dropAll(); }

    void setPaused(bool paused) { m_paused = paused; }

    // ---------- playtest spectating ----------
    void beginPlaytest() {
        ++m_playSession;
        m_playtesting = true;
    }
    void endPlaytest() {
        if (!m_playtesting) return;
        m_playtesting = false;
        auto st = playerState();
        st.flags = 0;  // stopped: the others keep the trail and hide the icon
        SessionManager::get()->setLocalPlayerState(st);
    }

    void playtestTick() {
        if (m_playtesting && SessionManager::get()->inSession()) SessionManager::get()->setLocalPlayerState(playerState());
    }

    static std::uint8_t modeOf(PlayerObject* p) {
        if (p->m_isShip) return 1;
        if (p->m_isBall) return 2;
        if (p->m_isBird) return 3;
        if (p->m_isDart) return 4;
        if (p->m_isRobot) return 5;
        if (p->m_isSpider) return 6;
        if (p->m_isSwing) return 7;
        return 0;
    }

    nexlevel::session::PlayerState playerState() {
        nexlevel::session::PlayerState st;
        st.session = m_playSession;
        auto* p1 = m_layer->m_player1;
        if (!p1) return st;
        st.flags = nexlevel::session::PlayerState::Playing;
        // The player's own icons and colour (GameManager::activeIconForType / colorForIdx).
        auto* gm = GameManager::sharedState();
        const auto col = gm->colorForIdx(gm->m_playerColor);
        st.r = col.r;
        st.g = col.g;
        st.b = col.b;
        if (p1->m_isDead) st.flags |= nexlevel::session::PlayerState::Dead;
        if (p1->m_isUpsideDown) st.flags |= nexlevel::session::PlayerState::UpsideDown1;
        st.mode1 = modeOf(p1);
        st.icon1 = static_cast<std::uint16_t>(std::clamp(gm->activeIconForType(static_cast<IconType>(st.mode1)), 1, 1999));
        st.x1 = p1->getPositionX();
        st.y1 = p1->getPositionY();
        st.rot1 = p1->getRotation();
        st.scale1 = p1->m_vehicleSize;
        auto* p2 = m_layer->m_player2;
        if (p2 && m_layer->m_gameState.m_isDualMode) {
            st.flags |= nexlevel::session::PlayerState::Dual;
            if (p2->m_isUpsideDown) st.flags |= nexlevel::session::PlayerState::UpsideDown2;
            st.mode2 = modeOf(p2);
            st.icon2 = static_cast<std::uint16_t>(std::clamp(gm->activeIconForType(static_cast<IconType>(st.mode2)), 1, 1999));
            st.x2 = p2->getPositionX();
            st.y2 = p2->getPositionY();
            st.rot2 = p2->getRotation();
            st.scale2 = p2->m_vehicleSize;
        }
        return st;
    }
    LevelEditorLayer* layer() const { return m_layer; }
    GJGameLevel* level() const override { return m_layer->m_level; }
    void switchLevel(GJGameLevel* level) override { m_layer->m_level = level; }
    std::string levelString() const override { return std::string(m_layer->getLevelString()); }

    // ---------- local edit detection (ignored while applying remote changes) ----------

    bool ignoring() const { return m_paused || m_applyingRemote; }

    void snapshotIfUnknown(GameObject* o) {
        if (!o || ignoring() || m_cache.contains(o)) return;
        if (isUnderEditorUI(o)) {
            if (isPortalCompanion(o)) markEntryOf(o);
            return;
        }
        cachePut(o, read(o));
    }
    void markDirty(GameObject* o) {
        if (!o || ignoring()) return;
        if (isPortalCompanion(o)) { markEntryOf(o); return; }
        m_dirty.insert(o);
    }
    // The entry portal whose exit this is (its save string holds the exit's offset).
    void markEntryOf(GameObject* exit) {
        if (auto it = m_portalEntry.find(exit); it != m_portalEntry.end() && m_cache.contains(it->second)) {
            auto* tp = typeinfo_cast<TeleportPortalObject*>(it->second);
            if (tp && tp->m_orangePortal == exit) {
                m_dirty.insert(it->second);
                return;
            }
            m_portalEntry.erase(it);
        }
        for (auto& [o, _] : m_cache) {
            auto* tp = o->m_objectID == 747 ? typeinfo_cast<TeleportPortalObject*>(o) : nullptr;
            if (tp && tp->m_orangePortal == exit) {
                m_portalEntry[exit] = o;
                m_dirty.insert(o);
                return;
            }
        }
    }
    void markAllDirty() {
        if (ignoring()) return;
        for (auto& [o, _] : m_cache) m_dirty.insert(o);
    }
    void maybeRemoved(GameObject* o) {
        if (o && !ignoring() && m_armed) m_pendingRemoval.insert(o);
    }
    void cancelRemoval(GameObject* o) {
        if (!ignoring()) m_pendingRemoval.erase(o);
    }

    // Idempotent: createObject also goes through addToSection, so a known object is ignored.
    void onCreated(GameObject* o, const char* via) {
        if (!o || ignoring() || !m_armed || m_cache.contains(o) || isUnderEditorUI(o)) return;
        auto props = read(o);
        if (m_createdLogged++ < kMaxLogLinesPerKind) {
            log::debug("NexLevel diff: created uid={} id={} ({} props, via {})", o->m_uniqueID, o->m_objectID, props.size(), via);
        }
        publishCreate(o, props);
        cachePut(o, std::move(props));
        m_dirty.erase(o);
    }

    void onRemoved(GameObject* o) {
        if (!o || ignoring() || !m_armed) return;
        // removeObject and the end-of-frame section check can both report the same removal.
        if (!m_removedThisFrame.insert(o).second) return;
        if (m_removedLogged++ < kMaxLogLinesPerKind) {
            // The object may already be freed: log from the cache, never dereference it.
            auto c = m_cache.find(o);
            const std::string kind = c != m_cache.end() && c->second.contains(1) ? c->second.at(1) : "?";
            log::debug("NexLevel diff: removed object id={}", kind);
        }
        if (auto it = m_objToId.find(o); it != m_objToId.end()) {
            send(nls::Op::erase(it->second));
            unmap(o);
        }
        forget(o);
    }

    // Re-read every cached object without logging (after playtest, GD restores object state).
    void resyncSilently() {
        for (auto& [o, props] : m_cache) props = read(o);
        m_dirty.clear();
        m_settingsCache = nexlevel::mod::readLevelSettings(m_layer);
    }

    // Level settings have no hookable "changed" event for every screen that edits them
    // (settings layer, colour list, song picker), so they are compared a few times per second.
    void checkLevelSettings() {
        if (++m_settingsTick < 20) return;
        m_settingsTick = 0;
        if (m_settingsDeferred && !settingsScreenOpen()) {
            // Publish what the player changed on the screen first, then take the room's values.
            auto mine = nexlevel::mod::readLevelSettings(m_layer);
            auto* sm = SessionManager::get();
            auto d = nls::diffProps(m_settingsCache, mine);
            if (!d.empty() && sm->inSession() && sm->view() && sm->view()->find(nls::kLevelSettingsId)) {
                sm->submitLocal(nls::Op::setProps(nls::kLevelSettingsId, d));
            }
            if (auto* view = sm->view()) {
                RemoteScope scope(*this);
                applySettingsFromView(*view);
            }
            return;
        }
        auto now = nexlevel::mod::readLevelSettings(m_layer);
        if (now.empty()) return;
        if (!m_settingsKnown) {
            m_settingsCache = std::move(now);
            m_settingsKnown = true;
            return;
        }
        if (now == m_settingsCache) return;
        auto d = nls::diffProps(m_settingsCache, now);
        m_settingsCache = std::move(now);
        auto* sm = SessionManager::get();
        if (!sm->inSession() || d.empty()) return;
        log::info("NexLevel: level settings changed: {}", nls::describe(d));
        auto* view = sm->view();
        if (view && view->find(nls::kLevelSettingsId)) sm->submitLocal(nls::Op::setProps(nls::kLevelSettingsId, d));
        else if (sm->role() == nexlevel::mod::Role::Host) sm->submitLocal(nls::Op::create(nls::kLevelSettingsId, m_settingsCache));
    }

    // While the local player has the level settings screen open, its stale values would be written
    // back on close; apply remote settings once it is closed.
    static bool settingsScreenOpen() {
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        return scene && scene->getChildByType<LevelSettingsLayer>(0) != nullptr;
    }

    void applySettingsFromView(const nls::Replica& view) {
        if (settingsScreenOpen()) {
            m_settingsDeferred = true;
            return;
        }
        m_settingsDeferred = false;
        if (const auto* st = view.find(nls::kLevelSettingsId)) {
            nexlevel::mod::applyLevelSettings(m_layer, st->props);
        }
        m_settingsCache = nexlevel::mod::readLevelSettings(m_layer);
        m_settingsKnown = true;
    }

    void tick(CCArray* selected, GameObject* single) {
        if (!m_armed) {  // first frame: the level has finished loading
            m_armed = true;
            SessionManager::get()->attachEditor(this);
            return;
        }
        const bool wasBusy = m_busy;
        m_busy = editorBusy(EditorUI::get());
        // Safety net: a touch-end or key-up that never arrives (focus lost mid-gesture) must not
        // hold the room back forever. (An open popup has no time limit: it really is in use.)
        if (m_busy) {
            if (!wasBusy) m_busySince = std::chrono::steady_clock::now();
            else if (std::chrono::steady_clock::now() - m_busySince > std::chrono::seconds(30)) {
                log::warn("NexLevel: editor looked busy for 30 s, releasing held changes");
                g_editorTouches = 0;
                m_busy = false;
            }
        }
        if (editPopupOpen()) m_busy = true;
        if (wasBusy && !m_busy) flushOutbox();  // before this frame's own changes, to keep the order
        playtestTick();  // rate limited to ~30/s inside
        if (!ignoring()) {
            reconcileRemovals();
            if (selected) {
                for (auto* o : CCArrayExt<GameObject*>(selected)) {
                    snapshotIfUnknown(o);
                    m_dirty.insert(o);
                }
            }
            // A single selection lives in EditorUI::m_selectedObject, not in the selection array.
            if (single) {
                snapshotIfUnknown(single);
                m_dirty.insert(single);
            }
            std::size_t changedLogged = 0, changedTotal = 0;
            for (auto* o : m_dirty) {
                auto it = m_cache.find(o);
                if (it == m_cache.end()) continue;
                auto now = read(o);
                auto d = nls::diffProps(it->second, now);
                if (d.empty()) continue;
                ++changedTotal;
                if (changedLogged++ < kMaxLogLinesPerKind) {
                    log::debug("NexLevel diff: changed uid={} id={}: {}", o->m_uniqueID, o->m_objectID, nls::describe(d));
                }
                publishChange(o, now, d);
                it->second = std::move(now);
            }
            flushSummary("changed", changedTotal);
            checkLevelSettings();
            if (SessionManager::get()->inSession()) sweep();
        }
        flushSummary("created", m_createdLogged);
        flushSummary("removed", m_removedLogged);
        m_createdLogged = m_removedLogged = 0;
        m_dirty.clear();
        m_removedThisFrame.clear();

        // "Placed by" in the editor label for a single selected object (its id says who created it).
        {
            GameObject* only = single;
            if (!only && selected && selected->count() == 1) only = static_cast<GameObject*>(selected->objectAtIndex(0));
            std::uint16_t creator = 0;
            if (only && SessionManager::get()->inSession()) {
                if (auto it = m_objToId.find(only); it != m_objToId.end()) creator = it->second.peer();
            }
            nexlevel::mod::setSelectionCreator(creator);
        }
        nexlevel::mod::updatePresence(m_layer, m_paused);

        // Remote changes held back while the local user had the object selected or was mid-gesture.
        if (!m_deferred.empty() && !m_paused && !m_busy) {
            std::vector<nls::NetId> retry(m_deferred.begin(), m_deferred.end());
            m_deferred.clear();
            applyRemote(retry);
        }
    }

    void detach() { SessionManager::get()->detachEditor(this); }

    // Safety net for edits made by paths no hook sees (some tools, triggers' settings screens,
    // other mods): a few objects per frame are re-read and compared, and after each full pass
    // anything that left the level unnoticed is reported. ~100 objects per frame: a 50k-object
    // level is fully checked every few seconds.
    void sweep() {
        auto* arr = m_layer->m_objects;
        if (!arr) return;
        const unsigned n = arr->count();
        const unsigned end = std::min(n, m_sweepIndex + kSweepPerFrame);
        for (unsigned i = m_sweepIndex; i < end; ++i) {
            auto* o = static_cast<GameObject*>(arr->objectAtIndex(i));
            if (!o || o->m_isSelected || isUnderEditorUI(o)) continue;  // selected ones are diffed every frame already
            auto it = m_cache.find(o);
            if (it == m_cache.end()) {
                onCreated(o, "sweep");
                continue;
            }
            auto now = read(o);
            auto d = nls::diffProps(it->second, now);
            if (d.empty()) continue;
            log::debug("NexLevel diff: sweep found a change uid={} id={}: {}", o->m_uniqueID, o->m_objectID, nls::describe(d));
            publishChange(o, now, d);
            it->second = std::move(now);
        }
        m_sweepIndex = end;
        if (m_sweepIndex < n) return;
        m_sweepIndex = 0;
        // Full pass done: objects we know about that are no longer in the level.
        std::unordered_set<GameObject*> live;
        live.reserve(n);
        for (unsigned i = 0; i < n; ++i) live.insert(static_cast<GameObject*>(arr->objectAtIndex(i)));
        std::vector<GameObject*> gone;
        for (auto& [o, _] : m_cache) {
            if (!live.contains(o)) gone.push_back(o);
        }
        for (auto* o : gone) onRemoved(o);
        m_removedThisFrame.clear();
    }

    // ---------- IEditorBridge: session -> editor ----------

    void seedSession() override {
        auto* sm = SessionManager::get();
        std::size_t n = 0;
        forEachLevelObject([&](GameObject* o) {
            auto props = read(o);
            auto id = sm->newId();
            if (!id || props.empty()) return;
            map(o, *id);
            sm->submitLocal(nls::Op::create(*id, props));
            cachePut(o, std::move(props));
            ++n;
        });
        log::info("NexLevel: published {} existing objects to the room", n);
        m_settingsCache = nexlevel::mod::readLevelSettings(m_layer);
        m_settingsKnown = true;
        if (!m_settingsCache.empty()) sm->submitLocal(nls::Op::create(nls::kLevelSettingsId, m_settingsCache));
    }

    // Make the editor match the room (joining, or the host restarted). Objects that are already
    // identical are kept and paired (adoptFromSession), so only real differences cost anything.
    void rebuildFromSession() override {
        auto* view = SessionManager::get()->view();
        if (!view) return;
        if (auto* ui = EditorUI::get()) ui->deselectAll();  // no selection may point at removed objects
        // Old undo/redo entries point at objects that may no longer be part of the shared level.
        if (m_layer->m_undoObjects) m_layer->m_undoObjects->removeAllObjects();
        if (m_layer->m_redoObjects) m_layer->m_redoObjects->removeAllObjects();
        dropAll();
        m_dirty.clear();
        m_pendingRemoval.clear();
        m_deferred.clear();
        adoptFromSession();
    }

    // The editor just loaded a level string written from the room state (host coming back into the
    // editor, or a player who joined from the main menu): GD's own loader already built everything,
    // so instead of removing and re-creating the whole level, pair each object with its room id by
    // content. Only what differs (changes that arrived meanwhile) is removed or created.
    void adoptFromSession() override {
        auto* view = SessionManager::get()->view();
        if (!view) return;
        RemoteScope scope(*this);
        std::unordered_map<std::string, std::vector<nls::NetId>> byContent;
        byContent.reserve(view->objects().size());
        for (const auto& [id, state] : view->objects()) {
            if (!nls::isLevelSettings(id) && spawnable(state.props)) byContent[nls::toSaveString(state.props)].push_back(id);
        }
        std::vector<GameObject*> extras;
        std::size_t matched = 0;
        forEachLevelObject([&](GameObject* o) {
            auto props = read(o);
            auto it = byContent.find(nls::toSaveString(props));
            if (it == byContent.end() || it->second.empty()) {
                extras.push_back(o);
                return;
            }
            map(o, it->second.back());
            it->second.pop_back();
            cachePut(o, std::move(props));
            ++matched;
        });
        for (auto* o : extras) o->retain();
        for (auto* o : extras) {
            if (o->getParent()) removeForRemote(o);
        }
        for (auto* o : extras) o->release();
        std::vector<std::pair<nls::NetId, const nls::PropMap*>> missing;
        for (auto& [_, ids] : byContent) {
            for (auto id : ids) {
                if (const auto* st = view->find(id)) missing.emplace_back(id, &st->props);
            }
        }
        spawnList(missing);
        applySettingsFromView(*view);
        log::info("NexLevel: editor matched to the room: {} objects kept, {} created, {} removed", matched, missing.size(),
                  extras.size());
    }

    void applyRemote(const std::vector<nls::NetId>& ids) override {
        auto* view = SessionManager::get()->view();
        if (!view || m_paused) {
            for (auto id : ids) m_deferred.insert(id);
            return;
        }
        RemoteScope scope(*this);
        log::debug("NexLevel: applying {} remote change(s)", ids.size());
        for (auto id : ids) {
            if (nls::isLevelSettings(id)) {  // not a GameObject: level-wide settings
                log::debug("NexLevel trace: level settings update");
                if (view->find(id)) applySettingsFromView(*view);
                continue;
            }
            const auto* state = view->find(id);
            auto it = m_idToObj.find(id);
            GameObject* obj = it == m_idToObj.end() ? nullptr : it->second;
            if (obj && m_busy) {  // never remove/replace an object while GD is mid-gesture
                m_deferred.insert(id);
                continue;
            }
            if (!state) {
                if (obj) {
                    log::debug("NexLevel trace: delete {} (obj id {})", id.value, obj->m_objectID);
                    removeForRemote(obj);  // our references keep it alive until unmap/forget
                    unmap(obj);
                    forget(obj);
                }
                continue;
            }
            if (!obj) {
                spawn(id, state->props);
                continue;
            }
            auto cached = m_cache.find(obj);
            if (cached != m_cache.end() && cached->second == state->props) continue;  // already up to date
            if (obj->m_isSelected) {  // don't yank an object out from under the local user
                m_deferred.insert(id);
                continue;
            }
            log::debug("NexLevel trace: replace {} (obj id {})", id.value, obj->m_objectID);
            removeForRemote(obj);
            unmap(obj);
            forget(obj);
            spawn(id, state->props);
            log::debug("NexLevel trace: replaced {}", id.value);
        }
    }

private:
    struct RemoteScope {
        EditorTracker& t;
        explicit RemoteScope(EditorTracker& tracker) : t(tracker) { t.m_applyingRemote = true; }
        ~RemoteScope() { t.m_applyingRemote = false; }
    };

    template <class F>
    void forEachLevelObject(F&& f) {
        if (!m_layer->m_objects) return;
        for (auto* o : CCArrayExt<GameObject*>(m_layer->m_objects)) {
            if (!isUnderEditorUI(o)) f(o);
        }
    }

    // Objects from the network must be something GD can build: a known object id (key 1).
    static bool spawnable(const nls::PropMap& props) {
        auto it = props.find(1);
        if (it == props.end()) return false;
        int objectId = 0;
        try { objectId = std::stoi(it->second); } catch (...) { return false; }
        return objectId > 0 && objectId < 10000 && objectId != 749;  // 749: portal exit, created by its 747
    }

    void spawn(nls::NetId id, const nls::PropMap& props) {
        if (!spawnable(props)) {
            log::warn("NexLevel: skipped an object that GD cannot create (net id {})", id.value);
            return;
        }
        auto str = nls::toSaveString(props) + ";";
        log::debug("NexLevel trace: spawn {}: {}", id.value, str.substr(0, 120));
        auto* objs = m_layer->createObjectsFromString(str, true, true);
        if (!objs || objs->count() == 0) {
            log::warn("NexLevel: could not create object for net id {}", id.value);
            return;
        }
        auto* o = static_cast<GameObject*>(objs->objectAtIndex(0));
        map(o, id);
        cachePut(o, read(o));
    }

    // Whole level at once (join / rebuild): one createObjectsFromString call per batch is far
    // faster than one per object. Batches map back to ids by order; if GD skipped any object in a
    // batch, that batch is redone one object at a time so every id still gets its object.
    void spawnList(const std::vector<std::pair<nls::NetId, const nls::PropMap*>>& todo) {
        constexpr std::size_t kBatch = 2000;
        for (std::size_t start = 0; start < todo.size(); start += kBatch) {
            const std::size_t end = std::min(todo.size(), start + kBatch);
            std::string str;
            for (std::size_t i = start; i < end; ++i) str += nls::toSaveString(*todo[i].second) + ";";
            auto* objs = m_layer->createObjectsFromString(str, true, true);
            if (objs && objs->count() == end - start) {
                for (std::size_t i = start; i < end; ++i) {
                    auto* o = static_cast<GameObject*>(objs->objectAtIndex(static_cast<unsigned>(i - start)));
                    map(o, todo[i].first);
                    cachePut(o, read(o));
                }
                continue;
            }
            log::warn("NexLevel: batch spawn created {} of {} objects, retrying one by one", objs ? objs->count() : 0, end - start);
            if (objs) {
                for (auto* o : CCArrayExt<GameObject*>(objs)) {
                    releaseFromEditor(o);
                    m_layer->removeObject(o, true);
                }
            }
            for (std::size_t i = start; i < end; ++i) spawn(todo[i].first, *todo[i].second);
        }
    }

    // Local object ops: queued while the player is mid-gesture, sent in order when it ends.
    void send(nls::Op op) {
        auto* sm = SessionManager::get();
        if (!sm->inSession()) return;
        if (m_busy) m_outbox.push_back(std::move(op));
        else sm->submitLocal(std::move(op));
    }
    void flushOutbox() {
        if (m_outbox.empty()) return;
        auto* sm = SessionManager::get();
        log::debug("NexLevel: sending {} change(s) made during the gesture", m_outbox.size());
        if (sm->inSession()) {
            for (auto& op : m_outbox) sm->submitLocal(std::move(op));
        }
        m_outbox.clear();
    }

    void publishCreate(GameObject* o, const nls::PropMap& props) {
        auto* sm = SessionManager::get();
        if (!sm->inSession() || m_objToId.contains(o)) return;
        auto id = sm->newId();
        if (!id) return;
        map(o, *id);
        send(nls::Op::create(*id, props));
    }

    void publishChange(GameObject* o, const nls::PropMap& now, const nls::PropDiff& d) {
        auto* sm = SessionManager::get();
        if (!sm->inSession()) return;
        auto it = m_objToId.find(o);
        if (it == m_objToId.end()) {
            publishCreate(o, now);  // touched for the first time since joining
            return;
        }
        send(nls::Op::setProps(it->second, d));
    }

    // Every object NexLevel tracks is retained (cache and id maps hold one reference each), so no
    // pointer we keep can ever point at freed memory, whatever path GD uses to delete objects.
    void cachePut(GameObject* o, nls::PropMap props) {
        auto [it, inserted] = m_cache.try_emplace(o);
        if (inserted) o->retain();
        it->second = std::move(props);
    }
    void map(GameObject* o, nls::NetId id) {
        if (auto old = m_idToObj.find(id); old != m_idToObj.end() && old->second != o) unmap(old->second);
        if (auto prev = m_objToId.find(o); prev != m_objToId.end()) {
            if (prev->second == id) return;
            m_idToObj.erase(prev->second);
            prev->second = id;
        } else {
            m_objToId.emplace(o, id);
            o->retain();
        }
        m_idToObj[id] = o;
    }
    void dropAll() {
        for (auto& [o, _] : m_objToId) o->release();
        for (auto& [o, _] : m_cache) o->release();
        m_objToId.clear();
        m_idToObj.clear();
        m_cache.clear();
        m_portalEntry.clear();
    }
    // The editor UI keeps raw pointers to the selection: an object removed by a remote change must
    // leave the selection first, or the next frame touches a freed object.
    // Also leave its linked (sticky) group, like GD's own delete does (EditorUI::deleteTypeFromObjects).
    // Removes an object for a remote change. A teleport entry takes its exit with it (if GD didn't).
    void removeForRemote(GameObject* o) {
        GameObject* exit = nullptr;
        if (auto* tp = typeinfo_cast<TeleportPortalObject*>(o); tp && !tp->m_isYellowPortal && tp->m_orangePortal) {
            exit = tp->m_orangePortal;
            exit->retain();
            releaseFromEditor(exit);
        }
        releaseFromEditor(o);
        m_layer->removeObject(o, true);
        if (exit) {
            if (exit->getParent()) m_layer->removeObject(exit, true);
            exit->release();
        }
    }

    void releaseFromEditor(GameObject* o) {
        if (!o) return;
        if (o->m_isSelected) {
            if (auto* ui = EditorUI::get()) ui->deselectObject(o);
        }
        if (o->m_linkedGroup > 0) {
            if (auto* group = m_layer->getStickyGroup(o->m_linkedGroup)) group->removeObject(o, true);
        }
    }
    // unmap/forget may drop the last reference: callers must not use `o` afterwards except as a key.
    void unmap(GameObject* o) {
        if (auto it = m_objToId.find(o); it != m_objToId.end()) {
            m_idToObj.erase(it->second);
            m_objToId.erase(it);
            o->release();
        }
    }
    void forget(GameObject* o) {
        m_dirty.erase(o);
        m_pendingRemoval.erase(o);
        if (auto it = m_cache.find(o); it != m_cache.end()) {
            m_cache.erase(it);
            o->release();
        }
    }

    void reconcileRemovals() {
        if (m_pendingRemoval.empty()) return;
        std::unordered_set<GameObject*> inLevel;
        if (m_layer->m_objects) {
            for (auto* o : CCArrayExt<GameObject*>(m_layer->m_objects)) inLevel.insert(o);
        }
        auto pending = std::move(m_pendingRemoval);
        m_pendingRemoval.clear();
        for (auto* o : pending) {
            if (!inLevel.contains(o)) onRemoved(o);
        }
    }

    static void flushSummary(const char* kind, std::size_t total) {
        if (total > kMaxLogLinesPerKind) {
            log::debug("NexLevel diff: ... {} more {} this frame", total - kMaxLogLinesPerKind, kind);
        }
    }

    nls::PropMap read(GameObject* o) {
        std::string s = o->getSaveString(m_layer);
        auto p = nls::parseSaveString(s);
        if (!p) {
            log::warn("NexLevel diff: unparsable save string for uid={} id={}", o->m_uniqueID, o->m_objectID);
            return {};
        }
        return std::move(*p);
    }

    LevelEditorLayer* m_layer;
    std::unordered_map<GameObject*, nls::PropMap> m_cache;
    std::unordered_map<GameObject*, nls::NetId> m_objToId;
    std::unordered_map<nls::NetId, GameObject*, nls::NetIdHash> m_idToObj;
    std::unordered_set<GameObject*> m_dirty;
    std::unordered_set<GameObject*> m_pendingRemoval;
    std::unordered_set<GameObject*> m_removedThisFrame;
    std::unordered_set<nls::NetId, nls::NetIdHash> m_deferred;
    std::size_t m_createdLogged = 0, m_removedLogged = 0;
    bool m_paused = false;
    bool m_armed = false;
    bool m_applyingRemote = false;
    std::unordered_map<GameObject*, GameObject*> m_portalEntry;  // exit -> entry (lookup cache)
    bool m_busy = false;              // local gesture in progress (editorBusy)
    std::vector<nls::Op> m_outbox;    // local ops held during the gesture
    std::chrono::steady_clock::time_point m_busySince{};
    nls::PropMap m_settingsCache;
    int m_settingsTick = 0;
    bool m_settingsKnown = false;
    bool m_settingsDeferred = false;
    bool m_playtesting = false;
    unsigned m_sweepIndex = 0;
    static constexpr unsigned kSweepPerFrame = 100;
    std::uint16_t m_playSession = 0;
};

// Owned by the active EditorUI's fields; null outside the editor.
EditorTracker* g_tracker = nullptr;

} // namespace

class $modify(NLEditorUI, EditorUI) {
    struct Fields {
        std::unique_ptr<EditorTracker> tracker;
        ~Fields() {
            if (tracker) {
                tracker->detach();
                if (g_tracker == tracker.get()) g_tracker = nullptr;
            }
        }
    };

    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;
        m_fields->tracker = std::make_unique<EditorTracker>(editorLayer);
        g_tracker = m_fields->tracker.get();
        g_editorTouches = 0;
        this->schedule(schedule_selector(NLEditorUI::nlTick));
        log::info("NexLevel editor tracker active");
        return true;
    }

    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        const bool claimed = EditorUI::ccTouchBegan(touch, event);
        if (claimed) ++g_editorTouches;
        return claimed;
    }
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        EditorUI::ccTouchEnded(touch, event);
        g_editorTouches = std::max(0, g_editorTouches - 1);
    }
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        EditorUI::ccTouchCancelled(touch, event);
        g_editorTouches = std::max(0, g_editorTouches - 1);
    }

    void nlTick(float) {
        if (g_tracker) g_tracker->tick(this->getSelectedObjects(), m_selectedObject);
        nexlevel::mod::updatePingHud(this);
    }

    void onDeleteSelected(CCObject* sender) {
        if (g_tracker) forEachObject(this->getSelectedObjects(), [](GameObject* o) { g_tracker->maybeRemoved(o); });
        if (g_tracker && m_selectedObject) g_tracker->maybeRemoved(m_selectedObject);
        EditorUI::onDeleteSelected(sender);
    }

    void moveObject(GameObject* object, CCPoint offset) {
        if (g_tracker) g_tracker->snapshotIfUnknown(object);
        EditorUI::moveObject(object, offset);
        if (g_tracker) g_tracker->markDirty(object);
    }

    void transformObject(GameObject* object, EditCommand command, bool noOffset) {
        if (g_tracker) g_tracker->snapshotIfUnknown(object);
        EditorUI::transformObject(object, command, noOffset);
        if (g_tracker) g_tracker->markDirty(object);
    }

    void rotateObjects(CCArray* objects, float rotation, CCPoint pivotPoint) {
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->snapshotIfUnknown(o); });
        EditorUI::rotateObjects(objects, rotation, pivotPoint);
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->markDirty(o); });
    }

    void scaleObjects(CCArray* objects, float scaleX, float scaleY, CCPoint pivotPoint, ObjectScaleType type, bool lockMove) {
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->snapshotIfUnknown(o); });
        EditorUI::scaleObjects(objects, scaleX, scaleY, pivotPoint, type, lockMove);
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->markDirty(o); });
    }

    void flipObjectsX(CCArray* objects) {
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->snapshotIfUnknown(o); });
        EditorUI::flipObjectsX(objects);
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->markDirty(o); });
    }

    void flipObjectsY(CCArray* objects) {
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->snapshotIfUnknown(o); });
        EditorUI::flipObjectsY(objects);
        if (g_tracker) forEachObject(objects, [](GameObject* o) { g_tracker->markDirty(o); });
    }
};

class $modify(NLLevelEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* level, bool noUI) {
        nexlevel::mod::SessionManager::get()->beforeEditorLoads(level);  // before GD reads the level
        return LevelEditorLayer::init(level, noUI);
    }

    GameObject* createObject(int key, CCPoint position, bool noUndo) {
        auto obj = LevelEditorLayer::createObject(key, position, noUndo);
        if (g_tracker) g_tracker->onCreated(obj, "createObject");
        return obj;
    }

    CCArray* createObjectsFromString(gd::string const& str, bool noUndo, bool noLimit) {
        auto objs = LevelEditorLayer::createObjectsFromString(str, noUndo, noLimit);
        if (g_tracker) forEachObject(objs, [](GameObject* o) { g_tracker->onCreated(o, "createObjectsFromString"); });
        return objs;
    }

    void removeObject(GameObject* object, bool noUndo) {
        if (g_tracker) g_tracker->onRemoved(object);
        LevelEditorLayer::removeObject(object, noUndo);
    }

    void handleAction(bool undo, CCArray* undoObjects) {
        LevelEditorLayer::handleAction(undo, undoObjects);
        if (g_tracker) {
            log::debug("NexLevel diff: {} applied", undo ? "undo" : "redo");
            g_tracker->markAllDirty();
        }
    }

    // Runs every frame while playtesting (physics), even if the editor UI is paused.
    void postUpdate(float dt) {
        LevelEditorLayer::postUpdate(dt);
        if (g_tracker && static_cast<LevelEditorLayer*>(g_tracker->layer()) == this) g_tracker->playtestTick();
    }

    void onPlaytest() {
        if (g_tracker) {
            g_tracker->setPaused(true);
            g_tracker->beginPlaytest();
        }
        LevelEditorLayer::onPlaytest();
    }

    void onStopPlaytest() {
        if (g_tracker) g_tracker->endPlaytest();  // before GD resets the player
        LevelEditorLayer::onStopPlaytest();
        if (g_tracker) {
            g_tracker->setPaused(false);
            g_tracker->resyncSilently();
        }
    }
};

// Catch-all for objects entering/leaving the level by paths that skip createObject/removeObject.
class $modify(NLGJBaseGameLayer, GJBaseGameLayer) {
    void addToSection(GameObject* object) {
        GJBaseGameLayer::addToSection(object);
        if (g_tracker && static_cast<GJBaseGameLayer*>(g_tracker->layer()) == this && !isUnderEditorUI(object)) {
            g_tracker->cancelRemoval(object);
            g_tracker->onCreated(object, "addToSection");
        }
    }

    void removeObjectFromSection(GameObject* object) {
        if (g_tracker && static_cast<GJBaseGameLayer*>(g_tracker->layer()) == this) {
            g_tracker->maybeRemoved(object);
        }
        GJBaseGameLayer::removeObjectFromSection(object);
    }
};

// Edit Object popup (colours, HSV, channels). Snapshot targets when it opens, diff them when it closes.
class $modify(NLCustomizeObjectLayer, CustomizeObjectLayer) {
    static void forTargets(GameObject* single, CCArray* many, bool snapshot) {
        if (!g_tracker) return;
        auto visit = [snapshot](GameObject* o) {
            if (snapshot) g_tracker->snapshotIfUnknown(o);
            else g_tracker->markDirty(o);
        };
        if (single) visit(single);
        forEachObject(many, visit);
    }

    bool init(GameObject* object, CCArray* objects) {
        if (!CustomizeObjectLayer::init(object, objects)) return false;
        forTargets(m_targetObject, m_targetObjects, true);
        return true;
    }

    void onClose(CCObject* sender) {
        forTargets(m_targetObject, m_targetObjects, false);
        CustomizeObjectLayer::onClose(sender);
    }

    void keyBackClicked() {
        forTargets(m_targetObject, m_targetObjects, false);
        CustomizeObjectLayer::keyBackClicked();
    }
};
