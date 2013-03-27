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

static int root_getattr(const char *path, struct stat *stbuf)
{
    assert(std::string(path) == "/");
    
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = S_IFDIR | 0755;
    stbuf->st_nlink = 1 + _mtab.size();
    return 0;
}

static int root_access(const char *path, int mask)
{
    (void)mask;
    return 0;
}

static int root_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    // For readdir, we can simply copy the contents of the datadir
    // since everything should be properly represented there.
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    std::string data_path = DATA_DIR + path;
    
    dp = opendir(data_path.c_str());
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        if (!strcmp(de->d_name, ".config"))
            continue;
        
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

struct fuse_operations root_ops;
void init_root_ops()
{
    memset(&root_ops, 0, sizeof(root_ops));
    root_ops.getattr	= root_getattr;
    root_ops.access	= root_access;
    root_ops.readdir	= root_readdir;
};
