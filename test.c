#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <pthread.h>
#include <fcntl.h>

#define CLEAR_STDIN     while(getc(stdin) != '\n')
#define SCI_DEV_NAME    "bcasif"

#define SCR_IO_REINIT               0xAAA1
#define SCR_IO_GET_CARD_STATUS      0xAAA2
#define SCR_IO_GET_ATR              0xAAA3
#define SCR_IO_SET_ETU              0xAAA4
#define SCR_IO_READ                 0xAAA5
#define SCR_IO_WRITE                0xAAA6

typedef struct SCR_CMD {
    unsigned int rLen;
    unsigned int wLen;
    unsigned char buf[256];
} SCR_CMD;

static int fd_sc_dev = -1; /* Smart Card device file descriptor. */

static unsigned char PACKAGE_INITIAL_CARD_CMD[] =
{
    0x00, 0xc1, 0x01, 0xfe, 0x3e
};

static unsigned char PACKAGE_INITIAL_SETTING_CONDITIONS_CMD[] =
{
    0x00, 0x00, 0x05, 0x90, 0x30, 0x00, 0x00, 0x00, 0xa5
};

unsigned char buf[256];
SCR_CMD scrCmd = {0, };

void sc_open(void)
{
    int ret = 0;
    char name[32];
    if (fd_sc_dev == -1) {
        sprintf(name,"/dev/%s", SCI_DEV_NAME);
        fd_sc_dev = open(name, O_RDWR);
        if (fd_sc_dev < 0) {
            printf("Can't open device: %s\n", name);
            return;
        }
        ret = ioctl(fd_sc_dev, SCR_IO_GET_CARD_STATUS, NULL);
        printf("SmartCard Status : %d\n", ret);
    }
}

void sc_close(void)
{
    if (fd_sc_dev >= 0) {
        close(fd_sc_dev);
        fd_sc_dev = -1;
    }
}

void sc_get_card_status(void)
{
    int ret;
    if (fd_sc_dev >= 0) {
        ret = ioctl(fd_sc_dev, SCR_IO_GET_CARD_STATUS, NULL);
        printf("SmartCard Status : %d\n", ret);
    }
}

void sc_reset(void)
{
    int ret;
    if (fd_sc_dev >= 0) {
        ret = ioctl(fd_sc_dev, SCR_IO_REINIT, NULL);
        printf("sc_reset : %d\n", ret);
    }
}

void sc_set_rw_length(void)
{
    int ret, i;
    if (fd_sc_dev >= 0) {
        memcpy(scrCmd.buf, PACKAGE_INITIAL_CARD_CMD, 5);
        scrCmd.wLen= 5;
        scrCmd.rLen= 5;
        ret = ioctl(fd_sc_dev, SCR_IO_WRITE, &scrCmd);
        printf("sc_set_rw_length : %d\n", scrCmd.rLen);
        for (i = 0; i < scrCmd.rLen; i++)
            printf("0x%02x, \n", scrCmd.buf[i]);
        printf("\n");
    }
}

void sc_init_setting(void)
{
    int ret, i;
    if (fd_sc_dev >= 0) {
        memcpy(scrCmd.buf, PACKAGE_INITIAL_SETTING_CONDITIONS_CMD, 9);
        scrCmd.wLen= 9;
        scrCmd.rLen= 65;
        ret = ioctl(fd_sc_dev, SCR_IO_WRITE, &scrCmd);
        printf("sc_init_setting : %d\n", scrCmd.rLen);
        for (i = 0; i < scrCmd.rLen; i++)
            printf("0x%02x, \n", scrCmd.buf[i]);
        printf("\n");
    }
}

typedef struct SC_Info {
    unsigned char header[3];
    unsigned char Protocol_unit_number;
    unsigned char unit_length;
    unsigned char IC_Card_instruction[2];
    unsigned char Return_code[2];
    unsigned char numOfCardId;
    unsigned char displayCardId[10];
} SC_Info;

