/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* Synth modules (module_store.c), USB only. 84:
 *   [0] list: rc, skipped (1: this boot left the modules out), NMOD, then per slot: state (MS_*), engine,
 *       version (u14), length (u35), name (string)
 *   [1, slot, length u35] begin an upload; [2, slot, offset in 3 x 7 bits, packed bytes (at most 256)] its next bytes;
 *   [3, slot] commit: rc, the slot's state and engine; [4, slot] delete.
 * rc: 0 ok, 1 bad request, 2 storage full, 3 flash error, 4 busy (the transport would not stop), 5 not a module for
 * this firmware, 6 its engine number is taken, 7 no room in RAM (stored, not loaded). */
#define ED_MODULE 84u
static uint32_t ed_mod_rc(int rc)
{
    return rc == MS_LOADED || rc == MS_EMPTY ? 0u : rc == -5 ? 2u : rc == -2 || rc == MS_BAD ? 5u :
           rc == -3 || rc == MS_CLASH ? 6u : rc == MS_NORAM ? 7u : rc == MS_SKIPPED ? 0u : rc == -1 ? 1u : 3u;
}
static int ed_module_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (cmd != ED_MODULE) return 0;
    uint32_t op = n ? a[0] : 127u, k = n > 1u ? a[1] : NMOD, rc = 1;
    if (op == 0u && n == 1u) {
        ed_b(0); ed_b(mod_skipped); ed_b(NMOD);
        for (k = 0; k < NMOD; k++) {
            ed_b(mods[k].state); ed_b(mods[k].engine); ed_u14(mods[k].version); ed_u35(mods[k].len);
            ed_str(mods[k].name, MOD_NAME_MAX);
        }
        return 1;
    }
    if (k < NMOD && op >= 1u && op <= 4u) {
        if (ed_flash_stop()) rc = 4;
        else if (op == 1u && n == 7u)
            rc = ed_mod_rc(mod_put_begin(k, a[2] | (uint32_t)a[3] << 7 | (uint32_t)a[4] << 14 |
                                            (uint32_t)a[5] << 21 | (uint32_t)a[6] << 28));
        else if (op == 2u && n > 5u) {
            uint32_t m = ed_unpack7(a + 5, n - 5u, ed_transfer_buf, 256u);
            rc = m ? ed_mod_rc(mod_put_write(k, a[2] | (uint32_t)a[3] << 7 | (uint32_t)a[4] << 14, ed_transfer_buf, m))
                   : 1u;
        } else if (op == 3u && n == 2u) {
            rc = ed_mod_rc(mod_put_commit(k));
            ed_b(rc); ed_b(mods[k].state); ed_b(mods[k].engine);
            return 1;
        } else if (op == 4u && n == 2u)
            rc = ed_mod_rc(mod_delete(k));
    }
    ed_b(rc);
    return 1;
}
