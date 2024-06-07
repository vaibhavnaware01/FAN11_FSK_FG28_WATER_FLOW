/***************************************************************************//**
* @file app.c
* @brief Application code of the Wi-SUN Node Monitoring example
*******************************************************************************
* # License
* <b>Copyright 2023 Silicon Laboratories Inc. www.silabs.com</b>
*******************************************************************************
*
* SPDX-License-Identifier: Zlib
*
* The licensor of this software is Silicon Laboratories Inc.
*
* This software is provided 'as-is', without any express or implied
* warranty. In no event will the authors be held liable for any damages
* arising from the use of this software.
*
* Permission is granted to anyone to use this software for any purpose,
* including commercial applications, and to alter it and redistribute it
* freely, subject to the following restrictions:
*
* 1. The origin of this software must not be misrepresented; you must not
*    claim that you wrote the original software. If you use this software
*    in a product, an acknowledgment in the product documentation would be
*    appreciated but is not required.
* 2. Altered source versions must be plainly marked as such, and must not be
*    misrepresented as being the original software.
* 3. This notice may not be removed or altered from any source distribution.
*
******************************************************************************
*
* EXPERIMENTAL QUALITY
* This code has not been formally tested and is provided as-is.  It is not suitable for production environments.
* This code will not be maintained.
*
******************************************************************************/
// -----------------------------------------------------------------------------
//                                   Includes
// -----------------------------------------------------------------------------
#include <stdio.h>
#include <assert.h>
#include "app.h"
#include "sl_wisun_api.h"
#include "sl_string.h"
#include "em_i2c.h"
#include "em_chip.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_veml6035.h"


#ifdef    SL_CATALOG_WISUN_APP_CORE_PRESENT
  #include "sl_wisun_app_core_util.h"
  #include "sl_wisun_app_core_util_config.h"
#endif /* SL_CATALOG_WISUN_APP_CORE_PRESENT */

#ifdef    SL_CATALOG_WISUN_APP_OS_STAT_PRESENT
  #include "app_os_stat_config.h"
#endif /* SL_CATALOG_WISUN_APP_OS_STAT_PRESENT */

#ifdef    SL_CATALOG_WISUN_OTA_DFU_PRESENT
  #include "sl_wisun_ota_dfu_config.h"
#endif /* SL_CATALOG_WISUN_OTA_DFU_PRESENT */

#include "sl_wisun_event_mgr.h"
#include "printf.h"
#include "socket/socket.h"
#include "sl_wisun_config.h"

#include "app_timestamp.h"
#include "app_rtt_traces.h"

#include "sl_wisun_coap.h"
#include "sl_wisun_coap_config.h"
#include "sl_simple_led_instances.h"

#include "app_coap.h"
#include "app_check_neighbors.h"
#include "em_chip.h"
#include "em_gpio.h"
#include "em_emu.h"
#include "em_cmu.h"
#include "bsp.h"
// -----------------------------------------------------------------------------
//                              Macros and Typedefs
// -----------------------------------------------------------------------------
// Macros to treat possible errors
#define NO_ERROR(ret, ...)                   if (ret == SL_STATUS_OK) {printfBothTime(__VA_ARGS__);}
#define IF_ERROR(ret, ...)                   if (ret != SL_STATUS_OK) {printfBothTime("\n"); printfBoth(__VA_ARGS__);}
#define IF_ERROR_RETURN(ret, ...)            if (ret != SL_STATUS_OK) {printfBothTime("\n"); printfBoth(__VA_ARGS__); return ret;}
#define IF_ERROR_INCR(ret, error_count, ...) if (ret != SL_STATUS_OK) {printfBothTime("\n"); printfBoth(__VA_ARGS__); error_count++;}
#define HERE printf("%d\n", __LINE__);
#define I2C_ADDRESS_T6713 0x15 // Check your sensor's datasheet for the correct address
#define CO2_READ_COMMAND 0x04 // Command to read CO2 concentration
#ifdef    HISTORY
#define APPEND_TO_HISTORY(...) { \
  history_len = strlen(history_string); \
  snprintf(history_string + history_len, \
    SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE - history_len, \
    __VA_ARGS__); \
    printfBothTime(__VA_ARGS__); }
#endif /* HISTORY */

// JSON common format strings
#define START_JSON "{\n"

#define END_JSON   "}\n"

#define DEVICE_PARENT_RUNNING_JSON \
  "\"device\":\"%s\",\n"           \
  "\"chip\":  \"%s\",\n"           \
  "\"parent\":\"%s\",\n"           \
  "\"running\":     \"%s\",\n"

#ifdef  _SILICON_LABS_32B_SERIES_1             /** Product Series Identifier */
  #ifdef _SILICON_LABS_32B_SERIES_1_CONFIG_2
    #define CHIP "xG12"
  #endif
#endif

#ifdef  _SILICON_LABS_32B_SERIES_2             /** Product Series Identifier */
  #ifdef _SILICON_LABS_32B_SERIES_2_CONFIG_5
    #define CHIP "xG25"
  #endif
  #ifdef _SILICON_LABS_32B_SERIES_2_CONFIG_8                                                     /** Product Config Identifier */
    #define CHIP "xG28"
  #endif
