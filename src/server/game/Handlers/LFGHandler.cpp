/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
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

#include "LFGMgr.h"
#include "ObjectMgr.h"
#include "Group.h"
#include "Player.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GameEventMgr.h"

void BuildPlayerLockDungeonBlock(WorldPacket& data, lfg::LfgLockMap const& lock)
{
    data << uint32(lock.size());                           // Size of lock dungeons
    for (lfg::LfgLockMap::const_iterator it = lock.begin(); it != lock.end(); ++it)
    {
        data << uint32(it->first);                         // Dungeon entry (id + type)
        data << uint32(it->second);                        // Lock status
        data << uint32(0);                                 // Unknown 4.2.2
        data << uint32(0);                                 // Unknown 4.2.2
    }
}

void BuildPartyLockDungeonBlock(WorldPacket& data, lfg::LfgLockPartyMap const& lockMap)
{
    data << uint8(lockMap.size());
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
    {
        data << uint64(it->first);                         // Player guid
        BuildPlayerLockDungeonBlock(data, it->second);
    }
}

void WorldSession::HandleLfgJoinOpcode(WorldPacket& recvData)
{
    if (!sLFGMgr->isOptionEnabled(lfg::LFG_OPTION_ENABLE_DUNGEON_FINDER | lfg::LFG_OPTION_ENABLE_RAID_BROWSER) ||
        (GetPlayer()->GetGroup() && GetPlayer()->GetGroup()->GetLeaderGUID() != GetPlayer()->GetGUID() &&
        (GetPlayer()->GetGroup()->GetMembersCount() == MAXGROUPSIZE || !GetPlayer()->GetGroup()->isLFGGroup())))
    {
        recvData.rfinish();
        return;
    }

    uint32 dungeon;
    uint32 unk[3];
    std::string comment;
    uint32 roles;

    recvData >> roles;

    for (int8 i = 0; i < 3; ++i)
    {
        recvData >> unk[i]; // Unknown.
    }

    uint32 length = recvData.ReadBits(9);
    uint32 count  = recvData.ReadBits(24);

    comment = recvData.ReadString(length);

    if (!count) // numDungeons.
    {
        sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_LFG_JOIN [" UI64FMTD "] no dungeons selected", GetPlayer()->GetGUID());
        recvData.rfinish();
        return;
    }

    lfg::LfgDungeonSet newDungeons;
    for (uint32 i = 0; i < count; ++i)
    {
        recvData >> dungeon; // dungeonentry
        sLog->outError(LOG_FILTER_GENERAL, "Dentro %u", dungeon);
        newDungeons.insert((dungeon & 0x00FFFFFF));       // remove the type from the dungeon entry
    }

    sLog->outError(LOG_FILTER_GENERAL, "CMSG_LFG_JOIN [" UI64FMTD "] roles: %u, Dungeons: %u, Comment: %s", GetPlayer()->GetGUID(), roles, uint8(newDungeons.size()), comment.c_str());
    sLFGMgr->JoinLfg(GetPlayer(), uint8(roles), newDungeons, comment);
}

void WorldSession::HandleLfgLeaveOpcode(WorldPacket&  /*recvData*/)
{
    Group* group = GetPlayer()->GetGroup();
    uint64 guid = GetPlayer()->GetGUID();
    uint64 gguid = group ? group->GetGUID() : guid;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LEAVE %s in group: %u",
        GetPlayerInfo().c_str(), group ? 1 : 0);

    // Check cheating - only leader can leave the queue
    if (!group || group->GetLeaderGUID() == GetPlayer()->GetGUID())
        sLFGMgr->LeaveLfg(gguid);
}

void WorldSession::HandleLfgProposalResultOpcode(WorldPacket& recvData)
{
    uint32 lfgGroupID;                                      // Internal lfgGroupID
    bool accept;                                           // Accept to join?
    recvData >> lfgGroupID;
    recvData >> accept;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_PROPOSAL_RESULT %s proposal: %u accept: %u",
        GetPlayerInfo().c_str(), lfgGroupID, accept ? 1 : 0);
    sLFGMgr->UpdateProposal(lfgGroupID, GetPlayer()->GetGUID(), accept);
}

void WorldSession::HandleLfgSetRolesOpcode(WorldPacket& recvData)
{
    uint8 roles;
    recvData >> roles;                                     // Player Group Roles
    uint64 guid = GetPlayer()->GetGUID();
    Group* group = GetPlayer()->GetGroup();
    if (!group)
    {
        sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_ROLES %s Not in group",
            GetPlayerInfo().c_str());
        return;
    }
    uint64 gguid = group->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_ROLES: Group %u, Player %s, Roles: %u",
        GUID_LOPART(gguid), GetPlayerInfo().c_str(), roles);
    sLFGMgr->UpdateRoleCheck(gguid, guid, roles);
}

