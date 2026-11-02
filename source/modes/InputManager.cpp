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

#include "InputManager.h"

#include "modes/AbstractApplicationMode.h"
#include "modes/SFMLToOISListener.h"
#include "utils/LogManager.h"
#include "utils/MakeUnique.h"

#include <CEGUI/GUIContext.h>
#include <CEGUI/System.h>
#include <OISKeyboard.h>
#include <OgreRenderWindow.h>

#include <cstdint>

namespace
{
// SDL key codes that OgreBites does not expose by name.
const OgreBites::Keycode SDLK_LCTRL = (1 << 30) | 0xE0;
const OgreBites::Keycode SDLK_LALT = (1 << 30) | 0xE2;
const OgreBites::Keycode SDLK_LGUI = (1 << 30) | 0xE3;
const OgreBites::Keycode SDLK_RCTRL = (1 << 30) | 0xE4;
const OgreBites::Keycode SDLK_RSHIFT = (1 << 30) | 0xE5;
const OgreBites::Keycode SDLK_RALT = (1 << 30) | 0xE6;
const OgreBites::Keycode SDLK_RGUI = (1 << 30) | 0xE7;
const OgreBites::Keycode SDLK_APPLICATION = (1 << 30) | 0x65;
const OgreBites::Keycode SDLK_CAPSLOCK = (1 << 30) | 0x39;

OIS::KeyCode toOisKey(OgreBites::Keycode key)
{
    static const OIS::KeyCode letterKeys[] =
    {
        OIS::KC_A, OIS::KC_B, OIS::KC_C, OIS::KC_D, OIS::KC_E, OIS::KC_F,
        OIS::KC_G, OIS::KC_H, OIS::KC_I, OIS::KC_J, OIS::KC_K, OIS::KC_L,
        OIS::KC_M, OIS::KC_N, OIS::KC_O, OIS::KC_P, OIS::KC_Q, OIS::KC_R,
        OIS::KC_S, OIS::KC_T, OIS::KC_U, OIS::KC_V, OIS::KC_W, OIS::KC_X,
        OIS::KC_Y, OIS::KC_Z
    };
    static const OIS::KeyCode digitKeys[] =
    {
        OIS::KC_0, OIS::KC_1, OIS::KC_2, OIS::KC_3, OIS::KC_4,
        OIS::KC_5, OIS::KC_6, OIS::KC_7, OIS::KC_8, OIS::KC_9
    };

    if((key >= 'a') && (key <= 'z'))
        return letterKeys[key - 'a'];
    if((key >= '0') && (key <= '9'))
        return digitKeys[key - '0'];

    switch(key)
    {
        case OgreBites::SDLK_ESCAPE: return OIS::KC_ESCAPE;
        case '\t': return OIS::KC_TAB;
        case '\b': return OIS::KC_BACK;
        case OgreBites::SDLK_RETURN: return OIS::KC_RETURN;
        case OgreBites::SDLK_SPACE: return OIS::KC_SPACE;
        case '`': return OIS::KC_GRAVE;
        case '-': return OIS::KC_MINUS;
        case '=': return OIS::KC_EQUALS;
        case '[': return OIS::KC_LBRACKET;
        case ']': return OIS::KC_RBRACKET;
        case '\\': return OIS::KC_BACKSLASH;
        case ';': return OIS::KC_SEMICOLON;
        case '\'': return OIS::KC_APOSTROPHE;
        case ',': return OIS::KC_COMMA;
        case '.': return OIS::KC_PERIOD;
        case '/': return OIS::KC_SLASH;
        case OgreBites::SDLK_F1: return OIS::KC_F1;
        case OgreBites::SDLK_F2: return OIS::KC_F2;
        case OgreBites::SDLK_F3: return OIS::KC_F3;
        case OgreBites::SDLK_F4: return OIS::KC_F4;
        case OgreBites::SDLK_F5: return OIS::KC_F5;
        case OgreBites::SDLK_F6: return OIS::KC_F6;
        case OgreBites::SDLK_F7: return OIS::KC_F7;
        case OgreBites::SDLK_F8: return OIS::KC_F8;
        case OgreBites::SDLK_F9: return OIS::KC_F9;
        case OgreBites::SDLK_F10: return OIS::KC_F10;
        case OgreBites::SDLK_F11: return OIS::KC_F11;
        case OgreBites::SDLK_F12: return OIS::KC_F12;
        case OgreBites::SDLK_PRINTSCREEN: return OIS::KC_SYSRQ;
        case OgreBites::SDLK_SCROLLLOCK: return OIS::KC_SCROLL;
        case OgreBites::SDLK_PAUSE: return OIS::KC_PAUSE;
        case OgreBites::SDLK_INSERT: return OIS::KC_INSERT;
        case OgreBites::SDLK_HOME: return OIS::KC_HOME;
        case OgreBites::SDLK_PAGEUP: return OIS::KC_PGUP;
        case OgreBites::SDLK_DELETE: return OIS::KC_DELETE;
        case OgreBites::SDLK_END: return OIS::KC_END;
        case OgreBites::SDLK_PAGEDOWN: return OIS::KC_PGDOWN;
        case OgreBites::SDLK_RIGHT: return OIS::KC_RIGHT;
        case OgreBites::SDLK_LEFT: return OIS::KC_LEFT;
        case OgreBites::SDLK_DOWN: return OIS::KC_DOWN;
        case OgreBites::SDLK_UP: return OIS::KC_UP;
        case OgreBites::SDLK_NUMLOCKCLEAR: return OIS::KC_NUMLOCK;
        case OgreBites::SDLK_KP_DIVIDE: return OIS::KC_DIVIDE;
        case OgreBites::SDLK_KP_MULTIPLY: return OIS::KC_MULTIPLY;
        case OgreBites::SDLK_KP_MINUS: return OIS::KC_SUBTRACT;
        case OgreBites::SDLK_KP_PLUS: return OIS::KC_ADD;
        case OgreBites::SDLK_KP_ENTER: return OIS::KC_NUMPADENTER;
        case OgreBites::SDLK_KP_1: return OIS::KC_NUMPAD1;
        case OgreBites::SDLK_KP_2: return OIS::KC_NUMPAD2;
        case OgreBites::SDLK_KP_3: return OIS::KC_NUMPAD3;
        case OgreBites::SDLK_KP_4: return OIS::KC_NUMPAD4;
        case OgreBites::SDLK_KP_5: return OIS::KC_NUMPAD5;
        case OgreBites::SDLK_KP_6: return OIS::KC_NUMPAD6;
        case OgreBites::SDLK_KP_7: return OIS::KC_NUMPAD7;
        case OgreBites::SDLK_KP_8: return OIS::KC_NUMPAD8;
        case OgreBites::SDLK_KP_9: return OIS::KC_NUMPAD9;
        case OgreBites::SDLK_KP_0: return OIS::KC_NUMPAD0;
        case OgreBites::SDLK_KP_PERIOD: return OIS::KC_DECIMAL;
        case OgreBites::SDLK_LSHIFT: return OIS::KC_LSHIFT;
        case SDLK_RSHIFT: return OIS::KC_RSHIFT;
        case SDLK_LCTRL: return OIS::KC_LCONTROL;
        case SDLK_RCTRL: return OIS::KC_RCONTROL;
        case SDLK_LALT: return OIS::KC_LMENU;
        case SDLK_RALT: return OIS::KC_RMENU;
        case SDLK_LGUI: return OIS::KC_LWIN;
        case SDLK_RGUI: return OIS::KC_RWIN;
        case SDLK_APPLICATION: return OIS::KC_APPS;
        case SDLK_CAPSLOCK: return OIS::KC_CAPITAL;
        default: return OIS::KC_UNASSIGNED;
    }
}

OIS::MouseButtonID toOisButton(int button)
{
    switch(button)
    {
        case OgreBites::BUTTON_LEFT: return OIS::MB_Left;
        case OgreBites::BUTTON_MIDDLE: return OIS::MB_Middle;
        case OgreBites::BUTTON_RIGHT: return OIS::MB_Right;
        default: return OIS::MB_Left;
    }
}

uint32_t decodeUtf8(const char* text)
{
    if(text == nullptr)
        return 0;

    const unsigned char* c = reinterpret_cast<const unsigned char*>(text);
    if(c[0] < 0x80)
        return c[0];
    if((c[0] & 0xE0) == 0xC0)
        return ((c[0] & 0x1F) << 6) | (c[1] & 0x3F);
    if((c[0] & 0xF0) == 0xE0)
        return ((c[0] & 0x0F) << 12) | ((c[1] & 0x3F) << 6) | (c[2] & 0x3F);
    if((c[0] & 0xF8) == 0xF0)
        return ((c[0] & 0x07) << 18) | ((c[1] & 0x3F) << 12)
            | ((c[2] & 0x3F) << 6) | (c[3] & 0x3F);
    return 0;
}
} // namespace

