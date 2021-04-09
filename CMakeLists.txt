# author hongjun.liao <docici@126.com>
# date 2019/10/30

cmake_minimum_required(VERSION 2.8)
CMAKE_POLICY(SET CMP0015 NEW)

if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "Options are: None, Debug, Release, RelWithDebInfo, MinSizeRel." FORCE)
endif()

# -DCMAKE_EXPORT_COMPILE_COMMANDS is for lsp/clangd, see
# https://clangd.llvm.org/installation.html
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# show config
message("CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} Options are: Debug|Release")

# rmqtt
project(rmqtt)

# deps
file(GLOB SRCS0
	deps/mongoose/mongoose.c
    deps/inih/*.c
)
# sources
file(GLOB SRCS0 ${SRCS0} src/* src/gen/git_commit_id.h)
add_subdirectory(deps/libhp)
add_executable(rmqtt ${SRCS0} )
target_compile_options(rmqtt PUBLIC
    -DHAVE_CONFIG_H -D_GNU_SOURCE -O0 -g3 -Wall -DCMAKE_EXPORT_COMPILE_COMMANDS
    # for mongoose
    -DMG_ENABLE_LINES -DMG_ENABLE_DIRECTORY_LISTING
    # for redis
    -DDICT_BENCHMARK_MAIN
    # for cJSON
    -DcJSON_FREE_PRINT
	)
target_include_directories(rmqtt PRIVATE include/ src/ deps/libhp/include/ deps/libhp/ deps/libhp/deps/ deps/ ./)

#add_custom_command(TARGET rmqtt POST_BUILD  COMMAND ${CMAKE_COMMAND} -E echo done _________________________ VERBATIM)
#add_custom_command(OUTPUT ${PROJECT_SOURCE_DIR}/src/gen/git_commit_id.h COMMAND ../scripts/prebuild.sh COMMENT "prebuilding ...")
# hiredis
#add_subdirectory(deps/hiredis)

target_link_directories(rmqtt PRIVATE lib/)
target_link_libraries(rmqtt libhp dl m pthread uv zlog uuid curl hiredis z ssl crypto)

