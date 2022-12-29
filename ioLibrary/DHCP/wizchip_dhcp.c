
#include "wizchip_socket.h"
#include "wizchip_dhcp.h"

#define DBG_ENABLE

#define DBG_SECTION_NAME "dhcp"
#define DBG_LEVEL DBG_WARNING
#define DBG_COLOR

#include <stdio.h>
#include <string.h>
#include "RyanW5500Store.h"

/*DHCP 状态机。*/
typedef enum
{
    STATE_DHCP_INIT = 0,      // 初始化
    STATE_DHCP_DISCOVER = 1,  // 发送 DISCOVER 并等待 OFFER
    STATE_DHCP_REQUEST = 2,   // 发送请求并等待 ACK 或 NACK
    STATE_DHCP_LEASED = 3,    // 接收D ACK和IP租用
    STATE_DHCP_REREQUEST = 4, // 发送维护租用 IP 的请求
    STATE_DHCP_RELEASE = 5,   // 没用
    STATE_DHCP_STOP = 6,      // 停止处理 DHCP
} wizchipDhcpState_e;

#define DHCP_FLAGSBROADCAST 0x8000 //@ref RIP_MSG 中标志的广播值
#define DHCP_FLAGSUNICAST 0x0000   //@ref RIP_MSG 中标志的单播值

/*DHCP 消息 OP 代码*/
#define DHCP_BOOTREQUEST 1 // 在@ref RIP_MSG 的操作中使用的请求消息
#define DHCP_BOOTREPLY 2   // 回复消息使用了@ref RIP_MSG 的 i op

/*DHCP 消息类型*/
#define DHCP_DISCOVER 1 // 在@ref RIP_MSG 的 OPT 中发现消息
#define DHCP_OFFER 2    //@ref RIP_MSG 的 OPT 中的 OFFER 消息
#define DHCP_REQUEST 3  //@ref RIP_MSG 的 OPT 中的请求消息
#define DHCP_DECLINE 4  //@ref RIP_MSG 的 OPT 中的 DECLINE 消息
#define DHCP_ACK 5      //@ref RIP_MSG 的 OPT 中的 ACK 消息
#define DHCP_NAK 6      //@ref RIP_MSG 的 OPT 中的 NACK 消息
#define DHCP_RELEASE 7  // 在@ref RIP_MSG 的 OPT 中释放消息。没用
#define DHCP_INFORM 8   //@ref RIP_MSG 的 OPT 中的 INFORM 消息。没用

#define DHCP_HTYPE10MB 1  // 用于@ref RIP_MSG 类型
#define DHCP_HTYPE100MB 2 // 用于@ref RIP_MSG 类型

#define DHCP_HLENETHERNET 6 // 在@ref RIP_MSG 的 hlen 中使用
#define DHCP_HOPS 0         // 在@ref RIP_MSG 的跃点中使用
#define DHCP_SECS 0         // 在 @ref RIP_MSG 的秒中使用

#define INFINITE_LEASETIME 0xffffffff // 无限租用时间

#define OPT_SIZE 312                  ///@ref RIP_MSG 的最大 OPT 大小
#define RIP_MSG_SIZE (236 + OPT_SIZE) ///@ref RIP_MSG 的最大大小

/**
 *@brief DHCP 选项和值（参见 RFC1533）
 *
 */
