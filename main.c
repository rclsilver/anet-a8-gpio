#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>


/*======================================================================
  Constants
======================================================================*/
#define INPUT    "in"
#define OUTPUT   "out"
#define LOW      0
#define HIGH     1
#define MAX_PINS 20
#define BUFFER_SIZE 512
#define BOUNCE_MSEC 100
#define LONG_PRESS  4000

/*======================================================================
  Configuration
======================================================================*/
#define PIN_IN_MAIN     27
#define PIN_IN_LED      22
#define PIN_OUT_MAIN    20
#define PIN_OUT_LED     21


/*======================================================================
  Global variables
======================================================================*/
static int input_pins[] = { PIN_IN_MAIN, PIN_IN_LED };
static int output_pins[] = { PIN_OUT_MAIN, PIN_OUT_LED };
static int n_input_pins = sizeof(input_pins) / sizeof(int);
static int n_output_pins = sizeof(output_pins) / sizeof(int);
int running = 1;


/*======================================================================
  write_to_file
  Helper function for writing a text string to a file
======================================================================*/
void write_to_file(const char *filename, const char *text) {
    FILE *f = fopen(filename, "w");

    if(f) {
        fprintf(f, text);
        fclose(f);
    } else {
        fprintf(stderr, "Can't write to %s\n", filename);
        perror("fopen");
        exit(-1);
    }
}


/*======================================================================
  set_output
======================================================================*/
void set_output(int pin, int value) {
    char filename[BUFFER_SIZE] = {0};
    char buffer[2] = {0};

    snprintf(filename, BUFFER_SIZE, "/sys/class/gpio/gpio%d/value", pin);
    buffer[0] = value == LOW ? '0' : '1';
    printf("Writing value '%s' to pin %d...\n", buffer, pin);
    write_to_file(filename, buffer);
}


/*======================================================================
  export_pin
======================================================================*/
void export_pin(int pin, const char *direction, int initial_value) {
    char buffer[BUFFER_SIZE] = {0};

    snprintf(buffer, BUFFER_SIZE, "%d", pin);
    write_to_file("/sys/class/gpio/export", buffer);

    snprintf(buffer, BUFFER_SIZE, "/sys/class/gpio/gpio%d/direction", pin);
    write_to_file(buffer, direction);

    if(!strcmp(INPUT, direction)) {
        snprintf(buffer, BUFFER_SIZE, "/sys/class/gpio/gpio%d/edge", pin);
        write_to_file(buffer, "rising");
    } else if(!strcmp(OUTPUT, direction)) {
        set_output(pin, initial_value);
    }
}


/*======================================================================
  export_pins
======================================================================*/
void export_pins(void) {
    printf("Exporting pins...\n");
    for(int i = 0; i < n_input_pins; ++i) {
        printf("Configuring pin %d as input...\n", input_pins[i]);
        export_pin(input_pins[i], INPUT, -1);
    }
    for(int i = 0; i < n_output_pins; ++i) {
        printf("Configuring pin %d as output...\n", output_pins[i]);
        export_pin(output_pins[i], OUTPUT, HIGH);
    }
}


/*======================================================================
  unexport_pin
======================================================================*/
void unexport_pin(int pin) {
    char buffer[BUFFER_SIZE] = {0};

    snprintf(buffer, BUFFER_SIZE, "%d", pin);
    write_to_file("/sys/class/gpio/unexport", buffer);
}


/*======================================================================
  unexport_pins
======================================================================*/
void unexport_pins(void) {
    printf("Unexporting pins...\n");
    for(int i = 0; i < n_input_pins; ++i) {
        printf("Disabling pin %d...\n", input_pins[i]);
        unexport_pin(input_pins[i]);
    }
    for(int i = 0; i < n_output_pins; ++i) {
        printf("Disabling pin %d...\n", output_pins[i]);
        unexport_pin(output_pins[i]);
    }
}


/*======================================================================
  open_inputs
======================================================================*/
void open_inputs(struct pollfd *fdset) {
    char filename[BUFFER_SIZE] = {0};
    int fd;

    for(int i = 0; i < n_input_pins; ++i) {
        printf("Opening pin value file %d...\n", input_pins[i]);
        snprintf(filename, BUFFER_SIZE, "/sys/class/gpio/gpio%d/value", input_pins[i]);

        if((fd = open(filename, O_RDONLY|O_NONBLOCK)) < 0) {
            fprintf(stderr, "Can't open GPIO device %s\n", filename);
            perror("open");
            exit(-1);
        }

        fdset[i].fd = fd;  
        fdset[i].events = POLLPRI;
    }
}


