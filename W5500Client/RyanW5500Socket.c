#define DBG_ENABLE

#define DBG_SECTION_NAME ("RyanW5500Socket")
#define DBG_LEVEL LOG_LVL_INFO
#define DBG_COLOR

#include "RyanW5500Store.h"

#define RyanW5500LinkCheck(value)               \
    do                                          \
    {                                           \
        uint8_t linkState = 0;                  \
        ctlwizchip(CW_GET_PHYLINK, &linkState); \
        if (PHY_LINK_ON != linkState)           \
            return value;                       \
    } while (0);

// 可用套接字的全局数组
static RyanW5500Socket RyanW5500Sockets[RyanW5500MaxSocketNum] = {0};
static uint16_t wiz_port = 15500; // 用户可以自定义

/**
 * @brief 中断接收到数据回调函数
 *
 * @param socket
 * @return int
 */
int RyanW5500RecvDataCallback(int socket)
{
    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    rt_event_send(RyanW5500Entry.W5500EventHandle, 1 << sock->socket);
    return 0;
}

/**
 * @brief 中断接收到close信号回调函数
 *
 * @param socket
 * @return int
 */
int RyanW5500CloseCallback(int socket)
{
    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    sock->state = RyanW5500SocketClose;
    rt_event_send(RyanW5500Entry.W5500EventHandle, 1 << sock->socket);
    return 0;
}

/**
 * @brief
 *
 * @param s_addr
 * @param ipStrArr
 */
void inAddrToipStrArr(in_addr_t *s_addr, uint8_t *ipStrArr)
{
    assert(NULL != s_addr);
    assert(NULL != ipStrArr);

    // inet_pton(AF_INET, inet_ntoa(sin->sin_addr), &ipStrArr); // 效率有点低
    uint8_t *p = (uint8_t *)s_addr;
    ipStrArr[0] = *p;
    ipStrArr[1] = *(p + 1);
    ipStrArr[2] = *(p + 2);
    ipStrArr[3] = *(p + 3);
}

/**
 * @brief
 *
 * @param ipStrArr
 * @return in_addr_t
 */
in_addr_t ipStrArrToinAddr(uint8_t *ipStrArr)
{
    assert(NULL != ipStrArr);

    // 效率有点低
    // char remote_ipaddr[16] = {0};
    // snprintf(remote_ipaddr, sizeof(remote_ipaddr), "%d.%d.%d.%d", ipStrArr[0], ipStrArr[1], ipStrArr[2], ipStrArr[3]);
    // return inet_addr((const char *)remote_ipaddr);

    in_addr_t p2 = 0;
    uint8_t *p = (uint8_t *)&p2;
    *p = ipStrArr[0];
    *(p + 1) = ipStrArr[1];
    *(p + 2) = ipStrArr[2];
    *(p + 3) = ipStrArr[3];
    return p2;
}

/**
 * @brief 根据socket获取RyanW5500Socket结构体
 *
 * @param socket
 * @return RyanW5500Socket*
 */
RyanW5500Socket *RyanW5500GetSock(int socket)
{
    if (socket < 0 || socket >= RyanW5500MaxSocketNum)
        return NULL;

    // 检查套接字结构是否有效
    if (RyanW5500Sockets[socket].magic != WIZ_SOCKET_MAGIC)
        return NULL;

    return &RyanW5500Sockets[socket];
}

/**
 * @brief 给listen服务器套接字创建client客户端
 *
 * @param serviceSock
 * @return RyanW5500Socket*
 */
RyanW5500Socket *RyanW5500CreateListenClient(RyanW5500Socket *serviceSock)
{
    assert(NULL != serviceSock);

    RyanW5500Socket *clientSock = NULL;
    RyanW5500ClientInfo *clientInfo = NULL;
    clientSock = RyanW5500SocketCreate(serviceSock->type, serviceSock->port);
    RyanW5500CheckCode(NULL != clientSock, EMFILE, { goto err; });

    clientSock->serviceSocket = serviceSock->socket;

    // 创建客户端信息并将客户端添加到服务器clientList
    clientInfo = (RyanW5500ClientInfo *)malloc(sizeof(RyanW5500ClientInfo));
    RyanW5500CheckCode(NULL != clientInfo, ENOMEM, { goto err; });
    memset(clientInfo, 0, sizeof(RyanW5500ClientInfo));

    clientInfo->sock = clientSock;
    RyanListAddTail(&clientInfo->list, &serviceSock->serviceInfo->clientList);

    RyanW5500CheckCode(SOCK_OK == wizchip_listen(clientSock->socket), EPROTO, {wiz_closesocket(clientSock->socket); goto err; });
    clientSock->state = RyanW5500SocketListen;

    return clientSock;

err:
    if (NULL != clientInfo)
        wiz_closesocket(clientSock->socket);

    if (NULL != clientInfo)
        free(clientInfo);
    return NULL;
}

static void RyanW5500ListenSocketDestory(RyanW5500Socket *sock)
{

    RyanW5500Socket *serviceSock = RyanW5500GetSock(sock->serviceSocket);
    assert(NULL != serviceSock);
    assert(NULL != serviceSock->serviceInfo && sock->port == serviceSock->port);

    // 分配并初始化新的客户端套接字
    RyanList_t *curr = NULL,
               *next = NULL;
    RyanW5500ClientInfo *clientInfo = NULL;

    RyanListForEachSafe(curr, next, &serviceSock->serviceInfo->clientList)
    {
        // 获取此节点的结构体
        clientInfo = RyanListEntry(curr, RyanW5500ClientInfo, list);
        assert(NULL != clientInfo);
        if (clientInfo->sock->socket != sock->socket)
            continue;

        RyanListDel(&clientInfo->list);

        // 增加listen客户端数
        if (0 == serviceSock->serviceInfo->backlog)
        {
            RyanW5500Socket *sock = RyanW5500CreateListenClient(serviceSock);
            if (NULL == sock)
            {
                int8_t socket = -1;
                rt_mq_send(serviceSock->serviceInfo->clientInfoQueueHandle, &socket, sizeof(int8_t));
            }
        }

        // 添加服务器套接字监听数
        serviceSock->serviceInfo->backlog++;
        free(clientInfo);
        break;
    }
}

