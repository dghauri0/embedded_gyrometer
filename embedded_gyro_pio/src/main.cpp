#include <mbed.h>

/* START: Gyroscope Register Addresses */

// Control Registers
#define CTRL_REG1 0x20
#define CTRL_REG2 0x21
#define CTRL_REG3 0x22
#define CTRL_REG4 0x23

// Output Registers
// (Only start of output registers shown,
//  SPI will continue to next adjacent memory
//  locations for remaining output values).
#define OUT_X_L 0x28 

/* END: Gyroscope Register Addresses */


/* START: Gyroscope Control Register Configurations */

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

/* END: Gyroscope Control Register Configurations */

// EventFlags object construction.
EventFlags flags;

// SPI flag. Used for SPI transfers.
#define SPI_FLAG 1

// Data ready flag. Used for gyroscope data ready interrupt.
#define DATA_RDY_FLAG 2

// Scaling factor
#define SCALING_FACTOR (17.5f * 0.017453292519943295769236907684886f / 1000.0f)

// SPI callback function to service ISR.
void spi_cb(int event) {
    flags.set(SPI_FLAG);
}

// Data ready callback function to service ISR.
void data_rdy_cb() {
    flags.set(DATA_RDY_FLAG);
}

int main() {
    // Write (TX) and Read (RX) buffers for SPI communication.
    uint8_t write_buffer[32], read_buffer[32];

    /* START: SPI Initialization and Setup */

    // PF_9 --> Gyroscope SPI MOSI Pin
    // PF_8 --> Gyroscope SPI MISO Pin
    // PF_7 --> Gyroscope SPI Clock Pin
    // Using GPIO SSEL Line.
    SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);

    // 8-bits per SPI frame.
    // Clock polarity and phase mode, both 1.
    spi.format(8, 3);

    // Default SPI bus clock frequency (1 MHz).
    spi.frequency(1'000'000);

    /* END: SPI Initialization and Setup */


    /* START: Interrupt Initialization and Setup */

    // PA_2 --> Gyroscope INT2 Pin
    InterruptIn int2(PA_2, PullDown);

    // Set interrupt 2 to trigger routine on rising edge.
    int2.rise(&data_rdy_cb);

    /* END: Interrupt Initialization and Setup */

    /* START: Write configurations to control registers. */

    // CTRL_REG1
    write_buffer[0] = CTRL_REG1;
    write_buffer[1] = CTRL_REG1_CONFIG;
    spi.transfer(write_buffer, 2, read_buffer, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    // CTRL_REG3
    write_buffer[0] = CTRL_REG3;
    write_buffer[1] = CTRL_REG3_CONFIG;
    spi.transfer(write_buffer, 2, read_buffer, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    // CTRL_REG4
    write_buffer[0] = CTRL_REG4;
    write_buffer[1] = CTRL_REG4_CONFIG;
    spi.transfer(write_buffer, 2, read_buffer, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    /* END: Write configurations to control registers. */

    while(1) {
        uint16_t raw_gx, raw_gy, raw_gz;
        float gx, gy, gz;

        flags.wait_all(DATA_RDY_FLAG);
        write_buffer[0] = OUT_X_L | 0x80 | 0x40;

        spi.transfer(write_buffer, 7, read_buffer, 7, spi_cb);
        flags.wait_all(SPI_FLAG);

        //Process raw data
        raw_gx = (((uint16_t)read_buffer[2]) << 8) | ((uint16_t)read_buffer[1]);
        raw_gy = (((uint16_t)read_buffer[4]) << 8) | ((uint16_t)read_buffer[3]);
        raw_gz = (((uint16_t)read_buffer[6]) << 8) | ((uint16_t)read_buffer[5]);

        gx = ((float)raw_gx) * SCALING_FACTOR;
        gy = ((float)raw_gy) * SCALING_FACTOR;
        gz = ((float)raw_gz) * SCALING_FACTOR;

        //No Filter
        
        printf("RAW -> \t\tgx: %d \t gy: %d \t gz: %d\t\n", raw_gx, raw_gy, raw_gz);
        printf(">x_axis_raw:%4.5f|g\n", gx);
        printf(">y_axis_raw:%4.5f|g\n", gy);
        printf(">z_axis_raw:%4.5f|g\n", gz);

        thread_sleep_for(100);       
    }

    return 0;
}