/*! \file   ODApplication.cpp
 *  \author Ogre team, andrewbuck, oln, StefanP.MUC
 *  \date   07 April 2011
 *  \brief  Class ODApplication containing everything to start the game
 *
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

#include "ODApplication.h"

#include "modes/ModeManager.h"
#include "network/ODServer.h"
#include "network/ODClient.h"
#include "network/ServerMode.h"
#include "sound/MusicPlayer.h"
#include "sound/SoundEffectsManager.h"
#include "render/Gui.h"
#include "render/ODFrameListener.h"
#include "render/TextRenderer.h"
#include "render/RenderManager.h"
#include "utils/ConfigManager.h"
#include "utils/LogManager.h"
#include "utils/LogSinkConsole.h"
#include "utils/LogSinkFile.h"
#include "utils/LogSinkOgre.h"
#include "utils/Random.h"
#include "utils/ResourceManager.h"

#include <OgreApplicationContext.h>
#include <OgreRenderWindow.h>
#include <OgreRoot.h>
#include <Overlay/OgreOverlaySystem.h>
#include <RTShaderSystem/OgreShaderGenerator.h>
#if OGRE_VERSION > 0x10A00
#include <OgreMaterialManager.h>
#include <OgreSGTechniqueResolverListener.h>
#endif

#ifdef OD_USE_SFML_WINDOW
#include <SFML/Window.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Graphics.hpp>

#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#include <wingdi.h>
#endif /* OGRE_PLATFORM == OGRE_PLATFORM_WIN32 */
#endif /* OD_USE_SFML_WINDOW */

#include <boost/program_options.hpp>

#include <memory>
#include <string>
#include <sstream>
#include <fstream>

#ifndef OD_USE_SFML_WINDOW
namespace
{
//! \brief ApplicationContext subclass that creates the game window.
class ODApplicationContext : public OgreBites::ApplicationContext
{
public:
    ODApplicationContext() : OgreBites::ApplicationContext("OpenDungeons"), mWindowListener(nullptr)
    {
    }

    void setWindowListener(ODFrameListener* listener)
    {
        mWindowListener = listener;
    }

    void windowResized(Ogre::RenderWindow* window) override
    {
        if(mWindowListener != nullptr)
            mWindowListener->windowResized(window);
    }

protected:
    void createRoot() override
    {
        mRoot = new Ogre::Root(ResourceManager::getSingleton().getPluginsPath(), "", "");
        mOverlaySystem = new Ogre::OverlaySystem();
    }

    bool oneTimeConfig() override
    {
        return ConfigManager::getSingleton().initVideoConfig(*mRoot);
    }

    void setup() override
    {
        mRoot->initialise(false);
        createWindow("OpenDungeons " + ODApplication::VERSION);
    }

private:
    ODFrameListener* mWindowListener;
};

struct ApplicationContextDeleter
{
    void operator()(ODApplicationContext* context) const
    {
        if(context != nullptr)
        {
            context->closeApp();
            delete context;
        }
    }
};
} // namespace
#endif

void ODApplication::startGame(boost::program_options::variables_map& options)
{
    ResourceManager resMgr(options);

    LogManager logMgr;
    logMgr.setLevel(resMgr.getLogLevel());

    logMgr.addSink(std::unique_ptr<LogSink>(new LogSinkConsole()));
    logMgr.addSink(std::unique_ptr<LogSink>(new LogSinkFile(resMgr.getLogFile())));

    if(resMgr.isServerMode())
        startServer();
    else
        startClient();
}

void ODApplication::startServer()
{
    ResourceManager& resMgr = ResourceManager::getSingleton();

    OD_LOG_INF("Initializing");

    Random::initialize();
    ConfigManager configManager(resMgr.getConfigPath(), "", resMgr.getSoundPath());
    OD_LOG_INF("Launching server");

    const std::string& creator = resMgr.getServerModeCreator();

    ODServer server;
    if(!server.startServer(creator, resMgr.getServerModeLevel(), ServerMode::ModeGameMultiPlayer, !creator.empty()))
    {
        OD_LOG_ERR("Could not start server !!!");
        return;
    }

    if(!server.waitEndGame())
    {
        OD_LOG_ERR("Could not wait for end of game !!!");
        return;
    }

    OD_LOG_INF("Stopping server...");
    server.stopServer();
}