enum
{
    padOption = 0,
    subnetMask = 1,
    timerOffset = 2,
    routersOnSubnet = 3,
    timeServer = 4,
    nameServer = 5,
    dns = 6,
    logServer = 7,
    cookieServer = 8,
    lprServer = 9,
    impressServer = 10,
    resourceLocationServer = 11,
    hostName = 12,
    bootFileSize = 13,
    meritDumpFile = 14,
    domainName = 15,
    swapServer = 16,
    rootPath = 17,
    extentionsPath = 18,
    IPforwarding = 19,
    nonLocalSourceRouting = 20,
    policyFilter = 21,
    maxDgramReasmSize = 22,
    defaultIPTTL = 23,
    pathMTUagingTimeout = 24,
    pathMTUplateauTable = 25,
    ifMTU = 26,
    allSubnetsLocal = 27,
    broadcastAddr = 28,
    performMaskDiscovery = 29,
    maskSupplier = 30,
    performRouterDiscovery = 31,
    routerSolicitationAddr = 32,
    staticRoute = 33,
    trailerEncapsulation = 34,
    arpCacheTimeout = 35,
    ethernetEncapsulation = 36,
    tcpDefaultTTL = 37,
    tcpKeepaliveInterval = 38,
    tcpKeepaliveGarbage = 39,
    nisDomainName = 40,
    nisServers = 41,
    ntpServers = 42,
    vendorSpecificInfo = 43,
    netBIOSnameServer = 44,
    netBIOSdgramDistServer = 45,
    netBIOSnodeType = 46,
    netBIOSscope = 47,
    xFontServer = 48,
    xDisplayManager = 49,
    dhcpRequestedIPaddr = 50,
    dhcpIPaddrLeaseTime = 51,
    dhcpOptionOverload = 52,
    dhcpMessageType = 53,
    dhcpServerIdentifier = 54,
    dhcpParamRequest = 55,
    dhcpMsg = 56,
    dhcpMaxMsgSize = 57,
    dhcpT1value = 58,
    dhcpT2value = 59,
    dhcpClassIdentifier = 60,
    dhcpClientIdentifier = 61,
    endOption = 255
};

/**
 *@brief DHCP消息格式
 */
typedef struct
{
    uint8_t op;            //@ref DHCP_BOOTREQUEST 或 @ref DHCP_BOOTREPLY
    uint8_t htype;         //@ref DHCP_HTYPE10MB 或 @ref DHCP_HTYPE100MB
    uint8_t hlen;          //@ref DHCP_HLENETHERNET
    uint8_t hops;          //@ref DHCP_HOPS
    uint32_t xid;          //@ref DHCP_XID 这会在每个 DHCP 事务中增加一个。
    uint16_t secs;         //@ref DHCP_SECS
    uint16_t flags;        //@ref DHCP_FLAGSBROADCAST 或 @ref DHCP_FLAGSUNICAST
    uint8_t ciaddr[4];     //@ref 向 DHCP 服务器请求 IP
    uint8_t yiaddr[4];     //@ref 从 DHCP 服务器提供的 IP
    uint8_t siaddr[4];     // 没用
    uint8_t giaddr[4];     // 没用
    uint8_t chaddr[16];    // DHCP 客户端 6 字节 MAC 地址。其他填充为零
    uint8_t sname[64];     // 没用
    uint8_t file[128];     // 没用
    uint8_t OPT[OPT_SIZE]; // 选项
} RIP_MSG;

uint8_t DHCP_SOCKET;               // DHCP 的套接字号
uint8_t DHCP_SIP[4];               // DHCP 服务器 IP 地址
uint8_t DHCP_REAL_SIP[4];          // 用于在几个 DHCP 服务器中提取我的 DHCP 服务器
uint8_t OLD_allocated_ip[4] = {0}; // 以前的 IP 地址

wizchipDhcpState_e dhcp_state = STATE_DHCP_INIT; // DHCP 状态
platformTimer_t dhcp_lease_time = {0};           // dhcp租期定时器
uint32_t DHCP_XID;                               // 任何数字
uint8_t *HOST_NAME = DCHP_HOST_NAME;             // 主机名
RIP_MSG *pDHCPMSG;                               // DHCP 处理的缓冲区指针

char NibbleToHex(uint8_t nibble)
{
    nibble &= 0x0F;
    if (nibble <= 9)
        return nibble + '0';
    else
        return nibble + ('A' - 0x0A);
}

/**
 *@brief 生成通用dhcp消息
 *
 */
