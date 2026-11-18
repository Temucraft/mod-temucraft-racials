/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Heritage: a player may take on the racial traits of another race in place of their own (and go back later).
// A race's racials are its racial skill lines (the "secondary" skill lines the race is given at character creation,
// playercreateinfo_skills) with their SkillLineAbility spells that fit the race: the same spells the game hands a new
// character. So custom races (also those sharing stock racial lines, or above race 32) and racials that other modules
// add are offered too. Class-specific versions (Blood Fury, Arcane Torrent, Gift of the
// Naaru...) follow the player's class.
//
// - .heritage, .heritage choose <race>, .heritage reset; a Heritage button on the character window (addon messages,
//   prefix THR).
// - Rules in the config: minimum level, price, how long before changing again, same faction only.

#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "TemucraftAddonMessage.h"
#include "TemucraftModuleText.h"
#include <algorithm>
#include <map>
#include <cstring>
#include <set>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
    constexpr char const* MODULE = "mod-temucraft-heritage";
    constexpr char const* PREFIX = "THR";

    enum Text : uint32
    {
        T_DISABLED = 1, T_LEVEL, T_MONEY, T_COOLDOWN, T_FACTION, T_UNKNOWN, T_DONE, T_RESET, T_ALREADY, T_HEAD, T_ROW,
        T_CURRENT, T_HELP, T_BONUS_UNKNOWN, T_BONUS_DROPPED, T_BONUS_FULL, T_BONUS_TAKEN, T_BONUS_HEAD, T_BONUS_ROW
    };
    char const* Say(Player* player, uint32 id, char const* fallback) { return TemucraftModuleText(MODULE, player, id, fallback); }

    struct Settings
    {
        bool enable = true;
        uint8 minLevel = 10;
        uint32 cost = 10 * GOLD;
        uint32 cooldownHours = 24;
        bool sameFaction = false;
        uint32 bonusCount = 1;
        uint32 bonusCost = 10 * GOLD;
        std::set<uint32> bonusExclude;
    } settings;

    struct Racial { uint32 spell; uint32 raceMask; uint32 classMask; };
    std::unordered_map<uint32, std::vector<Racial>> lineSpells;   // racial skill line -> its spells
    std::map<uint8, std::set<uint32>> raceLines;                   // race -> its racial skill lines
    std::map<uint8, std::map<uint8, std::vector<uint32>>> raceExtra; // race -> class -> racials given at creation
    std::set<uint8> races;                                          // races with racials of their own

    // Second racials: racials of races nobody can play here, one (or more) of which a player may carry on top.
    struct Bonus { uint32 spell; std::string name; };
    std::vector<Bonus> bonusPool;
    std::unordered_map<uint32, std::vector<uint32>> bonusChosen;   // player guid -> chosen bonus spells

    struct Choice { uint8 race = 0; uint32 changed = 0; };
    std::unordered_map<uint32, Choice> choices;      // player guid -> heritage (race 0 = their own)

    // Secondary skill lines that are no racials although every race may get them.
    bool IsProfessionLine(uint32 skill) { return skill == 129 || skill == 185 || skill == 356 || skill == 762; }

    void LoadSettings()
    {
        settings.enable = sConfigMgr->GetOption<bool>("Temucraft.Heritage.Enable", true);
        settings.minLevel = uint8(std::clamp<uint32>(sConfigMgr->GetOption<uint32>("Temucraft.Heritage.MinLevel", 10), 1, 255));
        settings.cost = sConfigMgr->GetOption<uint32>("Temucraft.Heritage.Cost", 10 * GOLD);
        settings.cooldownHours = sConfigMgr->GetOption<uint32>("Temucraft.Heritage.CooldownHours", 24);
        settings.sameFaction = sConfigMgr->GetOption<bool>("Temucraft.Heritage.SameFaction", false);
        settings.bonusCount = sConfigMgr->GetOption<uint32>("Temucraft.Heritage.Bonus.Count", 1);
        settings.bonusCost = sConfigMgr->GetOption<uint32>("Temucraft.Heritage.Bonus.Cost", 10 * GOLD);
        settings.bonusExclude.clear();
        std::string list = sConfigMgr->GetOption<std::string>("Temucraft.Heritage.Bonus.Exclude", "");
        std::replace(list.begin(), list.end(), ',', ' ');
        std::istringstream in(list);
        uint32 id;
        while (in >> id)
            settings.bonusExclude.insert(id);
    }

    bool IsRacialRank(SpellInfo const* info)
    {
        char const* rank = info ? info->Rank[LOCALE_enUS] : nullptr;
        return rank && (std::strcmp(rank, "Racial") == 0 || std::strcmp(rank, "Racial Passive") == 0);
    }

    std::vector<uint32> SpellsFor(uint8 race, uint8 cls);

    void LoadRacials()
    {
        lineSpells.clear();
        raceLines.clear();
        raceExtra.clear();
        races.clear();
        bonusPool.clear();
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* e = sSkillLineAbilityStore.LookupEntry(i);
            if (!e || e->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN || IsProfessionLine(e->SkillLine))
                continue;
            SkillLineEntry const* line = sSkillLineStore.LookupEntry(e->SkillLine);
            if (!line || line->categoryId != SKILL_CATEGORY_SECONDARY || !sSpellMgr->GetSpellInfo(e->Spell))
                continue;
            lineSpells[e->SkillLine].push_back({ e->Spell, e->RaceMask, e->ClassMask });
        }
        for (uint32 race = 1; race < sChrRacesStore.GetNumRows() && race < 256; ++race)
        {
            if (!sChrRacesStore.LookupEntry(race))
                continue;
            for (uint32 cls = 1; cls < MAX_CLASSES; ++cls)
                if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, cls))
                {
                    for (PlayerCreateInfoSkill const& skill : info->skills)
                        if (lineSpells.count(skill.SkillId))
                        {
                            raceLines[uint8(race)].insert(skill.SkillId);
                            races.insert(uint8(race));
                        }
                    // races whose racials are given at creation instead of with a skill line (custom races)
                    for (uint32 spell : info->customSpells)
                        if (IsRacialRank(sSpellMgr->GetSpellInfo(spell)))
                        {
                            raceExtra[uint8(race)][uint8(cls)].push_back(spell);
                            races.insert(uint8(race));
                        }
                }
        }

        // Every racial a playable race carries, by name, to leave those out of the second racials
        std::set<std::string> playableNames;
        for (uint8 race : races)
            for (uint8 cls = 1; cls < MAX_CLASSES; ++cls)
                for (uint32 spell : SpellsFor(race, cls))
                    if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell))
                        playableNames.insert(info->SpellName[LOCALE_enUS]);
        std::set<std::string> seen;
        for (uint32 i = 0; i < sSpellMgr->GetSpellInfoStoreSize(); ++i)
        {
            SpellInfo const* info = sSpellMgr->GetSpellInfo(i);
            if (!IsRacialRank(info) || info->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY) || settings.bonusExclude.count(i))
                continue;
            std::string const name = info->SpellName[LOCALE_enUS];
            if (name.empty() || playableNames.count(name) || !seen.insert(name).second)
                continue;
            bonusPool.push_back({ i, name });
        }
        LOG_INFO("server.loading", "{}: racial traits of {} races, {} second racials.", MODULE, races.size(), bonusPool.size());
    }

    // A spell row fits a race the way the core decides it for new characters; races past 32 have no bit of their own,
    // so for them a row fits when it is meant for (nearly) every race.
    bool FitsRace(uint32 raceMask, uint8 race)
    {
        if (!raceMask)
            return true;
        if (race <= 32)
            return (raceMask & (1u << (race - 1))) != 0;
        uint32 bits = 0;
        for (uint32 m = raceMask; m; m &= m - 1)
            ++bits;
        return bits >= 20;
    }

    std::vector<uint32> SpellsFor(uint8 race, uint8 cls)
    {
        std::vector<uint32> out;
        uint32 const classMask = 1u << (cls - 1);
        if (auto itr = raceLines.find(race); itr != raceLines.end())
            for (uint32 skill : itr->second)
                for (Racial const& r : lineSpells[skill])
                    if ((!r.classMask || (r.classMask & classMask)) && FitsRace(r.raceMask, race))
                        if (std::find(out.begin(), out.end(), r.spell) == out.end())
                            out.push_back(r.spell);
        if (auto itr = raceExtra.find(race); itr != raceExtra.end())
            if (auto c = itr->second.find(cls); c != itr->second.end())
                for (uint32 spell : c->second)
                    if (std::find(out.begin(), out.end(), spell) == out.end())
                        out.push_back(spell);
        return out;
    }

    // every racial spell any race could carry with this class (all that Apply may take away)
    std::vector<uint32> AllRacialSpells(uint8 cls)
    {
        std::vector<uint32> out;
        uint32 const classMask = 1u << (cls - 1);
        std::set<uint32> lines;
        for (auto const& [race, skills] : raceLines)
            lines.insert(skills.begin(), skills.end());
        for (uint32 skill : lines)
            for (Racial const& r : lineSpells[skill])
                if (!r.classMask || (r.classMask & classMask))
                    if (std::find(out.begin(), out.end(), r.spell) == out.end())
                        out.push_back(r.spell);
        for (auto const& [race, byClass] : raceExtra)
            if (auto c = byClass.find(cls); c != byClass.end())
                for (uint32 spell : c->second)
                    if (std::find(out.begin(), out.end(), spell) == out.end())
                        out.push_back(spell);
        return out;
    }

    bool InBonusPool(uint32 spell)
    {
        return std::any_of(bonusPool.begin(), bonusPool.end(), [spell](Bonus const& b) { return b.spell == spell; });
    }

    // Learn the chosen second racials, forget second racials no longer chosen.
    void ApplyBonus(Player* player)
    {
        std::vector<uint32> const& chosen = bonusChosen[player->GetGUID().GetCounter()];
        for (Bonus const& b : bonusPool)
        {
            bool const want = settings.enable && std::find(chosen.begin(), chosen.end(), b.spell) != chosen.end();
            if (want && !player->HasSpell(b.spell))
                player->learnSpell(b.spell);
            else if (!want && player->HasSpell(b.spell))
                player->removeSpell(b.spell, SPEC_MASK_ALL, false);
        }
    }

    uint8 HeritageOf(Player* player)
    {
        auto itr = choices.find(player->GetGUID().GetCounter());
        return itr != choices.end() && itr->second.race ? itr->second.race : player->getRace(true);
    }

    // Makes the player's racials those of `race`, whatever they had before.
    void Apply(Player* player, uint8 race)
    {
        uint8 const cls = player->getClass();
        std::vector<uint32> keep = SpellsFor(race, cls);
        for (uint32 spell : AllRacialSpells(cls))
            if (std::find(keep.begin(), keep.end(), spell) == keep.end() && player->HasSpell(spell))
                player->removeSpell(spell, SPEC_MASK_ALL, false);
        for (uint32 spell : keep)
            if (!player->HasSpell(spell))
                player->learnSpell(spell);
    }

    std::string RaceName(Player* player, uint8 race)
    {
        ChrRacesEntry const* entry = sChrRacesStore.LookupEntry(race);
        if (!entry)
            return "?";
        LocaleConstant locale = player->GetSession() ? player->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
        char const* name = entry->name[locale];
        return name && *name ? name : entry->name[LOCALE_enUS];
    }

    bool Offered(Player* player, uint8 race)
    {
        if (race == player->getRace(true) || SpellsFor(race, player->getClass()).empty())
            return false;
        if (settings.sameFaction && Player::TeamIdForRace(race) != Player::TeamIdForRace(player->getRace(true)))
            return false;
        return true;
    }

    uint32 CooldownLeft(Player* player)
    {
        auto itr = choices.find(player->GetGUID().GetCounter());
        if (itr == choices.end() || !itr->second.changed || !settings.cooldownHours)
            return 0;
        uint32 const now = uint32(GameTime::GetGameTime().count());
        uint32 const ready = itr->second.changed + settings.cooldownHours * HOUR;
        return ready > now ? ready - now : 0;
    }

    void SendList(Player* player)
    {
        if (!player->GetSession())
            return;
        TemucraftAddonSend(player, PREFIX, Acore::StringFormat("S~{}~{}~{}~{}~{}~{}", settings.enable ? 1 : 0, HeritageOf(player),
            player->getRace(true), settings.minLevel, settings.cost, CooldownLeft(player)));
        if (!settings.enable)
            return;
        TemucraftAddonSend(player, PREFIX, "BEGIN");
        for (uint8 race : races)
        {
            if (race != player->getRace(true) && !Offered(player, race))
                continue;
            std::string spells;
            for (uint32 spell : SpellsFor(race, player->getClass()))
            {
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell); info && info->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY))
                    continue;
                if (!spells.empty())
                    spells += ',';
                spells += std::to_string(spell);
            }
            TemucraftAddonSend(player, PREFIX, Acore::StringFormat("R~{}~{}~{}", race, TemucraftAddonClean(RaceName(player, race)), spells));
        }
        // second racials: B~spell~chosen, and how many may be carried
        std::vector<uint32> const& chosen = bonusChosen[player->GetGUID().GetCounter()];
        TemucraftAddonSend(player, PREFIX, Acore::StringFormat("C~{}~{}", settings.bonusCount, settings.bonusCost));
        if (settings.bonusCount)
            for (Bonus const& b : bonusPool)
                TemucraftAddonSend(player, PREFIX, Acore::StringFormat("B~{}~{}", b.spell,
                    std::find(chosen.begin(), chosen.end(), b.spell) != chosen.end() ? 1 : 0));
        TemucraftAddonSend(player, PREFIX, "END");
    }

    // Takes on the racials of `race` (the player's own race: back to their own). Checks the rules, pays.
    bool Choose(Player* player, uint8 race)
    {
        ChatHandler handler(player->GetSession());
        if (!settings.enable)
        {
            handler.SendSysMessage(Say(player, T_DISABLED, "Heritage can't be changed on this server."));
            return false;
        }
        bool const back = race == player->getRace(true);
        if (!back && SpellsFor(player->getRace(true), player->getClass()).empty())
        {
            handler.SendSysMessage(Say(player, T_UNKNOWN, "That heritage isn't available to you."));
            return false;
        }
        if (!back && !Offered(player, race))
        {
            handler.SendSysMessage(Say(player, T_UNKNOWN, "That heritage isn't available to you."));
            return false;
        }
        if (race == HeritageOf(player))
        {
            handler.SendSysMessage(Say(player, T_ALREADY, "That is already your heritage."));
            return false;
        }
        if (player->GetLevel() < settings.minLevel)
        {
            handler.PSendSysMessage(Say(player, T_LEVEL, "You can change your heritage from level {}."), settings.minLevel);
            return false;
        }
        if (uint32 left = CooldownLeft(player))
        {
            handler.PSendSysMessage(Say(player, T_COOLDOWN, "You changed your heritage recently. Try again in {} h {} min."),
                left / HOUR, (left % HOUR) / MINUTE);
            return false;
        }
        if (!back && settings.cost && !player->HasEnoughMoney(settings.cost))
        {
            handler.PSendSysMessage(Say(player, T_MONEY, "Changing your heritage costs {} gold."), settings.cost / GOLD);
            return false;
        }
        if (!back && settings.cost)
            player->ModifyMoney(-int32(settings.cost));

        uint32 const guid = player->GetGUID().GetCounter();
        Choice& c = choices[guid];
        c.race = back ? 0 : race;
        c.changed = uint32(GameTime::GetGameTime().count());
        CharacterDatabase.Execute("REPLACE INTO `mod_temucraft_heritage` (`guid`, `race`, `changed`) VALUES ({}, {}, {})", guid, c.race, c.changed);
        Apply(player, race);
        if (back)
            handler.SendSysMessage(Say(player, T_RESET, "|cffe6cc80Heritage:|r your own racial traits are back."));
        else
            handler.PSendSysMessage(Say(player, T_DONE, "|cffe6cc80Heritage:|r you now carry the racial traits of the {}."), RaceName(player, race));
        SendList(player);
        return true;
    }

    void SaveBonus(Player* player)
    {
        uint32 const guid = player->GetGUID().GetCounter();
        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        trans->Append("DELETE FROM `mod_temucraft_heritage_bonus` WHERE `guid` = {}", guid);
        for (uint32 spell : bonusChosen[guid])
            trans->Append("INSERT INTO `mod_temucraft_heritage_bonus` (`guid`, `spell`) VALUES ({}, {})", guid, spell);
        CharacterDatabase.CommitTransaction(trans);
    }

    uint32 FindBonus(std::string const& text)
    {
        if (Optional<uint32> id = Acore::StringTo<uint32>(text); id && InBonusPool(*id))
            return *id;
        std::string want = text;
        std::transform(want.begin(), want.end(), want.begin(), ::tolower);
        for (Bonus const& b : bonusPool)
        {
            std::string name = b.name;
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (!want.empty() && name.compare(0, want.size(), want) == 0)
                return b.spell;
        }
        return 0;
    }

    // Take a second racial (paid), or drop one (free)
    bool ChooseBonus(Player* player, uint32 spell, bool take)
    {
        ChatHandler handler(player->GetSession());
        std::vector<uint32>& chosen = bonusChosen[player->GetGUID().GetCounter()];
        auto itr = std::find(chosen.begin(), chosen.end(), spell);
        if (!settings.enable || !settings.bonusCount || !InBonusPool(spell))
        {
            handler.SendSysMessage(Say(player, T_BONUS_UNKNOWN, "That second racial isn't available."));
            return false;
        }
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
        if (!take)
        {
            if (itr == chosen.end())
                return false;
            chosen.erase(itr);
            SaveBonus(player);
            ApplyBonus(player);
            handler.PSendSysMessage(Say(player, T_BONUS_DROPPED, "|cffe6cc80Heritage:|r you no longer carry {}."), info->SpellName[LOCALE_enUS]);
            SendList(player);
            return true;
        }
        if (itr != chosen.end())
            return false;
        if (player->GetLevel() < settings.minLevel)
        {
            handler.PSendSysMessage(Say(player, T_LEVEL, "You can change your heritage from level {}."), settings.minLevel);
            return false;
        }
        if (chosen.size() >= settings.bonusCount)
        {
            handler.PSendSysMessage(Say(player, T_BONUS_FULL, "You already carry {} second racial(s); drop one first."), settings.bonusCount);
            return false;
        }
        if (settings.bonusCost && !player->HasEnoughMoney(settings.bonusCost))
        {
            handler.PSendSysMessage(Say(player, T_MONEY, "Changing your heritage costs {} gold."), settings.bonusCost / GOLD);
            return false;
        }
        if (settings.bonusCost)
            player->ModifyMoney(-int32(settings.bonusCost));
        chosen.push_back(spell);
        SaveBonus(player);
        ApplyBonus(player);
        handler.PSendSysMessage(Say(player, T_BONUS_TAKEN, "|cffe6cc80Heritage:|r you now also carry {}."), info->SpellName[LOCALE_enUS]);
        SendList(player);
        return true;
    }

    // a race by id or (the start of) its name
    uint8 FindRace(Player* player, std::string const& text)
    {
        if (Optional<uint32> id = Acore::StringTo<uint32>(text); id && *id < 256)
            return uint8(*id);
        std::string want = text;
        std::transform(want.begin(), want.end(), want.begin(), ::tolower);
        for (uint8 race : races)
        {
            std::string name = RaceName(player, race);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (!want.empty() && name.compare(0, want.size(), want) == 0)
                return race;
        }
        return 0;
    }
}