void ODApplication::startClient()
{
    ResourceManager& resMgr = ResourceManager::getSingleton();

    {
        //NOTE: This prevents a segmentation fault from OpenGL on exit.
        //Creating the object sets up an OpenAL context using a static object
        //contained in a function. If this is done after initialising Ogre::Root
        //the application segfaults on exit for some reason.
        sf::Music m;
    }
    Random::initialize();
    //NOTE: The order of initialisation of the different "manager" classes is important,
    //as many of them depend on each other.
    OD_LOG_INF("Initializing Ogre; Plugins path: " + resMgr.getPluginsPath());

    ConfigManager configManager(resMgr.getConfigPath(), resMgr.getUserCfgFile(),
        resMgr.getSoundPath());

    Ogre::Root* ogreRoot = nullptr;
    Ogre::RenderWindow* renderWindow = nullptr;
    Ogre::OverlaySystem* overlaySystem = nullptr;

#ifdef OD_USE_SFML_WINDOW
    Ogre::Root sfmlRoot(resMgr.getPluginsPath(), "");

    ogreRoot = &sfmlRoot;
    if (!configManager.initVideoConfig(*ogreRoot))
        return;

    // Needed for the TextRenderer and the Render Manager
    Ogre::OverlaySystem sfmlOverlaySystem;
    overlaySystem = &sfmlOverlaySystem;

    const std::string windowTitle = "OpenDungeons " + VERSION;

    OD_LOG_INF("Creating window...");
    const unsigned int MIN_WIDTH = 300;
    const unsigned int MIN_HEIGHT = 200;

    // Get width/height values from config
    unsigned int w = MIN_WIDTH;
    unsigned int h = MIN_HEIGHT;
    {
        auto videoMode = configManager.getVideoValue(Config::VIDEO_MODE, "1280 x 720", false);
        std::stringstream ss(videoMode);
        // Ignore the x in the middle
        char ignore;
        ss >> w >> ignore >> h >> h;

        w = std::max(w, MIN_WIDTH);
        h = std::max(h, MIN_HEIGHT);
    }

    // Check if the config specifies fullscreen or windowed
    auto style = configManager.getVideoValue(Config::FULL_SCREEN, "No", false) == "Yes" && sf::VideoMode(w, h).isValid() ? sf::Style::Fullscreen : sf::Style::Default;

    // Create an SFML window
    // we make sure to grab the right bit depth from the current desktop mode as otherwise full screen
    // can end up not working properly.
    // TODO: Check anti-aliasing settings here.
    sf::RenderWindow sfmlWindow(sf::VideoMode(w, h, sf::VideoMode::getDesktopMode().bitsPerPixel)
                                , windowTitle, style, sf::ContextSettings(32, 0, 2, 1));

    ogreRoot->initialise(false);

    renderWindow = [&](){
        Ogre::NameValuePairList misc;
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
        auto winHandle = reinterpret_cast<size_t>(sfmlWindow.getSystemHandle());
        auto winGlContext = reinterpret_cast<size_t>(wglGetCurrentContext());
        misc["externalWindowHandle"] = Helper::toString(winHandle);
        misc["externalGLContext"] = Helper::toString(winGlContext);
        misc["externalGLControl"] = Ogre::String("True");
#else
        misc["currentGLContext"] = Ogre::String("true");
#endif
        return ogreRoot->createRenderWindow(windowTitle, w, h, style == sf::Style::Fullscreen, &misc);
    }();
    renderWindow->setVisible(true);
#else
    // ApplicationContext creates the Ogre root and the main window.
    std::unique_ptr<ODApplicationContext, ApplicationContextDeleter> appContext(new ODApplicationContext());
    appContext->initApp();

    ogreRoot = appContext->getRoot();
    renderWindow = appContext->getRenderWindow();
    overlaySystem = appContext->getOverlaySystem();
    if ((ogreRoot == nullptr) || (renderWindow == nullptr) || (overlaySystem == nullptr))
        return;
#endif /* OD_USE_SFML_WINDOW */


    //NOTE: This is currently done here as it has to be done after initialising mRoot,
    // but before running initialiseAllResourceGroups()
    resMgr.setupOgreResources(ogreRoot->getRenderSystem()->getNativeShadingLanguageVersion());

    // Setup Icon (On Windows)
    // NOTE: On linux at least, the icon is usually handled through desktop files.
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
#define IDI_ICON1 1 // See dist/icon.rc to know the resource number.
    HWND hwnd;
    renderWindow->getCustomAttribute("WINDOW", static_cast<void*>(&hwnd));
    HINSTANCE hInst = static_cast<HINSTANCE>(GetModuleHandle(nullptr));
    SetClassLong(hwnd, GCL_HICON, reinterpret_cast<LONG>(LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1))));
