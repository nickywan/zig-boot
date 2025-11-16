// Parallel computation tests
const serial = @import("serial.zig");
const std = @import("std");

const MAX_CPUS = 16;

// Test 1: Parallel counters (each CPU counts to 1M)
pub var per_cpu_counters: [MAX_CPUS]u64 = [_]u64{0} ** MAX_CPUS;

// Test 2: Distributed sum (sum of 1 to 10,000,000)
const SUM_TARGET: u64 = 10000000;
pub var partial_sums: [MAX_CPUS]u64 = [_]u64{0} ** MAX_CPUS;
pub var total_sum: u64 = 0;

// Test 3: Barrier synchronization
pub var barrier_count: u32 = 0;
pub var barrier_sense: u32 = 0;

// Simple barrier (sense-reversing)
pub fn barrier_wait(cpu_id: u32, cpu_count: u32) void {
    _ = cpu_id;  // Unused but kept for API consistency
    const my_sense = @atomicLoad(u32, &barrier_sense, .seq_cst);

    // Last CPU to arrive flips the sense
    // atomicRmw returns OLD value, so add 1 to get new count
    const old_count = @atomicRmw(u32, &barrier_count, .Add, 1, .seq_cst);
    const arrived = old_count + 1;

    if (arrived == cpu_count) {
        // Last one - reset and flip sense
        @atomicStore(u32, &barrier_count, 0, .seq_cst);
        @atomicStore(u32, &barrier_sense, if (my_sense == 0) 1 else 0, .seq_cst);
    } else {
        // Wait for sense to flip
        while (@atomicLoad(u32, &barrier_sense, .seq_cst) == my_sense) {
            asm volatile ("pause");
        }
    }
}

// Test 1: Parallel counter (each CPU counts to 1 million)
pub fn test_parallel_counters(cpu_id: u32) void {
    var i: u64 = 0;
    while (i < 1000000) : (i += 1) {
        per_cpu_counters[cpu_id] += 1;
        if (i % 100000 == 0) {
            asm volatile ("pause");
        }
    }
}

// Test 2: Distributed sum (divide work among CPUs)
pub fn test_distributed_sum(cpu_id: u32, cpu_count: u32) void {
    const per_cpu_work = SUM_TARGET / cpu_count;
    const start = cpu_id * per_cpu_work + 1;
    var end = (cpu_id + 1) * per_cpu_work;

    // Last CPU handles remainder
    if (cpu_id == cpu_count - 1) {
        end = SUM_TARGET;
    }

    var local_sum: u64 = 0;
    var i: u64 = start;
    while (i <= end) : (i += 1) {
        local_sum += i;
    }

    partial_sums[cpu_id] = local_sum;

    // Atomically add to total
    _ = @atomicRmw(u64, &total_sum, .Add, local_sum, .seq_cst);
}

// Test 3: Barrier synchronization test
pub fn test_barrier_sync(cpu_id: u32, cpu_count: u32) void {
    // Phase 1: Count to 500k
    var i: u64 = 0;
    while (i < 500000) : (i += 1) {
        per_cpu_counters[cpu_id] += 1;
    }

    // Barrier: wait for all CPUs
    barrier_wait(cpu_id, cpu_count);

    // Phase 2: Count to 1M (everyone starts together)
    while (i < 1000000) : (i += 1) {
        per_cpu_counters[cpu_id] += 1;
    }
}

// BSP runs all tests and displays results
pub fn run_all(cpu_count: u32) void {
    serial.write_string("===========================================\n");
    serial.write_string("  Running Parallel Computation Tests\n");
    serial.write_string("===========================================\n\n");

    serial.write_string("[TEST] Waiting for APs to initialize...\n");
    // Wait a bit for APs to be ready
    var wait: u32 = 0;
    while (wait < 5000000) : (wait += 1) {
        asm volatile ("pause");
    }

    serial.write_string("[TEST] BSP (CPU 0) running tests...\n\n");

    // Test 1: Parallel counters
    test_parallel_counters(0);
    barrier_wait(0, cpu_count);

    // Test 2: Distributed sum
    test_distributed_sum(0, cpu_count);
    barrier_wait(0, cpu_count);

    // Test 3: Barrier sync (reset counters first)
    per_cpu_counters[0] = 0;
    barrier_wait(0, cpu_count);  // Everyone resets together
    test_barrier_sync(0, cpu_count);

    serial.write_string("[TEST] All tests completed!\n\n");

    // Display results
    display_results(cpu_count);
}

fn display_results(cpu_count: u32) void {
    serial.write_string("===========================================\n");
    serial.write_string("  Test Results\n");
    serial.write_string("===========================================\n\n");

    // Test 1: Parallel Counters
    serial.write_string("TEST 1: Parallel Counters\n");
    serial.write_string("---------------------------\n");
    var i: u32 = 0;
    while (i < cpu_count) : (i += 1) {
        serial.write_string("  CPU ");
        serial.write_dec_u32(i);
        serial.write_string(": ");
        serial.write_dec_u64(per_cpu_counters[i]);
        if (per_cpu_counters[i] == 1000000) {
            serial.write_string(" [OK]\n");
        } else {
            serial.write_string(" [FAIL]\n");
        }
    }

    // Test 2: Distributed Sum
    serial.write_string("\nTEST 2: Distributed Sum (1 to 10,000,000)\n");
    serial.write_string("-------------------------------------------\n");

    // Expected sum: n * (n+1) / 2 = 10000000 * 10000001 / 2
    const expected_sum: u64 = 50000005000000;

    serial.write_string("  Partial sums:\n");
    i = 0;
    while (i < cpu_count) : (i += 1) {
        serial.write_string("    CPU ");
        serial.write_dec_u32(i);
        serial.write_string(": ");
        serial.write_dec_u64(partial_sums[i]);
        serial.write_string("\n");
    }

    serial.write_string("  Total sum: ");
    serial.write_dec_u64(total_sum);
    serial.write_string("\n");

    serial.write_string("  Expected:  ");
    serial.write_dec_u64(expected_sum);
    serial.write_string("\n");

    if (total_sum == expected_sum) {
        serial.write_string("  [OK] Sum is correct!\n");
    } else {
        serial.write_string("  [FAIL] Sum mismatch!\n");
    }

    // Test 3: Barrier Synchronization
    serial.write_string("\nTEST 3: Barrier Synchronization\n");
    serial.write_string("---------------------------------\n");
    serial.write_string("  (All CPUs should reach 1M after barrier)\n");

    var barrier_ok = true;
    i = 0;
    while (i < cpu_count) : (i += 1) {
        serial.write_string("  CPU ");
        serial.write_dec_u32(i);
        serial.write_string(": ");
        serial.write_dec_u64(per_cpu_counters[i]);
        if (per_cpu_counters[i] != 1000000) {
            serial.write_string(" [FAIL]\n");
            barrier_ok = false;
        } else {
            serial.write_string(" [OK]\n");
        }
    }

    if (barrier_ok) {
        serial.write_string("  [OK] Barrier synchronization worked!\n");
    } else {
        serial.write_string("  [FAIL] Some CPUs didn't reach barrier\n");
    }

    // Final status
    serial.write_string("\n");
    serial.write_string("===========================================\n");
    if (total_sum == expected_sum and barrier_ok) {
        serial.write_string("[SUCCESS] All parallel tests passed!\n");
    } else {
        serial.write_string("[WARNING] Some tests failed\n");
    }
    serial.write_string("===========================================\n");
}
