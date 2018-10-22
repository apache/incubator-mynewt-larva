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

#include <string.h>
#include <errno.h>
#include <assert.h>

#include <os/mynewt.h>
#include <console/console.h>
#include <hal/hal_i2c.h>
#include <mcu/nrf52_hal.h>
#include <hal/hal_gpio.h>
#include <adp5061/adp5061.h>
#include <bsp/bsp.h>
#include <charge-control/charge_control.h>
#include <i2cn/i2cn.h>
#include "adp5061_priv.h"

/**
* Default driver configuration
*/
static const struct adp5061_config default_config = {
    /* ILIM = 600mA */
    .vinx_pin_settings = 0x07,
    /* VTRM = 4.32V, CHG_LIM = 3.2V */
    .termination_settings = 0xA4,
    /* ICHG = 250mA, ITRK_DEAD = 20ma */
    .charging_current = 0x22,
    /* DIS_RCH = 0, VRCH = 260mV, VTRK_DEAD=2.5V, VWEAK = 3.0V*/
    .voltage_thresholds = 0x6B,
    /* Timers enable*/
    .timer_settings = 0x38,
    /* EN_EOC, EN_CHG */
    .functional_settings_1 = 0x07,
    /* VSYSTEM = 4.3V, IDEAL_DIODE = 01 */
    .functional_settings_2 = 0x08,
    /* Int THR enable */
    .interrupt_enable = 0xFF,
    /* TBAT_SHR = 30s, VBAT_SHR = 2.4V*/
    .battery_short = 0x84,
    /* SYS_EN_SET = 1 */
    .iend = 0x01,
};

#if MYNEWT_VAL(ADP5061_INT_PIN) >= 0
/**
* ADP5061 interrupt handler CB
* gets interrupt status and prints to console
*/

static void
adp5061_event(struct os_event *ev)
{
    struct adp5061_dev *dev;

    assert(ev);
    dev = ev->ev_arg;

    /* Interrupt generated by charger triggers out of schedule read */
    charge_control_read(&dev->a_chg_ctrl,
            CHARGE_CONTROL_TYPE_STATUS | CHARGE_CONTROL_TYPE_FAULT,
            NULL, NULL, OS_TIMEOUT_NEVER);
}

/**
* ADP5061 interrupt handler structure
*/
static struct os_event interrupt_handler = {
    .ev_cb = adp5061_event,
};

/**
* ADP5061 IRQ
* Add interrupt handling task to queue
*/
static void
adp5061_isr(void *arg){
    os_eventq_put(os_eventq_dflt_get(), &interrupt_handler);
}
#endif

static int
adp5061_write_configuration(struct adp5061_dev *dev)
{
    int rc = 0;
    const uint8_t regs[] = {
            dev->a_cfg.vinx_pin_settings,
            dev->a_cfg.termination_settings,
            dev->a_cfg.charging_current,
            dev->a_cfg.voltage_thresholds,
            dev->a_cfg.timer_settings,
            dev->a_cfg.functional_settings_1,
            dev->a_cfg.functional_settings_2,
            dev->a_cfg.interrupt_enable
    };

    rc = adp5061_set_reg(dev, 0x10, dev->a_cfg.battery_short);
    if (rc) {
        goto err;
    }
    rc = adp5061_set_reg(dev, 0x11, dev->a_cfg.iend);
    if (rc) {
        goto err;
    }
    rc = adp5061_set_regs(dev, 0x02, regs, sizeof(regs));
    if (rc) {
        goto err;
    }
err:
    return rc;
}

int
adp5061_set_config(struct adp5061_dev *dev,
        const struct adp5061_config *cfg)
{
    int rc = 0;

    dev->a_cfg = *cfg;

    rc = adp5061_write_configuration(dev);

    return rc;
}

/**
 * Lock access to the charge_control_itf specified by cci. Blocks until lock acquired.
 *
 * @param The charge_ctrl_itf to lock
 * @param The timeout
 *
 * @return 0 on success, non-zero on failure.
 */
static int
ad5061_itf_lock(struct charge_control_itf *cci, uint32_t timeout)
{
    int rc;
    os_time_t ticks;

    if (!cci->cci_lock) {
        return 0;
    }

    rc = os_time_ms_to_ticks(timeout, &ticks);
    if (rc) {
        return rc;
    }

    rc = os_mutex_pend(cci->cci_lock, ticks);
    if (rc == 0 || rc == OS_NOT_STARTED) {
        return (0);
    }

    return (rc);
}