void WorldSession::HandleLfgSetCommentOpcode(WorldPacket&  recvData)
{
    std::string comment;
    recvData >> comment;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_COMMENT %s comment: %s",
        GetPlayerInfo().c_str(), comment.c_str());

    sLFGMgr->SetComment(GetPlayer()->GetGUID(), comment);
}

void WorldSession::HandleLfgSetBootVoteOpcode(WorldPacket& recvData)
{
    bool agree;                                            // Agree to kick player
    recvData >> agree;

    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_SET_BOOT_VOTE %s agree: %u",
        GetPlayerInfo().c_str(), agree ? 1 : 0);
    sLFGMgr->UpdateBoot(guid, agree);
}

void WorldSession::HandleLfgTeleportOpcode(WorldPacket& recvData)
{
    bool out;
    recvData >> out;

    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_TELEPORT %s out: %u",
        GetPlayerInfo().c_str(), out ? 1 : 0);
    sLFGMgr->TeleportPlayer(GetPlayer(), out, true);
}

void WorldSession::HandleLfgPlayerLockInfoRequestOpcode(WorldPacket& /*recvData*/)
{
    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_NETWORKIO, "CMSG_LFD_PLAYER_LOCK_INFO_REQUEST [" UI64FMTD "]", guid);

    // Get Random dungeons that can be done at a certain level and expansion
    // FIXME - Should return seasonals (when not disabled)
    uint8 level = GetPlayer()->getLevel();
    uint8 expansion = GetPlayer()->GetSession()->Expansion();
    lfg::LfgDungeonSet /*const&*/ randomDungeons = sLFGMgr->GetRandomAndSeasonalDungeons(level, expansion);
    for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
    {
        LFGDungeonEntry const* dungeon = sLFGDungeonStore.LookupEntry(i);
        if (dungeon && dungeon->type == 6 && dungeon->expansion <= expansion && dungeon->minlevel <= level && level <= dungeon->maxlevel)
            randomDungeons.insert(dungeon->Entry());
        // Dungeons Seleccionables con el evento en el server correspondiente. (En Dungeon Finder)
        if (dungeon && dungeon->grouptype == 11 && dungeon->expansion <= expansion && dungeon->minlevel <= level && level <= dungeon->maxlevel)
        {
            QueryResult result = WorldDatabase.Query("SELECT dungeonId, eventEntry FROM lfg_dungeon_event");

            if (!result)
                return;

            Field* fields = NULL;
            do
            {
                fields = result->Fetch();
                uint32 dungeonId = fields[0].GetUInt32();
                uint32 eventEntry = fields[1].GetUInt32();
                if (dungeonId != dungeon->ID)
                    continue;

                if (eventEntry && sGameEventMgr->IsActiveEvent(eventEntry))
                    randomDungeons.insert(dungeon->Entry());

            }
            while (result->NextRow());
        }
    }

    // Get player locked Dungeons
    lfg::LfgLockMap const& lock = sLFGMgr->GetLockedDungeons(guid);
    uint32 rsize = uint32(randomDungeons.size());
    uint32 lsize = uint32(lock.size());

    sLog->outDebug(LOG_FILTER_NETWORKIO, "SMSG_LFG_PLAYER_INFO [" UI64FMTD "]", guid);
    WorldPacket data(SMSG_LFG_PLAYER_INFO, 1 + rsize * (4 + 1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4) + 4 + lsize * (1 + 4 + 4 + 4 + 4 + 1 + 4 + 4 + 4));

    data << uint8(randomDungeons.size());                  // Random Dungeon count
    for (lfg::LfgDungeonSet::const_iterator it = randomDungeons.begin(); it != randomDungeons.end(); ++it)
    {
        data << uint32(*it);                               // Dungeon Entry (id + type)
        lfg::LfgReward const* reward = sLFGMgr->GetRandomDungeonReward(*it, level);
        Quest const* qRew = NULL;
        uint8 done = 0;
        if (reward)
        {
            qRew = sObjectMgr->GetQuestTemplate(reward->firstQuest);
            if (qRew)
            {
                done = GetPlayer()->CanRewardQuest(qRew, false);
                if (done)
                    qRew = sObjectMgr->GetQuestTemplate(reward->otherQuest);
            }
        }

        if (qRew)
        {
            data << uint8(done);                                        //First Completion
            data << uint32(0);                                          //Base Money
            data << uint32(0);                                          //Base XP
            data << uint32(0);                                          //Variable Money
            data << uint32(0);                                          //Variable XP

            for(int j = 0; j < 9; j++)                                  //Unk 1,2,3,4,5,6,7,8,8.1
                data << uint32(0);

            data << uint8(0);                                           //Unk 9

            // 4.3.4 Loot Slot Info
            for(int j = 0; j < 3; j++)                                  //Unk 1 Array (index 0,1,2)
                data << uint32(0);

            data << uint32(qRew->GetRewOrReqMoney());                   //Unk 10 - Money rew
            data << uint32(qRew->XPValue(GetPlayer()));                 //Unk 11 - Exp rew

            if (qRew->GetRewItemsCount())
            {
                data << uint8(qRew->GetRewItemsCount());
                ItemTemplate const* iProto = NULL;
                for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
                {
                    if(!qRew->RewardItemId[i])
                        continue;

                    iProto = sObjectMgr->GetItemTemplate(qRew->RewardItemId[i]);

                    data << uint32(qRew->RewardItemId[i]);
                    data << uint32(iProto ? iProto->DisplayInfoID : 0);
                    data << uint32(qRew->RewardItemIdCount[i]);

                    data << uint8(iProto ? 0 : 1); // Is Currency
                }
            }
            if (qRew->GetRewCurrencyCount())
            {
                data << uint8(qRew->GetRewCurrencyCount());             //Reward Item/Currency Count
                for(uint8 i = 0; i < 4; i++)
                {
                    if(!qRew->RewardCurrencyId[i])
                        continue;

                    data << uint32(qRew->RewardCurrencyId[i]);          //Reward Item Or Currency Id
                    data << uint32(0);                                  //Reward Item Display ID
                    data << uint32(qRew->RewardCurrencyCount[i]*100);   //Reward Item Stack Count
                    data << uint8(1);                                   // Is Currency
                }
            }
        }
        else
        {
            data << uint8(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);
            data << uint32(0);

            // New 4.3.4 15595
            for(int j = 0; j < 8; j++)
                data << uint32(1);

            data << uint8(1);

            // 4.3.4 Loot Slot Info
            for(int j = 0; j < 3; j++)
                data << uint32(0);

            data << uint32(1);
            data << uint32(1);

            data << uint8(0);
        }
    }
    BuildPlayerLockDungeonBlock(data, lock);
    SendPacket(&data);
}


