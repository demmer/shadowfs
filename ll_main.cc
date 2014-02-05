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
#include <cstdlib>
#include <signal.h>
#include <fuse/fuse_lowlevel.h>

MountTable _mtab;
std::string DATA_DIR;
int debug = 0;

extern struct fuse_lowlevel_ops shadow_ll_ops;
extern void init_shadow_ll_ops();

int
read_mounts()
{
    DIR *dp;
    struct dirent *de;

    std::string config_path = DATA_DIR + ".config";
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

        std::string link_file = DATA_DIR + ".config/" + de->d_name;
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
//         debugfd = fopen("/tmp/shadowfs.log", "a");
        debugfd = stderr;
        debug = 1;
    } else {
        syslog(LOG_NOTICE, "disabling debug mode");
        fclose(debugfd);
        debug = 0;
    }
}

int main(int argc, char *argv[])
{
    DATA_DIR = std::string(getenv("HOME")) + "/ll_shadowfs_data/";
    
    openlog("shadowfs", LOG_PID | LOG_NDELAY, LOG_USER);
    syslog(LOG_NOTICE, "shadowfs initializing... (data dir %s)", DATA_DIR.c_str());

    signal(SIGUSR1, toggle_debug);
    signal(SIGUSR2, toggle_all_offline);

    init_shadow_ll_ops();

    toggle_debug(0);

    if (read_mounts() != 0) {
        return -1;
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_chan *ch;
    char *mountpoint;
    int err = -1;

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, NULL) != -1 &&
        (ch = fuse_mount(mountpoint, &args)) != NULL) {
        struct fuse_session *se;

        se = fuse_lowlevel_new(&args, &shadow_ll_ops,
                               sizeof(shadow_ll_ops), NULL);
        if (se != NULL) {
            if (fuse_set_signal_handlers(se) != -1) {
                fuse_session_add_chan(se, ch);
                err = fuse_session_loop(se);
                fuse_remove_signal_handlers(se);
                fuse_session_remove_chan(ch);
            }
            fuse_session_destroy(se);
        }
        fuse_unmount(mountpoint, ch);
    }
    fuse_opt_free_args(&args);
    
    return err ? 1 : 0;
}
