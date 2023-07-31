rmqtt - a Redis-based MQTT broker
=============
rmqtt use Redis Pub/Sub as the driver, support both QOS 0,1,2
currenty tested on Linux and Windows systems

# compile and run

* configure and build

```code
# make build dir: ${rmqtt}/build
$ mkdr build && cd build;
# configure on Linux, build debug version, set CMAKE_BUILD_TYPE=Release to a release verison
$ cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make -j 2
# configure on Windows, set BULID_SHARED_LIBS to OFF, rmqtt.exe will has less dependents 
$ cmake.exe -G"Visual Studio 15 2017 Win64" -DBULID_SHARED_LIBS=0 ..
# now make
$ cmake.exe --build .  -t rmqtt --config="Release"
```

* edit config file ${rmqtt}/conf/rmqtt.conf, set redis.shaXXX=(SHA of Redis Lua scripts), e.g. import Lua scripts to Redis :

```code
$ redis-cli -h 192.168.1.105 -p 6379 -a pass script load "$(cat scripts/sub.lua)"

(Redis outputs)
"93fe15317611aa92adc07b3fc0ef69b50eeb5601"
```

* run it!

```code
    rmqtt -fconf/rmqtt.conf
```

# tests and exmaples
see dir test/

# 实现简要说明

  * 基于Redis Pub/Sub实现
  * IOCP + thread message queue on Windows, epoll + master/slave on Linux
  * 使用zset,hash等数据结构保存会话及发布过的消息,发布即不丢失
  * 离线消息及会话

# 第三方库说明及致谢

${rmqtt}/deps/目录为项目依赖的第三库,感谢原作者们!

* mongoose, https://github.com/cesanta/mongoose.git
* getopt on Windows, https://www.ludvikjerabek.com
