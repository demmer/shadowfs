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
#include <algorithm>
#include <dirent.h>
#include <fuse/fuse_lowlevel.h>
#include <map>
#include <vector>

#define ATTR_TIMEOUT 5

#define OPEN_DIRECT_IO false
#define OPEN_KEEP_CACHE false

typedef std::vector<std::string> LinkVector;
struct ShadowInodeState {
    ShadowInodeState(const std::string& path)
        : path_(path), local_fd_(0), shadow_fd_(0), offline_(false) {}

    std::string path_;
    LinkVector links_;
    int local_fd_;
    int shadow_fd_;
    bool offline_;
    struct stat attr;
};

#define WRAPPED_SYSCALL(_syscall, _path, _args...)                      \
do {                                                                    \
    dsyslog("%s %s ...\n", #_syscall, _path);                           \
    int res = _syscall(_path, _args);                                   \
    if (res != 0) {                                                     \
        dsyslog("%s %s ...%s\n", #_syscall, _path, strerror(errno));    \
        fuse_reply_err(req, errno);                                     \
        return;                                                         \
    }                                                                   \
} while (0)

typedef std::map<int, ShadowInodeState*> InodeMap;
InodeMap inode_map_;

typedef std::map<std::string, ShadowInodeState*> PathMap;
PathMap path_map_;

static ShadowInodeState*
lookup_by_inode(fuse_ino_t inode)
{
    InodeMap::iterator iter = inode_map_.find(inode);
    if (iter == inode_map_.end()) {
        return NULL;
    }

    if (inode != FUSE_ROOT_ID) {
        assert(iter->second->attr.st_ino == inode);
    }
    return iter->second;
}

static ShadowInodeState*
lookup_by_path(const std::string& path)
{
    PathMap::iterator iter = path_map_.find(path);
    if (iter == path_map_.end()) {
        return NULL;
    }

    ShadowInodeState* state = iter->second;
    assert(state->path_ == path ||
           std::find(state->links_.begin(), state->links_.end(), path) != state->links_.end());
    
    return state;
}

static bool
add_link(ShadowInodeState* state, const char* name)
{
    if (state->path_ == name) {
        return false;
    }

    if (std::find(state->links_.begin(), state->links_.end(), name) !=
        state->links_.end()) {
        return false;
    }

    state->links_.push_back(name);
    return true;
}

static bool
del_link(ShadowInodeState* state, const char* name)
{
    if (state->path_ == name) {
        state->path_.clear();
        if (state->links_.empty()) {
            dsyslog("del_link: removing path %s: no more links\n", name);
            return true;
        } else {
            state->path_ = state->links_.back();
            state->links_.pop_back();
            dsyslog("del_link: removed path %s: promoted link %s\n",
                    name, state->path_.c_str());
            return false;
        }
    } else {
        LinkVector::iterator lvi =
            std::find(state->links_.begin(), state->links_.end(), name);
        if (lvi == state->links_.end()) {
            // XXX error??
            dsyslog("del_link: error can't find link %s for inode %llu\n",
                    name, state->attr.st_ino);
            return false;
        }

        state->links_.erase(lvi);
        dsyslog("removing link for %s", lvi->c_str());
        return false;
    }
}

static int
gen_entry(fuse_entry_param* ent, const char* op, fuse_ino_t parent,
          const char* name, const std::string& path, bool must_create,
          ShadowInodeState** statep = NULL)
{
    std::string local_path = DATA_DIR + path;
    if (lstat(local_path.c_str(), &ent->attr) != 0) {
        dsyslog("gen_entry(%s) parent inode %lu local path %s: %s\n",
                op, parent, local_path.c_str(), strerror(errno));
        return errno;
    }

    ShadowInodeState* state = lookup_by_inode(ent->attr.st_ino);
    if (state) {
        if (must_create) {
            dsyslog("gen_entry(%s) error: inode %llu exists for parent %lu path %s\n",
                    op, ent->attr.st_ino, parent, path.c_str());
            return EEXIST;
        }
        assert(state->attr.st_ino == ent->attr.st_ino);
        dsyslog("gen_entry(%s): parent inode %lu path %s: found existing entry %llu\n",
                op, parent, local_path.c_str(), state->attr.st_ino);
        
        bool add_path = add_link(state, path.c_str());
        if (add_path) {
            path_map_[path] = state;
        }
    } else {
        state = new ShadowInodeState(path);
        inode_map_[ent->attr.st_ino] = state;
        path_map_[path] = state;
    
        dsyslog("gen_entry(%s) parent inode %lu path %s... created %s -> %llu\n",
                op, parent, path.c_str(), local_path.c_str(), ent->attr.st_ino);
    }

    // always refresh the state attributes
    state->attr = ent->attr;
    
    ent->ino = ent->attr.st_ino;
    ent->generation = 1;
    ent->attr_timeout = ATTR_TIMEOUT;
    ent->entry_timeout = ATTR_TIMEOUT;

    if (statep) {
        *statep = state;
    }
    
    return 0;
}

static void
shadow_ll_init(void *userdata, struct fuse_conn_info *conn)
{
    dsyslog("init\n");

    // Create an entry for the root inode
    ShadowInodeState* state = new ShadowInodeState("");
    state->attr.st_ino = FUSE_ROOT_ID;
    inode_map_[FUSE_ROOT_ID] = state;
}

static void
shadow_ll_destroy(void *userdata)
{
    dsyslog("destroy\n");
}

static int
resolve_path(fuse_ino_t parent, const char *name, std::string* path)
{
    if (parent == FUSE_ROOT_ID) {
        *path = name;
    } else {
        ShadowInodeState* state = lookup_by_inode(parent);
        if (!state) {
            dsyslog("resolve_path parent inode %lu name %s... no such parent\n",
                    parent, name);
            return ENOENT;
        }
        *path = state->path_ + "/" + name;

        if (! S_ISDIR(state->attr.st_mode)) {
            dsyslog("resolve_path parent inode %lu path %s: parent not a dir\n",
                    parent, path->c_str());
            return ENOTDIR;
        }
    }

    dsyslog("resolve_path parent inode %lu name %s -> %s\n",
            parent, name, path->c_str());

    return 0;
}
    
static void
shadow_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    int err;
    std::string path;
    
    err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }
    
    fuse_entry_param ent;
    err = gen_entry(&ent, "lookup", parent, name, path, false /* must_create */, NULL);
    if (err != 0) {
        fuse_reply_err(req, err);
    } else {
        fuse_reply_entry(req, &ent);
    }
}

