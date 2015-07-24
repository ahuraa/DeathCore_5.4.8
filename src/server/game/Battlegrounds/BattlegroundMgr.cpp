/*
 * Copyright (C) 2013-2015 DeathCore <http://www.noffearrdeathproject.net/>
 *
 * Copyright (C) 2005-2015 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "ObjectMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "RatedMgr.h"
#include "RatedInfo.h"
#include "BattlegroundMgr.h"
#include "BattlegroundAV.h"
#include "BattlegroundAB.h"
#include "BattlegroundEY.h"
#include "BattlegroundWS.h"
#include "BattlegroundNA.h"
#include "BattlegroundBE.h"
#include "BattlegroundRL.h"
#include "BattlegroundSA.h"
#include "BattlegroundDS.h"
#include "BattlegroundRV.h"
#include "BattlegroundIC.h"
#include "BattlegroundTP.h"
#include "BattlegroundBFG.h"
#include "BattlegroundDG.h"
#include "BattlegroundSM.h"
#include "BattlegroundTOK.h"
#include "Chat.h"
#include "Map.h"
#include "MapInstanced.h"
#include "MapManager.h"
#include "Player.h"
#include "GameEventMgr.h"
#include "SharedDefines.h"
#include "Formulas.h"
#include "DisableMgr.h"
#include "Opcodes.h"

/*********************************************************/
/***            BATTLEGROUND MANAGER                   ***/
/*********************************************************/

BattlegroundMgr::BattlegroundMgr() :
    m_NextRatedArenaUpdate(sWorld->getIntConfig(CONFIG_ARENA_RATED_UPDATE_TIMER)),
    m_ArenaTesting(false), m_Testing(false)
{ }

BattlegroundMgr::~BattlegroundMgr()
{
    DeleteAllBattlegrounds();
}

void BattlegroundMgr::DeleteAllBattlegrounds()
{
    for (BattlegroundDataContainer::iterator itr1 = bgDataStore.begin(); itr1 != bgDataStore.end(); ++itr1)
    {
        BattlegroundData& data = itr1->second;

        while (!data.m_Battlegrounds.empty())
            delete data.m_Battlegrounds.begin()->second;
        data.m_Battlegrounds.clear();

        while (!data.BGFreeSlotQueue.empty())
            delete data.BGFreeSlotQueue.front();
    }

    bgDataStore.clear();
}

// used to update running battlegrounds, and delete finished ones
void BattlegroundMgr::Update(uint32 diff)
{
    for (BattlegroundDataContainer::iterator itr1 = bgDataStore.begin(); itr1 != bgDataStore.end(); ++itr1)
    {
        BattlegroundContainer& bgs = itr1->second.m_Battlegrounds;
        BattlegroundContainer::iterator itrDelete = bgs.begin();
        // first one is template and should not be deleted
        for (BattlegroundContainer::iterator itr = ++itrDelete; itr != bgs.end();)
        {
            itrDelete = itr++;
            Battleground* bg = itrDelete->second;

            bg->Update(diff);
            if (bg->ToBeDeleted())
            {
                itrDelete->second = NULL;
                bgs.erase(itrDelete);
                BattlegroundClientIdsContainer& clients = itr1->second.m_ClientBattlegroundIds[bg->GetBracketId()];
                if (!clients.empty())
                     clients.erase(bg->GetClientInstanceID());

                delete bg;
            }
        }
    }

    // update events timer
    for (int qtype = BATTLEGROUND_QUEUE_NONE; qtype < MAX_BATTLEGROUND_QUEUE_TYPES; ++qtype)
        m_BattlegroundQueues[qtype].UpdateEvents(diff);

    // update scheduled queues
    if (!m_QueueUpdateScheduler.empty())
    {
        std::vector<uint64> scheduled;
        std::swap(scheduled, m_QueueUpdateScheduler);

        for (uint8 i = 0; i < scheduled.size(); i++)
        {
            uint32 arenaMMRating = scheduled[i] >> 48;
            RatedType ratedType = RatedType(scheduled[i] >> 40 & 0xFF);
            BattlegroundQueueTypeId bgQueueTypeId = BattlegroundQueueTypeId(scheduled[i] >> 24 & 0xFFFF);
            BattlegroundTypeId bgTypeId = BattlegroundTypeId((scheduled[i] >> 8) & 0xFFFF);
            BattlegroundBracketId bracket_id = BattlegroundBracketId(scheduled[i] & 0xFF);
            m_BattlegroundQueues[bgQueueTypeId].BattlegroundQueueUpdate(diff, bgTypeId, bracket_id, ratedType, arenaMMRating > 0, arenaMMRating);
        }
    }

    // if rating difference counts, maybe force-update queues
    if (sWorld->getIntConfig(CONFIG_ARENA_MAX_RATING_DIFFERENCE) && sWorld->getIntConfig(CONFIG_ARENA_RATED_UPDATE_TIMER))
    {
        // it's time to force update
        if (m_NextRatedArenaUpdate < diff)
        {
            // forced update for rated arenas (scan all, but skipped non rated)
            TC_LOG_TRACE("bg.arena", "BattlegroundMgr: UPDATING ARENA QUEUES");
            for (int qtype = BATTLEGROUND_QUEUE_2v2; qtype <= BATTLEGROUND_QUEUE_10v10; ++qtype)
                for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
                    m_BattlegroundQueues[qtype].BattlegroundQueueUpdate(diff,
                        BATTLEGROUND_AA, BattlegroundBracketId(bracket),
                        BattlegroundMgr::GetRatedTypeByQueue(BattlegroundQueueTypeId(qtype)), true, 0);

            m_NextRatedArenaUpdate = sWorld->getIntConfig(CONFIG_ARENA_RATED_UPDATE_TIMER);
        }
        else
            m_NextRatedArenaUpdate -= diff;
    }
}

