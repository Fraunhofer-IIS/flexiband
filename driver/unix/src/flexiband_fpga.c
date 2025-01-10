#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "libusb_version_fixes.h"
#ifdef __APPLE__
#include "endian_darwin.h"
#else
#include <endian.h>
#endif

#define array_len(a) (sizeof(a) / sizeof(a[0]))

#define INTERFACE     0
#define ALT_INTERFACE 4
#define ENDPOINT_OUT  0x03
#define VID      0x27ae
#define MOD_CONFIG_LENGTH   31
#define DAC_CONFIG_LENGTH   2

static volatile bool do_exit = false;

/* List of all Product IDs which can be programmed using this application */
static const unsigned int PID[] = {
    0x1016, // TeleOrbit GTEC RFFE USB 3.0
    0x1018, // TeleOrbit GTEC MGSE USB 3.0
    0x1026, // TeleOrbit GTEC RFFE-2 USB 3.0
    0x1028, // TeleOrbit GTEC MGSE-2 USB 3.0
    0x10a2  // Innosense-v1 USB 3.0
};
/* List of all Product IDs which have to be programmed with the alternativ interface instead of jtag. */
static const unsigned int ALTERNATIV_INTERFACE[] = {
    0x1026, // TeleOrbit GTEC RFFE-2 USB 3.0
    0x1028, // TeleOrbit GTEC MGSE-2 USB 3.0
    0x10a1  // Innosense-v1 USB 3.0
};

static int show_fpga_info(libusb_device_handle *dev_handle);
static int upload_fpga_jtag(libusb_device_handle *dev_handle, const char *filename);
static int upload_fpga_alt(libusb_device_handle *dev_handle, const char *filename);
static int send_mod_config(libusb_device_handle *dev_handle, char *mod_config_string, unsigned int mod_num);
static int send_dac_config(libusb_device_handle *dev_handle, char *dac_config_string, unsigned int dac_num);
static unsigned char reverse(unsigned char b);
static void callbackUSBTransferComplete(struct libusb_transfer *xfr);

// This will catch user initiated CTRL+C type events and allow the program to exit
void sighandler(int signum) {
    printf("Exit\n");
    do_exit = true;
}

int main(int argc, char *argv[]) {
    int status = LIBUSB_SUCCESS;
    libusb_context *ctx;
    libusb_device_handle *dev_handle;
    bool is_alt_interface = false;
    char *mod_config_string1, *mod_config_string2, *dac_config_string1, *dac_config_string2;

    if (argc < 2) {
        printf("Usage: %s <filename> [<mod_config1>] [<mod_config2>] [<dac_config1>] [<dac_config2>]\n", argv[0]);
        return 1;
    }
    char *filename = argv[1];

    if (argc >= 3) {
        if (strlen(argv[2]) == MOD_CONFIG_LENGTH * 2) {
            mod_config_string1 = argv[2];
        } else {
            printf("mod_config1 must be 31 bytes long!\n");
            return -1;
        }
    }

    if (argc >= 4) {
        if (strlen(argv[3]) == MOD_CONFIG_LENGTH * 2) {
            mod_config_string2 = argv[3];
        } else if (strlen(argv[3]) == DAC_CONFIG_LENGTH * 2) {
            dac_config_string1 = argv[3];
        } else {
            printf("mod_config2 must be 31 bytes long or dac_config1 must be 2 bytes long!\n");
            return -1;
        }
    }

    if (argc >= 5) {
        if (strlen(argv[4]) == DAC_CONFIG_LENGTH * 2) {
            dac_config_string1 = argv[4];
        } else {
            printf("dac_config1 must be 2 bytes long!\n");
            return -1;
        }
    }

    if (argc == 6) {
        if (strlen(argv[5]) == DAC_CONFIG_LENGTH * 2) {
            dac_config_string2 = argv[5];
        } else {
            printf("dac_config2 must be 2 bytes long!\n");
            return -1;
        }
    }

    // Define signal handler to catch system generated signals
    // (If user hits CTRL+C, this will deal with it.)
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);

    status = libusb_init(&ctx);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    for (unsigned int i = 0; i < array_len(PID); ++i) {
        dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID[i]);
        for (unsigned int j = 0; j < array_len(ALTERNATIV_INTERFACE); j++) {
            if (PID[i] == ALTERNATIV_INTERFACE[j]) is_alt_interface = true;
        }
        if (dev_handle != NULL) goto usb_ok;
    }

    fprintf(stderr, "Error: No devices with VID=0x%04X found\n", VID);
    status = 1;
    goto err_usb;

