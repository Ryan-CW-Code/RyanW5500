
#include "RyanW5500Store.h"

/**
 * @brief link up
 *
 * @param netdev
 * @return int
 */
static int RyanW5500NetdevSetUp(struct netdev *netdev)
{
    netdev_low_level_set_status(netdev, RT_TRUE);
    return RT_EOK;
}

/**
 * @brief link down
 *
 * @param netdev
 * @return int
 */
static int RyanW5500NetdevSetDown(struct netdev *netdev)
{
    netdev_low_level_set_status(netdev, RT_FALSE);
    return RT_EOK;
}

/**
 * @brief set addr info
 *
 * @param netdev
 * @param ip_addr
 * @param netmask
 * @param gw
 * @return int
 */
static int RyanW5500NetdevSetAddrInfo(struct netdev *netdev, ip_addr_t *ip_addr, ip_addr_t *netmask, ip_addr_t *gw)
{
    RT_ASSERT(netdev);
    RT_ASSERT(ip_addr || netmask || gw);

    if (ip_addr)
        memcpy(gWIZNETINFO.ip, &ip_addr->addr, sizeof(gWIZNETINFO.ip));

    if (netmask)
        memcpy(gWIZNETINFO.sn, &netmask->addr, sizeof(gWIZNETINFO.sn));

    if (gw)
        memcpy(gWIZNETINFO.gw, &gw->addr, sizeof(gWIZNETINFO.gw));

    RyanW5500Entry.netDevFlag |= netDevSetDevInfo;
    netdev_low_level_set_link_status(netdev, RT_FALSE);
    return RT_EOK;
}

/**
 * @brief set dns server
 *
 * @param netdev
 * @param dns_num
 * @param dns_server
 * @return int
 */
static int RyanW5500NetdevSetDnsServer(struct netdev *netdev, uint8_t dns_num, ip_addr_t *dns_server)
{
    RT_ASSERT(netdev);
    RT_ASSERT(dns_server);
    if (0 != dns_num)
        return -RT_ERROR;

    RyanW5500Entry.netDevFlag |= netDevSetDevInfo;
    memcpy(gWIZNETINFO.dns, &dns_server->addr, sizeof(gWIZNETINFO.dns));
    netdev_low_level_set_link_status(netdev, RT_FALSE);
    return RT_EOK;
}

/**
 * @brief set dhcp
 *
 * @param netdev
 * @param is_enabled
 * @return int
 */
static int RyanW5500NetdevSetDhcp(struct netdev *netdev, rt_bool_t is_enabled)
{

    RyanW5500Entry.netDevFlag |= netDevDHCP;

    netdev_low_level_set_dhcp_status(netdev, is_enabled);
    netdev_low_level_set_link_status(netdev, RT_FALSE);
    return RT_EOK;
}

/**
 * @brief ping
 *
 * @param netdev
 * @param host
 * @param data_len
 * @param timeout
 * @param ping_resp
 * @return int
 */
static int RyanW5500NetdevPing(struct netdev *netdev, const char *host, size_t data_len, uint32_t timeout, struct netdev_ping_resp *ping_resp)
{
    RT_ASSERT(netdev);
    RT_ASSERT(host);
    RT_ASSERT(ping_resp);

    return RyanW5500Ping(netdev, host, data_len, timeout, ping_resp);
}

/**
 * @brief 用于网卡网络连接信息和端口使用情况
 *
 * @param netdev
 * @return int
 */
static int RyanW5500NetdevNetstat(struct netdev *netdev)
{
    return 0;
}

// netdev设备操作
const struct netdev_ops wiz_netdev_ops =
    {
        .set_up = RyanW5500NetdevSetUp,
        .set_down = RyanW5500NetdevSetDown,
        .set_addr_info = RyanW5500NetdevSetAddrInfo,
        .set_dns_server = RyanW5500NetdevSetDnsServer,
        .set_dhcp = RyanW5500NetdevSetDhcp,
#ifdef RT_USING_FINSH
        .ping = RyanW5500NetdevPing,
        .netstat = RyanW5500NetdevNetstat,
#endif
};

/**
 * @brief socket操作
 *
 */
static struct sal_socket_ops RyanW5500SocketOps =
    {
        .socket = wiz_socket,
        .closesocket = wiz_closesocket,
        .bind = wiz_bind,
        .listen = wiz_listen,
        .connect = wiz_connect,
        .accept = wiz_accept,
        .sendto = wiz_sendto,
        .recvfrom = wiz_recvfrom,
        .getsockopt = wiz_getsockopt,
        .setsockopt = wiz_setsockopt,
        .shutdown = wiz_shutdown,
        .getpeername = NULL,
        .getsockname = NULL,
        .ioctlsocket = NULL,
        // #ifdef SAL_USING_POSIX
        //         wiz_poll,
        // #endif /* SAL_USING_POSIX */
};

/**
 * @brief sal 网络数据库名称解析
 *
 */
static const struct sal_netdb_ops RyanW5500NetdbOps =
    {
        .gethostbyname = wiz_gethostbyname,
        .gethostbyname_r = wiz_gethostbyname_r,
        .getaddrinfo = wiz_getaddrinfo,
        .freeaddrinfo = wiz_freeaddrinfo,
};

/**
 * @brief RyanW5500支持的协议族
 *
 */
static const struct sal_proto_family RyanW5500Family =
    {
        .family = AF_WIZ,
        .sec_family = AF_INET,
        .skt_ops = &RyanW5500SocketOps,
        .netdb_ops = &RyanW5500NetdbOps,
};

/**
 * @brief RyanW5500注册到netdev
 *
 * @param netdev_name
 * @return struct netdev*
 */
struct netdev *RyanW5500NetdevRegister(char *netdev_name)
{
    struct netdev *netdev = NULL;

    netdev = (struct netdev *)malloc(sizeof(struct netdev));
    if (NULL == netdev)
        return NULL;
    memset(netdev, 0, sizeof(struct netdev));

    netdev->flags = 0;                                // 网络接口设备状态标志
    netdev->mtu = 1460;                               // 最大传输单位 (以字节为单位)
    netdev->hwaddr_len = 6;                           // 硬件地址长度 mac地址
    netdev->ops = &wiz_netdev_ops;                    // 网卡操作回调函数
    netdev->sal_user_data = (void *)&RyanW5500Family; // sal协议族相关参数

    if (0 != netdev_register(netdev, netdev_name, NULL))
    {
        free(netdev);
        return NULL;
    }

    return netdev;
}