void makeDHCPMSG(void)
{
    uint8_t bk_mac[6];
    uint8_t *ptmp;
    uint8_t i;
    getSHAR(bk_mac);
    pDHCPMSG->op = DHCP_BOOTREQUEST;
    pDHCPMSG->htype = DHCP_HTYPE10MB;
    pDHCPMSG->hlen = DHCP_HLENETHERNET;
    pDHCPMSG->hops = DHCP_HOPS;

    ptmp = (uint8_t *)(&pDHCPMSG->xid);
    *(ptmp + 0) = (uint8_t)((DHCP_XID & 0xFF000000) >> 24);
    *(ptmp + 1) = (uint8_t)((DHCP_XID & 0x00FF0000) >> 16);
    *(ptmp + 2) = (uint8_t)((DHCP_XID & 0x0000FF00) >> 8);
    *(ptmp + 3) = (uint8_t)((DHCP_XID & 0x000000FF) >> 0);

    pDHCPMSG->secs = DHCP_SECS;
    ptmp = (uint8_t *)(&pDHCPMSG->flags);
    *(ptmp + 0) = (uint8_t)((DHCP_FLAGSBROADCAST & 0xFF00) >> 8);
    *(ptmp + 1) = (uint8_t)((DHCP_FLAGSBROADCAST & 0x00FF) >> 0);

    memset(pDHCPMSG->ciaddr, 0, 4);
    memset(pDHCPMSG->yiaddr, 0, 4);
    memset(pDHCPMSG->siaddr, 0, 4);
    memset(pDHCPMSG->giaddr, 0, 4);

    memcpy(pDHCPMSG->chaddr, gWIZNETINFO.mac, 6);

    for (i = 6; i < 16; i++)
        pDHCPMSG->chaddr[i] = 0;
    for (i = 0; i < 64; i++)
        pDHCPMSG->sname[i] = 0;
    for (i = 0; i < 128; i++)
        pDHCPMSG->file[i] = 0;

    // 魔法饼干
    pDHCPMSG->OPT[0] = (uint8_t)((MAGIC_COOKIE & 0xFF000000) >> 24);
    pDHCPMSG->OPT[1] = (uint8_t)((MAGIC_COOKIE & 0x00FF0000) >> 16);
    pDHCPMSG->OPT[2] = (uint8_t)((MAGIC_COOKIE & 0x0000FF00) >> 8);
    pDHCPMSG->OPT[3] = (uint8_t)(MAGIC_COOKIE & 0x000000FF) >> 0;
}

/*SEND DHCP DISCOVER*/
/**
 *@brief 发送dhcp discover消息
 *
 */
void send_DHCP_DISCOVER(void)
{
    uint16_t i;
    uint8_t ip[4] = {255, 255, 255, 255};
    uint16_t k = 0;

    makeDHCPMSG();

    memset(DHCP_SIP, 0, 4);
    memset(DHCP_REAL_SIP, 0, 4);

    k = 4; // 因为 MAGIC_COOKIE 已经由 makeDHCPMSG() 生成

    // 选项请求参数
    pDHCPMSG->OPT[k++] = dhcpMessageType;
    pDHCPMSG->OPT[k++] = 0x01;
    pDHCPMSG->OPT[k++] = DHCP_DISCOVER;

    // 客户端标识符
    pDHCPMSG->OPT[k++] = dhcpClientIdentifier;
    pDHCPMSG->OPT[k++] = 0x07;
    pDHCPMSG->OPT[k++] = 0x01;

    memcpy(pDHCPMSG->OPT + k, gWIZNETINFO.mac, 6);
    k += 6;

    // 主机名
    pDHCPMSG->OPT[k++] = hostName;
    pDHCPMSG->OPT[k++] = 0; // 填充零长度的主机名
    for (i = 0; HOST_NAME[i] != 0; i++)
        pDHCPMSG->OPT[k++] = HOST_NAME[i];
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[3] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[3]);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[4] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[4]);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[5] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[5]);
    pDHCPMSG->OPT[k - (i + 6 + 1)] = i + 6; // 主机名长度

    pDHCPMSG->OPT[k++] = dhcpParamRequest;
    pDHCPMSG->OPT[k++] = 0x06; // 请求长度
    pDHCPMSG->OPT[k++] = subnetMask;
    pDHCPMSG->OPT[k++] = routersOnSubnet;
    pDHCPMSG->OPT[k++] = dns;
    pDHCPMSG->OPT[k++] = domainName;
    pDHCPMSG->OPT[k++] = dhcpT1value;
    pDHCPMSG->OPT[k++] = dhcpT2value;
    pDHCPMSG->OPT[k++] = endOption;

    for (i = k; i < OPT_SIZE; i++)
        pDHCPMSG->OPT[i] = 0;

    LOG_D("> Send DHCP_DISCOVER");

    // 广播发送
    wizchip_sendto(DHCP_SOCKET, (uint8_t *)pDHCPMSG, RIP_MSG_SIZE, ip, DHCP_SERVER_PORT);
}

/**
 *@brief 发送请求地址租用 request报文
 *
 */
