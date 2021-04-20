rmqtt - a Redis-based MQTT broker
=============
rmqtt use Redis Pub/Sub as the driver, support both QOS 0,1,2
currenty tested on Linux and Windows systems

# compile and run
* 1.cd to rmqtt root dir
* 2.unzip depencies: deps-win.zip
* 3.confiure deps/libhp, comment all LIBHP_WITH_XXX macros(just for simplify configuration) except:
```code
/* for rmqtt Redis is required */
#define LIBHP_WITH_REDIS
```
see libhp project for more details
* 4.configure and build other depencies if needed
* 5.mkdir build && cd build
```code
# build debug version, set CMAKE_BUILD_TYPE=Release to a release verison
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=1 .. && make -j 2
```
* 6.edit rmqtt config file rmqtt.conf, set redis.shaXXX=(SHA of Redis Lua scripts), e.g. import Lua scripts to Redis :
```code
$ redis-cli -h 1921.68.1.105 -p 6379 -a pass script load "$(cat scripts/sub.lua)"

(Redis outputs)
"93fe15317611aa92adc07b3fc0ef69b50eeb5601"
```
* 8.run it!
```code
    rmqtt [rmqtt.conf]
```
# tests and exmaples
see dir test/

# 实现简要说明

* hp_pu/sub, Redis Pub/Sub增强
  * 基于Redis Pub/Sub实现
  * 使用zset,hash数据结构等保存会话及发布过的消息,发布即不丢失
  * 消息需要确认,以支持离线消息及会话

# 第三方库说明及致谢

deps/目录为项目依赖的第三库,感谢原作者们! 为节约时间, 已将所有依赖打包

* mongoose, https://github.com/cesanta/mongoose.git
