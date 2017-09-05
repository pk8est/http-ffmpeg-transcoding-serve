#include <stdio.h>
#include <libavutil/time.h>

#include "server.h"
#include "ffmpeg.h"

#define STDIN   0
#define STDOUT  1
#define STDERR  2
#define BLOCK_SIZE 4096

char *file_path = "/mnt/hgfs/web/c++/http-ffmpeg-transocding/build%s";

void http_transcoding_handler(int client, const char *path, const char *method, const char *query_string)
{
    printf("【method=%s, query_string=%s】path=%s;\n", method, query_string, path);

    int ret;
    int pfds[2];
    pid_t pid;

    ret = pipe(pfds);
    if (ret< 0) {
        cannot_execute(client);
        return;
    }

    pid = fork();
    if(pid < 0){
        cannot_execute(client);
        return;
    }else if(pid == 0){
        dup2(pfds[1], STDOUT);
        close(pfds[0]);
        av_log_set_level(AV_LOG_ERROR);
        create_trans_task(path, "pipe:");
        return ;
    }else{
        
        write_ts_header(client);
        close(pfds[1]);
        size_t bytes;
        char buffer[BLOCK_SIZE];

        while ((ret = read(pfds[0], buffer, sizeof(buffer))) > 0){
            send(client, buffer, ret, 0);
        }
        shutdown(client, SHUT_RDWR);
        close(pfds[0]);
    }



    printf("transcoding end!\n");
    return(0);
}


int run_transcoding() {
    int ret;
    int64_t ti; 
    char *inputFile = "./build/input.mp4";
    char *outputFile = "./build/output.ts";
    set_log_level(VERBOSE);
    ti = av_gettime_relative();
    //ret = create_trans_task(inputFile, "pipe:");
    ret = create_trans_task(inputFile, outputFile);
    if (ret < 0) {
        printf("fail!\n");
    }
    av_log(NULL, AV_LOG_INFO, "bench: utime=%0.3fs\n", (av_gettime_relative() - ti) / 1000000.0);
    return 0;
}

int main(int argc, char **argv){
    u_short port = 4000;
    //run_server(port, http_transcoding_handler);
    run_transcoding();
    return 0;
}