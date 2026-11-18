// Geode: Popup. Unban closes the list and opens a fresh one on the next frame (a button is never
// removed while its own click is still running).
#include "ban_list.hpp"

#include "session_manager.hpp"
#include "toast.hpp"
#include "ui_kit.hpp"

using namespace geode::prelude;

namespace nexlevel::mod {

namespace {

constexpr float kW = 300.f, kH = 230.f;
constexpr std::size_t kRows = 7;

class BanListPanel : public geode::Popup {
public:
    static BanListPanel* create() {
        auto* ret = new BanListPanel();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

private:
    std::vector<SessionManager::BanEntry> m_bans;

    bool init() override {
        if (!Popup::init(kW, kH, "square02b_001.png", {0, 0, 80, 80})) return false;
        m_bgSprite->setVisible(false);
        if (m_closeBtn) m_closeBtn->setVisible(false);
        auto* panel = kit::card({kW, kH}, 14.f, kit::rgba(16, 18, 38, .97f), kit::rgba(kit::kViolet, 1.f), 1.f);
        m_mainLayer->addChildAtPosition(panel, Anchor::Center);
        panel->setZOrder(-10);
        m_mainLayer->addChildAtPosition(kit::text("Banned players", 13.f, kit::kText, 1), Anchor::Top, {0.f, -20.f});
        m_mainLayer->addChildAtPosition(kit::text("Kept for all your rooms", 7.f, kit::kMuted), Anchor::Top, {0.f, -36.f});

        m_bans = SessionManager::get()->savedBans();
        if (m_bans.empty()) {
            m_mainLayer->addChildAtPosition(kit::text("Nobody is banned", 9.f, kit::kMuted), Anchor::Center, {0.f, 8.f});
        }
        for (std::size_t i = 0; i < m_bans.size() && i < kRows; ++i) {
            const float y = kH / 2.f - 58.f - static_cast<float>(i) * 22.f;
            auto* name = kit::text(m_bans[i].name.empty() ? std::string("(no name)") : m_bans[i].name, 8.5f, kit::kText, 1);
            name->setAnchorPoint({0.f, .5f});
            if (name->getContentSize().width > 170.f) name->setScale(170.f / name->getContentSize().width);
            m_mainLayer->addChildAtPosition(name, Anchor::Center, {-kW / 2.f + 18.f, y});
            auto* b = kit::button("Unban", {54.f, 17.f}, kit::Style::Secondary, this, menu_selector(BanListPanel::onUnban), 7.f);
            b->setTag(static_cast<int>(i));
            m_buttonMenu->addChildAtPosition(b, Anchor::Center, {kW / 2.f - 42.f, y});
        }
        if (m_bans.size() > kRows) {
            m_mainLayer->addChildAtPosition(kit::text("+" + std::to_string(m_bans.size() - kRows) + " more", 7.f, kit::kMuted), Anchor::Bottom,
                                            {0.f, 44.f});
        }
        auto* close = kit::button("Close", {90.f, 22.f}, kit::Style::Primary, this, menu_selector(BanListPanel::onDone), 8.5f);
        m_buttonMenu->addChildAtPosition(close, Anchor::Bottom, {0.f, 20.f});
        return true;
    }

    void onUnban(CCObject* sender) {
        const auto i = static_cast<std::size_t>(static_cast<CCNode*>(sender)->getTag());
        if (i >= m_bans.size()) return;
        const auto entry = m_bans[i];
        SessionManager::get()->unban(entry);
        toast("Unbanned " + entry.name, "They can join your rooms again", ToastKind::Success);
        this->onClose(nullptr);
        geode::queueInMainThread([] { showBanList(); });
    }
    void onDone(CCObject*) { this->onClose(nullptr); }
};

} // namespace

void showBanList() {
    if (auto* panel = BanListPanel::create()) panel->show();
}

} // namespace nexlevel::mod
