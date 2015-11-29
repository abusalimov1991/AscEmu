/*
 * AscEmu Framework based on ArcEmu MMORPG Server
 * Copyright (C) 2014-2015 AscEmu Team <http://www.ascemu.org/>
 * Copyright (C) 2008-2012 ArcEmu Team <http://www.ArcEmu.org/>
 * Copyright (C) 2005-2007 Ascent Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "StdAfx.h"


Mutex m_transportGuidGen;
uint32 m_transportGuidMax = 50;

bool Transporter::CreateAsTransporter(uint32 EntryID, const char* Name, int32 Time)
{
    // Lookup GameobjectInfo
    if (!CreateFromProto(EntryID, 0, 0, 0, 0, 0))
        return false;

    // Override these flags to avoid mistakes in proto
    SetFlags(40);
    SetAnimProgress(100);

    //Maybe this would be the perfect way, so there would be no extra checks in Object.cpp:
    //but these fields seems to change often and between server flavours (ArcEmu, Aspire, name another one) - by: VLack aka. VLsoft
    if (pInfo)
        pInfo->type = GAMEOBJECT_TYPE_TRANSPORT;
    else
        LOG_ERROR("Transporter id[%i] name[%s] - can't set GAMEOBJECT_TYPE - it will behave badly!", EntryID, Name);

    m_overrides = GAMEOBJECT_INFVIS | GAMEOBJECT_ONMOVEWIDE; //Make it forever visible on the same map

    // Set period
    m_period = Time;

    // Generate waypoints
    if (!GenerateWaypoints())
        return false;

    // Set position
    SetMapId(m_WayPoints[0].mapid);
    SetPosition(m_WayPoints[0].x, m_WayPoints[0].y, m_WayPoints[0].z, 0);
    SetLevel(m_period);
    // Add to world
    AddToWorld();

    return true;
}

bool FillPathVector(uint32 PathID, TransportPath & Path)
{
    // Store dbc values into current Path array
    Path.Resize(dbcTaxiPathNode.GetNumRows());

    uint32 i = 0;
    for (uint32 j = 0; j < dbcTaxiPathNode.GetNumRows(); j++)
    {
        DBCTaxiPathNode* pathnode = dbcTaxiPathNode.LookupRow(j);
        if (pathnode->path == PathID)
        {
            Path[i].mapid = pathnode->mapid;
            Path[i].x = pathnode->x;
            Path[i].y = pathnode->y;
            Path[i].z = pathnode->z;
            Path[i].actionFlag = pathnode->flags;
            Path[i].delay = pathnode->waittime;
            ++i;
        }
    }

    Path.Resize(i);
    return (i > 0 ? true : false);
}

bool Transporter::GenerateWaypoints()
{
    TransportPath path;
    FillPathVector(GetInfo()->parameter_0, path);

    if (path.Size() == 0) return false;

    std::vector<keyFrame> keyFrames;
    int mapChange = 0;
    for (int i = 1; i < (int)path.Size() - 1; i++)
    {
        if (mapChange == 0)
        {
            if ((path[i].mapid == path[i + 1].mapid))
            {
                keyFrame k(path[i].x, path[i].y, path[i].z, path[i].mapid, path[i].actionFlag, path[i].delay);
                keyFrames.push_back(k);
            }
            else
            {
                mapChange = 1;
            }
        }
        else
        {
            mapChange--;
        }
    }

    int lastStop = -1;
    int firstStop = -1;

    // first cell is arrived at by teleportation :S
    keyFrames[0].distFromPrev = 0;
    if (keyFrames[0].actionflag == 2)
    {
        lastStop = 0;
    }

    // find the rest of the distances between key points
    for (size_t i = 1; i < keyFrames.size(); i++)
    {
        if ((keyFrames[i - 1].actionflag == 1) || (keyFrames[i].mapid != keyFrames[i - 1].mapid))
        {
            keyFrames[i].distFromPrev = 0;
        }
        else
        {
            keyFrames[i].distFromPrev =
                sqrt(pow(keyFrames[i].x - keyFrames[i - 1].x, 2) +
                pow(keyFrames[i].y - keyFrames[i - 1].y, 2) +
                pow(keyFrames[i].z - keyFrames[i - 1].z, 2));
        }
        if (keyFrames[i].actionflag == 2)
        {
            if (firstStop < 0)
                firstStop = (int)i;

            lastStop = (int)i;
        }
    }

    float tmpDist = 0;
    for (int i = 0; i < (int)keyFrames.size(); i++)
    {
        int j = (i + lastStop) % (int)keyFrames.size();
        if (keyFrames[j].actionflag == 2)
            tmpDist = 0;
        else
            tmpDist += keyFrames[j].distFromPrev;
        keyFrames[j].distSinceStop = tmpDist;
    }

    for (int i = int(keyFrames.size()) - 1; i >= 0; i--)
    {
        int j = (i + (firstStop + 1)) % (int)keyFrames.size();
        tmpDist += keyFrames[(j + 1) % keyFrames.size()].distFromPrev;
        keyFrames[j].distUntilStop = tmpDist;
        if (keyFrames[j].actionflag == 2)
            tmpDist = 0;
    }

    for (size_t i = 0; i < keyFrames.size(); i++)
    {
        if (keyFrames[i].distSinceStop < (30 * 30 * 0.5))
            keyFrames[i].tFrom = sqrt(2 * keyFrames[i].distSinceStop);
        else
            keyFrames[i].tFrom = ((keyFrames[i].distSinceStop - (30 * 30 * 0.5f)) / 30) + 30;

        if (keyFrames[i].distUntilStop < (30 * 30 * 0.5))
            keyFrames[i].tTo = sqrt(2 * keyFrames[i].distUntilStop);
        else
            keyFrames[i].tTo = ((keyFrames[i].distUntilStop - (30 * 30 * 0.5f)) / 30) + 30;

        keyFrames[i].tFrom *= 1000;
        keyFrames[i].tTo *= 1000;
    }

    //    for (int i = 0; i < keyFrames.size(); i++) {
    //        sLog.outString("%f, %f, %f, %f, %f, %f, %f", keyFrames[i].x, keyFrames[i].y, keyFrames[i].distUntilStop, keyFrames[i].distSinceStop, keyFrames[i].distFromPrev, keyFrames[i].tFrom, keyFrames[i].tTo);
    //    }

    // Now we're completely set up; we can move along the length of each waypoint at 100 ms intervals
    // speed = max(30, t) (remember x = 0.5s^2, and when accelerating, a = 1 unit/s^2
    int t = 0;
    bool teleport = false;
    if (keyFrames[keyFrames.size() - 1].mapid != keyFrames[0].mapid)
        teleport = true;

    TWayPoint pos(keyFrames[0].mapid, keyFrames[0].x, keyFrames[0].y, keyFrames[0].z, teleport);
    uint32 last_t = 0;
    m_WayPoints[0] = pos;
    t += keyFrames[0].delay * 1000;

    int cM = keyFrames[0].mapid;
    for (size_t i = 0; i < keyFrames.size() - 1; i++)        //
    {
        float d = 0;
        float tFrom = keyFrames[i].tFrom;
        float tTo = keyFrames[i].tTo;

        // keep the generation of all these points; we use only a few now, but may need the others later
        if (((d < keyFrames[i + 1].distFromPrev) && (tTo > 0)))
        {
            while ((d < keyFrames[i + 1].distFromPrev) && (tTo > 0))
            {
                tFrom += 100;
                tTo -= 100;

                if (d > 0)
                {
                    float newX, newY, newZ;
                    newX = keyFrames[i].x + (keyFrames[i + 1].x - keyFrames[i].x) * d / keyFrames[i + 1].distFromPrev;
                    newY = keyFrames[i].y + (keyFrames[i + 1].y - keyFrames[i].y) * d / keyFrames[i + 1].distFromPrev;
                    newZ = keyFrames[i].z + (keyFrames[i + 1].z - keyFrames[i].z) * d / keyFrames[i + 1].distFromPrev;

                    teleport = false;
                    if ((int)keyFrames[i].mapid != cM)
                    {
                        teleport = true;
                        cM = keyFrames[i].mapid;
                    }

                    //                    sLog.outString("T: %d, D: %f, x: %f, y: %f, z: %f", t, d, newX, newY, newZ);
                    TWayPoint pos2(keyFrames[i].mapid, newX, newY, newZ, teleport);
                    if (teleport || ((t - last_t) >= 1000))
                    {
                        m_WayPoints[t] = pos2;
                        last_t = t;
                    }
                }

                if (tFrom < tTo)                            // caught in tFrom dock's "gravitational pull"
                {
                    if (tFrom <= 30000)
                    {
                        d = 0.5f * (tFrom / 1000) * (tFrom / 1000);
                    }
                    else
                    {
                        d = 0.5f * 30 * 30 + 30 * ((tFrom - 30000) / 1000);
                    }
                    d = d - keyFrames[i].distSinceStop;
                }
                else
                {
                    if (tTo <= 30000)
                    {
                        d = 0.5f * (tTo / 1000) * (tTo / 1000);
                    }
                    else
                    {
                        d = 0.5f * 30 * 30 + 30 * ((tTo - 30000) / 1000);
                    }
                    d = keyFrames[i].distUntilStop - d;
                }
                t += 100;
            }
            t -= 100;
        }

        if (keyFrames[i + 1].tFrom > keyFrames[i + 1].tTo)
            t += 100 - ((long)keyFrames[i + 1].tTo % 100);
        else
            t += (long)keyFrames[i + 1].tTo % 100;

        teleport = false;
        if ((keyFrames[i + 1].actionflag == 1) || (keyFrames[i + 1].mapid != keyFrames[i].mapid))
        {
            teleport = true;
            cM = keyFrames[i + 1].mapid;
        }

        TWayPoint pos2(keyFrames[i + 1].mapid, keyFrames[i + 1].x, keyFrames[i + 1].y, keyFrames[i + 1].z, teleport);

        //        sLog.outString("T: %d, x: %f, y: %f, z: %f, t:%d", t, pos.x, pos.y, pos.z, teleport);

        //if (teleport)
        //m_WayPoints[t] = pos;
        if (keyFrames[i + 1].delay > 5)
            pos2.delayed = true;

        m_WayPoints.insert(WaypointMap::value_type(t, pos2));
        last_t = t;

        t += keyFrames[i + 1].delay * 1000;
        //        sLog.outString("------");
    }

    uint32 timer = t;

    mCurrentWaypoint = m_WayPoints.begin();
    //mCurrentWaypoint = GetNextWaypoint();
    mNextWaypoint = GetNextWaypoint();
    m_pathTime = timer;
    m_timer = 0;
    return true;
}

WaypointIterator Transporter::GetNextWaypoint()
{
    WaypointIterator iter = mCurrentWaypoint;
    iter++;
    if (iter == m_WayPoints.end())
        iter = m_WayPoints.begin();
    return iter;
}

uint32 TimeStamp();
void Transporter::UpdatePosition()
{
    if (m_WayPoints.size() <= 1)
        return;

    m_timer = getMSTime() % m_period;

    while (((m_timer - mCurrentWaypoint->first) % m_pathTime) >= ((mNextWaypoint->first - mCurrentWaypoint->first) % m_pathTime))
    {
        /*printf("%s from %u %f %f %f to %u %f %f %f\n", this->GetInfo()->Name,
            mCurrentWaypoint->second.mapid, mCurrentWaypoint->second.x,mCurrentWaypoint->second.y,mCurrentWaypoint->second.z,
            mNextWaypoint->second.mapid, mNextWaypoint->second.x,mNextWaypoint->second.y,mNextWaypoint->second.z);*/

        mCurrentWaypoint = mNextWaypoint;
        mNextWaypoint = GetNextWaypoint();
        if (mCurrentWaypoint->second.mapid != GetMapId() || mCurrentWaypoint->second.teleport)
        {
            passengers.clear();
            TransportPassengers(mCurrentWaypoint->second.mapid, GetMapId(), mCurrentWaypoint->second.x, mCurrentWaypoint->second.y, mCurrentWaypoint->second.z);
            break;
        }
        else{
            SetPosition(mCurrentWaypoint->second.x, mCurrentWaypoint->second.y, mCurrentWaypoint->second.z, m_position.o, false);
            MovePassengers(mCurrentWaypoint->second.x, mCurrentWaypoint->second.y, mCurrentWaypoint->second.z, m_position.o);
        }

        if (mCurrentWaypoint->second.delayed)
        {
            //Transprter Script = sScriptMgr.CreateAIScriptClassForGameObject(GetEntry(), this);
            switch (GetInfo()->display_id)
            {
                case 3015:
                case 7087:
                {
                    PlaySoundToSet(5154);        // ShipDocked LightHouseFogHorn.wav
                }
                break;
                case 3031:
                {
                    PlaySoundToSet(11804);        // ZeppelinDocked    ZeppelinHorn.wav
                }
                break;
                default:
                {
                    PlaySoundToSet(5495);        // BoatDockingWarning    BoatDockedWarning.wav
                }
                break;
            }
            TransportGossip(GetInfo()->display_id);
        }
    }
}

