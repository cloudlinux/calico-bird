/*
 *	BIRD -- OSPF
 *
 *	(c) 1999 Ondrej Filip <feela@network.cz>
 *
 *	Can be freely distributed and used under the terms of the GNU GPL.
 */

#define LOCAL_DEBUG

#include <string.h>

#include "nest/bird.h"
#include "nest/iface.h"
#include "nest/protocol.h"
#include "nest/route.h"
#include "conf/conf.h"
#include "lib/ip.h"
#include "lib/socket.h"
#include "lib/lists.h"
#include "lib/timer.h"
#include "lib/checksum.h"

#include "ospf.h"

void
ospf_hello_rx(struct ospf_hello_packet *ps, struct proto *p,
  struct ospf_iface *ifa)
{
  switch(ifa->state)
  {
    case OSPF_IS_DOWN:
      die("%s: Iface %s in down state?",p->name, ifa->iface->name);
      break;
    case OSPF_IS_WAITING:
      DBG(p->name);
      DBG(": Neighbour? on iface ");
      DBG(ifa->iface->name);
      DBG("\n");
      break;
    case OSPF_IS_PTP:
    case OSPF_IS_DROTHER:
    case OSPF_IS_BACKUP:
    case OSPF_IS_DR:
      DBG("OSPF, RX, Unimplemented state.\n");
      break;
    default:
      die("%s: Iface %s in unknown state?",p->name, ifa->iface->name);
      break;
  }
}

int
ospf_rx_hook(sock *sk, int size)
{
#ifndef IPV6
  struct ospf_packet *ps;
  struct ospf_iface *ifa;
  struct proto *p;
  int i;
  u8 *pu8;


  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG(p->name);
  DBG(": RX_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");

  ps=(struct ospf_packet *)(sk->rbuf+5*4);

  if(size<=(20+sizeof(struct ospf_packet)))
  {
    log("%s: Bad packet received: too short", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  if((ntohs(ps->length))!=(size-20))
  {
    log("%s: Bad packet received: size fields does not match", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(ps->version!=OSPF_VERSION)
  {
    log("%s: Bad packet received: version %d", p->name, ps->version);
    log("%s: Discarding",p->name);
    return(1);
  }

  if(!ipsum_verify(ps, 16,(void *)ps+sizeof(struct ospf_packet),
    ntohs(ps->length)-sizeof(struct ospf_packet), NULL))
  {
    log("%s: Bad packet received: bad checksum", p->name);
    log("%s: Discarding",p->name);
    return(1);
  }

  /* FIXME: Count checksum */
  /* FIXME: Do authetification */

  if(ps->areaid!=ifa->area)
  {
    log("%s: Bad packet received: other area %ld", p->name, ps->areaid);
    log("%s: Discarding",p->name);
    return(1);
  }

  /* Dump packet */
  pu8=(u8 *)(sk->rbuf+5*4);
  for(i=0;i<ntohs(ps->length);i+=4)
    debug("%s: received %d,%d,%d,%d\n",p->name, pu8[i+0], pu8[i+1], pu8[i+2],
		    pu8[i+3]);
  debug("%s: received size: %d\n",p->name,size);

  switch(ps->type)
  {
    case HELLO:
      DBG(p->name);
      DBG(": Hello received.\n");
      ospf_hello_rx((struct ospf_hello_packet *)ps, p, ifa);
      break;
    case DBDES:
      break;
    case LSREQ:
      break;
    case LSUPD:
      break;
    case LSACK:
      break;
    default:
      log("%s: Bad packet received: wrong type %d", p->name, ps->type);
      log("%s: Discarding",p->name);
      return(1);
  };
  DBG("\n");
#else
#error RX_Hook does not work for IPv6 now.
#endif
  return(1);
}

void
ospf_tx_hook(sock *sk)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG(p->name);
  DBG(": TX_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");
}

void
ospf_err_hook(sock *sk, int err)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)(sk->data);

  p=(struct proto *)(ifa->proto);
  DBG(p->name);
  DBG(": Err_Hook called on interface ");
  DBG(sk->iface->name);
  DBG(".\n");
}

/* This will change ! */
sock *
ospf_open_socket(struct proto *p, struct ospf_iface *ifa)
{
  sock *mcsk;

  /* No NBMA networks now */

  if(ifa->iface->flags & IF_MULTICAST)
  {
    mcsk=sk_new(p->pool);
    mcsk->type=SK_IP_MC;
    mcsk->dport=OSPF_PROTO;
    mcsk->saddr=AllSPFRouters;
    mcsk->daddr=AllSPFRouters;
    mcsk->ttl=1;
    mcsk->rx_hook=ospf_rx_hook;
    mcsk->tx_hook=ospf_tx_hook;
    mcsk->err_hook=ospf_err_hook;
    mcsk->iface=ifa->iface;
    mcsk->rbsize=ifa->iface->mtu;
    mcsk->tbsize=ifa->iface->mtu;
    mcsk->data=(void *)ifa;
    if(sk_open(mcsk)!=0)
    {
      DBG(p->name);
      DBG(": SK_OPEN: failed\n");
      return(NULL);
    }
    DBG(p->name);
    DBG(": SK_OPEN: open\n");
    return(mcsk);
  }
  else return(NULL);
}

/* 
 * This will later decide, wheter use iface for OSPF or not
 * depending on config
 */
int
is_good_iface(struct proto *p, struct iface *iface)
{
  if(iface->flags & IF_UP)
  {
    if(!(iface->flags & IF_IGNORE)) return 1;
  }
  return 0;
}

/* Of course, it's NOT true now */
int
ospf_iface_clasify(struct iface *ifa)
{
  /* FIXME: Latter I'll use config - this is incorrect */
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    (IF_MULTIACCESS|IF_MULTICAST))
  {
     DBG(" OSPF: Clasifying BROADCAST.\n");
     return OSPF_IT_BROADCAST;
  }
  if((ifa->flags & (IF_MULTIACCESS|IF_MULTICAST))==
    IF_MULTIACCESS)
  {
    DBG(" OSPF: Clasifying NBMA.\n");
    return OSPF_IT_NBMA;
  }
  DBG(" OSPF: Clasifying P-T-P.\n");
  return OSPF_IT_PTP;
}

