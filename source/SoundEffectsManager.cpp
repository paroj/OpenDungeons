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

#include "SoundEffectsManager.h"

#include "CreatureSound.h"
#include "Helper.h"
#include "ResourceManager.h"
#include "Random.h"

#include <map>

// class GameSound
GameSound::GameSound(const std::string& filename, bool spatialSound):
    mSound(NULL),
    mSoundBuffer(NULL)
{
    // Loads the buffer
    mSoundBuffer = new sf::SoundBuffer();
    if (mSoundBuffer->loadFromFile(filename) == false)
    {
        delete mSoundBuffer;
        mSoundBuffer = NULL;
        return;
    }

    mFilename = filename;

    mSound = new sf::Sound();
    // Loads the main sound object
    mSound->setBuffer(*mSoundBuffer);

    mSound->setLoop(false);

    // Sets a correct attenuation value if the sound is spatial
    if (spatialSound == true)
    {
        // Set convenient spatial fading unit.
        mSound->setVolume(100.0f);
        mSound->setAttenuation(3.0f);
        mSound->setMinDistance(3.0f);
    }
    else // Disable attenuation for sounds that must heard the same way everywhere
    {
        // Prevents the sound from being too loud
        mSound->setVolume(30.0f);
        mSound->setAttenuation(0.0f);
    }
}

GameSound::~GameSound()
{
    // The sound object must be stopped and destroyed before its corresponding
    // buffer to ensure detaching the sound stream from the sound source.
    // This prevents a lot of warnings at app quit.
    if (mSound != NULL)
    {
        mSound->stop();
        delete mSound;
    }

    if (mSoundBuffer != NULL)
        delete mSoundBuffer;
}

void GameSound::setPosition(float x, float y, float z)
{
    mSound->setPosition(x, y, z);
}

// SoundEffectsManager class
template<> SoundEffectsManager* Ogre::Singleton<SoundEffectsManager>::msSingleton = 0;

SoundEffectsManager::SoundEffectsManager()
{
    initializeInterfaceSounds();
    initializeDefaultCreatureSounds();
}

SoundEffectsManager::~SoundEffectsManager()
{
    // Clear up every cached sounds...
    std::map<std::string, GameSound*>::iterator it = mGameSoundCache.begin();
    std::map<std::string, GameSound*>::iterator it_end = mGameSoundCache.end();
    for (; it != it_end; ++it)
    {
        if (it->second != NULL)
            delete it->second;
    }

    std::map<std::string, CreatureSound*>::iterator it2 = mCreatureSoundCache.begin();
    std::map<std::string, CreatureSound*>::iterator it2_end = mCreatureSoundCache.end();
    for (; it2 != it2_end; ++it2)
    {
        if (it2->second != NULL)
            delete it2->second;
    }
}

void SoundEffectsManager::initializeInterfaceSounds()
{
    // Test wether the interface sounds are already loaded.
    if (mInterfaceSounds.empty() == false)
        return;

    Ogre::String soundFolderPath = ResourceManager::getSingletonPtr()->getSoundPath();

    // TODO: Dehard-code it and place the sound filename in a config file.

    // Initializes vectors for interface sounds
    for (unsigned int i = 0; i < NUM_INTERFACE_SOUNDS; ++i)
        mInterfaceSounds.insert(std::make_pair(static_cast<InterfaceSound>(i), std::vector<GameSound*>()));

    // Only one click sounds atm...
    {
        GameSound* gm = getGameSound(soundFolderPath + "Game/click.ogg", false);
        if (gm != NULL)
            mInterfaceSounds[BUTTONCLICK].push_back(gm);
    }

    // Only one dig select sound atm...
    {
        GameSound* gm = getGameSound(soundFolderPath + "Game/click.ogg", false);
        if (gm != NULL)
            mInterfaceSounds[DIGSELECT].push_back(gm);
    }

    // Only one build room sound atm...
    {
        GameSound* gm = getGameSound(soundFolderPath + "Rooms/default_build_room.ogg", false);
        if (gm != NULL)
            mInterfaceSounds[BUILDROOM].push_back(gm);
    }

    // Only one build trap sound atm...
    {
        GameSound* gm = getGameSound(soundFolderPath + "Rooms/default_build_trap.ogg", false);
        if (gm != NULL)
            mInterfaceSounds[BUILDTRAP].push_back(gm);
    }

    // Rock falling sounds
    std::vector<std::string> soundFilenames;
    Helper::fillFilesList(soundFolderPath + "Game/RocksFalling/", soundFilenames, ".ogg");
    for (unsigned int i = 0; i < soundFilenames.size(); ++i)
    {
        GameSound* gm = getGameSound(soundFilenames[i], true);
        if (gm != NULL)
            mInterfaceSounds[ROCKFALLING].push_back(gm);
    }

    // Claim sounds
    soundFilenames.clear();
    Helper::fillFilesList(soundFolderPath + "Game/ClaimTile/", soundFilenames, ".ogg");
    for (unsigned int i = 0; i < soundFilenames.size(); ++i)
    {
        GameSound* gm = getGameSound(soundFilenames[i], true);
        if (gm != NULL)
            mInterfaceSounds[CLAIMED].push_back(gm);
    }
}

