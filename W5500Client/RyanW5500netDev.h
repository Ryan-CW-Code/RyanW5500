

#ifndef __RyanW5500netDev__
#define __RyanW5500netDev__

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <stdint.h>

#include <netdb.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // 定义枚举类型

    // 定义结构体类型

    /* extern variables-----------------------------------------------------------*/
    extern struct netdev *RyanW5500NetdevRegister(const char *netdev_name);

#ifdef __cplusplus
}
#endif

#endif
