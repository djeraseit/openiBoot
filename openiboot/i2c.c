#include "openiboot.h"
#include "i2c.h"
#include "hardware/i2c.h"
#include "util.h"
#include "clock.h"
#include "gpio.h"
#include "timer.h"
#include "tasks.h"
#include "interrupt.h"

#if !defined(CONFIG_IPHONE_4) && !defined(CONFIG_IPAD)
static const I2CInfo I2CInit[] = {
			{0, 0, I2CNoError, 0, 0, I2CDone, 0, 0, 0, 0, 0, NULL, 0, NULL, I2C0_SCL_GPIO, I2C0_SDA_GPIO, I2C0 + IICCON, I2C0 + IICSTAT, I2C0 + IICADD, I2C0 + IICDS, I2C0 + IICLC,
				I2C0 + IICREG14, I2C0 + IICREG18, I2C0 + IICREG1C, I2C0 + IICREG20},
			{0, 0, I2CNoError, 0, 0, I2CDone, 0, 0, 0, 0, 0, NULL, 0, NULL, I2C1_SCL_GPIO, I2C1_SDA_GPIO, I2C1 + IICCON, I2C1 + IICSTAT, I2C1 + IICADD, I2C1 + IICDS, I2C1 + IICLC,
				I2C1 + IICREG14, I2C1 + IICREG18, I2C1 + IICREG1C, I2C1 + IICREG20}
		};
static I2CInfo I2C[2];
static void init_i2c(I2CInfo* i2c, FrequencyBase freqBase);
static void do_i2c(I2CInfo* i2c);
#else
static const I2CInfo I2CInit[] = {
			{0, 0, 0, 0, 0, I2CDone, 0, 0, 0, 0x904, 0x903, 0x13, I2C0 + IICREG0, I2C0 + IICREG8, I2C0 + IICREGC, I2C0 + IICREG10, I2C0 + IICREG14, I2C0 + IICREG18, I2C0 + IICREG20, I2C0 + IICREG24, 0x39},
			{0, 0, 0, 0, 0, I2CDone, 0, 0, 0, 0x905, 0x906, 0x14, I2C1 + IICREG0, I2C1 + IICREG8, I2C1 + IICREGC, I2C1 + IICREG10, I2C1 + IICREG14, I2C1 + IICREG18, I2C1 + IICREG20, I2C1 + IICREG24, 0x3A},
			{0, 0, 0, 0, 0, I2CDone, 0, 0, 0, 0xA00, 0x907, 0x15, I2C2 + IICREG0, I2C2 + IICREG8, I2C2 + IICREGC, I2C2 + IICREG10, I2C2 + IICREG14, I2C2 + IICREG18, I2C2 + IICREG20, I2C2 + IICREG24, 0x3B},
		};
static I2CInfo I2C[3];
static void init_i2c(I2CInfo* i2c);
static void i2cIRQHandler(uint32_t token);
#endif

static I2CError i2c_readwrite(I2CInfo* i2c);

int i2c_setup() {
	memcpy(I2C, I2CInit, sizeof(I2CInfo) * (sizeof(I2C) / sizeof(I2CInfo)));

#if !defined(CONFIG_IPHONE_4) && !defined(CONFIG_IPAD)
	clock_gate_switch(I2C0_CLOCKGATE, ON);
	clock_gate_switch(I2C1_CLOCKGATE, ON);

	init_i2c(&I2C[0], FrequencyBasePeripheral);
	init_i2c(&I2C[1], FrequencyBasePeripheral);
#else
	init_i2c(&I2C[0]);
	init_i2c(&I2C[1]);
#endif

	return 0;
}

#if !defined(CONFIG_IPHONE_4) && !defined(CONFIG_IPAD)
I2CError i2c_rx(int bus, int iicaddr, const uint8_t* registers, int num_regs, void* buffer, int len) {
	I2C[bus].address = iicaddr;
	I2C[bus].is_write = FALSE;
	I2C[bus].registers = registers;
	I2C[bus].num_regs = num_regs;
	I2C[bus].bufferLen = len;
	I2C[bus].buffer = (uint8_t*) buffer;
	return i2c_readwrite(&I2C[bus]);
}

