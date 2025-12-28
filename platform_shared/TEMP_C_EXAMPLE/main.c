#include <stdio.h>
#include <stdint.h>
#include <pb_encode.h>
#include "platform_shared/example.pb.h"

int main(int argc, char const *argv[])
{
    // Create an IMUReport message
    IMUReport report = IMUReport_init_zero;

    // Set the IMU type to GYRO
    report.type = IMUType_IMU_ACCEL;

    // Set the x_val to some example value
    report.x_val = 4;

    // Buffer to hold the encoded message
    uint8_t buffer[IMUReport_size];

    // Create a stream that writes to the buffer
    pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));

    // Encode the message
    bool status = pb_encode(&stream, &IMUReport_msg, &report);

    if (!status) {
        printf("Encoding failed: %s\n", PB_GET_ERROR(&stream));
        return 1;
    }

    // Print the encoded message in hexadecimal
    printf("Encoded IMUReport:\n");
    printf("Type: %d (IMU_GYRO)\n", report.type);
    printf("X Value: %f\n", report.x_val);
    printf("\nEncoded bytes (%zu bytes):\n", stream.bytes_written);

    for (size_t i = 0; i < stream.bytes_written; i++) {
        printf("%02X ", buffer[i]);
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
    printf("\n");

    return 0;
}