#endif

#ifndef CHIP
  #define CHIP SL_BOARD_NAME
#endif

// -----------------------------------------------------------------------------
//                          Static Function Declarations
// -----------------------------------------------------------------------------
void        _join_state_custom_callback(sl_wisun_evt_t *evt);
void        _check_neighbors(void);
char*       _connection_json_string();
char*       _status_json_string (char * start_text);
sl_wisun_mac_address_t _get_parent_mac_address(void);
sl_status_t _select_destinations(void);
sl_status_t _open_udp_sockets(void);
sl_status_t _coap_notify(char* json_string);
uint8_t     _print_and_send_messages (char *in_msg, bool with_time,
                            bool to_console, bool to_rtt, bool to_udp, bool to_coap);

// -----------------------------------------------------------------------------
//                                Global Variables
// -----------------------------------------------------------------------------
uint16_t connection_count = 0;       // number of connections
uint64_t connect_time_sec;           // time stamp of Wisun connect call
uint64_t connection_time_sec;        // last connection time stamp
uint64_t disconnection_time_sec;     // last disconnection time stamp
uint64_t connected_total_sec = 0;    // total time connected
uint64_t disconnected_total_sec = 0; // total time disconnected

char chip[8];
char device_tag[8];
char parent_tag[8];
char application[100];
char version[50];
char device_type[25];

bool just_disconnected = false;
#ifdef    HISTORY
uint16_t history_len;
#endif /* HISTORY */
volatile uint32_t pulseCounter = 0;


uint32_t get_pulse_counter() {
    return pulseCounter;
}
// -----------------------------------------------------------------------------
//                                Static Variables
// -----------------------------------------------------------------------------
sl_wisun_join_state_t join_state = SL_WISUN_JOIN_STATE_DISCONNECTED;
static  uint64_t app_join_state_sec[6];
        uint64_t app_join_state_delay_sec[6];
static uint16_t previous_join_state = 0;
char json_string[SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE];
#ifdef    HISTORY
char history_string[SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE];
#endif /* HISTORY */
sl_wisun_mac_address_t device_mac;
sl_wisun_mac_address_t parent_mac;

// IPv6 address structures
in6_addr_t device_global_ipv6;
in6_addr_t border_router_ipv6;
in6_addr_t udp_notification_ipv6;
in6_addr_t coap_notification_ipv6;

sockaddr_in6_t udp_notification_sockaddr_in6;
sockaddr_in6_t coap_notification_sockaddr_in6;

// IPv6 address strings (for printing)
char  device_global_ipv6_string[40];
char  border_router_ipv6_string[40];
char  udp_notification_ipv6_string[40];
char  coap_notification_ipv6_string[40];

// Notification sockets
sl_wisun_socket_id_t udp_notification_socket_id = 0;
sl_wisun_socket_id_t coap_notification_socket_id = 0;

char udp_msg[1024];
char coap_msg[1024];

uint16_t msg_len;
uint16_t auto_send_sec = 30; // Notification period in seconds
uint8_t  trace_level = SL_WISUN_TRACE_LEVEL_DEBUG;    // Trace level for all trace groups

// UDP ports
#define UDP_NOTIFICATION_PORT  1237
#define COAP_NOTIFICATION_PORT 5685

#define SL_WISUN_STATUS_CONNECTION_URI_PATH  "/status/connection"
#define SL_WISUN_STATUS_JSON_STR_MAX_LEN 280

// Notifications destinations (UDP and CoAP)
// Set to fixed IPv6 strings
#define UDP_NOTIFICATION_DEST  "fd12:3456::1" // fixed IPv6 string
#define COAP_NOTIFICATION_DEST "fd12:3456::2" // fixed IPv6 string

// CoAP Notification channel structure definition
typedef struct sl_wisun_coap_notify_ch {
  /// Notification socket
  int32_t sockid;
  /// Notification address
  sockaddr_in6_t addr;
  /// Notification packet
  sl_wisun_coap_packet_t pkt;
} sl_wisun_coap_notify_ch_t;

// CoAP Notification channel structure instance and defaults
static sl_wisun_coap_notify_ch_t coap_notify_ch = {
  .sockid = -1L,
  .addr = { 0U },
  .pkt = { 0U },
};

// -----------------------------------------------------------------------------
//                          Public Function Definitions
// -----------------------------------------------------------------------------
void escapeHatch(void)
{
  printf("escapeHatch");

  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, gpioModeInputPullFilter, 1);
  if (GPIO_PinInGet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN) == 0) {
    GPIO_PinModeSet(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN, gpioModePushPull, 1);
    __BKPT(0);
  }
  // Pin not asserted, so disable input
  else {
    GPIO_PinModeSet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, gpioModeDisabled, 0);
    CMU_ClockEnable(cmuClock_GPIO, false);
  }
}

/**************************************************************************//**
 * @brief Setup GPIO interrupt for pushbuttons.
 *****************************************************************************/