static int RyanW5500SocketDestory(RyanW5500Socket *sock)
{

    assert(NULL != sock);

    rt_mutex_take(RyanW5500Entry.socketMutexHandle, RT_WAITING_FOREVER); // 获取互斥锁
    if (sock->remoteAddr)
        free(sock->remoteAddr);

    if (-1 != sock->serviceSocket) // 根据记录的serviceSocket判断是否为listen客户端
        RyanW5500ListenSocketDestory(sock);

    // listen服务器套接字，释放服务器信息并关闭所有客户端套接字clientList
    else if (NULL != sock->serviceInfo)
    {
        RyanW5500ClientInfo *clientInfo = NULL;

        // 分配并初始化新的客户端套接字
        RyanList_t *curr = NULL,
                   *next = NULL;

        RyanListForEachSafe(curr, next, &sock->serviceInfo->clientList)
        {
            // 获取此节点的结构体
            clientInfo = RyanListEntry(curr, RyanW5500ClientInfo, list);
            if (NULL == clientInfo)
                continue;

            RyanListDel(&clientInfo->list);
            wiz_closesocket(clientInfo->sock->socket);
            free(clientInfo);
        }

        if (sock->serviceInfo->clientInfoQueueHandle)
            rt_mq_delete(sock->serviceInfo->clientInfoQueueHandle);

        if (sock->serviceInfo)
            free(sock->serviceInfo);
    }

    setSn_IR(sock->socket, 0xff); // 清空套接字中断
    setSn_IMR(sock->socket, 0);   // 设置套接字ISR状态支持
    memset(sock, 0, sizeof(RyanW5500Socket));
    rt_mutex_release(RyanW5500Entry.socketMutexHandle); // 释放互斥锁
    return 0;
}

/**
 * @brief 创建socket
 *
 * @param type
 * @param port
 * @return RyanW5500Socket*
 */
RyanW5500Socket *RyanW5500SocketCreate(int type, int port)
{

    RyanW5500Socket *sock = NULL;
    uint8_t idx = 0;

    rt_mutex_take(RyanW5500Entry.socketMutexHandle, RT_WAITING_FOREVER); // 获取互斥锁
    // 找到一个空的 WIZnet 套接字条目
    for (idx = 0; idx < RyanW5500MaxSocketNum && RyanW5500Sockets[idx].magic; idx++)
        ;

    // 没有空闲socket
    RyanW5500CheckCode(RyanW5500MaxSocketNum != idx, EMFILE, { goto err; });

    sock = &(RyanW5500Sockets[idx]);
    sock->magic = WIZ_SOCKET_MAGIC;
    sock->socket = idx;
    sock->type = type;
    sock->port = port;
    sock->state = RyanW5500SocketInit;
    sock->remoteAddr = NULL;
    sock->recvTimeout = 0;
    sock->sendTimeout = 0;
    sock->serviceInfo = NULL; // 用于服务器套接字
    sock->serviceSocket = -1;

    rt_mutex_release(RyanW5500Entry.socketMutexHandle); // 释放互斥锁

    setSn_IR(sock->socket, 0xff);              // 清空套接字中断
    setSn_IMR(sock->socket, RyanW5500SnIMR);   // 设置套接字ISR状态支持
    wiz_recv_ignore(sock->socket, UINT16_MAX); // 清空之前的接收缓存

    int8_t result = wizchip_socket(sock->socket, sock->type, sock->port, 0); // wizchip_socket内部会先close一次
    if (result != sock->socket)
    {
        LOG_E("RyanW5500 socket(%d) create failed!  result: %d", sock->socket, result);
        goto err;
    }

    return sock;

err:
    rt_mutex_release(RyanW5500Entry.socketMutexHandle); // 释放互斥锁
    wiz_closesocket(sock->socket);
    return NULL;
}

/**
 * @brief 新的listen客户端连接
 *
 * @param serviceSock
 * @param clientSock
 */
void RyanListenServiceAddClient(RyanW5500Socket *serviceSock, RyanW5500Socket *clientSock)
{

    assert(NULL != serviceSock);
    assert(NULL != clientSock);
    assert(serviceSock->serviceInfo->backlog > 0);

    int8_t socket = -1;

    serviceSock->serviceInfo->backlog--;
    clientSock->state = RyanW5500SocketEstablished;

    socket = clientSock->socket;
    rt_mq_send(serviceSock->serviceInfo->clientInfoQueueHandle, &socket, sizeof(int8_t));

    if (serviceSock->serviceInfo->backlog > 0)
    {
        RyanW5500Socket *sock = RyanW5500CreateListenClient(serviceSock);
        if (NULL == sock)
        {
            socket = -1;
            rt_mq_send(serviceSock->serviceInfo->clientInfoQueueHandle, &socket, sizeof(int8_t));
        }
    }
}

/**
 * @brief 创建套接字
 *
 * @param domain 协议族类型
 * @param type 协议类型
 * @param protocol 实际使用的运输层协议
 * @return int 返回一个代表套接字描述符的整数
 */
int wiz_socket(int domain, int type, int protocol)
{
    RyanW5500LinkCheck(-1);

    RyanW5500Socket *sock = NULL;

    // 该实现不支持指定的协议族类型。
    RyanW5500CheckCode(AF_INET == domain || AF_WIZ == domain, EAFNOSUPPORT, { return -1; });
    // 不支持特定的运输层协议
    // RyanW5500CheckCode(0 == protocol, EPROTONOSUPPORT, { return -1; });

    switch (type)
    {
    case SOCK_STREAM:
    case SOCK_DGRAM:
    case SOCK_RAW:
        break;

    default:
        // 协议不支持套接字类型
        RyanW5500CheckCode(NULL, EPROTOTYPE, { return -1; });
    }

    // 分配并初始化一个新的 WIZnet 套接字
    sock = RyanW5500SocketCreate(type, wiz_port);
    RyanW5500CheckCode(NULL != sock, EMFILE, { return -1; });

    sock->state = RyanW5500SocketInit;
    wiz_port++;

    return sock->socket;
}

/**
 * @brief 该函数用于将端口号和 IP 地址绑定带指定套接字上。
 *
 * @param socket 套接字描述符
 * @param name 指向 sockaddr 结构体的指针
 * @param namelen sockaddr 结构体的长度
 * @return int
 */
int wiz_bind(int socket, const struct sockaddr *name, socklen_t namelen)
{

    RyanW5500LinkCheck(-1);

    RyanW5500Socket *sock = NULL;
    uint16_t port = 0;

    RyanW5500CheckCode(NULL != name && 0 != namelen, EAFNOSUPPORT, { return -1; }); // 非法地址

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    // PRASE IP 地址和端口
    const struct sockaddr_in *sin = (const struct sockaddr_in *)name;
    port = (uint16_t)htons(sin->sin_port);

    if (sock->port == port)
        return 0;

    sock->port = port;
    // wizchip_socket内部会先close一次
    int8_t result = wizchip_socket(sock->socket, sock->type, sock->port, 0);
    RyanW5500CheckCode(result == sock->socket, EMFILE, { return -1; });

    sock->state = RyanW5500SocketInit;

    return 0;
}

/**
 * @brief 建立连接
 *
 * @param socket 套接字描述符
 * @param name 服务器地址信息
 * @param namelen 服务器地址结构体的长度
 * @return int
 */