void Transporter::TransportGossip(uint32 route)
{
    if (route == 241)
    {
        if (mCurrentWaypoint->second.mapid)
        {
            Log.Debug("Transporter", "Arrived in Ratchet at %u", m_timer);
        }
        else
        {
            Log.Debug("Transporter", "Arrived in Booty at %u", m_timer);
        }
    }
}

void Transporter::TransportPassengers(uint32 mapid, uint32 oldmap, float x, float y, float z)
{
    sEventMgr.RemoveEvents(this, EVENT_TRANSPORTER_NEXT_WAYPOINT);

    if (mPassengers.size() > 0)
    {
        PassengerIterator itr = mPassengers.begin();
        PassengerIterator it2;

        WorldPacket Pending(SMSG_TRANSFER_PENDING, 12);
        Pending << mapid << GetEntry() << oldmap;

        WorldPacket NewWorld;
        LocationVector v;

        for (; itr != mPassengers.end();)
        {
            it2 = itr;
            ++itr;

            Player* plr = objmgr.GetPlayer(it2->first);
            if (!plr)
            {
                // remove all non players from map
                mPassengers.erase(it2);
                continue;
            }
            if (!plr->GetSession() || !plr->IsInWorld())
                continue;

            v.x = x + plr->GetTransPositionX();
            v.y = y + plr->GetTransPositionY();
            v.z = z + plr->GetTransPositionZ();
            v.o = plr->GetOrientation();

            if (mapid == 530 && !plr->GetSession()->HasFlag(ACCOUNT_FLAG_XPACK_01))
            {
                // player does not have BC content, repop at graveyard
                plr->RepopAtGraveyard(plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetMapId());
                continue;
            }

            if (mapid == 571 && !plr->GetSession()->HasFlag(ACCOUNT_FLAG_XPACK_02))
            {
                plr->RepopAtGraveyard(plr->GetPositionX(), plr->GetPositionY(), plr->GetPositionZ(), plr->GetMapId());
                continue;
            }

            plr->GetSession()->SendPacket(&Pending);
            plr->_Relocate(mapid, v, false, true, 0);

            // Lucky bitch. Do it like on official.
            if (plr->IsDead())
            {
                plr->ResurrectPlayer();
                plr->SetHealth(plr->GetMaxHealth());
                plr->SetPower(POWER_TYPE_MANA, plr->GetMaxPower(POWER_TYPE_MANA));
            }
        }
    }

    // Set our position
    RemoveFromWorld(false);
    SetMapId(mapid);
    SetPosition(x, y, z, m_position.o, false);
    AddToWorld();
}

