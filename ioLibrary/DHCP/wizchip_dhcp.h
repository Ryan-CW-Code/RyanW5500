
#ifndef _WIZCHIP_DHCP_H_
#define _WIZCHIP_DHCP_H_

#ifdef __cplusplus
extern "C"
{
#endif

#define MAX_DHCP_RETRY 6    // 最大重试计数
#define DHCP_WAIT_TIME 3000 // 等待时间

#define DHCP_SERVER_PORT 67 // DHCP 服务器端口号
#define DHCP_CLIENT_PORT 68 // DHCP 客户端端口号

#define MAGIC_COOKIE 0x63825363 // You should not modify it number.

#define DCHP_HOST_NAME "RyanW5500DHCP\0" // 主机名

    enum
    {
        DHCP_FAILED = 0, // 处理失败
        DHCP_RUNNING,    // 处理 DHCP 协议
        DHCP_IP_ASSIGN,  // 首先从DHPC服务器占用IP（如果cbfunc ==空，则充当默认default_ip_assign）
        DHCP_IP_CHANGED, // 通过来自 DHCP 的新 IP 更改 IP 地址（如果 cbfunc == null，则充当默认 default_ip_update）
        DHCP_IP_LEASED,  // dhcp ip已准备好
        DHCP_STOPPED     // 停止处理 DHCP 协议
    };

    uint8_t DHCP_run(uint8_t flag);

    // dhcp租用时间相关
    uint32_t getDHCPLeaseTime(void);
    uint32_t getDHCPRemainLeaseTime(void);

#ifdef __cplusplus
}
#endif

#endif /* _WIZCHIP_DHCP_H_ */
