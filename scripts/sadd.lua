--[[
this file is part of rmqtt project
@author hongjun.liao <docici@126.com>, @date 2020/10/21

see libhp/hp_pub, Redis Pub/Sub system:
  update session: add key(s, topics) to session

import to Redis: 
redis-cli -h 192.168.1.105 -p 6379 -a pass script load "$(cat scripts/file.lua)"

usage: 
eval SHA1 n_topics topics session
e.g.
evalsha 02c65a4e190d212bc624624870f42337481e670d 2 rmqtt_test:group:usb rmqtt_test:group::wifi rmqtt_test:s:000052044881234

--]]

local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(KEYS) do
   -- set score to lastest
   local score = 0;
   local c = redis.call('zrevrange', v, 0, 0, 'withscores');
   if c[2] then
     score = c[2];
   end

   -- insert new using hsetnx
   redis.call('hsetnx', ARGV[1], v, score);
end
return 1;


