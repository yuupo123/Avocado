#include "cdrom.h"
#include <imgui.h>
#include "disc/empty.h"
#include "disc/format/cue.h"
#include "system.h"
#include "utils/string.h"

using namespace disc;
#define POSITION(x) ((useFrames) ? string_format("%d", (x).toLba()).c_str() : (x).toString().c_str())

namespace gui::debug::cdrom {

void Cdrom::cdromWindow(System* sys) {
    ImGui::Begin("CDROM", &cdromWindowOpen);

    Disc* disc = sys->cdrom->disc.get();

    if (auto noCd = dynamic_cast<Empty*>(disc)) {
        ImGui::Text("No CD");
    } else if (auto cue = dynamic_cast<format::Cue*>(disc)) {
        ImGui::Text("%s", cue->file.c_str());

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::Text("Track  Pregap    Start     End       Offset     Type   File");

        ImGuiListClipper clipper((int)cue->getTrackCount());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                auto track = cue->tracks[i];

                auto line = string_format("%5d  %-8s  %-8s  %-8s  %-9zu  %-5s  %s", i + 1, POSITION(track.pregap), POSITION(track.index1),
                                          POSITION(track.index1 + Position::fromLba(track.frames)), track.offset,
                                          track.type == TrackType::DATA ? "DATA" : "AUDIO", getFilenameExt(track.filename).c_str());

                ImGui::Selectable(line.c_str());
            }
        }
        ImGui::PopStyleVar();

        ImGui::Checkbox("Use frames", &useFrames);
    }
    ImGui::End();
}

void Cdrom::displayWindows(System* sys) {
    if (cdromWindowOpen) cdromWindow(sys);
}
}  // namespace gui::debug::cdrom