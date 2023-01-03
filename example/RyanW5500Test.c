

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <board.h>

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_spi.h"
#include <rtdbg.h>
#include "ulog.h"
#include "RyanW5500.h"
#include "sys/socket.h"
#include "netdb.h"
#include "sal_socket.h"
#include "sal_netdb.h"
#include "netdev_ipaddr.h"
#include "netdev.h"

#ifdef PKG_USING_RYANW5500_EXAMPLE
static const char *TAG = "RyanW5500Test";

static struct netdev *RyanNetdev = NULL;

void neDevStatusChangeCallback(struct netdev *netdev, enum netdev_cb_type type)
{
    ulog_i(TAG, "w5500 nedev state: %d", type);
}

int w5500Start(int argc, char *argv[])
{

    if (NULL != RyanNetdev)
    {
        ulog_w(TAG, "w5500已经启动,不要重复选择");
        return -1;
    }

    wiz_NetInfo netInfo = {0};

    // mac地址有48bit
    // mac地址高24bit表示网卡制造商，由IEEE分配，称为OUI（组织唯一标识符）, 低24bit为网卡制造商分配的唯一编号
    // mac地址首位偶数单播，首位奇数为多播地址,多播作为设备地址是无效(第48bit 0 单播， 1 多播)
    // 广播mac地址:FF-FF-FF-FF-FF-FF
    // 第一个字节一般为00
    uint8_t myMac[6] = {0x00, 0x08, 0xdc, 0x2f, 0x0c, 0x37};

    // stm32可以使用唯一96Bit芯片序列号
    // myMac[3] = *(uint8_t *)(UID_BASE + 0);
    // myMac[4] = *(uint8_t *)(UID_BASE + 4);
    // myMac[5] = *(uint8_t *)(UID_BASE + 8);

    memcpy(netInfo.mac, myMac, sizeof(netInfo.mac));

    // 用户也使用随机数来，需要支持rand函数才行
    // ?但操作系统启动时间几乎时恒定的，ms时钟，可能造成随机数种子相同，随机数也一样的可能性
    // srand(rt_tick_get()); // 设立随机数种子
    // myMac[3] = rand() % 254 + 0;// 生成0~254的随机数
    // srand(rt_tick_get()); // 设立随机数种子
    // myMac[4] = rand() % 254 + 0;// 生成0~254的随机数
    // srand(rt_tick_get()); // 设立随机数种子
    // myMac[5] = rand() % 254 + 0;// 生成0~254的随机数

    uint8_t ipStrArr[4] = {0};
    inet_pton(AF_INET, "192.168.3.69", &ipStrArr);
    memcpy(netInfo.ip, ipStrArr, 4);

    inet_pton(AF_INET, "255.255.252.0", &ipStrArr);
    memcpy(netInfo.sn, ipStrArr, 4);

    inet_pton(AF_INET, "192.168.1.1", &ipStrArr);
    memcpy(netInfo.gw, ipStrArr, 4);

    inet_pton(AF_INET, "114.114.114.114", &ipStrArr);
    memcpy(netInfo.dns, ipStrArr, 4);

    netInfo.dhcp = NETINFO_DHCP; // 使能dhcp

    if (0 != RyanW5500Init(&netInfo)) // 初始化w5500并启动
    {
        ulog_e(TAG, "初始化w5500错误");
        return -1;
    }

    RyanNetdev = netdev_get_by_name("RyanW5500"); // netdev
    if (NULL == RyanNetdev)
    {
        ulog_e(TAG, "No device found");
        return -1;
    }

    netdev_set_default(RyanNetdev);
    netdev_set_status_callback(RyanNetdev, neDevStatusChangeCallback);

    ulog_i(TAG, "w5500 启动成功");

    // while (!netdev_is_link_up(RyanNetdev))
    // {
    //     delay(200);
    // }

    return 0;
}