static void
del_state(ShadowInodeState* state)
{
    dsyslog("del_state %s ino %llu\n", state->path_.c_str(), state->attr.st_ino);
    
    inode_map_.erase(state->attr.st_ino);

    path_map_.erase(state->path_);
    LinkVector::iterator lvi;
    for (lvi = state->links_.begin(); lvi != state->links_.end(); ++lvi) {
        path_map_.erase(*lvi);
    }
    
    delete state;
}

static void
shadow_ll_forget(fuse_req_t req, fuse_ino_t ino, unsigned long nlookup)
{
    ShadowInodeState* state = lookup_by_inode(ino);
    if (!state) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    assert(ino == state->attr.st_ino);
    del_state(state);
    
    fuse_reply_err(req, 0);
}

static void
shadow_ll_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    struct stat st;
    dsyslog("getattr %lu...\n", ino);
    ShadowInodeState *state = lookup_by_inode(ino);
    if (!state) {
        dsyslog("getattr %lu... no such inode\n", ino);
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    std::string local_path = DATA_DIR + state->path_;
    WRAPPED_SYSCALL(lstat, local_path.c_str(), (&state->attr));
    st = state->attr;
    dsyslog("getattr %lu... success %s\n", ino, local_path.c_str());
        
    fuse_reply_attr(req, &st, ATTR_TIMEOUT);
}

