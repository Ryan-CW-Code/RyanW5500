#define rlogEnable                 // 是否使能日志
#define rlogColorEnable            // 是否使能日志颜色
#define rlogLevel (rlogLvlWarning) // 日志打印等级
#define rlogTag "W5500"            // 日志tag

#include "RyanW5500Store.h"

/**
 * @brief W5500中断回调函数
 *
 * @param argument
 */
void RyanW5500IRQCallback(void *argument)
{
    rt_event_send(RyanW5500Entry.W5500EventHandle, RyanW5500IRQBit);
}

void RyanW5500NetDevInfoUpdate(struct netdev *netdev)
{
    uint8_t linkState = 0;
    wiz_NetInfo netinfo = {0};
    ctlnetwork(CN_GET_NETINFO, (void *)&netinfo);                                // 获取网络信息
    netdev_low_level_set_ipaddr(netdev, (const ip_addr_t *)&netinfo.ip);         // 设置网络接口设备 IP 地址。
    netdev_low_level_set_gw(netdev, (const ip_addr_t *)&netinfo.gw);             // 设置网络接口设备网关地址
    netdev_low_level_set_netmask(netdev, (const ip_addr_t *)&netinfo.sn);        // 设置网络接口设备网络掩码地址。
    netdev_low_level_set_dns_server(netdev, 0, (const ip_addr_t *)&netinfo.dns); // 设置网络接口设备DNS服务器地址
    memcpy(netdev->hwaddr, (const void *)&netinfo.mac, netdev->hwaddr_len);      // 设置mac地址

    // netdev_low_level_set_dhcp_status(netdev, (NETINFO_DHCP == gWIZNETINFO.dhcp) ? RT_TRUE : RT_FALSE);

    ctlwizchip(CW_GET_PHYLINK, &linkState);
    netdev_low_level_set_link_status(netdev, (PHY_LINK_ON == linkState) ? RT_TRUE : RT_FALSE);
}

/**
 * @brief 网络初始化
 *
 * @param netdev
 * @return int
 */