void send_DHCP_REQUEST(void)
{
    int i;
    uint16_t k = 0;
    uint8_t ip[4] = {255, 255, 255, 255};

    makeDHCPMSG();

    if (dhcp_state == STATE_DHCP_LEASED || dhcp_state == STATE_DHCP_REREQUEST)
    {
        *((uint8_t *)(&pDHCPMSG->flags)) = ((DHCP_FLAGSUNICAST & 0xFF00) >> 8);
        *((uint8_t *)(&pDHCPMSG->flags) + 1) = (DHCP_FLAGSUNICAST & 0x00FF);

        memcpy(pDHCPMSG->ciaddr, gWIZNETINFO.ip, 4);
        memcpy(ip, DHCP_SIP, 4);
    }

    k = 4; // 因为 MAGIC_COOKIE 已经由 makeDHCPMSG() 生成

    // 选项请求参数。
    pDHCPMSG->OPT[k++] = dhcpMessageType;
    pDHCPMSG->OPT[k++] = 0x01;
    pDHCPMSG->OPT[k++] = DHCP_REQUEST;

    pDHCPMSG->OPT[k++] = dhcpClientIdentifier;
    pDHCPMSG->OPT[k++] = 0x07;
    pDHCPMSG->OPT[k++] = 0x01;

    memcpy(pDHCPMSG->OPT + k, gWIZNETINFO.mac, 6);
    k += 6;

    if (ip[3] == 255) // 如果（dchp_state == STATE_DHCP_LEASED || dchp_state == DHCP_REREQUEST_STATE）
    {
        pDHCPMSG->OPT[k++] = dhcpRequestedIPaddr;
        pDHCPMSG->OPT[k++] = 0x04;

        memcpy(pDHCPMSG->OPT + k, gWIZNETINFO.ip, 4);
        k += 4;

        pDHCPMSG->OPT[k++] = dhcpServerIdentifier;
        pDHCPMSG->OPT[k++] = 0x04;

        memcpy(pDHCPMSG->OPT + k, DHCP_SIP, 4);
        k += 4;
    }

    // 主机名
    pDHCPMSG->OPT[k++] = hostName;
    pDHCPMSG->OPT[k++] = 0; // 主机名长度
    for (i = 0; HOST_NAME[i] != 0; i++)
        pDHCPMSG->OPT[k++] = HOST_NAME[i];
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[3] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[3]);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[4] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[4]);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[5] >> 4);
    pDHCPMSG->OPT[k++] = NibbleToHex(gWIZNETINFO.mac[5]);
    pDHCPMSG->OPT[k - (i + 6 + 1)] = i + 6; // 主机名长度

    pDHCPMSG->OPT[k++] = dhcpParamRequest;
    pDHCPMSG->OPT[k++] = 0x08;
    pDHCPMSG->OPT[k++] = subnetMask;
    pDHCPMSG->OPT[k++] = routersOnSubnet;
    pDHCPMSG->OPT[k++] = dns;
    pDHCPMSG->OPT[k++] = domainName;
    pDHCPMSG->OPT[k++] = dhcpT1value;
    pDHCPMSG->OPT[k++] = dhcpT2value;
    pDHCPMSG->OPT[k++] = performRouterDiscovery;
    pDHCPMSG->OPT[k++] = staticRoute;
    pDHCPMSG->OPT[k++] = endOption;

    for (i = k; i < OPT_SIZE; i++)
        pDHCPMSG->OPT[i] = 0;

    LOG_D("> Send DHCP_REQUEST");

    wizchip_sendto(DHCP_SOCKET, (uint8_t *)pDHCPMSG, RIP_MSG_SIZE, ip, DHCP_SERVER_PORT);
}

