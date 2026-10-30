#pragma once

namespace NorthlightAOActorPolicy {
// The finite HBAO approximation is validated at the default 2yd radius. Fade
// its confidence from 2 to 3yd, then use the original kernel without mask work:
// applying the full correction at 4..10yd underestimates real occlusion.
constexpr bool enabled(bool master,bool contact,unsigned method,float strength,float radius){
    return master&&contact&&strength>0.f&&radius>.5f&&(method==3||(method==2&&radius<3.f));
}
}
