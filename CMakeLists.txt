# author hongjun.liao <docici@126.com>
# date 2019/10/30

cmake_minimum_required(VERSION 2.8.12)

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()
# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

##########################################################################################
# rmqtt 
project(rmqtt)

set(LIBHP_WITH_MQTT 1 CACHE INTERNAL "LIBHP_WITH_MQTT")
set(LIBHP_WITH_REDIS 1 CACHE INTERNAL "LIBHP_WITH_REDIS")
set(LIBHP_WITH_CJSON 1 CACHE INTERNAL "LIBHP_WITH_CJSON")
add_subdirectory(deps/libhp)

# sources
file(GLOB SRCS
    deps/inih/*.c
	deps/mongoose/mongoose.c
)
file(GLOB SRCS ${SRCS} src/*.c)

if(MSVC)
	file(GLOB SRCS ${SRCS} src/*.c deps/libc/*.c)
else()
#	find_package(PkgConfig REQUIRED)
#	pkg_check_modules(libiberty REQUIRED libiberty)
endif()

add_executable(rmqtt ${SRCS} )
target_compile_options(rmqtt PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
)
if(NOT WIN32)
	target_compile_options(rmqtt PUBLIC  -O0 -g3  -Wall)
else()
	target_compile_options(rmqtt PUBLIC -D_CRT_SECURE_NO_WARNINGS  /MTd)
endif()

target_include_directories(rmqtt PRIVATE "${PROJECT_BINARY_DIR}" include/ src/ deps/libhp/include/ 
	deps/libhp/ deps/libhp/deps/ deps/)
if(MSVC)
target_include_directories(rmqtt PRIVATE deps/libhp/include/win/)
endif()
if(LIBHP_WITH_WIN32_INTERROP)
target_include_directories(rmqtt PRIVATE deps/libhp/deps/redis/deps/)
endif()

#add_custom_command(TARGET rmqtt POST_BUILD  COMMAND ${CMAKE_COMMAND} -E echo done _________________________ VERBATIM)
#add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/src/gen/git_commit_id.h COMMAND ../scripts/prebuild.sh COMMENT "prebuilding ...")

#if(WIN32)
#target_link_options(rmqtt PRIVATE /NODEFAULTLIB:libc /NODEFAULTLIB:libcd.lib /NODEFAULTLIB:libcmt.lib 
#	/NODEFAULTLIB:msvcrtd.lib /NODEFAULTLIB:msvcrtd.lib)
#endif()

target_link_directories(rmqtt PRIVATE lib/ deps/libhp/lib/)
target_link_libraries(rmqtt libhp dl paho-mqtt3a)
if(NOT WIN32)
	target_link_libraries(rmqtt uv uuid  z ssl crypto pthread)
elseif(LIBHP_WITH_WIN32_INTERROP)
	target_link_libraries(rmqtt Win32_Interop lua jemalloc redis-server uv_a)
else()
	target_link_libraries(rmqtt uv_a libhp  hiredis_static)
endif()

if(NOT MSVC)
#	target_link_libraries(rmqtt libiberty)
endif()
##########################################################################################
# test: pub_file
project(pub_file)
# sources
file(GLOB SRCS test/pub_file/*.c test/README.md)
add_executable(pub_file ${SRCS} )
target_compile_options(pub_file PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
)
if(NOT WIN32)
	target_compile_options(pub_file PUBLIC  -O0 -g3  -Wall)
else()
	target_compile_options(pub_file PUBLIC -D_CRT_SECURE_NO_WARNINGS  /MTd)
endif()

target_include_directories(pub_file PRIVATE  "${PROJECT_BINARY_DIR}" include/ src/ deps/libhp/include/ deps/libhp/ deps/libhp/deps/ deps/ 
	deps/libhp/deps/redis/deps/)
if(MSVC)
target_include_directories(pub_file PRIVATE deps/libhp/include/win/)
endif()
target_link_directories(pub_file PRIVATE lib/ deps/libhp/lib/)
target_link_libraries(pub_file libhp dl paho-mqtt3a)
if(NOT WIN32)
target_link_libraries(pub_file libhp dl uv uuid z ssl crypto pthread)
elseif(LIBHP_WITH_WIN32_INTERROP)
target_link_libraries(pub_file uv_a Win32_Interop lua jemalloc redis-server)
else()
target_link_libraries(pub_file uv_a libhp  hiredis_static)
endif()

##########################################################################################
configure_file( "config.h.in" "config.h")