/*发送 DHCP DHCPDECLINE*/
void send_DHCP_DECLINE(void)
{
    int i;
    uint8_t ip[4];
    uint16_t k = 0;

    makeDHCPMSG();

    k = 4; // 因为 MAGIC_COOKIE 已经由 makeDHCPMSG() 生成

    *((uint8_t *)(&pDHCPMSG->flags)) = ((DHCP_FLAGSUNICAST & 0xFF00) >> 8);
    *((uint8_t *)(&pDHCPMSG->flags) + 1) = (DHCP_FLAGSUNICAST & 0x00FF);

    // 选项请求参数。
    pDHCPMSG->OPT[k++] = dhcpMessageType;
    pDHCPMSG->OPT[k++] = 0x01;
    pDHCPMSG->OPT[k++] = DHCP_DECLINE;

    pDHCPMSG->OPT[k++] = dhcpClientIdentifier;
    pDHCPMSG->OPT[k++] = 0x07;
    pDHCPMSG->OPT[k++] = 0x01;

    memcpy(pDHCPMSG->OPT + k, gWIZNETINFO.mac, 6);
    k += 6;

    pDHCPMSG->OPT[k++] = dhcpRequestedIPaddr;
    pDHCPMSG->OPT[k++] = 0x04;

    memcpy(pDHCPMSG->OPT + k, gWIZNETINFO.ip, 4);
    k += 4;

    pDHCPMSG->OPT[k++] = dhcpServerIdentifier;
    pDHCPMSG->OPT[k++] = 0x04;

    memcpy(pDHCPMSG->OPT + k, DHCP_SIP, 4);
    k += 4;

    pDHCPMSG->OPT[k++] = endOption;

    for (i = k; i < OPT_SIZE; i++)
        pDHCPMSG->OPT[i] = 0;

    // 发送广播包
    memset(ip, 0xFF, 4);

    LOG_D("> Send DHCP_DECLINE");

    wizchip_sendto(DHCP_SOCKET, (uint8_t *)pDHCPMSG, RIP_MSG_SIZE, ip, DHCP_SERVER_PORT);
}

/**
 *@brief 解析回复的dhcp报文
 *
 *@return int8_t
 */
int8_t parseDHCPMSG(void)
{
    uint8_t svr_addr[6];
    uint16_t svr_port;
    uint16_t len;

    uint8_t *p;
    uint8_t *e;
    uint8_t type = 0;
    uint8_t opt_len;
    len = getSn_RX_RSR(DHCP_SOCKET);
    if (len <= 0)
        return 0;

    len = wizchip_recvfrom(DHCP_SOCKET, (uint8_t *)pDHCPMSG, len, svr_addr, &svr_port);
    LOG_D("DHCP message : %d.%d.%d.%d(%d) %d received. ", svr_addr[0], svr_addr[1], svr_addr[2], svr_addr[3], svr_port, len);

    if (DHCP_SERVER_PORT != svr_port)
        return 0;

    // 校验mac地址
    if (0 != memcmp(gWIZNETINFO.mac, pDHCPMSG->chaddr, sizeof(gWIZNETINFO.mac)))
    {
        LOG_D("No My DHCP Message. This message is ignored.");
        return 0;
    }

    // 比较 DHCP 服务器 ip 地址
    if ((DHCP_SIP[0] != 0) || (DHCP_SIP[1] != 0) || (DHCP_SIP[2] != 0) || (DHCP_SIP[3] != 0))
    {
        if (0 != memcmp(DHCP_SIP, svr_addr, sizeof(DHCP_SIP)) &&
            0 != memcmp(DHCP_REAL_SIP, svr_addr, sizeof(DHCP_REAL_SIP)))
        {
            LOG_D("Another DHCP sever send a response message. This is ignored.");
            return 0;
        }
    }

    p = (uint8_t *)(&pDHCPMSG->op);
    p = p + 240; // 240 = sizeof(RIP_MSG) + RIP_MSG.opt 中的 MAGIC_COOKIE 大小 -sizeof(RIP_MSG.opt)
    e = p + (len - 240);

    while (p < e)
    {
        switch (*p)
        {

        case endOption:
            p = e; // 中断 while(p < e)
            break;

        case padOption:
            p++;
            break;

        case dhcpMessageType:
            p++;
            p++;
            type = *p++;
            break;
        case subnetMask:
            p++;
            p++;

            gWIZNETINFO.sn[0] = *p++;
            gWIZNETINFO.sn[1] = *p++;
            gWIZNETINFO.sn[2] = *p++;
            gWIZNETINFO.sn[3] = *p++;
            break;

        case routersOnSubnet:
            p++;
            opt_len = *p++;

            gWIZNETINFO.gw[0] = *p++;
            gWIZNETINFO.gw[1] = *p++;
            gWIZNETINFO.gw[2] = *p++;
            gWIZNETINFO.gw[3] = *p++;

            p = p + (opt_len - 4);
            break;

        case dns:
            p++;
            opt_len = *p++;

            gWIZNETINFO.dns[0] = *p++;
            gWIZNETINFO.dns[1] = *p++;
            gWIZNETINFO.dns[2] = *p++;
            gWIZNETINFO.dns[3] = *p++;

            p = p + (opt_len - 4);
            break;

        case dhcpIPaddrLeaseTime:
            p++;
            opt_len = *p++;

            uint32_t dhcpLeaseTime = 0;
            dhcpLeaseTime = *p++;
            dhcpLeaseTime = (dhcpLeaseTime << 8) + *p++;
            dhcpLeaseTime = (dhcpLeaseTime << 8) + *p++;
            dhcpLeaseTime = (dhcpLeaseTime << 8) + *p++;
            platformTimerCutdown(&dhcp_lease_time, dhcpLeaseTime * 1000);

            break;
        case dhcpServerIdentifier:
            p++;
            opt_len = *p++;
            DHCP_SIP[0] = *p++;
            DHCP_SIP[1] = *p++;
            DHCP_SIP[2] = *p++;
            DHCP_SIP[3] = *p++;

            memcpy(DHCP_REAL_SIP, svr_addr, 4);

            break;

        default:
            p++;
            opt_len = *p++;
            p += opt_len;
            break;
        } // 转变
    }     // 尽管
    return type;
}