int RyanW5500NetWorkInit(struct netdev *netdev)
{

    uint8_t MaintainFlag = 0; // dhcp续租标志
    uint8_t linkState = 0;

    if (0 != RyanW5500Entry.netDevFlag)
    {
        // nedev用户手动设置了ip / gw / mask / dnsService
        if (RyanW5500Entry.netDevFlag & netDevSetDevInfo)
        {
            RyanW5500Entry.netDevFlag &= ~netDevSetDevInfo;
            for (uint8_t socket = 0; socket < RyanW5500MaxSocketNum; socket++)
                wiz_closesocket(socket);
        }

        // nedev用户使能了dhcp
        if (RyanW5500Entry.netDevFlag & netDevDHCPEnable)
        {
            RyanW5500Entry.netDevFlag &= ~netDevDHCPEnable;

            MaintainFlag = 0;
            gWIZNETINFO.dhcp = NETINFO_DHCP;

            // 模式不一样，关闭所有socket
            if (gWIZNETINFO.dhcp != NETINFO_DHCP)
            {
                for (uint8_t socket = 0; socket < RyanW5500MaxSocketNum; socket++)
                    wiz_closesocket(socket);
            }
        }

        // nedev用户失能了dhcp
        if (RyanW5500Entry.netDevFlag & netDevDHCPDisable)
        {
            RyanW5500Entry.netDevFlag &= ~netDevDHCPDisable;

            MaintainFlag = 0;
            gWIZNETINFO.dhcp = NETINFO_STATIC;

            // ip可能不一样，直接关闭所有socket
            for (uint8_t socket = 0; socket < RyanW5500MaxSocketNum; socket++)
                wiz_closesocket(socket);
        }
    }
    else
    {

        // dhcp租期判断
        if (NETINFO_DHCP == gWIZNETINFO.dhcp)
        {
            if (getDHCPRemainLeaseTime() < 10 * 1000) // 如果租期只剩余10秒，重新获取ip
            {
                rlog_i("dhcp租期接近超时, 重新获取ip");
                MaintainFlag = 0;
                goto next;
            }

            if (getDHCPRemainLeaseTime() < (getDHCPLeaseTime() / 2)) // 超过一半就开始续租
            {
                rlog_i("dhcp续租");
                MaintainFlag = 1;
                goto next;
            }
        }

        ctlwizchip(CW_GET_PHYLINK, &linkState);
        if (PHY_LINK_ON == linkState && netdev_is_link_up(netdev)) // netdev状态和设备状态匹配,不进行下面操作
            return 0;

        if (PHY_LINK_ON == linkState) // w5500处于link状态，更新信息后就退出
        {
            rlog_d("link State: %d\r\n", linkState);
            RyanW5500NetDevInfoUpdate(netdev);
            return 0;
        }
    }

next:
    if (NETINFO_DHCP == gWIZNETINFO.dhcp)
    {
        if (0 == MaintainFlag)
        {
            memset(gWIZNETINFO.ip, 0, 4);
            memset(gWIZNETINFO.sn, 0, 4);
            memset(gWIZNETINFO.gw, 0, 4);
            memset(gWIZNETINFO.dns, 0, 4);
        }

        uint32_t dhcpState = DHCP_run(MaintainFlag); // MaintainFlag续租
        if (dhcpState != 0)
        {
            ctlnetwork(CN_SET_NETINFO, (void *)&gWIZNETINFO); // 从网络信息结构设置网络信息
            RyanW5500NetDevInfoUpdate(netdev);
            return -1;
        }
    }

    ctlnetwork(CN_SET_NETINFO, (void *)&gWIZNETINFO); // 从网络信息结构设置网络信息
    for (uint8_t count = 0; count < 100; count++)     // 测试link是否正常
    {
        ctlwizchip(CW_GET_PHYLINK, &linkState);
        if (PHY_LINK_ON == linkState)
            break;
        delay(10);
    }

    RyanW5500NetDevInfoUpdate(netdev);

    uint16_t ret = 0xff;     // 启用所有socket通道中断
    ret = (ret << 8) + 0x00; // 禁用所有通用中断，不使用
    ctlwizchip(CW_SET_INTRMASK, (void *)&ret);

    for (uint8_t i = 0; i < RyanW5500MaxSocketNum; i++) // 每次联网成功都进行socket中断掩码配置
        setSn_IMR(i, RyanW5500SnIMR);

    return 0;
}

static void wizIntDataTask(void *parameter)
{
    uint8_t ir = 0,
            sir = 0,
            sn_ir = 0;
    uint16_t intr = 0;
    struct netdev *netdev = (struct netdev *)parameter;
    platformTimer_t netWorkTimer = {0};

    // 检查w5500连接是否正常
    while (1)
    {

        RyanW5500Reset(); // 重启w5500
        delay(100);

        // 超时中断触发为retry_cnt * time_100us * 100us
        struct wiz_NetTimeout_t net_timeout = {
            .retry_cnt = 5,      // 重试次数
            .time_100us = 2000}; // 200ms认为失败
        ctlnetwork(CN_SET_TIMEOUT, (void *)&net_timeout);

        memset(&net_timeout, 0, sizeof(struct wiz_NetTimeout_t));
        ctlnetwork(CN_GET_TIMEOUT, (void *)&net_timeout);
        if (5 == net_timeout.retry_cnt && 2000 == net_timeout.time_100us)
            break;

        rlog_e("没有监测到w5500");
        delay(2000);
    }
    netdev_low_level_set_status(netdev, RT_TRUE); // 设置网络接口设备状态

    platformTimerCutdown(&netWorkTimer, 0);

    while (1)
    {

        if (0 == platformTimerRemain(&netWorkTimer))
        {
            if (-1 == RyanW5500NetWorkInit(netdev))
            {
                rlog_d("网络没有连接");
                delay(1000);
                continue;
            }

            platformTimerCutdown(&netWorkTimer, 1000);
        }

        rt_event_recv(RyanW5500Entry.W5500EventHandle, RyanW5500IRQBit,
                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      1000, NULL);

        while (1)
        {
            intr = 0;
            ctlwizchip(CW_GET_INTERRUPT, (void *)&intr); // 获取中断状态
            ir = (uint8_t)intr;
            sir = (uint8_t)(intr >> 8);
            setIR(ir); // 没有使用IR中断，保险起见进行清空

            if (0 == sir) // 所有socket都没有中断就退出循环
                break;

            for (uint8_t socket = 0; socket < RyanW5500MaxSocketNum; socket++)
            {
                if (!(sir & (1 << socket))) // 判断是否当前socket通道中断
                    continue;

                // setSIR(socket); // 清除当前socket中断  setSn_IR时w5500内部会自动清除
                // Sn_IR_SENDOK和Sn_IR_TIMEOUT wiz官方库有使用，这里不使用
                sn_ir = 0;
                sn_ir = getSn_IR(socket);         // 获取中断类型消息
                setSn_IR(socket, RyanW5500SnIMR); // 清除中断类型消息

                if (sn_ir & Sn_IR_RECV) // 接收到了对方数据
                {
                    RyanW5500RecvDataCallback(socket);
                    rlog_d("接收到数据");
                }

                if (sn_ir & Sn_IR_DISCON) // 当接收到对方的 FIN or FIN/ACK 包时
                {
                    RyanW5500CloseCallback(socket);
                    rlog_d("断开连接");
                }

                if (sn_ir & Sn_IR_CON) // 成功与对方建立连接
                {
                    rlog_d("连接成功");
                    RyanW5500Socket *clientSock = RyanW5500GetSock(socket);
                    if (-1 == clientSock->serviceSocket)
                        continue;

                    RyanW5500Socket *serviceSock = RyanW5500GetSock(clientSock->serviceSocket);
                    if (RyanW5500SocketListen != serviceSock->state || clientSock->port != serviceSock->port)
                        continue;

                    RyanListenServiceAddClient(serviceSock, clientSock);
                }
            }
        }
    }
}

