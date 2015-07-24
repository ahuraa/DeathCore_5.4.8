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

#ifndef TRINITY_WAYPOINTMANAGER_H
#define TRINITY_WAYPOINTMANAGER_H

#include <ace/Singleton.h>
#include <ace/Null_Mutex.h>
#include <vector>

struct WaypointData
{
    uint32 id;
    float x, y, z, orientation;
    uint32 delay;
    uint32 event_id;
    bool run;
    uint8 event_chance;
};

typedef std::vector<WaypointData*> WaypointPath;
typedef UNORDERED_MAP<uint32, WaypointPath> WaypointPathContainer;


struct SplineWaypointData
{
	uint8 wp_id;
	float x, y, z;
};

typedef std::vector<SplineWaypointData> SplineWaypointPath;
typedef std::unordered_map<uint8, SplineWaypointPath> SplineWaypointPathContainer;
typedef std::unordered_map<uint32, SplineWaypointPathContainer> CreatureSplineWaypointPathContainer;

class WaypointMgr
{
        friend class ACE_Singleton<WaypointMgr, ACE_Null_Mutex>;

    public:
        // Attempts to reload a single path from database
        void ReloadPath(uint32 id);

        // Loads all paths from database, should only run on startup
        void Load();

        // Returns the path from a given id
        WaypointPath const* GetPath(uint32 id) const
        {
            WaypointPathContainer::const_iterator itr = _waypointStore.find(id);
            if (itr != _waypointStore.end())
                return &itr->second;

            return NULL;
        }


		SplineWaypointPath const* GetSplinePath(uint32 c_entry, uint8 path_id) const
		{
			CreatureSplineWaypointPathContainer::const_iterator itr = m_splineWaypointStore.find(c_entry);

			if (itr != m_splineWaypointStore.end())
			{
				SplineWaypointPathContainer::const_iterator jitr = (*itr).second.find(path_id);

				if (jitr != (*itr).second.end())
					return &jitr->second;
			}

			return NULL;
		}

    private:
        // Only allow instantiation from ACE_Singleton
        WaypointMgr();
        ~WaypointMgr();

        WaypointPathContainer _waypointStore;
		CreatureSplineWaypointPathContainer m_splineWaypointStore;
};

#define sWaypointMgr ACE_Singleton<WaypointMgr, ACE_Null_Mutex>::instance()

#endif