/**
 * Unlock access to the charge_control_itf specified by bi.
 *
 * @param The charge_control_itf to unlock access to
 *
 * @return 0 on success, non-zero on failure.
 */
static void
adp5061_itf_unlock(struct charge_control_itf *cci)
{
    if (!cci->cci_lock) {
        return;
    }

    os_mutex_release(cci->cci_lock);
}

int
adp5061_get_reg(struct adp5061_dev *dev, uint8_t addr, uint8_t *value)
{
    int rc = 0;
    uint8_t payload;
    struct hal_i2c_master_data data_struct = {
        .address = dev->a_chg_ctrl.cc_itf.cci_addr,
        .len = 1,
        .buffer = &payload
    };

    rc = ad5061_itf_lock(&dev->a_chg_ctrl.cc_itf, OS_TIMEOUT_NEVER);
    if (rc) {
        return rc;
    }

    /* Register write */
    payload = addr;
    rc = i2cn_master_write(dev->a_chg_ctrl.cc_itf.cci_num, &data_struct,
            MYNEWT_VAL(ADP5061_I2C_TIMEOUT_TICKS), 1, MYNEWT_VAL(ADP5061_I2C_RETRIES));
    if (rc) {
        goto err;
    }

    /* Read one byte back */
    payload = addr;
    rc = i2cn_master_read(dev->a_chg_ctrl.cc_itf.cci_num, &data_struct,
            MYNEWT_VAL(ADP5061_I2C_TIMEOUT_TICKS), 1, MYNEWT_VAL(ADP5061_I2C_RETRIES));
    *value = payload;

err:
    adp5061_itf_unlock(&dev->a_chg_ctrl.cc_itf);

    return rc;
}

int
adp5061_set_reg(struct adp5061_dev *dev, uint8_t addr, uint8_t value)
{
    int rc = 0;
    uint8_t payload[2] = { addr, value };
    struct hal_i2c_master_data data_struct = {
        .address = dev->a_chg_ctrl.cc_itf.cci_addr,
        .len = 2,
        .buffer = payload
    };

    rc = ad5061_itf_lock(&dev->a_chg_ctrl.cc_itf, OS_TIMEOUT_NEVER);
    if (rc) {
        return rc;
    }

    rc = i2cn_master_write(dev->a_chg_ctrl.cc_itf.cci_num, &data_struct,
            MYNEWT_VAL(ADP5061_I2C_TIMEOUT_TICKS), 1, MYNEWT_VAL(ADP5061_I2C_RETRIES));

    adp5061_itf_unlock(&dev->a_chg_ctrl.cc_itf);

    return rc;
}

int
adp5061_set_regs(struct adp5061_dev *dev, uint8_t addr,
        const uint8_t *values, int count)
{
    int rc = 0;
    int i;
    uint8_t payload[1 + count];
    struct hal_i2c_master_data data_struct = {
        .address = dev->a_chg_ctrl.cc_itf.cci_addr,
        .len = count + 1,
        .buffer = payload
    };

    payload[0] = addr;
    for (i = 0; i < count; ++i) {
        payload[i + 1] = values[i];
    }

    rc = ad5061_itf_lock(&dev->a_chg_ctrl.cc_itf, OS_TIMEOUT_NEVER);
    if (rc) {
        return rc;
    }

    rc = i2cn_master_write(dev->a_chg_ctrl.cc_itf.cci_num, &data_struct,
            MYNEWT_VAL(ADP5061_I2C_TIMEOUT_TICKS), 1, MYNEWT_VAL(ADP5061_I2C_RETRIES));

    adp5061_itf_unlock(&dev->a_chg_ctrl.cc_itf);

    return rc;
}

int
adp5061_get_device_id(struct adp5061_dev *dev, uint8_t *dev_id)
{
    return adp5061_get_reg(dev, REG_PART_ID, dev_id);
}

int
adp5061_set_charge_currents(struct adp5061_dev *dev, uint8_t ichg,
        uint8_t itrk_dead, uint8_t i_lim)
{
    int rc = 0;

    if ((ichg > ((1<<ADP5061_ICHG_LEN)-1)) ||
        (itrk_dead > ((1<<ADP5061_ITRK_DEAD_LEN)-1))) {
        assert(0);
    }
    dev->a_cfg.charging_current &= ~(ADP5061_ICHG_MASK |
            ADP5061_ITRK_DEAD_MASK);
    dev->a_cfg.charging_current |= (ADP5061_ICHG_SET(ichg) |
            ADP5061_ITRK_DEAD_SET(itrk_dead));

    rc = adp5061_set_reg(dev, REG_CHARGING_CURRENT,
            dev->a_cfg.charging_current);
    if (rc != 0) {
        goto err;
    }

    /* ILIM in REG_VIN_PIN_SETTINGS*/
    dev->a_cfg.vinx_pin_settings &= ~(ADP5061_VIN_SETTINGS_MASK);
    dev->a_cfg.vinx_pin_settings |= ADP5061_VIN_SETTINGS_SET(i_lim);

    rc = adp5061_set_reg(dev, REG_VIN_PIN_SETTINGS,
            dev->a_cfg.vinx_pin_settings);
err:
    return rc;
}

