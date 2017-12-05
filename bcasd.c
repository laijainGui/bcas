// 包含头文件
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>

// 内部常量定义
#define SPIOCSTYPE	_IOW('q', 0x01, unsigned long)
#define SERIO_ANY   0xff
#define SERIO_BCAS  0xbc

// 内部函数实现
static int open_serial_port(char *dev)
{
    struct termios termattr;
    int ldisc, type, ret;

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

    ldisc = N_MOUSE;
    ret   = ioctl(fd, TIOCSETD, &ldisc);
    if (ret) {
        printf("failed to do ioctl TIOCSETD !\n");
    }
    type = (SERIO_BCAS << 0) | (SERIO_ANY << 8) | (SERIO_ANY << 16);
    ret  = ioctl(fd, SPIOCSTYPE, &type);
    if (ret) {
        printf("failed to do ioctl SPIOCSTYPE !\n");
    }

    return fd;
}

static int g_fd = -1;

void sig_handler(int sig)
{
    if (g_fd != -1) {
        close(g_fd);
        g_fd = -1;
    }
}

int main(void)
{
    #define SERIAL_PORT  "/dev/ttyS3"
    g_fd = open_serial_port(SERIAL_PORT);
    if (g_fd < 0) {
        printf("failed to open serial port %s !\n", SERIAL_PORT);
        exit(1);
    }

    signal(SIGINT , sig_handler);
    signal(SIGTERM, sig_handler);

    while (1) {
        // read
        printf("bcasd read+\n");
        read(g_fd, NULL, 0);
        printf("bcasd read-\n");
    }

    // close
    if (g_fd != -1) {
        close(g_fd);
        g_fd = -1;
    }

    return 0;
}

