#include <twr.h>

#define MEASURE_INTERVAL (5 * 60 * 1000)
#define SEND_DATA_INTERVAL (15 * 60 * 1000)

typedef struct
{
    uint8_t channel;
    float value;
    twr_tick_t next_pub;

} event_param_t;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_core_temperature_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
twr_data_stream_t sm_core_temperature;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_distance_buffer, (SEND_DATA_INTERVAL / MEASURE_INTERVAL))
twr_data_stream_t sm_distance;

TWR_DATA_STREAM_FLOAT_BUFFER(sm_voltage_buffer, 8)
twr_data_stream_t sm_voltage;

// LED instance
twr_led_t led;
// Button instance
twr_button_t button;
// Lora instance
twr_cmwx1zzabz_t lora;

twr_tmp112_t tmp112;

twr_scheduler_task_id_t battery_measure_task_id;

enum {
    HEADER_BOOT         = 0x00,
    HEADER_UPDATE       = 0x01,
    HEADER_BUTTON_PRESS = 0x02,

} header = HEADER_BOOT;

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) event_param;

    if (event == TWR_BUTTON_EVENT_PRESS)
    {
        header = HEADER_BUTTON_PRESS;

        twr_scheduler_plan_now(0);
    }
}

void tmp112_event_handler(twr_tmp112_t *self, twr_tmp112_event_t event, void *event_param)
{
    if (event == TWR_TMP112_EVENT_UPDATE)
    {
        float temperature;

        if (twr_tmp112_get_temperature_celsius(self, &temperature))
        {
            twr_data_stream_feed(&sm_core_temperature, &temperature);
        }
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        float voltage = NAN;

        twr_module_battery_get_voltage(&voltage);

        twr_data_stream_feed(&sm_voltage, &voltage);
    }
}

void battery_measure_task(void *param)
{
    if (!twr_module_battery_measure())
    {
        twr_scheduler_plan_current_now();
    }
}

void lora_callback(twr_cmwx1zzabz_t *self, twr_cmwx1zzabz_event_t event, void *event_param)
{
    if (event == TWR_CMWX1ZZABZ_EVENT_ERROR)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_BLINK_FAST);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_START)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_ON);

        twr_scheduler_plan_relative(battery_measure_task_id, 20);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_SEND_MESSAGE_DONE)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_READY)
    {
        twr_led_set_mode(&led, TWR_LED_MODE_OFF);
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_SUCCESS)
    {
        twr_atci_printfln("$JOIN_OK");
    }
    else if (event == TWR_CMWX1ZZABZ_EVENT_JOIN_ERROR)
    {
        twr_atci_printfln("$JOIN_ERROR");
    }
}

bool at_send(void)
{
    twr_scheduler_plan_now(0);

    return true;
}

bool at_status(void)
{
    float value_avg = NAN;

    if (twr_data_stream_get_average(&sm_voltage, &value_avg))
    {
        twr_atci_printfln("$STATUS: \"Voltage\",%.1f", value_avg);
    }
    else
    {
        twr_atci_printfln("$STATUS: \"Voltage\",");
    }

    float distance_last = NAN;

    if (twr_data_stream_get_last(&sm_distance, &distance_last))
    {
        twr_atci_printfln("$STATUS: \"Distance\",%.1f", distance_last);
    }
    else 
    {
        twr_atci_printfln("$STATUS: \"Distance\",");

    }

    return true;
}

void ultrasound_meassurement_update(void)
{
    twr_module_sensor_set_vdd(true);
    twr_system_pll_enable();
    twr_tick_wait(300);

    twr_tick_t timeout = twr_tick_get() + 5000;

    // Wait while signal is low
    while (twr_gpio_get_input(TWR_GPIO_P4))
    {
        if (twr_tick_get() >= timeout)
        {
            timeout = 0;
            break;
        }
    }

    // Wait until signal is high - rising edge
    while (!twr_gpio_get_input(TWR_GPIO_P4))
    {
        if (twr_tick_get() >= timeout)
        {
            timeout = 0;
            break;
        }
    }

    twr_timer_start();

    // Wait while signal is low
    while (twr_gpio_get_input(TWR_GPIO_P4))
    {
        if (twr_tick_get() >= timeout)
        {
            timeout = 0;
            break;
        }
    }

    uint32_t microseconds = twr_timer_get_microseconds();

    twr_timer_stop();

    float centimeters = microseconds / 58.0f;

    twr_system_pll_disable();
    twr_module_sensor_set_vdd(false);

    twr_scheduler_plan_current_from_now(MEASURE_INTERVAL);

    if (timeout == 0)
    {
        twr_data_stream_feed(&sm_distance, NULL);
        twr_log_error("APP: Sensor error");
    }
    else
    {
        twr_data_stream_feed(&sm_distance, &centimeters);
        twr_log_info("APP: Distance = %.1f cm", centimeters);
    }
}

