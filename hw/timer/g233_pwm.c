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

#define TYPE_G233_PWM "g233-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

#define PWM_GLB                  0x00 
#define PWM_CH_BASE(n)          (0x10 + ((n) * 0x10))
#define PWM_CH_CTRL(n)          (PWM_CH_BASE(n) + 0x00)
#define PWM_CH_PERIOD(n)        (PWM_CH_BASE(n) + 0x04)
#define PWM_CH_DUTY(n)          (PWM_CH_BASE(n) + 0x08)
#define PWM_CH_CNT(n)           (PWM_CH_BASE(n) + 0x0C)

#define PWM_CH_EN               (1u << 0)
#define PWM_CH_POL              (1u << 1)
#define PWM_CH_INTIE            (1u << 2)

#define PWM_GLB_EN(n)           (1u << n)
#define PWM_GLB_DONE(n)         (1u << (4 + n))

#define PWM_NUM_CHANNELS        4
#define PWM_SIZE                0x1000                  //4KB

typedef struct G233PWMChannel {
    uint32_t ctrl;
    uint32_t period;
    uint32_t duty;

    int64_t start_ns;
    QEMUTimer *timer;

    G233PWMState *parent;
    unsigned int index;
}G233PWMChannel;

struct G233PWMState  {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    qemu_irq irq;
    G233PWMChannel channel[PWM_NUM_CHANNELS];
    uint32_t done;
};

static void g233_pwm_update_irq(void *opaque)
{
    G233PWMState *s = (G233PWMState *)opaque;
    bool level_flag = false;

    for (int i = 0; i < PWM_NUM_CHANNELS; i++)
    {
        if ((s->done & PWM_GLB_DONE(i)) && (s->channel[i].ctrl & PWM_CH_INTIE))
            {
                level_flag = true;
                break;
            }    
    }
    qemu_set_irq(s->irq, level_flag);
}

static uint64_t g233_pwm_read(void *opaque, hwaddr offset, unsigned size)
{
    G233PWMState *s = (G233PWMState *)opaque;
    uint64_t val = 0;
    switch (offset) {
        case PWM_GLB:
            {
                val |= s->channel[0].ctrl & 0x01;
                val |= (s->channel[1].ctrl & 0x01) << 1;
                val |= (s->channel[2].ctrl & 0x01) << 2;
                val |= (s->channel[3].ctrl & 0x01) << 3;
                val |= s->done;
                return val;
            }
        case PWM_CH_CTRL(0):
            {
                val =  s->channel[0].ctrl;
                return val;
            }
        case PWM_CH_CTRL(1):
            {
                val = s->channel[1].ctrl;
                return val;
            }
        case PWM_CH_CTRL(2):
            {
                val = s->channel[2].ctrl;
                return val;
            }
        case PWM_CH_CTRL(3):
            {
                val = s->channel[3].ctrl;
                return val;
            }
        case PWM_CH_PERIOD(0):
            {
                val =  s->channel[0].period;
                return val;
            }
        case PWM_CH_PERIOD(1):
            {
                return s->channel[1].period;
            }     
        case PWM_CH_PERIOD(2):
            {
                return s->channel[2].period;
            }
        case PWM_CH_PERIOD(3):
            {
                return s->channel[3].period;
            }
        case PWM_CH_DUTY(0):  
            {
                return s->channel[0].duty;
            }
        case PWM_CH_DUTY(1):  
            {
                return s->channel[1].duty;
            }
        case PWM_CH_DUTY(2):  
            {
                return s->channel[2].duty;
            }
        case PWM_CH_DUTY(3):  
            {
                return s->channel[3].duty;
            }
        case PWM_CH_CNT(0):  
            {
                if (!(s->channel[0].ctrl & PWM_CH_EN))
                    return 0;
                int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                int64_t cnt = now_ns - s->channel[0].start_ns;
                return (cnt % ((uint64_t)s->channel[0].period + 1));
            }
        case PWM_CH_CNT(1):  
            {
                if (!(s->channel[1].ctrl & PWM_CH_EN))
                    return 0;
                int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                int64_t cnt = now_ns - s->channel[1].start_ns;
                return (cnt % ((uint64_t)s->channel[1].period + 1));            
            }
        case PWM_CH_CNT(2):  
            {
                if (!(s->channel[2].ctrl & PWM_CH_EN))
                    return 0;
                int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                int64_t cnt = now_ns - s->channel[2].start_ns;
                return (cnt % ((uint64_t)s->channel[2].period + 1));            
            }
        case PWM_CH_CNT(3):  
            {
                if (!(s->channel[3].ctrl & PWM_CH_EN))
                    return 0;
                int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                int64_t cnt = now_ns - s->channel[3].start_ns;
                return (cnt % ((uint64_t)s->channel[3].period + 1));            
            }
        default:
        {
            qemu_log_mask(LOG_GUEST_ERROR, "g233_pwm_read: Bad offset %x\n", (unsigned int)offset);
            return 0;
        }
    }
}

