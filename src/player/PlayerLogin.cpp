// Sign up / Log in (on a Guts&Bolts server).
//
// Your account is the key on this device. Signing up gives it a username and
// a user number, and puts a copy of the key on the server, locked with your
// password, so you can log in on another device. The password never leaves
// the device: only a login token made from it does (see Account::passwordKeys).
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../game/Profile.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Scene.h"
#include "LaunchLink.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdio>

using namespace Site;
using json = nlohmann::json;

bool PlayerApp::needsLogin() const {
    if (m_playOffline || m_page == Page::Game) return false;
    if (m_page == Page::Login) return true;
    return Online::online() && Online::me().value("userId", 0LL) == 0 && !Online::isGuest();
}

void PlayerApp::signUp(const std::string& username, const std::string& password) {
    if (std::string problem = Online::usernameProblem(username, Account::iAmStaff()); !problem.empty()) { m_loginMsg = problem; return; }
    if (password.size() < Online::kMinPassword) { m_loginMsg = "Your password needs at least 8 characters."; return; }
    // Lock a copy of this device's key with the password (the server can't open it).
    std::string salt = Account::randomHex(16), lock, auth;
    if (!Account::passwordKeys(password, salt, lock, auth)) { m_loginMsg = "Something went wrong. Try again."; return; }
    std::string blob = Account::backupKey(lock);
    m_busy = true;
    m_loginMsg = "Signing up...";
    Online::request("account.signup", {{"username", username}, {"salt", salt}, {"auth", auth}, {"key", blob}},
                    [this, username](const json& r) {
        m_busy = false;
        if (!r.value("ok", false)) {
            m_loginMsg = r.value("error", std::string("Couldn't sign up."));
            std::printf("SIGNUP failed: %s\n", m_loginMsg.c_str());
            std::fflush(stdout);
            return;
        }
        m_loginMsg.clear();
        m_loginPass.clear();
        m_loginPass2.clear();
        long long id = r.contains("me") ? r["me"].value("userId", 0LL) : 0;
        std::printf("SIGNUP ok %s #%lld\n", username.c_str(), id);
        std::fflush(stdout);
        m_notice = "Welcome to Guts&Bolts, " + username + "! You're user #" + std::to_string(id) + ".";
        m_page = Page::Home;
    });
}

void PlayerApp::logIn(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) { m_loginMsg = "Type your username and password."; return; }
    m_busy = true;
    m_loginMsg = "Logging in...";
    Online::request("account.salt", {{"username", username}}, [this, username, password](const json& r) {
        if (!r.value("ok", false)) {
            m_busy = false;
            m_loginMsg = r.value("error", std::string("Couldn't log in."));
            std::printf("LOGIN failed: %s\n", m_loginMsg.c_str());
            std::fflush(stdout);
            return;
        }
        std::string lock, auth;
        if (!Account::passwordKeys(password, r.value("salt", std::string()), lock, auth)) {
            m_busy = false;
            m_loginMsg = "Something went wrong. Try again.";
            return;
        }
        Online::request("account.login", {{"username", username}, {"auth", auth}}, [this, lock](const json& r) {
            m_busy = false;
            if (!r.value("ok", false)) {
                m_loginMsg = r.value("error", std::string("Couldn't log in."));
                std::printf("LOGIN failed: %s\n", m_loginMsg.c_str());
                std::fflush(stdout);
                return;
            }
            // Unlock the key copy: from now on this device IS that account.
            if (!Account::restoreKey(lock, r.value("key", std::string())) || Account::id() != r.value("account", std::string())) {
                m_loginMsg = "Couldn't unlock your account on this device.";
                return;
            }
            Profile& p = Profile::get();
            p.name = r.value("username", std::string("Player"));
            p.grants.clear();   // badges belong to the account: the server sends them back
            p.save();
            m_loginMsg.clear();
            m_loginPass.clear();
            std::printf("LOGIN ok %s account %s\n", r.value("username", std::string()).c_str(), Account::shortId().c_str());
            std::fflush(stdout);
            m_friends = json::object();
            m_loaded.clear();
            Online::setServerAddress(Online::serverAddress());   // say hello again as the real account
            m_page = Page::Home;
        });
    });
}

