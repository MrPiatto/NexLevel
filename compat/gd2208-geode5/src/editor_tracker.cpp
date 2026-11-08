// GD 2.2081 / Geode v5 - editor adapter.
// Detects local edits (Phase 1 tracker) and, while in a NexLevel session, turns them into ops;
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

#include "session_manager.hpp"

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

// Editor UI previews (build tab buttons) are GameObjects too; they live under EditorUI, not in the level.
bool isUnderEditorUI(CCNode* node) {
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

class EditorTracker : public nexlevel::mod::IEditorBridge {
public:
    explicit EditorTracker(LevelEditorLayer* layer) : m_layer(layer) {}

    void setPaused(bool paused) { m_paused = paused; }
    LevelEditorLayer* layer() const { return m_layer; }
    GJGameLevel* level() const override { return m_layer->m_level; }

    // ---------- local edit detection (ignored while applying remote changes) ----------

    bool ignoring() const { return m_paused || m_applyingRemote; }

    void snapshotIfUnknown(GameObject* o) {
        if (!o || ignoring() || m_cache.contains(o)) return;
        m_cache.emplace(o, read(o));
    }
    void markDirty(GameObject* o) {
        if (o && !ignoring()) m_dirty.insert(o);
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
        if (!o || ignoring() || !m_armed || m_cache.contains(o)) return;
        auto props = read(o);
        if (m_createdLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: created uid={} id={} ({} props, via {})", o->m_uniqueID, o->m_objectID, props.size(), via);
        }
        publishCreate(o, props);
        m_cache[o] = std::move(props);
        m_dirty.erase(o);
    }

    void onRemoved(GameObject* o) {
        if (!o || ignoring() || !m_armed) return;
        // removeObject and the end-of-frame section check can both report the same removal.
        if (!m_removedThisFrame.insert(o).second) return;
        if (m_removedLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: removed uid={} id={}", o->m_uniqueID, o->m_objectID);
        }
        if (auto it = m_objToId.find(o); it != m_objToId.end()) {
            if (SessionManager::get()->inSession()) SessionManager::get()->submitLocal(nls::Op::erase(it->second));
            unmap(o);
        }
        forget(o);
    }

    // Re-read every cached object without logging (after playtest, GD restores object state).
    void resyncSilently() {
        for (auto& [o, props] : m_cache) props = read(o);
        m_dirty.clear();
    }

    void tick(CCArray* selected, GameObject* single) {
        if (!m_armed) {  // first frame: the level has finished loading
            m_armed = true;
            SessionManager::get()->attachEditor(this);
            return;
        }
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
                    log::info("NexLevel diff: changed uid={} id={}: {}", o->m_uniqueID, o->m_objectID, nls::describe(d));
                }
                publishChange(o, now, d);
                it->second = std::move(now);
            }
            flushSummary("changed", changedTotal);
        }
        flushSummary("created", m_createdLogged);
        flushSummary("removed", m_removedLogged);
        m_createdLogged = m_removedLogged = 0;
        m_dirty.clear();
        m_removedThisFrame.clear();

        // Remote changes held back while the local user had the object selected.
        if (!m_deferred.empty() && !m_paused) {
            std::vector<nls::NetId> retry(m_deferred.begin(), m_deferred.end());
            m_deferred.clear();
            applyRemote(retry);
        }
    }

    void detach() { SessionManager::get()->detachEditor(this); }

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
            m_cache[o] = std::move(props);
            ++n;
        });
        log::info("NexLevel: published {} existing objects to the room", n);
    }

    void rebuildFromSession() override {
        auto* view = SessionManager::get()->view();
        if (!view) return;
        RemoteScope scope(*this);
        std::vector<GameObject*> existing;
        forEachLevelObject([&](GameObject* o) { existing.push_back(o); });
        for (auto* o : existing) m_layer->removeObject(o, true);
        // Old undo/redo entries point at objects that are no longer part of the shared level.
        if (m_layer->m_undoObjects) m_layer->m_undoObjects->removeAllObjects();
        if (m_layer->m_redoObjects) m_layer->m_redoObjects->removeAllObjects();
        m_cache.clear();
        m_objToId.clear();
        m_idToObj.clear();
        m_dirty.clear();
        m_pendingRemoval.clear();
        m_deferred.clear();
        for (const auto& [id, state] : view->objects()) spawn(id, state.props);
        log::info("NexLevel: editor rebuilt from room ({} objects)", view->objects().size());
    }

    void applyRemote(const std::vector<nls::NetId>& ids) override {
        auto* view = SessionManager::get()->view();
        if (!view || m_paused) {
            for (auto id : ids) m_deferred.insert(id);
            return;
        }
        RemoteScope scope(*this);
        log::info("NexLevel: applying {} remote change(s)", ids.size());
        for (auto id : ids) {
            const auto* state = view->find(id);
            auto it = m_idToObj.find(id);
            GameObject* obj = it == m_idToObj.end() ? nullptr : it->second;
            if (!state) {
                if (obj) {
                    unmap(obj);
                    forget(obj);
                    m_layer->removeObject(obj, true);
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
            unmap(obj);
            forget(obj);
            m_layer->removeObject(obj, true);
            spawn(id, state->props);
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

    void spawn(nls::NetId id, const nls::PropMap& props) {
        auto str = nls::toSaveString(props) + ";";
        auto* objs = m_layer->createObjectsFromString(str, true, true);
        if (!objs || objs->count() == 0) {
            log::warn("NexLevel: could not create object for net id {}", id.value);
            return;
        }
        auto* o = static_cast<GameObject*>(objs->objectAtIndex(0));
        map(o, id);
        m_cache[o] = read(o);
    }

    void publishCreate(GameObject* o, const nls::PropMap& props) {
        auto* sm = SessionManager::get();
        if (!sm->inSession() || m_objToId.contains(o)) return;
        auto id = sm->newId();
        if (!id) return;
        map(o, *id);
        sm->submitLocal(nls::Op::create(*id, props));
    }

    void publishChange(GameObject* o, const nls::PropMap& now, const nls::PropDiff& d) {
        auto* sm = SessionManager::get();
        if (!sm->inSession()) return;
        auto it = m_objToId.find(o);
        if (it == m_objToId.end()) {
            publishCreate(o, now);  // touched for the first time since joining
            return;
        }
        sm->submitLocal(nls::Op::setProps(it->second, d));
    }

    void map(GameObject* o, nls::NetId id) {
        m_objToId[o] = id;
        m_idToObj[id] = o;
    }
    void unmap(GameObject* o) {
        if (auto it = m_objToId.find(o); it != m_objToId.end()) {
            m_idToObj.erase(it->second);
            m_objToId.erase(it);
        }
    }
    void forget(GameObject* o) {
        m_cache.erase(o);
        m_dirty.erase(o);
        m_pendingRemoval.erase(o);
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
            log::info("NexLevel diff: ... {} more {} this frame", total - kMaxLogLinesPerKind, kind);
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
        this->schedule(schedule_selector(NLEditorUI::nlTick));
        log::info("NexLevel editor tracker active");
        return true;
    }

    void nlTick(float) {
        if (g_tracker) g_tracker->tick(this->getSelectedObjects(), m_selectedObject);
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
            log::info("NexLevel diff: {} applied", undo ? "undo" : "redo");
            g_tracker->markAllDirty();
        }
    }

    void onPlaytest() {
        if (g_tracker) g_tracker->setPaused(true);
        LevelEditorLayer::onPlaytest();
    }

    void onStopPlaytest() {
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
