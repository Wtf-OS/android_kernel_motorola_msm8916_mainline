// SPDX-License-Identifier: GPL-2.0-only
// SPI framing and Osprey wiring follow Motorola's stml0xx driver.
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/pm_wakeirq.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>

#define MSG_SIZE 96
struct hub {
    struct spi_device *spi;
    struct gpio_desc *ready, *ack, *reset, *boot, *wake, *wakeirq;
    struct mutex lock;
    u8 tx[MSG_SIZE], rx[MSG_SIZE];
    int lux, proximity;
};

static u16 crc(const u8 *buf, int size)
{
    u16 result = 0;
    int i;
    while (size--) {
        result ^= *buf++ << 8;
        for (i = 0; i < 8; i++)
            result = result & 0x8000 ? (result << 1) ^ 0x1021 : result << 1;
    }
    return result;
}

static void swap_pairs(u8 *buf)
{
    int i;
    for (i = 0; i < MSG_SIZE - 2; i += 2)
        swap(buf[i], buf[i + 1]);
}

static int ready(struct hub *h)
{
    int i, value;
    for (i = 0; i < 100; i++) {
        value = gpiod_get_value_cansleep(h->ready);
        if (value < 0)
            return value;
        if (value)
            return 0;
        usleep_range(50, 100);
    }
    return -ETIMEDOUT;
}

static int transfer(struct hub *h, bool read)
{
    struct spi_transfer xfer = {
        .tx_buf = read ? NULL : h->tx, .rx_buf = h->rx,
        .len = MSG_SIZE, .speed_hz = 4000000, .bits_per_word = 8,
    };
    int ret = ready(h);
    if (ret)
        return ret;
    ret = spi_sync_transfer(h->spi, &xfer, 1);
    if (ret)
        return ret;
    ret = ready(h);
    if (ret)
        return ret;
    ret = gpiod_get_value_cansleep(h->ack);
    return ret < 0 ? ret : ret ? 0 : -EIO;
}

static int reg_transfer_once(struct hub *h, u8 type, u8 reg, u8 *buf, int len, bool read)
{
    int ret;
    if (len <= 0 || len > 88)
        return -EINVAL;
    mutex_lock(&h->lock);
    gpiod_set_value_cansleep(h->wake, 1);
    memset(h->tx, 0, MSG_SIZE);
    h->tx[0] = 0xf9; h->tx[1] = 0xae;
    h->tx[2] = type; h->tx[3] = reg; h->tx[5] = len;
    if (!read)
        memcpy(h->tx + 6, buf, len);
    swap_pairs(h->tx);
    put_unaligned_be16(crc(h->tx, MSG_SIZE - 2), h->tx + MSG_SIZE - 2);
    ret = transfer(h, false);
    if (ret || !read)
        goto out;
    ret = transfer(h, true);
    if (ret)
        goto out;
    if (get_unaligned_be16(h->rx + MSG_SIZE - 2) != crc(h->rx, MSG_SIZE - 2)) {
        ret = -EBADMSG;
        goto out;
    }
    swap_pairs(h->rx);
    memcpy(buf, h->rx, len);
out:
    gpiod_set_value_cansleep(h->wake, 0);
    mutex_unlock(&h->lock);
    return ret;
}

static int msg_transfer(struct hub *h, u8 type, u8 reg, u8 *buf, int len, bool read)
{
    int ret, attempt;
    for (attempt = 0; attempt < 5; attempt++) {
        ret = reg_transfer_once(h, type, reg, buf, len, read);
        if (!ret || ret == -EINVAL) return ret;
        msleep(20);
    }
    return ret;
}

static int reg_transfer(struct hub *h, u8 reg, u8 *buf, int len, bool read)
{
    return msg_transfer(h, read ? 1 : 2, reg, buf, len, read);
}

static ssize_t accel_xyz_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct hub *h = dev_get_drvdata(dev);
    u8 data[6];
    int ret = reg_transfer(h, 0x3b, data, sizeof(data), true);
    if (ret)
        return ret;
    return sysfs_emit(buf, "%d %d %d\n", (s16)get_unaligned_be16(data),
                      (s16)get_unaligned_be16(data + 2), (s16)get_unaligned_be16(data + 4));
}
static DEVICE_ATTR_RO(accel_xyz);
static ssize_t light_lux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct hub *h = dev_get_drvdata(dev);
    u8 packet[88];
    int ret = msg_transfer(h, 3, 0, packet, sizeof(packet), true);
    if (ret) return ret;
    if (packet[0] & 0x20) h->lux = get_unaligned_be16(packet + 42);
    return h->lux < 0 ? -EAGAIN : sysfs_emit(buf, "%u\n", h->lux);
}
static DEVICE_ATTR_RO(light_lux);
static irqreturn_t wake_interrupt(int irq, void *data)
{
    struct hub *h = data;
    u8 packet[88];
    int ret = msg_transfer(h, 4, 0, packet, sizeof(packet), true);
    if (ret) return IRQ_HANDLED;
    if ((packet[0] & 2) && packet[6] <= 2) {
        WRITE_ONCE(h->proximity, packet[6]);
        pm_wakeup_event(&h->spi->dev, 1000);
    }
    return IRQ_HANDLED;
}
static void clear_wake(void *data)
{
    struct device *dev = data;
    dev_pm_clear_wake_irq(dev);
    device_init_wakeup(dev, false);
}

