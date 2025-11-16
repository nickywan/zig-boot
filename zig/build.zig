const std = @import("std");

pub fn build(b: *std.Build) void {
    // Bare-metal x86-64 target
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .os_tag = .freestanding,
        .abi = .none,
    });

    const optimize = b.standardOptimizeOption(.{});

    // Kernel executable
    const kernel = b.addExecutable(.{
        .name = "kernel",
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Add assembly boot file
    kernel.addAssemblyFile(b.path("src/boot.S"));

    // Bare-metal kernel configuration
    kernel.setLinkerScript(b.path("linker.ld"));
    kernel.pie = false;
    kernel.root_module.red_zone = false;

    // Build the kernel
    b.installArtifact(kernel);

    // Create ISO
    const iso_dir = "isodir";
    const iso_file = "kernel.iso";

    // Copy kernel to isodir/boot/
    const copy_kernel = b.addSystemCommand(&.{"cp"});
    copy_kernel.addArtifactArg(kernel);
    copy_kernel.addArg(iso_dir ++ "/boot/kernel");
    copy_kernel.step.dependOn(b.getInstallStep());

    // Create ISO with grub-mkrescue
    const make_iso = b.addSystemCommand(&.{
        "grub-mkrescue",
        "-o",
        iso_file,
        iso_dir,
    });
    make_iso.step.dependOn(&copy_kernel.step);

    const iso_step = b.step("iso", "Create bootable ISO");
    iso_step.dependOn(&make_iso.step);

    // QEMU run step
    const run_qemu = b.addSystemCommand(&.{
        "qemu-system-x86_64",
        "-cdrom",
        iso_file,
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "256M",
        "-smp",
        "4",
    });
    run_qemu.step.dependOn(&make_iso.step);

    const run_step = b.step("run", "Run the kernel in QEMU");
    run_step.dependOn(&run_qemu.step);

    // TCG mode (software emulation)
    const run_tcg = b.addSystemCommand(&.{
        "qemu-system-x86_64",
        "-cdrom",
        iso_file,
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "256M",
        "-smp",
        "4",
        "-accel",
        "tcg",
    });
    run_tcg.step.dependOn(&make_iso.step);

    const tcg_step = b.step("run-tcg", "Run in TCG mode (no KVM)");
    tcg_step.dependOn(&run_tcg.step);

    // Debug mode
    const debug_qemu = b.addSystemCommand(&.{
        "qemu-system-x86_64",
        "-cdrom",
        iso_file,
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "256M",
        "-smp",
        "4",
        "-d",
        "cpu_reset,guest_errors",
        "-no-reboot",
    });
    debug_qemu.step.dependOn(&make_iso.step);

    const debug_step = b.step("debug", "Run with debug output");
    debug_step.dependOn(&debug_qemu.step);
}
