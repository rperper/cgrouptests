cmake_minimum_required(VERSION 2.9)
project(cgroup)
include_directories("/usr/lib64/glib-2.0/include")
include_directories("/usr/include/glib-2.0")
include_directories("../../lsr/include")

SET(cgroup_STAT_SRCS
    cgroupconn.cpp 
    cgroupuse.cpp
)

add_library(cgroup STATIC ${cgroup_STAT_SRCS})


