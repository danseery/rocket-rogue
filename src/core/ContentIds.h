#pragma once

namespace rocket::content {

namespace unlock {
inline constexpr const char* starter = "starter";
inline constexpr const char* thermal = "thermal";
inline constexpr const char* recovery = "recovery";
inline constexpr const char* deepSpace = "deep_space";
inline constexpr const char* ai = "ai";
inline constexpr const char* exotic = "exotic";
inline constexpr const char* surfaceProbes = "surface_probes";
inline constexpr const char* surfaceDrills = "surface_drills";
inline constexpr const char* cargoRigs = "cargo_rigs";
inline constexpr const char* analysisLab = "analysis_lab";
inline constexpr const char* perimeterDrones = "perimeter_drones";
inline constexpr const char* perimeterCoordination = "perimeter_coordination";
inline constexpr const char* arkScaffold = "ark_scaffold";
inline constexpr const char* droneBay = "drone_bay";
inline constexpr const char* droneSupportSuite = "drone_support_suite";
inline constexpr const char* ioHazardDrone = "io_hazard_drone";
inline constexpr const char* routeMars = "route_mars";
inline constexpr const char* routeJupiter = "route_jupiter";
inline constexpr const char* routeSaturn = "route_saturn";
inline constexpr const char* routeUranus = "route_uranus";
inline constexpr const char* routeNeptune = "route_neptune";
} // namespace unlock

namespace scenario {
inline constexpr const char* lunarProspector = "lunar_prospector_contract";
inline constexpr const char* marsBayExpansion = "mars_bay_expansion";
inline constexpr const char* volcanicDescent = "volcanic_descent";
inline constexpr const char* outerTransfer = "outer_transfer";
inline constexpr const char* saturnDeparture = "saturn_departure";
inline constexpr const char* uranusDeparture = "uranus_departure";
inline constexpr const char* neptuneDiscovery = "neptune_straylight_discovery";
inline constexpr const char* generatedTemplate = "generated_mining_template";
} // namespace scenario

namespace transferAssist {
inline constexpr const char* marsJupiter = "mars_jupiter_slingshot";
} // namespace transferAssist

namespace routeLink {
inline constexpr const char* earthMoon = "earth_moon";
inline constexpr const char* moonMars = "moon_mars";
inline constexpr const char* marsJupiter = "mars_jupiter";
inline constexpr const char* jupiterSaturn = "jupiter_saturn";
inline constexpr const char* saturnUranus = "saturn_uranus";
inline constexpr const char* uranusNeptune = "uranus_neptune";
} // namespace routeLink

namespace miningSite {
inline constexpr const char* lunarAnomalyCrevice = "lunar_anomaly_crevice";
inline constexpr const char* thermalLayeredRecovery = "thermal_layered_recovery";
} // namespace miningSite

namespace postSolarSystem {
inline constexpr const char* aaruVale = "aaru_vale";
inline constexpr const char* khepriPrime = "khepri_prime";
inline constexpr const char* riftBelt = "rift_belt";
} // namespace postSolarSystem

namespace protectedObjective {
inline constexpr const char* lunarSignalArtifact = "lunar_signal_artifact";
inline constexpr const char* ioMinorArtifact = "io_minor_artifact";
} // namespace protectedObjective

namespace module {
inline constexpr const char* fuelTanks1 = "fuel_tanks_1";
inline constexpr const char* fuelTanks2 = "fuel_tanks_2";
inline constexpr const char* fuelTanks3 = "fuel_tanks_3";
inline constexpr const char* flightControls1 = "flight_controls_1";
inline constexpr const char* flightControls2 = "flight_controls_2";
inline constexpr const char* flightControls3 = "flight_controls_3";
inline constexpr const char* coolingSystem1 = "cooling_system_1";
inline constexpr const char* coolingSystem2 = "cooling_system_2";
inline constexpr const char* coolingSystem3 = "cooling_system_3";
inline constexpr const char* hullPlating1 = "hull_plating_1";
inline constexpr const char* hullPlating2 = "hull_plating_2";
inline constexpr const char* hullPlating3 = "hull_plating_3";
inline constexpr const char* surveyArray1 = "survey_array_1";
inline constexpr const char* surveyArray2 = "survey_array_2";
inline constexpr const char* surveyArray3 = "survey_array_3";
inline constexpr const char* boreSystem1 = "bore_system_1";
inline constexpr const char* boreSystem2 = "bore_system_2";
inline constexpr const char* boreSystem3 = "bore_system_3";
inline constexpr const char* rigFuelLoop1 = "rig_fuel_loop_1";
inline constexpr const char* rigFuelLoop2 = "rig_fuel_loop_2";
inline constexpr const char* rigFuelLoop3 = "rig_fuel_loop_3";
// Version-nine launch refit ids remain available only for save migration.
inline constexpr const char* sparrowInjectorTune = "sparrow_injector_tune";
inline constexpr const char* reserveFeedManifold = "reserve_feed_manifold";
inline constexpr const char* sustainedBurnPackage = "sustained_burn_package";
inline constexpr const char* radiatorVaneExtension = "radiator_vane_extension";
inline constexpr const char* telemetryNoiseFilter = "telemetry_noise_filter";
inline constexpr const char* pressureBalanceBaffles = "pressure_balance_baffles";
inline constexpr const char* patchworkCrossBracing = "patchwork_cross_bracing";
inline constexpr const char* springCapsuleRetropack = "spring_capsule_retropack";
inline constexpr const char* recoveryCradle = "recovery_cradle";
inline constexpr const char* sparrowEngine = "sparrow_engine";
inline constexpr const char* kestrelEngine = "kestrel_engine";
inline constexpr const char* novaDrive = "nova_drive";
inline constexpr const char* stableTank = "stable_tank";
inline constexpr const char* slushTank = "slush_tank";
inline constexpr const char* deepReservoir = "deep_reservoir";
inline constexpr const char* patchworkHull = "patchwork_hull";
inline constexpr const char* titaniumRib = "titanium_rib";
inline constexpr const char* ablativeSkin = "ablative_skin";
inline constexpr const char* radiatorVanes = "radiator_vanes";
inline constexpr const char* cryoLoop = "cryo_loop";
inline constexpr const char* sacrificialSink = "sacrificial_sink";
inline constexpr const char* analogTelemetry = "analog_telemetry";
inline constexpr const char* hazardRadar = "hazard_radar";
inline constexpr const char* predictiveGuidance = "predictive_guidance";
inline constexpr const char* springCapsule = "spring_capsule";
inline constexpr const char* abortTower = "abort_tower";
inline constexpr const char* phoenixPod = "phoenix_pod";
inline constexpr const char* surfaceMapper = "surface_mapper";
inline constexpr const char* regolithAuger = "regolith_auger";
inline constexpr const char* oreSorter = "ore_sorter";
inline constexpr const char* coolantSleeve = "coolant_sleeve";
inline constexpr const char* diamondBearings = "diamond_bearings";
inline constexpr const char* deepBoreFrame = "deep_bore_frame";
inline constexpr const char* cargoSpine = "cargo_spine";
inline constexpr const char* haulerThrusters = "hauler_thrusters";
inline constexpr const char* massDriverWinch = "mass_driver_winch";
} // namespace module

namespace crewUpgrade {
inline constexpr const char* analogSimBay = "analog_sim_bay";
inline constexpr const char* highGSimulator = "high_g_simulator";
inline constexpr const char* medicalRecoveryWard = "medical_recovery_ward";
inline constexpr const char* missionPsychOffice = "mission_psych_office";
inline constexpr const char* traitCoachingLab = "trait_coaching_lab";
} // namespace crewUpgrade

namespace research {
inline constexpr const char* blueprintSurvey = "blueprint_survey";
inline constexpr const char* appliedMaterialsLab = "applied_materials_lab";
inline constexpr const char* prototypeSchematic = "prototype_schematic";
inline constexpr const char* xenogeologyProgram = "xenogeology_program";
inline constexpr const char* artifactDecoding = "artifact_decoding";
inline constexpr const char* fieldProbeNetwork = "field_probe_network";
inline constexpr const char* regolithDrillRig = "regolith_drill_rig";
inline constexpr const char* cargoReturnRig = "cargo_return_rig";
inline constexpr const char* missionAnalysisLab = "mission_analysis_lab";
inline constexpr const char* perimeterDroneNetwork = "perimeter_drone_network";
inline constexpr const char* arkScaffoldProgram = "ark_scaffold_program";
inline constexpr const char* droneBayProgram = "drone_bay_program";
} // namespace research

namespace surfaceUpgrade {
inline constexpr const char* resonantDischarge = "resonant_discharge";
inline constexpr const char* thermalDrillJackets = "thermal_drill_jackets";
inline constexpr const char* widebandPulse = "wideband_pulse";
inline constexpr const char* cargoSkids = "cargo_skids";
inline constexpr const char* shockMounts = "shock_mounts";
inline constexpr const char* oreScentArray = "ore_scent_array";
inline constexpr const char* coolantMist = "coolant_mist";
inline constexpr const char* recoilBraces = "recoil_braces";
inline constexpr const char* oreHopper = "ore_hopper";
inline constexpr const char* emergencyWinch = "emergency_winch";
inline constexpr const char* deepEchoMapper = "deep_echo_mapper";
inline constexpr const char* expandablePanniers = "expandable_panniers";
inline constexpr const char* vectorNozzles = "vector_nozzles";
inline constexpr const char* artifactTowline = "artifact_towline";
} // namespace surfaceUpgrade

namespace drone {
inline constexpr const char* miningDrone = "mining_drone";
inline constexpr const char* resourceDrone = "resource_drone";
inline constexpr const char* surveyDrone = "survey_drone";
inline constexpr const char* hazardDrone = "hazard_drone";
inline constexpr const char* legacyStabilizerDrone = "stabilizer_drone";
inline constexpr const char* attackDrone = "attack_drone";
inline constexpr const char* defenseDrone = "defense_drone";
} // namespace drone

namespace droneModule {
inline constexpr const char* combatDrill = "combat_drill";
inline constexpr const char* drillGuard = "drill_guard";
inline constexpr const char* pulseStrike = "pulse_strike";
inline constexpr const char* spectrumFilter = "spectrum_filter";
inline constexpr const char* oreRelay = "ore_relay";
inline constexpr const char* treasurePing = "treasure_ping";
inline constexpr const char* containmentShell = "containment_shell";
inline constexpr const char* reclamationLoop = "reclamation_loop";
inline constexpr const char* targetedAssault = "targeted_assault";
inline constexpr const char* penetratingImpact = "penetrating_impact";
inline constexpr const char* retributionArc = "retribution_arc";
inline constexpr const char* hazardScreen = "hazard_screen";
inline constexpr const char* resonantDischarge = "resonant_discharge";
} // namespace droneModule

namespace frame {
inline constexpr const char* pathfinder = "pathfinder";
inline constexpr const char* sprinter = "sprinter";
inline constexpr const char* ark = "ark";
} // namespace frame

namespace astronaut {
inline constexpr const char* ava = "ava";
inline constexpr const char* marco = "marco";
inline constexpr const char* nia = "nia";
inline constexpr const char* eli = "eli";
inline constexpr const char* jo = "jo";
inline constexpr const char* sana = "sana";
} // namespace astronaut

namespace destination {
inline constexpr const char* earthOrbit = "earth_orbit";
inline constexpr const char* moon = "moon";
inline constexpr const char* mars = "mars";
inline constexpr const char* jupiter = "jupiter";
inline constexpr const char* saturn = "saturn";
inline constexpr const char* uranus = "uranus";
inline constexpr const char* neptune = "neptune";
// Legacy save alias. This id is intentionally absent from the live catalog.
inline constexpr const char* outerPlanets = "outer_planets";
inline constexpr const char* nearbyStar = "nearby_star";
inline constexpr const char* nearbyGalaxy = "nearby_galaxy";
} // namespace destination

namespace achievement {
inline constexpr const char* skinOfYourTeeth = "skin_of_your_teeth";
} // namespace achievement

} // namespace rocket::content