// TCP并发ECHO服务器
void deal_client_fun(void *argument)
{

    int fd = *(int *)argument; // 通过arg获得已连接套接字
    char buf[512] = {0};

    // struct timeval tv = {
    //     .tv_sec = 2,
    //     .tv_usec = 0};
    // setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(struct timeval)); // 设置接收超时

    while (1)
    {

        // 获取客户端请求
        int len = recv(fd, buf, sizeof(buf), 0);
        if (len <= 0)
        {
            if ((errno == EAGAIN ||      // 套接字已标记为非阻塞，而接收操作被阻塞或者接收超时
                 errno == EWOULDBLOCK || // 发送时套接字发送缓冲区已满，或接收时套接字接收缓冲区为空
                 errno == EINTR))        // 操作被信号中断
            {
                ulog_w(TAG, "接收超时...........");
                continue;
            }

            ulog_e(TAG, "遇到错误, 退出 socket: %d, len: %d", fd, len);
            closesocket(fd);
            return;
        }

        // rt_kprintf("客户端的请求为:%s recv:%d", buf, len);
        send(fd, buf, len, 0); // 回应客户端
    }
}

void tcpEchoTask(void *argument)
{
    int32_t port = (int32_t)argument;

    while (1)
    {

        // 创建一个tcp监听套接字
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);

        // 使用bind函数 给监听套接字 绑定固定的ip以及端口
        struct sockaddr_in my_addr = {
            .sin_family = AF_INET,                 // 协议族
            .sin_port = htons(port),               // 端口号
            .sin_addr.s_addr = htonl(INADDR_ANY)}; // 设置地址

        bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));

        // 使用listen创建连接队列 主动变被动
        listen(sockfd, 2);

        while (1)
        {
            // 使用accpet函数从连接队列中 提取已完成的连接 得到已连接套接字
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int new_fd = accept(sockfd, (struct sockaddr *)&cli_addr, &cli_len);
            if (new_fd < 0)
                break;

            // new_fd代表的是客户端的连接   cli_addr存储是客户端的信息
            ulog_i(TAG, "客户端: %s, port: %hu, 连接了服务器", inet_ntoa(cli_addr.sin_addr.s_addr), ntohs(cli_addr.sin_port));

            rt_thread_t idex = rt_thread_create("socket123123123", deal_client_fun, (void *)&new_fd, 2048, 12, 5);
            if (idex != NULL)
                rt_thread_startup(idex);
        }

        // 关闭监听套接字
        closesocket(sockfd);
    }
}

void udpEchoServiceTask(void *argument)
{
    int32_t port = (int32_t)argument;

    // 创建通讯的udp套接字(没有port， ip)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    ulog_i(TAG, "UDP套接字sockfd=%d", sockfd);

    // 定义一个IPv4地址结构, 存放客户端的地址信息(本地主机)
    struct sockaddr_in myAddr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)};

    // 给udp套接字 bind绑定一个固定的地址信息
    bind(sockfd, (struct sockaddr *)&myAddr, sizeof(myAddr));

    // 定义一个IPv4地址结构  存放发送者的数据
    struct sockaddr_in from_addr;
    socklen_t fromLen = sizeof(from_addr);
    char buf[512] = {0};

    while (1)
    {
        int len = recvfrom(sockfd, buf, sizeof(buf), 0,
                           (struct sockaddr *)&from_addr, &fromLen);
        if (len <= 0)
        {
            if ((errno == EAGAIN ||      // 套接字已标记为非阻塞，而接收操作被阻塞或者接收超时
                 errno == EWOULDBLOCK || // 发送时套接字发送缓冲区已满，或接收时套接字接收缓冲区为空
                 errno == EINTR))        // 操作被信号中断
            {
                ulog_w(TAG, "接收超时...........");
                continue;
            }

            ulog_e(TAG, "遇到错误, 退出 socket: %d, len: %d", sockfd, len);
            break;
        }

        // ulog_i(TAG, "udp echo service, 消息来自: %s, port: %hu", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));
        // ulog_i(TAG, "udp echo service, len: %d, msg: %s", len, buf);

        sendto(sockfd, buf, len, 0, (struct sockaddr *)&from_addr, sizeof(from_addr));

        memset(buf, 0, len);
    }

    // 关闭套接字
    closesocket(sockfd);
}

