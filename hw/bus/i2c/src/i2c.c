/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include "defs/error.h"
#include "hal/hal_i2c.h"
#include "bus/bus.h"
#include "bus/bus_debug.h"
#include "bus/i2c.h"

static int
bus_i2c_enable(struct bus_dev *bdev)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)bdev;
    int rc;

    BUS_DEBUG_VERIFY_DEV(dev);

    rc = hal_i2c_enable(dev->cfg.i2c_num);
    if (rc) {
        return SYS_EINVAL;
    }

    return 0;
}

static int
bus_i2c_configure(struct bus_dev *bdev, struct bus_node *bnode)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)bdev;
    struct bus_i2c_node *node = (struct bus_i2c_node *)bnode;
    struct bus_i2c_node *current_node = (struct bus_i2c_node *)bdev->configured_for;
    struct hal_i2c_settings i2c_cfg;
    int rc;

    BUS_DEBUG_VERIFY_DEV(dev);
    BUS_DEBUG_VERIFY_NODE(node);

    if (current_node && (current_node->freq == node->freq)) {
        return 0;
    }

    rc = hal_i2c_disable(dev->cfg.i2c_num);
    if (rc) {
        goto done;
    }

    i2c_cfg.frequency = node->freq;

    rc = hal_i2c_config(dev->cfg.i2c_num, &i2c_cfg);
    if (rc) {
        goto done;
    }

    rc = hal_i2c_enable(dev->cfg.i2c_num);

done:
    if (rc) {
        rc = SYS_EIO;
    }

    return rc;
}

static int
bus_i2c_read(struct bus_dev *bdev, struct bus_node *bnode, uint8_t *buf,
             uint16_t length, os_time_t timeout, uint16_t flags)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)bdev;
    struct bus_i2c_node *node = (struct bus_i2c_node *)bnode;
    struct hal_i2c_master_data i2c_data;
    uint8_t last_op;
    int rc;

    BUS_DEBUG_VERIFY_DEV(dev);
    BUS_DEBUG_VERIFY_NODE(node);

    i2c_data.address = node->addr;
    i2c_data.buffer = buf;
    i2c_data.len = length;

    last_op = !(flags & BUS_F_NOSTOP);

    rc = hal_i2c_master_read(dev->cfg.i2c_num, &i2c_data,
                             os_time_ticks_to_ms32(timeout), last_op);

    return rc;
}

static int
bus_i2c_write(struct bus_dev *bdev, struct bus_node *bnode, const uint8_t *buf,
              uint16_t length, os_time_t timeout, uint16_t flags)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)bdev;
    struct bus_i2c_node *node = (struct bus_i2c_node *)bnode;
    struct hal_i2c_master_data i2c_data;
    uint8_t last_op;
    int rc;

    BUS_DEBUG_VERIFY_DEV(dev);
    BUS_DEBUG_VERIFY_NODE(node);

    i2c_data.address = node->addr;
    i2c_data.buffer = (uint8_t *)buf;
    i2c_data.len = length;

    last_op = !(flags & BUS_F_NOSTOP);

    rc = hal_i2c_master_write(dev->cfg.i2c_num, &i2c_data,
                              os_time_ticks_to_ms32(timeout), last_op);

    return rc;
}

static int bus_i2c_disable(struct bus_dev *bdev)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)bdev;
    int rc;

    BUS_DEBUG_VERIFY_DEV(dev);

    rc = hal_i2c_disable(dev->cfg.i2c_num);
    if (rc) {
        return SYS_EINVAL;
    }

    return 0;
}

static const struct bus_dev_ops bus_i2c_ops = {
    .enable = bus_i2c_enable,
    .configure = bus_i2c_configure,
    .read = bus_i2c_read,
    .write = bus_i2c_write,
    .disable = bus_i2c_disable,
};

int
bus_i2c_dev_init_func(struct os_dev *odev, void *arg)
{
    struct bus_i2c_dev *dev = (struct bus_i2c_dev *)odev;
    struct bus_i2c_dev_cfg *cfg = arg;
    struct hal_i2c_hw_settings hal_cfg;
    int rc;

    BUS_DEBUG_POISON_DEV(dev);

    hal_cfg.pin_scl = cfg->pin_scl;
    hal_cfg.pin_sda = cfg->pin_sda;
    rc = hal_i2c_init_hw(cfg->i2c_num, &hal_cfg);
    if (rc) {
        return SYS_EINVAL;
    }

    rc = bus_dev_init_func(odev, (void*)&bus_i2c_ops);
    assert(rc == 0);

    dev->cfg = *cfg;

    rc = hal_i2c_enable(dev->cfg.i2c_num);
    assert(rc == 0);

    return 0;
}

int
bus_i2c_node_init_func(struct os_dev *odev, void *arg)
{
    struct bus_i2c_node *node = (struct bus_i2c_node *)odev;
    struct bus_i2c_node_cfg *cfg = arg;
    struct bus_node_cfg *node_cfg = &cfg->node_cfg;
    int rc;

    BUS_DEBUG_POISON_NODE(node);

    rc = bus_node_init_func(odev, node_cfg);
    if (rc) {
        return rc;
    }

    node->freq = cfg->freq;
    node->addr = cfg->addr;
    node->quirks = cfg->quirks;

    return 0;
}
