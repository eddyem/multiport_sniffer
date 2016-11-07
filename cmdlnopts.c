/*
 * cmdlnopts.c - the only function that parse cmdln args and returns glob parameters
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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "cmdlnopts.h"
#include "usefull_macros.h"

/*
 * here are global parameters initialisation
 */
int help;
glob_pars  G;

int rewrite_ifexists = 0.; // rewrite existing files == 0 or 1

//            DEFAULTS
// default global parameters
glob_pars const Gdefault = {
    NULL,           // list of input ports
    NULL,           // speeds of interfaces (if differs)
    0,              // number of rest parameters
    57600,          // common speed for all terminals
    NULL,           // name of common log file (dublicate of stdout)
    NULL,           // the rest parameters: array of char*
    0               // use character mode instead of lines
};

/*
 * Define command line options by filling structure:
 *  name    has_arg flag    val     type        argptr          help
*/
myoption cmdlnopts[] = {
    // set 1 to param despite of its repeating number:
    {"help",    NO_ARGS,    NULL,   'h',    arg_int,    APTR(&help),        _("show this help")},
    {"port",    MULT_PAR,   NULL,   'p',    arg_string, APTR(&G.ports),     _("input port (also you can name it without any key)")},
    {"totalrate",NEED_ARG,  NULL,   't',    arg_int,    APTR(&G.glob_spd),  _("global baudrate for all interfaces")},
    {"baudrate",MULT_PAR,   NULL,   'b',    arg_int,    APTR(&G.speeds),    _("baudrate for given port")},
    {"all-log", NEED_ARG,   NULL,   'o',    arg_string, APTR(&G.commonlog), _("filename of common log")},
    {"rewrite", NO_ARGS,    NULL,   'r',    arg_none,   APTR(&rewrite_ifexists),_("rewrite existing log files")},
    {"char-mode",NO_ARGS,   NULL,   'c',    arg_none,   APTR(&G.charmode),  _("use character mode instead of lines")},
    end_option
};

/**
 * Parse command line options and return dynamically allocated structure
 *      to global parameters
 * @param argc - copy of argc from main
 * @param argv - copy of argv from main
 * @return allocated structure with global parameters
 */
glob_pars *parse_args(int argc, char **argv){
    int i;
    void *ptr;
    ptr = memcpy(&G, &Gdefault, sizeof(G)); assert(ptr);
    // format of help: "Usage: progname [args]\n"
    change_helpstring("Usage: %s [args] [ports]\n\n\tWhere args are:\n");
    // parse arguments
    parseargs(&argc, &argv, cmdlnopts);
    if(help) showhelp(-1, cmdlnopts);
    if(argc > 0){
        G.rest_pars_num = argc;
        G.rest_pars = calloc(argc, sizeof(char*));
        for (i = 0; i < argc; i++)
            G.rest_pars[i] = strdup(argv[i]);
    }
    return &G;
}

