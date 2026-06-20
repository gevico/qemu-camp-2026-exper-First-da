#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/typedefs.h"
#include "qom/object.h"
#include "system/memory.h"
#include <stdbool.h>
#include <stdint.h>
#include "hw/ssi/ssi.h"
#include "exec/hwaddr.h"
#include "hw/core/qdev.h"

#define TYPE_G233_SPI "g233-spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

#define SPI_SIZE 0x1000     //4KB

#define SPI_CR1 0x00
#define SPI_CR2 0x04
#define SPI_SR  0x08
#define SPI_DR  0x0C

#define SPI_CR1_SPE         (1u << 0)
#define SPI_CR1_MSTR        (1u << 2)
#define SPI_CR1_ERRIE       (1u << 5)
#define SPI_CR1_RXNEIE      (1u << 6)
#define SPI_CR1_TXEIE       (1u << 7)

#define SPI_CR2_CS          (0x3u)

#define SPI_SR_RXNE         (1u << 0)
#define SPI_SR_TXE          (1u << 1)
#define SPI_SR_OVERRUN      (1u << 4)

// //flash cmd
// #define FLASH_CMD_WRITE_ENABLE          0x06
// #define FLASH_CMD_WRITE_JEDEC_ID        0x9F
// #define FLASH_CMD_WRITE_RDSR            0x05
// #define FLASH_CMD_WRITE_WRDI            0x04
// #define FLASH_CMD_WRITE_SE              0x20
// #define FLASH_CMD_WRITE_PP              0x02
// #define FLASH_CMD_WRITE_READ            0x03

// #define FLASH_SR_BUSY       (1u << 0)
// #define FLASH_SR_WEL        (1u << 1)

struct G233SPIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t cr1;
    uint32_t cr2;
    uint32_t sr;
    uint32_t dr;

    qemu_irq cs[4];

    SSIBus * ssi;
};

static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    G233SPIState *s = (G233SPIState *)opaque;
    switch (offset) {
        case SPI_CR1:
            return s->cr1;
        case SPI_CR2:
            return s->cr2;
        case SPI_SR:
            return s->sr;
        case SPI_DR:
            {
                s->sr &= ~(SPI_SR_RXNE);
                //int update
                return s->dr & 0xFF;
            }
        default:
            {
                qemu_log_mask(LOG_GUEST_ERROR, "g233_spi_read: Bad offset %x\n", (unsigned int)offset);
                return 0;
            }
    }
}

static void g233_spi_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    G233SPIState *s = (G233SPIState *)opaque;
    switch (offset) {
        case SPI_CR1:
            {
                s->cr1 = value & (SPI_CR1_SPE | SPI_CR1_MSTR | SPI_CR1_ERRIE | SPI_CR1_RXNEIE | SPI_CR1_TXEIE);
                //int update
                return ;
            }
        case SPI_CR2:
            {
                uint32_t temp = SPI_CR2_CS & value;
                for (uint32_t i = 0; i < 4; i++)
                    qemu_set_irq(s->cs[i], !(i == temp));
                s->cr2 = temp;
                //int update
                return ;
            }
        case SPI_SR:
            {
                s->sr &= ~(SPI_SR_OVERRUN & value);
                //int update
                return ;
            }
        case SPI_DR:
            {
                if (SPI_SR_RXNE & s->sr)
                    s->sr |= SPI_SR_OVERRUN ;
                s->dr = ssi_transfer(s->ssi, value & 0xFF) & 0xFF;
                s->sr |= SPI_SR_TXE;
                s->sr |= SPI_SR_RXNE;
                return;
            }
        default:
            {
                qemu_log_mask(LOG_GUEST_ERROR, "g233_spi_write: Bad offset %x\n", (unsigned int)offset);
                return;
            }
    }
}


static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *s = G233_SPI(dev);

    s->cr1 = 0;
    s->cr2 = 0;
    s->sr  = 0x02;
    s->dr  = 0;

    for (int i = 0; i < 4; i ++)
        qemu_set_irq(s->cs[i], 1);
    //int update
    //qemu_set_irq(s->irq, 0);
}

static struct MemoryRegionOps g233_spi_ops = {
    .read           = g233_spi_read,
    .write          = g233_spi_write,
    .endianness     = DEVICE_LITTLE_ENDIAN
};


static void g233_spi_init(Object *obj)
{
    G233SPIState *s = G233_SPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_out_named(DEVICE(obj), s->cs, "spi-cs", 4);

    memory_region_init_io(&s->iomem, obj, &g233_spi_ops, s, TYPE_G233_SPI, SPI_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->ssi = ssi_create_bus((DeviceState *)s, "ssi-spi-bus");
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass * dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_spi_reset);
}

static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .instance_init = g233_spi_init,
    .class_init = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
};

type_init(g233_spi_register_types)