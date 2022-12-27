#ifndef __RyanW5500Ping__
#define __RyanW5500Ping__

#ifdef __cplusplus
extern "C"
{
#endif

#include "RyanW5500Store.h"

    // 定义枚举类型

    // 定义结构体类型

    /* extern variables-----------------------------------------------------------*/
    extern int RyanW5500Ping(struct netdev *netdev, const char *host, size_t data_len,
                             uint32_t times, struct netdev_ping_resp *ping_resp);

#ifdef __cplusplus
}
#endif

#endif