Transporter::Transporter(uint64 guid) : GameObject(guid)
{
    m_pathTime = 0;
    m_timer = 0;
    m_period = 0;
}

Transporter::~Transporter()
{
    sEventMgr.RemoveEvents(this);
    for (TransportNPCMap::iterator itr = m_npcs.begin(); itr != m_npcs.end(); ++itr)
        delete itr->second;
}

Creature* Transporter::GetCreature(uint32 Guid)
{
    TransportNPCMap::iterator itr = m_npcs.find(Guid);
    if (itr == m_npcs.end())
        return nullptr;
    if (itr->second->IsCreature())
        return static_cast< Creature* >(itr->second);
    else
        return nullptr;
}

void ObjectMgr::LoadTransporters()
{
    Log.Notice("TransporterHandler", "Start loading transport_data");
    {
        const char* loadAllTransportData = "SELECT entry, name, period FROM transport_data";

        QueryResult* result = WorldDatabase.Query(loadAllTransportData);
        if (!result)
        {
            Log.Error("TransporterHandler", "Query failed: %s", loadAllTransportData);
            return;
        }

        uint32 count = 0;
        do
        {
            Field* field = result->Fetch();

            TransporterDataQueryResult dbResult;
            dbResult.entry = field[0].GetUInt32();
            dbResult.name = field[1].GetString();
            dbResult.period = field[2].GetUInt32();

            auto gameobject_info = GameObjectNameStorage.LookupEntry(dbResult.entry);
            if (gameobject_info == nullptr)
            {
                Log.Error("TransporterHandler", "Transporter gameobject %u not available in GameObjectNameStorage!", dbResult.entry);
                continue;
            }

            Transporter* pTransporter = new Transporter((uint64)HIGHGUID_TYPE_TRANSPORTER << 32 | dbResult.entry);
            pTransporter->SetInfo(gameobject_info);
            if (!pTransporter->CreateAsTransporter(dbResult.entry, "", dbResult.period))
            {
                Log.Error("TransporterHandler", "Transporter %s failed creation for some reason.", dbResult.name.c_str());
                delete pTransporter;
            }
            else
            {
                Log.Debug("TransporterHandler", "%s, Entry: %u, Period: %u loaded", dbResult.name.c_str(), dbResult.entry, dbResult.period);
                AddTransport(pTransporter);
                ++count;
            }

        }
        while (result->NextRow());

        delete result;
        Log.Success("TransporterHandler", "%u transporters loaded from table transporter_data", count);
    }

    Log.Notice("TransporterHandler", "Start loading transport_creatures");
    {
        const char* loadTransportPassengers = "SELECT transport_entry, creature_entry, position_x, position_y, position_z, orientation FROM transport_creatures";
        bool success = false;
        QueryResult* result = WorldDatabase.Query(&success, loadTransportPassengers);
        if (!success)
        {
            Log.Error("TransporterHandler", "Query failed: %s", loadTransportPassengers);
            return;
        }

        uint32 count = 0;
        if (result)
        {
            do
            {
                Field* field = result->Fetch();

                uint32 transport_entry = field[0].GetUInt32();
                uint32 creature_entry = field[1].GetUInt32();

                auto transporter = GetTransporterByEntry(transport_entry);
                if (transporter == nullptr)
                {
                    Log.Error("TransporterHandler", "Could not find transporter %u for transport_creatures entry %u", transport_entry, creature_entry);
                    continue;
                }

                TransporterCreaturesQueryResult dbResult;
                dbResult.transport_entry = field[0].GetUInt32();
                dbResult.creature_entry = field[1].GetUInt32();
                dbResult.position_x = field[2].GetFloat();
                dbResult.position_y = field[3].GetFloat();
                dbResult.position_z = field[4].GetFloat();
                dbResult.orientation = field[5].GetFloat();

                transporter->creature_transport_data.push_back(dbResult);

                transporter->AddNPC(dbResult.creature_entry, dbResult.position_x, dbResult.position_y, dbResult.position_z, dbResult.orientation);

                ++count;

            }
            while (result->NextRow());
            delete result;
            Log.Success("TransporterHandler", "%u transport passengers from table transport_creatures loaded.", count);
        }
    }
}

