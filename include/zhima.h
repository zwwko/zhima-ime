#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 不透明会话句柄；所有操作由调用线程串行执行。 */
typedef struct Handle ZfHandle;
/* 从 UTF-8 词典文件创建会话，失败返回 NULL。 */
ZfHandle *zf_create(const char *wubi,const char *pinyin);
/* 释放会话；不得重复释放。 */
void zf_destroy(ZfHandle *handle);
/* 无副作用地判断是否消费按键。 */
bool zf_test_key(const ZfHandle *handle,const char *key);
/* 返回 JSON 结果，使用 zf_string_free 释放。 */
char *zf_process(ZfHandle *handle,const char *key);
/* 仅在平台确认提交成功后学习。 */
void zf_ack_commit(ZfHandle *handle,uint64_t id,bool success);
/* 释放结果字符串。 */
void zf_string_free(char *value);
#ifdef __cplusplus
}
#endif
