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
static TranslationBlock *my_tb_find_slow(CPUState *env,
                                         target_ulong pc,
                                         target_ulong cs_base,
                                         uint64_t flags)
{
    TranslationBlock *tb, **ptb1;
    unsigned int h;
    tb_page_addr_t phys_pc, phys_page1, phys_page2;
    target_ulong virt_page2;

    tb_invalidated_flag = 0;

    /* find translated block using physical mappings */
    phys_pc = get_page_addr_code(env, pc);
    phys_page1 = phys_pc & TARGET_PAGE_MASK;
    phys_page2 = -1;
    h = tb_phys_hash_func(phys_pc);
    ptb1 = &tb_phys_hash[h];
    for(;;) {
        tb = *ptb1;
        if (!tb)
            return tb;
        if (tb->pc == pc &&
            tb->page_addr[0] == phys_page1 &&
            tb->cs_base == cs_base &&
            tb->flags == flags) {
            /* check next page if needed */
            if (tb->page_addr[1] != -1) {
                virt_page2 = (pc & TARGET_PAGE_MASK) +
                    TARGET_PAGE_SIZE;
                phys_page2 = get_page_addr_code(env, virt_page2);
                if (tb->page_addr[1] == phys_page2)
                    goto found;
            } else {
                goto found;
            }
        }
        ptb1 = &tb->phys_hash_next;
    }

 found:
    /* we add the TB in the virtual pc hash table */
    env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)] = tb;
    return tb;
}

static inline TranslationBlock *my_tb_find_fast(CPUState *env, target_ulong pc)
{
    TranslationBlock *tb;
    target_ulong cs_base;
    int flags;

    /* we record a subset of the CPU state. It will
       always be the same before a given translated block
       is executed. */
    cpu_get_tb_cpu_state(env, &pc, &cs_base, &flags);
    tb = env->tb_jmp_cache[tb_jmp_cache_hash_func(pc)];
    if (unlikely(!tb || tb->pc != pc || tb->cs_base != cs_base ||
                 tb->flags != flags)) {
        tb = my_tb_find_slow(env, pc, cs_base, flags);
    }
    return tb;
}

//shadow_pair **shl;
//unsigned long *slot;

static inline void shack_init(CPUState *env)
{
    env->shack = (uint64_t *) malloc(SHACK_SIZE * sizeof(uint64_t));
    env->shadow_hash_list = malloc(MAX_CALL_SLOT * sizeof(shadow_pair*));
    memset(env->shadow_hash_list, 0, sizeof(shadow_pair*) * MAX_CALL_SLOT);
    env->shadow_ret_addr = malloc(MAX_CALL_SLOT * sizeof(unsigned long));

    env->shack_top = env->shack;
    env->shack_end = env->shack_top + SHACK_SIZE;
}