void PlayerApp::logOut() {
    // Only offered once the account has a password, so it can always come back.
    Account::newKey();
    Profile& p = Profile::get();
    p.name = "Player";
    p.grants.clear();
    p.save();
    m_friends = json::object();
    m_loaded.clear();
    m_loginTab = 1;
    Online::setServerAddress(Online::serverAddress());
    m_page = Page::Home;
}

void PlayerApp::drawLogin() {
    const bool official = Account::iAmStaff();
    const float w = std::min(420.0f, ImGui::GetContentRegionAvail().x);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(m_loginTab == 0 ? "Sign Up" : "Log In");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled(m_loginTab == 0 ? "Make your Guts&Bolts account. It's free!" : "Welcome back!");
    ImGui::Spacing();

    const char* tabs[] = {"Sign Up", "Log In"};
    for (int i = 0; i < 2; ++i) {
        if (i) ImGui::SameLine();
        bool on = m_loginTab == i;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2((w - 8) / 2, 30)) : ImGui::Button(tabs[i], ImVec2((w - 8) / 2, 30))) {
            m_loginTab = i;
            m_loginMsg.clear();
        }
    }
    ImGui::Spacing();

    if (!Online::online()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextDisabled("%s", Online::configured() ? Online::statusText().c_str()
                                                       : "Accounts live on a Guts&Bolts server. Pick one first.");
        ImGui::PopTextWrapPos();
        if (Classic::button("Pick a server", Classic::kBlue, ImVec2(w, 32))) { m_serverInput = Online::serverAddress(); m_showServer = true; }
    } else {
        ImGui::TextUnformatted("Username");
        ImGui::SetNextItemWidth(w);
        bool changed = ImGui::InputTextWithHint("##user", "3-20 letters or numbers", &m_loginUser,
                                                ImGuiInputTextFlags_CharsNoBlank);
        if (m_loginTab == 0) {
            // Check the name as you type (after a short pause).
            if (changed) { m_nameCheck = json::object(); m_nameCheckAt = ImGui::GetTime() + 0.5; }
            if (!m_loginUser.empty() && m_nameCheckAt > 0 && ImGui::GetTime() > m_nameCheckAt) {
                m_nameCheckAt = 0;
                std::string asked = m_loginUser;
                Online::request("account.check", {{"username", asked}}, [this, asked](const json& r) {
                    if (asked == m_loginUser && r.value("ok", false)) { m_nameCheck = r; m_nameCheck["for"] = asked; }
                });
            }
            if (!m_loginUser.empty() && m_nameCheck.value("for", std::string()) == m_loginUser) {
                if (m_nameCheck.value("available", false))
                    ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "%s is available!", m_loginUser.c_str());
                else
                    ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1), "%s", m_nameCheck.value("problem", std::string()).c_str());
            }
        }
        ImGui::TextUnformatted("Password");
        ImGui::SetNextItemWidth(w);
        bool enter = ImGui::InputTextWithHint("##pass", m_loginTab == 0 ? "at least 8 characters" : "", &m_loginPass,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
        if (m_loginTab == 0) {
            ImGui::TextUnformatted("Password again");
            ImGui::SetNextItemWidth(w);
            enter = ImGui::InputTextWithHint("##pass2", "", &m_loginPass2,
                                             ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue) || enter;
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(m_busy);
        if (Classic::button(m_busy ? "Working...##go" : m_loginTab == 0 ? "Sign Up##go" : "Log In##go", Classic::kPlay, ImVec2(w, 40)) || (enter && !m_busy)) {
            if (m_loginTab == 1) logIn(m_loginUser, m_loginPass);
            else if (m_loginPass != m_loginPass2) m_loginMsg = "The two passwords don't match.";
            else signUp(m_loginUser, m_loginPass);
        }
        ImGui::EndDisabled();
        if (!m_loginMsg.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
            ImGui::TextColored(m_busy ? ImVec4(0.3f, 0.35f, 0.45f, 1) : ImVec4(0.8f, 0.2f, 0.1f, 1), "%s", m_loginMsg.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        if (m_loginTab == 0)
            ImGui::TextDisabled("Usernames can't be changed, and once taken they're gone for good. "
                                "Your password never leaves this device. If you forget it, nobody can get it back, "
                                "so write it down somewhere safe.%s",
                                official ? "\n\nThis is the staff computer: sign up as Guts to add a password to user #1." : "");
        else
            ImGui::TextDisabled("Logging in makes this device your account. Whatever it was playing as before "
                                "(if it never signed up) is left behind.");
        ImGui::PopTextWrapPos();
    }
    ImGui::Spacing();
    // No account? Play as a guest (games only: no chat, friends or Bolts).
    if (Online::me().value("userId", 0LL) == 0) {
        if (Classic::button("Play as Guest", Classic::kBlue, ImVec2(ImGui::GetContentRegionAvail().x, 34)))
            m_charPickOpen = true;   // pick a boy or a girl first (drawCharacterPicker)
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextDisabled("Guests can play every game, alone or with others, without an account. "
                            "To chat, make friends and get Bolts, sign up (it's free).");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (ImGui::SmallButton("Play offline instead")) { m_playOffline = true; if (m_page == Page::Login) m_page = Page::Home; }
    if (m_page == Page::Login && Online::me().value("userId", 0LL) > 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Back")) m_page = Page::Home;
    }
    ImGui::EndGroup();
}

// --- "Choose Your Character:" -------------------------------------------------
// Guests pick who to play as, like the old sites did: two characters in green
// frames, and a link for people who already have an account.
namespace {
// The two guest looks: white head, black clothes, and a black cap or a ponytail.
glm::vec3 guestHatColor(int which) { return which == 0 ? glm::vec3(0.1f, 0.1f, 0.11f) : glm::vec3(-1.0f); }
void dressGuest(Player& p, int which) {
    BodyColors bc = Player::colorPresets()[0].second;
    const glm::vec3 skin{0.94f, 0.94f, 0.92f}, black{0.1f, 0.1f, 0.12f};
    bc.head = skin;
    bc.torso = bc.leftArm = bc.rightArm = black;
    bc.leftLeg = bc.rightLeg = black;
    p.setBodyColors(bc);
    p.setHat(which == 0 ? HatStyle::Cap : HatStyle::Ponytail, guestHatColor(which));   // (same as applyGuestLook)
}
} // namespace

// Play as a guest character: this device's look while it's a guest.
void PlayerApp::applyGuestLook(int which) {
    Profile& me = Profile::get();
    BodyColors bc = Player::colorPresets()[0].second;
    const glm::vec3 skin{0.94f, 0.94f, 0.92f}, black{0.1f, 0.1f, 0.12f};
    bc.head = skin;
    bc.torso = bc.leftArm = bc.rightArm = black;
    bc.leftLeg = bc.rightLeg = black;
    me.colors = bc;
    me.hat = which == 0 ? HatStyle::Cap : HatStyle::Ponytail;
    me.hatColor = guestHatColor(which);
    me.wearing.clear();
    me.save();
    if (m_avatarScene) if (Player* p = m_avatarScene->player()) me.applyTo(*p);
}

// The website's Play button opened us with gutsandbolts://play/<game>[?guest=boy|girl].
void PlayerApp::takeLink(const std::string& url) {
    LaunchLink::Link link;
    if (!LaunchLink::parse(url, link)) return;
    m_linkGame = link.game;
    m_linkGuest = link.guest;
}

void PlayerApp::followLink() {
    if (m_linkGame.empty()) return;
    if (!Online::online()) {   // not connected yet: wait (a server that's switched off never answers)
        if (!Online::configured()) { m_status = "Connect to a Guts&Bolts server to play games from the website."; m_linkGame.clear(); }
        return;
    }
    if (m_busy) return;
    const std::string id = m_linkGame;
    m_linkGame.clear();
    if (m_page == Page::Game) leaveGame();
    if (Online::me().value("userId", 0LL) == 0) {   // not signed up on this device: play as a guest
        if (!m_linkGuest.empty()) applyGuestLook(m_linkGuest == "girl" ? 1 : 0);
        Online::setGuest(true);
    }
    std::string title;
    for (const auto& g : m_onlineGames) if (g.value("id", std::string()) == id) title = g.value("name", std::string());
    m_page = Page::Home;
    playGame(id, title, onlineStarter(id));
}

void PlayerApp::drawCharacterPicker() {
    if (!m_charPickOpen) return;
    if (!ImGui::IsPopupOpen("##charpick")) ImGui::OpenPopup("##charpick");

    // A little stage for each character (made once).
    for (int i = 0; i < 2; ++i) {
        if (m_charScene[i]) continue;
        m_charScene[i] = std::make_unique<Scene>();
        Scene& sc = *m_charScene[i];
        std::vector<SceneNode*> remove;
        for (auto& c : sc.root()->children)
            if (!sc.isProtected(c.get())) remove.push_back(c.get());   // no floor: just the character on white
        for (auto* r : remove) sc.removeNode(r);
        if (Player* p = sc.player()) { p->setSpawn({0, 0, 0}); p->build(); dressGuest(*p, i); }
        Environment& e = sc.environment();
        e.fogEnabled = false;
        e.sunAzimuth = 60.0f;
        e.sunElevation = 35.0f;
        e.skyZenith = e.skyHorizon = e.skyGround = glm::vec3(1.0f);   // plain white behind them
        e.clouds = false;
        e.stars = false;
    }

    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float w = std::min(560.0f, screen.x - 24.0f);
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 1));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0, 0, 0, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.1f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::BeginPopupModal("##charpick", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        const float base = ImGui::GetFontSize();
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const float inner = ImGui::GetContentRegionAvail().x;
        int picked = -1;

        // The round close button in the top-right corner.
        const ImVec2 xc(wp.x + ws.x - 20, wp.y + 20);
        ImGui::SetCursorScreenPos(ImVec2(xc.x - 14, xc.y - 14));
        const bool closeHit = ImGui::InvisibleButton("##close", ImVec2(28, 28));
        const bool closeHover = ImGui::IsItemHovered();
        dl->AddCircleFilled(xc, 13, closeHover ? IM_COL32(90, 90, 96, 255) : IM_COL32(60, 60, 66, 255));
        dl->AddCircle(xc, 13, IM_COL32(255, 255, 255, 255), 0, 2.0f);
        dl->AddLine(ImVec2(xc.x - 5, xc.y - 5), ImVec2(xc.x + 5, xc.y + 5), IM_COL32(255, 255, 255, 255), 2.5f);
        dl->AddLine(ImVec2(xc.x + 5, xc.y - 5), ImVec2(xc.x - 5, xc.y + 5), IM_COL32(255, 255, 255, 255), 2.5f);
        ImGui::SetCursorScreenPos(ImVec2(wp.x + 20, wp.y + 18));

        // Title, big and bold (drawn twice, a pixel apart, for the bold look).
        const char* title = "Choose Your Character:";
        const float titleSize = base * (inner < 360 ? 1.45f : 1.8f);
        ImVec2 ts = font->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, title);
        ImVec2 tp(wp.x + (ws.x - ts.x) * 0.5f, ImGui::GetCursorScreenPos().y + 10);
        dl->AddText(font, titleSize, tp, IM_COL32(15, 15, 18, 255), title);
        dl->AddText(font, titleSize, ImVec2(tp.x + 1, tp.y), IM_COL32(15, 15, 18, 255), title);
        ImGui::Dummy(ImVec2(inner, ts.y + 30));

        // The two characters.
        const float gap = std::min(80.0f, inner * 0.12f);
        const float box = std::min(200.0f, (inner - gap) * 0.5f);
        const float startX = wp.x + (ws.x - (box * 2 + gap)) * 0.5f;
        const float y0 = ImGui::GetCursorScreenPos().y;
        const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
        const char* labels[2] = {"Play As Boy", "Play As Girl"};
        for (int i = 0; i < 2; ++i) {
            ImVec2 p0(startX + i * (box + gap), y0), p1(p0.x + box, p0.y + box);
            ImGui::SetCursorScreenPos(p0);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##pick", ImVec2(box, box + base * 1.8f))) picked = i;
            const bool hover = ImGui::IsItemHovered();
            ImGui::PopID();
            int px = (int)((box - 8) * fb);
            m_charView[i].resize(px, px);
            m_charCam.resize(px, px);
            m_charCam.pivot = {0, 1.45f, 0};
            m_charCam.yaw = 58.0f + (hover ? std::sin((float)ImGui::GetTime() * 2.0f) * 25.0f : 0.0f);
            m_charCam.pitch = 6.0f;
            m_charCam.distance = 4.9f;
            m_charCam.fov = 42.0f;
            m_renderer->render(*m_charScene[i], m_charCam, m_charView[i], false);
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, 255));
            dl->AddImage((ImTextureID)(intptr_t)m_charView[i].colorTexture(), ImVec2(p0.x + 4, p0.y + 4),
                         ImVec2(p1.x - 4, p1.y - 4), ImVec2(0, 1), ImVec2(1, 0));
            const ImU32 green = hover ? IM_COL32(40, 170, 40, 255) : IM_COL32(20, 130, 20, 255);
            dl->AddRect(p0, p1, green, 0.0f, 0, 4.0f);
            const float ls = base * 1.25f;
            ImVec2 lt = font->CalcTextSizeA(ls, FLT_MAX, 0.0f, labels[i]);
            ImVec2 lp(p0.x + (box - lt.x) * 0.5f, p1.y + 4);
            dl->AddText(font, ls, lp, IM_COL32(20, 20, 24, 255), labels[i]);
            dl->AddText(font, ls, ImVec2(lp.x + 1, lp.y), IM_COL32(20, 20, 24, 255), labels[i]);
        }
        ImGui::SetCursorScreenPos(ImVec2(wp.x + 20, y0 + box + base * 1.8f + 36));

        // "Have an Account?"
        const char* link = "Have an Account?";
        const float ls = base * 1.2f;
        ImVec2 lt = font->CalcTextSizeA(ls, FLT_MAX, 0.0f, link);
        ImVec2 lp(wp.x + (ws.x - lt.x) * 0.5f, ImGui::GetCursorScreenPos().y);
        ImGui::SetCursorScreenPos(lp);
        const bool haveAccount = ImGui::InvisibleButton("##have", lt);
        const bool linkHover = ImGui::IsItemHovered();
        dl->AddText(font, ls, lp, IM_COL32(20, 140, 40, 255), link);
        dl->AddText(font, ls, ImVec2(lp.x + 1, lp.y), IM_COL32(20, 140, 40, 255), link);
        if (linkHover) dl->AddLine(ImVec2(lp.x, lp.y + lt.y), ImVec2(lp.x + lt.x, lp.y + lt.y), IM_COL32(20, 140, 40, 255), 1.5f);
        ImGui::Dummy(ImVec2(inner, 14));

        const bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (picked >= 0) {
            applyGuestLook(picked);
            Online::setGuest(true);
            m_page = Page::Home;
        } else if (haveAccount) {
            m_page = Page::Login;
            m_loginTab = 1;
        }
        if (picked >= 0 || haveAccount || closeHit || esc) {
            m_charPickOpen = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}