void WorldSession::HandleLfgPartyLockInfoRequestOpcode(WorldPacket&  /*recvData*/)
{
    uint64 guid = GetPlayer()->GetGUID();
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_PARTY_LOCK_INFO_REQUEST %s", GetPlayerInfo().c_str());

    Group* group = GetPlayer()->GetGroup();
    if (!group)
        return;

    // Get the locked dungeons of the other party members
    lfg::LfgLockPartyMap lockMap;
    for (GroupReference* itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
    {
        Player* plrg = itr->getSource();
        if (!plrg)
            continue;

        uint64 pguid = plrg->GetGUID();
        if (pguid == guid)
            continue;

        lockMap[pguid] = sLFGMgr->GetLockedDungeons(pguid);
    }

    uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = lockMap.begin(); it != lockMap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4 + 4 + 4);

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PARTY_INFO %s", GetPlayerInfo().c_str());
    WorldPacket data(SMSG_LFG_PARTY_INFO, 1 + size);
    BuildPartyLockDungeonBlock(data, lockMap);
    SendPacket(&data);
}

void WorldSession::HandleLfrJoinOpcode(WorldPacket& recvData)
{
    uint32 entry;                                          // Raid id to search
    recvData >> entry;
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LFR_JOIN %s dungeon entry: %u",
        GetPlayerInfo().c_str(), entry);
    //SendLfrUpdateListOpcode(entry);
}

void WorldSession::HandleLfrLeaveOpcode(WorldPacket& recvData)
{
    uint32 dungeonId;                                      // Raid id queue to leave
    recvData >> dungeonId;
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_LFR_LEAVE %s dungeonId: %u",
        GetPlayerInfo().c_str(), dungeonId);
    //sLFGMgr->LeaveLfr(GetPlayer(), dungeonId);
}

void WorldSession::HandleLfgGetStatus(WorldPacket& /*recvData*/)
{
    sLog->outDebug(LOG_FILTER_LFG, "CMSG_LFG_GET_STATUS %s", GetPlayerInfo().c_str());

    uint64 guid = GetPlayer()->GetGUID();
    lfg::LfgUpdateData updateData = sLFGMgr->GetLfgStatus(guid);

    if (GetPlayer()->GetGroup())
    {
        SendLfgUpdateParty(updateData);
        updateData.dungeons.clear();
        SendLfgUpdatePlayer(updateData);
    }
    else
    {
        SendLfgUpdatePlayer(updateData);
        updateData.dungeons.clear();
        SendLfgUpdateParty(updateData);
    }
}

