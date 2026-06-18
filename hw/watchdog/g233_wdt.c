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
#include "qemu/timer.h"
#include "system/runstate.h"
#include "exec/hwaddr.h"

#define TYPE_G233_WDT "g233-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(G233WDTState, G233_WDT) 

#define WDT_SIZE 0x1000             //4KB

#define WDT_CTRL 0x00
#define WDT_LOAD 0x04
#define WDT_VAL  0x08
#define WDT_SR   0x0C
#define WDT_KEY  0x10

//register bit
#define WDT_CTRL_LOCK       (1u << 3)
#define WDT_CTRL_RSTEN      (1u << 2)
#define WDT_CTRL_INTEN      (1u << 1)
#define WDT_CTRL_EN         (1u << 0)

#define WDT_SR_TIMEOUT      (1u << 0)

#define WDT_KEY_FEED        0x5A5A5A5A
#define WDT_KEY_LOCK        0x1ACCE551        

struct G233WDTState {

    SysBusDevice parent_obj;

    MemoryRegion iomem;

    //val
    QEMUTimer *timer;
    int64_t expire_ns;
    uint32_t val;

    uint32_t ctrl;
    uint32_t load;
    uint32_t sr;

    qemu_irq irq;

};

static uint32_t g233_wdt_get_val(G233WDTState *s)
{
    int64_t now_ns;
    if ((WDT_CTRL_EN & s->ctrl) == 0)
        return s->val;
    now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (now_ns < s->expire_ns)
        return s->expire_ns - now_ns;
    else 
        return 0;
}

static void g233_wdt_timer_cb(void *opaque)
{
    G233WDTState *s = (G233WDTState *)opaque;
    s->sr |= WDT_SR_TIMEOUT;
    s->val = 0;
    
    qemu_set_irq(s->irq, (s->ctrl & WDT_CTRL_INTEN) != 0);

    if (s->ctrl & WDT_CTRL_RSTEN)
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

static uint64_t g233_wdt_read(void *opaque, hwaddr offset, unsigned size)
{
    G233WDTState *s = (G233WDTState *)opaque;
    switch (offset) {
        case WDT_CTRL:
            return s->ctrl;
        case WDT_LOAD:
            return s->load;
        case WDT_VAL:
            return g233_wdt_get_val(s);
        case WDT_SR:
            return s->sr;
        case WDT_KEY:
            return 0;
        default:
            {
                qemu_log_mask(LOG_GUEST_ERROR, "g233_wdt_read: Bad offset %x\n", (unsigned int)offset);
                return 0;                
            }
    }
}

static void g233_wdt_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    G233WDTState *s = (G233WDTState *)opaque;
    switch (offset) {
        case WDT_CTRL:
            {
                if (!(s->ctrl & WDT_CTRL_LOCK))             //lock == 0
                {
                    bool old_en =  s->ctrl & WDT_CTRL_EN;
                    bool new_en = value & WDT_CTRL_EN;
                    s->ctrl = value & (WDT_CTRL_EN | WDT_CTRL_INTEN | WDT_CTRL_RSTEN); 

                    if (!old_en & new_en)               //rising edge
                    {
                        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        s->val = s->load;
                        s->expire_ns = now_ns + s->val;
                        timer_mod(s->timer, s->expire_ns);
                    }
                    else if (old_en & !new_en)          //falling edge
                    {
                        int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        s->val = (s->expire_ns > now_ns)  ? (s->expire_ns - now_ns) : 0;
                        timer_del(s->timer);
                    }
                    qemu_set_irq(s->irq, (s->sr & WDT_SR_TIMEOUT) && (s->ctrl & WDT_CTRL_INTEN));
                }
                return;
            }
        case WDT_LOAD:
            {
                s->load = value;
                return;
            }
        case WDT_VAL:
            return ;
        case WDT_SR:
            {
                uint32_t clear = value & WDT_SR_TIMEOUT;
                if (clear)
                {
                    s->sr &= ~clear;
                    qemu_set_irq(s->irq, 0);
                }
                return;
            }
        case WDT_KEY:
            {
                if (value == WDT_KEY_FEED)
                    {
                        s->val = s->load;
                        s->sr &= ~WDT_SR_TIMEOUT;
                        if (s->ctrl & WDT_CTRL_EN)
                        {
                            int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                            s->expire_ns = now_ns + s->val;
                            timer_mod(s->timer, s->expire_ns);    
                        }
                        qemu_set_irq(s->irq, 0);
                    }
                else if (value == WDT_KEY_LOCK)
                    s->ctrl |= WDT_CTRL_LOCK;               //lock = 1
                return;
            }
        default:
            {
                qemu_log_mask(LOG_GUEST_ERROR, "g233_wdt_write: Bad offset %x\n", (unsigned int)offset);
                return;
            }
    }
}

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *ds = G233_WDT(dev);
    timer_del(ds->timer);

    ds->ctrl = 0x00000000;
    ds->load = 0x0000FFFF;
    ds->val  = 0x0000FFFF;
    ds->sr   = 0x00000000;
    qemu_set_irq(ds->irq, 0);
}

static struct MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static void g233_wdt_init(Object *obj)
{
    G233WDTState *s = G233_WDT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_wdt_timer_cb, s);

    memory_region_init_io(&s->iomem, obj, &g233_wdt_ops, s, TYPE_G233_WDT, WDT_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}

static struct TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)