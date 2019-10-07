//
// Created by wangnaizheng on 2018/4/25.
//

#ifndef HTTPPROXY_LIST_H
#define HTTPPROXY_LIST_H
#include "proxy.h"

const char *list_get_key(struct METADATA_HEAD *list, const char *key);
void list_edit_key(struct METADATA_HEAD *list, const char *key, char *value);
#endif //HTTPPROXY_LIST_H
