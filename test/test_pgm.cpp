#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "cgroupconn.h"
#include "cgroupuse.h"

enum tests 
{
    none, cpu, memory, block_io, block_write, block_read, tasks 
};

char file[256] = { 0 };
char pgm[256] = { 0 };

void use()
{
    printf("Usage: test [-u <uid>] [-cputest <secs>] [-memorytest] [-blockiotest] [-readstest <file>] [-writestest <file>] [-forkstest] [-execute <pgm>] [-validate]\n");
}


int cpu_load(int length)
{
    // Now stress the CPU
    int stress = 1;
    char stress_arr[2];
    time_t start, current;
    time(&start);
    current = start;
    while ((stress > 0) && (current - start < length))
    {
        stress_arr[0] = stress;
        stress++;
        stress--;
        time(&current);
    }
    return 0;
}


int mem_load(void)
{
    int index = 0;
    int increment = 100000;
    int end       = 1000000000;
    char *memory_array[(end / increment) + 1];
    printf("Stack vars: %d, increment: %d, full size: %d\n", sizeof(memory_array),
           increment, end);
    int uid = getuid();
    if (uid != 0)
        printf("UID is not zero (%d), can't lock memory\n");
    {
        for (index = 0; index < end; index += increment)
        {
            memory_array[index / increment] = (char *)malloc(index);
            if (!memory_array[index / increment])
            {
                printf("(%d) ran out of memory after allocating %d bytes: %s\n", 
                       getpid(), index - increment, strerror(errno));
                break;
            }
            if ((uid == 0) &&
                (mlock(memory_array[index / increment], increment) != 0))
            {
                printf("(%d) ran out of REAL memory after allocating %d bytes: %s\n",
                       getpid(), index, strerror(errno));
                break;
            }
        }
        if (index >= end) 
            printf("(%d) was able to allocate the full %d bytes\n",
                   getpid(), index);
        
    }
}


int block_io_load(void)
{
    char *file_input = (char *)"read.data";
    int fd_input = open(file_input, O_RDONLY);
    int sz = 10000;
    char buffer[sz];
    long count = 0;
    long start_time;
    long current_time;
    struct timespec ts;
    int error = 0;
    
    clock_gettime(CLOCK_REALTIME, &ts);
    start_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
    if (fd_input == -1)
    {
        printf("Error opening %s for read-only: %s\n", file_input, strerror(errno));
        return -1;
    }
    char *file_output = (char *)"write.data";
    int fd_output = open(file_output, O_WRONLY | O_CREAT, 0666);

    while (!error) 
    {
        int len;
        count = 0;
        if (lseek(fd_input, 0, SEEK_SET) == -1)
        {
            printf("Error doing lseek to zero of input: %s\n", strerror(errno));
            error = 1;
            break;
        }
        if (lseek(fd_output, 0, SEEK_SET) == -1)
        {
            printf("Error doing lseek to zero of output: %s\n", strerror(errno));
            error = 1;
            break;
        }
        while ((len = read(fd_input, buffer, sz)) > 0)
        {
            if (write(fd_output, buffer, len) < 0)
            {
                printf("Error in write: %s\n", strerror(errno));
                error = 1;
                break;
            }
            count += len;
        }
        if (error)
            break;
        if (len == -1)
        {
            printf("Error reading input file: %s\n", strerror(errno));
            error = 1;
            break;
        }
        clock_gettime(CLOCK_REALTIME, &ts);
        current_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
        printf("Read/write full file, %lu chars, %lu chars/sec, %lu secs\n", count, 
               (count * 100000000) / (current_time - start_time), 
               (current_time - start_time) / 1000000000);
        start_time = current_time;
    }
    close(fd_input);
    close(fd_output);
    return error;
}


int block_read_load()
{
    int fd = open(file, O_RDONLY);
    int sz = 1000;
    char buffer[sz];
    long count = 0;
    long start_time;
    long current_time;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);   
    start_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
    if (fd == -1)
    {
        printf("Error opening %s for read-only: %s\n", file, strerror(errno));
        return -1;
    }
    while (read(fd, buffer, sz) == sz)
    {
        count++;
        if (!(count % 1000000))
        {
            clock_gettime(CLOCK_REALTIME, &ts);
            current_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
            printf("%ld reads, %lu chars/sec\n", count, (1000000000000000000) / (current_time - start_time));
            start_time = current_time;
        } 
    }
    close(fd);
    return 0;
}


