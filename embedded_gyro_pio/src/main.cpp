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

// EventFlags object construction.
EventFlags flags;

// Global constructor for LED DigitalOut.
DigitalOut led1(LED1);

// Global constructor for timer.
Timer t;

// Time to record values for.
#define RECORD_TIME 20

// SPI flag. Used for SPI transfers.
#define SPI_FLAG 1

// Data ready flag. Used for gyroscope data ready interrupt.
#define DATA_RDY_FLAG 2

// Scaling factor (Convert to radians per second)
#define SCALING_FACTOR (17.5f * 0.017453292519943295769236907684886f / 1000.0f)

// SPI callback function to service ISR.
void spi_cb(int event) {
    flags.set(SPI_FLAG);
}

// Data ready callback function to service ISR.
void data_rdy_cb() {
    flags.set(DATA_RDY_FLAG);
}

void start_cb() {
    
    button_pressed = true;
    t.start();
}

void reset_screen() {
    setup_background_layer();
    setup_foreground_layer();

    //creates c-strings in the display buffers, in preparation
    //for displaying them on the screen
    // snprintf(display_buf[0],60,"width: %d pixels",lcd.GetXSize());
    // snprintf(display_buf[1],60,"height: %d pixels",lcd.GetYSize());
    snprintf(display_buf[0],60,"The Embedded");
    snprintf(display_buf[1],60,"Gyrometer");
    snprintf(display_buf[9],60,"Rev_A_12102023");
    snprintf(display_buf[2],60,"Press Blue Button");
    snprintf(display_buf[3],60,"To Start..");
    lcd.SelectLayer(FOREGROUND);
    //display the buffered string on the screen
    lcd.DisplayStringAt(0, LINE(0), (uint8_t *)display_buf[0], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(1), (uint8_t *)display_buf[1], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(19), (uint8_t *)display_buf[9], RIGHT_MODE);
    lcd.DisplayStringAt(0, LINE(5), (uint8_t *)display_buf[2], LEFT_MODE);
    lcd.DisplayStringAt(0, LINE(6), (uint8_t *)display_buf[3], LEFT_MODE);

    //draw the graph window on the background layer
    // with x-axis tick marks every 10 pixels
    //draw_graph_window(10);


    lcd.SelectLayer(FOREGROUND); 
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
    //thread_sleep_for(5000);
    printf("Gyroscope Identifier (WHOAMI) = 0x%X\n", read_buffer[1]);
    //thread_sleep_for(5000);

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
    int_button.fall(&start_cb);

    /* START: LCD-related */

    reset_screen();

    /* END: LCD-related */

    float gx_min = FLT_MAX;
    float gy_min = FLT_MAX;
    float gz_min = FLT_MAX;

    float gx_max = FLT_MIN;
    float gy_max = FLT_MIN;
    float gz_max = FLT_MIN;

    while(1) {
        int16_t raw_gx, raw_gy, raw_gz;
        float gx, gy, gz;

        if (button_pressed) {

          flags.wait_all(DATA_RDY_FLAG);
          write_buffer[0] = OUT_X_L | 0x80 | 0x40;

          spi.transfer(write_buffer, 7, read_buffer, 7, spi_cb);
          flags.wait_all(SPI_FLAG);

          // Process raw data
          raw_gx = (((uint16_t)read_buffer[2]) << 8) | ((uint16_t)read_buffer[1]);
          raw_gy = (((uint16_t)read_buffer[4]) << 8) | ((uint16_t)read_buffer[3]);
          raw_gz = (((uint16_t)read_buffer[6]) << 8) | ((uint16_t)read_buffer[5]);

          gx = ((float)raw_gx) * SCALING_FACTOR;
          gy = ((float)raw_gy) * SCALING_FACTOR;
          gz = ((float)raw_gz) * SCALING_FACTOR;

          // Add to array!!!
          
          //printf("RAW -> \t\tgx: %d \t gy: %d \t gz: %d\t\n", raw_gx, raw_gy, raw_gz);
          // printf(">x_axis:%4.5f|g\n", gx);
          // printf(">y_axis:%4.5f|g\n", gy);
          // printf(">z_axis:%4.5f|g\n", gz);
  
          // printf(">x_axis_raw:%d\n", raw_gx);
          // printf(">y_axis_raw:%d\n", raw_gy);
          // printf(">z_axis_raw:%d\n", raw_gz);

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
        
        }

        float time_elapsed = t.read();
        if (time_elapsed >= RECORD_TIME) {
          printf("%f\n", time_elapsed);
          button_pressed = false;
          reset_screen();
          t.reset();
        }
        //flags.clear(START_FLAG);
    }

    return 0;
}