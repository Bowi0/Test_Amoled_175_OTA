#ifndef TEST_ARDUINO_H
#define TEST_ARDUINO_H

#include <cstddef>
#include <cstdint>

#define SERIAL_8N1 0
#define LOW 0
#define HIGH 1
#define OUTPUT 1

uint32_t millis();
void delay(uint32_t ms);
void digitalWrite(uint8_t pin, int value);
void pinMode(uint8_t pin, int mode);

class HardwareSerial {
public:
    explicit HardwareSerial(int) {}
    void begin(int baud, int config, int rx, int tx);
    int available();
    int availableForWrite() { return 256; }
    int read();
    size_t write(const uint8_t *data, size_t size);
    void flush() {}
private:
    int endpoint = -1;
};

struct TestSerial {
    void begin(int) {}
    void println(const char *) {}
    template <typename... Args> void printf(const char *, Args...) {}
};
struct TestESP { void restart(); };
extern TestSerial Serial;
extern TestESP ESP;

#endif