int block_write_load()
{
    int fd = open(file, O_WRONLY | O_CREAT, 0666);
    int sz = 1000;
    char buffer[sz];
    long count = 0;
    long start_time;
    long current_time;
    struct timespec ts;
    memset(buffer,0xff, sz);
    clock_gettime(CLOCK_REALTIME, &ts);
    start_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
    if (fd == -1)
    {
        printf("Error opening %s for write-only: %s\n", file, strerror(errno));
        return -1;
    }
    while (write(fd, buffer, sz) == sz)
    {
        if (lseek(fd, 0, SEEK_SET) == -1)
        {
            printf("lseek to zero failed, %s\n", strerror(errno));
            break;
        }
        count++;
        if (!(count % 1000000))
        {
            clock_gettime(CLOCK_REALTIME, &ts);
            current_time = ts.tv_sec * 1000000000 + ts.tv_nsec;
            printf("%ld reads, %lu chars/sec\n", count, (1000000000000000000) / (current_time - start_time));
            start_time = current_time;
        }
    }
    close(fd);
    return 0;
}


int tasks_load()
{
    int forks = 0;
    int pid;
    do {
        pid = fork();
        if (!pid)
        {
            sleep(10);
            exit(0);
        }
        else if (pid > 0)
        {
            usleep(1);
            forks++;
        }
    } while (pid > 0);
    printf("Did %d forks before failing\n", forks);
}


int main(int argc, char *argv[])
{
    int uid = getuid();
    char opt;
    int secs;
    int validate = 0;
    enum tests test = none;
    printf("Entering cgroup test program\n");
    int uid_set = 0;
    while ((opt = getopt(argc, argv, "ve:u:c:mbr:w:f?")) != -1)
    {
        switch (opt)
        {
            case 'e':
                strcpy(pgm, optarg);
                printf("Execute %s\n", pgm);
                break;
            case 'u':
                printf("Set uid to %u\n", uid = atoi(optarg));
                uid_set = 1;
                break;
            case 'c':
                printf("CPU test for %d secs\n", secs = atoi(optarg));
                test = cpu;
                break;
            case 'm':
                printf("Memory test\n");
                test = memory;
                break;
            case 'b':
                printf("Block IO test\n");
                test = block_io;
                break;
            case 'r':
                strncpy(file, optarg, sizeof(file));
                printf("Read test of: %s\n", file);
                test = block_read;
                break;
            case 'w':
                strncpy(file, optarg, sizeof(file));
                printf("Write test of: %s\n", file);
                test = block_write;
                break;
            case 'f':
                printf("Forks test\n");
                test = tasks;
                break;
            case 'v':
                printf("Validate test\n");
                validate = 1;
                break;
            case '?':
            default:
                use();
                return 1;
        }
    }
    if (uid_set)
    {
        int pid = fork();
        if (pid)
        {
            int status;
            printf("Parent pid of child: %u\n", pid);
            waitpid(pid, &status, 0);
            printf("Parent rc of child: %d\n", WEXITSTATUS(status));
            return status;
        }
        printf("Child pid: %d\n", getpid());
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
            
        if (use->apply(uid))
        {
            printf("Error in applying use class: %s\n", conn->getErrorText());
            return 1;
        }
        if (validate) 
        {
            int rc = 0;
            if (use->validate() == 0)
            {
                printf("VALID FOR THIS OS\n");
            }
            else
            {
                printf("VALIDATE FAILED\n");
                rc = 1;
            }
            printf("Child pausing, pid: %d ->", getpid());
            char input[80];
            gets(input);
            return rc;
        }
        if (seteuid(uid))
        {
            printf("Error setting euid: %s\n", strerror(errno));
            return 1;
        }
    }
    else if (validate)
        printf("Validate meaningless without setuid first (-u)\n");
    switch (test)
    {
        case none:
        default:
            break;
        case cpu:
            cpu_load(secs);
            break;
        case memory:
            mem_load();
            break;
        case block_io:
            block_io_load();
            break;
        case block_read:
            block_read_load();
            break;
        case block_write:
            block_write_load();
            break;
        case tasks:
            tasks_load();
            break;
    }
    if (pgm[0])
    {
        FILE *fp = popen(pgm, "r");
        if (fp == NULL)
            printf("Error starting %s: %s\n", pgm, strerror(errno));
        else
        {
            char out[1024];
            while (fgets(out, sizeof(out), fp))
                printf("OUTPUT: %s", out);
            printf("Status: %d\n", pclose(fp));
        }  
    }
    printf("Everything done.  I'm pid: %d.  Press enter to exit-> ", getpid());
    char input[80];
    gets(input);
    return 0;
}  
