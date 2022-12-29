#define DBG_ENABLE

#define DBG_SECTION_NAME ("dns")
#define DBG_LEVEL DBG_WARNING
#define DBG_COLOR

#include "RyanW5500Store.h"

#include "wizchip_socket.h"
#include "wizchip_dns.h"

#define INITRTT 2000L                                       /* Initial smoothed response time */
#define MAXCNAME (MAX_DOMAIN_NAME + (MAX_DOMAIN_NAME >> 1)) /* Maximum amount of cname recursion */

#define TYPE_A 1      /* Host address */
#define TYPE_NS 2     /* Name server */
#define TYPE_MD 3     /* Mail destination (obsolete) */
#define TYPE_MF 4     /* Mail forwarder (obsolete) */
#define TYPE_CNAME 5  /* Canonical name */
#define TYPE_SOA 6    /* Start of Authority */
#define TYPE_MB 7     /* Mailbox name (experimental) */
#define TYPE_MG 8     /* Mail group member (experimental) */
#define TYPE_MR 9     /* Mail rename name (experimental) */
#define TYPE_NULL 10  /* Null (experimental) */
#define TYPE_WKS 11   /* Well-known sockets */
#define TYPE_PTR 12   /* Pointer record */
#define TYPE_HINFO 13 /* Host information */
#define TYPE_MINFO 14 /* Mailbox information (experimental)*/
#define TYPE_MX 15    /* Mail exchanger */
#define TYPE_TXT 16   /* Text strings */
#define TYPE_ANY 255  /* Matches any type */

#define CLASS_IN 1 /* The ARPA Internet */

/* Round trip timing parameters */
#define AGAIN 8  /* Average RTT gain = 1/8 */
#define LAGAIN 3 /* Log2(AGAIN) */
#define DGAIN 4  /* Mean deviation gain = 1/4 */
#define LDGAIN 2 /* log2(DGAIN) */

/* Header for all domain messages */
struct dhdr
{
    uint16_t id; /* Identification */
    uint8_t qr;  /* Query/Response */
#define QUERY 0
#define RESPONSE 1
    uint8_t opcode;
#define IQUERY 1
    uint8_t aa;    /* Authoratative answer */
    uint8_t tc;    /* Truncation */
    uint8_t rd;    /* Recursion desired */
    uint8_t ra;    /* Recursion available */
    uint8_t rcode; /* Response code */
#define NO_ERROR 0
#define FORMAT_ERROR 1
#define SERVER_FAIL 2
#define NAME_ERROR 3
#define NOT_IMPL 4
#define REFUSED 5
    uint16_t qdcount; /* Question count */
    uint16_t ancount; /* Answer count */
    uint16_t nscount; /* Authority (name server) count */
    uint16_t arcount; /* Additional record count */
};

uint8_t *pDNSMSG;   // DNS message buffer
uint8_t DNS_SOCKET; // SOCKET number for DNS
uint16_t DNS_MSGID; // DNS message ID

/* converts uint16_t from network buffer to a host byte order integer. */
uint16_t get16(uint8_t *s)
{
    uint16_t i;
    i = *s++ << 8;
    i = i + *s;
    return i;
}

/* copies uint16_t to the network buffer with network byte order. */
uint8_t *put16(uint8_t *s, uint16_t i)
{
    *s++ = i >> 8;
    *s++ = i;
    return s;
}

/*
 *              CONVERT A DOMAIN NAME TO THE HUMAN-READABLE FORM
 *
 * Description : This function converts a compressed domain name to the human-readable form
 * Arguments   : msg        - is a pointer to the reply message
 *               compressed - is a pointer to the domain name in reply message.
 *               buf        - is a pointer to the buffer for the human-readable form name.
 *               len        - is the MAX. size of buffer.
 * Returns     : the length of compressed message
 */
int parse_name(uint8_t *msg, uint8_t *compressed, char *buf, int16_t len)
{
    uint16_t slen; /* Length of current segment */
    uint8_t *cp;
    int clen = 0;     /* Total length of compressed name */
    int indirect = 0; /* Set if indirection encountered */
    int nseg = 0;     /* Total number of segments in name */

    cp = compressed;

    for (;;)
    {
        slen = *cp++; /* Length of this segment */

        if (!indirect)
            clen++;

        if ((slen & 0xc0) == 0xc0)
        {
            if (!indirect)
                clen++;
            indirect = 1;
            /* Follow indirection */
            cp = &msg[((slen & 0x3f) << 8) + *cp];
            slen = *cp++;
        }

        if (slen == 0) /* zero length == all done */
            break;

        len -= slen + 1;

        if (len < 0)
            return -1;

        if (!indirect)
            clen += slen;

        while (slen-- != 0)
            *buf++ = (char)*cp++;
        *buf++ = '.';
        nseg++;
    }

    if (nseg == 0)
    {
        /* Root name; represent as single dot */
        *buf++ = '.';
        len--;
    }

    *buf++ = '\0';
    len--;

    return clen; /* Length of compressed message */
}

