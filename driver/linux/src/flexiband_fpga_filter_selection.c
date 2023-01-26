/*
flexiband_fpga_filter_selection.c
This program does the filter coefficient upload on flexiband_2 systems FIR- and notch filters.
The filter coefficients are read from an input file.
The upload uses the libusb control transfer protocol.
The specific filter for the coefficients is specified via CLI Arguments.
*/

#include <endian.h>
#include <errno.h>
#include <getopt.h>
#include <libusb-1.0/libusb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include "libusb_version_fixes.h"

#define INTERFACE 0
#define ALT_INTERFACE 4
#define RETRIES 1
#define ENDPOINT_OUT 0x03
#define WRITING_OFFSET 0x80
#define VID 0x27ae
#define PIDS { 0x1016, 0x1018, 0x1026, 0x1028 }
// a maximum of 255=512/2-1 COEFFs can be transmitted at a single time 512 because of 512 Bytes 2 bypes per coeff -1 Byte setup see:
// libusb_control_transfer -> Transfer length limitations
#define FILTER_ORDER 62      // default 64
#define COEFF_AMOUNT (FILTER_ORDER / 2 + 1)
#define MAX_COEFF_AMOUNT 256 // default 256
#define MAX_NOTCH_COEFF_AMOUNT 2 // default 2
#define NOTCH_COEFF_A_ADRESS 9 // Dez. Adress 9 not Hex
#define NOTCH_COEFF_B_ADRESS 10 // Dez. Adress 10 not Hex
#define FILTER_ADRESS 16 // Dez. Adress 16 not Hex
#define ERROR_NOT_FLEXIBAND2 27
#define TIMEOUTINMS 1000
#define RESET 999
// Info bits for which notch and fir filter are in the design
#define NOTCH0_INFO 0x00010000
#define NOTCH1_INFO 0x00020000
#define NOTCH2_INFO 0x00040000
#define FIR0_INFO 0x00000001
#define FIR1_INFO 0x00000002
#define FIR2_INFO 0x00000004


// ToDo: make enum for Errors and make it printable.
// Errors:
// file_flag == 1 -> File Error Cannot open file
// file_flag == 2 -> File Error to many Coefficients in File for the Variant on the Connected Device
// read_error == true -> Multiple Attempts (see func. show_fir_coeffs) failed
// status or return == -2 -> fir Coefficients < 1 and Notch not specified
// return == 1 -> Multiple attempts of writing the Coefficients failed

// do_exit is set to 1 if Ctrl + C was pressed -> Aborting at next safe possibility
static volatile bool do_exit = false;

// do restart from err_restart: once if coefficients could not be written properly the first time
static volatile bool do_restart = true;

// set to true if reading fails 2 times
static volatile bool read_error = false;

static const unsigned int PID[] = PIDS;

uint32_t volatile coeff_amount_read = 0;

uint32_t volatile dsp_chain_enable_clear_previous = 0;

uint32_t volatile fir_notch_info = 0;

static volatile size_t DSPconfig = -1;

// This buffer stores the Data that is written to the FPGA FILTER Registers
// COEFF_AMOUNT because each Register has 2 Coefficients and registers are transferred
volatile int32_t coeffbuffwriting[MAX_COEFF_AMOUNT] = {0};

// This buffer stores the register-indeces ascending with wrong Values according to coeffbuffwriting
// MAX_COEFF_AMOUNT because each Register has 2 Coefficients and registers are transferred
volatile int coeffregstowritebuff[MAX_COEFF_AMOUNT] = {-1};

// LIBUSB_ENDPOINT_IN device-to-host transfer host is PC that runs this script
static const uint8_t VENDOR_IN = LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR;
// LIBUSB_ENDPOINT_OUT host-to-device transfer host is PC that runs this script
static const uint8_t VENDOR_OUT = LIBUSB_ENDPOINT_OUT | LIBUSB_RECIPIENT_DEVICE | LIBUSB_REQUEST_TYPE_VENDOR;

static int show_fpga_info(libusb_device_handle *dev_handle);
static int show_fir_coeff(libusb_device_handle *dev_handle, const bool do_restart, const int retries);
static int show_notch_coeff(libusb_device_handle *dev_handle, const bool do_restart, const int retries);
static int upload_filter(libusb_device_handle *dev_handle, bool flexiband2);
static void callbackUSBTransferComplete(struct libusb_transfer *xfr);
static int fillBuffer(const char *filename);
static int checkDSPChain(int *fir_config_pass);
static int loadDSPChain(libusb_device_handle *dev_handle);
static int32_t sortforTransfer(int32_t tosort);
// give more information on command line
static int verbose_flag = 0;
// read coefficients
static int read_flag = 0;
// write coefficients
static int write_flag = 0;
// show times needed for writing coeffs
static int timing_flag = 0;
// don't continue if done
static volatile bool done_flag = false;
// test coefficient write
static bool test_flag = false;
// write passthrough coefficients
static int passthrough_flag = 0;
// write notch coefficients
static int notch_flag = 0;
// file given or not
static volatile int file_flag = 0;
// input validation
static volatile bool inputinvalid_flag = false;

// clang-format off
// Command line options long
static struct option long_options[] = {
    {"help",        no_argument,        NULL, 'h'},
    {"verbose",     no_argument,        NULL, 'v'},
    {"timing",      no_argument,        NULL, 'z'},
    {"complete",    no_argument,        NULL, 'c'},
    {"read",        no_argument,        NULL, 'r'},
    {"write",       no_argument,        NULL, 'w'},
    {"test",        no_argument,        NULL, 't'},
    {"passthrough", no_argument,        NULL, 'p'},
    {"notch",       required_argument,  NULL, 'n'},
    {"dsp",         required_argument,  NULL, 'd'},
    {NULL, 0, NULL, 0}
};
// clang-format on
// This will catch user initiated CTRL+C type events and allow the program to exit
void sighandler(int signum)
{
    printf("Exit\n");
    do_exit = true;
}

