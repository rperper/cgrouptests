/****************************************************************************
*    Copyright (C) 2019  LiteSpeed Technologies, Inc.                       *
*    All rights reserved.                                                   *
*    LiteSpeed Technologies Proprietary/Confidential.                       *
****************************************************************************/
#ifndef _CGROUP_CONN_H
#define _CGROUP_CONN_H

#include <lsdef.h>

/**
 * @file cgroupconn.h
 */

struct _GDBusConnection;
struct _GDBusProxy;
struct _GError;
typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusProxy      GDBusProxy;
typedef struct _GError          GError;

class CGroupUse;

/**
 * @class CGroupConn  
 * @brief Every task (including subtasks) which need to access CGroups needs
 * to create an instance of this class which is passed into the CGroupUse
 * class.  Typically you create the CGroupConn and then immediately use it
 * with CGroupUse in the subtask.
 * @note This class does not do error reporting or debug logging.
 **/

class CGroupConn
{
private:
    friend class CGroupUse;
    /**
     * @enum CGroupErrors 
     * @brief If a method (in either this class or the attached CGroupUse 
     * class) returns -1, you must call set_error with one of these errors 
     * indicating the cause of the error.  Used in getErrorText.
     **/
    enum CGroupErrors {
        CERR_NO_ERROR = 0,
        CERR_GDERR,
        CERR_INSUFFICIENT_MEMORY,
        CERR_BAD_SYSTEMD,
        CERR_SYSTEM_ERROR
    };
    
    _GDBusConnection *m_conn;
    _GDBusProxy      *m_proxy;
    GError           *m_err;
    CGroupErrors      m_err_num;
    void set_error(enum CGroupErrors err)
    {   m_err_num = err;   }
    void clear_err();
        
public:
    CGroupConn() 
    {   
        m_conn = NULL; 
        m_proxy = NULL;
        m_err = NULL;
        m_err_num = CERR_NO_ERROR;
    }
    
    ~CGroupConn();
    
    /**
     * @fn int create()
     * @brief Call after instantiation to perform the actual creation.  
     * @return 0 if success or -1 if it failed.  If it failed you can call
     * getErrorText to get the details.
     **/
    int create();
    /**
     * @fn char *getErrorText
     * @brief Call if a method returns -1 in either this class or the attached
     * CGroupUse class(s).
     * @return A pointer to a text description of the problem for error 
     * reporting or debugging.  
     **/
    char *getErrorText(); // For any functions returning -1

    /**
     * @fn int getErrorNum
     * @brief Call if a method returns -1 in either this class or the attached
     * CGroupUse class(s).
     * @return A number that might mean something to someone (actually one of 
     * the enumerated types).   
     **/
    int  getErrorNum(); // For any functions returning -1

    LS_NO_COPY_ASSIGN(CGroupConn);
};

#endif // _CGROUP_USE_H

