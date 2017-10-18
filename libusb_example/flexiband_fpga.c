#include <endian.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#include "libusb_version_fixes.h"

#define INTERFACE     0
#define VID      0x27ae
#define PID      0x1016

static volatile bool do_exit = false;

static int show_fpga_info(libusb_device_handle *dev_handle);
static int upload_fpga(libusb_device_handle *dev_handle, const char *filename);

// This will catch user initiated CTRL+C type events and allow the program to exit
void sighandler(int signum) {
    printf("Exit\n");
    do_exit = true;
}

int main(int argc, char *argv[]) {
    int status = LIBUSB_SUCCESS;
    int fd = -1;
    libusb_context *ctx;
    libusb_device_handle* dev_handle;

    if (argc < 2) {
        printf("Usage: %s [<filename>]      Upload a bit file to the FPGA\n", argv[0]);
        printf("   or: %s -i                Show information about the current design\n", argv[0]);
        return 1;
    }
    char *option = argv[1];

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

    dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID);
    if (dev_handle == NULL) {
        fprintf(stderr, "Error: No device with VID=0x%04X, PID=0x%04X\n", VID, PID);
        status = 1;
        goto err_usb;
    }

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
    if (strncmp(option, "-i", 2) != 0) {
        status = upload_fpga(dev_handle, option);
        status = show_fpga_info(dev_handle);
    }

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
    struct tm year_2000 = { 0, 0, 0, 1, 1, 100, 0, 0, 0 };
    time_t build_time_sec = be32toh(timestamp) + mktime(&year_2000);
    printf("  Build time: %s\n", ctime(&build_time_sec));

err_ret:
    return status;
}

static int upload_fpga(libusb_device_handle *dev_handle, const char *filename) {
    int status = 0;
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Open file %s\n%s\n", filename, strerror(errno));
        return 1;
    }

    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    const unsigned ep0_buf_size = 512; // TODO read this from the USB descriptor
    unsigned page = 0;
    unsigned num_pages = (size - 1) / ep0_buf_size + 1;

    printf("Upload FPGA configuration...\n");
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

