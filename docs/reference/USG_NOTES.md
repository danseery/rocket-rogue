# Primary Design Source: USG Notes

Source PDF: docs/reference/source-pdfs/USG Notes.pdf

This is an agent-readable text extract generated from the source PDF. Preserve the PDF as source of truth when the extract loses layout nuance.

## Page 1

USG Notes

## Page 2

Golden Triangle What is our Golden Triangle? Example: Boss Fight (Pick 2) Tough (High HP) Fast (Speedy) Strong (High Damage) Possible Planet Pillars Danger (High Damage, Radiation, Inclimate Weather, Lava Pools, etc.) Durability (High HP, Larger Planets, Impassible Materials) Value (Lucrivity, Scarcity, Technology) Element Pillars Weight Durability Value Tetherable vs Collectable Movement System View Illusion of 6 degrees of movement Really no up / down Cruise Mode Player flies around above planets Planets grow in scale depending on player proximity Intercept Mode Player flies on same plane as planets Gravity is in effect, allowing player to slingshot around planets Game Aesthetics Chunky Blocky Geometric Mobile games meets Straylight Mini games. Landing. Orbiting. Mining: Harvest rare elements and minerals Salvage: Archeology: Teleport down to the planet from orbit. Mini game: warp between systems. Must stay inside the warp pipe or be thrown out early. - This could be how ships wind up between systems.

## Page 3

Next Todo: Focus System Pressing spacebar will cycle through available targets in range When player is locked on: If player velocity is close to target velocity, match player velocity to target 
velocity
 UI: return information about the target object Activate gravity gauge (Line Renderer) for satellite objects or 
non-landable
 
objects
 Activate Altimeter for landable objects Todo: Animate thruster with random length and rotation when forward key is pressed Swap Camera between ship and orbited planet Galaxy Map / Warp System Bind T key to Target nearest planet: Match movement Gain information Mass Gravity Atmo Radius AU Lock Camera onto Planet Adjust Dolly param based on distance Activate Orbital Attitude Line Renderer Waypoint markers to point off screen towards objectives / POIs Orbital Attitude LR - Child of ship - Fires forward to track ships potential trajectory - Used to safely land or orbit a planet ******** Restructure Notes ******** - Player class will contain all eventual player-related information - Spatial Bodies will generate a Surface Collider based on scale. - SBs will have a gravity field Collider that will act as a trigger for applying gravitational 
forces
 
to
 
Orbital
 
objects
 - public interface Orbital - Require the necessary internals to allow an object to be affected by a gravity field - public interface Atmosphere - bool hasAtmo = true; - Atmosphere Collider to add drag around planet - Planetary gravity should be handled by its own script

## Page 4

- this will promote readability and modularity Parents : Children I. SpatialBody A. Sun 1. B. Planet 1. Rocky 2. Ice Giant 3. Gas Giant C. Satellite 1. Moon 2. Station Notes on Controller vs Handler: - Controller tends to be broader in spectrum, managing the overall flow of a scene, level, 
or
 
gameplay
 
mechanic
 - ex. LevelController - manage level progression, spawning enemies, loading new 
areas,
 
etc.
 - Handler is more focused, handling specific events or data types - ex. Input Handler captures player input and translates it into movement or 
actions,
 
or
 - CollisionHandler handles collision events between objects; triggers appropriate 
responses

## Page 5

Game Design Document

## Page 6

Orebit (???) Game Design Document Overview In this roguelite space-mining survival game, players choose from three unique planets per run to mine 
resources,
 
battle
 
enemies,
 
and
 
survive
 
intense
 
conditions.
 
Core
 
gameplay
 
revolves
 
around
 
exploration,
 
mining,
 
and
 
strategic
 
combat
 
using
 
passive
 
defense
 
mechanics
 
inspired
 
by
 
Brotato
 
and
 
Vampire
 
Survivor.
 
Gameplay Mechanics Planet Selection Standard Planets: In each run, players select 1 of 3 procedurally generated planets. Distinct environmental characteristics (lava flows, ice caves, radioactive zones). Used for resource mining, enemy encounters, and leveling up permanent upgrades. Planets have 3 levels. Burrow Directly to L2 or L3. Balatro style skip blinds. Fewer 
resources
 
gathered
 
so
 
its
 
harder,
 
but
 
bigger
 
reward,
 
less
 
time
 
wasted.
 Boss Planets: Four permanent planets containing ship components essential for repairing the Arc. Persistent tunnels across multiple visits; incremental progress allowed. Higher difficulty: tougher terrain, stronger enemies proportional to the planet's level. 
Player Entry and Movement Players directly drop onto planet surfaces using: Small landing craft (non-pilotable) Personal space suit equipped with a jetpack (to be finalized). Drop ship upgrade technology 10 Pt system. (10% further dig) 
Exploration and Mining Worlds feature procedurally generated terrain including destructible landscapes and dynamically 
created
 
underground
 
tunnels:
 Virgin soil: randomly generated free-form areas that ensure diverse and unique mining 
experiences
 
every
 
run.
 Pre-dug tunnels: algorithmically placed structures featuring a combination of linear and 
branching
 
paths,
 
enemy
 
encounter
 
zones,
 
and
 
specialized
 
rooms
 
such
 
as
 
treasure
 
vaults.
 Mining: Resource gathering to craft upgrades and defenses. Strategic mining is required to manage fuel, oxygen, and resource capacity.

## Page 7

