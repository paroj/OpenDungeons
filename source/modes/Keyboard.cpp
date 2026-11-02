#ifdef OD_USE_SFML_WINDOW
#include <SFML/Window/Keyboard.hpp>
#else
#include <OgreInput.h>
#endif

#include "Keyboard.h"

bool Keyboard::isModifierDown(OIS::Keyboard::Modifier code)
{
#ifdef OD_USE_SFML_WINDOW
    switch(code)
    {
    // SFML distinguishes between left and right keys (which OIS doesn't) so we check both.
        case OIS::Keyboard::Alt:
            return sf::Keyboard::isKeyPressed(sf::Keyboard::LAlt) || sf::Keyboard::isKeyPressed(sf::Keyboard::RAlt);
        case OIS::Keyboard::Ctrl:
            return sf::Keyboard::isKeyPressed(sf::Keyboard::LControl) || sf::Keyboard::isKeyPressed(sf::Keyboard::LControl);
        case OIS::Keyboard::Shift:
            return sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) || sf::Keyboard::isKeyPressed(sf::Keyboard::RShift);
        default:
            break;
    }

    return false;
#else
    switch(code)
    {
        case OIS::Keyboard::Alt:
            return (mModifiers & OgreBites::KMOD_ALT) != 0;
        case OIS::Keyboard::Ctrl:
            return (mModifiers & OgreBites::KMOD_CTRL) != 0;
        case OIS::Keyboard::Shift:
            return (mModifiers & OgreBites::KMOD_SHIFT) != 0;
        default:
            break;
    }

    return false;
#endif
}
