/*
 * RasPi SPI Flash storage driver
 *
 * Copyright (C) 2020, HENSOLDT Cyber GmbH
 */

#include "OS_Error.h"
#include "LibDebug/Debug.h"
#include "OS_Dataport.h"
#include "TimeServer.h"

#include "bcm2837_spi.h"
#include "spiflash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <camkes.h>

static const if_OS_Timer_t timer =
    IF_OS_TIMER_ASSIGN(
        timeServer_rpc,
        timeServer_notify);

static struct
{
    bool           init_ok;
    spiflash_t     spi_flash_ctx;
    OS_Dataport_t  port_storage;

} ctx =
{
    .port_storage  = OS_DATAPORT_ASSIGN(storage_port),
    .init_ok       = false,
};


//------------------------------------------------------------------------------
static
bool
isValidFlashArea(
    const spiflash_t* spi_flash_ctx,
    size_t const offset,
    size_t const size)
{
    size_t const end = offset + size;
    // Checking integer overflow first. The end index is not part of the area,
    // but we allow offset = end with size = 0 here
    return ( (end >= offset) && (end <= spi_flash_ctx->cfg->sz) );
}


//------------------------------------------------------------------------------
// callback from SPI library
static
__attribute__((__nonnull__))
int
impl_spiflash_spi_txrx(
    spiflash_t* spi,
    const uint8_t* tx_data,
    uint32_t tx_len,
    uint8_t* rx_buffer,
    uint32_t rx_len)
{
    // we can't use the rx_buffer directly, because bcm2837_spi_transfernb()
    // already populates it when sending the tx bytes. The actual data received
    // starts at offset tx_len then. For flash reading we support obtaining
    // 4 KiByte blocks at once and there a 4 byte protocol overhead for the SPI
    // command (1 command byte and 3 address bytes).
    static uint8_t buffer[1 + 3 + 4096];

    const uint32_t len = tx_len + rx_len;
    if (len > sizeof(buffer))
    {
        Debug_LOG_ERROR("tx_len+rx_len %u exceeds buffer size", len);
        return SPIFLASH_ERR_INTERNAL;
    }

    bcm2837_spi_transfernb(
        (void*)tx_data,
        (char*)buffer,
        len);

    if (rx_len > 0)
    {
        memcpy(rx_buffer, &buffer[tx_len], rx_len);
    }

    return SPIFLASH_OK;
}


//------------------------------------------------------------------------------
// callback from SPI library
static
__attribute__((__nonnull__))
void
impl_spiflash_spi_cs(
    spiflash_t* spi,
    uint8_t cs)
{
    bcm2837_spi_chipSelect(cs ? BCM2837_SPI_CS0 : BCM2837_SPI_CS2);
    return;
}


//------------------------------------------------------------------------------
// callback from SPI library
static
__attribute__((__nonnull__))
void
impl_spiflash_wait(
    spiflash_t* spi,
    uint32_t ms)
{
    OS_Error_t err;

    if ((err = TimeServer_sleep(&timer, TimeServer_PRECISION_MSEC,
                                ms)) != OS_SUCCESS)
    {
        Debug_LOG_ERROR("TimeServer_sleep() failed with %d", err);
    }
}