void WorldSession::SendLfgUpdateStatus(lfg::LfgUpdateData const& updateData)
{
    uint32 queueId = GetPlayer()->GetTeam();
    uint64 guid = GetPlayer()->GetGUID();
    //ObjectGuid pguid = guid;

    uint8 pguid[8];
    memcpy(pguid, &guid, 8); // Fu

    bool queued = false;
    bool extrainfo = false;
    bool join = false;

    switch (updateData.updateType)
    {
    case lfg::LFG_UPDATETYPE_JOIN_QUEUE:
    case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:
        join = true;
        queued = true;
        extrainfo = true;
        break;
    case lfg::LFG_UPDATETYPE_REMOVED_FROM_QUEUE:
        extrainfo = true;
        queued = false;
        join = false;
        break;
        //case LFG_UPDATETYPE_CLEAR_LOCK_LIST: // TODO: Sometimes has extrainfo - Check ocurrences...
    case lfg::LFG_UPDATETYPE_PROPOSAL_BEGIN:
        join = true;
        queued = false;
        extrainfo = true;
        break;
    default:
        break;
    }

    uint8 size = uint8(updateData.dungeons.size());

    sLog->outError(LOG_FILTER_GENERAL,"updateData.dungeons.size() = %u, getMSTime() = %u", uint8(updateData.dungeons.size()), getMSTime());

    WorldPacket data(SMSG_LFG_UPDATE_STATUS);

    data.WriteBit(pguid[1]);

    data.WriteBit(0); // Unk Bit 65

    data.WriteBits(size, 24);

    data.WriteBit(pguid[6]);

    data.WriteBit(join ? 1 : 0); // Joined

    data.WriteBits(updateData.comment.length(), 9);

    data.WriteBit(pguid[4]);
    data.WriteBit(pguid[7]);
    data.WriteBit(pguid[2]);

    data.WriteBit(join ? 1 : 0); // LFGJoined

    data.WriteBit(pguid[0]);
    data.WriteBit(pguid[3]);
    data.WriteBit(pguid[5]);

    data.WriteBit(queued);
    data << uint8(0); // Unk Byte 64

    data.WriteString(updateData.comment);
    data << uint32(queueId); // Queue Id
    data << uint32(getMSTime());

    data.WriteByteSeq(pguid[6]);

    data << uint8(0); // Unk Byte
    data << uint8(0); // Unk Byte
    data << uint8(0); // Unk Byte

    data.WriteByteSeq(pguid[1]);
    data.WriteByteSeq(pguid[2]);
    data.WriteByteSeq(pguid[4]);
    data.WriteByteSeq(pguid[3]);
    data.WriteByteSeq(pguid[5]);
    data.WriteByteSeq(pguid[0]);

    data << uint32(3);

    data.WriteByteSeq(pguid[7]);

    if (size)
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
        {
            data << uint32(*it);
            sLog->outError(LOG_FILTER_GENERAL, "dungeon = %u", (*it));
        }


    SendPacket(&data);
}

void WorldSession::SendLfgUpdatePlayer(lfg::LfgUpdateData const& updateData)
{
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_JOIN_QUEUE:
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:
            queued = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_UPDATE_PLAYER %s updatetype: %u",
        GetPlayerInfo().c_str(), updateData.updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PLAYER, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgUpdateParty(const lfg::LfgUpdateData& updateData)
{
    bool join = false;
    bool queued = false;
    uint8 size = uint8(updateData.dungeons.size());

    switch (updateData.updateType)
    {
        case lfg::LFG_UPDATETYPE_ADDED_TO_QUEUE:                // Rolecheck Success
            queued = true;
            // no break on purpose
        case lfg::LFG_UPDATETYPE_PROPOSAL_BEGIN:
            join = true;
            break;
        case lfg::LFG_UPDATETYPE_UPDATE_STATUS:
            join = updateData.state != lfg::LFG_STATE_ROLECHECK && updateData.state != lfg::LFG_STATE_NONE;
            queued = updateData.state == lfg::LFG_STATE_QUEUED;
            break;
        default:
            break;
    }

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_UPDATE_PARTY %s updatetype: %u",
        GetPlayerInfo().c_str(), updateData.updateType);
    WorldPacket data(SMSG_LFG_UPDATE_PARTY, 1 + 1 + (size > 0 ? 1 : 0) * (1 + 1 + 1 + 1 + 1 + size * 4 + updateData.comment.length()));
    data << uint8(updateData.updateType);                  // Lfg Update type
    data << uint8(size > 0);                               // Extra info
    if (size)
    {
        data << uint8(join);                               // LFG Join
        data << uint8(queued);                             // Join the queue
        data << uint8(0);                                  // unk - Always 0
        data << uint8(0);                                  // unk - Always 0
        for (uint8 i = 0; i < 3; ++i)
            data << uint8(0);                              // unk - Always 0

        data << uint8(size);
        for (lfg::LfgDungeonSet::const_iterator it = updateData.dungeons.begin(); it != updateData.dungeons.end(); ++it)
            data << uint32(*it);
        data << updateData.comment;
    }
    SendPacket(&data);
}

