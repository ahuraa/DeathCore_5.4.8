/*
 * Copyright (C) 2013-2015 DeathCore <http://www.noffearrdeathproject.net/>
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

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "shadopan_monastery.h"

class instance_shadopan_monastery : public InstanceMapScript
{
public:
    instance_shadopan_monastery() : InstanceMapScript("instance_shadopan_monastery", 959) { }

    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_shadopan_monastery_InstanceMapScript(map);
    }

    struct instance_shadopan_monastery_InstanceMapScript : public InstanceScript
    {
        instance_shadopan_monastery_InstanceMapScript(Map* map) : InstanceScript(map) { }
    };
};

void AddSC_instance_shadopan_monastery()
{
    new instance_shadopan_monastery();
}
