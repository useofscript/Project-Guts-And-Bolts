#pragma once
#include <memory>
#include <string>
#include <vector>
#include "EditorState.h"
#include "Premades.h"
#include "../scene/SceneNode.h"

struct GLFWwindow;
class Scene;
class GameSession;
class ViewportPanel;
class OutlinerPanel;
class PropertiesPanel;
class EnvironmentPanel;
class ToolboxPanel;
class PlayerPanel;
class OutputPanel;
class ScriptEditorPanel;
class TeamCreate;

class Editor {
public:
    Editor(GLFWwindow* window, Scene* scene);
    ~Editor();
    void render(float dt);
    void openFile(const std::string& path);
    void togglePlay();
    // Test / command-line helpers.
    void startTeamCreate(bool host, const std::string& address);
    void testAddPart(const std::string& name);
    void testPremades(const std::string& list);

private:
    void buildDockspace();
    void renderMenuBar();
    void renderToolbar();
    void renderStatusBar();
    void renderDialogs();
    void renderTeamPanel();
    void handleShortcuts();

    // Creating / removing things
    SceneNode* addPrimitive(const char* label, PrimitiveType type);
    void       spawnPrimitive(PrimitiveType type);
    void       spawnPremade(Premade kind);
    void       addScript(SceneNode* parent);
    void       addModel();
    void       addLight(LightType type);
    void       addSound();
    void       connectParts(int type, SceneNode* a, glm::vec3 pa, SceneNode* b, glm::vec3 pb);
    void       openScript(SceneNode* script);
    void       duplicateSelected();
    void       deleteSelected();
    void       copySelected();
    void       paste();
    bool       canEdit(const SceneNode* n) const;
    glm::vec3  spawnPoint() const;

    // Files
    void newScene();
    void saveFile(const std::string& path);
    void save();
    void updateTitle();

    // Undo / redo (whole-scene snapshots)
    void trackChanges();
    void undo();
    void redo();
    void restore(const std::string& snapshot);
    void resetHistory();


    GLFWwindow*                        m_window;
    Scene*                             m_scene;
    EditorState                        m_state;
    std::unique_ptr<GameSession>       m_session;
    std::unique_ptr<ViewportPanel>     m_viewport;
    std::unique_ptr<OutlinerPanel>     m_outliner;
    std::unique_ptr<PropertiesPanel>   m_properties;
    std::unique_ptr<EnvironmentPanel>  m_environment;
    std::unique_ptr<ToolboxPanel>      m_toolbox;
    std::unique_ptr<PlayerPanel>       m_player;
    std::unique_ptr<OutputPanel>       m_output;
    std::unique_ptr<ScriptEditorPanel> m_scriptEditor;
    std::unique_ptr<TeamCreate>        m_team;
    bool        m_openTeam = false;
    std::string m_teamAddress;
    std::string m_teamChat;
    std::string m_teamError;

    bool m_firstLayout = true;
    bool m_showSettings = false;
    bool m_playing     = false;
    int  m_objCounter  = 0;
    int  m_frame       = 0;

    std::string              m_playSnapshot;   // world before Play
    std::vector<std::string> m_undo, m_redo;
    std::string              m_committed;      // last known scene state
    std::string              m_clipboard;

    std::string m_path;                         // current file ("" = never saved)
    bool        m_dirty = false;
    std::string m_shownTitle;

    // Dialogs
    enum class Pending { None, New, Open };
    Pending     m_pending = Pending::None;      // waiting on "discard changes?"
    bool        m_openSaveAs = false, m_openOpen = false, m_openDiscard = false,
                m_openInfo = false;
    std::string m_nameInput;
    std::string m_openPathInput;
};
