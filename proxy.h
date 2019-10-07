//
// Created by wangnaizheng on 2018/4/25.
//

#ifndef HTTPPROXY_PROXY_H
#define HTTPPROXY_PROXY_H
#ifndef PROXY_HH
#define PROXY_HH

#include <sys/queue.h>

#define LOG_ERROR		0
#define LOG_WARNING 1
#define LOG_NOTICE 	2
#define LOG_TRACE		3

#define ACTIVE_LEVEL 3
#define LOG(LEVEL, MSG, ...) 					\
  if(LEVEL <= ACTIVE_LEVEL)  {		\
    printf("LOG(%d): ", LEVEL); 	\
    printf(MSG, ##__VA_ARGS__);									\
  }																\

extern int http_methods_len;
extern const char *http_methods[];

char *read_line(int sockfd);

enum http_methods_enum {
    OPTIONS,
    GET,
    HEAD,
    POST,
    PUT,
    DELETE,
    TRACE,
    CONNECT,
    UNKNOWN
};
//版本号
enum http_versions_enum {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
    HTTP_VERSION_INVALID
};
// HTTP请求报文的主结构体
typedef struct http_request
{
    enum http_methods_enum method;
    enum http_versions_enum version;
    const char *search_path;
    //使用unix系统底层的一个queue维护链表
    TAILQ_HEAD(METADATA_HEAD, http_metadata_item) metadata_head;
} http_request;
//每一个HTTP请求的键值对
typedef struct http_metadata_item
{
    const char *key;
    const char *value;

    TAILQ_ENTRY(http_metadata_item) entries;
} http_metadata_item;


#endif

#endif //HTTPPROXY_PROXY_H
