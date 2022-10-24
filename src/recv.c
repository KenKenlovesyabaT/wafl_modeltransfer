#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <assert.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/select.h>
#include <signal.h>

enum {CMD_NAME, PORT};

#define FRAGMENT_SIZE 1024
#define MAXBUFF 4096 /* バッファの最大値 */
#define INTERVAL 1*1e3 /* UDPパケット送信間隔(単位:microsec) */
#define PACKET_NUM 400 /* 想定する受信パケット数 */
#define TIMEOUT_SEC 60 /* タイムアウト時間(selectの停止はmainからのkill signalで行う) */
#define NODE_NUM 10 /* 実験時のノードの個数 */


//sigusr1用シグナルハンドラ関数
void
sigusr1_handler(int sig)
{
  printf("signal called\n");
}

int main(int argc, char *argv[]) {
    struct sockaddr_in receive_sa; /* パケットを待ち受けるアドレスの情報 */
    struct sockaddr_in send_sa; /* パケットを送ってきたアドレスの情報 */
    int sd; /* パケット受信用ソケットディスクリプタ */
    int max_sd; /* ソケットディスクリプタの最大値(監視するソケットディスクリプタの数) */
    char buff[MAXBUFF]; /* パケット受信用バッファ */
    struct timeval tv; /* selectのタイムアウト時間を定める構造体 */
    fd_set readfd; /* selectするファイルディスクリプタ */
    char *p; /* ヘッダの開始位置を示す作業用ポインタ */
    struct ether_header *eth; /* Ethernetヘッダ構造体 */
    struct ip *ip; /* ipヘッダ構造体 */
    struct udphdr *udp; /* udpヘッダ構造体 */
    int i = 0;
    int n;
    uint32_t *fragment_p;
    uint32_t *epoch_p;
    char *send_msg;
    int zero = 0;
    socklen_t addrlen;
    uint8_t self_id;
    uint8_t node_id;
    time_t t;
    struct tm* tmp;
    char date[25];
    char filename[200];
    FILE *log_fp;
    struct ifreq ifr;
    
    if (argc != 2) {
        fprintf(stderr, "usage: %s port\n", argv[CMD_NAME]);
        exit(EXIT_FAILURE);
    }

    // sigusr1を受け取ることでselectのブロックを解除することができる
    // シグナルハンドラを登録することでアプリの終了ではなくブロック解除を実現
    signal(SIGUSR1, sigusr1_handler);

    /* 受信用UDPソケットのオープン */
    if((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket(SOCK_DGRAM)");
        exit(EXIT_FAILURE);
    }

    /* ソケットのbind */
    memset((char *)&receive_sa, 0, sizeof(receive_sa));
    receive_sa.sin_family = AF_INET;
    receive_sa.sin_addr.s_addr = htonl(INADDR_ANY);
    receive_sa.sin_port = htons(atoi(argv[PORT]));
    if((bind(sd, (struct sockaddr *)&receive_sa, sizeof(receive_sa))) < 0) {
        printf("error:bind\n");
        perror("bind");
        exit(EXIT_FAILURE);
    }

    /* 自ip addrの取得(ログファイルに書かないため) */
    ifr.ifr_addr.sa_family = AF_INET;
    // strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
    // 自宅pc用
    strncpy(ifr.ifr_name, "wlp2s0", IFNAMSIZ-1);
    ioctl(sd, SIOCGIFADDR, &ifr);
    struct sockaddr_in *tmp_ifr = (struct sockaddr_in *) &ifr.ifr_addr;

    /* self_id = 自分のIPアドレス下位1オクテット - 1 */
    self_id = (uint8_t)(ntohl(tmp_ifr->sin_addr.s_addr) & 0x000000ff); 
    self_id -= 1;

    /* p,fragment_p,epoch_pの設定 */
    p = buff;
    fragment_p = (uint32_t *)p;
    epoch_p = fragment_p + 1;


    /* ログファイルのオープン */
    t = time(NULL);
    tmp = localtime(&t);
    if (tmp == NULL) {
        printf("error:localtime\n");
        perror("localtime");
        exit(EXIT_FAILURE);
    }

    // 日付をdateに出力
    if (strftime(date, sizeof(date) - 1, "mon%m_d%d_h%H_min%M", tmp) == 0) {
        printf("error:strftime\n");
        perror("strftime");
        exit(EXIT_FAILURE);
    }

    // 絶対パスを生成
    if (sprintf(filename, "/home/elab/udp/log/recv_n%d_%s.txt", self_id, date) < 0) {
    // 自宅pc用
    // if (sprintf(filename, "/home/kentaro/kenkyu/wafl_modeltransfer/log/recv_n%d_%s.txt", self_id, date) < 0) {
        printf("error:sprintf\n");
        perror("sprintf");
        exit(EXIT_FAILURE);
    }

    log_fp = fopen(filename, "w");

    if(log_fp < 0) {
        printf("error:fopen(filename)\n");
        perror("fopen(filename)");
        exit(EXIT_FAILURE);
    }

    /* ファイルの分割受信 */
    while(1) {

        /* selectのタイムアウトの設定 */
        tv.tv_sec = TIMEOUT_SEC;
        tv.tv_usec = 0;

        /* selectで検査するディスクリプタの設定 */
        FD_ZERO(&readfd);
        FD_SET(sd, &readfd);
        if (select(sd + 1, &readfd, NULL, NULL, &tv) <= 0) {
            printf("sig sent!\n");
            break;
        }

        memset(buff, 0, MAXBUFF);
        addrlen = sizeof(send_sa);
        n = recvfrom(sd, buff, MAXBUFF, 0, (struct sockaddr *) &send_sa, &addrlen);
        // printf("%d\n",n);
        if(n < 0) {
        printf("error:recvfrom\n");
        perror("recvfrom");
        exit(EXIT_FAILURE);
        } 

        /* 受信パケットの解析 */
        /* 受信したパケットが自分のものだったらログに書き込まない */
        if (ntohl(send_sa.sin_addr.s_addr) == ntohl(tmp_ifr->sin_addr.s_addr)) {
            continue;
        }


        /* node_id = パケット送信元のIPアドレス下位1オクテット - 1 */
        node_id = (uint8_t)(ntohl(send_sa.sin_addr.s_addr) & 0x000000ff); 
        node_id -= 1;

        fprintf(log_fp, "%u,%u,%u\n", node_id, ntohl(*epoch_p), ntohl(*fragment_p));
        // fprintf(log_fp, "%s,%u,%u\n", inet_ntoa(send_sa.sin_addr), ntohl(*epoch_p), ntohl(*fragment_p));
        fflush(log_fp);
    }
    close(sd);
    fclose(log_fp);

    return EXIT_SUCCESS;

}
