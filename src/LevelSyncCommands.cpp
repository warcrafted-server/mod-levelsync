#include "LevelSync.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "CharacterCache.h"
#include "DBCEnums.h"
#include "InstanceSaveMgr.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include <openssl/sha.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>

using namespace Acore::ChatCommands;

enum LevelSyncCommandStrings
{
    LANG_LS_NO_MEMBERS              = 100010,
    LANG_LS_ACCOUNT_CHARS           = 100011,
    LANG_LS_MEMBER_IP_LINE          = 100012,
    LANG_LS_SYNC_GROUP              = 100013,
    LANG_LS_ACCOUNTS                = 100014,
    LANG_LS_TOTAL_CHARS             = 100015,
    LANG_LS_LEVEL_STATUS            = 100016,
    LANG_LS_PROG_STATUS             = 100017,
    LANG_LS_GROUP_MEMBERS           = 100018,
    LANG_LS_ADDON_LINK              = 100019,
    LANG_LS_MODULE_DISABLED         = 100020,
    LANG_LS_USAGE_SETKEY            = 100021,
    LANG_LS_ACCOUNT_KEY_SET         = 100022,
    LANG_LS_USAGE_ADDACCOUNT        = 100023,
    LANG_LS_ACCOUNT_NOT_FOUND       = 100024,
    LANG_LS_KEY_REQUIRED            = 100025,
    LANG_LS_INVALID_KEY             = 100026,
    LANG_LS_ACCOUNT_DIFFERENT_GROUP = 100027,
    LANG_LS_FAILED_CREATE           = 100028,
    LANG_LS_GROUP_FULL              = 100029,
    LANG_LS_NO_CHARS_ACCOUNT        = 100030,
    LANG_LS_SKIPPED_OTHER_GROUP     = 100031,
    LANG_LS_LINKED_CHARS            = 100032,
    LANG_LS_LEVEL_OFF               = 100033,
    LANG_LS_IP_OFF                  = 100034,
    LANG_LS_NO_NEW_CHARS            = 100035,
    LANG_LS_USAGE_ADDCHAR           = 100036,
    LANG_LS_CHAR_NOT_FOUND          = 100037,
    LANG_LS_KEY_REQUIRED_CHAR       = 100038,
    LANG_LS_CHAR_DIFFERENT_GROUP    = 100039,
    LANG_LS_CHAR_IN_YOUR_GROUP      = 100040,
    LANG_LS_ADDED_TO_GROUP          = 100041,
    LANG_LS_USAGE_REMOVEACCOUNT     = 100042,
    LANG_LS_NOT_IN_GROUP            = 100043,
    LANG_LS_USAGE_REMOVEACCOUNT_ID  = 100044,
    LANG_LS_INVALID_ACCOUNT_ID      = 100045,
    LANG_LS_ACCOUNT_NOT_IN_GROUP    = 100046,
    LANG_LS_YOUR_REMOVED            = 100047,
    LANG_LS_ACCOUNT_REMOVED         = 100048,
    LANG_LS_USAGE_REMOVECHAR        = 100049,
    LANG_LS_CHAR_NOT_IN_GROUP       = 100050,
    LANG_LS_REMOVED_SYNC_GROUP      = 100051,
    LANG_LS_YOUR_DISBANDED          = 100052,
    LANG_LS_SYNC_GROUP_DISBANDED    = 100053,
    LANG_LS_NO_GROUPS_FOUND         = 100054,
    LANG_LS_DISBANDED_GROUPS        = 100055,
    LANG_LS_USAGE_LISTACCOUNT       = 100056,
    LANG_LS_KEY_REQUIRED_VIEW       = 100057,
    LANG_LS_NO_CHARS_ACCOUNT_LIST   = 100058,
    LANG_LS_CHARS_ON_ACCOUNT        = 100059,
    LANG_LS_CHAR_LINE               = 100060,
    LANG_LS_LEVEL_DISABLED_SRV      = 100061,
    LANG_LS_USAGE_LEVEL             = 100062,
    LANG_LS_LEVEL_FIRE_ONLY         = 100063,
    LANG_LS_MUST_WAIT               = 100064,
    LANG_LS_SYNCING_LEVEL           = 100065,
    LANG_LS_LEVEL_FIRED             = 100066,
    LANG_LS_LEVEL_SYNC_LEGACY       = 100067,
    LANG_LS_PROG_DISABLED_SRV       = 100068,
    LANG_LS_USAGE_IP                = 100069,
    LANG_LS_IP_FIRE_ONLY            = 100070,
    LANG_LS_SYNCING_PROG            = 100071,
    LANG_LS_IP_FIRED                = 100072,
    LANG_LS_PROG_SYNC_LEGACY        = 100073,
    LANG_LS_MONEY_DISABLED_SRV      = 100074,
    LANG_LS_NO_OTHER_MEMBERS        = 100075,
    LANG_LS_NO_GOLD                 = 100076,
    LANG_LS_CAP_EXCEEDED            = 100077,
    LANG_LS_POOLED_FROM             = 100078,
    LANG_LS_UNBIND_DISABLED_SRV     = 100079,
    LANG_LS_NOT_FOUND_OFFLINE       = 100080,
    LANG_LS_UNBOUND_INSTANCES       = 100081,
    LANG_LS_UNBOUND_ON              = 100082,
    LANG_LS_USAGE_GM_REMOVEALL      = 100083,
    LANG_LS_KEY_REMOVED             = 100084,
    LANG_LS_DISBANDED_BY_GM         = 100085,
    LANG_LS_GROUP_DISBANDED         = 100086,
    LANG_LS_USAGE_GM_XP             = 100087,
    LANG_LS_XP_POSITIVE             = 100088,
    LANG_LS_NO_TARGET               = 100089,
    LANG_LS_GRANTED_XP              = 100090,
};


// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

// Escape a string for safe interpolation into a CharacterDatabase query.
// Defends against names/inputs containing apostrophes or other SQL special
// characters reaching the query layer (which would otherwise crash with a
// syntax error). Stock AC name validators block such inputs at character
// creation, but custom servers, GM operations, and external imports can
// still produce them.
static std::string EscChar(std::string s)
{
    CharacterDatabase.EscapeString(s);
    return s;
}

static std::string EscLogin(std::string s)
{
    LoginDatabase.EscapeString(s);
    return s;
}

