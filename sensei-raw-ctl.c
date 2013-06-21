/*
 * sensei-raw-ctl.c: SteelSeries Sensei Raw control utility
 *
 * Everything has been reverse-engineered via Wireshark/usbmon and VirtualBox.
 * Device configuration has been discovered by accident.
 *
 * The code might be a bit long but does very little; most of it is UI.
 *
 * Copyright (c) 2013, Přemysl Janouch <p.janouch@gmail.com>
 * All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#include <getopt.h>
#include <strings.h>
#include <libusb.h>

#include "config.h"

// --- Utilities ---------------------------------------------------------------

/** Search for a device with given vendor and product ID. */
static libusb_device_handle *
find_device (int vendor, int product, int *error)
{
	libusb_device **list;
	libusb_device *found = NULL;
	libusb_device_handle *handle = NULL;
	int err = 0;

	ssize_t cnt = libusb_get_device_list (NULL, &list);
	if (cnt < 0)
		goto out;

	ssize_t i = 0;
	for (i = 0; i < cnt; i++)
	{
		libusb_device *device = list[i];

		struct libusb_device_descriptor desc;
		if (libusb_get_device_descriptor (device, &desc))
			continue;

		if (desc.idVendor == vendor && desc.idProduct == product)
		{
			found = device;
			break;
		}
	}

	if (found)
	{
		err = libusb_open (found, &handle);
		if (err)
			goto out_free;
	}

out_free:
	libusb_free_device_list(list, 1);
out:
	if (error != NULL && err != 0)
		*error = err;
	return handle;
}

/** Search for a device under various product ID's. */
static libusb_device_handle *
find_device_list (int vendor,
	const int *products, size_t n_products, int *error)
{
	int err = 0;
	libusb_device_handle *handle;

	while (n_products--)
	{
		handle = find_device (vendor, *products++, &err);
		if (handle)
			return handle;
		if (err)
			break;
	}

	if (error != NULL && err != 0)
		*error = err;
	return NULL;
}

// --- Device configuration ----------------------------------------------------

#define USB_VENDOR_STEELSERIES  0x1038
#define USB_PRODUCT_STEELSERIES_SENSEI_RAW  0x1369
#define USB_PRODUCT_STEELSERIES_COD_BO2  0x136f

#define USB_GET_REPORT  0x01
#define USB_SET_REPORT  0x09

#define SENSEI_CTL_IFACE  0

#define SENSEI_CPI_MIN  0x01
#define SENSEI_CPI_MAX  0x3f
#define SENSEI_CPI_STEP  90

/** Backlight pulsation. */
enum sensei_pulsation
{
	PULSATION_STEADY = 1,
	PULSATION_SLOW,
	PULSATION_MEDIUM,
	PULSATION_FAST
};

/** Device mode. */
/* Just guessing the names, could be anything */
enum sensei_mode
{
	MODE_LEGACY = 1,
	MODE_NORMAL
};

/** Backlight intensity. */
enum sensei_intensity
{
	INTENSITY_OFF = 1,
	INTENSITY_LOW,
	INTENSITY_MEDIUM,
	INTENSITY_HIGH
};

/** Polling frequency. */
enum sensei_polling
{
	POLLING_1000_HZ = 1,
	POLLING_500_HZ,
	POLLING_250_HZ,
	POLLING_125_HZ
};

/** Overall device configuration. */
struct sensei_config
{
	enum sensei_mode mode;
	int cpi_off;
	int cpi_on;
	enum sensei_pulsation pulsation;
	enum sensei_intensity intensity;
	enum sensei_polling polling;
};

/** Send a command to the mouse via SET_REPORT. */
static int
sensei_send_command (libusb_device_handle *device,
	unsigned char *data, uint16_t length)
{
	int result = libusb_control_transfer (device, LIBUSB_ENDPOINT_OUT
		| LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		USB_SET_REPORT, 0x0200, 0x0000, data, length, 0);
	return result < 0 ? result : 0;
}

