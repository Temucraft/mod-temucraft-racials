# mod-temucraft-racials

Racials for Temucraft: every racial at its best and usable more often, and heritage (carry another race's racials, plus a second racial from the races you can't play).

Before: mod-temucraft-racials, mod-temucraft-heritage. Their tables, SQL file names, texts (`module_string` rows under their old names), settings and addon prefixes are unchanged, so an existing server only swaps the folders.

## From mod-temucraft-racials


Racials at their best: the strongest version each racial had over the years, with the nerfs left out, and usable
more often.

| Change | Before | After |
|---|---|---|
| Cooldowns of every racial | 3 min / 2 min / 105 sec | 2 min / 90 sec / ~80 sec (25% shorter, at most 2 min, not below 45 sec) |
| Shadowmeld, Escape Artist | 2 min, 105 sec | 1 min |
| Stoneform | poison, disease, bleed; +10% armor | also harmful curses and magic, and 10% less damage taken while it lasts |
| Gift of the Naaru | fixed heal | at least 20% of the target's maximum health |
| Hardiness / Da Voodoo Shuffle | 15% | 20% shorter stuns / movement impairing effects |
| The Human Spirit | +3% Spirit | +10% Spirit |
| Diplomacy | +10% reputation | also 5% off vendor prices with factions you're Friendly with |
| Expansive Mind | +5% mana | also +5% rage, energy and runic power |

The cooldown rule covers every spell ranked "Racial", so custom races' racials ([mod-worgoblin-high-elf](https://github.com/Temucraft/mod-worgoblin-high-elf)'s allied
races, the classes module's) get shorter cooldowns too. The server's and the client's cooldown are both corrected right
after the cast. Everything has a switch in the config.

Racials that later expansions split into several spells, and racials the 3.3.5 client has no spell for (Might of the
Mountain, Brawn, Arcane Acuity, Touch of Elune...), need new client spell data and aren't part of this module.

### Works with other modules

mod-temucraft-heritage: swapped and second racials get the same treatment. [mod-temucraft-class-tweaks](https://github.com/Temucraft/mod-temucraft-class-tweaks) no longer sets
Shadowmeld's cooldown by default (its `Cooldowns` list is empty); if you set racial cooldowns there too, leave them out
of one of the two.

### Install

Copy the module into `modules/`, re-run CMake and build. No SQL.

## From mod-temucraft-heritage


Heritage: a player can carry the racial traits of another race instead of their own: a dwarf with Shadowmeld and
Quickness, an orc with Every Man for Himself. Going back to their own traits is always possible (and free).

- **Choosing**: the **Heritage** button beside the character window (or `/heritage`) lists the races with their
  traits; or `.heritage` (list), `.heritage choose <race name or id>`, `.heritage reset`.
- **Rules** (config): from level 10, 10 gold, then 24 hours before the next change; optionally only races of the
  player's own faction.
- **Second racial**: on top of the heritage, a character can carry racials of races that can't be played on this
  server (Pandaren, Vulpera, Dracthyr... whatever the client has that no playable race owns), one by default, 10 gold
  each, dropping is free: in the same menu, or `.heritage bonus` (list), `.heritage take <name or id>`,
  `.heritage drop <name or id>`. Kept per character (`mod_temucraft_heritage_bonus`).
- **What changes**: all racial traits of the race (actives and passives, weapon specializations, resistances...).
  Class versions follow the player's class (Blood Fury for casters, Arcane Torrent for energy users, Gift of the
  Naaru...). Languages, looks and reputations stay those of the player's own race.

A race's traits are worked out at startup the way the game hands them to a new character: the racial skill lines
the race is given at creation (`playercreateinfo_skills`) and their SkillLineAbility spells that fit the race and the
player's class. So custom races are offered too, also those sharing the stock racial lines (as
[mod-worgoblin-high-elf](https://github.com/Temucraft/mod-worgoblin-high-elf)'s races do) and races above 32; racials other modules add to those lines come along. A race
whose own traits can't be found can't change heritage (it would keep its own next to the new ones), and a heritage
that can't be carried any more (race removed, module turned off) gives the own traits back at login. The choice is kept per
character (`mod_temucraft_heritage`) and put back on every login (the game hands a character its own racials again
when it loads).

The Heritage button is part of the Temucraft interface built by [mod-temucraft-client-version](https://github.com/Temucraft/mod-temucraft-client-version) (addon messages,
prefix THR); without it the commands work on their own.

### Install

1. Put this folder in `modules/` of your AzerothCore source, re-run CMake and build.
2. The SQL in `data/sql/` is applied by the worldserver on its next start: `mod_temucraft_heritage` (characters) and
   the module's texts in `module_string`.
3. Copy `conf/mod-temucraft-heritage.conf.dist` to `configs/modules/` as `mod-temucraft-heritage.conf` to change the
   settings. `.reload config` applies them.

### Works with other modules

- Other modules' cooldown or tuning changes to a racial (for example [mod-temucraft-class-tweaks](https://github.com/Temucraft/mod-temucraft-class-tweaks)' Shadowmeld) follow
  the spell, so they apply to whoever carries it.
- Spellbook tooltips are the game's own; nothing on the client changes.

### Independence

This module doesn't need any other module, nor any other Temucraft module.

### Texts and translations

The 13 player-facing texts are in `module_string` under `mod-temucraft-heritage`. Translations go in
`module_string_locale`. If a row is missing the English text in the code is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.