void
hello_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  debug("%s: Hello timer fired on interface %s.\n",
    p->name, ifa->iface->name);
}

void
add_hello_timer(struct ospf_iface *ifa)
{
  struct proto *p;
  p=(struct proto *)(ifa->proto);

  if(ifa->helloint==0) ifa->helloint=HELLOINT_D;
  
  ifa->timer->hook=hello_timer_hook;
  ifa->timer->recurrent=ifa->helloint;
  ifa->timer->expires=0;
  tm_start(ifa->timer,0);
  DBG("%s: Installing hello timer.\n", p->name);
  if(ifa->type=OSPF_IT_PTP) ifa->state=OSPF_IS_PTP;
}

void
wait_timer_hook(timer *timer)
{
  struct ospf_iface *ifa;
  struct proto *p;

  ifa=(struct ospf_iface *)timer->data;
  p=(struct proto *)(ifa->proto);
  debug("%s: Wait timer fired on interface %s.\n",
    p->name, ifa->iface->name);
  if(ifa->state=OSPF_IS_WAITING)
  {
    /*
     * Wait time fired. Now we must change state
     * to DR or DROTHER depending on priority
     */
    if(ifa->priority!=0)
    {
      debug("%s: Changing state into DR.\n", p->name);

      ifa->state=OSPF_IS_DR;
      ifa->drip=ifa->iface->addr->ip;
      /* FIXME: Set ifa->drid */
    }
    else
    {
      debug("%s: Changing state into DROTHER.\n",p->name);
      ifa->state=OSPF_IS_DROTHER;
    }
    add_hello_timer(ifa);
  }
  else
  {
    die("%s: Wait timer fired I'm not in WAITING state?", p->name);
  }
}

void
add_wait_timer(struct ospf_iface *ifa, pool *pool, int wait)
{
  struct proto *p;

  p=(struct proto *)(ifa->proto);
  ifa->timer=tm_new(pool);
  ifa->timer->data=ifa;
  ifa->timer->randomize=1;
  if((ifa->type!=OSPF_IT_PTP))
  {
    ifa->timer->hook=wait_timer_hook;
    ifa->timer->recurrent=0;
    ifa->timer->expires=0;
    ifa->state=OSPF_IS_WAITING;
    tm_start(ifa->timer,(wait!=0 ? wait : WAIT_D));
    DBG(p->name);
    DBG(": Installing wait timer.\n");
  }
  else
  {
    add_hello_timer(ifa);
  }
}

