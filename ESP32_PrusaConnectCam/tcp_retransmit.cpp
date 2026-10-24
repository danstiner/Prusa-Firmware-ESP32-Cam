/**
   @file tcp_retransmit.cpp

   @brief Early retransmit for stream connections, see tcp_retransmit.h

   @bug: no know bug
*/

#include "tcp_retransmit.h"
#include "mcu_cfg.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "lwip/priv/tcp_priv.h"

/* this reads lwIP internals (tcp_active_pcbs, tcp_rexmit_fast, a pcb's dupacks and flags) as lwIP 2.1 has them */
#if (true == TCP_EARLY_RETRANSMIT) && LWIP_TCPIP_CORE_LOCKING && (2 == LWIP_VERSION_MAJOR) && (1 == LWIP_VERSION_MINOR)
#define TCP_RETRANSMIT_ACTIVE 1
#else
#define TCP_RETRANSMIT_ACTIVE 0
#endif

#if (1 == TCP_RETRANSMIT_ACTIVE)
/* connections watched at once; a stream started while all are taken is left to lwIP alone */
#define TCP_RETRANSMIT_WATCHED 4

struct TcpRetransmitWatch {
  struct tcp_pcb *pcb;  ///< the watched connection, NULL when the entry is free
  u16_t remote_port;    ///< tells the watched connection from a later one given the same pcb
  u32_t lastack;        ///< highest acknowledged sequence number at the last check
  u32_t progress_ms;    ///< when lastack last changed [sys_now ms]
};

static TcpRetransmitWatch TcpRetransmitWatched[TCP_RETRANSMIT_WATCHED];
static bool TcpRetransmitTimerArmed = false;

/**
   @brief Whether a pcb is still an active connection. lwIP frees the pcb of a closed connection.
   @param const struct tcp_pcb* pcb
   @return bool
*/
static bool TcpRetransmit_IsActive(const struct tcp_pcb *pcb) {
  for (const struct tcp_pcb *p = tcp_active_pcbs; NULL != p; p = p->next) {
    if (p == pcb) {
      return true;
    }
  }
  return false;
}

/**
   @brief Check the watched connections. Runs in lwIP's thread with the core locked, every
          TCP_EARLY_RETRANSMIT_CHECK while a connection is watched. lwIP counts duplicate ACKs while
          data is outstanding and clears the count on progress; once it is in fast or timeout
          recovery it is left to recover alone.
   @param void* arg - unused
   @return void
*/
static void TcpRetransmit_Check(void *arg) {
  (void)arg;
  u32_t now = sys_now();
  bool watching = false;

  for (TcpRetransmitWatch &w : TcpRetransmitWatched) {
    if (NULL == w.pcb) {
      continue;
    }
    if ((false == TcpRetransmit_IsActive(w.pcb)) || (w.pcb->remote_port != w.remote_port)) {
      w.pcb = NULL;
      continue;
    }
    watching = true;

    struct tcp_pcb *pcb = w.pcb;
    if (pcb->lastack != w.lastack) {
      w.lastack = pcb->lastack;
      w.progress_ms = now;
      continue;
    }

    if ((ESTABLISHED == pcb->state) && (NULL != pcb->unacked) && (0 != pcb->dupacks) &&
        (0 == (pcb->flags & (TF_INFR | TF_RTO))) && ((now - w.progress_ms) >= TCP_EARLY_RETRANSMIT_WAIT)) {
      tcp_rexmit_fast(pcb);
      tcp_output(pcb);
    }
  }

  if (true == watching) {
    sys_timeout(TCP_EARLY_RETRANSMIT_CHECK, TcpRetransmit_Check, NULL);
  } else {
    TcpRetransmitTimerArmed = false;
  }
}
#endif

/**
   @brief Watch a stream connection for early retransmit, until it closes. Does nothing when
          TCP_EARLY_RETRANSMIT is false, on an lwIP other than 2.1, or when TCP_RETRANSMIT_WATCHED
          connections are watched already.
   @param AsyncClient* client
   @return void
*/
void TcpRetransmit_Watch(AsyncClient *client) {
#if (1 == TCP_RETRANSMIT_ACTIVE)
  LOCK_TCPIP_CORE();
  struct tcp_pcb *pcb = (NULL != client) ? client->pcb() : NULL;
  TcpRetransmitWatch *entry = NULL;
  for (TcpRetransmitWatch &w : TcpRetransmitWatched) {
    if ((NULL != pcb) && (w.pcb == pcb)) {
      entry = &w;
      break;
    }
    if ((NULL == w.pcb) && (NULL == entry)) {
      entry = &w;
    }
  }
  if ((NULL != pcb) && (NULL != entry)) {
    entry->pcb = pcb;
    entry->remote_port = pcb->remote_port;
    entry->lastack = pcb->lastack;
    entry->progress_ms = sys_now();
    if (false == TcpRetransmitTimerArmed) {
      TcpRetransmitTimerArmed = true;
      sys_timeout(TCP_EARLY_RETRANSMIT_CHECK, TcpRetransmit_Check, NULL);
    }
  }
  UNLOCK_TCPIP_CORE();
#else
  (void)client;
#endif
}

/* EOF */
