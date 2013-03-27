#!/bin/sh
set -x

SHADOWFS_DIR=/u/$USER/ll_shadowfs

mount | grep ll_shadowfs > /dev/null
if [ $? = 0 ] ; then
    umount $SHADOWFS_DIR
fi

if ! test -z $GDB ; then
   GDB_ARGS='gdb --args '
fi

$GDB_ARGS ll_shadowfs $* -odefault_permissions,noappledouble $SHADOWFS_DIR