int main(int argc, char *argv[])
{
    int status = LIBUSB_SUCCESS;
    int rewritingtrys;
    int current_option;
    libusb_context *ctx;
    libusb_device_handle *dev_handle;
    bool is_flexiband2 = false;
    int fir_config_pass = -1;

    if (argc < 2)
    {
        printf("Usage: %s [-hvtcrwtp] [-nint|-dint] <filename> \n", argv[0]);
        return 1;
    }
    // char *filename = argv[1];
    char *filename = NULL;
    // read command line options
    // the : symbol must be after an option that needs a parameter
    while ((current_option = getopt_long(argc, argv, "hvzcrwtpn:d:", long_options, &optind)) != -1)
    {
        switch (current_option)
        {
            case 'h':
                printf(
                    "#     Synopsis: flexiband_fpga_filter_selection [-h|-v|-z|-c|-r|-w|-t|-p|-n21|-d21] <file>\n"
                    "#       System: flexiband_2_0\n"
                    "#  Description: Loads filter coefficients into flexiband_2_0 fpga for filter selection\n"
                    "#      Options: [-h | --help] Show this help section\n"
                    "#               [-v | --verbose] Show more information output\n"
                    "#               [-z | --timing] Show more information on writing timing\n"
                    "#               [-c | --complete] Run full upload (default)\n"
                    "#               [-r | --read] Read filter coefficients\n"
                    "#               [-w | --write] Write filter coefficients specified in file\n"
                    "#               [-t | --test] Write filter coefficients with 0xABCDEF15 + i\n"
                    "#               [-p | --passthrough] Write passthrough filter coefficients\n"
                    "#               [-n | --notch] Write notch filter coefficients specified in file, upload Channel needs to be stated numerically like \"--notch=21\" or \"-n21\" \n"
                    "#               [-d | --dsp] Select the DSP-Chains to upload the filter coefficients into, DSP Channels needs to be stated numerically like \"--dsp=10\" or \"-d10\"\n"
                    "#       Author: Gold Maximilian\n");
                return 0;
            case 'v':
                verbose_flag = 1;
                break;
            case 'z':
                timing_flag = 1;
                break;
            case 'c':
                read_flag = 1;
                write_flag = 1;
                break;
            case 'r':
                read_flag = 1;
                break;
            case 'w':
                write_flag = 1;
                break;
            case 't':
                test_flag = true;
                break;
            case 'p':
                passthrough_flag = 1;
                break;
            case 'n':
                notch_flag = 1;
                if (verbose_flag)
                {
                    printf("optarg for notch:%s\n", optarg);
                    if(optarg == NULL)
                    {
                        printf("optarg is NULL\n");
                    }
                    if(strlen(optarg) < 1)
                    {
                        printf("optarg has no sufficent length (<1)\n");
                    }
                }
                for (int i = 0; i < strlen(optarg); i++)
                {
                    if (!isdigit(optarg[i]))
                    {
                        fprintf(stderr,"Argument for DSP-Chain selection must only contain Digits.\n");
                        inputinvalid_flag = true;
                        goto err_ret;
                    }
                }
                DSPconfig = (atoi(optarg));
                if (verbose_flag)
                {
                    printf("DSPconfig for notch:%d\n", DSPconfig);
                }
                break;
            case 'd':
                if (verbose_flag)
                {
                    printf("optarg for DSP selection:%s\n", optarg);
                    if(optarg == NULL)
                    {
                        printf("optarg is NULL\n");
                    }
                    if(strlen(optarg) < 1)
                    {
                        printf("optarg has no sufficent length (<1)\n");
                    }
                }
                for (int i = 0; i < strlen(optarg); i++)
                {
                    if (!isdigit(optarg[i]))
                    {
                        fprintf(stderr,"Argument for DSP-Chain selection must only contain Digits.\n");
                        inputinvalid_flag = true;
                        goto err_ret;
                    }
                }
                DSPconfig = (atoi(optarg));
                if (verbose_flag)
                {
                    printf("DSPconfig for Filter:%d\n", DSPconfig);
                }
                break;

            default:
                printf("Option %s not recognized\n", current_option);
                break;
        }
    }

    // Input Validation for Notch Channelselection
    if (write_flag && DSPconfig == -1)
    {
        if (notch_flag)
        {
            printf("Specify a Channelselection for the notch Coefficients.\n");
        }
        else 
        {
            printf("Specify a Channelselection for the FIR Coefficients.\n");
        }
        printf( "Possible Configurations:\n"
                "           Reset -> 999\n"
                "Channels   2,1,0 -> 210\n"
                "Channels   2,1   -> 21\n"
                "Channels   2,0   -> 20\n"
                "Channels   1,0   -> 10\n"
                "Channel    2     -> 2\n"
                "Channel    1     -> 1\n"
                "Channel    0     -> 0\n");
    }
    else if (write_flag)
    {
        if (DSPconfig != RESET && DSPconfig != 210 && DSPconfig != 21 && DSPconfig != 20 && DSPconfig != 10 && DSPconfig != 2 && DSPconfig != 1 && DSPconfig != 0)
        {
            if (notch_flag)
            {
                fprintf(stderr, "Error: Notch Channelselection invalid\n");
            }
            else 
            {
                fprintf(stderr, "Error: FIR Channelselection invalid\n");
            }
            printf( "Possible Channel Configurations:\n"
                "Reset            -> 999 (no Channel or Notch active)\n"
                "Channels   2,1,0 -> 210\n"
                "Channels   2,1   -> 21\n"
                "Channels   2,0   -> 20\n"
                "Channels   1,0   -> 10\n"
                "Channel    2     -> 2\n"
                "Channel    1     -> 1\n"
                "Channel    0     -> 0\n");
            goto err_ret;
        }
    }
    else{
        DSPconfig = 1;
    }

    // no specification what to do -> complete
    if (write_flag == 0 && read_flag == 0)
    {
        read_flag = 1;
        write_flag = 1;
    }
    // if (verbose_flag)
    // {
    //     printf("optind: %d\n", optind);
    //     printf("argc: %d\n", argc);
    //     printf("optind < argc?\n");
    //     printf("%d < %d\n", optind, argc);
    // }
    // get filename
    if (optind < argc)
    {
        filename = argv[optind];
        if (verbose_flag == 1)
        {
            printf("filename: %s\n", filename);
        }
    }
    else
    {
        fprintf(stderr,"Not enough arguments given.\n");
    }

    if (filename == NULL && write_flag && !test_flag && !passthrough_flag)
    {
        printf("Usage: %s [-hvzcrwtpnd] <filename> \n", argv[0]);
        printf("Use: %s -h for a detaled description of the options.\n", argv[0]);
        return 1;
    }

    // Define signal handler to catch system generated signals
    // (If user hits CTRL+C, this will deal with it.)
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);
    signal(SIGQUIT, sighandler);
