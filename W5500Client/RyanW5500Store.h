
#ifndef __RyanW5500Store__
#define __RyanW5500Store__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/ioctl.h>
#include <sys/time.h>

#include <rtthread.h>

#include <sal_socket.h>
#include <sal_netdb.h>
#if (RTTHREAD_VERSION > 40100)
#include <sal_low_lvl.h>
#else
#include <sal.h>
#endif

#include "netdev_ipaddr.h"
#include "netdev.h"

#include "RyanList.h"
#include "RyanW5500Log.h"
#include "platformTimer.h"
#include "platformW5500Hardware.h"
#include "wizchip_conf.h"
#include "wizchip_socket.h"
#include "wizchip_dhcp.h"
#include "wizchip_dns.h"
#include "w5500.h"
#include "RyanW5500.h"
#include "RyanW5500Socket.h"
#include "RyanW5500Ping.h"
#include "RyanW5500netDev.h"

#ifndef delay
#define delay(ms) rt_thread_mdelay(ms)
#endif

// WIZnet套接字魔术词
#define WIZ_SOCKET_MAGIC 0x3120

// WIZnet 套接字地址系列
#ifndef AF_WIZ
#define AF_WIZ 46
#endif

#define RyanW5500MaxSocketNum (_WIZCHIP_SOCK_NUM_)
#define netDevDHCP (1 << 2)
#define netDevSetDevInfo (1 << 3)

// event标志
// 前8bit用于socket通道数据解析
#define RyanW5500IRQBit (1 << RyanW5500MaxSocketNum)

#define RyanW5500SnIMR (Sn_IR_RECV | Sn_IR_DISCON | Sn_IR_CON)     // Sn_IMR
#define RyanW5500IMR (IR_CONFLICT | IR_UNREACH | IR_PPPoE | IR_MP) // IMR (中断屏蔽寄存器)

#define RyanW5500CheckCodeNoReturn(EX, ErrorCode, Ryanlevel, code) \
    if (!(EX))                                                     \
    {                                                              \
                                                                   \
        Ryanlevel("ErrorCode: %d, strError: %s",                   \
                  ErrorCode, RyanW5500StrError(ErrorCode));        \
        errno = (ErrorCode);                                       \
        {code};                                                    \
    }

#define RyanW5500CheckCode(EX, ErrorCode, level, code) RyanW5500CheckCodeNoReturn(EX, ErrorCode, level, { {code}; return ErrorCode; });

#define RyanW5500CheckNoReturn(EX, ErrorCode, level) RyanW5500CheckCodeNoReturn(EX, ErrorCode, level, {})
#define RyanW5500Check(EX, ErrorCode, level) RyanW5500CheckCode(EX, ErrorCode, level, {})
#define RyanW5500CheckAssert(EX, ErrorCode, level) RyanW5500CheckCodeNoReturn(EX, ErrorCode, level, { assert(NULL); })

    // 定义枚举类型

    // 定义结构体类型
    typedef struct
    {
        uint32_t netDevFlag;          // netdev用flag
        rt_event_t W5500EventHandle;  // 事件标志组，用于中断通知和socket状态通知
        rt_mutex_t socketMutexHandle; // socket锁
        rt_thread_t w5500TaskHandle;  // W5500线程
    } RyanW5500Entry_t;

    /* extern variables-----------------------------------------------------------*/

    extern wiz_NetInfo gWIZNETINFO;
    extern RyanW5500Entry_t RyanW5500Entry;

#ifdef __cplusplus
}
#endif

#endif
