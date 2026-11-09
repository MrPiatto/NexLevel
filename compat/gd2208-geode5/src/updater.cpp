// Update notice + one-click install from the official GitHub release, and the welcome screen after
// an update. Same look as the NexLevel menu.
#include "session_manager.hpp"
#include "ui_kit.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/utils/web.hpp>

#include <string>

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr const char* kLatestReleaseApi = "https://api.github.com/repos/MrPiatto/NexLevel/releases/latest";
constexpr const char* kPackageName = "forg3studios.nexlevel.geode";
constexpr float kW = 380.f, kH = 250.f;

// A panel in the NexLevel style: title, subtitle, markdown body, up to two buttons.
class NexPanel : public geode::Popup {
public:
    static NexPanel* create(std::string title, std::string subtitle, std::string body) {
        auto* ret = new NexPanel();
        ret->m_title = std::move(title);
        ret->m_subtitle = std::move(subtitle);
        ret->m_body = std::move(body);
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    CCMenuItemSpriteExtra* addAction(const std::string& caption, kit::Style style, float x, SEL_MenuHandler sel) {
        auto* b = kit::button(caption, {120.f, 26.f}, style, this, sel, 9.5f);
        m_buttonMenu->addChildAtPosition(b, Anchor::Bottom, {x, 26.f});
        return b;
    }

    CCLabelTTF* m_status = nullptr;

protected:
    std::string m_title, m_subtitle, m_body;

    bool init() {
        if (!Popup::init(kW, kH, "square02b_001.png", {0, 0, 80, 80})) return false;
        m_bgSprite->setVisible(false);
        if (m_closeBtn) m_closeBtn->setVisible(false);
        auto* panel = kit::card({kW, kH}, 14.f, kit::rgba(16, 18, 38, .97f), kit::rgba(kit::kViolet, .5f), .7f);
        m_mainLayer->addChildAtPosition(panel, Anchor::Center);
        panel->setZOrder(-10);
        auto* title = kit::text(m_title, 15.f, kit::kText, 1);
        m_mainLayer->addChildAtPosition(title, Anchor::Top, {0.f, -22.f});
        auto* sub = kit::text(m_subtitle, 8.f, kit::kMuted);
        m_mainLayer->addChildAtPosition(sub, Anchor::Top, {0.f, -40.f});
        auto* box = kit::card({kW - 30.f, kH - 108.f}, 10.f, kit::rgba(30, 34, 66, .85f), kit::rgba(kit::kViolet, .3f), .8f);
        m_mainLayer->addChildAtPosition(box, Anchor::Center, {0.f, 4.f});
        auto* text = MDTextArea::create(m_body.empty() ? "No changelog." : m_body, {kW - 44.f, kH - 118.f});
        m_mainLayer->addChildAtPosition(text, Anchor::Center, {0.f, 4.f});
        m_status = kit::text("", 9.f, kit::kWarn, 1);
        m_mainLayer->addChildAtPosition(m_status, Anchor::Bottom, {0.f, 26.f});
        m_status->setVisible(false);
        return true;
    }
};

class UpdatePanel : public NexPanel {
public:
    static UpdatePanel* create(const std::string& version, const std::string& changelog, const std::string& url) {
        auto* ret = new UpdatePanel();
        ret->m_title = "Update available";
        ret->m_subtitle = "NexLevel " + version + " is out (you have " + Mod::get()->getVersion().toVString() +
                          "). Rooms need the same version.";
        ret->m_body = changelog;
        ret->m_url = url;
        ret->m_version = version;
        if (ret->init()) {
            ret->autorelease();
            ret->m_later = ret->addAction("Later", kit::Style::Secondary, -70.f, menu_selector(UpdatePanel::onLater));
            ret->m_install = ret->addAction("Install", kit::Style::Primary, 70.f, menu_selector(UpdatePanel::onInstall));
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    std::string m_url, m_version;
    CCMenuItemSpriteExtra* m_later = nullptr;
    CCMenuItemSpriteExtra* m_install = nullptr;
    int m_countdown = 3;
    arc::TaskHandle<void> m_download;

    void setStatus(const std::string& s, ccColor3B c = kit::kWarn) {
        m_later->setVisible(false);
        m_install->setVisible(false);
        m_status->setVisible(true);
        m_status->setColor(c);
        kit::setTextIfChanged(m_status, s);
    }

    void onLater(CCObject*) { this->onClose(nullptr); }

    void onInstall(CCObject*) {
        setStatus("Downloading NexLevel " + m_version + "...", kit::kTeal);
        Ref<UpdatePanel> self = this;
        m_download = geode::async::spawn(
            web::WebRequest().followRedirects(true).timeout(std::chrono::seconds(60)).header("User-Agent", "NexLevel-mod").get(m_url),
            [self](web::WebResponse res) {
                if (!res.ok() || res.data().size() < 1024) {
                    log::warn("NexLevel: update download failed (HTTP {})", res.code());
                    self->setStatus("Download failed - try again later", kit::kDanger);
                    self->m_later->setVisible(true);
                    return;
                }
                auto written = utils::file::writeBinarySafe(Mod::get()->getPackagePath(), res.data());
                if (written.isErr()) {
                    log::warn("NexLevel: could not write the update: {}", written.unwrapErr());
                    self->setStatus("Could not install the update", kit::kDanger);
                    self->m_later->setVisible(true);
                    return;
                }
                log::info("NexLevel: update {} installed, restarting", self->m_version);
                self->tick(0.f);
                self->schedule(schedule_selector(UpdatePanel::tick), 1.f);
            });
    }

    void tick(float) {
        if (m_countdown <= 0) {
            this->unschedule(schedule_selector(UpdatePanel::tick));
            auto* sm = SessionManager::get();
            if (sm->inSession()) sm->leave();  // saves the host's level first
            utils::game::restart(true);
            return;
        }
        std::string dots(static_cast<std::size_t>(4 - m_countdown), '.');
        setStatus("Installed! Restarting in " + std::to_string(m_countdown) + dots, kit::kGood);
        --m_countdown;
    }
};

class WelcomePanel : public NexPanel {
public:
    static WelcomePanel* create(const std::string& changelog, bool updated) {
        auto* ret = new WelcomePanel();
        ret->m_title = "Welcome to NexLevel " + Mod::get()->getVersion().toVString();
        ret->m_subtitle = updated ? "Updated! Here's what's new." : "Create Together. Build Beyond.  -  Forg3 Studios";
        ret->m_body = changelog;
        if (ret->init()) {
            ret->autorelease();
            ret->addAction("Let's build!", kit::Style::Primary, 0.f, menu_selector(WelcomePanel::onOk));
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    void onOk(CCObject*) { this->onClose(nullptr); }
};

// The part of changelog.md for the running version (first "# vX" section).
std::string currentChangelogSection() {
    const auto& full = Mod::get()->getMetadata().getChangelog();
    if (!full) return {};
    const std::string& s = *full;
    auto start = s.find("# ");
    if (start == std::string::npos) return s;
    auto next = s.find("\n# ", start + 2);
    return s.substr(start, next == std::string::npos ? std::string::npos : next - start);
}

arc::TaskHandle<void> g_check;

void checkForUpdate() {
    if (!Mod::get()->getSettingValue<bool>("check-updates")) return;
    g_check = geode::async::spawn(
        web::WebRequest().timeout(std::chrono::seconds(10)).header("User-Agent", "NexLevel-mod").get(kLatestReleaseApi),
        [](web::WebResponse res) {
            if (!res.ok()) {
                log::info("NexLevel: update check skipped (HTTP {})", res.code());
                return;
            }
            auto json = res.json();
            if (json.isErr()) return;
            const matjson::Value j = std::move(json).unwrap();
            const std::string tag = j["tag_name"].asString().unwrapOr("");
            if (j["prerelease"].asBool().unwrapOr(false) || j["draft"].asBool().unwrapOr(false)) return;
            auto latest = VersionInfo::parse(tag);
            if (latest.isErr() || !(latest.unwrap() > Mod::get()->getVersion())) return;
            std::string url;
            if (auto assets = j["assets"].asArray(); assets.isOk()) {
                for (const auto& a : assets.unwrap()) {
                    if (a["name"].asString().unwrapOr("") == kPackageName) url = a["browser_download_url"].asString().unwrapOr("");
                }
            }
            if (url.empty()) return;
            const std::string body = j["body"].asString().unwrapOr("");
            log::info("NexLevel: update available: {}", tag);
            if (auto* panel = UpdatePanel::create(tag, body, url)) panel->show();
        });
}

} // namespace

} // namespace nexlevel::mod

class $modify(NLUpdateMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool done = false;
        if (done) return true;
        done = true;
        geode::queueInMainThread([] {
            const std::string current = Mod::get()->getVersion().toVString();
            const std::string last = Mod::get()->getSavedValue<std::string>("last-run-version", "");
            Mod::get()->setSavedValue<std::string>("last-run-version", current);
            if (last != current) {
                // First start of this version (fresh install or just updated): welcome + what's new.
                if (auto* p = nexlevel::mod::WelcomePanel::create(nexlevel::mod::currentChangelogSection(), !last.empty())) p->show();
                return;
            }
            nexlevel::mod::checkForUpdate();
        });
        return true;
    }
};
