/**
 * \file
 *
 * \brief Empty user application template
 *
 */

/**
 * \mainpage User Application template doxygen documentation
 *
 * \par Empty user application template
 *
 * Bare minimum empty user application template
 *
 * \par Content
 *
 * -# Include the ASF header files (through asf.h)
 * -# "Insert system clock initialization code here" comment
 * -# Minimal main function that starts with a call to board_init()
 * -# "Insert application code here" comment
 *
 */

/*
 * Include header files for all drivers that have been imported from
 * Atmel Software Framework (ASF).
 */
/*
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */
#include <asf.h>

#if 0
#include <compiler.h>
#include <sysclk.h>
#include <gpio.h>
#include <delay.h>
#include <adc.h>
#endif

#include "main.h"



/* GLOBAL section */

uint16_t			g_adc_ldr							= 0;
uint16_t			g_adc_ldr_last						= 0;



/* MAIN STATIC section */

static uint8_t		runmode								= 0;			// global runmode



/* HELPERS */

static void s_reset_global_vars(void)
{
	sei();
	g_adc_ldr_last = g_adc_ldr = 0;
	cli();
}

static void s_asm_break(void)
{
	__asm__ __volatile__ ("break" ::: "memory");
}


/* ISR routines */

#if 0
void bad_interrupt(void)
{
	s_asm_break();
}

ISR(__vector_1)
{	/* INT0 */
	bad_interrupt();
}

ISR(__vector_2)
{	/* INT1 */
	bad_interrupt();
}

ISR(__vector_3)
{	/* PCINT0 */
	bad_interrupt();
}

ISR(__vector_4)
{	/* PCINT1 */
	bad_interrupt();
}

ISR(__vector_5)
{	/* PCINT2 */
	bad_interrupt();
}

ISR(__vector_6)
{	/* WDT - Watchdog Timeout */
	bad_interrupt();
}

ISR(__vector_7)
{	/* TIMER 2 COMP-A */
	bad_interrupt();
}

ISR(__vector_8)
{	/* TIMER 2 COMP-B */
	bad_interrupt();
}

ISR(__vector_9)
{	/* TIMER 2 OVF - Overflow */
	bad_interrupt();
}

ISR(__vector_10)
{	/* TIMER 1 CAPT */
	bad_interrupt();
}

ISR(__vector_11)
{	/* TIMER 1 COMP-A */
	bad_interrupt();
}

ISR(__vector_12)
{	/* TIMER 1 COMP-B */
	bad_interrupt();
}

ISR(__vector_13)
{	/* TIMER 1 OVF - Overflow */
	bad_interrupt();
}

ISR(__vector_14)
{	/* TIMER 0 COMP-A */
	bad_interrupt();
}

ISR(__vector_15)
{	/* TIMER 0 COMP-B */
	bad_interrupt();
}

ISR(__vector_16)
{	/* TIMER 0 OVF - Overflow */
	bad_interrupt();
}

ISR(__vector_17)
{	/* SPI, STC - Serial Transfer Complete */
	bad_interrupt();
}

ISR(__vector_18)
{	/* USART, RX - Complete */
	bad_interrupt();
}

ISR(__vector_19)
{	/* USART, UDRE - Data Register Empty */
	bad_interrupt();
}

ISR(__vector_20)
{	/* USART, TX - Complete */
	bad_interrupt();
}
#endif

ISR(__vector_21)
{	/* ADC */
	uint16_t adc_val = ADCL;
	adc_val |= ADCH << 8;
	
	g_adc_ldr_last = g_adc_ldr;
	g_adc_ldr  = adc_val;
}

#if 0
ISR(__vector_22)
{	/* EEREADY */
	bad_interrupt();
}

ISR(__vector_23)
{	/* ANALOG COMP */
	bad_interrupt();
}

ISR(__vector_24)
{	/* TWI */
	bad_interrupt();
}

ISR(__vector_25)
{	/* SPM READY - Store Program Memory Ready */
	bad_interrupt();
}
#endif


/* INIT section */