/*
 *              PARSE QUESTION SECTION
 *
 * Description : This function parses the qeustion record of the reply message.
 * Arguments   : msg - is a pointer to the reply message
 *               cp  - is a pointer to the qeustion record.
 * Returns     : a pointer the to next record.
 */
uint8_t *dns_question(uint8_t *msg, uint8_t *cp)
{
    int len;
    char name[MAXCNAME];

    len = parse_name(msg, cp, name, MAXCNAME);

    if (len == -1)
        return 0;

    cp += len;
    cp += 2; /* type */
    cp += 2; /* class */

    return cp;
}

/*
 *              PARSE ANSER SECTION
 *
 * Description : This function parses the answer record of the reply message.
 * Arguments   : msg - is a pointer to the reply message
 *               cp  - is a pointer to the answer record.
 * Returns     : a pointer the to next record.
 */
uint8_t *dns_answer(uint8_t *msg, uint8_t *cp, uint8_t *ip_from_dns)
{
    int len, type;
    char name[MAXCNAME];

    len = parse_name(msg, cp, name, MAXCNAME);

    if (len == -1)
        return 0;

    cp += len;
    type = get16(cp);
    cp += 2; /* type */
    cp += 2; /* class */
    cp += 4; /* ttl */
    cp += 2; /* len */

    switch (type)
    {
    case TYPE_A:
        /* Just read the address directly into the structure */
        ip_from_dns[0] = *cp++;
        ip_from_dns[1] = *cp++;
        ip_from_dns[2] = *cp++;
        ip_from_dns[3] = *cp++;
        break;
    case TYPE_CNAME:
    case TYPE_MB:
    case TYPE_MG:
    case TYPE_MR:
    case TYPE_NS:
    case TYPE_PTR:
        /* These types all consist of a single domain name */
        /* convert it to ascii format */
        len = parse_name(msg, cp, name, MAXCNAME);
        if (len == -1)
            return 0;

        cp += len;
        break;
    case TYPE_HINFO:
        len = *cp++;
        cp += len;

        len = *cp++;
        cp += len;
        break;
    case TYPE_MX:
        cp += 2;
        /* Get domain name of exchanger */
        len = parse_name(msg, cp, name, MAXCNAME);
        if (len == -1)
            return 0;

        cp += len;
        break;
    case TYPE_SOA:
        /* Get domain name of name server */
        len = parse_name(msg, cp, name, MAXCNAME);
        if (len == -1)
            return 0;

        cp += len;

        /* Get domain name of responsible person */
        len = parse_name(msg, cp, name, MAXCNAME);
        if (len == -1)
            return 0;

        cp += len;

        cp += 4;
        cp += 4;
        cp += 4;
        cp += 4;
        cp += 4;
        break;
    case TYPE_TXT:
        /* Just stash */
        break;
    default:
        /* Ignore */
        break;
    }

    return cp;
}

/*
 *              PARSE THE DNS REPLY
 *
 * Description : This function parses the reply message from DNS server.
 * Arguments   : dhdr - is a pointer to the header for DNS message
 *               buf  - is a pointer to the reply message.
 *               len  - is the size of reply message.
 * Returns     : -1 - Domain name lenght is too big
 *                0 - Fail (Timout or parse error)
 *                1 - Success,
 */
int8_t parseDNSMSG(struct dhdr *pdhdr, uint8_t *pbuf, uint8_t *ip_from_dns)
{
    uint16_t tmp;
    uint16_t i;
    uint8_t *msg;
    uint8_t *cp;

    msg = pbuf;
    memset(pdhdr, 0, sizeof(*pdhdr));

    pdhdr->id = get16(&msg[0]);
    tmp = get16(&msg[2]);
    if (tmp & 0x8000)
        pdhdr->qr = 1;

    pdhdr->opcode = (tmp >> 11) & 0xf;

    if (tmp & 0x0400)
        pdhdr->aa = 1;
    if (tmp & 0x0200)
        pdhdr->tc = 1;
    if (tmp & 0x0100)
        pdhdr->rd = 1;
    if (tmp & 0x0080)
        pdhdr->ra = 1;

    pdhdr->rcode = tmp & 0xf;
    pdhdr->qdcount = get16(&msg[4]);
    pdhdr->ancount = get16(&msg[6]);
    pdhdr->nscount = get16(&msg[8]);
    pdhdr->arcount = get16(&msg[10]);

    /* Now parse the variable length sections */
    cp = &msg[12];

    /* Question section */
    for (i = 0; i < pdhdr->qdcount; i++)
    {
        cp = dns_question(msg, cp);

        if (!cp)
            return -1;
    }

    /* Answer section */
    for (i = 0; i < pdhdr->ancount; i++)
    {
        cp = dns_answer(msg, cp, ip_from_dns);

        if (!cp)
            return -1;
    }

    /* Name server (authority) section */
    for (i = 0; i < pdhdr->nscount; i++)
    {
        ;
    }

    /* Additional section */
    for (i = 0; i < pdhdr->arcount; i++)
    {
        ;
    }

    if (pdhdr->rcode == 0)
        return 1; // No error
    else
        return 0;
}