void
ospf_iface_default(struct ospf_iface *ifa)
{
  int i;

  ifa->area=0; /* FIXME: Read from config */
  ifa->cost=COST_D;
  ifa->rxmtint=RXMTINT_D;
  ifa->iftransdelay=IFTRANSDELAY_D;
  ifa->priority=PRIORITY_D;
  ifa->helloint=HELLOINT_D;
  ifa->deadint=DEADINT_D;
  ifa->autype=0;
  for(i=0;i<8;i++) ifa->aukey[i]=0;
  ifa->options=0;
  ifa->drip=ipa_from_u32(0x00000000);
  ifa->drid=0;
  ifa->bdrip=ipa_from_u32(0x00000000);
  ifa->bdrid=0;
  ifa->type=ospf_iface_clasify(ifa->iface);
}

struct ospf_iface*
find_iface(struct proto_ospf *p, struct iface *what)
{
  struct ospf_iface *i;

  WALK_LIST (i, p->iface_list)
    if ((i)->iface == what)
      return i;
  return NULL;
}

void
ospf_if_notify(struct proto *p, unsigned flags, struct iface *iface)
{
  struct ospf_iface *ifa;
  sock *mcsk, *newsk;
  struct ospf_sock *osk;

  struct ospf_config *c;
  c=(struct ospf_config *)(p->cf);

  DBG(" OSPF: If notify called\n");
  if (iface->flags & IF_IGNORE)
    return;

  if((flags & IF_CHANGE_UP) && is_good_iface(p, iface))
  {
    debug(" OSPF: using interface %s.\n", iface->name);
    /* FIXME: Latter I'll use config - this is incorrect */
    ifa=mb_alloc(p->pool, sizeof(struct ospf_iface));
    ifa->proto=(struct proto_ospf *)p;
    ifa->iface=iface;
    add_tail(&((struct proto_ospf *)p)->iface_list, NODE ifa);
    ospf_iface_default(ifa);
    /* FIXME: This should read config */
    add_wait_timer(ifa,p->pool,0);
    init_list(&(ifa->sk_list));
    if((mcsk=ospf_open_socket(p, ifa))!=NULL)
    {
      osk=(struct ospf_sock *)mb_alloc(p->pool, sizeof(struct ospf_sock));
      osk->sk=mcsk;
      add_tail(&(ifa->sk_list),NODE osk);
    }
  }

  if(flags & IF_CHANGE_DOWN)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug(" OSPF: killing interface %s.\n", iface->name);
      /* FIXME: This should delete ifaces */
    }
  }

  if(flags & IF_CHANGE_MTU)
  {
    if((ifa=find_iface((struct proto_ospf *)p, iface))!=NULL)
    {
      debug(" OSPF: changing MTU on interface %s.\n", iface->name);
      /* FIXME: change MTU */
    }
  }
}


static int
ospf_start(struct proto *p)
{
  DBG(p->name);
  DBG(": Start\n");

  p->if_notify=ospf_if_notify;

  return PS_UP;
}

static void
ospf_dump(struct proto *p)
{
  char areastr[20];
  struct ospf_config *c = (void *) p->cf;

  DBG(p->name);
  DBG(": Dump.\n");
  debug(" -AreaID: %d\n", c->area );
}

static struct proto *
ospf_init(struct proto_config *c)
{
  struct proto *p = proto_new(c, sizeof(struct proto_ospf));

  DBG(" OSPF: Init.\n");
  p->neigh_notify = NULL;
  p->if_notify = NULL;
  init_list(&((struct proto_ospf *)p)->iface_list);
  return p;
}

static void
ospf_preconfig(struct protocol *p, struct config *c)
{
  DBG( " OSPF: preconfig\n" );
}

static void
ospf_postconfig(struct proto_config *c)
{
  DBG( " OSPF: postconfig\n" );
}


struct protocol proto_ospf = {
  name:		"OSPF",
  init:		ospf_init,
  dump:		ospf_dump,
  start:	ospf_start,
  preconfig:	ospf_preconfig,
  postconfig:	ospf_postconfig,
};