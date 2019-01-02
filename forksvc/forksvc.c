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
#include <security/pam_appl.h>
#include <security/pam_misc.h>

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

static struct pam_response pam_resp = { "", 0 };

int forksvc_conv(int num_msg, const struct pam_message **msg,
                 struct pam_response **resp, void *appdata_ptr)
{
    /* appdata_ptr is FILE * for the log file! */
    FILE *bfd = appdata_ptr;
    int i;
    char tmbuf[80];
    fprintf(bfd, "%s (%d) Entering forksvc_conv function %d msgs\n", 
            str_time(tmbuf), getpid(), num_msg);
    (*resp) = malloc(sizeof(struct pam_response) * num_msg);
    for (i = 0; i < num_msg; ++i)
    {
        fprintf(bfd, "%s (%d) PAM_MSG: msg[%d]: %d=%s\n", str_time(tmbuf),
                getpid(), i + 1, msg[i]->msg_style, msg[i]->msg);
        
        resp[i]->resp = "pwd";
        resp[i]->resp_retcode = PAM_SUCCESS;
    }
    fprintf(bfd, "%s (%d) Exiting forksvc_conv function\n", str_time(tmbuf),
            getpid());
    return PAM_SUCCESS;
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
    int opt;
    int cpu_load = 0;
    int mem_load = 0;
    int no_log = 0;
    int port = 8000;
    int user_switch = 0;
    int use_pam = 0;
    
