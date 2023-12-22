#include <mbed.h>                       // MBED Library.
#include "drivers/LCD_DISCO_F429ZI.h"   // LCD Library.
#include <float.h>

/* START: LCD Configuration */

#define BACKGROUND 1
#define FOREGROUND 0
#define GRAPH_PADDING 5

LCD_DISCO_F429ZI lcd;

// Buffer for holding displayed text strings.
char display_buf[22][60];

// Sets the background layer 
// to be visible, transparent, and
// resets its colors to all black.
void setup_background_layer(){
  lcd.SelectLayer(BACKGROUND);
  lcd.Clear(LCD_COLOR_BLACK);
  lcd.SetBackColor(LCD_COLOR_BLACK);
  lcd.SetTextColor(LCD_COLOR_GREEN);
  lcd.SetLayerVisible(BACKGROUND,ENABLE);
  lcd.SetTransparency(BACKGROUND,0x7Fu);
}

// Resets the foreground layer to
// all black.
void setup_foreground_layer(){
    lcd.SelectLayer(FOREGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_LIGHTGREEN);
}

/* END: LCD Configuration */

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

// Button pressed flag.
volatile bool button_pressed = false;

// Helper flag for countdown sequence text.
volatile bool countdown = false;

// EventFlags object construction.
EventFlags flags;

// Global constructor for LED DigitalOut.
DigitalOut led1(LED1);

// Global constructor for timer.
Timer t;

// Statically allocated location in memory for recorded z gyroscope values. 
// In my testing, the max recorded values for my capture settings was around 150 values;
// however, I've allocated extra just in case. 
volatile int16_t recorded_gyro_values_z[300];

// Provides a track of the outer bound of recorded_gyro_values within a given 0.5s interval.
volatile int value_index_track[45];

// Keeps track of how many values have been captured.
// Also, provides an index for the values in the preceding array. 
volatile int value_index = 0;

// Helper iterator for populating value_index_track array.
volatile int vit_count = 0;

// Helper iterator for interval.
volatile float curr_interval = 0.5;

// Time (seconds) to record values for.
#define RECORD_TIME 20

// SPI flag. Used for SPI transfers.
#define SPI_FLAG 1

// Data ready flag. Used for gyroscope data ready interrupt.
#define DATA_RDY_FLAG 2

// Scaling factor (Convert to radians per second)
#define SCALING_FACTOR (17.5f * 0.017453292519943295769236907684886f / 1000.0f)

// Total Samples (20 seconds / 0.5 seconds).
#define SAMPLES 40

// Sampling Interval.
#define SAMPLE_INTERVAL 0.5

// Radius from gyroscope placement to axis of rotation (i.e., hip leg socket)
#define RADIUS_ROT 0.19

// SPI callback function to service ISR.
void spi_cb(int event) {
    flags.set(SPI_FLAG);
}

// Data ready callback function to service ISR.
void data_rdy_cb() {
    flags.set(DATA_RDY_FLAG);
}

void reset_screen() {
    setup_background_layer();
    setup_foreground_layer();

    // Creates c-strings in the display buffers, in preparation
    // for displaying them on the screen.
    snprintf(display_buf[0],60,"The Embedded");
    snprintf(display_buf[1],60,"Gyrometer");
    snprintf(display_buf[9],60,"Rev_A_12102023");
    lcd.SelectLayer(FOREGROUND);

    // Display the buffered string on the screen.
    lcd.DisplayStringAt(0, LINE(0), (uint8_t *)display_buf[0], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(1), (uint8_t *)display_buf[1], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(19), (uint8_t *)display_buf[9], RIGHT_MODE);

    lcd.SelectLayer(FOREGROUND); 
}

// Start recording data callback function to service ISR.
void start_cb() {
    button_pressed = true;
    led1 = 1;
}

// Display UI helper text on LCD on how to start use of the system.
void startup_text() {
    snprintf(display_buf[2],60,"Press Blue Button");
    snprintf(display_buf[3],60,"To Start..");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[3], LEFT_MODE);
}

// Helper text to give user time to prepare before starting walk for more accurate readings
// (i.e., reduce human error).
void countdown_text() {
    reset_screen();
    snprintf(display_buf[2],60,"3..");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    thread_sleep_for(1000);

    snprintf(display_buf[2],60,"2..");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    thread_sleep_for(1000);
    reset_screen();

    snprintf(display_buf[2],60,"1..");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    thread_sleep_for(1000);
    reset_screen();

    snprintf(display_buf[2],60,"GO!");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    thread_sleep_for(200);
    reset_screen();

    // After user has been given the "GO!" signal, we'll start timer to start recording values.
    t.start();
}

