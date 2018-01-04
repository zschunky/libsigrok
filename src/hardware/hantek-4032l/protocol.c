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

SR_PRIV int h4032l_protocol_receive_data(int fd, int revents, void *cb_data)
{
	(void)fd;
	(void)revents;
	(void)cb_data;

	struct timeval tv;
	struct drv_context *drvc;


	drvc = (struct drv_context *)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

void LIBUSB_CALL h4032l_protocol_usb_callback(struct libusb_transfer *transfer) 
{
	sr_dbg("h4032l_protocol_usb_callback");
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
		const struct sr_dev_inst *sdi=transfer->user_data;
		struct h4032l_protocol_device_context *device_context=sdi->priv;
		struct sr_usb_dev_inst* usb=sdi->conn;

		gboolean command=FALSE;
		uint32_t max_samples=512/sizeof(uint32_t);
		uint32_t *buffer=(uint32_t*)transfer->buffer;
		switch (device_context->status) {
			case H4032L_PROTOCOL_STATUS_IDLE:
				sr_err("usb callback called in idle");
				break;
			case H4032L_PROTOCOL_STATUS_COMMAND_CONFIGURE:
				// select status request as next
				command=TRUE;
				device_context->command_packet.command=H4032L_PROTOCOL_COMMAND_STATUS;
				device_context->status=H4032L_PROTOCOL_STATUS_COMMAND_STATUS;
				break;
			case H4032L_PROTOCOL_STATUS_COMMAND_STATUS:
				// select status request as next
				device_context->status=H4032L_PROTOCOL_STATUS_RESPONSE_STATUS;
				break;
			case H4032L_PROTOCOL_STATUS_RESPONSE_STATUS: {
					// check magic and if status is complete, then select First Transfer as next
					struct h4032l_protocol_status_packet *status=(struct h4032l_protocol_status_packet *)transfer->buffer;
					if (status->magic != H4032L_PROTOCOL_STATUS_PACKET_MAGIC) {
						device_context->status=H4032L_PROTOCOL_STATUS_COMMAND_STATUS;
						device_context->command_packet.command=H4032L_PROTOCOL_COMMAND_STATUS;
						command=TRUE;
					}
					else if (status->status == 2) {
						device_context->status=H4032L_PROTOCOL_STATUS_RESPONSE_STATUS_CONTINUE;
					}
					else {
						device_context->status=H4032L_PROTOCOL_STATUS_RESPONSE_STATUS_RETRY;
					}
					break;
				}
			case H4032L_PROTOCOL_STATUS_RESPONSE_STATUS_RETRY:
				device_context->status=H4032L_PROTOCOL_STATUS_COMMAND_STATUS;
				device_context->command_packet.command=H4032L_PROTOCOL_COMMAND_STATUS;
				command=TRUE;
				break;
			case H4032L_PROTOCOL_STATUS_RESPONSE_STATUS_CONTINUE:
				device_context->status=H4032L_PROTOCOL_STATUS_COMMAND_GET;
				device_context->command_packet.command=H4032L_PROTOCOL_COMMAND_GET;
				command=TRUE;
				break;
			case H4032L_PROTOCOL_STATUS_COMMAND_GET:
				device_context->status=H4032L_PROTOCOL_STATUS_FIRST_TRANSFER;
				break;
			case H4032L_PROTOCOL_STATUS_FIRST_TRANSFER: 
				if (buffer[0] != H4032L_PROTOCOL_START_PACKET_MAGIC) {
					sr_err("mismatch magic number of start poll");
					device_context->status=H4032L_PROTOCOL_STATUS_IDLE;
					break;
				}
				device_context->status=H4032L_PROTOCOL_STATUS_TRANSFER;
				max_samples--;
				buffer++;
			case H4032L_PROTOCOL_STATUS_TRANSFER: {
					uint32_t number_samples=(device_context->remaining_samples < max_samples)?device_context->remaining_samples:max_samples;
					device_context->remaining_samples-=number_samples;
					struct sr_datafeed_packet packet;
					struct sr_datafeed_logic logic;
					packet.type = SR_DF_LOGIC;
					packet.payload = &logic;
					logic.length = number_samples*sizeof(uint32_t);
					logic.unitsize = sizeof(uint32_t);
					logic.data = buffer;
					sr_session_send(sdi, &packet);
					sr_dbg("remaining:%d %08X %08X", device_context->remaining_samples, buffer[0], buffer[1]);
					if (device_context->remaining_samples==0) {
						packet.type = SR_DF_END;
						sr_session_send(sdi, &packet);
						usb_source_remove(sdi->session, ((struct drv_context *)sdi->driver->context)->sr_ctx);
						device_context->status=H4032L_PROTOCOL_STATUS_IDLE;
						if (buffer[number_samples] != H4032L_PROTOCOL_END_PACKET_MAGIC)
							sr_err("mismatch magic number of end poll");
					}
					break;
				}
		}

		if (device_context->status!=H4032L_PROTOCOL_STATUS_IDLE) {
			if (command) {
				// setup new usb command packet, reuse transfer object
				sr_dbg("new command:%d", device_context->status);
				libusb_fill_bulk_transfer(transfer, usb->devhdl, 2 | LIBUSB_ENDPOINT_OUT, (unsigned char *)&device_context->command_packet, sizeof(struct h4032l_protocol_command_packet), h4032l_protocol_usb_callback, (void*)sdi, H4032L_PROTOCOL_USB_TIMEOUT);
			}
			else {
				// setup new usb poll packet, reuse transfer object
				sr_dbg("poll:%d", device_context->status);
				libusb_fill_bulk_transfer(transfer, usb->devhdl, 6 | LIBUSB_ENDPOINT_IN, device_context->buffer, ARRAY_SIZE(device_context->buffer), h4032l_protocol_usb_callback, (void*)sdi, H4032L_PROTOCOL_USB_TIMEOUT);
			}
			int ret;
			// send prepared usb packet
			if ((ret = libusb_submit_transfer(transfer)) != 0) {
				sr_err("Failed to submit transfer: %s.", libusb_error_name(ret));
				device_context->status=H4032L_PROTOCOL_STATUS_IDLE;
			}
		}
		else {
			sr_dbg("now idle");
		}
		if (device_context->status==H4032L_PROTOCOL_STATUS_IDLE)
			libusb_free_transfer(transfer);
		}
	else {
		sr_err("h4032l_protocol_usb_callback fail:%d", transfer->status);
	}
	sr_dbg("h4032l_protocol_usb_callback done");
}

