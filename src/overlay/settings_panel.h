#pragma once
// The settings window's contents (Dear ImGui). Render thread only, between ImGui::NewFrame and ImGui::Render.
// Edits are a draft (settings_draft.h): live ("F") fields preview through Service::preview, Save commits the draft,
// Revert discards it. Labels, grouping and help text follow the retired in-game Lua menu.
#include <memory>
#include <string>
#include <functional>

namespace NorthlightSettings { class Service; }

namespace NorthlightOverlay {
class SettingsPanel {
    struct State;
    std::unique_ptr<State> s;
public:
    // hotkey: the toggle's name, for the closed-window reminder.
    using CalibrationDraw=std::function<void(const float* rectangle,const float* color)>;
    SettingsPanel(std::shared_ptr<NorthlightSettings::Service> service,std::string hotkey,CalibrationDraw calibration={});
    ~SettingsPanel();
    SettingsPanel(const SettingsPanel&)=delete;
    SettingsPanel& operator=(const SettingsPanel&)=delete;
    // The window was shown: fetch a fresh snapshot.
    void opened();
    // scale: the overlay's UI scale (1 at 1080p). Returns false when the user closed the window. Polls the service itself.
    bool draw(float scale);
    // While the window is closed (no ImGui needed): advances pending service work (preview, clear, commit result, snapshot).
    void poll();
    bool pending()const;   // service work is still outstanding
    bool unsaved()const;   // the draft has edits
    // Closed window with an unsaved draft: one unobtrusive line in the lower left corner (no input).
    void notice(float scale);
};
} // namespace NorthlightOverlay
