#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

char *str_time(char timebuf[])
{
    struct timespec ts;
    struct tm ltm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &ltm);
    sprintf(timebuf, "%02u:%02u:%02u.%06lu", ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
            ts.tv_nsec / 1000);
    return timebuf;
}


int main(int argc, char *argv[])
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    struct sockaddr_in sa2;
    int sa2len = sizeof(sa2);
    int fd2;
    FILE *bfd = NULL;
    char tmbuf[80];
    int next_user = 0;
    int opt;
    int CPULoad = 0;
    int MemLoad = 0;
    int NoLog = 0;
    int port = 8000;
    int UserSwitch = 0;
    
    printf("Running forksvc\n");
    while ((opt = getopt(argc, argv, "c:mlup:?")) != -1)
    {
        switch (opt)
        {
            case 'c':
                CPULoad = atoi(optarg);
                printf("CPU load %d secs\n", CPULoad);
                break;
            case 'm':
                MemLoad = 1;
                printf("Memory load\n");
                break;
            case 'l':
                NoLog = 1;
                printf("No log\n");
                break;
            case 'u':
                UserSwitch = 1;
                printf("Switch user\n");
                break;
            case 'p':
                port = atoi(optarg);
                printf("Use port: %d\n", port);
                break;
            default:
                printf("forksvc [-cpuload <secs>] [-memoryload] [-lognone] [-userswitch] [-port <port>]\n");
                return 1;
        }
    }
    
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (!NoLog)
        if (!(bfd = fopen("/home/user/proj/cgroup/forksvc/forksvc.log", "a")))
        {
            fprintf(stderr, "Error opening log file: %s\n", strerror(errno));
            return 1;
        }
    setvbuf(bfd, NULL, _IONBF, 0);
    if (UserSwitch)
    {
        seteuid(65534);
        setegid(65533);
    }
    if ((!NoLog) && 
        (fprintf(bfd, 
                 "%s: Bind and listen with forks on port %d, pid #%d%s%s%s\n", 
                 str_time(tmbuf), port, getpid(), CPULoad ? " [CPU Load]" : "",
                 MemLoad ? " [Memory Load]" : "", 
                 UserSwitch ? " [User Switch]" : "") < 0))
    {
        fprintf(stderr, "Error actually writing to the log: %s\n", 
                strerror(errno));
        return 1;
    }
    if (NoLog)
        bfd = stderr;
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1)
    {
        fprintf(bfd, "%s: Error in bind to port %d: %s\n", str_time(tmbuf),
                htons(sa.sin_port), strerror(errno));
        return 1;
    }
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
    {
        fprintf(bfd, "%s: Error setting reuseaddr %s\n", str_time(tmbuf), 
                strerror(errno));
        return 1;
    }
    if (listen(fd, 5) == -1)
    {
        fprintf(bfd, "%s: Error in listen: %s\n", str_time(tmbuf), 
                strerror(errno));
        return 1;
    }
    while ((fd2 = accept(fd, (struct sockaddr *)&sa2, &sa2len)) >= 0)
    {
        if (!NoLog)
            fprintf(bfd, "%s: Received connection from %u.%u.%u.%u (fd: %d)\n", 
                    str_time(tmbuf),
                    ((unsigned char *)&sa2.sin_addr.s_addr)[0],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[1],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[2],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[3],
                    fd2);
        next_user = (next_user + 1) % 2;
        pid_t pid;
        pid = fork();
        if (pid == -1)
        {
            fprintf(bfd, "%s: fork failed: %s\n", str_time(tmbuf), strerror(errno));
            break;
        }
        else if (pid > 0)
        {
            int status = 0;
            int rc;
            close(fd2);
            if (!NoLog)
                fprintf(bfd, "%s: PID #%d to process request\n", str_time(tmbuf), pid);
            rc = waitpid(pid, &status, 1); // waitpid
            if (!NoLog)
                fprintf(bfd,"%s: PID #%d terminated, rc: %d, status %d\n", 
                        str_time(tmbuf), getpid(), rc, status);
        }
        else
        {
            // child
            char buffer[8192];
            int len;
            close(fd);
            if (UserSwitch)
            {
                int user = next_user ? 1001 : 1000;
                if (seteuid(0) == -1)
                    fprintf(bfd, "%s: (%d) child error resetting to euid of 0: %s\n",
                            str_time(tmbuf), getpid(), strerror(errno));
                if (setegid(100) == -1)
                    fprintf(bfd, "%s: (%d) child error setting egid to %d: %s\n",
                            str_time(tmbuf), getpid(), 100, strerror(errno));
                if (seteuid(user) == -1)
                    fprintf(bfd, "%s: (%d) child error setting euid to %d: %s\n", 
                            str_time(tmbuf), getpid(), user, strerror(errno));
            }
            if (!NoLog)
                fprintf(bfd, "%s: (%d) child beginning recv, running as user/group: %d/%d\n", 
                        str_time(tmbuf), getpid(), geteuid(), getegid());
            int index = 0;
            int increment = 10000000;
            int end = 1000000000;
            char *memory_array[(increment / end) + 1];
            if (MemLoad)
            {
                for (index = 0; index < end; index += increment)
                {
                    memory_array[index / increment] = malloc(index);
                    if (!memory_array[index / increment])
                    {
                        if (!NoLog)
                            fprintf(bfd, "%s: (%d) ran out of memory after "
                                    "allocating %d bytes\n", str_time(tmbuf), 
                                    getpid(), index - increment);
                        break;
                    }
                }
                if ((index >= end) && (!NoLog))
                    fprintf(bfd, "%s: (%d) was able to allocate the full %d bytes\n",
                            str_time(tmbuf), getpid(), index);
            }
            if (CPULoad)
            {
                // Now stress the CPU
                int stress = 1;
                char stress_arr[2];
                time_t start, current;
                time(&start);
                current = start;
                while ((stress > 0) && (current - start < CPULoad))
                {
                    stress_arr[0] = stress;
                    stress++;
                    stress--;
                    time(&current);
                }
            }
            while ((len = recv(fd2, buffer, sizeof(buffer) - 1, 0)) > 0)
            {
                buffer[len] = 0;
                if (!NoLog)
                    fprintf(bfd, "%s: (%d) Received %s\n", str_time(tmbuf), 
                            getpid(), buffer);
                if (send(fd2, buffer, len, 0) < 0)
                {
                    fprintf(bfd, "%s: (%d) resent of recvd packet failed: %s\n",
                            str_time(tmbuf), getpid(), strerror(errno));
                    break;
                }
            }
            if (len == -1)
            {
                fprintf(bfd, "%s: (%d) Receive error %s\n", str_time(tmbuf), 
                        getpid(), strerror(errno));
            }
            close(fd2);
            if (MemLoad)
            {
                int index2;
                for (index2 = 0; index2 < index; index2 += increment)
                {
                    free(memory_array[index2 / increment]);
                }
            }
            if (!NoLog)
                fprintf(bfd, "%s: (%d) Terminating\n", str_time(tmbuf), getpid());
            return 0;
        }
    }
    fprintf(bfd, "%s: accept failed %s\n", str_time(tmbuf), strerror(errno));
    return 0;
}