void Transporter::OnPushToWorld()
{
    // Create waypoint event
    sEventMgr.AddEvent(this, &Transporter::UpdatePosition, EVENT_TRANSPORTER_NEXT_WAYPOINT, 100, 0, EVENT_FLAG_DO_NOT_EXECUTE_IN_WORLD_CONTEXT);
}

void Transporter::AddNPC(uint32 Entry, float offsetX, float offsetY, float offsetZ, float offsetO)
{
    uint32 guid;
    m_transportGuidGen.Acquire();
    guid = ++m_transportGuidMax;
    m_transportGuidGen.Release();

    CreatureInfo* inf = CreatureNameStorage.LookupEntry(Entry);
    CreatureProto* proto = CreatureProtoStorage.LookupEntry(Entry);
    if (inf == NULL || proto == NULL)
        return;

    Creature* pCreature = new Creature((uint64)HIGHGUID_TYPE_TRANSPORTER << 32 | guid);
    pCreature->Load(proto, m_position.x, m_position.y, m_position.z);
    pCreature->obj_movement_info.transporter_info.position.x = offsetX;
    pCreature->obj_movement_info.transporter_info.position.y = offsetY;
    pCreature->obj_movement_info.transporter_info.position.z = offsetZ;
    pCreature->obj_movement_info.transporter_info.position.o = offsetO;
    pCreature->obj_movement_info.transporter_info.guid = GetGUID();
    m_npcs.insert(std::make_pair(guid, pCreature));
}