int
adp5061_set_charge_voltages(struct adp5061_dev *dev, uint8_t vweak,
        uint8_t vtrk_dead, uint8_t vtrm, uint8_t chg_vlim, uint8_t vrch)
{
    int rc = 0;

    if (vweak > ((1<<ADP5061_VOLTAGE_THRES_VWEAK_LEN)-1) ||
        vtrk_dead > ((1<<ADP5061_VOLTAGE_THRES_VTRK_DEAD_LEN)-1)) {
        assert(0);
    }
    /* VWEAK, V_RCH, VTRK_DEAD in REG_VOLTAGE_THRES */
    dev->a_cfg.voltage_thresholds &= ~(ADP5061_VOLTAGE_THRES_VWEAK_MASK |
            ADP5061_VOLTAGE_THRES_VTRK_DEAD_MASK |
            ADP5061_VOLTAGE_THRES_VRCH_MASK);
    dev->a_cfg.voltage_thresholds |= (ADP5061_VOLTAGE_THRES_VWEAK_SET(vweak) |
            ADP5061_VOLTAGE_THRES_VTRK_DEAD_SET(vtrk_dead) |
            ADP5061_VOLTAGE_THRES_VRCH_SET(vrch));

    rc = adp5061_set_reg(dev, REG_VOLTAGE_THRES,
            dev->a_cfg.voltage_thresholds);
    if (rc != 0) {
        goto err;
    }
    /* VTRM & CHG_VLIM in REG_TERM_SETTINGS */
    dev->a_cfg.termination_settings &=
            (uint8_t)~(ADP5061_VTRM_MASK | ADP5061_CHG_VLIM_MASK);
    dev->a_cfg.termination_settings |=
            (ADP5061_VTRM_SET(vtrm) | ADP5061_CHG_VLIM_SET(chg_vlim));

    rc = adp5061_set_reg(dev, REG_TERM_SETTINGS,
            dev->a_cfg.termination_settings);
err:
    return rc;
}

int
adp5061_charge_enable(struct adp5061_dev *dev)
{
    int rc = 0;

    dev->a_cfg.functional_settings_1 |= ADP5061_FUNC_SETTINGS_1_EN_CHG_SET(1);
    rc = adp5061_set_reg(dev, REG_FUNC_SETTINGS_1,
            dev->a_cfg.functional_settings_1);

    return rc;
}

int
adp5061_charge_disable(struct adp5061_dev *dev)
{
    int rc = 0;

    dev->a_cfg.functional_settings_1 &= ~(ADP5061_FUNC_SETTINGS_1_EN_CHG_MASK);
    rc = adp5061_set_reg(dev, REG_FUNC_SETTINGS_1,
            dev->a_cfg.functional_settings_1);

    return rc;
}

int
adp5061_recharge_enable(struct adp5061_dev *dev)
{
    int rc = 0;

    dev->a_cfg.voltage_thresholds |= ADP5061_VOLTAGE_THRES_DIS_RCH_MASK;
    rc = adp5061_set_reg(dev, REG_VOLTAGE_THRES,
            dev->a_cfg.voltage_thresholds);

    return rc;
}

int
adp5061_recharge_disable(struct adp5061_dev *dev)
{
    int rc = 0;

    dev->a_cfg.voltage_thresholds &= ~ADP5061_VOLTAGE_THRES_DIS_RCH_MASK;
    rc = adp5061_set_reg(dev, REG_VOLTAGE_THRES,
            dev->a_cfg.voltage_thresholds);

    return rc;
}

int
adp5061_enable_int(struct adp5061_dev *dev, uint8_t mask)
{
    return adp5061_set_reg(dev, REG_INT_EN, mask);
}

