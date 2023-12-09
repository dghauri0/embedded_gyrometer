#include <mbed.h>

/* Gyroscope Control Register Addresses */
#define CTRL_REG1 0x20
#define CTRL_REG2 0x21
#define CTRL_REG3 0x22
#define CTRL_REG4 0x23

/* Gyroscope Control Register Configurations */

// CTRL_REG1
// +-----+-----+-----+-----+----+-----+-----+-----+
// | DR1 | DR0 | BW1 | BW0 | PD | Zen | Yen | Xen |
// +-----+-----+-----+-----+----+-----+-----+-----+
// | 0   | 1   | 1   | 0   | 1  | 1   | 1   | 1   |
// +-----+-----+-----+-----+----+-----+-----+-----+
// Sets output data rate (ODR) = 200 Hz, Cutoff = 50.
// Gyroscope set to normal operating mode (power down mode disabled).
// All axes enabled.
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1

// CTRL_REG3
// +---------+---------+-----------+-------+---------+--------+---------+----------+
// | I1_Int1 | I1_Boot | H_Lactive | PP_OD | I2_DRDY | I2_WTM | I2_ORun | I2_Empty |
// +---------+---------+-----------+-------+---------+--------+---------+----------+
// | 0       | 0       | 0         | 0     | 1       | 0      | 0       | 0        |
// +---------+---------+-----------+-------+---------+--------+---------+----------+
// Enable interrupt 2 to assert when data is ready.
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000

// CTRL_REG4
// +---+-----+-----+-----+---+-----+-----+-----+
// | 0 | BLE | FS1 | FS0 | - | ST1 | ST0 | SIM |
// +---+-----+-----+-----+---+-----+-----+-----+
// | 0 | 0   | 0   | 1   | 0 | 0   | 0   | 0   |
// +---+-----+-----+-----+---+-----+-----+-----+
// Little endian mode. LSB @ lower address.
// Set full scale selection to 500 dps.
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0



int main() {

    /* SPI Initialization */

    // PF_9 --> Gyroscope SPI MOSI Pin
    // PF_8 --> Gyroscope SPI MISO Pin
    // PF_7 --> Gyroscope SPI Clock Pin
    // Using GPIO SSEL Line.
    SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);

    

    return 0;
}