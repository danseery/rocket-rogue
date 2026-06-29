# Supporting Source: Roguelite Elements

Source PDF: removed from the repository because the original export contained account-specific URLs.

This is the maintained agent-readable reference extract.

## Page 1

Tab 1

## Page 2

Core Roguelite Elements: 
 Procedural Generation: This is a cornerstone. Levels, items, and enemy placements are randomized, 
ensuring
 
each
 
playthrough
 
feels
 
unique.
 
 Permadeath (with Progression): While Roguelikes are known for strict permadeath, Roguelites often soften this by 
introducing
 
persistent
 
progression.
 
This
 
might
 
involve:
 Unlocking new items, abilities, or characters. Upgrading base stats or facilities. Gaining knowledge that aids future runs. Run-Based Structure: Gameplay is typically divided into distinct "runs," where the player attempts to 
progress
 
as
 
far
 
as
 
possible
 
before
 
inevitable
 
failure.
 
 Variety and Replayability: The combination of procedural generation and persistent progression 
encourages
 
multiple
 
playthroughs,
 
with
 
each
 
run
 
offering
 
new
 
challenges
 
and
 
opportunities.
 
 Emphasis on Player Skill and Adaptation: While randomness plays a role, success in a Roguelite often relies on the 
player's
 
ability
 
to
 
adapt
 
to
 
unexpected
 
situations
 
and
 
make
 
strategic
 
decisions.
 
 
Incorporating a Core Digging Mechanic: 
A digging mechanic can add a fascinating layer to a Roguelite, opening up new possibilities for 
exploration,
 
resource
 
gathering,
 
and
 
strategic
 
gameplay.
 
Here's
 
how
 
you
 
could
 
incorporate
 
it:
 
 Environmental Manipulation: Creating Paths: Players could dig through walls or obstacles to create shortcuts 
or
 
access
 
hidden
 
areas.
 Resource Gathering: Digging could yield valuable resources, such as minerals, 
ores,
 
or
 
rare
 
materials.
 Terrain Modification: Players could alter the terrain to create defensive 
positions,
 
traps,
 
or
 
escape
 
routes.
 Strategic Depth: Risk vs. Reward: Digging could be time-consuming or risky, exposing players to 
dangers
 
but
 
offering
 
significant
 
rewards.

## Page 3

Environmental Hazards: Digging could trigger traps, release enemies, or 
uncover
 
hazardous
 
materials.
 Digging Tools and Upgrades: Implement a system of digging tools with varying 
properties,
 
allowing
 
for
 
upgrades
 
that
 
increase
 
efficiency,
 
range,
 
or
 
special
 
abilities.
 
 Procedural Generation Integration: Diggable Layers: Generate levels with varying layers of diggable materials, 
each
 
with
 
unique
 
properties
 
and
 
resources.
 Hidden Chambers: Randomly generate hidden chambers or tunnels that can 
only
 
be
 
accessed
 
through
 
digging.
 Dynamic Terrain: Allow the terrain to dynamically change as players dig, 
creating
 
emergent
 
gameplay
 
scenarios.
 
 Roguelite Progression: Permanent Tool Upgrades: Allow players to unlock permanent upgrades for 
their
 
digging
 
tools,
 
increasing
 
their
 
effectiveness
 
in
 
future
 
runs.
 New Digging Abilities: Introduce new digging abilities or techniques that can be 
unlocked
 
through
 
progression.
 Resource Persistence: Allow players to retain certain resources gathered 
through
 
digging
 
between
 
runs,
 
providing
 
a
 
sense
 
of
 
persistent
 
progression.
 
Examples of Digging Mechanic uses: 
 Imagine a roguelite where you are trying to reach the core of a planet. Digging would be 
the
 
main
 
way
 
to
 
progress,
 
but
 
you
 
would
 
have
 
to
 
manage
 
oxygen,
 
and
 
heat,
 
while
 
dealing
 
with
 
the
 
planets
 
native
 
hostile
 
lifeforms.
 Imagine a roguelite where you are a dwarf, that is trying to find a legendary lost treasure, 
that
 
is
 
hidden
 
deep
 
within
 
a
 
mountain.
 
Digging
 
would
 
be
 
your
 
primary
 
way
 
of
 
exploring,
 
and
 
finding
 
new
 
areas,
 
and
 
new
 
dangers.
 
By carefully considering these elements, you can create a compelling Roguelite experience that 
seamlessly
 
integrates
 
a
 
core
 
digging
 
mechanic.

## Page 4

Orebit - Game Design Document

## Page 5

Game Design Document 
Overview 
In this roguelite space-mining survival game, players choose from three unique planets per run 
to
 
mine
 
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
 
 
Gameplay Mechanics 
Planet Selection 
 Standard Planets: In each run, players select 1 of 3 procedurally generated planets. Distinct environmental characteristics (lava flows, ice caves, radioactive zones). Used for resource mining, enemy encounters, and leveling up permanent 
upgrades.
 Boss Planets: Four permanent planets containing ship components essential for repairing the 
Arc.
 Persistent tunnels across multiple visits; incremental progress allowed. Higher difficulty: tougher terrain, stronger enemies proportional to the planet's 
level.
 
Player Entry and Movement 
 Players directly drop onto planet surfaces using: Small landing craft (non-pilotable, cinematic entry). Personal space suit equipped with a jetpack (to be finalized). 
Exploration and Mining 
 Worlds feature procedurally generated terrain including destructible landscapes and 
dynamically
 
created
 
underground
 
tunnels:
 Virgin soil: randomly generated free-form areas that ensure diverse and unique 
mining
 
experiences
 
every
 
run.
 Pre-dug tunnels: algorithmically placed structures featuring a combination of 
linear
 
and
 
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

## Page 6

Mining: Resource gathering to craft upgrades and defenses. Strategic mining is required to manage fuel, oxygen, and resource capacity. 
 
Combat and Defense System 
Enemies 
 Ant-like creatures: Basic threat, moderate speed, and damage. Beetle-like creatures: Armored; slow-moving but heavy damage resistance. Elemental Monsters: Adapt to planet type (lava, ice, radioactive, toxic); special abilities, 
and
 
area
 
effects.
 
Rooms and Encounters 
 Larger rooms containing: Rich resource deposits. Miniboss encounters providing significant upgrade rewards. 
Passive Defense System 
 Players possess a suite of automatically firing weapons and abilities to handle enemy 
hordes.
 Defensive upgrades include: Orbiting drones and turrets. Reactive armor and environmental shields. Area control abilities (elemental fields, slowing zones, damage-over-time effects). 
 
Progression and Upgrades 
 Permanent upgrades unlocked through mining, defeating minibosses, and surviving 
planetary
 
challenges.
 Upgrades enhance: Mining efficiency. Jetpack movement and fuel efficiency. Passive defense capabilities (increased fire rate, damage, additional defensive 
units).

## Page 7

Visual and Audio Style 
 Atmospheric visuals tailored to planet themes. Bangin' follow-up soundtrack to the award-winning STRAYLIGHT OST Rich, ambient soundscapes enhance immersion and tension. Retro-inspired UI and aesthetics with modern fluid animations. 
 
This document will serve as the foundational guide for further detailing individual systems and 
facilitating
 
development
 
planning.

## Page 8

Feedback

## Page 9

Repo/Game 
GitHub: https://github.com/<owner>/rocket-rogue Playable Web Version: https://<app-name>.azurestaticapps.net/rocket_rogue.html
Feedback 
 Game Speed multiplier for testing
