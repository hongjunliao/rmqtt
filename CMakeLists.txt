###########################################################################################
#
# This file is PART of rmqtt project
#
# author hongjun.liao <docici@126.com>
# date 2023/6/27

###########################################################################################
cmake_minimum_required(VERSION 2.8.12)
include(deps/libhp/cmakefiles/libhp.cmake) # hp_cmake_find_deps

###########################################################################################
# rmqtt 
project(rmqtt)

###########################################################################################
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

###########################################################################################
# sources and deps
set(g_withs 1 1 1 1 1 1 1)
set(g_hdrs .nullfilesub.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h .nullfilesrc.h)
set(g_incs "deps/libhp/include/" .find_path "${PROJECT_BINARY_DIR}/ deps/" .find_path .find_path .find_path .find_path )
set(g_deps libhp sds .src mongoose inih redis c-vector)
set(g_libs libhp "deps/sds/*.c" "src/*.c " "deps/mongoose/mongoose.c" "deps/inih/*.c"
	"deps/redis/src/adlist.c deps/redis/src/zmalloc.c deps/redis/src/dict.c deps/redis/src/siphash.c deps/redis/src/mt19937-64.c"
	"deps/c-vector/*.c deps/c-vector/*.cc")

###########################################################################################

# for libhp
set(LIBHP_WITH_MQTT 1 CACHE INTERNAL "LIBHP_WITH_MQTT")
set(LIBHP_WITH_REDIS 1 CACHE INTERNAL "LIBHP_WITH_REDIS")
set(LIBHP_WITH_CJSON 1 CACHE INTERNAL "LIBHP_WITH_CJSON")

hp_cmake_find_deps(SRCS RMQTT_WITH_NULL_ g_withs g_hdrs g_incs g_deps g_libs)
if(MSVC)
	file(GLOB SRCS ${SRCS} deps/libc/*.c)
endif()

#message("rmqtt: SRCS=${SRCS}")
add_executable(rmqtt ${SRCS} )
###########################################################################################

foreach(dep IN LISTS g_deps  )
	message("rmqtt: dep='${dep}', ${dep}_INCLUDE_DIRS='${${dep}_INCLUDE_DIRS}', ${dep}_LIBRARIES='${${dep}_LIBRARIES}'")
	target_include_directories(rmqtt PRIVATE ${${dep}_INCLUDE_DIRS} )
	target_link_libraries(rmqtt ${${dep}_LIBRARIES} ) 
endforeach() 

#target_include_directories(rmqtt PRIVATE "${PROJECT_BINARY_DIR}" src/ deps/libhp/include/ 
#	deps/libhp/ deps/libhp/deps/ deps/)
	
if(MSVC)
target_include_directories(rmqtt PRIVATE deps/libhp/include/win/)
endif()
if(LIBHP_WITH_WIN32_INTERROP)
target_include_directories(rmqtt PRIVATE deps/libhp/deps/redis/deps/)
endif()

target_compile_options(rmqtt PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
)
if(MSVC)
	target_compile_options(rmqtt PUBLIC -D_CRT_SECURE_NO_WARNINGS  /MTd)
endif()

target_link_directories(rmqtt PRIVATE lib/ deps/libhp/lib/)
if(LIBHP_WITH_WIN32_INTERROP)
	target_link_libraries(rmqtt Win32_Interop lua jemalloc redis-server uv_a)
endif()

###########################################################################################
configure_file("config.h.in" "config.h")

################################################################################################################################################


# sources


#add_custom_command(TARGET rmqtt POST_BUILD  COMMAND ${CMAKE_COMMAND} -E echo done _________________________ VERBATIM)
#add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/src/gen/git_commit_id.h COMMAND ../scripts/prebuild.sh COMMENT "prebuilding ...")

#if(WIN32)
#target_link_options(rmqtt PRIVATE /NODEFAULTLIB:libc /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmt.lib 
#	/NODEFAULTLIB:msvcrtd.lib /NODEFAULTLIB:msvcrtd.lib)
#endif()


##########################################################################################
# test: pub_file
#project(pub_file)
# sources
#file(GLOB SRCS test/pub_file/*.c test/README.md)
#add_executable(pub_file ${SRCS} )
#target_compile_options(pub_file PUBLIC
#    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
#     for mongoose
#    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
#     for redis
#    -DDICT_BENCHMARK_MAIN
#     for cJSON
#    -DcJSON_FREE_PRINT
#)
#if(NOT WIN32)
#	target_compile_options(pub_file PUBLIC  -O0 -g3  -Wall)
#else()
#	target_compile_options(pub_file PUBLIC -D_CRT_SECURE_NO_WARNINGS  /MTd)
#endif()
#
#target_include_directories(pub_file PRIVATE  "${PROJECT_BINARY_DIR}" include/ src/ deps/libhp/include/ deps/libhp/ deps/libhp/deps/ deps/ 
#	deps/libhp/deps/redis/deps/)
#if(MSVC)
#target_include_directories(pub_file PRIVATE deps/libhp/include/win/)
#endif()
#target_link_directories(pub_file PRIVATE lib/ deps/libhp/lib/)
#target_link_libraries(pub_file libhp dl paho-mqtt3a)
#if(NOT WIN32)
#target_link_libraries(pub_file libhp dl uv uuid z ssl crypto pthread)
#elseif(LIBHP_WITH_WIN32_INTERROP)
#target_link_libraries(pub_file uv_a Win32_Interop lua jemalloc redis-server)
#else()
#target_link_libraries(pub_file uv_a libhp  hiredis_static)
#endif()

##########################################################################################
#configure_file( "config.h.in" "config.h")
