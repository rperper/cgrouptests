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
    if (!m_err)
        return;
    g_clear_error(&m_err);
    m_err_num = CERR_NO_ERROR;
}    
    
int CGroupConn::create()
{
    clear_err();
    m_conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                            NULL, // GCancellable
                            &m_err);
    if (m_err)
    {
        set_error(CERR_GDERR);
        return -1;
    }
    clear_err();
    m_proxy = g_dbus_proxy_new_sync(m_conn,
                                    G_DBUS_PROXY_FLAGS_NONE,
                                    NULL,                                 /* GDBusInterfaceInfo */
                                    "org.freedesktop.systemd1",           /* name */
                                    "/org/freedesktop/systemd1",          /* object path */
                                    "org.freedesktop.systemd1.Manager",   /* interface */
                                    NULL,                                 /* GCancellable */
                                    &m_err);
    if (m_err)
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
    if (m_proxy)
        g_object_unref(m_proxy);
    if (m_conn)
        g_object_unref(m_conn);
    clear_err();
}

   
char *CGroupConn::getErrorText()
{
    enum CGroupErrors errnum = m_err_num;
    switch (errnum) 
    {
        case CERR_GDERR :
            return m_err->message;
        case CERR_INSUFFICIENT_MEMORY:
            return (char *)"Insufficient memory";
        case CERR_BAD_SYSTEMD:
            return (char *)"systemd not at high enough level for cgroups";
        case CERR_SYSTEM_ERROR:
            return (char *)"system error prevented tests";
        case CERR_NO_ERROR :
        default:
            return NULL;
    }
    return NULL;
}


int CGroupConn::getErrorNum()
{
    return (int)m_err_num;
}
