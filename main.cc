/*
 * Copyright (c) 2009-2013 Riverbed Technology, Inc.
 *
 * This file is part of Shadowfs
 *
 * Shadowfs is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.

 * Shadowfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Shadowfs.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shadowfs.h"
#include <signal.h>

MountTable _mtab;
std::string DATA_DIR;
int debug = 0;

int
read_mounts()
{
    DIR *dp;
    struct dirent *de;

    std::string config_path = DATA_DIR + "/.config";
    dp = opendir(config_path.c_str());
    if (dp == NULL) {
        syslog(LOG_ERR, "error in opendir(%s): %s\n",
                config_path.c_str(), strerror(errno));
        return -1;
    }

    while ((de = readdir(dp)) != NULL) {
        char link_target[1024];
        
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;

        if (de->d_type != DT_LNK) {
            syslog(LOG_ERR, "bad file in config directory: %s not a link\n",
                    de->d_name);
            continue;
        }

        std::string link_file = DATA_DIR + "/.config/" + de->d_name;
        int len = readlink(link_file.c_str(), link_target, sizeof(link_target));
        if (len == -1) {
            syslog(LOG_ERR, "error in readlink(%s): %s\n",
                    link_file.c_str(), strerror(errno));
            continue;
        }
        link_target[len] = '\0';
        
        syslog(LOG_NOTICE, "initializing mount %s -> %s\n", de->d_name, link_target);
        _mtab[de->d_name] = MountInfo(link_target);
    }

    closedir(dp);
    return 0;
}

FILE* debugfd;
static void toggle_debug(int signo) {
    if (! debug)  {
        syslog(LOG_NOTICE, "enabling debug mode");
        debugfd = fopen("/tmp/shadowfs.log", "a");
        debug = 1;
    } else {
        syslog(LOG_NOTICE, "disabling debug mode");
        fclose(debugfd);
        debug = 0;
    }
}

int main(int argc, char *argv[])
{
    DATA_DIR = std::string(getenv("HOME")) + "/shadowfs_data";
    
    openlog("shadowfs", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_NOTICE, "shadowfs initializing... (data dir %s)", DATA_DIR.c_str());

    signal(SIGUSR1, toggle_debug);
    signal(SIGUSR2, toggle_all_offline);
    
    init_dispatch_ops();
    init_root_ops();
    init_shadow_ops();

    if (read_mounts() != 0) {
        return -1;
    }

    umask(0);
    return fuse_main(argc, argv, &dispatch_ops, NULL);
}