//------------------------------------------------------------------------------
void post_init(void)
{
    Debug_LOG_INFO("BCM2837_SPI_Flash init");

    // initialize BCM2837 SPI library
    if (!bcm2837_spi_begin(regBase))
    {
        Debug_LOG_ERROR("bcm2837_spi_begin() failed");
        return;
    }

    bcm2837_spi_setBitOrder(BCM2837_SPI_BIT_ORDER_MSBFIRST);
    bcm2837_spi_setDataMode(BCM2837_SPI_MODE0);
    // divider 8 gives 50 MHz assuming the RasPi3 is running with the default
    // 400 MHz, but for some reason we force it to run at just 250 MHz with
    // "core_freq=250" in config.txt and thus end up at 31.25 MHz SPI speed.
    bcm2837_spi_setClockDivider(BCM2837_SPI_CLOCK_DIVIDER_8);
    bcm2837_spi_chipSelect(BCM2837_SPI_CS0);
    bcm2837_spi_setChipSelectPolarity(BCM2837_SPI_CS0, 0);

    // setting of the W25Q64 Flash with 8 MiByte storage space
    static const spiflash_config_t spiflash_config =
    {
        .sz = 1024 * 1024 * 8,                  // 8 MiByte flash
        .page_sz = 256,                         // 256 byte pages
        .addr_sz = 3,                           // 3 byte SPI addressing
        .addr_dummy_sz = 0,                     // using single line data, not quad
        .addr_endian = SPIFLASH_ENDIANNESS_BIG, // big endianess on addressing
        .sr_write_ms = 15,                      // write delay (typical 10 ms, max 15 ms)
        .page_program_ms = 3,                   // page programming takes typical 0.8 ms, max 3 ms
        .block_erase_4_ms = 300,                // 4k block erase takes typical 45 ms, max 300 ms
        .block_erase_8_ms = 0,                  // 8k block erase is not supported
        .block_erase_16_ms = 0,                 // 16k block erase is not supported
        .block_erase_32_ms = 800,               // 32k block erase takes typical 120 ms, max 800 ms
        .block_erase_64_ms = 1000,              // 64k block erase takes typical 150 ms, max 1000 ms
        .chip_erase_ms = 6000                   // chip erase takes typical 2 sec, max 6 sec
    };

    // initialize Flash library
    static const spiflash_cmd_tbl_t cmds = SPIFLASH_CMD_TBL_STANDARD;

    static const spiflash_hal_t hal =
    {
        ._spiflash_spi_txrx  = impl_spiflash_spi_txrx,
        ._spiflash_spi_cs    = impl_spiflash_spi_cs,
        ._spiflash_wait      = impl_spiflash_wait,
    };

    SPIFLASH_init(
        &ctx.spi_flash_ctx,
        &spiflash_config,
        &cmds,
        &hal,
        NULL, // asynchronous callback
        SPIFLASH_SYNCHRONOUS,
        NULL); // user data

    if ( (NULL == ctx.spi_flash_ctx.cfg) ||
         (NULL == ctx.spi_flash_ctx.cmd_tbl) ||
         (NULL == ctx.spi_flash_ctx.hal) )
    {
        Debug_LOG_ERROR("SPIFLASH_init() failed");
        return;
    }

    ctx.init_ok = true;

    Debug_LOG_INFO("BCM2837_SPI_Flash done");
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "written"
// never points to NULL.
OS_Error_t
__attribute__((__nonnull__))
storage_rpc_write(
    off_t   offset,
    size_t  size,
    size_t* written)
{
    // set defaults
    *written = 0;

    Debug_LOG_DEBUG(
        "SPI write: offset %" PRIiMAX " (0x%" PRIxMAX "), size %zu (0x%zx)",
        offset, offset, size, size);

    if (!ctx.init_ok)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    size_t dataport_size = OS_Dataport_getSize(ctx.port_storage);
    if (size > dataport_size)
    {
        // the client did a bogus request, it knows the data port size and
        // never ask for more data
        Debug_LOG_ERROR(
            "size %zu exceeds dataport size %zu",
            size,
            dataport_size );

        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!isValidFlashArea(&(ctx.spi_flash_ctx), offset, size))
    {
        Debug_LOG_ERROR(
            "write area at offset %" PRIiMAX " with size %zu out of bounds",
            offset, size);
        return OS_ERROR_OUT_OF_BOUNDS;
    }

    const void* buffer = OS_Dataport_getBuf(ctx.port_storage);

    // max one page (size must be of the form 2^n) can be written at once,
    // worst case is that the buffer starts and end within a page:
    //
    //    Buffer:         |------buffer-----|
    //    Pages:  ...|--------|--------|--------|...

    const size_t page_size = ctx.spi_flash_ctx.cfg->page_sz;
    const size_t page_len_mask = page_size - 1; // work for 2^n values only
    size_t offset_in_page = offset & page_len_mask;

    size_t size_left = size;
    while (size_left > 0)
    {
        const size_t size_in_page = page_size - offset_in_page;
        size_t write_len = (size_in_page < size_left) ? size_in_page : size_left;

        const size_t size_already_written = size - size_left;
        const size_t offs = offset + size_already_written;
        void* buf = (void*)((uintptr_t)buffer + size_already_written);

        int ret = SPIFLASH_write(&(ctx.spi_flash_ctx), offs, write_len, buf);
        if (ret < 0)
        {
            Debug_LOG_ERROR(
                "SPIFLASH_write() failed, offset %zu (0x%zx), size %zu (0x%zx), code %d",
                offs, offs, write_len, write_len, ret);
            return OS_ERROR_GENERIC;
        }

        // we can't write more then what is left to write
        assert( size_left >= write_len);
        size_left -= write_len;

        *written = size_already_written + write_len;

        // move to next page
        offset_in_page = 0;

    } // end while (size_left > 0)

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "read"
// never points to NULL.
OS_Error_t
__attribute__((__nonnull__))
storage_rpc_read(
    off_t   offset,
    size_t  size,
    size_t* read)
{
    // set defaults
    *read = 0;

    Debug_LOG_DEBUG(
        "SPI read: offset %" PRIiMAX " (0x%" PRIxMAX "), size %zu (0x%zx)",
        offset, offset, size, size);

    if (!ctx.init_ok)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    size_t dataport_size = OS_Dataport_getSize(ctx.port_storage);
    if (size > dataport_size)
    {
        // the client did a bogus request, it knows the data port size and
        // never ask for more data
        Debug_LOG_ERROR(
            "size %zu exceeds dataport size %zu",
            size,
            dataport_size );

        return OS_ERROR_INVALID_PARAMETER;
    }

    if (!isValidFlashArea(&(ctx.spi_flash_ctx), offset, size))
    {
        Debug_LOG_ERROR(
            "read area at offset %" PRIiMAX " with size %zu out of bounds",
            offset, size);

        return OS_ERROR_OUT_OF_BOUNDS;
    }

    int ret = SPIFLASH_read(
                  &(ctx.spi_flash_ctx),
                  offset,
                  size,
                  OS_Dataport_getBuf(ctx.port_storage));
    if (ret < 0)
    {
        Debug_LOG_ERROR(
            "SPIFLASH_read() failed, offset %" PRIiMAX " (0x%" PRIxMAX "), "
            "size %zu (0x%zx), code %d",
            offset, offset, size, size, ret);
        return OS_ERROR_GENERIC;
    }

    *read = size;

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "erased"
// never points to NULL.
OS_Error_t
__attribute__((__nonnull__))
storage_rpc_erase(
    off_t  offset,
    off_t  size,
    off_t* erased)
{
    // set defaults
    *erased = 0;

    Debug_LOG_DEBUG(
        "SPI erase: offset %" PRIiMAX " (0x%" PRIxMAX "), "
        "size %" PRIiMAX " (0x%" PRIxMAX ")",
        offset, offset, size, size);

    if (!ctx.init_ok)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    if (!isValidFlashArea(&(ctx.spi_flash_ctx), offset, size))
    {
        Debug_LOG_ERROR(
            "erase area at offset %" PRIiMAX " with size %" PRIiMAX " out of "
            "bounds",
            offset, size);
        return OS_ERROR_OUT_OF_BOUNDS;
    }

    int ret = SPIFLASH_erase(&(ctx.spi_flash_ctx), offset, size);
    if (ret < 0)
    {
        Debug_LOG_ERROR(
            "SPIFLASH_erase() failed, offset %" PRIiMAX " (0x%" PRIxMAX "), "
            "size %" PRIiMAX " (0x%" PRIxMAX "), code %d",
            offset, offset, size, size, ret);
        return OS_ERROR_GENERIC;
    }

    *erased = size;
    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "size"
// never points to NULL.
OS_Error_t
__attribute__((__nonnull__))
storage_rpc_getSize(
    off_t* size)
{
    if (!ctx.init_ok)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    *size = ctx.spi_flash_ctx.cfg->sz;

    return OS_SUCCESS;
}


//------------------------------------------------------------------------------
// This is a CAmkES RPC interface handler. It's guaranteed that "flags"
// never points to NULL.
OS_Error_t
__attribute__((__nonnull__))
storage_rpc_getState(
    uint32_t* flags)
{
    if (!ctx.init_ok)
    {
        Debug_LOG_ERROR("initialization failed, fail call %s()", __func__);
        return OS_ERROR_INVALID_STATE;
    }

    *flags = 0U;
    return OS_SUCCESS;
}