void WorldSession::SendLfgRoleChosen(uint64 guid, uint8 roles)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_ROLE_CHOSEN %s guid: %u roles: %u",
        GetPlayerInfo().c_str(), GUID_LOPART(guid), roles);

    WorldPacket data(SMSG_LFG_ROLE_CHOSEN, 8 + 1 + 4);
    data << uint64(guid);                                  // Guid
    data << uint8(roles > 0);                              // Ready
    data << uint32(roles);                                 // Roles
    SendPacket(&data);
}

void WorldSession::SendLfgRoleCheckUpdate(lfg::LfgRoleCheck const& roleCheck)
{
    lfg::LfgDungeonSet dungeons;
    if (roleCheck.rDungeonId)
        dungeons.insert(roleCheck.rDungeonId);
    else
        dungeons = roleCheck.dungeons;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_ROLE_CHECK_UPDATE %s", GetPlayerInfo().c_str());
    WorldPacket data(SMSG_LFG_ROLE_CHECK_UPDATE, 4 + 1 + 1 + dungeons.size() * 4 + 1 + roleCheck.roles.size() * (8 + 1 + 4 + 1));

    data << uint32(roleCheck.state);                       // Check result
    data << uint8(roleCheck.state == lfg::LFG_ROLECHECK_INITIALITING);
    data << uint8(dungeons.size());                        // Number of dungeons
    if (!dungeons.empty())
        for (lfg::LfgDungeonSet::iterator it = dungeons.begin(); it != dungeons.end(); ++it)
            data << uint32(sLFGMgr->GetLFGDungeonEntry(*it)); // Dungeon

    data << uint8(roleCheck.roles.size());                 // Players in group
    if (!roleCheck.roles.empty())
    {
        // Leader info MUST be sent 1st :S
        uint64 guid = roleCheck.leader;
        uint8 roles = roleCheck.roles.find(guid)->second;
        data << uint64(guid);                              // Guid
        data << uint8(roles > 0);                          // Ready
        data << uint32(roles);                             // Roles
        Player* player = ObjectAccessor::FindPlayer(guid);
        data << uint8(player ? player->getLevel() : 0);    // Level

        for (lfg::LfgRolesMap::const_iterator it = roleCheck.roles.begin(); it != roleCheck.roles.end(); ++it)
        {
            if (it->first == roleCheck.leader)
                continue;

            guid = it->first;
            roles = it->second;
            data << uint64(guid);                          // Guid
            data << uint8(roles > 0);                      // Ready
            data << uint32(roles);                         // Roles
            player = ObjectAccessor::FindPlayer(guid);
            data << uint8(player ? player->getLevel() : 0);// Level
        }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgJoinResult(lfg::LfgJoinResultData const& joinData)
{
    uint32 queueId = GetPlayer()->GetTeam();
    uint32 size = 0;
    ObjectGuid playerGuid = GetPlayer()->GetGUID();

    for (lfg::LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4 + 4 + 4);

    sLog->outError(LOG_FILTER_GENERAL, "SMSG_LFG_JOIN_RESULT [" UI64FMTD "] checkResult: %u checkValue: %u", GetPlayer()->GetGUID(), joinData.result, joinData.state);
    WorldPacket data(SMSG_LFG_JOIN_RESULT);

    data << uint32(3);                                      //Unk_UInt32_1
    data << uint8(joinData.result);                         //Check Result
    data << uint32(queueId);
    data << uint8(joinData.state);                          // state
    // data << uint32(sLFGMgr->GetJoinedTime(GetPlayer()->GetGUID()));
    data << uint32(100);

    data.WriteBit(playerGuid[2]);
    data.WriteBit(playerGuid[7]);
    data.WriteBit(playerGuid[3]);
    data.WriteBit(playerGuid[0]);

    data.WriteBits(joinData.lockmap.size(),24);

    for(lfg::LfgLockPartyMap::const_iterator itr = joinData.lockmap.begin(); itr != joinData.lockmap.end(); ++itr)
    {
        ObjectGuid playerGuid1 = itr->first;
        data.WriteBit(playerGuid1[7]);
        data.WriteBit(playerGuid1[5]);
        data.WriteBit(playerGuid1[3]);
        data.WriteBit(playerGuid1[6]);
        data.WriteBit(playerGuid1[0]);
        data.WriteBit(playerGuid1[2]);
        data.WriteBit(playerGuid1[4]);
        data.WriteBit(playerGuid1[1]);
        data.WriteBits(itr->second.size(),22);
    }

    data.WriteBit(playerGuid[4]);
    data.WriteBit(playerGuid[5]);
    data.WriteBit(playerGuid[1]);
    data.WriteBit(playerGuid[6]);

    data.FlushBits();

    for(lfg::LfgLockPartyMap::const_iterator itr = joinData.lockmap.begin(); itr != joinData.lockmap.end(); ++itr)
    {
        for (lfg::LfgLockMap::const_iterator itr1 = itr->second.begin(); itr1 != itr->second.end(); ++itr1)
        {
            data << uint32(itr1->first);                         // Dungeon entry (id + type)
            data << uint32(itr1->second);                        // Lock status
            data << uint32(0);                                   // needed ILvL
            data << uint32(0);                                   // player ILvL
        }

        ObjectGuid playerGuid1 = itr->first;

        data.WriteByteSeq(playerGuid1[2]);
        data.WriteByteSeq(playerGuid1[5]);
        data.WriteByteSeq(playerGuid1[1]);
        data.WriteByteSeq(playerGuid1[0]);
        data.WriteByteSeq(playerGuid1[4]);
        data.WriteByteSeq(playerGuid1[3]);
        data.WriteByteSeq(playerGuid1[6]);
        data.WriteByteSeq(playerGuid1[7]);
    }

    data.WriteByteSeq(playerGuid[1]);
    data.WriteByteSeq(playerGuid[7]);
    data.WriteByteSeq(playerGuid[3]);
    data.WriteByteSeq(playerGuid[5]);
    data.WriteByteSeq(playerGuid[0]);
    data.WriteByteSeq(playerGuid[7]);
    data.WriteByteSeq(playerGuid[2]);
    data.WriteByteSeq(playerGuid[6]);

    SendPacket(&data);
    return;

/*     uint32 size = 0;
    for (lfg::LfgLockPartyMap::const_iterator it = joinData.lockmap.begin(); it != joinData.lockmap.end(); ++it)
        size += 8 + 4 + uint32(it->second.size()) * (4 + 4 + 4 + 4);

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_JOIN_RESULT %s checkResult: %u checkValue: %u",
        GetPlayerInfo().c_str(), joinData.result, joinData.state);

    WorldPacket data(SMSG_LFG_JOIN_RESULT, 4 + 4 + size);
    data << uint32(joinData.result);                       // Check Result
    data << uint32(joinData.state);                        // Check Value
    if (!joinData.lockmap.empty())
        BuildPartyLockDungeonBlock(data, joinData.lockmap);
    SendPacket(&data);*/
}

void WorldSession::SendLfgQueueStatus(lfg::LfgQueueStatusData const& queueData)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_QUEUE_STATUS %s dungeon: %u, waitTime: %d, "
        "avgWaitTime: %d, waitTimeTanks: %d, waitTimeHealer: %d, waitTimeDps: %d, "
        "queuedTime: %u, tanks: %u, healers: %u, dps: %u",
        GetPlayerInfo().c_str(), queueData.dungeonId, queueData.waitTime, queueData.waitTimeAvg,
        queueData.waitTimeTank, queueData.waitTimeHealer, queueData.waitTimeDps,
        queueData.queuedTime, queueData.tanks, queueData.healers, queueData.dps);

    ObjectGuid pguid = GetPlayer()->GetGUID();
 
    WorldPacket data(SMSG_LFG_QUEUE_STATUS);
 
    data.WriteBit(pguid[3]);
    data.WriteBit(pguid[2]);
    data.WriteBit(pguid[0]);
    data.WriteBit(pguid[6]);
    data.WriteBit(pguid[5]);
    data.WriteBit(pguid[7]);
    data.WriteBit(pguid[1]);
    data.WriteBit(pguid[4]);
 
    data.WriteByteSeq(pguid[0]);
 
    data << uint8(queueData.tanks);
    data << uint32(queueData.waitTimeTank);                         // Wait For Tank Time
 
    data << uint8(queueData.healers);
    data << uint32(queueData.waitTimeHealer);                       // Wait For Healer Time
 
    data << uint8(queueData.dps);
    data << uint32(queueData.waitTimeDps);                          // Wait For Damage Time
 
    data.WriteByteSeq(pguid[4]);
    data.WriteByteSeq(pguid[6]);
 
    data << int32(queueData.waitTimeAvg);                           // Average Wait time
    data << int32(queueData.queuedTime);
    data << uint32(queueData.dungeonId);                            // Dungeon
    data << uint32(queueData.queuedTime);                           // Wait Time
 
    data.WriteByteSeq(pguid[5]);
    data.WriteByteSeq(pguid[7]);
    data.WriteByteSeq(pguid[3]);
 
    data << uint32(0);                                              // Queue ID
 
    data.WriteByteSeq(pguid[1]);
    data.WriteByteSeq(pguid[2]);
 
    data << int32(queueData.waitTime);
    data << uint32(3);
 
    SendPacket(&data);
/*
    WorldPacket data(SMSG_LFG_QUEUE_STATUS, 4 + 4 + 4 + 4 + 4 +4 + 1 + 1 + 1 + 4);
    data << uint32(queueData.dungeonId);                   // Dungeon
    data << int32(queueData.waitTimeAvg);                  // Average Wait time
    data << int32(queueData.waitTime);                     // Wait Time
    data << int32(queueData.waitTimeTank);                 // Wait Tanks
    data << int32(queueData.waitTimeHealer);               // Wait Healers
    data << int32(queueData.waitTimeDps);                  // Wait Dps
    data << uint8(queueData.tanks);                        // Tanks needed
    data << uint8(queueData.healers);                      // Healers needed
    data << uint8(queueData.dps);                          // Dps needed
    data << uint32(queueData.queuedTime);                  // Player wait time in queue
    SendPacket(&data);*/
}