void multicastEchoServiceTask(void *argument)
{
    int32_t port = (int32_t)argument;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // 让sockfd有一个固定的IP端口
    struct sockaddr_in my_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)};

    bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));

    // 224.0.0.1 ~ 239.255.255.254 任意一个IP地址 都代表一个多播组
    // 加入到多播组 224.0.0.252中
    struct ip_mreq mreq = {
        .imr_multiaddr.s_addr = inet_addr("224.0.0.252"),
        .imr_interface.s_addr = htonl(INADDR_ANY)};

    setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    struct sockaddr_in from_addr = {0};
    socklen_t fromLen = sizeof(from_addr);
    char buf[512] = {0};

    while (1)
    {

        int len = recvfrom(sockfd, buf, sizeof(buf), 0,
                           (struct sockaddr *)&from_addr, &fromLen);
        if (len <= 0)
        {
            if ((errno == EAGAIN ||      // 套接字已标记为非阻塞，而接收操作被阻塞或者接收超时
                 errno == EWOULDBLOCK || // 发送时套接字发送缓冲区已满，或接收时套接字接收缓冲区为空
                 errno == EINTR))        // 操作被信号中断
            {
                ulog_w(TAG, "multicast, 接收超时...........");
                continue;
            }

            ulog_e(TAG, "multicast, 遇到错误, 退出 socket: %d, len: %d", sockfd, len);
            break;
        }

        ulog_i(TAG, "multicast, 消息来自: %s, port: %hu", inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));
        ulog_i(TAG, "multicast, len: %d, msg: %s", len, buf);

        // socket加入多播组后，sendto消息只能发送给多播组，这是w5500硬件限制的，如果想单播回复组播收到的信息，需要重新创建socket
        // sendto(sockfd, "hellow", strlen("hellow"), 0, (struct sockaddr *)&from_addr, sizeof(from_addr));

        int sockfd2 = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in ser_addr = {.sin_family = AF_INET,
                                       .sin_port = from_addr.sin_port,
                                       .sin_addr.s_addr = from_addr.sin_addr.s_addr};
        sendto(sockfd2, buf, len, 0, (struct sockaddr *)&ser_addr, sizeof(ser_addr));
        closesocket(sockfd2); // 关闭套接字

        memset(buf, 0, len);
    }

    closesocket(sockfd);
}

static int w5500Static(int argc, char *argv[])
{
    // 测试netDev
    netdev_dhcp_enabled(RyanNetdev, RT_FALSE);

    //  设置网卡 IP 地址
    uint32_t addr = inet_addr("192.168.3.69");
    netdev_set_ipaddr(RyanNetdev, (const ip_addr_t *)&addr);

    addr = inet_addr("192.168.1.1");
    //  设置网卡网关地址
    netdev_set_gw(RyanNetdev, (const ip_addr_t *)&addr);

    addr = inet_addr("255.255.252.0");
    //  设置网卡子网掩码地址
    netdev_set_netmask(RyanNetdev, (const ip_addr_t *)&addr);

    addr = inet_addr("114.114.114.114");
    //  设置网卡子网掩码地址
    netdev_set_dns_server(RyanNetdev, 0, (const ip_addr_t *)&addr);
    ulog_w(TAG, "w5500Static");
    return 0;
}

static int w5500Dhcp(int argc, char *argv[])
{
    netdev_dhcp_enabled(RyanNetdev, RT_TRUE);
    ulog_w(TAG, "w5500Dhcp");
    return 0;
}

