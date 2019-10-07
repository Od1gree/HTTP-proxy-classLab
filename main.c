#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <ctype.h>

#include "proxy.h"
#include "net.h"
#include "list.h"
#include "http_message.h"

#define LENGTH_OF_DATE 25
#define LENGTH_OF_FILENAME 19

//将收到的HTTP报文按行读取,以便分析
char *read_line(int sockfd)
{
    int buffer_size = 2;
    char *line = (char*)malloc(sizeof(char)*buffer_size+1);
    char c;
    int length = 0;
    int counter = 0;

    while(1)
    {
        //接收,读到回车就返回整行
        length = recv(sockfd, &c, 1, 0);
        line[counter++] = c;
        if(c == '\n')
        {
            line[counter] = '\0';
            return line;
        }

        // 如果超过缓冲区大小则增加内存量
        if(counter == buffer_size)
        {
            buffer_size *= 2;

            line = (char*)realloc(line, sizeof(char)*buffer_size);
        }

    }
}

// 用于敏感词汇检测,
// str[]是待检测字符串
// 返回是否包含敏感词汇
int containing_forbidden_words(char str[]){

    // Forbidden words
    LOG(LOG_TRACE,"enter check containing words\n");
    char *words[] = {"Mike",  "cms.hit.edu.cn"};
    int hits[] = {0, 0}; // Every forbidden word need to have a zero in this array to be able to count number of char hits.
    int numb_words = 2; // Number of forbidden words

    int str_length = strlen(str);
    int c, w;   // Index for char in str, and index for word in words

    // 由于敏感词汇不长,因此没有在此处使用优化匹配算法
    for (c = 0; c < str_length; c++)
    {
        for (w = 0; w < numb_words; w++)
        {
            if (tolower(words[w][ hits[w] ]) == tolower(str[c])){
                if(++hits[w] == strlen(words[w])) {
                    LOG(LOG_TRACE,"it containing blocked words\n");
                    return 1;
                }
            }
            else if (hits[w] != 0)
                hits[w--] = 0;
        }
    }
    LOG(LOG_TRACE,"it not containing blocked words\n");
    return 0;
}

// 将接收并操作的HTTP报文送给客户端
// data为要传输的数据
// packages_size 为每次调用send函数所传输的大小
// length为数据总长度
int send_to_client(int client_sockfd, char data[], int packages_size, ssize_t length)
{
    // if packages_size is set to 0, then the function will try to send all data as one package.
    if(packages_size < 1)
    {
        if(send(client_sockfd, data, length, 0) == -1)
        {
            perror("Couldn't send data to the client.");
            return -1;
        }
    }
    else
    {
        int p;
        for(p = 0; p*packages_size + packages_size < length; p++){
            //socket发送给客户端
            if(send(client_sockfd, (data + p*packages_size), packages_size, 0) == -1)
            {
                perror("Couldn't send any or just some data to the client. (loop)\n");
                return -1;
            }
        }

        if (p*packages_size < length)
        {
            if(send(client_sockfd, (data + p*packages_size), length - p*packages_size, 0) == -1)
            {
                perror("Couldn't send any or just some data to the client.\n");
                return -1;
            }
        }
    }

    return 0;
}

// 对传入字符串进行哈希运算, 返回哈希值
unsigned int APHash(char *str)
{
    unsigned int hash = 0;
    int i;

    for (i=0; *str; i++)
    {
        if ((i & 1) == 0)
        {
            hash ^= ((hash << 7) ^ (*str++) ^ (hash >> 3));
        }
        else
        {
            hash ^= (~((hash << 11) ^ (*str++) ^ (hash >> 5)));
        }
    }

    return (hash & 0x7FFFFFFF);
}

//获取cache文件的大小
//注意: 如果文件不存在则可能返回任意非0数
int file_size(char* filename)
{
    struct stat statbuf;
    stat(filename,&statbuf);
    int size=statbuf.st_size;

    return size;
}
// 获得以hash为文件名的cache的时间
char * get_cache_date(int hash){
    //通过文件夹路径和哈希值构造文件路径
    char *path = "/Users/abc123one/CLionProjects/httpproxy/cache/";
    int path_len = strlen(path) + LENGTH_OF_FILENAME + 1;
    char  real_path_txt[path_len];
    char  real_path_date[path_len];
    char buffer[LENGTH_OF_FILENAME];
    sprintf(buffer,"%d",hash);
    strcpy(real_path_txt,path);
    strcat(real_path_txt,buffer);
    strcpy(real_path_date,real_path_txt);
    strcat(real_path_txt,".txt");
    strcat(real_path_date,".date");
    //打开相应文件,首先检测文件是否存在
    FILE *fp_date = NULL;
    fp_date = fopen(real_path_date,"a+");
    fseek(fp_date, 0, SEEK_SET);
    int date_length = file_size(real_path_date);
    if(date_length==0) {
        LOG(LOG_TRACE,"date is NULL\n");
        return NULL;
    }
    LOG(LOG_TRACE,"date is not zero, is %d\n",date_length);
    //文件存在,读取日期
    char *date = malloc(sizeof(char)*(date_length+2));
    if(fgets(date,date_length+1,fp_date)!=NULL)
        LOG(LOG_ERROR,"fgets not null\n");
    fclose(fp_date);
    LOG(LOG_TRACE,"returned date\n");
    return date;
}

