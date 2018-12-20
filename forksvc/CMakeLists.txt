cmake_minimum_required(VERSION 2.6)
project(forksvc_test)
set(SOURCE_FILES forksvc.c)
add_executable(forksvc ${SOURCE_FILES})
include_directories("/usr/lib64/glib-2.0/include")
include_directories("/usr/include/glib-2.0")
target_link_libraries( forksvc ${EXTRA_LIBS} gio-2.0 glib-2.0 )

