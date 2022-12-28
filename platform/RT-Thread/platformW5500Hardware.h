

#ifndef __plarformW5500Spi__
#define __plarformW5500Spi__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>

    // 定义枚举类型

    // 定义结构体类型

    /* extern variables-----------------------------------------------------------*/
    extern int RyanW5500SpiInit();
    extern void RyanW5500WriteByte(uint8_t data);
    extern uint8_t RyanW5500ReadByte(void);
    extern void RyanW5500WriteBurst(uint8_t *pbuf, uint16_t len);
    extern void RyanW5500ReadBurst(uint8_t *pbuf, uint16_t len);
    extern void RyanW5500CriticalEnter(void);
    extern void RyanW5500CriticalExit(void);
    extern void RyanW5500CsSelect(void);
    extern void RyanW5500CsDeselect(void);

    extern void RyanW5500Reset(void);
    extern void RyanW5500AttachIRQ(void (*RyanW5500IRQCallback)(void *argument));

#ifdef __cplusplus
}
#endif

#endif
