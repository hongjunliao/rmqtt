--[[
this file is part of rmqtt project
@author hongjun.liao <docici@126.com>, @date 2020/7/7

see libhp/hp_pub, Redis Pub/Sub system:
  update session

import to Redis: 
redis-cli -h 192.168.1.105 -p 6379 -a pass script load "$(cat scripts/file.lua)"

usage: 
eval SHA1 n_topics topics session
e.g.
evalsha 7dfb24710befe978f25be0846ae1b50bb772d799 3 rmqtt_test:1:000052044881234 rmqtt_test:apps:chat.tox.antox rmqtt_test:dept:1006 rmqtt_test:s:000052044881234

--]]

local k = nil;
local keys={};
local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(KEYS) do
   keys[v] = true;

   -- set score to lastest
   local score = 0;
   local c = redis.call('zrevrange', v, 0, 0, 'withscores');
   if c[2] then
     score = c[2];
   end

   -- insert new using hsetnx
   redis.call('hsetnx', ARGV[1], v, score);
end
for i,v in pairs(s) do
   if((i % 2) == 0) then
     if not keys[k] then
       -- remove old ones
       redis.call('hdel', ARGV[1], k);
     end
  else k = v;
  end
end
return 1;