static void gpioSetup(void)
{
  printf("gpioSetup");

  // Configure GPIO Clock. Note this is not required for EFR32xG21
  CMU_ClockEnable(cmuClock_GPIO, true);

  // Configure Button PB0 as input and enable interrupt
  GPIO_PinModeSet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, gpioModeInputPull, 1);
  GPIO_ExtIntConfig(BSP_GPIO_PB0_PORT,
                    BSP_GPIO_PB0_PIN,
                    BSP_GPIO_PB0_PIN,
                    false,
                    true,
                    true);

  // Configure Button PB1 as input and enable interrupt
  GPIO_PinModeSet(BSP_GPIO_PB1_PORT, BSP_GPIO_PB1_PIN, gpioModeInputPull, 1);
  GPIO_ExtIntConfig(BSP_GPIO_PB1_PORT,
                    BSP_GPIO_PB1_PIN,
                    BSP_GPIO_PB1_PIN,
                    false,
                    true,
                    true);

  // Enable EVEN interrupt to catch button press that changes slew rate
  NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
  NVIC_EnableIRQ(GPIO_EVEN_IRQn);

  // Enable ODD interrupt to catch button press that changes slew rate
  NVIC_ClearPendingIRQ(GPIO_ODD_IRQn);
  NVIC_EnableIRQ(GPIO_ODD_IRQn);

  // Configure LED0 and LED1 as a push pull output for LED drive
  GPIO_PinModeSet(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN, gpioModePushPull, 1);
  GPIO_PinModeSet(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN, gpioModePushPull, 1);
  printf("gpioSetup completed");

}

/**************************************************************************//**
 * @brief GPIO Interrupt handler for even pins.
 *****************************************************************************/
void GPIO_EVEN_IRQHandler(void)
{
//  a++;

  // Get and clear all pending GPIO interrupts
  uint32_t interruptMask = GPIO_IntGet();
  GPIO_IntClear(interruptMask);

  // Check if button 0 was pressed
  if (interruptMask & (1 << BSP_GPIO_PB0_PIN))
  {

    GPIO_PinOutToggle(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN);
  }

  // Check if button 1 was pressed
  else if (interruptMask & (1 << BSP_GPIO_PB1_PIN))
  {
      pulseCounter++;


    //GPIO_PinOutToggle(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN);
  }

}

/**************************************************************************//**
 * @brief GPIO Interrupt handler for even pins.
 *****************************************************************************/
void GPIO_ODD_IRQHandler(void)
{
//  b++;

  // Get and clear all pending GPIO interrupts
  uint32_t interruptMask = GPIO_IntGet();
  GPIO_IntClear(interruptMask);

  // Check if button 1 was pressed
  if (interruptMask & (1 << BSP_GPIO_PB1_PIN))
  {
      pulseCounter++;

    //GPIO_PinOutToggle(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN);
  }

  // Check if button 0 was pressed
  else if (interruptMask & (1 << BSP_GPIO_PB0_PIN))
  {
    GPIO_PinOutToggle(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN);
  }
}

