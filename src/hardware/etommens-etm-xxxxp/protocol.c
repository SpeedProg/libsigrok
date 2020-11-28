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

#include <config.h>
#include "protocol.h"

/**
 * @private
 * Read a single register from the device.
 *
 * @param[in] The device instance
 * @param[in] register address to read from
 * @param[out] buffer to store the read register value
 *
 * @return SR_OK upon success, SR_ERR_ARG on invalid arguments,
 *         SR_ERR_DATA upon invalid data, or SR_ERR on failure.
 */
SR_PRIV int etommens_etm_xxxxp_reg_get(const struct sr_dev_inst *sdi,
		uint16_t address, uint16_t *value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	uint16_t registers[1];
	int ret;

	devc = sdi->priv;
	modbus = sdi->conn;

	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_read_holding_registers(modbus, address, 1, registers);
	g_mutex_unlock(&devc->rw_mutex);
	if (ret == SR_OK)
		*value = RB16(registers + 0);
	return ret;
}

/**
 * @private
 * Write a single register onto the device
 *
 * This device only supports 0x06 write single register.
 * So we can't use the more general modbus write function that use 0x10.
 *
 * @param[in] sdi The device instance
 * @param[in] address register address to write into
 * @param[in] value data to write into the register
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments,
 *         or SR_ERR on failure.
 */
SR_PRIV int etommens_etm_xxxxp_reg_set(const struct sr_dev_inst *sdi,
		uint16_t address, uint16_t value)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;
	uint8_t request[5], reply[5];

	devc = sdi->priv;
	modbus = sdi->conn;
	W8(request + 0, 0x06); // write single register
	WB16(request + 1, address);
	WB16(request + 3, value);

	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_request_reply(modbus, request, sizeof(request), reply,
			sizeof(reply));
	g_mutex_unlock(&devc->rw_mutex);
	return ret;
}

/**
 * @private
 * Read model from the device
 *
 * @param[in] modbus The modbus interface
 * @param[out] model buffer for the model to be written into
 * @param[out] dclass device class, this is used in manufacturer software to load info
 * @param[out] max_voltage upper limit for voltage using device internal format
 * @param[out] max_current upper limit for current using device internal format
 * @param[out] digits_voltage how many digits are used for voltage (use this to divide device format with 10^digits_voltage)
 * @param[out] digits_current how many digits are used for current (see digits_voltage)
 * @param[out] digits_power how many digits are used for power (see digits_voltage)
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments,
 *         SR_ERR_DATA upon invalid data, or SR_ERR on failure.
 */
SR_PRIV int etommens_etm_xxxxp_device_info_get(
		struct sr_modbus_dev_inst *modbus, uint16_t *model,
		uint16_t *dclass, uint16_t *max_voltage, uint16_t *max_current,
		uint16_t *digits_voltage, uint16_t *digits_current,
		uint16_t *digits_power)
{
	uint16_t registers[3];
	uint16_t registersumax[1];
	uint16_t registersimax[1];
	uint16_t decimals;
	int ret;

	ret = sr_modbus_read_holding_registers(modbus, REG_MODEL, 3, registers);
	if (ret == SR_OK) {
		*model = RB16(registers);
		*dclass = RB16(registers + 1);
		decimals = RB16(registers + 2);
		*digits_voltage = (decimals >> 8) & 0x000F;
		*digits_current = (decimals >> 4) & 0x000F;
		*digits_power = decimals & 0x000F;
	} else {
		return ret;
	}

	ret = sr_modbus_read_holding_registers(modbus, REG_U_CEIL, 1,
			registersumax);
	if (ret == SR_OK)
		*max_voltage = RB16(registersumax);
	else
		return ret;
	ret = sr_modbus_read_holding_registers(modbus, REG_I_CEIL, 1,
			registersimax);
	if (ret == SR_OK)
		*max_current = RB16(registersimax);
	else
		return ret;
	sr_dbg("Decimals: 0x%X", decimals);
	sr_dbg("decimals for voltage 0x%X current 0x%X power 0x%X",
			*digits_voltage, *digits_current, *digits_power);
	sr_dbg("Max voltage: %d Max current", *max_voltage, *max_current);
	return SR_OK;
}

