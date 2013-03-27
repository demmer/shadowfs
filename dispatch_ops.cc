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

static struct fuse_operations*
dispatch(const char* path)
{
    std::string root = root_dir(path);

    if (root == "") {
        return &root_ops;
    }
    
//     if (root == ".config") {
//         return &config_ops;
//     }

    MountTable::iterator iter = _mtab.find(root);
    if (iter != _mtab.end()) {
        return &shadow_ops;
    }

    return NULL;
}

static int do_dispatch_getattr(const char *path, struct stat *stbuf)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->getattr == NULL) {
        return -ENOSYS;
    }
    return ops->getattr(path, stbuf);
}

static int dispatch_getattr(const char *path, struct stat *stbuf)
{
//    dsyslog("getattr(%s)...\n", path);
    int ret = do_dispatch_getattr(path, stbuf);
    if (ret >= 0) {
//        dsyslog("getattr(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("getattr(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}

static int do_dispatch_access(const char *path, int mask)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->access == NULL) {
        return -ENOSYS;
    }
    return ops->access(path, mask);
}

static int dispatch_access(const char *path, int mask)
{
    dsyslog("access(%s)...\n", path);
    int ret = do_dispatch_access(path, mask);
    if (ret >= 0) {
        dsyslog("access(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("access(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_readlink(const char *path, char *buf, size_t size)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->readlink == NULL) {
        return -ENOSYS;
    }
    return ops->readlink(path, buf, size);
}

static int dispatch_readlink(const char *path, char *buf, size_t size)
{
    dsyslog("readlink(%s)...\n", path);
    int ret = do_dispatch_readlink(path, buf, size);
    if (ret >= 0) {
        dsyslog("readlink(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("readlink(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                               off_t offset, struct fuse_file_info *fi)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->readdir == NULL) {
        return -ENOSYS;
    }
    return ops->readdir(path, buf, filler, offset, fi);
}

static int dispatch_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                            off_t offset, struct fuse_file_info *fi)
{
    dsyslog("readdir(%s)...\n", path);
    int ret = do_dispatch_readdir(path, buf, filler, offset, fi);
    if (ret >= 0) {
        dsyslog("readdir(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("readdir(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_mknod(const char *path, mode_t mode, dev_t rdev)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->mknod == NULL) {
        return -ENOSYS;
    }
    return ops->mknod(path, mode, rdev);
}

static int dispatch_mknod(const char *path, mode_t mode, dev_t rdev)
{
    dsyslog("mknod(%s)...\n", path);
    int ret = do_dispatch_mknod(path, mode, rdev);
    if (ret >= 0) {
        dsyslog("mknod(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("mknod(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_mkdir(const char *path, mode_t mode)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->mkdir == NULL) {
        return -ENOSYS;
    }
    return ops->mkdir(path, mode);
}

static int dispatch_mkdir(const char *path, mode_t mode)
{
    dsyslog("mkdir(%s)...\n", path);
    int ret = do_dispatch_mkdir(path, mode);
    if (ret >= 0) {
        dsyslog("mkdir(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("mkdir(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_unlink(const char *path)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->unlink == NULL) {
        return -ENOSYS;
    }
    return ops->unlink(path);
}

static int dispatch_unlink(const char *path)
{
    dsyslog("unlink(%s)...\n", path);
    int ret = do_dispatch_unlink(path);
    if (ret >= 0) {
        dsyslog("unlink(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("unlink(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_rmdir(const char *path)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->rmdir == NULL) {
        return -ENOSYS;
    }
    return ops->rmdir(path);
}

static int dispatch_rmdir(const char *path)
{
    dsyslog("rmdir(%s)...\n", path);
    int ret = do_dispatch_rmdir(path);
    if (ret >= 0) {
        dsyslog("rmdir(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("rmdir(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_symlink(const char *from, const char *to)
{
    struct fuse_operations* ops = dispatch(to);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->symlink == NULL) {
        return -ENOSYS;
    }
    return ops->symlink(from, to);
}

static int dispatch_symlink(const char *from, const char *to)
{
    dsyslog("symlink(%s -> %s)...\n", from, to);
    int ret = do_dispatch_symlink(from, to);
    if (ret >= 0) {
        dsyslog("symlink(%s -> %s)... OK (ret == %d)\n", from, to, ret);
    } else {
        dsyslog("symlink(%s -> %s)... ERROR %s\n", from, to, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_rename(const char *from, const char *to)
{
    struct fuse_operations* ops = dispatch(to);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->rename == NULL) {
        return -ENOSYS;
    }
    return ops->rename(from, to);
}

static int dispatch_rename(const char *from, const char *to)
{
    dsyslog("rename(%s -> %s)...\n", from, to);
    int ret = do_dispatch_rename(from, to);
    if (ret >= 0) {
        dsyslog("rename(%s -> %s)... OK (ret == %d)\n", from, to, ret);
    } else {
        dsyslog("rename(%s -> %s)... ERROR %s\n", from, to, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_link(const char *from, const char *to)
{
    struct fuse_operations* ops = dispatch(to);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->link == NULL) {
        return -ENOSYS;
    }
    return ops->link(from, to);
}

static int dispatch_link(const char *from, const char *to)
{
    dsyslog("link(%s -> %s)...\n", from, to);
    int ret = do_dispatch_link(from, to);
    if (ret >= 0) {
        dsyslog("link(%s -> %s)... OK (ret == %d)\n", from, to, ret);
    } else {
        dsyslog("link(%s -> %s)... ERROR %s\n", from, to, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_chmod(const char *path, mode_t mode)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->chmod == NULL) {
        return -ENOSYS;
    }
    return ops->chmod(path, mode);
}

static int dispatch_chmod(const char *path, mode_t mode)
{
    dsyslog("chmod(%s)...\n", path);
    int ret = do_dispatch_chmod(path, mode);
    if (ret >= 0) {
        dsyslog("chmod(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("chmod(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_chown(const char *path, uid_t uid, gid_t gid)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->chown == NULL) {
        return -ENOSYS;
    }
    return ops->chown(path, uid, gid);
}

static int dispatch_chown(const char *path, uid_t uid, gid_t gid)
{
    dsyslog("chown(%s)...\n", path);
    int ret = do_dispatch_chown(path, uid, gid);
    if (ret >= 0) {
        dsyslog("chown(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("chown(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_truncate(const char *path, off_t size)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->truncate == NULL) {
        return -ENOSYS;
    }
    return ops->truncate(path, size);
}

static int dispatch_truncate(const char *path, off_t size)
{
    dsyslog("truncate(%s)...\n", path);
    int ret = do_dispatch_truncate(path, size);
    if (ret >= 0) {
        dsyslog("truncate(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("truncate(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_utimens(const char *path, const struct timespec ts[2])
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->utimens == NULL) {
        return -ENOSYS;
    }
    return ops->utimens(path, ts);
}

static int dispatch_utimens(const char *path, const struct timespec ts[2])
{
    dsyslog("utimens(%s)...\n", path);
    int ret = do_dispatch_utimens(path, ts);
    if (ret >= 0) {
        dsyslog("utimens(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("utimens(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_open(const char *path, struct fuse_file_info *fi)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->open == NULL) {
        return -ENOSYS;
    }
    return ops->open(path, fi);
}

static int dispatch_open(const char *path, struct fuse_file_info *fi)
{
    dsyslog("open(%s)...\n", path);
    int ret = do_dispatch_open(path, fi);
    if (ret >= 0) {
        dsyslog("open(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("open(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_read(const char *path, char *buf, size_t size, off_t offset,
                            struct fuse_file_info *fi)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->read == NULL) {
        return -ENOSYS;
    }
    return ops->read(path, buf, size, offset, fi);
}

static int dispatch_read(const char *path, char *buf, size_t size, off_t offset,
                         struct fuse_file_info *fi)
{
    dsyslog("read(%s)...\n", path);
    int ret = do_dispatch_read(path, buf, size, offset, fi);
    if (ret >= 0) {
        dsyslog("read(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("read(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_write(const char *path, const char *buf, size_t size,
                             off_t offset, struct fuse_file_info *fi)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->write == NULL) {
        return -ENOSYS;
    }
    return ops->write(path, buf, size, offset, fi);
}

static int dispatch_write(const char *path, const char *buf, size_t size,
                          off_t offset, struct fuse_file_info *fi)
{
    dsyslog("write(%s)...\n", path);
    int ret = do_dispatch_write(path, buf, size, offset, fi);
    if (ret >= 0) {
        dsyslog("write(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("write(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int dispatch_statfs(const char *path, struct statvfs *stbuf)
{
    int res;
    res = statvfs("/", stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int do_dispatch_release(const char *path, struct fuse_file_info *fi)
{
    dsyslog("release(%s)...\n", path);
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->release == NULL) {
        return -ENOSYS;
    }
    return ops->release(path, fi);
}

static int dispatch_release(const char *path, struct fuse_file_info *fi)
{
    dsyslog("release(%s)...\n", path);
    int ret = do_dispatch_release(path, fi);
    if (ret >= 0) {
        dsyslog("release(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("release(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_fsync(const char *path, int isdatasync,
                             struct fuse_file_info *fi)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->fsync == NULL) {
        return -ENOSYS;
    }
    return ops->fsync(path, isdatasync, fi);
}

static int dispatch_fsync(const char *path, int isdatasync,
                          struct fuse_file_info *fi)
{
    dsyslog("fsync(%s)...\n", path);
    int ret = do_dispatch_fsync(path, isdatasync, fi);
    if (ret >= 0) {
        dsyslog("fsync(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("fsync(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int do_dispatch_setxattr(const char *path, const char *name, const char *value,
                                size_t size, int flags)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->setxattr == NULL) {
        return -ENOSYS;
    }
    return ops->setxattr(path, name, value, size, flags);
}

static int dispatch_setxattr(const char *path, const char *name, const char *value,
                             size_t size, int flags)
{
    dsyslog("setxattr(%s)...\n", path);
    int ret = do_dispatch_setxattr(path, name, value, size, flags);
    if (ret >= 0) {
        dsyslog("setxattr(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("setxattr(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int dispatch_getxattr(const char *path, const char *name, char *value,
                             size_t size)
{
    dsyslog("getxattr(%s)...\n", path);
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->getxattr == NULL) {
        return -ENOSYS;
    }
    return ops->getxattr(path, name, value, size);
}

static int do_dispatch_listxattr(const char *path, char *list, size_t size)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->listxattr == NULL) {
        return -ENOSYS;
    }
    return ops->listxattr(path, list, size);
}

static int dispatch_listxattr(const char *path, char *list, size_t size)
{
    dsyslog("listxattr(%s)...\n", path);
    int ret = do_dispatch_listxattr(path, list, size);
    if (ret >= 0) {
        dsyslog("listxattr(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("listxattr(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        

static int do_dispatch_removexattr(const char *path, const char *name)
{
    struct fuse_operations* ops = dispatch(path);
    if (ops == NULL) {
        return -ENOENT;
    }

    if (ops->removexattr == NULL) {
        return -ENOSYS;
    }
    return ops->removexattr(path, name);
}

static int dispatch_removexattr(const char *path, const char *name)
{
    dsyslog("removexattr(%s)...\n", path);
    int ret = do_dispatch_removexattr(path, name);
    if (ret >= 0) {
        dsyslog("removexattr(%s)... OK (ret == %d)\n", path, ret);
    } else {
        dsyslog("removexattr(%s)... ERROR %s\n", path, strerror(-ret));
    }
    return ret;
}        
#endif /* HAVE_SETXATTR */

struct fuse_operations dispatch_ops;
void init_dispatch_ops()
{
    memset(&dispatch_ops, 0, sizeof(dispatch_ops));
    dispatch_ops.getattr	= dispatch_getattr;
    dispatch_ops.access		= dispatch_access;
    dispatch_ops.readlink	= dispatch_readlink;
    dispatch_ops.readdir	= dispatch_readdir;
    dispatch_ops.mknod		= dispatch_mknod;
    dispatch_ops.mkdir		= dispatch_mkdir;
    dispatch_ops.symlink	= dispatch_symlink;
    dispatch_ops.unlink		= dispatch_unlink;
    dispatch_ops.rmdir		= dispatch_rmdir;
    dispatch_ops.rename		= dispatch_rename;
    dispatch_ops.link		= dispatch_link;
    dispatch_ops.chmod		= dispatch_chmod;
    dispatch_ops.chown		= dispatch_chown;
    dispatch_ops.truncate	= dispatch_truncate;
    dispatch_ops.utimens	= dispatch_utimens;
    dispatch_ops.open		= dispatch_open;
    dispatch_ops.read		= dispatch_read;
    dispatch_ops.write		= dispatch_write;
    dispatch_ops.statfs		= dispatch_statfs;
    dispatch_ops.release	= dispatch_release;
    dispatch_ops.fsync		= dispatch_fsync;
#ifdef HAVE_SETXATTR
    dispatch_ops.setxattr	= dispatch_setxattr;
    dispatch_ops.getxattr	= dispatch_getxattr;
    dispatch_ops.listxattr	= dispatch_listxattr;
    dispatch_ops.removexattr	= dispatch_removexattr;
#endif
};
