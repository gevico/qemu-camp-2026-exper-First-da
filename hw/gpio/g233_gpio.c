#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include <stdint.h>


#define TYPE_G233_GPIO "g233-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(G233GPIOState, G233_GPIO)

#define GPIO_SIZE       0x100                  // SIZE: 256B

#define GPIO_DIR        0x00
#define GPIO_OUT        0x04
#define GPIO_IN         0x08
#define GPIO_IE         0x0C
#define GPIO_IS         0x10
#define GPIO_TRIG       0x14
#define GPIO_POL        0x18

struct G233GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    uint32_t dir;
    uint32_t out;
    uint32_t in;
    uint32_t ie;
    uint32_t is;
    uint32_t trig;
    uint32_t pol;
};

static uint32_t g233_gpio_get_level(G233GPIOState *s)
{
    return ((s->dir & s->out) | (~s->dir & s->in)) ; 
}

static void g233_gpio_update_irq(G233GPIOState *s, uint32_t old_level, uint32_t new_level)
{
    uint32_t rising;
    uint32_t falling;
    uint32_t edge_status;
    uint32_t level_status;

    rising = new_level & ~old_level;
    falling = ~new_level & old_level;

    //edge trigger irq
    edge_status = ~s->trig & ((~s->pol & falling) | (s->pol & rising));
    edge_status &= s->ie;
    s->is |= edge_status;

    //level trigger irq
    level_status = s->trig & ((~s->pol & ~new_level) | (s->pol & new_level));
    level_status &= s->ie;
    s->is = (s->is & ~s->trig) | level_status;
    
    qemu_set_irq(s->irq, (s->is & s->ie) != 0);
}


static uint64_t g233_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    G233GPIOState * s = (G233GPIOState *)opaque;
    switch (offset) {
        case GPIO_DIR:
            return s->dir;
        case GPIO_OUT:
            return s->out;
        case GPIO_IN:
            return (s->dir & s->out) | (~s->dir & s->in);
        case GPIO_IE:
            return s->ie;
        case GPIO_IS:
            return s->is;
        case GPIO_TRIG:
            return s->trig;
        case GPIO_POL:
            return s->pol;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "g233_gpio_read: Bad offset %x\n", (int)offset);
            return 0;
    }
}

static void g233_gpio_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    G233GPIOState *s = (G233GPIOState *)opaque;
    switch (offset) {
        case GPIO_DIR:
            s->dir = (uint32_t)value;
            break;
        case GPIO_OUT:
            {
                uint32_t old_level, new_level;
                old_level = g233_gpio_get_level(s);
                s->out = (uint32_t)value;
                new_level = g233_gpio_get_level(s);
                
                g233_gpio_update_irq(s, old_level, new_level);
                break;
            }
        case GPIO_IN:
            break;
        case GPIO_IE:
            s->ie = (uint32_t)value;
            qemu_set_irq(s->irq, (s->is & s->ie) !=0);
            break;
        case GPIO_IS:
            {
                s->is &= ~(uint32_t)value;
                qemu_set_irq(s->irq, (s->is & s->ie) !=0);
                break;
            }
        case GPIO_TRIG:
            s->trig = (uint32_t)value;
            break;
        case GPIO_POL:
            s->pol = (uint32_t)value;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "g233_gpio_write: Bad offset %x\n", (int)offset);
            break;
    }
}

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);

    s->dir      = 0x00000000;
    s->out      = 0x00000000;
    s->in       = 0x00000000;
    s->ie       = 0x00000000;
    s->is       = 0x00000000;
    s->trig     = 0x00000000;
    s->pol      = 0x00000000;

    qemu_set_irq(s->irq, 0);
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static void g233_gpio_init(Object *obj)
{
    G233GPIOState *s = G233_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &g233_gpio_ops, s, TYPE_G233_GPIO, GPIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

}

static void g233_gpio_class_init(ObjectClass * klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, g233_gpio_reset);
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)