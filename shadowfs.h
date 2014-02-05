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
#ifndef _SHADOWFS_H_
#define _SHADOWFS_H_

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#include <assert.h>
#include <syslog.h>
#include <stdarg.h>

#include <map>
#include <string>

extern struct fuse_operations dispatch_ops;
extern struct fuse_operations root_ops;
extern struct fuse_operations shadow_ops;
extern struct fuse_operations config_ops;

extern void init_dispatch_ops();
extern void init_root_ops();
extern void init_shadow_ops();
extern void init_config_ops();

struct MountInfo {
    MountInfo(const std::string& path = "")
        : path_(path), online_(true) {}
    
    std::string path_;
    bool        online_;
};

typedef std::map<std::string, MountInfo> MountTable;
extern MountTable _mtab;

inline std::string root_dir(const char* path)
{
    // skip leading /
    assert(path[0] == '/');
    path += 1;
    
    const char* slash = strchr(path, '/');
    if (slash) {
        return std::string(path, slash - path);
    } else {
        return std::string(path);
    }
}

inline std::string get_shadow_path(const char* path)
{
    std::string root = root_dir(path);
    MountInfo& mi = _mtab[root];
    return mi.path_ + (path + root.length() + 1);
}

extern std::string DATA_DIR;

extern bool is_offline(const char* path);
extern void toggle_all_offline(int);

extern FILE* debugfd;
extern int debug;
//#define dsyslog(args...) if (debug) { syslog(LOG_NOTICE, args); }
#define dsyslog(args...) if (debug) { fprintf(debugfd, args); }

#endif /* _SHADOWFS_H_ */
