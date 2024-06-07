/***************************************************************************//**
 * @brief RAIL Configuration
 * @details
 *   WARNING: Auto-Generated Radio Config Header  -  DO NOT EDIT
 *   Radio Configurator Version: 2304.4.4
 *   RAIL Adapter Version: 2.4.33
 *   RAIL Compatibility: 2.x
 *******************************************************************************
 * # License
 * <b>Copyright 2019 Silicon Laboratories Inc. www.silabs.com</b>
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
 ******************************************************************************/

#ifndef __RAIL_CONFIG_H__
#define __RAIL_CONFIG_H__

#include <stdint.h>
#include "rail_types.h"
#include "rail_ieee802154.h"

#define WISUNCONF_ACCELERATION_BUFFER wisunconfAccelerationBuffer
extern uint32_t wisunconfAccelerationBuffer[];

#define RADIO_CONFIG_XTAL_FREQUENCY 39000000UL

#define RAIL0_WISUNCONF_CHANNEL_GROUP_1_PHY_WISUN_FAN_1V1_863MHZ_PLAN33_2FSK_3_EU
#define RAIL0_WISUNCONF_CHANNEL_GROUP_1_PROFILE_WISUN_FAN_1_1
extern const RAIL_ChannelConfig_t *channelConfigs[];

#define WISUN_MODESWITCHPHRS_ARRAY_SIZE 1
extern const uint8_t wisun_modeSwitchPhrsLength;
extern const RAIL_IEEE802154_ModeSwitchPhr_t wisun_modeSwitchPhrs[WISUN_MODESWITCHPHRS_ARRAY_SIZE];

typedef struct RAIL_StackInfoWisun {
  RAIL_StackInfoCommon_t stackInfoCommon;
  uint8_t version;
  uint8_t wisunChannelParam;// wisunOperatingClass for version=0, wisunChannelPlanId for version=1
  uint8_t wisunRegDomain;
} RAIL_StackInfoWisun_t;

#endif // __RAIL_CONFIG_H__
