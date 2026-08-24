#define create_clear_buffer_kernel(size, type) \
    __kernel void clear_buffer## size (type global* input, type clear_value) { \
        size_t idx = get_global_id(0); \
        input[idx] = clear_value; \
    }

create_clear_buffer_kernel(1, uchar);
create_clear_buffer_kernel(2, ushort);
create_clear_buffer_kernel(4, uint);
create_clear_buffer_kernel(8, uint2);
create_clear_buffer_kernel(16, uint4);
create_clear_buffer_kernel(32, uint8);
create_clear_buffer_kernel(64, uint16);
