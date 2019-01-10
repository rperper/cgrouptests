#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "cgroupconn.h"
#include "cgroupuse.h"

void use()
{
    printf("Usage: test -u <uid> \n");
}


int main(int argc, char *argv[])
{
    int uid = -1;
    char opt;
    printf("Entering cgroup test program\n");
    while ((opt = getopt(argc, argv, "u:?")) != -1)
    {
        switch (opt)
        {
            case 'u':
                printf("Set uid to %u\n", uid = atoi(optarg));
                if (seteuid(uid))
                {
                    printf("Error setting euid: %s\n", strerror(errno));
                    return 1;
                }
                break;
            case '?':
            default:
                use();
                return 1;
        }
    }
    class CGroupConn *conn = new CGroupConn();
    if (!conn)
    {
        printf("Error creating connection\n");
        return 1;
    }
    if (conn->create())
    {
        printf("Error in applying connection: %s\n", conn->getErrorText());
        return 1;
    }
    class CGroupUse *use = new CGroupUse(conn);
    if (!use)
    {
        printf("Error in creating use class: %s\n", conn->getErrorText());
        return 1;
    }
    if (use->apply())
    {
        printf("Error in applying use class: %s\n", conn->getErrorText());
        return 1;
    }
    printf("Everything done.  I'm pid: %d.  Press enter to exit-> ", getpid());
    char input[80];
    gets(input);
    return 0;
}
