/**
   @file response_slots.h

   @brief A few slots for long-running HTTP responses (the stream viewer, photo downloads). A new request takes a free
          slot, or else closes a holder that has had no send callback for SilentMs (its client stopped reading) and takes
          that slot, or else is refused.

   Used only from the web server task, so it needs no lock. Header-only and templated on the request type, so it also
   builds on a PC with a fake request.

   @bug: no know bug
*/

#pragma once

#include <stdint.h>
#include <stddef.h>

template <class Request>
class ResponseSlots {
public:
  static constexpr uint8_t MaxSlots = 2;  ///< capacity of the table

private:
  struct Slot {
    Request *request;   ///< the holder's request, closed on takeover
    const void *owner;  ///< the holder's response, or the request while only reserved
    uint32_t last_ms;   ///< time of the holder's last send callback
  };

  Slot slots[MaxSlots];     ///< the slots
  const uint8_t Count;      ///< slots in use, at most MaxSlots
  const uint32_t SilentMs;  ///< silence after which a holder may be replaced [ms]

  void Clear(uint8_t i) {
    slots[i].request = nullptr;
    slots[i].owner = nullptr;
    slots[i].last_ms = 0;
  }

public:
  /**
     @brief Construct the slots
     @param uint8_t i_count - number of slots, at most 2
     @param uint32_t i_silent_ms - silence after which a holder may be replaced [ms]
  */
  ResponseSlots(uint8_t i_count, uint32_t i_silent_ms) : Count((i_count < MaxSlots) ? i_count : MaxSlots), SilentMs(i_silent_ms) {
    for (uint8_t i = 0; i < MaxSlots; i++) {
      Clear(i);
    }
  }

  /**
     @brief Reserve a slot for a new request. Closes a silent holder if no slot is free; closing deletes its response
            at once, and its destructor calls Release().
     @param Request* i_request - the new request
     @param uint32_t i_now_ms - millis()
     @return int8_t - the slot, or -1 when every holder is active
  */
  int8_t Acquire(Request *i_request, uint32_t i_now_ms) {
    for (uint8_t i = 0; i < Count; i++) {
      if (nullptr == slots[i].owner) {
        slots[i].request = i_request;
        slots[i].owner = i_request;
        slots[i].last_ms = i_now_ms;
        return i;
      }
    }
    for (uint8_t i = 0; i < Count; i++) {
      if ((uint32_t)(i_now_ms - slots[i].last_ms) >= SilentMs) {
        Request *silent = slots[i].request;
        /* take the slot first, so a late Release() from the old response cannot free it */
        slots[i].request = i_request;
        slots[i].owner = i_request;
        slots[i].last_ms = i_now_ms;
        silent->client()->close();
        return i;
      }
    }
    return -1;
  }

  /**
     @brief The response that holds a reserved slot from now on
     @param int8_t i_slot - from Acquire()
     @param void* i_owner - the response
  */
  void Bind(int8_t i_slot, const void *i_owner) {
    if ((i_slot >= 0) && (i_slot < Count)) {
      slots[i_slot].owner = i_owner;
    }
  }

  /**
     @brief Give back a reserved slot that no response took
     @param int8_t i_slot - from Acquire()
  */
  void Cancel(int8_t i_slot) {
    if ((i_slot >= 0) && (i_slot < Count)) {
      Clear(i_slot);
    }
  }

  /**
     @brief The holder sent data
     @param void* i_owner - the response
     @param uint32_t i_now_ms - millis()
  */
  void Touch(const void *i_owner, uint32_t i_now_ms) {
    for (uint8_t i = 0; i < Count; i++) {
      if (i_owner == slots[i].owner) {
        slots[i].last_ms = i_now_ms;
      }
    }
  }

  /**
     @brief The holder's response is being destroyed. Frees the slot only if this response still holds it.
     @param void* i_owner - the response
  */
  void Release(const void *i_owner) {
    for (uint8_t i = 0; i < Count; i++) {
      if (i_owner == slots[i].owner) {
        Clear(i);
      }
    }
  }
};

/* EOF */
