# Persistent expeditions

The [GDD](Rocket_Rogue_Game_Design_Document.docx) is the definitive design. This document describes the shared C++ implementation of solar travel and the return-or-continue loop. Battery missions and Ark/post-solar campaign integration remain specific TBDs in GDD Section 8.

## Runtime ownership

`SystemContent` defines stationary system transforms, body relationships, environment profiles, landing sites, hazards, and dock markers. Geology reuse does not imply shared campaign objectives: Mercury and Venus have separate site identities and no Moon/Mars objective binding. Gas giants have no terrain landing site; Io, Titan, Titania, and Triton supply satellite environments.

`FlightRunState` is the live ship. `SystemLocation` supplies frame identity and the saved pose boundary. `advanceExpeditionFlight` routes manual/cruise inputs through `updateLaunchFlight`, encounters bodies from actual position, and converts position and velocity with boundary hysteresis. Target selection never calls `beginLaunchFlight`, changes resources, or grants route permission.

`ExpeditionProgressionState` owns temporary XP, drafts, ranks, grafts, and synergies. `PersistentSiteState` owns dormant terrain and local objects. Loading a visited site preserves excavation without restoring an older build. Rig fuel is an independent expedition tank and does not refill on landing or destination change.

## Opening and flight guidance

The campaign opens at the authored Earth launch berth, held at rest with Moon selected and cruise off. `beginEarthOpening` runs only for demonstrably unstarted Earth campaigns; prior attempts, progress, sites, wrecks and uncertain records preserve the existing location. The campaign-once `lunar_approach` Incoming Message explains launch, controls, trajectory and orbit capture. Ready to launch acknowledges the message; a separate Launch action releases the berth and starts the expedition with its authored departure impulse. Fresh gameplay inputs are required. The berth, acknowledgement and release persist in v21; reloading cannot launch again or grant another impulse.

`FlightGuidance` supplies target position, bearing/distance, actual-body orbit guidance, predicted impact and one next-action hint to native and web. The bounded approach camera frames actual Earth below-left and the Moon above-right, then blends into local lunar orbit and descent. The Earth home icon appears only when the entire Earth sprite is outside the actual flight viewport, with no duplicate home icon when Earth is selected. Dock markers render independently of planet visibility. These camera transforms never alter physical position or momentum. Orbit bands preview the selected destination while departing a home frame, then belong to the actual encountered expedition body; landing gates appear only in their owning body frame; the navigation target uses a separate marker or edge arrow. `refreshExpeditionTrajectory` forecasts coasting with the live integrator and frame conversions, suppressing recursive prediction. The map's unattended-cruise forecast remains distinct.

Depart dock arms an attached waiting state. Forward thrust calls the shared release operation and applies normal thrust in the same step; waiting cannot consume fuel or fall. Optional `opening1` fields preserve initialization, departure history and this waiting state within v21. No campaign reset is required.

## Navigation and servicing

`plotSystemCourse` computes guidance only. Numerical fuel forecasts and unattended-cruise trajectory previews use the live integrator; forecast mode suppresses recursive HUD predictions. The fuel estimate assumes burn-and-coast piloting and a disclosed manual-approach allowance. A failed forecast is reported as unavailable. It is not an autopilot arrival guarantee.

`ToggleCruise` is C / left-stick click in Flight. Cruise applies ordinary forward thrust and steering toward the selected body center. Manual steering/thrust cancels it. There is no braking, obstacle avoidance, capture assistance, or resource exemption. Map inspection pauses gameplay and resumes the prior cruise state after fresh-input handling.

The Earth service dock is 1.962 units toward the Moon from Earth center, outside every local gravity region including its full docking range. Dock offsets belong to system content. Explicitly attached Earth dock saves follow the current marker, preserving resources and heading; free-flight and surface positions remain unchanged. `canDockExpedition` and `dockExpedition` use the same range/speed requirements; docking is explicit. Operational-home docking banks carried ore and arrival payout, services the ship and Rig tank, and clears temporary progression once. Departing retains the dock pose and creates no destination-relative replacement flight.

`PayloadTransfer` assigns contract ore first and puts ordinary deliveries into the expedition hold. The hold follows existing ship-hold capacity across visits. First accepted authored-site arrival carries the existing arrival payout; the site registry prevents repeat payout on revisits. Home banking makes these credits spendable in the deterministic next-rank shipyard. Installed capability and existing prices are preserved. Rank I is available without lessons or random offers; further research depends on the battery milestones defined by the GDD.

## Recovery and decisions

Unlocked Drone Ops remains available at an operational home and at the parked mining ship. Returning from loadout selection restores that service location without entering a route/refit gate; drone unlocks, costs, choices, and effects use the existing systems.

Ship loss and explicit abandonment create one wreck per expedition loss, containing carried salvage and unbanked payout. Fatal-body impacts place a signal outside the collider along the incoming path. A viable replacement appears at the operational home with permanent capability retained and temporary progression reset. `salvageWreck` requires physical rendezvous and explicit transfer; excess ore stays in the wreck if the hold fills. Repeated transfer and docking cannot duplicate cargo.

`queueExpeditionDecision` records a significant completed site objective. After ascent and safe presentation, the shared modal coordinator presents return home, recommended lead, or map. Acknowledgements and pending occurrences persist. Ordinary visits do not create this interruption. Incoming Messages remain the separate reusable one-button transmission component and use the same modal priority and input rules.

Scenario rewards and acknowledgements never relocate an initialized expedition. Moon/Mars rewards produce leads; retired Flyby activities are excluded from its objective presentation and action routing. Broader flexible-order battery/story reconciliation remains TBD S3.

## Persistence and verification

Version 21 remains the only accepted campaign schema. Optional travel/economy records default safely for existing v21 saves; no campaign reset or migration to another version is performed. A valid recorded physical flight/landing or confirmed Earth home initializes travel once. A pre-deployment flight without a Mining tank retains its prepared Rig allotment, or initializes the existing pack-plus-remaining-fuel allotment once; ship propellant stays unchanged. Ambiguous legacy route records retain their data and display a position-unavailable notice rather than receiving a guessed location. Legacy/post-solar compatibility paths are not evidence that the battery/Ark integration is complete.

Tests cover frame conversion, unselected encounters, ordinary cruise collisions and manual cancellation, a physically piloted Earth–Moon–Mars–Earth circuit, persistent build/site records, home banking, next-rank eligibility, wreck ownership, payout recovery, optional-site objective isolation, shared modal pauses, semantic actions, and exact v21 save/load. Existing landing, underground excavation, towing, deployment, and ascent tests remain relevant. Human play checks own travel feel, sustained fuel pressure, map readability, and physical controller acceptance.

## Local gravity

Each body applies capped inverse-square gravity inside its authored influence radius, then smoothly fades to zero by 1.1 times that radius. Bodies beyond this boundary exert no pull; live flight and forecasts share the same calculation. Local landing gravity remains separate.