err_restart:
    rewritingtrys = 0;
    status = libusb_init(&ctx);
    // Set libusb Debug level 3
    libusb_set_debug(ctx, 3);
    for (unsigned int i = 0; i < sizeof(PID); ++i)
    {
        dev_handle = libusb_open_device_with_vid_pid(ctx, VID, PID[i]);
        is_flexiband2 = (PID[i] == 0x1026 || PID[i] == 0x1028) ? true : false;
        if (dev_handle != NULL)
            goto usb_ok;
    }

    fprintf(stderr, "Error: No devices with VID=0x%04X found\n", VID);
    status = 1;
    goto err_usb;

usb_ok:
    if (libusb_kernel_driver_active(dev_handle, INTERFACE) == 1)
    {
        printf("Warning: Kernel driver active, detaching kernel driver...");
        status = libusb_detach_kernel_driver(dev_handle, INTERFACE);
        if (status)
        {
            fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
            goto err_dev;
        }
    }

    status = libusb_claim_interface(dev_handle, INTERFACE);
    if (status)
    {
        fprintf(stderr, "Claim interface: %s\n", libusb_strerror((enum libusb_error)status));
        goto err_dev;
    }
    if (read_flag == 1 && do_restart)
    {
        status = show_fpga_info(dev_handle);
        if (status == -2)
        {
            goto err_dev;
        }
        if (fir_config_pass <= 0 && write_flag)
        {
            status = checkDSPChain(&fir_config_pass);
            if (status > 0)
            {
                goto err_dev;
            }
        }
    }
    if (filename != NULL)
    {
        // Fill Buffer for Fir Coefficient Upload
        file_flag = fillBuffer(filename);
        if (file_flag == 1)
        {
            fprintf(stderr, "Error: Cannot Read file.\n", VID);
            goto err_ret;
        }
        else if (file_flag == 2)
        {
            fprintf(stderr, "Error: Too many Coefficients for the Variant.\n", VID);
            printf("The maximum Amout of Coefficients for this Variant is %d.\n", coeff_amount_read);
            goto err_ret;
        }
    }
    if (notch_flag)
    {
        status = show_notch_coeff(dev_handle, do_restart, rewritingtrys);
        if (status < 0)
        {
            status = show_notch_coeff(dev_handle, do_restart, rewritingtrys);
            if (status < 0)
            {
                read_error = true;
                fprintf(stderr, "Error: reading notch coeff. Retry timeout. Reading tries 2 \n", rewritingtrys);
                goto err_dev;
            }
        }
        if (write_flag == 0)
        {
            if(verbose_flag)
            {
                printf("No writing.\n");
            }
            goto err_dev;
        }
    }
    if (read_flag == 1 && !done_flag && !notch_flag)
    {
        status = show_fir_coeff(dev_handle, do_restart, rewritingtrys);
        if (status < 0)
        {
            status = show_fir_coeff(dev_handle, do_restart, rewritingtrys);
            if (status < 0)
            {
                read_error = true;
                fprintf(stderr, "Error: reading fir coeff. Retry timeout. Reading tries 2 \n", rewritingtrys);
                goto err_dev;
            }
        }
        if (write_flag == 0)
        {
            if(verbose_flag)
            {
                printf("No writing.\n");
            }
            goto err_dev;
        }
    }
    if (do_exit)
    {
        goto err_dev;
    }
    if (verbose_flag == 1)
    {
        printf("read_flag = %i\n", read_flag);
        printf("write_flag = %i\n", write_flag);
        printf("notch_flag = %i, value = %i\n", notch_flag, DSPconfig);
    }
    while (coeffregstowritebuff[0] >= 0)
    {
        rewritingtrys++;
        if (write_flag == 1 && !done_flag)
        {
            status = upload_filter(dev_handle, is_flexiband2);
        }
        if (do_exit)
        {
            goto err_dev;
        }
        if (notch_flag && !done_flag)
        {
            status = show_notch_coeff(dev_handle, do_restart, rewritingtrys);
            if (status < 0)
            {
                status = show_notch_coeff(dev_handle, do_restart, rewritingtrys);
                if (status < 0)
                {
                    read_error = true;
                    fprintf(stderr, "Error: reading notch coeff. Retry timeout. Reading tries 2 \n", rewritingtrys);
                    goto err_dev;
                }
            }
        }
        if (read_flag == 1 && !done_flag && !notch_flag)
        {
            status = show_fir_coeff(dev_handle, do_restart, rewritingtrys);
            if (status < 0)
            {
                status = show_fir_coeff(dev_handle, do_restart, rewritingtrys);
                if (status < 0)
                {
                    read_error = true;
                    fprintf(stderr, "Error: reading fir coeff. Retry timeout. Reading tries 2 \n", rewritingtrys);
                    goto err_dev;
                }
            }
        }
        if (do_exit)
        {
            goto err_dev;
        }
        if (rewritingtrys >= RETRIES && !done_flag)
        {
            if (verbose_flag == 1)
            {
                fprintf(stderr, "Error: write fir coeff. Retry timeout. Writing tries %i \n", rewritingtrys);
            }
            goto err_dev;
        }
    }
    if(done_flag && write_flag)
    {
        status = loadDSPChain(dev_handle);
        if (status < 0)
        {
            fprintf(stderr, "Error: load DSP-Chain. Cannot write dsp_chain_select_reg.\n");
        }
        if (verbose_flag == 1)
        {
            if (status < 0)
            {
                fprintf(stderr, "Error: status of loadDSPChain = %d\n", status);
            }
            if (status > 0)
            {
                printf("DSP-Chain coefficient Upload complete.\n");
            }
        }
    }
err_dev:
    libusb_close(dev_handle);

err_usb:
    libusb_exit(ctx);
    if (rewritingtrys >= RETRIES && do_restart && write_flag == 1 && fir_config_pass < 0)
    {
        do_restart = false;
        goto err_restart;
    }
err_ret:
    if(file_flag == 1)
    {
        fprintf(stderr, "File cannot be opened.\n Exiting.\n");
    }
    else if(file_flag == 2)
    {
        fprintf(stderr, "The Amout of Coefficients in the Configuration File is to great.\n Exiting.\n");
    }
    else if(fir_config_pass == 1)
    {
        fprintf(stderr, "No upload into uncertain configuration.\n Exiting.\n");
    }
    else if(fir_config_pass == 2)
    {
        fprintf(stderr, "No upload into non-existing filter.\n Exiting.\n");
    }
    else if (read_error)
    {
        fprintf(stderr, "\nCould not read all Coefficients properly. Please retry.\n");
    }
    else if (status == -2)
    {
        printf("Neither fir Coefficient amount sufficient nor Notch specified exiting.\n");
    }
    else if (rewritingtrys == 0 && do_restart == true && write_flag == 1 && is_flexiband2)
    {
        printf( "The Coefficients are already as desired. Nothing needs to be done.\n");
    }
    else if (rewritingtrys >= RETRIES && do_restart == false && write_flag == 1)
    {
        fprintf(stderr, "\nCould not write all Coefficients properly. Please retry.\n");
        return 1;
    }
    else if (done_flag)
    {
        printf("Done\n");
    }
    else
    {
        fprintf(stderr, "\nCould not write/read all Coefficients properly. Please retry.\n");
    }
    if (done_flag)
    {
        return 0;
    }
    return status;
}