static std::string CapFirst(std::string s)
{
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

// Wipes every bound instance the target has across all 4 difficulty slots
// (dungeons + raids), preserving the binding for the map the target is
// currently inside. Returns the number of bindings cleared. Used by both
// the player-facing `.levelsync unbindall` and the GM `.levelsync gm
// unbindall` commands.
static uint32 UnbindAllInstancesOn(Player* target)
{
    uint32 counter = 0;
    for (uint8 i = 0; i < MAX_DIFFICULTY; ++i)
    {
        BoundInstancesMap const& binds = sInstanceSaveMgr->PlayerGetBoundInstances(target->GetGUID(), Difficulty(i));
        for (BoundInstancesMap::const_iterator itr = binds.begin(); itr != binds.end();)
        {
            if (itr->first != target->GetMapId())
            {
                sInstanceSaveMgr->PlayerUnbindInstance(target->GetGUID(), itr->first, Difficulty(i), true, target);
                // PlayerUnbindInstance mutates the map being iterated;
                // restart the inner loop from the beginning.
                itr = binds.begin();
                ++counter;
            }
            else
                ++itr;
        }
    }
    return counter;
}

static std::string FormatMoneyDisplay(uint32 copper)
{
    uint32 g = copper / 10000;
    uint32 s = (copper % 10000) / 100;
    uint32 c = copper % 100;
    std::string out;
    if (g > 0)
        out += std::to_string(g) + "g";
    if (s > 0)
    {
        if (!out.empty()) out += " ";
        out += std::to_string(s) + "s";
    }
    if (c > 0 || out.empty())
    {
        if (!out.empty()) out += " ";
        out += std::to_string(c) + "c";
    }
    return out;
}

static uint32 GetAccountIdByName(const std::string& name)
{
    QueryResult r = LoginDatabase.Query(
        "SELECT id FROM account WHERE username = '{}'", EscLogin(name));
    if (!r)
        return 0;
    return r->Fetch()[0].Get<uint32>();
}

static std::string HashKey(const std::string& key)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(key.c_str()), key.size(), hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

static bool VerifyAccountKey(uint32 accountId, const std::string& key)
{
    std::string hashed = HashKey(key);
    QueryResult r = CharacterDatabase.Query(
        "SELECT 1 FROM levelsync_account_keys WHERE account_id = {} AND security_key = '{}'",
        accountId, hashed);
    return r != nullptr;
}

static const char* ClassName(uint8 cls)
{
    switch (cls)
    {
        case 1:  return "Warrior";
        case 2:  return "Paladin";
        case 3:  return "Hunter";
        case 4:  return "Rogue";
        case 5:  return "Priest";
        case 6:  return "Death Knight";
        case 7:  return "Shaman";
        case 8:  return "Mage";
        case 9:  return "Warlock";
        case 11: return "Druid";
        default: return "Unknown";
    }
}

static const char* ClassColor(uint8 cls)
{
    switch (cls)
    {
        case 1:  return "C79C6E";
        case 2:  return "F58CBA";
        case 3:  return "ABD473";
        case 4:  return "FFF569";
        case 5:  return "FFFFFF";
        case 6:  return "C41F3B";
        case 7:  return "0070DE";
        case 8:  return "69CCF0";
        case 9:  return "9482C9";
        case 11: return "FF7D0A";
        default: return "FFFFFF";
    }
}

static const char* IPTierName(uint8 tier)
{
    switch (tier)
    {
        case 0:  return "Molten Core / Onyxia";
        case 1:  return "Molten Core / Onyxia";
        case 2:  return "Blackwing Lair";
        case 3:  return "Pre-AQ";
        case 4:  return "AQ War Effort";
        case 5:  return "Ahn'Qiraj";
        case 6:  return "Naxxramas 40";
        case 7:  return "Pre-TBC";
        case 8:  return "Karazhan / Gruul's Lair / Magtheridon's Lair";
        case 9:  return "Serpentshrine Cavern / Tempest Keep";
        case 10: return "Hyjal Summit / Black Temple";
        case 11: return "(Skipped)";
        case 12: return "Sunwell Plateau";
        case 13: return "Naxxramas / Eye of Eternity / Obsidian Sanctum";
        case 14: return "Ulduar";
        case 15: return "Trial of the Crusader";
        case 16: return "Icecrown Citadel";
        case 17: return "Ruby Sanctum";
        case 18: return "Tiers Complete";
        default: return "Unknown (IP may not be enabled)";
    }
}

static const char* IPTierColor(uint8 tier)
{
    switch (tier)
    {
        case 0:  return "808080";
        case 1:  return "de8c33";
        case 2:  return "de8c33";
        case 3:  return "d94040";
        case 4:  return "4da64d";
        case 5:  return "4d80d9";
        case 6:  return "a64dd9";
        case 7:  return "33b8b8";
        case 8:  return "c0b840";
        case 9:  return "cc4780";
        case 10: return "8c8cb3";
        case 11: return "ff8026";
        case 12: return "38cc8c";
        case 13: return "4d80ff";
        case 14: return "8cc74d";
        case 15: return "b380f2";
        case 16: return "38bfb3";
        case 17: return "e6334d";
        case 18: return "59cc59";
        default: return "808080";
    }
}

// Displays all members of a group grouped by account, with class color, class name, and IP tier.
static void DisplayGroupMembers(ChatHandler* handler, uint32 groupId)
{
    QueryResult r = CharacterDatabase.Query(
        "SELECT c.name, c.level, c.`class`, m.account_id, c.guid "
        "FROM levelsync_members m "
        "JOIN characters c ON m.char_guid = c.guid "
        "WHERE m.group_id = {} "
        "ORDER BY m.account_id ASC, c.level DESC",
        groupId);

    if (!r)
    {
        handler->PSendSysMessage(LANG_LS_NO_MEMBERS);
        return;
    }

    uint32 currentAccount = 0;
    do
    {
        std::string name  = r->Fetch()[0].Get<std::string>();
        uint8  level      = r->Fetch()[1].Get<uint8>();
        uint8  cls        = r->Fetch()[2].Get<uint8>();
        uint32 accountId  = r->Fetch()[3].Get<uint32>();
        uint32 guid       = r->Fetch()[4].Get<uint32>();

        if (accountId != currentAccount)
        {
            QueryResult acctCount = CharacterDatabase.Query(
                "SELECT COUNT(*) FROM levelsync_members WHERE group_id = {} AND account_id = {}",
                groupId, accountId);
            uint64 charCount = acctCount ? acctCount->Fetch()[0].Get<uint64>() : 0;
            handler->PSendSysMessage(LANG_LS_ACCOUNT_CHARS, accountId, charCount);
            currentAccount = accountId;
        }

        // Prefer the in-memory Player state when the character is online or
        // bot-loaded. The DB row only persists on the next save tick or
        // logout, so reading c.level / character_queststatus_rewarded
        // directly can lag behind reality (mod-levelsync's own sync writes
        // go through GiveLevel / RewardQuest, which update memory first).
        uint8 displayLevel = level;
        uint8 displayTier  = sLevelSync->GetCharacterIPTierPublic(guid);

        if (Player* p = ObjectAccessor::FindPlayerByLowGUID(guid))
        {
            displayLevel = p->GetLevel();
            displayTier  = 0;
            for (uint8 i = 1; i <= LEVELSYNC_IP_MAX_TIER; ++i)
            {
                if (p->GetQuestStatus(LEVELSYNC_IP_QUEST_BASE + i) == QUEST_STATUS_REWARDED)
                    displayTier = i;
            }
        }

        handler->PSendSysMessage(LANG_LS_MEMBER_IP_LINE,
            ClassColor(cls), CapFirst(name), static_cast<uint32>(displayLevel),
            ClassName(cls),
            IPTierColor(displayTier), static_cast<uint32>(displayTier), IPTierName(displayTier));
    } while (r->NextRow());
}

static void DisplayGroupStatus(ChatHandler* handler, uint32 groupId)
{
    uint32 accounts  = sLevelSync->GetGroupAccountCount(groupId);

    // Fire-only model: sync only runs when the player invokes
    //   .levelsync level on   or   .levelsync IP on
    // Status reflects whether the server conf permits the sync, NOT a
    // persistent per-group toggle. "Ready" = conf-enabled and the
    // command will fire when invoked; "Off" = conf-disabled.
    bool levelReady = sLevelSync->IsLevelSyncAllowed();
    bool progReady  = sLevelSync->IsProgressionAllowed();

    // OLD (per-group persistent toggle, kept for reference):
    // bool levelSync = sLevelSync->IsGroupLevelSyncEnabled(groupId);
    // bool progSync  = sLevelSync->IsGroupProgressionSyncEnabled(groupId);

    QueryResult countResult = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM levelsync_members WHERE group_id = {}", groupId);
    uint64 totalChars = countResult ? countResult->Fetch()[0].Get<uint64>() : 0;

    handler->PSendSysMessage(LANG_LS_SYNC_GROUP, groupId);
    handler->PSendSysMessage(LANG_LS_ACCOUNTS, accounts, sLevelSync->GetMaxLinkedAccounts());
    handler->PSendSysMessage(LANG_LS_TOTAL_CHARS, totalChars);
    handler->PSendSysMessage(LANG_LS_LEVEL_STATUS, levelReady ? "|cff00ff00Available|r" : "|cffff0000Disabled|r");
    handler->PSendSysMessage(LANG_LS_PROG_STATUS, progReady ? "|cff00ff00Available|r" : "|cffff0000Disabled|r");
    handler->PSendSysMessage(LANG_LS_GROUP_MEMBERS);
    DisplayGroupMembers(handler, groupId);
    handler->PSendSysMessage(LANG_LS_ADDON_LINK);
}

// Creates a new group and returns its group_id.
//
// founderGuid is written into the row so the SELECT-after-INSERT can find it
// without relying on LAST_INSERT_ID(), which is per-connection and unsafe
// under AzerothCore's round-robin connection pool. The SELECT picks the most
// recent group_id for this founder so a player who has left a previous group
// gets a fresh one rather than silently rejoining the old one.
static uint32 CreateGroup(uint32 founderGuid)
{
    CharacterDatabase.DirectExecute(
        "INSERT INTO levelsync_groups (founder_guid, level_sync_enabled, sync_progression) VALUES ({}, 0, 0)",
        founderGuid);

    QueryResult r = CharacterDatabase.Query(
        "SELECT group_id FROM levelsync_groups WHERE founder_guid = {} ORDER BY group_id DESC LIMIT 1",
        founderGuid);
    if (!r)
        return 0;
    return r->Fetch()[0].Get<uint32>();
}

// Returns the group_id the character is in, or creates+joins a new one.
static uint32 GetOrCreateGroup(uint32 charGuid, uint32 accountId)
{
    uint32 groupId = sLevelSync->GetGroupId(charGuid);
    if (groupId)
        return groupId;

    groupId = CreateGroup(charGuid);
    if (!groupId)
        return 0;

    CharacterDatabase.Execute(
        "INSERT IGNORE INTO levelsync_members (group_id, char_guid, account_id) VALUES ({}, {}, {})",
        groupId, charGuid, accountId);

    return groupId;
}

// -----------------------------------------------------------------------
// Command class
// -----------------------------------------------------------------------

class LevelSyncCommandScript : public CommandScript
{
public:
    LevelSyncCommandScript() : CommandScript("LevelSyncCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable gmTable =
        {
            { "removeall",  HandleGmRemoveAllCommand,  SEC_GAMEMASTER, Console::No },
            { "xp",         HandleGmXpCommand,         SEC_GAMEMASTER, Console::No },
            { "unbindall",  HandleGmUnbindAllCommand,  SEC_GAMEMASTER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "setkey",         HandleSetKeyCommand,         SEC_PLAYER,     Console::No },
            { "addaccount",     HandleAddAccountCommand,     SEC_PLAYER,     Console::No },
            { "addchar",        HandleAddCharCommand,        SEC_PLAYER,     Console::No },
            { "removeaccount",  HandleRemoveAccountCommand,  SEC_PLAYER,     Console::No },
            { "removechar",     HandleRemoveCharCommand,     SEC_PLAYER,     Console::No },
            { "removeall",      HandleRemoveAllCommand,      SEC_PLAYER,     Console::No },
            { "disbandaccount", HandleDisbandAccountCommand, SEC_PLAYER,     Console::No },
            { "listaccount",    HandleListAccountCommand,    SEC_PLAYER,     Console::No },
            { "status",         HandleStatusCommand,         SEC_PLAYER,     Console::No },
            { "level",          HandleLevelCommand,          SEC_PLAYER,     Console::No },
            { "IP",               HandleIndivProgressionCommand, SEC_PLAYER, Console::No },
            { "money",          HandleMoneyCommand,          SEC_PLAYER,     Console::No },
            { "unbindall",      HandleUnbindAllCommand,      SEC_PLAYER,     Console::No },
            { "gm",             gmTable },
        };

        static ChatCommandTable rootTable =
        {
            { "levelsync", commandTable },
        };

        return rootTable;
    }

    // -------------------------------------------------------------------
    // .levelsync setkey <key>
    // -------------------------------------------------------------------
    static bool HandleSetKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* keyArg = strtok(const_cast<char*>(args), " ");
        if (!keyArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_SETKEY);
            return true;
        }

        uint32 accountId = handler->GetSession()->GetAccountId();
        std::string hashed = HashKey(std::string(keyArg));

        CharacterDatabase.Execute(
            "INSERT INTO levelsync_account_keys (account_id, security_key) VALUES ({}, '{}') "
            "ON DUPLICATE KEY UPDATE security_key = '{}'",
            accountId, hashed, hashed);

        handler->PSendSysMessage(LANG_LS_ACCOUNT_KEY_SET);
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync addaccount <account> [key]
    // -------------------------------------------------------------------
    static bool HandleAddAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* accountArg = strtok(const_cast<char*>(args), " ");
        char* keyArg     = strtok(nullptr, " ");

        if (!accountArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_ADDACCOUNT);
            return true;
        }

        Player* player        = handler->GetSession()->GetPlayer();
        uint32  myGuid        = player->GetGUID().GetCounter();
        uint32  myAccountId   = player->GetSession()->GetAccountId();

        uint32 targetAccountId = GetAccountIdByName(std::string(accountArg));
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_LS_ACCOUNT_NOT_FOUND, accountArg);
            return true;
        }

        bool ownAccount = (targetAccountId == myAccountId);

        if (!ownAccount)
        {
            if (!keyArg)
            {
                handler->PSendSysMessage(LANG_LS_KEY_REQUIRED);
                return true;
            }

            if (!VerifyAccountKey(targetAccountId, std::string(keyArg)))
            {
                handler->PSendSysMessage(LANG_LS_INVALID_KEY);
                return true;
            }

            // Block if any char on target account is in a DIFFERENT group
            QueryResult conflict = CharacterDatabase.Query(
                "SELECT m.group_id FROM levelsync_members m "
                "JOIN characters c ON m.char_guid = c.guid "
                "WHERE c.account = {} LIMIT 1",
                targetAccountId);

            if (conflict)
            {
                uint32 theirGroup = conflict->Fetch()[0].Get<uint32>();
                uint32 myGroup    = sLevelSync->GetGroupId(myGuid);

                if (theirGroup != myGroup)
                {
                    handler->PSendSysMessage(LANG_LS_ACCOUNT_DIFFERENT_GROUP);
                    return true;
                }
            }
        }

        // Get or create this player's group
        uint32 groupId = GetOrCreateGroup(myGuid, myAccountId);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_FAILED_CREATE);
            return true;
        }

        // Slot check — only if target account isn't already represented in this group
        QueryResult acctInGroup = CharacterDatabase.Query(
            "SELECT 1 FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE m.group_id = {} AND c.account = {} LIMIT 1",
            groupId, targetAccountId);

        if (!acctInGroup)
        {
            if (sLevelSync->GetGroupAccountCount(groupId) >= sLevelSync->GetMaxLinkedAccounts())
            {
                handler->PSendSysMessage(LANG_LS_GROUP_FULL,
                    sLevelSync->GetGroupAccountCount(groupId),
                    sLevelSync->GetMaxLinkedAccounts());
                return true;
            }
        }

        QueryResult chars = CharacterDatabase.Query(
            "SELECT guid, name FROM characters WHERE account = {}", targetAccountId);

        if (!chars)
        {
            handler->PSendSysMessage(LANG_LS_NO_CHARS_ACCOUNT, accountArg);
            return true;
        }

        // Insert all chars from target account
        uint32 linked = 0;
        do
        {
            uint32 charGuid      = chars->Fetch()[0].Get<uint32>();
            std::string charName = chars->Fetch()[1].Get<std::string>();

            // Skip if already in this group
            QueryResult alreadyHere = CharacterDatabase.Query(
                "SELECT 1 FROM levelsync_members WHERE char_guid = {} AND group_id = {}",
                charGuid, groupId);
            if (alreadyHere)
                continue;

            // Skip if in a different group
            QueryResult otherGroup = CharacterDatabase.Query(
                "SELECT group_id FROM levelsync_members WHERE char_guid = {}", charGuid);
            if (otherGroup)
            {
                handler->PSendSysMessage(LANG_LS_SKIPPED_OTHER_GROUP, charName);
                continue;
            }

            CharacterDatabase.DirectExecute(
                "INSERT IGNORE INTO levelsync_members (group_id, char_guid, account_id) VALUES ({}, {}, {})",
                groupId, charGuid, targetAccountId);
            ++linked;
        } while (chars->NextRow());

        if (linked > 0)
        {
            bool levelWasOn = sLevelSync->IsGroupLevelSyncEnabled(groupId);
            bool ipWasOn    = sLevelSync->IsGroupProgressionSyncEnabled(groupId);

            if (levelWasOn)
                CharacterDatabase.DirectExecute(
                    "UPDATE levelsync_groups SET level_sync_enabled = 0 WHERE group_id = {}", groupId);
            if (ipWasOn)
                CharacterDatabase.DirectExecute(
                    "UPDATE levelsync_groups SET sync_progression = 0 WHERE group_id = {}", groupId);

            DisplayGroupStatus(handler, groupId);
            handler->PSendSysMessage(LANG_LS_LINKED_CHARS, linked, accountArg);
            if (levelWasOn)
                handler->PSendSysMessage(LANG_LS_LEVEL_OFF);
            if (ipWasOn)
                handler->PSendSysMessage(LANG_LS_IP_OFF);
        }
        else
            handler->PSendSysMessage(LANG_LS_NO_NEW_CHARS, accountArg);

        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync addchar <charname> [key]
    // -------------------------------------------------------------------
    static bool HandleAddCharCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* nameArg = strtok(const_cast<char*>(args), " ");
        char* keyArg  = strtok(nullptr, " ");

        if (!nameArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_ADDCHAR);
            return true;
        }

        Player* player      = handler->GetSession()->GetPlayer();
        uint32  myGuid      = player->GetGUID().GetCounter();
        uint32  myAccountId = player->GetSession()->GetAccountId();

        std::string charName(nameArg);

        QueryResult charResult = CharacterDatabase.Query(
            "SELECT guid, account FROM characters WHERE LOWER(name) = LOWER('{}')", EscChar(charName));
        if (!charResult)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_NOT_FOUND, charName);
            return true;
        }

        uint32 targetGuid      = charResult->Fetch()[0].Get<uint32>();
        uint32 targetAccountId = charResult->Fetch()[1].Get<uint32>();
        bool   ownAccount      = (targetAccountId == myAccountId);

        if (!ownAccount)
        {
            if (!keyArg)
            {
                handler->PSendSysMessage(LANG_LS_KEY_REQUIRED_CHAR);
                return true;
            }

            if (!VerifyAccountKey(targetAccountId, std::string(keyArg)))
            {
                handler->PSendSysMessage(LANG_LS_INVALID_KEY);
                return true;
            }
        }

        // Check target not already in a different group
        uint32 targetGroup = sLevelSync->GetGroupId(targetGuid);
        uint32 myGroup     = sLevelSync->GetGroupId(myGuid);

        if (targetGroup && targetGroup != myGroup)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_DIFFERENT_GROUP, charName);
            return true;
        }

        if (targetGroup && targetGroup == myGroup)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_IN_YOUR_GROUP, charName);
            return true;
        }

        uint32 groupId = GetOrCreateGroup(myGuid, myAccountId);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_FAILED_CREATE);
            return true;
        }

        // Slot check — only if this account isn't already represented
        QueryResult acctInGroup = CharacterDatabase.Query(
            "SELECT 1 FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE m.group_id = {} AND c.account = {} LIMIT 1",
            groupId, targetAccountId);

        if (!acctInGroup)
        {
            if (sLevelSync->GetGroupAccountCount(groupId) >= sLevelSync->GetMaxLinkedAccounts())
            {
                handler->PSendSysMessage(LANG_LS_GROUP_FULL,
                    sLevelSync->GetGroupAccountCount(groupId),
                    sLevelSync->GetMaxLinkedAccounts());
                return true;
            }
        }

        CharacterDatabase.DirectExecute(
            "INSERT IGNORE INTO levelsync_members (group_id, char_guid, account_id) VALUES ({}, {}, {})",
            groupId, targetGuid, targetAccountId);

        bool levelWasOn = sLevelSync->IsGroupLevelSyncEnabled(groupId);
        bool ipWasOn    = sLevelSync->IsGroupProgressionSyncEnabled(groupId);

        if (levelWasOn)
            CharacterDatabase.DirectExecute(
                "UPDATE levelsync_groups SET level_sync_enabled = 0 WHERE group_id = {}", groupId);
        if (ipWasOn)
            CharacterDatabase.DirectExecute(
                "UPDATE levelsync_groups SET sync_progression = 0 WHERE group_id = {}", groupId);

        DisplayGroupStatus(handler, groupId);
        handler->PSendSysMessage(LANG_LS_ADDED_TO_GROUP, CapFirst(charName));
        if (levelWasOn)
            handler->PSendSysMessage(LANG_LS_LEVEL_OFF);
        if (ipWasOn)
            handler->PSendSysMessage(LANG_LS_IP_OFF);

        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync removeaccount <account>
    // .levelsync removeaccount # <accountid>
    // -------------------------------------------------------------------
    static bool HandleRemoveAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* accountArg = strtok(const_cast<char*>(args), " ");
        if (!accountArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_REMOVEACCOUNT);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        uint32 targetAccountId = 0;
        std::string displayName;

        if (std::string(accountArg) == "#")
        {
            char* idArg = strtok(nullptr, " ");
            if (!idArg)
            {
                handler->PSendSysMessage(LANG_LS_USAGE_REMOVEACCOUNT_ID);
                return true;
            }
            targetAccountId = static_cast<uint32>(std::strtoul(idArg, nullptr, 10));
            if (!targetAccountId)
            {
                handler->PSendSysMessage(LANG_LS_INVALID_ACCOUNT_ID);
                return true;
            }
            displayName = std::string(idArg);
        }
        else
        {
            targetAccountId = GetAccountIdByName(std::string(accountArg));
            if (!targetAccountId)
            {
                handler->PSendSysMessage(LANG_LS_ACCOUNT_NOT_FOUND, accountArg);
                return true;
            }
            displayName = std::string(accountArg);
        }

        // Notify online members being removed
        QueryResult toRemove = CharacterDatabase.Query(
            "SELECT m.char_guid FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE m.group_id = {} AND c.account = {}",
            groupId, targetAccountId);

        if (!toRemove)
        {
            handler->PSendSysMessage(LANG_LS_ACCOUNT_NOT_IN_GROUP, displayName);
            return true;
        }

        do
        {
            uint32 g = toRemove->Fetch()[0].Get<uint32>();
            if (Player* member = ObjectAccessor::FindPlayerByLowGUID(g))
                ChatHandler(member->GetSession()).PSendSysMessage(LANG_LS_YOUR_REMOVED);
        } while (toRemove->NextRow());

        CharacterDatabase.DirectExecute(
            "DELETE m FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE m.group_id = {} AND c.account = {}",
            groupId, targetAccountId);

        DisplayGroupStatus(handler, groupId);
        handler->PSendSysMessage(LANG_LS_ACCOUNT_REMOVED, displayName);
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync removechar <charname>
    // -------------------------------------------------------------------
    static bool HandleRemoveCharCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* nameArg = strtok(const_cast<char*>(args), " ");
        if (!nameArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_REMOVECHAR);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        std::string charName(nameArg);
        QueryResult charResult = CharacterDatabase.Query(
            "SELECT guid, account FROM characters WHERE LOWER(name) = LOWER('{}')", EscChar(charName));
        if (!charResult)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_NOT_FOUND, charName);
            return true;
        }

        uint32 targetGuid  = charResult->Fetch()[0].Get<uint32>();
        uint32 targetGroup = sLevelSync->GetGroupId(targetGuid);

        if (targetGroup != groupId)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_NOT_IN_GROUP, charName);
            return true;
        }

        // Notify if online
        if (Player* member = ObjectAccessor::FindPlayerByLowGUID(targetGuid))
            ChatHandler(member->GetSession()).PSendSysMessage(LANG_LS_YOUR_REMOVED);

        CharacterDatabase.DirectExecute(
            "DELETE FROM levelsync_members WHERE char_guid = {}", targetGuid);

        DisplayGroupStatus(handler, groupId);
        handler->PSendSysMessage(LANG_LS_REMOVED_SYNC_GROUP, CapFirst(charName));
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync removeall  — full nuke
    // -------------------------------------------------------------------
    static bool HandleRemoveAllCommand(ChatHandler* handler, char const* /*args*/)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        // Notify all online members
        for (uint32 g : sLevelSync->GetGroupMemberGuids(groupId, myGuid))
        {
            if (Player* member = ObjectAccessor::FindPlayerByLowGUID(g))
                ChatHandler(member->GetSession()).PSendSysMessage(LANG_LS_YOUR_DISBANDED);
        }

        // Wipe members then group
        CharacterDatabase.Execute(
            "DELETE FROM levelsync_members WHERE group_id = {}", groupId);

        CharacterDatabase.Execute(
            "DELETE FROM levelsync_groups WHERE group_id = {}", groupId);

        handler->PSendSysMessage(LANG_LS_SYNC_GROUP_DISBANDED);
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync disbandaccount
    // -------------------------------------------------------------------
    static bool HandleDisbandAccountCommand(ChatHandler* handler, char const* /*args*/)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        Player* player      = handler->GetSession()->GetPlayer();
        uint32  myAccountId = player->GetSession()->GetAccountId();

        QueryResult r = CharacterDatabase.Query(
            "SELECT DISTINCT m.group_id FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE c.account = {}",
            myAccountId);

        if (!r)
        {
            handler->PSendSysMessage(LANG_LS_NO_GROUPS_FOUND);
            return true;
        }

        std::vector<uint32> groups;
        do {
            groups.push_back(r->Fetch()[0].Get<uint32>());
        } while (r->NextRow());

        for (uint32 groupId : groups)
        {
            for (uint32 g : sLevelSync->GetGroupMemberGuids(groupId))
            {
                if (Player* member = ObjectAccessor::FindPlayerByLowGUID(g))
                    ChatHandler(member->GetSession()).PSendSysMessage(LANG_LS_YOUR_DISBANDED);
            }

            CharacterDatabase.Execute(
                "DELETE FROM levelsync_members WHERE group_id = {}", groupId);

            CharacterDatabase.Execute(
                "DELETE FROM levelsync_groups WHERE group_id = {}", groupId);
        }

        handler->PSendSysMessage(LANG_LS_DISBANDED_GROUPS,
            static_cast<uint32>(groups.size()));
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync listaccount <account>
    // -------------------------------------------------------------------
    static bool HandleListAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        char* accountArg = strtok(const_cast<char*>(args), " ");
        char* keyArg     = strtok(nullptr, " ");
        if (!accountArg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_LISTACCOUNT);
            return true;
        }

        Player* player      = handler->GetSession()->GetPlayer();
        uint32  myAccountId = player->GetSession()->GetAccountId();

        uint32 targetAccountId = GetAccountIdByName(std::string(accountArg));
        if (!targetAccountId)
        {
            handler->PSendSysMessage(LANG_LS_ACCOUNT_NOT_FOUND, accountArg);
            return true;
        }

        bool ownAccount = (targetAccountId == myAccountId);
        if (!ownAccount)
        {
            if (!keyArg)
            {
                handler->PSendSysMessage(LANG_LS_KEY_REQUIRED_VIEW);
                return true;
            }
            if (!VerifyAccountKey(targetAccountId, std::string(keyArg)))
            {
                handler->PSendSysMessage(LANG_LS_INVALID_KEY);
                return true;
            }
        }

        QueryResult chars = CharacterDatabase.Query(
            "SELECT c.name, c.level, c.`class`, m.group_id "
            "FROM characters c "
            "LEFT JOIN levelsync_members m ON m.char_guid = c.guid "
            "WHERE c.account = {} "
            "ORDER BY c.level DESC",
            targetAccountId);

        if (!chars)
        {
            handler->PSendSysMessage(LANG_LS_NO_CHARS_ACCOUNT_LIST, accountArg);
            return true;
        }

        handler->PSendSysMessage(LANG_LS_CHARS_ON_ACCOUNT, accountArg);
        do
        {
            std::string name  = chars->Fetch()[0].Get<std::string>();
            uint8 level       = chars->Fetch()[1].Get<uint8>();
            uint8 cls         = chars->Fetch()[2].Get<uint8>();
            uint32 grp        = chars->Fetch()[3].IsNull() ? 0 : chars->Fetch()[3].Get<uint32>();

            std::string status = grp ? "[Group " + std::to_string(grp) + "]" : "[No Group]";

            handler->PSendSysMessage(LANG_LS_CHAR_LINE,
                ClassColor(cls), name, static_cast<uint32>(level), ClassName(cls), status);
        } while (chars->NextRow());

        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync status
    // -------------------------------------------------------------------
    static bool HandleStatusCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        // Per-character 1.5s cooldown. Silent fail on spam (no chat output).
        // Timer stamps only on accepted calls so a spam burst can't slide
        // the next allowed call forward.
        using clock = std::chrono::steady_clock;
        static std::unordered_map<uint32, clock::time_point> lastStatus;
        constexpr auto COOLDOWN = std::chrono::milliseconds(1500);
        auto now = clock::now();
        auto it = lastStatus.find(myGuid);
        if (it != lastStatus.end() && now - it->second < COOLDOWN)
            return true;
        lastStatus[myGuid] = now;

        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        DisplayGroupStatus(handler, groupId);
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync level on
    //
    // Fire-only: runs a single level sync (highest member level
    // propagated to the rest of the group), then status returns to
    // "Ready". No persistent on/off state.
    // -------------------------------------------------------------------
    static bool HandleLevelCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        if (!sLevelSync->IsLevelSyncAllowed())
        {
            handler->PSendSysMessage(LANG_LS_LEVEL_DISABLED_SRV);
            return true;
        }

        char* arg = strtok(const_cast<char*>(args), " ");
        if (!arg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_LEVEL);
            return true;
        }

        std::string argStr(arg);
        if (argStr == "off")
        {
            handler->PSendSysMessage(LANG_LS_LEVEL_FIRE_ONLY);
            return true;
        }
        if (argStr != "on")
        {
            handler->PSendSysMessage(LANG_LS_USAGE_LEVEL);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        // 10s per-group cooldown for players. GMs bypass.
        bool isGM = handler->GetSession() && handler->GetSession()->GetSecurity() > SEC_PLAYER;
        if (!isGM)
        {
            uint32 secondsRemaining = 0;
            if (!sLevelSync->TryConsumeToggleCooldown(groupId, secondsRemaining))
            {
                handler->PSendSysMessage(LANG_LS_MUST_WAIT,
                    secondsRemaining);
                return true;
            }
        }

        handler->PSendSysMessage(LANG_LS_SYNCING_LEVEL);
        sLevelSync->SyncGroupOnLevelToggle(groupId);

        DisplayGroupStatus(handler, groupId);
        handler->PSendSysMessage(LANG_LS_LEVEL_FIRED);

        return true;

        // OLD (persistent on/off toggle, kept for reference):
        // bool enable = (std::string(arg) == "on");
        // CharacterDatabase.DirectExecute(
        //     "UPDATE levelsync_groups SET level_sync_enabled = {} WHERE group_id = {}",
        //     enable ? 1 : 0, groupId);
        // if (enable)
        // {
        //     handler->PSendSysMessage(LANG_LS_SYNCING_LEVEL);
        //     sLevelSync->SyncGroupOnLevelToggle(groupId);
        // }
        // DisplayGroupStatus(handler, groupId);
        // handler->PSendSysMessage(LANG_LS_LEVEL_SYNC_LEGACY, enable ? "|cff00ff00enabled|r" : "|cffff0000disabled|r");
    }

    // -------------------------------------------------------------------
    // .levelsync IP on
    //
    // Fire-only: runs a single IP-tier sync (highest tier in the group
    // propagated to the rest), then status returns to "Ready". No
    // persistent on/off state.
    // -------------------------------------------------------------------
    static bool HandleIndivProgressionCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        if (!sLevelSync->IsProgressionAllowed())
        {
            handler->PSendSysMessage(LANG_LS_PROG_DISABLED_SRV);
            return true;
        }

        char* arg = strtok(const_cast<char*>(args), " ");
        if (!arg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_IP);
            return true;
        }

        std::string argStr(arg);
        if (argStr == "off")
        {
            handler->PSendSysMessage(LANG_LS_IP_FIRE_ONLY);
            return true;
        }
        if (argStr != "on")
        {
            handler->PSendSysMessage(LANG_LS_USAGE_IP);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        // 10s per-group cooldown for players. GMs bypass.
        bool isGM = handler->GetSession() && handler->GetSession()->GetSecurity() > SEC_PLAYER;
        if (!isGM)
        {
            uint32 secondsRemaining = 0;
            if (!sLevelSync->TryConsumeToggleCooldown(groupId, secondsRemaining))
            {
                handler->PSendSysMessage(LANG_LS_MUST_WAIT,
                    secondsRemaining);
                return true;
            }
        }

        handler->PSendSysMessage(LANG_LS_SYNCING_PROG);
        sLevelSync->SyncIPOnToggle(groupId);

        DisplayGroupStatus(handler, groupId);
        handler->PSendSysMessage(LANG_LS_IP_FIRED);

        return true;

        // OLD (persistent on/off toggle, kept for reference):
        // bool enable = (std::string(arg) == "on");
        // CharacterDatabase.DirectExecute(
        //     "UPDATE levelsync_groups SET sync_progression = {} WHERE group_id = {}",
        //     enable ? 1 : 0, groupId);
        // if (enable)
        // {
        //     handler->PSendSysMessage(LANG_LS_SYNCING_PROG);
        //     sLevelSync->SyncIPOnToggle(groupId);
        // }
        // DisplayGroupStatus(handler, groupId);
        // handler->PSendSysMessage(LANG_LS_PROG_SYNC_LEGACY, enable ? "|cff00ff00enabled|r" : "|cffff0000disabled|r");
    }

    // -------------------------------------------------------------------
    // .levelsync money
    //
    // Pools every other group member's gold (online + offline) into the
    // caller's wallet. One-shot — no toggle, no persistent state. Shares
    // the 10-second per-group toggle cooldown so it can't be spammed.
    // Online drains use Player::SetMoney(0) so PLAYER_FIELD_COINAGE marks
    // dirty and the client wallet refreshes within a tick; offline drains
    // are a direct UPDATE on characters.money. Caller's pre-pool money is
    // untouched and the credit goes through Player::ModifyMoney.
    // -------------------------------------------------------------------
    static bool HandleMoneyCommand(ChatHandler* handler, char const* /*args*/)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        if (!sLevelSync->IsMoneyCommandsAllowed())
        {
            handler->PSendSysMessage(LANG_LS_MONEY_DISABLED_SRV);
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        uint32  myGuid = player->GetGUID().GetCounter();

        uint32 groupId = sLevelSync->GetGroupId(myGuid);
        if (!groupId)
        {
            handler->PSendSysMessage(LANG_LS_NOT_IN_GROUP);
            return true;
        }

        bool isGM = handler->GetSession() && handler->GetSession()->GetSecurity() > SEC_PLAYER;
        if (!isGM)
        {
            uint32 secondsRemaining = 0;
            if (!sLevelSync->TryConsumeToggleCooldown(groupId, secondsRemaining))
            {
                handler->PSendSysMessage(LANG_LS_MUST_WAIT,
                    secondsRemaining);
                return true;
            }
        }

        auto result = sLevelSync->PoolGroupMoney(player, groupId);

        switch (result.status)
        {
            case LevelSyncMgr::PoolStatus::AloneInGroup:
                handler->PSendSysMessage(LANG_LS_NO_OTHER_MEMBERS);
                return true;

            case LevelSyncMgr::PoolStatus::NoGold:
                handler->PSendSysMessage(LANG_LS_NO_GOLD);
                return true;

            case LevelSyncMgr::PoolStatus::CapExceeded:
            {
                std::string total = FormatMoneyDisplay(
                    result.totalDrained > 0xFFFFFFFFull
                        ? 0xFFFFFFFFu
                        : uint32(result.totalDrained));
                handler->PSendSysMessage(LANG_LS_CAP_EXCEEDED,
                    total);
                return true;
            }

            case LevelSyncMgr::PoolStatus::Ok:
            {
                std::string total = FormatMoneyDisplay(
                    result.totalDrained > 0xFFFFFFFFull
                        ? 0xFFFFFFFFu
                        : uint32(result.totalDrained));
                handler->PSendSysMessage(LANG_LS_POOLED_FROM,
                    total, result.contributors);
                return true;
            }
        }

        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync unbindall [name]
    //
    // Non-GM equivalent of the core `.instance unbind all` command. Wipes
    // every bound instance across all 4 difficulty slots (dungeons +
    // raids), preserving the binding for the map the target is currently
    // inside.
    //
    // Target resolution (overloaded):
    //   - No arg → clicked/tab-selected player, falls back to self.
    //   - Name arg → online player matching that name (case-insensitive).
    //                Refused if the named player is offline / not found.
    //
    // Gated by LevelSync.AllowRaidUnbind (default 0). No cooldown.
    // -------------------------------------------------------------------
    static bool HandleUnbindAllCommand(ChatHandler* handler, char const* args)
    {
        if (!sLevelSync->IsEnabled())
        {
            handler->PSendSysMessage(LANG_LS_MODULE_DISABLED);
            return true;
        }

        if (!sLevelSync->IsRaidUnbindAllowed())
        {
            handler->PSendSysMessage(LANG_LS_UNBIND_DISABLED_SRV);
            return true;
        }

        Player* target = nullptr;
        char* nameArg  = strtok(const_cast<char*>(args), " ");

        if (nameArg && *nameArg)
        {
            // Name lookup is case-insensitive; FindPlayerByName does the
            // normalisation internally and returns nullptr if the player
            // is offline or doesn't exist.
            target = ObjectAccessor::FindPlayerByName(std::string(nameArg));
            if (!target)
            {
                handler->PSendSysMessage(LANG_LS_NOT_FOUND_OFFLINE, nameArg);
                return true;
            }
        }
        else
        {
            target = handler->getSelectedPlayer();
            if (!target)
                target = handler->GetSession()->GetPlayer();
        }

        if (!target)
            return true;

        uint32 counter = UnbindAllInstancesOn(target);

        if (target == handler->GetSession()->GetPlayer())
            handler->PSendSysMessage(LANG_LS_UNBOUND_INSTANCES, counter);
        else
            handler->PSendSysMessage(LANG_LS_UNBOUND_ON,
                counter, target->GetName());
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync gm unbindall [name]
    //
    // GM-gated counterpart to .levelsync unbindall. Same overloaded
    // target resolution: clicked/tab-selected player with self fallback
    // if no arg, or online named player (case-insensitive) if given.
    // Always available to GMs regardless of LevelSync.AllowRaidUnbind —
    // the conf flag only controls the SEC_PLAYER form.
    // -------------------------------------------------------------------
    static bool HandleGmUnbindAllCommand(ChatHandler* handler, char const* args)
    {
        Player* target = nullptr;
        char* nameArg  = strtok(const_cast<char*>(args), " ");

        if (nameArg && *nameArg)
        {
            target = ObjectAccessor::FindPlayerByName(std::string(nameArg));
            if (!target)
            {
                handler->PSendSysMessage(LANG_LS_NOT_FOUND_OFFLINE, nameArg);
                return true;
            }
        }
        else
        {
            target = handler->getSelectedPlayer();
            if (!target)
                target = handler->GetSession()->GetPlayer();
        }

        if (!target)
            return true;

        uint32 counter = UnbindAllInstancesOn(target);

        if (target == handler->GetSession()->GetPlayer())
            handler->PSendSysMessage(LANG_LS_UNBOUND_INSTANCES, counter);
        else
            handler->PSendSysMessage(LANG_LS_UNBOUND_ON,
                counter, target->GetName());
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync gm removeall <account|charname>
    // -------------------------------------------------------------------
    static bool HandleGmRemoveAllCommand(ChatHandler* handler, char const* args)
    {
        char* arg = strtok(const_cast<char*>(args), " ");
        if (!arg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_GM_REMOVEALL);
            return true;
        }

        std::string target(arg);

        QueryResult charResult = CharacterDatabase.Query(
            "SELECT account FROM characters WHERE LOWER(name) = LOWER('{}')", EscChar(target));

        if (!charResult)
        {
            handler->PSendSysMessage(LANG_LS_CHAR_NOT_FOUND, target);
            return true;
        }

        uint32 accountId = charResult->Fetch()[0].Get<uint32>();

        // Find their group via any character on this account
        QueryResult r = CharacterDatabase.Query(
            "SELECT DISTINCT m.group_id FROM levelsync_members m "
            "JOIN characters c ON m.char_guid = c.guid "
            "WHERE c.account = {} LIMIT 1",
            accountId);

        if (!r)
        {
            // Not in a group — just nuke their key
            CharacterDatabase.Execute(
                "DELETE FROM levelsync_account_keys WHERE account_id = {}", accountId);
            handler->PSendSysMessage(LANG_LS_KEY_REMOVED, target);
            return true;
        }

        uint32 groupId = r->Fetch()[0].Get<uint32>();

        // Notify all online members
        for (uint32 g : sLevelSync->GetGroupMemberGuids(groupId))
        {
            if (Player* member = ObjectAccessor::FindPlayerByLowGUID(g))
                ChatHandler(member->GetSession()).PSendSysMessage(LANG_LS_DISBANDED_BY_GM);
        }

        // Wipe keys, members, group
        CharacterDatabase.DirectExecute(
            "DELETE ak FROM levelsync_account_keys ak "
            "JOIN levelsync_members m ON ak.account_id = m.account_id "
            "WHERE m.group_id = {}",
            groupId);

        CharacterDatabase.Execute(
            "DELETE FROM levelsync_members WHERE group_id = {}", groupId);

        CharacterDatabase.Execute(
            "DELETE FROM levelsync_groups WHERE group_id = {}", groupId);

        handler->PSendSysMessage(LANG_LS_GROUP_DISBANDED, groupId);
        return true;
    }

    // -------------------------------------------------------------------
    // .levelsync gm xp <amount>
    //
    // Adds XP to the GM's current target (or self if no target). Uses the
    // engine's normal Player::GiveXP path so any resulting level-up is
    // handled correctly. Required because AC has no built-in xp modify
    // command and direct DB writes are invisible to online characters.
    // -------------------------------------------------------------------
    static bool HandleGmXpCommand(ChatHandler* handler, char const* args)
    {
        char* arg = strtok(const_cast<char*>(args), " ");
        if (!arg)
        {
            handler->PSendSysMessage(LANG_LS_USAGE_GM_XP);
            return true;
        }

        int32 amount = atoi(arg);
        if (amount <= 0)
        {
            handler->PSendSysMessage(LANG_LS_XP_POSITIVE);
            return true;
        }

        Player* target = handler->getSelectedPlayer();
        if (!target)
            target = handler->GetSession()->GetPlayer();

        if (!target)
        {
            handler->PSendSysMessage(LANG_LS_NO_TARGET);
            return true;
        }

        target->GiveXP(static_cast<uint32>(amount), nullptr);

        handler->PSendSysMessage(LANG_LS_GRANTED_XP,
            amount, target->GetName(),
            static_cast<uint32>(target->GetLevel()),
            target->GetUInt32Value(PLAYER_XP));
        return true;
    }
};

void AddSC_LevelSyncCommands()
{
    new LevelSyncCommandScript();
}
