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
#include <unistd.h>         // usleep
//#include <pthread.h>

#include "term.h"
#include "usefull_macros.h"

#define LOGBUFSZ (1024)

typedef struct {
    int speed;  // communication speed in bauds/s
    int bspeed; // baudrate from termios.h
} spdtbl;

typedef struct {
    char *portname;         // device filename (should be freed before structure freeing)
    int baudrate;           // baudrate (B...)
    struct termio oldtty;   // TTY flags for previous port settings
    struct termio tty;      // TTY flags for current settings
    int comfd;              // TTY file descriptor
    int logfd;              // log file descriptor
    char logbuf[LOGBUFSZ];  // buffer for data readed
    int logbuflen;          // length of data in logbuf
    char linerdy;           // flag of getting '\n' in input data
    //pthread_t thread;       // thread identificator for kill/join
} TTY_descr;

// global mutex for all threads writing to common log file / stdout
//static pthread_mutex_t write_mutex;
// array of opened TTY descriptors
static TTY_descr *descriptors = NULL;
// amount of opened descriptors
static int descr_amount = 0;
// common log fd
static int common_fd = 0;
// max value of comfd - for select()
static int maxfd = 0;
// name of common log file
static char *commonlogname = NULL;
// time of start
static double t0 = -10.;
// character mode
static int charmode = 0;

// in cmdlnopts.c
extern int rewrite_ifexists;

static int tty_init(TTY_descr *descr);
static void restore_ttys();
static void write_logblocks(char force);

/**
 * change value of common log filename
 */
void set_comlogname(char* nm){
    FREE(commonlogname);
    commonlogname = strdup(nm);
}

void set_charmode(){
    charmode = 1;
}

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
    restore_ttys();
    WARNX("Exit! (%d)\n", ex_stat);
    exit(ex_stat);
}

/**
 * Open & setup terminal
 * @param descr (io) - port descriptor
 * @return 0 if all OK
 */
static int tty_init(TTY_descr *descr){
    DBG("\nOpen port...");
    if ((descr->comfd = open(descr->portname, O_RDONLY|O_NOCTTY|O_NONBLOCK)) < 0){
        WARN(_("Can't use port %s"), descr->portname);
        return globErr ? globErr : 1;
    }
    DBG("OK\nGet current settings...");
    if(ioctl(descr->comfd, TCGETA, &descr->oldtty) < 0){ // Get settings
        WARN(_("Can't get old TTY settings"));
        return globErr ? globErr : 1;
    }
    descr->tty = descr->oldtty;
    struct termio  *tty = &descr->tty;
    tty->c_lflag     = 0; // ~(ICANON | ECHO | ECHOE | ISIG)
    tty->c_oflag     = 0;
    tty->c_cflag     = descr->baudrate|CS8|CREAD|CLOCAL; // 9.6k, 8N1, RW, ignore line ctrl
    tty->c_cc[VMIN]  = 0;  // non-canonical mode
    tty->c_cc[VTIME] = 5;
    if(ioctl(descr->comfd, TCSETA, &descr->tty) < 0){
        WARN(_("Can't apply new TTY settings"));
        return globErr ? globErr : 1;
    }
    if(descr->comfd >= maxfd) maxfd = descr->comfd + 1;
    DBG("OK");
    return 0;
}

/**
 * Restore all opened TTYs to previous state, close them and free all memory
 * occupied by their descriptors
 */
static void restore_ttys(){
    FNAME();
    write_logblocks(1); // write rest of data
    for(int i = 0; i < descr_amount; ++i){
        TTY_descr *d = &descriptors[i];
        DBG("%dth TTY: %s", i, d->portname);
        FREE(d->portname);
        if(!d->comfd) continue; // not opened
        DBG("close file..");
        ioctl(d->comfd, TCSANOW, &d->oldtty); // return TTY to previous state
        close(d->comfd);
        DBG("close log file..");
        if(d->logfd > 0)
            close(d->logfd);
        DBG("done!\n");
    }
    FREE(descriptors);
    descr_amount = 0;
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
 *
static int read_tty_and_console(char *buff, size_t *length, int *rb){
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
        *      DBG("readed %zd bytes, try more.. ", L);
                buffsz -= L;
                while(buffsz > 0 && (l = read(comfd, buff+L, buffsz)) > 0){
                    L += l;
                    buffsz -= l;
                }
                DBG("full len: %zd\n", L); *
                *length = (size_t) L;
                retval = 1;
            }
        }else{
            *length = 0;
        }
    }
    return retval;
}*/
/*
static void con_sig(int rb){
    char cmd;
    if(rb < 1) return;
    if(rb == 'q') term_quit(0); // q == exit
    cmd = (char) rb;
    write(comfd, &cmd, 1);
    *switch(rb){
        case 'h':
            help();
        break;
        default:
            cmd = (uint8_t) rb;
            write(comfd, &cmd, 1);
    }*
}*/

/**
 * Create log file (open in exclusive mode: error if file exists)
 * @param  descr - device descriptor
 * @return fd of opened file if all OK, 0 in case of error
 */