static void
shadow_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                  int to_set, struct fuse_file_info *fi)
{
    ShadowInodeState *state = lookup_by_inode(ino);
    if (state == NULL) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string local_path = DATA_DIR + state->path_;
    if (to_set & FUSE_SET_ATTR_MODE) {
        WRAPPED_SYSCALL(chmod, local_path.c_str(), attr->st_mode);
        state->attr.st_mode = attr->st_mode;
    }

    if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID)) {
        uid_t uid = -1;
        gid_t gid = -1;
        
        if (to_set & FUSE_SET_ATTR_UID) {
            state->attr.st_uid = uid = attr->st_uid;
        }
            
        if (to_set & FUSE_SET_ATTR_GID) {
            state->attr.st_gid = gid = attr->st_gid;
        }

        WRAPPED_SYSCALL(chown, local_path.c_str(), uid, gid);
    }

    if (to_set & FUSE_SET_ATTR_SIZE) {
        WRAPPED_SYSCALL(truncate, local_path.c_str(), attr->st_size);
        state->attr.st_size = attr->st_size;
    }


// #define FUSE_SET_ATTR_ATIME	(1 << 4)
// #define FUSE_SET_ATTR_MTIME	(1 << 5)

    fuse_reply_attr(req, &state->attr, ATTR_TIMEOUT);
}

static void
shadow_ll_readlink(fuse_req_t req, fuse_ino_t ino)
{
    ShadowInodeState *state = lookup_by_inode(ino);
    if (state == NULL) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string local_path = DATA_DIR + state->path_;
    char buf[256];
    dsyslog("readlink %lu %s...\n", ino, local_path.c_str());
    int pathlen = readlink(local_path.c_str(), buf, sizeof(buf));
    if (pathlen == -1) {
        fuse_reply_err(req, errno);
        return;
    }
    buf[pathlen] = '\0';
    dsyslog("readlink %lu %s -> %s\n", ino, state->path_.c_str(), buf);

    fuse_reply_readlink(req, buf);
}

static void
shadow_ll_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
                mode_t mode, dev_t rdev)
{
    fuse_reply_err(req, ENOSYS);
}

static void
shadow_ll_create(fuse_req_t req, fuse_ino_t parent, const char *name,
                 mode_t mode, struct fuse_file_info* fi)
{
    dsyslog("create: parent inode %lu name %s mode %o...\n",
            parent, name, mode);

    std::string path;
    int err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }
    
    std::string local_path = DATA_DIR + path;

    // XXX
    if (strstr(local_path.c_str(), ".git")) {
        mode |= S_IWUSR;
    }

    dsyslog("create(%s): opening file flags %o mode %o\n",
            local_path.c_str(), fi->flags, mode);
    int fd = open(local_path.c_str(), fi->flags | O_CREAT | O_EXCL, mode);
    if (fd < 0) {
        dsyslog("create(%s): error %s\n", local_path.c_str(), strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    fuse_entry_param ent;
    ShadowInodeState* state;
    err = gen_entry(&ent, "create", parent, name, path, true /* must_create */, &state);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    state->local_fd_ = fd;
    fi->direct_io = OPEN_DIRECT_IO;
    fi->keep_cache = OPEN_KEEP_CACHE;
    fi->fh = (u_int64_t)state;

    fuse_reply_create(req, &ent, fi);
}

static void
shadow_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
                mode_t mode)
{
    dsyslog("mkdir: parent inode %lu name %s mode %o...\n",
            parent, name, mode);

    std::string path;
    int err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }
    
    std::string local_path = DATA_DIR + path;

    // XXX
    if (strstr(local_path.c_str(), ".git")) {
        mode |= S_IWUSR;
    }

    WRAPPED_SYSCALL(mkdir, local_path.c_str(), mode);

    fuse_entry_param ent;
    ShadowInodeState* state;
    err = gen_entry(&ent, "mkdir", parent, name, path, true /* must_create */, &state);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    fuse_reply_entry(req, &ent);
}