void BattlegroundMgr::BuildBattlegroundStatusPacket(WorldPacket* data, Battleground* bg, Player* player, uint8 QueueSlot, uint8 StatusID, uint32 Time1, uint32 Time2, RatedType ratedType)
{
    ObjectGuid playerGuid = player->GetGUID();
    ObjectGuid bgGuid;

    if (bg)
        bgGuid = bg->GetGUID();
    else
        StatusID = STATUS_NONE;

    switch (StatusID)
    {
        case STATUS_NONE:
        {
            data->Initialize(SMSG_BATTLEFIELD_STATUS_NONE);

            *data << uint32(Time2);                     // Join Time
            *data << uint32(0);                         // Id
            *data << uint32(QueueSlot);                 // Queue slot

            data->WriteBit(playerGuid[2]);
            data->WriteBit(playerGuid[0]);
            data->WriteBit(playerGuid[4]);
            data->WriteBit(playerGuid[5]);
            data->WriteBit(playerGuid[3]);
            data->WriteBit(playerGuid[7]);
            data->WriteBit(playerGuid[1]);
            data->WriteBit(playerGuid[6]);

            data->WriteByteSeq(playerGuid[3]);
            data->WriteByteSeq(playerGuid[5]);
            data->WriteByteSeq(playerGuid[6]);
            data->WriteByteSeq(playerGuid[4]);
            data->WriteByteSeq(playerGuid[2]);
            data->WriteByteSeq(playerGuid[0]);
            data->WriteByteSeq(playerGuid[1]);
            data->WriteByteSeq(playerGuid[7]);
            break;
        }
        case STATUS_WAIT_QUEUE:
        {
            data->Initialize(SMSG_BATTLEFIELD_STATUS_QUEUED);

            data->WriteBit(bgGuid[1]);
            data->WriteBit(bgGuid[5]);
            data->WriteBit(playerGuid[0]);
            data->WriteBit(bgGuid[7]);
            data->WriteBit(bg->IsRated());              // Is Rated
            data->WriteBit(0);                          // AsGroup
            data->WriteBit(1);                          // Eligible In Queue
            data->WriteBit(playerGuid[6]);
            data->WriteBit(playerGuid[4]);
            data->WriteBit(playerGuid[1]);
            data->WriteBit(playerGuid[3]);
            data->WriteBit(playerGuid[7]);
            data->WriteBit(playerGuid[5]);
            data->WriteBit(0);                          // IsSuspended
            data->WriteBit(bgGuid[3]);
            data->WriteBit(bgGuid[0]);
            data->WriteBit(bgGuid[2]);
            data->WriteBit(playerGuid[2]);
            data->WriteBit(bgGuid[6]);
            data->WriteBit(bgGuid[4]);

            data->FlushBits();

            data->WriteByteSeq(bgGuid[4]);
            data->WriteByteSeq(playerGuid[6]);
            *data << uint32(0);                         // Id
            data->WriteByteSeq(bgGuid[5]);
            *data << uint32(Time2);                     // Estimated Wait Time
            data->WriteByteSeq(bgGuid[3]);
            data->WriteByteSeq(playerGuid[3]);
            data->WriteByteSeq(playerGuid[4]);
            data->WriteByteSeq(playerGuid[0]);
            *data << uint8(bg->GetMinLevel());          // MinLevel
            data->WriteByteSeq(bgGuid[0]);
            *data << uint32(Time1);                     // Join Time
            *data << uint8(0);                          // unk (former premade size)
            data->WriteByteSeq(bgGuid[1]);
            *data << uint32(GetMSTimeDiffToNow(Time2)); // Time since joined
            data->WriteByteSeq(bgGuid[7]);
            *data << uint32(QueueSlot);                 // Queue slot
            data->WriteByteSeq(playerGuid[2]);
            data->WriteByteSeq(bgGuid[6]);
            *data << uint8(bg->GetMaxLevel());          // MaxLevel
            data->WriteByteSeq(bgGuid[2]);
            data->WriteByteSeq(playerGuid[5]);
            data->WriteByteSeq(playerGuid[1]);
            *data << uint32(bg->GetClientInstanceID()); // Client Instance ID
            data->WriteByteSeq(playerGuid[7]);
            break;
        }
        case STATUS_WAIT_JOIN:
        {
            data->Initialize(SMSG_BATTLEFIELD_STATUS_NEEDCONFIRMATION);

            data->WriteBit(playerGuid[7]);
            data->WriteBit(playerGuid[5]);
            data->WriteBit(playerGuid[4]);
            data->WriteBit(playerGuid[1]);
            data->WriteBit(bgGuid[3]);
            data->WriteBit(bgGuid[2]);
            data->WriteBit(1); // Missing Role
            data->WriteBit(bgGuid[0]);
            data->WriteBit(playerGuid[0]);
            data->WriteBit(playerGuid[6]);
            data->WriteBit(bgGuid[7]);
            data->WriteBit(bgGuid[4]);
            data->WriteBit(bgGuid[1]);
            data->WriteBit(bg->IsRated());              // Is Rated
            data->WriteBit(playerGuid[2]);
            data->WriteBit(bgGuid[6]);
            data->WriteBit(playerGuid[3]);
            data->WriteBit(bgGuid[5]);

            data->WriteByteSeq(playerGuid[1]);
            data->WriteByteSeq(bgGuid[1]);
            data->WriteByteSeq(playerGuid[2]);
            *data << uint8(0);                          // unk (former premade size)
            *data << uint32(QueueSlot);                 // Queue slot
            // *data << uint32(role);
            *data << uint32(bg->GetClientInstanceID()); // Client Instance ID
            data->WriteByteSeq(bgGuid[6]);
            data->WriteByteSeq(bgGuid[7]);
            *data << uint32(Time2);                     // Join Time
            data->WriteByteSeq(playerGuid[7]);
            *data << uint8(bg->GetMaxLevel());          // MaxLevel
            data->WriteByteSeq(playerGuid[4]);
            data->WriteByteSeq(bgGuid[2]);
            data->WriteByteSeq(bgGuid[4]);
            *data << uint32(Time1);                     // Time until closed
            *data << uint8(bg->GetMinLevel());          // Min Level
            data->WriteByteSeq(playerGuid[3]);
            data->WriteByteSeq(bgGuid[0]);
            data->WriteByteSeq(playerGuid[5]);
            data->WriteByteSeq(playerGuid[6]);
            *data << uint32(0);                         // Id
            data->WriteByteSeq(bgGuid[3]);
            data->WriteByteSeq(playerGuid[0]);
            data->WriteByteSeq(bgGuid[5]);
            *data << uint32(bg->GetMapId());            // Map Id
            break;
        }
        case STATUS_IN_PROGRESS:
        {
            data->Initialize(SMSG_BATTLEFIELD_STATUS_ACTIVE);

            data->WriteBit(playerGuid[0]);
            data->WriteBit(bgGuid[3]);
            data->WriteBit(playerGuid[3]);
            data->WriteBit(bgGuid[2]);
            data->WriteBit(playerGuid[2]);
            data->WriteBit(bgGuid[5]);
            data->WriteBit(bgGuid[1]);
            data->WriteBit(playerGuid[7]);
            data->WriteBit(0);                          // Left Early
            data->WriteBit(playerGuid[6]);
            data->WriteBit(bgGuid[0]);
            data->WriteBit(player->GetTeam() == HORDE ? 0 : 1);
            data->WriteBit(bgGuid[6]);
            data->WriteBit(bgGuid[7]);
            data->WriteBit(bgGuid[4]);
            data->WriteBit(playerGuid[1]);
            data->WriteBit(playerGuid[4]);
            data->WriteBit(playerGuid[5]);
            data->WriteBit(bg->IsRated());              // Is Rated

            data->FlushBits();

            data->WriteByteSeq(playerGuid[3]);
            *data << uint32(Time1);                     // Join Time
            *data << uint32(bg->GetRemainingTime());    // Remaining Time
            data->WriteByteSeq(bgGuid[7]);
            data->WriteByteSeq(bgGuid[5]);
            data->WriteByteSeq(playerGuid[1]);
            data->WriteByteSeq(bgGuid[6]);
            *data << uint32(Time2);                     // Elapsed Time
            *data << uint8(bg->GetMaxLevel());          // Max Level
            data->WriteByteSeq(bgGuid[1]);
            data->WriteByteSeq(bgGuid[2]);
            *data << uint32(QueueSlot);                 // Queue slot
            data->WriteByteSeq(playerGuid[4]);
            *data << uint8(bg->GetMinLevel());          // Min Level
            data->WriteByteSeq(playerGuid[6]);
            *data << uint32(bg->GetMapId());            // Map Id
            data->WriteByteSeq(playerGuid[0]);
            data->WriteByteSeq(playerGuid[5]);
            data->WriteByteSeq(playerGuid[7]);
            data->WriteByteSeq(bgGuid[4]);
            *data << uint32(bg->GetClientInstanceID()); // Client Instance ID or faction ?
            data->WriteByteSeq(playerGuid[2]);
            *data << uint8(0);                          // Unknown (former premade size)
            *data << uint32(0);                         // Id
            data->WriteByteSeq(bgGuid[3]);
            data->WriteByteSeq(bgGuid[0]);
            break;
        }
        case STATUS_WAIT_LEAVE:
            break;
    }
}

