/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Constantin Wenger <constantin.wenger@googlemail.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_ETOMMENS_ETM_XXXXP_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ETOMMENS_ETM_XXXXP_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "etommens_etm_xxxxp"

struct etommens_etm_xxxxp_model {
	unsigned int classid;
	unsigned int modelid;
	const char *name;
	unsigned int max_current;
	unsigned int max_voltage;
	unsigned int max_power;
	unsigned int current_digits;
	unsigned int voltage_digits;
	unsigned int power_digits;
};

struct dev_context {
	const struct etommens_etm_xxxxp_model *model;
	struct sr_sw_limits limits;
	GMutex rw_mutex;
	double current_multiplier;
	double voltage_multiplier;
	gboolean last_output_state;
	gboolean last_ovp_state;
	gboolean last_ocp_state;
	gboolean last_otp_state;
};

enum etommens_etm_xxxxp_register {
	REG_POWER_OUT	= 0x01,
	REG_PROTECTION	= 0x02,
	REG_MODEL	= 0x03,
	REG_CLASS	= 0x04,
	REG_UOUT	= 0x10,
	REG_IOUT	= 0x11,
	REG_POWER1	= 0x12, // W power has 2x16bit
	REG_POWER2	= 0x13,
	REG_USET	= 0x30,
	REG_ISET	= 0x31,
	REG_OVP_VALUE	= 0x20,
	REG_OCP_VALUE	= 0x21,
	REG_OPP_VALUE1	= 0x22,// W power has 2x16bits
	REG_OPP_VALUE2	= 0x23,
};

SR_PRIV int etommens_etm_xxxxp_reg_get(const struct sr_dev_inst *sdi,
				uint16_t address, uint16_t *value);
SR_PRIV int etommens_etm_xxxxp_reg_set(const struct sr_dev_inst *sdi,
				uint16_t address, uint16_t value);
SR_PRIV int etommens_etm_xxxxp_device_info_get(struct sr_modbus_dev_inst *modbus,
				uint16_t *model, uint16_t *dclass);
SR_PRIV int etommens_etm_xxxxp_receive_data(int fd, int revents, void *cb_data);

#endif
