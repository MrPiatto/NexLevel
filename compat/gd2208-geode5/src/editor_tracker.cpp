// GD 2.2081 / Geode v5 - Phase 1 editor spike: detect local edits and log property diffs.
// No networking yet. Signatures copied from geode-sdk/bindings 2.2081/GeometryDash.bro.
// Windows-inline (not hookable): EditorUI::deleteObject, LevelEditorLayer::undoLastAction/redoLastAction,
// so deletion is caught at LevelEditorLayer::removeObject and undo/redo at LevelEditorLayer::handleAction.
#include <Geode/Geode.hpp>
#include <Geode/modify/CustomizeObjectLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>

#include <nexlevel/core/sync/object_props.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace geode::prelude;
namespace nls = nexlevel::sync;

namespace {

constexpr std::size_t kMaxLogLinesPerKind = 20;  // per tick, so a big paste can't flood the log

class EditorTracker {
public:
    explicit EditorTracker(LevelEditorLayer* layer) : m_layer(layer) {}

    void setPaused(bool paused) { m_paused = paused; }
    bool paused() const { return m_paused; }

    // Call BEFORE an edit so the "before" state is known.
    void snapshotIfUnknown(GameObject* o) {
        if (!o || m_paused || m_cache.contains(o)) return;
        m_cache.emplace(o, read(o));
    }
    void markDirty(GameObject* o) {
        if (o && !m_paused) m_dirty.insert(o);
    }
    void markAllDirty() {
        for (auto& [o, _] : m_cache) m_dirty.insert(o);
    }

    // Idempotent: createObject also goes through addToSection, so a known object is ignored.
    void onCreated(GameObject* o, const char* via) {
        if (!o || m_paused || !m_armed || m_cache.contains(o)) return;
        auto props = read(o);
        if (m_createdLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: created uid={} id={} ({} props, via {})", o->m_uniqueID, o->m_objectID, props.size(), via);
        }
        m_cache[o] = std::move(props);
        m_dirty.erase(o);
    }

    void onRemoved(GameObject* o) {
        if (!o || m_paused || !m_armed) return;
        if (m_removedLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: removed uid={} id={}", o->m_uniqueID, o->m_objectID);
        }
        m_cache.erase(o);
        m_dirty.erase(o);
        m_pendingRemoval.erase(o);
    }

    // Deletion through EditorUI::deleteObject is inline on Windows and skips LevelEditorLayer::removeObject,
    // so removals are confirmed at end of frame: a candidate that is no longer in the layer's object list
    // was removed; one that is still there only changed section (or the delete was cancelled).
    void maybeRemoved(GameObject* o) {
        if (o && !m_paused && m_armed) m_pendingRemoval.insert(o);
    }
    void cancelRemoval(GameObject* o) { m_pendingRemoval.erase(o); }

    // Re-read every cached object without logging (after playtest, GD restores object state).
    void resyncSilently() {
        for (auto& [o, props] : m_cache) props = read(o);
        m_dirty.clear();
    }

    LevelEditorLayer* layer() const { return m_layer; }

    void tick(CCArray* selected, GameObject* single) {
        // Armed on the first frame, so objects added while the level loads are not reported.
        if (!m_armed) {
            m_armed = true;
            return;
        }
        if (!m_paused) {
            // Selection catch-all: popups (colour, groups, properties) edit the selection.
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
            reconcileRemovals();
            std::size_t changedLogged = 0, changedTotal = 0;
            for (auto* o : m_dirty) {
                auto it = m_cache.find(o);
                if (it == m_cache.end()) continue;
                auto now = read(o);
                auto d = nls::diffProps(it->second, now);
                if (!d.empty()) {
                    ++changedTotal;
                    if (changedLogged++ < kMaxLogLinesPerKind) {
                        log::info("NexLevel diff: changed uid={} id={}: {}", o->m_uniqueID, o->m_objectID, nls::describe(d));
                    }
                    it->second = std::move(now);
                }
            }
            flushSummary("changed", changedTotal);
        }
        flushSummary("created", m_createdLogged);
        flushSummary("removed", m_removedLogged);
        m_createdLogged = m_removedLogged = 0;
        m_dirty.clear();
    }

private:
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
    std::unordered_map<GameObject*, nls::PropMap> m_cache;  // objects touched this editor session
    std::unordered_set<GameObject*> m_dirty;
    std::unordered_set<GameObject*> m_pendingRemoval;
    std::size_t m_createdLogged = 0, m_removedLogged = 0;
    bool m_paused = false;
    bool m_armed = false;
};

// Owned by the active EditorUI's fields; null outside the editor.
EditorTracker* g_tracker = nullptr;

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

} // namespace

class $modify(NLEditorUI, EditorUI) {
    struct Fields {
        std::unique_ptr<EditorTracker> tracker;
        ~Fields() {
            if (tracker && g_tracker == tracker.get()) g_tracker = nullptr;
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

// Catch-all for objects entering the level by paths that skip createObject (e.g. undo of a delete).
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