InputManager::InputManager(Ogre::RenderWindow* renderWindow):
    mKeyboard(nullptr),
    mLMouseDown(false),
    mRMouseDown(false),
    mMMouseDown(false),
    mMouseDownOnCEGUIWindow(false),
    mKeeperHandPos(Ogre::Vector3::ZERO),
    mXPos(0),
    mYPos(0),
    mLStartDragX(0),
    mLStartDragY(0),
    mRStartDragX(0),
    mRStartDragY(0),
    mSeatIdSelected(0),
    mCommandState(InputCommandState::infoOnly),
    mCurrentAMode(nullptr)
#ifdef OD_USE_SFML_WINDOW
    ,
    mListener(Utils::make_unique<SFMLToOISListener>(mCurrentAMode, renderWindow->getWidth(), renderWindow->getHeight()))
#endif
{
    OD_LOG_INF("*** Initializing Input Manager ***");

    mMouseState.width = static_cast<int>(renderWindow->getWidth());
    mMouseState.height = static_cast<int>(renderWindow->getHeight());

    for (int i = 0; i < 10; ++i)
    {
        mHotkeyLocationIsValid[i] = false;
        mHotkeyLocation[i] = Ogre::Vector3::ZERO;
    }

    mKeyboard.reset(new Keyboard());
}

