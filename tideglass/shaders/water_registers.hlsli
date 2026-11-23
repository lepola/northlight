// SM3 water bindings shared with the device writer and pass capture.
#ifndef TIDEGLASS_WATER_REGISTERS
#define TIDEGLASS_WATER_REGISTERS

#define WATER_SLOT_cViewportPx 0
#define WATER_SLOT_cTexelGrid 1
#define WATER_SLOT_cViewToNdc 2
#define WATER_SLOT_cDepthMapping 3
#define WATER_SLOT_cViewToWorld0 4
#define WATER_SLOT_cViewToWorld1 5
#define WATER_SLOT_cViewToWorld2 6
#define WATER_SLOT_cViewToWorld3 7
#define WATER_SLOT_cModeProbe 8
#define WATER_SLOT_cWaterTimeWavesFoamClarity 9
#define WATER_SLOT_cWaterReflectSpecTint 10
#define WATER_SLOT_cToLight 11
#define WATER_SLOT_cDirectColor 12
#define WATER_SLOT_cAmbientColor 13
#define WATER_SLOT_cFogColor 14
#define WATER_SLOT_cFogRange 15
#define WATER_SLOT_cWorldToView0 16
#define WATER_SLOT_cWorldToView1 17
#define WATER_SLOT_cWorldToView2 18
#define WATER_SLOT_cWorldToView3 19
#define WATER_SLOT_cSunColor 20
#define WATER_SLOT_cShallowColor 21
#define WATER_SLOT_cDeepColorDepth 22
#define WATER_SLOT_cStyleExtras 23
#define WATER_SLOT_cFoamColor 24
#define WATER_SLOT_cInsideAndReflection 25
#define WATER_SLOT_cRippleArea 26
#define WATER_SLOT_cRippleGrid 27
#define WATER_SLOT_cAbyss 28
#define WATER_SLOT_cGroundInfo 31
#define WATER_SLOT_cSurface 32
#define WATER_SLOT_cLookingDown 33
#define WATER_SLOT_cBoat 34
#define WATER_SLOT_cBoatShape 38
#define WATER_SLOT_cRingMap 44
#define WATER_SLOT_cSkyLayout 45
#define WATER_SLOT_cReflectionGrid 46
#define WATER_SLOT_cWaterDebug 47
#define WATER_SLOT_cLiquidOptions 48
#define WATER_SLOT_cShipBox 51
#define WATER_SLOT_cReflectionOptions 55
#define WATER_SLOT_cReflectionTuning 56
#define WATER_SLOT_cEnvironment 80
#define WATER_SLOT_cEnvironmentLight 81
#define WATER_SLOT_cEnvironmentPalette 82
#define WATER_SLOT_cGameFog 88
#define WATER_SLOT_cWaveMomentSamples 89
#define WATER_SLOT_cSurfaceVisibility 90
#define WATER_SLOT_cLampPositions 91
#define WATER_SLOT_cLampColours 95
#define WATER_SLOT_sDepth 0
#define WATER_SLOT_sOpaqueDepth 1
#define WATER_SLOT_sSceneBefore 2
#define WATER_SLOT_sSceneWithWater 3
#define WATER_SLOT_sWaterLevel 4
#define WATER_SLOT_sRememberedSky 5
#define WATER_SLOT_sRipples 6
#define WATER_SLOT_sSeabed 7
#define WATER_SLOT_sShore 8
#define WATER_SLOT_sRingMap 9
#define WATER_SLOT_sSurfaceVisibility 9
#define WATER_SLOT_sBoatPaths 10
#define WATER_SLOT_sSurfaceDetail 10
#define WATER_SLOT_sSurfaceSlopes 11
#define WATER_SLOT_sReflection 12
#define WATER_SLOT_sWakeMapPlace 12
#define WATER_SLOT_sReflectionHit 13
#define WATER_SLOT_sWakeMapWay 13
#define WATER_SLOT_sNoiseLattice 14
#define WATER_SLOT_sUnderwaterScene 14
#define WATER_SLOT_sFoamPoints 15
#define WATER_SLOT_sUnderwaterThickness 15

#define WATER_REGISTER_JOIN_(kind, slot) kind##slot
#define WATER_REGISTER_JOIN(kind, slot) WATER_REGISTER_JOIN_(kind, slot)
#define WATER_C_REGISTER(slot) register(WATER_REGISTER_JOIN(c, slot))
#define WATER_S_REGISTER(slot) register(WATER_REGISTER_JOIN(s, slot))
#endif
