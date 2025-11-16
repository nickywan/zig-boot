const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .os_tag = .freestanding,
        .abi = .none,
    });

    const optimize = b.standardOptimizeOption(.{});

    // Create kernel executable
    const kernel = b.addExecutable(.{
        .name = "kernel.elf",
        .root_source_file = b.path("kernel/main.zig"),
        .target = target,
        .optimize = optimize,
    });

    // Kernel configuration
    kernel.pie = false;
    kernel.root_module.red_zone = false;
    kernel.setLinkerScript(b.path("linker.ld"));

    // Add C source files
    kernel.addCSourceFiles(.{
        .files = &[_][]const u8{
            "boot/init.c",
            "boot/services.c",
        },
        .flags = &[_][]const u8{
            "-std=c11",
            "-ffreestanding",
            "-fno-stack-protector",
            "-fno-pic",
            "-mno-red-zone",
            "-mno-80387",
            "-mno-mmx",
            "-mno-sse",
            "-mno-sse2",
            "-mcmodel=kernel",
            "-Wall",
            "-Wextra",
        },
    });

    // Add assembly files
    kernel.addAssemblyFile(b.path("boot/boot.S"));
    kernel.addAssemblyFile(b.path("boot/trampoline.S"));
    kernel.addAssemblyFile(b.path("boot/interrupt_stub.S"));

    // Build kernel
    b.installArtifact(kernel);

    // Create ISO directory structure
    const iso_dir = b.fmt("{s}/iso", .{b.install_path});
    const boot_dir = b.fmt("{s}/boot", .{iso_dir});
    const grub_dir = b.fmt("{s}/grub", .{boot_dir});

    // Create grub.cfg
    const grub_cfg_content =
        \\set timeout=0
        \\set default=0
        \\
        \\menuentry "Hybrid C+Zig Kernel" {
        \\    multiboot2 /boot/kernel.elf
        \\    boot
        \\}
    ;

    // Create ISO step
    const create_iso_step = b.step("iso", "Create bootable ISO");

    // Create directories
    const mkdir_iso = b.addSystemCommand(&[_][]const u8{ "mkdir", "-p", grub_dir });
    create_iso_step.dependOn(&mkdir_iso.step);

    // Copy kernel
    const kernel_path = b.getInstallPath(.bin, "kernel.elf");
    const copy_kernel = b.addSystemCommand(&[_][]const u8{
        "cp",
        kernel_path,
        b.fmt("{s}/kernel.elf", .{boot_dir}),
    });
    copy_kernel.step.dependOn(&kernel.step);
    create_iso_step.dependOn(&copy_kernel.step);

    // Create grub.cfg
    const write_grub_cfg = b.addSystemCommand(&[_][]const u8{
        "sh",
        "-c",
        b.fmt("echo '{s}' > {s}/grub.cfg", .{ grub_cfg_content, grub_dir }),
    });
    create_iso_step.dependOn(&write_grub_cfg.step);

    // Create ISO with grub-mkrescue
    const mkrescue = b.addSystemCommand(&[_][]const u8{
        "grub-mkrescue",
        "-o",
        b.fmt("{s}/boot.iso", .{b.install_path}),
        iso_dir,
    });
    mkrescue.step.dependOn(create_iso_step);

    // Add run step
    const run_step = b.step("run", "Run in QEMU");
    const qemu = b.addSystemCommand(&[_][]const u8{
        "qemu-system-x86_64",
        "-cdrom",
        b.fmt("{s}/boot.iso", .{b.install_path}),
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "256M",
        "-smp",
        "4",
    });
    qemu.step.dependOn(&mkrescue.step);
    run_step.dependOn(&qemu.step);

    // Add run-kvm step for hardware acceleration
    const run_kvm_step = b.step("run-kvm", "Run in QEMU with KVM acceleration");
    const qemu_kvm = b.addSystemCommand(&[_][]const u8{
        "qemu-system-x86_64",
        "-cdrom",
        b.fmt("{s}/boot.iso", .{b.install_path}),
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "256M",
        "-smp",
        "4",
        "-accel",
        "kvm",
    });
    qemu_kvm.step.dependOn(&mkrescue.step);
    run_kvm_step.dependOn(&qemu_kvm.step);
}