#ifndef SL_CATALOG_WISUN_EVENT_MGR_PRESENT // Event Manager also defines this handler
/*Wi-SUN event handler*/
void sl_wisun_on_event(sl_wisun_evt_t *evt)
{
  (void) evt->header.id;

  /////////////////////////////////////////////////////////////////////////////
  // Put your Wi-SUN event handling here!                                    //
  // This is called from Wi-SUN stack.                                       //
  // Do not call blocking functions from here!                               //
  // Protect your data during event handling!                                //
  /////////////////////////////////////////////////////////////////////////////
}
#endif
void i2c_init(void) {
    // Enable clock for I2C and GPIO
    CMU_ClockEnable(cmuClock_I2C0, true);
    CMU_ClockEnable(cmuClock_GPIO, true);


    // Route I2C pins
}
/* App task function*/
void app_task(void *args)
{
  (void) args;
  bool with_time, to_console, to_rtt, to_udp, to_coap;

  app_timestamp_init();
  CHIP_Init();
  escapeHatch();

  // Initialize Push Buttons and PA0
  gpioSetup();
  i2c_init();
  with_time = to_console = to_rtt = true;
  to_udp = to_coap = false;
  printf("\n");
  sprintf(chip, "%s", CHIP);
  snprintf(application, 100, "%s", "Wi-SUN Node Monitoring V1.0.0");
  printfBothTime("%s/%s %s\n", chip, SL_BOARD_NAME, application);
  snprintf(version, 50, "Compiled on %s at %s", __DATE__, __TIME__);
  printfBothTime("%s\n", chip);
  printfBothTime("%s\n", SL_BOARD_NAME);
  printfBothTime("%s\n", application);
  printfBothTime("%s\n", version);
  app_set_all_traces(SL_WISUN_TRACE_LEVEL_DEBUG, true);
  sl_simple_led_init_instances();
  sl_simple_led_turn_off(sl_led_led0.context);
  sl_simple_led_turn_off(sl_led_led1.context);
  osDelay(1000);
  sl_simple_led_toggle(sl_led_led0.context);
  sl_simple_led_toggle(sl_led_led1.context);
  osDelay(1000);
  sl_simple_led_turn_off(sl_led_led0.context);
  sl_simple_led_turn_off(sl_led_led1.context);
  printf("led init done\n");
  app_set_trace(SL_WISUN_TRACE_GROUP_RF  , SL_WISUN_TRACE_LEVEL_INFO, true);
  app_set_trace(SL_WISUN_TRACE_GROUP_FHSS, SL_WISUN_TRACE_LEVEL_INFO, true);
  sl_veml6035_enable_sensor(I2C0,true);

#ifdef    HISTORY
  snprintf(history_string, SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE, "%s", "");
#endif /* HISTORY */

  #ifdef    SL_CATALOG_WISUN_COAP_PRESENT
    printfBothTime("With     CoAP Support\n");
  #endif /* SL_CATALOG_WISUN_COAP_PRESENT */

  #ifdef    SL_CATALOG_WISUN_OTA_DFU_PRESENT
    printfBothTime("With     OTA DFU Support\n");
  #endif /* SL_CATALOG_WISUN_OTA_DFU_PRESENT */

  // Set device_tag to last 2 bytes of MAC address
  sl_wisun_get_mac_address(&device_mac);
  sprintf(device_tag, "%02x%02x", device_mac.address[6], device_mac.address[7]);
  printfBothTime("device_tag %s\n", device_tag);

  // Set device_type based on application settings
#ifdef   SL_CATALOG_WISUN_LFN_DEVICE_SUPPORT_PRESENT
  if (WISUN_CONFIG_DEVICE_TYPE == SL_WISUN_LFN ) {
    #if !defined(WISUN_CONFIG_DEVICE_PROFILE)
      sprintf(device_type, "LFN (null profile)");
      #else /* WISUN_CONFIG_DEVICE_PROFILE */
      switch (WISUN_CONFIG_DEVICE_PROFILE) {
        case SL_WISUN_LFN_PROFILE_TEST: {
          sprintf(device_type, "LFN (Test Profile)");
          break;
        }
        case SL_WISUN_LFN_PROFILE_BALANCED: {
          sprintf(device_type, "LFN (Balanced Profile)");
          break;
        }
        case SL_WISUN_LFN_PROFILE_ECO: {
          sprintf(device_type, "LFN (Eco Profile)");
          break;
        }
        default: {
          sprintf(device_type, "LFN (NO Profile)");
          break;
        }
      }
    #endif  /* WISUN_CONFIG_DEVICE_PROFILE */
  } else if (WISUN_CONFIG_DEVICE_TYPE == SL_WISUN_ROUTER ) {
      sprintf(device_type, "FFN with LFN support");
  }
#else /* SL_CATALOG_WISUN_LFN_DEVICE_SUPPORT_PRESENT */
  sprintf(device_type, "FFN with No LFN support");
#endif /* SL_CATALOG_WISUN_LFN_DEVICE_SUPPORT_PRESENT */
  printfBothTime("device_type %s\n", device_type);

  // Register our join state custom callback function with the event manager (aka 'em')
  app_wisun_em_custom_callback_register(SL_WISUN_MSG_JOIN_STATE_IND_ID, _join_state_custom_callback);

  // Store the time where we call app_wisun_connect_and_wait()
  connect_time_sec = now_sec();
  sl_wisun_clear_credential_cache();
  printfBothTime("Cleared credential cache\n");

  #ifdef    SL_CATALOG_WISUN_APP_CORE_PRESENT
    // connect to the wisun network
    app_wisun_connect_and_wait();
  #endif /* SL_CATALOG_WISUN_APP_CORE_PRESENT */

  /*******************************************
  /  We only reach this part once connected  /
  *******************************************/

  // Once connected for the first time, reduce RTT traces to the minimum
  app_set_all_traces(SL_WISUN_TRACE_LEVEL_WARN, true);

  // Get ready to listen to and send notifications to the Border Router
  //  also get ready for CoAP communication
  _open_udp_sockets();

#ifdef    SL_CATALOG_WISUN_OTA_DFU_PRESENT
  in6_addr_t global_ipv6;

  printf("OTA DFU will download chunks of '<TFTP_DIRECTORY>/%s' from %s/%d\n",
        SL_WISUN_OTA_DFU_GBL_FILE,
        SL_WISUN_OTA_DFU_HOST_ADDR,
        SL_WISUN_OTA_DFU_TFTP_PORT
      );

  sl_wisun_get_ip_address(SL_WISUN_IP_ADDRESS_TYPE_GLOBAL, &global_ipv6);
  printf("OTA DFU 'start' command:\n");
  printf(" coap-client -m post -N -B 10 -t text coap://[%s]:%d%s -e \"start\"\n",
        app_wisun_trace_util_get_ip_str(&global_ipv6),
        5683,
        SL_WISUN_OTA_DFU_URI_PATH
      );
  printf("Follow OTA DFU progress (from node, intrusive) using:\n");
  printf(" coap-client -m get -N -B 10 -t text coap://[%s]:%d%s\n",
      app_wisun_trace_util_get_ip_str(&global_ipv6),
      SL_WISUN_COAP_RESOURCE_HND_SERVICE_PORT,
      SL_WISUN_OTA_DFU_URI_PATH
  );

  if (SL_WISUN_OTA_DFU_HOST_NOTIFY_ENABLED) {
      printf("OTA DFU notifications enabled (every %d chunks)\n",
        SL_WISUN_OTA_DFU_NOTIFY_DOWNLOAD_CHUNK_CNT
      );
      printf("OTA DFU notifications will be POSTed to notification server coap://[%s]:%d%s\n",
        SL_WISUN_OTA_DFU_NOTIFY_HOST_ADDR,
        SL_WISUN_OTA_DFU_NOTIFY_PORT,
        SL_WISUN_OTA_DFU_NOTIFY_URI_PATH
      );
    printf("Follow OTA DFU progress (from notification server) using:\n");
    printf(" coap-client -m get -N -B 1 -t text coap://[%s]:%d%s\n",
      SL_WISUN_OTA_DFU_NOTIFY_HOST_ADDR,
      SL_WISUN_OTA_DFU_NOTIFY_PORT,
      SL_WISUN_OTA_DFU_NOTIFY_URI_PATH
    );
  }
#endif /* SL_CATALOG_WISUN_OTA_DFU_PRESENT */

  // Print info on possible CoAP commands, now that CoAP communication is set
  print_coap_help(device_global_ipv6_string, border_router_ipv6_string);

  // Print and send initial connection message
  to_udp = to_coap = true;
  _print_and_send_messages (_connection_json_string(""),
              with_time, to_console, to_rtt, to_udp, to_coap);

  while (1) {
    ///////////////////////////////////////////////////////////////////////////
    // Put your application code here!                                       //
    ///////////////////////////////////////////////////////////////////////////
    // We can only send messages outside if connected
    if (join_state == SL_WISUN_JOIN_STATE_OPERATIONAL) {
        sl_simple_led_turn_on(sl_led_led0.context);
        sl_simple_led_turn_on(sl_led_led1.context);
      to_udp = to_coap = true;

    } else {
#ifdef    AUTO_CLEAR_CREDENTIAL_CACHE
      if (just_disconnected) {
          just_disconnected = false;
          sl_wisun_disconnect();
          sl_wisun_clear_credential_cache();
          app_wisun_network_connect();
      }
#endif /* AUTO_CLEAR_CREDENTIAL_CACHE */
      to_udp = to_coap = false;
    }
    // Print status message every auto_send_sec seconds
    printf("%d",pulseCounter);


    _print_and_send_messages (_status_json_string(""),
              with_time, to_console, to_rtt, to_udp, to_coap);
    osDelay(auto_send_sec*1000);
  }
}