class TemucraftHeritageWorld : public WorldScript
{
public:
    TemucraftHeritageWorld() : WorldScript("TemucraftHeritageWorld", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP }) { }

    void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
    void OnStartup() override { LoadRacials(); }
};

class TemucraftHeritagePlayer : public PlayerScript
{
public:
    TemucraftHeritagePlayer() : PlayerScript("TemucraftHeritagePlayer", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_LEVEL_CHANGED, PLAYERHOOK_ON_DELETE, PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT }) { }

    void OnPlayerLogin(Player* player) override
    {
        uint32 const guid = player->GetGUID().GetCounter();
        if (QueryResult r = CharacterDatabase.Query("SELECT `race`, `changed` FROM `mod_temucraft_heritage` WHERE `guid` = {}", guid))
            choices[guid] = { r->Fetch()[0].Get<uint8>(), r->Fetch()[1].Get<uint32>() };
        std::vector<uint32>& chosen = bonusChosen[guid];
        chosen.clear();
        if (QueryResult r = CharacterDatabase.Query("SELECT `spell` FROM `mod_temucraft_heritage_bonus` WHERE `guid` = {}", guid))
            do chosen.push_back(r->Fetch()[0].Get<uint32>()); while (r->NextRow());
        ApplyBonus(player);
        // the core gives a character its own racials back on every login: put the chosen ones back in their place
        Restore(player);
    }

    void OnPlayerLogout(Player* player) override
    {
        choices.erase(player->GetGUID().GetCounter());
        bonusChosen.erase(player->GetGUID().GetCounter());
    }

    void OnPlayerDelete(ObjectGuid guid, uint32 /*accountId*/) override
    {
        CharacterDatabase.Execute("DELETE FROM `mod_temucraft_heritage` WHERE `guid` = {}", guid.GetCounter());
        CharacterDatabase.Execute("DELETE FROM `mod_temucraft_heritage_bonus` WHERE `guid` = {}", guid.GetCounter());
    }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        Restore(player);
    }

    // A heritage that can no longer be carried (its race gone, the module turned off) gives the own racials back.
    static void Restore(Player* player)
    {
        uint8 const own = player->getRace(true);
        uint8 const race = HeritageOf(player);
        if (race == own)
            return;
        if (!settings.enable || SpellsFor(race, player->getClass()).empty())
        {
            choices[player->GetGUID().GetCounter()].race = 0;
            Apply(player, own);
            return;
        }
        Apply(player, race);
    }

    using PlayerScript::OnPlayerCanUseChat;
    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 language, std::string& msg, Player* receiver) override
    {
        std::string body;
        if (!TemucraftAddonParse(player, type, language, msg, receiver, PREFIX, body))
            return true;
        std::vector<std::string> a = TemucraftAddonFields(body);
        if (a.empty())
            return false;
        if (a[0] == "LIST")
            SendList(player);
        else if (a[0] == "RESET")
            Choose(player, player->getRace(true));
        else if (a[0] == "PICK" && a.size() >= 2)
        {
            if (Optional<uint32> race = Acore::StringTo<uint32>(a[1]); race && *race < 256)
                Choose(player, uint8(*race));
        }
        else if ((a[0] == "BTAKE" || a[0] == "BDROP") && a.size() >= 2)
            if (Optional<uint32> spell = Acore::StringTo<uint32>(a[1]))
                ChooseBonus(player, *spell, a[0] == "BTAKE");
        return false;
    }
};

