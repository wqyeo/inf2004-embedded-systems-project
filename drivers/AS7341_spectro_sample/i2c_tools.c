#include <stdio.h>
#include <pico/stdlib.h>
#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/irq.h>
#include <hardware/regs/intctrl.h>
#include "i2c_tools.h"

static int _timeout = 500;
static i2c_inst_t *_i2c;
static int _sda;
static int _scl;
static int _clkHz;

static bool _running;
static bool _slave;
static uint8_t _addr;
static bool _txBegun;

static uint8_t _buff[WIRE_BUFFER_SIZE];
static int _buffLen;
static int _buffOff;
static PinMode _pm[30];

// TWI clock frequency
static const uint32_t TWI_CLOCK = 100000;

void digitalWrite(int pin, int val)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    if (_pm[pin] == INPUT_PULLDOWN)
    {
        if (val == LOW)
        {
            gpio_set_dir(pin, false);
        }
        else
        {
            gpio_set_dir(pin, true);
        }
    }
    else if (_pm[pin] == INPUT_PULLUP)
    {
        if (pin == HIGH)
        {
            gpio_set_dir(pin, false);
        }
        else
        {
            gpio_set_dir(pin, true);
        }
    }
    else
    {
        gpio_put(pin, val == LOW ? 0 : 1);
    }
}

bool digitalRead(int pin)
{
    return gpio_get(pin) ? HIGH : LOW;
}

