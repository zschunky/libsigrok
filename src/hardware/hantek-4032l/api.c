/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Andreas Zschunke <andreas.zschunke@gmx.net>
 * Copyright (C) 2017 Andrej Valek <andy@skyrain.eu>
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

// begin look up tables
static const uint32_t s_h4032l_api_scan_options[] = {
	SR_CONF_CONN,
};

static const uint32_t s_h4032l_api_driver_options[] = {
	SR_CONF_LOGIC_ANALYZER, 
};

static const uint32_t s_h4032l_api_device_options[] = {
	SR_CONF_SAMPLERATE | SR_CONF_LIST| SR_CONF_SET | SR_CONF_GET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_SET | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_GET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_SET | SR_CONF_LIST,
};

static const int32_t s_h4032l_api_triggers[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const uint64_t s_h4032l_api_sample_rates_sorted[] = {
	SR_KHZ(1),
	SR_KHZ(2),
	SR_KHZ(4),
	SR_KHZ(8),
	SR_KHZ(16),
	SR_HZ(31250),
	SR_HZ(62500),
	SR_KHZ(125),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_KHZ(625),
	SR_HZ(781250),
	SR_MHZ(1),
	SR_KHZ(1250),
	SR_HZ(1562500),
	SR_MHZ(2),
	SR_KHZ(2500),
	SR_KHZ(3125),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_KHZ(6250),
	SR_MHZ(10),
	SR_KHZ(12500),
	SR_MHZ(20),
	SR_MHZ(25),
	SR_MHZ(40),
	SR_MHZ(50),
	SR_MHZ(80),
	SR_MHZ(100),
	SR_MHZ(160),
	SR_MHZ(200),
	SR_MHZ(320),
	SR_MHZ(400),
};

static const uint64_t s_h4032l_api_sample_rates_config[] = {
	SR_MHZ(100),
	SR_MHZ(50),
	SR_MHZ(25),
	SR_KHZ(12500),
	SR_KHZ(6250),
	SR_KHZ(3125),
	SR_HZ(1562500),
	SR_HZ(781250),
	SR_MHZ(80),
	SR_MHZ(40),
	SR_MHZ(20),
	SR_MHZ(10),
	SR_MHZ(5),
	SR_KHZ(2500),
	SR_KHZ(1250),
	SR_KHZ(625),
	SR_MHZ(4),
	SR_MHZ(2),
	SR_MHZ(1),
	SR_KHZ(500),
	SR_KHZ(250),
	SR_KHZ(125),
	SR_HZ(62500),
	SR_HZ(31250),
	SR_KHZ(16),
	SR_KHZ(8),
	SR_KHZ(4),
	SR_KHZ(2),
	SR_KHZ(1),
	0,
	0,
	0,
	SR_MHZ(200),
	SR_MHZ(160),
	SR_MHZ(400),
	SR_MHZ(320),
};
// end look up tables

SR_PRIV struct sr_dev_driver hantek_4032l_driver_info;

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(di, sr_ctx);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	GSList *l, *devices, *conn_devices;
	libusb_device **devlist;
	struct libusb_device_descriptor des;
	const char *conn;
	int i;
	char connection_id[64];

	devices = NULL;
	conn_devices = NULL;
	drvc->instances = NULL;
	conn = NULL;

	for (l = options; l; l = l->next) {
		struct sr_config *src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	// find all hantek devices
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			struct sr_usb_dev_inst *usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
					&& usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != H4032L_PROTOCOL_USB_VENDOR ||
			des.idProduct != H4032L_PROTOCOL_USB_PRODUCT)
			continue;

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		struct sr_dev_inst *device = g_malloc0(sizeof(struct sr_dev_inst));
		device->driver = &hantek_4032l_driver_info;
		device->vendor = g_strdup("Hantek");
		device->model = g_strdup("4032L");
		device->connection_id = g_strdup(connection_id);

		// create channel groups
		struct sr_channel_group *channel_groups[2];
		for (int group=0; group<2; group++) {
			struct sr_channel_group *channel_group=g_malloc0(sizeof(struct sr_channel_group));
			channel_group->name=g_strdup_printf("%c", 'A' + group);
			channel_groups[group]=channel_group;
			device->channel_groups = g_slist_append(device->channel_groups, channel_group);
		}

		// assemble channel list and add channel to channel groups
		for (int index=0; index < NUM_CHANNELS; index++) {
			char channel_name[4];
			sprintf(channel_name,"%c%d", 'A'+ (index&1), index /2);

			struct sr_channel *channel = sr_channel_new(device, index, SR_CHANNEL_LOGIC, TRUE, channel_name);
			struct sr_channel_group *channel_group=channel_groups[index&1];
			channel_group->channels = g_slist_append(channel_group->channels, channel);
		}

		// create device context
		struct h4032l_protocol_device_context *device_context = g_malloc0(sizeof(struct h4032l_protocol_device_context));

		// initialize command packet
		device_context->command_packet.magic=H4032L_PROTOCOL_COMMAND_PACKET_MAGIC;
		device_context->command_packet.pwm_a=h4032l_protocol_voltage2pwm(2.5);
		device_context->command_packet.pwm_b=h4032l_protocol_voltage2pwm(2.5);
		device_context->command_packet.sample_size=16384;
		device_context->command_packet.pre_trigger_size=1024;

		// set status
		device_context->status=H4032L_PROTOCOL_STATUS_IDLE;

		// set ratio
		device_context->gcapture_ratio=5;

		// create libusb transfer
		device_context->usb_transfer=libusb_alloc_transfer(0);

		device->priv = device_context;
		devices = g_slist_append(devices, device);

		device->status = SR_ST_INACTIVE;
		device->inst_type = SR_INST_USB;
		device->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
	}

	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	ret = h4032l_protocol_dev_open(sdi);
	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
	if (ret != 0) {
		switch (ret) {
		case LIBUSB_ERROR_BUSY:
			sr_err("Unable to claim USB interface. Another "
				"program or driver has already claimed it.");
			break;
		case LIBUSB_ERROR_NO_DEVICE:
			sr_err("Device has been disconnected.");
			break;
		default:
			sr_err("Unable to claim interface: %s.",
				libusb_error_name(ret));
			break;
		}

		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return SR_ERR_BUG;

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(const struct sr_dev_driver *di)
{
	return std_dev_clear(di);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)cg;

	struct h4032l_protocol_device_context *device_context=sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(s_h4032l_api_sample_rates_config[device_context->command_packet.sample_rate]);
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(device_context->gcapture_ratio);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES: 
		*data = g_variant_new_uint64(device_context->command_packet.sample_size);
		return SR_OK;
	case SR_CONF_CONN: {
			if (!sdi->conn)
				return SR_ERR_ARG;
			struct sr_usb_dev_inst *usb = sdi->conn;
			char str[128];
			snprintf(str, 128, "%d.%d", usb->bus, usb->address);
			*data = g_variant_new_string(str);
			return SR_OK;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)cg;
	struct h4032l_protocol_device_context *device_context=sdi->priv;
	struct h4032l_protocol_command_packet *command_packet=&device_context->command_packet;
	switch (key) {
		case SR_CONF_SAMPLERATE: {
				uint64_t sample_rate=g_variant_get_uint64(data);
				uint8_t i=0;
				while (i < ARRAY_SIZE(s_h4032l_api_sample_rates_config) && s_h4032l_api_sample_rates_config[i] != sample_rate)
					i++;

				if (i==ARRAY_SIZE(s_h4032l_api_sample_rates_config) || sample_rate == 0) {
					sr_err("invalid sample rate");
					return SR_ERR_SAMPLERATE;
				}
				command_packet->sample_rate=i;

				return SR_OK;
			}
		case SR_CONF_CAPTURE_RATIO: {
				uint64_t ratio = g_variant_get_uint64(data);
				if (ratio > 100) {
					sr_err("capture ratio should be between 0 ... 100");
					return SR_ERR;
				}
				device_context->gcapture_ratio=ratio;
				return SR_OK;
			}
		case SR_CONF_LIMIT_SAMPLES: {
				uint64_t number_samples = g_variant_get_uint64(data);
				number_samples+=511;
				number_samples&=0xfffffe00;
				if (number_samples < 2048 || number_samples > 64*1024*1024) {
					sr_err("invalid sample range 2k...64M: %ld", number_samples);
					return SR_ERR;
				}
				command_packet->sample_size=number_samples;
				return SR_OK;
			}
	case SR_CONF_VOLTAGE_THRESHOLD: {
				double d1,d2;
				g_variant_get(data, "(dd)", &d1, &d2);
				device_context->command_packet.pwm_a=h4032l_protocol_voltage2pwm(d1);
				device_context->command_packet.pwm_b=h4032l_protocol_voltage2pwm(d2);
				return SR_OK;
			}
	}

	return SR_ERR_NA;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void)cg;

	GVariantBuilder var;
	GVariant *gvar, *range[2];

	switch (key) {
		case SR_CONF_SCAN_OPTIONS: 
			// connection options
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, s_h4032l_api_scan_options, ARRAY_SIZE(s_h4032l_api_scan_options), sizeof(uint32_t));
			return SR_OK; 
		case SR_CONF_DEVICE_OPTIONS:
			if (sdi)
				// device options
				*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, s_h4032l_api_device_options, ARRAY_SIZE(s_h4032l_api_device_options), sizeof(uint32_t));
			else
				// driver options
				*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32, s_h4032l_api_driver_options, ARRAY_SIZE(s_h4032l_api_driver_options), sizeof(uint32_t));
			return SR_OK; 
		case SR_CONF_SAMPLERATE:
			// list of supported samplerates
			g_variant_builder_init(&var, G_VARIANT_TYPE("a{sv}"));
			g_variant_builder_add(&var, "{sv}", "samplerates", g_variant_new_fixed_array(G_VARIANT_TYPE("t"), s_h4032l_api_sample_rates_sorted, ARRAY_SIZE(s_h4032l_api_sample_rates_sorted), sizeof(uint64_t)));
			*data = g_variant_builder_end(&var);
			return SR_OK; 
		case SR_CONF_TRIGGER_MATCH:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32, s_h4032l_api_triggers, ARRAY_SIZE(s_h4032l_api_triggers), sizeof(int32_t));
			return SR_OK; 
		case SR_CONF_VOLTAGE_THRESHOLD:
			g_variant_builder_init(&var, G_VARIANT_TYPE_ARRAY);
			range[0] = g_variant_new_double(2.5);
			range[1] = g_variant_new_double(2.5);
			gvar = g_variant_new_tuple(range, 2);
			g_variant_builder_add_value(&var, gvar);
			*data = g_variant_builder_end(&var);
			
			return SR_OK; 
	}

	return SR_ERR_NA;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	struct h4032l_protocol_device_context *device_context=sdi->priv;
	struct sr_trigger *trigger=sr_session_trigger_get(sdi->session);
	struct h4032l_protocol_command_packet *command_packet=&device_context->command_packet;

	// calculate packet ratio
	command_packet->pre_trigger_size=(command_packet->sample_size * device_context->gcapture_ratio)/100;

	command_packet->trig_flags.enable_trigger1=0;
	command_packet->trig_flags.enable_trigger2=0;
	command_packet->trig_flags.trigger_and_logic=0;
	if (trigger && trigger->stages) {
		GSList *stages=trigger->stages;
		struct sr_trigger_stage *stage1=stages->data;
		if (stages->next) {
			sr_err("only one trigger stage supported for now");
			return SR_ERR;
		}
		command_packet->trig_flags.enable_trigger1=1;
		command_packet->trigger[0].flags.edge_type=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_DISABLED;
		command_packet->trigger[0].flags.data_range_enabled=0;
		command_packet->trigger[0].flags.time_range_enabled=0;
		command_packet->trigger[0].flags.combined_enabled=0;
		command_packet->trigger[0].flags.data_range_type=H4032L_PROTOCOL_TRIGGER_DATA_RANGE_TYPE_MAX;
		command_packet->trigger[0].data_range_mask=0;
		command_packet->trigger[0].data_range_max=0;

		// initialize range mask values
		uint32_t range_mask=0;
		uint32_t range_value=0;

		GSList *channel=stage1->matches;
		while (channel) {
			struct sr_trigger_match *match=channel->data;

			switch (match->match) {
				case SR_TRIGGER_ZERO:
					range_mask|=(1<<match->channel->index);
					break;
				case SR_TRIGGER_ONE:
					range_mask|=(1<<match->channel->index);
					range_value|=(1<<match->channel->index);
					break;
				case SR_TRIGGER_RISING:
					if (command_packet->trigger[0].flags.edge_type!=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_DISABLED) {
						sr_err("only one trigger signal with fall/rising/edge allowed");
						return SR_ERR;
					}
					command_packet->trigger[0].flags.edge_type=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_RISE;
					command_packet->trigger[0].flags.edge_signal=match->channel->index;
					break;
				case SR_TRIGGER_FALLING:
					if (command_packet->trigger[0].flags.edge_type!=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_DISABLED) {
						sr_err("only one trigger signal with fall/rising/edge allowed");
						return SR_ERR;
					}
					command_packet->trigger[0].flags.edge_type=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_FALL;
					command_packet->trigger[0].flags.edge_signal=match->channel->index;
					break;
				case SR_TRIGGER_EDGE:
					if (command_packet->trigger[0].flags.edge_type!=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_DISABLED) {
						sr_err("only one trigger signal with fall/rising/edge allowed");
						return SR_ERR;
					}
					command_packet->trigger[0].flags.edge_type=H4032L_PROTOCOL_TRIGGER_EDGE_TYPE_TOGGLE;
					command_packet->trigger[0].flags.edge_signal=match->channel->index;
					break;
				default:
					sr_err("unknown trigger value");
					return SR_ERR;
			}

			channel=channel->next;
		}

		// commpres range mask value and apply range settings
		if (range_mask) {
			command_packet->trigger[0].flags.data_range_enabled=1;
			command_packet->trigger[0].data_range_mask|=(range_mask);

			uint32_t new_range_value=0;
			uint32_t bit_mask=1;
			while (range_mask) {
				if ((range_mask & 1) != 0) {
					new_range_value<<=1;
					if ((range_value & 1) != 0)
						new_range_value|=bit_mask;
					bit_mask<<=1;
				}
				range_mask >>= 1;
				range_value >>= 1;
			}
			command_packet->trigger[0].data_range_max|=range_value;

		}
	}

	usb_source_add(sdi->session, ((struct drv_context *)sdi->driver->context)->sr_ctx, 10000, h4032l_protocol_receive_data, sdi->driver->context);

	// start capturing
	return h4032l_protocol_start(sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* TODO: stop acquisition. */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver hantek_4032l_driver_info = {
	.name = "hantek-4032l",
	.longname = "Hantek 4032l",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_4032l_driver_info);
