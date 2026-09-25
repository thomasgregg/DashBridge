#include "app_internal.hpp"
#include "dashbridge/adapters/features.hpp"
#include "dashbridge/ports/runtime_services.hpp"
#include "dashbridge/adapters/setup_gatt_v1_adapter.hpp"
#include "dashbridge/core/setup.hpp"
#include "sdkconfig.h"

#if CONFIG_BRIDGE_PHONE
namespace runtime {
using namespace dashbridge::ports;
namespace setup = dashbridge::core::setup;

void setup_refresh_status() {
    setup::Status value;
    value.phone_bluetooth = phone_bluetooth_ready();
    value.notifications = phone_notifications_ready();
#if CONFIG_BRIDGE_CALL_RELAY
    value.phone_calls = relay_calls_ready();
#endif
    value.internal_link = phone_board_link_ready();
    value.tesla_messages = phone_car_message_ready();
    value.tesla_transport = phone_car_transport_ready();
    value.tesla_sync = phone_car_sync_ready();
    value.tesla_calls = phone_car_call_ready();
    setup_controller().update_status(value);
}

void setup_ble_start() {
    setup_refresh_status();
    dashbridge::adapters::start_setup_gatt_v1(setup_controller());
}

} // namespace runtime
#endif