void app_reset_statistics(void) {
  connection_time_sec = now_sec();
  disconnection_time_sec = connection_time_sec;
  connection_count = 0;
  connected_total_sec = 0;
  disconnected_total_sec = 0;
}

//                          Static Function Definitions
// -----------------------------------------------------------------------------



sl_wisun_mac_address_t _get_parent_mac_address(void) {
  sl_status_t ret;
  uint8_t neighbor_count;
  uint8_t i;
  sl_wisun_neighbor_info_t neighbor_info;
  sl_wisun_mac_address_t   mac_address;
  for ( i = 0 ; i<  SL_WISUN_MAC_ADDRESS_SIZE ; i++) {
      mac_address.address[i] = 0;
  }
  ret = sl_wisun_get_neighbor_count(&neighbor_count);
  if (ret) printf("[Failed: sl_wisun_get_neighbor_count() returned 0x%04x]\n", (uint16_t)ret);
  sl_wisun_mac_address_t neighbor_mac_addresses[neighbor_count];
  ret = sl_wisun_get_neighbors(&neighbor_count, neighbor_mac_addresses);
  if (ret) printf("[Failed: sl_wisun_get_neighbors() returned 0x%04x]\n", (uint16_t)ret);
  for (i = 0 ; i < neighbor_count; i++) {
      sl_wisun_get_neighbor_info(&neighbor_mac_addresses[i], &neighbor_info);
      if (neighbor_info.type == SL_WISUN_NEIGHBOR_TYPE_PRIMARY_PARENT) {
        mac_address = neighbor_mac_addresses[i];
        break;
      }
  }
  return mac_address;
}

void refresh_parent_tag(void) {
  parent_mac = _get_parent_mac_address();
  sprintf(parent_tag, "%02x%02x", parent_mac.address[6], parent_mac.address[7]);
};

