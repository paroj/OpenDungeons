/*
 *  Copyright (C) 2011-2014  OpenDungeons Team
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

/*TODO list:
 * - replace hardcoded calculations by scripts and/or read the numbers from XML defintion files
 * - the doTurn() functions needs script support
 */

#include "Creature.h"

#include "CreatureAction.h"
#include "BattleField.h"
#include "Weapon.h"
#include "GameMap.h"
#include "RenderRequest.h"
#include "SoundEffectsHelper.h"
#include "CreatureSound.h"
#include "Player.h"
#include "Seat.h"
#include "RenderManager.h"
#include "Random.h"
#include "ODServer.h"
#include "ServerNotification.h"
#include "LogManager.h"
#include "CullingQuad.h"
#include "Helper.h"
#include "RoomTreasury.h"
#include "RoomDormitory.h"
#include "ODClient.h"

#include <CEGUI/System.h>
#include <CEGUI/WindowManager.h>
#include <CEGUI/Window.h>
#include <CEGUI/widgets/PushButton.h>
#include <CEGUI/Event.h>
#include <CEGUI/UDim.h>
#include <CEGUI/Vector.h>

#include <OgreQuaternion.h>
#include <OgreVector3.h>
#include <OgreVector2.h>

#include <cmath>
#include <algorithm>

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define snprintf_is_banned_in_OD_code _snprintf
#endif

#define NB_COUNTER_DEATH        10

static const int MAX_LEVEL = 30;
//TODO: make this read from definition file?
static const int MaxGoldCarriedByWorkers = 1500;

const std::string Creature::CREATURE_PREFIX = "Creature_";

Creature::Creature(GameMap* gameMap, CreatureDefinition* definition, bool forceName, const std::string& name) :
    MovableGameEntity        (gameMap),
    mTracingCullingQuad      (NULL),
    mWeaponL                 (NULL),
    mWeaponR                 (NULL),
    mHomeTile                (NULL),
    mDefinition              (definition),
    mIsOnMap                 (false),
    mHasVisualDebuggingEntities (false),
    mAwakeness               (100.0),
    mHunger                  (0.0),
    mMaxHP                   (100.0),
    mMaxMana                 (100.0),
    mLevel                   (1),
    mHp                      (100.0),
    mMana                    (100.0),
    mExp                     (0.0),
    mDigRate                 (1.0),
    mDanceRate               (1.0),
    mDeathCounter            (NB_COUNTER_DEATH),
    mGold                    (0),
    mBattleFieldAgeCounter   (0),
    mJobCooldown             (0),
    mEatCooldown             (0),
    mPreviousPositionTile    (NULL),
    mJobRoom                 (NULL),
    mEatRoom                 (NULL),
    mStatsWindow             (NULL),
    mForceAction             (forcedActionNone),
    mSound                   (SoundEffectsHelper::getSingleton().createCreatureSound(getName()))
{
    assert(definition);
    if(forceName)
        setName(name);
    else
        setName(getUniqueCreatureName());

    mBattleField = new BattleField(getGameMap(), getName());
    setIsOnMap(false);

    setObjectType(GameEntity::creature);

    pushAction(CreatureAction::idle);

    // Note: We reset the creature to level 1 in that case.
    setLevel(1);
    mExp = 0.0;

    mMaxHP = mDefinition->getHpPerLevel();
    setHP(mMaxHP);
    mMaxMana = mDefinition->getManaPerLevel();
    setMana(mMaxMana);
    setMoveSpeed(mDefinition->getMoveSpeed());
    mDigRate = mDefinition->getDigRate();
    mDanceRate = mDefinition->getDanceRate();
}

Creature::Creature(GameMap* gameMap) :
    MovableGameEntity        (gameMap),
    mTracingCullingQuad      (NULL),
    mWeaponL                 (NULL),
    mWeaponR                 (NULL),
    mHomeTile                (NULL),
    mDefinition              (NULL),
    mIsOnMap                 (false),
    mHasVisualDebuggingEntities (false),
    mAwakeness               (100.0),
    mHunger                  (0.0),
    mMaxHP                   (100.0),
    mMaxMana                 (100.0),
    mLevel                   (1),
    mHp                      (100.0),
    mMana                    (100.0),
    mExp                     (0.0),
    mDigRate                 (1.0),
    mDanceRate               (1.0),
    mDeathCounter            (NB_COUNTER_DEATH),
    mGold                    (0),
    mBattleFieldAgeCounter   (0),
    mJobCooldown             (0),
    mEatCooldown             (0),
    mPreviousPositionTile    (NULL),
    mBattleField             (NULL),
    mJobRoom                 (NULL),
    mEatRoom                 (NULL),
    mForceAction             (forcedActionNone),
    mStatsWindow             (NULL)
{
    setObjectType(GameEntity::creature);
}

/* Destructor is needed when removing from Quadtree */
Creature::~Creature()
{
    if(mTracingCullingQuad != NULL)
    {
        mTracingCullingQuad->entry->creature_list.remove(this);
        mTracingCullingQuad->mortuaryInsert(this);
    }

    if(mBattleField != NULL)
        delete mBattleField;
}

void Creature::createMeshLocal()
{
    MovableGameEntity::createMeshLocal();
    if(getGameMap()->isServerGameMap())
        return;

    RenderRequest* request = new RenderRequest;
    request->type   = RenderRequest::createCreature;
    request->str    = static_cast<Creature*>(this)->getDefinition()->getMeshName();
    request->vec    = static_cast<Creature*>(this)->getDefinition()->getScale();
    request->p = static_cast<void*>(this);
    RenderManager::queueRenderRequest(request);
}

void Creature::destroyMeshLocal()
{
    MovableGameEntity::destroyMeshLocal();
    getWeaponL()->destroyMesh();
    getWeaponR()->destroyMesh();
    if(getGameMap()->isServerGameMap())
        return;

    destroyStatsWindow();
    RenderRequest* request = new RenderRequest;
    request->type = RenderRequest::destroyCreature;
    request->p = static_cast<void*>(this);
    RenderManager::queueRenderRequest(request);
}

void Creature::deleteYourselfLocal()
{
    MovableGameEntity::deleteYourselfLocal();
    // If standing on a valid tile, notify that tile we are no longer there.
    if(positionTile() != 0)
        positionTile()->removeCreature(this);

    getWeaponL()->deleteYourself();
    getWeaponR()->deleteYourself();
    if(getGameMap()->isServerGameMap())
        return;

    RenderRequest* request = new RenderRequest;
    request->type = RenderRequest::deleteCreature;
    request->p = static_cast<void*>(this);
    RenderManager::queueRenderRequest(request);
}

//! \brief A function which returns a string describing the IO format of the << and >> operators.
std::string Creature::getFormat()
{
    //NOTE:  When this format changes changes to RoomPortal::spawnCreature() may be necessary.
    return "className\tname\tposX\tposY\tposZ\tcolor\tweaponL"
        + Weapon::getFormat() + "\tweaponR" + Weapon::getFormat() + "\tHP\tmana";
}

//! \brief A matched function to transport creatures between files and over the network.
std::ostream& operator<<(std::ostream& os, Creature *c)
{
    assert(c);

    // Check creature weapons
    Weapon* wL = c->mWeaponL;
    if (wL == NULL)
        wL = new Weapon(c->getGameMap(), "none", 0.0, 1.0, 0.0, "L", c);
    Weapon* wR = c->mWeaponR;
    if (wR == NULL)
        wR = new Weapon(c->getGameMap(), "none", 0.0, 1.0, 0.0, "R", c);

    os << c->mDefinition->getClassName() << "\t" << c->getName() << "\t";

    os << c->getPosition().x << "\t";
    os << c->getPosition().y << "\t";
    os << c->getPosition().z << "\t";
    os << c->getColor() << "\t";
    os << wL << "\t" << wR << "\t";
    os << c->getHP() << "\t";
    os << c->getMana() << "\t";
    os << c->getLevel();

    // If we had to create dummy weapons for serialization, delete them now.
    if (c->mWeaponL == NULL)
        delete wL;
    if (c->mWeaponR == NULL)
        delete wR;

    return os;
}

/*! \brief A matched function to transport creatures between files and over the network.
 *
 */
std::istream& operator>>(std::istream& is, Creature *c)
{
    double xLocation = 0.0, yLocation = 0.0, zLocation = 0.0;
    double tempDouble = 0.0;
    std::string tempString;

    is >> tempString;

    if (tempString.compare("autoname") == 0)
        tempString = c->getUniqueCreatureName();

    c->setName(tempString);

    is >> xLocation >> yLocation >> zLocation;
    c->setPosition(Ogre::Vector3((Ogre::Real)xLocation, (Ogre::Real)yLocation, (Ogre::Real)zLocation));

    int color = 0;
    is >> color;
    c->setColor(color);

    // TODO: Load weapon from a catalog file.
    c->setWeaponL(new Weapon(c->getGameMap(), std::string(), 0.0, 0.0, 0.0, std::string()));
    is >> c->mWeaponL;

    c->setWeaponR(new Weapon(c->getGameMap(), std::string(), 0.0, 0.0, 0.0, std::string()));
    is >> c->mWeaponR;

    is >> tempDouble;
    c->setHP(tempDouble);
    is >> tempDouble;
    c->setMana(tempDouble);
    is >> tempDouble;
    c->setLevel(tempDouble);

    return is;
}

//! \brief A matched function to transport creatures between files and over the network.
ODPacket& operator<<(ODPacket& os, Creature *c)
{
    assert(c);

    // Check creature weapons
    Weapon* wL = c->mWeaponL;
    if (wL == NULL)
        wL = new Weapon(c->getGameMap(), "none", 0.0, 1.0, 0.0, "L", c);
    Weapon* wR = c->mWeaponR;
    if (wR == NULL)
        wR = new Weapon(c->getGameMap(), "none", 0.0, 1.0, 0.0, "R", c);

    std::string name = c->getName();
    os << name;

    Ogre::Vector3 position = c->getPosition();
    int color = c->getColor();
    os << position;
    os << color;
    os << wL << wR;
    os << c->mHp;
    os << c->mMana;
    os << c->mLevel;
    os << c->mDigRate;
    os << c->mDanceRate;
    os << c->mMoveSpeed;
    os << c->mMaxHP;
    os << c->mMaxMana;
    os << c->mAwakeness;
    os << c->mHunger;

    // If we had to create dummy weapons for serialization, delete them now.
    if (c->mWeaponL == NULL)
        delete wL;
    if (c->mWeaponR == NULL)
        delete wR;

    return os;
}

/*! \brief A matched function to transport creatures between files and over the network.
 *
 */
ODPacket& operator>>(ODPacket& is, Creature *c)
{
    double tempDouble = 0.0;
    std::string tempString;

    is >> tempString;
    c->setName(tempString);

    Ogre::Vector3 position;
    is >> position;
    c->setPosition(position);

    int color;
    is >> color;
    c->setColor(color);

    // TODO: Load weapon from a catalog file.
    c->setWeaponL(new Weapon(c->getGameMap(), std::string(), 0.0, 0.0, 0.0, std::string()));
    is >> c->mWeaponL;

    c->setWeaponR(new Weapon(c->getGameMap(), std::string(), 0.0, 0.0, 0.0, std::string()));
    is >> c->mWeaponR;
    is >> c->mHp;
    is >> c->mMana;
    is >> c->mLevel;
    is >> c->mDigRate;
    is >> c->mDanceRate;
    is >> c->mMoveSpeed;
    is >> c->mMaxHP;
    is >> c->mMaxMana;
    is >> c->mAwakeness;
    is >> c->mHunger;

    return is;
}

