#include <stdbool.h>
#include <stdio.h>

#include "common/platform.h"
#include "fw/src/mgos_app.h"
#include "fw/src/mgos_console.h"
#include "fw/src/mgos_gpio.h"
#include "fw/src/mgos_hal.h"
#include "fw/src/mgos_i2c.h"
#include "fw/src/mgos_mongoose.h"
#include "fw/src/mgos_sys_config.h"
#include "fw/src/mgos_timers.h"

#include "mcp9808.h"

#if CS_PLATFORM == CS_P_ESP8266
/* ESP8266 - main target */
#define LED_GPIO 10
#define RELAY_GPIO 13
#elif(CS_PLATFORM == CS_P_STM32) && defined(BSP_NUCLEO_F746ZG)
/* Nucleo-144 F746 */
#define LED_GPIO STM32_PIN_PB7
#define RELAY_GPIO STM32_PIN_PC13
#elif(CS_PLATFORM == CS_P_STM32) && defined(BSP_DISCO_F746G)
/* Discovery-0 F746 */
#define LED_GPIO STM32_PIN_PI1
#define RELAY_GPIO STM32_PIN_PI11
#else
#error Unknown platform
#endif

static bool s_heater = false;

static void set_heater(bool on) {
  CONSOLE_LOG(LL_INFO, ("Heater %s", (on ? "on" : "off")));
  mgos_gpio_write(LED_GPIO, on);
  mgos_gpio_write(RELAY_GPIO, on);
  s_heater = on;
}

static void handle_heater(struct mg_connection *nc, int ev, void *ev_data,
                          void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  mg_send_response_line(nc, 200,
                        "Content-Type: text/html\r\n"
                        "Connection: close\r\n");
  double temp = mcp9808_read_temp();
  mg_printf(nc,
            "<h1>Welcome to Cesanta Office IoT!</h1>\r\n"
            "<p>Temperature is %.2lf&deg;C.</p>\r\n"
            "<p>Heater is %s.</p>\r\n"
            "<form action=/heater/%s><input type=submit value='Turn heater "
            "%s'></form>\r\n"
            "<hr>\r\n"
            "Heater FW %s (%s)",
            temp, (s_heater ? "on" : "off"), (s_heater ? "off" : "on"),
            (s_heater ? "off" : "on"), get_ro_vars()->fw_version,
            get_ro_vars()->fw_id);
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) ev_data;
  (void) user_data;
}

static void handle_heater_action(struct mg_connection *nc, int ev,
                                 void *ev_data, void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  if (mg_vcmp(&hm->uri, "/heater/on") == 0) {
    set_heater(true);
  } else if (mg_vcmp(&hm->uri, "/heater/off") == 0) {
    set_heater(false);
  }
  mg_http_send_redirect(nc, 302, mg_mk_str("/heater"), mg_mk_str(NULL));
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) user_data;
}

static void handle_debug(struct mg_connection *nc, int ev, void *ev_data,
                         void *user_data) {
  if (ev != MG_EV_HTTP_REQUEST) return;
  struct http_message *hm = (struct http_message *) ev_data;
  mg_send_response_line(nc, 200,
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n");
  mg_printf(nc, "Time is %.2lf. Free RAM %u.\r\n", mg_time(),
            mgos_get_free_heap_size());
  nc->flags |= MG_F_SEND_AND_CLOSE;
  (void) hm;
  (void) user_data;
}

struct mg_connection *s_sensor_conn = NULL;

static void handle_sensor_conn(struct mg_connection *nc, int ev, void *ev_data,
                               void *user_data) {
  switch (ev) {
    case MG_EV_HTTP_REPLY: {
      nc->flags |= MG_F_CLOSE_IMMEDIATELY;
      break;
    }
    case MG_EV_CLOSE: {
      s_sensor_conn = NULL;
      break;
    }
  }
  (void) ev_data;
  (void) user_data;
}

static void sensor_timer_cb(void *arg) {
  if (s_sensor_conn != NULL) return; /* In progress. */
  double temp = mcp9808_read_temp();
  if (temp <= -1000) return; /* Error */
  char *eh = NULL, *post_data = NULL;
  mg_asprintf(&post_data, 0, "{\"office_temperature\": %.2lf}", temp);
  if (get_cfg()->hsw.auth != NULL) {
    mg_asprintf(&eh, 0, "Authorization: %s\r\n", get_cfg()->hsw.auth);
  }
  s_sensor_conn =
      mg_connect_http(mgos_get_mgr(), handle_sensor_conn, NULL,
                      get_cfg()->hsw.sensor_data_url, eh, post_data);
  free(eh);
  free(post_data);
  (void) arg;
}

enum mgos_app_init_result mgos_app_init(void) {
  mgos_gpio_set_mode(LED_GPIO, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_set_mode(RELAY_GPIO, MGOS_GPIO_MODE_OUTPUT);
  mgos_gpio_write(LED_GPIO, 0);
  mgos_gpio_write(RELAY_GPIO, 0);
  mgos_register_http_endpoint("/heater/", handle_heater_action, NULL);
  mgos_register_http_endpoint("/heater", handle_heater, NULL);
  mgos_register_http_endpoint("/debug", handle_debug, NULL);

  struct sys_config_hsw *hcfg = &get_cfg()->hsw;
  if (hcfg->sensor_report_interval_ms > 0 && hcfg->sensor_data_url != NULL) {
    mgos_set_timer(hcfg->sensor_report_interval_ms, true /* repeat */,
                   sensor_timer_cb, NULL);
  }

  return MGOS_APP_INIT_SUCCESS;
}
