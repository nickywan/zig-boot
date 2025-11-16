# Boot Linux Minimal - 64-bit SMP Kernel

Un noyau minimaliste 64-bit avec support SMP (Symmetric Multi-Processing) qui :
- Détecte automatiquement les CPUs via ACPI MADT
- Boot en mode 64-bit avec Multiboot2
- Lance les Application Processors (APs) via INIT-SIPI-SIPI
- Exécute une computation parallèle sur tous les cœurs
- Affiche les résultats sur le port série COM1

## Architecture

```
boot-linux/
├── boot/
│   ├── boot.S          # Bootloader Multiboot2 + passage 64-bit
│   └── trampoline.S    # Code trampoline pour les APs
├── kernel/
│   ├── main.c          # Point d'entrée et computation
│   ├── serial.c        # Driver série COM1
│   ├── sync.c          # Spinlocks et atomics
│   ├── acpi.c          # Parser ACPI MADT
│   └── smp.c           # Initialisation SMP et APIC
├── include/
│   ├── types.h
│   ├── serial.h
│   ├── sync.h
│   ├── acpi.h
│   └── smp.h
├── linker.ld           # Linker script
├── Makefile
└── README.md
```

## Prérequis

- **GCC** (cross-compiler x86_64 ou natif)
- **GNU Make**
- **QEMU** (qemu-system-x86_64)

### Installation sur Ubuntu/Debian

```bash
sudo apt-get install build-essential qemu-system-x86
```

## Compilation

```bash
make all
```

Cela génère `kernel.elf`, un exécutable ELF 64-bit bootable.

## Test

### Exécution avec QEMU (TCG - émulation pure)

```bash
make run-tcg
```

### Exécution avec QEMU (KVM - virtualisation matérielle)

```bash
make run-kvm
```

### Exécution avec debug

```bash
make debug
```

Les logs détaillés seront dans `qemu.log`.

## Sortie attendue

```
=== Boot Linux Minimal - 64-bit SMP Kernel ===

[Boot] Detecting CPUs via ACPI...
[ACPI] Searching for RSDP...
[ACPI] RSDP found at 0xe0000
[ACPI] MADT found at 0xf1234
[ACPI] CPU 0: APIC ID = 0
[ACPI] CPU 1: APIC ID = 1
[ACPI] CPU 2: APIC ID = 2
[ACPI] CPU 3: APIC ID = 3
[ACPI] Detected 4 CPUs
[Boot] Using ACPI for SMP detection
[Boot] Detected 4 possible CPUs
[Boot] Initializing SMP...
[SMP] LAPIC base: 0xfee00000
[SMP] BSP APIC ID: 0
[Boot] Starting Application Processors...
[SMP] Boot complete: 4/4 CPUs online
[Boot] Boot complete: 4 CPUs online
[Core 0] APIC ID: 0
[Core 1] APIC ID: 1
[Core 2] APIC ID: 2
[Core 3] APIC ID: 3

[Computation] Starting parallel computation...
[Core 0] Computation done (local result: 500000500000)
[Core 1] Computation done (local result: 500000500000)
[Core 2] Computation done (local result: 500000500000)
[Core 3] Computation done (local result: 500000500000)

=== Results ===
Total result: 2000002000000
Expected: 500000500000 (per core) * 4 (cores) = 2000002000000
[SUCCESS] All APs booted and functional!

=== System Halted ===
```

## Détails techniques

### Boot sequence

1. **Bootloader (boot.S)** :
   - Chargé par Multiboot2 (GRUB ou QEMU `-kernel`)
   - Configure paging (identity map 2MB)
   - Active PAE et mode long (64-bit)
   - Saute vers `kernel_main`

2. **Initialisation (main.c)** :
   - Initialise le port série COM1
   - Parse ACPI pour détecter les CPUs
   - Initialise le Local APIC

3. **Boot des APs (smp.c)** :
   - Copie la trampoline à 0x8000 (mémoire basse)
   - Envoie INIT-SIPI-SIPI IPIs à chaque AP
   - Chaque AP exécute la trampoline → 64-bit → `ap_boot_complete`
   - **IMPORTANT** : Aucune trace série pendant le boot des APs (fragile)

4. **Computation parallèle** :
   - Chaque CPU calcule la somme de 1 à 1 000 000
   - Résultat stocké dans une variable partagée (protégée par spinlock)
   - Synchronisation avec `atomic_t` pour compter les CPUs terminés

### Synchronisation

- **Spinlocks** (`spinlock_t`) : protègent `shared_result` pendant les mises à jour
- **Atomics** (`atomic_t`) : comptent les CPUs bootés et les tâches terminées
- **Barrières mémoire** : garanties par les built-ins GCC (`__sync_*`)

### ACPI MADT

Le parser ACPI minimal :
- Recherche la RSDP dans la zone BIOS (0xE0000-0xFFFFF)
- Lit la RSDT/XSDT pour trouver la MADT
- Parse les entrées LAPIC pour obtenir les APIC IDs

### Trampoline SMP

Code exécuté par chaque AP au boot :
- Démarre en 16-bit real mode à 0x8000
- Passe en protected mode (32-bit)
- Active le long mode (64-bit)
- Charge les mêmes page tables que le BSP
- Saute vers `ap_boot_complete`

## Limitations

- **Maximum 16 CPUs** (limite arbitraire dans le code)
- **Identity mapping** seulement pour les premiers 2MB
- **Pas d'interruptions** configurées (IDT vide)
- **Pas de scheduler** : computation one-shot puis halt
- **Pas de gestion mémoire** dynamique (pas de malloc)

## Troubleshooting

### Le kernel ne boote pas

- Vérifiez que QEMU supporte Multiboot2 : `qemu-system-x86_64 --version`
- Essayez avec `-no-reboot -no-shutdown` pour voir les erreurs

### Les APs ne bootent pas

- Vérifiez que QEMU est lancé avec `-smp N` (ex: `-smp 4`)
- Regardez `qemu.log` avec `make debug`
- La trampoline doit être en dessous de 1MB (vérifiez `linker.ld`)

### Résultat incorrect

- Vérifiez que tous les CPUs ont bien exécuté la computation
- Vérifiez la synchronisation (spinlocks, atomics)

## Ressources

- [OSDev Wiki - SMP](https://wiki.osdev.org/Symmetric_Multiprocessing)
- [OSDev Wiki - APIC](https://wiki.osdev.org/APIC)
- [OSDev Wiki - ACPI](https://wiki.osdev.org/ACPI)
- [Intel SDM Vol. 3A](https://software.intel.com/content/www/us/en/develop/articles/intel-sdm.html)

## Licence

Domaine public / CC0