Creature* Creature::loadFromLine(const std::string& line, GameMap* gameMap)
{
    assert(gameMap);
    std::vector<std::string> elems = Helper::split(line, '\t');
    CreatureDefinition *creatureClass = gameMap->getClassDescription(elems[0]);
    std::string creatureName = elems[1];
    Creature* c = new Creature(gameMap, creatureClass, true, creatureName);

    if (creatureName.compare("autoname") == 0)
        creatureName = c->getUniqueCreatureName();
    c->setName(creatureName);

    double xLocation = Helper::toDouble(elems[2]);
    double yLocation = Helper::toDouble(elems[3]);
    double zLocation = Helper::toDouble(elems[4]);

    c->setPosition(Ogre::Vector3((Ogre::Real)xLocation, (Ogre::Real)yLocation, (Ogre::Real)zLocation));

    c->setColor(Helper::toInt(elems[5]));

    // TODO: Load weapons from a catalog file.
    c->setWeaponL(new Weapon(c->getGameMap(), elems[6], Helper::toDouble(elems[7]),
                             Helper::toDouble(elems[8]), Helper::toDouble(elems[9]), "L", c));

    c->setWeaponR(new Weapon(c->getGameMap(), elems[10], Helper::toDouble(elems[11]),
                             Helper::toDouble(elems[12]), Helper::toDouble(elems[13]), "R", c));

    c->setHP(Helper::toDouble(elems[14]));
    c->setMana(Helper::toDouble(elems[15]));
    c->setLevel(Helper::toDouble(elems[16]));

    return c;
}

void Creature::setPosition(const Ogre::Vector3& v)
{
    // If we are on the gameMap we may need to update the tile we are in
    if (getIsOnMap())
    {
        // We are on the map
        // Move the creature relative to its parent scene node.  We record the
        // tile the creature is in before and after the move to properly
        // maintain the results returned by the positionTile() function.
        Tile *oldPositionTile = positionTile();

        MovableGameEntity::setPosition(v);
        Tile *newPositionTile = positionTile();

        if (oldPositionTile != newPositionTile)
        {
            if (oldPositionTile != 0)
                oldPositionTile->removeCreature(this);

            if (positionTile() != 0)
                positionTile()->addCreature(this);
        }

        if(!getGameMap()->isServerGameMap())
            mTracingCullingQuad->moveEntryDelta(this,get2dPosition());
    }
    else
    {
        // We are not on the map
        MovableGameEntity::setPosition(v);
    }
}

void Creature::drop(const Ogre::Vector3& v)
{
    setIsOnMap(true);
    setPosition(v);
    mForceAction = forcedActionSearchAction;
}

void Creature::setHP(double nHP)
{
    mHp = nHP;
}

double Creature::getHP() const
{
    double tempDouble = mHp;
    return tempDouble;
}

void Creature::setMana(double nMana)
{
    mMana = nMana;
}

double Creature::getMana() const
{
    double tempDouble = mMana;

    return tempDouble;
}

void Creature::setIsOnMap(bool nIsOnMap)
{
    mIsOnMap = nIsOnMap;
}

bool Creature::getIsOnMap() const
{
    return mIsOnMap;
}

void Creature::setWeaponL(Weapon* wL)
{
    if (mWeaponL)
        delete mWeaponL;
    mWeaponL = wL;
    if (!mWeaponL)
        return;

    mWeaponL->setParentCreature(this);
    mWeaponL->setHandString("L");
}

void Creature::setWeaponR(Weapon* wR)
{
    if (mWeaponR)
        delete mWeaponR;
    mWeaponR = wR;
    if (!mWeaponR)
        return;

    mWeaponR->setParentCreature(this);
    mWeaponR->setHandString("R");
}

