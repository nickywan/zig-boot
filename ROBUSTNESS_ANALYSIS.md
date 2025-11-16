# Analyse de robustesse - Points à améliorer

## ⚠️ Problèmes critiques actuels

### 1. **STACKS PER-CPU MANQUANTS** ❌ CRITIQUE

**Problème actuel :**
```c
// kernel/main.c
trampoline_stack = 0x7000;  // TOUS les APs partagent le même stack !
```

**Impact :**
- Tous les APs utilisent le même stack
- Corruption de stack garantie avec >1 AP
- Crashes aléatoires pendant le boot

**Solution requise :**
```c
// Allouer des stacks per-CPU
#define AP_STACK_SIZE 8192
static uint8_t ap_stacks[MAX_CPUS][AP_STACK_SIZE] __attribute__((aligned(16)));

// Dans smp_boot_aps(), avant de booter chaque AP :
trampoline_stack = (uint64_t)&ap_stacks[i][AP_STACK_SIZE];
```

---

### 2. **IDT NON INITIALISÉE** ❌ CRITIQUE

**Problème actuel :**
```
Aucune IDT → Toute exception = triple fault = reboot
```

**Impact :**
- Division par zéro → triple fault
- Page fault → triple fault
- Double fault → triple fault

**Solution requise :**
```c
// Créer une IDT minimale avec handlers
struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

static struct idt_entry idt[256];

void exception_handler(void) {
    serial_puts("[EXCEPTION] System halted\n");
    while(1) asm("hlt");
}

void setup_idt(void) {
    // Remplir IDT avec handler par défaut
    // Charger avec lidt
}
```

---

### 3. **RACE CONDITIONS DANS LA TRAMPOLINE** ❌ CRITIQUE

**Problème actuel :**
```c
// kernel/main.c
extern uint32_t trampoline_cr3;
extern uint64_t trampoline_stack;
extern uint64_t trampoline_entry;

// Écrites AVANT de booter les APs
trampoline_cr3 = (uint32_t)cr3;
trampoline_stack = 0x7000;
trampoline_entry = (uint64_t)ap_boot_complete;

// Mais si on boot plusieurs APs en parallèle ?
// AP1 lit trampoline_stack pendant que BSP écrit pour AP2 !
```

**Solution requise :**
```c
// Variables per-CPU dans la trampoline
struct trampoline_data {
    uint32_t cr3;
    uint64_t stack;
    uint64_t entry;
    uint64_t cpu_id;
} __attribute__((packed));

// Dans la trampoline
static struct trampoline_data trampoline_vars[MAX_CPUS];
```

---

### 4. **FONCTION on_each_cpu() DÉFAILLANTE** ❌ CRITIQUE

**Problème actuel :**
```c
void on_each_cpu(smp_call_func_t func, void *info) {
    global_func = func;
    global_info = info;
    atomic_set(&cpus_ready, cpu_count);

    // PROBLÈME : On exécute sur le BSP en premier
    for (int i = 0; i < cpu_count; i++) {
        func(info);  // ← BSP exécute cpu_count fois !
        atomic_inc(&cpus_finished);
    }
}
```

**Impact :**
- Le BSP exécute la fonction cpu_count fois
- Les APs ne l'exécutent jamais (ils sont en hlt)
- Résultat totalement faux

**Solution requise :**
```c
void on_each_cpu(smp_call_func_t func, void *info) {
    global_func = func;
    global_info = info;
    atomic_set(&cpus_ready, cpu_count);
    atomic_set(&cpus_finished, 0);

    // Réveiller les APs via IPI
    send_ipi_all(CALL_FUNCTION_VECTOR);

    // Exécuter localement (BSP)
    func(info);
    atomic_inc(&cpus_finished);

    // Attendre les APs
    while (atomic_read(&cpus_finished) < cpu_count) {
        cpu_relax();
    }
}
```

---

### 5. **MEMORY BARRIERS INSUFFISANTES** ⚠️ IMPORTANT

**Problème actuel :**
```c
atomic_inc(&cpus_booted);

// Autre CPU lit immédiatement
while (atomic_read(&cpus_booted) < expected) ...
```

**Impact :**
- Réordonnancement mémoire possible
- Lectures stale des variables

**Solution requise :**
```c
static inline void smp_mb(void) {
    __asm__ volatile("mfence" ::: "memory");
}

static inline void smp_rmb(void) {
    __asm__ volatile("lfence" ::: "memory");
}

static inline void smp_wmb(void) {
    __asm__ volatile("sfence" ::: "memory");
}

// Utilisation
atomic_inc(&cpus_booted);
smp_wmb();  // Garantit que l'écriture est visible
```

---

### 6. **TIMEOUTS NON FONCTIONNELS** ⚠️ IMPORTANT

**Problème actuel :**
```c
// kernel/smp.c
int timeout = 1000;
while (atomic_read(&cpus_booted) == initial_count && timeout-- > 0) {
    delay_ms(1);
}
```

**Impact :**
- `delay_ms()` est approximatif (busy wait)
- Pas de vraie mesure du temps
- Timeout peut être trop court ou trop long

