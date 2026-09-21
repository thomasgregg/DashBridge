/* Minimal persisted-peer reconnect hook for the stock ESP-IDF HFP AG example. */
#pragma once

#include "esp_err.h"
#include "esp_hf_ag_api.h"

esp_err_t reconnect_init(void);
void reconnect_on_profile_ready(void);
void reconnect_on_connection_state(esp_hf_connection_state_t state, const esp_bd_addr_t remote_bda);