I2CError i2c_tx(int bus, int iicaddr, void* buffer, int len) {
	I2C[bus].address = iicaddr;
	I2C[bus].is_write = TRUE;
	I2C[bus].registers = NULL;
	I2C[bus].num_regs = 0;
	I2C[bus].bufferLen = len;
	I2C[bus].buffer = (uint8_t*) buffer;
	return i2c_readwrite(&I2C[bus]);
}

static void init_i2c(I2CInfo* i2c, FrequencyBase freqBase) {
	i2c->frequency = 256000000000ULL / clock_get_frequency(freqBase);
	int divisorRequired = 640000/i2c->frequency;
	int prescaler;
	if(divisorRequired < 512) {
		// round up
		i2c->iiccon_settings = IICCON_INIT | IICCON_TXCLKSRC_FPCLK16;
		prescaler = ((divisorRequired + 0x1F) >> 5) - 1;
	} else {
		i2c->iiccon_settings = IICCON_INIT | IICCON_TXCLKSRC_FPCLK512;
		prescaler = ((divisorRequired + 0x1FF) >> 9) - 1;
	}

	if(prescaler == 0)
		prescaler = 1;

	i2c->iiccon_settings |= prescaler;

	gpio_custom_io(i2c->iic_sda_gpio, 0xE); // pull sda low?

	int i;
	for(i = 0; i < 19; i++) {
		gpio_custom_io(i2c->iic_scl_gpio, ((i % 2) == 1) ? 0xE : 0x0);
		udelay(5);
	}

	gpio_custom_io(i2c->iic_scl_gpio, 0x2);	// generate stop condition?
	gpio_custom_io(i2c->iic_sda_gpio, 0x2);
}

static I2CError i2c_readwrite(I2CInfo* i2c) {
	SET_REG(i2c->register_IICCON, i2c->iiccon_settings);

	i2c->iiccon_settings |= IICCON_ACKGEN;
	i2c->state = I2CStart;
	i2c->operation_result = 0;
	i2c->error_code = I2CNoError;

	SET_REG(i2c->register_20, IICCON_INIT);

	do_i2c(i2c);

	if(i2c->error_code != I2CNoError)
		return i2c->error_code;

	while(TRUE) {
		int hardware_status;
		do {
			hardware_status = GET_REG(i2c->register_20);
			SET_REG(i2c->register_20, hardware_status);
			i2c->operation_result &= ~hardware_status;
		} while(hardware_status != 0);

		if(i2c->state == I2CDone) {
			break;
		}

		do_i2c(i2c);
	}

	return i2c->error_code;
}

