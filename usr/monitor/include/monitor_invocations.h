/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, 2012, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef MONITOR_INVOCATIONS_H
#define MONITOR_INVOCATIONS_H

bool monitor_can_send_cap(struct capability *cap);
errval_t monitor_cap_identify(struct capref cap, struct capability *out);
errval_t monitor_domains_cap_identify(struct capref croot, capaddr_t cap,
                                      int vbits, struct capability *out);
errval_t monitor_cap_remote(struct capref cap, bool is_remote, bool * has_dep);
errval_t monitor_cap_create(struct capref dest, struct capability *cap,
                            coreid_t owner);
errval_t monitor_identify_cnode_get_cap(struct capability *cnode_raw, 
                                        capaddr_t slot, struct capability *ret);
errval_t monitor_nullify_cap(struct capref cap);
errval_t monitor_retype_remote_cap(struct capref croot, 
                                   capaddr_t src, enum objtype newtype, 
                                   int objbits, capaddr_t to, capaddr_t slot, 
                                   int bits);
errval_t monitor_delete_remote_cap(struct capref croot, capaddr_t src, int bits);
errval_t monitor_revoke_remote_cap(struct capref croot, capaddr_t src, int bits);

#endif
