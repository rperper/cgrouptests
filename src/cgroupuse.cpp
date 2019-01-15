/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
*    LiteSpeed Technologies Proprietary/Confidential.                       *
****************************************************************************/
#include <unistd.h>
#include <stdio.h>
#include <gio/gio.h>
#include "cgroupuse.h"

int CGroupUse::apply_slice()
{
    // The functions below are supposed to be NULL safe.  That's why I test at 
    // the end for NULL.
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    GVariantBuilder *pids_array = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    GVariant *pvarpid = g_variant_new("u", (unsigned int)getpid());
    g_variant_builder_add_value(pids_array, pvarpid);
    GVariant *pvarparr = g_variant_new("au", pids_array);
    g_variant_builder_add(properties, "(sv)", "PIDs", pvarparr);
    char slice[256];
    snprintf(slice, sizeof(slice), "user-%u.slice", (unsigned int)m_uid);
    GVariant *pvarslice = g_variant_new("s", slice);
    g_variant_builder_add(properties, "(sv)", "Slice", pvarslice);
    GVariant *pSIGHUP = g_variant_new("b", TRUE);
    g_variant_builder_add(properties, "(sv)", "SendSIGHUP", pSIGHUP);
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
        conn->set_error(CGroupConn::CERR_INSUFFICIENT_MEMORY);
        return -1;
    }
    conn->clear_err();
    g_dbus_proxy_call_sync(conn->proxy,
                           fn,
                           parms,
                           G_DBUS_CALL_FLAGS_NONE,
                           -1,   // Default timeout
                           NULL, // GCancellable
                           &conn->err);// userdata
    if (conn->err)
    {
        conn->set_error(CGroupConn::CERR_GDERR);
        return -1;
    }
    return 0;
}


int CGroupUse::apply_session()
{
    char *fn = (char *)"CreateSession";

    GVariant *parms = g_variant_new("(uusssssussbssa(sv))",
                                    m_uid,                //uid
                                    getpid(),           //pid
                                    "",                 //service
                                    "unspecified",      //"unspecified"type = getenv("XDG_SESSION_TYPE");
                                    "background",             //class (could be "user")
                                    "",                 //desktop = getenv("XDG_SESSION_DESKTOP");
                                    "",                 //seat = getenv("XDG_SEAT");
                                    0,                  //atoi(cvtnr = getenv("XDG_VTNR"));
                                    "",                 //tty
                                    "",                 //display = tty
                                    0,                  //remote
                                    "",                 //remote_user
                                    "",                 //remote_host
                                    NULL);              //properties
    
    GVariant *rc = g_dbus_proxy_call_sync(conn->proxy_login,
                                          fn,
                                          parms,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &conn->err);
    if (conn->err)
    {
        conn->set_error(CGroupConn::CERR_GDERR);
        return -1;
    }
    /*
        {
            char *session_id;
            char *object_path;
            char *runtime_path;
            short session_fd;
            int   original_uid;
            char *seat;
            int   vtnr;
            int   existing;
        
            printf("%s returned no error", fn);
            g_variant_get(rc,"(soshusub)",
                          &session_id,
                          &object_path,
                          &runtime_path,
                          &session_fd,
                          &original_uid,
                          &seat,
                          &vtnr,
                          &existing);
            
            printf("%s output:\n", fn);
            printf("   session_id   : %s\n", session_id);
            printf("   object_path  : %s\n", object_path);
            printf("   runtime_path : %s\n", runtime_path);
            printf("   session_fd   : %u\n", session_fd);
            printf("   original_uid : %u\n", original_uid);
            printf("   seat         : %s\n", seat);
            printf("   vtnr         : %u\n", vtnr);
            printf("   existing     : %s\n", existing ? "YES" : "NO");
        }
    */
    return 0;
}



int CGroupUse::apply(int uid)
{
    int rc;
    m_uid = uid;
    rc = apply_slice();
    if (!rc)
        rc = apply_session();
    return rc;
}