int create_log(TTY_descr *descr){
    int fd;
    char fdname[256], *filedev;
    if(!(filedev = strrchr(descr->portname, '/'))) filedev = descr->portname;
    else{
        ++filedev;
        if(!*filedev) filedev = descr->portname;
    }
    snprintf(fdname, 256, "log_%s.txt", filedev);
    int oflag = O_WRONLY | O_CREAT;
    if(rewrite_ifexists) oflag |= O_TRUNC;
    else oflag |= O_EXCL;
    if ((fd = open(fdname, oflag,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1){
        WARN("open(%s) failed", fdname);
        return 0;
    }
    DBG("%s opened", fdname);
    descr->logfd = fd;
    return fd;
}

/**
 * Open terminal & do initial setup
 * @param port - port device filename
 * @param spd  - speed (for ioctl TCSETA)
 * @return pointer to descriptor if all OK or NULL in case of error
 */
static TTY_descr *prepare_tty(TTY_descr *descr){
    if(!descr->portname) return NULL;
    if(tty_init(descr)){
        WARNX(_("Can't open device %s"), descr->portname);
        return NULL;
    }
    if(!create_log(descr)) return NULL;
    return descr;
}

/**
 * read all TTYs, put data into linebuffers & store full lines in log file
 * @return 1 if any buffer became full
 */
static int read_ttys(){
    struct timeval tv;
    int i, sel, retval = 0;
    fd_set rfds;
    FD_ZERO(&rfds);
    for(i = 0; i < descr_amount; ++i)
        FD_SET(descriptors[i].comfd, &rfds);
    // wait no more than 10ms
    tv.tv_sec = 0; tv.tv_usec = 10000;
    sel = select(maxfd, &rfds, NULL, NULL, &tv);
    TTY_descr *d = descriptors;
    if(sel > 0) for(i = 0; i < descr_amount; ++i, ++d){
        int bsyctr = 0;
        if(FD_ISSET(d->comfd, &rfds)){
            size_t L = d->logbuflen;
            //DBG("%s got data (have %zd)",  d->portname, L);
            char *bufptr = d->logbuf + L;
            while(L < LOGBUFSZ){
                usleep(1000);
                if(read(d->comfd, bufptr, 1) < 1){ // disconnect or other troubles
                    if(errno == EAGAIN){ // file was blocked outside
                        if(bsyctr++ < 10) continue;
                        break;
                    }
                    WARN(_("Some error or %s disconnected"), d->portname);
                    break;
                }else{
                    bsyctr = 0;
                    // all OK continue reading
                    ++L;
                    if(*bufptr++ == '\n'){ // line ready
                        d->linerdy = 1;
                        retval = 1;
                        break;
                    }
                }
            }
            descriptors[i].logbuflen = L;
            if(L == LOGBUFSZ || charmode) retval = 1; // buffer is full - write data to logs
        }
    }
    return retval;
}

/**
 * Open all TTY's from given lists & start monitoring
 * @param ports     - TTY device filename
 * @param speeds    - array of speeds or NULL
 * @param globspeed - common speed for all ports (if `speeds` not NULL)
 */
void ttys_open(char **ports, int **speeds, int globspeed){
    int commonspd = 0, N = 0;
    if(!speeds) commonspd = conv_spd(globspeed);
    // count amount of ports to open
    char **p = ports;
    for(descr_amount = 0; *p; ++descr_amount, ++p);
    DBG("User wanna open %d descriptors", descr_amount);
    descriptors = MALLOC(TTY_descr, descr_amount); // allocate memory for descriptors array
    while(*ports){
        int spd = commonspd ? commonspd : conv_spd(**speeds);
        DBG("open %s with speed %d (%d)", *ports, commonspd ? globspeed : **speeds, spd);
        TTY_descr *cur_descr = &descriptors[N++];
        cur_descr->portname = strdup(*ports);
        cur_descr->baudrate = spd;
        if(!prepare_tty(cur_descr)) term_quit(globErr);
        ++ports;
        if(!commonspd) ++speeds;
    }
    if(commonlogname){ // open common log file - non-critical
        int oflag = O_WRONLY | O_CREAT;
        if(rewrite_ifexists) oflag |= O_TRUNC; // truncate if -r passed
        if ((common_fd = open(commonlogname, oflag,
            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH )) == -1)
                WARN("open(%s) failed", commonlogname);
    }
    // start monitoring
    while(1){
        if(read_ttys()) write_logblocks(0);
    }
}

/**
 * Write all received data into log files
 * @param force == 1 to write all data (@ exit)
 */
static void write_logblocks(char force){
    TTY_descr *d = descriptors;
    char tmbuf[256];
    if(t0 < 0.) t0 = dtime();
    double twr = dtime() - t0;
    for(int i = 0; i < descr_amount; ++i, ++d){
        if(charmode || (force && d->logbuflen) || d->linerdy || d->logbuflen == LOGBUFSZ){
            // write trailing '\n' if `force` active
            int writen = ((force || charmode) && !d->linerdy) ? 1 : 0;
            size_t L = snprintf(tmbuf, 256, "%g\n", twr);
            write(d->logfd, tmbuf, L);
            write(d->logfd, d->logbuf, d->logbuflen);
            if(writen) write(d->logfd, "\n", 1);
            L = snprintf(tmbuf, 256, "%g: %s\n", twr, d->portname);
            write(1, tmbuf, L);
            write(1, d->logbuf, d->logbuflen);
            if(writen) write(1, "\n", 1);
            if(common_fd > 0){
                write(common_fd, tmbuf, L);
                write(common_fd, d->logbuf, d->logbuflen);
                if(writen) write(common_fd, "\n", 1);
            }
            d->linerdy = 0;
            d->logbuflen = 0;
        }
    }
}
