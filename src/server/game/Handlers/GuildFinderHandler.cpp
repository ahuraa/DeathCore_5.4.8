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

#include "WorldSession.h"
#include "WorldPacket.h"
#include "Object.h"
#include "SharedDefines.h"
#include "GuildFinderMgr.h"
#include "GuildMgr.h"

void WorldSession::HandleGuildFinderAddApplication(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_ADD_APPLICATION");

    if (sGuildFinderMgr->GetAllMembershipRequestsForPlayer(GetPlayer()->GetGUIDLow()).size() == 10)
        return;

    uint32 classRoles = 0;
    uint32 availability = 0;
    uint32 guildInterests = 0;

    recvPacket >> classRoles >> availability >> guildInterests;

    ObjectGuid guid;
    recvPacket.ReadGuidMask(guid, 7, 5, 2, 6, 1, 0);
    
    uint16 commentLength = recvPacket.ReadBits(10);

    recvPacket.ReadGuidMask(guid, 3, 4);
   
    recvPacket.ReadBits(6); //Flush bits

    recvPacket.ReadGuidBytes(guid, 4, 0, 2);

    std::string comment = recvPacket.ReadString(commentLength);

    recvPacket.ReadGuidBytes(guid, 6, 1, 5, 7, 3);

    uint32 guildLowGuid = GUID_LOPART(uint64(guid));

    if (!IS_GUILD_GUID(guid))
        return;
    if (!(classRoles & GUILDFINDER_ALL_ROLES) || classRoles > GUILDFINDER_ALL_ROLES)
        return;
    if (!(availability & AVAILABILITY_ALWAYS) || availability > AVAILABILITY_ALWAYS)
        return;
    if (!(guildInterests & ALL_INTERESTS) || guildInterests > ALL_INTERESTS)
        return;

    MembershipRequest request = MembershipRequest(GetPlayer()->GetGUIDLow(), guildLowGuid, availability, classRoles, guildInterests, comment, time(NULL));
    sGuildFinderMgr->AddMembershipRequest(guildLowGuid, request);
}

