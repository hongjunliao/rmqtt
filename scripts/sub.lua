--[[
this file is part of rmqtt project
@author hongjun.liao <docici@126.com>, @date 2020/7/7

see libhp/hp_pub, Redis Pub/Sub system:
  return "off-line" messages

import to Redis: 
redis-cli -h 192.168.1.105 -p 6379 -a pass script load "$(cat scripts/file.lua)"
  
usage: 
eval SHA1 0 session
e.g.
evalsha 456be53be0c61603929c31c2895af5340e3b2d30 0 rmqtt_test/s/000052044881234

--]]
local rst={};
local k = nil;
local j = 1;
local s=redis.call('hgetall', ARGV[1]);
for i,v in pairs(s) do
  if((i % 2) == 0) then
    local c = redis.pcall('ZRANGEBYSCORE', k, v + 1, "+inf", "WITHSCORES");
    if not c.err then
      rst[j]=k;
      rst[j+1]=c;
      j = j + 2;
    end
  else k = v;
  end
end
return rst

