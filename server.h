#pragma once
#ifndef _SERVER_H_
#define _SERVER_H_

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdint.h>

extern char *file_path;

void accept_request(void *);
void error_die(const char *);
int get_line(int, char *, int);
void not_found(int);
int startup(u_short *);
void unimplemented(int);
int run_server(u_short port, void (*handler)(int client, const char *path, const char *method, const char *query_string));

#endif