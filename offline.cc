/*
 * Copyright (c) 2013 Michael Demmer <demmer@gmail.com>
 *
 * This file is part of Shadowfs
 *
 * Shadowfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * Shadowfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Shadowfs.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shadowfs.h"

bool all_online = true;

void
toggle_all_offline(int)
{
    if (all_online) {
        syslog(LOG_NOTICE, "putting all filesystems offline\n");
        all_online = false;
    } else {
        syslog(LOG_NOTICE, "putting all filesystems online\n");
        all_online = true;
    }
}

bool
is_offline(const char* path)
{
    if (! all_online) {
        return true;
    }

    // Special-case hack for .glimpse files to keep them only on the
    // local FS for efficiency by pretending they're "offline"
    if (strstr(path, "/.glimpse_")) {
        return true;
    }

    // Ditto for .git files
    if (strstr(path, "/.git/")) {
        return true;
    }
    
    // Ditto for the Mac's ._ files
    if (strstr(path, "/._")) {
        return true;
    }

    // Ditto for .svn/lock files
    if (strstr(path, "/.svn/lock")) {
        return true;
    }
    
    return false;
}