void WorldSession::SendLfgPlayerReward(lfg::LfgPlayerRewardData const& rewardData)
{
    if (!rewardData.rdungeonEntry || !rewardData.sdungeonEntry || !rewardData.quest)
        return;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PLAYER_REWARD %s rdungeonEntry: %u, sdungeonEntry: %u, done: %u",
        GetPlayerInfo().c_str(), rewardData.rdungeonEntry, rewardData.sdungeonEntry, rewardData.done);

    uint8 itemNum = rewardData.quest->GetRewItemsCount();

    WorldPacket data(SMSG_LFG_PLAYER_REWARD, 4 + 4 + 1 + 4 + 4 + 4 + 4 + 4 + 1 + itemNum * (4 + 4 + 4));
    data << uint32(rewardData.rdungeonEntry);              // Random Dungeon Finished
    data << uint32(rewardData.sdungeonEntry);              // Dungeon Finished
    data << uint8(rewardData.done);
    data << uint32(1);
    data << uint32(rewardData.quest->GetRewOrReqMoney());
    data << uint32(rewardData.quest->XPValue(GetPlayer()));
    data << uint32(0);
    data << uint32(0);
    data << uint8(itemNum);
    if (itemNum)
    {
        for (uint8 i = 0; i < QUEST_REWARDS_COUNT; ++i)
            if (uint32 itemId = rewardData.quest->RewardItemId[i])
            {
                ItemTemplate const* item = sObjectMgr->GetItemTemplate(itemId);
                data << uint32(itemId);
                data << uint32(item ? item->DisplayInfoID : 0);
                data << uint32(rewardData.quest->RewardItemIdCount[i]);
            }
    }
    SendPacket(&data);
}

