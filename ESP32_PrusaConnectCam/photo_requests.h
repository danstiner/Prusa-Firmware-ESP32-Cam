/**
   @file photo_requests.h

   @brief Tickets for /action_capture. The web server task takes a ticket per request and waits;
          the photo task serves every ticket taken before its photo started.

   Header-only and free of Arduino types, so it also builds on a PC.

   @bug: no know bug
*/

#pragma once

#include <atomic>
#include <stdint.h>

class PhotoRequests {
private:
  std::atomic<uint32_t> Requested;  ///< last ticket handed out, written by the web server task only
  std::atomic<uint32_t> Served;     ///< last ticket served, written by the photo task only
  const uint32_t MaxWaiting;        ///< tickets that may wait at once

public:
  /**
     @brief Construct the ticket pair
     @param uint32_t i_max_waiting - tickets that may wait at once
     @param uint32_t i_start - first counter value (a PC test starts near the wraparound)
  */
  explicit PhotoRequests(uint32_t i_max_waiting, uint32_t i_start = 0) : Requested(i_start), Served(i_start), MaxWaiting(i_max_waiting) {}

  /**
     @brief Web server task: take a ticket
     @param uint32_t* o_ticket - the ticket
     @return bool - false when MaxWaiting tickets already wait
  */
  bool Request(uint32_t *o_ticket) {
    uint32_t requested = Requested.load();
    if ((requested - Served.load()) >= MaxWaiting) {
      return false;
    }
    requested++;
    Requested.store(requested);
    *o_ticket = requested;
    return true;
  }

  /**
     @brief Web server task: has a photo that started after this ticket was taken finished?
     @param uint32_t i_ticket - the ticket
     @return bool
  */
  bool Done(uint32_t i_ticket) const {
    return (int32_t)(Served.load() - i_ticket) >= 0;
  }

  /**
     @brief Photo task: read before the photo starts; the photo serves every ticket up to this one
     @return uint32_t
  */
  uint32_t Pending() const {
    return Requested.load();
  }

  /**
     @brief Photo task: does i_pending (from Pending()) hold tickets not yet served?
     @param uint32_t i_pending
     @return bool
  */
  bool HasWaiting(uint32_t i_pending) const {
    return i_pending != Served.load();
  }

  /**
     @brief Photo task: the photo is done, successful or not
     @param uint32_t i_pending - the value Pending() returned before the photo
  */
  void ServeUpTo(uint32_t i_pending) {
    Served.store(i_pending);
  }
};

/* EOF */
