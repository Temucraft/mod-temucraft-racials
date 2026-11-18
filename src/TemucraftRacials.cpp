/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

// Racials at their best: the strongest version each racial had over the years, used more often.
//
// - Cooldowns: every racial (Rank "Racial") with a cooldown is shortened by CooldownCut %, never above MaxCooldown nor
//   below MinCooldown seconds (Berserking 3 min -> 2, War Stomp 2 min -> 90 sec...); per-spell overrides. The server and
//   the client's cooldown are both corrected right after the cast.
// - Stoneform also removes curses and magic and cuts damage taken by 10% while it lasts.
// - Gift of the Naaru heals at least 20% of the target's maximum health.
// - Hardiness and Da Voodoo Shuffle: 20% shorter stuns / movement impairing effects (from 15%).
// - The Human Spirit: 10% more Spirit (from 3%). Diplomacy: also a vendor discount with factions you're Friendly
//   with, and a bigger one at Exalted (25%).
// - Expansive Mind also gives 5% more rage, energy and runic power.
// Each part has its own switch.

#include "Config.h"
#include "Creature.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace
{
    constexpr uint32 SPELL_STONEFORM = 20594;
    constexpr uint32 SPELL_STONEFORM_ARMOR = 65116;
    constexpr uint32 SPELL_HARDINESS = 20573;
    constexpr uint32 SPELL_DA_VOODOO = 58943;
    constexpr uint32 SPELL_HUMAN_SPIRIT = 20598;
    constexpr uint32 SPELL_DIPLOMACY = 20599;
    constexpr uint32 SPELL_EXPANSIVE_MIND = 20591;
    std::unordered_set<uint32> const GIFT_OF_THE_NAARU = { 28880, 59542, 59543, 59544, 59545, 59547, 59548, 80869, 80870, 80871, 121093 };

    struct Settings
    {
        bool enable = true;
        bool cooldowns = true;
        float cut = 25.0f;
        uint32 maxCooldown = 120;
        uint32 minCooldown = 45;
        std::unordered_map<uint32, uint32> overrides;   // spell -> seconds
        bool stoneform = true;
        bool gift = true;
        float giftPct = 20.0f;
        bool resistance = true;
        int32 resistancePct = 20;
        bool humanSpirit = true;
        int32 humanSpiritPct = 10;
        bool diplomacy = true;
        float diplomacyPct = 5.0f;
        float diplomacyExaltedPct = 25.0f;
        bool expansiveMind = true;
        float expansiveMindPct = 5.0f;
    } settings;

    bool IsRacial(SpellInfo const* info)
    {
        char const* rank = info->Rank[LOCALE_enUS];
        return rank && (!std::strcmp(rank, "Racial") || !std::strcmp(rank, "Racial Passive"));
    }

    void LoadSettings()
    {
        settings.enable = sConfigMgr->GetOption<bool>("Temucraft.Racials.Enable", true);
        settings.cooldowns = sConfigMgr->GetOption<bool>("Temucraft.Racials.Cooldowns", true);
        settings.cut = std::clamp(sConfigMgr->GetOption<float>("Temucraft.Racials.CooldownCut", 25.0f), 0.0f, 90.0f);
        settings.maxCooldown = sConfigMgr->GetOption<uint32>("Temucraft.Racials.MaxCooldown", 120);
        settings.minCooldown = sConfigMgr->GetOption<uint32>("Temucraft.Racials.MinCooldown", 45);
        settings.overrides.clear();
        std::string list = sConfigMgr->GetOption<std::string>("Temucraft.Racials.CooldownOverrides", "58984:60 20589:60");
        std::replace(list.begin(), list.end(), ',', ' ');
        std::istringstream in(list);
        std::string pair;
        while (in >> pair)
            if (std::size_t colon = pair.find(':'); colon != std::string::npos)
                settings.overrides[uint32(std::strtoul(pair.substr(0, colon).c_str(), nullptr, 10))] =
                    uint32(std::strtoul(pair.substr(colon + 1).c_str(), nullptr, 10));
        settings.stoneform = sConfigMgr->GetOption<bool>("Temucraft.Racials.Stoneform", true);
        settings.gift = sConfigMgr->GetOption<bool>("Temucraft.Racials.GiftOfTheNaaru", true);
        settings.giftPct = sConfigMgr->GetOption<float>("Temucraft.Racials.GiftOfTheNaaru.Percent", 20.0f);
        settings.resistance = sConfigMgr->GetOption<bool>("Temucraft.Racials.Hardiness", true);
        settings.resistancePct = sConfigMgr->GetOption<int32>("Temucraft.Racials.Hardiness.Percent", 20);
        settings.humanSpirit = sConfigMgr->GetOption<bool>("Temucraft.Racials.HumanSpirit", true);
        settings.humanSpiritPct = sConfigMgr->GetOption<int32>("Temucraft.Racials.HumanSpirit.Percent", 10);
        settings.diplomacy = sConfigMgr->GetOption<bool>("Temucraft.Racials.Diplomacy", true);
        settings.diplomacyPct = sConfigMgr->GetOption<float>("Temucraft.Racials.Diplomacy.Discount", 5.0f);
        settings.diplomacyExaltedPct = sConfigMgr->GetOption<float>("Temucraft.Racials.Diplomacy.ExaltedDiscount", 25.0f);
        settings.expansiveMind = sConfigMgr->GetOption<bool>("Temucraft.Racials.ExpansiveMind", true);
        settings.expansiveMindPct = sConfigMgr->GetOption<float>("Temucraft.Racials.ExpansiveMind.Percent", 5.0f);
    }

    uint32 WantedCooldown(SpellInfo const* info, uint32 stock)
    {
        if (auto itr = settings.overrides.find(info->Id); itr != settings.overrides.end())
            return itr->second * IN_MILLISECONDS;
        uint32 wanted = uint32(stock * (100.0f - settings.cut) / 100.0f);
        wanted = std::min(wanted, settings.maxCooldown * IN_MILLISECONDS);
        wanted = std::max(wanted, std::min(stock, settings.minCooldown * IN_MILLISECONDS));
        return wanted;
    }

    // spell data: passive racial values (a restart applies a changed setting)
    void TuneSpell(SpellInfo* spell)
    {
        if (!settings.enable)
            return;
        auto setFirst = [spell](int32 value)
        {
            for (SpellEffectInfo& effect : spell->_GetEffects())
                if (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.BasePoints)
                {
                    effect.BasePoints = (effect.BasePoints < 0 ? -value : value) - 1;   // stored one below the value
                    return;
                }
        };
        if (settings.resistance && (spell->Id == SPELL_HARDINESS || spell->Id == SPELL_DA_VOODOO))
            setFirst(settings.resistancePct);
        else if (settings.humanSpirit && spell->Id == SPELL_HUMAN_SPIRIT)
            setFirst(settings.humanSpiritPct);
    }
}

