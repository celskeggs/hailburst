#ifndef FSW_VIVID_RTOS_REPLICATE_H
#define FSW_VIVID_RTOS_REPLICATE_H

// This file co-operates with the replication linker under toolchain/ (as configured by the Vivid SConscript) to
// allow object code replication of particular functions, without any of their associated mutable data.

struct replication {
    void *base_pointer;
    void *replica_pointer;
};

macro_define(REPLICATE_OBJECT_CODE, original_function, replica_name) {
    extern typeof(original_function) replica_name;
    const __attribute__((section("replicas"))) struct replication symbol_join(replica_name, metadata) = {
        .base_pointer = &original_function,
        .replica_pointer = &replica_name,
    }
}

#endif /* FSW_VIVID_RTOS_REPLICATE_H */