static void do_i2c(I2CInfo* i2c) {
	int proceed = FALSE;
	while(i2c->operation_result == 0 || proceed) {
		proceed = FALSE;
		switch(i2c->state) {
			case I2CStart:
				if(i2c->num_regs != 0) {
					SET_REG(i2c->register_IICCON, i2c->iiccon_settings);
					SET_REG(i2c->register_IICDS, i2c->address & ~1);
					i2c->operation_result = OPERATION_SEND;
					i2c->current_iicstat =
						(IICSTAT_MODE_MASTERTX << IICSTAT_MODE_SHIFT)
						| (IICSTAT_STARTSTOPGEN_START << IICSTAT_STARTSTOPGEN_SHIFT)
						| (1 << IICSTAT_DATAOUTPUT_ENABLE_SHIFT);
					SET_REG(i2c->register_IICSTAT, i2c->current_iicstat);
					i2c->cursor = 0;
					i2c->state = I2CSendRegister;
				} else {
					i2c->operation_result = 0;
					i2c->state = I2CSetup;
					proceed = TRUE;
				}
				break;
			case I2CSendRegister:
				if((GET_REG(i2c->register_IICSTAT) & IICSTAT_LASTRECEIVEDBIT) == 0) {
					// ack received
					if(i2c->cursor != i2c->num_regs) {
						// need to send more from the register list
						i2c->operation_result = OPERATION_SEND;
						SET_REG(i2c->register_IICDS, i2c->registers[i2c->cursor++]);
						SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_INTPENDING);
					} else {
						i2c->state = I2CRegistersDone;
						proceed = TRUE;	
					}
				} else {
					// ack not received
					bufferPrintf("i2c: ack not received before register/command %d\r\n", i2c->cursor);
					i2c->error_code = -1;
					i2c->state = I2CFinish;
					proceed = TRUE;
				}
				break;
			case I2CRegistersDone:
				if(i2c->is_write) {
					i2c->state = I2CSetup;
					proceed = TRUE;
				} else {
					i2c->operation_result = OPERATION_CONDITIONCHANGE;
					i2c->current_iicstat = (IICSTAT_MODE_MASTERRX << IICSTAT_MODE_SHIFT) | (IICSTAT_STARTSTOPGEN_MASK << IICSTAT_STARTSTOPGEN_SHIFT);
					SET_REG(i2c->register_IICSTAT, i2c->current_iicstat);
					SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_INTPENDING);
					i2c->state = I2CSetup;
					proceed = TRUE;
				}
				break;
			case I2CSetup:
				SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_ACKGEN);
				i2c->operation_result = OPERATION_SEND;
				if(i2c->is_write) {
					SET_REG(i2c->register_IICDS, i2c->address & ~1);
					i2c->current_iicstat =
						(IICSTAT_MODE_MASTERTX << IICSTAT_MODE_SHIFT)
						| (IICSTAT_STARTSTOPGEN_START << IICSTAT_STARTSTOPGEN_SHIFT)
						| (1 << IICSTAT_DATAOUTPUT_ENABLE_SHIFT);
				} else {
					SET_REG(i2c->register_IICDS, i2c->address | 1);
					i2c->current_iicstat =
						(IICSTAT_MODE_MASTERRX << IICSTAT_MODE_SHIFT)
						| (IICSTAT_STARTSTOPGEN_START << IICSTAT_STARTSTOPGEN_SHIFT)
						| (1 << IICSTAT_DATAOUTPUT_ENABLE_SHIFT);
				}
				SET_REG(i2c->register_IICSTAT, i2c->current_iicstat);
				i2c->cursor = 0;
				if(i2c->is_write) {
					i2c->state = I2CTx;
				} else {
					i2c->state = I2CRxSetup;
				}
				break;
			case I2CTx:
				if((GET_REG(i2c->register_IICSTAT) & IICSTAT_LASTRECEIVEDBIT) == 0) {
					if(i2c->cursor != i2c->bufferLen) {
						// need to send more from the register list
						i2c->operation_result = OPERATION_SEND;
						SET_REG(i2c->register_IICDS, i2c->buffer[i2c->cursor++]);
						SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_INTPENDING | IICCON_ACKGEN);
					} else {
						i2c->state = I2CFinish;
						proceed = TRUE;	
					}
				} else {
					// ack not received
					bufferPrintf("i2c: ack not received before byte %d\r\n", i2c->cursor);
					i2c->error_code = -1;
					i2c->state = I2CFinish;
					proceed = TRUE;
				}
				break;
			case I2CRx:
				i2c->buffer[i2c->cursor++] = GET_REG(i2c->register_IICDS);
				// fall into I2CRxSetup
			case I2CRxSetup:
				if(i2c->cursor != 0 || (GET_REG(i2c->register_IICSTAT) & IICSTAT_LASTRECEIVEDBIT) == 0) {
					if(i2c->cursor != i2c->bufferLen) {
						if((i2c->bufferLen - i2c->cursor) == 1) {
							// last byte
							i2c->iiccon_settings &= ~IICCON_ACKGEN;
						}
						i2c->operation_result = OPERATION_SEND;
						SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_INTPENDING);
						i2c->state = I2CRx;
					} else {
						i2c->state = I2CFinish;
					}
				} else {
					bufferPrintf("i2c: ack not received for rx\r\n"); 
					i2c->error_code = -1;
					i2c->state = I2CFinish;
					proceed = TRUE;
				}
				break;
			case I2CFinish:
				i2c->operation_result = OPERATION_CONDITIONCHANGE;
				i2c->current_iicstat &= ~(IICSTAT_STARTSTOPGEN_MASK << IICSTAT_STARTSTOPGEN_SHIFT);
				if(i2c->is_write) {
					// turn off the tx bit in the mode
					i2c->current_iicstat &= ~(IICSTAT_MODE_SLAVETX << IICSTAT_MODE_SHIFT);
				}
				SET_REG(i2c->register_IICSTAT, i2c->current_iicstat);
				SET_REG(i2c->register_IICCON, i2c->iiccon_settings | IICCON_INTPENDING);
				i2c->state =I2CDone;
				break;
			case I2CDone:
				// should not occur
				break;
		}
	}
}
#else
static void init_i2c(I2CInfo* i2c) {
	clock_gate_switch(i2c->clockgate, ON);

	gpio_set(i2c->iic_sda_gpio, 2); // pull sda low?

	int i;
	for (i = 0; i < 19; i++) {
		gpio_set(i2c->iic_scl_gpio, (i % 2) ? 2 : 0);
		udelay(5);
	}

	gpio_set(i2c->iic_scl_gpio, 5); // generate stop condition?
	gpio_set(i2c->iic_sda_gpio, 5);

	SET_REG(i2c->register_8, 0x30);
	SET_REG(i2c->register_C, 0x37);

	i2c->operation_result = 0;

	interrupt_install(i2c->interrupt, i2cIRQHandler, (uint32_t)i2c);
	interrupt_enable(i2c->interrupt);
}

