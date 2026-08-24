__kernel void clear_buffer128(ulong16 global* input, ulong16 clear_value) {
    size_t idx = get_global_id(0);
    input[idx] = clear_value;
}
