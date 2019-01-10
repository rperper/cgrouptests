/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
*    LiteSpeed Technologies Proprietary/Confidential.                       *
****************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <gio/gio.h>
#include "cgroupconn.h"

void CGroupConn::clear_err()
{
    if (!err)
        return;
    g_clear_error(&err);
    err_num = CERR_NO_ERROR;
}    
    
int CGroupConn::create()
{
    int euid = geteuid();
    seteuid(getuid());
    clear_err();
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                          NULL, // GCancellable
                          &err);
    seteuid(euid);
    if (err)
    {
        set_error(CERR_GDERR);
        return -1;
    }
    seteuid(getuid());
    clear_err();
    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_NONE,
                                  NULL,                                 /* GDBusInterfaceInfo */
                                  "org.freedesktop.systemd1",           /* name */
                                  "/org/freedesktop/systemd1",          /* object path */
                                  "org.freedesktop.systemd1.Manager",   /* interface */
                                  NULL,                                 /* GCancellable */
                                  &err);
    seteuid(euid);
    if (err)
    {
        set_error(CERR_GDERR);
        return -1;
    }

    return 0;
};


CGroupConn::~CGroupConn()
{
    /* Actually this is kind of unnecessary as these are dynamic pointers, but
     * I never really believe that.  */
    if (proxy)
        g_object_unref(proxy);
    if (proxy_login)
        g_object_unref(proxy_login);
    if (conn)
        g_object_unref(conn);
    clear_err();
}

   
char *CGroupConn::getErrorText()
{
    enum CGroupErrors errnum = err_num;
    err_num = CERR_NO_ERROR;
    switch (errnum) 
    {
        case CERR_GDERR :
            return err->message;
        case CERR_INSUFFICIENT_MEMORY:
            return (char *)"Insufficient memory";
        case CERR_NO_ERROR :
        default:
            return NULL;
    }
    return NULL;
}