void BattlegroundMgr::BuildPvpLogDataPacket(WorldPacket* data, Battleground* bg)
{
    ByteBuffer buff;

    data->Initialize(SMSG_PVP_LOG_DATA, (1 + 1 + 4 + 40 * bg->GetPlayerScoresSize()));

    *data << uint8(bg->GetPlayersCountByTeam(ALLIANCE));
    *data << uint8(bg->GetPlayersCountByTeam(HORDE));

    data->WriteBit(bg->GetStatus() == STATUS_WAIT_LEAVE);       // If Ended
    data->WriteBit(0);                                          // Unk Some player stuff

    // placeholder for unk some player stuff 

    data->WriteBit(bg->IsRated());                              // isRated
    data->WriteBits(bg->GetPlayerScoresSize(), 19);

    for (Battleground::BattlegroundScoreMap::const_iterator itr = bg->GetPlayerScoresBegin(); itr != bg->GetPlayerScoresEnd(); ++itr)
    {
        if (!bg->IsPlayerInBattleground(itr->first))
        {
            TC_LOG_ERROR("network", "Player " UI64FMTD " has scoreboard entry for battleground %u but is not in battleground!", itr->first, bg->GetTypeID(true));
            continue;
        }

        Player* player = ObjectAccessor::FindPlayer(itr->first);
        ObjectGuid playerGUID = itr->first;
        BattlegroundScore* score = itr->second;

        data->WriteBit(playerGUID[6]);
        data->WriteBit(player->GetTeam() == HORDE ? 0 : 1);
        data->WriteBit(bg->IsRated());                              // Rating Change - for every rated match
        data->WriteBit(playerGUID[0]);
        data->WriteBit(0);                                          // MMR Change
        data->WriteBit(playerGUID[7]);
        data->WriteBit(0);                                          // PreMatch MMR
        data->WriteBit(playerGUID[3]);
        data->WriteBit(0);                                          // Prematch Rating
        data->WriteBit(playerGUID[4]);
        data->WriteBit(playerGUID[1]);

        buff << uint32(score->HealingDone);
        buff.WriteGuidBytes(playerGUID, 4, 5, 2);

        if (!bg->IsArena())
        {
            buff << uint32(score->Deaths);
            buff << uint32(score->HonorableKills);
            buff << uint32(score->BonusHonor / 100);
        }

        buff.WriteByteSeq(playerGUID[3]);
        buff << uint32(score->DamageDone);
        buff << uint32(score->KillingBlows);

        if (bg->IsRated())
        {
            buff << int32(score->PersonalRatingChange);
        }

        buff.WriteGuidBytes(playerGUID, 1, 6, 7, 0);
        buff << int32(player->GetSpecializationId(player->GetActiveSpec()));

        switch (bg->GetTypeID(true))                                                        // Custom values
        {
            case BATTLEGROUND_RB:
                switch (bg->GetMapId())
                {
                    case 489:
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundWGScore*)score)->FlagCaptures);        // flag captures
                        buff << uint32(((BattlegroundWGScore*)score)->FlagReturns);         // flag returns
                        break;
                    case 566:
                        data->WriteBits(1, 22);
                        buff << uint32(((BattlegroundEYScore*)score)->FlagCaptures);        // flag captures
                        break;
                    case 529:
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundABScore*)score)->BasesAssaulted);      // bases assaulted
                        buff << uint32(((BattlegroundABScore*)score)->BasesDefended);       // bases defended
                        break;
                    case 30:
                        data->WriteBits(5, 22);
                        buff << uint32(((BattlegroundAVScore*)score)->GraveyardsAssaulted); // GraveyardsAssaulted
                        buff << uint32(((BattlegroundAVScore*)score)->GraveyardsDefended);  // GraveyardsDefended
                        buff << uint32(((BattlegroundAVScore*)score)->TowersAssaulted);     // TowersAssaulted
                        buff << uint32(((BattlegroundAVScore*)score)->TowersDefended);      // TowersDefended
                        buff << uint32(((BattlegroundAVScore*)score)->MinesCaptured);       // MinesCaptured
                        break;
                    case 607:
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundSAScore*)score)->demolishers_destroyed);
                        buff << uint32(((BattlegroundSAScore*)score)->gates_destroyed);
                        break;
                    case 628:                                   // IC
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundICScore*)score)->BasesAssaulted);       // bases assaulted
                        buff << uint32(((BattlegroundICScore*)score)->BasesDefended);        // bases defended
                        break;
                    case 726:
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundTPScore*)score)->FlagCaptures);         // flag captures
                        buff << uint32(((BattlegroundTPScore*)score)->FlagReturns);          // flag returns
                        break;
                    case 761:
                        data->WriteBits(2, 22);
                        buff << uint32(((BattlegroundBFGScore*)score)->BasesAssaulted);      // bases assaulted
                        buff << uint32(((BattlegroundBFGScore*)score)->BasesDefended);       // bases defended
                        break;
                    default:
                        data->WriteBits(0, 22);
                        break;
                }
                break;
            case BATTLEGROUND_AV:
                data->WriteBits(5, 22);
                buff << uint32(((BattlegroundAVScore*)score)->GraveyardsAssaulted); // GraveyardsAssaulted
                buff << uint32(((BattlegroundAVScore*)score)->GraveyardsDefended);  // GraveyardsDefended
                buff << uint32(((BattlegroundAVScore*)score)->TowersAssaulted);     // TowersAssaulted
                buff << uint32(((BattlegroundAVScore*)score)->TowersDefended);      // TowersDefended
                buff << uint32(((BattlegroundAVScore*)score)->MinesCaptured);       // MinesCaptured
                break;
            case BATTLEGROUND_WS:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundWGScore*)score)->FlagCaptures);        // flag captures
                buff << uint32(((BattlegroundWGScore*)score)->FlagReturns);         // flag returns
                break;
            case BATTLEGROUND_AB:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundABScore*)score)->BasesAssaulted);      // bases assaulted
                buff << uint32(((BattlegroundABScore*)score)->BasesDefended);       // bases defended
                break;
            case BATTLEGROUND_EY:
                data->WriteBits(1, 22);
                buff << uint32(((BattlegroundEYScore*)score)->FlagCaptures);        // flag captures
                break;
            case BATTLEGROUND_SA:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundSAScore*)score)->demolishers_destroyed);
                buff << uint32(((BattlegroundSAScore*)score)->gates_destroyed);
                break;
            case BATTLEGROUND_IC:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundICScore*)score)->BasesAssaulted);       // bases assaulted
                buff << uint32(((BattlegroundICScore*)score)->BasesDefended);        // bases defended
                break;
            case BATTLEGROUND_TP:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundTPScore*)score)->FlagCaptures);         // flag captures
                buff << uint32(((BattlegroundTPScore*)score)->FlagReturns);          // flag returns
                break;
            case BATTLEGROUND_BFG:
                data->WriteBits(2, 22);
                buff << uint32(((BattlegroundBFGScore*)score)->BasesAssaulted);      // bases assaulted
                buff << uint32(((BattlegroundBFGScore*)score)->BasesDefended);       // bases defended
                break;
            case BATTLEGROUND_SM:
                data->WriteBits(1, 22);
                buff << uint32(((BattlegroundSMScore*)score)->MineCartCaptures);    // mine carts captured
                break;
            case BATTLEGROUND_NA:
            case BATTLEGROUND_BE:
            case BATTLEGROUND_AA:
            case BATTLEGROUND_RL:
            case BATTLEGROUND_DS:
            case BATTLEGROUND_RV:
                data->WriteBits(0, 22);
                break;
            default:
                data->WriteBits(0, 22);
                break;
        }

        data->WriteBit(player->IsInWorld());
        data->WriteBit(!bg->IsArena());
        data->WriteBit(playerGUID[5]);
        data->WriteBit(playerGUID[2]);
    }

    data->FlushBits();
    data->append(buff);

    if (bg->IsRated())
    {
        *data << int32(0);  // unk (negative value)
        *data << int32(0);  // unk (negative value)
        *data << int32(bg->GetTeamMatchmakerRating(TEAM_ALLIANCE));  // Players team MMR
        *data << int32(-1); // unk - always -1
        *data << int32(-1); // unk - always -1
        *data << int32(bg->GetTeamMatchmakerRating(TEAM_HORDE));  // Enemys team MMR

        TC_LOG_DEBUG("bg.battleground", "rating change: %d", 0);
    }

    if (bg->GetStatus() == STATUS_WAIT_LEAVE)
        *data << uint8(bg->GetWinner());

}