**Solution requise :**
```c
// Utiliser RDTSC pour mesurer le temps réel
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t start = rdtsc();
uint64_t timeout_cycles = 1000 * tsc_khz;  // 1 seconde

while (atomic_read(&cpus_booted) == initial_count) {
    if ((rdtsc() - start) > timeout_cycles) {
        break;  // Timeout
    }
    cpu_relax();
}
```

---

### 7. **PAS DE VÉRIFICATION APIC** ⚠️ IMPORTANT

**Problème actuel :**
```c
// On assume que l'APIC existe et fonctionne
apic_base = (volatile uint32_t*)(apic_msr & 0xFFFFF000);
```

**Impact :**
- Si pas d'APIC → crash
- Si APIC désactivé → crash

**Solution requise :**
```c
// Vérifier CPUID pour APIC
static int check_apic(void) {
    uint32_t eax, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=d"(edx)
                     : "a"(1)
                     : "ebx", "ecx");
    return (edx & (1 << 9)) != 0;  // APIC bit
}

if (!check_apic()) {
    serial_puts("[ERROR] No APIC support!\n");
    return;
}
```

---

### 8. **VARIABLES GLOBALES NON ALIGNÉES** ⚠️ IMPORTANT

**Problème actuel :**
```c
static unsigned long shared_result = 0;
static spinlock_t result_lock = SPINLOCK_INIT;
```

**Impact :**
- False sharing entre CPUs
- Dégradation des performances
- Cache line bouncing

**Solution requise :**
```c
static unsigned long shared_result __attribute__((aligned(64))) = 0;
static spinlock_t result_lock __attribute__((aligned(64))) = SPINLOCK_INIT;
```

---

### 9. **PAS DE GESTION DES ERREURS APIC** ⚠️ IMPORTANT

**Problème actuel :**
```c
// On n'efface jamais les erreurs APIC
apic_write(0xF0, apic_read(0xF0) | 0x100);
```

**Solution requise :**
```c
// Effacer les erreurs APIC avant et après
apic_write(APIC_ESR, 0);
apic_write(APIC_ESR, 0);

uint32_t errors = apic_read(APIC_ESR);
if (errors) {
    serial_printf("[APIC] Errors detected: 0x%x\n", errors);
}
```

---

### 10. **TRAMPOLINE NON VÉRIFIÉE** ⚠️ IMPORTANT

**Problème actuel :**
```c
// On copie la trampoline sans vérifier si elle est valide
for (int i = 0; i < trampoline_size; i++) {
    trampoline_dest[i] = trampoline_start[i];
}
```

**Solution requise :**
```c
// Vérifier que la zone 0x8000 est accessible
if (trampoline_size == 0 || trampoline_size > 4096) {
    serial_printf("[ERROR] Invalid trampoline size: %d\n", trampoline_size);
    return;
}

// Vérifier checksum après copie
uint32_t checksum_src = 0, checksum_dst = 0;
for (int i = 0; i < trampoline_size; i++) {
    checksum_src += trampoline_start[i];
    checksum_dst += trampoline_dest[i];
}

if (checksum_src != checksum_dst) {
    serial_puts("[ERROR] Trampoline copy failed!\n");
    return;
}
```

---

## 📊 Résumé des priorités

| Problème | Sévérité | Impact | Effort |
|----------|----------|--------|--------|
| Stacks per-CPU | CRITIQUE | Crash garanti avec >1 AP | Moyen |
| IDT manquante | CRITIQUE | Triple fault sur exception | Élevé |
| Race conditions trampoline | CRITIQUE | Boot aléatoire des APs | Moyen |
| on_each_cpu() cassé | CRITIQUE | Résultat incorrect | Faible |
| Memory barriers | IMPORTANT | Bugs subtils | Faible |
| Timeouts approximatifs | IMPORTANT | Boot lent ou échoue | Moyen |
| Vérification APIC | IMPORTANT | Crash si pas d'APIC | Faible |
| Cache alignment | IMPORTANT | Perf dégradées | Faible |
| Erreurs APIC | IMPORTANT | Bugs silencieux | Faible |
| Vérification trampoline | IMPORTANT | Corruption mémoire | Faible |

---

## ✅ Corrections à implémenter (par priorité)

### Priorité 1 (CRITIQUE)
1. ✅ Stacks per-CPU pour les APs
2. ✅ IDT minimale avec handlers d'exceptions
3. ✅ Variables trampoline per-CPU
4. ✅ Correction de on_each_cpu() avec IPIs

### Priorité 2 (IMPORTANT)
5. ✅ Memory barriers (mfence/lfence/sfence)
6. ✅ Timeouts avec RDTSC
7. ✅ Vérification CPUID pour APIC
8. ✅ Alignment des variables partagées

### Priorité 3 (NICE TO HAVE)
9. ✅ Gestion erreurs APIC
10. ✅ Vérification trampoline
11. ✅ Logging amélioré (niveaux de debug)
12. ✅ Statistiques per-CPU

---

## 🔧 Prochaines étapes

Voulez-vous que j'implémente ces corrections ?

Options :
- **Option A** : Corriger uniquement les problèmes CRITIQUES (1-4)
- **Option B** : Corriger CRITIQUES + IMPORTANTS (1-8)
- **Option C** : Implémentation complète (1-12)

Quelle option préférez-vous ?
