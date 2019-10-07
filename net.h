//
// Created by wangnaizheng on 2018/4/25.
//

#ifndef HTTPPROXY_NET_H
#define HTTPPROXY_NET_H
int http_connect(http_request *req);
http_request *http_read_header(int sockfd);
char *http_read_chunk(int sockfd, ssize_t *length);

#endif //HTTPPROXY_NET_H
