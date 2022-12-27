#include "RyanApplication.h"

static struct rt_spi_device *RyanW5500SpiDevice = NULL;
#define RyanW5500SpiDeviceName "spi26"
#define RyanW5500SpiFreqMax (40 * 1000 * 1000)

#define RyanW5500ResetGpio GET_PIN(C, 0)
#define RyanW5500IRQGpio GET_PIN(C, 1)

/**
 * @brief SPI 初始化
 *
 */
int RyanW5500SpiInit()
{
    rt_hw_spi_device_attach("spi2", RyanW5500SpiDeviceName, GPIOE, GPIO_PIN_15);

    RyanW5500SpiDevice = rt_device_find(RyanW5500SpiDeviceName);
    if (RyanW5500SpiDevice == NULL)
    {
        LOG_E("You should attach [%s] into SPI bus firstly.", RyanW5500SpiDeviceName);
        return RT_ERROR;
    }

    struct rt_spi_configuration cfg = {
        .data_width = 8,
        .mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB, //  SPI Compatible Modes 0
        .max_hz = RyanW5500SpiFreqMax                       // SPI Interface with Clock Speeds Up to 40 MHz
    };
    rt_spi_configure(RyanW5500SpiDevice, &cfg);

    if (rt_device_open(RyanW5500SpiDevice, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open WIZnet SPI device %s error.", RyanW5500SpiDeviceName);
        return RT_ERROR;
    }

    return RT_EOK;
}

/**
 * @brief SPI 写入一个字节
 *
 * @param data
 */
void RyanW5500WriteByte(uint8_t data)
{
    struct rt_spi_message spiMsg = {
        .send_buf = &data,
        .length = 1};

    rt_spi_transfer_message(RyanW5500SpiDevice, &spiMsg);
}

/**
 * @brief SPI 读取一个字节
 *
 * @return uint8_t
 */
uint8_t RyanW5500ReadByte(void)
{
    uint8_t data;
    struct rt_spi_message spiMsg = {
        .recv_buf = &data,
        .length = 1};

    rt_spi_transfer_message(RyanW5500SpiDevice, &spiMsg);

    return data;
}

/**
 * @brief SPI 写入多个字节
 *
 * @param pbuf
 * @param len
 */
void RyanW5500WriteBurst(uint8_t *pbuf, uint16_t len)
{
    struct rt_spi_message spiMsg = {
        .send_buf = pbuf,
        .length = len};

    rt_spi_transfer_message(RyanW5500SpiDevice, &spiMsg);
}

/**
 * @brief SPI 读取多个字节
 *
 * @param pbuf
 * @param len
 */
void RyanW5500ReadBurst(uint8_t *pbuf, uint16_t len)
{

    struct rt_spi_message spiMsg = {
        .recv_buf = pbuf,
        .length = len};

    rt_spi_transfer_message(RyanW5500SpiDevice, &spiMsg);
}

/**
 * @brief 片选使能
 *
 */
void RyanW5500CsSelect(void)
{
    rt_spi_take(RyanW5500SpiDevice);
}

/**
 * @brief 片选失能
 *
 */
void RyanW5500CsDeselect(void)
{
    rt_spi_release(RyanW5500SpiDevice);
}

/**
 * @brief w5500重启函数,只会在w5500初始化时调用
 *
 */
void RyanW5500Reset(void)
{
    rt_pin_mode(RyanW5500ResetGpio, PIN_MODE_OUTPUT); // 设置重启引脚电平

    rt_pin_write(RyanW5500ResetGpio, PIN_LOW);
    rt_thread_mdelay(2);

    rt_pin_write(RyanW5500ResetGpio, PIN_HIGH);
    rt_thread_mdelay(2);
}

/**
 * @brief w5500中断绑定函数,只会在w5500初始化时调用
 *
 * @param RyanW5500IRQCallback 中断回调函数
 */
void RyanW5500AttachIRQ(void (*RyanW5500IRQCallback)(void *argument))
{
    rt_pin_mode(RyanW5500IRQGpio, PIN_MODE_INPUT); // 初始化中断引脚
    rt_pin_attach_irq(RyanW5500IRQGpio, PIN_IRQ_MODE_FALLING, RyanW5500IRQCallback, NULL);
    rt_pin_irq_enable(RyanW5500IRQGpio, PIN_IRQ_ENABLE);
}
