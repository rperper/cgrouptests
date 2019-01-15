#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/mman.h>
#include <gio/gio.h>


// Some variables we're going to need no matter what we do.
enum tests {
    none, cpu, memory, block_io, block_write, block_read, tasks };
    
GDBusConnection *conn;
GError *err = NULL;
GDBusProxy *proxy;
GDBusInterfaceInfo *bus;
int uid = -1;
char unit[256] = { 0 };
char slice[256] = { 0 };
enum tests test = none;
char file[256] = { 0 };


static int print_annotations(GDBusAnnotationInfo **annotations, char *prefix)
{
    int index = 0;
    if (!annotations)
        return 0;
    while (*annotations)
    {
        printf("%sannotations[%d], ref_count: %d\n", prefix, index, 
               (*annotations)->ref_count);
        printf("%s   key: %s\n", prefix, (*annotations)->key);
        printf("%s   value: %s\n", prefix, (*annotations)->value);
        GDBusAnnotationInfo **subannotations = (*annotations)->annotations;
        char prefixa[2048];
        sprintf(prefixa,"%s   ", prefix);
        print_annotations(subannotations, prefixa);
        annotations++;
        index++;
    }
    return 0;
}

    
    
static int print_args(GDBusArgInfo **args, char *prefix, int in)
{
    int index = 0;
    if (!args)
        return 0;
    while (*args)
    {
        printf("%s%s_args[%d], ref_count: %d\n", prefix, in ? "in" : "out", 
               index, (*args)->ref_count);
        printf("%s   name: %s\n", prefix, (*args)->name);
        printf("%s   signature: %s\n", prefix, (*args)->signature);
        GDBusAnnotationInfo **annotations = (*args)->annotations;
        int aindex = 0;
        char prefixa[2048];
        sprintf(prefixa, "%s   ", prefix);
        print_annotations(annotations, prefixa);
        index++;
    }
    return 0;
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
            memory_array[index / increment] = malloc(index);
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
    char *file_input = "read.data";
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
    char *file_output = "write.data";
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


int add_properties(GVariantBuilder *properties, int argc, char *argv[], 
                   int *cpu, int *memory, int *block_io, int *tasks)
{
    int opt;
    for (opt = optind; opt < argc; ++opt)
    {
        char param[1024];
        char *equals1;
        char *equals2;
        char *name;
        char type_char;
        char *type;
        char *value;
        
        strcpy(param, argv[opt]);
        if ((!(equals1 = strchr(param, '='))) ||
            (!(equals2 = strchr(equals1 + 1, '='))))
        {
            printf("Bad argument (missing double equals): %s\n", argv[opt]);
            return -1;
        }
        *equals1 = 0;
        *equals2 = 0;
        name = param;
        type_char = *(equals1 + 1);
        type = (equals1 + 1);
        value = (equals2 + 1);
        if ((cpu) && (strstr(name, "CPU")))
            *cpu = 1;
        if ((memory) && (strstr(name,"Memory")))
            *memory = 1;
        if ((block_io) && (strstr(name,"Block")))
            *block_io = 1;
        if ((tasks) && (strstr(name, "Tasks")))
            *tasks = 1;
        switch (type_char) 
        {
            case 'b'://*(char *)G_VARIANT_TYPE_BOOLEAN:
            case 'y'://*(char *)G_VARIANT_TYPE_BYTE:
            case 'n'://*(char *)G_VARIANT_TYPE_INT16:
            case 'q'://*(char *)G_VARIANT_TYPE_UINT16:
            case 'i'://*(char *)G_VARIANT_TYPE_INT32:
            case 'u'://*(char *)G_VARIANT_TYPE_UINT32
            case 'h'://*(char *)G_VARIANT_TYPE_HANDLE
                printf("Adding int property: %s=%d\n", name, atoi(value));
                g_variant_builder_add(properties, "(sv)", name, 
                                      g_variant_new(type, atoi(value)));
                break;
            case 'x'://*(char *)G_VARIANT_TYPE_INT64:
            case 't'://*(char *)G_VARIANT_TYPE_UINT64:
                printf("Adding long property: %s=%ld\n", name, atol(value));
                g_variant_builder_add(properties, "(sv)", name, 
                                      g_variant_new(type, atol(value)));
                break;
            case 's'://*(char *)G_VARIANT_TYPE_STRING:
            case 'o'://*(char *)G_VARIANT_TYPE_OBJECT_PATH:
                printf("Adding string property: %s=%s\n", name, value);
                g_variant_builder_add(properties, "(sv)", name, 
                                      g_variant_new(type, value));
                break;
            default:
                printf("Type not handled\n");
                return -1;
        }
    }          
    //g_variant_builder_add(properties, "(sv)", "CPUShares", g_variant_new("s", "100"));
    return 0;
}


int gbus_systemd(int argc, char *argv[])
{
    printf("Did g_bus initial call (conn: %s), try to get the proxy\n", 
           conn ? "NOT NULL" : "NULL");
    memset(&bus, 0, sizeof(bus));
    proxy = g_dbus_proxy_new_sync(conn,
                                  0,//G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                                 /* GDBusInterfaceInfo */
                                  "org.freedesktop.systemd1",           /* name */
                                  "/org/freedesktop/systemd1",          /* object path */
                                  "org.freedesktop.systemd1.Manager",   /* interface */
                                  NULL,                                 /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");
    /*
    bus = g_dbus_proxy_get_interface_info(proxy);
    
    if (!bus)
        printf("No interface info???\n");
    else
    {
        printf("interface gotten, ref_count: %d\n", bus->ref_count);
        printf("D-bus name: %s\n", bus->name);
        GDBusMethodInfo **methods = bus->methods;
        int index = 0;
        while ((methods) && (*methods))
        {
            printf("   method[%d] ref_count: %d\n", index, (*methods)->ref_count);
            printf("      name      : %s\n", (*methods)->name);
            GDBusArgInfo **args;
            args = (*methods)->in_args;
            char *prefix = "         ";
            print_args(args, prefix, 1);
            args = (*methods)->out_args;
            print_args(args, prefix, 0);
            GDBusAnnotationInfo **annotations = (*methods)->annotations;
            print_annotations(annotations, prefix);
            methods++;
            index++;
        }
        index = 0;
        GDBusSignalInfo **signals = bus->signals;
        while ((signals) && (*signals))
        {
            printf("   signal[%d] ref_count: %d\n", index, (*signals)->ref_count);
            printf("      name   : %s\n", (*signals)->name);
            GDBusArgInfo **args;
            args = (*signals)->args;
            char *prefix = "         ";
            print_args(args, prefix, 1);
            GDBusAnnotationInfo **annotations = (*signals)->annotations;
            print_annotations(annotations, prefix);
            signals++;
            index++;
        }
        index = 0;
        GDBusPropertyInfo **properties = bus->properties;
        while ((properties) && (*properties))
        {
            printf("   property[%d] ref_count: %d\n", index, (*properties)->ref_count);
            printf("      name     : %s\n", (*properties)->name);
            printf("      signature: %s\n", (*properties)->signature);
            printf("      flags    : %d\n", (int)(*properties)->flags);
            char *prefix = "         ";
            GDBusAnnotationInfo **annotations = (*properties)->annotations;
            print_annotations(annotations, prefix);
            signals++;
            index++;
        }
        char *prefix = "   ";
        GDBusAnnotationInfo **annotations = bus->annotations;
        print_annotations(annotations, prefix);
    }
    
    gchar **names = g_dbus_proxy_get_cached_property_names(proxy);
    gchar **free_names = names;
    int index = 0;
    while ((names) && (*names))
    {
        GVariant *value = g_dbus_proxy_get_cached_property(proxy, *names);
        printf("property_name[%d]: %s = %s\n", index, *names,
               strcmp(g_variant_get_type_string(value), (char *)G_VARIANT_TYPE_STRING) ?
               g_variant_get_type(value) : g_variant_get_data(value));
        names++;
        index++;
    }
    if ((!free_names) || (!*free_names))
        printf("No property names returned\n");
    
    g_strfreev(free_names);
    */
    printf("Building properties variant\n");
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    //g_variant_builder_add(properties, "(sv)", "CPUShares", g_variant_new("s", "100"));
    g_variant_builder_add(properties, "(sv)", "Description", g_variant_new("s", "Bobs_Unit"));
    char unit[256];
    sprintf(unit,"run-%u.scope", (unsigned int)getpid());
    GVariantBuilder *pids_array = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add_value(pids_array, g_variant_new("u", (unsigned int)getpid()));
    g_variant_builder_add(properties, "(sv)", "PIDs", g_variant_new("au", pids_array));
    char *fn = "StartTransientUnit";
    int  cpu = 0;
    int  memory = 0;
    int  block_io = 0;
    int  tasks = 0;
    if (optind < argc)
    {
        if (add_properties(properties, argc, argv, &cpu, &memory, &block_io, &tasks) == -1)
            return 1;
    }
    /*
    printf("Building aux2 variant\n");
    GVariantBuilder *aux2 = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add(aux2, "(sv)", "unit", g_variant_new("s", unit2));
    printf("Building aux variant\n");
    GVariantBuilder *aux = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    printf("aux variant add \n");
    g_variant_builder_add(aux, "(sa(sv))", unit2, aux2);
    */
    printf("Building parms variant\n");
    /*
    GVariant *parms = g_variant_new("(ssa(sv)a(sa(sv)))",
                                    unit,       // unit
                                    "fail",     // mode
                                    properties,
                                    aux);
    */
    GVariant *parms = g_variant_new("(ssa(sv)a(sa(sv)))",
                                    unit,
                                    "fail",
                                    properties,
                                    NULL);
    printf("Doing proxy call\n");
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          parms,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);// userdata
    if (rc)
    {
        if (!(strcmp((char *)g_variant_get_type(rc), (char *)G_VARIANT_TYPE_STRING)))
            printf("%s returned string: %s\n", fn, g_variant_get_data(rc));
        else if (g_variant_is_container(rc))
            printf("%s returned a container\n", fn);
        else 
            printf("%s returned a type %s\n", fn, g_variant_get_type(rc));
    }
    else if (err)
    {
        printf("Error in %s: %s\n", fn, err->message);
    }
    else
        printf("No rc or Error???\n");
    return 0;
}


int gbus_setproperties(int argc, char *argv[], int uid)
{
    int finalrc = 1;
    if (uid == -1)
    {
        printf("Invalid uid\n");
        return -1;
    }
    printf("Did g_bus initial call (conn: %s), try to set the unit properties\n", 
           conn ? "NOT NULL" : "NULL");
    GDBusProxy *proxy = g_dbus_proxy_new_sync(conn,
                                              G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                              NULL,                                 /* GDBusInterfaceInfo */
                                              "org.freedesktop.systemd1",           /* name */
                                              "/org/freedesktop/systemd1",          /* object path */
                                              "org.freedesktop.systemd1.Manager",   /* interface */
                                              NULL,                                 /* GCancellable */
                                              &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");

    char *fn = "SetUnitProperties";
    printf("Call GetUnit to get the path\n");
    char unit[256];
    sprintf(unit, "user-%u.slice", uid);
    printf("Building properties variant\n");
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    if (add_properties(properties, argc, argv, NULL, NULL, NULL, NULL) < 0)
        return 1;
    
    GVariant *parms = g_variant_new("(sba(sv))",
                                    unit,             // unit
                                    0,                // runtime
                                    properties);
    
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          parms,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
        printf("Set properties ok\n");
    g_object_unref(proxy);
    return finalrc;
}


int parse_uid_from_slice(char *slice)
{
    char remain[256];
    char *suffix;
    if (memcmp("user-", slice, 5))
        return -1;
    strcpy(remain, &slice[5]);
    if (suffix = strstr(remain, ".slice"))
    {
        *suffix = 0;
        int uid = atoi(remain);
        printf("Extracted uid: %d\n", uid);
        return uid;
    }
    return -1;
}


int start_transient(int argc, char *argv[])
{
    printf("Building properties variant\n");
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    //g_variant_builder_add(properties, "(sv)", "Description", g_variant_new("s", "Bobs_Unit"));
    GVariantBuilder *pids_array = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    if (optind < argc)
    {
        if (gbus_setproperties(argc, argv, parse_uid_from_slice(slice)) == -1)
            return 1;
    }
    //if (optind < argc)
    //{
    //    if (add_properties(properties, argc, argv, NULL, NULL, NULL, NULL) == -1)
    //        return 1;
    //}
    
    g_variant_builder_add_value(pids_array, g_variant_new("u", (unsigned int)getpid()));
    g_variant_builder_add(properties, "(sv)", "PIDs", g_variant_new("au", pids_array));
    if (slice[0])
        g_variant_builder_add(properties, "(sv)", "Slice", g_variant_new("s", slice));
    char *fn = "StartTransientUnit";
    printf("Building parms variant\n");
    //char unit[256];
    //sprintf(unit,"run-%u.scope", (unsigned int)getpid());
    GVariant *parms = g_variant_new("(ssa(sv)a(sa(sv)))",
                                    unit,
                                    "fail",
                                    properties,
                                    NULL);
    printf("Doing proxy call\n");
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          parms,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);// userdata
    if (rc)
    {
        if (!(strcmp((char *)g_variant_get_type(rc), (char *)G_VARIANT_TYPE_STRING)))
            printf("%s returned string: %s\n", fn, g_variant_get_data(rc));
        else if (g_variant_is_container(rc))
            printf("%s returned a container\n", fn);
        else 
            printf("%s returned a type %s\n", fn, g_variant_get_type(rc));
    }
    else if (err)
    {
        printf("Error in %s: %s\n", fn, err->message);
    }
    else
    
        printf("No rc or Error???\n");
    
    return 0;
}



int gbus_add(int argc, char *argv[])
{
    proxy = g_dbus_proxy_new_sync(conn,
                                  0,//G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                                 /* GDBusInterfaceInfo */
                                  "org.freedesktop.systemd1",           /* name */
                                  "/org/freedesktop/systemd1",//output_path,                          /* object path */
                                  "org.freedesktop.systemd1.Manager",   /* interface */
                                  NULL,                                 /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    sprintf(unit, "run_%d.scope", getpid());
    printf("Final unit: %s\n", unit);
    return start_transient(argc, argv);
}


int gbus_linger(int enable_linger)
{
    printf("Did g_bus initial call (conn: %s), try to get the logind proxy\n", 
           conn ? "NOT NULL" : "NULL");
    memset(&bus, 0, sizeof(bus));
    GDBusProxy *proxy_login = g_dbus_proxy_new_sync(conn,
                                                    G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                                    NULL,                               /* GDBusInterfaceInfo */
                                                    "org.freedesktop.login1",           /* name */
                                                    "/org/freedesktop/login1",          /* object path */
                                                    "org.freedesktop.login1.Manager",   /* interface */
                                                    NULL,                               /* GCancellable */
                                                    &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    /*
    printf("Created proxy connection\n");

    bus = g_dbus_proxy_get_interface_info(proxy_login);
    
    if (!bus)
        printf("No interface info???\n");
    else
    {
        printf("interface gotten, ref_count: %d\n", bus->ref_count);
        printf("D-bus name: %s\n", bus->name);
        GDBusMethodInfo **methods = bus->methods;
        int index = 0;
        while ((methods) && (*methods))
        {
            printf("   method[%d] ref_count: %d\n", index, (*methods)->ref_count);
            printf("      name      : %s\n", (*methods)->name);
            GDBusArgInfo **args;
            args = (*methods)->in_args;
            char *prefix = "         ";
            print_args(args, prefix, 1);
            args = (*methods)->out_args;
            print_args(args, prefix, 0);
            GDBusAnnotationInfo **annotations = (*methods)->annotations;
            print_annotations(annotations, prefix);
            methods++;
            index++;
        }
        index = 0;
        GDBusSignalInfo **signals = bus->signals;
        while ((signals) && (*signals))
        {
            printf("   signal[%d] ref_count: %d\n", index, (*signals)->ref_count);
            printf("      name   : %s\n", (*signals)->name);
            GDBusArgInfo **args;
            args = (*signals)->args;
            char *prefix = "         ";
            print_args(args, prefix, 1);
            GDBusAnnotationInfo **annotations = (*signals)->annotations;
            print_annotations(annotations, prefix);
            signals++;
            index++;
        }
        index = 0;
        GDBusPropertyInfo **properties = bus->properties;
        while ((properties) && (*properties))
        {
            printf("   property[%d] ref_count: %d\n", index, (*properties)->ref_count);
            printf("      name     : %s\n", (*properties)->name);
            printf("      signature: %s\n", (*properties)->signature);
            printf("      flags    : %d\n", (int)(*properties)->flags);
            char *prefix = "         ";
            GDBusAnnotationInfo **annotations = (*properties)->annotations;
            print_annotations(annotations, prefix);
            signals++;
            index++;
        }
        char *prefix = "   ";
        GDBusAnnotationInfo **annotations = bus->annotations;
        print_annotations(annotations, prefix);
    }
    
    gchar **names = g_dbus_proxy_get_cached_property_names(proxy_login);
    gchar **free_names = names;
    int index = 0;
    while ((names) && (*names))
    {
        GVariant *value = g_dbus_proxy_get_cached_property(proxy_login, *names);
        printf("property_name[%d]: %s = %s\n", index, *names,
               strcmp(g_variant_get_type_string(value), (char *)G_VARIANT_TYPE_STRING) ?
               g_variant_get_type(value) : g_variant_get_data(value));
        names++;
        index++;
    }
    if ((!free_names) || (!*free_names))
        printf("No property names returned\n");
    
    g_strfreev(free_names);
    */
    printf("Call SetUserLinger\n");
    printf("Doing proxy call, uid: %u, euid: %u\n", getuid(), geteuid());
    char *fn = "SetUserLinger";
    GVariant *rc = g_dbus_proxy_call_sync(proxy_login,
                                          fn,
                                          g_variant_new("(ubb)",
                                                        uid,
                                                        enable_linger,
                                                        0),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else if (!(strcmp((char *)g_variant_get_type(rc), (char *)G_VARIANT_TYPE_STRING)))
        printf("%s returned string: %s\n", fn, g_variant_get_data(rc));
    else if (g_variant_is_container(rc))
        printf("%s returned a container\n", fn);
    else 
        printf("%s returned a type %s\n", fn, g_variant_get_type(rc));
    return 0;
    
}


int bus_print_all_properties(const char *dest, const char *path) 
{
    GDBusProxy *proxy;
    
    memset(&bus, 0, sizeof(bus));
    printf("Getting properties of %s, %s\n", dest, path);
    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                               /* GDBusInterfaceInfo */
                                  dest,                               /* name */
                                  path,                               /* object path */
                                  "org.freedesktop.DBus.Properties",  /* interface */
                                  NULL,                               /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created secondary proxy connection\n");
    char *fn = "GetAll";
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          g_variant_new("(s)",
                                                        ""),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else if (!(strcmp((char *)g_variant_get_type(rc), (char *)G_VARIANT_TYPE_STRING)))
        printf("%s returned string: %s\n", fn, g_variant_get_data(rc));
    else 
        printf("%s returned a type %s\n", fn, g_variant_get_type(rc));
    // I'll assume that the actual type is (a{sv})
    int children;
    int element;
    GVariant *map = g_variant_get_child_value(rc, 0);
    printf("%d elements, type: %s\n", children = g_variant_n_children(map),
           g_variant_get_type(map));
    for (element = 0; element < children; ++element)
    {
        GVariant *pair = g_variant_get_child_value(map, element);
        printf("       child type[%d]: %s\n", element, g_variant_get_type(pair));
        char *key;
        GVariant *value;
        const GVariantType *type;
        char type_char;
        g_variant_get(pair, "{sv}", &key, &value);
        printf("          key: %s, type of value: %s\n", key, type = g_variant_get_type(value));
        type_char = *(char *)type;
        switch (type_char) 
        {
            case 'a':// G_VARIANT_TYPE_ARRAY
                if (*(((char *)type) + 1) == 's')
                {
                    gsize len = g_variant_n_children(value);
                    GVariant *mem;
                    char *arr_type = (((char *)type) + 1);
                    int i;
                    printf("          %d elements:\n", len);
                    for (i = 0; i < len; ++i)
                    {
                        unsigned char *my_char;  
                            
                        GVariant *child = g_variant_get_child_value(value, i);
                        g_variant_get(child, (char *)arr_type, &my_char);
                        printf("            [%u]: %s\n", i, my_char);
                    }
                }
                else
                    printf("          Type not handled\n");
                break;
            case 'b'://*(char *)G_VARIANT_TYPE_BOOLEAN:
                {
                    int my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %s\n", my_int ? "TRUE" : "FALSE");
                }
                break;
            case 'y'://*(char *)G_VARIANT_TYPE_BYTE:
                {
                    unsigned char my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'n'://*(char *)G_VARIANT_TYPE_INT16:
            case 'q'://*(char *)G_VARIANT_TYPE_UINT16:
                {
                    unsigned short my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'i'://*(char *)G_VARIANT_TYPE_INT64:
            case 'u'://*(char *)G_VARIANT_TYPE_UINT32
            case 'h'://*(char *)G_VARIANT_TYPE_HANDLE
                {
                    unsigned int my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'x'://*(char *)G_VARIANT_TYPE_INT64:
            case 't'://*(char *)G_VARIANT_TYPE_UINT64:
                {
                    unsigned long my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %lu\n", my_int);
                }
                break;
            case 's'://*(char *)G_VARIANT_TYPE_STRING:
            case 'o'://*(char *)G_VARIANT_TYPE_OBJECT_PATH:
                {
                    unsigned char *my_char;  
                    g_variant_get(value, (char *)type, &my_char);
                    printf("          value: %s\n", my_char);
                }
                break;
            default:
                {
                    printf("          Type not handled\n");
                }
        }
    }
    return 0;
}



int gbus_getuser()
{
    int finalrc = 1;
    printf("Did g_bus initial call (conn: %s), try to get the logind proxy\n", 
           conn ? "NOT NULL" : "NULL");
    // This proxy is only used to get the path.
    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                               /* GDBusInterfaceInfo */
                                  "org.freedesktop.login1",           /* name */
                                  "/org/freedesktop/login1",          /* object path */
                                  "org.freedesktop.login1.Manager",   /* interface */
                                  NULL,                               /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");

    char *fn = "GetUser";
    printf("Call GetUser to get the path\n");
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          g_variant_new("(u)",
                                                        uid),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
    {
        char *output_path;
        g_variant_get(rc, "(o)", &output_path);
        printf("Output path: %s\n", output_path);
        finalrc = bus_print_all_properties("org.freedesktop.login1", output_path);
    }
    g_object_unref(proxy);
    return finalrc;
    
}


int gbus_getproperties()
{
    int finalrc = 1;
    printf("Did g_bus initial call (conn: %s), try to get the unit properties\n", 
           conn ? "NOT NULL" : "NULL");
    proxy = g_dbus_proxy_new_sync(conn,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                                 /* GDBusInterfaceInfo */
                                  "org.freedesktop.systemd1",           /* name */
                                  "/org/freedesktop/systemd1",          /* object path */
                                  "org.freedesktop.systemd1.Manager",   /* interface */
                                  NULL,                                 /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");


    char *fn = "GetUnit";
    printf("Call GetUnit to get the path\n");
    if (uid != -1)
    {
        sprintf(unit, "user-%u.slice", uid);
    }
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          g_variant_new("(s)",
                                                        unit),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
    {
        char *output_path;
        g_variant_get(rc, "(o)", &output_path);
        printf("Output path: %s\n", output_path);
        finalrc = bus_print_all_properties("org.freedesktop.systemd1", output_path);
    }
    g_object_unref(proxy);
    return finalrc;
    
}


int get_session_path()
{
    GDBusProxy          *proxy;
    GDBusConnection     *conn2;
    
    conn2 = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                           NULL, // GCancellable
                           &err);

    int finalrc = 1;
    printf("Extract the session path for the PID\n"); 
    // This proxy is only used to get the path.
    proxy = g_dbus_proxy_new_sync(conn2,
                                  G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
                                  NULL,                               /* GDBusInterfaceInfo */
                                  "org.freedesktop.login1",           /* name */
                                  "/org/freedesktop/login1",          /* object path */
                                  "org.freedesktop.login1.Manager",   /* interface */
                                  NULL,                               /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");

    char *fn = "GetSessionByPID";
    printf("Call %s to get the path\n", fn);
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          g_variant_new("(u)",
                                                        getpid()),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
    {
        char *output_path;
        g_variant_get(rc, "(o)", &output_path);
        printf("Output path: %s\n", output_path);
        finalrc = 0;
    }
    g_object_unref(conn2);
    g_object_unref(proxy);
    return finalrc;
    
}


int do_list_sessions()
{
    GDBusProxy          *proxy;
    GDBusConnection     *conn2;
    
    conn2 = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                           NULL, // GCancellable
                           &err);

    int finalrc = 1;
    printf("List sessions:\n"); 
    // This proxy is only used to get the path.
    proxy = g_dbus_proxy_new_sync(conn2,
                                  0,
                                  NULL,                               /* GDBusInterfaceInfo */
                                  "org.freedesktop.login1",           /* name */
                                  "/org/freedesktop/login1",          /* object path */
                                  "org.freedesktop.login1.Manager",   /* interface */
                                  NULL,                               /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync: %s\n", err->message);
        return 1;
    }
    printf("Created proxy connection\n");

    char *fn = "ListSessions";
    printf("Call %s to get the list\n", fn);
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          NULL,
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
        printf("Error returned by g_bus_proxy_call_sync: %s\n", err->message);
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
    {
        printf("List sessions: type: %s, container: %s, children: %u, class: %d\n", 
               g_variant_get_type_string(rc), 
               g_variant_is_container(rc) ? "YES" : "NO",
               g_variant_n_children(rc),
               g_variant_classify(rc));
        GVariant *array = g_variant_get_child_value(rc, 0);
        printf("Extracted array\n");
        int i = 0;
        gsize len = g_variant_n_children(array);
        printf("Extracted len: %d\n", len);
        GVariant *child;
        for (i = 0; i < len; ++i)
        {
            char *session_id = NULL;;  
            int   user_id = 0;
            char *user_name = NULL;
            char *seat_id = NULL;
            char *user_object_path = NULL;
            
            child = g_variant_get_child_value(array, i);
            printf("parent type: %s, child type: %s\n", g_variant_get_type_string(array),
                   g_variant_get_type_string(child));
            g_variant_get(child, (char *)"(susso)", &session_id, &user_id, 
                          &user_name, &seat_id, &user_object_path);
            printf("  [%u]: id: %s, user: %u, name: %s, seat: %s, path: %s\n", 
                   i, session_id, user_id, user_name, seat_id, user_object_path);
            i++;
        }
        finalrc = 0;
    }
    g_object_unref(conn2);
    g_object_unref(proxy);
    return finalrc;
    
}


int do_create_session()
{
    GDBusProxy          *proxy;
    char *fn;
    
    int finalrc = 1;
    // This proxy is only used to get the path.
    proxy = g_dbus_proxy_new_sync(conn,
                                  0,
                                  NULL,                               /* GDBusInterfaceInfo */
                                  "org.freedesktop.login1",           /* name */
                                  "/org/freedesktop/login1",          /* object path */
                                  "org.freedesktop.login1.Manager",   /* interface */
                                  NULL,                               /* GCancellable */
                                  &err);
    if (err)
    {
        printf("Error returned by g_bus_proxy_new_sync for login1: %s\n", 
               err->message);
        return 1;
    }
    printf("Created proxy connection\n");
    
    seteuid(uid);
    
    fn = "GetSessionByPID";
    printf("Call %s to get the path\n", fn);
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                          fn,
                                          g_variant_new("(u)",
                                                        getpid()),
                                          G_DBUS_CALL_FLAGS_NONE,
                                          -1,   // Default timeout
                                          NULL, // GCancellable
                                          &err);
    printf("Returned from proxy call\n");
    if (err)
    {
        printf("Error returned by %s: %s\n", fn, err->message);
        finalrc = 0;  // This is actually good!
        err = NULL;
    }
    else if (!rc)
        printf("%s returned NULL!\n", fn);
    else 
    {
        char *output_path;
        g_variant_get(rc, "(o)", &output_path);
        printf("Output path for session: %s\n", output_path);
        finalrc = 0;
    }

    if (!finalrc)
    {
        finalrc = 1;
        fn = "CreateSession";
                                      
        GVariant *parms = g_variant_new("(uusssssussbssa(sv))",
                                        uid,                //uid
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
    
        GVariant *rc = g_dbus_proxy_call_sync(proxy,
                                              fn,
                                              parms,
                                              G_DBUS_CALL_FLAGS_NONE,
                                              -1,   // Default timeout
                                              NULL, // GCancellable
                                              &err);
        printf("Returned from proxy call\n");
        if (err)
            printf("Error returned by %s: %s\n", fn, err->message);
        else if (!rc)
            printf("%s returned NULL!\n", fn);
        else 
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
            finalrc = 0;
        }
    }
    g_object_unref(proxy);
    return finalrc;
    
}


void print_opts()
{
    printf("gdbus [-linger <uid>] [-nolinger <uid>] [-getuser <uid> ] "
           "[-properties <uid or unit>] [-setproperties <uid>] "
           "[name=(type)=value]..] [-transientunitstart] [-a [slice_name]] "
           "[-cputest] [-memorytest] [-blockiotest] [-readstest <file>] "
           "[-writestest <file>] [-forkstest] [-enter] [-2proc(fork)] [-idsession] "
           "[-1.ListSessions] [3.CreateSession <uid>]\n");
    printf("For example:\n");
    printf("   To set linger: ./gdbus -l 1001\n");
    printf("To set properties, you should get properties first, remember the name and type and enter them, like this:\n");
    printf("   ./gdbus -s 1001 CPUShares=t=100 DevicePolicy=s=strict\n");
}


int main(int argc, char *argv[])
{
    int opt;
    int linger = 0;
    int no_linger = 0;
    int start = 0;
    int get_user = 0;
    int get_properties = 0;
    int set_properties = 0;
    int add = 0;
    int enter = 0;    
    int rc = 0;
    int do_fork = 0;
    int session_path = 0;
    int list_sessions = 0;
    int create_session = 0;
    int fork_first = 0;
    
    printf("Test GDBus in my environment to see if it works as advertized\n");
    if (err)
    {
        fprintf(stderr, "Error returned by g_bus_get_sync: %s\n", err->message);
        return 1;
    }
    printf("Did g_bus initial call (conn: %s), try to get the proxy\n", 
           conn ? "NOT NULL" : "NULL");
    while ((opt = getopt(argc, argv, "3:1e2icmr:bw:fl:n:g:p:s:ta:?")) != -1)
    {
        switch (opt)
        {
            case 'c':
                test = cpu;
                printf("Specified CPU test\n");
                break;
            case 'm':
                test = memory;
                printf("Specified Memory test\n");
                break;
            case 'b':
                test = block_io;
                printf("Specified Block I/O test\n");
                break;
            case 'r':
                test = block_read;
                strcpy(file, optarg);
                printf("Specified Block read test\n");
                break;
            case 'w':
                test = block_write;
                strcpy(file, optarg);
                printf("Specified Block write test\n");
                break;
            case 'f':
                test = tasks;
                printf("Specified forks (tasks) test\n");
                break;
            case 'l':
                linger = 1;
                uid = atoi(optarg);
                printf("Linger for user: %u\n", uid);
                break;
            case 'n':
                no_linger = 1;
                uid = atoi(optarg);
                printf("No linger for user: %u\n", uid);
                break;
            case 'g':
                get_user = 1;
                uid = atoi(optarg);
                printf("Get user for: %u\n", uid);
                break;
            case 'p':
                get_properties = 1;
                if ((optarg[0] >= '0') && (optarg[0] <= '9'))
                {
                    uid = atoi(optarg);
                    printf("Get properties for user: %u\n", uid);
                }
                else 
                {
                    strcpy(unit, optarg);
                    printf("Get properties for unit: %s\n", unit);
                }
                break;
            case 's':
                set_properties = 1;
                uid = atoi(optarg);
                printf("Set properties for user: %u\n", uid);
                break;
            case 'a':
                add = 1;
                strcpy(slice, optarg);
                printf("CreateScope in slice: %s\n", unit);
                break;
            case 't':
                start = 1;
                printf("Start transient unit\n");
                break;
            case 'e':
                enter = 1;
                printf("Wait for enter\n");
                break;
            case '2':
                do_fork = 1;
                printf("Do fork after function\n");
                break;
            case 'i':
                session_path = 1;
                printf("Get login session path for current pid (%d)\n", getpid());
                break;
            case '1':
                list_sessions = 1;
                printf("List sessions\n");
                break;
            case '3':
                create_session = 1;
                uid = atoi(optarg);
                printf("CreateSession for uid: %d\n", uid);
                break;
            default:
                print_opts();
                return 1;
        }
    }
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                          NULL, // GCancellable
                          &err);
    if (add)
        rc = gbus_add(argc, argv);
    if (create_session)
        rc = do_create_session();
    if (start)
        rc = gbus_systemd(argc, argv);
    if ((linger) || (no_linger))
        return gbus_linger(linger);
    if (get_user)
        return gbus_getuser();
    if (get_properties)
        return gbus_getproperties();
    if (set_properties)
        return gbus_setproperties(argc, argv, uid);
    g_object_unref(conn);
    if (do_fork)
        fork();
    if (session_path)
        get_session_path();
    if (list_sessions)
        do_list_sessions();
    switch (test)
    {
        case none:
        default:
            printf("No test specified\n");
            break;      
        case cpu:
            rc = cpu_load(60);
        case memory:
            rc = mem_load();
        case block_io:
            rc = block_io_load();
        case block_read:
            rc =  block_read_load();
        case block_write:
            rc =  block_write_load();
        case tasks:
            rc = tasks_load();
    }
    if (enter)
    {
        printf("(%u) Press ENTER to end ->", getpid());
        char input[80];
        gets(input);
    }
    printf("gdbus end\n");
    return rc;
}