#define rlogEnable 1               // 是否使能日志
#define rlogColorEnable 1          // 是否使能日志颜色
#define rlogLevel (rlogLvlWarning) // 日志打印等级
#define rlogTag "W5500Ping"        // 日志tag

#include "RyanW5500Store.h"

#define Sn_PROTO(ch) (0x001408 + (ch << 5))

#define WIZ_PING_DATA_LEN 32
#define WIZ_PING_HEAD_LEN 8

#define WIZ_PING_PORT 3000
#define WIZ_PING_REQUEST 8
#define WIZ_PING_REPLY 0
#define WIZ_PING_CODE 0
#define WIZ_PING_DELAY (1000)
#define WIZ_PING_TIMEOUT (2000)

struct wiz_ping_msg
{
    uint8_t type;                   // 0 - Ping Reply, 8 - Ping Request
    uint8_t code;                   // Always 0
    uint16_t check_sum;             // Check sum
    uint16_t id;                    // Identification
    uint16_t seq_num;               // Sequence Number
    int8_t data[WIZ_PING_DATA_LEN]; // Ping Data  : 1452 = IP RAW MTU - sizeof(type+code+check_sum+id+seq_num)
};

/**
 * @brief 计算字符串校验值
 *
 * @param src
 * @param len
 * @return uint16_t
 */
static uint16_t wiz_checksum(uint8_t *src, uint32_t len)
{
    uint16_t sum = 0,
             tsum = 0,
             i = 0,
             j = 0;
    uint32_t lsum = 0;

    j = len >> 1;
    lsum = 0;

    for (i = 0; i < j; i++)
    {
        tsum = src[i * 2];
        tsum = tsum << 8;
        tsum += src[i * 2 + 1];
        lsum += tsum;
    }

    if (len % 2)
    {
        tsum = src[i * 2];
        lsum += (tsum << 8);
    }

    sum = lsum;
    sum = ~(sum + (lsum >> 16));
    return (uint16_t)sum;
}

static int wiz_ping_request(int socket)
{
    uint16_t tmp_checksum = 0;
    int idx = 0,
        send_len = 0;
    struct wiz_ping_msg ping_req = {0};

    // 设置请求ping消息对象
    ping_req.type = WIZ_PING_REQUEST;
    ping_req.code = WIZ_PING_CODE;
    ping_req.id = htons(rand() % 0xffff);
    ping_req.seq_num = htons(rand() % 0xffff);
    for (idx = 0; idx < WIZ_PING_DATA_LEN; idx++)
        ping_req.data[idx] = (idx) % 8;

    ping_req.check_sum = 0;
    // 计算请求ping消息校验值
    tmp_checksum = wiz_checksum((uint8_t *)&ping_req, sizeof(ping_req));
    ping_req.check_sum = htons(tmp_checksum);

    // 发送请求ping消息
    send_len = wiz_send(socket, &ping_req, sizeof(ping_req), 0);
    if (send_len != sizeof(ping_req))
        return -1;

    return send_len - WIZ_PING_HEAD_LEN;
}

