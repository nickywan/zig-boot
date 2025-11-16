# ⚠️ Step 4 : Boot APs (INIT-SIPI-SIPI) - PARTIEL

## État actuel

**Statut** : ⚠️ Partiel - INIT-SIPI implémenté, APs causent triple fault

### Ce qui fonctionne ✅

```
===========================================
  Step 4: Boot APs (INIT-SIPI-SIPI)
===========================================

[OK] Serial port initialized (COM1)
[OK] Running in 64-bit long mode

[TSC] Calibrating Time Stamp Counter...
[TSC] TSC frequency: 2000000 kHz

[ACPI] Searching for RSDP...
[ACPI] RSDP found!
[ACPI] Searching for MADT...
[ACPI] MADT found!
[ACPI] Parsing MADT entries...
[ACPI] CPU 0 detected (APIC ID 0)
[ACPI] CPU 1 detected (APIC ID 1)
[ACPI] CPU 2 detected (APIC ID 2)
[ACPI] CPU 3 detected (APIC ID 3)

[ACPI] Detected 4 CPU(s)

[APIC] Initializing Local APIC...
[APIC] Physical address: 0xFEE00000 (default)
[APIC] Enabling APIC (SVR register)...
[APIC] BSP APIC ID: 0
[APIC] Local APIC initialized successfully!

[SMP] Setting up trampoline...
[SMP] Trampoline size: 216 bytes
[SMP] Trampoline copied to 0x8000
[SMP] Trampoline configured

[SMP] Application Processors booted
[SMP] CPUs online: 1 / 4

[WARNING] Not all CPUs came online
```

### Fonctionnalités validées ✅

- [x] Step 1-3 fonctionnels (ACPI + APIC + Trampoline)
- [x] TSC calibration (estimation fixe 2 GHz)
- [x] INIT IPI envoyé avec succès (BSP ne crash pas)
- [x] SIPI envoyé avec séquence Linux (APIC_DM_STARTUP | start_eip >> 12)
- [x] Memory barriers (mfence)
- [x] Pas de traces serial pendant SMP boot (fragile!)
- [x] Compteur atomique CPUs online
- [x] Per-CPU stacks alloués (8KB × 16)

### Problème identifié ❌

**APs causent triple fault au démarrage**

- INIT IPI fonctionne ✅
- SIPI envoyé correctement ✅
- APs démarrent à 0x8000 via trampoline
- **Triple fault pendant transition 16→32→64 bit** ❌

**Cause probable** : Adresses relatives dans `boot/trampoline.S` mal calculées

## Code ajouté (~140 LOC)

### kernel/minimal_step4.c (~540 LOC total)

**Calibration TSC** :
```c
static void calibrate_tsc(void) {
    // Fixed estimate - 2 GHz
    tsc_khz = 2000000;
}
```

**Atomics** :
```c
static volatile uint32_t cpus_online = 0;

static inline void atomic_inc(volatile uint32_t *ptr) {
    __asm__ volatile("lock incl %0" : "+m"(*ptr) : : "memory");
}
```

**INIT-SIPI-SIPI (séquence Linux)** :
```c
static void boot_ap(int cpu_idx) {
    uint8_t apic_id = cpu_apic_ids[cpu_idx];
    unsigned long start_eip = 0x8000;

    trampoline_stack = (uint64_t)&ap_stacks[cpu_idx][AP_STACK_SIZE];

    // INIT assert
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_INT_ASSERT | APIC_DM_INIT);
    apic_wait_icr();

    // Delay
    for (volatile int i = 0; i < 10000; i++) __asm__ volatile("pause");

    // INIT deassert
    send_ipi(apic_id, APIC_INT_LEVELTRIG | APIC_DM_INIT);
    apic_wait_icr();

    // Memory barrier
    __asm__ volatile("mfence" ::: "memory");

    // SIPI #1 - NOTE: start_eip >> 12 !
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();

    // Delay
    for (volatile int i = 0; i < 1000; i++) __asm__ volatile("pause");

    // SIPI #2
    send_ipi(apic_id, APIC_DM_STARTUP | (start_eip >> 12));
    apic_wait_icr();
}
```

**Boot all APs (SANS traces serial!)** :
```c
static void boot_all_aps(void) {
    // NO SERIAL OUTPUT during SMP boot - it's fragile!

    cpus_online = 1;  // BSP

    for (int i = 1; i < cpu_count; i++) {
        boot_ap(i);
    }

    // Wait
    for (volatile int i = 0; i < 1000000; i++) __asm__ volatile("pause");

    // NOW print (after SMP boot)
    puts("\n[SMP] Application Processors booted\n");
    puts("[SMP] CPUs online: ");
    print_dec(cpus_online);
    puts(" / ");
    print_dec(cpu_count);
    puts("\n");
}
```

**AP entry** :
```c
void ap_entry(void) {
    // Simplest possible - just increment and halt
    atomic_inc(&cpus_online);
    while (1) __asm__ volatile("hlt");
}
```

## Problèmes rencontrés et leçons

### 1. Traces serial pendant SMP boot ❌→✅

**Problème** : Kernel reboote en boucle avec traces pendant boot APs
**Cause** : "il faut faut pas tracer le smpboot car très fragile" (user)
**Solution** : AUCUNE trace serial entre début et fin de boot_all_aps()

### 2. TSC delays causent reboot ❌→✅