SC_Info scInfo = {0};
unsigned char infodata[20];
unsigned long long scId;
unsigned short scCheckCode;
void sc_init_get_card_info(void)
{
    int ret, i;
    if (fd_sc_dev >= 0) {
        //memcpy(scrCmd.buf, CARD_ID_INFORMATION_ACQUIRE_CMD, 5);
        scrCmd.buf[0] = 0;
        scrCmd.buf[1] = 0x40;
        scrCmd.buf[2] = 0x05;
        scrCmd.buf[3] = 0x90;
        scrCmd.buf[4] = 0x32;
        scrCmd.buf[5] = 0;
        scrCmd.buf[6] = 0;
        scrCmd.buf[7] = 0;
        scrCmd.buf[8] = 0;
        for (i = 0; i < 8; i++)
            scrCmd.buf[8] ^= scrCmd.buf[i];

        scrCmd.wLen = 9;
        scrCmd.rLen = 23;
        ret = ioctl(fd_sc_dev, SCR_IO_WRITE, &scrCmd);
        printf("sc_init_get_card_info : %d\n", scrCmd.rLen);
        for (i = 0; i < scrCmd.rLen; i++)
            printf("0x%02x, \n", scrCmd.buf[i]);
        printf("\n");

        if (scrCmd.rLen > 0) {
            memcpy(&scInfo, scrCmd.buf, sizeof(scInfo));
            memcpy(&infodata, &scInfo.displayCardId[0],10);

            scId = ((unsigned long long)infodata[2]<<40)|
                   ((unsigned long long)infodata[3]<<32)|
                   ((unsigned long long)infodata[4]<<24)|
                   ((unsigned long long)infodata[5]<<16)|
                   ((unsigned long long)infodata[6]<<8 )|
                   ((unsigned long long)infodata[7]);

            scCheckCode = ((unsigned long long)infodata[8]<<8)|
                          ((unsigned long long)infodata[9]);
            printf( "Bcas_card_no.Card_ID[0] : %c\n", 0x30);
            for (i = 1; i < 15; i++) {
                int zz = 0;
                unsigned long long tmpDiv = 1;

                for (zz = 0; zz < 14 - i; zz++)
                    tmpDiv *= 10;
                printf( "Bcas_card_no.Card_ID[%d] : %c\n", i, (scId / tmpDiv) % 10 + 0x30);
            }

            for (i = 15; i < 20; i++) {
                int zz = 0;
                unsigned long long tmpDiv = 1;

                for (zz = 0; zz < 19 - i; zz++)
                    tmpDiv *= 10;
                printf( "Bcas_card_no.Card_ID[%d] : %c\n", i, (scCheckCode / tmpDiv) % 10 + 0x30);
            }
        }
    }
}

void sc_set_etu(void)
{
    int ret, i;
    if (fd_sc_dev >= 0) {
        ioctl(fd_sc_dev, SCR_IO_SET_ETU, 0);
    }
}

void sc_get_atr(void)
{
    int ret, i;
    if (fd_sc_dev >= 0) {
        printf("sc_get_atr\n");
        scrCmd.rLen = 0;
        ioctl(fd_sc_dev, SCR_IO_GET_ATR, &scrCmd);
        for (i = 0; i < scrCmd.rLen; i++)
            printf("0x%02x, \n", scrCmd.buf[i]);
        printf("\n");
    }
}

int main(void)
{
    int key, ret;

    while (1) {
        printf("===============================================\n");
        printf("\t1: SmartCard open\n");
        printf("\t2: SmartCard close\n");
        printf("\t3: SmartCard get card status\n");
        printf("\t4: SmartCard reset\n");
        printf("\t5: Set Card RW length\n");
        printf("\t6: Set Card Initial Setting\n");
        printf("\t7: Get Card Info\n");
        printf("\t8: Set ETU\n");
        printf("\t9: Get ATR\n");
        printf("\tq or Q: Quit\n");
        printf("===============================================\n");

        fflush(stdin);
        key = getc(stdin);
        CLEAR_STDIN;

        switch (key) {
            case '1':
                sc_open();
                break;
            case '2':
                sc_close();
                break;
            case '3':
                sc_get_card_status();
                break;
            case '4':
                sc_reset();
                break;
            case '5':
                sc_set_rw_length();
                break;
            case '6':
                sc_init_setting();
                break;
            case '7':
                sc_init_get_card_info();
                break;
            case '8':
                sc_set_etu();
                break;
            case '9':
                sc_get_atr();
                break;
            case 'q':
            case 'Q': goto APP_MAIN_EXIT;
            default: printf("[%c]\n", key);
        }
    }

APP_MAIN_EXIT:
    if (fd_sc_dev != -1)
        sc_close();
    return 0;
}

