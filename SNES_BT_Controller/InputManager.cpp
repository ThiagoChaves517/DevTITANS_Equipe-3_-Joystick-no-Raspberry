#include "InputManager.h"
#include "SnesState.h"

void InputManager::begin()
{
    buttons.begin();
}

uint16_t InputManager::readState()
{
    uint16_t state = Snes::ALL_RELEASED;

    if (buttons.isBPressed())
    {
        state &= ~(1u << Snes::B);
    }

    if (buttons.isYPressed())
    {
        state &= ~(1u << Snes::Y);
    }

    if (buttons.isSelectPressed())
    {
        state &= ~(1u << Snes::SELECT);
    }

    if (buttons.isStartPressed())
    {
        state &= ~(1u << Snes::START);
    }

    if (buttons.isAPressed())
    {
        state &= ~(1u << Snes::A);
    }

    if (buttons.isXPressed())
    {
        state &= ~(1u << Snes::X);
    }

    return state;
}