/**
 * @brief 初始化W5500 不应重复调用,内部没有判断
 *
 * @param netInfo
 * @return int
 */
int RyanW5500Init(wiz_NetInfo *netInfo)
{
    struct netdev *netdev = NULL;
    memcpy(&gWIZNETINFO, netInfo, sizeof(wiz_NetInfo));
    memset(&RyanW5500Entry, 0, sizeof(RyanW5500Entry));

    RyanW5500SpiInit();                                                     // w5500 spi初始化
    reg_wizchip_cris_cbfunc(RyanW5500CriticalEnter, RyanW5500CriticalExit); // 注册临界区函数
    reg_wizchip_cs_cbfunc(RyanW5500CsSelect, RyanW5500CsDeselect);          // 注册片选函数
    reg_wizchip_spi_cbfunc(RyanW5500ReadByte, RyanW5500WriteByte);          // 注册读写函数
    reg_wizchip_spiburst_cbfunc(RyanW5500ReadBurst, RyanW5500WriteBurst);   // 注册多个字节读写

    RyanW5500Entry.socketMutexHandle = rt_mutex_create("RyanW5500SocketMutex", RT_IPC_FLAG_FIFO);
    RyanW5500Entry.dnsMutexHandle = rt_mutex_create("RyanW5500DnsMutex", RT_IPC_FLAG_FIFO);
    RyanW5500Entry.W5500EventHandle = rt_event_create("RyanW5500Event", RT_IPC_FLAG_PRIO);
    RyanW5500AttachIRQ(RyanW5500IRQCallback); // 绑定w5500中断回调函数

    netdev = RyanW5500NetdevRegister("RyanW5500");                                                     // W5500
    netdev_low_level_set_status(netdev, RT_FALSE);                                                     // 设置网络接口设备状态
    netdev_low_level_set_dhcp_status(netdev, (NETINFO_DHCP == gWIZNETINFO.dhcp) ? RT_TRUE : RT_FALSE); // 同步dhcp状态

    RyanW5500Entry.w5500TaskHandle = rt_thread_create("RyanW5500",    // 线程name
                                                      wizIntDataTask, // 线程入口函数
                                                      (void *)netdev, // 线程入口函数参数
                                                      1536,           // 线程栈大小
                                                      8,              // 线程优先级
                                                      5);             // 线程时间片

    if (RyanW5500Entry.w5500TaskHandle != NULL)
        rt_thread_startup(RyanW5500Entry.w5500TaskHandle);

    return 0;
}