void WorldSession::HandleGuildFinderBrowse(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_BROWSE");
    uint32 classRoles = 0;
    uint32 availability = 0;
    uint32 guildInterests = 0;
    uint32 playerLevel = 0; // Raw player level (1-85), do they use MAX_FINDER_LEVEL when on level 85 ?

    recvPacket >> playerLevel >> availability >> classRoles >> guildInterests;

    bool filters = true;

    if (!(classRoles & GUILDFINDER_ALL_ROLES) || classRoles > GUILDFINDER_ALL_ROLES)
        filters = false;
    if (!(availability & AVAILABILITY_ALWAYS) || availability > AVAILABILITY_ALWAYS)
        filters = false;
    if (!(guildInterests & ALL_INTERESTS) || guildInterests > ALL_INTERESTS)
        filters = false;
    if (playerLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL) || playerLevel < 1)
        filters = false;

    Player* player = GetPlayer();

    LFGuildStore guildList;
    uint32 guildCount = 0;

    if (filters)
    {
        LFGuildPlayer settings(player->GetGUIDLow(), classRoles, availability, guildInterests, ANY_FINDER_LEVEL);
        guildList = sGuildFinderMgr->GetGuildsMatchingSetting(settings, player->GetTeamId());
        guildCount = guildList.size();
    }

    if (guildCount == 0)
    {
        WorldPacket packet(SMSG_LF_GUILD_BROWSE_UPDATED, 0);
        player->SendDirectMessage(&packet);
        return;
    }

    ByteBuffer bufferDataHead;
    ByteBuffer bufferDataTail;

    bufferDataHead.WriteBits(guildCount, 18);

    for (LFGuildStore::const_iterator itr = guildList.begin(); itr != guildList.end(); ++itr)
    {
        LFGuildSettings guildSettings = itr->second;
        Guild* guild = sGuildMgr->GetGuildById(itr->first);

        ObjectGuid guildGUID = ObjectGuid(guild->GetGUID());

        //Write guild's headers
        bufferDataHead.WriteGuidMask(guildGUID, 6, 5, 4, 0, 1);

        bufferDataHead.WriteBits(guildSettings.GetComment().size(), 10);

        bufferDataHead.WriteBit(guildGUID[3]);

        bufferDataHead.WriteBits(guild->GetName().size(), 7);

        bufferDataHead.WriteGuidMask(guildGUID, 7, 2);

        //Write guild's info at tail
        bufferDataTail.WriteByteSeq(guildGUID[3]);

        bufferDataTail << uint32(guild->GetEmblemInfo().GetStyle());
        bufferDataTail << uint8(sGuildFinderMgr->HasRequest(player->GetGUIDLow(), guild->GetGUID())); // Request pending

        bufferDataTail.WriteByteSeq(guildGUID[0]);

        bufferDataTail << uint32(guild->GetAchievementMgr().GetAchievementPoints());

        bufferDataTail.WriteByteSeq(guildGUID[2]);

        bufferDataTail << uint32(guildSettings.GetInterests());
        bufferDataTail << uint32(guild->GetEmblemInfo().GetBackgroundColor());
        bufferDataTail << uint32(guild->GetLevel());
        bufferDataTail << uint32(guildSettings.GetAvailability());
        bufferDataTail << uint32(guildSettings.GetClassRoles());

        bufferDataTail.WriteByteSeq(guildGUID[5]);

        bufferDataTail << uint32(0); //Unk: always before guild name

        bufferDataTail.WriteString(guild->GetName());

        bufferDataTail << uint32(guild->GetEmblemInfo().GetBorderStyle()); // Guessed
        bufferDataTail << uint8(0); //Unk

        bufferDataTail.WriteByteSeq(guildGUID[7]);

        bufferDataTail << uint32(guild->GetEmblemInfo().GetColor());

        bufferDataTail.WriteByteSeq(guildGUID[6]);

        bufferDataTail << uint32(0); // Unk

        bufferDataTail.WriteString(guildSettings.GetComment());

        bufferDataTail << uint32(guild->GetEmblemInfo().GetBorderColor());
        bufferDataTail << uint32(guild->GetMembersCount());

        bufferDataTail.WriteGuidBytes(guildGUID, 1, 4);
    }

    bufferDataHead.FlushBits();
    bufferDataTail.FlushBits();

    WorldPacket data(SMSG_LF_GUILD_BROWSE_UPDATED, bufferDataHead.size() + bufferDataTail.size());
    
    data.append(bufferDataHead);
    data.append(bufferDataTail);

    player->SendDirectMessage(&data);
}

void WorldSession::HandleGuildFinderDeclineRecruit(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_DECLINE_RECRUIT");

    ObjectGuid playerGuid;

    recvPacket.ReadGuidMask(playerGuid, 1, 4, 5, 2, 6, 7, 0, 3);

    recvPacket.ReadGuidBytes(playerGuid, 5, 7, 2, 3, 4, 1, 0, 6);

    if (!IS_PLAYER_GUID(playerGuid))
        return;

    sGuildFinderMgr->RemoveMembershipRequest(GUID_LOPART(playerGuid), GetPlayer()->GetGuildId());
}

