// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// Decoder
///
/// \file   dcc/rx/decoder.hpp
/// \author Vincent Hamp
/// \date   29/11/2022

#pragma once

#include "../address.hpp"
#include "readable.hpp"
#include "writable.hpp"

namespace dcc::rx {

template<typename T>
concept Decoder = Readable<T> && Writable<T> &&
                  requires(T t,
                           Address::value_type addr,
                           bool dir,
                           int32_t speed,
                           uint32_t mask,
                           uint32_t state,
                           bool service_mode,
                           std::span<uint8_t const> bytes) {
                    { t.direction(addr, dir) };
                    { t.speed(addr, speed) };
                    { t.function(addr, mask, state) };
                    { t.serviceModeHook(service_mode) };
                    { t.serviceAck() };
                    { t.transmitBiDi(bytes) };
                  };

} // namespace dcc::rx