static int w5500UdpClient(int argc, char *argv[])
{
    if (argc < 4)
    {
        ulog_w(TAG, "请输入udp服务器的IP, port ");
        return 0;
    }

    char *serviceIP = argv[2];
    int32_t servicePort = atoi(argv[3]);

    // 创建通讯的udp套接字(没有port， ip)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    ulog_i(TAG, "UDP客户端套接字sockfd: %d", sockfd);

    // 定义一个IPv4地址结构, 存放服务器的地址信息(目标主机)
    struct sockaddr_in ser_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(servicePort),         // 将主机字节序转换成网络字节序
        .sin_addr.s_addr = inet_addr(serviceIP) // 将服务器ip地址转换为32位整型数据
    };

    char buf[] = "This is a udp client test message";
    sendto(sockfd, buf, strlen(buf),
           0, (struct sockaddr *)&ser_addr, sizeof(ser_addr));

    // 关闭套接字
    closesocket(sockfd);
    return 0;
}

static int w5500UdpService(int argc, char *argv[])
{
    if (argc < 3)
    {
        ulog_w(TAG, "请输入udpService的port ");
        return 0;
    }

    int32_t port = atoi(argv[2]);
    static rt_thread_t hid = NULL;
    if (NULL != hid)
    {
        ulog_w(TAG, "udp服务器已启动, 请勿重复创建");
        return -1;
    }

    // 创建WIZnet SPI RX线程
    hid = rt_thread_create("udpService",       // 线程name
                           udpEchoServiceTask, // 线程入口函数
                           (void *)port,       // 线程入口函数参数
                           2048,               // 线程栈大小
                           18,                 // 线程优先级
                           5);                 // 线程时间片

    if (NULL == hid)
    {
        ulog_w(TAG, "创建udp echo线程失败");
        return -1;
    }

    rt_thread_startup(hid);
    ulog_i(TAG, "udp echo服务器启动成功 service: %s, port: %d", inet_ntoa(RyanNetdev->ip_addr), port);

    return 0;
}

static int w5500TcpClient(int argc, char *argv[])
{
    if (argc < 4)
    {
        ulog_w(TAG, "请输入tcp服务器的IP, port ");
        return 0;
    }

    char *serviceIP = argv[2];
    int32_t servicePort = atoi(argv[3]);

    int32_t result = 0;

    // 创建一个TCP套接字 SOCK_STREAM
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ulog_i(TAG, "TCP客户端套接字sockfd: %d", sockfd);

    // bind是可选的,这里使用，纯粹为了演示
    // !此库w5500实现, 不推荐使用bind，使用bind会释放之前申请socket，重新申请。这是因为w5500特性造成
    struct sockaddr_in my_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(45876),
        .sin_addr.s_addr = htonl(INADDR_ANY)};
    bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr));

    // connect链接服务器
    struct sockaddr_in ser_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(servicePort),         // 服务器的端口
        .sin_addr.s_addr = inet_addr(serviceIP) // 服务器的IP
    };

    // 如果sockfd没有绑定固定的IP以及端口
    // 正常情况，在调用connect时候 系统给sockfd分配自身IP以及随机端口
    // 堆区此库W5500实现，是在socket时进行绑定的
    result = connect(sockfd, (struct sockaddr *)&ser_addr, sizeof(ser_addr));
    if (0 != result)
    {
        ulog_i(TAG, "connect错误, 目标ip: %s, 目标端口: %d, err code: %s", serviceIP, servicePort, strerror(errno));
        return -1;
    }

    char buf[] = "This is a tdp client test message";

    result = send(sockfd, buf, strlen(buf), 0);
    if (result < 0)
    {
        ulog_i(TAG, "send错误, 目标ip: %s, 目标端口: %s, err code: %s", serviceIP, servicePort, strerror(errno));
        return -1;
    }

    // 关闭套接字
    closesocket(sockfd);
    return 0;
}

/**
 * @brief
 * !注意: 由于W5500一个socket只能listen一个连接
 * !RyanW5500库实现的listen多连接，原有服务器套接字不使用，
 * !accept时会保证服务器socket链表中有一个套接字进行listen，当有客户端连接时，返回此套接字
 *
 * @param argc
 * @param argv
 * @return int
 */