uint16_t h4032l_protocol_voltage2pwm(double voltage) 
{
	/* word PwmA - channel A Vref PWM value, pseudocode:
		 -6V < ThresholdVoltage < +6V
		 Vref = 1.8-ThresholdVoltage
		 if Vref>10.0
			 Vref = 10.0
		 if Vref<-5.0
			 Vref = -5.0
		 pwm = ToInt((Vref + 5.0) / 15.0 * 4096.0)
			 if pwm>4095
			 pwm = 4095*/
	voltage=1.8 - voltage;
	if (voltage > 10.0)
		voltage=10.0;
	else if (voltage < -5.0)
		voltage=-5.0;

	return (uint16_t)((voltage + 5.0)*(4096.0/15.0));
}

SR_PRIV int h4032l_protocol_start(const struct sr_dev_inst *sdi) 
{
	struct h4032l_protocol_device_context *device_context=sdi->priv;
	struct sr_usb_dev_inst* usb=sdi->conn;

	// send configure command to arm the logic analyzer
	device_context->command_packet.command=H4032L_PROTOCOL_COMMAND_CONFIGURE;
	device_context->status=H4032L_PROTOCOL_STATUS_COMMAND_CONFIGURE;
	device_context->remaining_samples=device_context->command_packet.sample_size;
	struct libusb_transfer *transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, usb->devhdl, 2 | LIBUSB_ENDPOINT_OUT, (unsigned char *)&device_context->command_packet, sizeof(struct h4032l_protocol_command_packet), h4032l_protocol_usb_callback, (void*)sdi, H4032L_PROTOCOL_USB_TIMEOUT);
	int ret;
	if ((ret = libusb_submit_transfer(transfer)) != 0) {
		sr_err("Failed to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		return SR_ERR;
	}

	std_session_send_df_header(sdi);
	return SR_OK;
}

SR_PRIV int h4032l_protocol_dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int ret, i, device_count;
	char connection_id[64];

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.", libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0;i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != H4032L_PROTOCOL_USB_VENDOR ||
			des.idProduct != H4032L_PROTOCOL_USB_PRODUCT)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
			(sdi->status == SR_ST_INACTIVE)) {
			/*
			 * Check device by its physical USB bus/port address.
			 */
			usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.", libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		ret = SR_OK;
		break;
	}

	libusb_free_device_list(devlist, 1);
	return ret;
}
