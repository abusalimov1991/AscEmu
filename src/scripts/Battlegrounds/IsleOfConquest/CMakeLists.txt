# Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>

set(PATH_PREFIX IsleOfConquest)

set(SRC_IsleOfConquest
    ${PATH_PREFIX}/IsleOfConquest.cpp
    ${PATH_PREFIX}/IsleOfConquest.h
    ${PATH_PREFIX}/IsleOfConquestDefinitions.h
)

source_group(IsleOfConquest FILES ${SRC_IsleOfConquest})
unset(PATH_PREFIX)