void WorldSession::HandleGuildFinderGetApplications(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_GET_APPLICATIONS"); // Empty opcode

    std::list<MembershipRequest> applicatedGuilds = sGuildFinderMgr->GetAllMembershipRequestsForPlayer(GetPlayer()->GetGUIDLow());
    uint32 applicationsCount = applicatedGuilds.size();
    
    ByteBuffer bufferDataHead;
    ByteBuffer bufferDataTail;

    if (applicationsCount > 0)
    {
        bufferDataHead.WriteBits(applicationsCount, 19);

        for (std::list<MembershipRequest>::const_iterator itr = applicatedGuilds.begin(); itr != applicatedGuilds.end(); ++itr)
        {

            Guild* guild = sGuildMgr->GetGuildById(itr->GetGuildId());
            LFGuildSettings guildSettings = sGuildFinderMgr->GetGuildSettings(itr->GetGuildId());
            MembershipRequest request = *itr;
            ObjectGuid guildGuid = ObjectGuid(guild->GetGUID());

            //Write buffer head
            bufferDataHead.WriteGuidMask(guildGuid, 0, 4, 2, 7);

            bufferDataHead.WriteBits(guild->GetName().size(), 7);

            bufferDataHead.WriteGuidMask(guildGuid, 1, 3);

            bufferDataHead.WriteBits(request.GetComment().size(), 10);

            bufferDataHead.WriteGuidMask(guildGuid, 6, 5);

            //Write buffer tail
            bufferDataTail << uint32(request.GetInterests());
            bufferDataTail << uint32(0); //unk: always before guild name

            bufferDataTail.WriteString(guild->GetName());

            bufferDataTail.WriteByteSeq(guildGuid[4]);

            bufferDataTail << uint32(request.GetClassRoles());

            bufferDataTail.WriteGuidBytes(guildGuid, 6, 5);

            bufferDataTail << uint32(time(NULL) - request.GetSubmitTime()); // Time since application (seconds)

            bufferDataTail.WriteGuidBytes(guildGuid, 1, 3, 0, 7, 2);

            bufferDataTail << uint32(request.GetExpiryTime() - time(NULL)); // Time left to application expiry (seconds)
            bufferDataTail << uint32(request.GetAvailability());

            bufferDataTail.WriteString(request.GetComment());
        }

        bufferDataHead.FlushBits();
        bufferDataTail.FlushBits();
    }
    else
    {
        bufferDataHead.WriteBits(0, 24);
    }

    WorldPacket data(SMSG_LF_GUILD_APPLICATIONS_LIST_UPDATED, bufferDataHead.size() + bufferDataTail.size() + 8);

    data.append(bufferDataHead);

    if (bufferDataTail.size() > 0)
        data.append(bufferDataTail);

    data << uint32(10 - sGuildFinderMgr->CountRequestsFromPlayer(GetPlayer()->GetGUIDLow())); // Applications count left

    GetPlayer()->SendDirectMessage(&data);
}

// Lists all recruits for a guild - Misses times
void WorldSession::HandleGuildFinderGetRecruits(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_GET_RECRUITS");

    uint32 unkTime = 0;
    recvPacket >> unkTime;

    Player* player = GetPlayer();
    if (!player->GetGuildId())
        return;

    std::vector<MembershipRequest> recruitsList = sGuildFinderMgr->GetAllMembershipRequestsForGuild(player->GetGuildId());
    uint32 recruitCount = recruitsList.size();

    ByteBuffer dataBuffer(53 * recruitCount);
    WorldPacket data(SMSG_LF_GUILD_APPLICANT_LIST_UPDATED, 7 + 26 * recruitCount + 53 * recruitCount); 
    data.WriteBits(recruitCount, 20);

    for (std::vector<MembershipRequest>::const_iterator itr = recruitsList.begin(); itr != recruitsList.end(); ++itr)
    {
        MembershipRequest request = *itr;
        ObjectGuid playerGuid(MAKE_NEW_GUID(request.GetPlayerGUID(), 0, HIGHGUID_PLAYER));

        data.WriteBits(request.GetComment().size(), 11);
        data.WriteGuidMask(playerGuid, 2, 4, 3, 7, 0);
        data.WriteBits(request.GetName().size(), 7);
        data.WriteGuidMask(playerGuid, 5, 1, 6);

        dataBuffer.WriteByteSeq(playerGuid[4]);

        dataBuffer << int32(time(NULL) <= request.GetExpiryTime());

        dataBuffer.WriteGuidBytes(playerGuid, 3, 0, 1);

        dataBuffer << int32(request.GetLevel());

        dataBuffer.WriteGuidBytes(playerGuid, 6, 7, 2);

        dataBuffer << int32(time(NULL) - request.GetSubmitTime()); // Time in seconds since application submitted.
        dataBuffer << int32(request.GetAvailability());
        dataBuffer << int32(request.GetClassRoles());
        dataBuffer << int32(request.GetInterests());
        dataBuffer << int32(request.GetExpiryTime() - time(NULL)); // TIme in seconds until application expires.

        dataBuffer.WriteString(request.GetName());
        dataBuffer.WriteString(request.GetComment());

        dataBuffer << int32(request.GetClass());

        dataBuffer.WriteByteSeq(playerGuid[5]);
    }

    data.FlushBits();
    data.append(dataBuffer);
    data << uint32(time(NULL)); // Unk time

    player->SendDirectMessage(&data);
}

