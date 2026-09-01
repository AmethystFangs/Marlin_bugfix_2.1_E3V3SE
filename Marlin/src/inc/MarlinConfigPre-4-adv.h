/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2024 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

#ifndef __MARLIN_DEPS__
  #include HAL_PATH(.., inc/Conditionals_LCD.h)
#endif

#include "Conditionals-3-etc.h"

#include "../core/drivers.h"

#if USE_STD_CONFIGS
  #if __has_include("../../Configuration_adv.h")
    #include "../../Configuration_adv.h"
  #endif
#endif

// Ender-3 V3 SE F401 / TMC2208 reliability defaults for FT Motion.
// See navaismo/Marlin_bugfix_2.1_E3V3SE#23.
#if MB(CREALITY_F401RE) && ENABLED(FT_MOTION)
  #undef LIN_ADVANCE
  #undef STEALTHCHOP_E
  #if AXIS_DRIVER_TYPE_X(TMC2208) || AXIS_DRIVER_TYPE_X(TMC2208_STANDALONE)
    #define FTM_DIR_CHANGE_HOLD_X
  #endif
  #if AXIS_DRIVER_TYPE_Y(TMC2208) || AXIS_DRIVER_TYPE_Y(TMC2208_STANDALONE)
    #define FTM_DIR_CHANGE_HOLD_Y
  #endif
  #if AXIS_DRIVER_TYPE_Z(TMC2208) || AXIS_DRIVER_TYPE_Z(TMC2208_STANDALONE)
    #define FTM_DIR_CHANGE_HOLD_Z
  #endif
  #if HAS_E_DRIVER(TMC2208) || HAS_E_DRIVER(TMC2208_STANDALONE)
    #define FTM_DIR_CHANGE_HOLD_E
  #endif
#endif
