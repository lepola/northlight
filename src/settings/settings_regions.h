#pragma once
#include <map>
#include <string>

namespace NorthlightSettings {
// Known outdoor regions (3.3.5a). Installed profiles may add other Map:id entries.
inline const std::map<std::string,const char*> Regions={
    {"Azeroth:1","Dun Morogh"},{"Azeroth:3","Badlands"},{"Azeroth:4","Blasted Lands"},{"Azeroth:8","Swamp of Sorrows"},{"Azeroth:10","Duskwood"},{"Azeroth:11","Wetlands"},
    {"Azeroth:12","Elwynn Forest"},{"Azeroth:28","Western Plaguelands"},{"Azeroth:33","Stranglethorn Vale"},{"Azeroth:38","Loch Modan"},{"Azeroth:40","Westfall"},
    {"Azeroth:41","Deadwind Pass"},{"Azeroth:44","Redridge Mountains"},{"Azeroth:45","Arathi Highlands"},{"Azeroth:46","Burning Steppes"},{"Azeroth:47","The Hinterlands"},
    {"Azeroth:51","Searing Gorge"},{"Azeroth:85","Tirisfal Glades"},{"Azeroth:130","Silverpine Forest"},{"Azeroth:139","Eastern Plaguelands"},{"Azeroth:267","Hillsbrad Foothills"},
    {"Azeroth:1497","Undercity"},{"Azeroth:1519","Stormwind City"},{"Azeroth:1537","Ironforge"},
    {"Kalimdor:14","Durotar"},{"Kalimdor:15","Dustwallow Marsh"},{"Kalimdor:16","Azshara"},{"Kalimdor:17","The Barrens"},{"Kalimdor:141","Teldrassil"},{"Kalimdor:148","Darkshore"},
    {"Kalimdor:215","Mulgore"},{"Kalimdor:331","Ashenvale"},{"Kalimdor:357","Feralas"},{"Kalimdor:361","Felwood"},{"Kalimdor:400","Thousand Needles"},{"Kalimdor:405","Desolace"},
    {"Kalimdor:406","Stonetalon Mountains"},{"Kalimdor:440","Tanaris"},{"Kalimdor:490","Un'Goro Crater"},{"Kalimdor:493","Moonglade"},{"Kalimdor:618","Winterspring"},
    {"Kalimdor:1377","Silithus"},{"Kalimdor:1637","Orgrimmar"},{"Kalimdor:1638","Thunder Bluff"},{"Kalimdor:1657","Darnassus"},
    {"Expansion01:3483","Hellfire Peninsula"},{"Expansion01:3518","Nagrand"},{"Expansion01:3519","Terokkar Forest"},{"Expansion01:3520","Shadowmoon Valley"},
    {"Expansion01:3521","Zangarmarsh"},{"Expansion01:3522","Blade's Edge Mountains"},{"Expansion01:3523","Netherstorm"},{"Expansion01:3703","Shattrath City"},
    {"Northrend:65","Dragonblight"},{"Northrend:66","Zul'Drak"},{"Northrend:67","The Storm Peaks"},{"Northrend:210","Icecrown"},{"Northrend:394","Grizzly Hills"},
    {"Northrend:495","Howling Fjord"},{"Northrend:2817","Crystalsong Forest"},{"Northrend:3537","Borean Tundra"},{"Northrend:3711","Sholazar Basin"},
    {"Northrend:4197","Wintergrasp"},{"Northrend:4395","Dalaran"},
};

} // namespace NorthlightSettings
