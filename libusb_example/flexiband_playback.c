#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libusb-1.0/libusb.h>

#include "libusb_version_fixes.h"

#define INTERFACE     0
#define ALT_INTERFACE 3

#define VID      0x27ae
#define PID      0x1018
#define ENDPOINT 0x03
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
    struct stat sb;
    libusb_context *ctx;
    libusb_device_handle* dev_handle;

    if (argc < 2) {
        printf("Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    char *filename = argv[1];

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
            fprintf(stderr, "Detach: %s\n", libusb_strerror((enum libusb_error)status));
            goto err_dev;
        }
    }

    status = libusb_reset_device(dev_handle);
    if (status) {
        fprintf(stderr, "Reset: %s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = libusb_claim_interface(dev_handle, INTERFACE);
    if (status) {
        fprintf(stderr, "Claim interface: %s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }

    status = libusb_set_interface_alt_setting(dev_handle, INTERFACE, ALT_INTERFACE);
    if (status) {
        fprintf(stderr, "Set alternate interface: %s\n", libusb_strerror((enum libusb_error)status));
        goto err_intf;
    }

    fd = open(filename, O_RDONLY | O_NOATIME /* | O_DIRECT */);
    if (fd < 0) {
        fprintf(stderr, "Failed to open %s\n%s\n", filename, strerror(errno));
        status = 1;
        goto err_intf;
    }
    fstat(fd, &sb);

    printf("Playback %s...\n", filename);
    status = transfer_data(ctx, dev_handle, fd, sb.st_size);
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

struct statistics {
    int64_t min;
    int64_t max;
    int64_t sum;
    int64_t num;
};

struct transfer_ctrl {
    uint64_t len;
    uint64_t transferred;
    unsigned pending;
    int fd;
    int status;
    struct statistics usb;
    struct statistics disk;
};

static int64_t now_usec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static void init_statistics(struct statistics *stat) {
    stat->min = INT64_MAX;
    stat->max = 0;
    stat->sum = 0;
    stat->num = 0;
}

static void update_statistics(struct statistics *stat, int64_t duration) {
    stat->min = duration < stat->min ? duration : stat->min;
    stat->max = duration > stat->max ? duration : stat->max;
    stat->sum += duration;
    stat->num++;
}

static void transfer_callback(struct libusb_transfer *transfer) {
    static int64_t start_usb = -1;
    struct transfer_ctrl *ctrl = (struct transfer_ctrl*)transfer->user_data;
    if (ctrl == NULL) {
        ctrl->status = -1;
        return;
    }

    if (start_usb > 0) {
        int64_t duration = now_usec() - start_usb;
        update_statistics(&ctrl->usb, duration);
    }

    ctrl->pending--;
    if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        fprintf(stderr, "Error: Transfer not completed, status %i\n", transfer->status);
        ctrl->status = transfer->status;
        return;
    }
    ctrl->transferred += transfer->length;

    if (ctrl->transferred < ctrl->len && !do_exit) {
        long start = now_usec();
        read(ctrl->fd, transfer->buffer, XFER_LEN);
        long duration = now_usec() - start;
        update_statistics(&ctrl->disk, duration);

        ctrl->status = libusb_submit_transfer(transfer);
        if (ctrl->status) {
            fprintf(stderr, "Error: Submit transfer\n%s\n", libusb_strerror((enum libusb_error)ctrl->status));
            return;
        }
        ctrl->pending++;
    }

    start_usb = now_usec();
}

static int transfer_data(libusb_context *ctx, libusb_device_handle *dev_handle, int fd, uint64_t len) {
    int status = 0;
    bool is_terminal = isatty(fileno(stdout));
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
    init_statistics(&ctrl.disk);
    init_statistics(&ctrl.usb);

    for (unsigned i = 0; i < QUEUE_SIZE; i++) {
        transfers[i] = libusb_alloc_transfer(0);
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
        libusb_fill_bulk_transfer(transfers[i], dev_handle, ENDPOINT, buffer, XFER_LEN, transfer_callback, &ctrl, TIMEOUT_MS);
   }

    // send start command 
    status = libusb_control_transfer(dev_handle, LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, 0x00, 0x00, 0x00, NULL, 0, 1000);
    if (status) {
        fprintf(stderr, "Error: Start command\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_alloc;
    }

    // start all transfers
    for (unsigned i = 0; i < QUEUE_SIZE; i++) {
        read(ctrl.fd, transfers[i]->buffer, XFER_LEN);
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
            if (status != LIBUSB_ERROR_INTERRUPTED) {
                fprintf(stderr, "Handle events: %s\n", libusb_strerror((enum libusb_error)status));
            }
            goto err_stop;
        }
        time_t now = time(NULL);
        if (difftime(now, last_time) > 1.0) {
            double dt = difftime(now, last_time);
            if (is_terminal) printf("\33[2K\r");
            printf("Throughput: %f MB/s, %lu MB / %lu MB  USB: min %lu us, max %lu us, avg %lu us  DISK: min %lu us, max %lu us, avg %lu us",
                   (double)(ctrl.transferred - last_bytes) / dt / (1000*1000), ctrl.transferred / (1000*1000), ctrl.len / (1000*1000),
                   ctrl.usb.min, ctrl.usb.max, ctrl.usb.sum / ctrl.usb.num, ctrl.disk.min, ctrl.disk.max, ctrl.disk.sum / ctrl.disk.num);
            if (is_terminal) fflush(stdout); else printf("\n");
            init_statistics(&ctrl.disk);
            init_statistics(&ctrl.usb);
            last_time = now;
            last_bytes = ctrl.transferred;
        }
    }
    time_t now = time(NULL);
    if (is_terminal) printf("\33[2K\r");
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
    for (unsigned i = 0; i < QUEUE_SIZE; i++) {
        free(transfers[i]->buffer);
        libusb_free_transfer(transfers[i]);
    }

    return status;
}

