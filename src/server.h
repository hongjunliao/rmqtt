
 /* This file is PART of rmqtt project
 * @author hongjun.liao <docici@126.com>, @date 2020/3/23
 *
 * */

#ifndef RMQTT_SREVER_H
#define RMQTT_SREVER_H

/* Keys hashing / comparison functions for dict.c hash tables. */
uint64_t dictSdsHash(const void *key);
int dictSdsKeyCompare(void *privdata, const void *key1, const void *key2);
void dictSdsDestructor(void *privdata, void *val);

#endif //RMQTT_SREVER_H