int wiz_connect(int socket, const struct sockaddr *name, socklen_t namelen)
{
    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != name && 0 != namelen, EAFNOSUPPORT, { return -1; }); // 非法地址

    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });
    RyanW5500CheckCode(RyanW5500SocketInit == sock->state, EADDRINUSE, { return -1; }); // 尝试建立 已在使用的 / 未初始化 socket的连接。

    switch (getSn_SR(socket))
    {
    case SOCK_UDP: // udp
    case SOCK_IPRAW:
        if (NULL == sock->remoteAddr)
        {
            sock->remoteAddr = (struct sockaddr *)malloc(sizeof(struct sockaddr));
            RyanW5500CheckCode(NULL != sock->remoteAddr, ENOMEM, { return -1; });
            memset(sock->remoteAddr, 0, sizeof(struct sockaddr));
        }

        // 复制远程端口，ip，协议族
        sock->remoteAddr->sa_len = name->sa_len;
        sock->remoteAddr->sa_family = name->sa_family;
        memcpy(sock->remoteAddr->sa_data, name->sa_data, sizeof(sock->remoteAddr->sa_data));
        break;

    case SOCK_INIT: // tcp
    {
        uint8_t ipStrArr[4] = {0};
        struct sockaddr_in *sin = (struct sockaddr_in *)name;

        inAddrToipStrArr(&sin->sin_addr.s_addr, ipStrArr);

        int result = wizchip_connect(socket, ipStrArr, htons(sin->sin_port));
        if (SOCK_OK != result)
        {
            RyanW5500CheckCode(SOCKERR_IPINVALID != result, EAFNOSUPPORT, { return -1; }); // 无效的 IP 地址
            RyanW5500CheckCode(SOCKERR_TIMEOUT != result, ETIMEDOUT, { return -1; });      // 连接超时
            return -1;
        }
    }

    break;

    default:
        break;
    }

    sock->state = RyanW5500SocketEstablished;
    return 0;
}

/**
 * @brief 监听套接字
 *
 * @param socket 套接字描述符
 * @param backlog 表示一次能够等待的最大连接数目
 * @return int
 */
int wiz_listen(int socket, int backlog)
{
    RyanW5500LinkCheck(-1);

    RyanW5500Socket *sock = RyanW5500GetSock(socket);

    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });                               // 套接字参数不是有效的文件描述符。
    RyanW5500CheckCode(RyanW5500SocketEstablished != sock->state, EINVAL, { return -1; }); // 套接字已连接
    RyanW5500CheckCode(Sn_MR_IPRAW != sock->type, EOPNOTSUPP, { return -1; });             // 套接字协议不支持 listen()
    RyanW5500CheckCode(NULL == sock->serviceInfo, EINVAL, { return -1; });                 // 套接字第一次调用listen

    sock->serviceInfo = (RyanW5500ServiceInfo *)malloc(sizeof(RyanW5500ServiceInfo));
    RyanW5500CheckCode(NULL != sock->serviceInfo, ENOMEM, { goto err; });
    memset(sock->serviceInfo, 0, sizeof(RyanW5500ServiceInfo));

    // 创建listen客户端连接消息队列
    char name[32] = {0};
    snprintf(name, 32, "wiz_mb%d", sock->socket);
    sock->serviceInfo->backlog = backlog > 0 ? backlog : 0;

    // BSD socket允许listen为0，这里消息队列创建1大小，为了appect可以阻塞
    sock->serviceInfo->clientInfoQueueHandle = rt_mq_create(name, sizeof(int8_t), backlog > 0 ? backlog : 1, RT_IPC_FLAG_FIFO);
    RyanW5500CheckCode(NULL != sock->serviceInfo->clientInfoQueueHandle, ENOMEM, { goto err; });

    RyanListInit(&sock->serviceInfo->clientList);
    sock->state = RyanW5500SocketListen;

    if (NULL == RyanW5500CreateListenClient(sock))
        goto err;

    return 0;

err:
    if (NULL != sock->serviceInfo)
    {
        if (NULL != sock->serviceInfo->clientInfoQueueHandle)
            rt_mq_delete(sock->serviceInfo->clientInfoQueueHandle);
        free(sock->serviceInfo);
        sock->serviceInfo = NULL;
    }

    return -1;
}

/**
 * @brief 接收连接
 *
 * @param socket 套接字描述符
 * @param addr 存储客户端设备地址结构体
 * @param addrlen 客户端设备地址结构体的长度
 * @return int
 */