static int
adp5061_chg_ctrl_read(struct charge_control *cc, charge_control_type_t type,
        charge_control_data_func_t data_func, void *data_arg, uint32_t timeout)
{
    struct adp5061_dev *dev = (struct adp5061_dev *)cc->cc_dev;
    uint8_t charger_status_1_reg = 0;
    uint8_t int_reg;
    charge_control_status_t status = 0;
    charge_control_fault_t fault = CHARGE_CONTROL_FAULT_NONE;
    int rc = 0;

    rc = adp5061_get_reg(dev, REG_INT_ACTIVE, &int_reg);
    if (rc) {
        goto err;
    }
    if (int_reg) {
        console_printf("ADP5061 interrupt 0x%02X\n", int_reg);
    }
    if ((type & (CHARGE_CONTROL_TYPE_STATUS | CHARGE_CONTROL_TYPE_FAULT))) {
        rc = adp5061_get_reg(dev, REG_CHARGER_STATUS_1, &charger_status_1_reg);
        if (rc) {
            goto err;
        }
    }
    if (ADP5061_INT_EN_CHG_GET(int_reg)) {
        /* Change in charge state detected
         * If VIN_OK is 1 apply requested settings.
         */
        if (ADP5061_CHG_STATUS_1_VIN_OK_GET(charger_status_1_reg)) {
            adp5061_write_configuration(dev);
            rc = adp5061_get_reg(dev, REG_CHARGER_STATUS_1,
                    &charger_status_1_reg);
            if (rc) {
                goto err;
            }
        }
    }
    if (type & CHARGE_CONTROL_TYPE_STATUS) {
        switch (ADP5061_CHG_STATUS_1_GET(charger_status_1_reg)) {
        case ADP5061_CHG_STATUS_OFF:
            status = CHARGE_CONTROL_STATUS_NO_SOURCE;
            break;
        case ADP5061_CHG_STATUS_TCK_CHG:
        case ADP5061_CHG_STATUS_FAST_CHG_CC:
        case ADP5061_CHG_STATUS_FAST_CHG_CV:
            status = CHARGE_CONTROL_STATUS_CHARGING;
            break;
        case ADP5061_CHG_STATUS_CHG_COMPLETE:
            status = CHARGE_CONTROL_STATUS_CHARGE_COMPLETE;
            break;
        case ADP5061_CHG_STATUS_LDO_MODE:
        case ADP5061_CHG_STATUS_TCK_EXP:
            status = CHARGE_CONTROL_STATUS_SUSPEND;
            break;
        case ADP5061_CHG_STATUS_BAT_DET:
            status = CHARGE_CONTROL_STATUS_DISABLED;
            break;
        }
        if (data_func) {
            data_func(cc, data_arg, &status, CHARGE_CONTROL_TYPE_STATUS);
        }
    }
    if (type & CHARGE_CONTROL_TYPE_FAULT) {
        if (ADP5061_CHG_STATUS_1_VIN_OV_GET(charger_status_1_reg)) {
            fault |= CHARGE_CONTROL_FAULT_OV;
        }
        if (!ADP5061_CHG_STATUS_1_VIN_OK_GET(charger_status_1_reg)) {
            fault |= CHARGE_CONTROL_FAULT_UV;
        }
        if (ADP5061_CHG_STATUS_1_THERM_LIM_GET(charger_status_1_reg) ||
                ADP5061_CHG_STATUS_1_VIN_ILIM_GET(charger_status_1_reg)) {
            fault |= CHARGE_CONTROL_FAULT_ILIM;
        }
        if (ADP5061_INT_ACTIVE_TSD_GET(int_reg)) {
            fault |= CHARGE_CONTROL_FAULT_THERM;
        }
        if (fault && data_func) {
            data_func(cc, data_arg, &fault, CHARGE_CONTROL_TYPE_FAULT);
        }
    }
err:
    return rc;
}

static int
adp5061_chg_ctrl_get_config(struct charge_control *cc,
        charge_control_type_t type, struct charge_control_cfg *cfg)
{
    struct adp5061_dev *adp5061 = (struct adp5061_dev *)cc->cc_dev;

    *(struct adp5061_config *)cfg = adp5061->a_cfg;

    return 0;
}

static int
adp5061_chg_ctrl_set_config(struct charge_control *cc, void *cfg)
{
    struct adp5061_dev *adp5061 = (struct adp5061_dev *)cc->cc_dev;
    int rc;

    rc = adp5061_set_config(adp5061, (struct adp5061_config *)cfg);
    if (rc == 0) {
        adp5061->a_cfg = *(struct adp5061_config *)cfg;
    }
    return 0;
}

