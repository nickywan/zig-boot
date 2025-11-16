# Bare-Metal Kernel - Status Report

**Date**: 2025-11-16
**Version**: C (stable) + Zig 0.14 (in progress)

## C Version: ✅ STABLE

La version C est entièrement fonctionnelle :
- 4 CPUs bootent correctement via INIT-SIPI-SIPI
- Timer APIC fonctionne sur BSP (26 interruptions/2 sec)
- Gestionnaires de mémoire (PMM, VMM, Heap) opérationnels
- Tests parallèles passent (compteurs, somme distribuée, barrières)
- Compatible TCG et KVM
- Sortie série (COM1) stable

## Zig Version: ⚠️ EN COURS

### Implémenté

**Infrastructure de build**:
- ✅ Zig 0.14.0 installé (`~/.local/zig-0.14.0`)
- ✅ `build.zig` configuré (bare-metal x86-64)
- ✅ Création d'ISO via `grub-mkrescue`
- ✅ Steps: `zig build`, `zig build iso`, `zig build run`

**Modules de base (complets)**:
- ✅ `serial.zig` - Driver COM1 avec I/O inline ASM
- ✅ `multiboot.zig` - Header Multiboot2 (dans boot.S)
- ✅ `pmm.zig` - Allocateur bitmap (4KB pages)
- ✅ `acpi.zig` - Parse RSDP/MADT pour détection CPUs
- ✅ `apic.zig` - Init APIC via MSR
- ✅ `panic.zig` - Handler de panic bare-metal

**Modules stubs**:
- 🔄 `vmm.zig` - Gestionnaire mémoire virtuelle (stub)
- 🔄 `smp.zig` - Boot multi-CPU (stub)
- 🔄 `tests.zig` - Tests parallèles (stub)

**Bootloader**:
- ✅ `boot.S` - Multiboot2 → 64-bit (copié depuis C)
- ✅ Header Multiboot2 à offset 0x1000 (trouvé via hexdump)
- ✅ Entry point `_start` @ 0x101000
- ✅ Appelle `kernel_main(multiboot_addr: u64)`

### Problème Actuel: Kernel ne produit aucune sortie

**Symptômes**:
- ISO se crée correctement (6578 sectors)
- QEMU boot sans erreur
- Aucune sortie série malgré `serial.write_string("Hello from Zig!")`
- Pas de triple fault visible (QEMU reste actif)

**Investigations effectuées**:
1. ✅ Multiboot2 magic vérifié (`0xE85250D6` @ offset 0x1000)
2. ✅ Symbols corrects : `_start` @ 0x101000, `kernel_main` @ 0x11c360
3. ✅ ELF entry point correct : 0x101000
4. ✅ Signature `kernel_main` corrigée : `u64` au lieu de `(u32, u32)`
5. ✅ Bootloader appelle bien `kernel_main` (ligne 142 de boot.S)
6. ❌ **Aucune sortie malgré tout**

**Hypothèses restantes**:
- Problème d'initialisation série dans Zig (inline ASM)
- Issue de compilation Zig (sections mal alignées?)
- Crash avant l'appel à `kernel_main`
- Problème de calling convention C/Zig

### À Faire

**Immédiat**:
1. Déboguer problème de boot (peut-être tester avec VGA au lieu de série)
2. Ajouter sortie VGA text mode (versions C ET Zig)
3. Une fois boot fonctionnel, compléter SMP/VMM/tests

**Moyen terme**:
- Porter trampoline AP en Zig
- Implémenter VMM avec paging récursif
- Timer APIC multi-CPU
- Tests parallèles (compteurs, somme, barrières)

## Notes Techniques

### Différences C vs Zig

| Aspect | C | Zig |
|--------|---|-----|
| I/O ports | `outb`/`inb` inline ASM | `asm volatile` blocks |
| Error handling | Return codes | `!` error unions + `catch` |
| Comptime | Macros | `comptime` native |
| Safety | UB possible | Pas d'UB (compile-time checks) |

### Commandes Utiles

```bash
# C version
cd c/ && make run

# Zig version
cd zig/
export PATH="$HOME/.local/bin:$PATH"
zig build run        # Compile + run
zig build iso        # Créer ISO seulement
```

### Prochaines Étapes

1. **Priorité 1**: Résoudre boot Zig (probablement ajouter VGA pour debug)
2. **Priorité 2**: Support VGA text mode (C + Zig)
3. Compléter modules Zig (SMP, VMM, tests)
4. Tests de régression
5. Documentation finale et commit

---

**Auteur**: Claude + nickywan
**Repo**: git@github.com:nickywan/zig-boot.git
**Dernière modification**: 2025-11-16 06:30 UTC
