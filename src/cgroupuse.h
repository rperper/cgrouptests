/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
****************************************************************************/
#ifndef _CGROUP_USE_H
#define _CGROUP_USE_H

/****************************************************************************
* Create an instance of this class after you have done the fork and set the *
* effective uid.  It applies the CGroup info by changing the slice the      *
* process runs in.                                                          *
****************************************************************************/
#include "cgroupconn.h"

class CGroupUse 
{
private:
    friend class CGroupConn;
    CGroupConn *conn;
    int apply_slice();
    int apply_session();
    int m_uid;
public:
    CGroupUse(CGroupConn *conn)
    { CGroupUse::conn = conn; }
    
    ~CGroupUse()
    {   }
    
    int apply(int uid);
 
    LS_NO_COPY_ASSIGN(CGroupUse);
};

#endif // _CGROUP_USE_H