class TemucraftRacialsWorld : public WorldScript
{
public:
    TemucraftRacialsWorld() : WorldScript("TemucraftRacialsWorld", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }
    void OnAfterConfigLoad(bool /*reload*/) override { LoadSettings(); }
};

class TemucraftRacialsSpells : public GlobalScript
{
public:
    TemucraftRacialsSpells() : GlobalScript("TemucraftRacialsSpells", { GLOBALHOOK_ON_LOAD_SPELL_CUSTOM_ATTR }) { }
    void OnLoadSpellCustomAttr(SpellInfo* spell) override { TuneSpell(spell); }
};

class TemucraftRacialsUnit : public UnitScript
{
public:
    TemucraftRacialsUnit() : UnitScript("TemucraftRacialsUnit", true,
        { UNITHOOK_MODIFY_MELEE_DAMAGE, UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK }) { }

    static bool Stone(Unit* target)
    {
        return settings.enable && settings.stoneform && target && target->IsPlayer() && target->HasAura(SPELL_STONEFORM_ARMOR);
    }

    void ModifyMeleeDamage(Unit* target, Unit* /*attacker*/, uint32& damage) override
    {
        if (Stone(target))
            damage = uint32(damage * 0.9f);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* /*attacker*/, int32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (damage > 0 && Stone(target))
            damage = int32(damage * 0.9f);
    }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* /*attacker*/, uint32& damage, SpellInfo const* /*spellInfo*/) override
    {
        if (Stone(target))
            damage = uint32(damage * 0.9f);
    }
};

class TemucraftRacialsPlayer : public PlayerScript
{
public:
    TemucraftRacialsPlayer() : PlayerScript("TemucraftRacialsPlayer",
        { PLAYERHOOK_ON_SPELL_CAST, PLAYERHOOK_ON_AFTER_UPDATE_MAX_POWER, PLAYERHOOK_ON_GET_REPUTATION_PRICE_DISCOUNT }) { }