static void
unlink_or_rmdir(const char* op, fuse_req_t req, fuse_ino_t parent, const char *name)
{
    // Lookup the parent inode to get the path
    // Also need path -> state mapping
    std::string path;
    int err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    ShadowInodeState* state = lookup_by_path(path);
    if (!state) {
        dsyslog("%s: no entry for path %s in path table\n", op, path.c_str());
        fuse_reply_err(req, ENOENT);
        return;
    }

    ShadowInodeState* state2 = lookup_by_inode(state->attr.st_ino);
    if (!state2) {
        dsyslog("%s: no entry for path %s inode %llu in inode table\n",
                op, path.c_str(), state->attr.st_ino);
        // XXX???
        fuse_reply_err(req, ENOENT);
        return;
    }

    assert(state == state2);

    std::string local_path = DATA_DIR + path;

    if (!strcmp(op, "unlink")) {
        err = unlink(local_path.c_str());
    } else {
        err = rmdir(local_path.c_str());
    }
    
    if (err != 0) {
        dsyslog("%s(%s) error: %s\n", op, local_path.c_str(), strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    path_map_.erase(path);
    
    bool last_link = del_link(state, name);
    if (last_link) {
        del_state(state);
    }
    
    fuse_reply_err(req, 0);
}

static void
shadow_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    unlink_or_rmdir("unlink", req, parent, name);
}

static void
shadow_ll_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    unlink_or_rmdir("rmdir", req, parent, name);
}


static void
shadow_ll_symlink(fuse_req_t req, const char *link, fuse_ino_t parent,
                  const char *name)
{
    std::string path;
    int err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    std::string local_path = DATA_DIR + path;

    WRAPPED_SYSCALL(symlink, link, local_path.c_str());

    struct fuse_entry_param ent;
    err = gen_entry(&ent, "symlink", parent, name, path, true, NULL);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    fuse_reply_entry(req, &ent);
}

static void
shadow_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
                 fuse_ino_t newparent, const char *newname)
{
    std::string path, newpath;
    int err;

    err = resolve_path(parent, name, &path);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    err = resolve_path(newparent, newname, &newpath);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    ShadowInodeState* state = lookup_by_path(path);
    if (!state) {
        dsyslog("rename: no entry for path %s in path table\n", path.c_str());
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    ShadowInodeState* state2 = lookup_by_path(newpath);
    if (state2) {
        dsyslog("rename: entry already exists for newpath %s\n", newpath.c_str());
        fuse_reply_err(req, EEXIST);
        return;
    }

    std::string local_path = DATA_DIR + path;
    std::string local_newpath = DATA_DIR + newpath;

    err = rename(local_path.c_str(), local_newpath.c_str());
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    // update the local state
    path_map_.erase(path);
    bool last_link = del_link(state, path.c_str());
    if (last_link) {
        state->path_ = newpath;
    } else {
        add_link(state, newpath.c_str());
        path_map_[newpath] = state;
    }
    
    fuse_reply_err(req, 0);
}

static void
shadow_ll_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
               const char *newname)
{
    ShadowInodeState* state = lookup_by_inode(ino);
    if (!state) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    std::string newpath;
    int err = resolve_path(newparent, newname, &newpath);
    
    std::string local_path1 = DATA_DIR + state->path_;
    std::string local_path2 = DATA_DIR + newpath;

    dsyslog("link: hard link %s -> %s\n", newpath.c_str(), state->path_.c_str());
    WRAPPED_SYSCALL(link, local_path1.c_str(), local_path2.c_str());

    struct fuse_entry_param ent;
    err = gen_entry(&ent, "link", newparent, newname, newpath, false);
    if (err != 0) {
        fuse_reply_err(req, err);
        return;
    }

    fuse_reply_entry(req, &ent);
}