void BattlegroundMgr::BuildStatusFailedPacket(WorldPacket* data, Battleground* bg, Player* player, uint8 QueueSlot, GroupJoinBattlegroundResult result)
{
    ObjectGuid playerGuid = player->GetGUID(); // player who caused the error
    ObjectGuid battlegroundGuid = bg->GetGUID();
    ObjectGuid unkGuid3 = 0;

    data->Initialize(SMSG_BATTLEFIELD_STATUS_FAILED);

    *data << uint32(player->GetBattlegroundQueueJoinTime(bg->GetTypeID())); // Join Time
    *data << uint32(0);                         // Id ???
    *data << uint32(QueueSlot);                 // Queue slot
    *data << uint32(result);                    // Result

    data->WriteBit(unkGuid3[7]);
    data->WriteBit(battlegroundGuid[2]);
    data->WriteBit(battlegroundGuid[7]);
    data->WriteBit(unkGuid3[5]);
    data->WriteBit(playerGuid[2]);
    data->WriteBit(battlegroundGuid[6]);
    data->WriteBit(playerGuid[7]);
    data->WriteBit(playerGuid[3]);
    data->WriteBit(battlegroundGuid[0]);
    data->WriteBit(battlegroundGuid[3]);
    data->WriteBit(unkGuid3[4]);
    data->WriteBit(playerGuid[1]);
    data->WriteBit(unkGuid3[0]);
    data->WriteBit(playerGuid[0]);
    data->WriteBit(unkGuid3[2]);
    data->WriteBit(battlegroundGuid[4]);
    data->WriteBit(playerGuid[4]);
    data->WriteByteSeq(battlegroundGuid[1]);
    data->WriteBit(unkGuid3[3]);
    data->WriteBit(battlegroundGuid[5]);
    data->WriteBit(unkGuid3[1]);
    data->WriteBit(playerGuid[6]);
    data->WriteBit(playerGuid[5]);
    data->WriteBit(unkGuid3[6]);

    data->WriteByteSeq(unkGuid3[1]);
    data->WriteByteSeq(unkGuid3[2]);
    data->WriteByteSeq(unkGuid3[7]);
    data->WriteByteSeq(battlegroundGuid[6]);
    data->WriteByteSeq(battlegroundGuid[0]);
    data->WriteByteSeq(playerGuid[5]);
    data->WriteByteSeq(playerGuid[0]);
    data->WriteByteSeq(battlegroundGuid[1]);
    data->WriteByteSeq(battlegroundGuid[7]);
    data->WriteByteSeq(playerGuid[6]);
    data->WriteByteSeq(unkGuid3[0]);
    data->WriteByteSeq(battlegroundGuid[5]);
    data->WriteByteSeq(unkGuid3[6]);
    data->WriteByteSeq(playerGuid[1]);
    data->WriteByteSeq(battlegroundGuid[2]);
    data->WriteByteSeq(playerGuid[7]);
    data->WriteByteSeq(playerGuid[2]);
    data->WriteByteSeq(playerGuid[3]);
    data->WriteByteSeq(unkGuid3[5]);
    data->WriteByteSeq(playerGuid[4]);
    data->WriteByteSeq(unkGuid3[3]);
    data->WriteByteSeq(battlegroundGuid[3]);
    data->WriteByteSeq(unkGuid3[4]);
    data->WriteByteSeq(battlegroundGuid[4]);
}

void BattlegroundMgr::BuildUpdateWorldStatePacket(WorldPacket* data, uint32 field, uint32 value)
{
    data->Initialize(SMSG_UPDATE_WORLD_STATE, 4+4);
    data->WriteBit(0);
    *data << uint32(value);
    *data << uint32(field);
}

void BattlegroundMgr::BuildPlaySoundPacket(WorldPacket* data, uint32 soundid)
{
    data->Initialize(SMSG_PLAY_SOUND, 4 + 9);
    data->WriteBits(0, 8);
    *data << uint32(soundid);
}

void BattlegroundMgr::BuildPlayerLeftBattlegroundPacket(WorldPacket* data, uint64 guid)
{
    ObjectGuid guidBytes = guid;

    data->Initialize(SMSG_BATTLEGROUND_PLAYER_LEFT, 8);
    data->WriteBit(guidBytes[3]);
    data->WriteBit(guidBytes[5]);
    data->WriteBit(guidBytes[6]);
    data->WriteBit(guidBytes[0]);
    data->WriteBit(guidBytes[1]);
    data->WriteBit(guidBytes[2]);
    data->WriteBit(guidBytes[7]);
    data->WriteBit(guidBytes[4]);

    data->WriteByteSeq(guidBytes[0]);
    data->WriteByteSeq(guidBytes[6]);
    data->WriteByteSeq(guidBytes[5]);
    data->WriteByteSeq(guidBytes[7]);
    data->WriteByteSeq(guidBytes[2]);
    data->WriteByteSeq(guidBytes[1]);
    data->WriteByteSeq(guidBytes[3]);
    data->WriteByteSeq(guidBytes[4]);
}

void BattlegroundMgr::BuildPlayerJoinedBattlegroundPacket(WorldPacket* data, uint64 guid)
{
    ObjectGuid guidBytes = guid;

    data->Initialize(SMSG_BATTLEGROUND_PLAYER_JOINED, 9);
    data->WriteBit(guidBytes[7]);
    data->WriteBit(guidBytes[1]);
    data->WriteBit(guidBytes[0]);
    data->WriteBit(guidBytes[4]);
    data->WriteBit(guidBytes[2]);
    data->WriteBit(guidBytes[5]);
    data->WriteBit(guidBytes[6]);
    data->WriteBit(guidBytes[3]);

    data->WriteByteSeq(guidBytes[0]);
    data->WriteByteSeq(guidBytes[3]);
    data->WriteByteSeq(guidBytes[7]);
    data->WriteByteSeq(guidBytes[5]);
    data->WriteByteSeq(guidBytes[2]);
    data->WriteByteSeq(guidBytes[6]);
    data->WriteByteSeq(guidBytes[4]);
    data->WriteByteSeq(guidBytes[1]);
}

Battleground* BattlegroundMgr::GetBattlegroundThroughClientInstance(uint32 instanceId, BattlegroundTypeId bgTypeId)
{
    //cause at HandleBattlegroundJoinOpcode the clients sends the instanceid he gets from
    //SMSG_BATTLEFIELD_LIST we need to find the battleground with this clientinstance-id
    Battleground* bg = GetBattlegroundTemplate(bgTypeId);
    if (!bg)
        return NULL;

    if (bg->IsArena())
        return GetBattleground(instanceId, bgTypeId);

    BattlegroundDataContainer::const_iterator it = bgDataStore.find(bgTypeId);
    if (it == bgDataStore.end())
        return NULL;

    for (BattlegroundContainer::const_iterator itr = it->second.m_Battlegrounds.begin(); itr != it->second.m_Battlegrounds.end(); ++itr)
    {
        if (itr->second->GetClientInstanceID() == instanceId)
            return itr->second;
    }

    return NULL;
}

Battleground* BattlegroundMgr::GetBattleground(uint32 instanceId, BattlegroundTypeId bgTypeId)
{
    if (!instanceId)
        return NULL;

    BattlegroundDataContainer::const_iterator begin, end;

    if (bgTypeId == BATTLEGROUND_TYPE_NONE)
    {
        begin = bgDataStore.begin();
        end = bgDataStore.end();
    }
    else
    {
        end = bgDataStore.find(bgTypeId);
        if (end == bgDataStore.end())
            return NULL;
        begin = end++;
    }

    for (BattlegroundDataContainer::const_iterator it = begin; it != end; ++it)
    {
        BattlegroundContainer const& bgs = it->second.m_Battlegrounds;
        BattlegroundContainer::const_iterator itr = bgs.find(instanceId);
        if (itr != bgs.end())
           return itr->second;
    }

    return NULL;
}

Battleground* BattlegroundMgr::GetBattlegroundTemplate(BattlegroundTypeId bgTypeId)
{
    BattlegroundDataContainer::const_iterator itr = bgDataStore.find(bgTypeId);
    if (itr == bgDataStore.end())
        return NULL;

    BattlegroundContainer const& bgs = itr->second.m_Battlegrounds;
    // map is sorted and we can be sure that lowest instance id has only BG template
    return bgs.empty() ? NULL : bgs.begin()->second;
}

uint32 BattlegroundMgr::CreateClientVisibleInstanceId(BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id)
{
    if (IsArenaType(bgTypeId))
        return 0;                                           //arenas don't have client-instanceids

    // we create here an instanceid, which is just for
    // displaying this to the client and without any other use..
    // the client-instanceIds are unique for each battleground-type
    // the instance-id just needs to be as low as possible, beginning with 1
    // the following works, because std::set is default ordered with "<"
    // the optimalization would be to use as bitmask std::vector<uint32> - but that would only make code unreadable

    BattlegroundClientIdsContainer& clientIds = bgDataStore[bgTypeId].m_ClientBattlegroundIds[bracket_id];
    uint32 lastId = 0;
    for (BattlegroundClientIdsContainer::const_iterator itr = clientIds.begin(); itr != clientIds.end();)
    {
        if ((++lastId) != *itr)                             //if there is a gap between the ids, we will break..
            break;
        lastId = *itr;
    }

    clientIds.insert(++lastId);
    return lastId;
}

