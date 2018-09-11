// Copyright 2011 Juri Glass, Mathias Runge, Nadim El Sayed
// DAI-Labor, TU-Berlin
//
// This file is part of libSML.
// Thanks to Thomas Binder and Axel (tuxedo) for providing code how to
// print OBIS data (see transport_receiver()).
// https://community.openhab.org/t/using-a-power-meter-sml-with-openhab/21923
//
// libSML is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// libSML is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with libSML.  If not, see <http://www.gnu.org/licenses/>.

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <sys/ioctl.h>
#include <assert.h>

#include <sml/sml_file.h>
#include <sml/sml_transport.h>
#include <sml/sml_value.h>

#include <mosquitto.h>

#include <unit.h>

int serial_port_open(const char* device) {
	int bits;
	struct termios config;
	memset(&config, 0, sizeof(config));

	if (!strcmp(device, "-"))
		return 0; // read stdin when "-" is given for the device

#ifdef O_NONBLOCK
	int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
#else
	int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
#endif
	if (fd < 0) {
		fprintf(stderr, "error: open(%s): %s\n", device, strerror(errno));
		return -1;
	}

	// set RTS
	ioctl(fd, TIOCMGET, &bits);
	bits |= TIOCM_RTS;
	ioctl(fd, TIOCMSET, &bits);

	tcgetattr(fd, &config);

	// set 8-N-1
	config.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR
			| ICRNL | IXON);
	config.c_oflag &= ~OPOST;
	config.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	config.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB);
	config.c_cflag |= CS8;

	// set speed to 9600 baud
	cfsetispeed(&config, B9600);
	cfsetospeed(&config, B9600);

	tcsetattr(fd, TCSANOW, &config);
	return fd;
}

const char* HOST = "localhost"; //"mqtt-broker.ionet";
const int   PORT = 1883;

typedef struct {
	double value;
	char* unit;
} power_data_t;


// printf("%d-%d:%d.%d.%d*%d#%.1f#",
// 	entry->obj_name->str[0], entry->obj_name->str[1],
// 	enrty->obj_name->str[2], entry->obj_name->str[3],
// 	entry->obj_name->str[4], entry->obj_name->str[5], value);

// example data
// 129-129:199.130.3*255#EMH#
// 1-0:0.0.9*255#06 45 4d 48 01 04 c5 6d a0 ba #
// 1-0:1.8.0*255#12374148.4#Wh
// 1-0:1.8.1*255#12374148.4#Wh
// 1-0:16.7.0*255#163.5#W

bool is_identifier(sml_list *entry, const char* id) {
	char buffer[10] = {0};
	snprintf(buffer, 10, "%d.%d.%d", entry->obj_name->str[2], entry->obj_name->str[3], entry->obj_name->str[4]);
	return strcmp(buffer, id) == 0;
}

bool is_current_data(sml_list *entry) {
	return is_identifier(entry, "16.7.0");
}

bool is_total_data(sml_list *entry) {
	return is_identifier(entry, "1.8.0");
}

void parse_power_data(sml_file *file, power_data_t *current, power_data_t *total) {

	for (int i = 0; i < file->messages_len; i++) {
		sml_message *message = file->messages[i];

		if (*message->message_body->tag != SML_MESSAGE_GET_LIST_RESPONSE) {
			continue;
		}

		sml_get_list_response *body = (sml_get_list_response *) message->message_body->data;

		for (sml_list* entry = body->val_list; entry != NULL; entry = entry->next) {

			if (!entry->value) { // do not crash on null value
				fprintf(stderr, "Error in data stream. entry->value should not be NULL. Skipping this.\n");
				continue;
			}

			if (((entry->value->type & SML_TYPE_FIELD) != SML_TYPE_INTEGER) &&
					((entry->value->type & SML_TYPE_FIELD) != SML_TYPE_UNSIGNED)) {
				continue;
			}

			power_data_t *used_data =  NULL;

			if(is_total_data(entry)) {
				used_data = total;
			}
			else if(is_current_data(entry)) {
				used_data = current;
			}
			else {
				used_data = NULL;
				continue;
			}

			const double value = sml_value_to_double(entry->value);
			const int scaler = (entry->scaler) ? *entry->scaler : 0;
			used_data->value = value * pow(10, scaler);

			if (entry->unit) {
				used_data->unit = dlms_get_unit((unsigned char) *entry->unit);
			}
		}
	}
}

void write_value_to_buffer(char* buffer, size_t max_length, power_data_t *data) {
	assert(data);
	if(data->unit) {
		snprintf(buffer, max_length, "%.1f %s", data->value, data->unit);
	}
	else {
		snprintf(buffer, max_length, "%.1f", data->value);
	}
}

// void on_log(struct mosquitto *mosq, void *userdata, int level, const char *str) {
// 	printf("%i:%s\n", level, str);
// }


void transport_receiver(unsigned char *buffer, size_t buffer_len) {
	
	// connect to the server
    struct mosquitto *mosq = mosquitto_new("power-id", true, NULL);

	//mosquitto_log_callback_set(mosq, on_log);

    mosquitto_connect(mosq, HOST, PORT, 60);
	mosquitto_loop_start(mosq);

	// read the data from the usb receiver
	sml_file *file = sml_file_parse(buffer + 8, buffer_len - 16);
	power_data_t current = {-1, NULL};
	power_data_t total = {-1, NULL};;
	
	
	parse_power_data(file, &current, &total);

	// send data to the server
	char result[32] = {0};

	if(current.value >= 0) {
		write_value_to_buffer(result, 32, &current);
		printf("%s\n", result);
    	mosquitto_publish(mosq, NULL, "resources/power/current", strlen(result), result, 0, false);
	}

	if(total.value >= 0) {
		write_value_to_buffer(result, 32, &total);
		printf("%s\n", result);
    	mosquitto_publish(mosq, NULL, "resources/power/total", strlen(result), result, 0, false);
	}

	// disconnect

    mosquitto_disconnect(mosq);
	mosquitto_loop_stop(mosq, false);
	mosquitto_destroy(mosq);

	sml_file_free(file);
}

int main(int argc, char *argv[]) {
	// this example assumes that a EDL21 meter sending SML messages via a
	// serial device. Adjust as needed.
	if (argc != 2) {
		printf("Usage: %s <device>\n", argv[0]);
		printf("device - serial device of connected power meter e.g. /dev/cu.usbserial, or - for stdin\n");
		exit(1); // exit here
	}

	// open serial port
	int fd = serial_port_open(argv[1]);
	if (fd < 0) {
		printf("Error: failed to open device (%s)\n", argv[1]);
		exit(3);
	}

	mosquitto_lib_init();

	// listen on the serial device, this call is blocking.
	sml_transport_listen(fd, &transport_receiver);
	close(fd);

	mosquitto_lib_cleanup();

	return 0;
}