// Processes data (i.e., convert measured data to forward movement velocity and then distance).
void processing() {
    snprintf(display_buf[2],60,"Processing..");
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    thread_sleep_for(1000);

    float distance_traveled = 0.0;
    int lower_bound = 0;
    for (int i = 0; i < SAMPLES - 1; i++) {
        float change_in_angle = 0.0;
        for (int j = lower_bound; j <= value_index_track[i]; j++) {
            if (j == lower_bound || j == value_index_track[i]) {
                change_in_angle += fabs((recorded_gyro_values_z[j] * SCALING_FACTOR));
            } else {
                change_in_angle += 2 * fabs((recorded_gyro_values_z[j] * SCALING_FACTOR));
            }
        }
        change_in_angle *= (SAMPLE_INTERVAL / 2);
        distance_traveled += (change_in_angle * RADIUS_ROT);
        printf("Distance Traveled: %f\n", distance_traveled);
        lower_bound = value_index_track[i] + 1;
    }

    printf("Total Distance Traveled: %f\n", distance_traveled);
    snprintf(display_buf[2],60,"Total Distance Traveled:");
    snprintf(display_buf[3],60, "%f meters.", distance_traveled);
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[3], LEFT_MODE);
    thread_sleep_for(10000);

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

    // Establish communicating device (read WHOAMI register).
    write_buffer[0] = 0x8f;
    spi.transfer(write_buffer, 2, read_buffer, 2, spi_cb);
    flags.wait_all(SPI_FLAG);
    printf("Gyroscope Identifier (WHOAMI) = 0x%X\n", read_buffer[1]);

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

    // Reboot condition. Gyroscope has data-ready interrupt configured already
    // on second run. Pin level may rise before interrupt handler configured.  
    // Manually check the signal and set the flag for the first sample.
    if(!(flags.get() & DATA_RDY_FLAG) && (int2.read() == 1)) {
        flags.set(DATA_RDY_FLAG);
    }

    InterruptIn int_button(PA_0);
    int_button.rise(&start_cb);

    /* START: LCD-related */

    // Set up the initial screen display.
    reset_screen();

    /* END: LCD-related */

    while(1) {
        int16_t raw_gx, raw_gy, raw_gz;
        float gx, gy, gz;

        if (button_pressed) {

            // Clear startup text instructions.
            reset_screen();

            if (!countdown) {
                countdown = true;
                countdown_text();
            }

            flags.wait_all(DATA_RDY_FLAG);
            write_buffer[0] = OUT_X_L | 0x80 | 0x40;

            spi.transfer(write_buffer, 7, read_buffer, 7, spi_cb);
            flags.wait_all(SPI_FLAG);

            // Real-time pre-processing of raw data.
            raw_gx = (((uint16_t)read_buffer[2]) << 8) | ((uint16_t)read_buffer[1]);
            raw_gy = (((uint16_t)read_buffer[4]) << 8) | ((uint16_t)read_buffer[3]);
            raw_gz = (((uint16_t)read_buffer[6]) << 8) | ((uint16_t)read_buffer[5]);

            recorded_gyro_values_z[value_index] = raw_gz;
            //printf("%d\n", recorded_gyro_values_z[value_index]);
            
            value_index++;
            printf("%d\n\n", value_index);


            gx = ((float)raw_gx) * SCALING_FACTOR;
            gy = ((float)raw_gy) * SCALING_FACTOR;
            gz = ((float)raw_gz) * SCALING_FACTOR; 

            snprintf(display_buf[5],60,"X-AXIS: ");
            snprintf(display_buf[6],60,"Y-AXIS: ");
            snprintf(display_buf[7],60,"Z-AXIS: ");

            lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[5], LEFT_MODE);
            lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[6], LEFT_MODE);
            lcd.DisplayStringAt(0, LINE(7), (uint8_t *)display_buf[7], LEFT_MODE);

            snprintf(display_buf[2],60,"%4.5f|g", gx);
            snprintf(display_buf[3],60,"%4.5f|g", gy);
            snprintf(display_buf[4],60,"%4.5f|g", gz);

            lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], RIGHT_MODE);
            lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[3], RIGHT_MODE);
            lcd.DisplayStringAt(0, LINE(7), (uint8_t *)display_buf[4], RIGHT_MODE);

            thread_sleep_for(100);
            
            snprintf(display_buf[8],60,"            ");
            lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[8], RIGHT_MODE);
            lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[8], RIGHT_MODE);
            lcd.DisplayStringAt(0, LINE(7), (uint8_t *)display_buf[8], RIGHT_MODE);
        
        } else {

            // Keep displaying startup text.
            startup_text();
           
        }

        // Record values until time limit has been reached. 
        // Then, wait for user's button press again.
        if (button_pressed) {
            float time_elapsed = t.read();

            // Obtain time-stamps for 0.5 second interval capture.
            if (time_elapsed >= curr_interval) {
                value_index_track[vit_count] = value_index - 1;
                printf("  %d\n", value_index_track[vit_count]);
                vit_count++;
                curr_interval += 0.5;
            }

            if (time_elapsed >= RECORD_TIME) {
                printf("%f\n", time_elapsed);
                button_pressed = false;
                countdown = false;
                led1 = 0;
                value_index = 0;
                vit_count = 0;
                curr_interval = 0.5;
                reset_screen();
                t.stop();
                t.reset();

                processing();
                reset_screen();
            }
        }
    }

    return 0;
}