static int w5500tcpService(int argc, char *argv[])
{
    if (argc < 3)
    {
        ulog_w(TAG, "请输入tcpService的port ");
        return 0;
    }

    int32_t port = atoi(argv[2]);

    static rt_thread_t hid = NULL;
    if (NULL != hid)
    {
        ulog_w(TAG, "tcp服务器已启动, 请勿重复创建");
        return -1;
    }

    // 创建WIZnet SPI RX线程
    hid = rt_thread_create("tcpService", // 线程name
                           tcpEchoTask,  // 线程入口函数
                           (void *)port, // 线程入口函数参数
                           2048,         // 线程栈大小
                           16,           // 线程优先级
                           5);           // 线程时间片

    if (NULL == hid)
    {
        ulog_w(TAG, "创建tcp echo线程失败");
        return -1;
    }

    rt_thread_startup(hid);
    ulog_i(TAG, "tcp echo服务器启动成功 service: %s, port: %d", inet_ntoa(RyanNetdev->ip_addr), port);

    return 0;
}

static int w5500Broadcast(int argc, char *argv[])
{

    if (argc < 4)
    {
        ulog_w(TAG, "请输入broadcast发送的port和消息内容 ");
        return 0;
    }

    int32_t port = atoi(argv[2]);
    char *msg = argv[3];

    // udp支持广播
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // 让sockfd支持广播
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    // 发送广播地址（目的地址 是广播地址）
    struct sockaddr_in dst_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr("255.255.255.255")};

    sendto(sockfd, msg, strlen(msg), 0,
           (struct sockaddr *)&dst_addr, sizeof(dst_addr));

    closesocket(sockfd);

    ulog_i(TAG, "broadcast发送成功");
    return 0;
}

/**
 * @brief
 * !注意：RyanW5500 socket组播实现不支持加入多个组播组，这是由W5500硬件限制的.
 * !这和tcp服务器一样,虽然可以像tcp listen一样实现多组播，但考虑多组播功能并不常用，且实现较为复杂且占资源，暂时没有实现多组播
 * !目前如果需要加入多个组播的话，就申请多个socket分别加入组播组吧
 * !socket加入多播组后，sendto消息只能发送给多播组，这是w5500硬件限制的，如果想单播回复组播收到的信息，需要重新创建socket
 *
 * @param argc
 * @param argv
 * @return int
 */
static int w5500Multicast(int argc, char *argv[])
{

    if (argc < 3)
    {
        ulog_w(TAG, "请输入multicast发送的port ");
        return 0;
    }

    int32_t port = atoi(argv[2]);

    static rt_thread_t hid = NULL;
    if (NULL != hid)
    {
        ulog_w(TAG, "组播echo服务器已启动, 请勿重复创建");
        return -1;
    }

    // 创建WIZnet SPI RX线程
    hid = rt_thread_create("multicast",              // 线程name
                           multicastEchoServiceTask, // 线程入口函数
                           (void *)port,             // 线程入口函数参数
                           2048,                     // 线程栈大小
                           19,                       // 线程优先级
                           5);                       // 线程时间片

    if (NULL == hid)
    {
        ulog_w(TAG, "创建multicast echo线程失败");
        return -1;
    }

    rt_thread_startup(hid);
    ulog_i(TAG, "multicast echo服务器启动成功");
    ulog_i(TAG, "multicast 地址: %s, port: %d", "224.0.0.252", port);
    return 0;
}

static int w5500dhcpLeasetime(int argc, char *argv[])
{
    if (RT_TRUE != netdev_is_dhcp_enabled(RyanNetdev))
    {
        ulog_w(TAG, "dhcp服务未启动, 目前处于静态ip状态");
        return 0;
    }

    ulog_i(TAG, "租期总时长：%d s, 剩余时长: %d s", getDHCPLeaseTime() / 1000, getDHCPRemainLeaseTime() / 1000);
    return 0;
}