static void i2cIRQHandler(uint32_t token) {
	uint32_t result;
	I2CInfo* i2c = (I2CInfo*) token;

	udelay(1);
	result = GET_REG(i2c->register_C);
	if (!i2c->is_write) {
		uint32_t count;
		count = result & 0x20;
		if (!(result & 0x20)) {
			while (count < i2c->bufferLen) {
				SET_REG(i2c->buffer + count++, GET_REG(i2c->register_20));
			}
		}
	}
	SET_REG(i2c->register_C, result);
	i2c->operation_result = result;
}

I2CError i2c_tx(int bus, int iicaddr, void* buffer, int len) {
	I2C[bus].address = iicaddr;
	I2C[bus].is_write = TRUE;
	I2C[bus].registers = ((uint8_t*) buffer);
	I2C[bus].buffer = ((uint8_t*) (buffer))+1;
	I2C[bus].bufferLen = len-1;
	return i2c_readwrite(&I2C[bus]);
}

I2CError i2c_rx(int bus, int iicaddr, const uint8_t* registers, int num_regs, void* buffer, int len) {
	if (num_regs != 1)
		return 1;

	I2C[bus].address = iicaddr;
	I2C[bus].is_write = FALSE;
	I2C[bus].registers = registers;
	I2C[bus].bufferLen = len;
	I2C[bus].buffer = (uint8_t*) buffer;
	return i2c_readwrite(&I2C[bus]);
}


static I2CError i2c_readwrite(I2CInfo* i2c) { 
	if (i2c->bufferLen > 0x80)
		return 1;

	while (i2c->state != I2CDone) {
		task_yield();
	}

	i2c->state = I2CStart;

	SET_REG(i2c->register_0, i2c->address >> 1);
	SET_REG(i2c->register_14, 0);
	SET_REG(i2c->register_10, *i2c->registers);
	SET_REG(i2c->register_18, i2c->bufferLen);

	if (i2c->is_write) {
		int i = 0;
		while (i < i2c->bufferLen) {
			SET_REG(i2c->register_20, *(i2c->buffer + i++));
		}
	}

	SET_REG(i2c->register_24, i2c->is_write | 4);

	while (!i2c->operation_result) {
		task_yield();
	}

	i2c->operation_result = 0;
	i2c->state = I2CDone;

	if(i2c->operation_result & 0x20)
		return 1;

	return I2CNoError;
}
#endif