Combat and Defense System Enemies Ant-like creatures: Basic threat, moderate speed, and damage. Flying creatures : Fast, darting creatures that follow you regardless of terrain Beetle-like creatures: Armored; slow-moving but heavy damage resistance. Elemental Monsters: Adapt to planet type (lava, ice, radioactive, toxic); special abilities, and area 
effects
 Mammals: Organic burrows and tunnels. Larger boss/treasure rooms. Harder more advanced 
tech
 
Rooms and Encounters Larger rooms containing: Rich resource deposits. Miniboss encounters provide significant upgrade rewards. 
Passive Defense System Players possess a suite of automatically firing weapons and abilities to handle enemy hordes. Defensive upgrades include: Orbiting drones and turrets. Reactive armor and environmental shields. Area control abilities (elemental fields, slowing zones, damage-over-time effects). 
Progression and Upgrades Permanent upgrades are unlocked through mining, defeating minibosses, and surviving 
planetary
 
challenges.
 Upgrades enhance: Mining efficiency. Jetpack movement and fuel efficiency. Passive defense capabilities (increased fire rate, damage, additional defensive units). 
Visual and Audio Style Atmospheric visuals tailored to planet themes. Bangin' follow-up soundtrack to the award-winning STRAYLIGHT OST Rich, ambient soundscapes enhance immersion and tension. Retro-inspired UI and aesthetics with modern fluid animations. This document will serve as the foundational guide for further detailing individual systems and 
facilitating
 
development
 
planning.

## Page 8

Animal Cast

## Page 9

Classes / Skills Capybara - the Tank, focus Survival Beast Mode - Become temporarily invulnerable Beaver - the Engineer, focus Resilience Hard Reboot - Turn your ship off and on again, resetting health and life support Fox - the Ace, focus Navigation Outta Here - Jump to the surface of a planet Prairie Dog - the Scout, focus Digging Deep Focus - Instantly scan all bodies w/in range Squirrel - the Hoarder, focus Resource Gathering Rummage Sale - Increased odds of finding rare or valuable materials Chipmunk - the Speedster, focus Exploration Phase Shift - Become temporarily incorporeal Animal Size Exp RG Sur Focus 
Capybara Very Large 1 1 3 Survival 
Beaver Large 1 2 2 Resilience 
Fox Medium 2 1 2 Navigation 
Prairie Dog Small 2 2 1 Digging 
Squirrel Very Small 1 3 1 Resource Gathering 
Chipmunk Tiny 3 1 1 Exploration Traits: Exploration Discover and Navigate the Cosmos Involves Planetary Scanning Valuable resources potential hazards POIs Navigation Charting courses Optimizing Travel Routes Avoiding Dangers / Hazards System Mapping Discover new star systems Charting system layouts Identifying potential jump points Anomaly Detection

## Page 10

Finding and investigating unusual phenomena Locating missing ship parts / lost crew Resource Gathering Acquiring materials necessary for survival and advancement Involves Mining: Extracting resources from planets & asteroids Salvaging: Recovering valuable materials from derelict ships / space 
debris
 Resource Management: Optimizing storage, usage, refinement of 
materials
 Survival Ensuring the continued operation of the ship and crew in the face of dangers Ship Maintenance Repairing damage Upgrading systems ensuring optimal ship performance Environmental Protections: Shielding the ship from Radiation Extreme Temperatures Environmental Hazards Life Support Mangement Maintaining Oxygen Levels Maintaining Life Support Systems Med Bay / Health Regen Hazard Mitigation: Dealing with the Unexpected Meteor Showers Solar Flares, etc New Possible Configurations: Exploration + Resource Gathering Logistics - Transporting, Storing, Hauling Resources Efficient resource transport Increased inventory storage Increased resource management efficiency (decreased costs) Increased Resource Transfer Rate / Range Advanced Towing Mechanics Improved Resource Refinement Techniques (increased yields) Excavation - Removing, Destroying, Refining Materials Increased Excavation Speed Reduced Digging Tool Wear / Tear Ability to Detect Valuable Resources Unlock Advanced Digging Techniques Increased Carrying Capacity for Resources Exploration + Survival Endurance

## Page 11

Sustaining operations over long periods of deep space travel Optimizing fuel consumption Navigating treacherous space anomalies Sustaining Crewman Health Focused on long-term sustainability and efficient travel across vast 
distances
 Ex. A player with high Endurance can travel vast distances with 
minimal
 
fuel
 
expenditure,
 
navigate
 
complex
 
asteroid
 
fields,
 
and
 
keep
 
their
 
crew
 
alive
 
and
 
productive
 
during
 
lengthy
 
expeditions.
 Navigation Improved Fuel Efficiency Enhanced Sensor Ranges / Visibility Ability to Chart Routes Reduced risk of collisions w/ debris Improved maneuverability Resource Gathering + Survival Engineering Improved Ship Engine Performance Advanced Ship Upgrades Reduced Repair Costs Resource Refinement Emergency Repairs Ability to Craft Emergency Supplies (02 Tanks, Hull Patches, etc) Enhanced Shield Tech Resilience Withstanding the hars realities of space exploration Surviving environmental hazards Maintaining Ship Health Efficiently utilizing resources to ensure long-term viability Focused on immediate threats and ship maintenance Ex. A player with high Resilience excels at minimizing resource waste, 
quickly
 
repairing
 
ship
 
damage,
 
and
 
adapting
 
to
 
challenging
 
environmental
 
conditions
 
(extreme
 
temperatures,
 
radiation,
 
etc).