//向远程服务器发送HTTP请求
//req为请求结构体的内容
unsigned int http_request_send(int sockfd, http_request *req)
{
    LOG(LOG_TRACE, "Requesting: %s\n", req->search_path);
    unsigned int hash = APHash(req->search_path);
    char * date = get_cache_date(hash);
    // 通过结构体构造请求字符串
    char *request_buffer = http_build_request(req,date);

    // send the http request to the web server
    // 如果返回值不为0,则请求错误
    if(send(sockfd, request_buffer, strlen(request_buffer), 0) == -1)
    {
        free(request_buffer);
        perror("send");
        return 0;
    }
    free(request_buffer);

    LOG(LOG_TRACE, "Sent HTTP header to web server\n");

    return hash;
}

// 接收客户端发来的数据
void handle_client(int client_sockfd)
{
    char *line;
    int server_sockfd;
    http_request *req;
    //先读取报文头部信息
    req = http_read_header(client_sockfd);

    if(req == NULL)
    {
        LOG(LOG_ERROR, "Failed to parse the header\n");
        return;
    }
    //判断是否含有敏感词汇
    //如果有敏感词汇则返回一个简易网页提示不能访问
    if (containing_forbidden_words((char*)req->search_path) || containing_forbidden_words((char*)list_get_key(&req->metadata_head, "Host"))){
        char *error1 = "HTTP/1.1 200 OK\r\nServer: Net Ninny\r\nContent-Type: text/html\r\n\r\n<html>\n\n<title>\nNet Ninny Error Page 1 for CPSC 441 Assignment 1\n</title>\n\n<body>\n<p>\nSorry, but the Web page that you were trying to access\nis inappropriate for you, based on the URL.\nThe page has been blocked to avoid insulting your intelligence.\n</p>\n\n<p>\nNet Ninny\n</p>\n\n</body>\n\n</html>\n";
        http_request_destroy(req);
        send_to_client(client_sockfd, error1, 0, strlen(error1));
        return;
    }
    LOG(LOG_TRACE,"begin to connect to req\n");
    //与目标主机建立连接
    server_sockfd = http_connect(req);
    if(server_sockfd == -1)
    {
        LOG(LOG_ERROR, "Failed to connect to host\n");
        http_request_destroy(req);
        return;
    }

    LOG(LOG_TRACE, "Connected to host\n");
    //发送http请求
    unsigned int hash = http_request_send(server_sockfd, req);
    LOG(LOG_TRACE,"hash: %d\n",hash);
    //发送请求后及时释放内存
    http_request_destroy(req);

    LOG(LOG_TRACE, "Beginning to retrieve the response header\n");
    int is_bad_encoding = 0;
    int is_text_content = 0;
    int line_length;
    // status=200, flag=1
    // ststus=304, flag=0
    // status=others, flag=-1
    int is_modified_flag = 1;
    //为读取cache做准备
    char *path = "/Users/abc123one/CLionProjects/httpproxy/cache/";
    int path_len = strlen(path) + LENGTH_OF_FILENAME + 1;
    char real_path_txt[path_len];
    char real_path_date[path_len];
    char buffer[LENGTH_OF_FILENAME];
    sprintf(buffer,"%d",hash);
    strcpy(real_path_txt,path);
    strcat(real_path_txt,buffer);
    strcpy(real_path_date,real_path_txt);
    strcat(real_path_txt,".txt");
    strcat(real_path_date,".date");
    char new_time[LENGTH_OF_DATE];
    //读取每一行HTTP HEADER作为一次循环,对每一行进行判断并操作
    while(1)
    {
        line = read_line(server_sockfd);
        line_length = strlen(line);
        printf("it is the HTTP response header:\n %s",line);
        //check the HTTP status code
        if(strncmp(line,"HTTP",4) == 0){
            int i = 9;
            if(line[i]=='2')
                is_modified_flag = 1;
            else if(line[i]=='3' && line[i+1]=='0' && line[i+2]=='4')
                is_modified_flag = 0;
            else
                is_modified_flag = -1;
        }
        //读取时间并保存下来
        if(strncmp(line,"Date",4) == 0){
            int i = 6, j = 0;
            while (line[i] != '\r'){
                new_time[j] = line[i];
                i++;
                j++;
            }
            LOG(LOG_TRACE, "saved date: %s",new_time);
            FILE *fp_date = NULL;
            fp_date = fopen(real_path_date,"w+");
            fprintf(fp_date,"%s",new_time);
            fclose(fp_date);
        }

        send_to_client(client_sockfd, line, 0, line_length);
        //读到开始为\r\n时表示header结束
        if(line[0] == '\r' && line[1] == '\n')
        {
            // We received the end of the HTTP header
            LOG(LOG_TRACE, "Received the end of the HTTP response header\n");
            free(line);
            break;
        }
        else if(18 <= line_length)
        {
            line[18] = '\0'; // Destroys the data in the line, but is needed to check if in coming data will be text format.
            if (strcmp(line, "Content-Type: text") == 0)
                is_text_content = 1;
            else if (strcmp(line, "Content-Encoding: ") == 0)
                is_bad_encoding = 1;
        }

        free(line);
    }
    //针对返回的HTTP状态码进行操作
    int chunk_length;
    char *temp = NULL;
    //状态码为304,则从本地读取cache作为HTTP报文的content
    if(is_modified_flag == 0){
        LOG(LOG_TRACE, "HTTP status 304, getting cache from file system\n");
        chunk_length = file_size(real_path_txt);
        FILE *fp_txt = NULL;
        fp_txt = fopen(real_path_txt,"r");
        char content[chunk_length+1];
        fgets(content,chunk_length+1,fp_txt);
        temp = content;
        fclose(fp_txt);
    }
    //状态码为200,正常转发回客户端,并把时间和content保存在本地作为cache
    else {
        LOG(LOG_TRACE, "HTTP status 200, beginning to retrieve content\n");
        temp = http_read_chunk(server_sockfd, &chunk_length);
        FILE *fp_txt = NULL;
        fp_txt = fopen(real_path_txt,"w+");
        fputs(temp,fp_txt);
        fclose(fp_txt);
    }
    LOG(LOG_TRACE, "Received the content, %d bytes\n", chunk_length);
    //printf("%s",temp);
    //再次检查返回的内容是否有屏蔽的内容
    if (is_text_content && !is_bad_encoding && containing_forbidden_words(temp))
    {
        LOG(LOG_TRACE, "Received data contains forbidden words!\n");
        char *error2 = "<html>\n<title>\nNet Ninny Error Page 3 for CPSC 441 Assignment 1\n</title>\n\n<body>\n<p>\nSorry, but the Web page that you were trying to access\nis inappropriate for you, based on some of the words it contains.\nThe page has been blocked to avoid insulting your intelligence.\n</p>\n\n<p>\nby the proxy\n</p>\n\n</body>\n\n</html>\n";

        send_to_client(client_sockfd, error2, 0, strlen(error2));
    }
    else
        send_to_client(client_sockfd, temp, 0, chunk_length);
    free(temp);
    free(real_path_date);
    free(real_path_txt);
    close(server_sockfd);
}
//启动服务
void start_server(char *port)
{
    printf("Starting server\n");

    int sockfd, new_fd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr;
    socklen_t sin_size;
    int rv;
    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM; //流
    hints.ai_flags = AI_PASSIVE; //被动
    //是否成功返回addrinfo
    if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }


    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        //获得套接字
        if((sockfd = socket(p->ai_family, p->ai_socktype,
                            p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }
        //设置属性,可在重启后再次使用此套接字端口和IP
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                      sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }
        //socket绑定地址
        if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if(p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        return;
    }

    freeaddrinfo(servinfo);
    //监听
    if(listen(sockfd, 10) == -1)
    {
        perror("listen");
        exit(1);
    }

    printf("server: waiting for connections..\n");
    while(1)
    {
        //等待客户端访问
        sin_size = sizeof(their_addr);
        new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size);
        printf("loop\n");
        if(new_fd == -1)
        {
            perror("accept");
            continue;
        }

        printf("Receieved connection\n");

        signal(SIGCHLD, SIG_IGN);
        pid_t child_pid = fork();
        if(!child_pid)
        {
            handle_client(new_fd);

            close(new_fd);
            exit(0);
        }
        close(new_fd);
    }
}

//support command line input
int main(int argc, char *argv[])
{
    char *port = "8080";
    if (argc > 1)
        port = argv[1];
    start_server(port);
    return 0;
}

