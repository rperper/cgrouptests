/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
*    LiteSpeed Technologies Proprietary/Confidential.                       *
****************************************************************************/
#ifndef _CGROUP_CONN_H
#define _CGROUP_CONN_H

/****************************************************************************
* Most of the time you're going to want to create one instance of this and  *
* use it for all of your CGroup functionality (CGroupUse and CGroupIO).     *
****************************************************************************/
#include <lsdef.h>

struct _GDBusConnection;
struct _GDBusProxy;
struct _GError;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusProxy      GDBusProxy;
typedef struct _GError          GError;

class CGroupUse;

class CGroupConn
{
public:
    enum CGroupErrors {
        CERR_NO_ERROR = 0,
        CERR_GDERR,
        CERR_INSUFFICIENT_MEMORY
    };
    
private:
    friend class CGroupUse;
    _GDBusConnection *conn;
    _GDBusProxy      *proxy;
    _GDBusProxy      *proxy_login;
    GError           *err;
    CGroupErrors      err_num;
    void set_error(enum CGroupErrors err)
    {   err_num = err;   }
    void clear_err();
        
public:
    CGroupConn() 
    {   
        conn = NULL; 
        proxy = NULL;
        proxy_login = NULL;
        err = NULL;
        err_num = CERR_NO_ERROR;
    }
    
    ~CGroupConn();
    
    int create();
    char *getErrorText(); // For any functions returning -1
 
    LS_NO_COPY_ASSIGN(CGroupConn);
};

#endif // _CGROUP_USE_H

