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
#include <math.h>
#include "protocol.h"

static struct sr_dev_driver etommens_etm_xxxxp_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	SR_CONF_MODBUSADDR,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET | SR_CONF_SET,
};

/* Class ID, Model ID, model name, max current, max voltage, max power,
 * current digits, voltatge digits, power digits
 */
static const struct etommens_etm_xxxxp_model supported_models[] = {
	// class id 19280=KP and model 3010
	{ 0x4B50, 3010, "eTM-3010P/RS310P/HM310P" },
	{ 0x4B50, 305, "eTM-305P/RS305P/HM305P" },
};

static struct sr_dev_inst *probe_device(struct sr_modbus_dev_inst *modbus)
{
	sr_dbg("probing device");
	const struct etommens_etm_xxxxp_model *model = NULL;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	uint16_t modelid;
	uint16_t dclassid;
	uint16_t limit_voltage;
	uint16_t limit_current;
	uint16_t digits_voltage;
	uint16_t digits_current;
	uint16_t digits_power;
	unsigned int i;
	int ret;

	ret = etommens_etm_xxxxp_device_info_get(
			modbus, &modelid, &dclassid, &limit_voltage,
			&limit_current, &digits_voltage, &digits_current,
			&digits_power);
	if (ret != SR_OK)
		return NULL;
	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (modelid == supported_models[i].modelid &&
				dclassid == supported_models[i].classid) {
			model = &supported_models[i];
			break;
		}
		if (model == NULL) {
			sr_err("Unknown model %d and class 0x%X combination.",
					modelid, dclassid);
			return NULL;
		}
	}

	model = &supported_models[0];
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("RockSeed");
	sdi->model = g_strdup(model->name);
	sdi->version = g_strdup("etommens_etm_xxxxp");
	sdi->conn = modbus;
	sdi->driver = &etommens_etm_xxxxp_driver_info;
	sdi->inst_type = SR_INST_MODBUS;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "V");
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "I");
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P");

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	devc->model = model;
	devc->current_multiplier = pow(10.0, digits_current);
	devc->voltage_multiplier = pow(10.0, digits_voltage);
	devc->power_multiplier = pow(10.0, digits_power);
	devc->max_voltage = limit_voltage / devc->voltage_multiplier;
	devc->max_current = limit_current / devc->current_multiplier;
	devc->digits_current = digits_current;
	devc->digits_voltage = digits_voltage;
	devc->digits_power = digits_power;

	sdi->priv = devc;

	return sdi;
}

static int config_compare(gconstpointer a, gconstpointer b)
{
	const struct sr_config *ac = a, *bc = b;

	return ac->key != bc->key;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config default_serialcomm = {
		.key = SR_CONF_SERIALCOMM,
		.data = g_variant_new_string("9600/8n1"),
	};
	struct sr_config default_modbusaddr = {
		.key = SR_CONF_MODBUSADDR,
		.data = g_variant_new_uint64(1),
	};
	GSList *opts = options, *devices;

	if (!g_slist_find_custom(options, &default_serialcomm, config_compare))
		opts = g_slist_prepend(opts, &default_serialcomm);
	if (!g_slist_find_custom(options, &default_modbusaddr, config_compare))
		opts = g_slist_prepend(opts, &default_modbusaddr);

	devices = sr_modbus_scan(di->context, opts, probe_device);

	while (opts != options)
		opts = g_slist_delete_link(opts, opts);
	g_variant_unref(default_serialcomm.data);
	g_variant_unref(default_modbusaddr.data);

	return devices;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus = sdi->conn;

	if (sr_modbus_open(modbus) < 0)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;

	modbus = sdi->conn;
	if (!modbus)
		return SR_ERR_BUG;

	return sr_modbus_close(modbus);
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;
	uint16_t ivalue;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_VOLTAGE_TARGET:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_USET, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->voltage_multiplier);
		break;
	case SR_CONF_CURRENT_LIMIT:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_ISET, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->current_multiplier);
		break;
	case SR_CONF_CURRENT:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_IOUT, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->current_multiplier);
		break;
	case SR_CONF_VOLTAGE:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_UOUT, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->voltage_multiplier);
		break;
	case SR_CONF_ENABLED:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_POWER_OUT, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_boolean(ivalue);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_PROTECTION, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_boolean((ivalue & 0x0001));
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_PROTECTION, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_boolean((ivalue & 0x0002));
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_PROTECTION, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_boolean((ivalue & 0x0008));
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_OCP_VALUE, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->current_multiplier);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		ret = etommens_etm_xxxxp_reg_get(sdi, REG_OVP_VALUE, &ivalue);
		if (ret == SR_OK)
			*data = g_variant_new_double((float)ivalue /
					devc->voltage_multiplier);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_VOLTAGE_TARGET:
		return etommens_etm_xxxxp_reg_set(sdi, REG_USET,
				g_variant_get_double(data) *
				devc->voltage_multiplier);
	case SR_CONF_CURRENT_LIMIT:
		return etommens_etm_xxxxp_reg_set(sdi, REG_ISET,
				g_variant_get_double(data) *
				devc->current_multiplier);
	case SR_CONF_ENABLED:
		return etommens_etm_xxxxp_reg_set(sdi, REG_POWER_OUT,
				g_variant_get_boolean(data));
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		return etommens_etm_xxxxp_reg_set(sdi, REG_OVP_VALUE,
				g_variant_get_double(data) *
				devc->voltage_multiplier);
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		return etommens_etm_xxxxp_reg_set(sdi, REG_OCP_VALUE,
				g_variant_get_double(data) *
				devc->current_multiplier);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	devc = (sdi) ? sdi->priv : NULL;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
				devopts);
	case SR_CONF_VOLTAGE_TARGET:
		*data = std_gvar_min_max_step(0.0, devc->max_voltage,
				1 / devc->voltage_multiplier);
		break;
	case SR_CONF_CURRENT_LIMIT:
		*data = std_gvar_min_max_step(0.0, devc->max_current,
				1 / devc->current_multiplier);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		*data = std_gvar_min_max_step(0.0, devc->max_current + 0.5,
				1 / devc->current_multiplier);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		*data = std_gvar_min_max_step(0.0, devc->max_voltage + 3,
				1 / devc->voltage_multiplier);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_modbus_dev_inst *modbus;
	int ret;

	modbus = sdi->conn;
	devc = sdi->priv;
	devc->last_ocp_state = 0x0;
	devc->last_otp_state = 0x0;
	devc->last_ovp_state = 0x0;
	devc->last_output_state = 0x0;
	ret = sr_modbus_source_add(sdi->session, modbus, G_IO_IN, 10,
			etommens_etm_xxxxp_receive_data, (void *)sdi);
	if (ret != SR_OK)
		return ret;
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);
	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_modbus_dev_inst *modbus;

	std_session_send_df_end(sdi);
	modbus = sdi->conn;
	sr_modbus_source_remove(sdi->session, modbus);

	return SR_OK;
}

static struct sr_dev_driver etommens_etm_xxxxp_driver_info = {
	.name = "etommens_etm_xxxxp",
	.longname = "Etommens eTM-XXXXP",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(etommens_etm_xxxxp_driver_info);
