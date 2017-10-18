// 包含头文件
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

// 常量定义
#define DUMP_COMMAND  1

// 类型定义
typedef struct {
    int     fd;
    uint8_t seq;
} CONTEXT;

#pragma pack (1)
typedef struct {
    uint8_t  bMsgType;
    uint32_t dwLength;
    uint8_t  bSlot;
    uint8_t  bSeq;
    uint8_t  bStatus;
    uint8_t  bError;
    uint8_t  bRFU;
} RSPHDR;
#pragma pack (0)

// 函数声明
void* au9580_init (char *dev );
void  au9580_close(void *ctxt);
int   au9580_slotstatus (void *ctxt, int *present);
int   au9580_iccpoweron (void *ctxt);
int   au9580_iccpoweroff(void *ctxt);
int   au9580_apdu(void *ctxt, uint8_t *apdu, int alen, uint8_t *rsp, int rlen);

// 内部函数实现
static int open_serial_port(char *dev)
{
    struct termios termattr;
    int ret;

    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        printf("failed to open serial port: %s !\n", dev);
        return fd;
    }

    ret = tcgetattr(fd, &termattr);
    if (ret < 0) {
        printf("the error of tcgetaddr is %s\n", strerror(errno));
    }

    termattr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    termattr.c_oflag &= ~(OPOST);
    termattr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    termattr.c_cflag &= ~(CSIZE | PARENB);
    termattr.c_cflag |=  (CS8);
    termattr.c_cflag &= ~(CRTSCTS);
    cfsetospeed(&termattr, B38400);
    cfsetispeed(&termattr, B38400);
    tcsetattr(fd, TCSANOW, &termattr);
    tcflush(fd, TCIOFLUSH);

    return fd;
}

static int read_serial_port(int fd, uint8_t *buf, int len)
{
    int retry = len;
    int ret   = -1;
    int m     = len;
    int n;
    while (len > 0 && retry > 0) {
        n = read(fd, buf, len);
        if (n < 0) {
            if (errno == EAGAIN) {
                retry--;
                usleep(10000);
            } else {
                goto done;
            }
        } else {
            len -= n;
            buf += n;
        }
    }

done:
    return (m - len);
}

static int get_response(int fd, uint8_t *rsp, int len, int timeout)
{
    fd_set         fds;
    struct timeval tv;
    RSPHDR        *hdr   = (RSPHDR*)rsp;
    int            total = 0;
    int            n;

    FD_ZERO(&fds);
    FD_SET (fd, &fds);
    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = timeout % 1000 * 1000;

    if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) {
        printf("select error or timeout !\n");
        return -1;
    }

    // 1. check response buffer size
    if (len < sizeof(RSPHDR)) {
        printf("invalid respone buffer size, size = %d, wanted = %d !\n", len, sizeof(RSPHDR));
        return -1;
    }

    n = read_serial_port(fd, rsp, sizeof(RSPHDR));
    total += n;
    if (n != sizeof(RSPHDR)) {
        printf("read respone header failed !\n");
        return total;
    }

    // 2. check response buffer size
    if (len < sizeof(RSPHDR) + hdr->dwLength + 1) {
        printf("invalid respone buffer size, size = %d, wanted = %d !\n", len, sizeof(RSPHDR) + hdr->dwLength + 1);
        return -1;
    }

    n = read_serial_port(fd, rsp + sizeof(RSPHDR), hdr->dwLength + 1);
    total += n;
    if (n != hdr->dwLength + 1) {
        printf("read respone data failed !\n");
    }
    return total;
}

static int check_lrc(uint8_t *buf, int len)
{
    uint8_t lrc = 0;
    int     i;
    for (i=0; i<len-1; i++) lrc ^= buf[i];
    return buf[len-1] == lrc ? 0 : -1;
}