void  _join_state_custom_callback(sl_wisun_evt_t *evt) {
  int i;
  uint64_t delay;
  // Use flags to select trace destinations
  // Traces can only be sent over UDP or CoAP once connected, so
  //  to_udp and to_coap are false by default.
  bool with_time, to_console, to_rtt, to_udp, to_coap;
  with_time = to_console = to_rtt = true;
  to_udp = to_coap = false;

  join_state = (sl_wisun_join_state_t)evt->evt.join_state.join_state;
  if (join_state >  SL_WISUN_JOIN_STATE_OPERATIONAL) {
      // Do not process intermediate join states, whose values ar > 5
      return;
  }
  if (join_state != previous_join_state) {
    // join_state changed...
    // print current join_state
    printfBothTime("[Join state %u->%u]\n", previous_join_state, join_state);
    if ((join_state > SL_WISUN_JOIN_STATE_DISCONNECTED) && (join_state <= SL_WISUN_JOIN_STATE_OPERATIONAL)) {
      app_join_state_sec[join_state] = now_sec();
      // Store transition delay
      delay = app_join_state_delay_sec[join_state] = app_join_state_sec[join_state] - app_join_state_sec[join_state-1];
      printfBothTime("app_join_state_delay_sec[%d] = %llu sec\n", join_state, delay);
    }
    if (join_state == SL_WISUN_JOIN_STATE_OPERATIONAL) {
      connection_time_sec = app_join_state_sec[join_state];
      connection_count ++;
      // Count disconnected time only once connected for the first time
      // (it would be unfair to count time before if the Border
      //  Router is off when the node is started)
      if (connection_count == 1) {
        printfBothTime("First connection after %llu sec\n", connection_time_sec);
        disconnected_total_sec = 0;
      } else {
        printfBothTime("Reconnected after %llu sec\n", connection_time_sec - disconnection_time_sec);
        disconnected_total_sec += connection_time_sec - disconnection_time_sec;
      }
#ifdef    HISTORY
      APPEND_TO_HISTORY(" (%d) %s |", join_state , now_str());
#endif /* HISTORY */
      parent_mac = _get_parent_mac_address();
      sprintf(parent_tag, "%02x%02x", parent_mac.address[6], parent_mac.address[7]);
      // if sockets are opened, print and send connection message
      // This will occur in case of a reconnection
      if (udp_notification_socket_id) {
        to_udp = to_coap = true;
        _print_and_send_messages (_connection_json_string(""),
            with_time, to_console, to_rtt, to_udp, to_coap);
      }
      app_set_all_traces(SL_WISUN_TRACE_LEVEL_WARN, true);
    }
    // Prepare counting disconnected time
    if (previous_join_state == SL_WISUN_JOIN_STATE_OPERATIONAL) {
      for (i=0; i<=join_state; i++) {
        // Clear join_state info for lower and equal join states
        app_join_state_sec[i] = now_sec();
      }
      for (i=0; i<=join_state; i++) {
        app_join_state_delay_sec[i+1] = app_join_state_sec[i+1] - app_join_state_sec[i];
      }
      disconnection_time_sec = app_join_state_sec[join_state];
      printfBothTime("Disconnected after %llu sec\n", disconnection_time_sec - connection_time_sec);
      connected_total_sec += disconnection_time_sec - connection_time_sec;
#ifdef    HISTORY
      APPEND_TO_HISTORY(" (%d) %s /", join_state , now_str());
#endif /* HISTORY */
      app_set_all_traces(SL_WISUN_TRACE_LEVEL_DEBUG, true);
      app_set_trace(SL_WISUN_TRACE_GROUP_RF  , SL_WISUN_TRACE_LEVEL_INFO, true);
      app_set_trace(SL_WISUN_TRACE_GROUP_FHSS, SL_WISUN_TRACE_LEVEL_INFO, true);
      just_disconnected = true;
    }
    previous_join_state = join_state;
  }
}

void  _check_neighbors(void) {
  sl_status_t ret;
  uint8_t neighbor_count;
  uint8_t i;
  ret = sl_wisun_get_neighbor_count(&neighbor_count);
  if (ret) printf("[Failed: sl_wisun_get_neighbor_count() returned 0x%04x]\n", (uint16_t)ret);
  if (neighbor_count == 0) {
    printf(" no neighbor\n");
  } else {
    for (i = 0 ; i < neighbor_count; i++) {
      printf("%s\n", app_neighbor_info_str(i));
    }
  }
}

char* _connection_json_string () {
  #define CONNECTION_JSON_FORMAT_STR                   \
    START_JSON                                         \
    DEVICE_PARENT_RUNNING_JSON                         \
    "\"join_states_sec\":[%llu,%llu,%llu,%llu,%llu]\n" \
    END_JSON

  char sec_string[20];
  sprintf(sec_string, "%s", now_str());
  refresh_parent_tag();

  snprintf(json_string, SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE,
    CONNECTION_JSON_FORMAT_STR,
    device_tag,
    CHIP,
    parent_tag,
    sec_string,
    app_join_state_delay_sec[1],
    app_join_state_delay_sec[2],
    app_join_state_delay_sec[3],
    app_join_state_delay_sec[4],
    app_join_state_delay_sec[5]
  );
  return json_string;
};

