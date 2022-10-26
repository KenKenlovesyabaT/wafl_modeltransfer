#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>

enum {CMD_NAME, FILE_NAME, DST_IP, SEND_RECV_PORT};

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s file_name dst_ip send_recv_port\n", argv[CMD_NAME]);
        exit(EXIT_FAILURE);
    }

    // 乱数シード設定
    srand((unsigned)time(NULL));

    // 受信プログラムのfork, exec
    pid_t recv_pid = fork();
    if (recv_pid < 0) {
        perror("recv_pid:fork");
        exit(EXIT_FAILURE);
    } else if(recv_pid == 0) {
        // 子プロセスで別プログラムを実行
        printf("proc_recv started\n");
        execlp("/home/elab/udp/bin/recv_eth","/home/elab/udp/bin/recv_eth",argv[SEND_RECV_PORT],NULL);
        // 自宅pc用
        // execlp("/home/kentaro/kenkyu/wafl_modeltransfer/bin/recv","/home/kentaro/kenkyu/wafl_modeltransfer/bin/recv",argv[SEND_RECV_PORT],NULL);
        perror("receive");
        exit(EXIT_FAILURE);
    }

    /* 5~7秒のインターバル */
    // ここの演算がボトルネックになっている可能性？
    unsigned int wait_sec = (rand() % 3) + 7;

    // //受信開始から送信開始まで10秒待つ
    // unsigned int wait_sec = 10;
    sleep(wait_sec);

    // 送信プログラムのfork, exec
    pid_t send_pid = fork();
    if (send_pid < 0) {
        perror("send_pid:fork");
        exit(EXIT_FAILURE);
    } else if(send_pid == 0) {
        // 子プロセスで別プログラムを実行
        printf("proc_send started\n");
        execlp("/home/elab/udp/bin/send_eth","/home/elab/udp/bin/send_eth",argv[FILE_NAME],argv[DST_IP],argv[SEND_RECV_PORT],NULL);
        // 自宅pc用
        // execlp("/home/kentaro/kenkyu/wafl_modeltransfer/bin/send","/home/kentaro/kenkyu/wafl_modeltransfer/bin/send",argv[FILE_NAME],argv[DST_IP],argv[SEND_RECV_PORT],NULL);
        perror("send");
        exit(EXIT_FAILURE);
    }

    //親プロセス
    int status;
    for(int i = 0; i < 2; i++) {
        pid_t r = waitpid(-1, &status, 0); //子プロセスの終了待ち
        if (r < 0) {
            perror("send:waitpid");
            exit(EXIT_FAILURE);
        }

        if (r == send_pid) {
            printf("proc send exited\n");
            int kill_ret = kill(recv_pid, SIGUSR1);
            if (kill_ret == -1) {
                perror("kill");
                exit(EXIT_FAILURE);
            }
        } else if (r == recv_pid) {
            printf("proc recv exited\n");
        }

        if (WIFEXITED(status)) {
            // 子プロセスが正常終了の場合
            printf("child exit-code=%d\n", WEXITSTATUS(status));
        } else {
            printf("child status=%d\n", WEXITSTATUS(status));
        }
    }
    return EXIT_SUCCESS;
}