int wiz_accept(int socket, struct sockaddr *addr, socklen_t *addrlen)
{
    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != addr && 0 != addrlen, EAFNOSUPPORT, { return -1; }); // 非法地址

    RyanW5500Socket *serviceSock = RyanW5500GetSock(socket);

    RyanW5500CheckCode(NULL != serviceSock, EBADF, { return -1; });                          // 套接字参数不是有效的文件描述符。
    RyanW5500CheckCode(RyanW5500SocketListen == serviceSock->state, EINVAL, { return -1; }); // 套接字不接受连接，没有listen
    RyanW5500CheckCode(NULL != serviceSock->serviceInfo, ENOBUFS, { return -1; });           // 没有可用的缓冲区空间,理论上不会出现

    while (1)
    {
        int8_t clientSocket = -1;
        // 接收客户端连接消息
        if (rt_mq_recv(serviceSock->serviceInfo->clientInfoQueueHandle, (void *)&clientSocket, sizeof(int8_t), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        RyanW5500CheckCode(-1 != clientSocket, EPROTO, { return -1; });

        // 检查连接消息类型
        if (SOCK_ESTABLISHED != getSn_SR(clientSocket))
        {
            // 错误按摩，关闭客户端套接字
            wiz_closesocket(clientSocket);
            RyanW5500CheckCode(NULL, EPROTO, { return -1; }); // 发生协议错误,客户端套接字不处于连接状态，极端情况才会出现
        }

        // 获取新的客户端套接字信息
        struct sockaddr_in *sin = (struct sockaddr_in *)addr;
        uint8_t ipStrArr[4] = {0};
        uint16_t remotePort = getSn_DPORT(clientSocket);
        getSn_DIPR(clientSocket, ipStrArr); // 获取远程 IP 地址和端口

        memset(sin, 0, sizeof(struct sockaddr_in));
        sin->sin_port = htons(remotePort);
        sin->sin_addr.s_addr = ipStrArrToinAddr(ipStrArr);

        *addrlen = sizeof(struct sockaddr);
        LOG_D("accept remote ip: %s, remote port: %d", inet_ntoa(sin->sin_addr.s_addr), remotePort);

        return clientSocket;
    }
}

/**
 * @brief
 *
 * @param socket 套接字描述符
 * @param data 发送的消息的缓冲区
 * @param size 以字节为单位消息的大小
 * @param flags 消息传输的类型,一般为 0
 * @param to 指向包含目标地址的sockaddr结构
 * @param tolen 指向的sockaddr结构的长度
 * @return int 返回发送的字节数
 */
int wiz_sendto(int socket, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != data && 0 != size, EFAULT, { return -1; });

    RyanW5500Socket *sock = NULL;
    uint8_t socketState = 0;
    int32_t sendLen = 0,
            timeout = 0;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    timeout = sock->sendTimeout;
    if (timeout <= 0)
        timeout = RT_WAITING_FOREVER;

    // 设置接收超时
    platformTimer_t recvTimer = {0};
    platformTimerCutdown(&recvTimer, timeout);

    switch (sock->type)
    {
    case Sn_MR_TCP:
        socketState = getSn_SR(socket);
        RyanW5500CheckCode(SOCK_ESTABLISHED == socketState, EDESTADDRREQ, { return -1; }); // 套接字不是连接模式，没有设置其对等地址，也没有指定目标地址。

        // 如果发送数据比剩余缓冲区大，则分片发送，否则缓冲区数据会被清空
        sendLen = getSn_TX_FSR(sock->socket);
        RyanW5500CheckCode(sendLen > 0, EWOULDBLOCK, { return -1; }); // 发送缓冲区已满

        if (sendLen > size)
            sendLen = size;
        sendLen = wizchip_send(socket, (uint8_t *)data, sendLen);
        RyanW5500CheckCode(sendLen > 0, EINTR, { return -1; }); // 发送失败，一般不会，所以将错误设置为信号中断
        return sendLen;

    case Sn_MR_UDP:
    case Sn_MR_IPRAW:
    {
        socketState = getSn_SR(socket);
        RyanW5500CheckCode(SOCK_UDP == socketState || SOCK_IPRAW == socketState, EDESTADDRREQ, { return -1; });

        struct sockaddr_in *sin = NULL;

        // 获取目标地址结构体指针
        if (RyanW5500SocketEstablished == sock->state && NULL != sock->remoteAddr)
            sin = (struct sockaddr_in *)sock->remoteAddr;
        else
            sin = (struct sockaddr_in *)to;

        // 查看是否是广播
        if (sin->sin_addr.s_addr == -1) // inet_addr("255.255.255.255")
            RyanW5500CheckCode(0 != (sock->soOptionsFlag & SO_BROADCAST), EACCES, { return -1; });

        // 将发送ip转换为W5500识别的
        uint8_t ipStrArr[4] = {0};
        inAddrToipStrArr(&sin->sin_addr.s_addr, ipStrArr);

        // 如果发送数据比剩余缓冲区大，则分片发送，否则缓冲区数据会被清空
        sendLen = getSn_TX_FSR(sock->socket);
        RyanW5500CheckCode(sendLen > 0, EWOULDBLOCK, { return -1; }); // 发送缓冲区已满

        if (sendLen > size)
            sendLen = size;
        sendLen = wizchip_sendto(sock->socket, (uint8_t *)data, sendLen, ipStrArr, htons(sin->sin_port));
        if (sendLen <= 0)
        {
            RyanW5500CheckCode(SOCKERR_SOCKCLOSED == sendLen, EPIPE, { return -1; }); // 套接字被关闭
            LOG_E("udp send fail, result: %d", sendLen);
            return -1;
        }

        return sendLen;
    }

    default:
        LOG_E("socket (%d) type %d is not support.", socket, sock->type);
        return -1;
    }
}

/**
 * @brief
 *
 * @param socket 套接字描述符
 * @param mem 接收消息的缓冲区
 * @param len 接收消息的缓冲区长度
 * @param flags 消息接收的类型，一般为 0
 * @param from 接收地址结构体指针，可以为NULL
 * @param fromlen 接收地址结构体长度
 * @return int 返回接收的数据的长度
 */
int wiz_recvfrom(int socket, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != mem && 0 != len, EFAULT, { return -1; });

    RyanW5500Socket *sock = NULL;
    uint8_t socketState = 0;
    int32_t recvLen = 0,
            timeout = 0,
            result = 0;
    platformTimer_t recvTimer = {0};

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    // 设置 WIZNnet 套接字接收超时
    timeout = sock->recvTimeout;
    if (timeout <= 0)
        timeout = RT_WAITING_FOREVER;

    platformTimerCutdown(&recvTimer, timeout);

again:
    // 判断是否超时
    RyanW5500CheckCode(0 != platformTimerRemain(&recvTimer), EAGAIN, { return -1; });

    switch (sock->type)
    {
    case Sn_MR_TCP:
    {
        socketState = getSn_SR(socket);
        RyanW5500CheckCode(SOCK_ESTABLISHED == socketState, ENOTCONN, { return -1; }); // 在未连接的连接模式套接字上尝试接收。

        result = rt_event_recv(RyanW5500Entry.W5500EventHandle, (1 << sock->socket),
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               timeout, NULL);
        RyanW5500CheckCode(RT_EOK == result, EAGAIN, { return -1; });                        // 判断是否超时
        RyanW5500CheckCode(RyanW5500SocketClose != sock->state, ECONNRESET, { return -1; }); // 连接被对等方关闭。

        // 获取数据寄存器，没有数据就重新接收
        recvLen = getSn_RX_RSR(socket);
        if (recvLen <= 0)
            goto again;

        // 如果数据比缓冲区大，则调整至缓冲区大小
        if (recvLen > len)
            recvLen = len;

        recvLen = wizchip_recv(socket, mem, recvLen);
        if (recvLen <= 0)
        {
            LOG_E("recv error, result: %d", recvLen);
            return -1;
        }

        return recvLen;
    }

    case Sn_MR_UDP:
    case Sn_MR_IPRAW:
    {
        socketState = getSn_SR(socket);
        RyanW5500CheckCode(SOCK_UDP == socketState || SOCK_IPRAW == socketState, ENOTCONN, { return -1; }); // 在未连接的连接模式套接字上尝试接收。

        result = rt_event_recv(RyanW5500Entry.W5500EventHandle, (1 << sock->socket),
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               timeout, NULL);
        RyanW5500CheckCode(RT_EOK == result, EAGAIN, { return -1; }); // 判断是否超时
        RyanW5500CheckCode(RyanW5500SocketClose != sock->state, ECONNRESET, { return -1; });

        // 获取数据寄存器，没有数据就重新接收
        recvLen = getSn_RX_RSR(socket);
        if (recvLen <= 0)
            goto again;

        uint16_t remotePort = 0;
        uint8_t remoteIp[4] = {0};

        // 如果数据比缓冲区大，则调整至缓冲区大小
        if (recvLen > len)
            recvLen = len;

        recvLen = wizchip_recvfrom(socket, mem, recvLen, remoteIp, &remotePort);
        if (recvLen <= 0)
        {
            LOG_E("recvfrom error, result: %d", recvLen);
            return -1;
        }

        // 将消息信息写入from结构体
        *fromlen = sizeof(struct sockaddr_in);
        struct sockaddr_in *sin = (struct sockaddr_in *)from;
        memset(sin, 0, sizeof(struct sockaddr_in));

        sin->sin_port = htons(remotePort);
        sin->sin_addr.s_addr = ipStrArrToinAddr(remoteIp);
        return recvLen;
    }

    default:
        LOG_E("socket (%d) type %d is not support.", socket, sock->type);
        return -1;
    }
}