static int show_fpga_info(libusb_device_handle *dev_handle)
{
    int status = 0;
    // Most of this Information is self Explenatory like the build_number, the git_hash and the timestamp
    printf("Read FPGA info...\n");
    uint16_t build_number;
    // 4th parameter is the adress read from, see filter_conf_reg_spi.vhd for register descriptions
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0001, 0x00, (unsigned char *)&build_number, sizeof(build_number), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA build number\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    uint32_t git_hash;
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0002, 0x00, (unsigned char *)&git_hash, sizeof(git_hash), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA git hash\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    uint32_t timestamp;
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0003, 0x00, (unsigned char *)&timestamp, sizeof(timestamp), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA build time\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    // The functionality of the dsp_chain_enable_clear (before transfer) is:
    // -first 3 bits 2-0 (LSB) is the input enable of the dsp-chains
    // -bits 18-16 are for clearing the filter configuration of the dsp-chains
    // more in-depth information is in GHT/systems/flexiband_2_0/ip/filter_conf_reg_spi/fliter_conf_reg_spi.vhd

    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0007, 0x00, (unsigned char *)&dsp_chain_enable_clear_previous, sizeof(dsp_chain_enable_clear_previous), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA dsp chain control\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    // The Coefficient amount is as in the output explained the maximum amout of real and imag coefficients each respectively
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x0008, 0x00, (unsigned char *)&coeff_amount_read, sizeof(coeff_amount_read), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA fir coeff amount\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    // The Coefficient amount is as in the output explained the maximum amout of real and imag coefficients each respectively
    status = libusb_control_transfer(dev_handle, VENDOR_IN, 0x03, 0x000F, 0x00, (unsigned char *)&fir_notch_info, sizeof(fir_notch_info), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read FPGA fir notch info\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    // Reordering the Coefficient Amount to Human Readable
    coeff_amount_read = sortforTransfer(coeff_amount_read);
    // Reordering the dsp_chain_enable to Human Readable
    dsp_chain_enable_clear_previous = sortforTransfer(dsp_chain_enable_clear_previous);
    // Reordering the fir_notch_info to Human Readable
    fir_notch_info = sortforTransfer(fir_notch_info);

    // Output all FPGA Information
    printf("  Build number: %i\n", be16toh(build_number));
    printf("  Git hash: %08x\n", be32toh(git_hash));
    struct tm year_2000 = {0, 0, 0, 1, 0, 100, 0, 0, 0};
    time_t build_time_sec = be32toh(timestamp) + mktime(&year_2000);
    printf("  Build time: %s\n", ctime(&build_time_sec));
    printf("  DSP-Chain enable: %08X\n", dsp_chain_enable_clear_previous);
    printf("  FIR + Notch info: %08X\n", fir_notch_info);
    printf("  Coeff-amount: %08X | as Integer: %4d (for imag and real Coefficients each)\n", coeff_amount_read, coeff_amount_read);
    if (coeff_amount_read < 1 && !notch_flag)
    {
        fprintf(stderr, "Error: Read FPGA Coefficient Amount is to small most likely design without fir filter. \n");
        status = -2;
        goto err_ret;
    }

err_ret:
    return status;
}

static int show_fir_coeff(libusb_device_handle *dev_handle, const bool do_restart, const int retries)
{
    int status = -1;
    if (coeff_amount_read == 0)
    {
        coeff_amount_read = COEFF_AMOUNT;
    }
    int32_t shortstorage;
    int32_t coeffbuff[coeff_amount_read];
    // maybe use to split the coeffs for further use
    // int16_t coeffs[COEFF_AMOUNT];
    int errorcounter = 1;
    int errorcounternonzero = 0;

    printf("Read fir coeffs...\n");

    // wait time after writing
    usleep(TIMEOUTINMS);
    // 4th parameter is the adress read from, see filter_conf_reg_spi.vhd for register descriptions
    // see start of file description for data restrictions parameter 7

    // Testing bigger transfers currently only working with 2 bytes (length of oneRegister)
    // status = libusb_control_transfer(dev_handle, VENDOR_IN, LIBUSB_REQUEST_SET_FEATURE, FILTER_ADRESS, 0x00, (unsigned char*)&coeffbuff, sizeof(coeffbuff), TIMEOUTINMS); if (status < 0) {
    //    fprintf(stderr, "Error: Read fir coeff. Problem with
    //    libusb_control_transfer.\n%s\n", libusb_strerror((enum
    //    libusb_error)status)); goto err_ret;
    //}
    
    // up until COEFF_AMOUNT
    for (int32_t i = 0; i < (coeff_amount_read); i++)
    {
        // split 32 bit Registers with imag coeff on upper 16 bits and real coeff on
        // lower 16 bits into seperate Registers
        status = libusb_control_transfer( dev_handle, VENDOR_IN, LIBUSB_REQUEST_SET_FEATURE, FILTER_ADRESS + i, 0x00, (unsigned char *)&coeffbuff[i], sizeof(coeffbuff[i]), TIMEOUTINMS);
        if (status < 0)
        {
            fprintf(stderr, "Error: Read fir coeff. Problem with libusb_control_transfer on %i.\n%s\nRetrying\n", i, libusb_strerror((enum libusb_error)status));
            status = libusb_control_transfer( dev_handle, VENDOR_IN, LIBUSB_REQUEST_SET_FEATURE, FILTER_ADRESS + i, 0x00, (unsigned char *)&coeffbuff[i], sizeof(coeffbuff[i]), TIMEOUTINMS);
            if (status < 0)
            {
                fprintf(stderr, "Error: Read fir coeff. Problem with libusb_control_transfer on %i.\n%s\nRetrying\n", i, libusb_strerror((enum libusb_error)status));
                goto err_ret;
            }
        }
        // if(i % 7 == 0){
        //     sleep(1);
        // }
    }

    if (status < 0)
    {
        fprintf( stderr, "Error: Read fir coeff. Problem with libusb_control_transfer.\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    else
    {
        errorcounter = 0;
        if (verbose_flag && !done_flag){

            for (int32_t i = 0; i < (coeff_amount_read); i++)
            {
                shortstorage = sortforTransfer(coeffbuff[i]);
                // split 32 bit Registers with imag coeff on upper 16 bits and real coeff on lower 16 bits into seperate Registers
                printf("  Read imag Coeff[%02i]: %6d,real Coeff[%02i]: %6d\n", i, (int16_t)((shortstorage & 0xFFFF0000) >> 16), i, (int16_t)(shortstorage & 0x0000FFFF));
            }
        }
        for (int32_t i = 0; i < (coeff_amount_read); i++)
        {

            // Comparing results
            if (coeffbuff[i] != coeffbuffwriting[i] && i <= (coeff_amount_read) && !done_flag)
            {
                coeffregstowritebuff[errorcounter] = i;
                ++errorcounter;
                if (do_restart == true && retries == 0 && verbose_flag)
                {
                    printf("Coefficient[%02i]: to be written: %08X previous: %08X\n", i + 1, coeffbuffwriting[i], coeffbuff[i]);
                }
                else if (verbose_flag)
                {
                    printf("Error Coefficient[%02i]: written: %08X read: %08X\n", i + 1, coeffbuffwriting[i], coeffbuff[i]);
                }
                if (coeffbuff[i] != 0)
                {
                    ++errorcounternonzero;
                    // printf("Error Coefficient[%02i]: written: %08X read: %08X\n", i + 1, coeffbuffwriting[i], coeffbuff[i]);
                }
            }
        }
        printf("OK: %02i,Errorcounternonzero: %02i, Errorcounter: %02i\n", (coeff_amount_read - errorcounter), errorcounternonzero, errorcounter);
        if (errorcounter == 0)
        {
            coeffregstowritebuff[errorcounter] = -1;
        }
    }
err_ret:
    if (errorcounter == 0)
    {
        done_flag = true;
    }
    return status;
}

static int show_notch_coeff(libusb_device_handle *dev_handle, const bool do_restart, const int retries)
{
    int status = -1;
    int32_t shortstorage;
    int32_t notchcoeffbuff[MAX_NOTCH_COEFF_AMOUNT];
    int errorcounter = 1;
    int errorcounternonzero = 0;
    int i = 0;

    printf("Read notch coeffs...\n");

    // wait time after writing
    usleep(TIMEOUTINMS);

    // up until COEFF_AMOUNT
    // split 32 bit Registers with imag coeff on upper 16 bits and real coeff on
    // lower 16 bits into seperate Registers
    status = libusb_control_transfer( dev_handle, VENDOR_IN, LIBUSB_REQUEST_SET_FEATURE, NOTCH_COEFF_A_ADRESS, 0x00, (unsigned char *)&notchcoeffbuff[i], sizeof(notchcoeffbuff[i]), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read notch coeff. Problem with libusb_control_transfer on %i.\n%s\n", i, libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    i++;
    status = libusb_control_transfer( dev_handle, VENDOR_IN, LIBUSB_REQUEST_SET_FEATURE, NOTCH_COEFF_B_ADRESS, 0x00, (unsigned char *)&notchcoeffbuff[i], sizeof(notchcoeffbuff[i]), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: Read notch coeff. Problem with libusb_control_transfer on %i.\n%s\n", i, libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }

    if (status < 0)
    {
        fprintf( stderr, "Error: Read notch coeff. Problem with libusb_control_transfer.\n%s\n", libusb_strerror((enum libusb_error)status));
        goto err_ret;
    }
    else
    {
        errorcounter = i = 0;
        if (verbose_flag && !done_flag)
        {

            shortstorage = sortforTransfer(notchcoeffbuff[i]);
            // split 32 bit Registers with imag coeff on upper 16 bits and real coeff on lower 16 bits into seperate Registers
            printf("  Read Notch imag Coeff[A]: %6d,real Coeff[A]: %6d\n", (int16_t)((shortstorage & 0xFFFF0000) >> 16), (int16_t)(shortstorage & 0x0000FFFF));
            i++;
            shortstorage = sortforTransfer(notchcoeffbuff[i]);
            // split 32 bit Registers with imag coeff on upper 16 bits and real coeff on lower 16 bits into seperate Registers
            printf("  Read Notch imag Coeff[B]: %6d,real Coeff[B]: %6d\n", (int16_t)((shortstorage & 0xFFFF0000) >> 16), (int16_t)(shortstorage & 0x0000FFFF));

        }
        for(i = 0;i < MAX_NOTCH_COEFF_AMOUNT; i++)
        {
            // Comparing results
            if (notchcoeffbuff[i] != coeffbuffwriting[i] && !done_flag)
            {
                coeffregstowritebuff[errorcounter] = i;
                ++errorcounter;
                if (do_restart && retries == 0 && verbose_flag)
                {
                    printf("Notch Coefficient[%02i]: to be written: %08X previous: %08X\n", i + 1, coeffbuffwriting[i], notchcoeffbuff[i]);
                }
                else if (verbose_flag)
                {
                    printf("Error Notch Coefficient[%02i]: written: %08X read: %08X\n", i + 1, coeffbuffwriting[i], notchcoeffbuff[i]);
                }
                if (notchcoeffbuff[i] != 0)
                {
                    ++errorcounternonzero;
                    // printf("Error Coefficient[%02i]: written: %08X read: %08X\n", i + 1, coeffbuffwriting[i], coeffbuff[i]);
                }
            }
        }
        printf("OK: %02i,Errorcounternonzero: %02i, Errorcounter: %02i\n", (MAX_NOTCH_COEFF_AMOUNT - errorcounter), errorcounternonzero, errorcounter);
        if (errorcounter == 0)
        {
            coeffregstowritebuff[errorcounter] = -1;
        }
    }

err_ret:
    if (errorcounter == 0)
    {
        done_flag = true;
    }
    return status;
}

static int upload_filter(libusb_device_handle *dev_handle, bool flexiband2)
{
    int status = 0;
    // clock_t writingspeedbuff[MAX_COEFF_AMOUNT] = {0};
    struct timespec timebuff[MAX_COEFF_AMOUNT];
    if (!flexiband2)
    {
        // if not flexiband 2 exit -> only flexiband 2 has fir registers
        status = ERROR_NOT_FLEXIBAND2;
        fprintf(stderr, "No Flexiband 2 detected. exiting. \n");
        goto err_ret;
    }
    else
    {
        // set dsp_chain_enable 1-3 see: filter_conf_reg_spi.vhd
        // uint32_t dsp_chain_enable_clear = 0x00000007;
        // status = libusb_control_transfer( dev_handle, VENDOR_OUT, 0x03, 0x0007 | WRITING_OFFSET, 0x00, (unsigned char *)&dsp_chain_enable_clear, sizeof(dsp_chain_enable_clear), TIMEOUTINMS);

        if (notch_flag)
        {
            int i = 0;
            // coeffregstowritebuff[i] = -1;
            status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x03, (NOTCH_COEFF_A_ADRESS) | WRITING_OFFSET, 0x00, (unsigned char *)&coeffbuffwriting[i], sizeof(coeffbuffwriting[i]), TIMEOUTINMS);
            if (status <= 0)
            {
                printf("ERROR: Upload Notch filter config libusb_control_transfer on %i\n", i);
                fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
                goto err_ret;
            }
            i++;
            status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x03, (NOTCH_COEFF_B_ADRESS) | WRITING_OFFSET, 0x00, (unsigned char *)&coeffbuffwriting[i], sizeof(coeffbuffwriting[i]), TIMEOUTINMS);
            if (status <= 0)
            {
                printf("ERROR: Upload Notch filter config libusb_control_transfer on %i\n", i);
                fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
                goto err_ret;
            }
            // For Notch Coefficient upload skip filter Coefficient upload
            goto notch_ret;
        }
        if (status < 0)
        {
            fprintf(stderr, "Error: write FPGA enable dsp clear dsp\n%s\n", libusb_strerror((enum libusb_error)status));
            goto err_ret;
        }
        int32_t i = 0;
        // printf("Entering Loop\n");
        while (coeffregstowritebuff[i] >= 0 && i < (coeff_amount_read))
        {
            coeffregstowritebuff[i] = -1;
            status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x03, (FILTER_ADRESS + i) | WRITING_OFFSET, 0x00, (unsigned char *)&coeffbuffwriting[i], sizeof(coeffbuffwriting[i]), TIMEOUTINMS);
            if (status <= 0)
            {
                printf("ERROR: Upload filter config libusb_control_transfer on %i\n", i);
                fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
                goto err_ret;
            }
            // writing the same Register twice because of stability problems with writing
            status = libusb_control_transfer(dev_handle, VENDOR_OUT, 0x03, (FILTER_ADRESS + i) | WRITING_OFFSET, 0x00, (unsigned char *)&coeffbuffwriting[i], sizeof(coeffbuffwriting[i]), TIMEOUTINMS);
            if (status <= 0)
            {
                printf("ERROR: Upload filter config libusb_control_transfer on %i\n", i);
                fprintf(stderr, "%s\n", libusb_strerror((enum libusb_error)status));
                goto err_ret;
            }
            if (timing_flag == 1 && write_flag == 1)
            {
                // old version
                // writingspeedbuff[i] = clock();
                clock_gettime(CLOCK_MONOTONIC_RAW, &timebuff[i]);
            }
            i++;
            // if(i % 7 == 0){
            //     sleep(1);
            // }
        }
        if (status > 0)
        {
            printf("successfully transferred whole Buffer\n");
        }
        if (timing_flag == 1 && write_flag == 1)
        {
            for (int j = 0; j < coeff_amount_read; j++)
            {
                printf("Coefficient[%3i]: Accurate time diff %8ld (usec), absolut time stamp %14ld\n", j + 1, (timebuff[j + 1].tv_sec - timebuff[j].tv_sec) * 1000000 + (timebuff[j + 1].tv_nsec - timebuff[j].tv_nsec)/1000, timebuff[j].tv_sec * 1000000 + timebuff[j].tv_nsec/1000);
                // old version
                // printf("Coefficient[%2i]: Absolute clicks: %ld Difference to next: %ld clicks (%f seconds)\n", j + 1, writingspeedbuff[j], writingspeedbuff[j + 1] - writingspeedbuff[j], ((float)(writingspeedbuff[j + 1] - writingspeedbuff[j]) / CLOCKS_PER_SEC));
            }
            // if (libusb_handle_events(NULL) != LIBUSB_SUCCESS) {
            //     printf("Error handle event");
            // }
        }
        usleep(100000); // TODO Check, if FPGA is ready
    }

notch_ret:
err_ret:
    return status;
}

static void callbackUSBTransferComplete(struct libusb_transfer *xfr)
{
    fprintf(stderr, "Error: xfr-> status\n%s\n",
            libusb_strerror((enum libusb_transfer_status)xfr->status));
    switch (xfr->status)
    {
    case LIBUSB_TRANSFER_COMPLETED:
        printf("Transfer Completed\n");
        break;
    case LIBUSB_TRANSFER_ERROR:
        printf("Transfer error\n");
        break;
    case LIBUSB_TRANSFER_TIMED_OUT:
        printf("Transfer timed out\n");
        break;
    case LIBUSB_TRANSFER_CANCELLED:
        printf("Transfer cancelled\n");
        break;
    case LIBUSB_TRANSFER_STALL:
        printf("Transfer stall\n");
        break;
    case LIBUSB_TRANSFER_NO_DEVICE:
        printf("Transfer no device\n");
        break;
    case LIBUSB_TRANSFER_OVERFLOW:
        printf("Transfer Overflow\n");
        break;
    default:
        printf("Transfer Error\n");
    }
}

// Fills Buffer that needs to be transmitted either from FILE, or according to set flag
static int fillBuffer(const char *filename)
{
    char *fircoeffreal = NULL;
    char *fircoeffimag = NULL;
    char *notchcoeffreal = NULL;
    char *notchcoeffimag = NULL;
    size_t len = 0;
    int shortstorage;
    uint32_t coefffilebuff[MAX_COEFF_AMOUNT] = {0};
    uint32_t fileCoefficientsAmount = 0;
    uint32_t fileCoefficientsAmountNotch = 0;
    int countup = 0;
    // open filter file
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error: Open file %s\n%s\n", filename, strerror(errno));
        return 1;
    }
    // for testing
    if (test_flag || passthrough_flag)
    {
        for (int32_t i = 0; i < coeff_amount_read; i++)
        {
            if (passthrough_flag)
            {
                coefffilebuff[i] = 0;
            }
            else if (test_flag)
            {
                //zählt genauso in ILA Hoch
                shortstorage = 0xE0CDF012 + i * 0x01000100;
                coefffilebuff[i] |= sortforTransfer(shortstorage);
            };
        }
    }
    // read notch Coefficients for notch upload
    if (write_flag && notch_flag)
    {
        for (fileCoefficientsAmountNotch = 0; fileCoefficientsAmountNotch < MAX_COEFF_AMOUNT; fileCoefficientsAmountNotch++) {
                // Read file line by line since it are even numbers of coefficients (real and imag same number) break if only one coefficent line left
                // only pairs of real and imag are transferred
            if ((getline(&notchcoeffreal, &len, fp)) == -1) {
                break; //break so that only full Registers are transferred
            }
            if (getline(&notchcoeffimag, &len, fp) == -1) {
                break; //break so that only full Registers are transferred
            }
            // coefffilebuff[i] = (atol(notchcoeffimag) << 16);
            // coefffilebuff[i] |= 0x0000FFFF &  atol(notchcoeffreal);
            shortstorage = (atol(notchcoeffimag) << 16);
            shortstorage |= 0x0000FFFF &  atol(notchcoeffreal);
            if (fileCoefficientsAmountNotch > MAX_NOTCH_COEFF_AMOUNT - 1)
            {
                if (verbose_flag)
                {
                    printf("Amount of Notch Coefficients in File > %3d\n", MAX_NOTCH_COEFF_AMOUNT);
                }
                fclose(fp);
                return 3;
            }

            // Reordering HexSequence for FPGA
            // coefffilebuff consists of Section1|Section2|Section3|Section4
            coefffilebuff[fileCoefficientsAmountNotch] = sortforTransfer(shortstorage);
            // printf("coeffbuff Before Reordering %3i: %08X\n",fileCoefficientsAmount ,coefffilebuff[fileCoefficientsAmount]);
        }
        free(notchcoeffimag);
        free(notchcoeffreal);
        // Rearranging the Coefficients that the Last Coefficient is in the last highest Coefficient Register
        countup = 0;
        // Remove loop over
        fileCoefficientsAmountNotch--;
        for (int32_t countdown = MAX_NOTCH_COEFF_AMOUNT - 1; countdown >= 0 ; countdown--)
        {
            if((countdown >= (MAX_NOTCH_COEFF_AMOUNT - 1) - fileCoefficientsAmountNotch) && ((MAX_NOTCH_COEFF_AMOUNT - fileCoefficientsAmountNotch) >= 0))
            {
                coeffbuffwriting[countdown] = coefffilebuff[fileCoefficientsAmountNotch - countup];
                if (verbose_flag)
                {
                    printf("coeffbuff %3i: %08X\n",countdown ,coeffbuffwriting[countdown]);
                }
            }
            else
            {
                fprintf(stderr, "Error: notch coeffs fill Buffer\n");
            }
            countup++;
        }
    }
    // read filter coefficients default
    if(write_flag && !test_flag && !passthrough_flag && !notch_flag)
    {
        for (fileCoefficientsAmount = 0; fileCoefficientsAmount < MAX_COEFF_AMOUNT; fileCoefficientsAmount++) {
                // Read file line by line since it are even numbers of coefficients (real and imag same number) break if only one coefficent line left
                // only pairs of real and imag are transferred
            if ((getline(&fircoeffreal, &len, fp)) == -1) {
                break; //break so that only full Registers are transferred
            }
            if (getline(&fircoeffimag, &len, fp) == -1) {
                break; //break so that only full Registers are transferred
            }
            // coefffilebuff[i] = (atol(fircoeffimag) << 16);
            // coefffilebuff[i] |= 0x0000FFFF &  atol(fircoeffreal);
            shortstorage = (atol(fircoeffimag) << 16);
            shortstorage |= 0x0000FFFF &  atol(fircoeffreal);
            if (fileCoefficientsAmount > coeff_amount_read - 1)
            {
                if (verbose_flag)
                {
                    printf("Amount of Coefficients in File > %3d\n", coeff_amount_read);
                }
                fclose(fp);
                return 2;
            }

            // Reordering HexSequence for FPGA
            // coefffilebuff consists of Section1|Section2|Section3|Section4
            coefffilebuff[fileCoefficientsAmount] = sortforTransfer(shortstorage);
            // printf("coeffbuff Before Reordering %3i: %08X\n",fileCoefficientsAmount ,coefffilebuff[fileCoefficientsAmount]);
        }
        free(fircoeffimag);
        free(fircoeffreal);
        // Rearranging the Coefficients that the Last Coefficient is in the last highest Coefficient Register
        countup = 0;
        // Remove loop over
        fileCoefficientsAmount -= 1;
        for (int32_t countdown = coeff_amount_read - 1; countdown >= 0 ; countdown--)
        {
            if((countdown >= (coeff_amount_read - 1) - fileCoefficientsAmount) && ((coeff_amount_read - fileCoefficientsAmount) >= 0))
            {
                coeffbuffwriting[countdown] = coefffilebuff[fileCoefficientsAmount - countup];
                if (verbose_flag)
                {
                    printf("coeffbuff %3i: %08X\n",countdown ,coeffbuffwriting[countdown]);
                }
            }
            else
            {
                // filling Coefficients that are not specified in Coefficient File with 0
                coeffbuffwriting[countdown] = 0;
                if (verbose_flag)
                {
                    printf("coeffbuff filling zeroes %3i: %08X\n",countdown ,coeffbuffwriting[countdown]);
                }
            }
            countup++;
        }
    }

    // Saving which coefficients should be written (all) for the case that the coefficients are not read back
    if (read_flag == 0)
    {
        for (int32_t j = 0; j < coeff_amount_read; j++)
        {
            coeffregstowritebuff[j] = j;
        }
    }

    fclose(fp);
    return 0;
}
static int checkDSPChain(int *fir_config_pass)
{
    if (notch_flag)
    {
        switch(DSPconfig)
        {
            case RESET:
                break;
            // 210 represent Bits 14-12 bits, 2MSB, 0LSB
            case 210:
                if ((NOTCH2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 2\n");
                    return 2;
                }
                else if ((NOTCH1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 1\n");
                    return 2;
                }
                else if ((NOTCH0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 0\n");
                    return 2;
                }
                break;
            case 21:
                if ((NOTCH2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 2\n");
                    return 2;
                }
                else if ((NOTCH1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 1\n");
                    return 2;
                }
                break;
            case 20:
                if ((NOTCH2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 2\n");
                    return 2;
                }
                else if ((NOTCH0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 0\n");
                    return 2;
                }
                break;
            case 10:
                if ((NOTCH1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 1\n");
                    return 2;
                }
                else if ((NOTCH0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 0\n");
                    return 2;
                }
                break;
            case 2:
                if ((NOTCH2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 2\n");
                    return 2;
                }
                break;
            case 1:
                if ((NOTCH1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 1\n");
                    return 2;
                }
                break;
            case 0:
                if ((NOTCH0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no notch filter on channel 0\n");
                    return 2;
                }
                break;
            default:
                fprintf(stderr, "Error: cannot check upload configuration to Variant Design\n");
                return 1;
                break;
        }
    }
    else
    {
        switch(DSPconfig)
        {
            case RESET:
                break;
            // 210 represent Bits 14-12 bits, 2MSB, 0LSB
            case 210:
                if ((FIR2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 2\n");
                    return 2;
                }
                else if ((FIR1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 1\n");
                    return 2;
                }
                else if ((FIR0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 0\n");
                    return 2;
                }
                break;
            case 21:
                if ((FIR2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 2\n");
                    return 2;
                }
                else if ((FIR1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 1\n");
                    return 2;
                }
                break;
            case 20:
                if ((FIR2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 2\n");
                    return 2;
                }
                else if ((FIR0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 0\n");
                    return 2;
                }
                break;
            case 10:
                if ((NOTCH1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 1\n");
                    return 2;
                }
                else if ((FIR0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 0\n");
                    return 2;
                }
                break;
            case 2:
                if ((FIR2_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 2\n");
                    return 2;
                }
                break;
            case 1:
                if ((FIR1_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 1\n");
                    return 2;
                }
                break;
            case 0:
                if ((FIR0_INFO & fir_notch_info) <= 0)
                {
                    fprintf(stderr, "Error: Design has no FIR filter on channel 0\n");
                    return 2;
                }
                break;
            default:
                fprintf(stderr, "Error: cannot check upload configuration to Variant Design\n");
                return 1;
                break;
        }
    }
    *fir_config_pass = 0;
    return 0;
}

static int loadDSPChain(libusb_device_handle *dev_handle)
{
    int status = 0;
    uint32_t dsp_chain_enable_clear = 0; //8000009F?
    uint32_t dsp_chain_enable_clear_value = dsp_chain_enable_clear_previous;

    if (notch_flag)
    {
        switch(DSPconfig)
        {
            case RESET:
                dsp_chain_enable_clear_value = 0x00000000;
                break;
            // always take previous configuration into account for next configuration
            // 210 represent Bits 14-12 bits, 2MSB, 0LSB
            case 210:
                dsp_chain_enable_clear_value = 0x0000F000 | dsp_chain_enable_clear_previous;
                break;
            case 21:
                dsp_chain_enable_clear_value = 0x0000E000 | dsp_chain_enable_clear_previous;
                break;
            case 20:
                dsp_chain_enable_clear_value = 0x0000D000 | dsp_chain_enable_clear_previous;
                break;
            case 10:
                dsp_chain_enable_clear_value = 0x0000B000 | dsp_chain_enable_clear_previous;
                break;
            case 2:
                dsp_chain_enable_clear_value = 0x0000C000 | dsp_chain_enable_clear_previous;
                break;
            case 1:
                dsp_chain_enable_clear_value = 0x0000A000 | dsp_chain_enable_clear_previous;
                break;
            case 0:
                dsp_chain_enable_clear_value = 0x00009000 | dsp_chain_enable_clear_previous;
                break;
            default:
                dsp_chain_enable_clear_value = 0x0000A000 | dsp_chain_enable_clear_previous;
                break;
        }
    }
    else {
        switch(DSPconfig)
        {
            case RESET:
                dsp_chain_enable_clear_value = 0x00000000;
                break;
            // always take previous configuration into account for next configuration
            // 210 represent the last 3 bits (2-0), 2MSB, 0LSB
            case 210:
                dsp_chain_enable_clear_value = 0x80000007 | dsp_chain_enable_clear_previous;
                break;
            case 21:
                dsp_chain_enable_clear_value = 0x80000006 | dsp_chain_enable_clear_previous;
                break;
            case 20:
                dsp_chain_enable_clear_value = 0x80000005 | dsp_chain_enable_clear_previous;
                break;
            case 10:
                dsp_chain_enable_clear_value = 0x80000003 | dsp_chain_enable_clear_previous;
                break;
            case 2:
                dsp_chain_enable_clear_value = 0x80000004 | dsp_chain_enable_clear_previous;
                break;
            case 1:
                dsp_chain_enable_clear_value = 0x80000002 | dsp_chain_enable_clear_previous;
                break;
            case 0:
                dsp_chain_enable_clear_value = 0x80000001 | dsp_chain_enable_clear_previous;
                break;
            default:
                dsp_chain_enable_clear_value = 0x80000002 | dsp_chain_enable_clear_previous;
                break;
        }
    }



    dsp_chain_enable_clear = sortforTransfer(dsp_chain_enable_clear_value);
    status = libusb_control_transfer( dev_handle, VENDOR_OUT, 0x03, 0x0007 | WRITING_OFFSET, 0x00, (unsigned char *)&dsp_chain_enable_clear, sizeof(dsp_chain_enable_clear), TIMEOUTINMS);
    if (status < 0)
    {
        fprintf(stderr, "Error: write FPGA enable dsp clear dsp\n%s\n", libusb_strerror((enum libusb_error)status));
        status = libusb_control_transfer( dev_handle, VENDOR_OUT, 0x03, 0x0007 | WRITING_OFFSET, 0x00, (unsigned char *)&dsp_chain_enable_clear, sizeof(dsp_chain_enable_clear), TIMEOUTINMS);
        if (status < 0)
        {
            fprintf(stderr, "Error: write FPGA enable dsp clear dsp\n%s\n", libusb_strerror((enum libusb_error)status));
            goto err_ret;
        }
        else if (status > 0 && verbose_flag)
        {
            printf("DSP-Chain coefficient transfer started.\n");
        }
    }
    else if (status > 0 && verbose_flag)
    {
        printf("DSP-Chain coefficient transfer started.\n");
    }
err_ret:
    return status;
}

static int32_t sortforTransfer(int32_t tosort){
    int32_t sorted = 0;
    // Named after what it will be in the Output
    int64_t firstByte = 0;
    int64_t secondByte = 0;
    int64_t thirdByte = 0;
    int64_t fourthByte = 0;

    // Here is the Bytewise extraction from Source
    firstByte = tosort & 0x000000FF;
    secondByte = tosort & 0x0000FF00;
    thirdByte = tosort & 0x00FF0000;
    fourthByte = tosort & 0xFF000000;

    // Rearange the Bytes
    sorted |= firstByte << 24;
    sorted |= secondByte << 8;
    sorted |= thirdByte >> 8;
    sorted |= fourthByte >> 24;
    return sorted;
}
