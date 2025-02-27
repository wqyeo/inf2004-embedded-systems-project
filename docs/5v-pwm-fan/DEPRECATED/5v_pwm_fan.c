#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"

#define FAN_TACHO_PIN      15
#define FAN_PWM_PIN        2
// In percentage...
#define DEFAULT_DUTY_CYCLE 100

volatile uint32_t tacho_count = 0;

// Called every time the pseudo button is pressed or released.
void tacho_callback(uint gpio, uint32_t events)
{
    if (events & GPIO_IRQ_EDGE_RISE)
    {
        tacho_count++;
    }
}

float calculate_fan_speed(uint32_t tacho_count)
{
    return (float) tacho_count * 60.0 / 2.0;
}

bool tacho_speed_check_callback(struct repeating_timer *t) 
{
    printf("Tacho count: %d (%.2f RPM)\n", tacho_count, calculate_fan_speed(tacho_count));
    tacho_count = 0;
    return true;
}

// Calculate the duty cycle based on the wrap value and the duty cycle percentage.
float get_pwm_dc_level(int duty_cycle_percent)
{
    // Clamp the duty cycle percentage, between 20% to 100%
    duty_cycle_percent = duty_cycle_percent < 20 ? 20 : duty_cycle_percent;
    duty_cycle_percent = duty_cycle_percent > 100 ? 100 : duty_cycle_percent;

    return (float) (50 * (duty_cycle_percent / 100.0));
}

void set_fan_speed(int speed_percent)
{
    uint fan_pwm_slice_num = pwm_gpio_to_slice_num(FAN_PWM_PIN);
    pwm_set_chan_level(fan_pwm_slice_num, PWM_CHAN_A, get_pwm_dc_level(speed_percent));
}

int main()
{
    stdio_init_all();
    // Sets up tachometer pin; Latches pin to HIGH state...
    gpio_pull_up(FAN_TACHO_PIN);
    gpio_set_irq_enabled_with_callback(FAN_TACHO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &tacho_callback);

    // Sets up pwm pin; Latches pin to LOW state...
    gpio_pull_down(FAN_PWM_PIN);

    gpio_set_function(FAN_PWM_PIN, GPIO_FUNC_PWM);
    
//#region CLOCK_SPEED_SETUP
    uint fan_pwm_slice_num = pwm_gpio_to_slice_num(FAN_PWM_PIN);
    // Step down clock diver to 1.25MHz, then further divide by 50 to get 25kHz
    pwm_set_clkdiv(fan_pwm_slice_num, 100);
    pwm_set_wrap(fan_pwm_slice_num, 50);
//#endregion
    
    set_fan_speed(DEFAULT_DUTY_CYCLE);
    // Enable the pwm channel
    pwm_set_enabled(fan_pwm_slice_num, true);
    
    struct repeating_timer timer;
    add_repeating_timer_ms(-1000, tacho_speed_check_callback, NULL, &timer);

    int speed_percent = 20;
    while(1)
    {
        set_fan_speed(speed_percent);
        printf("Fan duty cycle: %d\n", speed_percent);
        sleep_ms(5000);
        speed_percent += 20;
        if (speed_percent > 100)
        {
            speed_percent = 20;
        }
    };
}
