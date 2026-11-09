/* SPDX-License-Identifier: MIT */
#ifndef MSP_I2C_ASYNC_H
#define MSP_I2C_ASYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ti/driverlib/dl_i2c.h>

#define MSP_I2C_MAX_TX_BYTES 16U
#define MSP_I2C_QUEUE_SIZE 8U
#define MSP_I2C_DEFAULT_TIMEOUT_MS 20U

typedef enum {
    MSP_I2C_STATUS_IDLE = 0,
    MSP_I2C_STATUS_QUEUED,
    MSP_I2C_STATUS_ACTIVE,
    MSP_I2C_STATUS_OK,
    MSP_I2C_STATUS_ERROR,
    MSP_I2C_STATUS_TIMEOUT
} MspI2cStatus;

typedef struct {
    uint8_t address;
    uint8_t tx[MSP_I2C_MAX_TX_BYTES];
    uint8_t tx_len;
    uint8_t *rx;
    uint8_t rx_len;
    uint16_t timeout_ms;
    bool stop_between_phases;
    volatile MspI2cStatus status;
} MspI2cTransaction;

typedef struct {
    I2C_Regs *instance;
    IRQn_Type irqn;
    uint32_t irq_priority;
} MspI2cConfig;

/* Caller-owned storage; fields are implementation state, not configuration. */
typedef struct {
    MspI2cConfig config;
    MspI2cTransaction *queue[MSP_I2C_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    MspI2cTransaction *volatile active;
    volatile uint8_t hw_state;
    volatile uint8_t tx_count;
    volatile uint8_t rx_count;
    uint32_t start_ms;
} MspI2cBus;

bool msp_i2c_init(MspI2cBus *bus, const MspI2cConfig *config);
bool msp_i2c_setup(MspI2cTransaction *transaction, uint8_t address,
                   const uint8_t *tx, uint8_t tx_len,
                   uint8_t *rx, uint8_t rx_len, uint16_t timeout_ms);
bool msp_i2c_setup_read_reg8(MspI2cTransaction *transaction, uint8_t address,
                             uint8_t reg, uint8_t *rx, uint8_t rx_len,
                             uint16_t timeout_ms, bool split);
bool msp_i2c_setup_write_reg8(MspI2cTransaction *transaction, uint8_t address,
                              uint8_t reg, const uint8_t *data,
                              uint8_t data_len, uint16_t timeout_ms);
bool msp_i2c_submit(MspI2cBus *bus, MspI2cTransaction *transaction);
bool msp_i2c_take_result(MspI2cTransaction *transaction, MspI2cStatus *result);
void msp_i2c_step(MspI2cBus *bus, uint32_t now_ms);
bool msp_i2c_is_idle(const MspI2cBus *bus);
uint8_t msp_i2c_queue_count(const MspI2cBus *bus);
void msp_i2c_irq_handler(MspI2cBus *bus);

#endif
