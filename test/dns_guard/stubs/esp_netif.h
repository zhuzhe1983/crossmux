#pragma once
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
using esp_netif_callback_fn = esp_err_t (*)(void*);
extern "C" esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn, void*);