// create a new battleground that will really be used to play
Battleground* BattlegroundMgr::CreateNewBattleground(BattlegroundTypeId originalBgTypeId, PvPDifficultyEntry const* bracketEntry, RatedType ratedType, bool isRated)
{
    BattlegroundTypeId bgTypeId = originalBgTypeId;
    bool isRandom = false;

    switch (originalBgTypeId)
    {
        case BATTLEGROUND_RB:
            isRandom = true;
            /// Intentional fallback, "All Arenas" is random too
        case BATTLEGROUND_AA:
        case BATTLEGROUND_RATED_10_VS_10:
            bgTypeId = GetRandomBG(originalBgTypeId);
            break;
        default:
            break;
    }

    // get the template BG
    Battleground* bg_template = GetBattlegroundTemplate(bgTypeId);

    if (!bg_template)
    {
        TC_LOG_ERROR("bg.battleground", "Battleground: CreateNewBattleground - bg template not found for %u", bgTypeId);
        return NULL;
    }

    Battleground* bg = NULL;
    // create a copy of the BG template
    switch (bgTypeId)
    {
        case BATTLEGROUND_AV:
            bg = new BattlegroundAV(*(BattlegroundAV*)bg_template);
            break;
        case BATTLEGROUND_WS:
            bg = new BattlegroundWS(*(BattlegroundWS*)bg_template);
            break;
        case BATTLEGROUND_AB:
            bg = new BattlegroundAB(*(BattlegroundAB*)bg_template);
            break;
        case BATTLEGROUND_NA:
            bg = new BattlegroundNA(*(BattlegroundNA*)bg_template);
            break;
        case BATTLEGROUND_BE:
            bg = new BattlegroundBE(*(BattlegroundBE*)bg_template);
            break;
        case BATTLEGROUND_EY:
            bg = new BattlegroundEY(*(BattlegroundEY*)bg_template);
            break;
        case BATTLEGROUND_RL:
            bg = new BattlegroundRL(*(BattlegroundRL*)bg_template);
            break;
        case BATTLEGROUND_SA:
            bg = new BattlegroundSA(*(BattlegroundSA*)bg_template);
            break;
        case BATTLEGROUND_DS:
            bg = new BattlegroundDS(*(BattlegroundDS*)bg_template);
            break;
        case BATTLEGROUND_RV:
            bg = new BattlegroundRV(*(BattlegroundRV*)bg_template);
            break;
        case BATTLEGROUND_IC:
            bg = new BattlegroundIC(*(BattlegroundIC*)bg_template);
            break;
        case BATTLEGROUND_TP:
            bg = new BattlegroundTP(*(BattlegroundTP*)bg_template);
            break;
        case BATTLEGROUND_BFG:
            bg = new BattlegroundBFG(*(BattlegroundBFG*)bg_template);
            break;
        case BATTLEGROUND_SM:
            bg = new BattlegroundSM(*(BattlegroundSM*)bg_template);
            break;
        case BATTLEGROUND_RB:
        case BATTLEGROUND_AA:
        case BATTLEGROUND_RATED_10_VS_10:
            bg = new Battleground(*bg_template);
            break;
        default:
            return NULL;
    }

    bg->SetBracket(bracketEntry);
    bg->SetInstanceID(sMapMgr->GenerateInstanceId());
    bg->SetClientInstanceID(CreateClientVisibleInstanceId(isRandom ? BATTLEGROUND_RB : bgTypeId, bracketEntry->GetBracketId()));
    bg->Reset();                     // reset the new bg (set status to status_wait_queue from status_none)
    bg->SetStatus(STATUS_WAIT_JOIN); // start the joining of the bg
    bg->SetRatedType(ratedType);
    bg->SetTypeID(originalBgTypeId);
    bg->SetRandomTypeID(bgTypeId);
    bg->SetRated(isRated);
    bg->SetRandom(isRandom);
    bg->SetGuid(MAKE_NEW_GUID(bgTypeId, 0, HIGHGUID_BATTLEGROUND));

    // Set up correct min/max player counts for scoreboards
    if (bg->IsRated())
    {
        uint32 maxPlayersPerTeam = 0;
        switch (ratedType)
        {
            case RATED_TYPE_2v2:
                maxPlayersPerTeam = 2;
                break;
            case RATED_TYPE_3v3:
                maxPlayersPerTeam = 3;
                break;
            case RATED_TYPE_5v5:
                maxPlayersPerTeam = 5;
                break;
            case RATED_TYPE_10v10:
                maxPlayersPerTeam = 10;
                break;
        }

        bg->SetMaxPlayersPerTeam(maxPlayersPerTeam);
        bg->SetMaxPlayers(maxPlayersPerTeam * 2);
    }

    return bg;
}

// used to create the BG templates
bool BattlegroundMgr::CreateBattleground(CreateBattlegroundData& data)
{
    // Create the BG
    Battleground* bg = NULL;
    switch (data.bgTypeId)
    {
        case BATTLEGROUND_AV:
            bg = new BattlegroundAV;
            break;
        case BATTLEGROUND_WS:
            bg = new BattlegroundWS;
            break;
        case BATTLEGROUND_AB:
            bg = new BattlegroundAB;
            break;
        case BATTLEGROUND_NA:
            bg = new BattlegroundNA;
            break;
        case BATTLEGROUND_BE:
            bg = new BattlegroundBE;
            break;
        case BATTLEGROUND_EY:
            bg = new BattlegroundEY;
            break;
        case BATTLEGROUND_RL:
            bg = new BattlegroundRL;
            break;
        case BATTLEGROUND_SA:
            bg = new BattlegroundSA;
            break;
        case BATTLEGROUND_DS:
            bg = new BattlegroundDS;
            break;
        case BATTLEGROUND_RV:
            bg = new BattlegroundRV;
            break;
        case BATTLEGROUND_IC:
            bg = new BattlegroundIC;
            break;
        case BATTLEGROUND_AA:
            bg = new Battleground;
            break;
        case BATTLEGROUND_RB:
            bg = new Battleground;
            bg->SetRandom(true);
            break;
        case BATTLEGROUND_TP:
            bg = new BattlegroundTP;
            break;
        case BATTLEGROUND_BFG:
            bg = new BattlegroundBFG;
            break;
        case BATTLEGROUND_TOK:
            bg = new BattlegroundTOK;
            break;
        case BATTLEGROUND_DG:
            bg = new BattlegroundDG;
            break;
        case BATTLEGROUND_SM:
            bg = new BattlegroundSM;
            break;
        default:
            return false;
    }

    bg->SetMapId(data.MapID);
    bg->SetTypeID(data.bgTypeId);
    bg->SetInstanceID(0);
    bg->SetArenaorBGType(data.IsArena);
    bg->SetMinPlayersPerTeam(data.MinPlayersPerTeam);
    bg->SetMaxPlayersPerTeam(data.MaxPlayersPerTeam);
    bg->SetMinPlayers(data.MinPlayersPerTeam * 2);
    bg->SetMaxPlayers(data.MaxPlayersPerTeam * 2);
    bg->SetName(data.BattlegroundName);
    bg->SetTeamStartLoc(ALLIANCE, data.Team1StartLocX, data.Team1StartLocY, data.Team1StartLocZ, data.Team1StartLocO);
    bg->SetTeamStartLoc(HORDE,    data.Team2StartLocX, data.Team2StartLocY, data.Team2StartLocZ, data.Team2StartLocO);
    bg->SetStartMaxDist(data.StartMaxDist);
    bg->SetLevelRange(data.LevelMin, data.LevelMax);
    bg->SetScriptId(data.scriptId);
    bg->SetGuid(MAKE_NEW_GUID(data.bgTypeId, 0, HIGHGUID_BATTLEGROUND));

    AddBattleground(bg);

    return true;
}

