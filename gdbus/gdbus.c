#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

// Some variables we're going to need no matter what we do.
GDBusConnection *conn;
GError *err = NULL;
GDBusProxy *proxy;
GDBusInterfaceInfo *bus;
int uid;    

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

    
int gbus_systemd(void)
{
    printf("Did g_bus initial call (conn: %s), try to get the proxy\n", 
           conn ? "NOT NULL" : "NULL");
    memset(&bus, 0, sizeof(bus));
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
    
    printf("Building properties variant\n");
    GVariantBuilder *properties = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    //g_variant_builder_add(properties, "(sv)", "CPUShares", g_variant_new("s", "100"));
    g_variant_builder_add(properties, "(sv)", "Slice", g_variant_new("s", "test.slice"));
    g_variant_builder_add(properties, "(sv)", "Description", g_variant_new("s", "Bobs_Unit"));
    g_variant_builder_add(properties, "(sv)", "ExecStart", g_variant_new("s", "/bin/bash"));
    char *fn = "StartTransientUnit";
    printf("Building aux2 variant\n");
    GVariantBuilder *aux2 = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    g_variant_builder_add(aux2, "(sv)", "", g_variant_new("s", ""));
    printf("Building aux variant\n");
    GVariantBuilder *aux = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);
    printf("aux variant add \n");
    g_variant_builder_add(aux, "(sa(sv))", "", aux2);
    printf("Building parms variant\n");
    GVariant *parms = g_variant_new("(ssa(sv)a(sa(sv)))",
                                    "testunit.scope", // unit
                                    "replace",        // mode
                                    properties,
                                    aux);
    printf("Doing proxy call\n");
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
    else if (!(strcmp((char *)g_variant_get_type(rc), (char *)G_VARIANT_TYPE_STRING)))
        printf("%s returned job: %s\n", fn, g_variant_get_data(rc));
    else if (g_variant_is_container(rc))
        printf("%s returned a container\n", fn);
    else 
        printf("%s returned a type %s\n", fn, g_variant_get_type(rc));
    return 0;
}


int gbus_linger(int enable_linger)
{
    printf("Did g_bus initial call (conn: %s), try to get the logind proxy\n", 
           conn ? "NOT NULL" : "NULL");
    memset(&bus, 0, sizeof(bus));
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
    printf("Call SetUserLinger\n");
    printf("Doing proxy call\n");
    char *fn = "SetUserLinger";
    GVariant *rc = g_dbus_proxy_call_sync(proxy,
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
            case 'b':
                {
                    int my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %s\n", my_int ? "TRUE" : "FALSE");
                }
                break;
            case 'y':
                {
                    unsigned char my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'n':
            case 'q':
                {
                    unsigned short my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'i':
            case 'u':
            case 'h':
                {
                    unsigned int my_int;
                    g_variant_get(value, (char *)type, &my_int);
                    printf("          value: %u\n", my_int);
                }
                break;
            case 'x':
            case 't':
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
    printf("Did g_bus initial call (conn: %s), try to get the logind proxy\n", 
           conn ? "NOT NULL" : "NULL");
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
        return bus_print_all_properties("org.freedesktop.login1", output_path);
    }
    return 0;
    
}


int main(int argc, char *argv[])
{
    int opt;
    int linger = 0;
    int no_linger = 0;
    int start = 0;
    int get_user = 0;
    
    printf("Test GDBus in my environment to see if it works as advertized\n");
    conn = g_bus_get_sync(G_BUS_TYPE_SYSTEM,
                          NULL, // GCancellable
                          &err);
    if (err)
    {
        fprintf(stderr, "Error returned by g_bus_get_sync: %s\n", err->message);
        return 1;
    }
    printf("Did g_bus initial call (conn: %s), try to get the proxy\n", 
           conn ? "NOT NULL" : "NULL");
    while ((opt = getopt(argc, argv, "l:n:g:s?")) != -1)
    {
        switch (opt)
        {
            case 'l':
                linger = 1;
                uid = atoi(optarg);
                printf("Linger for user: %u\n", uid);
                break;
            case 'n':
                no_linger = 1;
                uid = atoi(optarg);
                printf("No linger for user: %u", uid);
                break;
            case 'g':
                get_user = 1;
                uid = atoi(optarg);
                printf("Get user for: %u", uid);
                break;
            case 's':
                start = 1;
                printf("Start transient unit\n");
                break;
            default:
                printf("gdbus [-linger <uid>] [-nolinger <uid>] [-getuser <uid> ] [-starttransientunit]\n");
                return 1;
        }
    }
    if (start)
        return gbus_systemd();
    if ((linger) || (no_linger))
        return gbus_linger(linger);
    if (get_user)
        return gbus_getuser();
    printf("You must specify an option\n");
    return 1;
}