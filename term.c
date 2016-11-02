/*
 * client.c - simple terminal client
 *
 * Copyright 2013 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <unistd.h>         // tcsetattr, close, read, write
#include <sys/ioctl.h>      // ioctl
#include <stdio.h>          // printf, getchar, fopen, perror
#include <stdlib.h>         // exit
#include <sys/stat.h>       // read
#include <fcntl.h>          // read
#include <signal.h>         // signal
#include <time.h>           // time
#include <string.h>         // memcpy
#include <stdint.h>         // int types
#include <sys/time.h>       // gettimeofday

#include "term.h"
#include "usefull_macros.h"

typedef struct {
    int speed;  // communication speed in bauds/s
    int bspeed; // baudrate from termios.h
} spdtbl;

char *comdev = "/dev/ttyACM0";
int BAUD_RATE = B115200;
struct termio oldtty, tty; // TTY flags
int comfd; // TTY fd

//sed 's/[^ ]* *B\([^ ]*\).*/    {\1, B\1},/g'
/*
#define  B50    0000001
#define  B75    0000002
#define  B110   0000003
#define  B134   0000004
#define  B150   0000005
#define  B200   0000006
#define  B300   0000007
#define  B600   0000010
#define  B1200  0000011
#define  B1800  0000012
#define  B2400  0000013
#define  B4800  0000014
#define  B9600  0000015
#define  B19200 0000016
#define  B38400 0000017
#define  B57600   0010001
#define  B115200  0010002
#define  B230400  0010003
#define  B460800  0010004
#define  B500000  0010005
#define  B576000  0010006
#define  B921600  0010007
#define  B1000000 0010010
#define  B1152000 0010011
#define  B1500000 0010012
#define  B2000000 0010013
#define  B2500000 0010014
#define  B3000000 0010015
#define  B3500000 0010016
#define  B4000000 0010017
*/
static spdtbl speeds[] = {
    {50, B50},
    {75, B75},
    {110, B110},
    {134, B134},
    {150, B150},
    {200, B200},
    {300, B300},
    {600, B600},
    {1200, B1200},
    {1800, B1800},
    {2400, B2400},
    {4800, B4800},
    {9600, B9600},
    {19200, B19200},
    {38400, B38400},
    {57600, B57600},
    {115200, B115200},
    {230400, B230400},
    {460800, B460800},
    {500000, B500000},
    {576000, B576000},
    {921600, B921600},
    {1000000, B1000000},
    {1152000, B1152000},
    {1500000, B1500000},
    {2000000, B2000000},
    {2500000, B2500000},
    {3000000, B3000000},
    {3500000, B3500000},
    {4000000, B4000000},
    {0,0}
};

/**
 * test if `speed` is in .speed of `speeds` array
 * if not, exit with error code
 * if all OK, return `Bxxx` speed for given baudrate
 */
int conv_spd(int speed){
    spdtbl *spd = speeds;
    int curspeed = 0;
    do{
        curspeed = spd->speed;
        if(curspeed == speed)
            return spd->bspeed;
        ++spd;
    }while(curspeed);
    ERRX(_("Wrong speed value: %d!"), speed);
    return 0;
}

/**
 * Exit & return terminal to old state
 * @param ex_stat - status (return code)
 */
void term_quit(int ex_stat){
    restore_console();
    restore_tty();
    ioctl(comfd, TCSANOW, &oldtty ); // return TTY to previous state
    close(comfd);
    restore_console();
    WARNX("Exit! (%d)\n", ex_stat);
    exit(ex_stat);
}

/**
 * Open & setup TTY, terminal
 */
void tty_init(){
    DBG("\nOpen port...\n");
    if ((comfd = open(comdev,O_RDWR|O_NOCTTY|O_NONBLOCK)) < 0){
        ERR(_("Can't use port %s\n"), comdev);
    }
    DBG(" OK\nGet current settings...\n");
    if(ioctl(comfd,TCGETA,&oldtty) < 0){ // Get settings
        ERR(_("Can't get old TTY settings"));
    }
    tty = oldtty;
    tty.c_lflag     = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    tty.c_oflag     = 0;
    tty.c_cflag     = BAUD_RATE|CS8|CREAD|CLOCAL; // 9.6k, 8N1, RW, ignore line ctrl
    tty.c_cc[VMIN]  = 0;  // non-canonical mode
    tty.c_cc[VTIME] = 5;
    if(ioctl(comfd, TCSETA, &tty) < 0){
        ERR(_("Can't apply new TTY settings"));
    }
    DBG(" OK\n");
}

void restore_tty(){
    return;
}

/**
 * getchar() without echo
 * wait until at least one character pressed
 * @return character readed
 *
int mygetchar(){
    int ret;
    do ret = read_console();
    while(ret == 0);
    return ret;
}*/

/**
 * read both tty & console
 * @param buff (o)    - buffer for messages readed from tty
 * @param length (io) - buff's length (return readed len or 0)
 * @param rb (o)      - byte readed from console or -1
 * @return 1 if something was readed here or there
 */
int read_tty_and_console(char *buff, size_t *length, int *rb){
    ssize_t L;
    // ssize_t l;
    size_t buffsz = *length;
    struct timeval tv;
    int sel, retval = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(STDIN_FILENO, &rfds);
    FD_SET(comfd, &rfds);
    tv.tv_sec = 0; tv.tv_usec = 10000;
    sel = select(comfd + 1, &rfds, NULL, NULL, &tv);
    if(sel > 0){
        if(FD_ISSET(STDIN_FILENO, &rfds)){
            *rb = getchar();
            retval = 1;
        }else{
            *rb = -1;
        }
        if(FD_ISSET(comfd, &rfds)){
            if((L = read(comfd, buff, buffsz)) < 1){ // disconnect or other troubles
                WARN(_("TTY error or disconnected!\n"));
            }else{
                // all OK continue reading
        /*      DBG("readed %zd bytes, try more.. ", L);
                buffsz -= L;
                while(buffsz > 0 && (l = read(comfd, buff+L, buffsz)) > 0){
                    L += l;
                    buffsz -= l;
                }
                DBG("full len: %zd\n", L); */
                *length = (size_t) L;
                retval = 1;
            }
        }else{
            *length = 0;
        }
    }
    return retval;
}

void con_sig(int rb){
    char cmd;
    if(rb < 1) return;
    if(rb == 'q') term_quit(0); // q == exit
    cmd = (char) rb;
    write(comfd, &cmd, 1);
    /*switch(rb){
        case 'h':
            help();
        break;
        default:
            cmd = (uint8_t) rb;
            write(comfd, &cmd, 1);
    }*/
}

/**
 * Open all TTY's from given lists
 * @param ports     - TTY device filename
 * @param speeds    - array of speeds or NULL
 * @param globspeed - common speed for all ports (if `speeds` not NULL)
 */
void ttys_open(char **ports, int **speeds, int globspeed){
    int commonspd = 0;
    if(!speeds) commonspd = conv_spd(globspeed);
    while(*ports){
        int spd = commonspd ? commonspd : conv_spd(**speeds);
        DBG("open %s with speed %d (%d)", *ports, commonspd ? globspeed : **speeds, spd);
        ;
        ++ports;
        if(!commonspd) ++speeds;
    }
}
