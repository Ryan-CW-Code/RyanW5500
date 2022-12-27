

#ifndef __RyanW5500Spi_
#define __RyanW5500Spi_

#ifdef __cplusplus
extern "C"
{
#endif
#include "RyanApplication.h"

    extern int RyanW5500SpiInit();
    extern void RyanW5500WriteByte(uint8_t data);
    extern uint8_t RyanW5500ReadByte(void);
    extern void RyanW5500WriteBurst(uint8_t *pbuf, uint16_t len);
    extern void RyanW5500ReadBurst(uint8_t *pbuf, uint16_t len);
    extern void RyanW5500CriticalEnter(void);
    extern void RyanW5500CriticalExit(void);
    extern void RyanW5500CsSelect(void);
    extern void RyanW5500CsDeselect(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIZ_SOCKET_H__ */