void Creature::attach()
{
    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::attachCreature;
    request->p = this;

    // Add the request to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}


void Creature::detach()
{
    RenderRequest *request = new RenderRequest;
    request->type = RenderRequest::detachCreature;
    request->p = this;

    // Add the request to the queue of rendering operations to be performed before the next frame.
    RenderManager::queueRenderRequest(request);
}

void Creature::doTurn()
{
    // if creature is not on map, we do nothing
    if(!getIsOnMap())
        return;

    // Check if the creature is alive
    if (getHP() <= 0.0)
    {
        // Let the creature lay dead on the ground for a few turns before removing it from the GameMap.
        if (mDeathCounter == NB_COUNTER_DEATH)
        {
            stopJob();
            stopEating();
            clearDestinations();
            setAnimationState("Die", false, false);
        }
        else if (mDeathCounter <= 0)
        {
            try
            {
                Player* player = getControllingPlayer();
                ServerNotification *serverNotification = new ServerNotification(
                    ServerNotification::removeCreature, player);
                std::string name = getName();
                serverNotification->mPacket << name;
                ODServer::getSingleton().queueServerNotification(serverNotification);
            }
            catch (std::bad_alloc&)
            {
                OD_ASSERT_TRUE(false);
                exit(1);
            }

            // If the creature has a homeTile where it sleeps, its bed needs to be destroyed.
            if (getHomeTile() != 0)
            {
                RoomDormitory* home = static_cast<RoomDormitory*>(getHomeTile()->getCoveringRoom());
                home->releaseTileForSleeping(getHomeTile(), this);
            }

            // Remove the creature from the game map and into the deletion queue, it will be deleted
            // when it is safe, i.e. all other pointers to it have been wiped from the program.
            getGameMap()->removeCreature(this);
            deleteYourself();
        }

        --mDeathCounter;
        return;
    }

    // If we are not standing somewhere on the map, do nothing.
    if (positionTile() == NULL)
        return;

    // Check to see if we have earned enough experience to level up.
    if(checkLevelUp())
    {
        setLevel(getLevel() + 1);
        //std::cout << "\n\n" << getName() << " has reached level " << getLevel() << "\n";

        if (mDefinition->isWorker())
        {
            mDigRate += 4.0 * getLevel() / (getLevel() + 5.0);
            mDanceRate += 0.12 * getLevel() / (getLevel() + 5.0);
            //std::cout << "New dig rate: " << mDigRate << "\tnew dance rate: " << mDanceRate << "\n";
        }

        mMoveSpeed += 0.4 / (getLevel() + 2.0);

        mMaxHP += mDefinition->getHpPerLevel();
        mMaxMana += mDefinition->getManaPerLevel();

        // Test the max HP/mana against their absolute class maximums
        if (mMaxHP > mDefinition->getMaxHp())
            mMaxHP = mDefinition->getMaxHp();
        if (mMaxMana > mDefinition->getMaxMana())
            mMaxMana = mDefinition->getMaxMana();

        if(getGameMap()->isServerGameMap())
        {
            try
            {
                ServerNotification *serverNotification = new ServerNotification(
                    ServerNotification::creatureRefresh, getControllingPlayer());
                serverNotification->mPacket << this;
                ODServer::getSingleton().queueServerNotification(serverNotification);
            }
            catch (std::bad_alloc&)
            {
                OD_ASSERT_TRUE(false);
                exit(1);
            }
        }
    }

    // TODO : this is auto heal. It could be done on client side
    // Heal.
    mHp += 0.1;
    if (mHp > getMaxHp())
        mHp = getMaxHp();

    // Regenerate mana.
    mMana += 0.45;
    if (mMana > mMaxMana)
        mMana = mMaxMana;

    mAwakeness -= 0.15;
    if (mAwakeness < 0.0)
        mAwakeness = 0.0;

    mHunger += 0.15;
    if (mHunger > 100.0)
        mHunger = 100.0;

    // Look at the surrounding area
    updateVisibleTiles();
    mVisibleEnemyObjects         = getVisibleEnemyObjects();
    mReachableEnemyObjects       = getReachableAttackableObjects(mVisibleEnemyObjects, 0, 0);
    mEnemyObjectsInRange         = getEnemyObjectsInRange(mVisibleEnemyObjects);
    mLivingEnemyObjectsInRange   = GameEntity::removeDeadObjects(mEnemyObjectsInRange);
    mVisibleAlliedObjects        = getVisibleAlliedObjects();
    mReachableAlliedObjects      = getReachableAttackableObjects(mVisibleAlliedObjects, 0, 0);

    std::vector<Tile*> markedTiles;

    if (mDefinition->getDigRate() > 0.0)
        markedTiles = getVisibleMarkedTiles();

    decideNextAction();

    // The loopback variable allows creatures to begin processing a new
    // action immediately after some other action happens.
    bool loopBack = false;
    unsigned int loops = 0;

    do
    {
        ++loops;
        loopBack = false;

        // Carry out the current task
        if (!mActionQueue.empty())
        {
            CreatureAction topActionItem = mActionQueue.front();

            switch (topActionItem.getType())
            {
                case CreatureAction::idle:
                    loopBack = handleIdleAction();
                    break;

                case CreatureAction::walkToTile:
                    loopBack = handleWalkToTileAction();
                    break;

                case CreatureAction::claimTile:
                    loopBack = handleClaimTileAction();
                    break;

                case CreatureAction::claimWallTile:
                    loopBack = handleClaimWallTileAction();
                    break;

                case CreatureAction::digTile:
                    loopBack = handleDigTileAction();
                    break;

                case CreatureAction::depositGold:
                    loopBack = handleDepositGoldAction();
                    break;

                case CreatureAction::findHome:
                    loopBack = handleFindHomeAction(false);
                    break;

                case CreatureAction::findHomeForced:
                    loopBack = handleFindHomeAction(true);
                    break;

                case CreatureAction::sleep:
                    loopBack = handleSleepAction();
                    break;

                case CreatureAction::jobdecided:
                    loopBack = handleJobAction(false);
                    break;

                case CreatureAction::jobforced:
                    loopBack = handleJobAction(true);
                    break;

                case CreatureAction::eatdecided:
                    loopBack = handleEatingAction(false);
                    break;

                case CreatureAction::eatforced:
                    loopBack = handleEatingAction(true);
                    break;

                case CreatureAction::attackObject:
                    loopBack = handleAttackAction();
                    break;

                case CreatureAction::maneuver:
                    loopBack = handleManeuverAction();
                    break;

                default:
                    LogManager::getSingleton().logMessage("ERROR:  Unhandled action type in Creature::doTurn():"
                        + Ogre::StringConverter::toString(topActionItem.getType()));
                    popAction();
                    loopBack = false;
                    break;
            }
        }
        else
        {
            LogManager::getSingleton().logMessage("ERROR:  Creature has empty action queue in doTurn(), this should not happen.");
            loopBack = false;
        }
    } while (loopBack && loops < 20);

    if(loops >= 20)
    {
        LogManager::getSingleton().logMessage("> 20 loops in Creature::doTurn name:" + getName() +
                " colour: " + Ogre::StringConverter::toString(getColor()) + ". Breaking out..");
    }

    // Update the visual debugging entities
    //if we are standing in a different tile than we were last turn
    if (mHasVisualDebuggingEntities && positionTile() != mPreviousPositionTile)
    {
        //TODO: This destroy and re-create is kind of a hack as its likely only a few
        //tiles will actually change.
        destroyVisualDebugEntities();
        createVisualDebugEntities();
    }
}

void Creature::decideNextAction()
{
    // If the creature can see enemies that are reachable.
    if (!mReachableEnemyObjects.empty())
    {
        // Check to see if there is any combat actions (maneuvering/attacking) in our action queue.
        bool alreadyFighting = false;
        for (unsigned int i = 0, size = mActionQueue.size(); i < size; ++i)
        {
            if (mActionQueue[i].getType() == CreatureAction::attackObject
                    || mActionQueue[i].getType() == CreatureAction::maneuver)
            {
                alreadyFighting = true;
                break;
            }
        }

        // If we are not already fighting with a creature or maneuvering then start doing so.
        if (!alreadyFighting)
        {
            mBattleFieldAgeCounter = 0;
            pushAction(CreatureAction::maneuver);
            // Jump immediately to the action processor since we don't want to decide to
            //train or something if there are enemies around.
            return;
        }
    }

    if (mBattleFieldAgeCounter > 0)
        --mBattleFieldAgeCounter;

    if (mDefinition->isWorker())
        return;

    // Check whether the creature is weak
    bool isWeak = (mHp < mMaxHP / 3);

    // Check to see if we have found a "home" tile where we can sleep yet.
    if (isWeak || (Random::Double(0.0, 1.0) < 0.03 && mHomeTile == NULL
        && peekAction().getType() != CreatureAction::findHome))
    {
        // Check to see if there are any dormitory owned by our color that we can reach.
        std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndColor(Room::dormitory, getColor());
        tempRooms = getGameMap()->getReachableRooms(tempRooms, positionTile(), mDefinition->getTilePassability());
        if (!tempRooms.empty())
        {
            pushAction(CreatureAction::findHome);
            return;
        }
    }

    // If we have found a home tile to sleep on, see if we are tired enough to want to go to sleep.
    if (isWeak || (mHomeTile != NULL && 100.0 * std::pow(Random::Double(0.0, 0.8), 2) > mAwakeness
        && peekAction().getType() != CreatureAction::sleep))
    {
        pushAction(CreatureAction::sleep);
    }
    // If we are hungry, we go to eat
    else if (Random::Double(50.0, 100.0) <= mHunger
             && peekAction().getType() != CreatureAction::eatdecided)
    {
        // Check to see if we can work
        pushAction(CreatureAction::eatdecided);
    }
    // Otherwise, we try to work
    else if (Random::Double(0.0, 1.0) < 0.1 && Random::Double(50.0, 100.0) < mAwakeness
             && peekAction().getType() != CreatureAction::jobdecided)
    {
        // Check to see if we can work
        pushAction(CreatureAction::jobdecided);
    }
}

bool Creature::handleIdleAction()
{
    double diceRoll = Random::Double(0.0, 1.0);
    bool loopBack = false;

    setAnimationState("Idle");

    if(mDefinition->isWorker() && mForceAction == forcedActionSearchAction)
    {
        // If a worker is dropped, he will search in the tile he is and in the 4 neighboor tiles.
        // 1 - If the tile he is in is treasury and he is carrying gold, he should deposit it
        // 2 - if one of the 4 neighboor tiles is marked, he will dig
        // 3 - if the the tile he is in is not claimed and one of the neigbboor tiles is claimed, he will claim
        // 4 - if the the tile he is in is claimed and one of the neigbboor tiles is not claimed, he will claim
        // 5 - If the tile he is in is claimed and one of the neigbboor tiles is a not claimed wall, he will claim
        Tile* position = positionTile();
        int playerColor = getControllingPlayer()->getSeat()->getColor();
        Tile* tileMarkedDig = NULL;
        Tile* tileToClaim = NULL;
        Tile* tileWallNotClaimed = NULL;
        std::vector<Tile*> creatureNeighbors = position->getAllNeighbors();
        for (std::vector<Tile*>::iterator it = creatureNeighbors.begin(); it != creatureNeighbors.end(); ++it)
        {
            Tile* tile = *it;

            if(tileMarkedDig == NULL &&
                tile->getMarkedForDigging(getControllingPlayer())
                )
            {
                tileMarkedDig = tile;
            }
            else if(tileToClaim == NULL &&
                tile->getType() == Tile::claimed &&
                tile->getColor() == playerColor &&
                position->getType() == Tile::dirt &&
                !position->isWallClaimable(playerColor)
                )
            {
                tileToClaim = position;
            }
            else if(tileToClaim == NULL &&
                position->getType() == Tile::claimed &&
                position->getColor() == playerColor &&
                tile->getType() == Tile::dirt &&
                !tile->isWallClaimable(playerColor)
                )
            {
                tileToClaim = tile;
            }
            else if(tileWallNotClaimed == NULL &&
                position->getType() == Tile::claimed &&
                position->getColor() == playerColor &&
                tile->isWallClaimable(playerColor)
                )
            {
                tileWallNotClaimed = tile;
            }
        }
        bool forceGoldDeposit = false;
        if(mGold > 0)
        {
            Room* room = position->getCoveringRoom();
            if((room != NULL) && (room->getType() == Room::treasury))
            {
                forceGoldDeposit = true;
            }
        }

        // Now, we can decide
        if(forceGoldDeposit)
        {
            // Deposing gold is one shot, no need to remind that we
            // were dropped on treasury
            mForceAction = forcedActionNone;
            pushAction(CreatureAction::depositGold);
            return true;
        }
        else if(tileMarkedDig != NULL)
        {
            mForceAction = forcedActionDigTile;
        }
        else if(tileToClaim != NULL)
        {
            mForceAction = forcedActionClaimTile;
        }
        else if(tileWallNotClaimed != NULL)
        {
            mForceAction = forcedActionClaimWallTile;
        }
        else
        {
            // We couldn't find why we were dropped here. Let's behave as usual
            mForceAction = forcedActionNone;
        }
    }

    // Handle if worker was dropped
    if(mDefinition->isWorker() && mForceAction != forcedActionNone)
    {
        switch(mForceAction)
        {
            case forcedActionDigTile:
            {
                pushAction(CreatureAction::digTile);
                return true;
            }
            case forcedActionClaimTile:
            {
                pushAction(CreatureAction::claimTile);
                return true;
            }
            case forcedActionClaimWallTile:
            {
                pushAction(CreatureAction::claimWallTile);
                return true;
            }
        }
    }

    // Decide to check for diggable tiles
    if (mDefinition->getDigRate() > 0.0 && !getVisibleMarkedTiles().empty())
    {
        loopBack = true;
        pushAction(CreatureAction::digTile);
    }
    // Decide to check for claimable tiles
    else if (mDefinition->getDanceRate() > 0.0 && diceRoll < 0.9)
    {
        loopBack = true;
        pushAction(CreatureAction::claimTile);
    }
    // Decide to deposit the gold we are carrying into a treasury.
    else if (mDefinition->getDigRate() > 0.0 && mGold > 0)
    {
        //TODO: We need a flag to see if we have tried to do this
        // so the creature won't get confused if we are out of space.
        loopBack = true;
        pushAction(CreatureAction::depositGold);
    }

    // Fighters
    if(!mDefinition->isWorker() && mForceAction == forcedActionSearchAction)
    {
        mForceAction = forcedActionNone;
        Tile* tile = positionTile();
        if(tile != NULL)
        {
            Room* room = tile->getCoveringRoom();
            if(room != NULL)
            {
                // we see if we are in an hatchery
                if((room->getType() == Room::hatchery) && room->hasOpenCreatureSpot(this))
                {
                    pushAction(CreatureAction::eatforced);
                    return true;
                }
                else if(room->getType() == Room::dormitory)
                {
                    pushAction(CreatureAction::sleep);
                    pushAction(CreatureAction::findHomeForced);
                    return true;
                }
                // If not, can we work in this room ?
                else if((room->getType() != Room::hatchery) && room->hasOpenCreatureSpot(this))
                {
                    pushAction(CreatureAction::jobforced);
                    return true;
                }
            }
        }
    }

    // Any creature.

    // Decide whether to "wander" a short distance
    if (diceRoll >= 0.6)
        return loopBack;

    // Note: Always return true from now on.
    pushAction(CreatureAction::walkToTile);

    // Workers should move around randomly at large jumps.  Non-workers either wander short distances or follow workers.
    int tempX = 0;
    int tempY = 0;

    if (!mDefinition->isWorker())
    {
        // Non-workers only.

        // Check to see if we want to try to follow a worker around or if we want to try to explore.
        double r = Random::Double(0.0, 1.0);
        //if(creatureJob == weakFighter) r -= 0.2;
        if (r < 0.7)
        {
            bool workerFound = false;
            // Try to find a worker to follow around.
            for (unsigned int i = 0; !workerFound && i < mReachableAlliedObjects.size(); ++i)
            {
                // Check to see if we found a worker.
                if (mReachableAlliedObjects[i]->getObjectType() == GameEntity::creature
                    && static_cast<Creature*>(mReachableAlliedObjects[i])->mDefinition->isWorker())
                {
                    // We found a worker so find a tile near the worker to walk to.  See if the worker is digging.
                    Tile* tempTile = mReachableAlliedObjects[i]->getCoveredTiles()[0];
                    if (static_cast<Creature*>(mReachableAlliedObjects[i])->peekAction().getType()
                            == CreatureAction::digTile)
                    {
                        // Worker is digging, get near it since it could expose enemies.
                        tempX = (int)(static_cast<double>(tempTile->x) + 3.0
                                * Random::gaussianRandomDouble());
                        tempY = (int)(static_cast<double>(tempTile->y) + 3.0
                                * Random::gaussianRandomDouble());
                    }
                    else
                    {
                        // Worker is not digging, wander a bit farther around the worker.
                        tempX = (int)(static_cast<double>(tempTile->x) + 8.0
                                * Random::gaussianRandomDouble());
                        tempY = (int)(static_cast<double>(tempTile->y) + 8.0
                                * Random::gaussianRandomDouble());
                    }
                    workerFound = true;
                }

                // If there are no workers around, choose tiles far away to "roam" the dungeon.
                if (!workerFound)
                {
                    if (!mVisibleTiles.empty())
                    {
                        Tile* tempTile = mVisibleTiles[static_cast<unsigned int>(Random::Double(0.6, 0.8)
                                                                                 * (mVisibleTiles.size() - 1))];
                        tempX = tempTile->x;
                        tempY = tempTile->y;
                    }
                }
            }
        }
        else
        {
            // Randomly choose a tile near where we are standing to walk to.
            if (!mVisibleTiles.empty())
            {
                unsigned int tileIndex = static_cast<unsigned int>(mVisibleTiles.size()
                                                                   * Random::Double(0.1, 0.3));
                Tile* myTile = positionTile();
                std::list<Tile*> tempPath = getGameMap()->path(myTile,
                        mVisibleTiles[tileIndex],
                        mDefinition->getTilePassability());
                if (setWalkPath(tempPath, 2, false))
                {
                    setAnimationState("Walk");
                    pushAction(CreatureAction::walkToTile);
                    return true;
                }
            }
        }
    }
    else
    {
        // Workers only.

        // Choose a tile far away from our current position to wander to.
        if (!mVisibleTiles.empty())
        {
            Tile* tempTile = mVisibleTiles[Random::Uint(mVisibleTiles.size() / 2, mVisibleTiles.size() - 1)];
            tempX = tempTile->x;
            tempY = tempTile->y;
        }
    }

    Tile *tempPositionTile = positionTile();
    std::list<Tile*> result;
    if (tempPositionTile != NULL)
    {
        result = getGameMap()->path(tempPositionTile->x, tempPositionTile->y,
                                    tempX, tempY, mDefinition->getTilePassability());
    }

    getGameMap()->cutCorners(result, mDefinition->getTilePassability());
    if (setWalkPath(result, 2, false))
    {
        setAnimationState("Walk");
        pushAction(CreatureAction::walkToTile);
    }
    return true;
}

bool Creature::handleWalkToTileAction()
{
    //TODO: This should be decided based on some aggressiveness parameter.
    if (Random::Double(0.0, 1.0) < 0.6 && !mEnemyObjectsInRange.empty())
    {
        popAction();
        pushAction(CreatureAction::attackObject);
        clearDestinations();
        return true;
    }

    //TODO: Peek at the item that caused us to walk
    // If we are walking toward a tile we are trying to dig out, check to see if it is still marked for digging.
    bool toDigTile = (mActionQueue[1].getType() == CreatureAction::digTile);
    if (toDigTile)
    {
        Player* tempPlayer = getControllingPlayer();

        // Check to see if the tile is still marked for digging
        unsigned int index = mWalkQueue.size();
        Tile *currentTile = NULL;
        if (index > 0)
            currentTile = getGameMap()->getTile((int) mWalkQueue[index - 1].x,
                    (int) mWalkQueue[index - 1].y);

        if (currentTile != NULL)
        {
            // If it is not marked
            if (tempPlayer != 0 && !currentTile->getMarkedForDigging(tempPlayer))
            {
                // Clear the walk queue
                clearDestinations();
            }
        }
    }

    //cout << "walkToTile ";
    if (mWalkQueue.empty())
    {
        popAction();

        // This extra post is included here because if the break statement happens
        // the one at the end of the 'if' block will not happen.
        return true;
    }
    return false;
}

bool Creature::handleClaimTileAction()
{
    Tile* myTile = positionTile();
    //NOTE:  This is a workaround for the problem with the positionTile() function,
    // it can be removed when that issue is resolved.
    if (myTile == NULL)
    {
        popAction();
        return false;
    }

    if(mForceAction != forcedActionClaimTile)
    {
        // Randomly decide to stop claiming with a small probability
        std::vector<Tile*> markedTiles = getVisibleMarkedTiles();
        if (Random::Double(0.0, 1.0) < 0.1 + 0.2 * markedTiles.size())
        {
            // If there are any visible tiles marked for digging start working on that.
            if (!markedTiles.empty())
            {
                popAction();
                pushAction(CreatureAction::digTile);
                return true;
            }
        }
    }

    // See if the tile we are standing on can be claimed
    if ((myTile->getColor() != getColor() || myTile->colorDouble < 1.0) && myTile->isGroundClaimable())
    {
        //cout << "\nTrying to claim the tile I am standing on.";
        // Check to see if one of the tile's neighbors is claimed for our color
        std::vector<Tile*> neighbors = myTile->getAllNeighbors();
        for (unsigned int j = 0; j < neighbors.size(); ++j)
        {
            // Check to see if the current neighbor is already claimed
            Tile* tempTile = neighbors[j];
            if (tempTile->getColor() == getColor() && tempTile->colorDouble >= 1.0)
            {
                //cout << "\t\tFound a neighbor that is claimed.";
                // If we found a neighbor that is claimed for our side than we can start
                // dancing on this tile.  If there is "left over" claiming that can be done
                // it will spill over into neighboring tiles until it is gone.
                setAnimationState("Claim");
                myTile->claimForColor(getColor(), mDefinition->getDanceRate());
                recieveExp(1.5 * (mDefinition->getDanceRate() / (0.35 + 0.05 * getLevel())));

                // Since we danced on a tile we are done for this turn
                return false;
            }
        }
    }

    // The tile we are standing on is already claimed or is not currently
    // claimable, find candidates for claiming.
    // Start by checking the neighbor tiles of the one we are already in
    std::vector<Tile*> neighbors = myTile->getAllNeighbors();
    while (!neighbors.empty())
    {
        // If the current neighbor is claimable, walk into it and skip to the end of this turn
        int tempInt = Random::Uint(0, neighbors.size() - 1);
        Tile* tempTile = neighbors[tempInt];
        //NOTE:  I don't think the "colorDouble" check should happen here.
        if (tempTile != NULL && tempTile->getTilePassability() == Tile::walkableTile
            && (tempTile->getColor() != getColor() || tempTile->colorDouble < 1.0)
            && tempTile->isGroundClaimable())
        {
            // The neighbor tile is a potential candidate for claiming, to be an actual candidate
            // though it must have a neighbor of its own that is already claimed for our side.
            Tile* tempTile2;
            std::vector<Tile*> neighbors2 = tempTile->getAllNeighbors();
            for (unsigned int i = 0; i < neighbors2.size(); ++i)
            {
                tempTile2 = neighbors2[i];
                if (tempTile2->getColor() == getColor()
                        && tempTile2->colorDouble >= 1.0)
                {
                    clearDestinations();
                    addDestination((Ogre::Real)tempTile->x, (Ogre::Real)tempTile->y);
                    setAnimationState("Walk");
                    return false;
                }
            }
        }

        neighbors.erase(neighbors.begin() + tempInt);
    }

    //cout << "\nLooking at the visible tiles to see if I can claim a tile.";
    // If we still haven't found a tile to claim, check the rest of the visible tiles
    std::vector<Tile*> claimableTiles;
    for (unsigned int i = 0; i < mVisibleTiles.size(); ++i)
    {
        // if this tile is not fully claimed yet or the tile is of another player's color
        Tile* tempTile = mVisibleTiles[i];
        if (tempTile != NULL && tempTile->getTilePassability() == Tile::walkableTile
            && (tempTile->colorDouble < 1.0 || tempTile->getColor() != getColor())
            && tempTile->isGroundClaimable())
        {
            // Check to see if one of the tile's neighbors is claimed for our color
            neighbors = mVisibleTiles[i]->getAllNeighbors();
            for (unsigned int j = 0; j < neighbors.size(); ++j)
            {
                tempTile = neighbors[j];
                if (tempTile->getColor() == getColor()
                        && tempTile->colorDouble >= 1.0)
                {
                    claimableTiles.push_back(tempTile);
                }
            }
        }
    }

    //cout << "  I see " << claimableTiles.size() << " tiles I can claim.";
    // Randomly pick a claimable tile, plot a path to it and walk to it
    unsigned int tempUnsigned = 0;
    Tile* tempTile = NULL;
    while (!claimableTiles.empty())
    {
        // Randomly find a "good" tile to claim.  A good tile is one that has many neighbors
        // already claimed, this makes the claimed are more "round" and less jagged.
        do
        {
            int numNeighborsClaimed = 0;

            // Start by randomly picking a candidate tile.
            tempTile = claimableTiles[Random::Uint(0, claimableTiles.size() - 1)];

            // Count how many of the candidate tile's neighbors are already claimed.
            neighbors = tempTile->getAllNeighbors();
            for (unsigned int i = 0; i < neighbors.size(); ++i)
            {
                if (neighbors[i]->getColor() == getColor() && neighbors[i]->colorDouble >= 1.0)
                    ++numNeighborsClaimed;
            }

            // Pick a random number in [0:1], if this number is high enough, than use this tile to claim.  The
            // bar for success approaches 0 as numTiles approaches N so this will be guaranteed to succeed at,
            // or before the time we get to the last unclaimed tile.  The bar for success is also lowered
            // according to how many neighbors are already claimed.
            //NOTE: The bar can be negative, when this happens we are guarenteed to use this candidate tile.
            double bar = 1.0 - (numNeighborsClaimed / 4.0) - (tempUnsigned / (double) (claimableTiles.size() - 1));
            if (Random::Double(0.0, 1.0) >= bar)
                break;

            // Safety catch to prevent infinite loop in case the bar for success is too high and is never met.
            if (tempUnsigned >= claimableTiles.size() - 1)
                break;

            // Increment the counter indicating how many candidate tiles we have rejected so far.
            ++tempUnsigned;
        } while (true);

        if (tempTile != NULL)
        {
            // If we find a valid path to the tile start walking to it and break
            std::list<Tile*> tempPath = getGameMap()->path(myTile, tempTile, mDefinition->getTilePassability());
            getGameMap()->cutCorners(tempPath, mDefinition->getTilePassability());
            if (setWalkPath(tempPath, 2, false))
            {
                setAnimationState("Walk");
                pushAction(CreatureAction::walkToTile);
                return false;
            }
        }

        // If we got to this point, the tile we randomly picked cannot be gotten to via a
        // valid path.  Delete it from the claimable tiles vector and repeat the outer
        // loop to try to find another valid tile.
        for (unsigned int i = 0; i < claimableTiles.size(); ++i)
        {
            if (claimableTiles[i] == tempTile)
            {
                claimableTiles.erase(claimableTiles.begin() + i);
                break; // Break out of this for loop.
            }
        }
    }

    // We couldn't find a tile to try to claim so we start searching for claimable walls
    mForceAction = forcedActionNone;
    popAction();
    pushAction(CreatureAction::claimWallTile);
    return true;
}

bool Creature::handleClaimWallTileAction()
{
    Tile* myTile = positionTile();
    //NOTE:  This is a workaround for the problem with the positionTile() function,
    // it can be removed when that issue is resolved.
    if (myTile == NULL)
    {
        popAction();
        return false;
    }

    // Randomly decide to stop claiming with a small probability
    if(mForceAction != forcedActionClaimWallTile)
    {
        std::vector<Tile*> markedTiles = getVisibleMarkedTiles();
        if (Random::Double(0.0, 1.0) < 0.1 + 0.2 * markedTiles.size())
        {
            // If there are any visible tiles marked for digging start working on that.
            if (!markedTiles.empty())
            {
                popAction();
                pushAction(CreatureAction::digTile);
                return true;
            }
        }
    }

    //std::cout << "Claim wall" << std::endl;

    // See if any of the tiles is one of our neighbors
    bool wasANeighbor = false;
    std::vector<Tile*> creatureNeighbors = myTile->getAllNeighbors();
    Player* tempPlayer = getControllingPlayer();
    for (unsigned int i = 0; i < creatureNeighbors.size() && !wasANeighbor; ++i)
    {
        if (tempPlayer == NULL)
            break;

        Tile* tempTile = creatureNeighbors[i];

        if (!tempTile->isWallClaimable(getColor()))
            continue;

        // Turn so that we are facing toward the tile we are going to dig out.
        faceToward(tempTile->x, tempTile->y);

        // Dig out the tile by decreasing the tile's fullness.
        setAnimationState("Claim", true);
        tempTile->claimForColor(getColor(), mDefinition->getDanceRate());
        recieveExp(1.5 * mDefinition->getDanceRate() / 20.0);

        wasANeighbor = true;
        //std::cout << "Claiming wall" << std::endl;
        break;
    }

    // If we successfully found a wall tile to claim then we are done for this turn.
    if (wasANeighbor)
        return false;
    //std::cout << "Looking for a wall to claim" << std::endl;

    // Find paths to all of the neighbor tiles for all of the visible wall tiles.
    std::vector<std::list<Tile*> > possiblePaths;
    std::vector<Tile*> wallTiles = getVisibleClaimableWallTiles();
    for (unsigned int i = 0; i < wallTiles.size(); ++i)
    {
        std::vector<Tile*> neighbors = wallTiles[i]->getAllNeighbors();
        for (unsigned int j = 0; j < neighbors.size(); ++j)
        {
            Tile* neighborTile = neighbors[j];
            if (neighborTile != NULL && neighborTile->getFullness() < 1)
                possiblePaths.push_back(getGameMap()->path(positionTile(), neighborTile, mDefinition->getTilePassability()));
        }
    }

    // Find the shortest path and start walking toward the tile to be dug out
    if (!possiblePaths.empty())
    {
        // Find the N shortest valid paths, see if there are any valid paths shorter than this first guess
        std::vector<std::list<Tile*> > shortPaths;
        for (unsigned int i = 0; i < possiblePaths.size(); ++i)
        {
            // If the current path is long enough to be valid
            unsigned int currentLength = possiblePaths[i].size();
            if (currentLength >= 2)
            {
                shortPaths.push_back(possiblePaths[i]);

                // If we already have enough short paths
                if (shortPaths.size() > 5)
                {
                    unsigned int longestLength, longestIndex;

                    // Kick out the longest
                    longestLength = shortPaths[0].size();
                    longestIndex = 0;
                    for (unsigned int j = 1; j < shortPaths.size(); ++j)
                    {
                        if (shortPaths[j].size() > longestLength)
                        {
                            longestLength = shortPaths.size();
                            longestIndex = j;
                        }
                    }

                    shortPaths.erase(shortPaths.begin() + longestIndex);
                }
            }
        }

        // Randomly pick a short path to take
        unsigned int numShortPaths = shortPaths.size();
        if (numShortPaths > 0)
        {
            unsigned int shortestIndex;
            shortestIndex = Random::Uint(0, numShortPaths - 1);
            std::list<Tile*> walkPath = shortPaths[shortestIndex];

            // If the path is a legitimate path, walk down it to the tile to be dug out
            getGameMap()->cutCorners(walkPath, mDefinition->getTilePassability());
            if (setWalkPath(walkPath, 2, false))
            {
                setAnimationState("Walk");
                pushAction(CreatureAction::walkToTile);
                return false;
            }
        }
    }

    // If we found no path, let's stop doing this
    mForceAction = forcedActionNone;
    popAction();
    return true;
}

bool Creature::handleDigTileAction()
{
    Tile* myTile = positionTile();
    if (myTile == NULL)
        return false;

    //cout << "dig ";

    // See if any of the tiles is one of our neighbors
    bool wasANeighbor = false;
    std::vector<Tile*> creatureNeighbors = myTile->getAllNeighbors();
    Player* tempPlayer = getControllingPlayer();
    for (unsigned int i = 0; i < creatureNeighbors.size() && !wasANeighbor; ++i)
    {
        if (tempPlayer == NULL)
            break;

        Tile* tempTile = creatureNeighbors[i];

        if (!tempTile->getMarkedForDigging(tempPlayer))
            continue;

        // We found a tile marked by our controlling seat, dig out the tile.

        // If the tile is a gold tile accumulate gold for this creature.
        if (tempTile->getType() == Tile::gold)
        {
            //FIXME: Make sure we can't dig gold if the creature has max gold.
            // Or let gold on the ground, until there is space so that the player
            // isn't stuck when making a way through gold.
            double tempDouble = 5 * std::min(mDefinition->getDigRate(), tempTile->getFullness());
            mGold += (int)tempDouble;
            getGameMap()->getSeatByColor(getColor())->mGoldMined += (int)tempDouble;
            recieveExp(5.0 * mDefinition->getDigRate() / 20.0);
        }

        // Turn so that we are facing toward the tile we are going to dig out.
        faceToward(tempTile->x, tempTile->y);

        // Dig out the tile by decreasing the tile's fullness.
        setAnimationState("Dig", true);
        double amountDug = tempTile->digOut(mDefinition->getDigRate(), true);
        if(amountDug > 0.0)
        {
            recieveExp(1.5 * mDefinition->getDigRate() / 20.0);

            // If the tile has been dug out, move into that tile and try to continue digging.
            if (tempTile->getFullness() < 1)
            {
                recieveExp(2.5);
                setAnimationState("Walk");

                // Remove the dig action and replace it with
                // walking to the newly dug out tile.
                //popAction();
                addDestination((Ogre::Real)tempTile->x, (Ogre::Real)tempTile->y);
                pushAction(CreatureAction::walkToTile);
            }
            //Set sound position and play dig sound.
            try
            {
                std::string name = getName();
                ServerNotification *serverNotification = new ServerNotification(
                    ServerNotification::playCreatureSound, getControllingPlayer());
                serverNotification->mPacket << name << CreatureSound::DIG;
                ODServer::getSingleton().queueServerNotification(serverNotification);
            }
            catch (std::bad_alloc&)
            {
                OD_ASSERT_TRUE(false);
                exit(1);
            }
        }
        else
        {
            //We tried to dig a tile we are not able to
            //Completely bail out if this happens.
            clearActionQueue();
        }

        wasANeighbor = true;
        break;
    }

    // Check to see if we are carrying the maximum amount of gold we can carry, and if so, try to take it to a treasury.
    if (mGold >= MaxGoldCarriedByWorkers)
    {
        // Remove the dig action and replace it with a depositGold action.
        pushAction(CreatureAction::depositGold);
    }

    // If we successfully dug a tile then we are done for this turn.
    if (wasANeighbor)
        return false;

    // Find paths to all of the neighbor tiles for all of the marked visible tiles.
    std::vector<std::list<Tile*> > possiblePaths;
    std::vector<Tile*> markedTiles = getVisibleMarkedTiles();
    for (unsigned int i = 0; i < markedTiles.size(); ++i)
    {
        std::vector<Tile*> neighbors = markedTiles[i]->getAllNeighbors();
        for (unsigned int j = 0; j < neighbors.size(); ++j)
        {
            Tile* neighborTile = neighbors[j];
            if (neighborTile != NULL && neighborTile->getFullness() < 1)
                possiblePaths.push_back(getGameMap()->path(positionTile(), neighborTile, mDefinition->getTilePassability()));
        }
    }

    // Find the shortest path and start walking toward the tile to be dug out
    if (!possiblePaths.empty())
    {
        // Find the N shortest valid paths, see if there are any valid paths shorter than this first guess
        std::vector<std::list<Tile*> > shortPaths;
        for (unsigned int i = 0; i < possiblePaths.size(); ++i)
        {
            // If the current path is long enough to be valid
            unsigned int currentLength = possiblePaths[i].size();
            if (currentLength >= 2)
            {
                shortPaths.push_back(possiblePaths[i]);

                // If we already have enough short paths
                if (shortPaths.size() > 5)
                {
                    unsigned int longestLength, longestIndex;

                    // Kick out the longest
                    longestLength = shortPaths[0].size();
                    longestIndex = 0;
                    for (unsigned int j = 1; j < shortPaths.size(); ++j)
                    {
                        if (shortPaths[j].size() > longestLength)
                        {
                            longestLength = shortPaths.size();
                            longestIndex = j;
                        }
                    }

                    shortPaths.erase(shortPaths.begin() + longestIndex);
                }
            }
        }

        // Randomly pick a short path to take
        unsigned int numShortPaths = shortPaths.size();
        if (numShortPaths > 0)
        {
            unsigned int shortestIndex;
            shortestIndex = Random::Uint(0, numShortPaths - 1);
            std::list<Tile*> walkPath = shortPaths[shortestIndex];

            // If the path is a legitimate path, walk down it to the tile to be dug out
            getGameMap()->cutCorners(walkPath, mDefinition->getTilePassability());
            if (setWalkPath(walkPath, 2, false))
            {
                setAnimationState("Walk");
                pushAction(CreatureAction::walkToTile);
                return false;
            }
        }
    }

    // If none of our neighbors are marked for digging we got here too late.
    // Finish digging
    mForceAction = forcedActionNone;
    bool isDigging = (mActionQueue.front().getType() == CreatureAction::digTile);
    if (isDigging)
    {
        popAction();
        if(mGold > 0)
            pushAction(CreatureAction::depositGold);

        return true;
    }
    return false;
}

bool Creature::handleDepositGoldAction()
{
    // Check to see if we are standing in a treasury.
    Tile* myTile = positionTile();
    if (myTile == NULL)
        return false;

    Room* tempRoom = myTile->getCoveringRoom();
    if (tempRoom != NULL && tempRoom->getType() == Room::treasury)
    {
        // Deposit as much of the gold we are carrying as we can into this treasury.
        mGold -= static_cast<RoomTreasury*>(tempRoom)->depositGold(mGold, myTile);

        // Depending on how much gold we have left (what did not fit in this treasury) we may want to continue
        // looking for another treasury to put the gold into.  Roll a dice to see if we want to quit looking not.
        if (Random::Double(1.0, MaxGoldCarriedByWorkers) > mGold)
        {
            popAction();
            return false;
        }
    }

    // We were not standing in a treasury that has enough room for the gold we are carrying, so try to find one to walk to.
    // Check to see if our seat controls any treasuries.
    std::vector<Room*> treasuriesOwned = getGameMap()->getRoomsByTypeAndColor(Room::treasury, getColor());
    if (treasuriesOwned.empty())
    {
        // There are no treasuries available so just go back to what we were doing.
        popAction();
        LogManager::getSingleton().logMessage("No space to put gold for creature for player "
            + Ogre::StringConverter::toString(getColor()));
        return true;
    }

    Tile* nearestTreasuryTile = NULL;
    unsigned int nearestTreasuryDistance = 0;
    bool validPathFound = false;
    std::list<Tile*> tempPath;

    // Loop over the treasuries to find the closest one.
    for (unsigned int i = 0; i < treasuriesOwned.size(); ++i)
    {
        if (!validPathFound)
        {
            // We have not yet found a valid path to a treasury, check to see if we can get to this treasury.
            unsigned int tempUnsigned = Random::Uint(0, treasuriesOwned[i]->numCoveredTiles() - 1);
            nearestTreasuryTile = treasuriesOwned[i]->getCoveredTile(tempUnsigned);
            tempPath = getGameMap()->path(myTile, nearestTreasuryTile, mDefinition->getTilePassability());
            if (tempPath.size() >= 2 && static_cast<RoomTreasury*>(treasuriesOwned[i])->emptyStorageSpace() > 0)
            {
                validPathFound = true;
                nearestTreasuryDistance = tempPath.size();
            }
        }
        else
        {
            // We have already found at least one valid path to a treasury, see if this one is closer.
            unsigned int tempUnsigned = Random::Uint(0, treasuriesOwned[i]->numCoveredTiles() - 1);
            Tile* tempTile = treasuriesOwned[i]->getCoveredTile(tempUnsigned);
            std::list<Tile*> tempPath2 = getGameMap()->path(myTile, tempTile, mDefinition->getTilePassability());
            if (tempPath2.size() >= 2 && tempPath2.size() < nearestTreasuryDistance
                && static_cast<RoomTreasury*>(treasuriesOwned[i])->emptyStorageSpace() > 0)
            {
                tempPath = tempPath2;
                nearestTreasuryDistance = tempPath.size();
            }
        }
    }

    if (validPathFound)
    {
        // Begin walking to this treasury.
        getGameMap()->cutCorners(tempPath, mDefinition->getTilePassability());
        if (setWalkPath(tempPath, 2, false))
        {
            setAnimationState("Walk");
            pushAction(CreatureAction::walkToTile);
            return false;
        }
    }

    // If we get to here, there is either no treasuries controlled by us, or they are all
    // unreachable, or they are all full, so quit trying to deposit gold.
    popAction();
    LogManager::getSingleton().logMessage("No space to put gold for creature for player "
        + Ogre::StringConverter::toString(getColor()));
    return true;
}

bool Creature::handleFindHomeAction(bool isForced)
{
    // Check to see if we are standing in an open dormitory tile that we can claim as our home.
    Tile* myTile = positionTile();
    if (myTile == NULL)
    {
        popAction();
        return false;
    }

    if((mHomeTile != NULL) && !isForced)
    {
        popAction();
        return false;
    }

    Room* tempRoom = myTile->getCoveringRoom();
    if (tempRoom != NULL && tempRoom->getType() == Room::dormitory)
    {
        Room* roomHomeTile = NULL;
        if(mHomeTile != NULL)
        {
            roomHomeTile = mHomeTile->getCoveringRoom();
            // Same dormitory nothing to do
            if(roomHomeTile == tempRoom)
            {
                popAction();
                return true;
            }
        }

        if (static_cast<RoomDormitory*>(tempRoom)->claimTileForSleeping(myTile, this))
        {
            // We could install the bed in the dormitory. If we already had one, we remove it
            if(roomHomeTile != NULL)
                static_cast<RoomDormitory*>(roomHomeTile)->releaseTileForSleeping(mHomeTile, this);

            mHomeTile = myTile;
            popAction();
            return true;
        }

        // The tile where we are is not claimable. We search if there is another in this dormitory
        Tile* tempTile = static_cast<RoomDormitory*>(tempRoom)->getLocationForBed(
            mDefinition->getBedDim1(), mDefinition->getBedDim2());
        if(tempTile != NULL)
        {
            std::list<Tile*> tempPath = getGameMap()->path(myTile, tempTile, mDefinition->getTilePassability());
            if (setWalkPath(tempPath, 1, false))
            {
                setAnimationState("Walk");
                pushAction(CreatureAction::walkToTile);
                return false;
            }
        }
    }

    // If we found a tile to claim as our home in the above block
    // If we have been forced, we do not search in another dormitory
    if ((mHomeTile != NULL) || isForced)
    {
        popAction();
        return true;
    }

    // Check to see if we can walk to a dormitory that does have an open tile.
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndColor(Room::dormitory, getColor());
    std::random_shuffle(tempRooms.begin(), tempRooms.end());
    unsigned int nearestDormitoryDistance = 0;
    bool validPathFound = false;
    std::list<Tile*> tempPath;
    for (unsigned int i = 0; i < tempRooms.size(); ++i)
    {
        // Get the list of open rooms at the current dormitory and check to see if
        // there is a place where we could put a bed big enough to sleep in.
        Tile* tempTile = static_cast<RoomDormitory*>(tempRooms[i])->getLocationForBed(
                        mDefinition->getBedDim1(), mDefinition->getBedDim2());

        // If the previous attempt to place the bed in this dormitory failed, try again with the bed the other way.
        if (tempTile == NULL)
            tempTile = static_cast<RoomDormitory*>(tempRooms[i])->getLocationForBed(
                                                                     mDefinition->getBedDim2(), mDefinition->getBedDim1());

        // Check to see if either of the two possible bed orientations tried above resulted in a successful placement.
        if (tempTile != NULL)
        {
            std::list<Tile*> tempPath2 = getGameMap()->path(myTile, tempTile,
                    mDefinition->getTilePassability());

            // Find out the minimum valid path length of the paths determined in the above block.
            if (!validPathFound)
            {
                // If the current path is long enough to be valid then record the path and the distance.
                if (tempPath2.size() >= 2)
                {
                    tempPath = tempPath2;
                    nearestDormitoryDistance = tempPath.size();
                    validPathFound = true;
                }
            }
            else
            {
                // If the current path is long enough to be valid but shorter than the
                // shortest path seen so far, then record the path and the distance.
                if (tempPath2.size() >= 2 && tempPath2.size()
                        < nearestDormitoryDistance)
                {
                    tempPath = tempPath2;
                    nearestDormitoryDistance = tempPath.size();
                }
            }
        }
    }

    // If we found a valid path to an open room in a dormitory, then start walking along it.
    if (validPathFound)
    {
        getGameMap()->cutCorners(tempPath, mDefinition->getTilePassability());
        if (setWalkPath(tempPath, 2, false))
        {
            setAnimationState("Walk");
            pushAction(CreatureAction::walkToTile);
            return false;
        }
    }

    // If we got here there are no reachable dormitory that are unclaimed so we quit trying to find one.
    popAction();
    return true;
}

bool Creature::handleJobAction(bool isForced)
{
    // Current creature tile position
    Tile* myTile = positionTile();

    // Randomly decide to stop working, we are more likely to stop when we are tired.
    if (Random::Double(20.0, 50.0) > mAwakeness)
    {
        popAction();

        stopJob();
        return true;
    }
    // Make sure we are on the map.
    else if (myTile != NULL)
    {
        // If we are already working, nothing to do
        if(mJobRoom != NULL)
            return false;

        // See if we are in a room where we can work. If so, we try to add the creature. If it is ok, the room
        // will handle the creature from here to make it go where it should
        Room* tempRoom = myTile->getCoveringRoom();
        if (tempRoom != NULL)
        {
            // It is the room responsability to test if the creature is suited for working in it
            if(tempRoom->hasOpenCreatureSpot(this) && (tempRoom->getType() != Room::hatchery) && tempRoom->addCreatureUsingRoom(this))
            {
                mJobRoom = tempRoom;
                return false;
            }
        }
    }
    else if (myTile == NULL)
    {
        // We are not on the map, don't do anything.
        popAction();

        stopJob();
        return false;
    }

    // TODO : We should decide which room to use depending on the creatures preferences (library
    // for wizards, ...).

    // Get the list of trainingHalls controlled by our seat and make sure there is at least one.
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndColor(Room::trainingHall, getColor());

    if (tempRooms.empty())
    {
        popAction();

        stopJob();
        return true;
    }

    // Pick a room we want to work in and try to walk to it.
    double maxTrainDistance = 40.0;
    Room* tempRoom = NULL;
    int nbTry = 5;
    do
    {
        int tempInt = Random::Uint(0, tempRooms.size() - 1);
        tempRoom = tempRooms[tempInt];
        tempRooms.erase(tempRooms.begin() + tempInt);
        double tempDouble = 1.0 / (maxTrainDistance - getGameMap()->crowDistance(myTile, tempRoom->getCoveredTile(0)));
        if (Random::Double(0.0, 1.0) < tempDouble)
            break;
        --nbTry;
    } while (nbTry > 0 && !tempRoom->hasOpenCreatureSpot(this) && !tempRooms.empty());

    if (!tempRoom || !tempRoom->hasOpenCreatureSpot(this))
    {
        // The room is already being used, stop trying to work.
        popAction();
        stopJob();
        return true;
    }

    Tile* tempTile = tempRoom->getCoveredTile(Random::Uint(0, tempRoom->numCoveredTiles() - 1));
    std::list<Tile*> tempPath = getGameMap()->path(myTile, tempTile, mDefinition->getTilePassability());
    if (tempPath.size() < maxTrainDistance && setWalkPath(tempPath, 2, false))
    {
        setAnimationState("Walk");
        pushAction(CreatureAction::walkToTile);
        return false;
    }
    else
    {
        // We could not find a room where we can work so stop trying to find one.
        popAction();
    }

    // Default action
    stopJob();
    return true;
}

bool Creature::handleEatingAction(bool isForced)
{
    // Current creature tile position
    Tile* myTile = positionTile();

    if ((isForced && mHunger < 5.0) ||
        (!isForced && mHunger > Random::Double(70.0, 100.0)))
    {
        popAction();

        stopEating();
        return true;
    }
    // Make sure we are on the map.
    else if (myTile != NULL)
    {
        // If we are already eating, nothing to do
        if(mEatRoom != NULL)
            return false;

        // See if we are in a hatchery. If so, we try to add the creature. If it is ok, the room
        // will handle the creature from here to make it go where it should
        Room* tempRoom = myTile->getCoveringRoom();
        if ((tempRoom != NULL) && (tempRoom->getType() == Room::hatchery && tempRoom->hasOpenCreatureSpot(this)))
        {
            if(tempRoom->addCreatureUsingRoom(this))
            {
                mEatRoom = tempRoom;
                return false;
            }
        }
    }
    else if (myTile == NULL)
    {
        // We are not on the map, don't do anything.
        popAction();

        stopEating();
        return false;
    }

    // TODO : We should decide which room to use depending on the creatures preferences (library
    // for wizards, ...)

    // Get the list of hatchery controlled by our seat and make sure there is at least one.
    std::vector<Room*> tempRooms = getGameMap()->getRoomsByTypeAndColor(Room::hatchery, getColor());

    if (tempRooms.empty())
    {
        popAction();

        stopEating();
        return true;
    }

    // Pick a hatchery and try to walk to it.
    // TODO : do we need a max distance for hatchery ?
    double maxDistance = 40.0;
    Room* tempRoom = NULL;
    int nbTry = 5;
    do
    {
        int tempInt = Random::Uint(0, tempRooms.size() - 1);
        tempRoom = tempRooms[tempInt];
        tempRooms.erase(tempRooms.begin() + tempInt);
        double tempDouble = 1.0 / (maxDistance - getGameMap()->crowDistance(myTile, tempRoom->getCoveredTile(0)));
        if (Random::Double(0.0, 1.0) < tempDouble)
            break;
        --nbTry;
    } while (nbTry > 0 && !tempRoom->hasOpenCreatureSpot(this) && !tempRooms.empty());

    if (!tempRoom || !tempRoom->hasOpenCreatureSpot(this))
    {
        // The room is already being used, stop trying to eat
        popAction();
        stopEating();
        return true;
    }

    Tile* tempTile = tempRoom->getCoveredTile(Random::Uint(0, tempRoom->numCoveredTiles() - 1));
    std::list<Tile*> tempPath = getGameMap()->path(myTile, tempTile, mDefinition->getTilePassability());
    if (tempPath.size() < maxDistance && setWalkPath(tempPath, 2, false))
    {
        setAnimationState("Walk");
        pushAction(CreatureAction::walkToTile);
        return false;
    }
    else
    {
        // We could not find a room where we can eat so stop trying to find one.
        popAction();
    }

    // Default action
    stopEating();
    return true;
}

void Creature::stopJob()
{
    if (mJobRoom == NULL)
        return;

    mJobRoom->removeCreatureUsingRoom(this);
    mJobRoom = NULL;
}

void Creature::stopEating()
{
    if (mEatRoom == NULL)
        return;

    mEatRoom->removeCreatureUsingRoom(this);
    mEatRoom = NULL;
}

void Creature::changeJobRoom(Room* newRoom)
{
    if (mJobRoom != NULL)
        mJobRoom->removeCreatureUsingRoom(this);


    if(newRoom != NULL && newRoom->addCreatureUsingRoom(this))
        mJobRoom = newRoom;
    else
        mJobRoom = NULL;
}

void Creature::changeEatRoom(Room* newRoom)
{
    if (mEatRoom != NULL)
        mEatRoom->removeCreatureUsingRoom(this);


    if(newRoom != NULL && newRoom->addCreatureUsingRoom(this))
        mEatRoom = newRoom;
    else
        mEatRoom = NULL;
}

bool Creature::handleAttackAction()
{
    // If there are no more enemies which are reachable, stop attacking
    if (mReachableEnemyObjects.empty())
    {
        popAction();
        return true;
    }

    // Find the first enemy close enough to hit and attack it
    if (mLivingEnemyObjectsInRange.empty())
    {
        // There is not an enemy within range, begin maneuvering to try to get near an enemy, or out of the combat situation.
        popAction();
        pushAction(CreatureAction::maneuver);
        return true;
    }

    GameEntity* tempAttackableObject = mLivingEnemyObjectsInRange[0];

    // Turn to face the creature we are attacking and set the animation state to Attack.
    //TODO:  This should be improved so it picks the closest tile rather than just the [0] tile.
    Tile* tempTile = tempAttackableObject->getCoveredTiles()[0];
    clearDestinations();
    faceToward(tempTile->x, tempTile->y);
    setAnimationState("Attack1", true);

    try
    {
        std::string name = getName();
        ServerNotification *serverNotification = new ServerNotification(
            ServerNotification::playCreatureSound, getControllingPlayer());
        serverNotification->mPacket << name << CreatureSound::ATTACK;
        ODServer::getSingleton().queueServerNotification(serverNotification);
    }
    catch (std::bad_alloc&)
    {
        OD_ASSERT_TRUE(false);
        exit(1);
    }

    // Calculate how much damage we do.
    Tile* myTile = positionTile();
    double damageDone = getHitroll(getGameMap()->crowDistance(myTile, tempTile));
    damageDone *= Random::Double(0.0, 1.0);
    damageDone -= std::pow(Random::Double(0.0, 0.4), 2.0) * tempAttackableObject->getDefense();

    // Make sure the damage is positive.
    if (damageDone < 0.0)
        damageDone = 0.0;

    // Do the damage and award experience points to both creatures.
    tempAttackableObject->takeDamage(damageDone, tempTile);
    double expGained;
    expGained = 1.0 + 0.2 * std::pow(damageDone, 1.3);
    mAwakeness -= 0.5;

    // Give a small amount of experince to the creature we hit.
    if(tempAttackableObject->getObjectType() == GameEntity::creature)
    {
        Creature* tempCreature = static_cast<Creature*>(tempAttackableObject);
        tempCreature->recieveExp(0.15 * expGained);

        // Add a bonus modifier based on the level of the creature we hit
        // to expGained and give ourselves that much experience.
        if (tempCreature->getLevel() >= getLevel())
            expGained *= 1.0 + (tempCreature->getLevel() - getLevel()) / 10.0;
        else
            expGained /= 1.0 + (getLevel() - tempCreature->getLevel()) / 10.0;
    }
    recieveExp(expGained);

    //std::cout << "\n" << getName() << " did " << damageDone
    //        << " damage to "
            //FIXME: Attackabe object needs a name...
    //        << "";
            //<< tempAttackableObject->getName();
    //std::cout << " who now has " << tempAttackableObject->getHP(
        //       tempTile) << "hp";

    // Randomly decide to start maneuvering again so we don't just stand still and fight.
    if (Random::Double(0.0, 1.0) <= 0.6)
        popAction();

    return false;
}

bool Creature::handleManeuverAction()
{
    // If there is an enemy within range, stop maneuvering and attack it.
    if (!mLivingEnemyObjectsInRange.empty())
    {
        popAction();

        // If the next action down the stack is not an attackObject action, add it.
        bool tempBool = (mActionQueue.front().getType() != CreatureAction::attackObject);
        if (tempBool)
            pushAction(CreatureAction::attackObject);

        return true;
    }

    // If there are no more enemies which are reachable, stop maneuvering.
    if (mReachableEnemyObjects.empty())
    {
        popAction();
        return true;
    }

    OD_ASSERT_TRUE(mBattleField != NULL);
    if (mBattleField == NULL)
        return true;

    /*
    // TODO: Check this
    // Check to see if we should try to strafe the enemy
    if(randomDouble(0.0, 1.0) < 0.3)
    {
        //TODO:  This should be improved so it picks the closest tile rather than just the [0] tile.
        tempTile = nearestEnemyObject->getCoveredTiles()[0];
        tempVector = Ogre::Vector3(tempTile->x, tempTile->y, 0.0);
        tempVector -= position;
        tempVector.normalise();
        tempVector *= randomDouble(0.0, 3.0);
        tempQuat.FromAngleAxis(Ogre::Degree((randomDouble(0.0, 1.0) < 0.5 ? 90 : 270)), Ogre::Vector3::UNIT_Z);
        tempTile = getGameMap()->getTile(positionTile()->x + tempVector.x, positionTile()->y + tempVector.y);
        if(tempTile != NULL)
        {
            tempPath = getGameMap()->path(positionTile(), tempTile, tilePassability);

            if(setWalkPath(tempPath, 2, false))
                setAnimationState("Walk");
        }
    }
    */

    // There are no enemy creatures in range so we will have to maneuver towards one.
    // Prepare the battlefield so we can decide where to move.
    if (mBattleFieldAgeCounter == 0)
    {
        computeBattlefield();
        mBattleFieldAgeCounter = Random::Uint(2, 6);
    }

    // Find a location on the battlefield to move to, we try to find a minumum if we are
    // trying to "attack" and a maximum if we are trying to "retreat".
    Tile* myTile = positionTile();
    bool attack_animation = true;
    SecurityTile minimumFieldValue(-1, -1, 0.0);

    // Check whether the hostility level is not under zero, meaning that we have enough allies
    // around there aren't enough enemies to go and attack.
    if (mBattleField->getTileSecurityLevel(myTile->x, myTile->y) > 0.0)
    {
        minimumFieldValue = mBattleField->getMinSecurityLevel(); // Attack where there are most enemies
        attack_animation = true;
    }
    else
    {
        // Too much enemies or not enough allies
        minimumFieldValue = mBattleField->getMaxSecurityLevel(); // Retreat where there are most allies
        attack_animation = false;
    }

    // Find a path if we obtained an actual tile to it
    if (minimumFieldValue.getPosX() < 0 || minimumFieldValue.getPosY() < 0)
        return true;

    // Pick a destination tile near the tile we got from the battlefield.
    clearDestinations();
    // Pick a true destination randomly within the max range of our weapons.
    double tempDouble = std::max(mWeaponL ? mWeaponL->getRange() : 0.0,
                                 mWeaponR ? mWeaponR->getRange() : 0.0);
    tempDouble = sqrt(tempDouble);

    std::list<Tile*> tempPath = getGameMap()->path(positionTile()->x, positionTile()->y,
                                              (int)minimumFieldValue.getPosX() + Random::Double(-1.0 * tempDouble, tempDouble),
                                              (int)minimumFieldValue.getPosY() + Random::Double(-1.0 * tempDouble, tempDouble),
                                              mDefinition->getTilePassability());

    // Walk a maximum of N tiles before recomputing the destination since we are in combat.
    unsigned int tempUnsigned = 5;
    if (tempPath.size() >= tempUnsigned)
        tempPath.resize(tempUnsigned);

    getGameMap()->cutCorners(tempPath, mDefinition->getTilePassability());
    if (setWalkPath(tempPath, 2, false))
    {
        setAnimationState(attack_animation ? "Walk" : "Flee");
    }

    // Push a walkToTile action into the creature's action queue to make them walk the path they have
    // decided on without recomputing, this helps prevent them from getting stuck in local minima.
    pushAction(CreatureAction::walkToTile);

    // This is a debugging statement, it produces a visual display of the battlefield seen by the first created creature.
    //TODO: Add support to display this when toggling the debug view. See ODFrameListener / GameMode.
    /*
    if (mBattleField->getName().compare("field_1") == 0)
    {
        mBattleField->refreshMeshes(1.0);
    }*/
    return false;
}

bool Creature::handleSleepAction()
{
    Tile* myTile = positionTile();
    if (mHomeTile == NULL)
    {
        popAction();
        return false;
    }

    if (myTile != mHomeTile)
    {
        // Walk to the the home tile.
        std::list<Tile*> tempPath = getGameMap()->path(myTile, mHomeTile, mDefinition->getTilePassability());
        getGameMap()->cutCorners(tempPath, mDefinition->getTilePassability());
        if (setWalkPath(tempPath, 2, false))
        {
            setAnimationState("Walk");
            pushAction(CreatureAction::walkToTile);
            return false;
        }
    }
    else
    {
        // We are at the home tile so sleep.
        setAnimationState("Sleep");
        // Improve awakeness
        mAwakeness += 1.5;
        if (mAwakeness > 100.0)
            mAwakeness = 100.0;
        // Improve HP but a bit slower.
        mHp += 1.0;
        if (mHp > mMaxHP)
            mHp = mMaxHP;
        // Improve mana
        mMana += 4.0;
        if (mMana > mMaxMana)
            mMana = mMaxMana;

        if (mAwakeness >= 100.0 && mHp >= mMaxHP && mMana >= mMaxMana)
            popAction();
    }
    return false;
}


double Creature::getHitroll(double range)
{
    double tempHitroll = 1.0;

    if (mWeaponL != NULL && mWeaponL->getRange() >= range)
        tempHitroll += mWeaponL->getDamage();
    if (mWeaponR != NULL && mWeaponR->getRange() >= range)
        tempHitroll += mWeaponR->getDamage();
    tempHitroll *= log((double) log((double) getLevel() + 1) + 1);

    return tempHitroll;
}

double Creature::getDefense() const
{
    double returnValue = 3.0;
    if (mWeaponL != NULL)
        returnValue += mWeaponL->getDefense();
    if (mWeaponR != NULL)
        returnValue += mWeaponR->getDefense();

    return returnValue;
}

//! \brief Increases the creature's level, adds bonuses to stat points, changes the mesh, etc.
bool Creature::checkLevelUp()
{
    if (getLevel() >= MAX_LEVEL)
        return false;

    if (mExp < 5 * (getLevel() + std::pow(getLevel() / 3.0, 2)))
        return false;

    return true;
}

void Creature::refreshFromCreature(Creature *creatureNewState)
{
    // We save the actual level to check if there is a levelup
    unsigned int oldLevel = mLevel;
    // TODO : send a messageServerNotification::creatureRefresh each time we want
    // to refresh a creature (when loss HP from combat, level up or whatever).
    // The creature update should be here and the data should be transfered
    // in the transfert functions in this file using ODPacket
    mLevel          = creatureNewState->mLevel;
    mDigRate        = creatureNewState->mDigRate;
    mDanceRate      = creatureNewState->mDanceRate;
    mMoveSpeed      = creatureNewState->mMoveSpeed;
    mMaxHP          = creatureNewState->mMaxHP;
    mMaxMana        = creatureNewState->mMaxMana;
    mHp             = creatureNewState->mHp;
    mMana           = creatureNewState->mMana;
    mAwakeness      = creatureNewState->mAwakeness;
    mHunger         = creatureNewState->mHunger;

    // Scale up the mesh.
    if ((oldLevel != getLevel()) && isMeshExisting() && ((getLevel() <= 30 && getLevel() % 2 == 0) || (getLevel() > 30 && getLevel()
            % 3 == 0)))
    {
        Ogre::Real scaleFactor = (Ogre::Real)(1.0 + static_cast<double>(getLevel()) / 250.0);
        if (scaleFactor > 1.03)
            scaleFactor = 1.04;

        RenderRequest *request = new RenderRequest;
        request->type = RenderRequest::scaleSceneNode;
        request->p = mSceneNode;
        request->vec = Ogre::Vector3(scaleFactor, scaleFactor, scaleFactor);
        RenderManager::queueRenderRequest(request);
    }
}

void Creature::updateVisibleTiles()
{
    mVisibleTiles = getGameMap()->visibleTiles(positionTile(), mDefinition->getSightRadius());
}

std::vector<GameEntity*> Creature::getVisibleEnemyObjects()
{
    return getVisibleForce(getColor(), true);
}

std::vector<GameEntity*> Creature::getReachableAttackableObjects(const std::vector<GameEntity*>& objectsToCheck,
                                                                 unsigned int* minRange, GameEntity** nearestObject)
{
    std::vector<GameEntity*> tempVector;
    Tile* myTile = positionTile();
    std::list<Tile*> tempPath;
    bool minRangeSet = false;

    // Loop over the vector of objects we are supposed to check.
    for (unsigned int i = 0; i < objectsToCheck.size(); ++i)
    {
        // Try to find a valid path from the tile this creature is in to the nearest tile where the current target object is.
        // TODO: This should be improved so it picks the closest tile rather than just the [0] tile.
        GameEntity* entity = objectsToCheck[i];
        Tile* objectTile = entity->getCoveredTiles()[0];
        if (getGameMap()->pathExists(myTile->x, myTile->y, objectTile->x,
                objectTile->y, mDefinition->getTilePassability()))
        {
            tempVector.push_back(objectsToCheck[i]);

            if (minRange == NULL)
                continue;

            // TODO: If this could be computed without the path call that would be better.
            tempPath = getGameMap()->path(myTile, objectTile, mDefinition->getTilePassability());

            if (!minRangeSet)
            {
                *nearestObject = objectsToCheck[i];
                *minRange = tempPath.size();
                minRangeSet = true;
            }
            else
            {
                if (tempPath.size() < *minRange)
                {
                    *minRange = tempPath.size();
                    *nearestObject = objectsToCheck[i];
                }
            }
        }
    }

    //TODO: Maybe think of a better canary value for this.
    if (minRange != NULL && !minRangeSet)
        *minRange = 999999;

    return tempVector;
}

std::vector<GameEntity*> Creature::getEnemyObjectsInRange(const std::vector<GameEntity*> &enemyObjectsToCheck)
{
    std::vector<GameEntity*> tempVector;

    // If there are no enemies to check we are done.
    if (enemyObjectsToCheck.empty())
        return tempVector;

    // Find our location and calculate the square of the max weapon range we have.
    Tile *myTile = positionTile();
    double weaponRangeSquared = std::max(mWeaponL ? mWeaponL->getRange() : 0.0,
                                         mWeaponR ? mWeaponR->getRange() : 0.0);
    weaponRangeSquared *= weaponRangeSquared;

    // Loop over the enemyObjectsToCheck and add any within range to the tempVector.
    for (unsigned int i = 0; i < enemyObjectsToCheck.size(); ++i)
    {
        //TODO:  This should be improved so it picks the closest tile rather than just the [0] tile.
        Tile *tempTile = enemyObjectsToCheck[i]->getCoveredTiles()[0];
        if (tempTile == NULL)
            continue;

        double rSquared = std::pow(myTile->x - tempTile->x, 2.0) + std::pow(
                myTile->y - tempTile->y, 2.0);

        if (rSquared < weaponRangeSquared)
            tempVector.push_back(enemyObjectsToCheck[i]);
    }

    return tempVector;
}

std::vector<GameEntity*> Creature::getVisibleAlliedObjects()
{
    return getVisibleForce(getColor(), false);
}

std::vector<Tile*> Creature::getVisibleMarkedTiles()
{
    std::vector<Tile*> tempVector;
    Player *tempPlayer = getControllingPlayer();

    // Loop over all the visible tiles.
    for (unsigned int i = 0, size = mVisibleTiles.size(); i < size; ++i)
    {
        // Check to see if the tile is marked for digging.
        if (tempPlayer != NULL && mVisibleTiles[i]->getMarkedForDigging(tempPlayer))
            tempVector.push_back(mVisibleTiles[i]);
    }

    return tempVector;
}

std::vector<Tile*> Creature::getVisibleClaimableWallTiles()
{
    std::vector<Tile*> claimableWallTiles;

    // Loop over all the visible tiles.
    for (unsigned int i = 0, size = mVisibleTiles.size(); i < size; ++i)
    {
        // Check to see if the tile is marked for digging.
        if (mVisibleTiles[i]->isWallClaimable(getColor()))
            claimableWallTiles.push_back(mVisibleTiles[i]);
    }

    return claimableWallTiles;
}

std::vector<GameEntity*> Creature::getVisibleForce(int color, bool invert)
{
    return getGameMap()->getVisibleForce(mVisibleTiles, color, invert);
}

void Creature::createVisualDebugEntities()
{
    mHasVisualDebuggingEntities = true;
    mVisualDebugEntityTiles.clear();

    Tile *currentTile = NULL;
    updateVisibleTiles();
    for (unsigned int i = 0; i < mVisibleTiles.size(); ++i)
    {
        currentTile = mVisibleTiles[i];

        if (currentTile == NULL)
            continue;

        // Create a render request to create a mesh for the current visible tile.
        RenderRequest *request = new RenderRequest;
        request->type = RenderRequest::createCreatureVisualDebug;
        request->p = currentTile;
        request->p2 = static_cast<void*>(this);

        // Add the request to the queue of rendering operations to be performed before the next frame.
        RenderManager::queueRenderRequest(request);

        mVisualDebugEntityTiles.push_back(currentTile);
    }
}

void Creature::destroyVisualDebugEntities()
{
    mHasVisualDebuggingEntities = false;

    Tile *currentTile = NULL;
    updateVisibleTiles();
    std::list<Tile*>::iterator itr;
    for (itr = mVisualDebugEntityTiles.begin(); itr != mVisualDebugEntityTiles.end(); ++itr)
    {
        currentTile = *itr;

        if (currentTile == NULL)
            continue;

        // Destroy the mesh for the current visible tile
        RenderRequest *request = new RenderRequest;
        request->type = RenderRequest::destroyCreatureVisualDebug;
        request->p = currentTile;
        request->p2 = static_cast<void*>(this);

        // Add the request to the queue of rendering operations to be performed before the next frame.
        RenderManager::queueRenderRequest(request);
    }

}

Tile* Creature::positionTile()
{
    Ogre::Vector3 tempPosition = getPosition();

    return getGameMap()->getTile((int) (tempPosition.x), (int) (tempPosition.y));
}

std::vector<Tile*> Creature::getCoveredTiles()
{
    std::vector<Tile*> tempVector;
    tempVector.push_back(positionTile());
    return tempVector;
}

std::string Creature::getUniqueCreatureName()
{
    std::string className = mDefinition ? mDefinition->getClassName() : std::string();
    std::string name = className + Ogre::StringConverter::toString(
        getGameMap()->nextUniqueNumberCreature());

    return name;
}

bool Creature::CloseStatsWindow(const CEGUI::EventArgs& /*e*/)
{
    destroyStatsWindow();
    return true;
}

void Creature::createStatsWindow()
{
    if (mStatsWindow != NULL)
        return;

    ClientNotification *clientNotification = new ClientNotification(
        ClientNotification::askCreatureInfos);
    std::string name = getName();
    clientNotification->mPacket << name << true;
    ODClient::getSingleton().queueClientNotification(clientNotification);

    CEGUI::WindowManager* wmgr = CEGUI::WindowManager::getSingletonPtr();
    CEGUI::Window* rootWindow = CEGUI::System::getSingleton().getDefaultGUIContext().getRootWindow();

    mStatsWindow = wmgr->createWindow("OD/FrameWindow", std::string("CreatureStatsWindows_") + getName());
    mStatsWindow->setPosition(CEGUI::UVector2(CEGUI::UDim(0.3, 0), CEGUI::UDim(0.3, 0)));
    mStatsWindow->setSize(CEGUI::USize(CEGUI::UDim(0, 300), CEGUI::UDim(0, 300)));

    CEGUI::Window* textWindow = wmgr->createWindow("OD/StaticText", "TextDisplay");
    textWindow->setPosition(CEGUI::UVector2(CEGUI::UDim(0.05, 0), CEGUI::UDim(0.15, 0)));
    textWindow->setSize(CEGUI::USize(CEGUI::UDim(0.9, 0), CEGUI::UDim(0.8, 0)));

    CEGUI::Window* closeButton = wmgr->createWindow("OD/Button", "CloseButton");
    closeButton->setPosition(CEGUI::UVector2(CEGUI::UDim(0.75, 0),CEGUI::UDim(0.80, 0)));
    closeButton->setSize(CEGUI::USize(CEGUI::UDim(0, 60), CEGUI::UDim(0, 45)));
    closeButton->setText("Close");
    // Make the button close the window
    closeButton->subscribeEvent(CEGUI::PushButton::EventClicked,
                                CEGUI::Event::Subscriber(&Creature::CloseStatsWindow, this));

    // Search for the autoclose button and make it work
    CEGUI::Window* childWindow = mStatsWindow->getChild("__auto_closebutton__");
    childWindow->subscribeEvent(CEGUI::PushButton::EventClicked,
                                        CEGUI::Event::Subscriber(&Creature::CloseStatsWindow, this));

    // Set the window title
    childWindow = mStatsWindow->getChild("__auto_titlebar__");
    childWindow->setText(getName() + " (" + getDefinition()->getClassName() + ")");

    mStatsWindow->addChild(textWindow);
    mStatsWindow->addChild(closeButton);
    rootWindow->addChild(mStatsWindow);
    mStatsWindow->show();

    updateStatsWindow("Loading...");
}

void Creature::destroyStatsWindow()
{
    if (mStatsWindow != NULL)
    {
        ClientNotification *clientNotification = new ClientNotification(
            ClientNotification::askCreatureInfos);
        std::string name = getName();
        clientNotification->mPacket << name << false;
        ODClient::getSingleton().queueClientNotification(clientNotification);

        mStatsWindow->destroy();
        mStatsWindow = NULL;
    }
}

void Creature::updateStatsWindow(const std::string& txt)
{
    if (mStatsWindow == NULL)
        return;

    CEGUI::Window* textWindow = mStatsWindow->getChild("TextDisplay");
    textWindow->setText(txt);
}

std::string Creature::getStatsText()
{
    // The creatures are not refreshed at each turn so this information is relevant in the server
    // GameMap only
    std::stringstream tempSS;
    tempSS << "Level: " << getLevel() << std::endl;
    tempSS << "Experience: " << mExp << std::endl;
    tempSS << "HP: " << getHP() << " / " << mMaxHP << std::endl;
    if (!getDefinition()->isWorker())
    {
        tempSS << "Awakeness: " << mAwakeness << std::endl;
        tempSS << "Hunger: " << mHunger << std::endl;
    }
    tempSS << "Move speed: " << getMoveSpeed() << std::endl;
    tempSS << "Left hand: Attack: " << mWeaponL->getDamage() << ", Range: " << mWeaponL->getRange() << std::endl;
    tempSS << "Right hand: Attack: " << mWeaponR->getDamage() << ", Range: " << mWeaponR->getRange() << std::endl;
    tempSS << "Total Defense: " << getDefense() << std::endl;
    if (getDefinition()->isWorker())
    {
        tempSS << "Dig Rate: : " << getDigRate() << std::endl;
        tempSS << "Dance Rate: : " << mDanceRate << std::endl;
    }
    tempSS << "Current Action:";
    for(std::deque<CreatureAction>::iterator it = mActionQueue.begin(); it != mActionQueue.end(); ++it)
    {
        CreatureAction& ca = *it;
        tempSS << " " << ca.toString();
    }
    tempSS << std::endl;
    tempSS << "Color: " << getColor() << std::endl;
    tempSS << "Position: " << Ogre::StringConverter::toString(getPosition()) << std::endl;
    return tempSS.str();
}

void Creature::takeDamage(double damage, Tile *tileTakingDamage)
{
    mHp -= damage;
    if(getGameMap()->isServerGameMap())
    {
        try
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::creatureRefresh, getControllingPlayer());
            serverNotification->mPacket << this;
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
        catch (std::bad_alloc&)
        {
            OD_ASSERT_TRUE(false);
            exit(1);
        }
    }
}