    printf("Running forksvc\n");
    while ((opt = getopt(argc, argv, "c:mlu:p:a?")) != -1)
    {
        switch (opt)
        {
            case 'c':
                cpu_load = atoi(optarg);
                printf("CPU load %d secs\n", cpu_load);
                break;
            case 'm':
                mem_load = 1;
                printf("Memory load\n");
                break;
            case 'l':
                no_log = 1;
                printf("No log\n");
                break;
            case 'u':
                user_switch = atoi(optarg);
                printf("Switch user: %u\n", user_switch);
                break;
            case 'p':
                port = atoi(optarg);
                printf("Use port: %d\n", port);
                break;
            case 'a':
                use_pam = 1;
                printf("Use PAM\n");
                break;
            default:
                printf("forksvc [-cpuload <secs>] [-memoryload] [-lognone] [-userswitch <uid> [-auth]] [-port <port>]\n");
                return 1;
        }
    }
    if ((use_pam) && (!user_switch))
    {
        printf("Use of authentication without user switch makes no sense\n");
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (!no_log)
    {
        const char *deflog = "/home/user/proj/cgroup/forksvc/forksvc.log";
        if (!(bfd = fopen(deflog, "a")))
        {
            fprintf(stderr, "Error opening default log file: %s, %s\n", 
                    deflog, strerror(errno));
            const char *log_only = "forksvc.log";
            if (!(bfd = fopen(log_only, "a")))
            {
                fprintf(stderr, "Error opening log on local dir: %s\n", 
                        strerror(errno));
                return 1;
            }
            else
                fprintf(stderr, "Using %s on local dir\n", log_only);
        }
        setvbuf(bfd, NULL, _IONBF, 0);
    }
    if (user_switch)
    {
        // Start the way Litespeed starts; as nobody:nobody
        seteuid(65534);
        setegid(65533);
    }
    if ((!no_log) && 
        (fprintf(bfd, 
                 "%s: Bind and listen with forks on port %d, pid #%d%s%s%s%s\n", 
                 str_time(tmbuf), port, getpid(), cpu_load ? " [CPU Load]" : "",
                 mem_load ? " [Memory Load]" : "", 
                 user_switch ? " [User Switch]" : "",
                 use_pam ? " [PAM]" : "") < 0))
    {
        fprintf(stderr, "Error actually writing to the log: %s\n", 
                strerror(errno));
        return 1;
    }
    if (no_log)
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
        if (!no_log)
            fprintf(bfd, "%s: Received connection from %u.%u.%u.%u (fd: %d)\n", 
                    str_time(tmbuf),
                    ((unsigned char *)&sa2.sin_addr.s_addr)[0],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[1],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[2],
                    ((unsigned char *)&sa2.sin_addr.s_addr)[3],
                    fd2);
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
            if (!no_log)
                fprintf(bfd, "%s: PID #%d to process request\n", str_time(tmbuf), pid);
            rc = waitpid(pid, &status, 1); // waitpid
            if (!no_log)
                fprintf(bfd,"%s: PID #%d terminated, rc: %d, status %d\n", 
                        str_time(tmbuf), getpid(), rc, status);
        }
        else
        {
            // child
            char buffer[8192];
            int len;
            pam_handle_t *pamh = NULL;
            struct pam_conv conv = { forksvc_conv, bfd };
            
            close(fd);
            if (user_switch)
            {
                if (seteuid(0) == -1)
                    fprintf(bfd, "%s: (%d) child error resetting to euid of 0: %s\n",
                            str_time(tmbuf), getpid(), strerror(errno));
                if (use_pam)
                {
                    int retval;
                    if (!no_log)
                        fprintf(bfd, "%s: (%d) using pam in child\n", 
                                str_time(tmbuf), getpid());
                    retval = pam_start("forksvc", "user2", &conv, &pamh);
                    if (retval != PAM_SUCCESS)
                        fprintf(bfd, "%s: (%d) CHILD pam_start ERROR #%d\n", 
                                str_time(tmbuf), getpid(), retval);
                    else
                    {
                        if (!no_log)
                            fprintf(bfd, "%s: (%d) Did pam_start\n", 
                                    str_time(tmbuf), getpid());
                        if ((retval = pam_authenticate(pamh, PAM_SILENT)) != PAM_SUCCESS)
                            fprintf(bfd, "%s: (%d) CHILD pam_authenticate ERROR #%d\n", 
                                    str_time(tmbuf), getpid(), retval);
                        else 
                        {
                            if (!no_log)
                                fprintf(bfd, "%s: (%d) Did pam_authenticate\n",
                                        str_time(tmbuf), getpid());
                            if ((retval = pam_open_session(pamh, PAM_SILENT)) != PAM_SUCCESS)
                                fprintf(bfd, "%s: (%d) CHILD pam_open_session ERROR #%d\n",
                                        str_time(tmbuf), getpid());
                            else
                            {
                                if (!no_log)
                                    fprintf(bfd, "%s: (%d) Did pam_open_session\n",
                                            str_time(tmbuf), getpid());
                                
                                if ((retval = pam_acct_mgmt(pamh, PAM_SILENT)) != PAM_SUCCESS)
                                    fprintf(bfd, "%s: (%d) CHILD pam_acct_mgmt ERROR #%d\n", 
                                            str_time(tmbuf), getpid(), retval);
                                else 
                                {
                                    if (!no_log)
                                        fprintf(bfd, "%s: (%d) CHILD pam authenticated!\n", 
                                                str_time(tmbuf), getpid());
                                }
                            }
                        }
                    }
                    if (!no_log)
                        fprintf(bfd, "%s: (%d) completed PAM process\n", 
                                str_time(tmbuf), getpid());
                }
                if (setegid(100) == -1)
                    fprintf(bfd, "%s: (%d) child error setting egid to %d: %s\n",
                            str_time(tmbuf), getpid(), 100, strerror(errno));
                if (seteuid(user_switch) == -1)
                    fprintf(bfd, "%s: (%d) child error setting euid to %d: %s\n", 
                            str_time(tmbuf), getpid(), user_switch, 
                            strerror(errno));
            }
            if (!no_log)
                fprintf(bfd, "%s: (%d) child beginning recv, running as "
                        "user/group: %d/%d\n", 
                        str_time(tmbuf), getpid(), geteuid(), getegid());
            int index = 0;
            int increment = 10000000;
            int end = 1000000000;
            char *memory_array[(increment / end) + 1];
            if (mem_load)
            {
                for (index = 0; index < end; index += increment)
                {
                    memory_array[index / increment] = malloc(index);
                    if (!memory_array[index / increment])
                    {
                        if (!no_log)
                            fprintf(bfd, "%s: (%d) ran out of memory after "
                                    "allocating %d bytes\n", str_time(tmbuf), 
                                    getpid(), index - increment);
                        break;
                    }
                }
                if ((index >= end) && (!no_log))
                    fprintf(bfd, "%s: (%d) was able to allocate the full %d bytes\n",
                            str_time(tmbuf), getpid(), index);
            }
            if (cpu_load)
            {
                // Now stress the CPU
                int stress = 1;
                char stress_arr[2];
                time_t start, current;
                time(&start);
                current = start;
                while ((stress > 0) && (current - start < cpu_load))
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
                if (!no_log)
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
            if (mem_load)
            {
                int index2;
                for (index2 = 0; index2 < index; index2 += increment)
                {
                    free(memory_array[index2 / increment]);
                }
            }
            if (pamh)
                pam_end(pamh, 0);
            if (!no_log)
                fprintf(bfd, "%s: (%d) Terminating\n", str_time(tmbuf), getpid());
            return 0;
        }
    }
    fprintf(bfd, "%s: accept failed %s\n", str_time(tmbuf), strerror(errno));
    return 0;
}
