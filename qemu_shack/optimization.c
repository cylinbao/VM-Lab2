/*
 *  (C) 2010 by Computer System Laboratory, IIS, Academia Sinica, Taiwan.
 *      See COPYRIGHT in top-level directory.
 */

#include <stdlib.h>
#include "exec-all.h"
#include "tcg-op.h"
#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"
#include "optimization.h"

extern uint8_t *optimization_ret_addr;

/*
 * Shadow Stack
 */
list_t *shadow_hash_list;

static inline void shack_init(CPUState *env)
{
    env->shack = (uint64_t *) malloc(SHACK_SIZE * sizeof(uint64_t));
    env->shack_top = env->shack;
    env->shack_end = env->shack_top + (SHACK_SIZE-1) * sizeof(uint64_t);

    env->shadow_hash_list = (void *) malloc(SHACK_SIZE * sizeof(struct shadow_pair));

    env->shadow_ret_addr = (unsigned long *) malloc(SHACK_SIZE * sizeof(unsigned long));
    env->shadow_ret_count = 0;
}

/*
 * shack_set_shadow()
 *  Insert a guest eip to host eip pair if it is not yet created.
 *  called when a new translation block is created
 *  called from function cpu_gen_code
 */
 void shack_set_shadow(CPUState *env, target_ulong guest_eip, unsigned long *host_eip)
{
}

/*
 * helper_shack_flush()
 *  Reset shadow stack.
 */
void helper_shack_flush(CPUState *env)
{
    env->shack_top = env->shack;
}

/*
 * push_shack()
 *  Push next guest eip into shadow stack.
 */
void push_shack(CPUState *env, TCGv_ptr cpu_env, target_ulong next_eip)
{
    TCGv_ptr tp0 = tcg_temp_new_ptr();
    TCGv_ptr tp1 = tcg_temp_new_ptr();
    int label = gen_new_label();

    tcg_gen_ld_ptr(tp0, cpu_env, offsetof(CPUState, shack_top)); 
    tcg_gen_ld_ptr(tp1, cpu_env, offsetof(CPUState, shack_end)); 

    // if shack_top doesn't reach the shack_end, jump to push.
    tcg_gen_brcond_ptr(TCG_COND_NE, tp0, tp1, label);

    // flush shack when it's full
    gen_helper_shack_flush(cpu_env);

    gen_set_label(label);
    tcg_gen_st_tl(tcg_const_tl(next_eip), tp0, 0);

    tcg_gen_addi_i32(tp0, tp0, 4);
    tcg_gen_st_tl(tp0, cpu_env, offsetof(CPUState, shack_top));

    tcg_gen_ld_ptr(tp0, cpu_env, offsetof(CPUState, shadow_ret_count)); 
    tcg_gen_addi_i32(tp0, tp0, 1);
    tcg_gen_st_tl(tp0, cpu_env, offsetof(CPUState, shadow_ret_count));

    tcg_temp_free_ptr(tp0);
    tcg_temp_free_ptr(tp1);
}

/*
 * pop_shack()
 *  Pop next host eip from shadow stack.
 */
void pop_shack(TCGv_ptr cpu_env, TCGv next_eip)
{
}

/*
 * Indirect Branch Target Cache
 */
__thread int update_ibtc;

/*
 * helper_lookup_ibtc()
 *  Look up IBTC. Return next host eip if cache hit or
 *  back-to-dispatcher stub address if cache miss.
 */
void *helper_lookup_ibtc(target_ulong guest_eip)
{
    return optimization_ret_addr;
}

/*
 * update_ibtc_entry()
 *  Populate eip and tb pair in IBTC entry.
 */
void update_ibtc_entry(TranslationBlock *tb)
{
}

/*
 * ibtc_init()
 *  Create and initialize indirect branch target cache.
 */
static inline void ibtc_init(CPUState *env)
{
}

/*
 * init_optimizations()
 *  Initialize optimization subsystem.
 */
int init_optimizations(CPUState *env)
{
    shack_init(env);
    //ibtc_init(env);

    return 0;
}

/*
 * vim: ts=8 sts=4 sw=4 expandtab
 */
