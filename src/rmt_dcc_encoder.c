// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

/// RMT DCC encoder
///
/// \file   rmt_dcc_encoder.c
/// \author Vincent Hamp
/// \date   08/01/2023

#include "rmt_dcc_encoder.h"
#include <esp_attr.h>
#include <esp_check.h>
#include <esp_heap_caps.h>
#include <limits.h>

#if __has_include(<esp_linux_helper.h>)
#  include <esp_linux_helper.h>
#endif

#if defined(CONFIG_RMT_TX_ISR_HANDLER_IN_IRAM)
#  define RMT_IRAM_ATTR IRAM_ATTR
#else
#  define RMT_IRAM_ATTR
#endif

// https://github.com/espressif/esp-idf/issues/13032
#if !defined(RMT_MEM_ALLOC_CAPS)
#  if CONFIG_RMT_ISR_IRAM_SAFE || CONFIG_RMT_RECV_FUNC_IN_IRAM
#    define RMT_MEM_ALLOC_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#  else
#    define RMT_MEM_ALLOC_CAPS MALLOC_CAP_DEFAULT
#  endif
#endif

static char const* TAG = "rmt";

/// DCC encoder
typedef struct {
  rmt_encoder_t base;
  rmt_encoder_t* copy_encoder;
  rmt_encoder_t* bytes_encoder;
  rmt_symbol_word_t bidi_symbol;
  rmt_symbol_word_t one_symbol;
  rmt_symbol_word_t zero_symbol;
  rmt_symbol_word_t end_symbol;
  size_t num_preamble_symbols;
  size_t num_symbols;
  enum { BiDi, Zimo0, Preamble, Start, Data, End } state;
  struct {
    bool zimo0 : 1;
  } flags;
} rmt_dcc_encoder_t;

/// Encode single bit
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \param  symbol        Symbol representing current bit
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR
rmt_encode_dcc_bit(rmt_dcc_encoder_t* dcc_encoder,
                   rmt_channel_handle_t channel,
                   rmt_encode_state_t* ret_state,
                   rmt_symbol_word_t const* symbol) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;
  encoded_symbols += copy_encoder->encode(
    copy_encoder, channel, symbol, sizeof(rmt_symbol_word_t), &state);
  *ret_state = state;
  return encoded_symbols;
}

/// Encode BiDi cutout
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR rmt_encode_dcc_bidi(rmt_dcc_encoder_t* dcc_encoder,
                                                rmt_channel_handle_t channel,
                                                rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;

  // Skip
  if (!dcc_encoder->bidi_symbol.duration0) {
    state |= RMT_ENCODING_COMPLETE;
    dcc_encoder->state = Zimo0;
  }
  // Encode 4 BiDi cutout symbols
  else
    while (dcc_encoder->state == BiDi) {
      size_t const tmp = copy_encoder->encode(copy_encoder,
                                              channel,
                                              &dcc_encoder->bidi_symbol,
                                              sizeof(rmt_symbol_word_t),
                                              &state);
      encoded_symbols += tmp;
      dcc_encoder->num_symbols += tmp;
      if (state & RMT_ENCODING_COMPLETE &&
          dcc_encoder->num_symbols >= 8u / 2u) {
        dcc_encoder->num_symbols = 0u;
        dcc_encoder->state = Zimo0;
      }
      if (state & RMT_ENCODING_MEM_FULL) break;
    }

  *ret_state = state;
  return encoded_symbols;
}

/// Encode ZIMO 0
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR
rmt_encode_dcc_zimo0(rmt_dcc_encoder_t* dcc_encoder,
                     rmt_channel_handle_t channel,
                     rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;

  // Skip
  if (!dcc_encoder->flags.zimo0) {
    state |= RMT_ENCODING_COMPLETE;
    dcc_encoder->state = Preamble;
  }
  // Encode ZIMO 0
  else {
    encoded_symbols += rmt_encode_dcc_bit(
      dcc_encoder, channel, &state, &dcc_encoder->zero_symbol);
    if (state & RMT_ENCODING_COMPLETE) dcc_encoder->state = Preamble;
  }

  *ret_state = state;
  return encoded_symbols;
}

