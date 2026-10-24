/**
   @file tcp_retransmit.h

   @brief Early retransmit for stream connections. lwIP starts a fast retransmit only after three
          duplicate ACKs, but receivers that merge arriving segments (Linux GRO, macOS LRO) send
          one or two per lost segment, so each loss waits for lwIP's retransmission timer, which
          counts 500 ms ticks and fires 1 s or more after the last ACK. For each connection passed
          to TcpRetransmit_Watch, this starts lwIP's own fast retransmit after one duplicate ACK
          and TCP_EARLY_RETRANSMIT_WAIT without progress.

   @bug: no know bug
*/

#pragma once

#include <AsyncTCP.h>

void TcpRetransmit_Watch(AsyncClient *);

/* EOF */
