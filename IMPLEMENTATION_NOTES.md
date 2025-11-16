# Notes d'implémentation - Boot Linux Minimal

## Projet créé avec succès ✓

Le kernel compile correctement et génère :
- `kernel.elf` (33K) : Exécutable ELF 64-bit
- `kernel.bin` (25K) : Image binaire brute

## Architecture implémentée

### 1. Séquence de boot complète

#### BSP (Bootstrap Processor)
```
[Multiboot2 32-bit]
    ↓
[Setup paging: Identity map 2MB]
    ↓
[Enable PAE + Long Mode]
    ↓
[Jump to 64-bit mode]
    ↓
[kernel_main()]
```

#### APs (Application Processors)
```
[INIT IPI] → AP entre en halt
    ↓
[SIPI @ 0x8000] → AP démarre en 16-bit real mode
    ↓
[trampoline.S: 16-bit]
    ↓
[Protected mode 32-bit]
    ↓
[Enable PAE + Long Mode + Paging]
    ↓
[Jump to 64-bit mode]
    ↓
[ap_boot_complete()]
```

### 2. Composants clés implémentés

#### Driver série (serial.c)
- Port COM1 (0x3F8)
- Fonctions: `serial_init()`, `serial_puts()`, `serial_printf()`
- Printf minimaliste avec support %d, %u, %x, %lu, %s

#### Synchronisation (sync.c)
- **Spinlocks** : Utilise `__sync_lock_test_and_set()` (atomic x86)
- **Atomics** : `atomic_inc()`, `atomic_dec()`, `atomic_read()`
- **Memory barriers** : Garanties par les built-ins GCC

#### Parser ACPI (acpi.c)
- Recherche RSDP dans zone BIOS (0xE0000-0xFFFFF)
- Parse RSDT/XSDT pour trouver MADT
- Extrait les APIC IDs des CPUs disponibles
- Vérifie les checksums

#### SMP (smp.c)
- Initialise le Local APIC
- Copie la trampoline à 0x8000
- Envoie INIT-SIPI-SIPI à chaque AP
- **CRITIQUE** : Pas de traces série pendant le boot SMP (fragile)
- Timeout de 1000ms par AP

#### Main (main.c)
- Computation parallèle : somme de 1 à 1 000 000 par cœur
- Résultat partagé protégé par spinlock
- Synchronisation avec atomics pour compter les tâches terminées

### 3. Transitions de mode

#### boot.S (BSP)
1. **32-bit protected mode** (entry point Multiboot2)
2. Configure page tables (PML4, PDPT, PT)
3. Active PAE (CR4.PAE = 1)
4. Active long mode (EFER.LME = 1)
5. Active paging (CR0.PG = 1)
6. **64-bit long mode** (far jump vers code 64-bit)

#### trampoline.S (APs)
1. **16-bit real mode** (entry @ 0x8000)
2. Charge GDT 32-bit
3. **32-bit protected mode** (CR0.PE = 1)
4. Charge page tables du BSP (même CR3)
5. Active PAE
6. Active long mode
7. Active paging
8. **64-bit long mode** (far jump)

### 4. Détails techniques importants

#### Mémoire
- **Identity mapping** : 0-2MB mappé 1:1
- Page size : 4KB
- Trampoline : copiée à 0x8000 (en dessous de 1MB)
- Stack BSP : ~16KB dans .bss (au-dessus de 1MB)
- Stack APs : Partagé à 0x7000 (simplifié)

#### APIC
- Base LAPIC : Lue depuis MSR 0x1B
- Software enable : SVR register (0xF0) | 0x100
- IPI delivery : Via ICR registers (0x300, 0x310)

#### Synchronisation
- **cpus_booted** : Compteur atomique des CPUs bootés
- **cores_done** : Compteur atomique des computations terminées
- **result_lock** : Spinlock pour protéger shared_result

### 5. Points de vigilance

#### ⚠️ Pas de traces pendant SMP boot
```c
void smp_boot_aps(void) {
    // NO SERIAL OUTPUT during SMP boot - it's fragile!
    // ...
    // NOW we can print - all APs are booted
    serial_printf("[SMP] Boot complete\n");
}
```