static void
shadow_ll_open(fuse_req_t req, fuse_ino_t ino,
               struct fuse_file_info *fi)
{
    ShadowInodeState *state = lookup_by_inode(ino);
    if (!state) {
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string local_path = DATA_DIR + state->path_;
    int fd = ::open(local_path.c_str(), fi->flags);
    if (fd < 0) {
        fuse_reply_err(req, errno);
        return;
    }

    state->local_fd_ = fd;
    fi->direct_io = OPEN_DIRECT_IO;
    fi->keep_cache = OPEN_KEEP_CACHE;
    fi->fh = (u_int64_t)state;
    fuse_reply_open(req, fi);
}

static void
shadow_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
               struct fuse_file_info *fi)
{
    char buf[size];
    ShadowInodeState *state = lookup_by_inode(ino);
    if (!state) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    dsyslog("read ino %lu path %s\n", ino, state->path_.c_str());
    int rc = pread(state->local_fd_, buf, size, off);
    if (rc < 0) {
        dsyslog("read error %s\n", strerror(rc));
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_buf(req, buf, rc);
}

static void
shadow_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
                size_t size, off_t off, struct fuse_file_info *fi)
{
    ShadowInodeState *state = lookup_by_inode(ino);
    if (!state) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    dsyslog("write ino %lu path %s\n", ino, state->path_.c_str());
    int rc = pwrite(state->local_fd_, buf, size, off);
    if (rc < 0) {
        dsyslog("write error %s\n", strerror(rc));
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_write(req, rc);
}

static void
shadow_ll_flush(fuse_req_t req, fuse_ino_t ino,
                struct fuse_file_info *fi)
{
    fuse_reply_err(req, 0);
}

static void
shadow_ll_release(fuse_req_t req, fuse_ino_t ino,
                  struct fuse_file_info *fi)
{
    ShadowInodeState* state = lookup_by_inode(ino);

    if (!state) {
        dsyslog("release(%lu)... no inode in map\n", ino);
        fuse_reply_err(req, ENOENT);
    }

    if (state->local_fd_ == 0) {
        dsyslog("release(%lu)... path %s file already closed\n",
                ino, state->path_.c_str());
    } else {
        dsyslog("release(%lu)... path %s closing file\n",
                ino, state->path_.c_str());
        int err = close(state->local_fd_);
        if (err != 0) {
            dsyslog("close(%d) error: %s\n", state->local_fd_, strerror(errno));
            fuse_reply_err(req, errno);
        }
        state->local_fd_ = 0;
    }
        
    fuse_reply_err(req, 0);
}

static void
shadow_ll_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                struct fuse_file_info *fi)
{
    fuse_reply_err(req, ENOSYS);
}


static void
shadow_ll_opendir(fuse_req_t req, fuse_ino_t ino,
                  struct fuse_file_info *fi)
{
    ShadowInodeState* state = lookup_by_inode(ino);
    if (!state) {
        dsyslog("opendir(%lu): no entry in inode table\n", ino);
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string local_path = DATA_DIR + state->path_;
    DIR* dir = opendir(local_path.c_str());
    if (!dir) {
        dsyslog("opendir(%lu): error in opendir %s\n", ino, strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    fi->fh = (u_int64_t)dir;
    fuse_reply_open(req, fi);
}

static void
shadow_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                  struct fuse_file_info *fi)
{
    DIR* dir = (DIR*)fi->fh;
    struct dirent* ent;
    char buf[size];
    char* bp = buf;
    size_t bufsize = size;
    size_t entsz;
    struct stat st;
    char name[256];

    if (off != 0) {
        seekdir(dir, off);
    }
    
    while (1) {
        ent = readdir(dir);
        if (!ent) {
            dsyslog("readdir(%lu): end of dir reached\n", ino);
            break;
        }

        off = telldir(dir);
#ifdef __APPLE__
        size_t namlen = ent->d_namlen;
#else
        size_t namlen = _D_EXACT_NAMLEN(ent);
#endif
        memcpy(name, ent->d_name, namlen);
        name[namlen] = '\0';

        dsyslog("readdir(%lu): offset %llu adding entry %s (%zu/%zu remaining)\n",
                ino, off, name, size, bufsize);
        
        st.st_ino = ino;
        st.st_mode = DTTOIF(ent->d_type);

        entsz = fuse_add_direntry(req, bp, size, name, &st, off);
        if (entsz < size) {
            size -= entsz;
            bp   += entsz;
        } else {
            dsyslog("readdir(%lu): can't add entry %s: size %zu > remaining %zu\n",
                    ino, name, entsz, size);
            break;
        }
    }

    if (size == bufsize) {
        fuse_reply_buf(req, 0, 0);
    } else {
        fuse_reply_buf(req, buf, bufsize - size);
    }
}


static void
shadow_ll_releasedir(fuse_req_t req, fuse_ino_t ino,
                     struct fuse_file_info *fi)
{
    int ok = closedir((DIR*)fi->fh);
    if (!ok) {
        dsyslog("closedir error: %s\n", strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    fi->fh = NULL;
    fuse_reply_err(req, 0);
}


static void
shadow_ll_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
                   struct fuse_file_info *fi)
{
    fuse_reply_err(req, ENOSYS);
}


static void
shadow_ll_statfs(fuse_req_t req, fuse_ino_t ino)
{
    struct statvfs st;
    int err = statvfs(DATA_DIR.c_str(), &st);
    if (err != 0) {
        fuse_reply_err(req, errno);
    }
    fuse_reply_statfs(req, &st);
}


static void
shadow_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                   const char *value, size_t size, int flags)
{
    fuse_reply_err(req, ENOSYS);
}


static void
shadow_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                   size_t size)
{
    fuse_reply_err(req, ENOSYS);
}


static void
shadow_ll_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
    fuse_reply_err(req, ENOSYS);
}