/*
 * shack_set_shadow()
 *  Insert a guest eip to host eip pair if it is not yet created.
 *  called when a new translation block is created
 *  called from function cpu_gen_code
 */
 void shack_set_shadow(CPUState *env, target_ulong guest_eip, unsigned long *host_eip)
{
    int idx = tb_jmp_cache_hash_func(guest_eip);
    shadow_pair *sp = ((shadow_pair**)env->shadow_hash_list)[idx];
    
    // the first sp should be null (set by memset in shack_init)
    while(sp){
        if(sp->guest_eip == guest_eip) {
            *sp->shadow_slot = (uintptr_t) host_eip;
            return;
        }
        sp = sp->prev_sp;
    }
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
 * insert_unresolved_eip()
 *  Store guest eip and unresolved eip
 */
inline void insert_unresolved_eip(CPUState *env, target_ulong next_eip, unsigned long *slot)
{
    int idx = tb_jmp_cache_hash_func(next_eip);
    *slot = 0;

    shadow_pair *sp = malloc(sizeof(shadow_pair));
    sp->guest_eip = next_eip;
    sp->shadow_slot = slot;
    // save current hash_list[idx] to sp->prev_sp
    sp->prev_sp = ((shadow_pair**)env->shadow_hash_list)[idx];
    // move top hash_list[idx] to sp
    ((shadow_pair**)env->shadow_hash_list)[idx] = sp;
}

/*
 * push_shack()
 *  Push next guest eip into shadow stack.
 */
void push_shack(CPUState *env, TCGv_ptr cpu_env, target_ulong next_eip)
{
    unsigned long *host_addr;
    TranslationBlock* tb;
    int label = gen_new_label();
    TCGv_ptr tp0 = tcg_temp_new_ptr();
    TCGv_ptr tp1 = tcg_temp_new_ptr();

    tcg_gen_ld_ptr(tp0, cpu_env, offsetof(CPUState, shack_top)); 
    tcg_gen_ld_ptr(tp1, cpu_env, offsetof(CPUState, shack_end)); 

    // if shack_top doesn't reach the shack_end, jump to label to push next_eip
    tcg_gen_brcond_ptr(TCG_COND_NE, tp0, tp1, label);

    // flush shack when it's full
    gen_helper_shack_flush(cpu_env);

    gen_set_label(label);

    // get a slot for host_addr
    host_addr = env->shadow_ret_addr++;
    // find whether the tb exists through my_tb_find_fast()
    tb = my_tb_find_fast(env, next_eip);
    if(tb)
        // store the addr to *host_addr if the tb exist
        *host_addr = (unsigned long) tb->tc_ptr;
    else
        // insert the next_eip and unresolved host_addr to shadow_hash_list
        insert_unresolved_eip(env, next_eip, host_addr);

    // reload shack_top to tp0 to avoid seg fault
    tcg_gen_ld_ptr(tp0, cpu_env, offsetof(CPUState, shack_top)); 

    // save host_addr to tp0[0]
    tcg_gen_st_tl(tcg_const_tl((int32_t)host_addr), tp0, 0);
    // save guest_addr to tp0[1]
    tcg_gen_st_tl(tcg_const_tl(next_eip), tp0, sizeof(target_ulong));

    // tp0 += 8
    tcg_gen_addi_ptr(tp0, tp0, sizeof(uint64_t));
    // store tp0 to shack_top
    tcg_gen_st_ptr(tp0, cpu_env, offsetof(CPUState, shack_top));

    tcg_temp_free_ptr(tp0);
    tcg_temp_free_ptr(tp1);
}

/*
 * pop_shack()
 *  Pop next host eip from shadow stack.
 */
void pop_shack(TCGv_ptr cpu_env, TCGv next_eip)
{
    TCGv_ptr tp0 = tcg_temp_local_new_ptr();
    TCGv_ptr tp1 = tcg_temp_local_new_ptr();
    TCGv t0 = tcg_temp_local_new();
    int label = gen_new_label();

    // load shack_top to tp0
    tcg_gen_ld_ptr(tp0, cpu_env, offsetof(CPUState, shack_top));
    // load guest_eip to t0
    tcg_gen_ld_tl(t0, tp0, -4);
    // if (guest_eip != next_eip) goto label;
    tcg_gen_brcond_tl(TCG_COND_NE, next_eip, t0, label);

    // load host_eip_ptr to tp1
    tcg_gen_ld_tl(tp1, tp0, -8);
    // load host_eip to t0 (*host_eip_ptr)
    tcg_gen_ld_tl(t0, tp1, 0);
    // if (host_eip == 0) goto l;
    tcg_gen_brcond_tl(TCG_COND_EQ, t0, tcg_const_tl(0), label);
    // tp0 -= 8;
    tcg_gen_subi_tl(tp0, tp0, 2*sizeof(target_ulong));
    // shack_top = tp0
    tcg_gen_st_ptr(tp0, cpu_env, offsetof(CPUState, shack_top));

    // jump to target address
    *gen_opc_ptr++ = INDEX_op_jmp;
    *gen_opparam_ptr++ = GET_TCGV_I32(t0);

    gen_set_label(label);

    tcg_temp_free_ptr(tp0);
    tcg_temp_free_ptr(tp1);
    tcg_temp_free(t0);
}

/*
 * Indirect Branch Target Cache
 */
__thread int update_ibtc;

struct ibtc_table *ibtc_tb;

/*
 * helper_lookup_ibtc()
 *  Look up IBTC. Return next host eip if cache hit or
 *  back-to-dispatcher stub address if cache miss.
 */
void *helper_lookup_ibtc(target_ulong guest_eip)
{
    uint16_t idx = guest_eip & IBTC_CACHE_MASK;
    struct jmp_pair *jp = &ibtc_tb->htable[idx];

    if (jp->guest_eip == guest_eip)
        return jp->tb->tc_ptr; // return to the taget tb

    update_ibtc = 1; // cache miss, turn on this flag to fill tb later
    return optimization_ret_addr;
}

/*
 * update_ibtc_entry()
 *  Populate eip and tb pair in IBTC entry.
 */
void update_ibtc_entry(TranslationBlock *tb)
{
    uint16_t idx = tb->pc & IBTC_CACHE_MASK;
    struct jmp_pair *jp = &ibtc_tb->htable[idx];

    // updating
    jp->guest_eip = tb->pc;
    jp->tb = tb;
    update_ibtc = 0; // finish updating, turn off this flag
}

/*
 * ibtc_init()
 *  Create and initialize indirect branch target cache.
 */
static inline void ibtc_init(CPUState *env)
{
    int i;
    ibtc_tb = malloc(sizeof(struct ibtc_table));
    for(i=0; i<IBTC_CACHE_SIZE; i++){
        ibtc_tb->htable[i].guest_eip = 0;
        ibtc_tb->htable[i].tb = NULL;
    }
    update_ibtc = 0;
}

/*
 * init_optimizations()
 *  Initialize optimization subsystem.
 */
int init_optimizations(CPUState *env)
{
    shack_init(env);
    ibtc_init(env);

    return 0;
}

/*
 * vim: ts=8 sts=4 sw=4 expandtab
 */
