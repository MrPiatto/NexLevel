// GD 2.2081 / Geode v5 - Phase 1 editor spike: detect local edits and log property diffs.
// No networking yet. Signatures copied from geode-sdk/bindings 2.2081/GeometryDash.bro.
// Windows-inline (not hookable): EditorUI::deleteObject, LevelEditorLayer::undoLastAction/redoLastAction,
// so deletion is caught at LevelEditorLayer::removeObject and undo/redo at LevelEditorLayer::handleAction.
#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
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

    void onCreated(GameObject* o, const char* via) {
        if (!o || m_paused) return;
        auto props = read(o);
        if (m_createdLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: created uid={} id={} ({} props, via {})", o->m_uniqueID, o->m_objectID, props.size(), via);
        }
        m_cache[o] = std::move(props);
        m_dirty.erase(o);
    }

    void onRemoved(GameObject* o) {
        if (!o || m_paused) return;
        if (m_removedLogged++ < kMaxLogLinesPerKind) {
            log::info("NexLevel diff: removed uid={} id={}", o->m_uniqueID, o->m_objectID);
        }
        m_cache.erase(o);
        m_dirty.erase(o);
    }

    // Re-read every cached object without logging (after playtest, GD restores object state).
    void resyncSilently() {
        for (auto& [o, props] : m_cache) props = read(o);
        m_dirty.clear();
    }

    void tick(CCArray* selected) {
        if (!m_paused) {
            // Selection catch-all: popups (colour, groups, properties) edit the selection.
            if (selected) {
                for (auto* o : CCArrayExt<GameObject*>(selected)) {
                    snapshotIfUnknown(o);
                    m_dirty.insert(o);
                }
            }
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
    std::size_t m_createdLogged = 0, m_removedLogged = 0;
    bool m_paused = false;
};

// Owned by the active EditorUI's fields; null outside the editor.
EditorTracker* g_tracker = nullptr;

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
        if (g_tracker) g_tracker->tick(this->getSelectedObjects());
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
