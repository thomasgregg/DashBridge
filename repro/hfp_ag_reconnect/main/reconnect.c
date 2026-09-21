/*
 * Minimal persisted-peer reconnect hook for Espressif's stock HFP AG example.
 * It deliberately contains no MAP, SPP, UART relay, audio relay, or custom SDP.
 */

#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_ag_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"

#include "reconnect.h"

#define RECONNECT_TAG "HFP_RECONNECT_REPRO"
#define RECONNECT_NAMESPACE "hfp_repro"
#define RECONNECT_KEY "peer"
#define RECONNECT_INITIAL_DELAY_MS 5000U
#define RECONNECT_MAX_DELAY_MS 60000U
#define RECONNECT_ATTEMPT_TIMEOUT_MS 20000U

typedef enum {
    RECONNECT_EVENT_PROFILE_READY,
    RECONNECT_EVENT_CONNECTION_STATE,
} reconnect_event_type_t;

typedef struct {
    reconnect_event_type_t type;
    esp_hf_connection_state_t state;
    esp_bd_addr_t address;
} reconnect_event_t;

static QueueHandle_t s_events;

static void log_address(const char *message, const esp_bd_addr_t address, uint8_t status)
{
    ESP_LOGI(RECONNECT_TAG,
             "%s %02x:%02x:%02x:%02x:%02x:%02x status/reason=0x%02x",
             message,
             address[0], address[1], address[2],
             address[3], address[4], address[5], status);
}

static void reconnect_gap_callback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
            log_address("ACL connect complete",
                        param->acl_conn_cmpl_stat.bda,
                        param->acl_conn_cmpl_stat.stat);
            break;
        case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
            log_address("ACL disconnected",
                        param->acl_disconn_cmpl_stat.bda,
                        param->acl_disconn_cmpl_stat.reason);
            break;
        default:
            break;
    }
}

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool is_bonded(const esp_bd_addr_t address)
{
    int count = esp_bt_gap_get_bond_device_num();
    if (count < 1) {
        return false;
    }

    esp_bd_addr_t *bonds = calloc((size_t)count, sizeof(esp_bd_addr_t));
    if (bonds == NULL) {
        return false;
    }

    int returned = count;
    bool found = false;
    if (esp_bt_gap_get_bond_device_list(&returned, bonds) == ESP_OK) {
        for (int i = 0; i < returned; ++i) {
            if (memcmp(address, bonds[i], ESP_BD_ADDR_LEN) == 0) {
                found = true;
                break;
            }
        }
    }
    free(bonds);
    return found;
}

static esp_err_t save_peer(const esp_bd_addr_t address)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(RECONNECT_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) {
        return error;
    }
    error = nvs_set_blob(handle, RECONNECT_KEY, address, ESP_BD_ADDR_LEN);
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    nvs_close(handle);
    return error;
}

static bool load_peer(esp_bd_addr_t address)
{
    nvs_handle_t handle;
    size_t size = ESP_BD_ADDR_LEN;
    esp_err_t error = nvs_open(RECONNECT_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_blob(handle, RECONNECT_KEY, address, &size);
        nvs_close(handle);
    }
    if (error == ESP_OK && size == ESP_BD_ADDR_LEN && is_bonded(address)) {
        ESP_LOGI(RECONNECT_TAG, "Loaded saved bonded peer");
        return true;
    }

    int count = esp_bt_gap_get_bond_device_num();
    if (count != 1) {
        ESP_LOGW(RECONNECT_TAG,
                 "No saved bonded peer and bond count is %d; connect manually once", count);
        return false;
    }

    int returned = 1;
    esp_bd_addr_t only_bond;
    if (esp_bt_gap_get_bond_device_list(&returned, &only_bond) != ESP_OK || returned != 1) {
        ESP_LOGW(RECONNECT_TAG, "Could not read the single bonded peer");
        return false;
    }

    memcpy(address, only_bond, ESP_BD_ADDR_LEN);
    error = save_peer(address);
    if (error != ESP_OK) {
        ESP_LOGE(RECONNECT_TAG, "Could not save adopted peer: %s", esp_err_to_name(error));
    }
    ESP_LOGI(RECONNECT_TAG, "Adopted the only existing bonded peer");
    return true;
}

static void schedule_retry(int64_t *deadline, uint32_t *delay)
{
    *deadline = now_ms() + *delay;
    ESP_LOGI(RECONNECT_TAG, "Retry scheduled in %" PRIu32 " ms", *delay);
    if (*delay < RECONNECT_MAX_DELAY_MS) {
        uint32_t doubled = *delay * 2U;
        *delay = doubled > RECONNECT_MAX_DELAY_MS ? RECONNECT_MAX_DELAY_MS : doubled;
    }
}

