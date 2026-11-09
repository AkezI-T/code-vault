/* SPDX-License-Identifier: MIT */
#include "msp_i2c_async.h"


#define MSP_I2C_CONTROLLER_INTERRUPTS \
    (DL_I2C_INTERRUPT_CONTROLLER_ARBITRATION_LOST | \
     DL_I2C_INTERRUPT_CONTROLLER_NACK | \
     DL_I2C_INTERRUPT_CONTROLLER_RXFIFO_TRIGGER | \
     DL_I2C_INTERRUPT_CONTROLLER_RX_DONE | \
     DL_I2C_INTERRUPT_CONTROLLER_TX_DONE | \
     DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER)

enum { I2C_HW_IDLE, I2C_HW_TX, I2C_HW_WAIT_SPLIT_RX, I2C_HW_RX };

static bool is_finished(MspI2cStatus status)
{
    return status == MSP_I2C_STATUS_OK ||
           status == MSP_I2C_STATUS_ERROR ||
           status == MSP_I2C_STATUS_TIMEOUT;
}

static void reset_transfer(MspI2cBus *bus)
{
    DL_I2C_disableInterrupt(bus->config.instance,
        DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    DL_I2C_resetControllerTransfer(bus->config.instance);
    DL_I2C_flushControllerTXFIFO(bus->config.instance);
    DL_I2C_flushControllerRXFIFO(bus->config.instance);
    DL_I2C_clearInterruptStatus(bus->config.instance,
        MSP_I2C_CONTROLLER_INTERRUPTS);
    bus->hw_state = I2C_HW_IDLE;
    bus->tx_count = 0;
    bus->rx_count = 0;
}

static void finish_active(MspI2cBus *bus, MspI2cStatus status)
{
    MspI2cTransaction *transaction = bus->active;
    if (transaction) transaction->status = status;
    bus->active = NULL;
    bus->hw_state = I2C_HW_IDLE;
}

static void drain_rx_fifo(MspI2cBus *bus)
{
    MspI2cTransaction *transaction = bus->active;
    while (!DL_I2C_isControllerRXFIFOEmpty(bus->config.instance)) {
        uint8_t data = DL_I2C_receiveControllerData(bus->config.instance);
        if (transaction && bus->rx_count < transaction->rx_len) {
            transaction->rx[bus->rx_count++] = data;
        }
    }
}

static void start_rx_phase(MspI2cBus *bus, MspI2cTransaction *transaction)
{
    DL_I2C_flushControllerRXFIFO(bus->config.instance);
    DL_I2C_clearInterruptStatus(bus->config.instance,
        DL_I2C_INTERRUPT_CONTROLLER_RXFIFO_TRIGGER |
        DL_I2C_INTERRUPT_CONTROLLER_RX_DONE);
    bus->rx_count = 0U;
    bus->hw_state = I2C_HW_RX;
    DL_I2C_startControllerTransferAdvanced(bus->config.instance,
        transaction->address, DL_I2C_CONTROLLER_DIRECTION_RX,
        transaction->rx_len, DL_I2C_CONTROLLER_START_ENABLE,
        DL_I2C_CONTROLLER_STOP_ENABLE,
        DL_I2C_CONTROLLER_ACK_DISABLE);
}

static void start_next(MspI2cBus *bus, uint32_t now_ms)
{
    MspI2cTransaction *transaction;
    if (bus->active || bus->count == 0U) return;
    if (!(DL_I2C_getControllerStatus(bus->config.instance) &
          DL_I2C_CONTROLLER_STATUS_IDLE)) return;

    transaction = bus->queue[bus->head];
    bus->head = (uint8_t)((bus->head + 1U) % MSP_I2C_QUEUE_SIZE);
    bus->count--;
    bus->active = transaction;

    transaction->status = MSP_I2C_STATUS_ACTIVE;
    /* Clear stale FIFO data before starting a new transaction. */
    DL_I2C_flushControllerTXFIFO(bus->config.instance);
    DL_I2C_flushControllerRXFIFO(bus->config.instance);
    DL_I2C_clearInterruptStatus(bus->config.instance,
        MSP_I2C_CONTROLLER_INTERRUPTS);
    bus->rx_count = 0;
    bus->tx_count = 0;
    bus->start_ms = now_ms;

    if (transaction->tx_len == 0U) {
        bus->hw_state = I2C_HW_RX;
        DL_I2C_startControllerTransferAdvanced(bus->config.instance,
            transaction->address, DL_I2C_CONTROLLER_DIRECTION_RX,
            transaction->rx_len, DL_I2C_CONTROLLER_START_ENABLE,
            DL_I2C_CONTROLLER_STOP_ENABLE,
            DL_I2C_CONTROLLER_ACK_DISABLE);
    } else {
        bus->tx_count = DL_I2C_fillControllerTXFIFO(
            bus->config.instance, transaction->tx, transaction->tx_len);
        if (bus->tx_count < transaction->tx_len) {
            DL_I2C_enableInterrupt(
                bus->config.instance, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
        } else {
            DL_I2C_disableInterrupt(
                bus->config.instance, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
        }
        bus->hw_state = I2C_HW_TX;
        DL_I2C_startControllerTransferAdvanced(bus->config.instance,
            transaction->address, DL_I2C_CONTROLLER_DIRECTION_TX,
            transaction->tx_len, DL_I2C_CONTROLLER_START_ENABLE,
            (transaction->rx_len && !transaction->stop_between_phases) ?
                DL_I2C_CONTROLLER_STOP_DISABLE :
                DL_I2C_CONTROLLER_STOP_ENABLE,
            DL_I2C_CONTROLLER_ACK_DISABLE);
    }
}

bool msp_i2c_init(MspI2cBus *bus, const MspI2cConfig *config)
{
    if (!bus || !config || !config->instance || config->irqn < 0 ||
        config->irq_priority >= (1UL << __NVIC_PRIO_BITS)) return false;
    NVIC_DisableIRQ(config->irqn);
    *bus = (MspI2cBus){0};
    bus->config = *config;
    DL_I2C_setControllerTXFIFOThreshold(config->instance, DL_I2C_TX_FIFO_LEVEL_BYTES_1);
    DL_I2C_setControllerRXFIFOThreshold(config->instance, DL_I2C_RX_FIFO_LEVEL_BYTES_1);
    reset_transfer(bus);
    DL_I2C_enableInterrupt(config->instance, MSP_I2C_CONTROLLER_INTERRUPTS &
        ~DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
    NVIC_ClearPendingIRQ(config->irqn);
    NVIC_SetPriority(config->irqn, config->irq_priority);
    NVIC_EnableIRQ(config->irqn);
    return true;
}

bool msp_i2c_setup(MspI2cTransaction *transaction, uint8_t address,
                   const uint8_t *tx, uint8_t tx_len,
                   uint8_t *rx, uint8_t rx_len, uint16_t timeout_ms)
{
    if (!transaction || address > 0x7FU || (tx_len == 0U && rx_len == 0U) ||
        (tx_len && !tx) || tx_len > MSP_I2C_MAX_TX_BYTES ||
        (rx_len && !rx) ||
        transaction->status != MSP_I2C_STATUS_IDLE) {
        return false;
    }

    transaction->address = address;
    for (uint8_t i = 0; i < tx_len; i++) transaction->tx[i] = tx[i];
    transaction->tx_len = tx_len;
    transaction->rx = rx;
    transaction->rx_len = rx_len;
    transaction->timeout_ms = timeout_ms ? timeout_ms : MSP_I2C_DEFAULT_TIMEOUT_MS;
    transaction->stop_between_phases = false;
    transaction->status = MSP_I2C_STATUS_IDLE;
    return true;
}

bool msp_i2c_setup_read_reg8(MspI2cTransaction *transaction,
                             uint8_t address, uint8_t reg,
                             uint8_t *rx, uint8_t rx_len,
                             uint16_t timeout_ms, bool split)
{
    if (rx_len == 0U || !msp_i2c_setup(transaction, address, &reg, 1U,
        rx, rx_len, timeout_ms)) return false;
    transaction->stop_between_phases = split;
    return true;
}

bool msp_i2c_setup_write_reg8(MspI2cTransaction *transaction,
                            uint8_t address, uint8_t reg,
                            const uint8_t *data, uint8_t data_len,
                            uint16_t timeout_ms)
{
    uint8_t tx[MSP_I2C_MAX_TX_BYTES];
    if (!data || data_len == 0U ||
        data_len >= MSP_I2C_MAX_TX_BYTES) return false;

    tx[0] = reg;
    for (uint8_t i = 0; i < data_len; i++) tx[i + 1U] = data[i];
    return msp_i2c_setup(transaction, address, tx, data_len + 1U,
                         NULL, 0, timeout_ms);
}

bool msp_i2c_submit(MspI2cBus *bus, MspI2cTransaction *transaction)
{
    if (!bus || !transaction || transaction->status != MSP_I2C_STATUS_IDLE ||
        transaction->address > 0x7FU || transaction->timeout_ms == 0U ||
        transaction->tx_len > MSP_I2C_MAX_TX_BYTES ||
        (transaction->tx_len == 0U && transaction->rx_len == 0U) ||
        (transaction->rx_len && !transaction->rx)) return false;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (bus->count >= MSP_I2C_QUEUE_SIZE) {
        __set_PRIMASK(primask);
        return false;
    }
    transaction->status = MSP_I2C_STATUS_QUEUED;
    bus->queue[bus->tail] = transaction;
    bus->tail = (uint8_t)((bus->tail + 1U) % MSP_I2C_QUEUE_SIZE);
    bus->count++;
    __set_PRIMASK(primask);
    return true;
}

bool msp_i2c_take_result(MspI2cTransaction *transaction,
                        MspI2cStatus *result)
{
    MspI2cStatus status;
    if (!transaction) return false;
    status = transaction->status;
    if (!is_finished(status)) return false;
    if (result) *result = status;
    transaction->status = MSP_I2C_STATUS_IDLE;
    return true;
}

void msp_i2c_step(MspI2cBus *bus, uint32_t now_ms)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    MspI2cTransaction *transaction = bus->active;
    if (transaction &&
        (uint32_t)(now_ms - bus->start_ms) >= transaction->timeout_ms) {
        reset_transfer(bus);
        finish_active(bus, MSP_I2C_STATUS_TIMEOUT);
    } else if (transaction && (bus->hw_state == I2C_HW_WAIT_SPLIT_RX) &&
        ((DL_I2C_getControllerStatus(bus->config.instance) &
          DL_I2C_CONTROLLER_STATUS_IDLE) != 0U)) {
        /* A split read starts only after the write STOP has released the bus. */
        start_rx_phase(bus, transaction);
    }

    start_next(bus, now_ms);
    __set_PRIMASK(primask);
}

bool msp_i2c_is_idle(const MspI2cBus *bus)
{
    return bus->active == NULL && bus->count == 0U;
}

uint8_t msp_i2c_queue_count(const MspI2cBus *bus)
{
    return bus->count;
}

void msp_i2c_irq_handler(MspI2cBus *bus)
{
    MspI2cTransaction *transaction = bus->active;
    switch (DL_I2C_getPendingInterrupt(bus->config.instance)) {
    case DL_I2C_IIDX_CONTROLLER_TX_DONE:
        if (!transaction || bus->hw_state != I2C_HW_TX) break;
        DL_I2C_disableInterrupt(
            bus->config.instance, DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
        if (transaction->rx_len == 0U) {
            finish_active(bus, MSP_I2C_STATUS_OK);
        } else if (transaction->stop_between_phases) {
            /* The next application step confirms bus idle before the read phase. */
            bus->hw_state = I2C_HW_WAIT_SPLIT_RX;
        } else {
            start_rx_phase(bus, transaction);
        }
        break;
    case DL_I2C_IIDX_CONTROLLER_RXFIFO_TRIGGER:
        drain_rx_fifo(bus);
        break;
    case DL_I2C_IIDX_CONTROLLER_TXFIFO_TRIGGER:
        if (transaction && bus->hw_state == I2C_HW_TX &&
            bus->tx_count < transaction->tx_len) {
            bus->tx_count += DL_I2C_fillControllerTXFIFO(bus->config.instance,
                &transaction->tx[bus->tx_count], transaction->tx_len - bus->tx_count);
        }
        if (!transaction || bus->tx_count >= transaction->tx_len) {
            DL_I2C_disableInterrupt(bus->config.instance,
                DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER);
        }
        break;
    case DL_I2C_IIDX_CONTROLLER_RX_DONE:
        if (!transaction || bus->hw_state != I2C_HW_RX) break;
        drain_rx_fifo(bus);
        finish_active(bus, transaction && bus->rx_count == transaction->rx_len ?
                      MSP_I2C_STATUS_OK : MSP_I2C_STATUS_ERROR);
        break;
    case DL_I2C_IIDX_CONTROLLER_ARBITRATION_LOST:
    case DL_I2C_IIDX_CONTROLLER_NACK:
        reset_transfer(bus);
        finish_active(bus, MSP_I2C_STATUS_ERROR);
        break;
    default:
        break;
    }
}