static int send_command(int fd, uint8_t *cmd, int clen, uint8_t *seq, uint8_t *rsp, int rlen, int timeout)
{
    int ret, i;

    cmd[6]     = (*seq)++; // seq
    cmd[clen-1]= 0;        // lrc
    for (i=0; i<clen-1; i++) cmd[clen-1] ^= cmd[i];

#if DUMP_COMMAND
    printf("+cmd: ");
    for (i=0; i<clen; i++) {
        printf("%02x ", cmd[i]);
    }
    printf("\n");
#endif

    ret = write(fd, cmd, clen);
    if (ret != clen) {
        printf("write failed, ret = %d, clen = %d\n", ret, clen);
        return -1;
    }

    rlen = get_response(fd, rsp, rlen, timeout);
    if (rlen < 0) {
        printf("get_response failed, rlen: %d%d\n", rlen);
        return -1;
    }

    if (check_lrc(rsp, rlen) < 0) {
        printf("check respone lrc failed !\n");
        return -1;
    }

    if (rsp[5] != cmd[5] || rsp[6] != cmd[6]) {
        printf("check respone slot and seq failed !\n");
        return -1;
    }

#if DUMP_COMMAND
    printf("-rsp: ");
    for (i=0; i<rlen; i++) {
        printf("%02x ", rsp[i]);
    }
    printf("\n\n");
#endif

    return 0;
}

// 函数实现
void* au9580_init(char *dev)
{
    CONTEXT *context = malloc(sizeof(CONTEXT));
    if (!context) return NULL;

    context->fd = open_serial_port(dev);
    return context;
}

void au9580_close(void *ctxt)
{
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return;

    close(context->fd);
    free (context);
}

int au9580_slotstatus(void *ctxt, int *present)
{
    uint8_t cmd[]    = { 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rsp[64]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 200) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    if (rsp[7] == 0 || rsp[7] == 1) *present = 1;
    else *present = 0;
    return 0;
}

int au9580_iccpoweron(void *ctxt)
{
    uint8_t cmd[]    = { 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rsp[64]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 200) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    return 0;
}

int au9580_iccpoweroff(void *ctxt)
{
    uint8_t cmd[]    = { 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rsp[64]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 200) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    return 0;
}

int au9580_xfrblock(void *ctxt, uint8_t *block, int blklen, uint8_t *rsp, int rlen)
{
    uint8_t cmd[267] = { 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00 };
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    cmd[1] = (blklen >> 0) & 0xff;
    cmd[2] = (blklen >> 8) & 0xff;
    cmd[3] = (blklen >>16) & 0xff;
    cmd[4] = (blklen >>24) & 0xff;
    memcpy(cmd + 10, block, blklen);

    if (send_command(context->fd, cmd, 11 + blklen, &context->seq, rsp, rlen, 200) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    return 0;
}

int main(void)
{
    void *bcas    = au9580_init("/dev/ttyS3");
    int   ret     = 0;
    int   present = 0;
    uint8_t init_apdu_cmd[]   = { 0x90, 0x30, 0x00, 0x00, 0x00 };
    uint8_t init_apdu_rsp[128]= { 0x00 };

    while (1) {
        ret = au9580_slotstatus(bcas, &present);
        printf("ret: %d, card present: %d\n\n", ret, present);
        if (ret == 0 && present == 1) break;
        sleep(1);
    }

    ret = au9580_iccpoweron(bcas);
    if (ret != 0) {
        printf("au9580_iccpoweron failed !\n");
        goto exit;
    }

    au9580_xfrblock(bcas, init_apdu_cmd, sizeof(init_apdu_cmd), init_apdu_rsp, sizeof(init_apdu_rsp));
    au9580_iccpoweroff(bcas);

exit:
    au9580_close(bcas);
    return 0;
}

#if 0
cas_init
cas_free
cas_card_present
cas_card_get_syskey_and_cbc
cas_card_get_datakey
cas_fc8300_receive_ecm
cas_fc8300_send_datakey
#endif