/*
 *              MAKE DNS QUERY MESSAGE
 *
 * Description : This function makes DNS query message.
 * Arguments   : op   - Recursion desired
 *               name - is a pointer to the domain name.
 *               buf  - is a pointer to the buffer for DNS message.
 *               len  - is the MAX. size of buffer.
 * Returns     : the pointer to the DNS message.
 */
int16_t dns_makequery(uint16_t op, char *name, uint8_t *buf, uint16_t len)
{
    uint8_t *cp;
    char *cp1;
    char sname[MAXCNAME];
    char *dname;
    uint16_t p;
    uint16_t dlen;

    cp = buf;

    DNS_MSGID++;
    cp = put16(cp, DNS_MSGID);
    p = (op << 11) | 0x0100; /* Recursion desired */
    cp = put16(cp, p);
    cp = put16(cp, 1);
    cp = put16(cp, 0);
    cp = put16(cp, 0);
    cp = put16(cp, 0);

    strcpy(sname, name);
    dname = sname;
    dlen = strlen(dname);
    for (;;)
    {
        /* Look for next dot */
        cp1 = strchr(dname, '.');

        if (cp1 != NULL)
            len = cp1 - dname; /* More to come */
        else
            len = dlen; /* Last component */

        *cp++ = len; /* Write length of component */
        if (len == 0)
            break;

        /* Copy component up to (but not including) dot */
        strncpy((char *)cp, dname, len);
        cp += len;
        if (cp1 == NULL)
        {
            *cp++ = 0; /* Last one; write null and finish */
            break;
        }
        dname += len + 1;
        dlen -= len + 1;
    }

    cp = put16(cp, 0x0001); /* type */
    cp = put16(cp, 0x0001); /* class */

    return ((int16_t)((uint32_t)(cp) - (uint32_t)(buf)));
}

/**
 * @brief DNS 进程初始化
 *
 * @param s 套接字
 * @param buf 消息的缓冲区
 */
// void DNS_init(uint8_t s, uint8_t *buf)
// {

//     DNS_SOCKET = s; // SOCK_DNS
//     pDNSMSG = buf;  // User's shared buffer
//     DNS_MSGID = DNS_MSG_ID;
// }

/**
 * @brief 发送DNS查询并接收DNS响应
 *
 * @param dns_ip DNS 服务器 IP
 * @param name 待查询域名
 * @param ip_from_dns 来自 DNS 服务器的 IP 地址
 * @return int8_t
 */
int8_t DNS_run(uint8_t *dns_ip, uint8_t *name, uint8_t *ip_from_dns, uint8_t *buf)
{
    int8_t ret = 0;
    uint8_t retry_count = 0;
    uint16_t len = 0,
             port = 0;
    uint8_t ip[4] = {0};
    struct dhdr dhp = {0};
    platformTimer_t recvTimer = {0};
    RyanW5500Socket *sock = NULL;

    sock = RyanW5500SocketCreate(SOCK_DGRAM, IPPORT_DOMAIN);
    if (NULL == sock)
    {
        LOG_W("dns socket失败");
        return -2;
    }

    LOG_D("> DNS Query to DNS Server : %d.%d.%d.%d", dns_ip[0], dns_ip[1], dns_ip[2], dns_ip[3]);

    DNS_SOCKET = sock->socket; // SOCK_DNS
    pDNSMSG = buf;             // User's shared buffer
    DNS_MSGID = DNS_MSG_ID;

    len = dns_makequery(0, (char *)name, pDNSMSG, MAX_DNS_BUF_SIZE);
    wizchip_sendto(DNS_SOCKET, pDNSMSG, len, dns_ip, IPPORT_DOMAIN);

    platformTimerCutdown(&recvTimer, DNS_WAIT_TIME);

    while (1)
    {
        if ((len = getSn_RX_RSR(DNS_SOCKET)) > 0)
        {
            if (len > MAX_DNS_BUF_SIZE)
                len = MAX_DNS_BUF_SIZE;
            len = wizchip_recvfrom(DNS_SOCKET, pDNSMSG, len, ip, &port);
            LOG_D("> Receive DNS message from %d.%d.%d.%d(%d). len = %d", ip[0], ip[1], ip[2], ip[3], port, len);
            ret = parseDNSMSG(&dhp, pDNSMSG, ip_from_dns);
            break;
        }

        if (0 == platformTimerRemain(&recvTimer))
        {
            LOG_D("> DNS Timeout\r\n");
            wizchip_sendto(DNS_SOCKET, pDNSMSG, len, dns_ip, IPPORT_DOMAIN);
            platformTimerCutdown(&recvTimer, DNS_WAIT_TIME);
            retry_count++;
        }

        if (retry_count >= MAX_DNS_RETRY)
        {
            LOG_D("> DNS Server is not responding : %d.%d.%d.%d", dns_ip[0], dns_ip[1], dns_ip[2], dns_ip[3]);
            wizchip_close(DNS_SOCKET);
            return 0; // timeout occurred
        }

        delay(3);
    }

    wiz_closesocket(DNS_SOCKET);

    // Return value
    // 0 > :  failed / 1 - success
    return ret;
}
