/*
 * Copyright (c) 2014-2016 Cesanta Software Limited
 * All rights reserved
 */

#include "fw/src/mgos_atca.h"

#if MGOS_ENABLE_RPC && MGOS_ENABLE_ATCA && MGOS_ENABLE_ATCA_SERVICE

#include "fw/src/mgos_rpc.h"

#include "cryptoauthlib.h"

static void mgos_atca_get_config(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi,
                                 struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    goto clean;
  }

  uint8_t config[ATCA_CONFIG_SIZE];
  for (int i = 0; i < ATCA_CONFIG_SIZE / ATCA_BLOCK_SIZE; i++) {
    int offset = (i * ATCA_BLOCK_SIZE);
    ATCA_STATUS status =
        atcab_read_zone(ATCA_ZONE_CONFIG, 0 /* slot */, i /* block */,
                        0 /* offset */, config + offset, ATCA_BLOCK_SIZE);
    if (status != ATCA_SUCCESS) {
      mg_rpc_send_errorf(ri, 500, "Failed to read config zone block %d: 0x%02x",
                         i, status);
      ri = NULL;
      goto clean;
    }
  }

  mg_rpc_send_responsef(ri, "{config: %V}", config, sizeof(config));
  ri = NULL;

clean:
  (void) cb_arg;
  (void) args;
}

static void mgos_atca_set_config(struct mg_rpc_request_info *ri, void *cb_arg,
                                 struct mg_rpc_frame_info *fi,
                                 struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    return;
  }

  uint8_t *config = NULL;
  uint32_t config_len = 0;
  json_scanf(args.p, args.len, ri->args_fmt, &config, &config_len);

  if (config_len != ATCA_CONFIG_SIZE) {
    mg_rpc_send_errorf(ri, 400, "Expected %d bytes, got %d",
                       (int) ATCA_CONFIG_SIZE, (int) config_len);
    ri = NULL;
    goto clean;
  }

  ATCA_STATUS status = atcab_write_config_zone(config);
  if (status != ATCA_SUCCESS) {
    mg_rpc_send_errorf(ri, 500, "Failed to set config: 0x%02x", status);
    ri = NULL;
    goto clean;
  }

  mg_rpc_send_responsef(ri, NULL);
  ri = NULL;

clean:
  if (config != NULL) free(config);
  (void) cb_arg;
}

static void mgos_atca_lock_zone(struct mg_rpc_request_info *ri, void *cb_arg,
                                struct mg_rpc_frame_info *fi,
                                struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    return;
  }

  int zone = -1;
  json_scanf(args.p, args.len, ri->args_fmt, &zone);

  ATCA_STATUS status;
  uint8_t lock_response = 0;
  switch (zone) {
    case LOCK_ZONE_CONFIG:
      status = atcab_lock_config_zone(&lock_response);
      break;
    case LOCK_ZONE_DATA:
      status = atcab_lock_data_zone(&lock_response);
      break;
    default:
      mg_rpc_send_errorf(ri, 403, "Invalid zone");
      ri = NULL;
      goto clean;
  }

  if (status != ATCA_SUCCESS) {
    mg_rpc_send_errorf(ri, 500, "Failed to lock zone %d: 0x%02x", zone, status);
    ri = NULL;
    goto clean;
  }

  mg_rpc_send_responsef(ri, NULL);
  ri = NULL;

clean:
  (void) cb_arg;
}

static void mgos_atca_set_key(struct mg_rpc_request_info *ri, void *cb_arg,
                              struct mg_rpc_frame_info *fi,
                              struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    return;
  }

  int slot = -1;
  uint8_t *key = NULL, *write_key = NULL;
  uint32_t key_len = 0, write_key_len = 0;
  uint32_t wk_slot = 0;
  bool is_ecc = false;
  json_scanf(args.p, args.len, ri->args_fmt, &slot, &is_ecc, &key, &key_len,
             &write_key, &write_key_len, &wk_slot);

  if (slot < 0 || slot > 15 || (is_ecc && slot > 7)) {
    mg_rpc_send_errorf(ri, 400, "Invalid slot");
    ri = NULL;
    goto clean;
  }

  uint32_t exp_key_len = (is_ecc ? ATCA_PRIV_KEY_SIZE : ATCA_KEY_SIZE);
  if (key_len != exp_key_len) {
    mg_rpc_send_errorf(ri, 400, "Expected %d bytes, got %d", (int) exp_key_len,
                       (int) key_len);
    ri = NULL;
    goto clean;
  }

  ATCA_STATUS status;
  if (is_ecc) {
    uint8_t key_arg[4 + ATCA_PRIV_KEY_SIZE];
    memset(key_arg, 0, 4);
    memcpy(key_arg + 4, key, ATCA_PRIV_KEY_SIZE);
    status = atcab_priv_write(slot, key_arg, wk_slot,
                              (write_key_len == 32 ? write_key : NULL));
  } else {
    status = atcab_write_zone(ATCA_ZONE_DATA, slot, 0, 0, key, key_len);
  }
  if (status != ATCA_SUCCESS) {
    mg_rpc_send_errorf(ri, 500, "Failed to set key: 0x%02x", status);
    ri = NULL;
    goto clean;
  }

  mg_rpc_send_responsef(ri, NULL);
  ri = NULL;