void WorldSession::SendLfgBootProposalUpdate(lfg::LfgPlayerBoot const& boot)
{
    uint64 guid = GetPlayer()->GetGUID();
    lfg::LfgAnswer playerVote = boot.votes.find(guid)->second;
    uint8 votesNum = 0;
    uint8 agreeNum = 0;
    uint32 secsleft = uint8((boot.cancelTime - time(NULL)) / 1000);
    for (lfg::LfgAnswerContainer::const_iterator it = boot.votes.begin(); it != boot.votes.end(); ++it)
    {
        if (it->second != lfg::LFG_ANSWER_PENDING)
        {
            ++votesNum;
            if (it->second == lfg::LFG_ANSWER_AGREE)
                ++agreeNum;
        }
    }
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_BOOT_PROPOSAL_UPDATE %s inProgress: %u - "
        "didVote: %u - agree: %u - victim: %u votes: %u - agrees: %u - left: %u - "
        "needed: %u - reason %s",
        GetPlayerInfo().c_str(), uint8(boot.inProgress), uint8(playerVote != lfg::LFG_ANSWER_PENDING),
        uint8(playerVote == lfg::LFG_ANSWER_AGREE), GUID_LOPART(boot.victim), votesNum, agreeNum,
        secsleft, lfg::LFG_GROUP_KICK_VOTES_NEEDED, boot.reason.c_str());
    WorldPacket data(SMSG_LFG_BOOT_PROPOSAL_UPDATE, 1 + 1 + 1 + 1 + 8 + 4 + 4 + 4 + 4 + boot.reason.length());
    data << uint8(boot.inProgress);                        // Vote in progress
    data << uint8(playerVote != lfg::LFG_ANSWER_PENDING);       // Did Vote
    data << uint8(playerVote == lfg::LFG_ANSWER_AGREE);         // Agree
    data << uint8(0);                                      // Unknown 4.2.2
    data << uint64(boot.victim);                           // Victim GUID
    data << uint32(votesNum);                              // Total Votes
    data << uint32(agreeNum);                              // Agree Count
    data << uint32(secsleft);                              // Time Left
    data << uint32(lfg::LFG_GROUP_KICK_VOTES_NEEDED);      // Needed Votes
    data << boot.reason.c_str();                           // Kick reason
    SendPacket(&data);
}

