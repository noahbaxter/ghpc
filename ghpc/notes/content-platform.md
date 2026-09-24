# The content platform

How GH1, GH2 and 80s content comes to live in one build, and what has to be
built for it. Supersedes `NEXT.md` and `BACKLOG.md` on this subject.

The aim, in Noah's words: all the venues, characters, models and songs in one
game, with the extra content reachable as its own campaign, and a way to switch
between sets from inside the menus.

## Settled: one engine, not three ports

**Do not recompile 80s or GH1 as separate targets.** One GH2 engine runs all
three games' content.

80s is GH2's engine with a different disc. Measured 2026-09-22 against the three
debug ELFs in `work/elf-debug/`: 12,279 of 12,663 functions share name and size
with GH2's, and the ~195 that differ are mostly renamed anonymous namespaces.
All 14 override targets exist at a constant shift (+0x128 low text, +0x140 high,
+0x180 data). A second recomp would be a second platform to maintain for nothing.

GH1 is an older engine: entry point 0x100010 against GH2's 0x100d68, 3,890 of
8,433 names shared with GH2, and 7 of our 14 override targets missing outright
(`PsCam::Select`, `PsMat::Select`, `PsEnviron::Select`, `PsMultiMesh::DrawShowing`,
`PsMesh::DrawShowing`, the PsMesh ctor, and `MetaMusic::Poll`, which in GH1 is a
set of free functions). Porting it would fork the platform and split mods. GH1
content reaches the platform as data instead, see below.

## Proven: 80s disc data runs on the GH2 build unmodified

2026-09-22, no code changes. `GEN/` from the 80s disc in `work/gh80s/` with a
symlink to `work/GH2_debug.elf` beside it, since the runtime takes game data and
`mc0/` from the ELF's directory. Driven with `route.sh`'s pad route for 150s
under `--native`.

Screen trail: boot, `bootup_create_save`, `main_screen`, new profile,
`sel_character_new_screen`, `tour_start_screen`, career, `sel_venue_screen`,
`sel_song_screen`, `game_screen`, fail, back to main. Three laps, no crash. The
shots show the 80s menu, the 80s setlist and the Battle of the Bands venue with
80s characters through the native renderer. 22 audio streams, no underruns.

Not checked: played by hand, audio by ear. The intro video failed only because
`VIDEOS/` was never extracted.

That disc CHD extracts with 2448-byte sectors, which 7z cannot read. Strip to
the first 2048 bytes of each sector first.

## Deluxe is not a data-layer mod

It patches the executable. Measured 2026-09-22: Deluxe 2.0's `GH2DEBUG.ELF` is
the same size as our vanilla `GH2_debug.elf` and carries the same 12,663
functions, but 186 of them differ in size. `CLAUDE.md` says Deluxe features
"attach to the data rather than the executable and apply to any host that runs
GH2's binary". That is wrong and should be corrected there.

**Deluxe data does not boot on the vanilla engine.** Driven run, same method as
the 80s test: the guest dies in an indirect call through a null target
(`source=0x126508 target=0x0`, `a1=0x30303130`, ASCII "0100") while loading
data, then presents one static frame for 17,863 frames. It never reaches a
screen.

## How Deluxe selects a campaign, which is the whole design

**One shared archive, nine executables, and they differ by one word.**

The Deluxe 2.0 disc has a single `GEN/` (3.6 GB) and nine ELFs: `GH2DX`,
`GH80SDX`, `GH1RDX`, `GHMEGADX`, `CUSTOMDX`, `DRUMDX`, `GHEXTRA`, plus
symbolized `GH2DEBUG.ELF` and `80SDEBUG.ELF`.

`GHMEGADX.ELF`, `GH80SDX.ELF` and `GH1RDX.ELF` are byte-identical except for 16
to 17 bytes in two places: a title string, and **one DTA preprocessor define**,
sitting next to `define`, `config` and `syscfg` in the string table.

    GHMEGADX     _MEGA
    GH80SDX      _ENCO
    GH1RDX       GH1RX
    GH2DEBUG     2DBUG
    80SDEBUG     8DBUG
    vanilla      none of them

Those are exactly the guard symbols in `ps2_deluxe/config/campaign.dta`. So the
shipped DTBs keep their preprocessor tags and the engine resolves them at load
time, from a define the executable injects. `dtb.py`'s known gap (its `TYPES`
omits tags 32 define, 33 include, 34 merge, 35 ifndef) says the same thing from
the other direction: real DTBs carry those tags.

That also explains the crash above. With no define set, every `#ifndef` guard
passes, so the config tree gets several `(order ...)` campaigns at once.

