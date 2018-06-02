/*
* THIS FILE IS FOR IP FORWARD TEST
*/
#include "sysInclude.h"
#include <vector>
using std::vector;

// system support
extern void fwd_LocalRcv(char *pBuffer, int length);

extern void fwd_SendtoLower(char *pBuffer, int length, unsigned int nexthop);

extern void fwd_DiscardPkt(char *pBuffer, int type);

extern unsigned int getIpv4Address( );

// implemented by students
typedef unsigned char B8;
typedef unsigned short B16;
typedef unsigned int B32;

// 计算头部校验�?
B16 calcHeaderChecksum(char *buffer,int IHL)
{
    B32 checksum = 0;
    for(int i=0; i<IHL*2; i++)
    {
        if(i!=5)
        {
            checksum += ((buffer[i*2]<<8)|(buffer[i*2+1]));
            if((checksum & 0xffff0000) != 0)
            {
                checksum = (checksum & 0x0000ffff) + (checksum >> 16);
            }
        }
    }
    return htons(~(B16)checksum);
}

// 根据子网掩码长度和IP获取子网IP
B32 get_Subnet_IP(B32 masklen,B32 destIP)
{
    return destIP & ((1<<31)>>(masklen-1)); //1是有符号�?
}

// 路由表项
struct RouteTableItem
{
    B32 destIP;
    B32 masklen;
    B32 nexthop;
    RouteTableItem(B32 _destIP, B32 _masklen, B32 _nexthop)
    {
        destIP = _destIP;
        masklen = _masklen;
        nexthop = _nexthop;
    }
};

// 路由�?
vector<RouteTableItem> routeTable;

void stud_Route_Init()
{
    routeTable.clear();
	return;
}

// 添加路由表项
void stud_route_add(stud_route_msg *proute)
{
    routeTable.push_back(
                            RouteTableItem
                            (
                                get_Subnet_IP(ntohl(proute->masklen), ntohl(proute->dest)),
                                ntohl(proute->masklen),
                                ntohl(proute->nexthop)
                            )
                        );
	return;
}

// 处理接收到的分组
int stud_fwd_deal(char *pBuffer, int length)
{
    int IHL = pBuffer[0] & 0xf; //获取头部长度
	int destIP = ntohl(*(B32 *)(pBuffer + 16)); //获取目的IP地址
    int ttl = pBuffer[8];
    // 如果是本机，则直接接�?
        if(destIP == getIpv4Address())
    {
        fwd_LocalRcv(pBuffer, length);
        return 0;
    }
    if(ttl == 0)
    {
        fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_TTLERROR);
        return 1;
    }

    // 路由查找
    int maxMatchIndex = -1;
    int maxMatchLength = 0;
    for(int i=0;i<routeTable.size();i++)
    {
        if(routeTable[i].destIP == get_Subnet_IP(routeTable[i].masklen,destIP) && routeTable[i].masklen > maxMatchLength)
        {
            maxMatchIndex = i;
            maxMatchLength = routeTable[i].masklen;
        }
    }

    if(maxMatchIndex == -1)
    {
        fwd_DiscardPkt(pBuffer, STUD_FORWARD_TEST_NOROUTE);
        return 1;
    }
    else
    {
        pBuffer[8]--;
        B16 new_checksum = calcHeaderChecksum(pBuffer, IHL);
        memcpy(pBuffer+10, &new_checksum, sizeof(new_checksum));
        fwd_SendtoLower(pBuffer, length, routeTable[maxMatchIndex].nexthop);
	   return 0;
    }
}

