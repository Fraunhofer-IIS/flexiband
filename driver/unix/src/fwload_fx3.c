/***********************************************************************************
 * Program Name.      : 0_fwload.c                                                 *
 * Author             : V. Radhakrishnan ( rk@atr-labs.com )                       *
 * License.           : GPL Ver 2.0                                                *
 * Copyright.         : Cypress Semiconductors Inc. / ATR-LABS                     *
 * Date written.      : March 27, 2012                                             *
 * Modification Notes :                                                            *
 * This program is a CLI program to download a firmware file ( .hex format )       *
 * into the chip using bRequest 0xA0		                                       *
 ***********************************************************************************/

#include "cyusb.h"
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/********** Cut and paste the following & modify as required  **********/
static const char         *program_name;
static const char *const   short_options  = "hvf:d:b:";
static const struct option long_options[] = {{"help", 0, NULL, 'h'}, {"version", 0, NULL, 'v'},
                                             {"file", 1, NULL, 'f'}, {"device", 1, NULL, 'd'},
                                             {"bus", 1, NULL, 'b'},  {NULL, 0, NULL, 0}};

static int next_option;

static void print_usage(FILE *stream, int exit_code)
{
    fprintf(stream, "Usage: %s [options] [filename]\n", program_name);
    fprintf(stream, "  -h  --help      Display this usage information.\n"
                    "  -v  --version   Print version.\n"
                    "  -f  --file      firmware file name (.hex) format\n"
                    "  -b  --bus       Bus number of target device.\n"
                    "  -d  --dev       Device number of target device.\n");
    exit(exit_code);
}
/***********************************************************************/

static char *filename = NULL;
static int   busnum   = -1;
static int   devnum   = -1;

int main(int argc, char **argv)
{
    int            r;
    cyusb_handle  *h  = NULL;
    FILE          *fp = NULL;
    struct stat    statbuf;
    unsigned char  reset = 0;
    char           buf[256];
    int            i;
    int            count = 0;
    int            fd;
    unsigned short address = 0;
    char           tbuf1[3];
    char           tbuf2[5];
    char           tbuf3[3];
    unsigned char  num_bytes = 0;
    unsigned char *dbuf;
    int            linecount = 0;

    program_name = argv[0];

    while ((next_option = getopt_long(argc, argv, short_options, long_options, NULL)) != -1) {
        switch (next_option) {
        case 'h':  // -h or --help
            print_usage(stdout, EXIT_SUCCESS);
            // does not return
        case 'v':  // -v or --version
            printf("%s (Ver 1.0)\n", program_name);
            printf("Copyright (C) 2012 Cypress Semiconductors Inc. / ATR-LABS\n");
            exit(0);
        case 'f':  // -f or --file
            filename = optarg;
            break;
        case 'b':  // -b or -bus
            busnum = atoi(optarg);
            break;
        case 'd':  // -d or -device
            devnum = atoi(optarg);
            break;
        case '?':  // Invalid option
            print_usage(stdout, EXIT_FAILURE);
            // does not return
        default:  // Something else, unexpected
            abort();
        }
    }

    if (optind < argc) { filename = argv[optind]; }

    if (filename == NULL) {
        fprintf(stderr, "Please provide full path to firmware image file\n");
        print_usage(stdout, EXIT_FAILURE);
    }

    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "File not found!\n");
        print_usage(stdout, EXIT_FAILURE);
    }
    fclose(fp);

    r = cyusb_open();
    if (r < 0) {
        printf("Error opening library\n");
        return -1;
    } else if (r == 0) {
        printf("No device found\n");
        return 0;
    } else if (r == 1) {
        // found only one device (legacy behaviour)
        h = cyusb_gethandle(0);
        r = stat(filename, &statbuf);
        printf("File size = %d\n", (int)statbuf.st_size);
        r = cyusb_download_fx3(h, filename);

    } else if (busnum != -1 & devnum != -1) {
        // search for specific device
        for (i = 0; i < r; i++) {
            h = cyusb_gethandle(i);
            if (busnum == cyusb_get_busnumber(h) && devnum == cyusb_get_devaddr(h)) {
                r = stat(filename, &statbuf);
                printf("File size = %d\n", (int)statbuf.st_size);
                r = cyusb_download_fx3(h, filename);
                break;
            }
        }
    } else {
        // prompt user to choose device
        printf("Enumerating %d devices...\n", r);
        for (i = 0; i < r; i++) {
            h                   = cyusb_gethandle(i);
            unsigned int pid    = cyusb_getproduct(h);
            unsigned int vid    = cyusb_getvendor(h);
            unsigned int busnum = cyusb_get_busnumber(h);
            unsigned int devnum = cyusb_get_devaddr(h);
            printf("[%i] vid = %x, pid = %x @ bus %d dev %d\n", i, vid, pid, busnum, devnum);
        }
        printf("Choose device: ");
        scanf("%d", &i);
        h = cyusb_gethandle(i);
        r = stat(filename, &statbuf);
        printf("\nFile size = %d\n", (int)statbuf.st_size);
        r = cyusb_download_fx3(h, filename);
    }

    cyusb_close();
    close(fd);
    return 0;
}
