#
# RasPi3 (BCM2837) SPI Flash storage driver
#
# Copyright (C) 2020-2021, HENSOLDT Cyber GmbH
# SPDX-License-Identifier: BSD-3-Clause
#

cmake_minimum_required(VERSION 3.17)


#-------------------------------------------------------------------------------
# Declare RasPi3 (BCM2837) SPI Flash Driver CAmkES Component
#
# Parameters:
#
#   <name>
#     required, component instance name
#
#   LIBS <lib_1> <lib_2> ...
#     additional libraries. At least a TimeServer client library is required
#
function(RPi_SPI_Flash_DeclareCAmkESComponent name)

    cmake_parse_arguments(
        PARSE_ARGV
        1
        RPi_SPI_Flash
        ""
        ""
        "LIBS"
    )

    DeclareCAmkESComponent(
        ${name}
        SOURCES
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RPi_SPI_Flash.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/3rdParty/bcm2837/bcm2837_gpio.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/3rdParty/bcm2837/bcm2837_spi.c
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/3rdParty/spiflash/spiflash.c
        INCLUDES
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/3rdParty/bcm2837
            ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/3rdParty/spiflash
        C_FLAGS
            -Wall
            -Werror
        LIBS
            os_core_api
            lib_debug
            ${RPi_SPI_Flash_LIBS}
    )

endfunction()
