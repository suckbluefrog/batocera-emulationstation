// SPDX-License-Identifier: GPL-2.0-or-later

#include "guis/GuiMoonlight.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "Scripting.h"
#include "SystemConf.h"
#include "ThemeData.h"
#include "Window.h"
#include "components/TextComponent.h"
#include "guis/GuiMsgBox.h"
#include "utils/Platform.h"

void GuiMoonlight::show(Window* window)
{
    window->pushGui(new GuiMoonlight(window));
}

GuiMoonlight::GuiMoonlight(Window* window)
    : GuiSettings(window, _("MOONLIGHT GAME STREAMING"))
{
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    char pin[5];
    std::snprintf(pin, sizeof(pin), "%04d", std::rand() % 10000);

    auto theme = ThemeData::getMenuTheme();
    auto pinUI = std::make_shared<TextComponent>(window, pin, theme->Text.font, theme->Text.color);
    auto runCommand = [](const std::string& cmd) -> int {
        Utils::Platform::ProcessStartInfo command(cmd);
        command.waitForExit = true;
        return command.run();
    };

    addGroup(_("TOOLS"));

    addEntry(_("QUIT CURRENT GAME"), false, [window, runCommand] {
        std::string serverIp = SystemConf::getInstance()->get("moonlight.host");
        if (serverIp.empty())
        {
            window->pushGui(new GuiMsgBox(window, _("Set SERVER IP first.")));
            return;
        }

        runCommand("batocera-moonlight quit");
    });

    addEntry(_("UPDATE MOONLIGHT GAMES"), false, [window, runCommand] {
        std::string serverIp = SystemConf::getInstance()->get("moonlight.host");
        if (serverIp.empty())
        {
            window->pushGui(new GuiMsgBox(window, _("Set SERVER IP first.")));
            return;
        }

        int rc = runCommand("batocera-moonlight init");
        if (rc == 0)
        {
            Scripting::fireEvent("quit", "restart");
            Utils::Platform::quitES(Utils::Platform::QuitMode::QUIT);
        }
        else
            window->pushGui(new GuiMsgBox(window, _("Unable to connect to server")));
    });

    addEntry(_("PAIR WITH SERVER"), false, [window, pin, runCommand] {
        std::string serverIp = SystemConf::getInstance()->get("moonlight.host");
        if (serverIp.empty())
        {
            window->pushGui(new GuiMsgBox(window, _("Set SERVER IP first.")));
            return;
        }

        std::string cmd = std::string("batocera-moonlight pair ") + pin;
        int rc = runCommand(cmd);
        if (rc == 0)
            window->pushGui(new GuiMsgBox(window, _("Successfully paired with server")));
        else
            window->pushGui(new GuiMsgBox(window, _("Unable to pair with server")));
    });

    addEntry(_("UNPAIR WITH SERVER"), false, [window, runCommand] {
        int rc = runCommand("batocera-moonlight clean");
        if (rc == 0)
            window->pushGui(new GuiMsgBox(window, _("Unpaired from server")));
        else
            window->pushGui(new GuiMsgBox(window, _("Unable to unpair from server")));
    });

    addGroup(_("SETTINGS"));
    addInputTextConfigRow(_("SERVER IP"), "moonlight.host", false);
    addWithLabel(_("PAIRING PIN"), pinUI);
}