static ssize_t proximity_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct hub *h = dev_get_drvdata(dev);
    int value = READ_ONCE(h->proximity);
    return value < 0 ? -EAGAIN : sysfs_emit(buf, "%u\n", value);
}
static DEVICE_ATTR_RO(proximity);
static struct attribute *attrs[] = { &dev_attr_accel_xyz.attr, &dev_attr_light_lux.attr, &dev_attr_proximity.attr, NULL };
static const struct attribute_group attrs_group = {.attrs = attrs};

static int probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct hub *h;
    // shortcut: fixed 25 Hz until sensorhub IRQ and runtime power management are ported.
    u8 id[2], orientations[] = {5, 5}, swapped = 1, period = 40, enabled[] = {0x21, 0, 0};
    u8 wake_enabled[] = {2, 0, 0}, als_period[] = {0, 200}, initialized = 1;
    // CT406 calibration from msm8916-osprey.dtsi and inherited moto-common defaults.
    u8 calibration[] = {0,150, 0,115, 0,100, 3,2,
        3,134, 6,147, 0,128, 1,31, 1,222, 0,128};
    int ret, irq;
    if (WARN_ON(crc((const u8 *)"123456789", 9) != 0x31c3))
        return -EINVAL;
    h = devm_kzalloc(dev, sizeof(*h), GFP_KERNEL);
    if (!h)
        return -ENOMEM;
    h->spi = spi;
    h->lux = h->proximity = -1;
    mutex_init(&h->lock);
    spi_set_drvdata(spi, h);
    ret = devm_regulator_get_enable(dev, "sensor1");
    if (ret)
        return dev_err_probe(dev, ret, "sensor1 supply\n");
    ret = devm_regulator_get_enable(dev, "sensor2");
    if (ret)
        return dev_err_probe(dev, ret, "sensor2 supply\n");
    h->ready = devm_gpiod_get(dev, "ready", GPIOD_IN);
    if (IS_ERR(h->ready)) return PTR_ERR(h->ready);
    h->ack = devm_gpiod_get(dev, "ack", GPIOD_IN);
    if (IS_ERR(h->ack)) return PTR_ERR(h->ack);
    h->boot = devm_gpiod_get(dev, "boot", GPIOD_OUT_LOW);
    if (IS_ERR(h->boot)) return PTR_ERR(h->boot);
    h->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(h->reset)) return PTR_ERR(h->reset);
    h->wake = devm_gpiod_get(dev, "wake", GPIOD_OUT_HIGH);
    if (IS_ERR(h->wake)) return PTR_ERR(h->wake);
    msleep(20);
    gpiod_set_value_cansleep(h->reset, 1);
    msleep(1000);
    ret = reg_transfer(h, 0, id, sizeof(id), true);
    if (ret)
        return dev_err_probe(dev, ret, "sensorhub ID read\n");
    dev_info(dev, "sensorhub ID: %02x %02x\n", id[0], id[1]);
    ret = reg_transfer(h, 4, &initialized, 1, false);
    if (!ret) ret = reg_transfer(h, 6, orientations, 2, false);
    if (!ret) ret = reg_transfer(h, 7, &swapped, 1, false);
    if (!ret) ret = reg_transfer(h, 0x16, &period, 1, false);
    if (!ret) ret = reg_transfer(h, 0x13, &period, 1, false);
    if (!ret) ret = reg_transfer(h, 0x33, calibration, sizeof(calibration), false);
    if (!ret) ret = reg_transfer(h, 0x6f, als_period, 2, false);
    if (!ret) ret = reg_transfer(h, 0x1a, enabled, 3, false);
    if (!ret) ret = reg_transfer(h, 0x1b, wake_enabled, 3, false);
    if (ret)
        return dev_err_probe(dev, ret, "sensorhub configuration\n");
    h->wakeirq = devm_gpiod_get(dev, "wakeirq", GPIOD_IN);
    if (IS_ERR(h->wakeirq)) return PTR_ERR(h->wakeirq);
    irq = gpiod_to_irq(h->wakeirq);
    if (irq < 0) return irq;
    ret = devm_request_threaded_irq(dev, irq, NULL, wake_interrupt,
                                   IRQF_TRIGGER_RISING | IRQF_ONESHOT, "stml0xx-wake", h);
    if (ret) return ret;
    ret = device_init_wakeup(dev, true);
    if (ret) return ret;
    ret = devm_add_action_or_reset(dev, clear_wake, dev);
    if (ret) return ret;
    ret = dev_pm_set_wake_irq(dev, irq);
    if (ret) return ret;
    wake_interrupt(irq, h);
    return devm_device_add_group(dev, &attrs_group);
}

static const struct of_device_id matches[] = {{ .compatible = "motorola,stml0xx-accel" }, {}};
MODULE_DEVICE_TABLE(of, matches);
static struct spi_driver stml_driver = {
    .driver = {.name = "stml0xx-accel", .of_match_table = matches},
    .probe = probe,
};
module_spi_driver(stml_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Osprey STML0xx accelerometer register access");