**This is the hook.** Campaign selection is one symbol handed to the DTA
preprocessor. In our build that becomes a host-side variable, so switching
campaigns needs no repack, no separate executables, and possibly no relaunch.
Deluxe ships nine ELFs only because it cannot change the one it has.

**The DX debug ELF looks nearly drop-in as a recompile target.** Same entry
point `0x100d68`, and all 14 of our override targets are at identical addresses
and sizes.

## The scene has already done the data work

Read `third_party/milo-script-library/gh2/ps2_deluxe/` before designing anything
here.

- **One merged data set already exists.** `ps2_deluxe/world/` carries 13 venue
  folders: GH2's 8 plus `battle2`, `small3`, `small4`, `theatre2`, `fest2` and
  `arena2` for the 80s venues.
- **The engine takes a much larger campaign than vanilla ships.**
  `ps2_deluxe/config/campaign.dta` holds a 14-venue campaign interleaving GH2
  and 80s venues tier by tier, 5 to 6 songs each, against vanilla's 8 and 5.
- **GH1 content is already converted to GH2 format.** The GH1 Redux archive
  header lists `char/alternagh1`, `char/classicgh1`, 13 venue folders and 99
  songs. So GH1 assets need no Milo version 10 to 24 converter from us. An
  earlier reading of this project said they would; that was wrong.
- **Campaigns are chosen at build time, and only because they must be.** Those
  variants sit behind preprocessor defines (`_ENCO`, `_MEGA`, `_ONYX`, `GH1RX`)
  that the DTA compiler resolves, so a build bakes in exactly one campaign.
  That is why the scene ships four separate ISOs. They cannot change the
  executable. We can. Runtime selection is the platform's first real
  contribution over what already exists.

Take data from the working `Guitar Hero II Deluxe 2.0.iso`, not from
GH2DX-Unified, whose README calls its PS2 side pre-alpha and not working.

## Design

One binary. One merged archive. Campaign, setlist and venue set selected at
runtime.

**The campaign is the pivot, and it is one define.** `config/campaign.dta` is
`#include`d into `gh2.dta` as a `(campaign ...)` node
(`ps2_final_060904A/config/gh2.dta:193`), and Deluxe's variants sit behind
`#ifndef` guards that the engine resolves when it loads the DTB. Setting that
define is the switching mechanism, and the data already supports it.

Building campaigns as separate dtbs and redirecting the read is the fallback, if
injecting a define turns out to be harder to reach than swapping the file.

**Quickplay's setlist follows the campaign, it is not a separate list.** The
song list is served by `song_provider`, which the panel merely turns on
(`career.dta:1069`, `{song_provider set_quickplay TRUE}`). Nothing in the DTA
supplies the quickplay set, and the engine has `Campaign::GetQuickplaySongs`
and `Campaign::GetQuickplayGuitars`. So switching the campaign switches what
quickplay shows. Strongly implied by the symbols, not yet traced. Confirm before
building on it.

**Venues need nothing new.** `VenueProvider` and `CampaignData::GetAllVenues`
are data-driven, so the merged data set makes the extra venues appear in the
venue screen as the campaign lists them.

**Saves go per content set.** The card save is a fixed 138,240 bytes, so two
campaigns cannot share one. We own the memcard root, so give each its own
folder.

## First slice

Setlist switching in quickplay, on the colour fret buttons, in the style of the
360 build's setlist/bonus/downloads tabs: Y for GH1, B for GH2, O for 80s. No
new UI art. It swaps what the existing list shows and nothing else.

That slice forces every piece the rest needs: merged data in the archive, a
selection the host holds, the campaign node redirect, and a list refresh. The
same swap then applies to the venue screen, and after that the campaign select
is the same mechanism with a screen in front of it.

## Open questions, in the order they block work

1. Does Deluxe data boot once a campaign define is set? Two ways to find out:
   recompile Deluxe's `GH2DEBUG.ELF`, which carries `2DBUG` already and whose
   override targets all match ours, or inject the define from the host into the
   vanilla build. The recompile is the lower-risk answer and tests the same
   thing.
2. Where does the engine read that define, and can the host set it? This is the
   switch. Start from the string's cross-references and `Defined__Fv`.
3. Where does `SongProvider` get its quickplay list? Trace it to
   `Campaign::GetQuickplaySongs` or find what else feeds it.
4. Can the config tree be re-read mid-session? This decides whether a switch is
   instant or needs a relaunch behind the loading art, about 6 seconds with
   boot skip.
5. Is there an engine ceiling on venues or songs per tier above Deluxe's 14?
   The fixed save size is the suspect.
6. How do loose files reach the game without repacking a 3 GB archive? Either
   repack with `arkhelper` per change, or serve loose files over the archive
   from the host. The overlay is the real mod platform and can wait until the
   first slice proves the flow.