static int
adp5061_chg_ctrl_get_status(struct charge_control *cc, int *status)
{
    int rc;
    uint8_t reg_value;
    struct adp5061_dev *adp5061 = (struct adp5061_dev *)cc->cc_dev;

    rc = adp5061_get_reg(adp5061, REG_CHARGER_STATUS_1, &reg_value);
    if (rc) {
        goto err;
    }
    *status = reg_value;
    rc = adp5061_get_reg(adp5061, REG_CHARGER_STATUS_2, &reg_value);
    if (rc) {
        goto err;
    }
    *status |= (((uint16_t)reg_value) << 8);
err:
    return rc;
}

static int
adp5061_chg_ctrl_get_fault(struct charge_control *cc,
        charge_control_fault_t *fault)
{
    uint8_t reg_value;
    struct adp5061_dev *adp5061 = (struct adp5061_dev *)cc->cc_dev;
    int rc;

    rc = adp5061_get_reg(adp5061, REG_CHARGER_STATUS_1, &reg_value);
    *fault = 0;
    if (reg_value & ADP5061_CHG_STATUS_1_VIN_OV_MASK) {
        *fault |= CHARGE_CONTROL_FAULT_OV;
    }
    if (reg_value & ADP5061_CHG_STATUS_1_VIN_ILIM_MASK) {
        *fault |= CHARGE_CONTROL_FAULT_ILIM;
    }
    if (reg_value & ADP5061_CHG_STATUS_1_THERM_LIM_MASK) {
        *fault |= CHARGE_CONTROL_FAULT_THERM;
    }

    return rc;
}

static int
adp5061_chg_ctrl_enable(struct charge_control *cc)
{
    return adp5061_charge_enable((struct adp5061_dev *)cc->cc_dev);
}

static int
adp5061_chg_ctrl_disable(struct charge_control *cc)
{
    return adp5061_charge_disable((struct adp5061_dev *)cc->cc_dev);
}

/* Exports for the charge control API */

static const struct charge_control_driver g_adp5061_chg_ctrl_driver = {
    .ccd_read = adp5061_chg_ctrl_read,
    .ccd_get_config = adp5061_chg_ctrl_get_config,
    .ccd_set_config = adp5061_chg_ctrl_set_config,
    .ccd_get_status = adp5061_chg_ctrl_get_status,
    .ccd_get_fault = adp5061_chg_ctrl_get_fault,
    .ccd_enable = adp5061_chg_ctrl_enable,
    .ccd_disable = adp5061_chg_ctrl_disable,
};

int
adp5061_init(struct os_dev *dev, void *arg)
{
    struct adp5061_dev *adp5061 = (struct adp5061_dev *)dev;
    struct charge_control *cc;
    struct charge_control_itf *cci;
    const struct adp5061_config *cfg;
    uint8_t device_id;
    int rc;

    if (!dev) {
        rc = SYS_ENODEV;
        goto err;
    }

    cc = &adp5061->a_chg_ctrl;

    cci = (struct charge_control_itf *)arg;

    rc = charge_control_init(cc, dev);
    if (rc) {
        goto err;
    }

    charge_control_set_interface(cc, cci);

    /* Add the driver with all the supported types */
    rc = charge_control_set_driver(cc, CHARGE_CONTROL_TYPE_STATUS,
            (struct charge_control_driver *)&g_adp5061_chg_ctrl_driver);
    if (rc) {
        goto err;
    }

    charge_control_set_type_mask(cc,
            CHARGE_CONTROL_TYPE_STATUS | CHARGE_CONTROL_TYPE_FAULT);

    cfg = &default_config;

    rc = adp5061_get_device_id(adp5061, &device_id);
    if (rc) {
        goto err;
    }
    /* Sanity check, device ID as specified in ADP5061 datasheet */
    if (device_id != 0x19) {
        rc = SYS_ENODEV;
        goto err;
    }

#if MYNEWT_VAL(ADP5061_INT_PIN) >= 0
    hal_gpio_irq_init(MYNEWT_VAL(ADP5061_INT_PIN), adp5061_isr, adp5061,
                      HAL_GPIO_TRIG_FALLING, HAL_GPIO_PULL_NONE);
#endif
    rc = adp5061_set_config(adp5061, cfg);
    if (rc) {
        goto err;
    }

    rc = charge_control_mgr_register(cc);
    if (rc) {
        goto err;
    }
#if MYNEWT_VAL(ADP5061_CLI)
    adp5061_shell_init(adp5061);
#endif

    return 0;
err:
    return rc;
}