    void OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/) override
    {
        if (!settings.enable || !spell || spell->IsTriggered() || spell->m_CastItem)
            return;
        SpellInfo const* info = spell->GetSpellInfo();
        if (!info || !IsRacial(info))
            return;

        if (settings.cooldowns)
        {
            uint32 const stock = info->RecoveryTime ? info->RecoveryTime : info->CategoryRecoveryTime;
            if (stock)
            {
                int32 const delta = int32(WantedCooldown(info, stock)) - int32(stock);
                uint32 const id = info->Id;
                if (delta)
                    player->m_Events.AddEventAtOffset([player, id, delta]()
                    {
                        if (player->HasSpellCooldown(id))
                            player->ModifySpellCooldown(id, delta);
                    }, 300ms);
            }
        }

        // Stoneform: curses and magic go too
        if (settings.stoneform && info->Id == SPELL_STONEFORM)
            player->m_Events.AddEventAtOffset([player]()
            {
                // harmful curses and magic only: the player's own magic buffs stay
                Unit::AuraApplicationMap& auras = player->GetAppliedAuras();
                for (auto itr = auras.begin(); itr != auras.end();)
                {
                    AuraApplication const* app = itr->second;
                    SpellInfo const* aInfo = app->GetBase()->GetSpellInfo();
                    if (!app->IsPositive() && !aInfo->IsPassive() && (aInfo->Dispel == DISPEL_CURSE || aInfo->Dispel == DISPEL_MAGIC))
                        player->RemoveAura(itr, AURA_REMOVE_BY_ENEMY_SPELL);
                    else
                        ++itr;
                }
            }, 100ms);

        // Gift of the Naaru: at least GiftPct of the target's maximum health over its duration
        if (settings.gift && GIFT_OF_THE_NAARU.count(info->Id))
        {
            Unit* target = spell->m_targets.GetUnitTarget();
            ObjectGuid const targetGuid = target ? target->GetGUID() : player->GetGUID();
            uint32 const id = info->Id;
            player->m_Events.AddEventAtOffset([player, targetGuid, id]()
            {
                Unit* t = targetGuid == player->GetGUID() ? player : ObjectAccessor::GetUnit(*player, targetGuid);
                if (!t)
                    return;
                Aura* aura = t->GetAura(id, player->GetGUID());
                if (!aura)
                    return;
                for (uint8 i = 0; i < MAX_SPELL_EFFECTS; ++i)
                    if (AuraEffect* eff = aura->GetEffect(i))
                        if (eff->GetAuraType() == SPELL_AURA_PERIODIC_HEAL && eff->GetAmplitude() > 0)
                        {
                            int32 const ticks = std::max(1, aura->GetMaxDuration() / eff->GetAmplitude());
                            int32 const perTick = int32(t->GetMaxHealth() * settings.giftPct / 100.0f / ticks);
                            if (perTick > eff->GetAmount())
                                eff->ChangeAmount(perTick);
                        }
            }, 100ms);
        }
    }

    // Expansive Mind: more rage, energy and runic power too
    void OnPlayerAfterUpdateMaxPower(Player* player, Powers& power, float& value) override
    {
        if (settings.enable && settings.expansiveMind && (power == POWER_RAGE || power == POWER_ENERGY || power == POWER_RUNIC_POWER) &&
            player->HasSpell(SPELL_EXPANSIVE_MIND))
            value *= 1.0f + settings.expansiveMindPct / 100.0f;
    }

    // Diplomacy: a discount with factions you're Friendly with or better
    void Discount(Player const* player, FactionTemplateEntry const* faction, float& discount)
    {
        if (!settings.enable || !settings.diplomacy || !faction || !player->HasSpell(SPELL_DIPLOMACY))
            return;
        ReputationRank const rank = player->GetReputationRank(faction->faction);
        if (rank >= REP_EXALTED)
            discount *= 1.0f - settings.diplomacyExaltedPct / 100.0f;
        else if (rank >= REP_FRIENDLY)
            discount *= 1.0f - settings.diplomacyPct / 100.0f;
    }

    // (the creature version of this hook runs after the faction one for the same purchase: only the faction one counts)
    void OnPlayerGetReputationPriceDiscount(Player const* player, FactionTemplateEntry const* faction, float& discount) override
    {
        Discount(player, faction, discount);
    }
};

void AddTemucraftRacialsScripts()
{
    new TemucraftRacialsWorld();
    new TemucraftRacialsSpells();
    new TemucraftRacialsUnit();
    new TemucraftRacialsPlayer();
}
