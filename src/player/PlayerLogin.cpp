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
        if (Classic::button("Play as Guest", Classic::kBlue, ImVec2(ImGui::GetContentRegionAvail().x, 34))) {
            Online::setGuest(true);
            m_page = Page::Home;
        }
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
