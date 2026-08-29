#pragma once

#include "WindowsCommand.h"

class WindowsCommandExecutor final
{
public:
    static bool Execute(WindowsCommand command);
};