uint32 Transporter::BuildCreateUpdateBlockForPlayer(ByteBuffer* data, Player* target)
{
    uint32 cnt = Object::BuildCreateUpdateBlockForPlayer(data, target);

    // add all the npcs to the packet
    for (TransportNPCMap::iterator itr = m_npcs.begin(); itr != m_npcs.end(); ++itr)
        cnt += itr->second->BuildCreateUpdateBlockForPlayer(data, target);

    return cnt;
}

void Transporter::MovePassengers(float x, float y, float z, float o)
{
    for (TransportNPCMap::iterator itr = m_npcs.begin(); itr != m_npcs.end(); ++itr)
    {
        Object *obj = itr->second;
        obj->SetPosition(x + obj->GetTransPositionX(), y + obj->GetTransPositionY(), z + obj->GetTransPositionZ(), o + obj->GetTransPositionO(), false);
    }

    for (PassengerMap::iterator itr = mPassengers.begin(); itr != mPassengers.end(); ++itr)
    {
        Player* p = itr->second;
        p->SetPosition(x + p->GetTransPositionX(), y + p->GetTransPositionY(), z + p->GetTransPositionZ(), o + p->GetTransPositionO(), false);
    }

    for (std::map< uint64, Object* >::iterator itr = passengers.begin(); itr != passengers.end(); ++itr)
    {
        Object *obj = itr->second;
        obj->SetPosition(x + obj->GetTransPositionX(), y + obj->GetTransPositionY(), z + obj->GetTransPositionZ(), o + obj->GetTransPositionO(), false);
    }
}

void Transporter::AddPassenger(Object *o)
{
    if (o->IsPlayer())
    {
        AddPlayer(static_cast<Player*>(o));
        return;
    }

    passengers[o->GetGUID()] = o;
}

void Transporter::RemovePassenger(Object *o)
{
    if (o->IsPlayer())
    {
        RemovePlayer(static_cast<Player*>(o));
        return;
    }

    passengers.erase(o->GetGUID());
}
