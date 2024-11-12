// Copyright © 2024 Collabora, Ltd.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "memstream.h"

class MemStreamTest : public ::testing::Test {
protected:
    struct u_memstream mem;
    char *buffer;
    size_t size;

    MemStreamTest() : buffer(nullptr), size(0) {
        u_memstream_open(&mem, &buffer, &size);
    }

    ~MemStreamTest() {
        u_memstream_close(&mem);
        free(buffer);
    }

    void resetStream() {
        u_memstream_close(&mem);
        free(buffer);
        buffer = nullptr;
        size = 0;
        u_memstream_open(&mem, &buffer, &size);
    }
};

TEST_F(MemStreamTest, OpenStream) {
    EXPECT_NE(mem.f, nullptr);
    EXPECT_EQ(size, 0);
}

TEST_F(MemStreamTest, WriteAndFlush) {
    const char *data = "Hello, world!";
    size_t data_len = strlen(data);

    EXPECT_EQ(fwrite(data, 1, data_len, mem.f), data_len);
    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data);
    EXPECT_EQ(size, data_len);
}

TEST_F(MemStreamTest, ResetStream) {
    const char *data = "Hello, world!";
    size_t data_len = strlen(data);

    EXPECT_EQ(fwrite(data, 1, data_len, mem.f), data_len);
    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data);
    EXPECT_EQ(size, data_len);

    resetStream();

    EXPECT_EQ(size, 0);
}

TEST_F(MemStreamTest, WriteAfterReset) {
    const char *data1 = "Hello, world!";
    const char *data2 = "New data";
    size_t data1_len = strlen(data1);
    size_t data2_len = strlen(data2);

    EXPECT_EQ(fwrite(data1, 1, data1_len, mem.f), data1_len);
    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data1);
    EXPECT_EQ(size, data1_len);

    resetStream();

    EXPECT_EQ(size, 0);

    EXPECT_EQ(fwrite(data2, 1, data2_len, mem.f), data2_len);
    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data2);
    EXPECT_EQ(size, data2_len);
}

TEST_F(MemStreamTest, MultipleFlushes) {
    const char *data = "Hello, world!";
    size_t data_len = strlen(data);

    EXPECT_EQ(fwrite(data, 1, data_len, mem.f), data_len);
    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data);
    EXPECT_EQ(size, data_len);

    EXPECT_EQ(u_memstream_flush(&mem), 0);

    EXPECT_STREQ(buffer, data);
    EXPECT_EQ(size, data_len);
}