/// Encode preamble
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR
rmt_encode_dcc_preamble(rmt_dcc_encoder_t* dcc_encoder,
                        rmt_channel_handle_t channel,
                        rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t copy_encoder = dcc_encoder->copy_encoder;

  while (dcc_encoder->state == Preamble) {
    size_t const tmp = copy_encoder->encode(copy_encoder,
                                            channel,
                                            &dcc_encoder->one_symbol,
                                            sizeof(rmt_symbol_word_t),
                                            &state);
    encoded_symbols += tmp;
    dcc_encoder->num_symbols += tmp;
    if (state & RMT_ENCODING_COMPLETE &&
        dcc_encoder->num_symbols >= dcc_encoder->num_preamble_symbols) {
      dcc_encoder->num_symbols = 0u;
      dcc_encoder->state = Start;
    }
    if (state & RMT_ENCODING_MEM_FULL) break;
  }

  *ret_state = state;
  return encoded_symbols;
}

/// Encode packet start
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR
rmt_encode_dcc_start(rmt_dcc_encoder_t* dcc_encoder,
                     rmt_channel_handle_t channel,
                     rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  encoded_symbols +=
    rmt_encode_dcc_bit(dcc_encoder, channel, &state, &dcc_encoder->zero_symbol);
  if (state & RMT_ENCODING_COMPLETE) dcc_encoder->state = Data;
  *ret_state = state;
  return encoded_symbols;
}

/// Encode data
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  primary_data  App data to be encoded into RMT symbols
/// \param  data_size     Size of primary_data, in bytes
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR rmt_encode_dcc_data(rmt_dcc_encoder_t* dcc_encoder,
                                                rmt_channel_handle_t channel,
                                                void const* primary_data,
                                                size_t data_size,
                                                rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encoder_handle_t bytes_encoder = dcc_encoder->bytes_encoder;
  uint8_t const* data = (uint8_t const*)primary_data;

  size_t const tmp =
    bytes_encoder->encode(bytes_encoder,
                          channel,
                          &data[dcc_encoder->num_symbols / CHAR_BIT],
                          sizeof(uint8_t),
                          &state);
  encoded_symbols += tmp;
  dcc_encoder->num_symbols += tmp;
  if (state & RMT_ENCODING_COMPLETE &&
      dcc_encoder->num_symbols >= data_size * CHAR_BIT) {
    dcc_encoder->num_symbols = 0u;
    dcc_encoder->state = End;
  }

  *ret_state = state;
  return encoded_symbols;
}

/// Encode packet end
///
/// \param  dcc_encoder   DCC encoder handle
/// \param  channel       RMT TX channel handle
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR rmt_encode_dcc_end(rmt_dcc_encoder_t* dcc_encoder,
                                               rmt_channel_handle_t channel,
                                               rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0u;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  encoded_symbols +=
    rmt_encode_dcc_bit(dcc_encoder, channel, &state, &dcc_encoder->end_symbol);
  if (state & RMT_ENCODING_COMPLETE) {
    dcc_encoder->num_symbols = 0u;
    dcc_encoder->state = BiDi;
  }
  *ret_state = state;
  return encoded_symbols;
}

