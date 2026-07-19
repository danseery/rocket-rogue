# OREBIT Steam Release Readiness

OREBIT has no public release date. It earns one by passing product, audience, platform, and creative-readiness gates. The public campaign begins and ends on Steam; the web build remains useful for internal development and parity testing, but it is not a public demo or a competing storefront funnel.

The product promise is:

> A compact space-mining roguelite where every resource decision affects whether your crew escapes with the ore—or gets home at all.

Launch, landing, fuel, crew, mining, and Ark progression must strengthen that mine-and-escape loop. OREBIT has no VR scope and is not planned as a live service.

## Machine-enforced policy

Two tracked files separate the durable agreement from changing evidence:

- `config/steam-release-policy.json` records the launch thresholds, platforms, Steam campaign shape, pricing, and AI boundary.
- `config/steam-readiness.json` records current evidence. It intentionally starts with no release date and every unproven gate marked not ready.

Use these commands from the repository root:

```powershell
npm run test:steam-readiness
npm run check:steam-readiness
npm run verify:steam-release
```

The first two commands run in native release CI before the Windows and Linux jobs. `verify:steam-release` is intentionally red today. It becomes a release gate only after the evidence file contains a real date, metrics, build reports, Deck review state, Playtest results, Steam campaign readiness, and human creative approvals. Evidence values should point to a retained report, artifact, or review URL rather than relying on memory.

Changing the thresholds is a product decision for the group, not a way to make a failing build green.

## Gate 1: page-ready vertical slice

Publish the Steam Coming Soon page only when all of the following are true:

- One polished 20–30 minute expedition represents the mine-and-escape loop.
- Final capsule direction is created and approved by Len or another consenting human artist.
- A gameplay trailer and screenshots honestly represent the build.
- Desktop and 1280×800 Deck UI are representative.
- Benjamin has reviewed story-facing store copy.
- The page omits a release year until the later readiness gates pass.

## Gate 2: Steam Playtest

Keep access and signups attached to the base Steam page. Run progressively larger waves:

1. 50-player private technical wave.
2. 250-player limited gameplay wave.
3. 1,000-player open-interest wave.

Advance only when the current wave has evidence for:

- At least 70% repeat-play intent.
- Median session length of at least 30 minutes.
- At least 60% of players understanding the ore/fuel/escape tradeoff.
- Clean Windows/Linux progression parity.
- No Deck controller, readability, suspend/resume, or save blocker.

## Gate 3: public Steam demo

Release the demo only after Playtest feedback stabilizes the core loop. Use the one-time demo notification deliberately and keep it at least 14 days away from another wishlist-email trigger. Meaningful Steam events should generally be six to eight weeks apart. Store-page broadcasts hosted by Rob or Chris are welcome when they want to participate; they are not obligations.

There is no public web-demo campaign, newsletter funnel, paid-ad plan, or non-Steam storefront competing for the call to action. Twitch and other appearances point players to the Steam page, Playtest, demo, broadcast, wishlist, or sale.

## Gate 4: festivals and release date

Use relevant themed Steam festivals while OREBIT is upcoming. Reserve the single Steam Next Fest appearance for the final polished demo. Prepare Curator Connect access for active curators covering mining, roguelites, strategy, space, and Steam Deck.

Do not set a release date until all of the following are evidenced:

- At least 10,000 outstanding wishlists.
- At least 3,000 public demo players.
- At least 70% positive repeat-play intent.
- Locked scope and a feature-complete release candidate.
- Ready Windows x64 and native Linux x64 artifacts with progression parity.
- Steam Deck criteria passed internally and Valve review completed, or reported issues corrected and resubmitted.
- Human-created final capsule art and visual direction.
- Human-created final music and story where those are in scope.

After the gates pass, choose a week that is not crowded by much stronger releases with OREBIT's main tags. Prepare Curator Connect, the final announcement, and a Steam Broadcast. Launch at $14.99 with a 10% discount. Concentrate optional collaborator showcases into launch week, then use announcements for the first substantial patch and genuinely major milestones.

## Platform acceptance

Every release candidate must pass Windows x64 and native Linux x64 packaging and tests. The existing native workflow builds both and packages their artifacts; Linux uses the pinned Steam Runtime SDK. Neither platform is a secondary port.

Steam Deck acceptance additionally requires:

- Complete controller navigation and correct glyphs.
- No launcher or mouse-only step.
- Readable UI at 1280×800.
- Correct suspend/resume behavior.
- Offline first launch.
- Cross-platform Steam Cloud saves.
- Stable frame pacing at the chosen performance target.
- No text-entry flow that fails to open the on-screen keyboard.

Codex can maintain CI, packaging parity, filesystem and save compatibility, controller matrices, Vulkan/shader checks, automated smoke tests, performance capture, and regression summaries. Physical Deck behavior and Valve review remain human/device verification.

## Collaborative ownership

These are contribution areas, not departments or assignments. The baseline assumes Dan is the only person continuously available.

- Dan keeps continuity between group sessions, implements shared direction, maintains builds, and connects the mechanical, technical, and creative pieces.
- Eric is OREBIT's closest product and quality collaborator. UI/UX, onboarding, interaction design, accessibility, information hierarchy, front-end implementation, and critical build review are worked through with him throughout development.
- Chris is a programming collaborator, technical sounding board, tester, and optional gameplay-broadcast partner.
- Len is a visual and art-direction collaborator. Art scope and timing begin with a conversation about his interest and availability.
- Rob owns his musical direction as composer and collaborator. Music work begins when OREBIT's rhythm, tone, and scope are stable enough to support it; streaming is never treated as payment.
- Benjamin is the story and voice collaborator. He shapes the narrative spine, crew/world character, and story-facing language after the mechanical structure is stable.

An informal recurring build session replaces top-down status meetings: play the current build, identify what is exciting or confusing, let the relevant collaborator lead in their domain, agree only on work someone wants and has time to own, and record decisions so Dan can maintain continuity.

Before publishing the commercial page, confirm together how the existing Dr. Bloc agreement applies to OREBIT's expenses, revenue, credits, music rights, streaming, and AI usage.

## AI boundary

AI may assist code, testing, builds, platform parity, Deck QA, profiling, bug reproduction, store-data analysis, release administration, UI implementation under Dan and Eric's direction, asset-pipeline tooling, localization infrastructure, accessibility checks, and footage indexing.

AI may not create or imitate art, visual identity, music, soundtracks, story, dialogue, lore, characters, narrative voice, a collaborator's style or unfinished work, final store imagery, or narrative trailers. Creative work remains human-created, human-owned, approved, and credited.
