
#ifndef _DNS_H_
#define _DNS_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

#define MAX_DNS_BUF_SIZE 256 // DNS 缓冲区的最大长度
#define MAX_DOMAIN_NAME 128  // 查询域名的最大长度 例如 "www.google.com"
#define MAX_DNS_RETRY 2      // 重试次数
#define DNS_WAIT_TIME 3000   // 等待响应时间，单位 ms。
#define IPPORT_DOMAIN 53     // DNS 服务器端口号
#define DNS_MSG_ID 0x1122    // DNS 消息的 ID。你可以修改它任何数字

    extern int8_t DNS_run(uint8_t *dns_ip, uint8_t *name, uint8_t *ip_from_dns, uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* _DNS_H_ */