char* _status_json_string (char * start_text) {

  #define CONNECTED_JSON_FORMAT_STR        \
    "%s"                                   \
    START_JSON                             \
    DEVICE_PARENT_RUNNING_JSON             \
    "\"connected\":   \"%s\",\n"           \
    "\"disconnected\":\"%s\",\n"           \
    "\"connections\": \"%d\",\n"           \
    "\"availability\":\"%6.2f\",\n"        \
    "\"connected_total\":   \"%s\",\n"     \
    "\"disconnected_total\":\"%s\"\n"      \
    END_JSON

  char running_sec_string[20];
  char current_sec_string[20];
  char connected_sec_string[20];
  char disconnected_sec_string[20];

  uint64_t current_state_sec;

  sprintf(running_sec_string, "%s", now_str());
  refresh_parent_tag();

  if (join_state == SL_WISUN_JOIN_STATE_OPERATIONAL) {
    current_state_sec = now_sec() - connection_time_sec;
    sprintf(current_sec_string,     "%s", dhms(current_state_sec));
    sprintf(connected_sec_string,   "%s", dhms(connected_total_sec + current_state_sec));
    sprintf(disconnected_sec_string,"%s", dhms(disconnected_total_sec));
    snprintf(json_string, SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE,
      CONNECTED_JSON_FORMAT_STR,
      start_text,
      device_tag,
      CHIP,
      parent_tag,
      running_sec_string,
      current_sec_string,
      "no",
      connection_count,
      100.0*(connected_total_sec + current_state_sec)/(connected_total_sec + current_state_sec + disconnected_total_sec),
      connected_sec_string,
      disconnected_sec_string
    );
  } else {
    current_state_sec = now_sec() - disconnection_time_sec;
    sprintf(current_sec_string,     "%s", dhms(current_state_sec));
    sprintf(connected_sec_string,   "%s", dhms(connected_total_sec));
    sprintf(disconnected_sec_string,"%s", dhms(disconnected_total_sec + current_state_sec));
    snprintf(json_string, SL_WISUN_COAP_RESOURCE_HND_SOCK_BUFF_SIZE,
      CONNECTED_JSON_FORMAT_STR,
      start_text,
      device_tag,
      CHIP,
      parent_tag,
      running_sec_string,
      "no",
      current_sec_string,
      connection_count,
      100.0*(connected_total_sec)/(connected_total_sec + disconnected_total_sec + current_state_sec),
      connected_sec_string,
      disconnected_sec_string
    );
  }

  return json_string;
}

sl_status_t _select_destinations(void) {
  sl_status_t ret = SL_STATUS_OK;

  // Store device IPV6 and set the corresponding string
  ret = sl_wisun_get_ip_address(SL_WISUN_IP_ADDRESS_TYPE_GLOBAL, &device_global_ipv6);
  sl_wisun_ip6tos(device_global_ipv6.address, device_global_ipv6_string);
  NO_ERROR(ret, "Device Global IPv6:            %s\n", device_global_ipv6_string);
  IF_ERROR(ret, "[Failed: unable to retrieve the Device Global IPv6: 0x%04x]\n", (uint16_t)ret);

  // Set the UDP notification destination
  printfBothTime("UDP_NOTIFICATION_DEST: %s\n", UDP_NOTIFICATION_DEST);
  sl_wisun_stoip6(UDP_NOTIFICATION_DEST, strlen(UDP_NOTIFICATION_DEST), udp_notification_sockaddr_in6.sin6_addr.address);
  sl_wisun_ip6tos(udp_notification_sockaddr_in6.sin6_addr.address, udp_notification_ipv6_string);
  printfBothTime("UDP  Notification destination: %s/%5d\n" , udp_notification_ipv6_string, udp_notification_sockaddr_in6.sin6_port);

  // Set the CoAP notification destination
  printfBothTime("COAP_NOTIFICATION_DEST: %s\n", COAP_NOTIFICATION_DEST);
  sl_wisun_stoip6(COAP_NOTIFICATION_DEST   , strlen(COAP_NOTIFICATION_DEST), coap_notification_sockaddr_in6.sin6_addr.address);
  sl_wisun_ip6tos(coap_notification_sockaddr_in6.sin6_addr.address, coap_notification_ipv6_string);
  printfBothTime("COAP Notification destination: %s/%5d\n"  , coap_notification_ipv6_string, coap_notification_sockaddr_in6.sin6_port);

  return ret;
}

sl_status_t _open_udp_sockets(void){
  sl_status_t ret;

  _select_destinations();

  // UDP Notifications (autonomously sent by the device)
  udp_notification_socket_id = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  ret =(udp_notification_socket_id == SOCKET_INVALID_ID) ? 1 : 0;
  NO_ERROR(ret, "Opened    the UDP  notification socket (id %ld)\n", udp_notification_socket_id);
  IF_ERROR_RETURN(ret, "[Failed: unable to open the UDP notification socket]\n");

  udp_notification_sockaddr_in6.sin6_family = AF_INET6;
  udp_notification_sockaddr_in6.sin6_port = UDP_NOTIFICATION_PORT;

  // (UDP) CoAP Notifications (autonomously sent by the device)
  ret = coap_notification_socket_id = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  ret =(coap_notification_socket_id == SOCKET_INVALID_ID) ? 1 : 0;
  NO_ERROR(ret, "Opened    the COAP notification socket (id %ld)\n", coap_notification_socket_id);
  IF_ERROR_RETURN(ret, "[Failed: unable to open the COAP notification socket]\n");

  coap_notification_sockaddr_in6.sin6_family = AF_INET6;
  coap_notification_sockaddr_in6.sin6_port = COAP_NOTIFICATION_PORT;
  coap_notify_ch.sockid = coap_notification_socket_id;
  coap_notify_ch.addr = coap_notification_sockaddr_in6;

  // Prepare CoAP notification packet
  coap_notify_ch.pkt.msg_code = COAP_MSG_CODE_REQUEST_PUT;
  coap_notify_ch.pkt.msg_id = 9002U;
  coap_notify_ch.pkt.msg_type = COAP_MSG_TYPE_NON_CONFIRMABLE;
  coap_notify_ch.pkt.content_format = COAP_CT_JSON;
  coap_notify_ch.pkt.uri_path_ptr = (uint8_t *)SL_WISUN_STATUS_CONNECTION_URI_PATH;
  coap_notify_ch.pkt.uri_path_len = sl_strnlen(
      (char *) SL_WISUN_STATUS_CONNECTION_URI_PATH,
               SL_WISUN_STATUS_JSON_STR_MAX_LEN);

  coap_notify_ch.pkt.token_ptr = NULL;
  coap_notify_ch.pkt.token_len = 0U;
  coap_notify_ch.pkt.options_list_ptr = NULL;

  return SL_STATUS_OK;

};

