#undef NDEBUG
#include <cassert>
#include "settings_panel.cpp"

void northlightImGuiAssert(const char*, const char*, int) {}

int main() {
    using NorthlightOverlay::regionGroup;
    using NorthlightOverlay::regionLabel;
    assert(regionGroup("Expansion01:3430") == "Eastern Kingdoms");
    assert(regionGroup("Expansion01:3433") == "Eastern Kingdoms");
    assert(regionGroup("Expansion01:3487") == "Eastern Kingdoms");
    assert(regionGroup("Expansion01:4080") == "Eastern Kingdoms");
    assert(regionGroup("Expansion01:3455") == "Eastern Kingdoms");
    assert(regionGroup("Expansion01:3430/3665") == "Eastern Kingdoms");

    assert(regionGroup("Expansion01:3524") == "Kalimdor");
    assert(regionGroup("Expansion01:3525") == "Kalimdor");
    assert(regionGroup("Expansion01:3557") == "Kalimdor");
    assert(regionGroup("Expansion01:3479") == "Kalimdor");
    assert(regionGroup("Expansion01:3524/3526") == "Kalimdor");

    assert(regionGroup("Expansion01:3518") == "Outland");
    assert(regionGroup("Expansion01:default") == "Outland map defaults");
    assert(regionGroup("Azeroth:default") == "Eastern Kingdoms map defaults");

    assert(regionLabel("Azeroth:44") == "Redridge Mountains  (Azeroth:44)");
    assert(regionLabel("Expansion01:3487") == "Silvermoon City  (Expansion01:3487)");
    assert(regionLabel("Expansion01:4080") == "Isle of Quel'Danas  (Expansion01:4080)");
    assert(regionLabel("Azeroth:1537") == "Ironforge  (Azeroth:1537)");
    assert(regionLabel("Northrend:3711") == "Sholazar Basin  (Northrend:3711)");
    assert(regionLabel("Northrend:4197") == "Wintergrasp  (Northrend:4197)");
    assert(regionLabel("Azeroth:11/836") == "Dun Algaz, Wetlands  (Azeroth:11/836)");
    assert(regionLabel("Azeroth:267/1057") == "Thoradin's Wall, Hillsbrad Foothills  (Azeroth:267/1057)");
    assert(regionLabel("Azeroth:45/880") == "Thandol Span, Arathi Highlands  (Azeroth:45/880)");
    assert(regionLabel("Northrend:394/4202") == "Vordrassil's Tears, Grizzly Hills  (Northrend:394/4202)");
    assert(regionLabel("Expansion01:3430/3462") == "Fairbreeze Village  (Expansion01:3430/3462)");
}
