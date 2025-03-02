/**
 * @file main.c
 * @author leeotus (leeotus@163.com)
 * @brief A simple example of io_uring
 * @date 2025-03-01
 */

#include <stdio.h>
#include <liburing.h>

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <unistd.h>

#define SERV_LISTEN_NUM 5
#define ENTRIES_LENGTH      4096

enum {
    READ,
    WRITE,
    ACCEPT,
};

struct conninfo {
    int connfd;
    int type;
};

/**
 * @brief 设置服务器端的accept状态
 * @param r io_uring对象
 * @param servfd 服务器的文件描述符
 * @param clnt_addr 之后需要保存的客户端的地址
 * @param clnt_len 客户端地址对象的长度
 * @param flag 标志符
 * @note 参考accept函数,io_uring_prep_accept函数其实就是封装好的io_uring的accept函数
 */
void set_accept_event(struct io_uring *r, int servfd,
                      struct sockaddr_in *clnt_addr, socklen_t *clnt_len,
                      int flag);

/**
 * @brief 设置客户端接入的服务器recv函数
 * @param ring io_uring对象
 * @param sockfd 客户端的文件描述符
 * @param buf 接收客户端数据的缓冲区
 * @param len 缓冲区buf的长度
 * @param flags 标志位
 * @note 客户端的文件描述符被保存在cq成员里面的res字段中
 */
void set_recv_event(struct io_uring* ring, int sockfd, void *buf, size_t len, int flags);

void set_send_event(struct io_uring *ring, int fd, const void* buf, size_t len, int flags);

int main(int argc, char **argv) {
    int ret;
    char buf[1024];
    struct sockaddr_in clnt_addr;
    socklen_t clint_addr_len = sizeof(clnt_addr);
    struct sockaddr_in serv_addr;
    int listenfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listenfd == -1) {
        perror("socket error!");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9999);

    ret = bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret == -1) {
        perror("bind() error!");
        exit(EXIT_FAILURE);
    }

    listen(listenfd, SERV_LISTEN_NUM);

    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    // TODO: 设置io_uring参数

    struct io_uring ring;  // 创建io_uring对象
    /**
     * @brief 设置io_uring对象内的SQ和CQ队列
     * @param entries 指定I/O uring队列的条目数,即可以同时处理的I/O请求的数量
     * @param ring 指向io_uring结构的指针,用于存储初始化之后的I/O uring信息
     * @param params 指向io_uring_params结构的指针,用于设置I/O uring的参数
     */
    io_uring_queue_init_params(ENTRIES_LENGTH, &ring, &params);

    socklen_t clnt_len = sizeof(clnt_addr);
    set_accept_event(&ring, listenfd, &clnt_addr, &clnt_len, 0);

    while (1) {
        struct io_uring_cqe *cqe;
        io_uring_submit(&ring);
        int ret = io_uring_wait_cqe(&ring, &cqe);

        struct io_uring_cqe *cqes[10];
        int cqecount = io_uring_peek_batch_cqe(&ring, cqes, 10);  // 类似于epoll

        int i = 0;
        unsigned cnt=0;
        for (; i < cqecount; ++i) {
            cqe = cqes[i];
            cnt+=1;
            struct conninfo ci;
            memcpy(&ci, &cqe->user_data, sizeof(ci));

            if (ci.type == ACCEPT) {
                // 需要进行accept操作的fd
                int connfd =
                    cqe->res;  // 来自客户端的文件描述符被放在完成队列成员的res字段中
                set_recv_event(&ring, connfd, buf, 1024, 0);
            } else if (ci.type == READ) {
                int bytes_read = cqe->res;  // 获取读取到的字节数
                if(bytes_read == 0) {
                    close(ci.connfd);
                } else if(bytes_read < 0) {
                    //TODO
                }
                printf("buffer: %s\r\n", buf);  // for test
                set_send_event(&ring, ci.connfd, buf, bytes_read, 0);
            } else if(ci.type == WRITE) {
                set_recv_event(&ring, ci.connfd, buf, 1024,0);
            }
        }
        io_uring_cq_advance(&ring, cnt);
    }

    return 0;
}

void set_accept_event(struct io_uring *r, int servfd,
                      struct sockaddr_in *clnt_addr, socklen_t *clnt_len,
                      int flag) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(r);
    io_uring_prep_accept(sqe, servfd, (struct sockaddr *)clnt_addr, clnt_len,
                         flag);

    struct conninfo ci = {.connfd = servfd, .type = ACCEPT};
    memcpy(&sqe->user_data, &ci, sizeof(struct conninfo));
}

void set_recv_event(struct io_uring *ring, int fd, void *buf, size_t len,
                    int flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    struct conninfo accept_info = {         // 接收客户端的fd,并且准备将其注册到io_uring中去
        .connfd = fd,
        .type = READ
    };
    io_uring_prep_recv(sqe, fd, buf, len, flags);
    memcpy(&sqe->user_data, &accept_info, sizeof(accept_info));
}

void set_send_event(struct io_uring *ring, int fd, const void *buf, size_t len,
                    int flags) {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    struct conninfo ci = {
        .connfd = fd,
        .type = WRITE
    };

    io_uring_prep_send(sqe, fd, buf, len, flags);
    memcpy(&sqe->user_data, &ci, sizeof(struct conninfo));
}