sl_status_t _coap_notify(char* json_string)
{
  sl_status_t ret = SL_STATUS_OK;
  uint16_t req_buff_size = 0UL;
  uint8_t * buff = NULL;

  coap_notify_ch.pkt.payload_ptr = (uint8_t *)json_string;
  coap_notify_ch.pkt.payload_len = sl_strnlen((char *) coap_notify_ch.pkt.payload_ptr,
                                              SL_WISUN_STATUS_JSON_STR_MAX_LEN);

  req_buff_size = sl_wisun_coap_builder_calc_size(&coap_notify_ch.pkt);

  buff = (uint8_t *) sl_wisun_coap_malloc(req_buff_size);
  if (buff == NULL) {
    printfBothTime("_coap_notify() error on line %d: sl_wisun_coap_malloc buff(%d)\n", __LINE__,
                  req_buff_size);
    return __LINE__;
  }
  if (sl_wisun_coap_builder(buff, &coap_notify_ch.pkt) < 0) {
    printfBothTime("_coap_notify() error on line %d: sl_wisun_coap_builder()\n", __LINE__);
  } else {
      /* Casting udp_notification_sockaddr_in6 (type sockaddr_in6_t) to (const struct sockaddr *) to match POSIX socket interface */
      if (sendto(coap_notify_ch.sockid,
                  buff,
                  req_buff_size,
                  0,
                  (const struct sockaddr *) &coap_notification_sockaddr_in6,
                  sizeof(sockaddr_in6_t)) == -1) {
          ret = SL_STATUS_TRANSMIT;
      }
  }
  sl_wisun_coap_free(buff);
  return ret;
}

uint8_t _print_and_send_messages (char *in_msg, bool with_time,
                            bool to_console, bool to_rtt, bool to_udp, bool to_coap) {
  sl_status_t ret = SL_STATUS_OK;
  uint8_t messages_processed = 0;
  uint16_t udp_msg_len;
  uint16_t coap_msg_len;

  if (to_console == true) { // Print to console
      if (with_time == true) {
        printfTime(in_msg);
      } else {
        printf(in_msg);
      }
    messages_processed++;
  }
  if (to_rtt == true) {     // Print to RTT traces
      if (with_time == true) {
        printfTimeRTT(in_msg);
      } else {
        printfRTT(in_msg);
      }
    messages_processed++;
  }
  if (to_udp == true) {     // Send to UDP port
    udp_msg_len  = snprintf(udp_msg,  1024, "%s", in_msg);
    if (sendto(udp_notification_socket_id,
                (uint8_t *)udp_msg,
                udp_msg_len,
                0L,
                (const struct sockaddr *) &udp_notification_sockaddr_in6,
                sizeof(sockaddr_in6_t)) == -1) {
      printfBothTime("\n[Failed (line %d): unable to send to the UDP notification socket (%ld %s/%d)] udp_msg_len %d\n", __LINE__,
              udp_notification_socket_id, udp_notification_ipv6_string , UDP_NOTIFICATION_PORT, udp_msg_len);
    } else {
      messages_processed++;
    }
  }
  if (to_coap == true) {    // Send to CoAP notification port
    coap_msg_len = snprintf(coap_msg, 1024, "%s", in_msg);
    if (coap_msg_len > SL_WISUN_STATUS_JSON_STR_MAX_LEN) {
        printfBothTime("\n[Failed (line %d): CoAP message len %d is higher than MAX %d]. Message not sent because it would overflow\n", __LINE__,
                coap_msg_len, SL_WISUN_STATUS_JSON_STR_MAX_LEN);
    } else {
      ret = _coap_notify(coap_msg);
      IF_ERROR(ret, "[Failed (line %d): unable to send to the CoAP notification socket (%ld %s/%d): 0x%04x. Check sl_status.h]\n", __LINE__,
              coap_notification_socket_id, coap_notification_ipv6_string, COAP_NOTIFICATION_PORT, (uint16_t)ret);
      if (ret == SL_STATUS_OK) messages_processed++;
    }
  }

  return messages_processed;
}
