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
#include <map>

struct ShadowFileState {
    int local_fd;
    int shadow_fd;
    bool offline;
};

typedef std::map<std::string, ShadowFileState*> OpenFileTable;
OpenFileTable open_files_;

static int shadow_getattr(const char *path, struct stat *stbuf)
{
    std::string local_path = std::string(DATA_DIR) + path;

    int res;

    res = lstat(local_path.c_str(), stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int shadow_access(const char *path, int mask)
{
    int res;

    std::string local_path = DATA_DIR + path;

    res = access(local_path.c_str(), mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int shadow_readlink(const char *path, char *buf, size_t size)
{
    std::string local_path = DATA_DIR + path;
    
    int res;

    res = readlink(local_path.c_str(), buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int shadow_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

    dp = opendir(local_path.c_str());
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
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

static int shadow_mknod(const char *path, mode_t mode, dev_t rdev)
{
    std::string local_path = DATA_DIR + path;

    int res;

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
    if (S_ISREG(mode)) {
        dsyslog("mknod(%s): regular file, mode %o\n", local_path.c_str(), mode);
        res = open(local_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(local_path.c_str(), mode);
    else
        res = mknod(local_path.c_str(), mode, rdev);
    if (res == -1)
        return -errno;

    // Need to set permissions to the calling user
    fuse_context* ctx = fuse_get_context();
    chown(local_path.c_str(), ctx->uid, ctx->gid);

    if (is_offline(path)) {
        return 0;
    }
    
    std::string shadow_path = get_shadow_path(path);

    if (S_ISREG(mode)) {
        res = open(shadow_path.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(shadow_path.c_str(), mode);
    else
        res = mknod(shadow_path.c_str(), mode, rdev);
    if (res == -1)
        syslog(LOG_ERR, "error in shadow mknod(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    
    chown(shadow_path.c_str(), ctx->uid, ctx->gid);
    return 0;
}

static int shadow_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    ShadowFileState* info;
    int fd;

    OpenFileTable::iterator iter = open_files_.find(std::string(path));
    if (iter != open_files_.end()) {
        // Something got screwed up in the fuse layer and it didn't
        // properly call release() on the old file handle. The safe
        // thing to do is to keep the ShadowFileState object around,
        // but close the open file descriptors.
        //syslog(LOG_ERR, "file %s opened multiple times (info %p)\n", path, info);
        /*
        if (close(info->local_fd) != 0) {
            syslog(LOG_ERR, "error in close(%d): %s\n",
                   info->local_fd, strerror(errno));
        }

        if (info->shadow_fd != -1 && close(info->shadow_fd) != 0) {
            syslog(LOG_ERR, "error in close(%d): %s\n",
                   info->shadow_fd, strerror(errno));
        }
        */
    }

    fd = creat(local_path.c_str(), mode);
    // TODO remove flags?
    dsyslog("creat(%s) 0x%x returned %d\n", local_path.c_str(), fi->flags, fd);
    if (fd == -1)
        return -errno;

    info = new ShadowFileState();
    fi->fh = (uint64_t)info;

    info->local_fd  = fd;
    info->shadow_fd = -1;
    info->offline   = false;

    open_files_[std::string(path)] = info;

    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        dsyslog("creat(%s) write-mode not set\n", local_path.c_str());
        return 0;
    }

    if (is_offline(path)) {
        info->offline = true;
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    fd = creat(shadow_path.c_str(), fi->flags);
    dsyslog("shadow creat(%s) returned %d\n", shadow_path.c_str(), fd);

    if (fd == -1) {
        syslog(LOG_ERR, "error in shadow creat(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    } else {
        info->shadow_fd = fd;
    }

    return 0;
}

static int shadow_mkdir(const char *path, mode_t mode)
{
    std::string local_path = DATA_DIR + path;
    
    int res;

    res = mkdir(local_path.c_str(), mode);
    if (res == -1)
        return -errno;

    // Need to set permissions to the calling user
    fuse_context* ctx = fuse_get_context();
    chown(local_path.c_str(), ctx->uid, ctx->gid);
    
    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = mkdir(shadow_path.c_str(), mode);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow mkdir(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }

    chown(shadow_path.c_str(), ctx->uid, ctx->gid);
    
    return 0;
}

static int shadow_unlink(const char *path)
{
    std::string local_path = DATA_DIR + path;

    int res;

    res = unlink(local_path.c_str());
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = unlink(shadow_path.c_str());
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow unlink(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }

    return 0;
}

static int shadow_rmdir(const char *path)
{
    std::string local_path = DATA_DIR + path;

    int res;

    res = rmdir(local_path.c_str());
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = rmdir(shadow_path.c_str());
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow rmdir(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }

    return 0;
}

static int shadow_symlink(const char *from, const char *to)
{
    std::string local_to = DATA_DIR + to;

    int res;

    // In this case don't modify from since it should remain unchanged
    // in both copies.
    res = symlink(from, local_to.c_str());
    if (res == -1)
        return -errno;

    // Need to set permissions to the calling user
    fuse_context* ctx = fuse_get_context();
    chown(local_to.c_str(), ctx->uid, ctx->gid);
    
    if (is_offline(to)) {
        return 0;
    }

    std::string shadow_to = get_shadow_path(to);

    res = symlink(from, shadow_to.c_str());
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow symlink(%s -> %s): %s\n",
               from, shadow_to.c_str(), strerror(errno));
    }

    chown(shadow_to.c_str(), ctx->uid, ctx->gid);
    
    return 0;
}

static int shadow_rename(const char *from, const char *to)
{
    std::string local_from = DATA_DIR + from;
    std::string local_to   = DATA_DIR + to;

    int res;

    res = rename(local_from.c_str(), local_to.c_str());
    if (res == -1)
        return -errno;

    if (is_offline(to)) {
        return 0;
    }

    std::string shadow_from = get_shadow_path(from);
    std::string shadow_to   = get_shadow_path(to);

    res = rename(shadow_from.c_str(), shadow_to.c_str());
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow rename(%s -> %s): %s\n",
               from, shadow_to.c_str(), strerror(errno));
    }

    return 0;
}

static int shadow_link(const char *from, const char *to)
{
    std::string local_from = DATA_DIR + from;
    std::string local_to   = DATA_DIR + to;
    
    int res;

    res = link(local_from.c_str(), local_to.c_str());
    if (res == -1)
        return -errno;

    // Need to set permissions to the calling user
    fuse_context* ctx = fuse_get_context();
    chown(local_to.c_str(), ctx->uid, ctx->gid);

    if (is_offline(to)) {
        return 0;
    }

    std::string shadow_from = get_shadow_path(from);
    std::string shadow_to   = get_shadow_path(to);

    res = link(shadow_from.c_str(), shadow_to.c_str());
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow link(%s -> %s): %s\n",
               from, shadow_to.c_str(), strerror(errno));
    }

    chown(shadow_to.c_str(), ctx->uid, ctx->gid);
    
    return 0;
}

static int shadow_chmod(const char *path, mode_t mode)
{
    std::string local_path = DATA_DIR + path;

    int res;

    res = chmod(local_path.c_str(), mode);
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = chmod(shadow_path.c_str(), mode);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow chmod(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    
    return 0;
}

static int shadow_chown(const char *path, uid_t uid, gid_t gid)
{
    std::string local_path = DATA_DIR + path;

    int res;

    res = lchown(local_path.c_str(), uid, gid);
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = chown(shadow_path.c_str(), uid, gid);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow chown(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    return 0;
}

static int shadow_truncate(const char *path, off_t size)
{
    std::string local_path = DATA_DIR + path;

    int res;

    res = truncate(local_path.c_str(), size);
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = truncate(shadow_path.c_str(), size);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow truncate(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    
    return 0;
}

static int shadow_utimens(const char *path, const struct timespec ts[2])
{
    std::string local_path = DATA_DIR + path;

    int res;
    struct timeval tv[2];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

    res = utimes(local_path.c_str(), tv);
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = utimes(shadow_path.c_str(), tv);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow utimes(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    
    return 0;
}

static int shadow_open(const char *path, struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    ShadowFileState* info;
    int fd;

    OpenFileTable::iterator iter = open_files_.find(std::string(path));
    if (iter != open_files_.end()) {
        // Something got screwed up in the fuse layer and it didn't
        // properly call release() on the old file handle. The safe
        // thing to do is to keep the ShadowFileState object around,
        // but close the open file descriptors.
        syslog(LOG_ERR, "file %s opened multiple times (info %p)\n", path, info);
        /*
        if (close(info->local_fd) != 0) {
            syslog(LOG_ERR, "error in close(%d): %s\n",
                   info->local_fd, strerror(errno));
        }

        if (info->shadow_fd != -1 && close(info->shadow_fd) != 0) {
            syslog(LOG_ERR, "error in close(%d): %s\n",
                   info->shadow_fd, strerror(errno));
        }
        */
    }

    fd = open(local_path.c_str(), fi->flags);
    dsyslog("open(%s) 0x%x returned %d\n", local_path.c_str(), fi->flags, fd);
    if (fd == -1)
        return -errno;

    info = new ShadowFileState();
    fi->fh = (uint64_t)info;
    
    info->local_fd  = fd;
    info->shadow_fd = -1;
    info->offline   = false;

    open_files_[std::string(path)] = info;

    if ((fi->flags & O_ACCMODE) == O_RDONLY) {
        dsyslog("open(%s) write-mode not set\n", local_path.c_str());
        return 0;
    }
    
    if (is_offline(path)) {
        info->offline = true;
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    fd = open(shadow_path.c_str(), fi->flags);
    dsyslog("shadow open(%s) returned %d\n", shadow_path.c_str(), fd);

    if (fd == -1) {
        syslog(LOG_ERR, "error in shadow open(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    } else {
        info->shadow_fd = fd;
    }
    
    return 0;
}

static int shadow_read(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    ShadowFileState* info = (ShadowFileState*)fi->fh;
    
    int res = pread(info->local_fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    return res;
}

static int shadow_write(const char *path, const char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    ShadowFileState* info = (ShadowFileState*)fi->fh;
    
    int res = pwrite(info->local_fd, buf, size, offset);
    if (res == -1)
        return -errno;

    if (info->offline) {
        return res;
    }

    if (info->shadow_fd == -1) {
        syslog(LOG_ERR, "shadow write(%s): no fd open for writing\n", path);
        return res;
    }

    int res2 = pwrite(info->shadow_fd, buf, size, offset);

    if (res2 == -1) {
        syslog(LOG_ERR, "error in shadow write(%s): %s\n",
                path, strerror(errno));
    }

    return res;
}

static int shadow_release(const char *path, struct fuse_file_info *fi)
{
    ShadowFileState* info = (ShadowFileState*)fi->fh;

    if (close(info->local_fd) != 0) {
        syslog(LOG_ERR, "error in close(%d): %s\n",
                info->local_fd, strerror(errno));
    }

    if (info->shadow_fd != -1 && close(info->shadow_fd) != 0) {
        syslog(LOG_ERR, "error in close(%d): %s\n",
                info->shadow_fd, strerror(errno));
    }

    delete info;
    fi->fh = 0;

    int n = open_files_.erase(std::string(path));
    if (n != 1) {
        syslog(LOG_ERR, "error in release(%s): %d entries in open file table\n", 
               path, n);
    }
            
    return 0;
}

static int shadow_fsync(const char *path, int isdatasync,
                        struct fuse_file_info *fi)
{
    std::string local_path = DATA_DIR + path;

    ShadowFileState* info = (ShadowFileState*)fi->fh;

    int res = fsync(info->local_fd);
    if (res == -1)
        return -errno;

    if (info->shadow_fd == -1)
        return res;

    int res2 = fsync(info->shadow_fd);

    if (res2 == -1) {
        syslog(LOG_ERR, "error in shadow fsync(%s): %s\n",
                path, strerror(errno));
    }

    return res;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int shadow_setxattr(const char *path, const char *name, const char *value,
                           size_t size, int flags)
{
    std::string local_path = DATA_DIR + path;

    int res = lsetxattr(local_path.c_str(), name, value, size, flags);
    if (res == -1)
        return -errno;

    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = lsetxattr(shadow_path.c_str(), name, value, size, flags);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow setxattr(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    
    return 0;
}

static int shadow_getxattr(const char *path, const char *name, char *value,
                           size_t size)
{
    std::string local_path = DATA_DIR + path;

    int res = lgetxattr(local_path.c_str(), name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int shadow_listxattr(const char *path, char *list, size_t size)
{
    std::string local_path = DATA_DIR + path;

    int res = llistxattr(local_path.c_str(), list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int shadow_removexattr(const char *path, const char *name)
{
    std::string local_path = DATA_DIR + path;

    int res = lremovexattr(local_path.c_str(), name);
    if (res == -1)
        return -errno;
    
    if (is_offline(path)) {
        return 0;
    }

    std::string shadow_path = get_shadow_path(path);

    res = lremovexattr(shadow_path.c_str(), name);
    if (res == -1) {
        syslog(LOG_ERR, "error in shadow setxattr(%s): %s\n",
                shadow_path.c_str(), strerror(errno));
    }
    
    return 0;
}
#endif /* HAVE_SETXATTR */

struct fuse_operations shadow_ops;
void init_shadow_ops()
{
    memset(&shadow_ops, 0, sizeof(shadow_ops));
    shadow_ops.getattr	= shadow_getattr;
    shadow_ops.access		= shadow_access;
    shadow_ops.readlink	= shadow_readlink;
    shadow_ops.readdir	= shadow_readdir;
    shadow_ops.mknod		= shadow_mknod;
    shadow_ops.create       = shadow_create;
    shadow_ops.mkdir		= shadow_mkdir;
    shadow_ops.symlink	= shadow_symlink;
    shadow_ops.unlink		= shadow_unlink;
    shadow_ops.rmdir		= shadow_rmdir;
    shadow_ops.rename		= shadow_rename;
    shadow_ops.link		= shadow_link;
    shadow_ops.chmod		= shadow_chmod;
    shadow_ops.chown		= shadow_chown;
    shadow_ops.truncate	= shadow_truncate;
    shadow_ops.utimens	= shadow_utimens;
    shadow_ops.open		= shadow_open;
    shadow_ops.read		= shadow_read;
    shadow_ops.write		= shadow_write;
    shadow_ops.release	= shadow_release;
    shadow_ops.fsync		= shadow_fsync;
#ifdef HAVE_SETXATTR
    shadow_ops.setxattr	= shadow_setxattr;
    shadow_ops.getxattr	= shadow_getxattr;
    shadow_ops.listxattr	= shadow_listxattr;
    shadow_ops.removexattr	= shadow_removexattr;
#endif
};