static int w5500GetNetInfo(int argc, char *argv[])
{
    uint8_t tmpstr[6] = {0};
    wiz_NetInfo netinfo = {0};
    ctlwizchip(CW_GET_ID, (void *)tmpstr);
    ctlnetwork(CN_GET_NETINFO, (void *)&netinfo); // 获取网络信息

    if (NETINFO_DHCP == netinfo.dhcp)
        ulog_i(TAG, "=== %s NET CONF : DHCP ===", (char *)tmpstr);
    else
        ulog_i(TAG, "=== %s NET CONF : Static ===", (char *)tmpstr);

    ulog_i(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X", netinfo.mac[0], netinfo.mac[1], netinfo.mac[2],
           netinfo.mac[3], netinfo.mac[4], netinfo.mac[5]);
    ulog_i(TAG, "SIP: %d.%d.%d.%d", netinfo.ip[0], netinfo.ip[1], netinfo.ip[2], netinfo.ip[3]);
    ulog_i(TAG, "GAR: %d.%d.%d.%d", netinfo.gw[0], netinfo.gw[1], netinfo.gw[2], netinfo.gw[3]);
    ulog_i(TAG, "SUB: %d.%d.%d.%d", netinfo.sn[0], netinfo.sn[1], netinfo.sn[2], netinfo.sn[3]);
    ulog_i(TAG, "DNS: %d.%d.%d.%d", netinfo.dns[0], netinfo.dns[1], netinfo.dns[2], netinfo.dns[3]);
    ulog_i(TAG, "===========================");

    return 0;
}

static int w5500GetHostByName(int argc, char *argv[])
{
    if (argc < 4)
    {
        ulog_w(TAG, "请版本、带解析的域名信息。 版本1使用线程安全版本, 0非线程安全版本");
        return 0;
    }

    uint8_t choice = atoi(argv[2]);
    char *nameStr = argv[3];

    if (0 == choice)
    {
        struct hostent *hent;
        hent = gethostbyname(nameStr);

        if (NULL == hent)
        {
            ulog_e(TAG, "gethostbyname error for hostname: %s", nameStr);
            return 0;
        }

        ulog_i(TAG, "name: %s, addrtype: %d, AF_INET: %d, len:%d",
               hent->h_name, hent->h_addrtype, AF_INET,
               hent->h_length);

        for (uint8_t i = 0; hent->h_aliases[i]; i++)
            ulog_i(TAG, "alias hostname: %s", hent->h_aliases[i]);

        for (uint8_t i = 0; hent->h_addr_list[i]; i++)
            ulog_i(TAG, "host addr is: %s", inet_ntoa(*(struct in_addr *)hent->h_addr_list[i]));
    }

    else
    {
        char buf[512];
        int ret;
        struct hostent hostinfo, *phost;

        if (0 != gethostbyname_r(nameStr, &hostinfo, buf, sizeof(buf), &phost, &ret))
        {
            ulog_e(TAG, "gethostbyname: %s, ret:%d", nameStr, ret);
            return 0;
        }

        ulog_i(TAG, "name: %s, addrtype: %d, AF_INET: %d, len: %d",
               phost->h_name, phost->h_addrtype, AF_INET,
               phost->h_length);

        for (uint8_t i = 0; hostinfo.h_aliases[i]; i++)
            ulog_i(TAG, "alias hostname: %s", hostinfo.h_aliases[i]);

        for (uint8_t i = 0; hostinfo.h_addr_list[i]; i++)
            ulog_i(TAG, "host addr is: %s", inet_ntoa(*((struct in_addr *)hostinfo.h_addr_list[i])));
    }

    return 0;
}

static int w5500GetAddrInfo(int argc, char *argv[])
{
    if (argc < 4)
    {
        ulog_w(TAG, "请输入要解析的域名和端口");
        return 0;
    }

    char *nameStr = argv[2];
    char *namePort = argv[3];

    struct addrinfo *addrList = NULL,
                    *aip;

    struct addrinfo hints = {0};

    int result = getaddrinfo(nameStr, namePort, &hints, &addrList);
    if (0 != result)
    {
        ulog_e(TAG, "getaddrinfo: %s ret:%d", nameStr, result);
        return 0;
    }

    struct sockaddr_in *sinp;
    const char *addr;
    char buf[40];

    for (aip = addrList; aip != NULL; aip = aip->ai_next)
    {
        sinp = (struct sockaddr_in *)aip->ai_addr;
        addr = inet_ntop(AF_INET, &sinp->sin_addr, buf, sizeof(buf));
        ulog_i(TAG, "addr: %s, port: %d", addr ? addr : "unknow ", ntohs(sinp->sin_port));
    }

    if (NULL != addrList)
        freeaddrinfo(addrList);

    return 0;
}