#endif

    //Initialise RTshader system
    // IMPORTANT: This needs to be initialized BEFORE the resource groups.
    // eg: Ogre::ResourceGroupManager::getSingletonPtr()->initialiseAllResourceGroups();
    // but after the render window, eg: mRoot->initialise();
    // This advice was taken from here:
    // http://www.ogre3d.org/forums/viewtopic.php?p=487445#p487445
    if (!Ogre::RTShader::ShaderGenerator::initialize())
    {
        OD_LOG_ERR("FATAL:"
                "Failed to initialize the Real Time Shader System, exiting");
        return;
    }
#if OGRE_VERSION > 0x10A00
    auto sgListener = new OgreBites::SGTechniqueResolverListener(Ogre::RTShader::ShaderGenerator::getSingletonPtr());
    Ogre::MaterialManager::getSingleton().addListener(sgListener);
#endif

    Ogre::ResourceGroupManager::getSingletonPtr()->initialiseAllResourceGroups();

    MusicPlayer musicPlayer(resMgr.getMusicPath(), resMgr.listAllMusicFiles());
    SoundEffectsManager soundEffectsManager;

    ODServer server;
    ODClient client;

    RenderManager renderManager(overlaySystem);

    Ogre::MaterialManager::getSingleton().setActiveScheme(Ogre::RTShader::ShaderGenerator::DEFAULT_SCHEME_NAME);
    Gui gui(&soundEffectsManager, resMgr.getCeguiLogFile(), *renderWindow);
    TextRenderer textRenderer;
    textRenderer.addTextBox("DebugMessages", ODApplication::MOTD.c_str(), 840,
                                30, 50, 70, Ogre::ColourValue::Green);
    textRenderer.addTextBox(ODApplication::POINTER_INFO_STRING, "",
                                0, 0, 200, 50, Ogre::ColourValue::White);

    ODFrameListener frameListener(resMgr.getConfigPath() + "mainmenuscene.cfg",
        renderWindow, overlaySystem, &gui);

    ogreRoot->addFrameListener(&frameListener);

#ifndef OD_USE_SFML_WINDOW
    // Input, window resize notifications and event pumping all go through the
    // ApplicationContext.
    appContext->setWindowListener(&frameListener);
    appContext->addInputListener(&frameListener.getModeManager()->getInputManager());
    ogreRoot->addFrameListener(appContext.get());
#endif

#ifdef OD_USE_SFML_WINDOW
    bool running = true;
    while(running)
    {
        sf::Event event;
        while (sfmlWindow.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
            {
                frameListener.requestExit();
                break;
            }
            else
            {
                if (event.type == sf::Event::Resized)
                {
                    renderWindow->resize(event.size.width, event.size.height);
                    frameListener.windowResized(renderWindow);
                }
                frameListener.getModeManager()->getInputManager().handleSFMLEvent(event);
            }
        }
        sfmlWindow.clear();
        // If renderOneFrame returns false, it indicates that an exit has been requested
        running = ogreRoot->renderOneFrame();
        sfmlWindow.display();
    }
#else /* OD_USE_SFML_WINDOW */
    ogreRoot->startRendering();
#endif /* OD_USE_SFML_WINDOW */

    OD_LOG_INF("Disconnecting client...");
    client.disconnect();
    OD_LOG_INF("Stopping server...");
    server.stopServer();
#ifndef OD_USE_SFML_WINDOW
    appContext->removeInputListener(&frameListener.getModeManager()->getInputManager());
    ogreRoot->removeFrameListener(appContext.get());
#endif
    ogreRoot->removeFrameListener(&frameListener);
    Ogre::MaterialManager::getSingleton().removeListener(sgListener);
    delete sgListener;
    Ogre::RTShader::ShaderGenerator::destroy();
#ifdef OD_USE_SFML_WINDOW
    ogreRoot->destroyRenderTarget(renderWindow);
#endif
}

//TODO: find some better places for some of these
double ODApplication::turnsPerSecond = 1.4;
#ifdef OD_VERSION
const std::string ODApplication::VERSION = OD_VERSION;
#else
const std::string ODApplication::VERSION = "undefined";
#endif
const std::string ODApplication::VERSIONSTRING = "OpenDungeons_Version:" + VERSION;
std::string ODApplication::MOTD = "Welcome to Open Dungeons\tVersion:  " + VERSION;
const std::string ODApplication::POINTER_INFO_STRING = "pointerInfo";
