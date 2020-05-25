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

#include "os/mynewt.h"
#include "syscfg/syscfg.h"
#include "oic/oc_gatt.h"

/* BLE */
#include "nimble/ble.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

#include "bleprph.h"

static int
bleprph_gap_event(struct ble_gap_event *event, void *arg);

/**
 * Logs information about a connection to the console.
 */
static void
bleprph_print_conn_desc(struct ble_gap_conn_desc *desc)
{
    DFLT_LOG_INFO("handle=%d our_ota_addr_type=%d our_ota_addr=",
                  desc->conn_handle, desc->our_ota_addr.type);
    print_addr(desc->our_ota_addr.val);
    DFLT_LOG_INFO(" our_id_addr_type=%d our_id_addr=",
                  desc->our_id_addr.type);
    print_addr(desc->our_id_addr.val);
    DFLT_LOG_INFO(" peer_ota_addr_type=%d peer_ota_addr=",
                  desc->peer_ota_addr.type);
    print_addr(desc->peer_ota_addr.val);
    DFLT_LOG_INFO(" peer_id_addr_type=%d peer_id_addr=",
                  desc->peer_id_addr.type);
    print_addr(desc->peer_id_addr.val);
    DFLT_LOG_INFO(" conn_itvl=%d conn_latency=%d supervision_timeout=%d "
                  "encrypted=%d authenticated=%d bonded=%d\n",
                  desc->conn_itvl, desc->conn_latency,
                  desc->supervision_timeout,
                  desc->sec_state.encrypted,
                  desc->sec_state.authenticated,
                  desc->sec_state.bonded);
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
void
bleprph_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    /**
     *  Set the advertisement data included in our advertisements:
     *     o Flags (indicates advertisement type and other general info).
     *     o Advertising tx power.
     *     o Device name.
     *     o service UUID.
     */

    memset(&fields, 0, sizeof fields);

    /* Advertise two flags:
     *     o Discoverability in forthcoming advertisement (general)
     *     o BLE-only (BR/EDR unsupported).
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Indicate that the TX power level field should be included; have the
     * stack fill this value automatically.  This is done by assiging the
     * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
     */
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *) name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

#if MYNEWT_VAL(ADVERTISE_128BIT_UUID)
    /* Advertise the 128-bit CoAP-over-BLE service UUID in the scan response. */
    fields.uuids128 = (ble_uuid128_t []) {
        BLE_UUID128_INIT(OC_GATT_UNSEC_SVC_UUID)
    };
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
#endif
#if MYNEWT_VAL(ADVERTISE_16BIT_UUID)
    /* Advertise the 16-bit CoAP-over-BLE service UUID in the scan response. */
    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(OC_GATT_SEC_SVC_UUID)
    };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
#endif
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        DFLT_LOG_ERROR("error setting advertisement data; rc=%d\n", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, bleprph_gap_event, NULL);
    if (rc != 0) {
        DFLT_LOG_ERROR("error enabling advertisement; rc=%d\n", rc);
        return;
    }
}

/**
 * The nimble host executes this callback when a GAP event occurs.  The
 * application associates a GAP event callback with each connection that forms.
 * bleprph uses the same callback for all connections.
 *
 * @param event                 The type of event being signalled.
 * @param ctxt                  Various information pertaining to the event.
 * @param arg                   Application-specified argument; unuesd by
 *                                  bleprph.
 *
 * @return                      0 if the application successfully handled the
 *                                  event; nonzero on failure.  The semantics
 *                                  of the return code is specific to the
 *                                  particular GAP event being signalled.
 */
static int
bleprph_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        /* A new connection was established or a connection attempt failed. */
        DFLT_LOG_INFO("connection %s; status=%d ",
                      event->connect.status == 0 ? "established" : "failed",
                      event->connect.status);
        if (event->connect.status == 0) {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            bleprph_print_conn_desc(&desc);
        }
        DFLT_LOG_INFO("\n");

        if (event->connect.status != 0) {
            /* Connection failed; resume advertising. */
            bleprph_advertise();
        } else {
            oc_ble_coap_conn_new(event->connect.conn_handle);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        DFLT_LOG_INFO("disconnect; reason=%d ", event->disconnect.reason);
        bleprph_print_conn_desc(&event->disconnect.conn);
        DFLT_LOG_INFO("\n");

        oc_ble_coap_conn_del(event->disconnect.conn.conn_handle);

        /* Connection terminated; resume advertising. */
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        /* The central has updated the connection parameters. */
        DFLT_LOG_INFO("connection updated; status=%d ",
                      event->conn_update.status);
        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        DFLT_LOG_INFO("\n");
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        DFLT_LOG_INFO("advertise complete; reason=%d\n",
                      event->adv_complete.reason);
        bleprph_advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        /* Encryption has been enabled or disabled for this connection. */
        DFLT_LOG_INFO("encryption change event; status=%d ",
                      event->enc_change.status);
        rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
        assert(rc == 0);
        bleprph_print_conn_desc(&desc);
        DFLT_LOG_INFO("\n");
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        DFLT_LOG_INFO("subscribe event; conn_handle=%d attr_handle=%d "
                      "reason=%d prevn=%d curn=%d previ=%d curi=%d\n",
                      event->subscribe.conn_handle,
                      event->subscribe.attr_handle,
                      event->subscribe.reason,
                      event->subscribe.prev_notify,
                      event->subscribe.cur_notify,
                      event->subscribe.prev_indicate,
                      event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        DFLT_LOG_INFO("mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                      event->mtu.conn_handle,
                      event->mtu.channel_id,
                      event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* We already have a bond with the peer, but it is attempting to
         * establish a new secure link.  This app sacrifices security for
         * convenience: just throw away the old bond and accept the new link.
         */

        /* Delete the old bond. */
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);

        /* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
         * continue with the pairing operation.
         */
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    return 0;
}