**Problème** : `udelay()`/`mdelay()` avec TSC causent reboot
**Cause** : Calibration TSC complexe avec PIT bloque
**Solution** : Busy loops simples avec `pause`

### 3. print_hex() crash ❌ (non résolu)

**Problème** : Affichage hex cause reboots
**Solution temporaire** : Désactivé l'affichage hex

### 4. SIPI vector format ❌→✅

**Erreur initiale** : `ICR_STARTUP | 0x08`
**Correct** : `APIC_DM_STARTUP | (0x8000 >> 12)`
Le vector SIPI doit être l'adresse divisée par 4096 (shift de 12 bits)

### 5. Trampoline adresses relatives ❌ (en cours)

**Problème** : APs causent triple fault au boot
**Cause probable** : Adresses relatives mal calculées dans trampoline.S
**Solution** : Utiliser le trampoline de Linux (adresses relatives correctes)

## Architecture actuelle

```
Kernel Step 4 (Partiel)
├── boot/boot_minimal.S      ~130 LOC   (Boot + Paging + APIC)
├── boot/trampoline.S         ~120 LOC   (AP boot - BUGGY)
├── kernel/minimal_step4.c    ~540 LOC   (ACPI + APIC + INIT-SIPI)
├── linker_minimal.ld                    (Linker script)
└── Makefile.step4                       (Build + Test)

Total: ~790 LOC
```

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

## Prochaine étape : Corriger le trampoline

### Problème du trampoline actuel

Le trampoline `boot/trampoline.S` utilise des adresses relatives à 0x8000 :

```asm
lgdtl (trampoline_gdt_ptr - trampoline_start + 0x8000)
```

**Problèmes potentiels** :
1. GDT mal alignée
2. Adresses 64-bit calculées incorrectement
3. Variables (CR3, stack, entry) pas à la bonne position

### Solution : Trampoline de Linux

Linux utilise `arch/x86/realmode/rm/trampoline_64.S` avec :
- Adressage segment:offset en 16-bit
- GDT construite dynamiquement
- Variables en fin de page (alignement garanti)

**À faire** :
1. Extraire `trampoline_64.S` de Linux
2. Simplifier (retirer init IDT, etc.)
3. Adapter pour notre kernel minimal
4. Tester TCG + KVM

## Détails techniques

### INIT-SIPI-SIPI Sequence

| Étape | IPI | Délai | Description |
|-------|-----|-------|-------------|
| 1 | INIT assert | - | Reset AP |
| 2 | Delay | ~10 µs | Wait for INIT |
| 3 | INIT deassert | - | Clear INIT |
| 4 | Memory barrier | - | Ensure order |
| 5 | SIPI #1 | - | Start AP at vector |
| 6 | Delay | ~10 µs | Wait for boot |
| 7 | SIPI #2 | - | Retry (some CPUs need it) |

### SIPI Vector Calculation

```
Address: 0x8000
Vector = Address >> 12 = 0x8000 >> 12 = 0x8
IPI = APIC_DM_STARTUP | 0x08
```

**Pourquoi >> 12 ?**
Le vector SIPI est multiplié par 4096 (0x1000) par le CPU :
- Vector 0x08 → CPU démarre à 0x08 × 0x1000 = 0x8000

### Memory Barriers

Linux utilise `mb()` après INIT deassert :
```c
mb();  // Memory barrier - ensure all writes complete
```

Notre implémentation :
```c
__asm__ volatile("mfence" ::: "memory");
```

## Statistiques

| Métrique | Step 3 | Step 4 | Delta |
|----------|--------|--------|-------|
| LOC totales | ~650 | ~790 | +140 |
| Fonctionnalités | Trampoline | + INIT-SIPI | +INIT-SIPI |
| CPUs bootés | 1 (BSP) | 1 (APs crash) | 0 |
| Tests passés | TCG + KVM | Partiel | ⚠️ |

## Validation

### ✅ Critères partiels Step 4

- [x] Step 3 fonctionnel
- [x] TSC calibration
- [x] INIT IPI envoyé
- [x] SIPI envoyé (format correct)
- [x] Séquence conforme Linux
- [x] Pas de traces pendant SMP
- [x] Memory barriers
- [ ] **APs démarrent** ❌ (triple fault)
- [ ] **cpus_online > 1** ❌
- [ ] Fonctionne en TCG ⚠️ (BSP OK)
- [ ] Fonctionne en KVM ⚠️ (BSP OK)

## Leçons apprises

### 🔴 Règles critiques SMP boot

1. **JAMAIS de traces serial pendant SMP boot**
   - Très fragile, perturbe la synchronisation
   - Traces uniquement AVANT et APRÈS

2. **Memory barriers obligatoires**
   - `mfence` après INIT deassert
   - `wmb()` avant notifier CPUs online

3. **SIPI vector = adresse >> 12**
   - Pas juste le byte bas !
   - `APIC_DM_STARTUP | (start_eip >> 12)`

4. **Adresses relatives dans trampoline**
   - Tout doit être relatif à 0x8000
   - GDT, variables, jumps
   - Utiliser trampoline Linux (testé et robuste)

5. **Delays sans TSC**
   - TSC peut ne pas être fiable pendant boot
   - Busy loops simples avec `pause`

---

**Date** : 2025-11-16
**Statut** : ⚠️ Step 4 PARTIEL - INIT-SIPI OK, APs triple fault
**Prochaine étape** : Utiliser trampoline Linux pour corriger adresses relatives
