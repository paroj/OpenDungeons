#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <OISKeyboard.h>

//! \brief A class to abstract between the SFML and OIS keyboard functionality
class Keyboard
{
public:
    bool isModifierDown(OIS::Keyboard::Modifier code);
#ifndef OD_USE_SFML_WINDOW
    void setModifiers(unsigned short modifiers) { mModifiers = modifiers; }

private:
    unsigned short mModifiers = 0;
#endif
};

#endif // KEYBOARD_H