/**
 *@简短的
 *
 *@return int8_t
 */
int8_t check_DHCP_leasedIP(void)
{
    uint8_t tmp;
    int32_t ret;

    // WIZchip RCR 值更改为 ARP 超时计数控制
    tmp = getRCR();
    setRCR(0x03);

    // IP 冲突检测：ARP 请求 -ARP 回复
    // 使用 UDP wizchip_sendto() 函数广播 ARP 请求以检查 IP 冲突
    ret = wizchip_sendto(DHCP_SOCKET, (uint8_t *)"CHECK_IP_CONFLICT", 17, gWIZNETINFO.ip, 5000);

    // Rcr值恢复
    setRCR(tmp);

    if (ret == SOCKERR_TIMEOUT)
    {
        // UDP 发送超时发生：分配的 IP 地址是唯一的，DHCP 成功
        LOG_D("> Check leased IP - OK");
        return 1;
    }
    else
    {
        // 收到 ARP 回复等：发生 IP 地址冲突，DHCP 失败
        send_DHCP_DECLINE();

        // 等待 1s 结束；等待完成发送 DECLINE 消息；
        rt_thread_mdelay(1000);
        return 0;
    }
}

/**
 *@brief 主循环中的 DHCP 客户端
 *
 *@return uint8_t
 */