static int wiz_ping_reply(int socket, struct sockaddr *from)
{

    uint8_t recv_buf[WIZ_PING_HEAD_LEN + WIZ_PING_DATA_LEN + 1] = {0};
    uint16_t tmp_checksum = 0;
    int recv_len = 0,
        idx = 0;
    struct wiz_ping_msg ping_rep = {0};
    platformTimer_t pingReplyTimer = {0};

    platformTimerCutdown(&pingReplyTimer, WIZ_PING_TIMEOUT);
    while (1)
    {
        if (0 == platformTimerRemain(&pingReplyTimer))
            return -1;

        struct sockaddr *sin = (struct sockaddr *)from;
        socklen_t addr_len = sizeof(struct sockaddr_in);
        recv_len = wiz_recvfrom(socket, recv_buf, WIZ_PING_HEAD_LEN + WIZ_PING_DATA_LEN, 0, sin, &addr_len);
        if (recv_len < 0)
            return -1;
        break;
    }

    switch (recv_buf[0])
    {
    case WIZ_PING_REPLY:
        ping_rep.type = recv_buf[0];
        ping_rep.code = recv_buf[1];
        ping_rep.check_sum = (recv_buf[3] << 8) + recv_buf[2];
        ping_rep.id = (recv_buf[5] << 8) + recv_buf[4];
        ping_rep.seq_num = (recv_buf[7] << 8) + recv_buf[6];
        for (idx = 0; idx < recv_len - 8; idx++)
            ping_rep.data[idx] = recv_buf[8 + idx];

        tmp_checksum = ~wiz_checksum(recv_buf, recv_len);
        if (tmp_checksum != 0xffff)
            return -2;

        break;

    case WIZ_PING_REQUEST:
        ping_rep.code = recv_buf[1];
        ping_rep.type = recv_buf[2];
        ping_rep.check_sum = (recv_buf[3] << 8) + recv_buf[2];
        ping_rep.id = (recv_buf[5] << 8) + recv_buf[4];
        ping_rep.seq_num = (recv_buf[7] << 8) + recv_buf[6];
        for (idx = 0; idx < recv_len - 8; idx++)
            ping_rep.data[idx] = recv_buf[8 + idx];

        tmp_checksum = ping_rep.check_sum;
        ping_rep.check_sum = 0;
        if (tmp_checksum != ping_rep.check_sum)
            return -2;

        break;

    default:
        rlog_w("unknown ping receive message.");
        return -1;
    }

    return recv_len - WIZ_PING_HEAD_LEN;
}

int RyanW5500Ping(struct netdev *netdev, const char *host, size_t data_len, uint32_t times, struct netdev_ping_resp *ping_resp)
{
    int result = 0,
        socket = 0;
    struct hostent *hostent = NULL;

    hostent = wiz_gethostbyname(host);
    if (NULL == hostent || NULL == hostent->h_addr_list[0])
    {
        rlog_w("hostent is NULL.");
        return -RT_FALSE;
    }

    struct in_addr serviceAddr = *(struct in_addr *)hostent->h_addr_list[0];

    // SOCK_RAW == Sn_MR_IPRAW == 3
    socket = wiz_socket(AF_WIZ, SOCK_RAW, 0);
    if (socket < 0)
    {
        rlog_w("create ping socket(%d) failed.", socket);
        return -1;
    }

    // 设置套接字ICMP协议
    IINCHIP_WRITE(Sn_PROTO(socket), IPPROTO_ICMP);

    struct timeval timeout = {.tv_sec = times / RT_TICK_PER_SECOND,
                              .tv_usec = times % RT_TICK_PER_SECOND * 1000000 / RT_TICK_PER_SECOND};

    // 设置接收和发送超时选项
    wiz_setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (void *)&timeout, sizeof(timeout));
    wiz_setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (void *)&timeout, sizeof(timeout));

    struct sockaddr_in server_addr = {.sin_family = AF_WIZ,
                                      .sin_port = htons(WIZ_PING_PORT),
                                      .sin_addr = serviceAddr};

    if (wiz_connect(socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        wiz_closesocket(socket);
        return -1;
    }

    result = wiz_ping_request(socket);

    platformTimer_t pingTimer = {0};
    platformTimerCutdown(&pingTimer, 0xffff);
    if (result > 0)
    {
        result = wiz_ping_reply(socket, (struct sockaddr *)&server_addr);

        ping_resp->ip_addr.addr = serviceAddr.s_addr;
        ping_resp->ticks = 0xffff - platformTimerRemain(&pingTimer);
        ping_resp->data_len = data_len;
        int optlen = sizeof(ping_resp->ttl);
        wiz_getsockopt(socket, IPPROTO_IP, IP_TTL, &ping_resp->ttl, (socklen_t *)&optlen);
    }

    wiz_closesocket(socket);

    return result;
}