/**
 * @brief mqtt msh命令
 *
 */
struct RyanMqttCmdDes
{
    const char *cmd;
    const char *explain;
    int (*fun)(int argc, char *argv[]);
};

static int w5500Help(int argc, char *argv[]);

static struct RyanMqttCmdDes cmdTab[] = {
    {"help", "打印帮助信息", w5500Help},
    {"start", "启动RyanW5500", w5500Start},
    {"static", "netdev设置w5500静态地址,如果触发了ip变化,会关闭所有已连接socket", w5500Static},
    {"dhcp", "netdev设置w5500 dhcp,如果触发了ip变化,会关闭所有已连接socket", w5500Dhcp},
    {"udpClient", "w5500 udp客户端 param: ip, port", w5500UdpClient},
    {"udpService", "w5500 udp echo服务器 param: port", w5500UdpService},
    {"tcpClient", "w5500 tcp客户端 param: ip, port", w5500TcpClient},
    {"tcpService", "w5500 tcp 多线程echo服务器 param: port", w5500tcpService},
    {"broadcast", "w5500 广播 param: port, msg", w5500Broadcast},
    {"multicast", "w5500 多播 echo服务器 param: port", w5500Multicast},

    {"dhcpLease", "w5500 获取dhcp租期和剩余时间", w5500dhcpLeasetime},
    {"netInfo", "w5500 获取芯片内部配置信息", w5500GetNetInfo},
    {"gethostbyname", "w5500 根据域名解析地址信息", w5500GetHostByName},
    {"getaddrinfo", "w5500 根据域名解析地址信息", w5500GetAddrInfo}

};

static int w5500Help(int argc, char *argv[])
{

    for (uint8_t i = 0; i < sizeof(cmdTab) / sizeof(cmdTab[0]); i++)
        rt_kprintf("w5500 %-16s %s\r\n", cmdTab[i].cmd, cmdTab[i].explain);

    return 0;
}

static int RyanMqttMsh(int argc, char *argv[])
{
    int32_t i = 0,
            result = 0;
    struct RyanMqttCmdDes *runCmd = NULL;

    if (argc == 1)
    {
        w5500Help(argc, argv);
        return 0;
    }

    for (i = 0; i < sizeof(cmdTab) / sizeof(cmdTab[0]); i++)
    {
        if (rt_strcmp(cmdTab[i].cmd, argv[1]) == 0)
        {
            runCmd = &cmdTab[i];
            break;
        }
    }

    if (runCmd == NULL)
    {
        w5500Help(argc, argv);
        return 0;
    }

    if (runCmd->fun != NULL)
        result = runCmd->fun(argc, argv);

    return result;
}

// stm32用户需要更改此代码为自己w5500实际挂载的spi总线
// 非stm32用户可以调用rt_spi_bus_attach_device，
// 参考连接:https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/spi/spi?id=%e6%8c%82%e8%bd%bd-spi-%e8%ae%be%e5%a4%87
static int RyanW5500SpiArrach(void)
{
    rt_err_t result = rt_hw_spi_device_attach("spi2", RYANW5500_SPI_DEVICE, GPIOE, GPIO_PIN_15);
    if (RT_EOK != result)
        rt_kprintf("RyanW5500 SPI init fail!!!!!");

    return result;
}
INIT_DEVICE_EXPORT(RyanW5500SpiArrach); // spi总线挂载

#if defined(RT_USING_MSH)
MSH_CMD_EXPORT_ALIAS(RyanMqttMsh, w5500, RyanMqtt command);
#endif

#endif
