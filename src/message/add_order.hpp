#pragma once

#include "../utils.hpp"
#include "message_header.hpp"
#include <bit>
#include <cstring>

struct __attribute__((packed)) raw_add_order
{
  message_header header; // offset 0,  11 bytes
  u64 order_ref;         // offset 11,  8 bytes
  char side;             // offset 19,  1 byte  ('B' or 'S')
  u32 shares;            // offset 20,  4 bytes
  char stock[8];         // offset 24,  8 bytes (right-padded spaces)
  u32 price;             // offset 32,  4 bytes (fixed point /10000)
}; // total: 36 bytes

struct add_order
{
  u64 timestamp;
  u64 order_ref;
  u32 shares;
  u32 price; // fixed point x10000 — ex: 10050 = $1.0050
  u16 stock_locate;
  char stock[8];
  char side;
};

inline add_order parse_add_order(const raw_add_order *raw)
{
  add_order o{
      .timestamp = get_timestamp(raw->header),
      .order_ref = std::byteswap(raw->order_ref),
      .shares = std::byteswap(raw->shares),
      .price = std::byteswap(raw->price),
      .stock_locate = get_stock_locate(raw->header),
      .stock = {},
      .side = raw->side,
  };
  std::memcpy(o.stock, raw->stock, 8);
  return o;
}