static void g233_pwm_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    G233PWMState *s = (G233PWMState *)opaque;
    switch (offset) {
        case PWM_GLB:
            {
                s->done &= ~(value & 0xF0 ) ;
                g233_pwm_update_irq(s);
                return ;
            }
        case PWM_CH_CTRL(0):
            {
                uint32_t old_value = s->channel[0].ctrl & PWM_CH_EN;
                uint32_t new_value = value & PWM_CH_EN;
                s->channel[0].ctrl = value & (PWM_CH_EN | PWM_CH_INTIE | PWM_CH_POL);
                if ( ~old_value & PWM_CH_EN & new_value)
                    {
                        s->channel[0].start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->channel[0].timer, s->channel[0].start_ns + s->channel[0].period + 1);
                    }
                else if (old_value & ~new_value & PWM_CH_EN)
                    {
                        timer_del(s->channel[0].timer);
                    }
                g233_pwm_update_irq(s);
                return ;
            }
        case PWM_CH_CTRL(1):
            {
                uint32_t old_value = s->channel[1].ctrl & PWM_CH_EN;
                uint32_t new_value = value & PWM_CH_EN;
                s->channel[1].ctrl = value & (PWM_CH_EN | PWM_CH_INTIE | PWM_CH_POL);
                if ( ~old_value & PWM_CH_EN & new_value)
                    {
                        s->channel[1].start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->channel[1].timer, s->channel[1].start_ns + s->channel[1].period + 1);
                    }
                else if (old_value & ~new_value & PWM_CH_EN)
                    {
                        timer_del(s->channel[1].timer);
                    }
                g233_pwm_update_irq(s);
                return ;
            }
        case PWM_CH_CTRL(2):
            {
                uint32_t old_value = s->channel[2].ctrl & PWM_CH_EN;
                uint32_t new_value = value & PWM_CH_EN;
                s->channel[2].ctrl = value & (PWM_CH_EN | PWM_CH_INTIE | PWM_CH_POL);
                if ( ~old_value & PWM_CH_EN & new_value)
                    {
                        s->channel[2].start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->channel[2].timer, s->channel[2].start_ns + s->channel[2].period + 1);
                    }
                else if (old_value & ~new_value & PWM_CH_EN)
                    {
                        timer_del(s->channel[2].timer);
                    }
                g233_pwm_update_irq(s);
                return ;
            }
        case PWM_CH_CTRL(3):
            {
                uint32_t old_value = s->channel[3].ctrl & PWM_CH_EN;
                uint32_t new_value = value & PWM_CH_EN;
                s->channel[3].ctrl = value & (PWM_CH_EN | PWM_CH_INTIE | PWM_CH_POL);
                if ( ~old_value & PWM_CH_EN & new_value)
                    {
                        s->channel[3].start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                        timer_mod(s->channel[3].timer, s->channel[3].start_ns + s->channel[3].period + 1);
                    }
                else if (old_value & ~new_value & PWM_CH_EN)
                    {
                        timer_del(s->channel[3].timer);
                    }
                g233_pwm_update_irq(s);
                return ;
            }
        case PWM_CH_PERIOD(0):
            {
                s->channel[0].period = value;
                return ;
            }
        case PWM_CH_PERIOD(1):
            {
                s->channel[1].period = value;
                return ;
            }
        case PWM_CH_PERIOD(2):
            {
                s->channel[2].period = value;
                return ;
            }
        case PWM_CH_PERIOD(3):
            {
                s->channel[3].period = value;
                return ;
            }
        case PWM_CH_DUTY(0):
            {
                s->channel[0].duty = value;
                return ;
            }
        case PWM_CH_DUTY(1):
            {
                s->channel[1].duty = value;
                return ;
            }
        case PWM_CH_DUTY(2):
            {
                s->channel[2].duty = value;
                return ;
            }
        case PWM_CH_DUTY(3):
            {
                s->channel[3].duty = value;
                return ;
            }
        case PWM_CH_CNT(0): 
        case PWM_CH_CNT(1): 
        case PWM_CH_CNT(2):
        case PWM_CH_CNT(3):
            return;
        default:
            {
                qemu_log_mask(LOG_INVALID_MEM, "g233_pwm_write: %x\n", (unsigned int)offset);
                return ;
            }
    }
}

static void g233_pwm_timer_cb(void * opaque)
{
    G233PWMChannel *ch = (G233PWMChannel *)opaque;
    G233PWMState *s = ch->parent;

    s->done |= PWM_GLB_DONE(ch->index);

    if (ch->ctrl & PWM_CH_EN)
        {
            ch->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            timer_mod(ch->timer, (uint64_t)ch->start_ns + (uint64_t)ch->period + 1);
        }
    g233_pwm_update_irq(s);
}

static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);
    s->done = 0x00000000;
    for (int i = 0; i < PWM_NUM_CHANNELS; i++)
    {
        s->channel[i].ctrl = 0;
        s->channel[i].duty = 0;
        s->channel[i].period = 0;
        s->channel[i].start_ns = 0;
        timer_del(s->channel[i].timer);
    }
    qemu_set_irq(s->irq, 0);
}

static struct MemoryRegionOps g233_pwm_ops = {
    .read       = g233_pwm_read,
    .write      = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static void g233_pwm_init(Object *obj)
{
    G233PWMState *s = G233_PWM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &g233_pwm_ops, s, TYPE_G233_PWM, PWM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    for (int i = 0; i < PWM_NUM_CHANNELS; i++)
    {
        s->channel[i].parent = s;
        s->channel[i].index = i;
        s->channel[i].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_pwm_timer_cb, &s->channel[i]);
    }
}

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_pwm_reset);
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)