void WorldSession::HandleGuildFinderPostRequest(WorldPacket& /*recvPacket*/)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_POST_REQUEST"); // Empty opcode

    Player* player = GetPlayer();

    if (!player->GetGuildId()) // Player must be in guild
        return;

    bool isGuildMaster = true;
    if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId()))
        if (guild->GetLeaderGUID() != player->GetGUID())
            isGuildMaster = false;

    LFGuildSettings settings = sGuildFinderMgr->GetGuildSettings(player->GetGuildId());

    WorldPacket data(SMSG_LF_GUILD_POST_UPDATED, 35);
    data.WriteBit(isGuildMaster); // Guessed

    if (isGuildMaster)
    {
        data.WriteBit(settings.IsListed());
        data.WriteBits(settings.GetComment().size(), 11);
        data << uint32(settings.GetLevel());
        data.WriteString(settings.GetComment());
        data << uint32(0); // Unk Int32
        data << uint32(settings.GetAvailability());
        data << uint32(settings.GetClassRoles());
        data << uint32(settings.GetInterests());
    }
    else
        data.FlushBits();
    player->SendDirectMessage(&data);
}

void WorldSession::HandleGuildFinderRemoveApplication(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_REMOVE_APPLICATION");

    ObjectGuid guildGuid;

    recvPacket.ReadGuidMask(guildGuid, 7, 5, 4, 1, 6, 3, 2, 0);

    recvPacket.ReadGuidBytes(guildGuid, 6, 3, 7, 1, 2, 0, 5, 4);

    if (!IS_GUILD_GUID(guildGuid))
        return;

    sGuildFinderMgr->RemoveMembershipRequest(GetPlayer()->GetGUIDLow(), GUID_LOPART(guildGuid));
}

// Sent any time a guild master sets an option in the interface and when listing / unlisting his guild
void WorldSession::HandleGuildFinderSetGuildPost(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: Received CMSG_LF_GUILD_SET_GUILD_POST");

    uint32 classRoles = 0;
    uint32 availability = 0;
    uint32 guildInterests =  0;
    uint32 level = 0;

    recvPacket >> level >> availability >> guildInterests >> classRoles;
    // Level sent is zero if untouched, force to any (from interface). Idk why
    if (!level)
        level = ANY_FINDER_LEVEL;

    uint16 length = recvPacket.ReadBits(11);
    bool listed = recvPacket.ReadBit();
    std::string comment = recvPacket.ReadString(length);

    if (!(classRoles & GUILDFINDER_ALL_ROLES) || classRoles > GUILDFINDER_ALL_ROLES)
        return;
    if (!(availability & AVAILABILITY_ALWAYS) || availability > AVAILABILITY_ALWAYS)
        return;
    if (!(guildInterests & ALL_INTERESTS) || guildInterests > ALL_INTERESTS)
        return;
    if (!(level & ALL_GUILDFINDER_LEVELS) || level > ALL_GUILDFINDER_LEVELS)
        return;

    Player* player = GetPlayer();

    if (!player->GetGuildId()) // Player must be in guild
        return;

    if (Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId())) // Player must be guild master
        if (guild->GetLeaderGUID() != player->GetGUID())
            return;

    LFGuildSettings settings(listed, player->GetTeamId(), player->GetGuildId(), classRoles, availability, guildInterests, level, comment);
    sGuildFinderMgr->SetGuildSettings(player->GetGuildId(), settings);
}