static void reconnect_task(void *unused)
{
    (void)unused;
    esp_bd_addr_t peer = {0};
    bool have_peer = load_peer(peer);
    bool profile_ready = false;
    bool connected = false;
    bool connecting = false;
    bool timeout_cancel = false;
    uint32_t delay_ms = RECONNECT_INITIAL_DELAY_MS;
    uint32_t attempts = 0;
    int64_t deadline = -1;

    for (;;) {
        reconnect_event_t event;
        if (xQueueReceive(s_events, &event, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (event.type == RECONNECT_EVENT_PROFILE_READY) {
                profile_ready = true;
                if (have_peer && !connected && !connecting) {
                    deadline = now_ms() + RECONNECT_INITIAL_DELAY_MS;
                    ESP_LOGI(RECONNECT_TAG, "Profile ready; first reconnect in %u ms",
                             RECONNECT_INITIAL_DELAY_MS);
                }
                continue;
            }

            switch (event.state) {
                case ESP_HF_CONNECTION_STATE_CONNECTING:
                    connecting = true;
                    deadline = now_ms() + RECONNECT_ATTEMPT_TIMEOUT_MS;
                    break;
                case ESP_HF_CONNECTION_STATE_CONNECTED:
                    connecting = true;
                    deadline = now_ms() + RECONNECT_ATTEMPT_TIMEOUT_MS;
                    break;
                case ESP_HF_CONNECTION_STATE_SLC_CONNECTED:
                    memcpy(peer, event.address, ESP_BD_ADDR_LEN);
                    have_peer = true;
                    connected = true;
                    connecting = false;
                    timeout_cancel = false;
                    deadline = -1;
                    delay_ms = RECONNECT_INITIAL_DELAY_MS;
                    {
                        esp_err_t error = save_peer(peer);
                        ESP_LOGI(RECONNECT_TAG, "SLC ready; peer saved=%s bonded=%d",
                                 esp_err_to_name(error), is_bonded(peer));
                    }
                    break;
                case ESP_HF_CONNECTION_STATE_DISCONNECTING:
                    break;
                case ESP_HF_CONNECTION_STATE_DISCONNECTED:
                    connected = false;
                    connecting = false;
                    if (timeout_cancel) {
                        timeout_cancel = false;
                    } else if (profile_ready && have_peer) {
                        schedule_retry(&deadline, &delay_ms);
                    }
                    break;
                default:
                    break;
            }
        }

        if (!profile_ready || !have_peer || connected || deadline < 0 || now_ms() < deadline) {
            continue;
        }

        if (connecting) {
            ESP_LOGW(RECONNECT_TAG, "Attempt timed out; requesting SLC disconnect");
            timeout_cancel = true;
            connecting = false;
            (void)esp_hf_ag_slc_disconnect(peer);
            schedule_retry(&deadline, &delay_ms);
            continue;
        }

        if (!is_bonded(peer)) {
            ESP_LOGE(RECONNECT_TAG, "Saved peer is no longer bonded; automatic reconnect disabled");
            have_peer = false;
            deadline = -1;
            continue;
        }

        ++attempts;
        esp_err_t error = esp_hf_ag_slc_connect(peer);
        ESP_LOGI(RECONNECT_TAG,
                 "Outgoing HFP reconnect attempt %" PRIu32 ": %s; bonded=1",
                 attempts, esp_err_to_name(error));
        if (error == ESP_OK) {
            connecting = true;
            deadline = now_ms() + RECONNECT_ATTEMPT_TIMEOUT_MS;
        } else {
            schedule_retry(&deadline, &delay_ms);
        }
    }
}

static void post_event(const reconnect_event_t *event)
{
    if (s_events == NULL || xQueueSend(s_events, event, 0) != pdTRUE) {
        ESP_LOGE(RECONNECT_TAG, "Reconnect event queue full");
    }
}

esp_err_t reconnect_init(void)
{
    s_events = xQueueCreate(8, sizeof(reconnect_event_t));
    if (s_events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t error = esp_bt_gap_register_callback(reconnect_gap_callback);
    if (error != ESP_OK) {
        vQueueDelete(s_events);
        s_events = NULL;
        return error;
    }
    if (xTaskCreate(reconnect_task, "hfp_reconnect", 4096, NULL, 5, NULL) != pdPASS) {
        vQueueDelete(s_events);
        s_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(RECONNECT_TAG, "Minimal stock HFP AG reconnect reproduction enabled");
    return ESP_OK;
}

void reconnect_on_profile_ready(void)
{
    reconnect_event_t event = {
        .type = RECONNECT_EVENT_PROFILE_READY,
    };
    post_event(&event);
}

void reconnect_on_connection_state(esp_hf_connection_state_t state, const esp_bd_addr_t remote_bda)
{
    reconnect_event_t event = {
        .type = RECONNECT_EVENT_CONNECTION_STATE,
        .state = state,
    };
    memcpy(event.address, remote_bda, ESP_BD_ADDR_LEN);
    post_event(&event);
}
