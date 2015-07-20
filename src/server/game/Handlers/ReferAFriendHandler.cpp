/*
 * Copyright (C) 2013-2015 DeathCore <http://www.noffearrdeathproject.net/>
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2014 MaNGOS <http://getmangos.com/>
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
#include "Player.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Log.h"

void WorldSession::HandleGrantLevel(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GRANT_LEVEL");

    ObjectGuid guid;

    guid[2] = recvData.ReadBit();  // 18
    guid[1] = recvData.ReadBit();  // 17
    guid[5] = recvData.ReadBit();  // 21
    guid[3] = recvData.ReadBit();  // 19
    guid[7] = recvData.ReadBit();  // 23
    guid[4] = recvData.ReadBit();  // 20
    guid[0] = recvData.ReadBit();  // 16
    guid[6] = recvData.ReadBit();  // 22

    recvData.ReadByteSeq(guid[1]);  // 17
    recvData.ReadByteSeq(guid[4]);  // 20
    recvData.ReadByteSeq(guid[2]);  // 18
    recvData.ReadByteSeq(guid[7]);  // 23
    recvData.ReadByteSeq(guid[5]);  // 21
    recvData.ReadByteSeq(guid[3]);  // 19
    recvData.ReadByteSeq(guid[6]);  // 22
    recvData.ReadByteSeq(guid[0]);  // 16
    
    Player* target = ObjectAccessor::GetObjectInWorld(guid, _player);

    // check cheating
    uint8 levels = _player->GetGrantableLevels();
    uint8 error = 0;
    if (!target)
        error = ERR_REFER_A_FRIEND_NO_TARGET;
    else if (levels == 0)
        error = ERR_REFER_A_FRIEND_INSUFFICIENT_GRANTABLE_LEVELS;
    else if (GetRecruiterId() != target->GetSession()->GetAccountId())
        error = ERR_REFER_A_FRIEND_NOT_REFERRED_BY;
    else if (target->GetTeamId() != _player->GetTeamId())
        error = ERR_REFER_A_FRIEND_DIFFERENT_FACTION;
    else if (target->getLevel() >= _player->getLevel())
        error = ERR_REFER_A_FRIEND_TARGET_TOO_HIGH;
    else if (target->getLevel() >= sWorld->getIntConfig(CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL))
        error = ERR_REFER_A_FRIEND_GRANT_LEVEL_MAX_I;
    else if (target->GetGroup() != _player->GetGroup())
        error = ERR_REFER_A_FRIEND_NOT_IN_GROUP;

    if (error)
    {
        WorldPacket data(SMSG_REFER_A_FRIEND_FAILURE, 24);
        data << uint32(error);
        if (error == ERR_REFER_A_FRIEND_NOT_IN_GROUP)
            data << target->GetName();

        SendPacket(&data);
        return;
    }

    
    ObjectGuid oGUID = _player->GetGUID();

    WorldPacket data2(SMSG_PROPOSE_LEVEL_GRANT, 8);
    
    data2.WriteBit(oGUID[6]);  // 22
    data2.WriteBit(oGUID[7]);  // 23
    data2.WriteBit(oGUID[2]);  // 18
    data2.WriteBit(oGUID[5]);  // 21
    data2.WriteBit(oGUID[3]);  // 19
    data2.WriteBit(oGUID[0]);  // 16
    data2.WriteBit(oGUID[1]);  // 17
    data2.WriteBit(oGUID[4]);  // 20

    data2.WriteByteSeq(oGUID[2]);  // 18
    data2.WriteByteSeq(oGUID[5]);  // 21
    data2.WriteByteSeq(oGUID[6]);  // 22
    data2.WriteByteSeq(oGUID[7]);  // 23
    data2.WriteByteSeq(oGUID[1]);  // 17
    data2.WriteByteSeq(oGUID[4]);  // 20
    data2.WriteByteSeq(oGUID[3]);  // 19
    data2.WriteByteSeq(oGUID[0]);  // 16
    
    target->GetSession()->SendPacket(&data2);
}

void WorldSession::HandleAcceptGrantLevel(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_ACCEPT_LEVEL_GRANT");

    ObjectGuid guid;

    guid[2] = recvData.ReadBit();
    guid[7] = recvData.ReadBit();
    guid[5] = recvData.ReadBit();
    guid[4] = recvData.ReadBit();
    guid[3] = recvData.ReadBit();
    guid[0] = recvData.ReadBit();
    guid[1] = recvData.ReadBit();
    guid[6] = recvData.ReadBit();

    recvData.ReadByteSeq(guid[5]);
    recvData.ReadByteSeq(guid[3]);
    recvData.ReadByteSeq(guid[2]);
    recvData.ReadByteSeq(guid[7]);
    recvData.ReadByteSeq(guid[4]);
    recvData.ReadByteSeq(guid[1]);
    recvData.ReadByteSeq(guid[0]);
    recvData.ReadByteSeq(guid[6]);

    Player* other = ObjectAccessor::GetObjectInWorld(guid, _player);
    if (!(other && other->GetSession()))
        return;

    if (GetAccountId() != other->GetSession()->GetRecruiterId())
        return;

    if (other->GetGrantableLevels())
        other->SetGrantableLevels(other->GetGrantableLevels() - 1);
    else
        return;

    _player->GiveLevel(_player->getLevel() + 1);
}