void SoundEffectsManager::initializeDefaultCreatureSounds()
{
    if (mCreatureSoundCache.empty() == false)
        return;

    Ogre::String soundFolderPath = ResourceManager::getSingletonPtr()->getSoundPath();

    // Create and populate a new "Default" creature sound library
    CreatureSound* crSound = new CreatureSound();
    // TODO: Everything is hard-coded here, later we might want to make this more dynamic.

    // Battle sounds
    std::vector<std::string> soundFilenames;
    Helper::fillFilesList(soundFolderPath + "Creatures/Default/Battle/", soundFilenames, ".ogg");
    std::vector<GameSound*>& attackSounds = crSound->mSoundsPerType[CreatureSound::ATTACK];
    for (unsigned int i = 0; i < soundFilenames.size(); ++i)
    {
        GameSound* gm = getGameSound(soundFilenames[i], true);
        if (gm != NULL)
            attackSounds.push_back(gm);
    }

    // Digging sounds
    soundFilenames.clear();
    Helper::fillFilesList(soundFolderPath + "Creatures/Default/Digging/", soundFilenames, ".ogg");
    std::vector<GameSound*>& diggingSounds = crSound->mSoundsPerType[CreatureSound::DIGGING];
    for (unsigned int i = 0; i < soundFilenames.size(); ++i)
    {
        GameSound* gm = getGameSound(soundFilenames[i], true);
        if (gm != NULL)
            diggingSounds.push_back(gm);
    }

    // Pickup sounds - PICKUP
    // 1 sound atm...
    {
        std::vector<GameSound*>& pickupSounds = crSound->mSoundsPerType[CreatureSound::PICKUP];
        GameSound* gm = getGameSound(soundFolderPath + "Game/click.ogg", true);
        pickupSounds.push_back(gm);
    }

    // Drop sounds - DROP
    // 1 sound atm...
    {
        std::vector<GameSound*>& dropSounds = crSound->mSoundsPerType[CreatureSound::DROP];
        GameSound* gm = getGameSound(soundFolderPath + "Rooms/default_build_trap.ogg", true);
        dropSounds.push_back(gm);
    }

    // Idle sounds, none atm... - IDLE, WALK, HURT, DYING.

    // Now that the default creature sounds have been created, we register
    // the new creature sound library in the corresponding cache.
    mCreatureSoundCache["Default"] = crSound;
}

void SoundEffectsManager::setListenerPosition(const Ogre::Vector3& position, const Ogre::Quaternion& orientation)
{
    sf::Listener::setPosition(static_cast<float> (position.x),
                              static_cast<float> (position.y),
                              static_cast<float> (position.z));

    Ogre::Vector3 vDir = orientation.zAxis();
    sf::Listener::setDirection(-vDir.x, -vDir.y, -vDir.z);
}

void SoundEffectsManager::playInterfaceSound(InterfaceSound soundType, float XPos, float YPos, float height)
{
    std::vector<GameSound*>& sounds = mInterfaceSounds[soundType];

    unsigned int soundId = Random::Uint(0, sounds.size() - 1);
    if (soundId < 0)
        return;

    sounds[soundId]->setPosition(XPos, YPos, height);
    sounds[soundId]->play();
}

CreatureSound* SoundEffectsManager::getCreatureClassSounds(const std::string& /*className*/)
{
    // For now, every creature shares the same creature sound library
    // TODO: Later, one will have to register the sounds according to the class name if not found here,
    // while making to check whether a sound isn't already in the game sound cache,
    // and then reuse in that case.
    return mCreatureSoundCache["Default"];
}

void SoundEffectsManager::createCreatureClassSounds(const std::string& /*className*/)
{
    // Not implemented yet
    // TODO: Later, one will have to register the sounds according to the class name,
    // while making to check whether a sound isn't already in the game sound cache,
    // and then reuse in that case.
}

GameSound* SoundEffectsManager::getGameSound(const std::string& filename, bool spatialSound)
{
    // We add a suffix to the sound filename as two versions of it can be kept, one spatial,
    // and one which isn't.
    std::string soundFile = filename + (spatialSound ? "_spatial": "");

    std::map<std::string, GameSound*>::iterator it = mGameSoundCache.find(soundFile);
    // Create a new game sound instance when the sound doesn't exist and register it.
    if (it == mGameSoundCache.end())
    {
        GameSound* gm = new GameSound(filename, spatialSound);
        if (gm->isInitialized() == false)
        {
            // Invalid sound filename
            delete gm;
            return NULL;
        }

        mGameSoundCache.insert(std::make_pair(soundFile, gm));
        return gm;
    }

    return it->second;
}
