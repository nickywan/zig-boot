# Quick Start Guide

## En 3 commandes

```bash
# 1. Compiler
make all

# 2. Lancer avec 4 CPUs
make run-tcg

# 3. Voir le résultat
# La sortie apparaît directement dans le terminal
```

## Sortie attendue

```
=== Boot Linux Minimal - 64-bit SMP Kernel ===

[Boot] Detecting CPUs via ACPI...
[ACPI] Searching for RSDP...
[ACPI] RSDP found at 0xe0000
[ACPI] MADT found at 0x...
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

## Commandes utiles

```bash
# Tester avec 1 CPU
qemu-system-x86_64 -kernel kernel.elf -serial stdio -smp 1

# Tester avec 8 CPUs
qemu-system-x86_64 -kernel kernel.elf -serial stdio -smp 8

# Tester avec KVM (plus rapide)
make run-kvm

# Debug avec logs QEMU
make debug
cat qemu.log

# Nettoyer
make clean
```

## Vérification

### ✅ La compilation réussit
```bash
$ make all
...
objcopy -O binary kernel.elf kernel.bin

$ ls -lh kernel.elf
-rwxrwxr-x 1 user user 33K ... kernel.elf
```

### ✅ Le kernel boote
Le message `=== Boot Linux Minimal - 64-bit SMP Kernel ===` apparaît.

### ✅ Les CPUs sont détectés
Vous voyez `[ACPI] CPU X: APIC ID = Y` pour chaque CPU.

### ✅ Les APs bootent
Le message `[SMP] Boot complete: N/N CPUs online` confirme que tous les CPUs ont booté.

### ✅ La computation fonctionne
Chaque core affiche `[Core X] Computation done`.

### ✅ Le résultat est correct
```
Total result: 2000002000000
Expected: ... = 2000002000000
[SUCCESS] All APs booted and functional!
```

## Troubleshooting

### Le kernel ne boote pas
```bash
# Vérifier QEMU
qemu-system-x86_64 --version

# Essayer avec debug
make debug
cat qemu.log
```

### Les APs ne bootent pas
```bash
# Vérifier que SMP est activé
make run-tcg  # Utilise -smp 4 par défaut

# Tester avec 1 seul CPU
qemu-system-x86_64 -kernel kernel.elf -serial stdio -smp 1
```

### Résultat incorrect
```bash
# Vérifier la sortie
make run-tcg 2>&1 | tee output.log

# Chercher les erreurs
grep ERROR output.log
grep WARNING output.log
```

## Documentation

- `README.md` : Documentation complète
- `IMPLEMENTATION_NOTES.md` : Détails techniques
- `PROJECT_SUMMARY.md` : Vue d'ensemble du projet

## Support

En cas de problème :
1. Vérifier `make clean && make all`
2. Lire les warnings de compilation
3. Consulter `IMPLEMENTATION_NOTES.md`
4. Vérifier la version de GCC/QEMU

---

**Prérequis** : GCC, Make, QEMU
**Testé sur** : Ubuntu 20.04+, Debian 11+, Arch Linux
