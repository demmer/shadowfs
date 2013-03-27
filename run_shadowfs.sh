#!/bin/sh

mount | grep shadowfs > /dev/null
if [ $? = 0 ] ; then
    umount /u/$USER/shadowfs
fi
shadowfs $* -odefault_permissions,noappledouble -s /u/$USER/shadowfs/