void pinMode(int pin, int mode)
{
    switch (mode)
    {
    case INPUT:
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_disable_pulls(pin);
        break;
    case INPUT_PULLUP:
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_up(pin);
        gpio_put(pin, 0);
        break;
    case INPUT_PULLDOWN:
        gpio_init(pin);
        gpio_set_dir(pin, false);
        gpio_pull_down(pin);
        gpio_put(pin, 1);
        break;
    case OUTPUT:
    case OUTPUT_4MA:
        gpio_init(pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_4MA);
        gpio_set_dir(pin, true);
        break;
    case OUTPUT_2MA:
        gpio_init(pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_dir(pin, true);
        break;
    case OUTPUT_8MA:
        gpio_init(pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_8MA);
        gpio_set_dir(pin, true);
        break;
    case OUTPUT_12MA:
        gpio_init(pin);
        gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
        gpio_set_dir(pin, true);
        break;
    }
    _pm[pin] = mode;
}

void i2c_tools_init(i2c_inst_t *i2c, int sda, int scl)
{
    _sda = sda;
    _scl = scl;
    _i2c = i2c;
    _clkHz = TWI_CLOCK;
    _running = false;
    _txBegun = false;
    _buffLen = 0;
}

bool i2c_tools_setSDA(int pin)
{
    if ((!_running))
    {
        _sda = pin;
        return true;
    }

    if (_sda == pin)
    {
        return true;
    }

    if (_running)
    {
        panic("FATAL: Attempting to set Wire%s.SDA while running", i2c_hw_index(_i2c) ? "1" : "");
    }
    else
    {
        panic("FATAL: Attempting to set Wire%s.SDA to illegal pin %d", i2c_hw_index(_i2c) ? "1" : "", pin);
    }
    return false;
}

bool i2c_tools_setSCL(int pin)
{
    if ((!_running))
    {
        _scl = pin;
        return true;
    }

    if (_scl == pin)
    {
        return true;
    }

    if (_running)
    {
        panic("FATAL: Attempting to set Wire%s.SCL while running", i2c_hw_index(_i2c) ? "1" : "");
    }
    else
    {
        panic("FATAL: Attempting to set Wire%s.SCL to illegal pin %d", i2c_hw_index(_i2c) ? "1" : "", pin);
    }
    return false;
}

void i2c_tools_setClock(uint32_t hz)
{
    _clkHz = hz;
    if (_running)
    {
        i2c_set_baudrate(_i2c, hz);
    }
}

void i2c_tools_begin()
{
    if (_running)
    {
        // ERROR
        return;
    }
    _slave = false;
    i2c_init(_i2c, _clkHz);
    i2c_set_slave_mode(_i2c, false, 0);
    gpio_set_function(_sda, GPIO_FUNC_I2C);
    gpio_pull_up(_sda);
    gpio_set_function(_scl, GPIO_FUNC_I2C);
    gpio_pull_up(_scl);

    _running = true;
    _txBegun = false;
    _buffLen = 0;
}

void i2c_tools_end()
{
    if (!_running)
    {
        // ERROR
        return;
    }

    i2c_deinit(_i2c);

    pinMode(_sda, INPUT);
    pinMode(_scl, INPUT);
    _running = false;
    _txBegun = false;
}

void i2c_tools_beginTransmission(uint8_t addr)
{
    if (!_running || _txBegun)
    {
        // ERROR
        return;
    }
    _addr = addr;
    _buffLen = 0;
    _buffOff = 0;
    _txBegun = true;
}

size_t i2c_tools_requestFrom_w_stopbit(uint8_t address, size_t quantity, bool stopBit)
{
    if (!_running || _txBegun || !quantity || (quantity > sizeof(_buff)))
    {
        return 0;
    }

    _buffLen = i2c_read_blocking_until(_i2c, address, _buff, quantity, !stopBit, make_timeout_time_ms(_timeout));
    if ((_buffLen == PICO_ERROR_GENERIC) || (_buffLen == PICO_ERROR_TIMEOUT))
    {
        _buffLen = 0;
    }
    _buffOff = 0;
    return _buffLen;
}

size_t i2c_tools_requestFrom(uint8_t address, size_t quantity)
{
    return i2c_tools_requestFrom_w_stopbit(address, quantity, true);
}

static bool _clockStretch(int pin)
{
    uint64_t end = time_us_64() + 100;
    while ((time_us_64() < end) && (!digitalRead(pin)))
    { /* noop */
    }
    return digitalRead(pin);
}

bool _probe(int addr, int sda, int scl, int freq)
{
    int delay = (1000000 / freq) / 2;
    bool ack = false;

    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    gpio_set_function(scl, GPIO_FUNC_SIO);
    gpio_set_function(sda, GPIO_FUNC_SIO);

    digitalWrite(sda, HIGH);
    sleep_us(delay);
    digitalWrite(scl, HIGH);
    if (!_clockStretch(scl))
    {
        goto stop;
    }
    digitalWrite(sda, LOW);
    sleep_us(delay);
    digitalWrite(scl, LOW);
    sleep_us(delay);
    for (int i = 0; i < 8; i++)
    {
        addr <<= 1;
        digitalWrite(sda, (addr & (1 << 7)) ? HIGH : LOW);
        sleep_us(delay);
        digitalWrite(scl, HIGH);
        sleep_us(delay);
        if (!_clockStretch(scl))
        {
            goto stop;
        }
        digitalWrite(scl, LOW);
        sleep_us(5); // Ensure we don't change too close to clock edge
    }

    digitalWrite(sda, HIGH);
    sleep_us(delay);
    digitalWrite(scl, HIGH);
    if (!_clockStretch(scl))
    {

        goto stop;
    }

    ack = digitalRead(sda) == LOW;
    sleep_us(delay);
    digitalWrite(scl, LOW);

stop:
    sleep_us(delay);
    digitalWrite(sda, LOW);
    sleep_us(delay);
    digitalWrite(scl, HIGH);
    sleep_us(delay);
    digitalWrite(sda, HIGH);
    sleep_us(delay);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_set_function(sda, GPIO_FUNC_I2C);

    return ack;
}

// Errors:
//  0 : Success
//  1 : Data too long
//  2 : NACK on transmit of address
//  3 : NACK on transmit of data
//  4 : Other error
uint8_t i2c_tools_endTransmission_w_stopbit(bool stopBit)
{
    if (!_running || !_txBegun)
    {
        return 4;
    }
    _txBegun = false;
    if (!_buffLen)
    {
        // Special-case 0-len writes which are used for I2C probing
        return _probe(_addr, _sda, _scl, _clkHz) ? 0 : 2;
    }
    else
    {
        int len = _buffLen;
        int ret = i2c_write_blocking_until(_i2c, _addr, _buff, _buffLen, !stopBit, make_timeout_time_ms(_timeout));
        _buffLen = 0;
        return (ret == len) ? 0 : 4;
    }
}

uint8_t i2c_tools_endTransmission()
{
    return i2c_tools_endTransmission_w_stopbit(true);
}

size_t i2c_tools_write(uint8_t ucData)
{
    if (!_running)
    {
        return 0;
    }

    if (_slave)
    {
        // Wait for a spot in the TX FIFO
        while (0 == (_i2c->hw->status & (1 << 1)))
        { /* noop wait */
        }
        _i2c->hw->data_cmd = ucData;
        return 1;
    }
    else
    {
        if (!_txBegun || (_buffLen == sizeof(_buff)))
        {
            return 0;
        }
        _buff[_buffLen++] = ucData;
        return 1;
    }
}

size_t i2c_tools_write_w_quantity(const uint8_t *data, size_t quantity)
{
    for (size_t i = 0; i < quantity; ++i)
    {
        if (!i2c_tools_write(data[i]))
        {
            return i;
        }
    }

    return quantity;
}

int i2c_tools_available(void)
{
    return _running ? _buffLen - _buffOff : 0;
}

int i2c_tools_read(void)
{
    if (i2c_tools_available())
    {
        return _buff[_buffOff++];
    }
    return -1; // EOF
}

int i2c_tools_peek(void)
{
    if (i2c_tools_available())
    {
        return _buff[_buffOff];
    }
    return -1; // EOF
}

void i2c_tools_flush(void)
{
    // Do nothing, use endTransmission(..) to force
    // data transfer.
}

void hexToAscii(uint32_t n)
{
    printf("%c%c%c%c%c%c%c%c\n",
           '0' + ((n) >> 28 & 0xF),
           '0' + ((n) >> 24 & 0xF),
           '0' + ((n) >> 20 & 0xF),
           '0' + ((n) >> 16 & 0xF),
           '0' + ((n) >> 12 & 0xF),
           '0' + ((n) >> 8 & 0xF),
           '0' + ((n) >> 4 & 0xF),
           '0' + ((n) & 0xF));
}

#ifndef __WIRE0_DEVICE
#define __WIRE0_DEVICE i2c0
#endif
#ifndef __WIRE1_DEVICE
#define __WIRE1_DEVICE i2c1
#endif