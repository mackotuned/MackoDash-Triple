#include "air_link.h"

#include "cluster_wireless.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define AIR_RIDE_STATE_TIMEOUT_MS 500U

static portMUX_TYPE state_lock = portMUX_INITIALIZER_UNLOCKED;
static cluster_air_ride_state_t latest;
static uint32_t received_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void air_link_store(const cluster_air_ride_state_t *state)
{
    taskENTER_CRITICAL(&state_lock);
    latest = *state;
    received_ms = now_ms();
    taskEXIT_CRITICAL(&state_lock);
}

void air_link_set_target(uint8_t target)
{
    taskENTER_CRITICAL(&state_lock);
    latest.adjustment_target = target;
    taskEXIT_CRITICAL(&state_lock);
}

void air_link_get(air_link_state_t *state)
{
    cluster_air_ride_state_t copy;
    uint32_t copy_received_ms;
    taskENTER_CRITICAL(&state_lock);
    copy = latest;
    copy_received_ms = received_ms;
    taskEXIT_CRITICAL(&state_lock);

    *state = (air_link_state_t){
        .fresh = copy_received_ms != 0U &&
                 now_ms() - copy_received_ms < AIR_RIDE_STATE_TIMEOUT_MS,
        .connected = copy.connected,
        .pressure_valid = copy.pressure_valid,
        .compressor_valid = copy.compressor_valid,
        .compressor_on = copy.compressor_on,
        .adjustment_open = copy.adjustment_open,
        .adjustment_target = copy.adjustment_target,
        .psi = {
            [AIR_LINK_FRONT_LEFT] = copy.front_left_psi,
            [AIR_LINK_FRONT_RIGHT] = copy.front_right_psi,
            [AIR_LINK_REAR_LEFT] = copy.rear_left_psi,
            [AIR_LINK_REAR_RIGHT] = copy.rear_right_psi,
            [AIR_LINK_TANK] = copy.tank_psi,
        },
    };
}

esp_err_t air_link_send_command(cluster_air_ride_command_code_t code)
{
    cluster_settings_t settings;
    settings_service_get(&settings);
    const cluster_air_ride_command_t command = {
        .code = code,
        .compressor_on_psi = settings.air_ride.compressor_on_psi,
        .compressor_off_psi = settings.air_ride.compressor_off_psi,
    };
    uint8_t payload[CLUSTER_AIR_RIDE_COMMAND_PAYLOAD_SIZE];
    if (cluster_air_ride_command_encode(&command, payload, sizeof(payload)) !=
        CLUSTER_PROTOCOL_OK) {
        return ESP_ERR_INVALID_ARG;
    }
    return cluster_wireless_send_message(CLUSTER_WIRELESS_ROLE_RIGHT,
                                         CLUSTER_MSG_AIR_RIDE_COMMAND,
                                         payload, sizeof(payload));
}

uint16_t air_link_target_pressure(const air_link_state_t *state,
                                  uint8_t target)
{
    const uint16_t *psi = state->psi;
    switch ((cluster_air_target_t)target) {
    case CLUSTER_AIR_TARGET_ALL:
        return (uint16_t)((psi[AIR_LINK_FRONT_LEFT] + psi[AIR_LINK_FRONT_RIGHT] +
                           psi[AIR_LINK_REAR_LEFT] + psi[AIR_LINK_REAR_RIGHT]) /
                          4U);
    case CLUSTER_AIR_TARGET_FRONT:
        return (uint16_t)((psi[AIR_LINK_FRONT_LEFT] +
                           psi[AIR_LINK_FRONT_RIGHT]) / 2U);
    case CLUSTER_AIR_TARGET_REAR:
        return (uint16_t)((psi[AIR_LINK_REAR_LEFT] +
                           psi[AIR_LINK_REAR_RIGHT]) / 2U);
    case CLUSTER_AIR_TARGET_FRONT_LEFT: return psi[AIR_LINK_FRONT_LEFT];
    case CLUSTER_AIR_TARGET_FRONT_RIGHT: return psi[AIR_LINK_FRONT_RIGHT];
    case CLUSTER_AIR_TARGET_REAR_LEFT: return psi[AIR_LINK_REAR_LEFT];
    case CLUSTER_AIR_TARGET_REAR_RIGHT: return psi[AIR_LINK_REAR_RIGHT];
    default: return 0U;
    }
}

bool air_link_web_state(settings_air_state_t *state, void *context)
{
    (void)context;
    air_link_state_t air;
    air_link_get(&air);
    state->right_online = air.fresh;
    state->air_connected = air.fresh && air.connected;
    state->pressure_valid = state->air_connected && air.pressure_valid;
    state->compressor_valid = state->air_connected && air.compressor_valid;
    state->compressor_on = state->compressor_valid && air.compressor_on;
    state->front_left_psi = air.psi[AIR_LINK_FRONT_LEFT];
    state->front_right_psi = air.psi[AIR_LINK_FRONT_RIGHT];
    state->rear_left_psi = air.psi[AIR_LINK_REAR_LEFT];
    state->rear_right_psi = air.psi[AIR_LINK_REAR_RIGHT];
    state->tank_psi = air.psi[AIR_LINK_TANK];
    return true;
}

esp_err_t air_link_web_command(cluster_air_ride_command_code_t command,
                               void *context)
{
    (void)context;
    air_link_state_t air;
    air_link_get(&air);
    if (command != CLUSTER_AIR_RIDE_RELEASE && (!air.fresh || !air.connected)) {
        return ESP_ERR_INVALID_STATE;
    }
    return air_link_send_command(command);
}