static void
shadow_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
    fuse_reply_err(req, ENOSYS);
}


struct fuse_lowlevel_ops shadow_ll_ops;

void init_shadow_ll_ops()
{
    memset(&shadow_ll_ops, 0, sizeof(shadow_ll_ops));
    shadow_ll_ops.init         = shadow_ll_init;
    shadow_ll_ops.destroy      = shadow_ll_destroy;
    shadow_ll_ops.lookup       = shadow_ll_lookup;
    shadow_ll_ops.forget       = shadow_ll_forget;
    shadow_ll_ops.getattr      = shadow_ll_getattr;
    shadow_ll_ops.setattr      = shadow_ll_setattr;
    shadow_ll_ops.readlink     = shadow_ll_readlink;
    shadow_ll_ops.mknod        = shadow_ll_mknod;
    shadow_ll_ops.create       = shadow_ll_create;
    shadow_ll_ops.mkdir        = shadow_ll_mkdir;
    shadow_ll_ops.unlink       = shadow_ll_unlink;
    shadow_ll_ops.rmdir        = shadow_ll_rmdir;
    shadow_ll_ops.symlink      = shadow_ll_symlink;
    shadow_ll_ops.rename       = shadow_ll_rename;
    shadow_ll_ops.link         = shadow_ll_link;
    shadow_ll_ops.open         = shadow_ll_open;
    shadow_ll_ops.read         = shadow_ll_read;
    shadow_ll_ops.write        = shadow_ll_write;
    shadow_ll_ops.flush        = shadow_ll_flush;
    shadow_ll_ops.release      = shadow_ll_release;
    shadow_ll_ops.fsync        = shadow_ll_fsync;
    shadow_ll_ops.opendir      = shadow_ll_opendir;
    shadow_ll_ops.readdir      = shadow_ll_readdir;
    shadow_ll_ops.releasedir   = shadow_ll_releasedir;
    shadow_ll_ops.fsyncdir     = shadow_ll_fsyncdir;
    shadow_ll_ops.statfs       = shadow_ll_statfs;
    shadow_ll_ops.setxattr     = shadow_ll_setxattr;
    shadow_ll_ops.setxattr     = shadow_ll_setxattr;
    shadow_ll_ops.getxattr     = shadow_ll_getxattr;
    shadow_ll_ops.getxattr     = shadow_ll_getxattr;
    shadow_ll_ops.listxattr    = shadow_ll_listxattr;
    shadow_ll_ops.removexattr  = shadow_ll_removexattr;
}
