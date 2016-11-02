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
#include <signal.h>
#include "term.h"
#include "usefull_macros.h"
#include "cmdlnopts.h"

#define BUFLEN 1024

void signals(int sig){
    term_quit(sig);
}


int main(int argc, char *argv[]){
    /*
    if(argc == 2){
        fout = fopen(argv[1], "a");
        if(!fout){
            perror("Can't open output file");
            exit(-1);
        }
        setbuf(fout, NULL);
    }*/
    initial_setup();
    glob_pars *Glob = parse_args(argc, argv);
    if(Glob->glob_spd != 57600 && !Glob->speeds){ // user gave global speed -> test it
        conv_spd(Glob->glob_spd);
        Glob->speeds = NULL; // glob_spd have bigger priority
    }
    // check free parameters & add them to ports' names
    if(Glob->rest_pars_num){
        int gpamount, i, curno;
        for(gpamount = 0; Glob->ports && Glob->ports[gpamount]; ++gpamount){ // count Glob->ports
            DBG("have by '-p': %s", Glob->ports[gpamount]);
        }
        curno = gpamount;
        gpamount += Glob->rest_pars_num;
        // now allocate memory: gpamount + rest_pars_num + 1 for terminating NULL
        Glob->ports = realloc(Glob->ports, gpamount + 1);
        if(!Glob->ports) ERR("Realloc");
        char **ptr = Glob->rest_pars;
        //gpamount; // for terminating NULL
        for(i = curno; i < gpamount; ++i){
            DBG("Add by free param (#%d): %s", i, *ptr);
            Glob->ports[i] = *ptr++;
        }
        Glob->ports[gpamount] = NULL;
    }
    if(!Glob->ports)
        ERRX(_("You should give at least name of one port"));
    setup_con();
    signal(SIGHUP,  signals);
    signal(SIGTERM, signals);   // kill (-15)
    signal(SIGINT,  signals);   // ctrl+C
    signal(SIGQUIT, signals);   // ctrl+\   .
    signal(SIGTSTP, SIG_IGN);   // ctrl+Z
    setbuf(stdout, NULL);
    // now, if user gave speeds different to each port, test their amount
    if(Glob->speeds){
        char **str = Glob->ports;
        int **spd = Glob->speeds;
        int portsamount = 0, bdramount = 0;
        for(bdramount = 0; *spd; ++bdramount, ++spd){
            DBG("speed #%d: %d", bdramount, **spd);
        }
        for(portsamount = 0; *str; ++portsamount, ++str){
            DBG("port #%d: %s", portsamount, *str);
        }
        if(bdramount != portsamount)
            ERRX(_("Amount of ports given, %d, not equal to amount of speeds given, %d!\n"),
                portsamount, bdramount);
    }
    // now run threads
    ttys_open(Glob->ports, Glob->speeds, Glob->glob_spd);
    /*
    double t0 = dtime();
    while(1){
        L = BUFLEN;
        if(read_tty_and_console(buff, &L, &rb)){
            if(rb > 0){
                con_sig(rb);
                oldcmd = rb;
            }
            if(L){
                buff[L] = 0;
                printf("%s", buff);
                if(fout){
                    copy_buf_to_file(buff, &oldcmd);
                }
            }
        }
    }*/
    term_quit(0);
    return 0;
}