uint8_t DHCP_run(uint8_t flag)
{
    uint8_t type = 0;
    uint8_t dhcp_retry_count = 0;
    platformTimer_t recvTimer = {0};
    RyanW5500Socket *sock = NULL;

    setSHAR(gWIZNETINFO.mac); // 设置w5500 mac

    sock = RyanW5500SocketCreate(Sn_MR_UDP, DHCP_CLIENT_PORT);
    if (NULL == sock)
    {
        LOG_W("dhcp socket失败");
        return -1;
    }

    // 申请dhcp报文空间
    uint8_t *dhcpDataBuf = (uint8_t *)rt_malloc(RIP_MSG_SIZE);
    if (NULL == dhcpDataBuf)
    {
        wiz_closesocket(sock->socket);
        return -1;
    }

    DHCP_SOCKET = sock->socket;
    pDHCPMSG = (RIP_MSG *)dhcpDataBuf;

    if (1 != flag) // flag 1表示处于续租状态
    {
        // 生成唯一事务id
        DHCP_XID = platformUptimeMs();
        DHCP_XID += gWIZNETINFO.mac[3];
        DHCP_XID += gWIZNETINFO.mac[4];
        DHCP_XID += gWIZNETINFO.mac[5];

        dhcp_state = STATE_DHCP_INIT;
        platformTimerInit(&dhcp_lease_time);
    }

    platformTimerCutdown(&recvTimer, DHCP_WAIT_TIME);

    while (1)
    {
        type = parseDHCPMSG();

        // 根据dhcp_state发送不同报文
        switch (dhcp_state)
        {
        case STATE_DHCP_INIT: // dhcp初始化状态
            memset(gWIZNETINFO.ip, 0, sizeof(gWIZNETINFO.ip));
            send_DHCP_DISCOVER(); // 客户端发送Discover报文
            dhcp_state = STATE_DHCP_DISCOVER;
            break;

        case STATE_DHCP_DISCOVER:
            if (DHCP_OFFER == type) // 服务器提供地址续约，offer报文
            {
                LOG_D("> Receive DHCP_OFFER");
                memcpy(gWIZNETINFO.ip, pDHCPMSG->yiaddr, sizeof(gWIZNETINFO.ip));
                send_DHCP_REQUEST(); // 发送请求地址租用 request报文
                dhcp_state = STATE_DHCP_REQUEST;
            }

            break;

        case STATE_DHCP_REQUEST:  // 客户端选择并请求地址租用
            if (DHCP_NAK == type) // 服务器取消把地址租用给客户端
            {
                LOG_D("> Receive DHCP_NACK");
                dhcp_state = STATE_DHCP_DISCOVER;
            }

            else if (DHCP_ACK == type) // 服务器确认将地址租用给客户端 ack报文
            {
                LOG_D("> Receive DHCP_ACK");
                // 发生 IP 地址冲突
                dhcp_state = check_DHCP_leasedIP() ? STATE_DHCP_LEASED : STATE_DHCP_INIT;
            }

            break;

        case STATE_DHCP_LEASED: // 当租期超过50%（1/2）时，客户端会以单播形式向DHCP服务器发送DHCP Request报文来续租IP地址。
            if (dhcp_lease_time.timeOut != 0 && platformTimerRemain(&dhcp_lease_time) < (dhcp_lease_time.timeOut / 2))
            {
                LOG_D("> Maintains the IP address ");
                DHCP_XID++;
                send_DHCP_REQUEST();

                dhcp_state = STATE_DHCP_REREQUEST;
            }
            break;

        case STATE_DHCP_REREQUEST: // 如果收到DHCP服务器发送的DHCP ACK报文，则按相应时间延长IP地址租期。如果没有，还是保持使用该IP地址。
            if (type == DHCP_ACK)
            {
                // 不需要判断续租分配的新ip还是旧ip，都会写入到w5500寄存器里
                LOG_D("> Receive DHCP_ACK, Maintains the IP address");
                dhcp_state = STATE_DHCP_LEASED;
            }
            else if (type == DHCP_NAK)
            {
                LOG_D("> Receive DHCP_NACK, Failed to maintain ip");
                dhcp_state = STATE_DHCP_DISCOVER;
            }

            break;

        default:
            break;
        }

        if (STATE_DHCP_LEASED == dhcp_state)
            goto next;

        // 如果超时，根据现有状态进行报文重发尝试
        if (0 == platformTimerRemain(&recvTimer))
        {
            switch (dhcp_state)
            {
            case STATE_DHCP_DISCOVER:
                LOG_D("<<timeout>> state : STATE_DHCP_DISCOVER");
                send_DHCP_DISCOVER();
                break;

            case STATE_DHCP_REQUEST:
                LOG_D("<<timeout>> state : STATE_DHCP_REQUEST");
                send_DHCP_REQUEST();
                break;

            case STATE_DHCP_REREQUEST:
                LOG_D("<<timeout>> state : STATE_DHCP_REREQUEST");
                send_DHCP_REQUEST();
                break;

            default:
                break;
            }

            platformTimerCutdown(&recvTimer, DHCP_WAIT_TIME);
            dhcp_retry_count++;
        }

        if (dhcp_retry_count >= MAX_DHCP_RETRY)
        {
            wiz_closesocket(sock->socket);
            free(dhcpDataBuf);
            return -1;
        }

        delay(100);
    }

next:
    wiz_closesocket(sock->socket);
    free(dhcpDataBuf);
    return 0;
}

/**
 *@brief 获取租用总时长 ms
 *
 *@return uint32_t
 */
uint32_t getDHCPLeaseTime(void)
{
    if (NETINFO_DHCP != gWIZNETINFO.dhcp)
        return 0;

    return dhcp_lease_time.timeOut;
}

uint32_t getDHCPRemainLeaseTime(void)
{
    return platformTimerRemain(&dhcp_lease_time);
}