/** Set the operating mode of the mouse. */
static int
sensei_set_mode (libusb_device_handle *device,
	enum sensei_mode mode)
{
	unsigned char cmd[32] = { 0x02, 0x00, mode };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Set backlight intensity. */
static int
sensei_set_intensity (libusb_device_handle *device,
	enum sensei_intensity intensity)
{
	unsigned char cmd[32] = { 0x05, 0x01, intensity };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Set pulsation speed. */
static int
sensei_set_pulsation (libusb_device_handle *device,
	enum sensei_pulsation pulsation)
{
	unsigned char cmd[32] = { 0x07, 0x01, pulsation };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Set sensitivity in CPI. */
static int
sensei_set_cpi (libusb_device_handle *device,
	int cpi, bool led_status)
{
	assert (cpi >= SENSEI_CPI_MIN && cpi <= SENSEI_CPI_MAX);
	unsigned char cmd[32] = { 0x03, led_status ? 2 : 1, cpi };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Set the polling frequency. */
static int
sensei_set_polling (libusb_device_handle *device,
	enum sensei_polling polling)
{
	unsigned char cmd[32] = { 0x04, 0x00, polling };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Save the current configuration to ROM. */
static int
sensei_save_to_rom (libusb_device_handle *device)
{
	unsigned char cmd[32] = { 0x09, 0x00, 0x00 };
	return sensei_send_command (device, cmd, sizeof cmd);
}

/** Read device configuration. */
static int
sensei_load_config (libusb_device_handle *device,
	struct sensei_config *config)
{
	unsigned char data[256];

	int result = libusb_control_transfer (device, LIBUSB_ENDPOINT_IN
		| LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
		USB_GET_REPORT, 0x0300, 0x0000, data, sizeof data, 0);
	if (result < 0)
		return result;

	config->intensity = data[102];
	config->pulsation = data[103];
	config->cpi_off   = data[107];
	config->cpi_on    = data[108];
	config->polling   = data[128];
	return 0;
}

// --- Control utility ---------------------------------------------------------

static void
sensei_display_config (const struct sensei_config *config)
{
	printf ("Backlight intensity: ");
	switch (config->intensity)
	{
	case INTENSITY_OFF:     printf ("off\n");     break;
	case INTENSITY_LOW:     printf ("low\n");     break;
	case INTENSITY_MEDIUM:  printf ("medium\n");  break;
	case INTENSITY_HIGH:    printf ("high\n");    break;
	default:                printf ("unknown\n");
	}

	printf ("Backlight pulsation: ");
	switch (config->pulsation)
	{
	case PULSATION_STEADY:  printf ("steady\n");  break;
	case PULSATION_SLOW:    printf ("slow\n");    break;
	case PULSATION_MEDIUM:  printf ("medium\n");  break;
	case PULSATION_FAST:    printf ("fast\n");    break;
	default:                printf ("unknown\n");
	}

	printf ("Speed in CPI (LED is off): %d\n", 90 * config->cpi_off);
	printf ("Speed in CPI (LED is on): %d\n", 90 * config->cpi_on);

	printf ("Polling frequency: ");
	switch (config->polling)
	{
	case POLLING_1000_HZ:  printf ("1000Hz\n");  break;
	case POLLING_500_HZ:   printf ("500Hz\n");   break;
	case POLLING_250_HZ:   printf ("250Hz\n");   break;
	case POLLING_125_HZ:   printf ("125Hz\n");   break;
	default:               printf ("unknown\n");
	}
}

struct options
{
	unsigned show_config   : 1;
	unsigned save_to_rom   : 1;
	unsigned set_pulsation : 1;
	unsigned set_mode      : 1;
	unsigned set_intensity : 1;
	unsigned set_polling   : 1;
	unsigned set_cpi_off   : 1;
	unsigned set_cpi_on    : 1;
};

static void
show_usage (const char *program_name)
{
	printf ("Usage: %s [OPTION]...\n", program_name);
	printf ("Configure SteelSeries Sensei Raw devices.\n\n");
	printf ("  -h, --help      Show this help\n");
	printf ("  --version       Show program version and exit\n");
	printf ("  --show          Show current mouse settings and exit\n");
	printf ("  --mode X        Set the mode of the mouse"
	                         " (can be either 'legacy' or 'normal')\n");
	printf ("  --polling X     Set polling to X Hz (1000, 500, 250, 125)\n");
	printf ("  --cpi-on X      Set CPI with the LED on to X\n");
	printf ("  --cpi-off X     Set CPI with the LED off to X\n");
	printf ("  --pulsation X   Set the pulsation mode"
	                         " (steady, slow, medium, fast)\n");
	printf ("  --intensity X   Set the backlight intensity"
	                         " (off, low, medium, high)\n");
	printf ("  --save          Save the current configuration to ROM\n");
	printf ("\n");
}

static int
encode_cpi (const char *str)
{
	char *end;
	long cpi = strtol (str, &end, 10);
	if (!*str || *end || cpi < 0)
	{
		fprintf (stderr, "Error: invalid CPI value\n");
		exit (EXIT_FAILURE);
	}

	cpi /= SENSEI_CPI_STEP;
	if (cpi < 1)
	{
		fprintf (stderr, "Notice: CPI too low, using %d\n",
			SENSEI_CPI_MIN * SENSEI_CPI_STEP);
		cpi = 1;
	}
	if (cpi > SENSEI_CPI_MAX)
	{
		fprintf (stderr, "Notice: CPI too high, using %d\n",
			SENSEI_CPI_MAX * SENSEI_CPI_STEP);
		cpi = SENSEI_CPI_MAX;
	}
	return cpi;
}

static void
parse_options (int argc, char *argv[],
	struct options *options, struct sensei_config *new_config)
{
	static struct option long_opts[] =
	{
		{ "help",      no_argument,       0, 'h' },
		{ "version",   no_argument,       0, 'V' },
		{ "show",      no_argument,       0, 's' },
		{ "save",      no_argument,       0, 'S' },
		{ "mode",      required_argument, 0, 'm' },
		{ "polling",   required_argument, 0, 'p' },
		{ "cpi-on",    required_argument, 0, 'c' },
		{ "cpi-off",   required_argument, 0, 'C' },
		{ "pulsation", required_argument, 0, 'P' },
		{ "intensity", required_argument, 0, 'i' },
		{ 0,           0,                 0,  0  }
	};

	if (argc == 1)
	{
		show_usage (argv[0]);
		exit (EXIT_FAILURE);
	}

	int c;
	while ((c = getopt_long (argc, argv, "h", long_opts, NULL)) != -1)
	{
	switch (c)
	{
	case 'h':
		show_usage (argv[0]);
		exit (EXIT_SUCCESS);
	case 'V':
		printf (PROJECT_NAME " " PROJECT_VERSION "\n");
		exit (EXIT_SUCCESS);
	case 's':
		options->show_config = true;
		break;
	case 'S':
		options->save_to_rom = true;
		break;
	case 'm':
		if (!strcasecmp (optarg, "legacy"))
			new_config->mode = MODE_LEGACY;
		else if (!strcasecmp (optarg, "normal"))
			new_config->mode = MODE_NORMAL;
		else
		{
			fprintf (stderr, "Error: invalid mode: %s\n", optarg);
			exit (EXIT_FAILURE);
		}
		options->set_mode = true;
		break;
	case 'p':
		if (!strcmp (optarg, "1000"))
			new_config->polling = POLLING_1000_HZ;
		else if (!strcmp (optarg, "500"))
			new_config->polling = POLLING_500_HZ;
		else if (!strcmp (optarg, "250"))
			new_config->polling = POLLING_250_HZ;
		else if (!strcmp (optarg, "125"))
			new_config->polling = POLLING_125_HZ;
		else
		{
			fprintf (stderr, "Error: invalid polling frequency: %s\n", optarg);
			exit (EXIT_FAILURE);
		}
		options->set_polling = true;
		break;
	case 'c':
		new_config->cpi_on = encode_cpi (optarg);
		options->set_cpi_on = true;
		break;
	case 'C':
		new_config->cpi_off = encode_cpi (optarg);
		options->set_cpi_off = true;
		break;
	case 'P':
		if (!strcasecmp (optarg, "steady"))
			new_config->pulsation = PULSATION_STEADY;
		else if (!strcasecmp (optarg, "slow"))
			new_config->pulsation = PULSATION_SLOW;
		else if (!strcasecmp (optarg, "medium"))
			new_config->pulsation = PULSATION_MEDIUM;
		else if (!strcasecmp (optarg, "fast"))
			new_config->pulsation = PULSATION_FAST;
		else
		{
			fprintf (stderr, "Error: invalid backlight pulsation: %s\n", optarg);
			exit (EXIT_FAILURE);
		}
		options->set_pulsation = true;
		break;
	case 'i':
		if (!strcasecmp (optarg, "off"))
			new_config->intensity = INTENSITY_OFF;
		else if (!strcasecmp (optarg, "low"))
			new_config->intensity = INTENSITY_LOW;
		else if (!strcasecmp (optarg, "medium"))
			new_config->intensity = INTENSITY_MEDIUM;
		else if (!strcasecmp (optarg, "high"))
			new_config->intensity = INTENSITY_HIGH;
		else
		{
			fprintf (stderr, "Error: invalid backlight intensity: %s\n", optarg);
			exit (EXIT_FAILURE);
		}
		options->set_intensity = true;
		break;
	case '?':
		exit (EXIT_FAILURE);
	}
	}

	if (optind < argc)
	{
		fprintf (stderr, "Error: extra parameters\n");
		exit (EXIT_FAILURE);
	}
}

static int
apply_options (libusb_device_handle *device,
	struct options *options, struct sensei_config *new_config)
{
	int result;
	if (options->show_config)
	{
		struct sensei_config config;
		if ((result = sensei_load_config (device, &config)))
			return result;
		sensei_display_config (&config);
		return 0;
	}

	if (options->set_mode)
		if ((result = sensei_set_mode (device, new_config->mode)))
			return result;
	if (options->set_polling)
		if ((result = sensei_set_polling (device, new_config->polling)))
			return result;
	if (options->set_intensity)
		if ((result = sensei_set_intensity (device, new_config->intensity)))
			return result;
	if (options->set_pulsation)
		if ((result = sensei_set_pulsation (device, new_config->pulsation)))
			return result;

	if (options->set_cpi_off)
		if ((result = sensei_set_cpi (device, new_config->cpi_off, false)))
			return result;
	if (options->set_cpi_on)
		if ((result = sensei_set_cpi (device, new_config->cpi_on, true)))
			return result;

	if (options->save_to_rom)
		if ((result = sensei_save_to_rom (device)))
			return result;

	return 0;
}

#define ERROR(label, ...)                         \
	do {                                          \
		fprintf (stderr, "Error: " __VA_ARGS__);  \
		status = 1;                               \
		goto label;                               \
	} while (0)

int
main (int argc, char *argv[])
{
	struct options options = { 0 };
	struct sensei_config new_config = { 0 };

	parse_options (argc, argv, &options, &new_config);

	int result, status = 0;

	result = libusb_init (NULL);
	if (result)
		ERROR (error_0, "libusb initialisation failed: %s\n",
			libusb_error_name (result));

	static const int products[] =
	{
		USB_PRODUCT_STEELSERIES_SENSEI_RAW,
		USB_PRODUCT_STEELSERIES_COD_BO2
	};

	result = 0;
	libusb_device_handle *device = find_device_list (USB_VENDOR_STEELSERIES,
		products, sizeof products / sizeof products[0], &result);
	if (!device)
	{
		if (result)
			ERROR (error_1, "couldn't open device: %s\n",
				libusb_error_name (result));
		else
			ERROR (error_1, "no suitable device found\n");
	}

	bool reattach_driver = false;

	result = libusb_kernel_driver_active (device, SENSEI_CTL_IFACE);
	switch (result)
	{
	case 0:
	case LIBUSB_ERROR_NOT_SUPPORTED:
		break;
	case 1:
		reattach_driver = true;
		result = libusb_detach_kernel_driver (device, SENSEI_CTL_IFACE);
		if (result)
			ERROR (error_2, "couldn't detach kernel driver: %s\n",
				libusb_error_name (result));
		break;
	default:
		ERROR (error_2, "coudn't detect kernel driver presence: %s\n",
			libusb_error_name (result));
	}

	result = libusb_claim_interface (device, SENSEI_CTL_IFACE);
	if (result)
		ERROR (error_3, "couldn't claim interface: %s\n",
			libusb_error_name (result));

	result = apply_options (device, &options, &new_config);
	if (result)
		ERROR (error_4, "operation failed: %s\n",
			libusb_error_name (result));

error_4:
	result = libusb_release_interface (device, SENSEI_CTL_IFACE);
	if (result)
		ERROR (error_3, "couldn't release interface: %s\n",
			libusb_error_name (result));

error_3:
	if (reattach_driver)
	{
		result = libusb_attach_kernel_driver (device, SENSEI_CTL_IFACE);
		if (result)
			ERROR (error_2, "couldn't reattach kernel driver: %s\n",
				libusb_error_name (result));
	}

error_2:
	libusb_close (device);
error_1:
	libusb_exit (NULL);
error_0:
	return status;
}

