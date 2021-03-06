/*
Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>
This file is released under the MIT license. See README-MIT for more information.
*/

#pragma once

#include "Threading/AEThread.h"

class WorldRunnable
{
    std::unique_ptr<AscEmu::Threading::AEThread> m_thread;
    void threadRunner(AscEmu::Threading::AEThread& thread);
    void threadInit();

public:
    WorldRunnable();
    ~WorldRunnable();

    void threadShutdown();
};
