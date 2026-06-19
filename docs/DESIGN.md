# Rocket Rogue Design Notes

## Pillars

- Hidden-risk launch tension with no real-money gambling mechanics.
- Ship-first management through readable module slots and damage.
- Light but painful crew consequences.
- Roguelite persistence through unlock variety, records, memorials, and blueprints.
- Asset-free proof of concept using WebGL primitives and browser UI.

## Core loop

1. Configure the ship in the hangar.
2. Tune the eject target for the current frontier.
3. Launch and watch multiplier, heat, stress, and distance climb.
4. Eject before the hidden failure point or lose the ship.
5. Spend rewards on repairs, training, rest, and module offers.
6. Repeat proving flights until enough frontier readiness is banked.
7. Commit the agency to the next frontier, then repeat the loop farther from home.

## Risk model

Each launch creates a deterministic hidden crash point from:

- Frontier hazard and multiplier ceiling.
- Aggregated ship module stats.
- Assigned astronaut training, stress, and trait.
- Seeded random tail behavior.

Telemetry hints become more alarming as the current multiplier approaches the hidden crash point. Sensors improve warning quality but never reveal certainty.

## Persistence

The save format is versioned and line-based for a small dependency-free POC. The web build stores it in `localStorage` via `RocketBridge`. Future production builds should replace this with a JSON or binary schema plus migration tests once the content stabilizes.
