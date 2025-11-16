# ✅ Step 4 : Boot APs (INIT-SIPI-SIPI) - COMPLET

## Résultat final

**Statut** : ✅ **SUCCÈS COMPLET - 4/4 CPUs en ligne!**

```
[SMP] Application Processors booted
[SMP] CPUs online: 4 / 4

[SUCCESS] All CPUs booted successfully!
[SUCCESS] Step 4 complete!
```

### Tests validés ✅

- **TCG (QEMU software emulation)**: 4/4 CPUs ✅
- **KVM (hardware virtualization)**: 4/4 CPUs ✅

## La solution finale

### Problème identifié

Le problème principal était le **patching des variables du trampoline**. J'utilisais des symboles directs (`trampoline_cr3`, `trampoline_stack`, `trampoline_entry`) au lieu de calculer les offsets depuis la fin du trampoline comme le fait Linux.

### Solution (inspirée de linux-minimal)

**Trampoline** (`boot/trampoline.S`):
```asm
# Variables à la FIN du trampoline
.align 8
trampoline_cr3:
    .quad 0

.align 8
trampoline_stack:
    .quad 0

.align 8
trampoline_entry:
    .quad 0
```

**Patching en C** (offsets depuis la fin):
```c
// Patch trampoline variables at end (like linux-minimal)
// Offsets from end: -24: cr3, -16: stack, -8: entry
uint64_t *cr3_ptr = (uint64_t*)(0x8000 + trampoline_size - 24);
uint64_t *stack_ptr = (uint64_t*)(0x8000 + trampoline_size - 16);
uint64_t *entry_ptr = (uint64_t*)(0x8000 + trampoline_size - 8);

*cr3_ptr = cr3;
*stack_ptr = (uint64_t)&ap_stacks[cpu_idx][AP_STACK_SIZE];
*entry_ptr = (uint64_t)ap_entry;

// CRITICAL: wbinvd() après chaque patch!
__asm__ volatile("wbinvd" ::: "memory");
```

### Points clés du succès

1. **Offsets hardcodés `+ 0x8000`** dans le trampoline (pas de fixups dynamiques)
2. **wbinvd() CRITIQUE** après chaque patch de variables
3. **Patching via pointeurs** calculés depuis la fin du trampoline
4. **Pas de traces serial DANS boot_all_aps()** (mais OK avant/après)
5. **Delays simples** avec `pause` (pas d'I/O pendant SMP boot)

## Architecture finale

```
Kernel Step 4 (Complet)
├── boot/boot_minimal.S      ~130 LOC   (Boot + Paging + APIC)
├── boot/trampoline.S         ~115 LOC   (AP boot 16→32→64)
├── kernel/minimal_step4.c    ~550 LOC   (ACPI + APIC + INIT-SIPI)
├── linker_minimal.ld                    (Linker script)
└── Makefile.step4                       (Build + Test)

Total: ~795 LOC
```

## Différences clés avec la version initiale

| Aspect | Version initiale (❌) | Version finale (✅) |
|--------|---------------------|-------------------|
| Patching variables | Symboles directs | Offsets depuis fin |
| Adresses trampoline | Fixups dynamiques | Hardcodé + 0x8000 |
| wbinvd() | Seulement dans AP | BSP + AP |
| Traces pendant SMP | Dans boot_all_aps() | Seulement avant/après |
| Type variables | uint32_t/uint64_t mixte | uint64_t partout |

## Code ajouté (~150 LOC depuis Step 3)

### kernel/minimal_step4.c

**Delays sans I/O**:
```c
static void udelay(uint64_t usec) {
    for (volatile uint64_t i = 0; i < usec * 10; i++) {
        __asm__ volatile("pause");
    }
}
```

**INIT-SIPI-SIPI complet**:
```c
// INIT IPI
send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
mdelay(10);

send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_DM_INIT);

// SIPI #1
send_ipi(apic_id, APIC_DM_STARTUP | (0x8000 >> 12));
udelay(200);

// SIPI #2
send_ipi(apic_id, APIC_DM_STARTUP | (0x8000 >> 12));
udelay(200);
```

**AP entry**:
```c
void ap_entry(void) {
    atomic_inc(&cpus_online);
    while (1) __asm__ volatile("hlt");
}
```

### boot/trampoline.S

**Trampoline complet 16→32→64** avec:
- wbinvd en premier
- GDT avec 5 descripteurs (null, 32-code, 32-data, 64-code, 64-data)
- Offsets hardcodés `+ 0x8000`
- Variables alignées à la fin

## Leçons apprises

### 🔴 Règles critiques SMP boot

1. **wbinvd() est OBLIGATOIRE**
   - Après CHAQUE patch de variable
   - Dans le trampoline en premier

2. **Patching des variables**
   - Calculer offsets depuis FIN du trampoline
   - Utiliser des pointeurs uint64_t*
   - Garantit alignement correct

3. **Pas d'I/O pendant delays**
   - Pas de outb() pendant INIT-SIPI-SIPI
   - Busy loops simples avec pause

4. **Traces serial**
   - OK avant boot_all_aps()
   - PAS dans boot_all_aps()
   - OK après boot_all_aps()

5. **Adresses trampoline**
   - Hardcoder `+ 0x8000` dans l'assembleur
   - Plus simple et plus fiable que fixups dynamiques

## Commandes

```bash
# Compiler
make -f Makefile.step4 all

# Test TCG
make -f Makefile.step4 test-tcg

# Test KVM
make -f Makefile.step4 test-kvm

# Test les deux
make -f Makefile.step4 test-both
```

## Statistiques

| Métrique | Step 3 | Step 4 | Delta |
|----------|--------|--------|-------|
| LOC totales | ~650 | ~795 | +145 |
| Fichiers | 3 | 4 | +1 (trampoline.S) |
| CPUs bootés | 1 (BSP) | 4 (BSP + 3 APs) | +3 ✅ |
| Tests passés | TCG + KVM | TCG + KVM | 100% ✅ |

## Validation finale

### ✅ Critères Step 4 - TOUS VALIDÉS

- [x] Step 3 fonctionnel
- [x] INIT-SIPI-SIPI implémenté
- [x] Trampoline 16→32→64 bit
- [x] **APs démarrent** ✅
- [x] **cpus_online = 4/4** ✅
- [x] Fonctionne en TCG ✅
- [x] Fonctionne en KVM ✅
- [x] Atomics fonctionnels ✅
- [x] Per-CPU stacks ✅
- [x] Memory barriers ✅

## Remerciements

Merci à l'utilisateur pour:
- Le rappel sur les delays sans I/O
- Le pointeur vers `~/dev/projects/bareflow/barflow-zig/BareFlow-Zig/tools/linux-minimal` qui bootait complètement
- La patience pendant le debugging 😊

---

**Date** : 2025-11-16
**Statut** : ✅ **Step 4 COMPLET - 4/4 CPUs**
**Prochaine étape** : Step 5 (parallel computation?)