class TemucraftHeritageCommands : public CommandScript
{
public:
    TemucraftHeritageCommands() : CommandScript("TemucraftHeritageCommands") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable sub =
        {
            { "choose", HandleChoose, SEC_PLAYER, Console::No },
            { "reset",  HandleReset,  SEC_PLAYER, Console::No },
            { "bonus",  HandleBonus,  SEC_PLAYER, Console::No },
            { "take",   HandleTake,   SEC_PLAYER, Console::No },
            { "drop",   HandleDrop,   SEC_PLAYER, Console::No },
            { "",       HandleList,   SEC_PLAYER, Console::No },
        };
        static ChatCommandTable root = { { "heritage", sub } };
        return root;
    }

    static bool HandleList(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        if (!settings.enable)
        {
            handler->SendSysMessage(Say(player, T_DISABLED, "Heritage can't be changed on this server."));
            return true;
        }
        handler->PSendSysMessage(Say(player, T_CURRENT, "|cffe6cc80Heritage:|r {} (level {}+, {} gold, then {} h before the next change)."),
            RaceName(player, HeritageOf(player)), settings.minLevel, settings.cost / GOLD, settings.cooldownHours);
        handler->SendSysMessage(Say(player, T_HEAD, "Racial traits you can take on:"));
        LocaleConstant locale = handler->GetSessionDbcLocale();
        for (uint8 race : races)
        {
            if (!Offered(player, race))
                continue;
            std::string names;
            for (uint32 spell : SpellsFor(race, player->getClass()))
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell); info && !info->HasAttribute(SPELL_ATTR0_DO_NOT_DISPLAY))
                    names += (names.empty() ? "" : ", ") + std::string(info->SpellName[locale]);
            handler->PSendSysMessage(Say(player, T_ROW, "  {} ({}): {}"), RaceName(player, race), race, names);
        }
        handler->SendSysMessage(Say(player, T_HELP, ".heritage choose <race> | .heritage reset | .heritage bonus"));
        return true;
    }

    static bool HandleChoose(ChatHandler* handler, Tail race)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        uint8 id = FindRace(player, std::string(race));
        if (!id)
        {
            handler->SendSysMessage(Say(player, T_UNKNOWN, "That heritage isn't available to you."));
            return true;
        }
        Choose(player, id);
        return true;
    }

    static bool HandleBonus(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        std::vector<uint32> const& chosen = bonusChosen[player->GetGUID().GetCounter()];
        handler->PSendSysMessage(Say(player, T_BONUS_HEAD, "Second racials (carry up to {}, {} gold each; .heritage take|drop <name>):"),
            settings.bonusCount, settings.bonusCost / GOLD);
        LocaleConstant locale = handler->GetSessionDbcLocale();
        for (Bonus const& b : bonusPool)
            if (SpellInfo const* info = sSpellMgr->GetSpellInfo(b.spell))
                handler->PSendSysMessage(Say(player, T_BONUS_ROW, "  {}{} ({})"),
                    std::find(chosen.begin(), chosen.end(), b.spell) != chosen.end() ? "> " : "", info->SpellName[locale], b.spell);
        return true;
    }

    static bool TakeOrDrop(ChatHandler* handler, std::string const& text, bool take)
    {
        Player* player = handler->GetPlayer();
        if (!player)
            return false;
        uint32 const spell = FindBonus(text);
        if (!spell)
            handler->SendSysMessage(Say(player, T_BONUS_UNKNOWN, "That second racial isn't available."));
        else
            ChooseBonus(player, spell, take);
        return true;
    }
    static bool HandleTake(ChatHandler* handler, Tail text) { return TakeOrDrop(handler, std::string(text), true); }
    static bool HandleDrop(ChatHandler* handler, Tail text) { return TakeOrDrop(handler, std::string(text), false); }

    static bool HandleReset(ChatHandler* handler)
    {
        if (Player* player = handler->GetPlayer())
            Choose(player, player->getRace(true));
        return true;
    }
};

void AddTemucraftHeritageScripts()
{
    new TemucraftHeritageWorld();
    new TemucraftHeritagePlayer();
    new TemucraftHeritageCommands();
}
