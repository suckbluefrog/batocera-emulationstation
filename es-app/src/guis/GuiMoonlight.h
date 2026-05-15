// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GuiSettings.h"

class GuiMoonlight : public GuiSettings
{
public:
    static void show(Window* window);

protected:
    explicit GuiMoonlight(Window* window);
};
