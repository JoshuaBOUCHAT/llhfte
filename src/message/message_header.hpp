#pragma once

#include "../utils.hpp"
#include <bit>
#include <cstring>

struct __attribute__((packed)) message_header
{
  u8 msg_type;
  u16 stock_locate;
  u16 tracking_nb;
  u8 timestamp[6];
};

inline u8 get_msg_type(const message_header &h) { return h.msg_type; }

inline u16 get_stock_locate(const message_header &h)
{
  return std::byteswap(h.stock_locate);
}

inline u16 get_tracking_nb(const message_header &h)
{
  return std::byteswap(h.tracking_nb);
}

inline u64 get_timestamp(const message_header &h)
{
  u64 ts = 0;
  std::memcpy(&ts, h.timestamp, 6);
  return std::byteswap(ts) >> 16;
}