static void s_tc_init(void)
{
	sysclk_set_prescalers(1);
	
	/* Timer Synchronous Mode - prepare */
	GTCCR   = (1 << TSM)					// Timer Synchronous Mode active
		    | (1 << PSRASY)					// Timer 2   prescaler is synced
		    | (1 << PSRSYNC);				// Timer 0/1 prescaler is synced

	
	/* TC0: not in use */
	{
		sysclk_disable_module(PRR, PRTIM0);
	}

	/* TC1 - OC1A: Audio output @ 16-bit counter PWM, used: 10-bit resolution - overflows with 15625 Hz */
	{
		sysclk_enable_module(PRR, PRTIM1);

		TCCR1A  = (0b10  << COM1A0)		 		// HI --> LO when compare value is reached - non-inverted PWM mode
				| (0b11  << WGM10);				// WGM: 0b0111 = Fast PWM 10 bit

		TCCR1B  = (0b01  << WGM12)		 
				| (0b001 << CS10);				// CLKio DIV 1 = 16 MHz
		   
		TCCR1C  = 0;

		TCNT1H  = 0b00000000           ;		// Clear current value for synchronous start (when restarting without reset)
		TCNT1L	=            0b00000000;
	
		OCR1AH  =       0b10           ;		// Mid-range compare value for zero audio output
		OCR1AL  =            0b00000000;
	
		TIMSK1  = 0;							// no interrupts (when restarting without reset)
		TIFR1   = 0b00100111;					// clear all flags (when restarting without reset)
	}

	/* TC2 - OC2A: LCD backlight w/ 8-bit resolution - overflows with abt. 61 Hz */
	{
		sysclk_enable_module(PRR, PRTIM2);
	
		TCCR2A  = (0b10  << COM2A0)				// HI --> LO when compare value is reached - non-inverted PWM mode
				| (0b11  << WGM20);				// WGM: 0b011 = Fast PWM mode 8 bit

		TCCR2B  = (0b0   << WGM22)
				| (0b111 << CS20);				// CLKio DIV 1024 = 15625 Hz
	
		TCNT2   = 0;							// Clear current value for synchronous start (when restarting without reset)
	
		OCR2A   = 0x40;							// LCD backlight dimmed down to 25% 

		TIMSK2  = 0;							// no interrupts (when restarting without reset)
		TIFR2   = 0b00000111;					// clear all flags (when restarting without reset)
	
		ASSR    = 0;							// no Async TOSC1 mode
	}
}

static void s_tc_start(void)
{
	/* TC0: not in use */
	/* TC1: Audio output @ 16-bit counter PWM, used: 10-bit resolution */
	/* TC2: LCD backlight w/ 8-bit resolution */
	{
		/* Timer Synchronous Mode - trigger */
		GTCCR   = (1 << PSRSYNC);			    // trigger the sync for all counters
	}
}


static void s_adc_init(void)
{
	sysclk_enable_module(PRR, PRADC);			// enable ADC sub-module
	adc_init(ADC_PRESCALER_DIV128);

	irqflags_t flags = cpu_irq_save();

	adc_disable_digital_inputs(1 << ADC0D);		// disable the digital input on the ADC0 port
	adc_set_admux(ADC_MUX_ADC0 | ADC_VREF_1V1 | ADC_ADJUSTMENT_RIGHT);
	//adc_set_mux(ADC_MUX_ADC0);
	//adc_set_voltage_reference(ADC_VREF_1V1);

	adc_set_autotrigger_source(ADC_AUTOTRIGGER_SOURCE_TC1_OVERFLOW);
	adc_enable_interrupt();						// enable the ADC interrupt

	cpu_irq_restore(flags);
}


/* TASK section */

static void s_task_backlight(uint16_t adc)
{
	/* calculate the 8-bit backlight PWM value based on the ADC LDR voltage */
	const uint16_t	MAX_INTENSITY		= 10000;
	const uint16_t	BL_OFF_INTENSITY	=  1000;
	const uint8_t	BL_MIN_PWM			=    26;  // 10%
	float			intensity			= MAX_INTENSITY;
	uint8_t			pwm					= 0;
	
	if (adc) {
		intensity = (MAX_INTENSITY >> 1) / adc;
	}
	
	if (intensity < BL_OFF_INTENSITY) {
		pwm = BL_MIN_PWM + (uint8_t)((255 - BL_MIN_PWM) * (intensity / BL_OFF_INTENSITY));
	}
	
	OCR2A = pwm;								// no interrupt lock needed
}


static void s_task(void)
{
	/* TASK when woken up */
	sei();
	uint16_t l_adc_ldr_last = g_adc_ldr_last;
	uint16_t l_adc_ldr      = g_adc_ldr;
	cli();
	
	/* calculate new backlight PWM value and set that */
	if (l_adc_ldr != l_adc_ldr_last) {
		s_task_backlight(l_adc_ldr);
	}
}

void halt(void)
{
	/* MAIN Loop Shutdown */
	runmode = 0;
}


/* MAIN section */

int main (void)
{
	uint8_t retcode = 0;
	
	/* Init of sub-modules */
	sysclk_init();
	ioport_init();
	s_tc_init();
	s_adc_init();

	/* I/O pins go active here */
	board_init();

	reset_cause_t rc = reset_cause_get_causes();
	if (rc & CHIP_RESET_CAUSE_EXTRST ||
		rc & CHIP_RESET_CAUSE_BOD_CPU ||
		rc & CHIP_RESET_CAUSE_POR) {
		s_reset_global_vars();
	} else {
		/* DEBUG */
		s_asm_break();
	}

	/* All interrupt sources prepared here - IRQ activation */
	cpu_irq_enable();
	
	/* Start of sub-modules */
	s_tc_start();								// All clocks and PWM timers start here

	/* Insert application code here, after the board has been initialized. */
    while (runmode) {
	    s_task();
	    //enter_sleep();
    }
    
    cpu_irq_disable();
    //enter_sleep();
    
    return retcode;
}