void WorldSession::SendLfgUpdateProposal(lfg::LfgProposal const& proposal)
{
    uint64 guid = GetPlayer()->GetGUID();
    uint64 gguid = proposal.players.find(guid)->second.group;
    bool silent = !proposal.isNew && gguid == proposal.group;
    uint32 dungeonEntry = proposal.dungeonId;

    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_PROPOSAL_UPDATE %s state: %u",
        GetPlayerInfo().c_str(), proposal.state);

    // show random dungeon if player selected random dungeon and it's not lfg group
    if (!silent)
    {
        lfg::LfgDungeonSet const& playerDungeons = sLFGMgr->GetSelectedDungeons(guid);
        if (playerDungeons.find(proposal.dungeonId) == playerDungeons.end())
            dungeonEntry = (*playerDungeons.begin());
    }

    dungeonEntry = sLFGMgr->GetLFGDungeonEntry(dungeonEntry);

    WorldPacket data(SMSG_LFG_PROPOSAL_UPDATE, 4 + 1 + 4 + 4 + 1 + 1 + proposal.players.size() * (4 + 1 + 1 + 1 + 1 +1));
    data << uint32(dungeonEntry);                          // Dungeon
    data << uint8(proposal.state);                         // Proposal state
    data << uint32(proposal.id);                           // Proposal ID
    data << uint32(proposal.encounters);                   // encounters done
    data << uint8(silent);                                 // Show proposal window
    data << uint8(proposal.players.size());                // Group size

    for (lfg::LfgProposalPlayerContainer::const_iterator it = proposal.players.begin(); it != proposal.players.end(); ++it)
    {
        lfg::LfgProposalPlayer const& player = it->second;
        data << uint32(player.role);                       // Role
        data << uint8(it->first == guid);                  // Self player
        if (!player.group)                                 // Player not it a group
        {
            data << uint8(0);                              // Not in dungeon
            data << uint8(0);                              // Not same group
        }
        else
        {
            data << uint8(player.group == proposal.group); // In dungeon (silent)
            data << uint8(player.group == gguid);          // Same Group than player
        }
        data << uint8(player.accept != lfg::LFG_ANSWER_PENDING);// Answered
        data << uint8(player.accept == lfg::LFG_ANSWER_AGREE);  // Accepted
    }
    SendPacket(&data);
}

void WorldSession::SendLfgLfrList(bool update)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_LFR_LIST %s update: %u",
        GetPlayerInfo().c_str(), update ? 1 : 0);
    WorldPacket data(SMSG_LFG_UPDATE_SEARCH, 1);
    data << uint8(update);                                 // In Lfg Queue?
    SendPacket(&data);
}

void WorldSession::SendLfgDisabled()
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_DISABLED %s", GetPlayerInfo().c_str());
    WorldPacket data(SMSG_LFG_DISABLED, 0);
    SendPacket(&data);
}

void WorldSession::SendLfgOfferContinue(uint32 dungeonEntry)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_OFFER_CONTINUE %s dungeon entry: %u",
        GetPlayerInfo().c_str(), dungeonEntry);
    WorldPacket data(SMSG_LFG_OFFER_CONTINUE, 4);
    data << uint32(dungeonEntry);
    SendPacket(&data);
}

void WorldSession::SendLfgTeleportError(uint8 err)
{
    sLog->outDebug(LOG_FILTER_LFG, "SMSG_LFG_TELEPORT_DENIED %s reason: %u",
        GetPlayerInfo().c_str(), err);
    WorldPacket data(SMSG_LFG_TELEPORT_DENIED, 4);
    data << uint32(err);                                   // Error
    SendPacket(&data);
}

/*
void WorldSession::SendLfrUpdateListOpcode(uint32 dungeonEntry)
{
    sLog->outDebug(LOG_FILTER_PACKETIO, "SMSG_LFG_UPDATE_LIST %s dungeon entry: %u",
        GetPlayerInfo().c_str(), dungeonEntry);
    WorldPacket data(SMSG_LFG_UPDATE_LIST);
    SendPacket(&data);
}
*/
