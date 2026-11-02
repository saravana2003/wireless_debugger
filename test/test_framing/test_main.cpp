// Native tests: OpenOCD cmsis-dap-tcp framing (layout documented in framing_dap_tcp.h).

#include <unity.h>

#include "framing_dap_tcp.h"
#include "gt911_parse.h"

namespace fr = net::framing_dap_tcp;

void setUp() {}
void tearDown() {}

void test_decode_valid_request() {
    const uint8_t hdr[fr::HEADER_SIZE] = {0x44, 0x41, 0x50, 0x00, 0x34, 0x02, 0x01, 0x00};  // "DAP\0", len 0x234
    uint16_t len = 0;
    TEST_ASSERT_TRUE(fr::decode_request(hdr, 1024, &len));
    TEST_ASSERT_EQUAL_UINT16(0x234, len);
}

void test_decode_rejects_bad_signature() {
    const uint8_t hdr[fr::HEADER_SIZE] = {0x45, 0x41, 0x50, 0x00, 0x10, 0x00, 0x01, 0x00};
    uint16_t len = 0;
    TEST_ASSERT_FALSE(fr::decode_request(hdr, 1024, &len));
}

void test_decode_rejects_response_type() {
    const uint8_t hdr[fr::HEADER_SIZE] = {0x44, 0x41, 0x50, 0x00, 0x10, 0x00, 0x02, 0x00};
    uint16_t len = 0;
    TEST_ASSERT_FALSE(fr::decode_request(hdr, 1024, &len));
}

void test_decode_length_limits() {
    uint8_t hdr[fr::HEADER_SIZE] = {0x44, 0x41, 0x50, 0x00, 0x00, 0x00, 0x01, 0x00};
    uint16_t len = 0;
    TEST_ASSERT_FALSE(fr::decode_request(hdr, 1024, &len));  // empty

    hdr[4] = 0x00;
    hdr[5] = 0x04;  // exactly 1024
    TEST_ASSERT_TRUE(fr::decode_request(hdr, 1024, &len));
    TEST_ASSERT_EQUAL_UINT16(1024, len);

    hdr[4] = 0x01;  // 1025
    TEST_ASSERT_FALSE(fr::decode_request(hdr, 1024, &len));
}

void test_encode_response() {
    uint8_t hdr[fr::HEADER_SIZE] = {};
    fr::encode_response(0x0123, hdr);
    const uint8_t expected[fr::HEADER_SIZE] = {0x44, 0x41, 0x50, 0x00, 0x23, 0x01, 0x02, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, hdr, fr::HEADER_SIZE);
}

void test_gt911_report() {
    TEST_ASSERT_FALSE(touch::gt911_ready(0x05));   // bit 7 clear: nothing new
    TEST_ASSERT_TRUE(touch::gt911_ready(0x81));
    TEST_ASSERT_EQUAL_UINT8(1, touch::gt911_count(0x81));
    TEST_ASSERT_EQUAL_UINT8(0, touch::gt911_count(0x80));  // ready, no points: finger lifted

    const uint8_t point[8] = {0x01, 0x1F, 0x03, 0xDF, 0x01, 0x10, 0x00, 0x00};
    uint16_t x = 0, y = 0;
    touch::gt911_point(point, &x, &y);
    TEST_ASSERT_EQUAL_UINT16(799, x);
    TEST_ASSERT_EQUAL_UINT16(479, y);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_decode_valid_request);
    RUN_TEST(test_decode_rejects_bad_signature);
    RUN_TEST(test_decode_rejects_response_type);
    RUN_TEST(test_decode_length_limits);
    RUN_TEST(test_encode_response);
    RUN_TEST(test_gt911_report);
    return UNITY_END();
}
