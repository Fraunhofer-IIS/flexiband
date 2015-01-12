#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libusb-1.0/libusb.h>

#define INTERFACE     0
#define ALT_INTERFACE 1

#define VID      0x27ae
#define PID      0x1016
#define ENDPOINT 0x83
#define PKG_LEN (16 * 1024)
#define NUM_PKG 32
#define XFER_LEN (NUM_PKG * PKG_LEN)
#define TIMEOUT_MS 1000
#define QUEUE_SIZE 2

static bool do_exit = false;

static int transfer_data(libusb_context *ctx, libusb_device_handle *dev_handle, int fd, uint64_t len);

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

    if (argc < 3) {
        printf("Usage: %s <bytes to transfer> <filename>\n", argv[0]);
        return 1;
    }
    uint64_t len = strtoull(argv[1], NULL, 0);
    char *filename = argv[2];

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

    status = libusb_reset_device(dev_handle);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = libusb_claim_interface(dev_handle, INTERFACE);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = libusb_set_interface_alt_setting(dev_handle, INTERFACE, ALT_INTERFACE);
    if (status) {
        fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
        goto err_intf;
    }

    // TODO Here we should reset the endpoint to clear any pending data from older transfers.
    //      Currently not possible with libusb, see http://www.libusb.org/ticket/50

    fd = open(filename, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n%s\n", filename, strerror(errno));
        status = 1;
        goto err_intf;
    }

    status = transfer_data(ctx, dev_handle, fd, len);
    close(fd);

err_intf:
    libusb_release_interface(dev_handle, INTERFACE);
err_dev:
    libusb_close(dev_handle);
err_usb:
    libusb_exit(ctx);
err_ret:
    return status;
}

struct transfer_ctrl {
    uint64_t len;
    uint64_t transferred;
    unsigned pending;
    int fd;
    int status;
};

static void transfer_callback(struct libusb_transfer *transfer) {
    struct transfer_ctrl *ctrl = (struct transfer_ctrl*)transfer->user_data;

    // write current transfer to file
    ctrl->pending--;
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        ctrl->status = transfer->status;
        return;
    }
    for (unsigned i = 0; i < transfer->num_iso_packets; i++) {
        if (transfer->iso_packet_desc[i].status != LIBUSB_TRANSFER_COMPLETED) continue;
        write(ctrl->fd, libusb_get_iso_packet_buffer_simple(transfer, i), transfer->iso_packet_desc[i].actual_length);
        ctrl->transferred += transfer->iso_packet_desc[i].actual_length;
    }

    if (ctrl->transferred < ctrl->len || do_exit) {
        ctrl->status = libusb_submit_transfer(transfer);
        if (ctrl->status) {
            fprintf(stderr, "Error: Submit transfer\n%s\n", libusb_strerror((enum libusb_error)ctrl->status));
        }
        ctrl->pending++;
    }
}

static int transfer_data(libusb_context *ctx, libusb_device_handle *dev_handle, int fd, uint64_t len) {
    int status = 0;
    time_t start, last_time;
    uint64_t last_bytes;
    struct transfer_ctrl ctrl;
    struct libusb_transfer *transfers[QUEUE_SIZE];
    memset(transfers, 0, sizeof(transfers));
    ctrl.len = len;
    ctrl.transferred = 0;
    ctrl.pending = 0;
    ctrl.fd = fd;
    ctrl.status = 0;

    for (unsigned i = 0; i < QUEUE_SIZE; i++) {
        transfers[i] = libusb_alloc_transfer(NUM_PKG);
        if (transfers[i] == NULL) {
            fprintf(stderr, "Error: allocating transfer\n");
            status = 1;
            goto err_alloc;
        }
        unsigned char *buffer = (unsigned char*)malloc(XFER_LEN);
        if (buffer == NULL) {
            fprintf(stderr, "Error: allocating buffer\n");
            status = 1;
            goto err_alloc;
        }
        libusb_fill_iso_transfer(transfers[i], dev_handle, ENDPOINT, buffer, XFER_LEN, NUM_PKG, transfer_callback, &ctrl, TIMEOUT_MS);
        libusb_set_iso_packet_lengths(transfers[i], PKG_LEN);
   }

    // send start command 
    status = libusb_control_transfer(dev_handle, LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, 0x00, 0x00, 0x00, NULL, 0, 1000);
    if (status) {
        fprintf(stderr, "Error: Start command\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_alloc;
    }

    // start all transfers but the last one
    for (unsigned i = 0; i < QUEUE_SIZE; i++) {
        status = libusb_submit_transfer(transfers[i]);
        if (status) {
            fprintf(stderr, "Error: Submit transfer\n%s\n", libusb_strerror((enum libusb_error)status));
            goto err_stop;
        }
        ctrl.pending++;
    }

    start = time(NULL);
    last_time = start;
    last_bytes = 0;
    while (ctrl.transferred < ctrl.len && ctrl.status == 0 && !do_exit) {
        status = libusb_handle_events_completed(ctx, NULL);
        if (status) {
            fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
            goto err_stop;
        }
        time_t now = time(NULL);
        if (difftime(now, last_time) > 1.0) {
            double dt = difftime(now, last_time);
            printf("Throughput: %f MB/s\n", (double)(ctrl.transferred - last_bytes) / dt / (1000*1000));
            last_time = now;
            last_bytes = ctrl.transferred;
        }
    }
    time_t now = time(NULL);
    printf("Throughput: %f MB/s\n", (double)ctrl.transferred / difftime(now, start) / (1000*1000));
 
err_stop:
    printf("\n");

    // wait for pending transfers
    while (ctrl.pending > 0) { 
        status = libusb_handle_events(ctx);
        if (status)
            fprintf(stderr, "Error: Wait for cancel\n%s\n", libusb_strerror((enum libusb_error)status));
    }

    // send stop command 
    status = libusb_control_transfer(dev_handle, LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, 0x00, 0x01, 0x00, NULL, 0, 1000);
    if (status) {
        fprintf(stderr, "Error: Stop command\n%s\n", libusb_strerror((enum libusb_error)status));
    }
err_alloc:
    for (unsigned i = 0; i < QUEUE_SIZE; i++)
        libusb_free_transfer(transfers[i]);
    return status;
}