clean:
  if (key != NULL) free(key);
  if (write_key != NULL) free(write_key);
  (void) cb_arg;
}

static void mgos_atca_get_or_gen_key(struct mg_rpc_request_info *ri,
                                     void *cb_arg, struct mg_rpc_frame_info *fi,
                                     struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    return;
  }

  uint8_t pubkey[ATCA_PUB_KEY_SIZE];
  int slot = -1;
  json_scanf(args.p, args.len, ri->args_fmt, &slot);

  if (slot < 0 || slot > 15) {
    mg_rpc_send_errorf(ri, 400, "Invalid slot");
    ri = NULL;
    goto clean;
  }

  if (strcmp((const char *) cb_arg, "ATCA.GenKey") == 0) {
    ATCA_STATUS status = atcab_genkey(slot, pubkey);
    if (status != ATCA_SUCCESS) {
      mg_rpc_send_errorf(ri, 500, "Failed generate key on slot %d: 0x%02x",
                         slot, status);
      ri = NULL;
      goto clean;
    }
  } else {
    ATCA_STATUS status = atcab_get_pubkey(slot, pubkey);
    if (status != ATCA_SUCCESS) {
      mg_rpc_send_errorf(ri, 500, "Failed get public key for slot %d: 0x%02x",
                         slot, status);
      ri = NULL;
      goto clean;
    }
  }

  mg_rpc_send_responsef(ri, "{pubkey: %V}", pubkey, sizeof(pubkey));
  ri = NULL;

clean:
  return;
}

static void mgos_atca_sign(struct mg_rpc_request_info *ri, void *cb_arg,
                           struct mg_rpc_frame_info *fi, struct mg_str args) {
  if (!fi->channel_is_trusted) {
    mg_rpc_send_errorf(ri, 403, "unauthorized");
    ri = NULL;
    return;
  }

  int slot = -1;
  uint8_t *digest = NULL;
  uint32_t digest_len = 0;
  json_scanf(args.p, args.len, ri->args_fmt, &slot, &digest, &digest_len);

  if (slot < 0 || slot > 7) {
    mg_rpc_send_errorf(ri, 400, "Invalid slot");
    ri = NULL;
    goto clean;
  }

  if (digest_len != 32) {
    mg_rpc_send_errorf(ri, 400, "Expected %d bytes, got %d", 32,
                       (int) digest_len);
    ri = NULL;
    goto clean;
  }

  uint8_t signature[ATCA_SIG_SIZE];
  ATCA_STATUS status = atcab_sign(slot, digest, signature);
  if (status != ATCA_SUCCESS) {
    mg_rpc_send_errorf(ri, 500, "Failed to sign: 0x%02x", status);
    ri = NULL;
    goto clean;
  }

  mg_rpc_send_responsef(ri, "{signature: %V}", signature, sizeof(signature));
  ri = NULL;

clean:
  if (digest != NULL) free(digest);
  (void) cb_arg;
  return;
}

enum mgos_init_result mgos_atca_service_init(void) {
  struct mg_rpc *c = mgos_rpc_get_global();
  if (c == NULL) return MGOS_INIT_OK; /* RPC is disabled. */
  if (!get_cfg()->sys.atca.enable || !mbedtls_atca_is_available()) {
    return MGOS_INIT_OK;
  }
  mg_rpc_add_handler(c, "ATCA.GetConfig", "", mgos_atca_get_config, NULL);
  mg_rpc_add_handler(c, "ATCA.SetConfig", "{config: %V}", mgos_atca_set_config,
                     NULL);
  mg_rpc_add_handler(c, "ATCA.LockZone", "{zone: %d}", mgos_atca_lock_zone,
                     NULL);
  mg_rpc_add_handler(c, "ATCA.SetKey",
                     "{slot:%d, ecc:%B, key:%V, wkey:%V, wkslot:%u}",
                     mgos_atca_set_key, NULL);
  mg_rpc_add_handler(c, "ATCA.GenKey", "{slot: %d}", mgos_atca_get_or_gen_key,
                     "ATCA.GenKey");
  mg_rpc_add_handler(c, "ATCA.GetPubKey", "{slot: %d}",
                     mgos_atca_get_or_gen_key, "ATCA.GetPubKey");
  mg_rpc_add_handler(c, "ATCA.Sign", "{slot: %d, digest: %V}", mgos_atca_sign,
                     NULL);
  return MGOS_INIT_OK;
}

#endif /* MGOS_ENABLE_RPC && MGOS_ENABLE_ATCA && MGOS_ENABLE_ATCA_SERVICE */