usb_ok:
    if (libusb_kernel_driver_active(dev_handle, INTERFACE) == 1) {
        printf("Warning: Kernel driver active, detaching kernel driver...");
        status = libusb_detach_kernel_driver(dev_handle, INTERFACE);
        if (status) {
            fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
            goto err_dev;
        }
    }

    status = libusb_claim_interface(dev_handle, INTERFACE);
    if (status) {
        fprintf(stderr, "Claim interface: %s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = show_fpga_info(dev_handle);
    if (argc >= 4 && strlen(argv[3]) == DAC_CONFIG_LENGTH * 2) status = send_dac_config(dev_handle, dac_config_string1, 1);
    if (argc >= 5) status = send_dac_config(dev_handle, dac_config_string1, 1);
    if (argc == 6) status = send_dac_config(dev_handle, dac_config_string2, 2);
    status = is_alt_interface ? upload_fpga_alt(dev_handle, filename) : upload_fpga_jtag(dev_handle, filename);
    if (argc >= 3) status = send_mod_config(dev_handle, mod_config_string1, 1);
    if (argc >= 4 && strlen(argv[3]) == MOD_CONFIG_LENGTH * 2) status = send_mod_config(dev_handle, mod_config_string2, 2);

err_dev:
    libusb_close(dev_handle);
err_usb:
    libusb_exit(ctx);
err_ret:
    return status;
}

static const uint8_t VENDOR_IN = LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR;
static const uint8_t VENDOR_OUT = LIBUSB_ENDPOINT_OUT | LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR;

static int show_fpga_info(libusb_device_handle *dev_handle) {
    int status = 0;

    printf("Read FPGA info...\n");
    uint16_t build_number;
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0001, 0x00, (unsigned char*)&build_number, sizeof(build_number), 1000);
    if (status < 0) {
        fprintf(stderr, "Error: Read FPGA build number\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    uint32_t git_hash;
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0002, 0x00, (unsigned char*)&git_hash, sizeof(git_hash), 1000);
    if (status < 0) {
        fprintf(stderr, "Error: Read FPGA git hash\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    uint32_t timestamp;
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0003, 0x00, (unsigned char*)&timestamp, sizeof(timestamp), 1000);
    if (status < 0) {
        fprintf(stderr, "Error: Read FPGA build time\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    printf("  Build number: %i\n", be16toh(build_number));
    printf("  Git hash: %08x\n", be32toh(git_hash));
    struct tm year_2000 = { 0, 0, 0, 1, 0, 100, 0, 0, 0 };
    time_t build_time_sec = be32toh(timestamp) + mktime(&year_2000);
    printf("  Build time: %s\n", ctime(&build_time_sec));

err_ret:
    return status;
}

static int upload_fpga_jtag(libusb_device_handle *dev_handle, const char *filename) {
    int status = 0;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Open file %s\n%s\n", filename, strerror(errno));
        return 1;
    }

    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    printf("Upload FPGA configuration...\n");
    const unsigned ep0_buf_size = 512; // TODO read this from the USB descriptor
    unsigned page = 0;
    unsigned num_pages = (size - 1) / ep0_buf_size + 1;

    while (!feof(fp) && !do_exit) {
        unsigned char data[ep0_buf_size];

        size_t len = fread(data, sizeof(char), ep0_buf_size, fp);
        status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x00, 0xff00, page, data, len, 1000);
        if (status < 0) {
            printf("\n");
            fprintf(stderr, "Error: Upload FPGA config\n%s\n", libusb_strerror((enum libusb_error)status));
            goto err_ret;
        }
        if (page % 30 == 0) {
            printf("%d%% .. ", page * 100 / num_pages);
            fflush(stdout);
        }
        page++;
    }
    status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x00, 0xff00, 0xffff, NULL, 0, 1000);
    if (status) {
        printf("\n");
        fprintf(stderr, "Error: Upload FPGA config\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    printf("100%%\n");

    // wait until FPGA is loaded
    sleep(1); // TODO Check, if FPGA is ready
    printf("Done\n");

err_ret:
    fclose(fp);
    return status;
}

static int upload_fpga_alt(libusb_device_handle *dev_handle, const char *filename) {
    int status = 0;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Open file %s\n%s\n", filename, strerror(errno));
        return 1;
    }

    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    printf("Upload FPGA configuration...\n");
    status = libusb_set_interface_alt_setting(dev_handle, INTERFACE, ALT_INTERFACE);
    if (status) {
        printf("ERROR: libusb_set_interface_alt_setting\n");
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        fclose(fp);
        return status;
    }

    unsigned char data[size];
    int len = (int)fread(data, sizeof(char), size, fp);

    unsigned char data_reverse[size];
    for (unsigned i = 0; i < size; i++) {
        data_reverse[i] = reverse(data[i]);
    }

    struct libusb_transfer *xfr;
    xfr = libusb_alloc_transfer(0);
    libusb_fill_bulk_transfer(xfr, dev_handle, ENDPOINT_OUT, data_reverse, len, callbackUSBTransferComplete, NULL, 5000);

    if (libusb_submit_transfer(xfr) < 0) {
        printf("ERROR: libusb_submit_transfer\n");
    }

    if (libusb_handle_events(NULL) != LIBUSB_SUCCESS) {
        printf("Error handle event");
    }

    libusb_free_transfer(xfr);
    usleep(100000); // TODO Check, if FPGA is ready
    printf("Done\n");
    fclose(fp);
    return status;
}

static int send_mod_config(libusb_device_handle *dev_handle, char *mod_config_string, unsigned int mod_num) {
    int status = 0;
    char mod_config[MOD_CONFIG_LENGTH], *pos = mod_config_string;
    for (size_t i = 0; i < MOD_CONFIG_LENGTH; ++i) {
        sscanf(pos, "%2hhx", &mod_config[i]);
        pos += 2;
    }
    printf("Sending modulator %d configuration...\n", mod_num);
    for (int num = MOD_CONFIG_LENGTH - 1; num >= 0; num--) {
        status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x0E, num, mod_num, (unsigned char *)(mod_config + num), MOD_CONFIG_LENGTH/MOD_CONFIG_LENGTH, 0);
        printf("%02x", *(mod_config + num));
    }
    printf("\nDone\n");
    return status;
}

static int send_dac_config(libusb_device_handle *dev_handle, char *dac_config_string, unsigned int dac_num) {
    int status = 0;
    uint32_t dac_config = strtoul(dac_config_string, NULL, 16);
    printf("Sending DAC %d configuration...\n%s\n", dac_num, dac_config_string);
    sleep(1); // Wait after sending mod_config and before sending dac_config to avoid transfer errors
    status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x0D, 0x01, dac_num, (uint8_t*)&dac_config, sizeof(uint32_t), 0);
    printf("Done\n");
    return status;
}

static unsigned char reverse(unsigned char b) {
   b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
   b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
   b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
   return b;
}

static void callbackUSBTransferComplete(struct libusb_transfer *xfr) {
    switch(xfr->status) {
    case LIBUSB_TRANSFER_COMPLETED:
        printf("Transfer Completed\n");
        break;
    default:
        printf("Transfer Error\n");
    }
}