int wiz_send(int socket, const void *data, size_t size, int flags)
{
    return wiz_sendto(socket, data, size, flags, NULL, 0);
}

/**
 * @brief TCP 数据接收
 *
 * @param socket 套接字描述符
 * @param mem 接收消息的缓冲区
 * @param len 接收消息的缓冲区长度
 * @param flags 消息接收的类型，一般为 0
 * @return int
 */
int wiz_recv(int socket, void *mem, size_t len, int flags)
{
    return wiz_recvfrom(socket, mem, len, flags, NULL, NULL);
}

/**
 * @brief 关闭套接字
 *
 * @param socket 套接字描述符
 * @return int
 */
int wiz_closesocket(int socket)
{
    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    if (SOCK_CLOSED == getSn_SR(sock->socket))
    {
        RyanW5500SocketDestory(sock);
        return -1;
    }

    int8_t result = SOCK_FATAL;
    // 属于tcp套接字，但又不是listen服务器套接字
    if (Sn_MR_TCP == sock->type && RyanW5500SocketEstablished == sock->state && NULL == sock->serviceInfo)
        result = wizchip_disconnect(sock->socket);

    if (SOCK_OK != result)
    {
        result = wizchip_close(sock->socket);
        if (SOCK_OK != result)
        {
            LOG_E("socket(%d) close failed.", sock->socket);
            RyanW5500SocketDestory(sock);
            return -1;
        }
    }

    return RyanW5500SocketDestory(sock);
}

/**
 * @brief 按设置关闭套接字
 *
 * @param socket 套接字描述符
 * @param how 套接字控制的方式
 * @return int
 */
int wiz_shutdown(int socket, int how)
{
    RyanW5500Socket *sock = NULL;

    switch (how)
    {
    case SHUT_RD:   // 禁用进一步的接收操作。
    case SHUT_WR:   // 禁用进一步的发送操作。
    case SHUT_RDWR: // 禁用进一步的发送和接收操作。
        break;

    default:
        RyanW5500CheckCode(NULL, EINVAL, return -1;)
    }

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    return wiz_closesocket(sock->socket);
}

/**
 * @brief 设置套接字选项
 *
 * @param socket 套接字描述符
 * @param level 协议栈配置选项
 * @param optname 需要设置的选项名
 * @param optval 设置选项值的缓冲区地址
 * @param optlen 设置选项值的缓冲区长度
 * @return int
 */