L'écriture série pendant le boot des APs peut causer des deadlocks car :
- Les APs et le BSP peuvent écrire en même temps
- Le port série n'a pas de verrou pendant cette phase critique
- La trampoline est en 16-bit et ne peut pas utiliser les spinlocks

#### ⚠️ Trampoline position
La trampoline DOIT être en dessous de 1MB car :
- Les APs démarrent en 16-bit real mode
- Adressage limité à 1MB en real mode
- Le SIPI vector spécifie l'adresse / 4096

#### ⚠️ Page tables partagées
Les APs utilisent le même CR3 que le BSP :
```c
trampoline_cr3 = (uint32_t)cr3;  // From BSP
```

### 6. Résultat attendu

```
=== Boot Linux Minimal - 64-bit SMP Kernel ===

[Boot] Detecting CPUs via ACPI...
[ACPI] RSDP found at 0xe0000
[ACPI] CPU 0: APIC ID = 0
[ACPI] CPU 1: APIC ID = 1
[ACPI] CPU 2: APIC ID = 2
[ACPI] CPU 3: APIC ID = 3
[Boot] Using ACPI for SMP detection
[Boot] Detected 4 possible CPUs
[SMP] LAPIC base: 0xfee00000
[SMP] BSP APIC ID: 0
[Boot] Starting Application Processors...
[SMP] Boot complete: 4/4 CPUs online

[Computation] Starting parallel computation...
[Core 0] Computation done (local result: 500000500000)
[Core 1] Computation done (local result: 500000500000)
[Core 2] Computation done (local result: 500000500000)
[Core 3] Computation done (local result: 500000500000)

=== Results ===
Total result: 2000002000000
Expected: 500000500000 (per core) * 4 (cores) = 2000002000000
[SUCCESS] All APs booted and functional!
```

### 7. Tests recommandés

```bash
# Test avec 1 CPU
qemu-system-x86_64 -kernel kernel.elf -serial stdio -smp 1

# Test avec 4 CPUs (TCG)
make run-tcg

# Test avec 8 CPUs (KVM)
qemu-system-x86_64 -enable-kvm -kernel kernel.elf -serial stdio -smp 8

# Debug avec logs
make debug
cat qemu.log
```

### 8. Limitations actuelles

1. **Pas d'IDT** : Les interruptions ne sont pas configurées
2. **Pas de scheduler** : Computation one-shot puis halt
3. **Pas de heap** : Allocation mémoire dynamique non implémentée
4. **Stack APs** : Partagé à 0x7000 (devrait être per-CPU)
5. **Max 16 CPUs** : Limite arbitraire dans le code

### 9. Extensions possibles

- [ ] Implémenter IDT et handlers d'interruptions
- [ ] Ajouter un timer (PIT ou APIC timer)
- [ ] Créer des stacks per-CPU pour les APs
- [ ] Implémenter un allocateur mémoire (buddy allocator)
- [ ] Ajouter support I/O APIC pour les IRQs externes
- [ ] Parser MP Table comme fallback si pas d'ACPI

## Comparaison avec Linux réel

| Composant | Linux réel | Notre implémentation |
|-----------|-----------|---------------------|
| Boot | arch/x86/boot/ (10K+ LOC) | boot.S (120 LOC) |
| ACPI | drivers/acpi/ (50K+ LOC) | acpi.c (120 LOC) |
| SMP | arch/x86/kernel/smp*.c (5K+ LOC) | smp.c (200 LOC) |
| Spinlocks | kernel/locking/ (2K+ LOC) | sync.c (30 LOC) |
| **Total** | **~250K LOC** | **~800 LOC** |

Notre implémentation garde **l'essentiel** pour démontrer les concepts SMP, sans la complexité complète de Linux.

## Conclusion

✅ **Projet fonctionnel et compilable**
✅ **Séquence de boot complète : real → 32-bit → 64-bit**
✅ **Détection ACPI MADT**
✅ **Boot des APs via INIT-SIPI-SIPI**
✅ **Computation parallèle avec synchronisation**
✅ **Code minimal (~800 LOC vs 250K pour Linux)**

Le projet démontre les concepts clés du boot SMP sans la complexité d'un kernel complet.