/// Encode the user data into RMT symbols and write into RMT memory
///
/// \param  encoder       Encoder handle
/// \param  channel       RMT TX channel handle
/// \param  primary_data  App data to be encoded into RMT symbols
/// \param  data_size     Size of primary_data, in bytes
/// \param  ret_state     Returned current encoder state
/// \return Number of RMT symbols that the primary data has been encoded into
static size_t RMT_IRAM_ATTR rmt_encode_dcc(rmt_encoder_t* encoder,
                                           rmt_channel_handle_t channel,
                                           void const* primary_data,
                                           size_t data_size,
                                           rmt_encode_state_t* ret_state) {
  size_t encoded_symbols = 0;
  rmt_encode_state_t state = RMT_ENCODING_RESET;
  rmt_encode_state_t session_state = RMT_ENCODING_RESET;
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);

  switch (dcc_encoder->state) {
    case BiDi:
      encoded_symbols +=
        rmt_encode_dcc_bidi(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Zimo0:
      encoded_symbols +=
        rmt_encode_dcc_zimo0(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Preamble:
      encoded_symbols +=
        rmt_encode_dcc_preamble(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Start:
    start:
      encoded_symbols +=
        rmt_encode_dcc_start(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough

    case Data:
      encoded_symbols += rmt_encode_dcc_data(
        dcc_encoder, channel, primary_data, data_size, &session_state);
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      if (dcc_encoder->state < End) goto start;
      // fallthrough

    case End:
      encoded_symbols +=
        rmt_encode_dcc_end(dcc_encoder, channel, &session_state);
      if (session_state & RMT_ENCODING_COMPLETE) state |= RMT_ENCODING_COMPLETE;
      if (session_state & RMT_ENCODING_MEM_FULL) {
        state |= RMT_ENCODING_MEM_FULL;
        break;
      }
      // fallthrough
  }

  *ret_state = state;
  return encoded_symbols;
}

/// Delete RMT DCC encoder
///
/// \param  encoder             RMT encoder handle
/// \retval ESP_OK              Delete RMT DCC encoder successfully
/// \retval ESP_ERR_INVALID_ARG Delete RMT DCC encoder failed because of invalid
///                             argument
/// \retval ESP_FAIL            Delete RMT DCC encoder failed because of other
///                             error
static esp_err_t rmt_del_dcc_encoder(rmt_encoder_t* encoder) {
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);
  rmt_del_encoder(dcc_encoder->copy_encoder);
  rmt_del_encoder(dcc_encoder->bytes_encoder);
  free(dcc_encoder);
  return ESP_OK;
}

/// Reset RMT DCC encoder
///
/// \param  encoder             RMT encoder handle
/// \retval ESP_OK              Reset RMT DCC encoder successfully
/// \retval ESP_ERR_INVALID_ARG Reset RMT DCC encoder failed because of invalid
///                             argument
/// \retval ESP_FAIL            Reset RMT DCC encoder failed because of other
///                             error
static esp_err_t RMT_IRAM_ATTR rmt_dcc_encoder_reset(rmt_encoder_t* encoder) {
  rmt_dcc_encoder_t* dcc_encoder =
    __containerof(encoder, rmt_dcc_encoder_t, base);
  rmt_encoder_reset(dcc_encoder->copy_encoder);
  rmt_encoder_reset(dcc_encoder->bytes_encoder);
  dcc_encoder->num_symbols = 0u;
  dcc_encoder->state = Zimo0;
  return ESP_OK;
}

/// Create RMT DCC encoder which encodes DCC byte stream into RMT symbols
///
/// \param  config              DCC encoder configuration
/// \param  ret_encoder         Returned encoder handle
/// \retval ESP_OK              Create RMT DCC encoder successfully
/// \retval ESP_ERR_INVALID_ARG Create RMT DCC encoder failed because of
///                             invalid argument
/// \retval ESP_ERR_NO_MEM      Create RMT DCC encoder failed because out of
///                             memory
/// \retval ESP_FAIL            Create RMT DCC encoder failed because of other
///                             error
esp_err_t rmt_new_dcc_encoder(dcc_encoder_config_t const* config,
                              rmt_encoder_handle_t* ret_encoder) {
  esp_err_t ret = ESP_OK;
  rmt_dcc_encoder_t* dcc_encoder = NULL;
  ESP_GOTO_ON_FALSE(
    config && ret_encoder &&                                        //
      config->num_preamble >= DCC_TX_MIN_PREAMBLE_BITS &&           //
      config->num_preamble <= DCC_TX_MAX_PREAMBLE_BITS &&           //
      (!config->bidibit_duration ||                                 //
       (config->bidibit_duration >= DCC_TX_MIN_BIDI_BIT_TIMING &&   //
        config->bidibit_duration <= DCC_TX_MAX_BIDI_BIT_TIMING)) && //
      config->bit1_duration >= DCC_TX_MIN_BIT_1_TIMING &&           //
      config->bit1_duration <= DCC_TX_MAX_BIT_1_TIMING &&           //
      config->bit0_duration >= DCC_TX_MIN_BIT_0_TIMING &&           //
      config->bit0_duration <= DCC_TX_MAX_BIT_0_TIMING &&           //
      config->endbit_duration <= DCC_TX_MAX_BIT_1_TIMING,           //
    ESP_ERR_INVALID_ARG,
    err,
    TAG,
    "invalid argument");
  dcc_encoder =
    heap_caps_calloc(1, sizeof(rmt_dcc_encoder_t), RMT_MEM_ALLOC_CAPS);
  ESP_GOTO_ON_FALSE(
    dcc_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for dcc encoder");

  dcc_encoder->base.encode = rmt_encode_dcc;
  dcc_encoder->base.del = rmt_del_dcc_encoder;
  dcc_encoder->base.reset = rmt_dcc_encoder_reset;

  rmt_copy_encoder_config_t copy_encoder_config = {};
  ESP_GOTO_ON_ERROR(
    rmt_new_copy_encoder(&copy_encoder_config, &dcc_encoder->copy_encoder),
    err,
    TAG,
    "create copy encoder failed");

  // Number of preamble symbols
  dcc_encoder->num_preamble_symbols = config->num_preamble;

  // Setup RMT symbols
  if (config->bidibit_duration)
    dcc_encoder->bidi_symbol = (rmt_symbol_word_t){
      .duration0 = config->bidibit_duration,
      .level0 = config->flags.level0,
      .duration1 = config->bidibit_duration,
      .level1 = !config->flags.level0,
    };
  dcc_encoder->one_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit1_duration,
    .level0 = config->flags.level0,
    .duration1 = config->bit1_duration,
    .level1 = !config->flags.level0,
  };
  dcc_encoder->zero_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit0_duration,
    .level0 = config->flags.level0,
    .duration1 = config->bit0_duration,
    .level1 = !config->flags.level0,
  };
  dcc_encoder->end_symbol = (rmt_symbol_word_t){
    .duration0 = config->bit1_duration,
    .level0 = config->flags.level0,
    .duration1 =
      config->endbit_duration ? config->endbit_duration : config->bit1_duration,
    .level1 = !config->flags.level0,
  };

  // Initial state
  dcc_encoder->state = Zimo0;

  // Flags
  dcc_encoder->flags.zimo0 = config->flags.zimo0;

  rmt_bytes_encoder_config_t bytes_encoder_config = {
    .bit1 = dcc_encoder->one_symbol,
    .bit0 = dcc_encoder->zero_symbol,
    .flags.msb_first = true,
  };
  ESP_GOTO_ON_ERROR(
    rmt_new_bytes_encoder(&bytes_encoder_config, &dcc_encoder->bytes_encoder),
    err,
    TAG,
    "create bytes encoder failed");

  *ret_encoder = &dcc_encoder->base;
  return ESP_OK;
err:
  if (dcc_encoder) {
    if (dcc_encoder->copy_encoder) rmt_del_encoder(dcc_encoder->copy_encoder);
    if (dcc_encoder->bytes_encoder) rmt_del_encoder(dcc_encoder->bytes_encoder);
    free(dcc_encoder);
  }
  return ret;
}