#include "emulator/emulator.h"

const uint8_t TandyLUTdb[256] = {
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F, 0x9F,
        0x9F, 0x9F, 0x9F, 0x9E, 0x9E, 0x9E, 0x9D, 0x9C, 0x9C, 0x9B, 0x9B, 0x9A, 0x9A, 0x9A, 0x99, 0x99,
        0x99, 0x99, 0x98, 0x98, 0x98, 0x98, 0x98, 0x97, 0x97, 0x97, 0x97, 0x97, 0x97, 0x96, 0x96, 0x96,
        0x96, 0x96, 0x96, 0x96, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x95, 0x94, 0x94, 0x94,
        0x94, 0x94, 0x94, 0x94, 0x94, 0x94, 0x94, 0x94, 0x94, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93,
        0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x93, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92,
        0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x92, 0x91, 0x91, 0x91, 0x91, 0x91,
        0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91, 0x91,
        0x91, 0x91, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90
};

#define FIFO_BUFFER_SIZE 16
static uint8_t samples_buffer[FIFO_BUFFER_SIZE] = { 0 };
static uint8_t fifo_buffer_length = 0, dss_active = 0;
static uint8_t checks = 0;

uint8_t dss_sample() { // core #1
    if (fifo_buffer_length == 0 || !dss_active || checks < 2) { // no bytes in buffer
        return 0;
    }
    const uint8_t sample = samples_buffer[0];

    memmove(samples_buffer, samples_buffer + 1, FIFO_BUFFER_SIZE - 1);

    fifo_buffer_length--;
    return sample;
}

inline static void fifo_push_byte(uint8_t value) { // core #0
    if (fifo_buffer_length == FIFO_BUFFER_SIZE)
        return;
    samples_buffer[fifo_buffer_length++] = value;
    //tandy_write(0, TandyLUTdb[value]);
//     printf("SS BUFF %i\r\n", value);
}

static inline uint8_t fifo_is_full() {
    if (fifo_buffer_length == FIFO_BUFFER_SIZE) {
        return 0x40;

    } else {
        return 0x00;
    }
    return fifo_buffer_length == FIFO_BUFFER_SIZE ? 0x40 : 0x00;
}

void dss_out(uint16_t portnum, uint8_t value) {
    //printf("OUT SS %x %x\r\n", portnum, value);
    static uint8_t last37a, port378 = 0;
    switch (portnum) {
        case 0x378:
            port378 = value;
            fifo_push_byte(value);
            last37a = 0;
            break;
        case 0x37A:
            // Зачем слать предидущий байт в буфер если это не инит?
            if ((value & 4) && !(last37a & 4)) {
                //putssourcebyte(port378);
            }
            if (value == 0x04) {
                dss_active = 1;
            }
            last37a = value;
            break;
    }
}

uint8_t dss_in(uint16_t portnum) {
    if (checks == 0) {
        // Silence 3 channels + noise
        tandy_write(0, 0x9F); // Channel 0
        tandy_write(0, 0xBF); // Channel 1
        tandy_write(0, 0xDF); // Channel 2
        tandy_write(0, 0xFF); // Noise

        // Set channel 0 to 125 KHz
        tandy_write(0, 0x80);
        tandy_write(0, 0x00);
    }
    if (checks < 2) checks++;
    return fifo_is_full();
}

void covox_out(uint16_t portnum, uint8_t value) {
    tandy_write(0, TandyLUTdb[value]);
}