InputManager::~InputManager()
{
    OD_LOG_INF("*** Destroying Input Manager ***");
}

void InputManager::setWidthAndHeight(int width, int height)
{
    mMouseState.width = width;
    mMouseState.height = height;
}

void InputManager::setCurrentAMode(AbstractApplicationMode& mode)
{
    mCurrentAMode = &mode;
#ifdef OD_USE_SFML_WINDOW
    mListener->setReceiver(&mode);
#endif
}

void InputManager::handleSFMLEvent(const sf::Event& evt)
{
#ifdef OD_USE_SFML_WINDOW
    mListener->handleEvent(evt);
#else
    OD_LOG_ERR("TRIED TO USE SFML EVENTS BUT THEY ARE DISABLED!");
#endif
}

bool InputManager::mouseMoved(const OgreBites::MouseMotionEvent& evt)
{
    if(mCurrentAMode == nullptr)
        return false;

    mMouseState.X.abs = evt.x;
    mMouseState.Y.abs = evt.y;
    mMouseState.Z.rel = 0;
    return mCurrentAMode->mouseMoved(OIS::MouseEvent(nullptr, mMouseState));
}

bool InputManager::mouseWheelRolled(const OgreBites::MouseWheelEvent& evt)
{
    if(mCurrentAMode == nullptr)
        return false;

    mMouseState.Z.rel = evt.y * 120;
    return mCurrentAMode->mouseMoved(OIS::MouseEvent(nullptr, mMouseState));
}

bool InputManager::mousePressed(const OgreBites::MouseButtonEvent& evt)
{
    if(mCurrentAMode == nullptr)
        return false;

    const OIS::MouseButtonID button = toOisButton(evt.button);
    mMouseState.X.abs = evt.x;
    mMouseState.Y.abs = evt.y;
    mMouseState.buttons |= (1 << static_cast<int>(button));
    return mCurrentAMode->mousePressed(OIS::MouseEvent(nullptr, mMouseState), button);
}

bool InputManager::mouseReleased(const OgreBites::MouseButtonEvent& evt)
{
    if(mCurrentAMode == nullptr)
        return false;

    const OIS::MouseButtonID button = toOisButton(evt.button);
    mMouseState.X.abs = evt.x;
    mMouseState.Y.abs = evt.y;
    mMouseState.buttons &= ~(1 << static_cast<int>(button));
    return mCurrentAMode->mouseReleased(OIS::MouseEvent(nullptr, mMouseState), button);
}

bool InputManager::keyPressed(const OgreBites::KeyboardEvent& evt)
{
    mKeyboard->setModifiers(evt.keysym.mod);
    if(mCurrentAMode == nullptr)
        return false;

    return mCurrentAMode->keyPressed(OIS::KeyEvent(nullptr, toOisKey(evt.keysym.sym), 0));
}

bool InputManager::keyReleased(const OgreBites::KeyboardEvent& evt)
{
    mKeyboard->setModifiers(evt.keysym.mod);
    if(mCurrentAMode == nullptr)
        return false;

    return mCurrentAMode->keyReleased(OIS::KeyEvent(nullptr, toOisKey(evt.keysym.sym), 0));
}

bool InputManager::textInput(const OgreBites::TextInputEvent& evt)
{
    if(mCurrentAMode == nullptr)
        return false;

    const uint32_t codepoint = decodeUtf8(evt.chars);
    if(codepoint == 0)
        return false;

    CEGUI::System::getSingleton().getDefaultGUIContext().injectChar(
        static_cast<CEGUI::String::value_type>(codepoint));
    return true;
}