/*======================================================================
  close_inputs
======================================================================*/
void close_inputs(struct pollfd *fdset) {
    for(int i = 0; i < n_input_pins; ++i) {
        printf("Closing pin value file %d...\n", input_pins[i]);
        close(fdset[i].fd);
    }
}


/*======================================================================
  quit_signal 
  In response to a quit, or interrupt, we must stop main loop
======================================================================*/
void quit_signal(int dummy) {
    running = 0;
}


/*======================================================================
  current_timestamp 
  Returns current timestamp in ms
======================================================================*/
long current_timestamp(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
}


/*======================================================================
  get_pin_state 
  Read the state of the pin from the gpio 'value' pseudo file. 
  In principle this function can return -1 if the data read is in
  the wrong format but, in practice, it always seems to read exactly
  two bytes, of which the first is the digit 0 or 1, and the second
  is the EOL. It seems that the read() call will never block (which is,
  I suppose, to be expected) 
======================================================================*/
int get_pin_state(int pin) {
    char filename[BUFFER_SIZE] = {0};
    char buffer[BUFFER_SIZE] = {0};
    int fd;
    int value = -1;

    snprintf (filename, BUFFER_SIZE, "/sys/class/gpio/gpio%d/value", pin);

    if((fd = open(filename, O_RDONLY)) > 0) {
        if(read(fd, buffer, BUFFER_SIZE) == 2) {
            value = buffer[0] - '0';
        }
        close(fd);
    }

    return value;
}


/*======================================================================
  handle_button 
  Handle button pressed event
======================================================================*/
void handle_button(int pin, long msec) {
    int main_value = get_pin_state(PIN_OUT_MAIN);
    int led_value = get_pin_state(PIN_OUT_LED);

    switch(pin) {
        case PIN_IN_MAIN: {
            switch(main_value) {
                case LOW: {
                    // To prevent accident, we expect a long press
                    // to stop main power
                    if(msec >= LONG_PRESS) {
                        set_output(PIN_OUT_MAIN, HIGH);
                    }
                    break;
                }

                case HIGH: {
                    set_output(PIN_OUT_MAIN, LOW);
                    break;
                }

                default: {
                }
            }
            break;
        }

        case PIN_IN_LED: {
            // Only if main output is enabled
            if(LOW == main_value) {
                switch(led_value) {
                    case LOW: {
                        set_output(PIN_OUT_LED, HIGH);
                        break;
                    }

                    case HIGH: {
                        set_output(PIN_OUT_LED, LOW);
                        break;
                    }

                    default: {
                    }
                }
            }
            break;
        }

        default: {
        }
    }
}


/*======================================================================
  Entry point
======================================================================*/
int main(void) {
    long ticks[MAX_PINS];
    struct pollfd fdset[MAX_PINS];
    struct pollfd fdset_base[MAX_PINS];
    char buffer[BUFFER_SIZE];

    // Catch signals
    signal(SIGQUIT, quit_signal);
    signal(SIGTERM, quit_signal);
    signal(SIGHUP, quit_signal);
    signal(SIGINT, quit_signal);
    signal(SIGPIPE, quit_signal);

    // Export pins
    export_pins();

    // Initialzing...
    memset(ticks, 0, sizeof(ticks));
    memset(fdset_base, 0, sizeof(fdset_base));

    // Opening value files
    open_inputs(fdset_base);

    // Main loop
    while(running) {
        memcpy(&fdset, &fdset_base, sizeof(fdset));
        if(poll(fdset, n_input_pins, LONG_PRESS)) {
            for(int i = 0; i < n_input_pins; ++i) {
                if(fdset[i].revents & POLLPRI) {
                    if(read(fdset[i].fd, buffer, BUFFER_SIZE) == 2) {
                        int value = buffer[0] - '0';
                        long now = current_timestamp();

                        if(LOW == value) {
                            ticks[i] = now;
                        } else if(HIGH == value) {
                            if(ticks[i]) {
                                long delay = now - ticks[i];

                                if(delay >= BOUNCE_MSEC) {
                                    handle_button(input_pins[i], delay);
                                }

                                ticks[i] = 0;
                            }
                        }
                    }
                    lseek(fdset[i].fd, 0, SEEK_SET);
                }
            }
        } else {
            long now = current_timestamp();

            for(int i = 0; i < n_input_pins; ++i) {
                long delay = now - ticks[i];

                if(ticks[i] > 0 && delay >= LONG_PRESS) {
                    handle_button(input_pins[i], delay);
                    ticks[i] = 0;
                }
            }
        }
    }

    // Closing value files
    close_inputs(fdset_base);

    // Unexport pins
    unexport_pins();

    return 0;
}