void Creature::recieveExp(double experience)
{
    if (experience < 0)
        return;

    mExp += experience;
}

bool Creature::getHasVisualDebuggingEntities()
{
    return mHasVisualDebuggingEntities;
}

//FIXME: This should be made into getControllingSeat(), when this is done it can simply be a call to GameMap::getSeatByColor().
Player* Creature::getControllingPlayer()
{
    return getGameMap()->getPlayerByColor(getColor());
}

void Creature::clearActionQueue()
{
    mActionQueue.clear();
    mActionQueue.push_front(CreatureAction::idle);
}

void Creature::pushAction(CreatureAction action)
{
    mActionQueue.push_front(action);
}

void Creature::popAction()
{
    mActionQueue.pop_front();
}

CreatureAction Creature::peekAction()
{
    CreatureAction tempAction = mActionQueue.front();

    return tempAction;
}

bool Creature::tryPickup()
{
    if (getHP() <= 0.0)
        return false;

    // Stop the creature walking and set it off the map to prevent the AI from running on it.
    setIsOnMap(false);
    clearDestinations();
    clearActionQueue();
    stopJob();
    stopEating();

    Tile* tile = positionTile();
    if(tile != NULL)
        tile->removeCreature(this);

    return true;
}

void Creature::computeBattlefield()
{
    if (mBattleField == NULL)
        return;

    Tile *tempTile = 0;
    int xDist, yDist = 0;
    GameEntity* tempObject = NULL;

    // Loop over the tiles in this creature's battleField and compute their value.
    // The creature will then walk towards the tile with the minimum value to
    // attack or towards the maximum value to retreat.
    mBattleField->clear();
    for (unsigned int i = 0; i < mVisibleTiles.size(); ++i)
    {
        tempTile = mVisibleTiles[i];
        double tileValue = 0.0;// - sqrt(rSquared)/sightRadius;

        // Enemies
        for (unsigned int j = 0; j < mReachableEnemyObjects.size(); ++j)
        {
            // Skip over objects which will not attack us (they either do not attack at all, or they are dead).
            tempObject = mReachableEnemyObjects[j];
            if ( ! (    tempObject->getObjectType() == GameEntity::creature
                     || tempObject->getObjectType() == GameEntity::trap)
                     || tempObject->getHP(0) <= 0.0)
            {
                continue;
            }

            //TODO:  This should be improved so it picks the closest tile rather than just the [0] tile.
            Tile *tempTile2 = tempObject->getCoveredTiles()[0];

            // Compensate for how close the creature is to me
            //rSquared = std::pow(myTile->x - tempTile2->x, 2.0) + std::pow(myTile->y - tempTile2->y, 2.0);
            //double factor = 1.0 / (sqrt(rSquared) + 1.0);

            // Subtract for the distance from the enemy creature to r
            xDist = tempTile->x - tempTile2->x;
            yDist = tempTile->y - tempTile2->y;
            tileValue -= 1.0 / sqrt((double) (xDist * xDist + yDist * yDist + 1));
        }

        // Allies
        for (unsigned int j = 0; j < mReachableAlliedObjects.size(); ++j)
        {
            //TODO:  This should be improved so it picks the closest tile rather than just the [0] tile.
            Tile *tempTile2 = mVisibleAlliedObjects[j]->getCoveredTiles()[0];

            // Compensate for how close the creature is to me
            //rSquared = std::pow(myTile->x - tempTile2->x, 2.0) + std::pow(myTile->y - tempTile2->y, 2.0);
            //double factor = 1.0 / (sqrt(rSquared) + 1.0);

            xDist = tempTile->x - tempTile2->x;
            yDist = tempTile->y - tempTile2->y;
            tileValue += 1.2 / (sqrt((double) (xDist * xDist + yDist * yDist + 1)));
        }

        const double jitter = 0.00;
        const double tileScaleFactor = 0.5;
        mBattleField->setTileSecurityLevel(tempTile->x, tempTile->y,
                                           (tileValue + Random::Double(-1.0 * jitter, jitter)) * tileScaleFactor);
    }
}

void Creature::playSound(CreatureSound::SoundType soundType)
{
    mSound->setPosition(getPosition());
    mSound->play(soundType);
}
