

#ifndef __RyanW5500Socket__
#define __RyanW5500Socket__

#include "RyanW5500Store.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        WIZ_EVENT_SEND = 0,
        WIZ_EVENT_RECV,
        WIZ_EVENT_ERROR,
    } RyanW5500Event_e;

    // 定义枚举类型
    typedef enum
    {
        RyanW5500SocketInit,        // 初始化状态
        RyanW5500SocketEstablished, // 以连接状态，调用connect后
        RyanW5500SocketListen,      // 监听状态
        RyanW5500SocketClose,       // 关闭状态
    } RyanW5500SocketState_e;

    typedef struct
    {
        int backlog;                   // 剩余accept连接个数
        rt_mq_t clientInfoQueueHandle; // 连接的客户端信息地址
        RyanList_t clientList;         // 客户端的列表
    } RyanW5500ServiceInfo;

    typedef struct
    {
        uint8_t type;          // WIZnet 套接字的类型（TCP、UDP 或 RAW）
        uint8_t soOptionsFlag; // so_options标志位
        int8_t serviceSocket;  // 当前套接字是listen套接字客户端时，存储listen服务套接字

        uint16_t rcvevent;  // 接收数据次数
        uint16_t sendevent; // 发送确认数据次数
        uint16_t errevent;  // 套接字发生错误

        uint16_t port;                     // 当前socket端口
        int socket;                        // w5500 真实socket套接字
        uint32_t magic;                    //
        uint32_t recvTimeout;              // 接收数据超时（以毫秒为单位）
        uint32_t sendTimeout;              // 等待发送超时（以毫秒为单位）
        RyanW5500SocketState_e state;      // RyanW5500 套接字的当前状态
        struct sockaddr *remoteAddr;       // 远程地址
        RyanW5500ServiceInfo *serviceInfo; // 服务器套接字信息

        rt_mutex_t recvLock; // 读取数据锁

#ifdef SAL_USING_POSIX
        rt_wqueue_t wait_head;
#endif

    } RyanW5500Socket;

    typedef struct
    {
        RyanW5500Socket *sock;
        RyanList_t list;
    } RyanW5500ClientInfo;

    /* extern variables-----------------------------------------------------------*/
    extern int wiz_socket(int domain, int type, int protocol);
    extern int wiz_closesocket(int socket);
    extern int wiz_shutdown(int socket, int how);
    extern int wiz_listen(int socket, int backlog);
    extern int wiz_bind(int socket, const struct sockaddr *name, socklen_t namelen);
    extern int wiz_connect(int socket, const struct sockaddr *name, socklen_t namelen);
    extern int wiz_accept(int socket, struct sockaddr *addr, socklen_t *addrlen);
    extern int wiz_sendto(int socket, const void *dwiza, size_t size, int flags, const struct sockaddr *to, socklen_t tolen);
    extern int wiz_send(int socket, const void *dwiza, size_t size, int flags);
    extern int wiz_recvfrom(int socket, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
    extern int wiz_recv(int socket, void *mem, size_t len, int flags);
    extern int wiz_getsockopt(int socket, int level, int optname, void *optval, socklen_t *optlen);
    extern int wiz_setsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen);

    extern struct hostent *wiz_gethostbyname(const char *name);
    extern int wiz_gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop);
    extern int wiz_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res);
    extern void wiz_freeaddrinfo(struct addrinfo *ai);

    extern RyanW5500Socket *RyanW5500GetSock(int socket); // 获取 WIZnet 套接字对象
    extern RyanW5500Socket *RyanW5500SocketCreate(int type, int port);
    extern void RyanListenServiceAddClient(RyanW5500Socket *serviceSock, RyanW5500Socket *clientSock);
    extern int RyanW5500RecvDataCallback(int socket);
    extern int RyanW5500CloseCallback(int socket);

    extern void RyanW5500DoEventChanges(RyanW5500Socket *sock, RyanW5500Event_e event, rt_bool_t is_plus);

#ifdef __cplusplus
}
#endif

#endif
