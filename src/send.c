#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <net/if.h>

enum {CMD_NAME, FILE_NAME, DST_IP, PORT};

#define FRAGMENT_SIZE 1024
#define MAXBUFF FRAGMENT_SIZE+8
#define PACKET_NUM 400
#define EPOCH_NUM 10
#define INTERVAL 1*1e2 /* UDPパケット送信間隔(単位:microsec) */

int main(int argc, char *argv[]) {
    struct sockaddr_in send_sa; /* 送信先のアドレス情報(broadcastを想定) */
    int sd; /* 送信用ソケットディスクリプタ */
    int fd; /* ファイルディスクリプタ */
    char send_buff[MAXBUFF]; /* パケット送信用バッファ */
    struct timeval tv; /* 遅延計測用構造体 */
    int option_value = 1;
    uint32_t *fragment_p;
    uint32_t *epoch_p;
    char *send_msg;
    time_t t;
    struct tm* tmp;
    uint8_t self_id;
    char date[25];
    char filename[200];
    FILE *log_fp;
    struct ifreq ifr;

    if (argc != 4) {
        fprintf(stderr, "usage: %s file_name dst_ip port\n", argv[CMD_NAME]);
        exit(EXIT_FAILURE);
    }

    // 乱数シード設定
    srand((unsigned)time(NULL));

    /* 送信先アドレスの設定 */
    send_sa.sin_family = AF_INET;
    send_sa.sin_port = htons(atoi(argv[PORT]));
    send_sa.sin_addr.s_addr = inet_addr(argv[DST_IP]);

    /* 送信用UDPソケットのオープン */
    if((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket(SOCK_DGRAM)");
        exit(EXIT_FAILURE);
    }

    /* UDPソケットのブロードキャスト設定 */
    if(setsockopt(sd, SOL_SOCKET, SO_BROADCAST, (void *)&option_value, sizeof(int)) < 0) {
        perror("setsocket(SO_BROADCAST)");
        exit(EXIT_FAILURE);
    }

    /* ログファイルのオープン */
    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        perror("localtime");
        exit(EXIT_FAILURE);
    }

    /* 自ip addrの取得(ログファイルに書かないため) */
    ifr.ifr_addr.sa_family = AF_INET;
    // strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
    // 自宅pc用j
    strncpy(ifr.ifr_name, "wlp2s0", IFNAMSIZ-1);
    ioctl(sd, SIOCGIFADDR, &ifr);
    struct sockaddr_in *tmp_ifr = (struct sockaddr_in *) &ifr.ifr_addr;

    /* self_id = 自分のIPアドレス下位1オクテット - 1 */
    self_id = (uint8_t)(ntohl(tmp_ifr->sin_addr.s_addr) & 0x000000ff); 
    self_id -= 1;

    // 日付をdateに出力
    // if (strftime(date, sizeof(date) - 1, "%m-%d-%H-%M", tmp) == 0) {
    if (strftime(date, sizeof(date) - 1, "mon%m_d%d_h%H_min%M", tmp) == 0) {
        perror("strftime");
        exit(EXIT_FAILURE);
    }

    // 絶対パスを生成
    // if (sprintf(filename, "/home/elab/udp/log/send-n%d-%s.txt", self_id, date) < 0) {
    // 自宅pc用
    if (sprintf(filename, "/home/kentaro/kenkyu/wafl_modeltransfer/log/send_n%d_%s.txt", self_id, date) < 0) {
        perror("sprintf");
        exit(EXIT_FAILURE);
    }

    log_fp = fopen(filename, "w");

    if(log_fp < 0) {
        perror("fopen(filename)");
        exit(EXIT_FAILURE);
    }

    /* ファイルの分割送信 */
    fragment_p = (uint32_t *)send_buff;
    epoch_p = fragment_p + 1;
    send_msg = send_buff + (2*sizeof(uint32_t));

    for(int epoch=0; epoch < EPOCH_NUM; epoch++) {
        /* ファイルのオープン */
        fd = open(argv[FILE_NAME], O_RDONLY);
        if(fd < 0) {
            perror("fopen(argv[FILE_NAME])");
            exit(EXIT_FAILURE);
        }

        /* fragment_numの初期化 */
        int fragment_num = 0;

        /* 5~7秒のインターバル */
        unsigned int wait_sec = (rand() % 3) + 5;

        while(1) {
            *fragment_p = htonl(fragment_num);
            *epoch_p = htonl(epoch);
            int n = read((int)fd, send_msg, FRAGMENT_SIZE);
            if(n <= 0) break;
            if((sendto(sd, send_buff, n+8, 0, (struct sockaddr *)&send_sa, sizeof send_sa)) < 0) {
                perror("sendto");
                exit(EXIT_FAILURE);
            }
            // printf("epoch %d: sent fragment %d.\n", epoch ,fragment_num);
            fragment_num++;
            usleep(INTERVAL);
        }
        fprintf(log_fp,"epoch %d: transmission completed.\n", epoch);
        fflush(log_fp);
        close(fd);
        sleep(wait_sec);
    }
    fclose(log_fp);
    return EXIT_SUCCESS;
}