/**
 * @private
 * Send a SR_DF_ANALOG packet with information.
 *
 * @param[in] sdi The device instance
 * @param[in] ch The channel
 * @param[in] value the value to send
 * @param[in] mq the message queue to send it to, like voltage or current
 * @param[in] mqflags additional message queue flag
 * @param[in] unit the unit of the value provided
 * @param[in] digits how many digits the value has
 */
static void send_value(const struct sr_dev_inst *sdi, struct sr_channel *ch,
		float value, enum sr_mq mq, enum sr_mqflag mqflags,
		enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &value;
	analog.meaning->mq = mq;
	analog.meaning->mqflags = mqflags;
	analog.meaning->unit = unit;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

/**
 * @private
 * Query data from the device and send it to the callback
 *
 * @param[in] fd
 * @param[in] revents
 * @param[in] cb_data The device instance
 *
 * @return always returns TRUE
 */
SR_PRIV int etommens_etm_xxxxp_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	struct sr_datafeed_packet packet;
	uint16_t registers[4];
	uint16_t power_register[1];
	uint16_t protect_register[1];
	int ret, ret_power, ret_protect;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	modbus = sdi->conn;
	devc = sdi->priv;

	g_mutex_lock(&devc->rw_mutex);
	ret = sr_modbus_read_holding_registers(modbus, REG_UOUT, 4, registers);
	ret_power = sr_modbus_read_holding_registers(modbus, REG_POWER_OUT, 1,
			power_register);
	ret_protect = sr_modbus_read_holding_registers(modbus, REG_PROTECTION,
			1, protect_register);
	g_mutex_unlock(&devc->rw_mutex);
	if (ret == SR_OK) {
		packet.type = SR_DF_FRAME_BEGIN;
		sr_session_send(sdi, &packet);

		send_value(sdi, sdi->channels->data,
				RB16(registers + 0) / devc->voltage_multiplier,
				SR_MQ_VOLTAGE, SR_MQFLAG_DC, SR_UNIT_VOLT,
				devc->digits_voltage);
		send_value(sdi, sdi->channels->next->data,
				RB16(registers + 1) / devc->current_multiplier,
				SR_MQ_CURRENT, SR_MQFLAG_DC, SR_UNIT_AMPERE,
				devc->digits_current);
		send_value(sdi, sdi->channels->next->next->data,
				RB32(registers + 2) / devc->power_multiplier,
				SR_MQ_POWER, 0, SR_UNIT_WATT,
				devc->digits_power);

		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);
		sr_sw_limits_update_samples_read(&devc->limits, 1);
	}
	if (ret_power == SR_OK) {
		if (devc->last_output_state != RB16(power_register)) {
			devc->last_output_state = RB16(power_register);
			sr_session_send_meta(sdi, SR_CONF_ENABLED,
					g_variant_new_boolean(
					devc->last_output_state));
		}
	}
	if (ret_protect == SR_OK) {
		if (devc->last_ovp_state != (RB16(protect_register) & 0x0001)) {
			devc->last_ovp_state = RB16(protect_register) & 0x0001;
			sr_session_send_meta(sdi,
					SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE,
					g_variant_new_boolean(
					devc->last_ovp_state));
		}
		if (devc->last_ocp_state != (RB16(protect_register) & 0x0002)) {
			devc->last_ocp_state = RB16(protect_register) & 0x0002;
			sr_session_send_meta(sdi,
					SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,
					g_variant_new_boolean(
					devc->last_ocp_state));
		}

		if (devc->last_otp_state != (RB16(protect_register) & 0x0008)) {
			devc->last_otp_state = RB16(protect_register) & 0x0008;
			sr_session_send_meta(sdi,
					SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE,
					g_variant_new_boolean(devc->last_otp_state));
		}
	}

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
	return TRUE;
}