void application_init(void)
{
    //twr_log_init(TWR_LOG_LEVEL_DUMP, TWR_LOG_TIMESTAMP_ABS);

    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_OFF);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, NULL);

    twr_tmp112_init(&tmp112, TWR_I2C_I2C0, 0x49);
    twr_tmp112_set_event_handler(&tmp112, tmp112_event_handler, NULL);
    twr_tmp112_set_update_interval(&tmp112, MEASURE_INTERVAL);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    battery_measure_task_id = twr_scheduler_register(battery_measure_task, NULL, 2020);

    // Initialize Sensor Module
    twr_module_sensor_init();
    twr_gpio_init(TWR_GPIO_P4);
    twr_gpio_set_mode(TWR_GPIO_P4, TWR_GPIO_MODE_INPUT);
    twr_scheduler_register(ultrasound_meassurement_update, NULL, MEASURE_INTERVAL);

    // Init stream buffers for averaging
    twr_data_stream_init(&sm_voltage, 1, &sm_voltage_buffer);
    twr_data_stream_init(&sm_distance, 1, &sm_distance_buffer);
    twr_data_stream_init(&sm_core_temperature, 1, &sm_core_temperature_buffer);

    // Initialize lora module
    twr_cmwx1zzabz_init(&lora, TWR_UART_UART1);
    twr_cmwx1zzabz_set_event_handler(&lora, lora_callback, NULL);
    twr_cmwx1zzabz_set_class(&lora, TWR_CMWX1ZZABZ_CONFIG_CLASS_A);
    //twr_cmwx1zzabz_set_debug(&lora, debug); // Enable debug output of LoRa Module commands to Core Module console

    // Initialize AT command interface
    twr_at_lora_init(&lora);
    static const twr_atci_command_t commands[] = {
            TWR_AT_LORA_COMMANDS,
            {"$SEND", at_send, NULL, NULL, NULL, "Immediately send packet"},
            {"$STATUS", at_status, NULL, NULL, NULL, "Show status"},
            TWR_ATCI_COMMAND_CLAC,
            TWR_ATCI_COMMAND_HELP
    };
    twr_atci_init(commands, TWR_ATCI_COMMANDS_LENGTH(commands));

    // Plan task 0 (application_task) to be run after 10 seconds
    twr_scheduler_plan_relative(0, 10 * 1000);
    
    twr_led_pulse(&led, 2000);

    twr_atci_println("@BUILD_DATE: " BUILD_DATE);
    twr_atci_println("@GIT_VERSION: " GIT_VERSION);
}

void application_task(void)
{
    if (!twr_cmwx1zzabz_is_ready(&lora))
    {
        twr_scheduler_plan_current_relative(100);

        return;
    }

    static uint8_t buffer[6];

    memset(buffer, 0xff, sizeof(buffer));

    buffer[0] = header;

    float voltage_avg = NAN;

    twr_data_stream_get_average(&sm_voltage, &voltage_avg);

    if (!isnan(voltage_avg))
    {
        buffer[1] = ceil(voltage_avg * 10.f);
    }

    float distance_last = NAN;

    //twr_data_stream_get_average(&sm_distance, &distance_avg);
    twr_data_stream_get_last(&sm_distance, &distance_last);

    if (!isnan(distance_last))
    {
        int16_t distance_i16 = (int16_t) (distance_last * 10.f);

        buffer[2] = distance_i16 >> 8;
        buffer[3] = distance_i16;
    }

    float core_temperature_avg = NAN;

    twr_data_stream_get_average(&sm_core_temperature, &core_temperature_avg);

    if (!isnan(core_temperature_avg))
    {
        int16_t temperature_i16 = (int16_t) (core_temperature_avg * 10.f);

        buffer[4] = temperature_i16 >> 8;
        buffer[5] = temperature_i16;
    }

    twr_cmwx1zzabz_send_message(&lora, buffer, sizeof(buffer));

    twr_scheduler_plan_current_relative(SEND_DATA_INTERVAL);
}
