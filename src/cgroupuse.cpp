/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
*    LiteSpeed Technologies Proprietary/Confidential.                       *
****************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <gio/gio.h>
#include "cgroupuse.h"

int CGroupUse::set_linger()
{
    int uid = geteuid();
    seteuid(getuid());
    if (!conn->proxy_login)
    {
        conn->clear_err();
        conn->proxy_login = g_dbus_proxy_new_sync(conn->conn,
                                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                  NULL,                               /* GDBusInterfaceInfo */
                                                  "org.freedesktop.login1",           /* name */
                                                  "/org/freedesktop/login1",          /* object path */
                                                  "org.freedesktop.login1.Manager",   /* interface */
                                                  NULL,                               /* GCancellable */
                                                  &conn->err);
        if (conn->err)
        {
            conn->set_error(CGroupConn::CERR_GDERR);
            seteuid(uid);
            return -1; // This error is more important than the original one.
        }
    }

    char *fn = (char *)"SetUserLinger";
    GVariant *parm = g_variant_new("(ubb)", uid, 1, 0);
    if (!parm)
    {
        conn->set_error(CGroupConn::CERR_INSUFFICIENT_MEMORY);
        seteuid(uid);
        return -1;
    }
    conn->clear_err();
    GVariant *rc = g_dbus_proxy_call_sync(conn->proxy_login,
                                          fn,
                                          parm,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &conn->err);
    seteuid(uid);
    if (conn->err)
    {
        conn->set_error(CGroupConn::CERR_GDERR);
        return -1; // This error is more important than the original one.
    }        
    return 0;
}


int CGroupUse::apply()
{
    // The functions below are supposed to be NULL safe.  That's why I test at 
    // the end for NULL.
    int euid = geteuid();
    seteuid(getuid());
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    GVariantBuilder *pids_array = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    GVariant *pvarpid = g_variant_new("u", (unsigned int)getpid());
    g_variant_builder_add_value(pids_array, pvarpid);
    GVariant *pvarparr = g_variant_new("au", pids_array);
    g_variant_builder_add(properties, "(sv)", "PIDs", pvarparr);
    char slice[256];
    snprintf(slice, sizeof(slice), "user-%u.slice", (unsigned int)euid);
    GVariant *pvarslice = g_variant_new("s", slice);
    g_variant_builder_add(properties, "(sv)", "Slice", pvarslice);
    char *fn = (char *)"StartTransientUnit";
    char unit[256];
    snprintf(unit, sizeof(unit), "run-%u.scope", (unsigned int)getpid());
    GVariant *parms = g_variant_new("(ssa(sv)a(sa(sv)))",
                                    unit,
                                    "fail",
                                    properties,
                                    NULL);
    if ((!properties) || (!pids_array) || (!pvarpid) || (!pvarparr) || 
        (!pvarslice) || (!parms))
    {
        // Note: These are supposed to be smart and deallocate when no longer
        // referenced.
        seteuid(euid);
        conn->set_error(CGroupConn::CERR_INSUFFICIENT_MEMORY);
        return -1;
    }
    conn->clear_err();
    GVariant *rc = g_dbus_proxy_call_sync(conn->proxy,
                                          fn,
                                          parms,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &conn->err);// userdata
    seteuid(euid);
    if (conn->err)
    {
        if ((!(getuid())) && (conn->err->code == 36))
        {
            // Try setting the user linger once!
            if (set_linger() == -1)
                return -1;
            // retry the StartTransientUnit call now...
            conn->clear_err();
            g_dbus_proxy_call_sync(conn->proxy,
                                   fn,
                                   parms,
                                   G_DBUS_CALL_FLAGS_NONE,
                                   -1,   // Default timeout
                                   NULL, // GCancellable
                                   &conn->err);// userdata            
            if (!conn->err) 
                return 0; // Linger worked.
        }
        conn->set_error(CGroupConn::CERR_GDERR);
        return -1;
    }
    return 0;
}
