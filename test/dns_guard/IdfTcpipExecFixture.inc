// SPDX-FileCopyrightText: 2019-2026 Espressif Systems (Shanghai) CO LTD
// SPDX-License-Identifier: Apache-2.0
// Verbatim function bodies from ESP-IDF v5.5.5:
// https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_netif/lwip/esp_netif_lwip.c
// Lines 176-209, 220-227, 706-714. Host declarations/OS primitives live in
// HalDnsGuardTest.cpp. Keep this fixture unchanged when testing the HAL wrapper.

static void esp_netif_api_cb(void *api_msg)
{
    esp_netif_api_msg_t *msg = (esp_netif_api_msg_t *)api_msg;

    if (!msg || !msg->api_fn) {
        ESP_LOGD(TAG, "null msg/api_fn");
        return;
    }

    msg->ret = msg->api_fn(msg);
    ESP_LOGD(TAG, "call api in lwip: ret=0x%x, give sem", msg->ret);
#if !LWIP_TCPIP_CORE_LOCKING
    sys_sem_signal(&api_sync_sem);
#endif
}

static inline esp_err_t esp_netif_lwip_ipc_call_msg(esp_netif_api_msg_t *msg)
{
    if (!sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER)) {
        ESP_LOGD(TAG, "check: remote, if=%p fn=%p", msg->esp_netif, msg->api_fn);
#if LWIP_TCPIP_CORE_LOCKING
        tcpip_send_msg_wait_sem((tcpip_callback_fn)esp_netif_api_cb, msg, NULL);
#else
        sys_arch_sem_wait(&api_lock_sem, 0);
        tcpip_send_msg_wait_sem((tcpip_callback_fn)esp_netif_api_cb, msg, &api_sync_sem);
        sys_sem_signal(&api_lock_sem);
#endif /* LWIP_TCPIP_CORE_LOCKING */
        return msg->ret;
    }
    ESP_LOGD(TAG, "check: local, if=%p fn=%p",  msg->esp_netif, msg->api_fn);
    return msg->api_fn(msg);
}

static inline esp_err_t esp_netif_lwip_ipc_call_fn(esp_netif_api_fn fn, esp_netif_callback_fn user_fn, void *ctx)
{
    esp_netif_api_msg_t msg = {
            .user_fn = user_fn,
            .data = ctx,
            .api_fn = fn
    };
    return esp_netif_lwip_ipc_call_msg(&msg);
}

static esp_err_t tcpip_exec_api(esp_netif_api_msg_t *msg)
{
    return msg->user_fn(msg->data);
}

esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void*ctx)
{
    return esp_netif_lwip_ipc_call_fn(tcpip_exec_api, fn, ctx);
}
