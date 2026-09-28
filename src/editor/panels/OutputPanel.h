#pragma once
#include <cstddef>

// Shows print() / warn() output and script errors (like Roblox's Output window).
class OutputPanel {
public:
    void render();

private:
    bool   m_autoScroll = true;
    bool   m_showInfo = true, m_showWarn = true, m_showError = true, m_showSystem = true;
    size_t m_lastCount = 0;
};
