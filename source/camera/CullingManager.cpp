/*
 *  Copyright (C) 2011-2016  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "camera/CullingManager.h"
#include "camera/CameraManager.h"
#include "entities/Tile.h"
#include "gamemap/GameMap.h"
#include "utils/LogManager.h"

#include <OgreVector3.h>
#include <OgreCamera.h>
#include <OgreRay.h>

#include <sstream>
#include <algorithm>

static const Ogre::Plane GROUND_PLANE(0, 0, 1, 0);

CullingManager::CullingManager(GameMap* gameMap, uint32_t cullingMask)
{
}
bool CullingManager::computeIntersectionPoints(Ogre::Camera* camera, std::vector<Ogre::Vector3>& ogreVectors)
{
    if(ogreVectors.size() != 4)
    {
        OD_LOG_ERR("Unexpected size for ogreVectors size=" + Helper::toString(ogreVectors.size()));
        return false;
    }

    const Ogre::Vector3* cameraVector = camera->getWorldSpaceCorners();
    for(int ii = 0 ; ii < 4; ++ii)
    {
        Ogre::Ray ray(cameraVector[ii], cameraVector[ii+4] - cameraVector[ii]);
        std::pair<bool, Ogre::Real> intersectionResult =  ray.intersects(GROUND_PLANE);
        if(intersectionResult.first)
            ogreVectors[ii]= (ray.getPoint(intersectionResult.second));
        else
        {
            OD_LOG_ERR("I didn't find the intersection point for " + Helper::toString(ii) + "th ray ");
        }
    }
    return true;
}

