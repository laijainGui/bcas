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

    int fd = open(dev, O_RDWR | O_NOCTTY);
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

static int get_response(int fd, uint8_t *rsp, int len, int timeout)
{
    fd_set        fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET (fd, &fds);
    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = timeout * 1000;

    if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) {
        printf("select error or timeout !\n");
        return -1;
    }

    return read(fd, rsp, len);
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
    if (ret != clen) return -1;

    ret = get_response(fd, rsp, rlen, timeout);
    if (ret != rlen) return -1;


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
    uint8_t rsp[11]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 100) == -1) {
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
    uint8_t rsp[23]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 100) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    return 0;
}

int au9580_iccpoweroff(void *ctxt)
{
    uint8_t cmd[]    = { 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rsp[11]  = {0};
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    if (send_command(context->fd, cmd, sizeof(cmd), &context->seq, rsp, sizeof(rsp), 100) == -1) {
        printf("failed to send command %02x !\n", cmd[0]);
        return -1;
    }

    return 0;
}

int au9580_apdu(void *ctxt, uint8_t *apdu, int alen, uint8_t *rsp, int rlen)
{
    uint8_t cmd[267] = { 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00 };
    CONTEXT *context = (CONTEXT*)ctxt;
    if (!context) return -1;

    cmd[1] = (alen >> 0) & 0xff;
    cmd[2] = (alen >> 8) & 0xff;
    cmd[3] = (alen >>16) & 0xff;
    cmd[4] = (alen >>24) & 0xff;
    cmd[8] = (rlen >> 0) & 0xff;
    cmd[9] = (rlen >> 8) & 0xff;
    memcpy(cmd + 10, rsp, alen);

    if (send_command(context->fd, cmd, 11 + alen, &context->seq, rsp, rlen, 200) == -1) {
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
    uint8_t init_apdu_rsp[50] = { 0x00 };

    while (1) {
        ret = au9580_slotstatus(bcas, &present);
        printf("ret: %d, present: %d\n", ret, present);
        if (ret == 0 && present == 1) break;
        sleep(1);
    }

    au9580_iccpoweron (bcas);
    au9580_apdu(bcas, init_apdu_cmd, sizeof(init_apdu_cmd), init_apdu_rsp, sizeof(init_apdu_rsp));
    au9580_iccpoweroff(bcas);

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