int wiz_setsockopt(int socket, int level, int optname, const void *optval, socklen_t optlen)
{
    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != optval && 0 != optlen, EFAULT, { return -1; });

    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; }); // 指定的选项在指定的套接字级别无效或套接字已关闭。

    if (SOL_SOCKET == level)
    {
        switch (optname)
        {
        case SO_BROADCAST:                                                       // 配置用于发送广播数据的套接字。此选项仅适用于支持广播 IP 和 UDP 的协议 布尔类型
            RyanW5500CheckCode(Sn_MR_TCP != sock->type, EINVAL, { return -1; }); // 指定的选项在指定的套接字级别无效或套接字已关闭。
            RyanW5500CheckCode(sizeof(int) == optlen, EFAULT, { return -1; });
            if (1 == *(int *)optval)
                sock->soOptionsFlag |= optname;
            else
                sock->soOptionsFlag &= ~optname;
            break;

        case SO_KEEPALIVE: // 为套接字连接启用保持连接。 仅适用于支持保持连接的协议，比如tcp, 对于 TCP，默认保持连接超时为 2 小时，保持连接间隔为 1 秒  布尔类型
        {
            if (sock->type != Sn_MR_TCP) // 对于w5500非tcp连接忽略
                break;
            uint32_t keepalive = 2 * 60 * 60 / 5; // w5500 Sn_KPALVTR单位时间为5
            if (1 == *(int *)optval)
                RyanW5500CheckCode(SOCK_OK == wizchip_setsockopt(sock->socket, SO_KEEPALIVEAUTO, (void *)&keepalive), EINVAL, { return -1; });
            break;
        }

        case SO_RCVTIMEO: // 阻止接收调用的超时（以毫秒为单位）。 此选项的默认值为零，表示接收操作不会超时 类型struct timeval
            RyanW5500CheckCode(sizeof(struct timeval) == optlen, EFAULT, { return -1; });
            sock->recvTimeout = ((const struct timeval *)optval)->tv_sec * 1000 +
                                ((const struct timeval *)optval)->tv_usec / 1000;
            break;

        case SO_SNDTIMEO: // 阻止发送调用的超时（以毫秒为单位）。 此选项的默认值为零，表示发送操作不会超时。类型struct timeval
            RyanW5500CheckCode(sizeof(struct timeval) == optlen, EFAULT, { return -1; });
            sock->sendTimeout = ((const struct timeval *)optval)->tv_sec * 1000 +
                                ((const struct timeval *)optval)->tv_usec / 1000;
            break;

        // 以下参数没有实现的原因是因为W5500 本身发送 / 接收缓冲区各16K是8个socket共享的
        // 用户可能不知道他修改的缓冲区是否是正确，如果超过16k则有可能造成个别socket通道发送 / 接收失败
        case SO_SNDBUF: // 设置发送缓冲区大小。此选项采用 int 值。
        case SO_RCVBUF: // 设置接收缓冲区大小。此选项采用 int 值。

        case SO_RCVLOWAT: // 此选项设置套接字输入操作要处理的最小字节数
        case SO_SNDLOWAT: // 此选项设置套接字输出操作要处理的最小字节数

        case SO_DEBUG:     // 启用调试输出
        case SO_REUSEADDR: // 允许套接字绑定到已使用的地址和端口。布尔类型
        case SO_LINGER:    // 如果存在数据，则停留在 close()上等待缓存数据发送完毕。 布尔类型
        case SO_OOBINLINE: // 指示应随常规数据一起返回超出边界的数据。 此选项仅适用于支持带外数据的面向连接的协议。 布尔类型
        case SO_DONTROUTE: // 指示应在套接字绑定到的任何接口上发送传出数据，而不是在其他接口上路由。 布尔类型
            return -1;

        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else if (IPPROTO_IP == level)
    {
        switch (optname)
        {
        case IP_TOS: // 本选项是int型的数值选项，允许对TCP、UDP的IP头中的tos字段进行设置
            RyanW5500CheckCode(SOCK_OK == wizchip_setsockopt(sock->socket, SO_TOS, (void *)optval), EINVAL, { return -1; });
            break;

        case IP_TTL: // 本选项是int型的数值选项，允许对单播报文的TTL默认值进行设置。
            RyanW5500CheckCode(SOCK_OK == wizchip_setsockopt(sock->socket, SO_TTL, (void *)optval), EINVAL, { return -1; });
            break;

        // UDP 多播的选项和类型
        case IP_ADD_MEMBERSHIP: // 加入一个组播组 struct ip_mreq
        {
            RyanW5500CheckCode(Sn_MR_TCP != sock->type, EINVAL, { return -1; }); // 指定的选项在指定的套接字级别无效或套接字已关闭。
            RyanW5500CheckCode(sizeof(struct ip_mreq) == optlen, EFAULT, { return -1; });
            struct ip_mreq *mreq = (struct ip_mreq *)optval;

            // 需要在Sn_CR命令之前，分开配置组播 IP 地址及端口号。
            wizchip_close(sock->socket);

            // 组播MAC地址的高24bit为0x01005e，高位第25bit为0，即高25bit为固定值。
            // MAC地址的低23bit为组播IP地址的低23bit
            uint8_t ipStrArr[4] = {0};
            inAddrToipStrArr(&mreq->imr_multiaddr.s_addr, ipStrArr);
            uint8_t DHAR[6] = {0x01, 0x00, 0x5e};
            memcpy(DHAR + 3, ipStrArr + 1, 3);
            DHAR[2] &= ~(1 << 7); // 高位第25bit为0
            setSn_DHAR(sock->socket, DHAR);
            setSn_DIPR(sock->socket, ipStrArr);
            setSn_DPORT(sock->socket, sock->port);

            int8_t result = wizchip_socket(sock->socket, sock->type, sock->port, Sn_MR_MULTI);
            RyanW5500CheckCode(result == sock->socket, EMFILE, { return -1; });
            break;
        }

        case IP_DROP_MEMBERSHIP: // 退出组播组 struct ip_mreq
        {
            int8_t result = wizchip_socket(sock->socket, sock->type, sock->port, 0);
            RyanW5500CheckCode(result == sock->socket, EMFILE, { return -1; });
            break;
        }

        case IP_MULTICAST_TTL:  // 设置多播组数据的TTL值, 范围为0～255之间的任何值
        case IP_MULTICAST_IF:   // 设置用于发送 IPv4 多播流量的传出接口。 struct in_addr
        case IP_MULTICAST_LOOP: // 对于已加入一个或多个多播组的套接字，此套接字控制它是否将收到通过所选多播接口发送到这些多播组的 传出 数据包的副本。布尔类型
            return -1;
            // case IP_RECVDSTADDR:     // 本选项是标志位，置上之后，允许对UDP socket调用recvfrom的时候，能够以辅助数据的形式获取到客户端报文的目的地址。
            // case IP_RECVIF:          // 本选项是标志位，置上之后，允许对UDP socket调用recvfrom的时候，能够以辅助数据的形式获取到客户端报文的目的接口。

        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else if (IPPROTO_TCP == level)
    {
        switch (optname)
        {
        case TCP_NODELAY:   // 启用或禁用 TCP 套接字的 Nagle 算法。
        case TCP_KEEPIDLE:  // 获取或设置 TCP 连接在发送到远程之前保持空闲状态的秒数。
        case TCP_KEEPINTVL: // 获取或设置 TCP 连接在发送另一个保留探测之前等待保持响应的秒数
        case TCP_KEEPCNT:   // 获取或设置将在连接终止之前发送的 TCP 保持活动探测数。 将TCP_KEEPCNT设置为大于 255 的值是非法的。
            return -1;
        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else
        RyanW5500CheckCode(NULL, EINVAL, { return -1; });

    return 0;
}

/**
 * @brief 获取套接字选项
 *
 * @param socket 套接字描述符
 * @param level 协议栈配置选项
 * @param optname 需要设置的选项名
 * @param optval 获取选项值的缓冲区地址
 * @param optlen 获取选项值的缓冲区长度地址
 *          如果选项值的大小大于option_len ，存储在option_value参数指向的对象中的值将被静默截断。
 *          否则， option_len参数指向的对象 应被修改以指示值的实际长度。
 * @return int
 */
int wiz_getsockopt(int socket, int level, int optname, void *optval, socklen_t *optlen)
{

    RyanW5500LinkCheck(-1);
    RyanW5500CheckCode(NULL != optval && NULL != optlen, EFAULT, { return -1; });

    RyanW5500Socket *sock = NULL;

    sock = RyanW5500GetSock(socket);
    RyanW5500CheckCode(NULL != sock, EBADF, { return -1; });

    if (SOL_SOCKET == level)
    {
        switch (optname)
        {
        case SO_BROADCAST: // 报告是否支持广播消息的传输 布尔类型
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            *(int *)optval = Sn_MR_TCP != sock->type ? 1 : 0;
            *optlen = sizeof(int);
            break;

            // 暂不实现
        case SO_KEEPALIVE: // 报告连接是否通过定期传输消息保持活动状态  布尔类型
            return -1;

        case SO_RCVTIMEO: // 报告阻止接收调用的超时。 类型struct timeval
            RyanW5500CheckCode(sizeof(struct timeval) <= *optlen, EINVAL, { return -1; });
            ((struct timeval *)(optval))->tv_sec = sock->recvTimeout / 1000U;
            ((struct timeval *)(optval))->tv_usec = (sock->recvTimeout % 1000U) * 1000U;
            *optlen = sizeof(struct timeval);
            break;

        case SO_SNDTIMEO: // 报告阻止发送调用的超时。 此选项的默认值为零，表示发送操作不会超时。类型struct timeval
            RyanW5500CheckCode(sizeof(struct timeval) <= *optlen, EINVAL, { return -1; });
            ((struct timeval *)optval)->tv_sec = sock->sendTimeout / 1000U;
            ((struct timeval *)optval)->tv_usec = (sock->sendTimeout % 1000U) * 1000U;
            *optlen = sizeof(struct timeval);
            break;

        case SO_SNDBUF: // 报告发送缓冲区大小。此选项采用 int 值。
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            wizchip_getsockopt(socket, (sockopt_type)SO_SENDBUF, (void *)optval);
            *optlen = sizeof(int);
            break;

        case SO_RCVBUF: // 报告接收缓冲区大小。此选项采用 int 值。
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            wizchip_getsockopt(socket, (sockopt_type)SO_RECVBUF, (void *)optval);
            *optlen = sizeof(int);
            break;

        case SO_ACCEPTCONN: // 报告是否启用套接字侦听 布尔类型
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            if (RyanW5500SocketListen == sock->state && NULL != sock->serviceInfo)
                *(int *)optval = 1;
            else
                *(int *)optval = 0;
            *optlen = sizeof(int);
            break;

        case SO_ERROR: // 报告有关错误状态的信息并将其清除。 int
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            *(int *)optval = errno;
            *optlen = sizeof(int);
            errno = 0;
            break;

        case SO_TYPE: // 报告套接字类型   int
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            *(int *)optval = sock->type;
            *optlen = sizeof(int);
            break;

        case SO_DEBUG:     // 报告是否正在记录调试信息 布尔类型
        case SO_REUSEADDR: // 报告是否允许套接字绑定到已使用的地址和端口。布尔类型
        case SO_LINGER:    // 报告套接字是否停留在 close()上
        case SO_OOBINLINE: // 报告套接字是否保留接收到的带外数据  此选项仅适用于支持带外数据的面向连接的协议。 布尔类型
        case SO_DONTROUTE: // 报告是否允许指示应在套接字绑定到的任何接口上发送传出数据，而不是在其他接口上路由。 布尔类型
        case SO_RCVLOWAT:  // 报告套接字输入操作要处理的最小字节数 int
        case SO_SNDLOWAT:  // 报告套接字输出操作要处理的最小字节数 int
            return -1;

        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else if (IPPROTO_IP == level)
    {
        switch (optname)
        {
        case IP_TOS: // 本选项是int型的数值选项，允许对TCP、UDP的IP头中的tos字段进行获取
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            wizchip_getsockopt(sock->socket, (sockopt_type)SO_TOS, optval);
            *optlen = sizeof(int);
            break;

        case IP_TTL: // 本选项是int型的数值选项，允许对单播报文的TTL默认值进行获取
            RyanW5500CheckCode(sizeof(int) <= *optlen, EINVAL, { return -1; });
            wizchip_getsockopt(sock->socket, (sockopt_type)SO_TTL, optval);
            *optlen = sizeof(int);
            break;

        // UDP 多播的选项和类型
        case IP_MULTICAST_TTL: // 获取多播组数据的TTL值, 范围为0～255之间的任何值
        case IP_MULTICAST_IF:  // 获取用于发送 IPv4 多播流量的传出接口。 struct in_addr
            return -1;

            // case IP_RECVDSTADDR: // 本选项是标志位，置上之后，允许对UDP socket调用recvfrom的时候，能够以辅助数据的形式获取到客户端报文的目的地址。
            // case IP_RECVIF:      // 本选项是标志位，置上之后，允许对UDP socket调用recvfrom的时候，能够以辅助数据的形式获取到客户端报文的目的接口。
            //     return -1;

        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else if (IPPROTO_TCP == level)
    {
        switch (optname)
        {
        case TCP_NODELAY:   // 获取或设置 TCP 套接字的 Nagle 算法。
        case TCP_KEEPIDLE:  // 获取或设置 TCP 连接在发送到远程之前保持空闲状态的秒数。
        case TCP_KEEPINTVL: // 获取或设置 TCP 连接在发送另一个保留探测之前等待保持响应的秒数
        case TCP_KEEPCNT:   // 获取或设置将在连接终止之前发送的 TCP 保持活动探测数。 将TCP_KEEPCNT设置为大于 255 的值是非法的。
            return -1;
        default:
            RyanW5500CheckCode(NULL, ENOPROTOOPT, { return -1; });
        }
    }
    else
        RyanW5500CheckCode(NULL, EINVAL, { return -1; });

    return 0;

    return 0;
}

int RyanW5500_gethostbyname(const char *name, ip_addr_t *addr)
{

    int idx = 0;
    int nameLen = strlen(name);
    char ipStrArr[16] = {0};

    // 检查域名 / ip地址
    for (idx = 0; idx < nameLen && !isalpha(name[idx]); idx++)
        ;

    // 输入名称为ip地址
    if (idx == strlen(name))
        strncpy(ipStrArr, name, nameLen);
    // 输入名称为域名需要调用dns
    else
    {
        int8_t ret = 0;
        uint8_t remote_ip[4] = {0};
        uint8_t data_buffer[512];

        ulog_w("TAG", "%s:%d 开始获取dns", __FILE__, __LINE__);
        // DNS客户端处理
        ret = DNS_run(gWIZNETINFO.dns, (uint8_t *)name, remote_ip, data_buffer);
        if (1 != ret)
        {
            if (-1 == ret)
            {
                LOG_E("MAX_DOMAIN_NAME is too small, should be redefined it.");
                return -1;
            }

            if (-2 == ret)
            {
                LOG_E("DNS failed, socket number is full.");
                return -2;
            }
            return -1;
        }

        // 域解析失败
        if (remote_ip[0] == 0)
            return -1;

        snprintf(ipStrArr, 16, "%u.%u.%u.%u", remote_ip[0], remote_ip[1], remote_ip[2], remote_ip[3]);
    }

    inet_aton(ipStrArr, addr);
    return 0;
}

/**
 * @brief 根据主机名解析地址信息 DNS
 *
 * @param name 主机名，dns规定长度限制为255
 * @return struct hostent*
 */
struct hostent *wiz_gethostbyname(const char *name)
{
    // 检查WIZnet初始化状态
    RyanW5500LinkCheck(NULL);

    ip_addr_t addr = {0};
    int err = 0;

    // 用于 gethostbyname() 的缓冲区变量
    static struct hostent s_hostent;
    static char s_hostname[DNS_MAX_NAME_LENGTH + 1]; // 主机名称
    static char *s_aliases;                          // 主机别名
    static ip_addr_t s_hostent_addr;                 // 临时主机IP
    static ip_addr_t *s_phostent_addr[2];            // 主机IP

    if (NULL == name)
    {
        LOG_E("gethostbyname input name err!");
        return NULL;
    }

    if (strlen(name) > DNS_MAX_NAME_LENGTH)
        return NULL;

    err = RyanW5500_gethostbyname(name, &addr);
    if (0 != err)
    {
        // 不支持h_errno
        // RyanW5500CheckCode(-1 == result, HOST_NOT_FOUND, return NULL);
        // RyanW5500CheckCode(-2 == result, TRY_AGAIN, return NULL);
        return NULL;
    }

    // 填充主机结构
    s_hostent_addr = addr;
    s_phostent_addr[0] = &s_hostent_addr;
    s_phostent_addr[1] = NULL;
    strncpy(s_hostname, name, DNS_MAX_NAME_LENGTH);
    s_hostname[DNS_MAX_NAME_LENGTH] = 0;
    s_aliases = NULL;

    s_hostent.h_name = s_hostname;
    s_hostent.h_aliases = &s_aliases;
    s_hostent.h_addrtype = AF_INET;
    s_hostent.h_length = sizeof(ip_addr_t);
    s_hostent.h_addr_list = (char **)&s_phostent_addr;

    return &s_hostent;
}

// 看以后要不要实现了

/**
 * @brief wiz_gethostbyname 线程安全版本
 *
 * @param name 主机名，dns规定长度限制为255
 * @param ret 预先分配的结构体，用于存储结果
 * @param buf 预先分配的缓冲区，用于存储额外的数据
 * @param buflen 缓冲区大小
 * @param result 指向主机指针的指针，成功时设置为 ret，错误时设置为零
 * @param h_errnop 指向存储错误的 int 的指针（而不是修改全局 h_errno）
 * @return int 0成功，非0错误
 */
int wiz_gethostbyname_r(const char *name, struct hostent *ret, char *buf, size_t buflen, struct hostent **result, int *h_errnop)
{

    // 检查WIZnet初始化状态
    RyanW5500LinkCheck(-1);

    // gethostbyname_r 的辅助结构，用于访问hostent中的 char*缓冲区
    struct gethostbyname_r_helper
    {
        ip_addr_t *addr_list[2];
        ip_addr_t addr;
        char *aliases;
    };

    char *hostname = NULL;
    int lh_errno = 0,
        err = 0;
    size_t namelen = 0;
    struct gethostbyname_r_helper *h = NULL;

    if (h_errnop == NULL)
        h_errnop = &lh_errno; // 确保 h_errnop 永远不会为 NULL

    if ((name == NULL) || (ret == NULL) || (buf == NULL))
    {
        *h_errnop = EINVAL; // 参数不足
        return -1;
    }

    if (result == NULL)
    {
        *h_errnop = EINVAL; // 参数不足
        return -1;
    }

    // 第一件事：将 *result 设置为空
    *result = NULL;

    namelen = strlen(name);
    if (buflen < (sizeof(struct gethostbyname_r_helper) + (namelen + 1))) // 计算buf是否足够存储hostent信息
    {
        *h_errnop = ERANGE; // buf 不能保存所需的数据 + 名称的副本
        return -1;
    }

    h = (struct gethostbyname_r_helper *)buf;                       // 从buf中分配用于hostent中的 char*缓冲区
    hostname = ((char *)h) + sizeof(struct gethostbyname_r_helper); // 从buf中分配用于hostent中 h_name缓冲区

    err = RyanW5500_gethostbyname(name, &h->addr);
    if (0 != err)
    {
        *h_errnop = HOST_NOT_FOUND;
        return -1;
    }

    // 拷贝hostname信息
    memcpy(hostname, name, namelen);
    hostname[namelen] = 0;

    // 填充主机结构
    h->addr_list[0] = &h->addr;
    h->addr_list[1] = NULL;
    h->aliases = NULL;

    ret->h_name = hostname;
    ret->h_aliases = &h->aliases;
    ret->h_addrtype = AF_INET;
    ret->h_length = sizeof(ip_addr_t);
    ret->h_addr_list = (char **)&h->addr_list;

    // 设置结果
    *result = ret;

    return 0;
}

/**
 * @brief 获取地址信息,应该是线程安全的
 * !没有ipv6实现，只支持ipv4 地址
 *
 * @param nodename 主机名，可以是域名，也可以是地址的点分十进制字符串
 * @param servname 服务名可以是十进制的端口号("8080")字符串，(也可以是已定义的服务名称，如"ftp"、"http"等，当前接口不支持)
 * @param hints 用户设定的 struct addrinfo 结构体
 * @param res 该参数获取一个指向存储结果的 struct addrinfo 结构体列表，使用完成后调用 freeaddrinfo() 释放存储结果空间。
 * @return int
 */
int wiz_getaddrinfo(const char *nodename, const char *servname, const struct addrinfo *hints, struct addrinfo **res)
{

    // 检查WIZnet初始化状态
    RyanW5500LinkCheck(-1);

    int err = 0,
        port_nr = 0,
        ai_family = 0;
    size_t total_size = 0;
    size_t namelen = 0;
    ip_addr_t addr = {0};
    struct addrinfo *ai = NULL;
    struct sockaddr_storage *sa = NULL;

    if (NULL == res)
        return EAI_FAIL;

    *res = NULL;

    // nodename 和 servname 可以设置为NULL，但同时只能有一个为NUL。
    if ((NULL == nodename) && (NULL == servname))
        return EAI_NONAME;

    if (NULL == hints)
        ai_family = AF_UNSPEC;
    else
    {
        ai_family = hints->ai_family; // 套接字的地址族

        if (ai_family != AF_WIZ &&
            ai_family != AF_INET &&
            ai_family != AF_UNSPEC)
            return EAI_FAMILY;
    }

    // 指定的服务名称: 转换为端口号
    if (servname != NULL)
    {
        port_nr = atoi(servname);
        if ((port_nr <= 0) || (port_nr > 0xffff))
            return EAI_SERVICE;
    }

    if (NULL == nodename)
    {
        inet_aton("127.0.0.1", &addr); // 没有指定服务位置，请使用回送地址
    }
    else if (nodename != NULL)
    {
        // 指定了服务位置，尝试解析
        if ((hints != NULL) && (hints->ai_flags & AI_NUMERICHOST))
        {
            if (AF_INET != ai_family)
                return EAI_NONAME;

            // 解析地址字符串
            if (!inet_aton(nodename, (ip4_addr_t *)&addr))
                return EAI_NONAME;
        }
        else
        {
            err = RyanW5500_gethostbyname(nodename, &addr);
            if (0 != err)
                return EAI_FAIL;
        }
    }

    total_size = sizeof(struct addrinfo) + sizeof(struct sockaddr_storage);
    if (NULL != nodename)
    {
        namelen = strlen(nodename);
        if (namelen > DNS_MAX_NAME_LENGTH)
            return EAI_FAIL;

        total_size += namelen + 1;
    }

    if (total_size > sizeof(struct addrinfo) + sizeof(struct sockaddr_storage) + DNS_MAX_NAME_LENGTH + 1)
        return EAI_FAIL;

    ai = (struct addrinfo *)malloc(total_size);
    if (ai == NULL)
        return ENOMEM;

    memset(ai, 0, total_size);

    // 通过void强制转换 * 以消除对齐警告
    sa = (struct sockaddr_storage *)(void *)((uint8_t *)ai + sizeof(struct addrinfo));
    struct sockaddr_in *sa4 = (struct sockaddr_in *)sa;

    /* set up sockaddr */
    sa4->sin_addr.s_addr = addr.addr;
    sa4->sin_family = AF_INET;
    sa4->sin_len = sizeof(struct sockaddr_in);
    sa4->sin_port = htons((uint16_t)port_nr);
    ai->ai_family = AF_INET;

    // 设置addrinfo
    if (hints != NULL)
    {
        // 如果指定，从指定中复制套接字类型和协议
        ai->ai_socktype = hints->ai_socktype;
        ai->ai_protocol = hints->ai_protocol;
    }

    if (nodename != NULL)
    {
        // 如果指定，将节点名称复制到canonname
        ai->ai_canonname = ((char *)ai + sizeof(struct addrinfo) + sizeof(struct sockaddr_storage));
        memcpy(ai->ai_canonname, nodename, namelen);
        ai->ai_canonname[namelen] = 0;
    }

    ai->ai_addrlen = sizeof(struct sockaddr_storage);
    ai->ai_addr = (struct sockaddr *)sa;

    *res = ai;
    return 0;
}

void wiz_freeaddrinfo(struct addrinfo *ai)
{
    struct addrinfo *next = NULL;

    while (ai != NULL)
    {
        next = ai->ai_next;
        free(ai);
        ai = next;
    }
}