void BattlegroundMgr::CreateInitialBattlegrounds()
{
    uint32 oldMSTime = getMSTime();
    //                                               0   1                  2                  3       4       5                 6               7              8            9             10         11        
    QueryResult result = WorldDatabase.Query("SELECT id, MinPlayersPerTeam, MaxPlayersPerTeam, MinLvl, MaxLvl, AllianceStartLoc, AllianceStartO, HordeStartLoc, HordeStartO, StartMaxDist, Weight, ScriptName FROM battleground_template");

    if (!result)
    {
        TC_LOG_ERROR("server.loading", ">> Loaded 0 battlegrounds. DB table `battleground_template` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 bgTypeId = fields[0].GetUInt32();
        if (DisableMgr::IsDisabledFor(DISABLE_TYPE_BATTLEGROUND, bgTypeId, NULL))
            continue;

        // can be overwrite by values from DB
        BattlemasterListEntry const* bl = sBattlemasterListStore.LookupEntry(bgTypeId);
        if (!bl)
        {
            TC_LOG_ERROR("bg.battleground", "Battleground ID %u not found in BattleMasterList.dbc. Battleground not created.", bgTypeId);
            continue;
        }

        CreateBattlegroundData data;
        data.bgTypeId = BattlegroundTypeId(bgTypeId);
        data.IsArena = (bl->type == TYPE_ARENA);
        data.MinPlayersPerTeam = fields[1].GetUInt16();
        data.MaxPlayersPerTeam = fields[2].GetUInt16();
        data.LevelMin = fields[3].GetUInt8();
        data.LevelMax = fields[4].GetUInt8();
        float dist = fields[9].GetFloat();
        data.StartMaxDist = dist * dist;

        data.scriptId = sObjectMgr->GetScriptId(fields[11].GetCString());
        data.BattlegroundName = bl->name;
        data.MapID = bl->mapid[0];

        if (data.MaxPlayersPerTeam == 0 || data.MinPlayersPerTeam > data.MaxPlayersPerTeam)
        {
            TC_LOG_ERROR("sql.sql", "Table `battleground_template` for id %u has bad values for MinPlayersPerTeam (%u) and MaxPlayersPerTeam(%u)",
                data.bgTypeId, data.MinPlayersPerTeam, data.MaxPlayersPerTeam);
            continue;
        }

        if (data.LevelMin == 0 || data.LevelMax == 0 || data.LevelMin > data.LevelMax)
        {
            TC_LOG_ERROR("sql.sql", "Table `battleground_template` for id %u has bad values for LevelMin (%u) and LevelMax(%u)",
                data.bgTypeId, data.LevelMin, data.LevelMax);
            continue;
        }

        if (data.bgTypeId == BATTLEGROUND_AA || data.bgTypeId == BATTLEGROUND_RB)
        {
            data.Team1StartLocX = 0;
            data.Team1StartLocY = 0;
            data.Team1StartLocZ = 0;
            data.Team1StartLocO = fields[6].GetFloat();
            data.Team2StartLocX = 0;
            data.Team2StartLocY = 0;
            data.Team2StartLocZ = 0;
            data.Team2StartLocO = fields[8].GetFloat();
        }
        else
        {
            uint32 startId = fields[5].GetUInt32();
            if (WorldSafeLocsEntry const* start = sWorldSafeLocsStore.LookupEntry(startId))
            {
                data.Team1StartLocX = start->x;
                data.Team1StartLocY = start->y;
                data.Team1StartLocZ = start->z;
                data.Team1StartLocO = fields[6].GetFloat();
            }
            else
            {
                TC_LOG_ERROR("sql.sql", "Table `battleground_template` for id %u have non-existed WorldSafeLocs.dbc id %u in field `AllianceStartLoc`. BG not created.", data.bgTypeId, startId);
                continue;
            }

            startId = fields[7].GetUInt32();
            if (WorldSafeLocsEntry const* start = sWorldSafeLocsStore.LookupEntry(startId))
            {
                data.Team2StartLocX = start->x;
                data.Team2StartLocY = start->y;
                data.Team2StartLocZ = start->z;
                data.Team2StartLocO = fields[8].GetFloat();
            }
            else
            {
                TC_LOG_ERROR("sql.sql", "Table `battleground_template` for id %u have non-existed WorldSafeLocs.dbc id %u in field `HordeStartLoc`. BG not created.", data.bgTypeId, startId);
                continue;
            }
        }

        if (!CreateBattleground(data))
            continue;

        if (data.IsArena)
        {
            if (data.bgTypeId != BATTLEGROUND_AA)
                m_ArenaSelectionWeights[data.bgTypeId] = fields[10].GetUInt8();
        }
        else if (data.bgTypeId != BATTLEGROUND_RB && data.bgTypeId != BATTLEGROUND_RATED_10_VS_10)
        {
            uint8 weight = fields[10].GetUInt8();
            
            // Random Battlegrounds pool
            m_BGSelectionWeights[data.bgTypeId] = weight;
            
            // Rated Battlegrounds pool            
            if (IsRatedBattlegroundTemplate(data.bgTypeId))
                m_RatedBattlegroundSelectionWeights[data.bgTypeId] = weight;
        }

        ++count;
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded %u battlegrounds in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void BattlegroundMgr::BuildBattlegroundListPacket(WorldPacket* data, uint64 guid, Player* player, BattlegroundTypeId bgTypeId)
{
    if (!player)
        return;

    BattlegroundDataContainer::iterator it = bgDataStore.find(bgTypeId);
    if (it == bgDataStore.end())
        return;

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(it->second.m_Battlegrounds.begin()->second->GetMapId(), player->getLevel());
    if (!bracketEntry)
        return;

    uint32 winnerConquest = (player->GetRandomWinner() ? sWorld->getIntConfig(CONFIG_BG_REWARD_WINNER_CONQUEST_LAST) : sWorld->getIntConfig(CONFIG_BG_REWARD_WINNER_CONQUEST_FIRST)) / CURRENCY_PRECISION;
    uint32 winnerHonor = (player->GetRandomWinner() ? sWorld->getIntConfig(CONFIG_BG_REWARD_WINNER_HONOR_LAST) : sWorld->getIntConfig(CONFIG_BG_REWARD_WINNER_HONOR_FIRST)) / CURRENCY_PRECISION;
    uint32 loserHonor = (!player->GetRandomWinner() ? sWorld->getIntConfig(CONFIG_BG_REWARD_LOSER_HONOR_FIRST) : sWorld->getIntConfig(CONFIG_BG_REWARD_LOSER_HONOR_LAST)) / CURRENCY_PRECISION;

    ObjectGuid guidBytes = guid;

    data->Initialize(SMSG_BATTLEFIELD_LIST);
    *data << uint32(winnerConquest)         // Winner Conquest Reward or Random Winner Conquest Reward
          << uint32(winnerConquest)               //uint32(loserHonor)             // Loser Honor Reward or Random Loser Honor Reward
          << uint8(bracketEntry->minLevel)    //uint8(bracketEntry->minLevel)  // min level
          << uint32(winnerConquest)         // Winner Conquest Reward or Random Winner Conquest Reward
          << uint32(winnerHonor)              //uint32(winnerHonor)            // Winner Honor Reward or Random Winner Honor Reward
          << uint32(bgTypeId)                 //uint32(bgTypeId)               // battleground id
          << uint32(winnerHonor)            // Winner Honor Reward or Random Winner Honor Reward
          << uint8(bracketEntry->maxLevel)    //uint8(bracketEntry->maxLevel)  // max level
          << uint32(loserHonor);            // Loser Honor Reward or Random Loser Honor Reward
          
    data->WriteBit(guidBytes[0]);
    data->WriteBit(0);                                      // unk
    data->WriteBit(guidBytes[4]);
    data->WriteBit(0);                                      // unk
    data->WriteBit(guidBytes[2]);
    data->WriteBit(1);                                      // unk
    data->WriteBit(guidBytes[7]);
    data->WriteBit(guidBytes[6]);
    data->WriteBit(guidBytes[5]);
    data->WriteBit(guidBytes[1]);
    data->WriteBit(0);                                      // unk
    data->WriteBit(guidBytes[3]);

    size_t count_pos = data->bitwpos();
    data->WriteBits(0, 22);                                 // placeholder

    data->FlushBits();

    data->WriteByteSeq(guidBytes[7]);
    data->WriteByteSeq(guidBytes[3]);
    data->WriteByteSeq(guidBytes[4]);
    data->WriteByteSeq(guidBytes[0]);
    data->WriteByteSeq(guidBytes[5]);
    data->WriteByteSeq(guidBytes[6]);
    data->WriteByteSeq(guidBytes[1]);
    data->WriteByteSeq(guidBytes[2]);

    uint32 count = 0;
    BattlegroundBracketId bracketId = bracketEntry->GetBracketId();
    BattlegroundClientIdsContainer& clientIds = it->second.m_ClientBattlegroundIds[bracketId];
    for (BattlegroundClientIdsContainer::const_iterator itr = clientIds.begin(); itr != clientIds.end(); ++itr)
    {
        *data << uint32(*itr);
        ++count;
    }
    data->PutBits(count_pos, count, 22);                    // bg instance count
}

void BattlegroundMgr::SendToBattleground(Player* player, uint32 instanceId, BattlegroundTypeId bgTypeId)
{
    if (Battleground* bg = GetBattleground(instanceId, bgTypeId))
    {
        float x, y, z, O;
        uint32 mapid = bg->GetMapId();
        uint32 team = player->GetTeam();

        bg->GetTeamStartLoc(team, x, y, z, O);
        TC_LOG_DEBUG("bg.battleground", "BattlegroundMgr::SendToBattleground: Sending %s to map %u, X %f, Y %f, Z %f, O %f (bgType %u)", player->GetName().c_str(), mapid, x, y, z, O, bgTypeId);
        player->TeleportTo(mapid, x, y, z, O);
    }
    else
        TC_LOG_ERROR("bg.battleground", "BattlegroundMgr::SendToBattleground: Instance %u (bgType %u) not found while trying to teleport player %s", instanceId, bgTypeId, player->GetName().c_str());
}

void BattlegroundMgr::SendAreaSpiritHealerQueryOpcode(Player* player, Battleground* bg, ObjectGuid guid)
{
    uint32 time_ = 30000 - bg->GetLastResurrectTime();      // resurrect every 30 seconds
    
    if (time_ == uint32(-1))
        time_ = 0;

    WorldPacket data(SMSG_AREA_SPIRIT_HEALER_TIME, 12);

    uint8 bitOrder[8] = {5, 2, 7, 6, 1, 0, 3, 4};
    data.WriteBitInOrder(guid, bitOrder);

    data.WriteGuidBytes(guid, 2, 3, 5, 4, 6);

    data << time_;

    data.WriteGuidBytes(guid, 7, 0, 1);

    player->GetSession()->SendPacket(&data);
}

bool BattlegroundMgr::IsArenaType(BattlegroundTypeId bgTypeId)
{
    return bgTypeId == BATTLEGROUND_AA
            || bgTypeId == BATTLEGROUND_BE
            || bgTypeId == BATTLEGROUND_NA
            || bgTypeId == BATTLEGROUND_DS
            || bgTypeId == BATTLEGROUND_RV
            || bgTypeId == BATTLEGROUND_RL;
}

bool BattlegroundMgr::IsRatedBattlegroundTemplate(BattlegroundTypeId bgTypeId)
{
    return bgTypeId == BATTLEGROUND_RATED_10_VS_10
        || bgTypeId == BATTLEGROUND_WS
        || bgTypeId == BATTLEGROUND_AB
        || bgTypeId == BATTLEGROUND_EY
        || bgTypeId == BATTLEGROUND_BFG
        || bgTypeId == BATTLEGROUND_TP;
}

BattlegroundQueueTypeId BattlegroundMgr::BGQueueTypeId(BattlegroundTypeId bgTypeId, RatedType ratedType)
{
    switch (bgTypeId)
    {
        case BATTLEGROUND_AB:
            return BATTLEGROUND_QUEUE_AB;
        case BATTLEGROUND_AV:
            return BATTLEGROUND_QUEUE_AV;
        case BATTLEGROUND_EY:
            return BATTLEGROUND_QUEUE_EY;
        case BATTLEGROUND_IC:
            return BATTLEGROUND_QUEUE_IC;
        case BATTLEGROUND_TP:
            return BATTLEGROUND_QUEUE_TP;
        case BATTLEGROUND_BFG:
            return BATTLEGROUND_QUEUE_BFG;
        case BATTLEGROUND_RB:
            return BATTLEGROUND_QUEUE_RB;
        case BATTLEGROUND_SA:
            return BATTLEGROUND_QUEUE_SA;
        case BATTLEGROUND_TOK:
            return BATTLEGROUND_QUEUE_TOK;
        case BATTLEGROUND_SM:
            return BATTLEGROUND_QUEUE_SM;
        case BATTLEGROUND_DG:
            return BATTLEGROUND_QUEUE_DG;
        case BATTLEGROUND_WS:
            return BATTLEGROUND_QUEUE_WS;
        case BATTLEGROUND_AA:
        case BATTLEGROUND_BE:
        case BATTLEGROUND_DS:
        case BATTLEGROUND_NA:
        case BATTLEGROUND_RL:
        case BATTLEGROUND_RV:
        case BATTLEGROUND_RATED_10_VS_10:
            switch (ratedType)
            {
                case RATED_TYPE_2v2:
                    return BATTLEGROUND_QUEUE_2v2;
                case RATED_TYPE_3v3:
                    return BATTLEGROUND_QUEUE_3v3;
                case RATED_TYPE_5v5:
                    return BATTLEGROUND_QUEUE_5v5;
                case RATED_TYPE_10v10:
                    return BATTLEGROUND_QUEUE_10v10;
                default:
                    return BATTLEGROUND_QUEUE_NONE;
            }
        default:
            return BATTLEGROUND_QUEUE_NONE;
    }
}

BattlegroundTypeId BattlegroundMgr::BGTemplateId(BattlegroundQueueTypeId bgQueueTypeId)
{
    switch (bgQueueTypeId)
    {
        case BATTLEGROUND_QUEUE_WS:
            return BATTLEGROUND_WS;
        case BATTLEGROUND_QUEUE_AB:
            return BATTLEGROUND_AB;
        case BATTLEGROUND_QUEUE_AV:
            return BATTLEGROUND_AV;
        case BATTLEGROUND_QUEUE_EY:
            return BATTLEGROUND_EY;
        case BATTLEGROUND_QUEUE_SA:
            return BATTLEGROUND_SA;
        case BATTLEGROUND_QUEUE_IC:
            return BATTLEGROUND_IC;
        case BATTLEGROUND_QUEUE_TP:
            return BATTLEGROUND_TP;
        case BATTLEGROUND_QUEUE_BFG:
            return BATTLEGROUND_BFG;
        case BATTLEGROUND_QUEUE_RB:
            return BATTLEGROUND_RB;
        case BATTLEGROUND_QUEUE_TOK:
            return BATTLEGROUND_TOK;
        case BATTLEGROUND_QUEUE_SM:
            return BATTLEGROUND_SM;
        case BATTLEGROUND_QUEUE_DG:
            return BATTLEGROUND_DG;
        case BATTLEGROUND_QUEUE_2v2:
        case BATTLEGROUND_QUEUE_3v3:
        case BATTLEGROUND_QUEUE_5v5:
            return BATTLEGROUND_AA;
        case BATTLEGROUND_QUEUE_10v10:
            return BATTLEGROUND_RATED_10_VS_10;
        default:
            // Used for uknown templates (it existed and do nothing)
            return BattlegroundTypeId(0);                   
    }
}

RatedType BattlegroundMgr::GetRatedTypeByQueue(BattlegroundQueueTypeId bgQueueTypeId)
{
    switch (bgQueueTypeId)
    {
        case BATTLEGROUND_QUEUE_2v2:
            return RATED_TYPE_2v2;
        case BATTLEGROUND_QUEUE_3v3:
            return RATED_TYPE_3v3;
        case BATTLEGROUND_QUEUE_5v5:
            return RATED_TYPE_5v5;
        case BATTLEGROUND_QUEUE_10v10:
            return RATED_TYPE_10v10;
        default:
            ASSERT(false && "Invalid Battleground Queue for rated match");
            return RATED_TYPE_NOT_RATED; 
    }
}

void BattlegroundMgr::ToggleTesting()
{
    m_Testing = !m_Testing;
    sWorld->SendWorldText(m_Testing ? LANG_DEBUG_BG_ON : LANG_DEBUG_BG_OFF);
}

void BattlegroundMgr::ToggleArenaTesting()
{
    m_ArenaTesting = !m_ArenaTesting;
    sWorld->SendWorldText(m_ArenaTesting ? LANG_DEBUG_ARENA_ON : LANG_DEBUG_ARENA_OFF);
}

void BattlegroundMgr::SetHolidayWeekends(uint32 mask)
{
    for (uint32 bgtype = 1; bgtype < MAX_BATTLEGROUND_TYPE_ID; ++bgtype)
    {
        if (Battleground* bg = GetBattlegroundTemplate(BattlegroundTypeId(bgtype)))
        {
            bg->SetHoliday(mask & (1 << bgtype));
        }
    }
}

void BattlegroundMgr::ScheduleQueueUpdate(uint32 arenaMatchmakerRating, RatedType ratedType, BattlegroundQueueTypeId bgQueueTypeId, BattlegroundTypeId bgTypeId, BattlegroundBracketId bracket_id)
{
    //This method must be atomic, @todo add mutex
    //we will use only 1 number created of bgTypeId and bracket_id
    uint64 const scheduleId = (uint64(arenaMatchmakerRating) << 48) | (uint64(ratedType) << 40) | (uint32(bgQueueTypeId) << 24) | (uint32(bgTypeId) << 8) | bracket_id;
    if (std::find(m_QueueUpdateScheduler.begin(), m_QueueUpdateScheduler.end(), scheduleId) == m_QueueUpdateScheduler.end())
        m_QueueUpdateScheduler.push_back(scheduleId);
}

uint32 BattlegroundMgr::GetMaxRatingDifference() const
{
    // this is for stupid people who can't use brain and set max rating difference to 0
    uint32 diff = sWorld->getIntConfig(CONFIG_ARENA_MAX_RATING_DIFFERENCE);
    if (diff == 0)
        diff = 5000;
    return diff;
}

uint32 BattlegroundMgr::GetRatingDiscardTimer() const
{
    return sWorld->getIntConfig(CONFIG_ARENA_RATING_DISCARD_TIMER);
}

uint32 BattlegroundMgr::GetPrematureFinishTime() const
{
    return sWorld->getIntConfig(CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER);
}

void BattlegroundMgr::LoadBattleMastersEntry()
{
    uint32 oldMSTime = getMSTime();

    mBattleMastersMap.clear();                                  // need for reload case

    QueryResult result = WorldDatabase.Query("SELECT entry, bg_template FROM battlemaster_entry");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 battlemaster entries. DB table `battlemaster_entry` is empty!");
        return;
    }

    uint32 count = 0;

    do
    {
        ++count;

        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();
        uint32 bgTypeId  = fields[1].GetUInt32();
        if (!sBattlemasterListStore.LookupEntry(bgTypeId))
        {
            TC_LOG_ERROR("sql.sql", "Table `battlemaster_entry` contain entry %u for not existed battleground type %u, ignored.", entry, bgTypeId);
            continue;
        }

        mBattleMastersMap[entry] = BattlegroundTypeId(bgTypeId);
    }
    while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded %u battlemaster entries in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
}

HolidayIds BattlegroundMgr::BGTypeToWeekendHolidayId(BattlegroundTypeId bgTypeId)
{
    switch (bgTypeId)
    {
        case BATTLEGROUND_AV: return HOLIDAY_CALL_TO_ARMS_AV;
        case BATTLEGROUND_EY: return HOLIDAY_CALL_TO_ARMS_EY;
        case BATTLEGROUND_WS: return HOLIDAY_CALL_TO_ARMS_WS;
        case BATTLEGROUND_SA: return HOLIDAY_CALL_TO_ARMS_SA;
        case BATTLEGROUND_AB: return HOLIDAY_CALL_TO_ARMS_AB;
        case BATTLEGROUND_IC: return HOLIDAY_CALL_TO_ARMS_IC;
        case BATTLEGROUND_TP: return HOLIDAY_CALL_TO_ARMS_TP;
        case BATTLEGROUND_BFG: return HOLIDAY_CALL_TO_ARMS_BFG;
        case BATTLEGROUND_TOK: return HOLIDAY_CALL_TO_ARMS_TOK;
        case BATTLEGROUND_DG: return HOLIDAY_CALL_TO_ARMS_DG;
        case BATTLEGROUND_SM: return HOLIDAY_CALL_TO_ARMS_SM;
        default: return HOLIDAY_NONE;
    }
}

BattlegroundTypeId BattlegroundMgr::WeekendHolidayIdToBGType(HolidayIds holiday)
{
    switch (holiday)
    {
        case HOLIDAY_CALL_TO_ARMS_AV: return BATTLEGROUND_AV;
        case HOLIDAY_CALL_TO_ARMS_EY: return BATTLEGROUND_EY;
        case HOLIDAY_CALL_TO_ARMS_WS: return BATTLEGROUND_WS;
        case HOLIDAY_CALL_TO_ARMS_SA: return BATTLEGROUND_SA;
        case HOLIDAY_CALL_TO_ARMS_AB: return BATTLEGROUND_AB;
        case HOLIDAY_CALL_TO_ARMS_IC: return BATTLEGROUND_IC;
        case HOLIDAY_CALL_TO_ARMS_TP: return BATTLEGROUND_TP;
        case HOLIDAY_CALL_TO_ARMS_BFG: return BATTLEGROUND_BFG;
        case HOLIDAY_CALL_TO_ARMS_TOK: return BATTLEGROUND_TOK;
        case HOLIDAY_CALL_TO_ARMS_DG: return BATTLEGROUND_DG;
        case HOLIDAY_CALL_TO_ARMS_SM: return BATTLEGROUND_SM;
        default: return BATTLEGROUND_TYPE_NONE;
    }
}

bool BattlegroundMgr::IsBGWeekend(BattlegroundTypeId bgTypeId)
{
    return IsHolidayActive(BGTypeToWeekendHolidayId(bgTypeId));
}

BattlegroundTypeId BattlegroundMgr::GetRandomBG(BattlegroundTypeId bgTypeId)
{
    uint32 weight = 0;
    BattlegroundTypeId returnBgTypeId = BATTLEGROUND_TYPE_NONE;
    BattlegroundSelectionWeightMap selectionWeights;

    if (bgTypeId == BATTLEGROUND_AA)
    {
        for (BattlegroundSelectionWeightMap::const_iterator it = m_ArenaSelectionWeights.begin(); it != m_ArenaSelectionWeights.end(); ++it)
        {
            if (it->second)
            {
                weight += it->second;
                selectionWeights[it->first] = it->second;
            }
        }
    }
    if (bgTypeId == BATTLEGROUND_RATED_10_VS_10)
    {
        // in the future, use own selection weights
        for (BattlegroundSelectionWeightMap::const_iterator it = m_RatedBattlegroundSelectionWeights.begin(); it != m_RatedBattlegroundSelectionWeights.end(); ++it)
        {
            if (it->second)
            {
                weight += it->second;
                selectionWeights[it->first] = it->second;
            }
        }
    }
    else if (bgTypeId == BATTLEGROUND_RB)
    {
        for (BattlegroundSelectionWeightMap::const_iterator it = m_BGSelectionWeights.begin(); it != m_BGSelectionWeights.end(); ++it)
        {
            if (it->second)
            {
                weight += it->second;
                selectionWeights[it->first] = it->second;
            }
        }
    }

    if (weight)
    {
        // Select a random value
        uint32 selectedWeight = urand(0, weight - 1);
        // Select the correct bg (if we have in DB A(10), B(20), C(10), D(15) --> [0---A---9|10---B---29|30---C---39|40---D---54])
        weight = 0;
        for (BattlegroundSelectionWeightMap::const_iterator it = selectionWeights.begin(); it != selectionWeights.end(); ++it)
        {
            weight += it->second;
            if (selectedWeight < weight)
            {
                returnBgTypeId = it->first;
                break;
            }
        }
    }

    return returnBgTypeId;
}

BGFreeSlotQueueContainer& BattlegroundMgr::GetBGFreeSlotQueueStore(BattlegroundTypeId bgTypeId)
{
    return bgDataStore[bgTypeId].BGFreeSlotQueue;
}

void BattlegroundMgr::AddToBGFreeSlotQueue(BattlegroundTypeId bgTypeId, Battleground* bg)
{
    bgDataStore[bgTypeId].BGFreeSlotQueue.push_front(bg);
}

void BattlegroundMgr::RemoveFromBGFreeSlotQueue(BattlegroundTypeId bgTypeId, uint32 instanceId)
{
    BGFreeSlotQueueContainer& queues = bgDataStore[bgTypeId].BGFreeSlotQueue;
    for (BGFreeSlotQueueContainer::iterator itr = queues.begin(); itr != queues.end(); ++itr)
        if ((*itr)->GetInstanceID() == instanceId)
        {
            queues.erase(itr);
            return;
        }
}

void BattlegroundMgr::AddBattleground(Battleground* bg)
{
    if (bg)
        bgDataStore[bg->GetTypeID()].m_Battlegrounds[bg->GetInstanceID()] = bg;
}

void BattlegroundMgr::RemoveBattleground(BattlegroundTypeId bgTypeId, uint32 instanceId)
{
    bgDataStore[bgTypeId].m_Battlegrounds.erase(instanceId);
}
